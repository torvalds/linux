#ifndef _DVB_USB_M920X_H_
#define _DVB_USB_M920X_H_

#define DVB_USB_LOG_PREFIX "m920x"
#include "dvb-usb.h"

extern int dvb_usb_m920x_debug;
#define deb_rc(args...)   dprintk(dvb_usb_m920x_debug,0x01,args)

#endif
