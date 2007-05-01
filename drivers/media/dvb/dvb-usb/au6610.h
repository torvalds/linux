#ifndef _DVB_USB_AU6610_H_
#define _DVB_USB_AU6610_H_

#define DVB_USB_LOG_PREFIX "au6610"
#include "dvb-usb.h"

#define deb_rc(args...)   dprintk(dvb_usb_au6610_debug,0x01,args)

#define AU6610_REQ_I2C_WRITE	0x14
#define AU6610_REQ_I2C_READ	0x13
#define AU6610_REQ_USB_WRITE	0x16
#define AU6610_REQ_USB_READ	0x15

#define AU6610_USB_TIMEOUT 1000

#define AU6610_ALTSETTING_COUNT 6
#define AU6610_ALTSETTING       5

#endif
