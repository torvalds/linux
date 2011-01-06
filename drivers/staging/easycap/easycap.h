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
 *                EASYCAP_IS_VIDEODEV_CLIENT
 *                EASYCAP_NEEDS_USBVIDEO_H
 *                EASYCAP_NEEDS_V4L2_DEVICE_H
 *                EASYCAP_NEEDS_V4L2_FOPS
 *
 *  IF REQUIRED THEY MUST BE EXTERNALLY DEFINED, FOR EXAMPLE AS COMPILER
 *  OPTIONS.
 */
/*---------------------------------------------------------------------------*/

#if (!defined(EASYCAP_H))
#define EASYCAP_H

#if defined(EASYCAP_DEBUG)
#if (9 < EASYCAP_DEBUG)
#error Debug levels 0 to 9 are okay.\
  To achieve higher levels, remove this trap manually from easycap.h
#endif
#endif /*EASYCAP_DEBUG*/
/*---------------------------------------------------------------------------*/
/*
 *  THESE ARE FOR MAINTENANCE ONLY - NORMALLY UNDEFINED:
 */
/*---------------------------------------------------------------------------*/
#undef  PREFER_NTSC
#undef  EASYCAP_TESTCARD
#undef  EASYCAP_TESTTONE
#undef  LOCKFRAME
#undef  NOREADBACK
#undef  AUDIOTIME
/*---------------------------------------------------------------------------*/
/*
 *
 *  DEFINE   BRIDGER   TO ACTIVATE THE ROUTINE FOR BRIDGING VIDEOTAPE DROPOUTS.
 *
 *             *** UNDER DEVELOPMENT/TESTING - NOT READY YET!***
 *
 */
/*---------------------------------------------------------------------------*/
#undef  BRIDGER
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
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/poll.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/types.h>

/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#if defined(EASYCAP_IS_VIDEODEV_CLIENT)
#if (!defined(__OLD_VIDIOC_))
#define __OLD_VIDIOC_
#endif /* !defined(__OLD_VIDIOC_) */

#include <media/v4l2-dev.h>

#if defined(EASYCAP_NEEDS_V4L2_DEVICE_H)
#include <media/v4l2-device.h>
#endif /*EASYCAP_NEEDS_V4L2_DEVICE_H*/
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

#if (!defined(__OLD_VIDIOC_))
#define __OLD_VIDIOC_
#endif /* !defined(__OLD_VIDIOC_) */
#include <linux/videodev2.h>

#include <linux/soundcard.h>

#if defined(EASYCAP_NEEDS_USBVIDEO_H)
#include <config/video/usbvideo.h>
#endif /*EASYCAP_NEEDS_USBVIDEO_H*/

#if (!defined(PAGE_SIZE))
#error "PAGE_SIZE not defined"
#endif

#define STRINGIZE_AGAIN(x) #x
#define STRINGIZE(x) STRINGIZE_AGAIN(x)

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

#define EASYCAP_DRIVER_VERSION "0.8.21"
#define EASYCAP_DRIVER_DESCRIPTION "easycapdc60"

#define USB_SKEL_MINOR_BASE     192
#define VIDEO_DEVICE_MANY 8

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
#define AUDIO_ISOC_ORDER 3
#define AUDIO_ISOC_BUFFER_SIZE (PAGE_SIZE << AUDIO_ISOC_ORDER)
/*---------------------------------------------------------------------------*/
/*
 *  AUDIO BUFFERS
 */
/*---------------------------------------------------------------------------*/
#define AUDIO_FRAGMENT_MANY 32
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
#define  STANDARD_MANY 10
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
FIELD_ALTERNATE,
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
struct data_buffer {
struct list_head list_head;
void *pgo;
void *pto;
__u16 kount;
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
__u16 mask;
struct v4l2_standard v4l2_standard;
};
struct easycap_format {
__u16 mask;
char name[128];
struct v4l2_format v4l2_format;
};
/*---------------------------------------------------------------------------*/
/*
 *   easycap.ilk == 0   =>  CVBS+S-VIDEO HARDWARE, AUDIO wMaxPacketSize=256
 *   easycap.ilk == 2   =>  CVBS+S-VIDEO HARDWARE, AUDIO wMaxPacketSize=9
 *   easycap.ilk == 3   =>     FOUR-CVBS HARDWARE, AUDIO wMaxPacketSize=9
 */
