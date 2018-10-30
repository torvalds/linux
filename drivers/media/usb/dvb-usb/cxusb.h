/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DVB_USB_CXUSB_H_
#define _DVB_USB_CXUSB_H_

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
	struct i2c_client *i2c_client_demod;
	struct i2c_client *i2c_client_tuner;

	unsigned char data[MAX_XFER_SIZE];

	struct mutex stream_mutex;
	u8 last_lock;
	int (*fe_read_status)(struct dvb_frontend *fe,
		enum fe_status *status);
};

#endif
