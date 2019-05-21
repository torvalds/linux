/****************************************************************************
 *
 *  Filename: cpia2.h
 *
 *  Copyright 2001, STMicrolectronics, Inc.
 *
 *  Contact:  steve.miller@st.com
 *
 *  Description:
 *     This is a USB driver for CPiA2 based video cameras.
 *
 *     This driver is modelled on the cpia usb driver by
 *     Jochen Scharrlach and Johannes Erdfeldt.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 ****************************************************************************/

#ifndef __CPIA2_H__
#define __CPIA2_H__

#include <linux/videodev2.h>
#include <linux/usb.h>
#include <linux/poll.h>
#include <media/v4l2-common.h>
#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>

#include "cpia2_registers.h"

/* define for verbose debug output */
//#define _CPIA2_DEBUG_

/***
 * Image defines
 ***/

/*  Misc constants */
#define ALLOW_CORRUPT 0		/* Causes collater to discard checksum */

/* USB Transfer mode */
#define XFER_ISOC 0
#define XFER_BULK 1

/* USB Alternates */
#define USBIF_CMDONLY 0
#define USBIF_BULK 1
#define USBIF_ISO_1 2	/*  128 bytes/ms */
#define USBIF_ISO_2 3	/*  384 bytes/ms */
#define USBIF_ISO_3 4	/*  640 bytes/ms */
#define USBIF_ISO_4 5	/*  768 bytes/ms */
#define USBIF_ISO_5 6	/*  896 bytes/ms */
#define USBIF_ISO_6 7	/* 1023 bytes/ms */

/* Flicker Modes */
#define NEVER_FLICKER   0
#define FLICKER_60      60
#define FLICKER_50      50

/* Debug flags */
#define DEBUG_NONE          0
#define DEBUG_REG           0x00000001
#define DEBUG_DUMP_PATCH    0x00000002
#define DEBUG_DUMP_REGS     0x00000004

/***
 * Video frame sizes
 ***/
enum {
	VIDEOSIZE_VGA = 0,	/* 640x480 */
	VIDEOSIZE_CIF,		/* 352x288 */
	VIDEOSIZE_QVGA,		/* 320x240 */
	VIDEOSIZE_QCIF,		/* 176x144 */
	VIDEOSIZE_288_216,
	VIDEOSIZE_256_192,
	VIDEOSIZE_224_168,
	VIDEOSIZE_192_144,
};

#define STV_IMAGE_CIF_ROWS    288
#define STV_IMAGE_CIF_COLS    352

#define STV_IMAGE_QCIF_ROWS   144
#define STV_IMAGE_QCIF_COLS   176

#define STV_IMAGE_VGA_ROWS    480
#define STV_IMAGE_VGA_COLS    640

#define STV_IMAGE_QVGA_ROWS   240
#define STV_IMAGE_QVGA_COLS   320

#define JPEG_MARKER_COM (1<<6)	/* Comment segment */

/***
 * Enums
 ***/
/* Sensor types available with cpia2 asics */
enum sensors {
	CPIA2_SENSOR_410,
	CPIA2_SENSOR_500
};

/* Asic types available in the CPiA2 architecture */
#define  CPIA2_ASIC_672 0x67

/* Device types (stv672, stv676, etc) */
#define  DEVICE_STV_672   0x0001
#define  DEVICE_STV_676   0x0002

enum frame_status {
	FRAME_EMPTY,
	FRAME_READING,		/* In the process of being grabbed into */
	FRAME_READY,		/* Ready to be read */
	FRAME_ERROR,
};

/***
 * Register access (for USB request byte)
 ***/
enum {
	CAMERAACCESS_SYSTEM = 0,
	CAMERAACCESS_VC,
	CAMERAACCESS_VP,
	CAMERAACCESS_IDATA
};

#define CAMERAACCESS_TYPE_BLOCK    0x00
#define CAMERAACCESS_TYPE_RANDOM   0x04
#define CAMERAACCESS_TYPE_MASK     0x08
#define CAMERAACCESS_TYPE_REPEAT   0x0C

#define TRANSFER_READ 0
#define TRANSFER_WRITE 1

#define DEFAULT_ALT   USBIF_ISO_6
#define DEFAULT_BRIGHTNESS 0x46
#define DEFAULT_CONTRAST 0x93
#define DEFAULT_SATURATION 0x7f

