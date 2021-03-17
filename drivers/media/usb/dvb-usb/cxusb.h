/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DVB_USB_CXUSB_H_
#define _DVB_USB_CXUSB_H_

#include <linux/completion.h>
#include <linux/i2c.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/usb.h>
#include <linux/workqueue.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-device.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-v4l2.h>

#define DVB_USB_LOG_PREFIX "cxusb"
#include "dvb-usb.h"

#define CXUSB_VIDEO_URBS (5)
#define CXUSB_VIDEO_URB_MAX_SIZE (512 * 1024)

#define CXUSB_VIDEO_PKT_SIZE 3030
#define CXUSB_VIDEO_MAX_FRAME_PKTS 346
#define CXUSB_VIDEO_MAX_FRAME_SIZE (CXUSB_VIDEO_MAX_FRAME_PKTS * \
					CXUSB_VIDEO_PKT_SIZE)

/* usb commands - some of it are guesses, don't have a reference yet */
#define CMD_BLUEBIRD_GPIO_RW 0x05

#define CMD_I2C_WRITE     0x08
#define CMD_I2C_READ      0x09

#define CMD_GPIO_READ     0x0d
#define CMD_GPIO_WRITE    0x0e
#define     GPIO_TUNER         0x02

#define CMD_POWER_OFF     0xdc
#define CMD_POWER_ON      0xde

#define CMD_STREAMING_ON  0x36
#define CMD_STREAMING_OFF 0x37

#define CMD_AVER_STREAM_ON  0x18
#define CMD_AVER_STREAM_OFF 0x19

#define CMD_GET_IR_CODE   0x47

#define CMD_ANALOG        0x50
#define CMD_DIGITAL       0x51

#define CXUSB_BT656_PREAMBLE ((const u8 *)"\xff\x00\x00")

#define CXUSB_BT656_FIELD_MASK BIT(6)
#define CXUSB_BT656_FIELD_1 0
#define CXUSB_BT656_FIELD_2 BIT(6)

#define CXUSB_BT656_VBI_MASK BIT(5)
#define CXUSB_BT656_VBI_ON BIT(5)
#define CXUSB_BT656_VBI_OFF 0

#define CXUSB_BT656_SEAV_MASK BIT(4)
#define CXUSB_BT656_SEAV_EAV BIT(4)
#define CXUSB_BT656_SEAV_SAV 0

/* Max transfer size done by I2C transfer functions */
#define MAX_XFER_SIZE  80

struct cxusb_state {
	u8 gpio_write_state[3];
	bool gpio_write_refresh[3];
	struct i2c_client *i2c_client_demod;
	struct i2c_client *i2c_client_tuner;

	unsigned char data[MAX_XFER_SIZE];

	struct mutex stream_mutex;
	u8 last_lock;
	int (*fe_read_status)(struct dvb_frontend *fe,
			      enum fe_status *status);
};

enum cxusb_open_type {
	CXUSB_OPEN_INIT,
	CXUSB_OPEN_NONE,
	CXUSB_OPEN_ANALOG,
	CXUSB_OPEN_DIGITAL
};

struct cxusb_medion_auxbuf {
	u8 *buf;
	unsigned int len;
	unsigned int paylen;
};

enum cxusb_bt656_mode {
	NEW_FRAME, FIRST_FIELD, SECOND_FIELD
};

enum cxusb_bt656_fmode {
	START_SEARCH, LINE_SAMPLES, VBI_SAMPLES
};

struct cxusb_bt656_params {
	enum cxusb_bt656_mode mode;
	enum cxusb_bt656_fmode fmode;
	unsigned int pos;
	unsigned int line;
	unsigned int linesamples;
	u8 *buf;
};

struct cxusb_medion_dev {
	/* has to be the first one */
	struct cxusb_state state;

	struct dvb_usb_device *dvbdev;

	enum cxusb_open_type open_type;
	unsigned int open_ctr;
	struct mutex open_lock;

#ifdef CONFIG_DVB_USB_CXUSB_ANALOG
	struct v4l2_device v4l2dev;
	struct v4l2_subdev *cx25840;
	struct v4l2_subdev *tuner;
	struct v4l2_subdev *tda9887;
	struct video_device *videodev, *radiodev;
	struct mutex dev_lock;

	struct vb2_queue videoqueue;
	u32 input;
	bool stop_streaming;
	u32 width, height;
	u32 field_order;
	struct cxusb_medion_auxbuf auxbuf;
	v4l2_std_id norm;

	struct urb *streamurbs[CXUSB_VIDEO_URBS];
	unsigned long urbcomplete;
	struct work_struct urbwork;
	unsigned int nexturb;

	struct cxusb_bt656_params bt656;
	struct cxusb_medion_vbuffer *vbuf;
	__u32 vbuf_sequence;

	struct list_head buflist;

	struct completion v4l2_release;
#endif
};

struct cxusb_medion_vbuffer {
	struct vb2_v4l2_buffer vb2;
	struct list_head list;
};

/* defines for "debug" module parameter */
#define CXUSB_DBG_RC BIT(0)
#define CXUSB_DBG_I2C BIT(1)
#define CXUSB_DBG_MISC BIT(2)
#define CXUSB_DBG_BT656 BIT(3)
#define CXUSB_DBG_URB BIT(4)
#define CXUSB_DBG_OPS BIT(5)
#define CXUSB_DBG_AUXB BIT(6)

extern int dvb_usb_cxusb_debug;

#define cxusb_vprintk(dvbdev, lvl, ...) do {				\
		struct cxusb_medion_dev *_cxdev = (dvbdev)->priv;	\
		if (dvb_usb_cxusb_debug & CXUSB_DBG_##lvl)		\
			v4l2_printk(KERN_DEBUG,			\
				    &_cxdev->v4l2dev, __VA_ARGS__);	\
	} while (0)

int cxusb_ctrl_msg(struct dvb_usb_device *d,
		   u8 cmd, const u8 *wbuf, int wlen, u8 *rbuf, int rlen);

#ifdef CONFIG_DVB_USB_CXUSB_ANALOG
int cxusb_medion_analog_init(struct dvb_usb_device *dvbdev);
int cxusb_medion_register_analog(struct dvb_usb_device *dvbdev);
void cxusb_medion_unregister_analog(struct dvb_usb_device *dvbdev);
#else
static inline int cxusb_medion_analog_init(struct dvb_usb_device *dvbdev)
{
	return -EINVAL;
}

static inline int cxusb_medion_register_analog(struct dvb_usb_device *dvbdev)
{
	return 0;
}

static inline void cxusb_medion_unregister_analog(struct dvb_usb_device *dvbdev)
{
}
#endif

int cxusb_medion_get(struct dvb_usb_device *dvbdev,
		     enum cxusb_open_type open_type);
void cxusb_medion_put(struct dvb_usb_device *dvbdev);

#endif
