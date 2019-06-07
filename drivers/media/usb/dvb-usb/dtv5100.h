/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * DVB USB Linux driver for AME DTV-5100 USB2.0 DVB-T
 *
 * Copyright (C) 2008  Antoine Jacquet <royale@zerezo.com>
 * http://royale.zerezo.com/dtv5100/
 */

#ifndef _DVB_USB_DTV5100_H_
#define _DVB_USB_DTV5100_H_

#define DVB_USB_LOG_PREFIX "dtv5100"
#include "dvb-usb.h"

#define DTV5100_USB_TIMEOUT 500

#define DTV5100_DEMOD_ADDR	0x00
#define DTV5100_DEMOD_WRITE	0xc0
#define DTV5100_DEMOD_READ	0xc1

#define DTV5100_TUNER_ADDR	0xc4
#define DTV5100_TUNER_WRITE	0xc7
#define DTV5100_TUNER_READ	0xc8

#define DRIVER_AUTHOR "Antoine Jacquet, http://royale.zerezo.com/"
#define DRIVER_DESC "AME DTV-5100 USB2.0 DVB-T"

static struct {
	u8 request;
	u8 value;
	u16 index;
} dtv5100_init[] = {
	{ 0x000000c5, 0x00000000, 0x00000001 },
	{ 0x000000c5, 0x00000001, 0x00000001 },
	{ }		/* Terminating entry */
};

#endif
