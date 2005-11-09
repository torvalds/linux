/*
   em2820-cards.c - driver for Empia EM2820/2840 USB video capture devices

   Copyright (C) 2005 Markus Rechberger <mrechberger@gmail.com>
                      Ludovico Cavedon <cavedon@sssup.it>
                      Mauro Carvalho Chehab <mchehab@brturbo.com.br>

   Based on the em2800 driver from Sascha Sommer <saschasommer@freenet.de>

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
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _EM2820_H
#define _EM2820_H

#include <linux/videodev.h>
#include <linux/i2c.h>

/* maximum number of frames that can be queued */
#define EM2820_NUM_FRAMES 5
/* number of frames that get used for v4l2_read() */
#define EM2820_NUM_READ_FRAMES 2

/* number of buffers for isoc transfers */
#define EM2820_NUM_BUFS 5

/* number of packets for each buffer */
// windows requests only 40 packets .. so we better do the same
// this is what I found out for all alternate numbers there!

#define EM2820_NUM_PACKETS 40

/* packet size for each packet */
/* no longer needed: read from endpoint descriptor */
//#define EM2820_MAX_PACKET_SIZE 3072 //7
//#define EM2820_MAX_PACKET_SIZE 2892 //6
//#define EM2820_MAX_PACKET_SIZE 2580 //5
//#define EM2820_MAX_PACKET_SIZE 1448 //2

/* default alternate; 0 means choose the best */
#define EM2820_PINOUT 0
#define EM2820_MAX_ALT 7

#define EM2820_INTERLACED_DEFAULT 1

/*
#define (use usbview if you want to get the other alternate number infos)
#define
#define alternate number 2
#define 			Endpoint Address: 82
			Direction: in
			Attribute: 1
			Type: Isoc
			Max Packet Size: 1448
			Interval: 125us

  alternate number 7

			Endpoint Address: 82
			Direction: in
			Attribute: 1
			Type: Isoc
			Max Packet Size: 3072
			Interval: 125us
*/

/* time to wait when stopping the isoc transfer */
#define EM2820_URB_TIMEOUT       msecs_to_jiffies(EM2820_NUM_BUFS * EM2820_NUM_PACKETS)

/* the various frame states */
enum em2820_frame_state {
	F_UNUSED = 0,
	F_QUEUED,
	F_GRABBING,
	F_DONE,
	F_ERROR,
};

/* stream states */
enum em2820_stream_state {
	STREAM_OFF,
	STREAM_INTERRUPT,
	STREAM_ON,
};

/* frames */
struct em2820_frame_t {
	void *bufmem;
	struct v4l2_buffer buf;
	enum em2820_frame_state state;
	struct list_head frame;
	unsigned long vma_use_count;
	int top_field;
	int fieldbytesused;
};

/* io methods */
enum em2820_io_method {
	IO_NONE,
	IO_READ,
	IO_MMAP,
};

/* inputs */

#define MAX_EM2820_INPUT 4
enum enum2820_itype {
	EM2820_VMUX_COMPOSITE1 = 1,
	EM2820_VMUX_COMPOSITE2,
	EM2820_VMUX_COMPOSITE3,
	EM2820_VMUX_COMPOSITE4,
	EM2820_VMUX_SVIDEO,
	EM2820_VMUX_TELEVISION,
	EM2820_VMUX_CABLE,
	EM2820_VMUX_DVB,
	EM2820_VMUX_DEBUG,
	EM2820_RADIO,
};

struct em2820_input {
	enum enum2820_itype type;
	unsigned int vmux;
	unsigned int amux;
};

#define INPUT(nr) (&em2820_boards[dev->model].input[nr])

enum em2820_decoder {
	EM2820_TVP5150,
	EM2820_SAA7113,
	EM2820_SAA7114
};

struct em2820_board {
	char *name;

	int vchannels;
	int norm;
	int tuner_type;

	/* i2c flags */
	unsigned int tda9887_conf;

