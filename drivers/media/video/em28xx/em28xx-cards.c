/*
   em2820-cards.c - driver for Empia EM2800/EM2820/2840 USB video capture devices

   Copyright (C) 2005 Markus Rechberger <mrechberger@gmail.com>
                      Ludovico Cavedon <cavedon@sssup.it>
                      Mauro Carvalho Chehab <mchehab@brturbo.com.br>

   Based on the em2800 driver from Sascha Sommer <saschasommer@freenet.de>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/usb.h>
#include <media/tuner.h>
#include <media/audiochip.h>
#include <media/tveeprom.h>
#include "msp3400.h"

#include "em2820.h"

struct em2820_board em2820_boards[] = {
	[EM2800_BOARD_UNKNOWN] = {
		.name         = "Unknown EM2800 video grabber",
		.is_em2800    = 1,
		.vchannels    = 2,
		.norm         = VIDEO_MODE_PAL,
		.tda9887_conf = TDA9887_PRESENT,
		.has_tuner    = 1,
		.decoder      = EM2820_SAA7113,
		.input           = {{
			.type     = EM2820_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = 1,
		},{
			.type     = EM2820_VMUX_SVIDEO,
			.vmux     = 9,
			.amux     = 1,
		}},
	},
	[EM2820_BOARD_UNKNOWN] = {
		.name         = "Unknown EM2820/2840 video grabber",
		.is_em2800    = 0,
		.vchannels    = 2,
		.norm         = VIDEO_MODE_PAL,
		.tda9887_conf = TDA9887_PRESENT,
		.has_tuner    = 1,
		.decoder      = EM2820_SAA7113,
		.input           = {{
			.type     = EM2820_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = 1,
		},{
			.type     = EM2820_VMUX_SVIDEO,
			.vmux     = 9,
			.amux     = 1,
		}},
	},
	[EM2820_BOARD_TERRATEC_CINERGY_250] = {
		.name         = "Terratec Cinergy 250 USB",
		.vchannels    = 3,
		.norm         = VIDEO_MODE_PAL,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.has_tuner    = 1,
		.decoder      = EM2820_SAA7113,
		.input          = {{
			.type     = EM2820_VMUX_TELEVISION,
			.vmux     = 2,
			.amux     = 0,
		},{
			.type     = EM2820_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = 1,
		},{
			.type     = EM2820_VMUX_SVIDEO,
			.vmux     = 9,
			.amux     = 1,
		}},
	},
	[EM2820_BOARD_PINNACLE_USB_2] = {
		.name         = "Pinnacle PCTV USB 2",
		.vchannels    = 3,
		.norm         = VIDEO_MODE_PAL,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.has_tuner    = 1,
		.decoder      = EM2820_SAA7113,
		.input          = {{
			.type     = EM2820_VMUX_TELEVISION,
			.vmux     = 2,
			.amux     = 0,
		},{
			.type     = EM2820_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = 1,
		},{
			.type     = EM2820_VMUX_SVIDEO,
			.vmux     = 9,
			.amux     = 1,
		}},
	},
	[EM2820_BOARD_HAUPPAUGE_WINTV_USB_2] = {
		.name         = "Hauppauge WinTV USB 2",
		.vchannels    = 3,
		.norm         = VIDEO_MODE_NTSC,
		.tuner_type   = TUNER_PHILIPS_FM1236_MK3,
		.tda9887_conf = TDA9887_PRESENT|TDA9887_PORT1_ACTIVE|TDA9887_PORT2_ACTIVE,
		.has_tuner    = 1,
		.decoder      = EM2820_TVP5150,
		.has_msp34xx  = 1,
		/*FIXME: S-Video not tested */
		.input          = {{
			.type     = EM2820_VMUX_TELEVISION,
			.vmux     = 0,
			.amux     = 0,
		},{
			.type     = EM2820_VMUX_SVIDEO,
			.vmux     = 2,
			.amux     = 1,
		}},
	},
	[EM2820_BOARD_MSI_VOX_USB_2] = {
		.name		= "MSI VOX USB 2.0",
		.vchannels	= 3,
		.norm		= VIDEO_MODE_PAL,
		.tuner_type	= TUNER_PHILIPS_PAL,
		.tda9887_conf	= TDA9887_PRESENT|TDA9887_PORT1_ACTIVE|TDA9887_PORT2_ACTIVE,
		.has_tuner	= 1,
		.decoder      = EM2820_SAA7114,
		.input          = {{
			.type     = EM2820_VMUX_TELEVISION,
			.vmux     = 2,
			.amux     = 0,
		},{
			.type     = EM2820_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = 1,
		},{
			.type     = EM2820_VMUX_SVIDEO,
			.vmux     = 9,
			.amux     = 1,
		}},
	},
	[EM2800_BOARD_TERRATEC_CINERGY_200] = {
		.name         = "Terratec Cinergy 200 USB",
		.chip_id      = 0x4,
		.is_em2800    = 1,
		.vchannels    = 3,
		.norm         = VIDEO_MODE_PAL,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.has_tuner    = 1,
		.decoder      = EM2820_SAA7113,
		.input          = {{
			.type     = EM2820_VMUX_TELEVISION,
			.vmux     = 2,
			.amux     = 0,
		},{
			.type     = EM2820_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = 1,
		},{
			.type     = EM2820_VMUX_SVIDEO,
			.vmux     = 9,
			.amux     = 1,
		}},
	},
	[EM2800_BOARD_LEADTEK_WINFAST_USBII] = {
		.name         = "Leadtek Winfast USB II",
		.chip_id      = 0x2,
		.is_em2800    = 1,
		.vchannels    = 3,
		.norm         = VIDEO_MODE_PAL,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.has_tuner    = 1,
		.decoder      = EM2820_SAA7113,
		.input          = {{
			.type     = EM2820_VMUX_TELEVISION,
			.vmux     = 2,
			.amux     = 0,
		},{
			.type     = EM2820_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = 1,
		},{
			.type     = EM2820_VMUX_SVIDEO,
			.vmux     = 9,
			.amux     = 1,
		}},
	},
	[EM2800_BOARD_KWORLD_USB2800] = {
		.name         = "Kworld USB2800",
		.chip_id      = 0x7,
		.is_em2800    = 1,
		.vchannels    = 3,
		.norm         = VIDEO_MODE_PAL,
		.tuner_type   = TUNER_PHILIPS_ATSC,
		.tda9887_conf = TDA9887_PRESENT,
		.has_tuner    = 1,
		.decoder      = EM2820_SAA7113,
		.input          = {{
			.type     = EM2820_VMUX_TELEVISION,
			.vmux     = 2,
			.amux     = 0,
		},{
			.type     = EM2820_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = 1,
		},{
			.type     = EM2820_VMUX_SVIDEO,
			.vmux     = 9,
			.amux     = 1,
		}},
	},
};
const unsigned int em2820_bcount = ARRAY_SIZE(em2820_boards);

