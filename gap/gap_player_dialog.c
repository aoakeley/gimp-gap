/*  gap_player_dialog.c
 *
 *  video (preview) playback of video frames  by Wolfgang Hofer (hof)
 *     supports both (fast) thumbnail based playback
 *     and full image playback (slow)
 *  the current implementation has audio support for RIFF WAV audiofiles
 *  but requires the wavplay executable as external audioserver program.
 *  2003/09/07
 *
 */

/* The GIMP -- an image manipulation program
 * Copyright (C) 1995 Spencer Kimball and Peter Mattis
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Revision history
 *  (2003/11/01)  v1.3.21d   hof: cleanup messages
 *  (2003/10/14)  v1.3.20d   hof: sourcecode cleanup
 *  (2003/10/06)  v1.3.20d   hof: bugfix: changed shell_window resize handling
 *  (2003/09/29)  v1.3.20c   hof: moved gap_arr_overwrite_file_dialog to module gap_arr_dialog.c
 *  (2003/09/23)  v1.3.20b   hof: use GAPLIBDIR to locate audioconvert_to_wav.sh
 *  (2003/09/14)  v1.3.20a   hof: bugfix: added p_create_wav_dialog 
 *                                now can create and resample WAVFILE from other audiofiles (MP3 and others)
 *                                based on external shellscript (using SOX and LAME to do that job)
 *                                Replaced direct wavplay Procedure calls
 *                                by abstracted AudioPlayerClient (APCL_) Procedures
 *  (2003/09/09)  v1.3.19b   hof: bugfix: on_framenr_spinbutton_changed must resync audio to the new Position when playing
 *                                bugfix: Selection of a new audiofile did continue play the old one
 *  (2003/09/07)  v1.3.19a   hof: audiosupport (based on wavplay, for UNIX only),
 *                                audiosupport is on by default, and can be disabled by defining
 *                                   GAP_DISABLE_WAV_AUDIOSUPPORT
 *  (2003/08/27)  v1.3.18b   hof: added ctrl/alt modifiers on_go_button_clicked,
 *                                added p_printout_range (for STORYBOARD FILE Processing
 *                                in the still unpublished GAP Videoencoder Project)
 *  (2003/07/31)  v1.3.17b   hof: message text fixes for translators (# 118392)
 *  (2003/06/26)  v1.3.16a   hof: bugfix: make preview drawing_area fit into frame (use an aspect_frame)
 *                                query gimprc for "show-tool-tips"
 *  (2003/06/21)  v1.3.15a   hof: created
 */

/* undefining GAP_ENABLE_AUDIO_SUPPORT will disable all audio stuff
 *  at compiletime
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <libgimp/gimp.h>
#include <libgimp/gimpui.h>

#include "gap_player_main.h"
#include "gap_player_dialog.h"
#include "gap_pdb_calls.h"
#include "gap_pview_da.h"
#include "gap_stock.h"
#include "gap_lib.h"
#include "gap_image.h"
#include "gap_vin.h"
#include "gap_timeconv.h"
#include "gap_thumbnail.h"
#include "gap_arr_dialog.h"


#include "gap-intl.h"

extern int gap_debug;  /* 1 == print debug infos , 0 dont print debug infos */
int cmdopt_x = 0;				/* Debug option flag */

#ifdef GAP_ENABLE_AUDIO_SUPPORT

#include "wpc_lib.h"   /* headerfile for libwavplayclient (preferred) */

char *env_WAVPLAYPATH = WAVPLAYPATH;		/* Default pathname of executable /usr/local/bin/wavplay */
char *env_AUDIODEV = AUDIODEV;			/* Default compiled in audio device */
unsigned long env_AUDIOLCK = AUDIOLCK;		/* Default compiled in locking semaphore */

#else

char *env_WAVPLAYPATH = NULL;			/* Default pathname of executable /usr/local/bin/wavplay */
char *env_AUDIODEV = NULL;			/* Default compiled in audio device */
unsigned long env_AUDIOLCK = 0;			/* Default compiled in locking semaphore */
#endif

#define GAP_PLAYER_MIN_SIZE 64
#define GAP_PLAYER_MAX_SIZE 800
#define GAP_STANDARD_PREVIEW_SIZE 256
#define GAP_SMALL_PREVIEW_SIZE 128

#define GAP_PLAYER_CHECK_SIZE 6
#define GAP_PLAY_MAX_GOBUTTONS 51
#define GAP_PLAYER_MIDDLE_GO_NUMBER ((GAP_PLAY_MAX_GOBUTTONS / 2))

typedef struct t_gobutton
{
  GapPlayerMainGlobalParams *gpp;
  gint             go_number;
} t_gobutton;


/* the callbacks */
static void   on_shell_window_destroy                (GtkObject       *object,
                                                      gpointer         user_data);

static void   on_from_spinbutton_changed             (GtkEditable     *editable,
                                                     gpointer         user_data);

static void   on_to_spinbutton_changed               (GtkEditable     *editable,
                                                       gpointer         user_data);



static gboolean on_vid_preview_button_press_event    (GtkWidget       *widget,
                                                      GdkEventButton  *bevent,
                                                      gpointer         user_data);
static gboolean on_vid_preview_expose_event          (GtkWidget       *widget,
                                                      GdkEventExpose  *eevent,
                                                      gpointer         user_data);
static void   on_vid_preview_size_allocate           (GtkWidget       *widget,
                                                      GtkAllocation   *allocation,
                                                      gpointer         user_data);
static void   on_shell_window_size_allocate           (GtkWidget       *widget,
                                                      GtkAllocation   *allocation,
                                                      gpointer         user_data);

static void   on_framenr_button_clicked              (GtkButton       *button,
                                                       gpointer         user_data);

static void   on_framenr_spinbutton_changed          (GtkEditable     *editable,
                                                      gpointer         user_data);


static void   on_origspeed_button_clicked            (GtkButton       *button,
                                                      gpointer         user_data);
static void   on_speed_spinbutton_changed            (GtkEditable     *editable,
                                                      gpointer         user_data);
static void   p_fit_initial_shell_window             (GapPlayerMainGlobalParams *gpp);
static void   p_fit_shell_window                     (GapPlayerMainGlobalParams *gpp);


static gboolean   on_size_button_button_press_event  (GtkWidget       *widget,
                                                      GdkEventButton  *event,
                                                      gpointer         user_data);



static void   on_size_spinbutton_changed             (GtkEditable     *editable,
                                                      gpointer         user_data);
static gboolean   on_size_spinbutton_enter           (GtkWidget        *widget,
                                                      GdkEvent         *event,
                                                      gpointer         user_data);
static gboolean   on_shell_window_leave              (GtkWidget        *widget,
                                                      GdkEvent         *event,
                                                      gpointer         user_data);


static void   on_exact_timing_checkbutton_toggled    (GtkToggleButton *togglebutton,
                                                      gpointer         user_data);

static void   on_use_thumb_checkbutton_toggled       (GtkToggleButton *togglebutton,
                                                      gpointer         user_data);

static void   on_pinpong_checkbutton_toggled         (GtkToggleButton *togglebutton,
                                                      gpointer         user_data);

static void   on_selonly_checkbutton_toggled         (GtkToggleButton *togglebutton,
                                                      gpointer         user_data);

static void   on_loop_checkbutton_toggled            (GtkToggleButton *togglebutton,
                                                      gpointer         user_data);

static void   on_play_button_clicked                (GtkButton       *button,
                                                     gpointer         user_data);

static gboolean on_pause_button_press_event         (GtkButton       *button,
                                                     GdkEventButton  *bevent,
                                                     gpointer         user_data);

static void   on_back_button_clicked                (GtkButton       *button,
                                                     gpointer         user_data);

static void   on_close_button_clicked               (GtkButton       *button,
                                                     gpointer         user_data);


static void   on_timer_playback                      (gpointer   user_data);

static void   on_timer_go_job                   (gpointer   user_data);

static void   on_go_button_clicked                   (GtkButton       *button,
                                                      GdkEventButton  *bevent,
                                                      gpointer         user_data);

static gboolean   on_go_button_enter                 (GtkButton       *button,
                                                      GdkEvent        *event,
                                                      gpointer         user_data);

static void   on_gobutton_hbox_leave                 (GtkWidget        *widget,
                                                      GdkEvent         *event,
                                                      gpointer         user_data);

static void   on_audio_enable_checkbutton_toggled (GtkToggleButton *togglebutton,
                                                   gpointer         user_data);
static void   on_audio_volume_spinbutton_changed  (GtkEditable     *editable,
                                                   gpointer         user_data);
static void   on_audio_frame_offset_spinbutton_changed (GtkEditable     *editable,
                                                      gpointer         user_data);
static void   on_audio_reset_button_clicked       (GtkButton       *button,
                                                   gpointer         user_data);
static void   on_audio_create_copy_button_clicked (GtkButton       *button,
                                                   gpointer         user_data);
static void   on_audio_filesel_button_clicked      (GtkButton       *button,
                                                   gpointer         user_data);
static void   on_audio_filename_entry_changed     (GtkWidget     *widget,
                                                   gpointer         user_data);

static void    p_update_position_widgets(GapPlayerMainGlobalParams *gpp);
static void    p_stop_playback(GapPlayerMainGlobalParams *gpp);
static void    p_connect_resize_handler(GapPlayerMainGlobalParams *gpp);
static void    p_disconnect_resize_handler(GapPlayerMainGlobalParams *gpp);


/* -----------------------------
 * p_check_tooltips
 * -----------------------------
 */
static void
p_check_tooltips(void)
{
  char *value_string;
  
  value_string = gimp_gimprc_query("show-tool-tips");
  
  if(value_string != NULL)
  {
    if (strcmp(value_string, "no") == 0)
    {
       gimp_help_disable_tooltips ();
    }
    else
    {
       gimp_help_enable_tooltips ();
    }
  }
  else
  {
       gimp_help_enable_tooltips ();
  }
  
}  /* end p_check_tooltips */


/* -----------------------------
 * p_audio_errfunc
 * -----------------------------
 */
static void
p_audio_errfunc(const char *format,va_list ap)
{
  char buf[1024];

  vsnprintf(buf,sizeof(buf),format,ap);	/* Format the message */
  g_message(_("Problem with audioplayback. The audiolib reported:\n%s"),buf);

}  /* end p_audio_errfunc */

         
/* -----------------------------
 * p_create_wav_dialog
 * -----------------------------
 * return TRUE : OK, caller can create the wavefile
 *        FALSE: user has cancelled, dont create wavefile
 */
static gboolean
p_create_wav_dialog(GapPlayerMainGlobalParams *gpp)
{
  static GapArrArg  argv[4];
  gint   l_ii;
  gint   l_ii_resample;
  gint   l_ii_samplerate;

  l_ii = 0;
  gap_arr_arg_init(&argv[l_ii], GAP_ARR_WGT_LABEL_LEFT);
  argv[l_ii].label_txt = _("Audiosource:");
  argv[l_ii].text_buf_ret = gpp->audio_filename;

  g_snprintf(gpp->audio_wavfile_tmp
            ,sizeof(gpp->audio_wavfile_tmp)
            ,"%s.tmp.wav"
            ,gpp->audio_filename
            );

  l_ii++;
  gap_arr_arg_init(&argv[l_ii], GAP_ARR_WGT_FILESEL);
  argv[l_ii].label_txt = _("Wavefile:");
  argv[l_ii].entry_width = 400;
  argv[l_ii].help_txt  = _("Name of wavefile to create as copy in RIFF WAVE format");
  argv[l_ii].text_buf_len = sizeof(gpp->audio_wavfile_tmp);
  argv[l_ii].text_buf_ret = gpp->audio_wavfile_tmp;
  
  l_ii++;
  l_ii_resample = l_ii;
  gap_arr_arg_init(&argv[l_ii], GAP_ARR_WGT_TOGGLE);
  argv[l_ii].label_txt = _("Resample:");
  argv[l_ii].help_txt  = _("ON: Resample the copy at specified samplerate.\n"
                           "OFF: Use original samplerate");
  argv[l_ii].int_ret   = 1;

  l_ii++;
  l_ii_samplerate = l_ii;
  gap_arr_arg_init(&argv[l_ii], GAP_ARR_WGT_INT_PAIR);
  argv[l_ii].constraint = TRUE;
  argv[l_ii].label_txt = _("Samplerate:");
  argv[l_ii].help_txt  = _("Target audio samperate in samples/sec. Ignored if resample is off)");
  argv[l_ii].int_min   = (gint)GAP_PLAYER_MAIN_MIN_SAMPLERATE;
  argv[l_ii].int_max   = (gint)GAP_PLAYER_MAIN_MAX_SAMPLERATE;
  argv[l_ii].int_ret   = (gint)22050;

  if(gpp->audio_samples > 0)
  {
    /* the original is a valid wavefile
     * in that case we know the samplerate of the original audiofile
     * and can limit the samplerate of the copy to this size
     * (resample with higher rates does not improve quality and is a waste of memory.
     *  Making a copy of the input wavfile should use a lower samplerate
     *  that makes it possible for the audioserver to follow fast videoplayback
     *  by switching from the original to the copy)
     */
    argv[l_ii].int_min   = (gint)GAP_PLAYER_MAIN_MIN_SAMPLERATE;
    argv[l_ii].int_max   = (gint)MIN(gpp->audio_samplerate, GAP_PLAYER_MAIN_MAX_SAMPLERATE);
    argv[l_ii].int_ret   = (gint)MIN((gpp->audio_samplerate / 2), GAP_PLAYER_MAIN_MAX_SAMPLERATE);
  }


  if(TRUE == gap_arr_ok_cancel_dialog( _("Copy Audiofile as Wavefile"),
                                 _("Settings"), 
                                  G_N_ELEMENTS(argv), argv))
  {
     gpp->audio_tmp_resample   = (gboolean)(argv[l_ii_resample].int_ret);
     gpp->audio_tmp_samplerate = (gint32)(argv[l_ii_samplerate].int_ret);
    
     return (gap_arr_overwrite_file_dialog(gpp->audio_wavfile_tmp));
  }

  return (FALSE);
}  /* end p_create_wav_dialog */




/* -----------------------------
 * p_audio_shut_server
 * -----------------------------
 */
static void
p_audio_shut_server(GapPlayerMainGlobalParams *gpp)
{
#ifdef GAP_ENABLE_AUDIO_SUPPORT
  /* if (gap_debug) printf("p_audio_shut_server\n"); */
  if(gpp->audio_status > GAP_PLAYER_MAIN_AUSTAT_NONE)
  {
    apcl_bye(0, p_audio_errfunc);
  }
  gpp->audio_status = GAP_PLAYER_MAIN_AUSTAT_NONE;
#endif
  return; 
}  /* end p_audio_shut_server */


/* -----------------------------
 * p_audio_resync
 * -----------------------------
 */
static void
p_audio_resync(GapPlayerMainGlobalParams *gpp)
{
#ifdef GAP_ENABLE_AUDIO_SUPPORT
  if(gpp->audio_resync < 1)
  {
    gpp->audio_resync = 1 + (gpp->speed / 5);
  }
  /* if (gap_debug) printf("p_audio_resync :%d\n", (int)gpp->audio_resync); */
#endif
  return; 
}  /* end p_audio_resync */

/* -----------------------------
 * p_audio_stop
 * -----------------------------
 */
static void
p_audio_stop(GapPlayerMainGlobalParams *gpp)
{
#ifdef GAP_ENABLE_AUDIO_SUPPORT
  /* if (gap_debug) printf("p_audio_stop\n"); */
  if(gpp->audio_status > GAP_PLAYER_MAIN_AUSTAT_NONE)
  {
    apcl_stop(0,p_audio_errfunc);  /* Tell the server to stop */
    gpp->audio_status = MIN(gpp->audio_status, GAP_PLAYER_MAIN_AUSTAT_FILENAME_SET);
  }
#endif
  return; 
}  /* end p_audio_stop */

/* -----------------------------
 * p_audio_init
 * -----------------------------
 */
static void
p_audio_init(GapPlayerMainGlobalParams *gpp)
{
#ifdef GAP_ENABLE_AUDIO_SUPPORT
  /* if (gap_debug) printf("p_audio_init\n"); */
  if(gpp->audio_samples > 0)       /* audiofile has samples (seems to be a valid audiofile) */
  {
    if(gpp->audio_status <= GAP_PLAYER_MAIN_AUSTAT_NONE)
    {
      if ( apcl_start(p_audio_errfunc) < 0 )
      {
	 g_message(_("Failure to start the wavplay server is fatal.\n"
	        "Please check the executability of the 'wavplay' command.\n"
		"If you have installed the wavplay executeable somewhere\n"
		"you can set the Environmentvariable WAVPLAYPATH before gimp startup\n"));
      }
      else
      {
        gpp->audio_status = GAP_PLAYER_MAIN_AUSTAT_SERVER_STARTED;
      }
      /* apcl_semreset(0,p_audio_errfunc); */  /* Tell server to reset semaphores */
    }
    else
    {
      p_audio_stop(gpp);
    }
    
    if(gpp->audio_status < GAP_PLAYER_MAIN_AUSTAT_FILENAME_SET)
    {
      apcl_path(gpp->audio_filename,0,p_audio_errfunc);	 /* Tell server the new path */
      gpp->audio_status = GAP_PLAYER_MAIN_AUSTAT_FILENAME_SET;
    }
  }
#endif
  return; 
}  /* end p_audio_init */


/* -----------------------------
 * p_audio_print_labels
 * -----------------------------
 */