	unsigned int has_tuner:1;
	unsigned int has_msp34xx:1;

	enum em2820_decoder decoder;

	struct em2820_input       input[MAX_EM2820_INPUT];
};

struct em2820_eeprom {
	u32 id;			/* 0x9567eb1a */
	u16 vendor_ID;
	u16 product_ID;

	u16 chip_conf;

	u16 board_conf;

	u16 string1, string2, string3;

	u8 string_idx_table;
};

/* device states */
enum em2820_dev_state {
	DEV_INITIALIZED = 0x01,
	DEV_DISCONNECTED = 0x02,
	DEV_MISCONFIGURED = 0x04,
};

/* tvnorms */
struct em2820_tvnorm {
	char *name;
	v4l2_std_id id;
	/* mode for saa7113h */
	int mode;
};

/* main device struct */
struct em2820 {
	/* generic device properties */
	char name[30];		/* name (including minor) of the device */
	int model;		/* index in the device_data struct */
	int video_inputs;	/* number of video inputs */
	unsigned int has_tuner:1;
	unsigned int has_msp34xx:1;
	unsigned int has_tda9887:1;

	enum em2820_decoder decoder;

	int tuner_type;		/* type of the tuner */
	int tuner_addr;		/* tuner address */
	int tda9887_conf;
	/* i2c i/o */
	struct i2c_adapter i2c_adap;
	struct i2c_client i2c_client;
	/* video for linux */
	int users;		/* user count for exclusive use */
	struct video_device *vdev;	/* video for linux device struct */
	struct video_picture vpic;	/* picture settings only used to init saa7113h */
	struct em2820_tvnorm *tvnorm;	/* selected tv norm */
	int ctl_freq;		/* selected frequency */
	unsigned int ctl_input;	/* selected input */
	unsigned int ctl_ainput;	/* slected audio input */
	int mute;
	int volume;
	/* frame properties */
	struct em2820_frame_t frame[EM2820_NUM_FRAMES];	/* list of frames */
	int num_frames;		/* number of frames currently in use */
	unsigned int frame_count;	/* total number of transfered frames */
	struct em2820_frame_t *frame_current;	/* the frame that is being filled */
	int width;		/* current frame width */
	int height;		/* current frame height */
	int frame_size;		/* current frame size */
	int field_size;		/* current field size */
	int bytesperline;
	int hscale;		/* horizontal scale factor (see datasheet) */
	int vscale;		/* vertical scale factor (see datasheet) */
	int interlaced;		/* 1=interlace fileds, 0=just top fileds */
	int type;

	/* states */
	enum em2820_dev_state state;
	enum em2820_stream_state stream;
	enum em2820_io_method io;
	/* locks */
	struct semaphore lock, fileop_lock;
	spinlock_t queue_lock;
	struct list_head inqueue, outqueue;
	wait_queue_head_t open, wait_frame, wait_stream;
	struct video_device *vbi_dev;

	unsigned char eedata[256];

	/* usb transfer */
	struct usb_device *udev;	/* the usb device */
	int alt;		/* alternate */
	int max_pkt_size;	/* max packet size of isoc transaction */
	unsigned int alt_max_pkt_size[EM2820_MAX_ALT + 1];	/* array of wMaxPacketSize */
	struct urb *urb[EM2820_NUM_BUFS];	/* urb for isoc transfers */
	char *transfer_buffer[EM2820_NUM_BUFS];	/* transfer buffers for isoc transfer */
	/* helper funcs that call usb_control_msg */
	int (*em2820_write_regs) (struct em2820 * dev, u16 reg, char *buf,
				  int len);
	int (*em2820_read_reg) (struct em2820 * dev, u16 reg);
	int (*em2820_read_reg_req_len) (struct em2820 * dev, u8 req, u16 reg,
					char *buf, int len);
	int (*em2820_write_regs_req) (struct em2820 * dev, u8 req, u16 reg,
				      char *buf, int len);
	int (*em2820_read_reg_req) (struct em2820 * dev, u8 req, u16 reg);
};

