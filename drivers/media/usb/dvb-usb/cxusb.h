/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DVB_USB_CXUSB_H_
#define _DVB_USB_CXUSB_H_

#include <linux/i2c.h>
#include <linux/mutex.h>

#define DVB_USB_LOG_PREFIX "cxusb"
#include "dvb-usb.h"

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
	CXUSB_OPEN_INIT, CXUSB_OPEN_NONE,
	CXUSB_OPEN_ANALOG, CXUSB_OPEN_DIGITAL
};

struct cxusb_medion_dev {
	/* has to be the first one */
	struct cxusb_state state;

	struct dvb_usb_device *dvbdev;

	enum cxusb_open_type open_type;
	unsigned int open_ctr;
	struct mutex open_lock;
};

/* defines for "debug" module parameter */
#define CXUSB_DBG_RC BIT(0)
#define CXUSB_DBG_I2C BIT(1)
#define CXUSB_DBG_MISC BIT(2)

extern int dvb_usb_cxusb_debug;

int cxusb_ctrl_msg(struct dvb_usb_device *d,
		   u8 cmd, const u8 *wbuf, int wlen, u8 *rbuf, int rlen);

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

int cxusb_medion_get(struct dvb_usb_device *dvbdev,
		     enum cxusb_open_type open_type);
void cxusb_medion_put(struct dvb_usb_device *dvbdev);

#endif
