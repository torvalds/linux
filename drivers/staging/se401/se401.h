
#ifndef __LINUX_se401_H
#define __LINUX_se401_H

#include <linux/uaccess.h>
#include "videodev.h"
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <linux/mutex.h>

#define se401_DEBUG	/* Turn on debug messages */

#ifdef se401_DEBUG
#  define PDEBUG(level, fmt, args...) \
if (debug >= level) \
	info("[" __PRETTY_FUNCTION__ ":%d] " fmt, __LINE__ , ## args)
#else
#  define PDEBUG(level, fmt, args...) do {} while (0)
#endif

/* An almost drop-in replacement for sleep_on_interruptible */
#define wait_interruptible(test, queue, wait) \
{ \
	add_wait_queue(queue, wait); \
	set_current_state(TASK_INTERRUPTIBLE); \
	if (test) \
		schedule(); \
	remove_wait_queue(queue, wait); \
	set_current_state(TASK_RUNNING); \
	if (signal_pending(current)) \
		break; \
}

#define SE401_REQ_GET_CAMERA_DESCRIPTOR		0x06
#define SE401_REQ_START_CONTINUOUS_CAPTURE	0x41
#define SE401_REQ_STOP_CONTINUOUS_CAPTURE	0x42
#define SE401_REQ_CAPTURE_FRAME			0x43
#define SE401_REQ_GET_BRT			0x44
#define SE401_REQ_SET_BRT			0x45
#define SE401_REQ_GET_WIDTH			0x4c
#define SE401_REQ_SET_WIDTH			0x4d
#define SE401_REQ_GET_HEIGHT			0x4e
#define SE401_REQ_SET_HEIGHT			0x4f
#define SE401_REQ_GET_OUTPUT_MODE		0x50
#define SE401_REQ_SET_OUTPUT_MODE		0x51
#define SE401_REQ_GET_EXT_FEATURE		0x52
#define SE401_REQ_SET_EXT_FEATURE		0x53
#define SE401_REQ_CAMERA_POWER			0x56
#define SE401_REQ_LED_CONTROL			0x57
#define SE401_REQ_BIOS				0xff

#define SE401_BIOS_READ				0x07

#define SE401_FORMAT_BAYER	0x40

/* Hyundai hv7131b registers
   7121 and 7141 should be the same (haven't really checked...) */
/* Mode registers: */
#define HV7131_REG_MODE_A		0x00
#define HV7131_REG_MODE_B		0x01
#define HV7131_REG_MODE_C		0x02
/* Frame registers: */
#define HV7131_REG_FRSU		0x10
#define HV7131_REG_FRSL		0x11
#define HV7131_REG_FCSU		0x12
#define HV7131_REG_FCSL		0x13
#define HV7131_REG_FWHU		0x14
#define HV7131_REG_FWHL		0x15
#define HV7131_REG_FWWU		0x16
#define HV7131_REG_FWWL		0x17
/* Timing registers: */
#define HV7131_REG_THBU		0x20
#define HV7131_REG_THBL		0x21
#define HV7131_REG_TVBU		0x22
#define HV7131_REG_TVBL		0x23
#define HV7131_REG_TITU		0x25
#define HV7131_REG_TITM		0x26
#define HV7131_REG_TITL		0x27
#define HV7131_REG_TMCD		0x28
/* Adjust Registers: */
#define HV7131_REG_ARLV		0x30
#define HV7131_REG_ARCG		0x31
#define HV7131_REG_AGCG		0x32
#define HV7131_REG_ABCG		0x33
#define HV7131_REG_APBV		0x34
#define HV7131_REG_ASLP		0x54
/* Offset Registers: */
#define HV7131_REG_OFSR		0x50
#define HV7131_REG_OFSG		0x51
#define HV7131_REG_OFSB		0x52
/* REset level statistics registers: */
#define HV7131_REG_LOREFNOH	0x57
#define HV7131_REG_LOREFNOL	0x58
#define HV7131_REG_HIREFNOH	0x59
#define HV7131_REG_HIREFNOL	0x5a

/* se401 registers */
#define SE401_OPERATINGMODE	0x2000


/* size of usb transfers */
#define SE401_PACKETSIZE	4096
/* number of queued bulk transfers to use, should be about 8 */
#define SE401_NUMSBUF		1
/* read the usb specs for this one :) */
#define SE401_VIDEO_ENDPOINT	1
#define SE401_BUTTON_ENDPOINT	2
/* number of frames supported by the v4l part */
#define SE401_NUMFRAMES		2
/* scratch buffers for passing data to the decoders */
#define SE401_NUMSCRATCH	32
/* maximum amount of data in a JangGu packet */
#define SE401_VLCDATALEN	1024
/* number of nul sized packets to receive before kicking the camera */
#define SE401_MAX_NULLPACKETS	4000
/* number of decoding errors before kicking the camera */
#define SE401_MAX_ERRORS	200

struct usb_device;

struct se401_sbuf {
	unsigned char *data;
};

enum {
	FRAME_UNUSED,		/* Unused (no MCAPTURE) */
	FRAME_READY,		/* Ready to start grabbing */
	FRAME_GRABBING,		/* In the process of being grabbed into */
	FRAME_DONE,		/* Finished grabbing, but not been synced yet */
	FRAME_ERROR,		/* Something bad happened while processing */
};

enum {
	FMT_BAYER,
	FMT_JANGGU,
};

enum {
	BUFFER_UNUSED,
	BUFFER_READY,
	BUFFER_BUSY,
	BUFFER_DONE,
};

struct se401_scratch {
	unsigned char *data;
	volatile int state;
	int offset;
	int length;
};

struct se401_frame {
	unsigned char *data;		/* Frame buffer */

	volatile int grabstate;	/* State of grabbing */

	unsigned char *curline;
	int curlinepix;
	int curpix;
};

struct usb_se401 {
	struct video_device vdev;

	/* Device structure */
	struct usb_device *dev;

	unsigned char iface;

	char *camera_name;

	int change;
	int brightness;
	int hue;
	int rgain;
	int ggain;
	int bgain;
	int expose_h;
	int expose_m;
	int expose_l;
	int resetlevel;

	int enhance;

	int format;
	int sizes;
	int *width;
	int *height;
	int cwidth;		/* current width */
	int cheight;		/* current height */
	int palette;
	int maxframesize;
	int cframesize;		/* current framesize */

	struct mutex lock;
	int user;		/* user count for exclusive use */
	int removed;		/* device disconnected */

	int streaming;		/* Are we streaming video? */

	char *fbuf;		/* Videodev buffer area */

	struct urb *urb[SE401_NUMSBUF];
	struct urb *inturb;

	int button;
	int buttonpressed;

	int curframe;		/* Current receiving frame */
	struct se401_frame frame[SE401_NUMFRAMES];
	int readcount;
	int framecount;
	int error;
	int dropped;

	int scratch_next;
	int scratch_use;
	int scratch_overflow;
	struct se401_scratch scratch[SE401_NUMSCRATCH];

	/* Decoder specific data: */
	unsigned char vlcdata[SE401_VLCDATALEN];
	int vlcdatapos;
	int bayeroffset;

	struct se401_sbuf sbuf[SE401_NUMSBUF];

	wait_queue_head_t wq;	/* Processes waiting */

	int nullpackets;
};



#endif

