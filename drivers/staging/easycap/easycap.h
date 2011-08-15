/*****************************************************************************
*                                                                            *
*  easycap.h                                                                 *
*                                                                            *
*****************************************************************************/
/*
 *
 *  Copyright (C) 2010 R.M. Thomas  <rmthomas@sciolus.org>
 *
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*
 *  THE FOLLOWING PARAMETERS ARE UNDEFINED:
 *
 *                EASYCAP_DEBUG
 *
 *  IF REQUIRED THEY MUST BE EXTERNALLY DEFINED, FOR EXAMPLE AS COMPILER
 *  OPTIONS.
 */
/*---------------------------------------------------------------------------*/

#ifndef __EASYCAP_H__
#define __EASYCAP_H__

/*---------------------------------------------------------------------------*/
/*
 *  THESE ARE NORMALLY DEFINED
 */
/*---------------------------------------------------------------------------*/
#define  PATIENCE  500
#define  PERSEVERE
/*---------------------------------------------------------------------------*/
/*
 *  THESE ARE FOR MAINTENANCE ONLY - NORMALLY UNDEFINED:
 */
/*---------------------------------------------------------------------------*/
#undef  EASYCAP_TESTCARD
/*---------------------------------------------------------------------------*/
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/usb.h>
#include <linux/uaccess.h>

#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/types.h>

#include <linux/vmalloc.h>
#include <linux/sound.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/info.h>
#include <sound/initval.h>
#include <sound/control.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <linux/videodev2.h>
#include <linux/soundcard.h>

/*---------------------------------------------------------------------------*/
/*  VENDOR, PRODUCT:  Syntek Semiconductor Co., Ltd
 *
 *      EITHER        EasyCAP USB 2.0 Video Adapter with Audio, Model No. DC60
 *               with input cabling:  AUDIO(L), AUDIO(R), CVBS, S-VIDEO.
 *
 *          OR        EasyCAP 4CHANNEL USB 2.0 DVR, Model No. EasyCAP002
 *               with input cabling:  MICROPHONE, CVBS1, CVBS2, CVBS3, CVBS4.
 */
/*---------------------------------------------------------------------------*/
#define USB_EASYCAP_VENDOR_ID	0x05e1
#define USB_EASYCAP_PRODUCT_ID	0x0408

#define EASYCAP_DRIVER_VERSION "0.9.01"
#define EASYCAP_DRIVER_DESCRIPTION "easycapdc60"

#define USB_SKEL_MINOR_BASE     192
#define DONGLE_MANY 8
#define INPUT_MANY 6
/*---------------------------------------------------------------------------*/
/*
 *  DEFAULT LUMINANCE, CONTRAST, SATURATION AND HUE
 */
/*---------------------------------------------------------------------------*/
#define SAA_0A_DEFAULT 0x7F
#define SAA_0B_DEFAULT 0x3F
#define SAA_0C_DEFAULT 0x2F
#define SAA_0D_DEFAULT 0x00
/*---------------------------------------------------------------------------*/
/*
 *  VIDEO STREAMING PARAMETERS:
 *  USB 2.0 PROVIDES FOR HIGH-BANDWIDTH ENDPOINTS WITH AN UPPER LIMIT
 *  OF 3072 BYTES PER MICROFRAME for wMaxPacketSize.
 */
/*---------------------------------------------------------------------------*/
#define VIDEO_ISOC_BUFFER_MANY 16
#define VIDEO_ISOC_ORDER 3
#define VIDEO_ISOC_FRAMESPERDESC ((unsigned int) 1 << VIDEO_ISOC_ORDER)
#define USB_2_0_MAXPACKETSIZE 3072
#if (USB_2_0_MAXPACKETSIZE > PAGE_SIZE)
#error video_isoc_buffer[.] will not be big enough
#endif
#define VIDEO_JUNK_TOLERATE VIDEO_ISOC_BUFFER_MANY
#define VIDEO_LOST_TOLERATE 50
/*---------------------------------------------------------------------------*/
/*
 *  VIDEO BUFFERS
 */