/* table of devices that work with this driver */
struct usb_device_id em2820_id_table [] = {
	{ USB_DEVICE(0xeb1a, 0x2800), .driver_info = EM2800_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2820), .driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0x0ccd, 0x0036), .driver_info = EM2820_BOARD_TERRATEC_CINERGY_250 },
	{ USB_DEVICE(0x2304, 0x0208), .driver_info = EM2820_BOARD_PINNACLE_USB_2 },
	{ USB_DEVICE(0x2040, 0x4200), .driver_info = EM2820_BOARD_HAUPPAUGE_WINTV_USB_2 },
	{ },
};

void em2820_card_setup(struct em2820 *dev)
{
	/* request some modules */
	if (dev->model == EM2820_BOARD_HAUPPAUGE_WINTV_USB_2) {
		struct tveeprom tv;
#ifdef CONFIG_MODULES
		request_module("tveeprom");
#endif
		/* Call first TVeeprom */

		tveeprom_hauppauge_analog(&dev->i2c_client, &tv, dev->eedata);

		dev->tuner_type= tv.tuner_type;
		if (tv.audio_processor == AUDIO_CHIP_MSP34XX) {
			dev->has_msp34xx=1;
		} else dev->has_msp34xx=0;
	}
}

EXPORT_SYMBOL(em2820_boards);
EXPORT_SYMBOL(em2820_bcount);
EXPORT_SYMBOL(em2820_id_table);

MODULE_DEVICE_TABLE (usb, em2820_id_table);
