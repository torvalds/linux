/* Common header file of Linux driver for the Yakumo/Hama/Typhoon DVB-T
 * USB2.0 receiver.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@desy.de)
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the Free
 *	Software Foundation, version 2.
 *
 * see Documentation/dvb/README.dvb-usb for more information
 */
#ifndef _DVB_USB_DTT200U_H_
#define _DVB_USB_DTT200U_H_

#define DVB_USB_LOG_PREFIX "dtt200u"
#include "dvb-usb.h"

extern int dvb_usb_dtt200u_debug;
#define deb_info(args...) dprintk(dvb_usb_dtt200u_debug,0x01,args)
#define deb_xfer(args...) dprintk(dvb_usb_dtt200u_debug,0x02,args)

/* guessed protocol description (reverse engineered):
 * read
 *  00 - USB type 0x02 for usb2.0, 0x01 for usb1.1
 *  81 - <TS_LOCK> <current frequency divided by 250000>
 *  82 - crash - do not touch
 *  83 - crash - do not touch
 *  84 - remote control
 *  85 - crash - do not touch (OK, stop testing here)
 *  88 - locking 2 bytes (0x80 0x40 == no signal, 0x89 0x20 == nice signal)
 *  89 - noise-to-signal
 *	8a - unkown 1 byte - signal_strength
 *  8c - ber ???
 *  8d - ber
 *  8e - unc
 */

#define GET_SPEED        0x00
#define GET_TUNE_STAT    0x81
#define GET_RC_KEY       0x84
#define GET_STATUS       0x88
#define GET_SNR          0x89
#define GET_SIG_STRENGTH 0x8a
#define GET_UNK          0x8c
#define GET_BER          0x8d
#define GET_UNC          0x8e

/* write
 *  01 - reset the demod
 *  02 - frequency (divided by 250000)
 *  03 - bandwidth
 *  04 - pid table (index pid(7:0) pid(12:8))
 *  05 - reset the pid table
 *  08 - demod transfer enabled or not (FX2 transfer is enabled by default)
 */

#define RESET_DEMOD      0x01
#define SET_FREQUENCY    0x02
#define SET_BANDWIDTH    0x03
#define SET_PID_FILTER   0x04
#define RESET_PID_FILTER 0x05
#define SET_TS_CTRL      0x08

extern struct dvb_frontend * dtt200u_fe_attach(struct dvb_usb_device *d);

#endif
