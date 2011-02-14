/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
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
#ifndef usbvideo_h
#define	usbvideo_h

#include "videodev.h"
#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>
#include <linux/usb.h>
#include <linux/mutex.h>

/* Most helpful debugging aid */
#define assert(expr) ((void) ((expr) ? 0 : (err("assert failed at line %d",__LINE__))))

#define USBVIDEO_REPORT_STATS	1	/* Set to 0 to block statistics on close */

/* Bit flags (options) */
#define FLAGS_RETRY_VIDIOCSYNC		(1 << 0)
#define	FLAGS_MONOCHROME		(1 << 1)
#define FLAGS_DISPLAY_HINTS		(1 << 2)
#define FLAGS_OVERLAY_STATS		(1 << 3)
#define FLAGS_FORCE_TESTPATTERN		(1 << 4)
#define FLAGS_SEPARATE_FRAMES		(1 << 5)
#define FLAGS_CLEAN_FRAMES		(1 << 6)
#define	FLAGS_NO_DECODING		(1 << 7)

/* Bit flags for frames (apply to the frame where they are specified) */
#define USBVIDEO_FRAME_FLAG_SOFTWARE_CONTRAST	(1 << 0)

/* Camera capabilities (maximum) */
#define CAMERA_URB_FRAMES       32
#define CAMERA_MAX_ISO_PACKET   1023 /* 1022 actually sent by camera */
#define FRAMES_PER_DESC		(CAMERA_URB_FRAMES)
#define FRAME_SIZE_PER_DESC	(CAMERA_MAX_ISO_PACKET)

/* This macro restricts an int variable to an inclusive range */
#define RESTRICT_TO_RANGE(v,mi,ma) { if ((v) < (mi)) (v) = (mi); else if ((v) > (ma)) (v) = (ma); }

#define V4L_BYTES_PER_PIXEL     3	/* Because we produce RGB24 */

/*
 * Use this macro to construct constants for different video sizes.
 * We have to deal with different video sizes that have to be
 * configured in the device or compared against when we receive
 * a data. Normally one would define a bunch of VIDEOSIZE_x_by_y
 * #defines and that's the end of story. However this solution
 * does not allow to convert between real pixel sizes and the
 * constant (integer) value that may be used to tag a frame or
 * whatever. The set of macros below constructs videosize constants
 * from the pixel size and allows to reconstruct the pixel size
 * from the combined value later.
 */
#define	VIDEOSIZE(x,y)	(((x) & 0xFFFFL) | (((y) & 0xFFFFL) << 16))
#define	VIDEOSIZE_X(vs)	((vs) & 0xFFFFL)
#define	VIDEOSIZE_Y(vs)	(((vs) >> 16) & 0xFFFFL)
typedef unsigned long videosize_t;

/*
 * This macro checks if the camera is still operational. The 'uvd'
 * pointer must be valid, uvd->dev must be valid, we are not
 * removing the device and the device has not erred on us.
 */
#define CAMERA_IS_OPERATIONAL(uvd) (\
	(uvd != NULL) && \
	((uvd)->dev != NULL) && \
	((uvd)->last_error == 0) && \
	(!(uvd)->remove_pending))

/*
 * We use macros to do YUV -> RGB conversion because this is
 * very important for speed and totally unimportant for size.
 *
 * YUV -> RGB Conversion
 * ---------------------
 *
 * B = 1.164*(Y-16)		    + 2.018*(V-128)
 * G = 1.164*(Y-16) - 0.813*(U-128) - 0.391*(V-128)
 * R = 1.164*(Y-16) + 1.596*(U-128)
 *
 * If you fancy integer arithmetics (as you should), hear this:
 *
 * 65536*B = 76284*(Y-16)		  + 132252*(V-128)
 * 65536*G = 76284*(Y-16) -  53281*(U-128) -  25625*(V-128)
 * 65536*R = 76284*(Y-16) + 104595*(U-128)
 *
 * Make sure the output values are within [0..255] range.
 */
