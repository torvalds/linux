/*
 *  Card-specific functions for the Siano SMS1xxx USB dongle
 *
 *  Copyright (c) 2008 Michael Krufky <mkrufky@linuxtv.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
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
#ifdef CONFIG_DVB_SIANO_SMS1XXX_SMS_IDS
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
#endif
	{ USB_DEVICE(0x2040, 0x1700),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_CATAMOUNT },
	{ USB_DEVICE(0x2040, 0x1800),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_OKEMO_A },
	{ USB_DEVICE(0x2040, 0x1801),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_OKEMO_B },
	{ USB_DEVICE(0x2040, 0x5500),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0x5580),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ USB_DEVICE(0x2040, 0x5590),
		.driver_info = SMS1XXX_BOARD_HAUPPAUGE_WINDHAM },
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, smsusb_id_table);

static struct sms_board sms_boards[] = {
	[SMS_BOARD_UNKNOWN] = {
		.name	= "Unknown board",
	},
	[SMS1XXX_BOARD_SIANO_STELLAR] = {
		.name	= "Siano Stellar Digital Receiver",
		.type	= SMS_STELLAR,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-stellar-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_SIANO_NOVA_A] = {
		.name	= "Siano Nova A Digital Receiver",
		.type	= SMS_NOVA_A0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-nova-a-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_SIANO_NOVA_B] = {
		.name	= "Siano Nova B Digital Receiver",
		.type	= SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-nova-b-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_SIANO_VEGA] = {
		.name	= "Siano Vega Digital Receiver",
		.type	= SMS_VEGA,
	},
	[SMS1XXX_BOARD_HAUPPAUGE_CATAMOUNT] = {
		.name	= "Hauppauge Catamount",
		.type	= SMS_STELLAR,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-stellar-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_HAUPPAUGE_OKEMO_A] = {
		.name	= "Hauppauge Okemo-A",
		.type	= SMS_NOVA_A0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-nova-a-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_HAUPPAUGE_OKEMO_B] = {
		.name	= "Hauppauge Okemo-B",
		.type	= SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-nova-b-dvbt-01.fw",
	},
	[SMS1XXX_BOARD_HAUPPAUGE_WINDHAM] = {
		.name	= "Hauppauge WinTV-Nova-T-MiniStick",
		.type	= SMS_NOVA_B0,
		.fw[DEVICE_MODE_DVBT_BDA] = "sms1xxx-hcw-55xxx-dvbt-01.fw",
	},
};

struct sms_board *sms_get_board(int id)
{
	BUG_ON(id >= ARRAY_SIZE(sms_boards));

	return &sms_boards[id];
}