/* Power state */
#define HI_POWER_MODE CPIA2_SYSTEM_CONTROL_HIGH_POWER
#define LO_POWER_MODE CPIA2_SYSTEM_CONTROL_LOW_POWER


/********
 * Commands
 *******/
enum {
	CPIA2_CMD_NONE = 0,
	CPIA2_CMD_GET_VERSION,
	CPIA2_CMD_GET_PNP_ID,
	CPIA2_CMD_GET_ASIC_TYPE,
	CPIA2_CMD_GET_SENSOR,
	CPIA2_CMD_GET_VP_DEVICE,
	CPIA2_CMD_GET_VP_BRIGHTNESS,
	CPIA2_CMD_SET_VP_BRIGHTNESS,
	CPIA2_CMD_GET_CONTRAST,
	CPIA2_CMD_SET_CONTRAST,
	CPIA2_CMD_GET_VP_SATURATION,
	CPIA2_CMD_SET_VP_SATURATION,
	CPIA2_CMD_GET_VP_GPIO_DIRECTION,
	CPIA2_CMD_SET_VP_GPIO_DIRECTION,
	CPIA2_CMD_GET_VP_GPIO_DATA,
	CPIA2_CMD_SET_VP_GPIO_DATA,
	CPIA2_CMD_GET_VC_MP_GPIO_DIRECTION,
	CPIA2_CMD_SET_VC_MP_GPIO_DIRECTION,
	CPIA2_CMD_GET_VC_MP_GPIO_DATA,
	CPIA2_CMD_SET_VC_MP_GPIO_DATA,
	CPIA2_CMD_ENABLE_PACKET_CTRL,
	CPIA2_CMD_GET_FLICKER_MODES,
	CPIA2_CMD_SET_FLICKER_MODES,
	CPIA2_CMD_RESET_FIFO,	/* clear fifo and enable stream block */
	CPIA2_CMD_SET_HI_POWER,
	CPIA2_CMD_SET_LOW_POWER,
	CPIA2_CMD_CLEAR_V2W_ERR,
	CPIA2_CMD_SET_USER_MODE,
	CPIA2_CMD_GET_USER_MODE,
	CPIA2_CMD_FRAMERATE_REQ,
	CPIA2_CMD_SET_COMPRESSION_STATE,
	CPIA2_CMD_GET_WAKEUP,
	CPIA2_CMD_SET_WAKEUP,
	CPIA2_CMD_GET_PW_CONTROL,
	CPIA2_CMD_SET_PW_CONTROL,
	CPIA2_CMD_GET_SYSTEM_CTRL,
	CPIA2_CMD_SET_SYSTEM_CTRL,
	CPIA2_CMD_GET_VP_SYSTEM_STATE,
	CPIA2_CMD_GET_VP_SYSTEM_CTRL,
	CPIA2_CMD_SET_VP_SYSTEM_CTRL,
	CPIA2_CMD_GET_VP_EXP_MODES,
	CPIA2_CMD_SET_VP_EXP_MODES,
	CPIA2_CMD_GET_DEVICE_CONFIG,
	CPIA2_CMD_SET_DEVICE_CONFIG,
	CPIA2_CMD_SET_SERIAL_ADDR,
	CPIA2_CMD_SET_SENSOR_CR1,
	CPIA2_CMD_GET_VC_CONTROL,
	CPIA2_CMD_SET_VC_CONTROL,
	CPIA2_CMD_SET_TARGET_KB,
	CPIA2_CMD_SET_DEF_JPEG_OPT,
	CPIA2_CMD_REHASH_VP4,
	CPIA2_CMD_GET_USER_EFFECTS,
	CPIA2_CMD_SET_USER_EFFECTS
};

enum user_cmd {
	COMMAND_NONE = 0x00000001,
	COMMAND_SET_FPS = 0x00000002,
	COMMAND_SET_COLOR_PARAMS = 0x00000004,
	COMMAND_GET_COLOR_PARAMS = 0x00000008,
	COMMAND_SET_FORMAT = 0x00000010,	/* size, etc */
	COMMAND_SET_FLICKER = 0x00000020
};

/***
 * Some defines specific to the 676 chip
 ***/
#define CAMACC_CIF      0x01
#define CAMACC_VGA      0x02
#define CAMACC_QCIF     0x04
#define CAMACC_QVGA     0x08


struct cpia2_register {
	u8 index;
	u8 value;
};