/*---------------------------------------------------------------------------*/
#define FIELD_BUFFER_SIZE (203 * PAGE_SIZE)
#define FRAME_BUFFER_SIZE (405 * PAGE_SIZE)
#define FIELD_BUFFER_MANY 4
#define FRAME_BUFFER_MANY 6
/*---------------------------------------------------------------------------*/
/*
 *  AUDIO STREAMING PARAMETERS
 */
/*---------------------------------------------------------------------------*/
#define AUDIO_ISOC_BUFFER_MANY 16
#define AUDIO_ISOC_ORDER 1
#define AUDIO_ISOC_FRAMESPERDESC 32
#define AUDIO_ISOC_BUFFER_SIZE (PAGE_SIZE << AUDIO_ISOC_ORDER)
/*---------------------------------------------------------------------------*/
/*
 *  AUDIO BUFFERS
 */
/*---------------------------------------------------------------------------*/
#define AUDIO_FRAGMENT_MANY 32
#define PAGES_PER_AUDIO_FRAGMENT 4
/*---------------------------------------------------------------------------*/
/*
 *  IT IS ESSENTIAL THAT EVEN-NUMBERED STANDARDS ARE 25 FRAMES PER SECOND,
 *                        ODD-NUMBERED STANDARDS ARE 30 FRAMES PER SECOND.
 *  THE NUMBERING OF STANDARDS MUST NOT BE CHANGED WITHOUT DUE CARE.  NOT
 *  ONLY MUST THE PARAMETER
 *                             STANDARD_MANY
 *  BE CHANGED TO CORRESPOND TO THE NEW NUMBER OF STANDARDS, BUT ALSO THE
 *  NUMBERING MUST REMAIN AN UNBROKEN ASCENDING SEQUENCE:  DUMMY STANDARDS
 *  MAY NEED TO BE ADDED.   APPROPRIATE CHANGES WILL ALWAYS BE REQUIRED IN
 *  ROUTINE fillin_formats() AND POSSIBLY ELSEWHERE.  BEWARE.
 */
/*---------------------------------------------------------------------------*/
#define  PAL_BGHIN      0
#define  PAL_Nc         2
#define  SECAM          4
#define  NTSC_N         6
#define  NTSC_N_443     8
#define  NTSC_M         1
#define  NTSC_443       3
#define  NTSC_M_JP      5
#define  PAL_60         7
#define  PAL_M          9
#define  PAL_BGHIN_SLOW    10
#define  PAL_Nc_SLOW       12
#define  SECAM_SLOW        14
#define  NTSC_N_SLOW       16
#define  NTSC_N_443_SLOW   18
#define  NTSC_M_SLOW       11
#define  NTSC_443_SLOW     13
#define  NTSC_M_JP_SLOW    15
#define  PAL_60_SLOW       17
#define  PAL_M_SLOW        19
#define  STANDARD_MANY 20
/*---------------------------------------------------------------------------*/
/*
 *  ENUMS
 */
/*---------------------------------------------------------------------------*/
enum {
	AT_720x576,
	AT_704x576,
	AT_640x480,
	AT_720x480,
	AT_360x288,
	AT_320x240,
	AT_360x240,
	RESOLUTION_MANY
};
enum {
	FMT_UYVY,
	FMT_YUY2,
	FMT_RGB24,
	FMT_RGB32,
	FMT_BGR24,
	FMT_BGR32,
	PIXELFORMAT_MANY
};
enum {
	FIELD_NONE,
	FIELD_INTERLACED,
	INTERLACE_MANY
};
#define SETTINGS_MANY	(STANDARD_MANY * \
			RESOLUTION_MANY * \
			2 * \
			PIXELFORMAT_MANY * \
			INTERLACE_MANY)
/*---------------------------------------------------------------------------*/
/*
 *  STRUCTURE DEFINITIONS
 */
