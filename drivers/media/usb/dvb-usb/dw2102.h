/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DW2102_H_
#define _DW2102_H_

#define DVB_USB_LOG_PREFIX "dw2102"
#include "dvb-usb.h"

#define deb_xfer(args...) dprintk(dvb_usb_dw2102_debug, 0x02, args)
#define deb_rc(args...)   dprintk(dvb_usb_dw2102_debug, 0x04, args)
#endif
