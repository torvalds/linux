/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Hauppauge HD PVR USB driver
 *
 * Copyright (C) 2008      Janne Grunau (j@jannau.net)
 */

#include <linux/usb.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/workqueue.h>
#include <linux/videodev2.h>

#include <media/v4l2-device.h>
#include <media/v4l2-ctrls.h>
#include <media/i2c/ir-kbd-i2c.h>

#define HDPVR_MAX 8
#define HDPVR_I2C_MAX_SIZE 128

/* Define these values to match your devices */
#define HD_PVR_VENDOR_ID	0x2040
#define HD_PVR_PRODUCT_ID	0x4900
#define HD_PVR_PRODUCT_ID1	0x4901
#define HD_PVR_PRODUCT_ID2	0x4902
#define HD_PVR_PRODUCT_ID4	0x4903
#define HD_PVR_PRODUCT_ID3	0x4982

#define UNSET    (-1U)

#define NUM_BUFFERS 64

#define HDPVR_FIRMWARE_VERSION		0x08
#define HDPVR_FIRMWARE_VERSION_AC3	0x0d
#define HDPVR_FIRMWARE_VERSION_0X12	0x12
#define HDPVR_FIRMWARE_VERSION_0X15	0x15
#define HDPVR_FIRMWARE_VERSION_0X1E	0x1e

/* #define HDPVR_DEBUG */

extern int hdpvr_debug;

#define MSG_INFO	1
#define MSG_BUFFER	2

struct hdpvr_options {
	u8	video_std;
	u8	video_input;
	u8	audio_input;
	u8	bitrate;	/* in 100kbps */
	u8	peak_bitrate;	/* in 100kbps */
	u8	bitrate_mode;
	u8	gop_mode;
	enum v4l2_mpeg_audio_encoding	audio_codec;
	u8	brightness;
	u8	contrast;
	u8	hue;
	u8	saturation;
	u8	sharpness;
};

/* Structure to hold all of our device specific stuff */
struct hdpvr_device {
	/* the v4l device for this device */
	struct video_device	video_dev;
	/* the control handler for this device */
	struct v4l2_ctrl_handler hdl;
	/* the usb device for this device */
	struct usb_device	*udev;
	/* v4l2-device unused */
	struct v4l2_device	v4l2_dev;
	struct { /* video mode/bitrate control cluster */
		struct v4l2_ctrl *video_mode;
		struct v4l2_ctrl *video_bitrate;
		struct v4l2_ctrl *video_bitrate_peak;
	};
	/* v4l2 format */
	uint width, height;

	/* the max packet size of the bulk endpoint */
	size_t			bulk_in_size;
	/* the address of the bulk in endpoint */
	__u8			bulk_in_endpointAddr;

	/* holds the current device status */
	__u8			status;

	/* holds the current set options */
	struct hdpvr_options	options;
	v4l2_std_id		cur_std;
	struct v4l2_dv_timings	cur_dv_timings;

	uint			flags;

	/* synchronize I/O */
	struct mutex		io_mutex;
	/* available buffers */
	struct list_head	free_buff_list;
	/* in progress buffers */
	struct list_head	rec_buff_list;
	/* waitqueue for buffers */
	wait_queue_head_t	wait_buffer;
	/* waitqueue for data */
	wait_queue_head_t	wait_data;
	/**/
	struct work_struct	worker;
	/* current stream owner */
	struct v4l2_fh		*owner;

	/* I2C adapter */
	struct i2c_adapter	i2c_adapter;
	/* I2C lock */
	struct mutex		i2c_mutex;
	/* I2C message buffer space */
	char			i2c_buf[HDPVR_I2C_MAX_SIZE];

	/* For passing data to ir-kbd-i2c */
	struct IR_i2c_init_data	ir_i2c_init_data;

	/* usb control transfer buffer and lock */
	struct mutex		usbc_mutex;
	u8			*usbc_buf;
	u8			fw_ver;
};

static inline struct hdpvr_device *to_hdpvr_dev(struct v4l2_device *v4l2_dev)
{
	return container_of(v4l2_dev, struct hdpvr_device, v4l2_dev);
}


/* buffer one bulk urb of data */
struct hdpvr_buffer {
	struct list_head	buff_list;

	struct urb		*urb;

	struct hdpvr_device	*dev;

	uint			pos;

	__u8			status;
};

/* */

struct hdpvr_video_info {
	u16	width;
	u16	height;
	u8	fps;
	bool	valid;
};

enum {
	STATUS_UNINITIALIZED	= 0,
	STATUS_IDLE,
	STATUS_STARTING,
	STATUS_SHUTTING_DOWN,
	STATUS_STREAMING,
	STATUS_ERROR,
	STATUS_DISCONNECTED,
};

enum {
	HDPVR_FLAG_AC3_CAP = 1,
};

enum {
	BUFSTAT_UNINITIALIZED = 0,
	BUFSTAT_AVAILABLE,
	BUFSTAT_INPROGRESS,
	BUFSTAT_READY,
};

