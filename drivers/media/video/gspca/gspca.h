#ifndef GSPCAV2_H
#define GSPCAV2_H

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <linux/mutex.h>

#ifdef GSPCA_DEBUG
/* GSPCA our debug messages */
extern int gspca_debug;
#define PDEBUG(level, fmt, args...) \
	do {\
		if (gspca_debug & (level)) \
			printk(KERN_INFO MODULE_NAME ": " fmt "\n", ## args); \
	} while (0)
#define D_ERR  0x01
#define D_PROBE 0x02
#define D_CONF 0x04
#define D_STREAM 0x08
#define D_FRAM 0x10
#define D_PACK 0x20
#define D_USBI 0x40
#define D_USBO 0x80
#else
#define PDEBUG(level, fmt, args...)
#endif
#undef err
#define err(fmt, args...) \
	do {\
		printk(KERN_ERR MODULE_NAME ": " fmt "\n", ## args); \
	} while (0)
#undef info
#define info(fmt, args...) \
	do {\
		printk(KERN_INFO MODULE_NAME ": " fmt "\n", ## args); \
	} while (0)
#undef warn
#define warn(fmt, args...) \
	do {\
		printk(KERN_WARNING MODULE_NAME ": " fmt "\n", ## args); \
	} while (0)

#define GSPCA_MAX_FRAMES 16	/* maximum number of video frame buffers */
/* ISOC transfers */
#define NURBS 4			/* number of URBs */
#define ISO_MAX_PKT 32		/* max number of packets in an ISOC transfer */
#define ISO_MAX_SIZE 0x10000	/* max size of one URB buffer (64 Kb) */

/* device information - set at probe time */
struct cam_mode {
	__u32 pixfmt;
	short width;
	short height;
	short mode;		/* subdriver value */
	short reserved;		/* subdriver value */
};
struct cam {
	char *dev_name;
	struct cam_mode *cam_mode;	/* size nmodes */
	char nmodes;
	__u8 epaddr;
};

struct gspca_dev;
struct gspca_frame;

/* subdriver operations */
typedef int (*cam_op) (struct gspca_dev *);
typedef void (*cam_v_op) (struct gspca_dev *);
typedef int (*cam_cf_op) (struct gspca_dev *, const struct usb_device_id *);
typedef int (*cam_jpg_op) (struct gspca_dev *,
				struct v4l2_jpegcompression *);
typedef int (*cam_qmnu_op) (struct gspca_dev *,
			struct v4l2_querymenu *);
typedef void (*cam_pkt_op) (struct gspca_dev *gspca_dev,
				struct gspca_frame *frame,
				unsigned char *data,
				int len);

struct ctrl {
	struct v4l2_queryctrl qctrl;
	int (*set)(struct gspca_dev *, __s32);
	int (*get)(struct gspca_dev *, __s32 *);
};

/* subdriver description */
struct sd_desc {
/* information */
	char *name;		/* sub-driver name */
/* controls */
	struct ctrl *ctrls;
	int nctrls;
/* operations */
	cam_cf_op config;	/* called on probe */
	cam_op open;		/* called on open */
	cam_v_op start;		/* called on stream on */
	cam_v_op stopN;		/* called on stream off - main alt */
	cam_v_op stop0;		/* called on stream off - alt 0 */
	cam_v_op close;		/* called on close */
	cam_v_op dq_callback;	/* called when a frame has been dequeued */
	cam_pkt_op pkt_scan;
	cam_jpg_op get_jcomp;
	cam_jpg_op set_jcomp;
	cam_qmnu_op querymenu;
};

struct gspca_pktbuf {
	char *data;
	struct urb *urb;
};

/* packet types when moving from iso buf to frame buf */
#define DISCARD_PACKET	0
#define FIRST_PACKET	1
#define INTER_PACKET	2
#define LAST_PACKET	3

struct gspca_frame {
	unsigned char *data;		/* frame buffer */
	unsigned char *data_end;	/* current end of frame while filling */
	int vma_use_count;
	struct v4l2_buffer v4l2_buf;
};

struct gspca_dev {
	struct video_device vdev;		/* !! must be the first item */
	struct usb_device *dev;

	struct cam cam;				/* device information */
	const struct sd_desc *sd_desc;		/* subdriver description */

	struct gspca_pktbuf pktbuf[NURBS];

	__u8 *frbuf;				/* buffer for nframes */
	struct gspca_frame frame[GSPCA_MAX_FRAMES];
	unsigned int frsz;			/* frame size */
	char nframes;				/* number of frames */
	char fr_i;				/* frame being filled */
	char fr_q;				/* next frame to queue */
	char fr_o;				/* next frame to dequeue */
	signed char fr_queue[GSPCA_MAX_FRAMES];	/* frame queue */
	char last_packet_type;

	__u8 iface;			/* USB interface number */
	__u8 alt;			/* USB alternate setting */
	char curr_mode;			/* current camera mode */
	__u32 pixfmt;			/* current mode parameters */
	short width;
	short height;

	atomic_t nevent;		/* number of frames done */
	wait_queue_head_t wq;		/* wait queue */
	struct mutex usb_lock;		/* usb exchange protection */
	struct mutex read_lock;		/* read protection */
	struct mutex queue_lock;	/* ISOC queue protection */
	__u32 sequence;			/* frame sequence number */
	signed char streaming;
	char users;			/* # open */
	char present;			/* device connected */
	char nbufread;			/* number of buffers for read() */
};

int gspca_dev_probe(struct usb_interface *intf,
		const struct usb_device_id *id,
		const struct sd_desc *sd_desc,
		int dev_size);
void gspca_disconnect(struct usb_interface *intf);
struct gspca_frame *gspca_frame_add(struct gspca_dev *gspca_dev,
				    int packet_type,
				    struct gspca_frame *frame,
				    unsigned char *data,
				    int len);
#endif /* GSPCAV2_H */