#define LIMIT_RGB(x) (((x) < 0) ? 0 : (((x) > 255) ? 255 : (x)))
#define YUV_TO_RGB_BY_THE_BOOK(my,mu,mv,mr,mg,mb) { \
    int mm_y, mm_yc, mm_u, mm_v, mm_r, mm_g, mm_b; \
    mm_y = (my) - 16;  \
    mm_u = (mu) - 128; \
    mm_v = (mv) - 128; \
    mm_yc= mm_y * 76284; \
    mm_b = (mm_yc		+ 132252*mm_v	) >> 16; \
    mm_g = (mm_yc -  53281*mm_u -  25625*mm_v	) >> 16; \
    mm_r = (mm_yc + 104595*mm_u			) >> 16; \
    mb = LIMIT_RGB(mm_b); \
    mg = LIMIT_RGB(mm_g); \
    mr = LIMIT_RGB(mm_r); \
}

#define	RING_QUEUE_SIZE		(128*1024)	/* Must be a power of 2 */
#define	RING_QUEUE_ADVANCE_INDEX(rq,ind,n) (rq)->ind = ((rq)->ind + (n)) & ((rq)->length-1)
#define	RING_QUEUE_DEQUEUE_BYTES(rq,n) RING_QUEUE_ADVANCE_INDEX(rq,ri,n)
#define	RING_QUEUE_PEEK(rq,ofs) ((rq)->queue[((ofs) + (rq)->ri) & ((rq)->length-1)])

struct RingQueue {
	unsigned char *queue;	/* Data from the Isoc data pump */
	int length;		/* How many bytes allocated for the queue */
	int wi;			/* That's where we write */
	int ri;			/* Read from here until you hit write index */
	wait_queue_head_t wqh;	/* Processes waiting */
};

enum ScanState {
	ScanState_Scanning,	/* Scanning for header */
	ScanState_Lines		/* Parsing lines */
};

/* Completion states of the data parser */
enum ParseState {
	scan_Continue,		/* Just parse next item */
	scan_NextFrame,		/* Frame done, send it to V4L */
	scan_Out,		/* Not enough data for frame */
	scan_EndParse		/* End parsing */
};

enum FrameState {
	FrameState_Unused,	/* Unused (no MCAPTURE) */
	FrameState_Ready,	/* Ready to start grabbing */
	FrameState_Grabbing,	/* In the process of being grabbed into */
	FrameState_Done,	/* Finished grabbing, but not been synced yet */
	FrameState_Done_Hold,	/* Are syncing or reading */
	FrameState_Error,	/* Something bad happened while processing */
};

/*
 * Some frames may contain only even or odd lines. This type
 * specifies what type of deinterlacing is required.
 */
enum Deinterlace {
	Deinterlace_None=0,
	Deinterlace_FillOddLines,
	Deinterlace_FillEvenLines
};

#define USBVIDEO_NUMFRAMES	2	/* How many frames we work with */
#define USBVIDEO_NUMSBUF	2	/* How many URBs linked in a ring */

/* This structure represents one Isoc request - URB and buffer */
struct usbvideo_sbuf {
	char *data;
	struct urb *urb;
};

struct usbvideo_frame {
	char *data;		/* Frame buffer */
	unsigned long header;	/* Significant bits from the header */

	videosize_t canvas;	/* The canvas (max. image) allocated */
	videosize_t request;	/* That's what the application asked for */
	unsigned short palette;	/* The desired format */

	enum FrameState frameState;/* State of grabbing */
	enum ScanState scanstate;	/* State of scanning */
	enum Deinterlace deinterlace;
	int flags;		/* USBVIDEO_FRAME_FLAG_xxx bit flags */

	int curline;		/* Line of frame we're working on */

	long seqRead_Length;	/* Raw data length of frame */
	long seqRead_Index;	/* Amount of data that has been already read */

	void *user;		/* Additional data that user may need */
};

/* Statistics that can be overlaid on screen */
struct usbvideo_statistics {
	unsigned long frame_num;	/* Sequential number of the frame */
	unsigned long urb_count;        /* How many URBs we received so far */
	unsigned long urb_length;       /* Length of last URB */
	unsigned long data_count;       /* How many bytes we received */
	unsigned long header_count;     /* How many frame headers we found */
	unsigned long iso_skip_count;	/* How many empty ISO packets received */
	unsigned long iso_err_count;	/* How many bad ISO packets received */
};