/*---------------------------------------------------------------------------*/
struct easycap_dongle {
	struct easycap *peasycap;
	struct mutex mutex_video;
	struct mutex mutex_audio;
};
/*---------------------------------------------------------------------------*/
struct data_buffer {
	struct list_head list_head;
	void *pgo;
	void *pto;
	u16 kount;
	u16 input;
};
/*---------------------------------------------------------------------------*/
struct data_urb {
	struct list_head list_head;
	struct urb *purb;
	int isbuf;
	int length;
};
/*---------------------------------------------------------------------------*/
struct easycap_standard {
	u16 mask;
struct v4l2_standard v4l2_standard;
};
struct easycap_format {
	u16 mask;
	char name[128];
struct v4l2_format v4l2_format;
};
struct inputset {
	int input;
	int input_ok;
	int standard_offset;
	int standard_offset_ok;
	int format_offset;
	int format_offset_ok;
	int brightness;
	int brightness_ok;
	int contrast;
	int contrast_ok;
	int saturation;
	int saturation_ok;
	int hue;
	int hue_ok;
};
/*---------------------------------------------------------------------------*/
/*
 *   easycap.ilk == 0   =>  CVBS+S-VIDEO HARDWARE, AUDIO wMaxPacketSize=256
 *   easycap.ilk == 2   =>  CVBS+S-VIDEO HARDWARE, AUDIO wMaxPacketSize=9
 *   easycap.ilk == 3   =>     FOUR-CVBS HARDWARE, AUDIO wMaxPacketSize=9
 */
/*---------------------------------------------------------------------------*/
struct easycap {
	int isdongle;
	int minor;

	struct video_device video_device;
	struct v4l2_device v4l2_device;

	int status;
	unsigned int audio_pages_per_fragment;
	unsigned int audio_bytes_per_fragment;
	unsigned int audio_buffer_page_many;

#define UPSAMPLE
#ifdef UPSAMPLE
	s16 oldaudio;
#endif /*UPSAMPLE*/

	int ilk;
	bool microphone;

	struct usb_device *pusb_device;
	struct usb_interface *pusb_interface;

	struct kref kref;

	int queued[FRAME_BUFFER_MANY];
	int done[FRAME_BUFFER_MANY];

	wait_queue_head_t wq_video;
	wait_queue_head_t wq_audio;
	wait_queue_head_t wq_trigger;

	int input;
	int polled;
	int standard_offset;
	int format_offset;
	struct inputset inputset[INPUT_MANY];

	bool ntsc;
	int fps;
	int usec;
	int tolerate;
	int skip;
	int skipped;
	int lost[INPUT_MANY];
	int merit[180];

	struct timeval timeval0;
	struct timeval timeval1;
	struct timeval timeval2;
	struct timeval timeval3;
	struct timeval timeval6;
	struct timeval timeval7;
	struct timeval timeval8;
	long long int dnbydt;

	int    video_interface;
	int    video_altsetting_on;
	int    video_altsetting_off;
	int    video_endpointnumber;
	int    video_isoc_maxframesize;
	int    video_isoc_buffer_size;
	int    video_isoc_framesperdesc;

	int    video_isoc_streaming;
	int    video_isoc_sequence;
	int    video_idle;
	int    video_eof;
	int    video_junk;

	struct data_buffer video_isoc_buffer[VIDEO_ISOC_BUFFER_MANY];
	struct data_buffer field_buffer[FIELD_BUFFER_MANY]
					[(FIELD_BUFFER_SIZE/PAGE_SIZE)];
	struct data_buffer frame_buffer[FRAME_BUFFER_MANY]
					[(FRAME_BUFFER_SIZE/PAGE_SIZE)];

	struct list_head urb_video_head;
	struct list_head *purb_video_head;

	u8 cache[8];
	u8 *pcache;
	int video_mt;
	int audio_mt;
	long long audio_bytes;
	u32 isequence;

