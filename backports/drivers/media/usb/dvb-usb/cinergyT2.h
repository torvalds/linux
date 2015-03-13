/*
 * TerraTec Cinergy T2/qanu USB2 DVB-T adapter.
 *
 * Copyright (C) 2007 Tomi Orava (tomimo@ncircle.nullnet.fi)
 *
 * Based on the dvb-usb-framework code and the
 * original Terratec Cinergy T2 driver by:
 *
 * Copyright (C) 2004 Daniel Mack <daniel@qanu.de> and
 *                  Holger Waechtler <holger@qanu.de>
 *
 *  Protocol Spec published on http://qanu.de/specs/terratec_cinergyT2.pdf
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License,  or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not,  write to the Free Software
 * Foundation,  Inc.,  675 Mass Ave,  Cambridge,  MA 02139,  USA.
 *
 */

#ifndef _DVB_USB_CINERGYT2_H_
#define _DVB_USB_CINERGYT2_H_

#include <linux/usb/input.h>

#define DVB_USB_LOG_PREFIX "cinergyT2"
#include "dvb-usb.h"

#define DRIVER_NAME "TerraTec/qanu USB2.0 Highspeed DVB-T Receiver"

extern int dvb_usb_cinergyt2_debug;

#define deb_info(args...)  dprintk(dvb_usb_cinergyt2_debug,  0x001, args)
#define deb_xfer(args...)  dprintk(dvb_usb_cinergyt2_debug,  0x002, args)
#define deb_pll(args...)   dprintk(dvb_usb_cinergyt2_debug,  0x004, args)
#define deb_ts(args...)    dprintk(dvb_usb_cinergyt2_debug,  0x008, args)
#define deb_err(args...)   dprintk(dvb_usb_cinergyt2_debug,  0x010, args)
#define deb_rc(args...)    dprintk(dvb_usb_cinergyt2_debug,  0x020, args)
#define deb_fw(args...)    dprintk(dvb_usb_cinergyt2_debug,  0x040, args)
#define deb_mem(args...)   dprintk(dvb_usb_cinergyt2_debug,  0x080, args)
#define deb_uxfer(args...) dprintk(dvb_usb_cinergyt2_debug,  0x100, args)



enum cinergyt2_ep1_cmd {
	CINERGYT2_EP1_PID_TABLE_RESET		= 0x01,
	CINERGYT2_EP1_PID_SETUP			= 0x02,
	CINERGYT2_EP1_CONTROL_STREAM_TRANSFER	= 0x03,
	CINERGYT2_EP1_SET_TUNER_PARAMETERS	= 0x04,
	CINERGYT2_EP1_GET_TUNER_STATUS		= 0x05,
	CINERGYT2_EP1_START_SCAN		= 0x06,
	CINERGYT2_EP1_CONTINUE_SCAN		= 0x07,
	CINERGYT2_EP1_GET_RC_EVENTS		= 0x08,
	CINERGYT2_EP1_SLEEP_MODE		= 0x09,
	CINERGYT2_EP1_GET_FIRMWARE_VERSION	= 0x0A
};


struct dvbt_get_status_msg {
	uint32_t freq;
	uint8_t bandwidth;
	uint16_t tps;
	uint8_t flags;
	__le16 gain;
	uint8_t snr;
	__le32 viterbi_error_rate;
	uint32_t rs_error_rate;
	__le32 uncorrected_block_count;
	uint8_t lock_bits;
	uint8_t prev_lock_bits;
} __attribute__((packed));


struct dvbt_set_parameters_msg {
	uint8_t cmd;
	__le32 freq;
	uint8_t bandwidth;
	__le16 tps;
	uint8_t flags;
} __attribute__((packed));


extern struct dvb_frontend *cinergyt2_fe_attach(struct dvb_usb_device *d);

#endif /* _DVB_USB_CINERGYT2_H_ */

