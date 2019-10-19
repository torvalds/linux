/* SPDX-License-Identifier: GPL-2.0-only */
/* Common header file of Linux driver for the WideView/ Yakumo/ Hama/
 * Typhoon/ Yuan DVB-T USB2.0 receiver.
 *
 * Copyright (C) 2004-5 Patrick Boettcher (patrick.boettcher@posteo.de)
 *
 * see Documentation/media/dvb-drivers/dvb-usb.rst for more information
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
 *  88 - locking 2 bytes (0x80 0x40 == no signal, 0x89 0x20 == nice signal)
 */

#define GET_SPEED		0x00
#define GET_TUNE_STATUS		0x81
#define GET_RC_CODE		0x84
#define GET_CONFIGURATION	0x88
#define GET_AGC			0x89
#define GET_SNR			0x8a
#define GET_VIT_ERR_CNT		0x8c
#define GET_RS_ERR_CNT		0x8d
#define GET_RS_UNCOR_BLK_CNT	0x8e

/* write
 *  01 - init
 *  02 - frequency (divided by 250000)
 *  03 - bandwidth
 *  04 - pid table (index pid(7:0) pid(12:8))
 *  05 - reset the pid table
 *  08 - transfer switch
 */

#define SET_INIT		0x01
#define SET_RF_FREQ		0x02
#define SET_BANDWIDTH		0x03
#define SET_PID_FILTER		0x04
#define RESET_PID_FILTER	0x05
#define SET_STREAMING		0x08

extern struct dvb_frontend * dtt200u_fe_attach(struct dvb_usb_device *d);

#endif
