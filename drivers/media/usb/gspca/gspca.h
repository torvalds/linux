/* SPDX-License-Identifier: GPL-2.0 */
#ifndef GSPCAV2_H
#define GSPCAV2_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/usb.h>
#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>
#include <linux/mutex.h>



/* GSPCA debug codes */

#define D_PROBE  1
#define D_CONF   2
#define D_STREAM 3
#define D_FRAM   4
#define D_PACK   5
#define D_USBI   6
#define D_USBO   7

extern int gspca_debug;


#define gspca_dbg(gspca_dev, level, fmt, ...)			\
	v4l2_dbg(level, gspca_debug, &(gspca_dev)->v4l2_dev,	\
		 fmt, ##__VA_ARGS__)

#define gspca_err(gspca_dev, fmt, ...)				\
	v4l2_err(&(gspca_dev)->v4l2_dev, fmt, ##__VA_ARGS__)

#define GSPCA_MAX_FRAMES 16	/* maximum number of video frame buffers */
/* image transfers */
#define MAX_NURBS 4		/* max number of URBs */


/* used to list framerates supported by a camera mode (resolution) */
struct framerates {
	const u8 *rates;
	int nrates;
};

/* device information - set at probe time */
struct cam {
	const struct v4l2_pix_format *cam_mode;	/* size nmodes */
	const struct framerates *mode_framerates; /* must have size nmodes,
						   * just like cam_mode */
	u32 bulk_size;		/* buffer size when image transfer by bulk */
	u32 input_flags;	/* value for ENUM_INPUT status flags */
	u8 nmodes;		/* size of cam_mode */
	u8 no_urb_create;	/* don't create transfer URBs */
	u8 bulk_nurbs;		/* number of URBs in bulk mode
				 * - cannot be > MAX_NURBS
				 * - when 0 and bulk_size != 0 means
				 *   1 URB and submit done by subdriver */
	u8 bulk;		/* image transfer by 0:isoc / 1:bulk */
	u8 npkt;		/* number of packets in an ISOC message
				 * 0 is the default value: 32 packets */
	u8 needs_full_bandwidth;/* Set this flag to notify the bandwidth calc.
				 * code that the cam fills all image buffers to
				 * the max, even when using compression. */
};

struct gspca_dev;
struct gspca_frame;

/* subdriver operations */
typedef int (*cam_op) (struct gspca_dev *);
typedef void (*cam_v_op) (struct gspca_dev *);
typedef int (*cam_cf_op) (struct gspca_dev *, const struct usb_device_id *);
typedef int (*cam_get_jpg_op) (struct gspca_dev *,
				struct v4l2_jpegcompression *);
typedef int (*cam_set_jpg_op) (struct gspca_dev *,
				const struct v4l2_jpegcompression *);
typedef int (*cam_get_reg_op) (struct gspca_dev *,
				struct v4l2_dbg_register *);
typedef int (*cam_set_reg_op) (struct gspca_dev *,
				const struct v4l2_dbg_register *);
typedef int (*cam_chip_info_op) (struct gspca_dev *,
				struct v4l2_dbg_chip_info *);
typedef void (*cam_streamparm_op) (struct gspca_dev *,
				  struct v4l2_streamparm *);
typedef void (*cam_pkt_op) (struct gspca_dev *gspca_dev,
				u8 *data,
				int len);
typedef int (*cam_int_pkt_op) (struct gspca_dev *gspca_dev,
				u8 *data,
				int len);
typedef void (*cam_format_op) (struct gspca_dev *gspca_dev,
				struct v4l2_format *fmt);
typedef int (*cam_frmsize_op) (struct gspca_dev *gspca_dev,
				struct v4l2_frmsizeenum *fsize);

/* subdriver description */
struct sd_desc {
/* information */
	const char *name;	/* sub-driver name */
/* mandatory operations */
	cam_cf_op config;	/* called on probe */
	cam_op init;		/* called on probe and resume */
	cam_op init_controls;	/* called on probe */
	cam_v_op probe_error;	/* called if probe failed, do cleanup here */
	cam_op start;		/* called on stream on after URBs creation */
	cam_pkt_op pkt_scan;
/* optional operations */
	cam_op isoc_init;	/* called on stream on before getting the EP */
	cam_op isoc_nego;	/* called when URB submit failed with NOSPC */
	cam_v_op stopN;		/* called on stream off - main alt */
	cam_v_op stop0;		/* called on stream off & disconnect - alt 0 */
	cam_v_op dq_callback;	/* called when a frame has been dequeued */
	cam_get_jpg_op get_jcomp;
	cam_set_jpg_op set_jcomp;
	cam_streamparm_op get_streamparm;
	cam_streamparm_op set_streamparm;
	cam_format_op try_fmt;
	cam_frmsize_op enum_framesizes;
#ifdef CONFIG_VIDEO_ADV_DEBUG
	cam_set_reg_op set_register;
	cam_get_reg_op get_register;
	cam_chip_info_op get_chip_info;
#endif
#if IS_ENABLED(CONFIG_INPUT)
	cam_int_pkt_op int_pkt_scan;
	/* other_input makes the gspca core create gspca_dev->input even when
	   int_pkt_scan is NULL, for cams with non interrupt driven buttons */
	u8 other_input;
#endif
};

/* packet types when moving from iso buf to frame buf */
enum gspca_packet_type {
	DISCARD_PACKET,
	FIRST_PACKET,
	INTER_PACKET,
	LAST_PACKET
};

struct gspca_buffer {
	struct vb2_v4l2_buffer vb;
	struct list_head list;
};

static inline struct gspca_buffer *to_gspca_buffer(struct vb2_buffer *vb2)
{
	return container_of(vb2, struct gspca_buffer, vb.vb2_buf);
}

struct gspca_dev {
	struct video_device vdev;	/* !! must be the first item */
	struct module *module;		/* subdriver handling the device */
	struct v4l2_device v4l2_dev;
	struct usb_device *dev;

#if IS_ENABLED(CONFIG_INPUT)
	struct input_dev *input_dev;
	char phys[64];			/* physical device path */
#endif

	struct cam cam;				/* device information */
	const struct sd_desc *sd_desc;		/* subdriver description */
	struct v4l2_ctrl_handler ctrl_handler;

	/* autogain and exposure or gain control cluster, these are global as
	   the autogain/exposure functions in autogain_functions.c use them */
	struct {
		struct v4l2_ctrl *autogain;
		struct v4l2_ctrl *exposure;
		struct v4l2_ctrl *gain;
		int exp_too_low_cnt, exp_too_high_cnt;
	};

#define USB_BUF_SZ 64
	__u8 *usb_buf;				/* buffer for USB exchanges */
	struct urb *urb[MAX_NURBS];
#if IS_ENABLED(CONFIG_INPUT)
	struct urb *int_urb;
#endif

	u8 *image;				/* image being filled */
	u32 image_len;				/* current length of image */
	__u8 last_packet_type;
	__s8 empty_packet;		/* if (-1) don't check empty packets */
	bool streaming;

	__u8 curr_mode;			/* current camera mode */
	struct v4l2_pix_format pixfmt;	/* current mode parameters */
	__u32 sequence;			/* frame sequence number */

	struct vb2_queue queue;

	spinlock_t qlock;
	struct list_head buf_list;

	wait_queue_head_t wq;		/* wait queue */
	struct mutex usb_lock;		/* usb exchange protection */
	int usb_err;			/* USB error - protected by usb_lock */
	u16 pkt_size;			/* ISOC packet size */
#ifdef CONFIG_PM
	char frozen;			/* suspend - resume */
#endif
	bool present;
	char memory;			/* memory type (V4L2_MEMORY_xxx) */
	__u8 iface;			/* USB interface number */
	__u8 alt;			/* USB alternate setting */
	int xfer_ep;			/* USB transfer endpoint address */
	u8 audio;			/* presence of audio device */

	/* (*) These variables are proteced by both usb_lock and queue_lock,
	   that is any code setting them is holding *both*, which means that
	   any code getting them needs to hold at least one of them */
};

int gspca_dev_probe(struct usb_interface *intf,
		const struct usb_device_id *id,
		const struct sd_desc *sd_desc,
		int dev_size,
		struct module *module);
int gspca_dev_probe2(struct usb_interface *intf,
		const struct usb_device_id *id,
		const struct sd_desc *sd_desc,
		int dev_size,
		struct module *module);
void gspca_disconnect(struct usb_interface *intf);
void gspca_frame_add(struct gspca_dev *gspca_dev,
			enum gspca_packet_type packet_type,
			const u8 *data,
			int len);
#ifdef CONFIG_PM
int gspca_suspend(struct usb_interface *intf, pm_message_t message);
int gspca_resume(struct usb_interface *intf);
#endif
int gspca_expo_autogain(struct gspca_dev *gspca_dev, int avg_lum,
	int desired_avg_lum, int deadzone, int gain_knee, int exposure_knee);
int gspca_coarse_grained_expo_autogain(struct gspca_dev *gspca_dev,
	int avg_lum, int desired_avg_lum, int deadzone);

#endif /* GSPCAV2_H */