static void
p_audio_print_labels(GapPlayerMainGlobalParams *gpp)
{
  char  txt_buf[100];
  gint  len;
  gint32 l_samplerate;
  gint32 l_samples;
  gint32 l_videoframes;
  
  l_samples = gpp->audio_samples;
  l_samplerate = gpp->audio_samplerate;
  
  if (gap_debug)
  {
    printf("p_audio_print_labels\n");
    printf("audio_filename: %s\n", gpp->audio_filename);
    printf("audio_enable: %d\n", (int)gpp->audio_enable);
    printf("audio_frame_offset: %d\n", (int)gpp->audio_frame_offset);
    printf("audio_bits: %d\n", (int)gpp->audio_bits);
    printf("audio_channels: %d\n", (int)gpp->audio_channels);
    printf("audio_samplerate: %d\n", (int)gpp->audio_samplerate);
    printf("audio_samples: %d\n", (int)gpp->audio_samples);
    printf("audio_status: %d\n", (int)gpp->audio_status);
  }
  if(gpp->audio_frame_offset < 0)
  {
    gap_timeconv_framenr_to_timestr( (gint32)(0 - gpp->audio_frame_offset)
                           , (gdouble)gpp->original_speed
                           , txt_buf
                           , sizeof(txt_buf)
                           );
    len = strlen(txt_buf);
    g_snprintf(&txt_buf[len], sizeof(txt_buf)-len, " %s", _("Audio Delay"));
  }
  else
  {
    gap_timeconv_framenr_to_timestr( (gint32)(gpp->audio_frame_offset)
                           , (gdouble)gpp->original_speed
                           , txt_buf
                           , sizeof(txt_buf)
                           );
    len = strlen(txt_buf);			   
    if(gpp->audio_frame_offset == 0)
    {
      g_snprintf(&txt_buf[len], sizeof(txt_buf)-len, " %s", _("Syncron"));
    }
    else
    {
      g_snprintf(&txt_buf[len], sizeof(txt_buf)-len, " %s", _("Audio Skipped"));
    }
  }			   
  gtk_label_set_text ( GTK_LABEL(gpp->audio_offset_time_label), txt_buf);

  gap_timeconv_samples_to_timestr( l_samples
                           , (gdouble)l_samplerate
                           , txt_buf
                           , sizeof(txt_buf)
                           );
  gtk_label_set_text ( GTK_LABEL(gpp->audio_total_time_label), txt_buf);

  g_snprintf(txt_buf, sizeof(txt_buf), _("%d (at %.4f frames/sec)")
             , (int)gap_timeconv_samples_to_frames(l_samples
	                                    ,(gdouble)l_samplerate
					    ,(gdouble)gpp->original_speed    /* framerate */
					    )
	     , (float)gpp->original_speed		    
	     );
  gtk_label_set_text ( GTK_LABEL(gpp->audio_total_frames_label), txt_buf);

  g_snprintf(txt_buf, sizeof(txt_buf), "%d", (int)l_samples );
  gtk_label_set_text ( GTK_LABEL(gpp->audio_samples_label), txt_buf);

  g_snprintf(txt_buf, sizeof(txt_buf), "%d", (int)l_samplerate );
  gtk_label_set_text ( GTK_LABEL(gpp->audio_samplerate_label), txt_buf);

  g_snprintf(txt_buf, sizeof(txt_buf), "%d", (int)gpp->audio_bits );
  gtk_label_set_text ( GTK_LABEL(gpp->audio_bits_label), txt_buf);

  g_snprintf(txt_buf, sizeof(txt_buf), "%d", (int)gpp->audio_channels );
  gtk_label_set_text ( GTK_LABEL(gpp->audio_channels_label), txt_buf);

  l_videoframes = 0;
  if(gpp->ainfo_ptr)
  {
    l_videoframes = 1+ (gpp->ainfo_ptr->last_frame_nr - gpp->ainfo_ptr->first_frame_nr);
  }
  gap_timeconv_framenr_to_timestr( l_videoframes
                         , gpp->original_speed
                         , txt_buf
                         , sizeof(txt_buf)
                         );
  gtk_label_set_text ( GTK_LABEL(gpp->video_total_time_label), txt_buf);

  g_snprintf(txt_buf, sizeof(txt_buf), "%d", (int)l_videoframes);
  gtk_label_set_text ( GTK_LABEL(gpp->video_total_frames_label), txt_buf);

}  /* end p_audio_print_labels */


/* -----------------------------
 * p_print_and_clear_audiolabels
 * -----------------------------
 */
static void
p_print_and_clear_audiolabels(GapPlayerMainGlobalParams *gpp)
{
  gpp->audio_samplerate = 0;
  gpp->audio_bits       = 0;
  gpp->audio_channels   = 0;
  gpp->audio_samples    = 0;
  p_audio_print_labels(gpp);
}  /* end p_print_and_clear_audiolabels */

/* -----------------------------
 * p_audio_filename_changed
 * -----------------------------
 * check if audiofile is a valid wavefile,
 * and tell audioserver to prepare if its OK 
 * (but dont start to play, just keep audioserver stand by)
 */
static void
p_audio_filename_changed(GapPlayerMainGlobalParams *gpp)
{
#ifdef GAP_ENABLE_AUDIO_SUPPORT
  int fd;
  int rc;
  int channels;				/* Channels recorded in this wav file */
  u_long samplerate;			/* Sampling rate */
  int sample_bits;			/* data bit size (8/12/16) */
  u_long samples;			/* The number of samples in this file */
  u_long datastart;			/* The offset to the wav data */
  
  if (gap_debug) printf("p_audio_filename_changed to:%s:\n", gpp->audio_filename);
  p_audio_stop(gpp);
  gpp->audio_status = MIN(gpp->audio_status, GAP_PLAYER_MAIN_AUSTAT_SERVER_STARTED);

  /* Open the file for reading: */
  if ( (fd = open(gpp->audio_filename,O_RDONLY)) < 0 ) 
  {
     p_print_and_clear_audiolabels(gpp);      
     return;
  }

  rc = WaveReadHeader(fd
                   ,&channels
		   ,&samplerate
		   ,&sample_bits
		   ,&samples
		   ,&datastart
		   ,p_audio_errfunc);
  close(fd);
  		   
  if(rc != 0)
  {
     g_message (_("Error at reading WAV header from file '%s'")
               ,  gpp->audio_filename);
     p_print_and_clear_audiolabels(gpp);      
     return;
  }
  
  gpp->audio_samplerate = samplerate;
  gpp->audio_bits       = sample_bits;
  gpp->audio_channels   = channels;
  gpp->audio_samples    = samples;

  p_audio_print_labels(gpp);
  p_audio_init(gpp);  /* tell ausioserver to go standby for this audiofile */
  
#endif
  return; 
}  /* end p_audio_filename_changed */



/* -----------------------------
 * p_audio_start_play
 * -----------------------------
 * conditional start audioplayback
 * note: if already playing this procedure does noting.
 *       (first call p_audio_stop to stop the old playing audiofile
 *        before you start another one)
 */
static void
p_audio_start_play(GapPlayerMainGlobalParams *gpp)
{
#ifdef GAP_ENABLE_AUDIO_SUPPORT
  gdouble offset_start_sec;
  gint32  offset_start_samples;
  gdouble flt_samplerate;
  gint32 l_samplerate;
  gint32 l_samples;

 
  
  /* if (gap_debug) printf("p_audio_start_play\n"); */
  
  /* Never play audio when disabled, or video play backwards,
   * (reverse audio is not supported by the wavplay server)
   */
  if(!gpp->audio_enable)                        { return; }
  if(gpp->audio_status >= GAP_PLAYER_MAIN_AUSTAT_PLAYING)       { return; }
  if(gpp->play_backward)                        { return; }
  if(gpp->ainfo_ptr == NULL)                    { return; }
  
  l_samples = gpp->audio_samples;
  l_samplerate = gpp->audio_samplerate;
  
  offset_start_sec = ( gpp->play_current_framenr
                     - gpp->ainfo_ptr->first_frame_nr
		     + gpp->audio_frame_offset
   		     ) / MAX(1, gpp->original_speed);

  offset_start_samples = offset_start_sec * l_samplerate;
  if(gpp->original_speed > 0)
  {
    /* use moidfied samplerate if video is not played at original speed
     */
    flt_samplerate = l_samplerate * gpp->speed / gpp->original_speed;
  }
  else
  {
    flt_samplerate = l_samplerate;
  }
  
  /* check if offset and rate is within playable limits */
  if((offset_start_samples >= 0)
  && ((gdouble)offset_start_samples < ((gdouble)l_samples - 1024.0))
  && (flt_samplerate >= GAP_PLAYER_MAIN_MIN_SAMPLERATE)
  )
  {
    UInt32  lu_samplerate;
    
    p_audio_init(gpp);  /* tell ausioserver to go standby for this audiofile */
    gpp->audio_required_samplerate = (guint32)flt_samplerate;
    if(flt_samplerate > GAP_PLAYER_MAIN_MAX_SAMPLERATE)
    {
      lu_samplerate = (UInt32)GAP_PLAYER_MAIN_MAX_SAMPLERATE;
      /* required samplerate is faster than highest possible audioplayback speed
       * (the audioplayback will be played but runs out of sync and cant follow)
       */
    }
    else
    {
      lu_samplerate = (UInt32)flt_samplerate;
    }
    apcl_sampling_rate(lu_samplerate,0,p_audio_errfunc);
    apcl_start_sample((UInt32)offset_start_samples,0,p_audio_errfunc);
    apcl_play(0,p_audio_errfunc);  /* Tell server to play */
    apcl_volume(gpp->audio_volume, 0, p_audio_errfunc);
   
    gpp->audio_status = GAP_PLAYER_MAIN_AUSTAT_PLAYING;
    gpp->audio_resync = 0;
  }
#endif
  return; 
}  /* end p_audio_start_play */


/* -----------------------------
 * p_audio_startup_server
 * -----------------------------
 */
static void
p_audio_startup_server(GapPlayerMainGlobalParams *gpp)
{
#ifdef GAP_ENABLE_AUDIO_SUPPORT
  const char *cp;
  gboolean wavplay_server_found;
  
  if (gap_debug) printf("p_audio_startup_server\n");
  
  wavplay_server_found = FALSE;

  /*
   * Set environmental values for the wavplay audio server:
   */
  
  /* check gimprc for the wavplay audio server: */
  if ( (cp = gimp_gimprc_query("wavplaypath")) != NULL )
  {
    env_WAVPLAYPATH = g_strdup(cp);		/* Environment overrides compiled in default for WAVPLAYPATH */
    if(g_file_test (env_WAVPLAYPATH, G_FILE_TEST_IS_EXECUTABLE) )
    {
      wavplay_server_found = TRUE;
    }
    else
    {
      g_message(_("WARNING: your gimprc file configuration for the wavplay audio server\n"
             "does not point to an executable program\n"
	     "the configured value for %s is: %s\n")
	     , "wavplaypath"
	     , env_WAVPLAYPATH
	     );
    }
  }
  
  /* check environment variable for the wavplay audio server: */
  if(!wavplay_server_found)
  {
    if ( (cp = g_getenv("WAVPLAYPATH")) != NULL )
    {
      env_WAVPLAYPATH = g_strdup(cp);		/* Environment overrides compiled in default for WAVPLAYPATH */
      if(g_file_test (env_WAVPLAYPATH, G_FILE_TEST_IS_EXECUTABLE) )
      {
	wavplay_server_found = TRUE;
      }
      else
      {
	g_message(_("WARNING: the environment variable %s\n"
               "does not point to an executable program\n"
	       "the current value is: %s\n")
	       , "WAVPLAYPATH"
	       , env_WAVPLAYPATH
	       );
      }
    }
  }
  
  if(!wavplay_server_found)
  {
    if ( (cp = g_getenv("PATH")) != NULL )
    {
      env_WAVPLAYPATH = gap_lib_searchpath_for_exefile("wavplay", cp);
      if(env_WAVPLAYPATH)
      {
        wavplay_server_found = TRUE;
      }
    }
  }

  if ( (cp = g_getenv("AUDIODEV")) != NULL )
	  env_AUDIODEV = g_strdup(cp);		/* Environment overrides compiled in default for AUDIODEV */

  if ( (cp = g_getenv("AUDIOLCK")) == NULL || sscanf(cp,"%lX",&env_AUDIOLCK) != 1 )
	  env_AUDIOLCK = AUDIOLCK;	/* Use compiled in default, if no environment, or its bad */

  if(wavplay_server_found)
  {
    p_audio_filename_changed(gpp);
    gpp->audio_enable = TRUE;
  }
  else
  {
    gpp->audio_enable = FALSE;
    g_message(_("No audiosupport available\n"
                 "the audioserver executable file '%s' was not found.\n"
                 "If you have installed '%s'\n"
		 "you should add the installation dir to your PATH\n"
		 "or set environment variable %s to the name of the executable\n"
		 "before you start GIMP")
		 , "wavplay"
		 , "wavplay 1.4"
		 , "WAVPLAYPATH"
		 );
  }
#endif
  return; 
}  /* end p_audio_startup_server */


/* -----------------------------
 * p_printout_range
 * -----------------------------
 * print selected RANGE to stdout
 * (Line Formated for STORYBOARD_FILE processing)
 */
static void 
p_printout_range(GapPlayerMainGlobalParams *gpp)
{
  if(gpp->ainfo_ptr == NULL) { return; }
  if(gpp->ainfo_ptr->basename == NULL) { return; }

  /* Storyboard command line for playback of GAP singleframe range */
  printf("VID_PLAY_FRAMES     1=track  %s %s %06d=frame_from %06d=frame_to  normal  1=repeatcount\n"
                     , gpp->ainfo_ptr->basename
                     , &gpp->ainfo_ptr->extension[1]
                     , (int)gpp->begin_frame
                     , (int)gpp->end_frame
                     );

}  /* end p_printout_range */

/* -----------------------------
 * p_reload_ainfo_ptr
 * -----------------------------
 */
static void
p_reload_ainfo_ptr(GapPlayerMainGlobalParams *gpp, gint32 image_id)
{
  gpp->image_id = image_id;
  
  if(gpp->ainfo_ptr)  { gap_lib_free_ainfo(&gpp->ainfo_ptr); }

  gpp->ainfo_ptr = gap_lib_alloc_ainfo(gpp->image_id, gpp->run_mode);
  if(gpp->ainfo_ptr == NULL)
  {
    return;
  }
  if (0 == gap_lib_dir_ainfo(gpp->ainfo_ptr))
  {
    if(0 == gap_lib_chk_framerange(gpp->ainfo_ptr))
    {
      GapVinVideoInfo *vin_ptr;
      vin_ptr = gap_vin_get_all(gpp->ainfo_ptr->basename);

      gpp->ainfo_ptr->width  = gimp_image_width(gpp->image_id);
      gpp->ainfo_ptr->height = gimp_image_height(gpp->image_id);

      if(vin_ptr)
      {
        gpp->original_speed = vin_ptr->framerate;
      }
      if(vin_ptr)
      {
        g_free(vin_ptr);
      }
    }
  }
  
}  /* end p_reload_ainfo_ptr */

/* -----------------------------
 * p_update_ainfo_dependent_widgets
 * -----------------------------
 */
static void
p_update_ainfo_dependent_widgets(GapPlayerMainGlobalParams *gpp)
{
  gdouble l_lower;
  gdouble l_upper;
  
  if(gpp == NULL) { return; }
  if(gpp->ainfo_ptr == NULL) { return; }
  
  l_lower = (gdouble)gpp->ainfo_ptr->first_frame_nr;
  l_upper = (gdouble)gpp->ainfo_ptr->last_frame_nr;
  
  GTK_ADJUSTMENT(gpp->from_spinbutton_adj)->lower = l_lower;
  GTK_ADJUSTMENT(gpp->from_spinbutton_adj)->upper = l_upper;
  GTK_ADJUSTMENT(gpp->from_spinbutton_adj)->value = CLAMP(GTK_ADJUSTMENT(gpp->from_spinbutton_adj)->value
                                                         ,l_lower, l_upper);
  
  GTK_ADJUSTMENT(gpp->to_spinbutton_adj)->lower = l_lower;
  GTK_ADJUSTMENT(gpp->to_spinbutton_adj)->upper = l_upper;
  GTK_ADJUSTMENT(gpp->to_spinbutton_adj)->value = CLAMP(GTK_ADJUSTMENT(gpp->to_spinbutton_adj)->value
                                                       ,l_lower, l_upper);
  
  GTK_ADJUSTMENT(gpp->framenr_spinbutton_adj)->lower = l_lower;
  GTK_ADJUSTMENT(gpp->framenr_spinbutton_adj)->upper = l_upper;
  GTK_ADJUSTMENT(gpp->framenr_spinbutton_adj)->value = CLAMP(GTK_ADJUSTMENT(gpp->framenr_spinbutton_adj)->value
                                                            ,l_lower, l_upper);
  
}  /* end p_update_ainfo_dependent_widgets */


/* ------------------------------------
 * p_find_master_image_id
 * ------------------------------------
 * try to find the master image by filename 
 * matching at basename and extension part
 *
 * return positive image_id  on success
 * return -1 if nothing found
 */
