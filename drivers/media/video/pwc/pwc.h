/* (C) 1999-2003 Nemosoft Unv.
   (C) 2004      Luc Saillard (luc@saillard.org)

   NOTE: this version of pwc is an unofficial (modified) release of pwc & pcwx
   driver and thus may have bugs that are not present in the original version.
   Please send bug reports and support requests to <luc@saillard.org>.
   The decompression routines have been implemented by reverse-engineering the
   Nemosoft binary pwcx module. Caveat emptor.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifndef PWC_H
#define PWC_H

#include <linux/config.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/spinlock.h>
#include <linux/videodev.h>
#include <linux/wait.h>
#include <linux/smp_lock.h>
#include <asm/semaphore.h>
#include <asm/errno.h>

#include "pwc-uncompress.h"
#include "pwc-ioctl.h"

/* Defines and structures for the Philips webcam */
/* Used for checking memory corruption/pointer validation */
#define PWC_MAGIC 0x89DC10ABUL
#undef PWC_MAGIC

/* Turn some debugging options on/off */
#define PWC_DEBUG 0

/* Trace certain actions in the driver */
#define TRACE_MODULE	0x0001
#define TRACE_PROBE	0x0002
#define TRACE_OPEN	0x0004
#define TRACE_READ	0x0008
#define TRACE_MEMORY	0x0010
#define TRACE_FLOW	0x0020
#define TRACE_SIZE	0x0040
#define TRACE_PWCX	0x0080
#define TRACE_SEQUENCE	0x1000

#define Trace(R, A...) if (pwc_trace & R) printk(KERN_DEBUG PWC_NAME " " A)
#define Debug(A...) printk(KERN_DEBUG PWC_NAME " " A)
#define Info(A...)  printk(KERN_INFO  PWC_NAME " " A)
#define Err(A...)   printk(KERN_ERR   PWC_NAME " " A)


/* Defines for ToUCam cameras */
#define TOUCAM_HEADER_SIZE		8
#define TOUCAM_TRAILER_SIZE		4

#define FEATURE_MOTOR_PANTILT		0x0001

/* Version block */
#define PWC_MAJOR	9
#define PWC_MINOR	0
#define PWC_VERSION 	"9.0.2-unofficial"
#define PWC_NAME 	"pwc"

/* Turn certain features on/off */
#define PWC_INT_PIPE 0

/* Ignore errors in the first N frames, to allow for startup delays */
#define FRAME_LOWMARK 5

/* Size and number of buffers for the ISO pipe. */
#define MAX_ISO_BUFS		2
#define ISO_FRAMES_PER_DESC	10
#define ISO_MAX_FRAME_SIZE	960
#define ISO_BUFFER_SIZE 	(ISO_FRAMES_PER_DESC * ISO_MAX_FRAME_SIZE)

/* Frame buffers: contains compressed or uncompressed video data. */
#define MAX_FRAMES		5
/* Maximum size after decompression is 640x480 YUV data, 1.5 * 640 * 480 */
#define PWC_FRAME_SIZE 		(460800 + TOUCAM_HEADER_SIZE + TOUCAM_TRAILER_SIZE)

/* Absolute maximum number of buffers available for mmap() */
#define MAX_IMAGES 		10

/* The following structures were based on cpia.h. Why reinvent the wheel? :-) */
struct pwc_iso_buf
{
	void *data;
	int  length;
	int  read;
	struct urb *urb;
};

/* intermediate buffers with raw data from the USB cam */
struct pwc_frame_buf
{
   void *data;
   volatile int filled;		/* number of bytes filled */
   struct pwc_frame_buf *next;	/* list */
#if PWC_DEBUG
   int sequence;		/* Sequence number */
#endif
};

struct pwc_device
{
   struct video_device *vdev;
#ifdef PWC_MAGIC
   int magic;
#endif
   /* Pointer to our usb_device */
   struct usb_device *udev;

   int type;                    /* type of cam (645, 646, 675, 680, 690, 720, 730, 740, 750) */
   int release;			/* release number */
   int features;		/* feature bits */
   char serial[30];		/* serial number (string) */
   int error_status;		/* set when something goes wrong with the cam (unplugged, USB errors) */
   int usb_init;		/* set when the cam has been initialized over USB */

   /*** Video data ***/
   int vopen;			/* flag */
   int vendpoint;		/* video isoc endpoint */
   int vcinterface;		/* video control interface */
   int valternate;		/* alternate interface needed */
   int vframes, vsize;		/* frames-per-second & size (see PSZ_*) */
   int vpalette;		/* palette: 420P, RAW or RGBBAYER */
   int vframe_count;		/* received frames */
   int vframes_dumped; 		/* counter for dumped frames */
   int vframes_error;		/* frames received in error */
   int vmax_packet_size;	/* USB maxpacket size */
   int vlast_packet_size;	/* for frame synchronisation */
   int visoc_errors;		/* number of contiguous ISOC errors */
   int vcompression;		/* desired compression factor */
   int vbandlength;		/* compressed band length; 0 is uncompressed */
   char vsnapshot;		/* snapshot mode */
   char vsync;			/* used by isoc handler */
   char vmirror;		/* for ToUCaM series */