/* Provided by em2820-i2c.c */

void em2820_i2c_call_clients(struct em2820 *dev, unsigned int cmd, void *arg);
int em2820_i2c_register(struct em2820 *dev);
int em2820_i2c_unregister(struct em2820 *dev);

/* Provided by em2820-core.c */

void em2820_print_ioctl(char *name, unsigned int cmd);

u32 em2820_request_buffers(struct em2820 *dev, u32 count);
void em2820_queue_unusedframes(struct em2820 *dev);
void em2820_release_buffers(struct em2820 *dev);

int em2820_read_reg_req_len(struct em2820 *dev, u8 req, u16 reg,
			    char *buf, int len);
int em2820_read_reg_req(struct em2820 *dev, u8 req, u16 reg);
int em2820_read_reg(struct em2820 *dev, u16 reg);
int em2820_write_regs_req(struct em2820 *dev, u8 req, u16 reg, char *buf,
			  int len);
int em2820_write_regs(struct em2820 *dev, u16 reg, char *buf, int len);
int em2820_write_reg_bits(struct em2820 *dev, u16 reg, u8 val,
			  u8 bitmask);
int em2820_write_ac97(struct em2820 *dev, u8 reg, u8 * val);
int em2820_audio_analog_set(struct em2820 *dev);
int em2820_colorlevels_set_default(struct em2820 *dev);
int em2820_capture_start(struct em2820 *dev, int start);
int em2820_outfmt_set_yuv422(struct em2820 *dev);
int em2820_accumulator_set(struct em2820 *dev, u8 xmin, u8 xmax, u8 ymin,
			   u8 ymax);
int em2820_capture_area_set(struct em2820 *dev, u8 hstart, u8 vstart,
			    u16 width, u16 height);
int em2820_scaler_set(struct em2820 *dev, u16 h, u16 v);
int em2820_resolution_set(struct em2820 *dev);
void em2820_isocIrq(struct urb *urb, struct pt_regs *regs);
int em2820_init_isoc(struct em2820 *dev);
void em2820_uninit_isoc(struct em2820 *dev);
int em2820_set_alternate(struct em2820 *dev);

/* Provided by em2820-cards.c */
extern void em2820_card_setup(struct em2820 *dev);
extern struct em2820_board em2820_boards[];
extern struct usb_device_id em2820_id_table[];

/* em2820 registers */
#define USBSUSP_REG	0x0c	/* */

#define AUDIOSRC_REG	0x0e
#define XCLK_REG	0x0f

#define VINMODE_REG	0x10
#define VINCTRL_REG	0x11
#define VINENABLE_REG	0x12	/* */

#define GAMMA_REG	0x14
#define RGAIN_REG	0x15
#define GGAIN_REG	0x16
#define BGAIN_REG	0x17
#define ROFFSET_REG	0x18
#define GOFFSET_REG	0x19
#define BOFFSET_REG	0x1a

#define OFLOW_REG	0x1b
#define HSTART_REG	0x1c
#define VSTART_REG	0x1d
#define CWIDTH_REG	0x1e
#define CHEIGHT_REG	0x1f

#define YGAIN_REG	0x20
#define YOFFSET_REG	0x21
#define UVGAIN_REG	0x22
#define UOFFSET_REG	0x23
#define VOFFSET_REG	0x24
#define SHARPNESS_REG	0x25

#define COMPR_REG	0x26
#define OUTFMT_REG	0x27

#define XMIN_REG	0x28
#define XMAX_REG	0x29
#define YMIN_REG	0x2a
#define YMAX_REG	0x2b

#define HSCALELOW_REG	0x30
#define HSCALEHIGH_REG	0x31
#define VSCALELOW_REG	0x32
#define VSCALEHIGH_REG	0x33

#define AC97LSB_REG	0x40
#define AC97MSB_REG	0x41
#define AC97ADDR_REG	0x42
#define AC97BUSY_REG	0x43