static gint32
p_find_master_image_id(GapPlayerMainGlobalParams *gpp)
{
  gint32 *images;
  gint    nimages;
  gint    l_idi;
  gint    l_baselen;
  gint    l_extlen;
  gint32  l_found_image_id;

  if(gpp->ainfo_ptr == NULL) { return -1; }
  if(gpp->ainfo_ptr->basename == NULL) { return -1; }

  if(gap_debug)
  {
    printf("p_find_master_image_id: image_id %s %s START\n"
           , gpp->ainfo_ptr->basename
           , gpp->ainfo_ptr->extension
           );
  }

  l_baselen = strlen(gpp->ainfo_ptr->basename);
  l_extlen = strlen(gpp->ainfo_ptr->extension);

  images = gimp_image_list(&nimages);
  l_idi = nimages -1;
  l_found_image_id = -1;
  while((l_idi >= 0) && images)
  {
    gchar *l_filename;
    
    l_filename = gimp_image_get_filename(images[l_idi]);
    if(l_filename)
    {
      if(gap_debug) printf("p_find_master_image_id: comare with %s\n", l_filename);
      
      if(strncmp(l_filename, gpp->ainfo_ptr->basename, l_baselen) == 0)
      {
         gint l_len;
         
         l_len = strlen(l_filename);
         if(l_len > l_extlen)
         {
            if(strncmp(&l_filename[l_len - l_extlen], gpp->ainfo_ptr->extension, l_extlen) == 0)
            {
              l_found_image_id = images[l_idi];
              break;
            }
         }
      }
      g_free(l_filename);
    }
    l_idi--;
  }
  if(images) g_free(images);

  return l_found_image_id;

}  /* end p_find_master_image_id */


/* ------------------------------------
 * p_keep_track_of_active_master_image
 * ------------------------------------
 */
static void
p_keep_track_of_active_master_image(GapPlayerMainGlobalParams *gpp)
{
  p_stop_playback(gpp);
  
  gpp->image_id = p_find_master_image_id(gpp);
  if(gpp->image_id >= 0)
  {
    p_reload_ainfo_ptr(gpp, gpp->image_id);
    p_update_ainfo_dependent_widgets(gpp);
  }
  else
  {
    /* cannot find the master image, so quit immediate */
    printf("p_keep_track_of_active_master_image: Master Image not found (may have been closed)\n");
    printf("Exiting Playback\n");
    on_shell_window_destroy(NULL, gpp);
  }
}  /* end p_keep_track_of_active_master_image */



/* -----------------------------
 * p_check_for_active_image
 * -----------------------------
 * check if framenr is the the active image
 * (from where we were called at plug-in invocation time)
 *
 * this procedure also tries to keep track of changes
 * of the active master image_id outside this plug-in.
 *
 * (stepping to another frame, using other GAP plug-ins
 *  changes the master image_id outside ...)
 */
gboolean
p_check_for_active_image(GapPlayerMainGlobalParams *gpp, gint32 framenr)
{
  
  if (gap_image_is_alive(gpp->image_id))
  {
    if(framenr == gpp->ainfo_ptr->curr_frame_nr)
    {
      return(TRUE);
    }
  }
  else
  {
    p_keep_track_of_active_master_image(gpp); 
    if(framenr == gpp->ainfo_ptr->curr_frame_nr)
    {
       return(TRUE);
    }
    return (FALSE);
  }
  
  return (FALSE);

}  /* end p_check_for_active_image */



/* -----------------------------
 * p_update_pviewsize
 * -----------------------------
 * set preview size nd size spinbutton
 */
static void
p_update_pviewsize(GapPlayerMainGlobalParams *gpp)
{
  /*
   * Resize the greater one of dwidth and dheight to PREVIEW_SIZE
   */
  if ( gpp->ainfo_ptr->width > gpp->ainfo_ptr->height )
  {
    /* landscape */
    gpp->pv_height = gpp->ainfo_ptr->height * gpp->pv_pixelsize / gpp->ainfo_ptr->width;
    gpp->pv_width = gpp->pv_pixelsize;
  }
  else
  {
    /* portrait */
    gpp->pv_width = gpp->ainfo_ptr->width * gpp->pv_pixelsize / gpp->ainfo_ptr->height;
    gpp->pv_height = gpp->pv_pixelsize;
  }

  if(gpp->pv_pixelsize != (gint32)GTK_ADJUSTMENT(gpp->size_spinbutton_adj)->value)
  { 
    gtk_adjustment_set_value( GTK_ADJUSTMENT(gpp->size_spinbutton_adj)
                            , (gfloat)gpp->pv_pixelsize
                            );
  }
                                                       
  gpp->pv_pixelsize = (gint32)GTK_ADJUSTMENT(gpp->size_spinbutton_adj)->value;

  gap_pview_set_size(gpp->pv_ptr
                  , gpp->pv_width
                  , gpp->pv_height
                  , MAX(GAP_PLAYER_CHECK_SIZE, (gpp->pv_pixelsize / 16))
                  );

} /* end p_update_pviewsize */


/* -----------------------------
 * p_update_position_widgets
 * -----------------------------
 * set position spinbutton and time entry
 */
static void
p_update_position_widgets(GapPlayerMainGlobalParams *gpp)
{
  static gchar time_txt[12];

  if((gint32)GTK_ADJUSTMENT(gpp->framenr_spinbutton_adj)->value != gpp->play_current_framenr)
  {
    gtk_adjustment_set_value( GTK_ADJUSTMENT(gpp->framenr_spinbutton_adj)
                            , (gfloat)gpp->play_current_framenr
                            );                            
  }
  gap_timeconv_framenr_to_timestr( (gpp->play_current_framenr - gpp->ainfo_ptr->first_frame_nr)
                           , gpp->original_speed
                           , time_txt
                           , sizeof(time_txt)
                           );

  gtk_label_set_text ( GTK_LABEL(gpp->timepos_label), time_txt);
}  /* end p_update_position_widgets */


/* -----------------------------
 * p_start_playback_timer
 * -----------------------------
 * this procedure sets up the delay
 * until next timercall.
 */
static void
p_start_playback_timer(GapPlayerMainGlobalParams *gpp)
{
  gint32 delay_until_next_timercall_millisec;
  gdouble cycle_time_secs;

  cycle_time_secs = 1.0 / MAX(gpp->speed, 1.0);
  if(cycle_time_secs != gpp->cycle_time_secs)
  {
     /* playback speed has changed while playing
      * reset timing (restart playing with new speed on the fly)
      * use a half delay, as guess after speed has changed
      */
     gpp->rest_secs = cycle_time_secs / 2.0;
     gpp->delay_secs = 0.0;                  /* reset the absolute delay (since start or speedchange) */
     gpp->framecnt = 0.0;
     g_timer_start(gpp->gtimer);  /* (re)start timer at start of playback (== reset to 0) */
  }
  else
  {
    if(gpp->rest_secs < 0)
    {
       /* use a minimal delay, bacause we are LATE */;
      gpp->rest_secs = 10.0 / 1000.0; 
      gpp->framecnt = 0.0;
      g_timer_start(gpp->gtimer);  /* (re)start timer at start of playback (== reset to 0) */
    }
  }

  gpp->cycle_time_secs = cycle_time_secs;
  delay_until_next_timercall_millisec =  (gpp->rest_secs) * 1000.0;
  
  /*if(gap_debug) printf("p_start_playback_timer: START delay_until_next_timercall_millisec:%d\n", (int)delay_until_next_timercall_millisec);*/

  gpp->play_is_active = TRUE;
  gpp->play_timertag = (gint32) g_timeout_add(delay_until_next_timercall_millisec,
                                        (GtkFunction)on_timer_playback, gpp);
}  /* end p_start_playback_timer */


/* -----------------------------
 * p_initial_start_playback_timer
 * -----------------------------
 */
static void
p_initial_start_playback_timer(GapPlayerMainGlobalParams *gpp)
{
  p_audio_stop(gpp);    /* stop old playback if there is any */
  gpp->audio_resync = 0;
  gpp->cycle_time_secs = 1.0 / MAX(gpp->speed, 1.0);
  gpp->rest_secs = 10.0 / 1000.0;   /* use minimal delay for the 1st call */
  gpp->delay_secs = 0.0;            /* absolute delay (for display) */
  gpp->framecnt = 0.0;
  gpp->go_job_framenr = -1;         /* pending timer_go_job gets useless, since we start playback now  */
  
  gtk_label_set_text ( GTK_LABEL(gpp->status_label), _("Playing"));

  g_timer_start(gpp->gtimer);  /* (re)start timer at start of playback (== reset to 0) */
  p_start_playback_timer(gpp);
  p_audio_start_play(gpp);
}  /* end p_initial_start_playback_timer */


/* -----------------------------
 * p_remove_play_timer
 * -----------------------------
 */
static void
p_remove_play_timer(GapPlayerMainGlobalParams *gpp)
{
  /*if(gap_debug) printf("p_remove_play_timer\n");*/

  if(gpp->play_timertag >= 0)
  {
    g_source_remove(gpp->play_timertag);
  }
  gpp->play_timertag = -1;
}  /* end p_remove_play_timer */


/* -----------------------------
 * p_stop_playback
 * -----------------------------
 */
static void
p_stop_playback(GapPlayerMainGlobalParams *gpp)
{
  /* if(gap_debug) printf("p_stop_playback\n"); */
  p_remove_play_timer(gpp);
  gpp->play_is_active = FALSE;
  gpp->pingpong_count = 0;

  gtk_label_set_text ( GTK_LABEL(gpp->status_label), _("Ready"));

  p_check_tooltips();
  p_audio_stop(gpp);
  gpp->audio_resync = 0;
}  /* end p_stop_playback */


#define PREVIEW_BPP 3


/* ------------------------------
 * p_display_frame
 * ------------------------------
 * display framnr from thumbnail or from full image
 * the active image (the one from where we were invoked)
 * is not read from discfile to reflect actual changes.
 */
static void
p_display_frame(GapPlayerMainGlobalParams *gpp, gint32 framenr)
{
  char *l_filename;
   gint32  l_th_width;
   gint32  l_th_height;
   gint32  l_th_data_count;
   gint32  l_th_bpp;
   guchar *l_th_data;
   gboolean framenr_is_the_active_image;

  /*if(gap_debug) printf("p_display_frame START: framenr:%d\n", (int)framenr);*/

  l_th_data = NULL;
  l_th_bpp = 3;
  
  l_filename = gap_lib_alloc_fname(gpp->ainfo_ptr->basename, framenr, gpp->ainfo_ptr->extension);

  framenr_is_the_active_image = p_check_for_active_image(gpp, framenr);

  if(gpp->use_thumbnails)
  {
    if(framenr_is_the_active_image)
    {
       gap_pdb_gimp_image_thumbnail(gpp->image_id
                          , gpp->pv_width
                          , gpp->pv_height
                          , &l_th_width
                          , &l_th_height
                          , &l_th_bpp
			  , &l_th_data_count
                          , &l_th_data
                          );
    }
    else
    {
      gap_pdb_gimp_file_load_thumbnail(l_filename
                                , &l_th_width, &l_th_height
                                , &l_th_data_count, &l_th_data);
    }
  }

  if (l_th_data)
  {
    gboolean l_th_data_was_grabbed;

    l_th_data_was_grabbed = gap_pview_render_from_buf (gpp->pv_ptr
                 , l_th_data
                 , l_th_width
                 , l_th_height
                 , l_th_bpp
                 , TRUE         /* allow_grab_src_data */
                 );
    if(l_th_data_was_grabbed)
    {
      /* the gap_pview_render_from_buf procedure can grab the l_th_data
       * instead of making a ptivate copy for later use on repaint demands.
       * if such a grab happened it returns TRUE.
       * (this is done for optimal performance reasons)
       * in such a case the caller must NOT free the src_data (l_th_data) !!!
       */
      l_th_data = NULL;
    }

  }
  else
  {
    gint32  l_image_id;
     
    /* got no thumbnail data, must use the full image */
    if(framenr_is_the_active_image)
    {
      l_image_id = gimp_image_duplicate(gpp->image_id);
    }
    else
    {
      l_image_id = gap_lib_load_image(l_filename);

      if (l_image_id < 0)
      {
        /* could not read the image
         * one reason could be, that frames were deleted while this plugin is active
         * so we stop playback,
         * and try to reload informations about all frames
         */
        if(gap_debug) printf("LOAD IMAGE_ID: %s failed\n", l_filename);
        p_keep_track_of_active_master_image(gpp);
      }
    }
    
    /* there is no need for undo on this scratch image
     * so we turn undo off for performance reasons
     */
    gimp_image_undo_disable (l_image_id);
    
    gap_pview_render_from_image (gpp->pv_ptr, l_image_id);
    if(gpp->use_thumbnails)
    {
      gap_thumb_cond_gimp_file_save_thumbnail(l_image_id, l_filename);
    }
    gimp_image_delete(l_image_id);
  }
  

  gpp->play_current_framenr = framenr;
  p_update_position_widgets(gpp);

  gdk_flush();

  if(l_th_data)  g_free(l_th_data);

  g_free(l_filename);

}  /* end p_display_frame */


/* ------------------------------
 * p_get_next_framenr_in_sequence /2
 * ------------------------------
 */
gint32
p_get_next_framenr_in_sequence2(GapPlayerMainGlobalParams *gpp)
{
  gint32 l_first;
  gint32 l_last;

  l_first = gpp->ainfo_ptr->first_frame_nr;
  l_last  = gpp->ainfo_ptr->last_frame_nr;
  if(gpp->play_selection_only)
  {
    l_first = gpp->begin_frame;
    l_last  = gpp->end_frame;
  }


  if(gpp->play_backward)
  {
    if(gpp->play_current_framenr <= l_first)
    {
      p_audio_resync(gpp);
      if(gpp->play_loop)
      {
        if(gpp->play_pingpong)
        {
          gpp->play_current_framenr = l_first + 1;
          gpp->play_backward = FALSE;
          gpp->pingpong_count++;
        }
        else
        {
          gpp->play_current_framenr = l_last;
        }
      }
      else
      {
        if((gpp->play_pingpong) && (gpp->pingpong_count <= 0))
        {
          gpp->play_current_framenr = l_first + 1;
          gpp->play_backward = FALSE;
          gpp->pingpong_count++;
        }
        else
        {
          gpp->pingpong_count = 0;
          return -1;  /* STOP if first frame reached */
        }
      }
    }
    else
    {
      gpp->play_current_framenr--;
    }
  }
  else
  {
    if(gpp->play_current_framenr >= l_last)
    {
      p_audio_resync(gpp);
      if(gpp->play_loop)
      {
        if(gpp->play_pingpong)
        {
          gpp->play_current_framenr = l_last - 1;
          gpp->play_backward = TRUE;
          gpp->pingpong_count++;
        }
        else
        {
          gpp->play_current_framenr = l_first;
        }
      }
      else
      {
        if((gpp->play_pingpong) && (gpp->pingpong_count <= 0))
        {
          gpp->play_current_framenr = l_last - 1;
          gpp->play_backward = TRUE;
          gpp->pingpong_count++;
        }
        else
        {
          gpp->pingpong_count = 0;
          return -1;  /* STOP if last frame reached */
        }
      }
    }
    else
    {
      gpp->play_current_framenr++;
    }
  }
  
  gpp->play_current_framenr = CLAMP(gpp->play_current_framenr, l_first, l_last);
  return (gpp->play_current_framenr);
}  /* end p_get_next_framenr_in_sequence2 */

gint32
p_get_next_framenr_in_sequence(GapPlayerMainGlobalParams *gpp)
{
  gint32 framenr;
  framenr = p_get_next_framenr_in_sequence2(gpp);
  
  if((framenr < 0)
  || (gpp->play_backward)
  || (gpp->audio_resync > 0))
  {
    /* stop audio at end (-1) or when video plays revers or resync needed */
    p_audio_stop(gpp);

    /* RESYNC: break for N frames, then restart audio at sync position
     * (NOTE: ideally we should restart immediate without any break
     *  but restarting the playback often and very quickly leads to
     *  deadlock (in the audioserver or clent/server communication)
     */
    if(gpp->audio_resync > 0)
    {
      gpp->audio_resync--;
    }
  }
  else
  {
    /* conditional audio start 
     * (can happen on any frame if audio offsets are used)
     */
    
#ifdef TRY_AUTO_SYNC_AT_FAST_PLAYBACKSPEED
    /* this codespart tries to keep audio playing sync at fast speed
     * but sounds not good, and does not work reliable
     * therefore it should not be compiled in public release version.
     * (asynchron audio that will not be able to follow up the videospeed
     *  will be the result in that case)
     */
    if (gpp->audio_required_samplerate > GAP_PLAYER_MAIN_MAX_SAMPLERATE)
    {
      gint32 nframes;
      /* required samplerate is faster than highest possible audioplayback speed
       * stop and restart at new offset at every n.th frame to follow the faster video
       * (this will result in glitches)
       */
      nframes = 8 * (1 + (gpp->speed / 5));
      if((framenr % nframes)  == 0)
      {
        printf("WARNING: Audiospeed cant follow up video speed nframes:%d\n", (int)nframes);
        p_audio_resync(gpp);  /* audioserver does not like tto much stop&go and hangs sometimes */
      }
    }
#endif
    p_audio_start_play(gpp);
  }
  return(framenr);	  
}  /*end p_get_next_framenr_in_sequence */


/* ------------------------------
 * p_framenr_from_go_number
 * ------------------------------
 */