	int vma_many;
/*---------------------------------------------------------------------------*/
/*
 *  BUFFER INDICATORS
 */
/*---------------------------------------------------------------------------*/
	int field_fill;	/* Field buffer being filled by easycap_complete().  */
			/*   Bumped only by easycap_complete().              */
	int field_page;	/* Page of field buffer page being filled by         */
			/*   easycap_complete().                             */
	int field_read;	/* Field buffer to be read by field2frame().         */
			/*   Bumped only by easycap_complete().              */
	int frame_fill;	/* Frame buffer being filled by field2frame().       */
			/*   Bumped only by easycap_dqbuf() when             */
			/*   field2frame() has created a complete frame.     */
	int frame_read;	/* Frame buffer offered to user by DQBUF.            */
			/*   Set only by easycap_dqbuf() to trail frame_fill.*/
	int frame_lock;	/* Flag set to 1 by DQBUF and cleared by QBUF        */
/*---------------------------------------------------------------------------*/
/*
 *  IMAGE PROPERTIES
 */
/*---------------------------------------------------------------------------*/
	u32                   pixelformat;
	int                     width;
	int                     height;
	int                     bytesperpixel;
	bool                    byteswaporder;
	bool                    decimatepixel;
	bool                    offerfields;
	int                     frame_buffer_used;
	int                     frame_buffer_many;
	int                     videofieldamount;

	int                     brightness;
	int                     contrast;
	int                     saturation;
	int                     hue;

	int allocation_video_urb;
	int allocation_video_page;
	int allocation_video_struct;
	int registered_video;
/*---------------------------------------------------------------------------*/
/*
 *  ALSA
 */
/*---------------------------------------------------------------------------*/
	struct snd_pcm_hardware alsa_hardware;
	struct snd_card *psnd_card;
	struct snd_pcm *psnd_pcm;
	struct snd_pcm_substream *psubstream;
	int dma_fill;
	int dma_next;
	int dma_read;
/*---------------------------------------------------------------------------*/
/*
 *  SOUND PROPERTIES
 */
/*---------------------------------------------------------------------------*/
	int audio_interface;
	int audio_altsetting_on;
	int audio_altsetting_off;
	int audio_endpointnumber;
	int audio_isoc_maxframesize;
	int audio_isoc_buffer_size;
	int audio_isoc_framesperdesc;

	int audio_isoc_streaming;
	int audio_idle;
	int audio_eof;
	int volume;
	int mute;
	s8 gain;

	struct data_buffer audio_isoc_buffer[AUDIO_ISOC_BUFFER_MANY];

	struct list_head urb_audio_head;
	struct list_head *purb_audio_head;
/*---------------------------------------------------------------------------*/
/*
 *  BUFFER INDICATORS
 */
/*---------------------------------------------------------------------------*/
	int audio_fill;	/* Audio buffer being filled by easycap_complete().  */
			/*   Bumped only by easycap_complete().              */
	int audio_read;	/* Audio buffer page being read by easycap_read().   */
			/*   Set by easycap_read() to trail audio_fill by    */
			/*   one fragment.                                   */
/*---------------------------------------------------------------------------*/
/*
 *  SOUND PROPERTIES
 */
/*---------------------------------------------------------------------------*/

	int audio_buffer_many;

	int allocation_audio_urb;
	int allocation_audio_page;
	int allocation_audio_struct;
	int registered_audio;

	long long int audio_sample;
	long long int audio_niveau;
	long long int audio_square;

	struct data_buffer audio_buffer[];
};
/*---------------------------------------------------------------------------*/
/*
 *  VIDEO FUNCTION PROTOTYPES
 */
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/
long easycap_unlocked_ioctl(struct file *, unsigned int, unsigned long);
int              easycap_dqbuf(struct easycap *, int);
int              submit_video_urbs(struct easycap *);
int              kill_video_urbs(struct easycap *);
int              field2frame(struct easycap *);
int              redaub(struct easycap *, void *, void *,
						int, int, u8, u8, bool);
void             easycap_testcard(struct easycap *, int);
int              fillin_formats(void);
int              newinput(struct easycap *, int);
int              adjust_standard(struct easycap *, v4l2_std_id);
int              adjust_format(struct easycap *, u32, u32, u32,
								int, bool);
int              adjust_brightness(struct easycap *, int);
int              adjust_contrast(struct easycap *, int);
int              adjust_saturation(struct easycap *, int);
int              adjust_hue(struct easycap *, int);
int              adjust_volume(struct easycap *, int);
/*---------------------------------------------------------------------------*/
/*
 *  AUDIO FUNCTION PROTOTYPES
 */
