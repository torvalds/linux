/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Abilis Systems Single DVB-T Receiver
 * Copyright (C) 2008 Pierrick Hascoet <pierrick.hascoet@abilis.com>
 * Copyright (C) 2010 Devin Heitmueller <dheitmueller@kernellabs.com>
 */
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

/* Sky Italia: Digital Key (green led) */
#define AS102_SKY_IT_DIGITAL_KEY_NAME	"Sky IT Digital Key (green led)"
#define SKY_IT_DIGITAL_KEY_USB_VID	0x2137
#define SKY_IT_DIGITAL_KEY_USB_PID	0x0001

void as102_urb_stream_irq(struct urb *urb);

struct as10x_usb_token_cmd_t {
	/* token cmd */
	struct as10x_cmd_t c;
	/* token response */
	struct as10x_cmd_t r;
};
#endif