gint32
p_framenr_from_go_number(GapPlayerMainGlobalParams *gpp, gint32 go_number)
{
  /*
  if(gap_debug) printf("p_framenr_from_go_number go_base_framenr: %d  go_base:%d go_number:%d curr_frame:%d\n"
     , (int)gpp->go_base_framenr
     , (int)gpp->go_base
     , (int)go_number
     , (int)gpp->play_current_framenr );
  */

  if((gpp->go_base_framenr < 0) || (gpp->go_base < 0))
  {
    gpp->go_base_framenr = gpp->play_current_framenr;
    gpp->go_base = go_number;
  }

  /* printf("p_framenr_from_go_number: result framenr:%d\n", (int)(gpp->go_base_framenr  + (go_number - gpp->go_base)) ); */

  return CLAMP(gpp->go_base_framenr  + (go_number - gpp->go_base)
                     , gpp->ainfo_ptr->first_frame_nr
                     , gpp->ainfo_ptr->last_frame_nr
                     );
}  /* end p_framenr_from_go_number */


/* ------------------
 * on_timer_playback
 * ------------------
 * This timer callback is called periodical in intervall depending of playback speed.
 * (the playback timer is completely removed while not playing)
 */
static void
on_timer_playback(gpointer   user_data)
{
  GapPlayerMainGlobalParams *gpp;
  gulong elapsed_microsecs;
  gdouble elapsed_secs;
  static char  status_txt[30];

  /*if(gap_debug) printf("\non_timer_playback: START\n");*/
  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp)
  {
    p_remove_play_timer(gpp);
    if(gpp->in_timer_playback)
    {
      /* if speed is too fast the timer may fire again, while still processing
       * the previous callback. in that case return without any action
       * NOTE: at test with gtk2.2.1 this line of CODE was never reached,
       *       even if the frame is too late.
       *       the check if in_timer_playback is done just to be at the safe side.
       *       (late frames are detected by checking the elapsed time).
       */
      printf("\n\n\n  on_timer_playback interrupted by next TIMERCALL \n\n");
      return;
    }
    gpp->in_timer_playback = TRUE;

    if(gpp->play_is_active)
    {
       gint ii;
       gint ii_max;
       gint32 l_prev_framenr;
       gint32 l_framenr = -1;
       gboolean l_frame_dropped;
       gdouble  l_delay;
       
       l_framenr = -1;
       l_frame_dropped = FALSE;
       l_delay = gpp->delay_secs;
       
       ii_max = (gpp->exact_timing) ? 20 : 2;
       
       /* - if we have exact timing handle (or drop) upto max 20 frames in one timercallback
        * - if your machine is fast enough to display the frame at gpp->speed
        *   this loop will display only one frame. (the ideal case)
        * - if NOT fast enough, but the frame is just a little late,
        *   it tries to display upto (3) frames without restarting the timer.
        *   (and without giving control back to the gtk main loop)
        * - if we are still late at this point
        *   we begin to drop  upto (20) frames to get back in exact time.
        * - if this does not help either, we give up
        *   (results in NON exact timing, regardless to the exact_timing flag).
        */
       for(ii=1; ii < ii_max; ii++)
       {
         l_prev_framenr = l_framenr;
         l_framenr = p_get_next_framenr_in_sequence(gpp);
         gpp->framecnt += 1.0;  /* count each handled frame (for timing purpose) */

         /*if(gap_debug) printf("on_timer_playback[%d]: l_framenr:%d\n", (int)ii, (int)l_framenr);*/

         if(l_framenr < 0)
         {
           if((l_frame_dropped) && (l_prev_framenr > 0))
           {
             /* the last frame in sequence (l_prev_framenr) would be DROPPED
              * to keep exact timing, but now it is time to STOP
              * in this case we display that frame.
              */
             p_display_frame(gpp, l_prev_framenr);
           }
           p_stop_playback(gpp);  /* STOP at end of sequence */
           break;
         }
         else
         {
           if((ii > 3)
           || ((0 - gpp->rest_secs) > (gpp->cycle_time_secs * 2)) )
           {
             /* if delay is too much 
              * (we are 2 cycletimes back, or already had handled 3 LATE frames in sequence)
              * start dropping frames now
              * until we are in time again
              */
             l_frame_dropped = TRUE;
             printf("DROP (SKIP) frame\n");
             gtk_label_set_text ( GTK_LABEL(gpp->status_label), _("Skip"));
           }
           else
           {
             p_display_frame(gpp, l_framenr);
           }

           /* get secs elapsed since playbackstart (or last speed change) */
           elapsed_secs = g_timer_elapsed(gpp->gtimer, &elapsed_microsecs);

           gpp->rest_secs = (gpp->cycle_time_secs * gpp->framecnt) - elapsed_secs;
           /*if(gap_debug) printf("on_timer_playback[%d]: rest:%.4f\n", (int)ii, (float)gpp->rest_secs); */

           if(gpp->rest_secs > 0)
           {
              if((!l_frame_dropped) && (ii == 1))
              {
                 gtk_label_set_text ( GTK_LABEL(gpp->status_label), _("Playing"));
              }
              /* we are fast enough, there is rest time to wait until next frame */
             l_delay = gpp->delay_secs;  /* we have no additional delay */
             break;
           }

           l_delay = gpp->delay_secs - gpp->rest_secs;
            
           /* no time left at this point, try to display (or drop) next frame immediate */
           if(!l_frame_dropped)
           {
                 g_snprintf(status_txt, sizeof(status_txt), _("Delay %.2f"), (float)(l_delay));
                 gtk_label_set_text ( GTK_LABEL(gpp->status_label), status_txt);
           }
         }
       }    /* end for */

       /* keep track of absolute delay (since start or speed change) just for display purposes */
       gpp->delay_secs = l_delay;
              
       /* restart timer for next cycle */
       if(gpp->play_is_active)
       {
         p_start_playback_timer(gpp);
       }

    }
    gpp->in_timer_playback = FALSE;
  }
}  /* end on_timer_playback */



/* ------------------
 * on_timer_go_job
 * ------------------
 * This timer callback is called once after a go_button enter/clicked
 * callback with a miniamal delay of 8 millisecs.
 *
 * here is an example how it works:
 *
 *  consider the user moves the mouse from left to right
 *  passing go_buttons 1 upto 7
 *      
 *    (1)     (2)     (3)     (4)    (5)    (6)    (7)
 *     +-------+-------+-------+------+------+------+-------> time axis
 *     ..XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
 *       < --  RENDER time for (1) ------------>
 *
 *  - at (1) the on_timer_go_job  Timer is initialized
 *  - The timer will fire after 8 millsecs, and starts rendering.
 *    while renderng 2,3,4,5,6 do happen, and get queued as pending events.
 *  - after render has finished
 *    the events are processed in sequence
 *    the first of the queued evnts (2) will init the go_timer again,
 *    and each of the oter events just sets the go_framenr (upto 6).
 *  - after another 8 millisecs delay the timer callback will start again
 *    and now processes the most recent go_framenr (6)
 *   
 * that way it is possible to skip frames for fast mousemoves on slow machines
 * (without that trick, the GUI would block until all visited go_bottons
 * are processed)
 */
static void
on_timer_go_job(gpointer   user_data)
{
  GapPlayerMainGlobalParams *gpp;

  if(gap_debug) printf("\non_timer_go_job: START\n");
  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp)
  {
    if(gpp->go_job_framenr >= 0)
    {
      p_display_frame(gpp, gpp->go_job_framenr);
    }
    
    if(gpp->go_timertag >= 0)
    {
      g_source_remove(gpp->go_timertag);
    }
    gpp->go_timertag = -1;

  }
}  /* end on_timer_go_job */

/* -----------------------------
 * on_play_button_clicked
 * -----------------------------
 */
static void
on_play_button_clicked(GtkButton *button, gpointer user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  gpp->play_backward = FALSE;

  if(gpp->play_is_active)
  {
    if (gpp->audio_required_samplerate > GAP_PLAYER_MAIN_MAX_SAMPLERATE)
    {
      p_audio_resync(gpp);
    }
    return;
  }

  if(gpp->play_selection_only)
  {
    if (gpp->play_current_framenr >= gpp->end_frame)
    {
      /* we are at selection end, start from selection begin */
      gpp->play_current_framenr = gpp->begin_frame;
    }
  }
  else
  {
    if (gpp->play_current_framenr >= gpp->ainfo_ptr->last_frame_nr)
    {
      /* we are at end, start from begin */
      gpp->play_current_framenr = gpp->ainfo_ptr->first_frame_nr;
    }
  }

  p_initial_start_playback_timer(gpp);

}  /* end on_play_button_clicked */


/* -----------------------------
 * on_pause_button_press_event
 * -----------------------------
 */
static gboolean
on_pause_button_press_event        (GtkButton       *button,
                                    GdkEventButton  *bevent,
                                    gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;
  gboolean         play_was_active;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return FALSE;
  }
  
  play_was_active = gpp->play_is_active;
  p_stop_playback(gpp);

  if(!play_was_active)
  {

    if ((bevent->button > 1)
    &&  (bevent->type == GDK_BUTTON_PRESS)
    &&  (gpp->ainfo_ptr))
    {
      if(bevent->button > 2)
      {
        p_display_frame(gpp, gpp->end_frame);  /* right mousebutton : goto end */
      }
      else
      {
        p_display_frame(gpp, gpp->ainfo_ptr->curr_frame_nr);  /* middle mousebutton : goto active image (invoker) */
      }
    }
    else
    {
      p_display_frame(gpp, gpp->begin_frame); /* left mousebutton : goto begin */
    }
    
    if((bevent->state & GDK_SHIFT_MASK)
    || (bevent->state & GDK_CONTROL_MASK)
    || (bevent->state & GDK_MOD1_MASK))
    {
      p_printout_range(gpp);
    }
  }

  return FALSE;
}  /* end on_pause_button_press_event */


/* -----------------------------
 * on_back_button_clicked
 * -----------------------------
 */
static void
on_back_button_clicked                 (GtkButton       *button,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  gpp->play_backward = TRUE;

  if(gpp->play_is_active)
  {
    return;
  }

  if(gpp->play_selection_only)
  {
    if (gpp->play_current_framenr <= gpp->begin_frame)
    {
      /* we are already at selection begin, start from selection end */
      gpp->play_current_framenr = gpp->end_frame;
    }
  }
  else
  {
    if (gpp->play_current_framenr <= gpp->ainfo_ptr->first_frame_nr)
    {
      /* we are already at begin, start from end */
      gpp->play_current_framenr = gpp->ainfo_ptr->last_frame_nr;
    }
  }

  p_initial_start_playback_timer(gpp);

} /* end on_back_button_clicked */


/* -----------------------------
 * on_from_spinbutton_changed
 * -----------------------------
 */
static void
on_from_spinbutton_changed             (GtkEditable     *editable,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }
  gpp->begin_frame = (gint32)GTK_ADJUSTMENT(gpp->from_spinbutton_adj)->value;
  if(gpp->begin_frame > gpp->end_frame)
  {
    gpp->end_frame = gpp->begin_frame;
    gtk_adjustment_set_value( GTK_ADJUSTMENT(gpp->to_spinbutton_adj)
                            , (gfloat)gpp->end_frame
                            );                            
  }
  
}  /* end on_from_spinbutton_changed */



/* -----------------------------
 * on_to_spinbutton_changed
 * -----------------------------
 */
static void
on_to_spinbutton_changed               (GtkEditable     *editable,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }
  gpp->end_frame = (gint32)GTK_ADJUSTMENT(gpp->to_spinbutton_adj)->value;
  if(gpp->end_frame < gpp->begin_frame)
  {
    gpp->begin_frame = gpp->end_frame;
    gtk_adjustment_set_value( GTK_ADJUSTMENT(gpp->from_spinbutton_adj)
                            , (gfloat)gpp->begin_frame
                            );                            
  }
  
}  /* end on_to_spinbutton_changed */


/* -----------------------------
 * on_vid_preview_button_press_event
 * -----------------------------
 */
gboolean
on_vid_preview_button_press_event      (GtkWidget       *widget,
                                        GdkEventButton  *bevent,
                                        gpointer         user_data)
{


  if(gap_debug) printf("on_vid_preview_button_press_event: START\n");
  on_framenr_button_clicked(NULL, user_data);
  
  return FALSE;

}  /* end on_vid_preview_button_press_event */


/* -----------------------------
 * on_vid_preview_expose_event
 * -----------------------------
 */
gboolean
on_vid_preview_expose_event      (GtkWidget       *widget,
                                  GdkEventExpose  *eevent,
                                  gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;


  /*if(gap_debug) printf(" xxxxxxxx on_vid_preview_expose_event: START\n");*/

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return FALSE;
  }

  if (gpp->play_is_active && (gpp->rest_secs < 0.2))
  {
    /* we are playing and the next frame update is very near (less than 2/10 sec)
     * in that case it is better choice to skip the repaint
     * on expose events.
     * (dont waste time for repaint in that case)
     */
    return FALSE;
  }
  gap_pview_repaint(gpp->pv_ptr);
  
  return FALSE;

}  /* end on_vid_preview_expose_event */

/* -----------------------------
 * on_shell_window_size_allocate
 * -----------------------------
 */
static void
on_shell_window_size_allocate            (GtkWidget       *widget,
                                         GtkAllocation   *allocation,
                                        gpointer         user_data)
{
   GapPlayerMainGlobalParams *gpp;
   gpp = (GapPlayerMainGlobalParams*)user_data;
   if((gpp == NULL) || (allocation == NULL))
   {
     return;
   }
   
   if(gap_debug) printf("on_shell_window_size_allocate: START  shell_alloc: w:%d h:%d \n"
                           , (int)allocation->width
                           , (int)allocation->height
                           );

   if(gpp->shell_initial_width < 0)
   {
     gpp->shell_initial_width = allocation->width;
     gpp->shell_initial_height = allocation->height;
     p_fit_initial_shell_window(gpp);  /* for setting window default size */
   }
   else
   {
     if((allocation->width < gpp->shell_initial_width)
     || (allocation->height < gpp->shell_initial_height))
     {
       /* dont allow shrink below initial size */
       p_fit_initial_shell_window(gpp);
     }
   }  			   
}  /* end on_shell_window_size_allocate */ 

/* -----------------------------
 * on_vid_preview_size_allocate
 * -----------------------------
 * this procedure handles automatic
 * resize of the preview when the user resizes the window.
 * While sizechanges via the Size spinbutton this handler is usually
 * disconnected, and will be reconnected when the mouse leaves the shell window.
 * This handlerprocedure acts on the drawing_area widget of the preview.
 * Size changes of drawing_area are propagated to
 * the preview (aspect_frame and drawing_area) by calls to p_update_pviewsize.
 */
static void
on_vid_preview_size_allocate            (GtkWidget       *widget,
                                         GtkAllocation   *allocation,
                                        gpointer         user_data)
{
   GapPlayerMainGlobalParams *gpp;
   gint32 allo_width;
   gint32 allo_height;

#define PV_BORDER_X 10
#define PV_BORDER_Y 10

   gpp = (GapPlayerMainGlobalParams*)user_data;
   if((gpp == NULL) || (allocation == NULL))
   {
     return;
   }

   if(gap_debug) printf("on_vid_preview_size_allocate: START old: ow:%d oh:%d  new: w:%d h:%d \n"
                           , (int)gpp->old_resize_width
                           , (int)gpp->old_resize_height
                           , (int)allocation->width
                           , (int)allocation->height
                           );

   if(gpp->pv_ptr->da_widget == NULL) { return; }
   if(gpp->pv_ptr->da_widget->window == NULL) { return; }

   if(gap_debug) printf("  on_vid_preview_size_allocate: ORIGINAL pv_w:%d pv_h:%d handler_id:%d\n"
                           , (int)gpp->pv_ptr->pv_width
                           , (int)gpp->pv_ptr->pv_height
                           , (int)gpp->resize_handler_id
                           );


   allo_width = allocation->width;
   allo_height = allocation->height;

   /* react on significant changes (min 6 pixels) only) */
   if(((gpp->old_resize_width / 6) != (allo_width / 6))
   || ((gpp->old_resize_height / 6) != (allo_height / 6)))
   {
     gint32  pv_pixelsize;
     gboolean blocked;
     gdouble  img_ratio;
     gdouble  alloc_ratio;
     
     
     
     blocked = FALSE;
     alloc_ratio = (gdouble)allo_width / (gdouble)allo_height;
     img_ratio   = (gdouble)gpp->ainfo_ptr->width / (gdouble)gpp->ainfo_ptr->height;
     if(gpp->ainfo_ptr == NULL)
     {
       blocked = FALSE;
       pv_pixelsize = gpp->pv_pixelsize;
     }
     else
     {
        if(img_ratio >= 1.0)
        {
           /* imageorientation is landscape */
           if(alloc_ratio <= img_ratio)
           {
             pv_pixelsize = CLAMP( (allo_width - PV_BORDER_X)
                       , GAP_PLAYER_MIN_SIZE
                       , GAP_PLAYER_MAX_SIZE);
           }
           else
           {
             pv_pixelsize = CLAMP( (((allo_height - PV_BORDER_Y) * gpp->ainfo_ptr->width) / gpp->ainfo_ptr->height)
                       , GAP_PLAYER_MIN_SIZE
                       , GAP_PLAYER_MAX_SIZE);
           }
        }
        else
        {
           /* imageorientation is portrait */
           if(alloc_ratio <= img_ratio)
           {
             pv_pixelsize = CLAMP( (((allo_width - PV_BORDER_X) * gpp->ainfo_ptr->height) / gpp->ainfo_ptr->width)
                       , GAP_PLAYER_MIN_SIZE
                       , GAP_PLAYER_MAX_SIZE);
           }
           else
           {
             pv_pixelsize = CLAMP( (allo_height - PV_BORDER_Y)
                       , GAP_PLAYER_MIN_SIZE
                       , GAP_PLAYER_MAX_SIZE);
           }
        }
     }


     if (gap_debug) printf("pv_pixelsize: %d  img_ratio:%.3f alloc_ratio:%.3f\n"
                               , (int)pv_pixelsize
                               , (float)img_ratio
                               , (float)alloc_ratio
                               );
     
     if(pv_pixelsize > gpp->pv_pixelsize)
     {
       if ((alloc_ratio > 1.0) /* landscape */
       && (allo_width < gpp->old_resize_width))
       {
         if(gap_debug) printf(" BLOCK preview grow on  width shrink\n");
         blocked = TRUE;
       }
       if ((alloc_ratio <= 1.0) /* portrait */
       && (allo_height < gpp->old_resize_height))
       {
         if(gap_debug) printf(" BLOCK preview grow on  height shrink\n");
         blocked = TRUE;
       }
     }
     if(!blocked)
     {
         gpp->pv_pixelsize =  pv_pixelsize;

         p_update_pviewsize(gpp);


         if(!gpp->play_is_active)
         {
           /* have to refresh current frame
            *  repaint is not enough because size has changed
            * (if play_is_active we can skip this, because the next update is on the way)
            */
           p_display_frame(gpp, gpp->play_current_framenr);
         }
     }

     if(gap_debug) printf("  on_vid_preview_size_allocate: AFTER resize pv_w:%d pv_h:%d\n"
                           , (int)gpp->pv_ptr->pv_width
                           , (int)gpp->pv_ptr->pv_height
                           );
   }

   gpp->old_resize_width = allo_width;
   gpp->old_resize_height = allo_height;

   if(gap_debug) printf("  on_vid_preview_size_allocate: END\n");
   
}  /* end on_vid_preview_size_allocate */


