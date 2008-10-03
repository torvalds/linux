#ifndef _DW2102_H_
#define _DW2102_H_

#define DVB_USB_LOG_PREFIX "dw2102"
#include "dvb-usb.h"

extern int dvb_usb_dw2102_debug;
#define deb_xfer(args...) dprintk(dvb_usb_dw2102_debug, 0x02, args)
#endif
