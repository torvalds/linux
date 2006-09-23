#ifndef _DVB_USB_MEGASKY_H_
#define _DVB_USB_MEGASKY_H_

#define DVB_USB_LOG_PREFIX "megasky"
#include "dvb-usb.h"

extern int dvb_usb_megasky_debug;
#define deb_rc(args...)   dprintk(dvb_usb_megasky_debug,0x01,args)

#endif