/* -----------------------------
 * on_framenr_button_clicked
 * -----------------------------
 */
static void
on_framenr_button_clicked            (GtkButton       *button,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;
   GimpParam          *return_vals;
   int              nreturn_vals;
 
  /*if(gap_debug) printf("on_framenr_button_clicked: START\n"); */

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }
 
  p_stop_playback(gpp);

   return_vals = gimp_run_procedure ("plug_in_gap_goto",
                                    &nreturn_vals,
	                            GIMP_PDB_INT32,    GIMP_RUN_NONINTERACTIVE,
				    GIMP_PDB_IMAGE,    gpp->image_id,
				    GIMP_PDB_DRAWABLE, -1,  /* dummy */
	                            GIMP_PDB_INT32,    gpp->play_current_framenr,
                                    GIMP_PDB_END);

  if(return_vals)
  {
    if (return_vals[0].data.d_status == GIMP_PDB_SUCCESS)
    {
       p_reload_ainfo_ptr(gpp, return_vals[1].data.d_image);
       p_update_ainfo_dependent_widgets(gpp); 
       gimp_displays_flush();
    }
     
    g_free(return_vals);
  }
  

}  /* end on_framenr_button_clicked */



/* -----------------------------
 * on_framenr_spinbutton_changed
 * -----------------------------
 */
static void
on_framenr_spinbutton_changed          (GtkEditable     *editable,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;
  gint32           framenr;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }
  framenr = (gint32)GTK_ADJUSTMENT(gpp->framenr_spinbutton_adj)->value;
  if(gpp->play_current_framenr != framenr)
  {
    p_display_frame(gpp, framenr);
    p_audio_resync(gpp);       /* force audio resync */
  }
  
}  /* end on_framenr_spinbutton_changed */


/* -----------------------------
 * on_origspeed_button_clicked
 * -----------------------------
 */
static void
on_origspeed_button_clicked            (GtkButton       *button,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  if(gpp->speed != gpp->original_speed)
  {
    gpp->prev_speed = gpp->speed;
    gpp->speed = gpp->original_speed;
    gtk_adjustment_set_value( GTK_ADJUSTMENT(gpp->speed_spinbutton_adj)
                            , (gfloat)gpp->speed
                            );                            
  }
  else
  {
    if(gpp->original_speed != gpp->prev_speed)
    {
      gpp->speed = gpp->prev_speed;
      gtk_adjustment_set_value( GTK_ADJUSTMENT(gpp->speed_spinbutton_adj)
                            , (gfloat)gpp->speed
                            );                            
    }
  }
  p_audio_resync(gpp);

}  /* end on_origspeed_button_clicked */


/* -----------------------------
 * on_speed_spinbutton_changed
 * -----------------------------
 */
static void
on_speed_spinbutton_changed            (GtkEditable     *editable,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  gpp->speed = GTK_ADJUSTMENT(gpp->speed_spinbutton_adj)->value;
  
  /* stop audio at speed changes 
   * (audio will restart automatically at next frame with samplerate matching new speed)
   */
  p_audio_resync(gpp);

}  /* end on_speed_spinbutton_changed */

 
/* --------------------------
 * p_fit_initial_shell_window
 * --------------------------
 */
static void 
p_fit_initial_shell_window(GapPlayerMainGlobalParams *gpp)
{
  gint width;
  gint height;

  if(gpp == NULL)                   { return; }
  if(gpp->shell_initial_width < 0)  { return; }

  width = gpp->shell_initial_width;
  height = gpp->shell_initial_height;

  gtk_widget_set_size_request (gpp->shell_window, width, height);  /* shrink shell window */
  gtk_window_set_default_size(GTK_WINDOW(gpp->shell_window), width, height);  /* shrink shell window */
  gtk_window_resize (GTK_WINDOW(gpp->shell_window), width, height);  /* shrink shell window */
}  /* end p_fit_initial_shell_window */
 
/* ---------------------
 * p_fit_shell_window
 * ---------------------
 */
static void 
p_fit_shell_window(GapPlayerMainGlobalParams *gpp)
{
  gint width;
  gint height;

  if((gpp->pv_ptr->pv_width <= GAP_STANDARD_PREVIEW_SIZE)
  && (gpp->pv_ptr->pv_height <= GAP_STANDARD_PREVIEW_SIZE))
  {
    p_fit_initial_shell_window(gpp);
    return;
  }

  /* FIXME: use preview size plus fix offsets (the offsets are just a guess
   * and may be too small for other languages and/or fonts
   */
  width =  MAX(gpp->pv_ptr->pv_width, GAP_STANDARD_PREVIEW_SIZE) + 272;
#ifdef GAP_ENABLE_AUDIO_SUPPORT
  height = MAX(gpp->pv_ptr->pv_height, GAP_STANDARD_PREVIEW_SIZE) + 178;
#else
  height = MAX(gpp->pv_ptr->pv_height, GAP_STANDARD_PREVIEW_SIZE) + 128;
#endif

  gtk_window_set_default_size(GTK_WINDOW(gpp->shell_window), width, height);  /* shrink shell window */
  gtk_window_resize (GTK_WINDOW(gpp->shell_window), width, height);  /* resize shell window */
}  /* end p_fit_shell_window */

/* -----------------------------
 * on_size_button_button_press_event
 * -----------------------------
 */
static gboolean
on_size_button_button_press_event  (GtkWidget       *widget,
                                    GdkEventButton  *bevent,
                                    gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;


  if(gap_debug) printf("\nON_SIZE_BUTTON_BUTTON_PRESS_EVENT START\n");

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return FALSE;
  }
  p_disconnect_resize_handler(gpp);

  if ((bevent->state & GDK_SHIFT_MASK)
  &&  (bevent->type == GDK_BUTTON_PRESS)
  &&  (gpp->ainfo_ptr))
  {
    if(gap_debug) printf("GDK_SHIFT_MASK !!\n\n");
    gpp->pv_pixelsize = CLAMP(MAX(gpp->ainfo_ptr->width, gpp->ainfo_ptr->height)
                       , GAP_PLAYER_MIN_SIZE
                       , GAP_PLAYER_MAX_SIZE);
  }
  else
  {
    /* toggle between normal and large thumbnail size */
    if(gpp->pv_pixelsize == GAP_STANDARD_PREVIEW_SIZE)
    {
      gpp->pv_pixelsize = GAP_SMALL_PREVIEW_SIZE;
    }
    else
    {
      gpp->pv_pixelsize = GAP_STANDARD_PREVIEW_SIZE;
    }
  }
  
  if(gpp->pv_pixelsize != (gint32)GTK_ADJUSTMENT(gpp->size_spinbutton_adj)->value)
  {
    p_update_pviewsize(gpp);
    if(!gpp->play_is_active)
    {
      p_display_frame(gpp, gpp->play_current_framenr);
    }
  }
  gtk_adjustment_set_value( GTK_ADJUSTMENT(gpp->size_spinbutton_adj)
                            , (gfloat)gpp->pv_pixelsize
                            );                            
  

  p_fit_initial_shell_window(gpp);
  p_connect_resize_handler(gpp);

  return FALSE;
}  /* end on_size_button_button_press_event */


/* -----------------------------
 * on_size_spinbutton_changed
 * -----------------------------
 */
static void
on_size_spinbutton_changed             (GtkEditable     *editable,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }
  
  if(gap_debug)
  {
     printf("on_size_spinbutton_changed: value: %d  pv_pixelsize: %d\n"
           , (int)GTK_ADJUSTMENT(gpp->size_spinbutton_adj)->value
           ,(int)gpp->pv_pixelsize
           );
  }
  
  if(gpp->pv_pixelsize != (gint32)GTK_ADJUSTMENT(gpp->size_spinbutton_adj)->value)
  {
    gpp->pv_pixelsize = (gint32)GTK_ADJUSTMENT(gpp->size_spinbutton_adj)->value;

    p_update_pviewsize(gpp);

    if(!gpp->play_is_active)
    {
      p_display_frame(gpp, gpp->play_current_framenr);
    }

    p_fit_shell_window(gpp);
  }
}  /* end on_size_spinbutton_changed */


/* -----------------------------
 * on_size_spinbutton_enter
 * -----------------------------
 */
static gboolean   on_size_spinbutton_enter               (GtkWidget        *widget,
                                                      GdkEvent         *event,
                                                      gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  /*if(gap_debug) printf("\n on_size_spinbutton_enter START\n");*/

  gpp = (GapPlayerMainGlobalParams *)user_data;
  if(gpp)
  {
    p_disconnect_resize_handler(gpp);
  }
  return FALSE;
}  /* end on_size_spinbutton_enter */
                                                      

/* -----------------------------
 * on_shell_window_leave
 * -----------------------------
 */
static gboolean   on_shell_window_leave               (GtkWidget        *widget,
                                                      GdkEvent         *event,
                                                      gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  /*if(gap_debug) printf("\n on_shell_window_leave START\n");*/

  gpp = (GapPlayerMainGlobalParams *)user_data;
  if(gpp)
  {
    p_connect_resize_handler(gpp);
  }
  return FALSE;
}  /* end on_shell_window_leave */




/* -----------------------------
 * on_exact_timing_checkbutton_toggled
 * -----------------------------
 */
static void
on_exact_timing_checkbutton_toggled       (GtkToggleButton *togglebutton,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  if (togglebutton->active)
  {
       gpp->exact_timing = TRUE;
  }
  else
  {
       gpp->exact_timing = FALSE;
  }

}  /* end on_exact_timing_checkbutton_toggled */


/* -----------------------------
 * on_use_thumb_checkbutton_toggled
 * -----------------------------
 */
static void
on_use_thumb_checkbutton_toggled       (GtkToggleButton *togglebutton,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  if (togglebutton->active)
  {
       gpp->use_thumbnails = TRUE;
  }
  else
  {
       gpp->use_thumbnails = FALSE;
  }

}  /* end on_use_thumb_checkbutton_toggled */

/* -----------------------------
 * on_pinpong_checkbutton_toggled
 * -----------------------------
 */
static void
on_pinpong_checkbutton_toggled         (GtkToggleButton *togglebutton,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  if (togglebutton->active)
  {
       gpp->play_pingpong = TRUE;
  }
  else
  {
       gpp->play_pingpong = FALSE;
  }

}  /* end on_pinpong_checkbutton_toggled */


/* -----------------------------
 * on_selonly_checkbutton_toggled
 * -----------------------------
 */
static void
on_selonly_checkbutton_toggled         (GtkToggleButton *togglebutton,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  if (togglebutton->active)
  {
       gpp->play_selection_only = TRUE;
  }
  else
  {
       gpp->play_selection_only = FALSE;
  }

}  /* end on_selonly_checkbutton_toggled */


/* -----------------------------
 * on_loop_checkbutton_toggled
 * -----------------------------
 */
static void
on_loop_checkbutton_toggled            (GtkToggleButton *togglebutton,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  if (togglebutton->active)
  {
       gpp->play_loop = TRUE;
  }
  else
  {
       gpp->play_loop = FALSE;
  }

}  /* end on_loop_checkbutton_toggled */




/* -----------------------------
 * on_close_button_clicked
 * -----------------------------
 */
static void
on_close_button_clicked                (GtkButton       *button,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }
  p_stop_playback(gpp);

  if(gpp->audio_tmp_dialog_is_open)
  {
    /* ignore close as long as sub dialog is open */
    return;
  }

  gtk_widget_destroy (GTK_WIDGET (gpp->shell_window));  /* close & destroy dialog window */
  gtk_main_quit ();

}  /* end on_close_button_clicked */


/* -----------------------------
 * on_shell_window_destroy
 * -----------------------------
 */
static void
on_shell_window_destroy                     (GtkObject       *object,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp)
  {
    p_stop_playback(gpp);
  }
  gtk_main_quit ();

}  /* end on_shell_window_destroy */



/* -----------------------------
 * on_go_button_clicked
 * -----------------------------
 */
static void
on_go_button_clicked                   (GtkButton       *button,
                                        GdkEventButton  *bevent,
                                        gpointer         user_data)
{
   t_gobutton *gob;
   GapPlayerMainGlobalParams *gpp;
   gint32  framenr;

   gob = (t_gobutton *)user_data;
   if(gob == NULL) { return; }

   gpp = gob->gpp;
   if(gpp == NULL) { return; }
   

   /*if (gap_debug) printf("on_go_button_clicked: go_number:%d\n", (int)gob->go_number );*/

   p_stop_playback(gpp);
   framenr = p_framenr_from_go_number(gpp, gob->go_number);
   if(gpp->play_current_framenr != framenr)
   {
     p_display_frame(gpp, framenr);
   }

   if(bevent->type == GDK_BUTTON_PRESS)
   {
     if(bevent->state & GDK_CONTROL_MASK)
     {
       gpp->begin_frame = framenr;
       gtk_adjustment_set_value( GTK_ADJUSTMENT(gpp->from_spinbutton_adj)
                               , (gfloat)gpp->begin_frame
			       );
       if(gpp->begin_frame > gpp->end_frame)
       {
	 gpp->end_frame = gpp->begin_frame;
	 gtk_adjustment_set_value( GTK_ADJUSTMENT(gpp->to_spinbutton_adj)
                        	 , (gfloat)gpp->end_frame
                        	 );                            
       }
       return;
     }
     if(bevent->state & GDK_MOD1_MASK)  /* ALT modifier Key */
     {
       gpp->end_frame = framenr;
       gtk_adjustment_set_value( GTK_ADJUSTMENT(gpp->to_spinbutton_adj)
                        	 , (gfloat)gpp->end_frame
                        	 );                            
       if(gpp->end_frame < gpp->begin_frame)
       {
	 gpp->begin_frame = gpp->end_frame;
	 gtk_adjustment_set_value( GTK_ADJUSTMENT(gpp->from_spinbutton_adj)
                        	 , (gfloat)gpp->begin_frame
                        	 );                            
       }
       return;
     }
   }
   
   if(framenr != gpp->ainfo_ptr->curr_frame_nr)
   {
     on_framenr_button_clicked(NULL, gpp);
   }
}  /* end on_go_button_clicked */


/* -----------------------------
 * on_go_button_enter
 * -----------------------------
 */
static gboolean
on_go_button_enter                   (GtkButton       *button,
                                        GdkEvent      *event,
                                        gpointer       user_data)
{
   t_gobutton *gob;
   GapPlayerMainGlobalParams *gpp;
   gint32  framenr;
   
   gob = (t_gobutton *)user_data;
   if(gob == NULL) { return FALSE; }

   gpp = gob->gpp;
   if(gpp == NULL) { return FALSE; }
   

   /*if (gap_debug) printf("ON_GO_BUTTON_ENTER: go_number:%d\n", (int)gob->go_number ); */
  
   p_stop_playback(gpp);
 
   /* skip display on full sized image mode 
    * (gui cant follow that fast on many machines, and would react slow)
    */ 
   if(gpp->use_thumbnails)
   { 
      framenr = p_framenr_from_go_number(gpp, gob->go_number);
      if(gpp->play_current_framenr != framenr)
      {
          if(gap_debug)
          {
             if(gpp->go_timertag >= 0) 
             {
               printf("on_go_button_enter: DROP GO_FRAMENR: %d\n", (int)gpp->go_job_framenr);
             }
          }
          
          /* we want the go_timer to display that framenr */
          gpp->go_job_framenr = framenr;
          if(gpp->go_timertag < 0)
          {
             /* if the go_timer is not already prepared to fire
              * we start  p_display_frame(gpp, gpp->go_job_framenr); 
              * after minimal delay of 8 millisecods.
              */
             gpp->go_timertag = (gint32) g_timeout_add(8, (GtkFunction)on_timer_go_job, gpp);
          }

      }
   }

   return FALSE;
}  /* end on_go_button_enter */

/* -----------------------------
 * on_gobutton_hbox_leave
 * -----------------------------
 */