#define CTRL_START_STREAMING_VALUE	0x0700
#define CTRL_STOP_STREAMING_VALUE	0x0800
#define CTRL_BITRATE_VALUE		0x1000
#define CTRL_BITRATE_MODE_VALUE		0x1200
#define CTRL_GOP_MODE_VALUE		0x1300
#define CTRL_VIDEO_INPUT_VALUE		0x1500
#define CTRL_VIDEO_STD_TYPE		0x1700
#define CTRL_AUDIO_INPUT_VALUE		0x2500
#define CTRL_BRIGHTNESS			0x2900
#define CTRL_CONTRAST			0x2a00
#define CTRL_HUE			0x2b00
#define CTRL_SATURATION			0x2c00
#define CTRL_SHARPNESS			0x2d00
#define CTRL_LOW_PASS_FILTER_VALUE	0x3100

#define CTRL_DEFAULT_INDEX		0x0003


	/* :0 s 38 01 1000 0003 0004 4 = 0a00ca00
	 * BITRATE SETTING
	 *   1st and 2nd byte (little endian): average bitrate in 100 000 bit/s
	 *                                     min: 1 mbit/s, max: 13.5 mbit/s
	 *   3rd and 4th byte (little endian): peak bitrate in 100 000 bit/s
	 *                                     min: average + 100kbit/s,
	 *                                      max: 20.2 mbit/s
	 */

	/* :0 s 38 01 1200 0003 0001 1 = 02
	 * BIT RATE MODE
	 *  constant = 1, variable (peak) = 2, variable (average) = 3
	 */

	/* :0 s 38 01 1300 0003 0001 1 = 03
	 * GOP MODE (2 bit)
	 *    low bit 0/1: advanced/simple GOP
	 *   high bit 0/1: IDR(4/32/128) / no IDR (4/32/0)
	 */

	/* :0 s 38 01 1700 0003 0001 1 = 00
	 * VIDEO STANDARD or FREQUENCY 0 = 60hz, 1 = 50hz
	 */

	/* :0 s 38 01 3100 0003 0004 4 = 03030000
	 * FILTER CONTROL
	 *   1st byte luma low pass filter strength,
	 *   2nd byte chroma low pass filter strength,
	 *   3rd byte MF enable chroma, min=0, max=1
	 *   4th byte n
	 */


	/* :0 s 38 b9 0001 0000 0000 0 */



/* :0 s 38 d3 0000 0000 0001 1 = 00 */
/*		ret = usb_control_msg(dev->udev, */
/*				      usb_sndctrlpipe(dev->udev, 0), */
/*				      0xd3, 0x38, */
/*				      0, 0, */
/*				      "\0", 1, */
/*				      1000); */

/*		info("control request returned %d", ret); */
/*		msleep(5000); */


	/* :0 s b8 81 1400 0003 0005 5 <
	 * :0 0 5 = d0024002 19
	 * QUERY FRAME SIZE AND RATE
	 *   1st and 2nd byte (little endian): horizontal resolution
	 *   3rd and 4th byte (little endian): vertical resolution
	 *   5th byte: frame rate
	 */

	/* :0 s b8 81 1800 0003 0003 3 <
	 * :0 0 3 = 030104
	 * QUERY SIGNAL AND DETECTED LINES, maybe INPUT
	 */

enum hdpvr_video_std {
	HDPVR_60HZ = 0,
	HDPVR_50HZ,
};

enum hdpvr_video_input {
	HDPVR_COMPONENT = 0,
	HDPVR_SVIDEO,
	HDPVR_COMPOSITE,
	HDPVR_VIDEO_INPUTS
};

enum hdpvr_audio_inputs {
	HDPVR_RCA_BACK = 0,
	HDPVR_RCA_FRONT,
	HDPVR_SPDIF,
	HDPVR_AUDIO_INPUTS
};

enum hdpvr_bitrate_mode {
	HDPVR_CONSTANT = 1,
	HDPVR_VARIABLE_PEAK,
	HDPVR_VARIABLE_AVERAGE,
};

enum hdpvr_gop_mode {
	HDPVR_ADVANCED_IDR_GOP = 0,
	HDPVR_SIMPLE_IDR_GOP,
	HDPVR_ADVANCED_NOIDR_GOP,
	HDPVR_SIMPLE_NOIDR_GOP,
};

void hdpvr_delete(struct hdpvr_device *dev);

/*========================================================================*/
/* hardware control functions */
int hdpvr_set_options(struct hdpvr_device *dev);

int hdpvr_set_bitrate(struct hdpvr_device *dev);

int hdpvr_set_audio(struct hdpvr_device *dev, u8 input,
		    enum v4l2_mpeg_audio_encoding codec);

int hdpvr_config_call(struct hdpvr_device *dev, uint value,
		      unsigned char valbuf);

int get_video_info(struct hdpvr_device *dev, struct hdpvr_video_info *vid_info);

/* :0 s b8 81 1800 0003 0003 3 < */
/* :0 0 3 = 0301ff */
int get_input_lines_info(struct hdpvr_device *dev);


/*========================================================================*/
/* v4l2 registration */
int hdpvr_register_videodev(struct hdpvr_device *dev, struct device *parent,
			    int devnumber);

int hdpvr_cancel_queue(struct hdpvr_device *dev);

/*========================================================================*/
/* i2c adapter registration */
int hdpvr_register_i2c_adapter(struct hdpvr_device *dev);

struct i2c_client *hdpvr_register_ir_i2c(struct hdpvr_device *dev);

/*========================================================================*/
/* buffer management */
int hdpvr_free_buffers(struct hdpvr_device *dev);
int hdpvr_alloc_buffers(struct hdpvr_device *dev, uint count);
