#ifndef _DVB_USB_CXUSB_H_
#define _DVB_USB_CXUSB_H_

#define DVB_USB_LOG_PREFIX "cxusb"
#include "dvb-usb.h"

extern int dvb_usb_cxusb_debug;
#define deb_info(args...)   dprintk(dvb_usb_cxusb_debug,0x01,args)

/* usb commands - some of it are guesses, don't have a reference yet */
#define CMD_I2C_WRITE     0x08
#define CMD_I2C_READ      0x09

#define CMD_GPIO_READ     0x0d
#define CMD_GPIO_WRITE    0x0e
#define     GPIO_TUNER         0x02

#define CMD_POWER_OFF     0xdc
#define CMD_POWER_ON      0xde

#define CMD_STREAMING_ON  0x36
#define CMD_STREAMING_OFF 0x37

#define CMD_ANALOG        0x50
#define CMD_DIGITAL       0x51

struct cxusb_state {
	u8 gpio_write_state[3];
};

#endif
