#ifndef _DVB_USB_CXUSB_H_
#define _DVB_USB_CXUSB_H_

#define DVB_USB_LOG_PREFIX "digitv"
#include "dvb-usb.h"

extern int dvb_usb_cxusb_debug;
#define deb_info(args...)   dprintk(dvb_usb_cxusb_debug,0x01,args)

/* usb commands - some of it are guesses, don't have a reference yet */
#define CMD_I2C_WRITE    0x08
#define CMD_I2C_READ     0x09

#define CMD_IOCTL        0x0e
#define    IOCTL_SET_I2C_PATH 0x02

#define CMD_POWER_OFF    0x50
#define CMD_POWER_ON     0x51

enum cxusb_i2c_pathes {
	PATH_UNDEF       = 0x00,
	PATH_CX22702     = 0x01,
	PATH_TUNER_OTHER = 0x02,
};

struct cxusb_state {
	enum cxusb_i2c_pathes cur_i2c_path;
};

#endif
