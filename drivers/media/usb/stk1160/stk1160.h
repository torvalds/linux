/*
 * STK1160 driver
 *
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 * Based on Easycap driver by R.M. Thomas
 *	Copyright (C) 2010 R.M. Thomas
 *	<rmthomas--a.t--sciolus.org>
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
 */

#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/ac97_codec.h>
#include <media/videobuf2-v4l2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#define STK1160_VERSION		"0.9.5"
#define STK1160_VERSION_NUM	0x000905

/* Decide on number of packets for each buffer */
#define STK1160_NUM_PACKETS 64

/* Number of buffers for isoc transfers */
#define STK1160_NUM_BUFS 16
#define STK1160_MIN_BUFS 1

/* TODO: This endpoint address should be retrieved */
#define STK1160_EP_VIDEO 0x82
#define STK1160_EP_AUDIO 0x81

/* Max and min video buffers */
#define STK1160_MIN_VIDEO_BUFFERS 8
#define STK1160_MAX_VIDEO_BUFFERS 32

#define STK1160_MIN_PKT_SIZE 3072

#define STK1160_MAX_INPUT 4
#define STK1160_SVIDEO_INPUT 4

#define STK1160_AC97_TIMEOUT 50

#define STK1160_I2C_TIMEOUT 100

/* TODO: Print helpers
 * I could use dev_xxx, pr_xxx, v4l2_xxx or printk.
 * However, there isn't a solid consensus on which
 * new drivers should use.
 *
 */
#ifdef DEBUG
#define stk1160_dbg(fmt, args...) \
	printk(KERN_DEBUG "stk1160: " fmt,  ## args)
#else
#define stk1160_dbg(fmt, args...)
#endif

#define stk1160_info(fmt, args...) \
	pr_info("stk1160: " fmt, ## args)

#define stk1160_warn(fmt, args...) \
	pr_warn("stk1160: " fmt, ## args)

#define stk1160_err(fmt, args...) \
	pr_err("stk1160: " fmt, ## args)

/* Buffer for one video frame */
struct stk1160_buffer {
	/* common v4l buffer stuff -- must be first */
	struct vb2_v4l2_buffer vb;
	struct list_head list;

	void *mem;
	unsigned int length;		/* buffer length */
	unsigned int bytesused;		/* bytes written */
	int odd;			/* current oddity */

	/*
	 * Since we interlace two fields per frame,
	 * this is different from bytesused.
	 */
	unsigned int pos;		/* current pos inside buffer */
};

struct stk1160_isoc_ctl {
	/* max packet size of isoc transaction */
	int max_pkt_size;

	/* number of allocated urbs */
	int num_bufs;

	/* urb for isoc transfers */
	struct urb **urb;

	/* transfer buffers for isoc transfer */
	char **transfer_buffer;

	/* current buffer */
	struct stk1160_buffer *buf;
};

struct stk1160_fmt {
	char  *name;
	u32   fourcc;          /* v4l2 format id */
	int   depth;
};

struct stk1160 {
	struct v4l2_device v4l2_dev;
	struct video_device vdev;
	struct v4l2_ctrl_handler ctrl_handler;

	struct device *dev;
	struct usb_device *udev;

	/* saa7115 subdev */
	struct v4l2_subdev *sd_saa7115;

	/* isoc control struct */
	struct list_head avail_bufs;

	/* video capture */
	struct vb2_queue vb_vidq;

	/* max packet size of isoc transaction */
	int max_pkt_size;
	/* array of wMaxPacketSize */
	unsigned int *alt_max_pkt_size;
	/* alternate */
	int alt;
	/* Number of alternative settings */
	int num_alt;

	struct stk1160_isoc_ctl isoc_ctl;

	/* frame properties */
	int width;		  /* current frame width */
	int height;		  /* current frame height */
	unsigned int ctl_input;	  /* selected input */
	v4l2_std_id norm;	  /* current norm */
	struct stk1160_fmt *fmt;  /* selected format */

	unsigned int sequence;

	/* i2c i/o */
	struct i2c_adapter i2c_adap;
	struct i2c_client i2c_client;

	struct mutex v4l_lock;
	struct mutex vb_queue_lock;
	spinlock_t buf_lock;

	struct file *fh_owner;	/* filehandle ownership */

	/* EXPERIMENTAL */
	struct snd_card *snd_card;
};

struct regval {
	u16 reg;
	u16 val;
};

/* Provided by stk1160-v4l.c */
int stk1160_vb2_setup(struct stk1160 *dev);
int stk1160_video_register(struct stk1160 *dev);
void stk1160_video_unregister(struct stk1160 *dev);
void stk1160_clear_queue(struct stk1160 *dev);

/* Provided by stk1160-video.c */
int stk1160_alloc_isoc(struct stk1160 *dev);
void stk1160_free_isoc(struct stk1160 *dev);
void stk1160_cancel_isoc(struct stk1160 *dev);
void stk1160_uninit_isoc(struct stk1160 *dev);

/* Provided by stk1160-i2c.c */
int stk1160_i2c_register(struct stk1160 *dev);
int stk1160_i2c_unregister(struct stk1160 *dev);

/* Provided by stk1160-core.c */
int stk1160_read_reg(struct stk1160 *dev, u16 reg, u8 *value);
int stk1160_write_reg(struct stk1160 *dev, u16 reg, u16 value);
int stk1160_write_regs_req(struct stk1160 *dev, u8 req, u16 reg,
		char *buf, int len);
int stk1160_read_reg_req_len(struct stk1160 *dev, u8 req, u16 reg,
		char *buf, int len);
void stk1160_select_input(struct stk1160 *dev);

/* Provided by stk1160-ac97.c */
void stk1160_ac97_setup(struct stk1160 *dev);
