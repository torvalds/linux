#ifndef _OPERA1_H_
#define _OPERA1_H_

#define DVB_USB_LOG_PREFIX "opera"
#include "dvb-usb.h"

extern int dvb_usb_opera1_debug;
#define deb_xfer(args...) dprintk(dvb_usb_opera1_debug,0x02,args)
#endif