/*---------------------------------------------------------------------------*/
struct easycap {
unsigned int audio_pages_per_fragment;
unsigned int audio_bytes_per_fragment;
unsigned int audio_buffer_page_many;

#define UPSAMPLE
#if defined(UPSAMPLE)
__s16 oldaudio;
#endif /*UPSAMPLE*/

struct easycap_format easycap_format[1 + SETTINGS_MANY];

int ilk;
bool microphone;

/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#if defined(EASYCAP_IS_VIDEODEV_CLIENT)
struct video_device *pvideo_device;
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

struct usb_device *pusb_device;
struct usb_interface *pusb_interface;

struct kref kref;

struct mutex mutex_mmap_video[FRAME_BUFFER_MANY];
struct mutex mutex_timeval0;
struct mutex mutex_timeval1;

int queued[FRAME_BUFFER_MANY];
int done[FRAME_BUFFER_MANY];

wait_queue_head_t wq_video;
wait_queue_head_t wq_audio;

int input;
int polled;
int standard_offset;
int format_offset;

int fps;
int usec;
int tolerate;
int merit[180];

struct timeval timeval0;
struct timeval timeval1;
struct timeval timeval2;
struct timeval timeval7;
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

int    fudge;

struct data_buffer video_isoc_buffer[VIDEO_ISOC_BUFFER_MANY];
struct data_buffer \
	     field_buffer[FIELD_BUFFER_MANY][(FIELD_BUFFER_SIZE/PAGE_SIZE)];
struct data_buffer \
	     frame_buffer[FRAME_BUFFER_MANY][(FRAME_BUFFER_SIZE/PAGE_SIZE)];

struct list_head urb_video_head;
struct list_head *purb_video_head;

int vma_many;

/*---------------------------------------------------------------------------*/
/*
 *  BUFFER INDICATORS
 */
/*---------------------------------------------------------------------------*/
int field_fill;		/* Field buffer being filled by easycap_complete().  */
			/*   Bumped only by easycap_complete().              */
int field_page;		/* Page of field buffer page being filled by         */
			/*   easycap_complete().                             */
int field_read;		/* Field buffer to be read by field2frame().         */
			/*   Bumped only by easycap_complete().              */
int frame_fill;		/* Frame buffer being filled by field2frame().       */
			/*   Bumped only by easycap_dqbuf() when             */
			/*   field2frame() has created a complete frame.     */
int frame_read;		/* Frame buffer offered to user by DQBUF.            */
			/*   Set only by easycap_dqbuf() to trail frame_fill.*/
int frame_lock;		/* Flag set to 1 by DQBUF and cleared by QBUF        */
/*---------------------------------------------------------------------------*/
/*
 *  IMAGE PROPERTIES
 */
/*---------------------------------------------------------------------------*/
__u32                   pixelformat;
__u32                   field;
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

struct data_buffer audio_isoc_buffer[AUDIO_ISOC_BUFFER_MANY];

struct list_head urb_audio_head;
struct list_head *purb_audio_head;
/*---------------------------------------------------------------------------*/
/*
 *  BUFFER INDICATORS
 */
/*---------------------------------------------------------------------------*/
int audio_fill;		/* Audio buffer being filled by easysnd_complete().  */
			/*   Bumped only by easysnd_complete().              */
int audio_read;		/* Audio buffer page being read by easysnd_read().   */
			/*   Set by easysnd_read() to trail audio_fill by    */
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
/*---------------------------------------------------------------------------*/
void             easycap_complete(struct urb *);
int              easycap_open(struct inode *, struct file *);
int              easycap_release(struct inode *, struct file *);
long             easycap_ioctl(struct file *, unsigned int,  unsigned long);

/*vvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvvv*/
#if defined(EASYCAP_IS_VIDEODEV_CLIENT)
int              easycap_open_noinode(struct file *);
int              easycap_release_noinode(struct file *);
int              videodev_release(struct video_device *);
#endif /*EASYCAP_IS_VIDEODEV_CLIENT*/
/*^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^*/

unsigned int     easycap_poll(struct file *, poll_table *);
int              easycap_mmap(struct file *, struct vm_area_struct *);
int              easycap_usb_probe(struct usb_interface *, \
						const struct usb_device_id *);
void             easycap_usb_disconnect(struct usb_interface *);
void             easycap_delete(struct kref *);

void             easycap_vma_open(struct vm_area_struct *);
void             easycap_vma_close(struct vm_area_struct *);
int              easycap_vma_fault(struct vm_area_struct *, struct vm_fault *);
int              easycap_dqbuf(struct easycap *, int);
int              submit_video_urbs(struct easycap *);
int              kill_video_urbs(struct easycap *);
int              field2frame(struct easycap *);
int              redaub(struct easycap *, void *, void *, \
						int, int, __u8, __u8, bool);
void             debrief(struct easycap *);
void             sayreadonly(struct easycap *);
void             easycap_testcard(struct easycap *, int);
int              explain_ioctl(__u32);
int              explain_cid(__u32);
int              fillin_formats(void);
int              adjust_standard(struct easycap *, v4l2_std_id);
int              adjust_format(struct easycap *, __u32, __u32, __u32, \
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
void             easysnd_complete(struct urb *);
ssize_t          easysnd_read(struct file *, char __user *, size_t, loff_t *);
int              easysnd_open(struct inode *, struct file *);
int              easysnd_release(struct inode *, struct file *);
long             easysnd_ioctl(struct file *, unsigned int,  unsigned long);
unsigned int     easysnd_poll(struct file *, poll_table *);
void             easysnd_delete(struct kref *);
int              submit_audio_urbs(struct easycap *);
int              kill_audio_urbs(struct easycap *);
void             easysnd_testtone(struct easycap *, int);
int              audio_setup(struct easycap *);
/*---------------------------------------------------------------------------*/
/*
 *  LOW-LEVEL FUNCTION PROTOTYPES
 */
/*---------------------------------------------------------------------------*/
int              audio_gainget(struct usb_device *);
int              audio_gainset(struct usb_device *, __s8);

int              set_interface(struct usb_device *, __u16);
int              wakeup_device(struct usb_device *);
int              confirm_resolution(struct usb_device *);
int              confirm_stream(struct usb_device *);

int              setup_stk(struct usb_device *);
int              setup_saa(struct usb_device *);
int              setup_vt(struct usb_device *);
int              check_stk(struct usb_device *);
int              check_saa(struct usb_device *);
int              ready_saa(struct usb_device *);
int              merit_saa(struct usb_device *);
int              check_vt(struct usb_device *);
int              select_input(struct usb_device *, int, int);
int              set_resolution(struct usb_device *, \
						__u16, __u16, __u16, __u16);

int              read_saa(struct usb_device *, __u16);
int              read_stk(struct usb_device *, __u32);
int              write_saa(struct usb_device *, __u16, __u16);
int              wait_i2c(struct usb_device *);
int              write_000(struct usb_device *, __u16, __u16);
int              start_100(struct usb_device *);
int              stop_100(struct usb_device *);
int              write_300(struct usb_device *);
int              read_vt(struct usb_device *, __u16);
int              write_vt(struct usb_device *, __u16, __u16);

int              set2to78(struct usb_device *);
int              set2to93(struct usb_device *);

int              regset(struct usb_device *, __u16, __u16);
int              regget(struct usb_device *, __u16, void *);
/*---------------------------------------------------------------------------*/
struct signed_div_result {
long long int quotient;
unsigned long long int remainder;
} signed_div(long long int, long long int);
/*---------------------------------------------------------------------------*/
/*
 *  MACROS
 */
/*---------------------------------------------------------------------------*/
#define GET(X, Y, Z) do { \
	int rc; \
	*(Z) = (__u16)0; \
	rc = regget(X, Y, Z); \
	if (0 > rc) { \
		JOT(8, ":-(%i\n", __LINE__);  return(rc); \
	} \
} while (0)

#define SET(X, Y, Z) do { \
	int rc; \
	rc = regset(X, Y, Z); \
	if (0 > rc) { \
		JOT(8, ":-(%i\n", __LINE__);  return(rc); \
	} \
} while (0)
/*---------------------------------------------------------------------------*/