struct usbvideo;

struct uvd {
	struct video_device vdev;	/* Must be the first field! */
	struct usb_device *dev;
	struct usbvideo *handle;	/* Points back to the struct usbvideo */
	void *user_data;		/* Camera-dependent data */
	int user_size;			/* Size of that camera-dependent data */
	int debug;			/* Debug level for usbvideo */
	unsigned char iface;		/* Video interface number */
	unsigned char video_endp;
	unsigned char ifaceAltActive;
	unsigned char ifaceAltInactive; /* Alt settings */
	unsigned long flags;		/* FLAGS_USBVIDEO_xxx */
	unsigned long paletteBits;	/* Which palettes we accept? */
	unsigned short defaultPalette;	/* What palette to use for read() */
	struct mutex lock;
	int user;		/* user count for exclusive use */

	videosize_t videosize;	/* Current setting */
	videosize_t canvas;	/* This is the width,height of the V4L canvas */
	int max_frame_size;	/* Bytes in one video frame */

	int uvd_used;        	/* Is this structure in use? */
	int streaming;		/* Are we streaming Isochronous? */
	int grabbing;		/* Are we grabbing? */
	int settingsAdjusted;	/* Have we adjusted contrast etc.? */
	int last_error;		/* What calamity struck us? */

	char *fbuf;		/* Videodev buffer area */
	int fbuf_size;		/* Videodev buffer size */

	int curframe;
	int iso_packet_len;	/* Videomode-dependent, saves bus bandwidth */

	struct RingQueue dp;	/* Isoc data pump */
	struct usbvideo_frame frame[USBVIDEO_NUMFRAMES];
	struct usbvideo_sbuf sbuf[USBVIDEO_NUMSBUF];

	volatile int remove_pending;	/* If set then about to exit */

	struct video_picture vpic, vpic_old;	/* Picture settings */
	struct video_capability vcap;		/* Video capabilities */
	struct video_channel vchan;	/* May be used for tuner support */
	struct usbvideo_statistics stats;
	char videoName[32];		/* Holds name like "video7" */
};

/*
 * usbvideo callbacks (virtual methods). They are set when usbvideo
 * services are registered. All of these default to NULL, except those
 * that default to usbvideo-provided methods.
 */
struct usbvideo_cb {
	int (*probe)(struct usb_interface *, const struct usb_device_id *);
	void (*userFree)(struct uvd *);
	void (*disconnect)(struct usb_interface *);
	int (*setupOnOpen)(struct uvd *);
	void (*videoStart)(struct uvd *);
	void (*videoStop)(struct uvd *);
	void (*processData)(struct uvd *, struct usbvideo_frame *);
	void (*postProcess)(struct uvd *, struct usbvideo_frame *);
	void (*adjustPicture)(struct uvd *);
	int (*getFPS)(struct uvd *);
	int (*overlayHook)(struct uvd *, struct usbvideo_frame *);
	int (*getFrame)(struct uvd *, int);
	int (*startDataPump)(struct uvd *uvd);
	void (*stopDataPump)(struct uvd *uvd);
	int (*setVideoMode)(struct uvd *uvd, struct video_window *vw);
};

struct usbvideo {
	int num_cameras;		/* As allocated */
	struct usb_driver usbdrv;	/* Interface to the USB stack */
	char drvName[80];		/* Driver name */
	struct mutex lock;		/* Mutex protecting camera structures */
	struct usbvideo_cb cb;		/* Table of callbacks (virtual methods) */
	struct video_device vdt;	/* Video device template */
	struct uvd *cam;			/* Array of camera structures */
	struct module *md_module;	/* Minidriver module */
};


/*
 * This macro retrieves callback address from the struct uvd object.
 * No validity checks are done here, so be sure to check the
 * callback beforehand with VALID_CALLBACK.
 */