struct cpia2_reg_mask {
	u8 index;
	u8 and_mask;
	u8 or_mask;
	u8 fill;
};

struct cpia2_command {
	u32 command;
	u8 req_mode;		/* (Block or random) | registerBank */
	u8 reg_count;
	u8 direction;
	u8 start;
	union reg_types {
		struct cpia2_register registers[32];
		struct cpia2_reg_mask masks[16];
		u8 block_data[64];
		u8 *patch_data;	/* points to function defined block */
	} buffer;
};

struct camera_params {
	struct {
		u8 firmware_revision_hi; /* For system register set (bank 0) */
		u8 firmware_revision_lo;
		u8 asic_id;	/* Video Compressor set (bank 1) */
		u8 asic_rev;
		u8 vp_device_hi;	/* Video Processor set (bank 2) */
		u8 vp_device_lo;
		u8 sensor_flags;
		u8 sensor_rev;
	} version;

	struct {
		u32 device_type;     /* enumerated from vendor/product ids.
				      * Currently, either STV_672 or STV_676 */
		u16 vendor;
		u16 product;
		u16 device_revision;
	} pnp_id;

	struct {
		u8 brightness;	/* CPIA2_VP_EXPOSURE_TARGET */
		u8 contrast;	/* Note: this is CPIA2_VP_YRANGE */
		u8 saturation;	/*  CPIA2_VP_SATURATION */
	} color_params;

	struct {
		u8 cam_register;
		u8 flicker_mode_req;	/* 1 if flicker on, else never flicker */
	} flicker_control;

	struct {
		u8 jpeg_options;
		u8 creep_period;
		u8 user_squeeze;
		u8 inhibit_htables;
	} compression;

	struct {
		u8 ohsize;	/* output image size */
		u8 ovsize;
		u8 hcrop;	/* cropping start_pos/4 */
		u8 vcrop;
		u8 hphase;	/* scaling registers */
		u8 vphase;
		u8 hispan;
		u8 vispan;
		u8 hicrop;
		u8 vicrop;
		u8 hifraction;
		u8 vifraction;
	} image_size;

	struct {
		int width;	/* actual window width */
		int height;	/* actual window height */
	} roi;

	struct {
		u8 video_mode;
		u8 frame_rate;
		u8 video_size;	/* Not a register, just a convenience for cropped sizes */
		u8 gpio_direction;
		u8 gpio_data;
		u8 system_ctrl;
		u8 system_state;
		u8 lowlight_boost;	/* Bool: 0 = off, 1 = on */
		u8 device_config;
		u8 exposure_modes;
		u8 user_effects;
	} vp_params;

	struct {
		u8 pw_control;
		u8 wakeup;
		u8 vc_control;
		u8 vc_mp_direction;
		u8 vc_mp_data;
		u8 quality;
	} vc_params;

	struct {
		u8 power_mode;
		u8 system_ctrl;
		u8 stream_mode;	/* This is the current alternate for usb drivers */
		u8 allow_corrupt;
	} camera_state;
};

#define NUM_SBUF    2

struct cpia2_sbuf {
	char *data;
	struct urb *urb;
};

struct framebuf {
	u64 ts;
	unsigned long seq;
	int num;
	int length;
	int max_length;
	volatile enum frame_status status;
	u8 *data;
	struct framebuf *next;
};

struct camera_data {
	/* locks */
	struct v4l2_device v4l2_dev;
	struct mutex v4l2_lock;	/* serialize file operations */
	struct v4l2_ctrl_handler hdl;
	struct {
		/* Lights control cluster */
		struct v4l2_ctrl *top_light;
		struct v4l2_ctrl *bottom_light;
	};
	struct v4l2_ctrl *usb_alt;

	/* camera status */
	int first_image_seen;
	enum sensors sensor_type;
	u8 flush;
	struct v4l2_fh *stream_fh;
	u8 mmapped;
	int streaming;		/* 0 = no, 1 = yes */
	int xfer_mode;		/* XFER_BULK or XFER_ISOC */
	struct camera_params params;	/* camera settings */

	/* v4l */
	int video_size;			/* VIDEO_SIZE_ */
	struct video_device vdev;	/* v4l videodev */
	u32 width;
	u32 height;			/* Its size */
	__u32 pixelformat;       /* Format fourcc      */

	/* USB */
	struct usb_device *dev;
	unsigned char iface;
	unsigned int cur_alt;
	unsigned int old_alt;
	struct cpia2_sbuf sbuf[NUM_SBUF];	/* Double buffering */

