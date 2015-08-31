/*
 *  Driver for the Auvitek AU0828 USB bridge
 *
 *  Copyright (c) 2008 Steven Toth <stoth@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/usb.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <media/tveeprom.h>

/* Analog */
#include <linux/videodev2.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-fh.h>
#include <media/media-device.h>

/* DVB */
#include "demux.h"
#include "dmxdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"
#include "dvbdev.h"

#include "au0828-reg.h"
#include "au0828-cards.h"

#define URB_COUNT   16
#define URB_BUFSIZE (0xe522)

/* Analog constants */
#define NTSC_STD_W      720
#define NTSC_STD_H      480

#define AU0828_INTERLACED_DEFAULT       1
#define V4L2_CID_PRIVATE_SHARPNESS  (V4L2_CID_PRIVATE_BASE + 0)

/* Defination for AU0828 USB transfer */
#define AU0828_MAX_ISO_BUFS    12  /* maybe resize this value in the future */
#define AU0828_ISO_PACKETS_PER_URB      128

#define AU0828_MIN_BUF 4
#define AU0828_DEF_BUF 8

#define AU0828_MAX_INPUT        4

/* au0828 resource types (used for res_get/res_lock etc */
#define AU0828_RESOURCE_VIDEO 0x01
#define AU0828_RESOURCE_VBI   0x02

enum au0828_itype {
	AU0828_VMUX_UNDEFINED = 0,
	AU0828_VMUX_COMPOSITE,
	AU0828_VMUX_SVIDEO,
	AU0828_VMUX_CABLE,
	AU0828_VMUX_TELEVISION,
	AU0828_VMUX_DVB,
	AU0828_VMUX_DEBUG
};

struct au0828_input {
	enum au0828_itype type;
	unsigned int vmux;
	unsigned int amux;
	void (*audio_setup) (void *priv, int enable);
};

struct au0828_board {
	char *name;
	unsigned int tuner_type;
	unsigned char tuner_addr;
	unsigned char i2c_clk_divider;
	unsigned char has_ir_i2c:1;
	unsigned char has_analog:1;
	struct au0828_input input[AU0828_MAX_INPUT];
};

struct au0828_dvb {
	struct mutex lock;
	struct dvb_adapter adapter;
	struct dvb_frontend *frontend;
	struct dvb_demux demux;
	struct dmxdev dmxdev;
	struct dmx_frontend fe_hw;
	struct dmx_frontend fe_mem;
	struct dvb_net net;
	int feeding;
	int start_count;
	int stop_count;

	int (*set_frontend)(struct dvb_frontend *fe);
};

enum au0828_stream_state {
	STREAM_OFF,
	STREAM_INTERRUPT,
	STREAM_ON
};

#define AUVI_INPUT(nr) (dev->board.input[nr])

/* device state */
enum au0828_dev_state {
	DEV_INITIALIZED = 0x01,
	DEV_DISCONNECTED = 0x02,
	DEV_MISCONFIGURED = 0x04
};

struct au0828_dev;

struct au0828_usb_isoc_ctl {
		/* max packet size of isoc transaction */
	int				max_pkt_size;

		/* number of allocated urbs */
	int				num_bufs;

		/* urb for isoc transfers */
	struct urb			**urb;

		/* transfer buffers for isoc transfer */
	char				**transfer_buffer;

		/* Last buffer command and region */
	u8				cmd;
	int				pos, size, pktsize;

		/* Last field: ODD or EVEN? */
	int				field;

		/* Stores incomplete commands */
	u32				tmp_buf;
	int				tmp_buf_len;

		/* Stores already requested buffers */
	struct au0828_buffer		*buf;
	struct au0828_buffer		*vbi_buf;

		/* Stores the number of received fields */
	int				nfields;

		/* isoc urb callback */
	int (*isoc_copy) (struct au0828_dev *dev, struct urb *urb);

};

/* buffer for one video frame */
struct au0828_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer vb;
	struct list_head list;

	void *mem;
	unsigned long length;
	int top_field;
	/* pointer to vmalloc memory address in vb */
	char *vb_buf;
};

struct au0828_dmaqueue {
	struct list_head       active;
	/* Counters to control buffer fill */
	int                    pos;
};

struct au0828_dev {
	struct mutex mutex;
	struct usb_device	*usbdev;
	int			boardnr;
	struct au0828_board	board;
	u8			ctrlmsg[64];

	/* I2C */
	struct i2c_adapter		i2c_adap;
	struct i2c_algorithm		i2c_algo;
	struct i2c_client		i2c_client;
	u32 				i2c_rc;

	/* Digital */
	struct au0828_dvb		dvb;
	struct work_struct              restart_streaming;

#ifdef CONFIG_VIDEO_AU0828_V4L2
	/* Analog */
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler v4l2_ctrl_hdl;
#endif
#ifdef CONFIG_VIDEO_AU0828_RC
	struct au0828_rc *ir;
#endif

	struct video_device vdev;
	struct video_device vbi_dev;

	/* Videobuf2 */
	struct vb2_queue vb_vidq;
	struct vb2_queue vb_vbiq;
	struct mutex vb_queue_lock;
	struct mutex vb_vbi_queue_lock;

	unsigned int frame_count;
	unsigned int vbi_frame_count;

	struct timer_list vid_timeout;
	int vid_timeout_running;
	struct timer_list vbi_timeout;
	int vbi_timeout_running;

