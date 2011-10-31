/*
 * Abilis Systems Single DVB-T Receiver
 * Copyright (C) 2008 Pierrick Hascoet <pierrick.hascoet@abilis.com>
 * Copyright (C) 2010 Devin Heitmueller <dheitmueller@kernellabs.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/version.h>

#ifndef _AS102_USB_DRV_H_
#define _AS102_USB_DRV_H_

#define AS102_USB_DEVICE_TX_CTRL_CMD	0xF1
#define AS102_USB_DEVICE_RX_CTRL_CMD	0xF2

/* define these values to match the supported devices */

/* Abilis system: "TITAN" */
#define AS102_REFERENCE_DESIGN		"Abilis Systems DVB-Titan"
#define AS102_USB_DEVICE_VENDOR_ID	0x1BA6
#define AS102_USB_DEVICE_PID_0001	0x0001

/* PCTV Systems: PCTV picoStick (74e) */
#define AS102_PCTV_74E			"PCTV Systems picoStick (74e)"
#define PCTV_74E_USB_VID		0x2013
#define PCTV_74E_USB_PID		0x0246

/* Elgato: EyeTV DTT Deluxe */
#define AS102_ELGATO_EYETV_DTT_NAME	"Elgato EyeTV DTT Deluxe"
#define ELGATO_EYETV_DTT_USB_VID	0x0fd9
#define ELGATO_EYETV_DTT_USB_PID	0x002c

/* nBox: nBox DVB-T Dongle */
#define AS102_NBOX_DVBT_DONGLE_NAME	"nBox DVB-T Dongle"
#define NBOX_DVBT_DONGLE_USB_VID	0x0b89
#define NBOX_DVBT_DONGLE_USB_PID	0x0007

void as102_urb_stream_irq(struct urb *urb);

struct as10x_usb_token_cmd_t {
	/* token cmd */
	struct as10x_cmd_t c;
	/* token response */
	struct as10x_cmd_t r;
};
#endif
/* EOF - vim: set textwidth=80 ts=8 sw=8 sts=8 noet: */