static void
on_gobutton_hbox_leave                 (GtkWidget        *widget,
                                        GdkEvent         *event,
                                        gpointer          user_data)
{
  GapPlayerMainGlobalParams *gpp;

  /*if (gap_debug) printf("ON_GOBUTTON_HBOX_LEAVE\n");*/

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  /* reset go_base */
  gpp->go_base_framenr = -1;
  gpp->go_base = -1;
 
  p_check_tooltips();
 
}  /* end on_gobutton_hbox_leave */


/* -----------------------------
 * on_audio_enable_checkbutton_toggled
 * -----------------------------
 */
static void
on_audio_enable_checkbutton_toggled       (GtkToggleButton *togglebutton,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  if (togglebutton->active)
  {
       gpp->audio_enable = TRUE;
       /* audio will start automatic at next frame when playback is running */
  }
  else
  {
       gpp->audio_enable = FALSE;
       gpp->audio_resync = 0;
       p_audio_stop(gpp);
  }

}  /* end on_audio_enable_checkbutton_toggled */


/* -----------------------------
 * on_audio_volume_spinbutton_changed
 * -----------------------------
 */
static void
on_audio_volume_spinbutton_changed             (GtkEditable     *editable,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  if(gpp->audio_volume != (gdouble)GTK_ADJUSTMENT(gpp->audio_volume_spinbutton_adj)->value)
  {
    gpp->audio_volume = (gdouble)GTK_ADJUSTMENT(gpp->audio_volume_spinbutton_adj)->value;

#ifdef GAP_ENABLE_AUDIO_SUPPORT
    if(gpp->audio_status >= GAP_PLAYER_MAIN_AUSTAT_PLAYING)
    {
      apcl_volume(gpp->audio_volume, 0, p_audio_errfunc);
    }
#endif
  }

}  /* end on_audio_volume_spinbutton_changed */


/* -----------------------------------------
 * on_audio_frame_offset_spinbutton_changed
 * -----------------------------------------
 */
static void
on_audio_frame_offset_spinbutton_changed (GtkEditable     *editable,
                                        gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  if(gpp->audio_frame_offset != (gint32)GTK_ADJUSTMENT(gpp->audio_frame_offset_spinbutton_adj)->value)
  {
    gpp->audio_frame_offset = (gint32)GTK_ADJUSTMENT(gpp->audio_frame_offset_spinbutton_adj)->value;
    /* resync audio will cause an automatic restart respecting te new audio_offset value
     */
    p_audio_resync(gpp);
    p_audio_print_labels(gpp);
  }

}  /* end on_audio_frame_offset_spinbutton_changed */

/* -----------------------------------------
 * on_audio_reset_button_clicked
 * -----------------------------------------
 */
static void
on_audio_reset_button_clicked (GtkButton       *button,
                               gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  gpp->audio_frame_offset = 0;
  gpp->audio_volume = 1.0;
  gtk_adjustment_set_value( GTK_ADJUSTMENT(gpp->audio_frame_offset_spinbutton_adj)
                            , (gfloat)gpp->audio_frame_offset
                            );
  gtk_adjustment_set_value( GTK_ADJUSTMENT(gpp->audio_volume_spinbutton_adj)
                            , (gfloat)gpp->audio_volume
                            );

  /* resync audio (for the case its already playing this
   * will cause an automatic restart respecting te new audio_offset value
   */
  p_audio_resync(gpp);
  p_audio_print_labels(gpp);
}  /* end on_audio_reset_button_clicked */


/* -----------------------------------------
 * on_audio_create_copy_button_clicked
 * -----------------------------------------
 */
static void
on_audio_create_copy_button_clicked (GtkButton       *button,
                               gpointer         user_data)
{
#ifdef GAP_ENABLE_AUDIO_SUPPORT
  GapPlayerMainGlobalParams *gpp;
  const char *cp;
  char  *envAUDIOCONVERT_TO_WAV;
  gboolean script_found;
  gboolean use_newly_created_wavfile;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }
  if(gpp->audio_tmp_dialog_is_open)
  {
    return;
  }

  script_found = FALSE;
  use_newly_created_wavfile = FALSE;
  envAUDIOCONVERT_TO_WAV = g_strdup(" ");
  /* check gimprc for the audioconvert_program */
  if ( (cp = gimp_gimprc_query("audioconvert_program")) != NULL )
  {
    g_free(envAUDIOCONVERT_TO_WAV);
    envAUDIOCONVERT_TO_WAV = g_strdup(cp);
    if(g_file_test (envAUDIOCONVERT_TO_WAV, G_FILE_TEST_IS_EXECUTABLE) )
    {
      script_found = TRUE;
    }
    else
    {
      g_message(_("WARNING: Your gimprc file configuration for the audioconverter script\n"
             "does not point to an executable program\n"
	     "the configured value for %s is: %s\n")
	     , "audioconvert_program"
	     , envAUDIOCONVERT_TO_WAV
	     );
    }
  }
  
  /* check environment variable for the audioconvert_program */
  if(!script_found)
  {
    if ( (cp = g_getenv("AUDIOCONVERT_TO_WAV")) != NULL )
    {
      g_free(envAUDIOCONVERT_TO_WAV);
      envAUDIOCONVERT_TO_WAV = g_strdup(cp);		/* Environment overrides compiled in default for WAVPLAYPATH */
      if(g_file_test (env_WAVPLAYPATH, G_FILE_TEST_IS_EXECUTABLE) )
      {
	script_found = TRUE;
      }
      else
      {
	g_message(_("WARNING: The environment variable %s\n"
               "does not point to an executable program\n"
	       "the current value is: %s\n")
	       , "AUDIOCONVERT_TO_WAV"
	       , envAUDIOCONVERT_TO_WAV
	       );
      }
    }
  }
  
  if(!script_found)
  {
      g_free(envAUDIOCONVERT_TO_WAV);
      envAUDIOCONVERT_TO_WAV = g_build_filename(GAPLIBDIR
					      , "audioconvert_to_wav.sh"
					      , NULL
					      );
      if(!g_file_test(envAUDIOCONVERT_TO_WAV, G_FILE_TEST_IS_EXECUTABLE))
      {
        g_message(_("ERROR: The external program for audioconversion is not executable.\n"
	            "Filename: '%s'\n")
	         , envAUDIOCONVERT_TO_WAV
		 );
        return;
      }
      script_found = TRUE;
  }

  gpp->audio_tmp_dialog_is_open  = TRUE;
  if(p_create_wav_dialog(gpp))
  {
      gint    l_rc;
      gchar  *l_cmd;
      gchar  *l_resample_params;
     
      printf("CREATE WAVFILE as %s\n"
           "  in progress ***\n"
	   ,gpp->audio_wavfile_tmp );
      
      gtk_label_set_text ( GTK_LABEL(gpp->audio_status_label), _("Creating audiofile - please wait"));
      gtk_widget_hide(gpp->audio_table);
      gtk_widget_show(gpp->audio_status_label);
      while (gtk_events_pending ())
      {
        gtk_main_iteration ();
      }	

      if(gpp->audio_tmp_resample)
      {
        l_resample_params = g_strdup_printf("--resample %d"
                                           , (int)gpp->audio_tmp_samplerate
                                           );
      }
      else
      {
        l_resample_params = g_strdup(" ");  /* do not resample */
      }

      l_cmd = g_strdup_printf("%s --in \"%s\" --out \"%s\" %s"
                             , envAUDIOCONVERT_TO_WAV
                             , gpp->audio_filename
                             , gpp->audio_wavfile_tmp
                             , l_resample_params 
                             );
      /* CALL the external audioconverter Program */
      l_rc =  system(l_cmd);
      g_free(l_cmd);
      g_free(l_resample_params);
      
      /* if the external converter created the wavfile
       * then use the newly created wavfile
       */
      if(g_file_test(gpp->audio_wavfile_tmp, G_FILE_TEST_EXISTS))
      {
        use_newly_created_wavfile = TRUE;
      }
  }

  g_free(envAUDIOCONVERT_TO_WAV);
  gpp->audio_tmp_dialog_is_open = FALSE; 
  gtk_label_set_text ( GTK_LABEL(gpp->audio_status_label), " ");
  gtk_widget_hide(gpp->audio_status_label);
  gtk_widget_show(gpp->audio_table);

  if(use_newly_created_wavfile)
  {
    gtk_entry_set_text(GTK_ENTRY(gpp->audio_filename_entry), gpp->audio_wavfile_tmp);
  }

#endif
return;
}  /* end on_audio_create_copy_button_clicked */

/* -----------------------------------------
 * on_audio_filename_entry_changed
 * -----------------------------------------
 */
static void
on_audio_filename_entry_changed (GtkWidget     *widget,
                                 gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }
 
  g_snprintf(gpp->audio_filename, sizeof(gpp->audio_filename), "%s"
            , gtk_entry_get_text(GTK_ENTRY(gpp->audio_filename_entry))
	     );

  p_audio_filename_changed(gpp);
}  /* end on_audio_filename_entry_changed */


/* --------------------------
 * AUDIO_FILESEL
 * --------------------------
 */
static void
on_audio_filesel_close_cb(GtkWidget *widget, gpointer user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }
  if(gpp->audio_filesel == NULL)
  {
    return;  /* filesel is already closed */
  }

  gtk_widget_destroy(GTK_WIDGET(gpp->audio_filesel));
  gpp->audio_filesel = NULL;
}  /* end on_audio_filesel_close_cb */

static void
on_audio_filesel_ok_cb(GtkWidget *widget, gpointer user_data)
{
  const gchar *filename;
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }

  if(gpp->audio_filesel == NULL)
  {
    return;  /* filesel is already closed */
  }

  filename = gtk_file_selection_get_filename (GTK_FILE_SELECTION (gpp->audio_filesel));
  if(filename)
  {
    if(*filename != '\0')
    {
      gtk_entry_set_text(GTK_ENTRY(gpp->audio_filename_entry), filename);
    }
  }
  
  on_audio_filesel_close_cb(widget, gpp);
}  /* end on_audio_filesel_ok_cb */


/* -----------------------------------------
 * on_audio_filesel_button_clicked
 * -----------------------------------------
 */
static void
on_audio_filesel_button_clicked (GtkButton       *button,
                               gpointer         user_data)
{
  GapPlayerMainGlobalParams *gpp;

  gpp = (GapPlayerMainGlobalParams*)user_data;
  if(gpp == NULL)
  {
    return;
  }
  if(gpp->audio_filesel)
  {
    return;  /* filesection dialog is already open */
  }

  gpp->audio_filesel = gtk_file_selection_new (_("Select Audiofile"));

  gtk_window_set_position (GTK_WINDOW (gpp->audio_filesel), GTK_WIN_POS_MOUSE);

  gtk_file_selection_set_filename (GTK_FILE_SELECTION (gpp->audio_filesel),
				   gpp->audio_filename);
  gtk_widget_show (gpp->audio_filesel);

  g_signal_connect (G_OBJECT (gpp->audio_filesel), "destroy",
		    G_CALLBACK (on_audio_filesel_close_cb),
		    gpp);

  g_signal_connect (G_OBJECT (GTK_FILE_SELECTION (gpp->audio_filesel)->ok_button),
		   "clicked",
                    G_CALLBACK (on_audio_filesel_ok_cb),
		    gpp);
  g_signal_connect (G_OBJECT (GTK_FILE_SELECTION (gpp->audio_filesel)->cancel_button),
		   "clicked",
                    G_CALLBACK (on_audio_filesel_close_cb),
		    gpp);

}  /* end on_audio_filesel_button_clicked */



/* -----------------------------
 * p_new_audioframe
 * -----------------------------
 * create widgets for the audio options
 */