	int users;
	int streaming_users;

	int width;
	int height;
	int vbi_width;
	int vbi_height;
	u32 vbi_read;
	v4l2_std_id std;
	u32 field_size;
	u32 frame_size;
	u32 bytesperline;
	int type;
	u8 ctrl_ainput;
	__u8 isoc_in_endpointaddr;
	u8 isoc_init_ok;
	int greenscreen_detected;
	int ctrl_freq;
	int input_type;
	int std_set_in_tuner_core;
	unsigned int ctrl_input;
	enum au0828_dev_state dev_state;
	enum au0828_stream_state stream_state;
	wait_queue_head_t open;

	struct mutex lock;

	/* Isoc control struct */
	struct au0828_dmaqueue vidq;
	struct au0828_dmaqueue vbiq;
	struct au0828_usb_isoc_ctl isoc_ctl;
	spinlock_t slock;

	/* usb transfer */
	int alt;		/* alternate */
	int max_pkt_size;	/* max packet size of isoc transaction */
	int num_alt;		/* Number of alternative settings */
	unsigned int *alt_max_pkt_size;	/* array of wMaxPacketSize */
	struct urb *urb[AU0828_MAX_ISO_BUFS];	/* urb for isoc transfers */
	char *transfer_buffer[AU0828_MAX_ISO_BUFS];/* transfer buffers for isoc
						   transfer */

	/* DVB USB / URB Related */
	bool		urb_streaming, need_urb_start;
	struct urb	*urbs[URB_COUNT];

	/* Preallocated transfer digital transfer buffers */

	char *dig_transfer_buffer[URB_COUNT];

#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_device *media_dev;
	struct media_pad video_pad, vbi_pad;
	struct media_entity *decoder;
	struct media_entity input_ent[AU0828_MAX_INPUT];
	struct media_pad input_pad[AU0828_MAX_INPUT];
#endif
};


/* ----------------------------------------------------------- */
#define au0828_read(dev, reg) au0828_readreg(dev, reg)
#define au0828_write(dev, reg, value) au0828_writereg(dev, reg, value)
#define au0828_andor(dev, reg, mask, value) 				\
	 au0828_writereg(dev, reg, 					\
	(au0828_readreg(dev, reg) & ~(mask)) | ((value) & (mask)))

#define au0828_set(dev, reg, bit) au0828_andor(dev, (reg), (bit), (bit))
#define au0828_clear(dev, reg, bit) au0828_andor(dev, (reg), (bit), 0)

/* ----------------------------------------------------------- */
/* au0828-core.c */
extern u32 au0828_read(struct au0828_dev *dev, u16 reg);
extern u32 au0828_write(struct au0828_dev *dev, u16 reg, u32 val);
extern int au0828_debug;

/* ----------------------------------------------------------- */
/* au0828-cards.c */
extern struct au0828_board au0828_boards[];
extern struct usb_device_id au0828_usb_id_table[];
extern void au0828_gpio_setup(struct au0828_dev *dev);
extern int au0828_tuner_callback(void *priv, int component,
				 int command, int arg);
extern void au0828_card_setup(struct au0828_dev *dev);

/* ----------------------------------------------------------- */
/* au0828-i2c.c */
extern int au0828_i2c_register(struct au0828_dev *dev);
extern int au0828_i2c_unregister(struct au0828_dev *dev);

/* ----------------------------------------------------------- */
/* au0828-video.c */
extern int au0828_analog_register(struct au0828_dev *dev,
			   struct usb_interface *interface);
extern void au0828_analog_unregister(struct au0828_dev *dev);
extern int au0828_start_analog_streaming(struct vb2_queue *vq,
						unsigned int count);
extern void au0828_stop_vbi_streaming(struct vb2_queue *vq);
#ifdef CONFIG_VIDEO_AU0828_V4L2
extern void au0828_v4l2_suspend(struct au0828_dev *dev);
extern void au0828_v4l2_resume(struct au0828_dev *dev);
#else
static inline void au0828_v4l2_suspend(struct au0828_dev *dev) { };
static inline void au0828_v4l2_resume(struct au0828_dev *dev) { };
#endif

/* ----------------------------------------------------------- */
/* au0828-dvb.c */
extern int au0828_dvb_register(struct au0828_dev *dev);
extern void au0828_dvb_unregister(struct au0828_dev *dev);
void au0828_dvb_suspend(struct au0828_dev *dev);
void au0828_dvb_resume(struct au0828_dev *dev);

/* au0828-vbi.c */
extern struct vb2_ops au0828_vbi_qops;

#define dprintk(level, fmt, arg...)\
	do { if (au0828_debug & level)\
		printk(KERN_DEBUG pr_fmt(fmt), ## arg);\
	} while (0)

/* au0828-input.c */
#ifdef CONFIG_VIDEO_AU0828_RC
extern int au0828_rc_register(struct au0828_dev *dev);
extern void au0828_rc_unregister(struct au0828_dev *dev);
extern int au0828_rc_suspend(struct au0828_dev *dev);
extern int au0828_rc_resume(struct au0828_dev *dev);
#else
static inline int au0828_rc_register(struct au0828_dev *dev) { return 0; }
static inline void au0828_rc_unregister(struct au0828_dev *dev) { }
static inline int au0828_rc_suspend(struct au0828_dev *dev) { return 0; }
static inline int au0828_rc_resume(struct au0828_dev *dev) { return 0; }
#endif
