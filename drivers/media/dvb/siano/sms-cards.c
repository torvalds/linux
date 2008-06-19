/*
 *  Card-specific functions for the Siano SMS1xxx USB dongle
 *
 *  Copyright (c) 2008 Michael Krufky <mkrufky@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 3 as
 *  published by the Free Software Foundation;
 *
 *  Software distributed under the License is distributed on an "AS IS"
 *  basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *
 *  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "sms-cards.h"

struct usb_device_id smsusb_id_table[] = {
	{ USB_DEVICE(0x187f, 0x0010),
		.driver_info = SMS1XXX_BOARD_SIANO_STELLAR },
	{ USB_DEVICE(0x187f, 0x0100),
		.driver_info = SMS1XXX_BOARD_SIANO_STELLAR },
	{ USB_DEVICE(0x187f, 0x0200),
		.driver_info = SMS1XXX_BOARD_SIANO_NOVA_A },
	{ USB_DEVICE(0x187f, 0x0201),
		.driver_info = SMS1XXX_BOARD_SIANO_NOVA_B },
	{ USB_DEVICE(0x187f, 0x0300),
		.driver_info = SMS1XXX_BOARD_SIANO_VEGA },
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, smsusb_id_table);

static struct sms_board sms_boards[] = {
	[SMS_BOARD_UNKNOWN] = {
		.name	= "Unknown board",
	},
	[SMS1XXX_BOARD_SIANO_SMS1000] = {
		.name	= "Siano Digital Receiver",
		.type	= SMS_STELLAR,
	},
	[SMS1XXX_BOARD_SIANO_STELLAR] = {
		.name	= "Siano Stellar reference board",
		.type	= SMS_STELLAR,
	},
	[SMS1XXX_BOARD_SIANO_NOVA_A] = {
		.name	= "Siano Nova A reference board",
		.type	= SMS_NOVA_A0,
	},
	[SMS1XXX_BOARD_SIANO_NOVA_B] = {
		.name	= "Siano Nova B reference board",
		.type	= SMS_NOVA_B0,
	},
	[SMS1XXX_BOARD_SIANO_VEGA] = {
		.name	= "Siano Vega reference board",
		.type	= SMS_VEGA,
	},
};

struct sms_board *sms_get_board(int id)
{
	BUG_ON(id >= ARRAY_SIZE(sms_boards));

	return &sms_boards[id];
}