/*---------------------------------------------------------------------------*/
int		easycap_alsa_probe(struct easycap *);
void            easycap_alsa_complete(struct urb *);

int              easycap_sound_setup(struct easycap *);
int              submit_audio_urbs(struct easycap *);
int              kill_audio_urbs(struct easycap *);
void             easyoss_testtone(struct easycap *, int);
int              audio_setup(struct easycap *);
/*---------------------------------------------------------------------------*/
/*
 *  LOW-LEVEL FUNCTION PROTOTYPES
 */
/*---------------------------------------------------------------------------*/
int              audio_gainget(struct usb_device *);
int              audio_gainset(struct usb_device *, s8);

int              set_interface(struct usb_device *, u16);
int              wakeup_device(struct usb_device *);
int              confirm_resolution(struct usb_device *);
int              confirm_stream(struct usb_device *);

int              setup_stk(struct usb_device *, bool);
int              setup_saa(struct usb_device *, bool);
int              setup_vt(struct usb_device *);
int              check_stk(struct usb_device *, bool);
int              check_saa(struct usb_device *, bool);
int              ready_saa(struct usb_device *);
int              merit_saa(struct usb_device *);
int              check_vt(struct usb_device *);
int              select_input(struct usb_device *, int, int);
int              set_resolution(struct usb_device *,
						u16, u16, u16, u16);

int              read_saa(struct usb_device *, u16);
int              read_stk(struct usb_device *, u32);
int              write_saa(struct usb_device *, u16, u16);
int              write_000(struct usb_device *, u16, u16);
int              start_100(struct usb_device *);
int              stop_100(struct usb_device *);
int              write_300(struct usb_device *);
int              read_vt(struct usb_device *, u16);
int              write_vt(struct usb_device *, u16, u16);
int		isdongle(struct easycap *);
/*---------------------------------------------------------------------------*/
struct signed_div_result {
	long long int quotient;
	unsigned long long int remainder;
} signed_div(long long int, long long int);


/*---------------------------------------------------------------------------*/
/*
 *  MACROS SAM(...) AND JOM(...) ALLOW DIAGNOSTIC OUTPUT TO BE TAGGED WITH
 *  THE IDENTITY OF THE DONGLE TO WHICH IT APPLIES, BUT IF INVOKED WHEN THE
 *  POINTER peasycap IS INVALID AN Oops IS LIKELY, AND ITS CAUSE MAY NOT BE
 *  IMMEDIATELY OBVIOUS FROM A CASUAL READING OF THE SOURCE CODE.  BEWARE.
*/
/*---------------------------------------------------------------------------*/
const char *strerror(int err);

#define SAY(format, args...) do { \
	printk(KERN_DEBUG "easycap:: %s: " \
			format, __func__, ##args); \
} while (0)
#define SAM(format, args...) do { \
	printk(KERN_DEBUG "easycap::%i%s: " \
			format, peasycap->isdongle, __func__, ##args);\
} while (0)

#ifdef CONFIG_EASYCAP_DEBUG
extern int easycap_debug;
#define JOT(n, format, args...) do { \
	if (n <= easycap_debug) { \
		printk(KERN_DEBUG "easycap:: %s: " \
			format, __func__, ##args);\
	} \
} while (0)
#define JOM(n, format, args...) do { \
	if (n <= easycap_debug) { \
		printk(KERN_DEBUG "easycap::%i%s: " \
			format, peasycap->isdongle, __func__, ##args);\
	} \
} while (0)

#else
#define JOT(n, format, args...) do {} while (0)
#define JOM(n, format, args...) do {} while (0)
#endif /* CONFIG_EASYCAP_DEBUG */

/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/
/* globals
 */
/*---------------------------------------------------------------------------*/

extern bool easycap_readback;
extern const struct easycap_standard easycap_standard[];
extern struct easycap_format easycap_format[];
extern struct v4l2_queryctrl easycap_control[];
extern struct usb_driver easycap_usb_driver;
extern struct easycap_dongle easycapdc60_dongle[];

#endif /* !__EASYCAP_H__  */