/* em202 registers */
#define MASTER_AC97	0x02
#define VIDEO_AC97	0x14

/* register settings */
#define EM2820_AUDIO_SRC_TUNER	0xc0
#define EM2820_AUDIO_SRC_LINE	0x80

/* printk macros */

#define em2820_err(fmt, arg...) do {\
        printk(KERN_ERR fmt , ##arg); } while (0)

#define em2820_errdev(fmt, arg...) do {\
        printk(KERN_ERR "%s: "fmt,\
			dev->name , ##arg); } while (0)

#define em2820_info(fmt, arg...) do {\
        printk(KERN_INFO "%s: "fmt,\
			dev->name , ##arg); } while (0)
#define em2820_warn(fmt, arg...) do {\
        printk(KERN_WARNING "%s: "fmt,\
			dev->name , ##arg); } while (0)

inline static int em2820_audio_source(struct em2820 *dev, int input)
{
	return em2820_write_reg_bits(dev, AUDIOSRC_REG, input, 0xc0);
}

inline static int em2820_audio_usb_mute(struct em2820 *dev, int mute)
{
	return em2820_write_reg_bits(dev, XCLK_REG, mute ? 0x00 : 0x80, 0x80);
}

inline static int em2820_audio_analog_setup(struct em2820 *dev)
{
	/* unmute video mixer with default volume level */
	return em2820_write_ac97(dev, VIDEO_AC97, "\x08\x08");
}

inline static int em2820_compression_disable(struct em2820 *dev)
{
	/* side effect of disabling scaler and mixer */
	return em2820_write_regs(dev, COMPR_REG, "\x00", 1);
}

inline static int em2820_contrast_get(struct em2820 *dev)
{
	return em2820_read_reg(dev, YGAIN_REG) & 0x1f;
}

inline static int em2820_brightness_get(struct em2820 *dev)
{
	return em2820_read_reg(dev, YOFFSET_REG);
}

inline static int em2820_saturation_get(struct em2820 *dev)
{
	return em2820_read_reg(dev, UVGAIN_REG) & 0x1f;
}

inline static int em2820_u_balance_get(struct em2820 *dev)
{
	return em2820_read_reg(dev, UOFFSET_REG);
}

inline static int em2820_v_balance_get(struct em2820 *dev)
{
	return em2820_read_reg(dev, VOFFSET_REG);
}

inline static int em2820_gamma_get(struct em2820 *dev)
{
	return em2820_read_reg(dev, GAMMA_REG) & 0x3f;
}

inline static int em2820_contrast_set(struct em2820 *dev, s32 val)
{
	u8 tmp = (u8) val;
	return em2820_write_regs(dev, YGAIN_REG, &tmp, 1);
}

inline static int em2820_brightness_set(struct em2820 *dev, s32 val)
{
	u8 tmp = (u8) val;
	return em2820_write_regs(dev, YOFFSET_REG, &tmp, 1);
}

inline static int em2820_saturation_set(struct em2820 *dev, s32 val)
{
	u8 tmp = (u8) val;
	return em2820_write_regs(dev, UVGAIN_REG, &tmp, 1);
}

inline static int em2820_u_balance_set(struct em2820 *dev, s32 val)
{
	u8 tmp = (u8) val;
	return em2820_write_regs(dev, UOFFSET_REG, &tmp, 1);
}

inline static int em2820_v_balance_set(struct em2820 *dev, s32 val)
{
	u8 tmp = (u8) val;
	return em2820_write_regs(dev, VOFFSET_REG, &tmp, 1);
}

inline static int em2820_gamma_set(struct em2820 *dev, s32 val)
{
	u8 tmp = (u8) val;
	return em2820_write_regs(dev, GAMMA_REG, &tmp, 1);
}

/*FIXME: maxw should be dependent of alt mode */
#define norm_maxw(dev) 720
inline static unsigned int norm_maxh(struct em2820 *dev)
{
	return (dev->tvnorm->id & V4L2_STD_625_50) ? 576 : 480;
}

#endif