	wait_queue_head_t wq_stream;

	/* Buffering */
	u32 frame_size;
	int num_frames;
	unsigned long frame_count;
	u8 *frame_buffer;	/* frame buffer data */
	struct framebuf *buffers;
	struct framebuf * volatile curbuff;
	struct framebuf *workbuff;

	/* MJPEG Extension */
	int APPn;		/* Number of APP segment to be written, must be 0..15 */
	int APP_len;		/* Length of data in JPEG APPn segment */
	char APP_data[60];	/* Data in the JPEG APPn segment. */

	int COM_len;		/* Length of data in JPEG COM segment */
	char COM_data[60];	/* Data in JPEG COM segment */
};

/* v4l */
int cpia2_register_camera(struct camera_data *cam);
void cpia2_unregister_camera(struct camera_data *cam);
void cpia2_camera_release(struct v4l2_device *v4l2_dev);

/* core */
int cpia2_reset_camera(struct camera_data *cam);
int cpia2_set_low_power(struct camera_data *cam);
void cpia2_dbg_dump_registers(struct camera_data *cam);
int cpia2_match_video_size(int width, int height);
void cpia2_set_camera_state(struct camera_data *cam);
void cpia2_save_camera_state(struct camera_data *cam);
void cpia2_set_color_params(struct camera_data *cam);
void cpia2_set_brightness(struct camera_data *cam, unsigned char value);
void cpia2_set_contrast(struct camera_data *cam, unsigned char value);
void cpia2_set_saturation(struct camera_data *cam, unsigned char value);
int cpia2_set_flicker_mode(struct camera_data *cam, int mode);
void cpia2_set_format(struct camera_data *cam);
int cpia2_send_command(struct camera_data *cam, struct cpia2_command *cmd);
int cpia2_do_command(struct camera_data *cam,
		     unsigned int command,
		     unsigned char direction, unsigned char param);
struct camera_data *cpia2_init_camera_struct(struct usb_interface *intf);
int cpia2_init_camera(struct camera_data *cam);
int cpia2_allocate_buffers(struct camera_data *cam);
void cpia2_free_buffers(struct camera_data *cam);
long cpia2_read(struct camera_data *cam,
		char __user *buf, unsigned long count, int noblock);
__poll_t cpia2_poll(struct camera_data *cam,
			struct file *filp, poll_table *wait);
int cpia2_remap_buffer(struct camera_data *cam, struct vm_area_struct *vma);
void cpia2_set_property_flip(struct camera_data *cam, int prop_val);
void cpia2_set_property_mirror(struct camera_data *cam, int prop_val);
int cpia2_set_gpio(struct camera_data *cam, unsigned char setting);
int cpia2_set_fps(struct camera_data *cam, int framerate);

/* usb */
int cpia2_usb_init(void);
void cpia2_usb_cleanup(void);
int cpia2_usb_transfer_cmd(struct camera_data *cam, void *registers,
			   u8 request, u8 start, u8 count, u8 direction);
int cpia2_usb_stream_start(struct camera_data *cam, unsigned int alternate);
int cpia2_usb_stream_stop(struct camera_data *cam);
int cpia2_usb_stream_pause(struct camera_data *cam);
int cpia2_usb_stream_resume(struct camera_data *cam);
int cpia2_usb_change_streaming_alternate(struct camera_data *cam,
					 unsigned int alt);


/* ----------------------- debug functions ---------------------- */
#ifdef _CPIA2_DEBUG_
#define ALOG(lev, fmt, args...) printk(lev "%s:%d %s(): " fmt, __FILE__, __LINE__, __func__, ## args)
#define LOG(fmt, args...) ALOG(KERN_INFO, fmt, ## args)
#define ERR(fmt, args...) ALOG(KERN_ERR, fmt, ## args)
#define DBG(fmt, args...) ALOG(KERN_DEBUG, fmt, ## args)
#else
#define ALOG(fmt,args...) printk(fmt,##args)
#define LOG(fmt,args...) ALOG(KERN_INFO "cpia2: "fmt,##args)
#define ERR(fmt,args...) ALOG(KERN_ERR "cpia2: "fmt,##args)
#define DBG(fmn,args...) do {} while(0)
#endif
/* No function or lineno, for shorter lines */
#define KINFO(fmt, args...) printk(KERN_INFO fmt,##args)

#endif