static GtkWidget *
p_new_audioframe(GapPlayerMainGlobalParams *gpp)
{
  GtkWidget *frame0a;
  GtkWidget *hseparator;
  GtkWidget *label;
  GtkWidget *vbox1;
  GtkWidget *table1;
  GtkWidget *entry;
  GtkWidget *button;
  GtkWidget *check_button;
  GtkWidget *spinbutton;
  GtkObject *adj;
  gint       row;
   
  if (gap_debug) printf("p_new_audioframe\n");
  
  frame0a = gtk_frame_new ("Audio Playback Settings");

  /* the vbox */
  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox1);
  gtk_container_add (GTK_CONTAINER (frame0a), vbox1);

  /* table */
  table1 = gtk_table_new (14, 3, FALSE);
  gpp->audio_table = table1;
  gtk_widget_show (table1);
  gtk_box_pack_start (GTK_BOX (vbox1), table1, TRUE, TRUE, 0);
  gtk_table_set_row_spacings (GTK_TABLE (table1), 4);
  gtk_table_set_col_spacings (GTK_TABLE (table1), 4);

  /* status label */
  label = gtk_label_new(" ");
  gpp->audio_status_label = label;
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_box_pack_start (GTK_BOX (vbox1), label, TRUE, TRUE, 0);
  gtk_widget_hide(label);

  row = 0;
  
  /* audiofile label */
  label = gtk_label_new (_("Audiofile:"));
  gtk_widget_show (label);
  gtk_table_attach (GTK_TABLE (table1), label, 0, 1,  row, row+1,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);     /* right alligned */
  /* gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5); */ /* left alligned */

  /* audiofile entry */
  entry = gtk_entry_new();
  gpp->audio_filename_entry = entry;
  gtk_widget_show (entry);
  gimp_help_set_help_data(entry, _("Enter an audiofile. The file must be in RIFF WAVE fileformat."),NULL);
  gtk_widget_set_size_request(entry, 300, -1);
  gtk_entry_set_text(GTK_ENTRY(entry), gpp->audio_filename);
  gtk_table_attach(GTK_TABLE(table1), entry, 1, 2, row, row + 1,
                    (GtkAttachOptions) GTK_FILL | GTK_EXPAND | GTK_SHRINK, 
		    (GtkAttachOptions) GTK_FILL, 4, 0);
  g_signal_connect(G_OBJECT(entry), "changed",
		     G_CALLBACK (on_audio_filename_entry_changed),
		     gpp);


  /* audiofile button (fileselect invoker) */
  button = gtk_button_new_with_label ( _("File Browser"));
  gtk_widget_show (button);
  gimp_help_set_help_data(button, _("Open audiofile selection browser dialog window"),NULL);
  gtk_table_attach(GTK_TABLE(table1), button, 2, 3, row, row + 1,
                    (GtkAttachOptions) GTK_FILL, 
		    (GtkAttachOptions) GTK_FILL, 4, 0);
  g_signal_connect (G_OBJECT (button), "pressed",
                      G_CALLBACK (on_audio_filesel_button_clicked),
                      gpp);

  row++;


  /* Volume */
  label = gtk_label_new(_("Volume:"));
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  /* volume spinutton */
  spinbutton = gimp_spin_button_new (&adj,  /* return value */
		      gpp->audio_volume,     /*   initial_val */
		      0.0,   /* umin */
		      1.0,   /* umax */
		      0.01,  /* sstep */
		      0.1,   /* pagestep */
		      0.1,                 /* page_size */
		      0.1,                 /* climb_rate */
		      2                    /* digits */
                      );
  gtk_widget_show (spinbutton);
  /*gtk_widget_set_sensitive(spinbutton, FALSE);*/  /* VOLUME CONTROL NOT IMPLEMENTED YET !!! */
  gpp->audio_volume_spinbutton_adj = adj;
  gtk_table_attach(GTK_TABLE(table1), spinbutton, 1, 2, row, row + 1, GTK_FILL, GTK_FILL, 4, 0);
  gimp_help_set_help_data(spinbutton, _("Audio Volume"),NULL);
  g_signal_connect (G_OBJECT (spinbutton), "changed",
                      G_CALLBACK (on_audio_volume_spinbutton_changed),
                      gpp);
 
  /* check button */
  check_button = gtk_check_button_new_with_label (_("Enable"));
  gtk_table_attach ( GTK_TABLE (table1), check_button, 2, 3, row, row+1, GTK_FILL, 0, 0, 0);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check_button),
				gpp->audio_enable);
  gimp_help_set_help_data(check_button, _("ON: Play button plays video + audio.\n"
                                          "OFF: Play video silently"),NULL);
  gtk_widget_show (check_button);
  g_signal_connect (G_OBJECT (check_button), "toggled",
                      G_CALLBACK (on_audio_enable_checkbutton_toggled),
                      gpp);

  row++;

  /* Sample Offset */
  label = gtk_label_new(_("Offset:"));
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  /* offset spinutton */
  spinbutton = gimp_spin_button_new (&adj,  /* return value */
		      gpp->audio_frame_offset,    /*   initial_val */
		     -500000,   /* umin */
		      500000,   /* umax */
		      1.0,  /* sstep */
		      100,   /* pagestep */
		      100,                 /* page_size */
		      1,                   /* climb_rate */
		      0                    /* digits */
                      );
  gtk_widget_show (spinbutton);
  gpp->audio_frame_offset_spinbutton_adj = adj;
  gtk_table_attach(GTK_TABLE(table1), spinbutton, 1, 2, row, row + 1, GTK_FILL, GTK_FILL, 4, 0);
  gimp_help_set_help_data(spinbutton
                         , _("Audio offset in frames at original video playback speed. "
                             "A value of 0 starts audio and video at synchron time. "
                             "A value of -10 will play frame 1 up to frame 9 silently "
                             "and start audio at frame 10. "
			     "A value of 10 starts audio at frame 1, "
			     "but skips the audio begin part in a length that is "
			     "equal to the duration of 10 frames "
			     "at original video playback speed.")
			 ,NULL);
  g_signal_connect (G_OBJECT (spinbutton), "changed",
                      G_CALLBACK (on_audio_frame_offset_spinbutton_changed),
                      gpp);

  /* reset button */
  button = gtk_button_new_from_stock (GIMP_STOCK_RESET);
  gtk_widget_show (button);
  gimp_help_set_help_data(button, _("Reset offset and volume"),NULL);
  gtk_table_attach(GTK_TABLE(table1), button, 2, 3, row, row + 1,
                    (GtkAttachOptions) GTK_FILL, 
		    (GtkAttachOptions) GTK_FILL, 4, 0);
  g_signal_connect (G_OBJECT (button), "pressed",
                      G_CALLBACK (on_audio_reset_button_clicked),
                      gpp);

  row++;

  /* create wavfile button */
  button = gtk_button_new_with_label(_("Copy As Wavfile"));
  gtk_widget_show (button);
  gimp_help_set_help_data(button, _("Create a copy from audiofile as RIFF WAVE audiofile "
                                    "and use the copy for audio playback"),NULL);
  gtk_table_attach(GTK_TABLE(table1), button, 1, 3, row, row + 1,
                    (GtkAttachOptions) GTK_FILL, 
		    (GtkAttachOptions) GTK_FILL, 4, 0);
  g_signal_connect (G_OBJECT (button), "pressed",
                      G_CALLBACK (on_audio_create_copy_button_clicked),
                      gpp);

  row++;

  hseparator = gtk_hseparator_new ();
  gtk_widget_show (hseparator);
  gtk_table_attach(GTK_TABLE(table1), hseparator, 0, 3, row, row + 1,
                    (GtkAttachOptions) GTK_FILL, 
		    (GtkAttachOptions) GTK_FILL,
		     0, 0);

  row++;

  /* Audio Offset Length (mm:ss:msec) */
  label = gtk_label_new(_("Offsettime:"));
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  label = gtk_label_new("mm:ss:msec");
  gpp->audio_offset_time_label = label;
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  row++;

  /* Total Audio Length (mm:ss:msec) */
  label = gtk_label_new(_("Audiotime:"));
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  label = gtk_label_new("mm:ss:msec");
  gpp->audio_total_time_label = label;
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);


  row++;

  /* Length (frames) */
  label = gtk_label_new(_("Audioframes:"));
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  label = gtk_label_new("####");
  gpp->audio_total_frames_label = label;
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);


  row++;

  /* Audiolength (Samples) */
  label = gtk_label_new(_("Samples:"));
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  label = gtk_label_new("######");
  gpp->audio_samples_label = label;
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);


  row++;

  /* Audio Samplerate */
  label = gtk_label_new(_("Samplerate:"));
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  label = gtk_label_new("######");
  gpp->audio_samplerate_label = label;
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  row++;

  /* Audio Channels */
  label = gtk_label_new(_("Channels:"));
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  label = gtk_label_new("#");
  gpp->audio_channels_label = label;
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  row++;

  /* Bits per Audio Sample */
  label = gtk_label_new(_("Bits/Sample:"));
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  label = gtk_label_new("##");
  gpp->audio_bits_label = label;
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);


  row++;

  /* Total Video Length (mm:ss:msec) */
  label = gtk_label_new(_("Videotime:"));
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  label = gtk_label_new("mm:ss:msec");
  gpp->video_total_time_label = label;
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);


  row++;

  /* Video Length (frames) */
  label = gtk_label_new(_("Videoframes:"));
  gtk_misc_set_alignment(GTK_MISC(label), 1.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 0, 1, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  label = gtk_label_new("000000");
  gpp->video_total_frames_label = label;
  gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
  gtk_table_attach(GTK_TABLE(table1), label, 1, 2, row, row + 1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show(label);

  return(frame0a); 
}  /* end p_new_audioframe */




/* -----------------------------
 * p_create_player_window
 * -----------------------------
 */
GtkWidget*
p_create_player_window (GapPlayerMainGlobalParams *gpp)
{
  GtkWidget *shell_window;
  GtkWidget *event_box;
  GtkWidget *notebook;
  
  GtkWidget *frame0;
  GtkWidget *frame0a;
  GtkWidget *aspect_frame;
  GtkWidget *frame2;

  GtkWidget *vbox0;
  GtkWidget *vbox1;

  GtkWidget *hbox1;
  GtkWidget *gobutton_hbox;

  GtkWidget *table1;
  GtkWidget *table11;
  GtkWidget *table2;

  GtkWidget *vid_preview;

  GtkWidget *status_label;
  GtkWidget *timepos_label;
  GtkWidget *label;
  GtkWidget *label_vid;
  GtkWidget *label_aud;

  GtkWidget *hseparator;

  GtkObject *from_spinbutton_adj;
  GtkWidget *from_spinbutton;
  GtkObject *to_spinbutton_adj;
  GtkWidget *to_spinbutton;
  GtkObject *framenr_spinbutton_adj;
  GtkWidget *framenr_spinbutton;
  GtkObject *speed_spinbutton_adj;
  GtkWidget *speed_spinbutton;
  GtkObject *size_spinbutton_adj;
  GtkWidget *size_spinbutton;


  GtkWidget *play_button;
  GtkWidget *pause_button;
  GtkWidget *back_button;
  GtkWidget *close_button;
  GtkWidget *origspeed_button;
  GtkWidget *size_button;
  GtkWidget *framenr_button;

  GtkWidget *use_thumb_checkbutton;
  GtkWidget *exact_timing_checkbutton;
  GtkWidget *pinpong_checkbutton;
  GtkWidget *selonly_checkbutton;
  GtkWidget *loop_checkbutton;


  shell_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gpp->shell_window = shell_window;
  gtk_window_set_title (GTK_WINDOW (shell_window), _("Videoframe Playback"));
  gtk_window_set_resizable(GTK_WINDOW (shell_window), TRUE);
  g_signal_connect (G_OBJECT (shell_window), "destroy",
                      G_CALLBACK (on_shell_window_destroy),
                      gpp);


  vbox0 = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox0);
  gtk_container_add (GTK_CONTAINER (shell_window), vbox0);

 

  frame0 = gtk_frame_new (gpp->ainfo_ptr->basename);
  gtk_widget_show (frame0);

#ifdef GAP_ENABLE_AUDIO_SUPPORT
  frame0a = p_new_audioframe (gpp);
  gtk_widget_show (frame0a);
  
  label_vid = gtk_label_new (_("Video Options"));
  label_aud = gtk_label_new (_("Audio Options"));
  gtk_widget_show (label_vid);
  gtk_widget_show (label_aud);

  notebook = gtk_notebook_new();
  gtk_widget_show (notebook);

  gtk_container_add (GTK_CONTAINER (notebook), frame0);
  gtk_notebook_set_tab_label (GTK_NOTEBOOK (notebook)
                             , gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), 0)
			     , label_vid
			     );
  gtk_container_add (GTK_CONTAINER (notebook), frame0a);
  gtk_notebook_set_tab_label (GTK_NOTEBOOK (notebook)
                             , gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook), 1)
			     , label_aud
			     );
  
  gtk_box_pack_start (GTK_BOX (vbox0), notebook, TRUE, TRUE, 0);

#else
  gtk_box_pack_start (GTK_BOX (vbox0), frame0, TRUE, TRUE, 0);