#define	GET_CALLBACK(uvd,cbName) ((uvd)->handle->cb.cbName)

/*
 * This macro returns either callback pointer or NULL. This is safe
 * macro, meaning that most of components of data structures involved
 * may be NULL - this only results in NULL being returned. You may
 * wish to use this macro to make sure that the callback is callable.
 * However keep in mind that those checks take time.
 */
#define	VALID_CALLBACK(uvd,cbName) ((((uvd) != NULL) && \
		((uvd)->handle != NULL)) ? GET_CALLBACK(uvd,cbName) : NULL)

int  RingQueue_Dequeue(struct RingQueue *rq, unsigned char *dst, int len);
int  RingQueue_Enqueue(struct RingQueue *rq, const unsigned char *cdata, int n);
void RingQueue_WakeUpInterruptible(struct RingQueue *rq);
void RingQueue_Flush(struct RingQueue *rq);

static inline int RingQueue_GetLength(const struct RingQueue *rq)
{
	return (rq->wi - rq->ri + rq->length) & (rq->length-1);
}

static inline int RingQueue_GetFreeSpace(const struct RingQueue *rq)
{
	return rq->length - RingQueue_GetLength(rq);
}

void usbvideo_DrawLine(
	struct usbvideo_frame *frame,
	int x1, int y1,
	int x2, int y2,
	unsigned char cr, unsigned char cg, unsigned char cb);
void usbvideo_HexDump(const unsigned char *data, int len);
void usbvideo_SayAndWait(const char *what);
void usbvideo_TestPattern(struct uvd *uvd, int fullframe, int pmode);

/* Memory allocation routines */
unsigned long usbvideo_kvirt_to_pa(unsigned long adr);

int usbvideo_register(
	struct usbvideo **pCams,
	const int num_cams,
	const int num_extra,
	const char *driverName,
	const struct usbvideo_cb *cbTable,
	struct module *md,
	const struct usb_device_id *id_table);
struct uvd *usbvideo_AllocateDevice(struct usbvideo *cams);
int usbvideo_RegisterVideoDevice(struct uvd *uvd);
void usbvideo_Deregister(struct usbvideo **uvt);

int usbvideo_v4l_initialize(struct video_device *dev);

void usbvideo_DeinterlaceFrame(struct uvd *uvd, struct usbvideo_frame *frame);

/*
 * This code performs bounds checking - use it when working with
 * new formats, or else you may get oopses all over the place.
 * If pixel falls out of bounds then it gets shoved back (as close
 * to place of offence as possible) and is painted bright red.
 *
 * There are two important concepts: frame width, height and
 * V4L canvas width, height. The former is the area requested by
 * the application -for this very frame-. The latter is the largest
 * possible frame that we can serve (we advertise that via V4L ioctl).
 * The frame data is expected to be formatted as lines of length
 * VIDEOSIZE_X(fr->request), total VIDEOSIZE_Y(frame->request) lines.
 */
static inline void RGB24_PUTPIXEL(
	struct usbvideo_frame *fr,
	int ix, int iy,
	unsigned char vr,
	unsigned char vg,
	unsigned char vb)
{
	register unsigned char *pf;
	int limiter = 0, mx, my;
	mx = ix;
	my = iy;
	if (mx < 0) {
		mx=0;
		limiter++;
	} else if (mx >= VIDEOSIZE_X((fr)->request)) {
		mx= VIDEOSIZE_X((fr)->request) - 1;
		limiter++;
	}
	if (my < 0) {
		my = 0;
		limiter++;
	} else if (my >= VIDEOSIZE_Y((fr)->request)) {
		my = VIDEOSIZE_Y((fr)->request) - 1;
		limiter++;
	}
	pf = (fr)->data + V4L_BYTES_PER_PIXEL*((iy)*VIDEOSIZE_X((fr)->request) + (ix));
	if (limiter) {
		*pf++ = 0;
		*pf++ = 0;
		*pf++ = 0xFF;
	} else {
		*pf++ = (vb);
		*pf++ = (vg);
		*pf++ = (vr);
	}
}

#endif /* usbvideo_h */