#define SAY(format, args...) do { \
	printk(KERN_DEBUG "easycap: %s: " format, __func__, ##args); \
} while (0)


#if defined(EASYCAP_DEBUG)
#define JOT(n, format, args...) do { \
	if (n <= easycap_debug) { \
		printk(KERN_DEBUG "easycap: %s: " format, __func__, ##args); \
	} \
} while (0)
#else
#define JOT(n, format, args...) do {} while (0)
#endif /*EASYCAP_DEBUG*/

#define POUT JOT(8, ":-(in file %s line %4i\n", __FILE__, __LINE__)

#define MICROSECONDS(X, Y) \
			((1000000*((long long int)(X.tv_sec - Y.tv_sec))) + \
					(long long int)(X.tv_usec - Y.tv_usec))

/*---------------------------------------------------------------------------*/
/*
 *  (unsigned char *)P           pointer to next byte pair
 *       (long int *)X           pointer to accumulating count
 *       (long int *)Y           pointer to accumulating sum
 *  (long long int *)Z           pointer to accumulating sum of squares
 */
/*---------------------------------------------------------------------------*/
#define SUMMER(P, X, Y, Z) do {                                 \
	unsigned char *p;                                    \
	unsigned int u0, u1, u2;                             \
	long int s;                                          \
	p = (unsigned char *)(P);                            \
	u0 = (unsigned int) (*p);                            \
	u1 = (unsigned int) (*(p + 1));                      \
	u2 = (unsigned int) ((u1 << 8) | u0);                \
	if (0x8000 & u2)                                     \
		s = -(long int)(0x7FFF & (~u2));             \
	else                                                 \
		s =  (long int)(0x7FFF & u2);                \
	*((X)) += (long int) 1;                              \
	*((Y)) += (long int) s;                              \
	*((Z)) += ((long long int)(s) * (long long int)(s)); \
} while (0)
/*---------------------------------------------------------------------------*/

#endif /*EASYCAP_H*/