#endif


  vbox1 = gtk_vbox_new (FALSE, 0);
  gtk_widget_show (vbox1);
  gtk_container_add (GTK_CONTAINER (frame0), vbox1);


  /* the hbox for the go button array */
  gobutton_hbox = gtk_hbox_new (TRUE, 0);
  gtk_widget_show (gobutton_hbox);

  if(gobutton_hbox)
  {
    gint go_number;
    GtkWidget *go_button;
    t_gobutton *gob;

    /* the gobutton array is be filled with [n] gobuttons */
    
    for(go_number=0; go_number < GAP_PLAY_MAX_GOBUTTONS; go_number++)
    {
       /* the go_button[s] */
       gob = g_malloc0(sizeof(t_gobutton));
       gob->gpp = gpp;
       gob->go_number = go_number;
       
       go_button = gtk_button_new ();
       gtk_widget_show (go_button);

       gtk_widget_set_events(go_button, GDK_ENTER_NOTIFY_MASK | GDK_BUTTON_PRESS_MASK);
       gtk_box_pack_start (GTK_BOX (gobutton_hbox), go_button, FALSE, TRUE, 0);
       gtk_widget_set_size_request (go_button, -1, 40);
       if(go_number == 0)
       {
         gimp_help_set_help_data (go_button, _("Click: go to frame, Ctrl-Click: set 'From Frame', Alt-Click: set 'To Frame'"), NULL);
       }
       g_signal_connect (go_button, "enter_notify_event",
                      G_CALLBACK (on_go_button_enter)
                      ,gob);
       g_signal_connect (G_OBJECT (go_button), "button_press_event",
                      G_CALLBACK (on_go_button_clicked),
                      gob);
    }
  }

  /* Create an EventBox and for the gobutton_hbox leave Event */
  event_box = gtk_event_box_new ();
  gtk_widget_show (event_box);
  gtk_container_add (GTK_CONTAINER (event_box), gobutton_hbox);
  gtk_widget_set_events(event_box
                       ,  GDK_LEAVE_NOTIFY_MASK /* | gtk_widget_get_events (event_box) */
                       );

  gtk_box_pack_start (GTK_BOX (vbox1), event_box, FALSE, FALSE, 0);
  g_signal_connect (event_box, "leave_notify_event",
                      G_CALLBACK (on_gobutton_hbox_leave)
                      ,gpp);



  table1 = gtk_table_new (2, 2, FALSE);
  gtk_widget_show (table1);
  gtk_box_pack_start (GTK_BOX (vbox1), table1, TRUE, TRUE, 0);



  /* the frame2 for range and playback mode control widgets */
  frame2 = gtk_frame_new (NULL);
  gtk_widget_show (frame2);
  gtk_table_attach (GTK_TABLE (table1), frame2, 1, 2, 0, 1
                   , (GtkAttachOptions) (0)
                   , (GtkAttachOptions) (0)
		   , 0, 0);

  /* table2 for range and playback mode control widgets */
  table2 = gtk_table_new (17, 2, FALSE);
  gtk_widget_show (table2);
  gtk_container_add (GTK_CONTAINER (frame2), table2);

  label = gtk_label_new (_("From Frame:"));
  gtk_widget_show (label);
  gtk_table_attach (GTK_TABLE (table2), label, 0, 1, 0, 1
                   , (GtkAttachOptions) (GTK_FILL)
                   , (GtkAttachOptions) (0)
		   , 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);

  label = gtk_label_new (_("To Frame:"));
  gtk_widget_show (label);
  gtk_table_attach (GTK_TABLE (table2), label, 0, 1, 1, 2
                   , (GtkAttachOptions) (GTK_FILL)
                   , (GtkAttachOptions) (0)
		   , 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);

  /* the FROM spinbutton (start of rangeselection)  */
  from_spinbutton_adj = gtk_adjustment_new ( gpp->begin_frame
                                           , gpp->ainfo_ptr->first_frame_nr
                                           , gpp->ainfo_ptr->last_frame_nr
                                           , 1, 10, 10);
  from_spinbutton = gtk_spin_button_new (GTK_ADJUSTMENT (from_spinbutton_adj), 1, 0);
  gtk_widget_show (from_spinbutton);
  gtk_table_attach (GTK_TABLE (table2), from_spinbutton, 1, 2, 0, 1,
                    (GtkAttachOptions) (0),
                    (GtkAttachOptions) (0),
		     0, 0);
  gtk_widget_set_size_request (from_spinbutton, 80, -1);
  gimp_help_set_help_data (from_spinbutton, _("Start framenumber of selection range"), NULL);
  g_signal_connect (G_OBJECT (from_spinbutton), "changed",
                      G_CALLBACK (on_from_spinbutton_changed),
                      gpp);

  /* the TO spinbutton (end of rangeselection)  */
  to_spinbutton_adj = gtk_adjustment_new ( gpp->end_frame
                                           , gpp->ainfo_ptr->first_frame_nr
                                           , gpp->ainfo_ptr->last_frame_nr
                                           , 1, 10, 10);
  to_spinbutton = gtk_spin_button_new (GTK_ADJUSTMENT (to_spinbutton_adj), 1, 0);
  gtk_widget_show (to_spinbutton);
  gtk_table_attach (GTK_TABLE (table2), to_spinbutton, 1, 2, 1, 2,
                    (GtkAttachOptions) (0),
                    (GtkAttachOptions) (0),
		     0, 0);
  gtk_widget_set_size_request (to_spinbutton, 80, -1);
  gimp_help_set_help_data (to_spinbutton, _("End framenumber of selection range"), NULL);
  g_signal_connect (G_OBJECT (to_spinbutton), "changed",
                      G_CALLBACK (on_to_spinbutton_changed),
                      gpp);

  hseparator = gtk_hseparator_new ();
  gtk_widget_show (hseparator);
  gtk_table_attach (GTK_TABLE (table2), hseparator, 0, 2, 2, 3,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (GTK_FILL),
		     0, 0);


  /* the framenr button */
  framenr_button = gtk_button_new_with_label (_("FrameNr"));
  gtk_widget_show (framenr_button);
  gtk_table_attach (GTK_TABLE (table2), framenr_button, 0, 1, 3, 4,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gimp_help_set_help_data (framenr_button, _("Load this frame into the calling image"), NULL);
  g_signal_connect (G_OBJECT (framenr_button), "clicked",
                      G_CALLBACK (on_framenr_button_clicked),
                      gpp);


  /* the FRAMENR spinbutton (current displayed frame)  */
  framenr_spinbutton_adj = gtk_adjustment_new ( gpp->play_current_framenr
                                           , gpp->ainfo_ptr->first_frame_nr
                                           , gpp->ainfo_ptr->last_frame_nr
                                           , 1, 10, 10);

  framenr_spinbutton = gtk_spin_button_new (GTK_ADJUSTMENT (framenr_spinbutton_adj), 1, 0);
  gtk_widget_show (framenr_spinbutton);
  gtk_table_attach (GTK_TABLE (table2), framenr_spinbutton, 1, 2, 3, 4,
                    (GtkAttachOptions) (0),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_widget_set_size_request (framenr_spinbutton, 80, -1);
  gimp_help_set_help_data (framenr_spinbutton, _("The currently displayed frame number"), NULL);
  g_signal_connect (G_OBJECT (framenr_spinbutton), "changed",
                      G_CALLBACK (on_framenr_spinbutton_changed),
                      gpp);


  /* the time position */
  label = gtk_label_new (_("Timepos:"));
  gtk_widget_show (label);
  gtk_table_attach (GTK_TABLE (table2), label, 0, 1, 4, 5,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);

  /* the timepos label */
  /* (had used an entry here before but had update performance problems
   *  beginning at playback speed of 17 frames/sec on PII 300 Mhz)
   */
  timepos_label = gtk_label_new ("00:00:000");
  gtk_widget_show (timepos_label);
  gtk_table_attach (GTK_TABLE (table2), timepos_label, 1, 2, 4, 5,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0),
		     0, 0);




  hseparator = gtk_hseparator_new ();
  gtk_widget_show (hseparator);
  gtk_table_attach (GTK_TABLE (table2), hseparator, 0, 2, 5, 6,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (GTK_FILL),
		    0, 0);



  /* the origspeed_button */
  origspeed_button = gtk_button_new_with_label (_("Speed"));
  gtk_widget_show (origspeed_button);
  gtk_table_attach (GTK_TABLE (table2), origspeed_button, 0, 1, 6, 7,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gimp_help_set_help_data (origspeed_button, _("Reset playback speed to original (or previous) value"), NULL);
  g_signal_connect (G_OBJECT (origspeed_button), "clicked",
                      G_CALLBACK (on_origspeed_button_clicked),
                      gpp);

  /* the SPEED spinbutton
   * with the given timer resolution of millisecs the theoretical
   * maximum speed is 1000 frames/sec that would result in 1 timertick
   * this implementation allows a max speed of 250 frames/sec (4 timerticks)
   */
  speed_spinbutton_adj = gtk_adjustment_new ( gpp->speed
                                           , 1.0
                                           , 250.0
                                           , 1, 10, 10);
  speed_spinbutton = gtk_spin_button_new (GTK_ADJUSTMENT (speed_spinbutton_adj), 1, 4);
  gtk_widget_show (speed_spinbutton);
  gtk_table_attach (GTK_TABLE (table2), speed_spinbutton, 1, 2, 6, 7,
                    (GtkAttachOptions) (0),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_widget_set_size_request (speed_spinbutton, 80, -1);
  gimp_help_set_help_data (speed_spinbutton, _("Current playback speed (frames/sec)"), NULL);

  hseparator = gtk_hseparator_new ();
  gtk_widget_show (hseparator);
  gtk_table_attach (GTK_TABLE (table2), hseparator, 0, 2, 7, 8,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (GTK_FILL),
		    0, 0);
  g_signal_connect (G_OBJECT (speed_spinbutton), "changed",
                      G_CALLBACK (on_speed_spinbutton_changed),
                      gpp);


  /* the size button */
  size_button = gtk_button_new_with_label (_("Size"));
  gtk_widget_show (size_button);
  gtk_widget_set_events(size_button, GDK_BUTTON_PRESS_MASK);
  gtk_table_attach (GTK_TABLE (table2), size_button, 0, 1, 8, 9,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0),
		    0, 0);
  gimp_help_set_help_data (size_button, _("Toggle size 128/256. <Shift> Set 1:1 full image size"), NULL);
  g_signal_connect (G_OBJECT (size_button), "button_press_event",
                      G_CALLBACK (on_size_button_button_press_event),
                      gpp);

  /* the SIZE spinbutton */
  size_spinbutton_adj = gtk_adjustment_new (gpp->pv_pixelsize
                                           , GAP_PLAYER_MIN_SIZE
                                           , GAP_PLAYER_MAX_SIZE
                                           , 1, 10, 10);

  size_spinbutton = gtk_spin_button_new (GTK_ADJUSTMENT (size_spinbutton_adj), 1, 0);
  gpp->size_spinbutton = size_spinbutton;
  gtk_widget_show (size_spinbutton);
  gtk_table_attach (GTK_TABLE (table2), size_spinbutton, 1, 2, 8, 9,
                    (GtkAttachOptions) (0),
                    (GtkAttachOptions) (0),
		    0, 0);
  gtk_widget_set_size_request (size_spinbutton, 80, -1);
  gimp_help_set_help_data (size_spinbutton, _("Video preview size (pixels)"), NULL);

  g_signal_connect (G_OBJECT (size_spinbutton), "value_changed",
                      G_CALLBACK (on_size_spinbutton_changed),
                      gpp);
  gtk_widget_set_events(size_spinbutton
                       ,  GDK_ENTER_NOTIFY_MASK
                       );
  g_signal_connect (G_OBJECT (size_spinbutton), "enter_notify_event",
                      G_CALLBACK (on_size_spinbutton_enter),
                      gpp);


  hseparator = gtk_hseparator_new ();
  gtk_widget_show (hseparator);
  gtk_table_attach (GTK_TABLE (table2), hseparator, 0, 2, 9, 10,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (GTK_FILL),
		     0, 0);

  /* the playback mode checkbuttons */

  loop_checkbutton = gtk_check_button_new_with_label (_("Loop"));
  gtk_widget_show (loop_checkbutton);
  gtk_table_attach (GTK_TABLE (table2), loop_checkbutton, 0, 2, 10, 11,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gimp_help_set_help_data (loop_checkbutton, _("ON: Play in endless loop.\n"
                                               "OFF: Play only once"), NULL);
  if(gpp->play_loop)
  {
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (loop_checkbutton), TRUE);
  }
  g_signal_connect (G_OBJECT (loop_checkbutton), "toggled",
                      G_CALLBACK (on_loop_checkbutton_toggled),
                      gpp);

  selonly_checkbutton = gtk_check_button_new_with_label (_("Play selection only"));
  gtk_widget_show (selonly_checkbutton);
  gtk_table_attach (GTK_TABLE (table2), selonly_checkbutton, 0, 2, 11, 12,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gimp_help_set_help_data (selonly_checkbutton, _("ON: Play selection only.\n"
                                                  "OFF: Play all frames"), NULL);
  if(gpp->play_selection_only)
  {
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (selonly_checkbutton), TRUE);
  }
  g_signal_connect (G_OBJECT (selonly_checkbutton), "toggled",
                      G_CALLBACK (on_selonly_checkbutton_toggled),
                      gpp);

  pinpong_checkbutton = gtk_check_button_new_with_label (_("Ping pong"));
  gtk_widget_show (pinpong_checkbutton);
  gtk_table_attach (GTK_TABLE (table2), pinpong_checkbutton, 0, 2, 12, 13,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gimp_help_set_help_data (pinpong_checkbutton, _("ON: Play alternating forward/backward"), NULL);
  if(gpp->play_pingpong)
  {
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pinpong_checkbutton), TRUE);
  }
  g_signal_connect (G_OBJECT (pinpong_checkbutton), "toggled",
                      G_CALLBACK (on_pinpong_checkbutton_toggled),
                      gpp);

  use_thumb_checkbutton = gtk_check_button_new_with_label (_("Use thumbnails"));
  gtk_widget_show (use_thumb_checkbutton);
  gtk_table_attach (GTK_TABLE (table2), use_thumb_checkbutton, 0, 2, 13, 14,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gimp_help_set_help_data (use_thumb_checkbutton, _("ON: Use thumbnails when available.\n"
                                                    "OFF: Read full sized frames"), NULL);
  if(gpp->use_thumbnails)
  {
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (use_thumb_checkbutton), TRUE);
  }
  g_signal_connect (G_OBJECT (use_thumb_checkbutton), "toggled",
                      G_CALLBACK (on_use_thumb_checkbutton_toggled),
                      gpp);


  exact_timing_checkbutton = gtk_check_button_new_with_label (_("Exact timing"));
  gtk_widget_show (exact_timing_checkbutton);
  gtk_table_attach (GTK_TABLE (table2), exact_timing_checkbutton, 0, 2, 14, 15,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gimp_help_set_help_data (exact_timing_checkbutton, _("ON: Skip frames to hold exact timing.\n"
                                                       "OFF: Disable frame skipping"), NULL);
  if(gpp->exact_timing)
  {
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (exact_timing_checkbutton), TRUE);
  }
  g_signal_connect (G_OBJECT (exact_timing_checkbutton), "toggled",
                      G_CALLBACK (on_exact_timing_checkbutton_toggled),
                      gpp);


  /* the status label */
  label = gtk_label_new (_("Status:"));
  gtk_widget_show (label);
  gtk_table_attach (GTK_TABLE (table2), label, 0, 1, 15, 16,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);

  /* the status value label */
  status_label = gtk_label_new (_("Ready"));
  gtk_widget_show (status_label);
  gtk_table_attach (GTK_TABLE (table2), status_label, 1, 2, 15, 16,
                    (GtkAttachOptions) (GTK_FILL),
                    (GtkAttachOptions) (0), 0, 0);
  gtk_misc_set_alignment (GTK_MISC (status_label), 0, 0.5);

  /* a dummy label to fill up table1 until bottom */
  label = gtk_label_new (" ");
  gtk_widget_show (label);
  gtk_table_attach (GTK_TABLE (table1), label, 1, 2, 1, 2,
                    (GtkAttachOptions) (0),
                    (GtkAttachOptions) (GTK_FILL | GTK_SHRINK | GTK_EXPAND), 0, 0);



  /* the playback stock button box */
  hbox1 = gtk_hbox_new (TRUE, 0);
  gtk_widget_show (hbox1);
  gtk_box_pack_start (GTK_BOX (vbox0), hbox1, FALSE, FALSE, 0);

  /* the PLAY button */
  play_button = gtk_button_new_from_stock (GAP_STOCK_PLAY);
  gtk_widget_show (play_button);
  gtk_box_pack_start (GTK_BOX (hbox1), play_button, FALSE, TRUE, 0);
  gimp_help_set_help_data (play_button, _("Start playback"), NULL);
  g_signal_connect (G_OBJECT (play_button), "clicked",
                      G_CALLBACK (on_play_button_clicked),
                      gpp);

  /* the PAUSE button */
  pause_button = gtk_button_new_from_stock (GAP_STOCK_PAUSE);
  gtk_widget_show (pause_button);
  gtk_widget_set_events(pause_button, GDK_BUTTON_PRESS_MASK);
  gtk_box_pack_start (GTK_BOX (hbox1), pause_button, FALSE, TRUE, 0);
  gimp_help_set_help_data (pause_button, _("Pause if playing (any mouseboutton). "
                                           "Go to selection start/active/end (left/middle/right mousebutton) if not playing"), NULL);
  g_signal_connect (G_OBJECT (pause_button), "button_press_event",
                      G_CALLBACK (on_pause_button_press_event),
                      gpp);

  /* the PLAY_REVERSE button */
  back_button = gtk_button_new_from_stock (GAP_STOCK_PLAY_REVERSE);
  gtk_widget_show (back_button);
  gtk_box_pack_start (GTK_BOX (hbox1), back_button, FALSE, TRUE, 0);
  gimp_help_set_help_data (back_button, _("Start reverse playback"), NULL);
  g_signal_connect (G_OBJECT (back_button), "clicked",
                      G_CALLBACK (on_back_button_clicked),
                      gpp);

  /* the CLOSE button */
  close_button = gtk_button_new_from_stock (GTK_STOCK_CLOSE);
  gtk_widget_show (close_button);
  gtk_box_pack_start (GTK_BOX (hbox1), close_button, FALSE, TRUE, 0);
  gimp_help_set_help_data (close_button, _("Close window"), NULL);
  g_signal_connect (G_OBJECT (close_button), "clicked",
                      G_CALLBACK (on_close_button_clicked),
                      gpp);


  /* aspect_frame is the CONTAINER for the video preview */
  aspect_frame = gtk_aspect_frame_new (NULL   /* without label */
                                      , 0.5   /* xalign center */
                                      , 0.5   /* yalign center */
                                      , gpp->ainfo_ptr->width / gpp->ainfo_ptr->height     /* ratio */
                                      , TRUE  /* obey_child */
                                      );
  gtk_widget_show (aspect_frame);

  /* table11 is used to center aspect_frame */
  table11 = gtk_table_new (3, 3, FALSE);
  gtk_widget_show (table11);
  {
    gint ix;
    gint iy;
    GtkWidget *dummy;
    
    for(ix = 0; ix < 3; ix++)
    {
      for(iy = 0; iy < 3; iy++)
      {
        if((ix == 1) && (iy == 1))
        {
           gtk_table_attach (GTK_TABLE (table11), aspect_frame, ix, ix+1, iy, iy+1,
                             (GtkAttachOptions) (0),
                             (GtkAttachOptions) (0), 0, 0);
        }
        else
        {
          /* dummy widgets to fill up table11  */
          dummy = gtk_vbox_new (FALSE,3);
          gtk_widget_show (dummy);
          gtk_table_attach (GTK_TABLE (table11), dummy, ix, ix+1, iy, iy+1,
                            (GtkAttachOptions) (GTK_FILL | GTK_SHRINK | GTK_EXPAND),
                            (GtkAttachOptions) (GTK_FILL | GTK_SHRINK | GTK_EXPAND), 0, 0);
        }
      }
    }    
  
  }
  
  {
    GtkWidget *wrap_frame;
    
    wrap_frame= gtk_frame_new(NULL);
    gtk_container_set_border_width (GTK_CONTAINER (wrap_frame), 0);
    gpp->resize_box = wrap_frame;
    gtk_widget_show(wrap_frame);
    gtk_container_add (GTK_CONTAINER (wrap_frame), table11);
    gtk_table_attach (GTK_TABLE (table1), wrap_frame, 0, 1, 0, 2,
                    (GtkAttachOptions) (GTK_EXPAND | GTK_SHRINK | GTK_FILL),
                    (GtkAttachOptions) (GTK_EXPAND | GTK_SHRINK | GTK_FILL), 0, 0);
  }

  gtk_widget_realize (shell_window);

  /* the preview drawing_area_widget */
  /* ############################### */
  gpp->pv_ptr = gap_pview_new(GAP_SMALL_PREVIEW_SIZE, GAP_SMALL_PREVIEW_SIZE, GAP_PLAYER_CHECK_SIZE, aspect_frame);
  vid_preview = gpp->pv_ptr->da_widget;
  gtk_container_add (GTK_CONTAINER (aspect_frame), vid_preview);
  gtk_widget_show (vid_preview);
  gtk_widget_set_events (vid_preview, GDK_BUTTON_PRESS_MASK | GDK_EXPOSURE_MASK);

  /* gpp copies of objects used outside this procedure  */
  gpp->from_spinbutton_adj = from_spinbutton_adj;
  gpp->to_spinbutton_adj = to_spinbutton_adj;
  gpp->framenr_spinbutton_adj = framenr_spinbutton_adj;
  gpp->speed_spinbutton_adj = speed_spinbutton_adj;
  gpp->size_spinbutton_adj = size_spinbutton_adj;


  gtk_widget_realize (gpp->pv_ptr->da_widget);
  p_update_pviewsize(gpp);
  
  g_signal_connect (G_OBJECT (vid_preview), "button_press_event",
                      G_CALLBACK (on_vid_preview_button_press_event),
                      gpp);
  g_signal_connect (G_OBJECT (vid_preview), "expose_event",
                      G_CALLBACK (on_vid_preview_expose_event),
                      gpp);


  gpp->status_label = status_label;
  gpp->timepos_label = timepos_label;

  return shell_window;
}  /* end p_create_player_window */

/* -----------------------------
 * p_connect_resize_handler
 * -----------------------------
 */

static void
p_connect_resize_handler(GapPlayerMainGlobalParams *gpp)
{
  if(gpp->resize_handler_id < 0)
  {
    gpp->resize_handler_id = g_signal_connect (G_OBJECT (gpp->resize_box), "size_allocate",
                      G_CALLBACK (on_vid_preview_size_allocate),
                      gpp);
  }
}  /* end p_connect_resize_handler */

/* -----------------------------
 * p_disconnect_resize_handler
 * -----------------------------
 */

static void
p_disconnect_resize_handler(GapPlayerMainGlobalParams *gpp)
{
  if(gpp->resize_handler_id >= 0)
  {
    g_signal_handler_disconnect(gpp->resize_box, gpp->resize_handler_id); 
    gpp->resize_handler_id = -1;
  }
}  /* end p_disconnect_resize_handler */

/* -----------------------------
 * gap_player_dlg_playback_dialog
 * -----------------------------
 */
void
gap_player_dlg_playback_dialog(GapPlayerMainGlobalParams *gpp)
{
  gimp_ui_init ("gap_player_dialog", FALSE);
  gap_stock_init();
  p_check_tooltips();

  gpp->startup = TRUE;
  gpp->shell_initial_width = -1;
  gpp->shell_initial_height = -1;
 
  gpp->ainfo_ptr = NULL;
  gpp->original_speed = 24.0;   /* default if framerate is unknown */
  gpp->prev_speed = 24.0;       /* default if framerate is unknown */

  p_reload_ainfo_ptr(gpp, gpp->image_id);
  if(gpp->ainfo_ptr == NULL)
  {
    return;
  }

  if(0 == gap_lib_chk_framerange(gpp->ainfo_ptr))
  {
    gpp->resize_handler_id = -1;
    gpp->play_is_active = FALSE;
    gpp->exact_timing = TRUE;
    gpp->play_timertag = -1;
    gpp->go_timertag = -1;
    gpp->go_base_framenr = -1;
    gpp->go_base = -1;
    gpp->pingpong_count = 0;
    gpp->gtimer = g_timer_new();
    gpp->cycle_time_secs = 0.3;
    gpp->rest_secs = 0.0;
    gpp->framecnt = 0.0;
    gpp->audio_volume = 1.0;
    gpp->audio_resync = 0;
    gpp->audio_frame_offset = 0;
    gpp->audio_filesel = NULL;
    gpp->audio_tmp_dialog_is_open = FALSE;

    if(gpp->autostart)
    {
      gpp->begin_frame = CLAMP(gpp->begin_frame
                              , gpp->ainfo_ptr->first_frame_nr
                              , gpp->ainfo_ptr->last_frame_nr);
      gpp->end_frame = CLAMP(gpp->end_frame
                              , gpp->ainfo_ptr->first_frame_nr
                              , gpp->ainfo_ptr->last_frame_nr);
      gpp->play_current_framenr = CLAMP(gpp->ainfo_ptr->curr_frame_nr
                              , gpp->begin_frame
                              , gpp->end_frame);
    }
    else
    {
      gpp->begin_frame = gpp->ainfo_ptr->curr_frame_nr;
      gpp->end_frame = gpp->ainfo_ptr->last_frame_nr;
      gpp->play_current_framenr = gpp->ainfo_ptr->curr_frame_nr;
    }

    gpp->pb_stepsize = 1;

    /* always startup at original speed */
    gpp->speed = gpp->original_speed;
    gpp->prev_speed = gpp->original_speed;
    
    if((gpp->pv_pixelsize < GAP_PLAYER_MIN_SIZE) || (gpp->pv_pixelsize > GAP_PLAYER_MAX_SIZE))
    {
      gpp->pv_pixelsize = GAP_STANDARD_PREVIEW_SIZE;
    }
    if ((gpp->pv_width < GAP_PLAYER_MIN_SIZE) || (gpp->pv_width > GAP_PLAYER_MAX_SIZE))
    {
      gpp->pv_width = GAP_STANDARD_PREVIEW_SIZE;
    }
    if ((gpp->pv_height < GAP_PLAYER_MIN_SIZE) || (gpp->pv_height > GAP_PLAYER_MAX_SIZE))
    {
      gpp->pv_height = GAP_STANDARD_PREVIEW_SIZE;
    }

    gpp->in_feedback = FALSE;
    gpp->in_timer_playback = FALSE;
    gpp->old_resize_width = 0;
    gpp->old_resize_height = 0;
    p_create_player_window(gpp);

    p_display_frame(gpp, gpp->play_current_framenr);
    gap_pview_repaint(gpp->pv_ptr);
    p_check_tooltips();
    
    p_audio_startup_server(gpp);

    if(gpp->autostart)
    {
       on_play_button_clicked(NULL, gpp);
    }
    gtk_widget_show (gpp->shell_window);
    
    g_signal_connect (G_OBJECT (gpp->shell_window), "size_allocate",
                      G_CALLBACK (on_shell_window_size_allocate),
                      gpp);
        
    g_signal_connect (G_OBJECT (gpp->shell_window), "leave_notify_event",
                      G_CALLBACK (on_shell_window_leave),
                      gpp);
    gpp->startup = FALSE;
    p_connect_resize_handler(gpp);
    
    gtk_main ();
    p_audio_shut_server(gpp);
    
    if(gpp->gtimer)
    {
      g_timer_destroy(gpp->gtimer);
      gpp->gtimer = NULL;
    }

  }


  gap_lib_free_ainfo(&gpp->ainfo_ptr);

}  /* end gap_player_dlg_playback_dialog */