   int cmd_len;
   unsigned char cmd_buf[13];

   /* The image acquisition requires 3 to 4 steps:
      1. data is gathered in short packets from the USB controller
      2. data is synchronized and packed into a frame buffer
      3a. in case data is compressed, decompress it directly into image buffer
      3b. in case data is uncompressed, copy into image buffer with viewport
      4. data is transferred to the user process

      Note that MAX_ISO_BUFS != MAX_FRAMES != MAX_IMAGES....
      We have in effect a back-to-back-double-buffer system.
    */
   /* 1: isoc */
   struct pwc_iso_buf sbuf[MAX_ISO_BUFS];
   char iso_init;

   /* 2: frame */
   struct pwc_frame_buf *fbuf;	/* all frames */
   struct pwc_frame_buf *empty_frames, *empty_frames_tail;	/* all empty frames */
   struct pwc_frame_buf *full_frames, *full_frames_tail;	/* all filled frames */
   struct pwc_frame_buf *fill_frame;	/* frame currently being filled */
   struct pwc_frame_buf *read_frame;	/* frame currently read by user process */
   int frame_header_size, frame_trailer_size;
   int frame_size;
   int frame_total_size; /* including header & trailer */
   int drop_frames;
#if PWC_DEBUG
   int sequence;			/* Debugging aid */
#endif

   /* 3: decompression */
   struct pwc_decompressor *decompressor;	/* function block with decompression routines */
   void *decompress_data;		/* private data for decompression engine */

   /* 4: image */
   /* We have an 'image' and a 'view', where 'image' is the fixed-size image
      as delivered by the camera, and 'view' is the size requested by the
      program. The camera image is centered in this viewport, laced with
      a gray or black border. view_min <= image <= view <= view_max;
    */
   int image_mask;			/* bitmask of supported sizes */
   struct pwc_coord view_min, view_max;	/* minimum and maximum viewable sizes */
   struct pwc_coord abs_max;            /* maximum supported size with compression */
   struct pwc_coord image, view;	/* image and viewport size */
   struct pwc_coord offset;		/* offset within the viewport */

   void *image_data;			/* total buffer, which is subdivided into ... */
   void *image_ptr[MAX_IMAGES];		/* ...several images... */
   int fill_image;			/* ...which are rotated. */
   int len_per_image;			/* length per image */
   int image_read_pos;			/* In case we read data in pieces, keep track of were we are in the imagebuffer */
   int image_used[MAX_IMAGES];		/* For MCAPTURE and SYNC */

   struct semaphore modlock;		/* to prevent races in video_open(), etc */
   spinlock_t ptrlock;			/* for manipulating the buffer pointers */

   /*** motorized pan/tilt feature */
   struct pwc_mpt_range angle_range;
   int pan_angle;			/* in degrees * 100 */
   int tilt_angle;			/* absolute angle; 0,0 is home position */

   /*** Misc. data ***/
   wait_queue_head_t frameq;		/* When waiting for a frame to finish... */
#if PWC_INT_PIPE
   void *usb_int_handler;		/* for the interrupt endpoint */
#endif
};


#ifdef __cplusplus
extern "C" {
#endif

/* Global variable */
extern int pwc_trace;

/** functions in pwc-if.c */
int pwc_try_video_mode(struct pwc_device *pdev, int width, int height, int new_fps, int new_compression, int new_snapshot);

/** Functions in pwc-misc.c */
/* sizes in pixels */
extern struct pwc_coord pwc_image_sizes[PSZ_MAX];

int pwc_decode_size(struct pwc_device *pdev, int width, int height);
void pwc_construct(struct pwc_device *pdev);

/** Functions in pwc-ctrl.c */
/* Request a certain video mode. Returns < 0 if not possible */
extern int pwc_set_video_mode(struct pwc_device *pdev, int width, int height, int frames, int compression, int snapshot);

/* Various controls; should be obvious. Value 0..65535, or < 0 on error */
extern int pwc_get_brightness(struct pwc_device *pdev);
extern int pwc_set_brightness(struct pwc_device *pdev, int value);
extern int pwc_get_contrast(struct pwc_device *pdev);
extern int pwc_set_contrast(struct pwc_device *pdev, int value);
extern int pwc_get_gamma(struct pwc_device *pdev);
extern int pwc_set_gamma(struct pwc_device *pdev, int value);
extern int pwc_get_saturation(struct pwc_device *pdev);
extern int pwc_set_saturation(struct pwc_device *pdev, int value);
extern int pwc_set_leds(struct pwc_device *pdev, int on_value, int off_value);
extern int pwc_get_cmos_sensor(struct pwc_device *pdev, int *sensor);

/* Power down or up the camera; not supported by all models */
extern int pwc_camera_power(struct pwc_device *pdev, int power);

/* Private ioctl()s; see pwc-ioctl.h */
extern int pwc_ioctl(struct pwc_device *pdev, unsigned int cmd, void *arg);


/** pwc-uncompress.c */
/* Expand frame to image, possibly including decompression. Uses read_frame and fill_image */
extern int pwc_decompress(struct pwc_device *pdev);

#ifdef __cplusplus
}
#endif


#endif
