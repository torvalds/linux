/*
   em28xx-cards.c - driver for Empia EM2800/EM2820/2840 USB
		    video capture devices

   Copyright (C) 2005 Ludovico Cavedon <cavedon@sssup.it>
		      Markus Rechberger <mrechberger@gmail.com>
		      Mauro Carvalho Chehab <mchehab@infradead.org>
		      Sascha Sommer <saschasommer@freenet.de>

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
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/usb.h>
#include <media/tuner.h>
#include <media/msp3400.h>
#include <media/saa7115.h>
#include <media/tvp5150.h>
#include <media/tveeprom.h>
#include <media/audiochip.h>
#include <media/v4l2-common.h>

#include "em28xx.h"
#include "tuner-xc2028.h"

static int tuner = -1;
module_param(tuner, int, 0444);
MODULE_PARM_DESC(tuner, "tuner type");

static unsigned int disable_ir;
module_param(disable_ir, int, 0444);
MODULE_PARM_DESC(disable_ir, "disable infrared remote support");

struct em28xx_hash_table {
	unsigned long hash;
	unsigned int  model;
	unsigned int  tuner;
};

/* Boards supported by driver */

#define EM2800_BOARD_UNKNOWN			0
#define EM2820_BOARD_UNKNOWN			1
#define EM2820_BOARD_TERRATEC_CINERGY_250	2
#define EM2820_BOARD_PINNACLE_USB_2		3
#define EM2820_BOARD_HAUPPAUGE_WINTV_USB_2      4
#define EM2820_BOARD_MSI_VOX_USB_2              5
#define EM2800_BOARD_TERRATEC_CINERGY_200       6
#define EM2800_BOARD_LEADTEK_WINFAST_USBII      7
#define EM2800_BOARD_KWORLD_USB2800             8
#define EM2820_BOARD_PINNACLE_DVC_90		9
#define EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900	10
#define EM2880_BOARD_TERRATEC_HYBRID_XS		11
#define EM2820_BOARD_KWORLD_PVRTV2800RF		12
#define EM2880_BOARD_TERRATEC_PRODIGY_XS	13
#define EM2820_BOARD_PROLINK_PLAYTV_USB2	14
#define EM2800_BOARD_VGEAR_POCKETTV             15
#define EM2880_BOARD_HAUPPAUGE_WINTV_HVR_950	16

struct em28xx_board em28xx_boards[] = {
	[EM2800_BOARD_UNKNOWN] = {
		.name         = "Unknown EM2800 video grabber",
		.is_em2800    = 1,
		.vchannels    = 2,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA7113,
		.input           = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = 1,
		} },
	},
	[EM2820_BOARD_UNKNOWN] = {
		.name         = "Unknown EM2750/28xx video grabber",
		.is_em2800    = 0,
		.tuner_type   = TUNER_ABSENT,
	},
	[EM2820_BOARD_KWORLD_PVRTV2800RF] = {
		.name         = "Kworld PVR TV 2800 RF",
		.is_em2800    = 0,
		.vchannels    = 2,
		.tuner_type   = TUNER_TEMIC_PAL,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA7113,
		.input           = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = 1,
		} },
	},
	[EM2820_BOARD_TERRATEC_CINERGY_250] = {
		.name         = "Terratec Cinergy 250 USB",
		.vchannels    = 3,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA7113,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = 1,
		} },
	},
	[EM2820_BOARD_PINNACLE_USB_2] = {
		.name         = "Pinnacle PCTV USB 2",
		.vchannels    = 3,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA7113,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = 0,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = 1,
		} },
	},
	[EM2820_BOARD_HAUPPAUGE_WINTV_USB_2] = {
		.name         = "Hauppauge WinTV USB 2",
		.vchannels    = 3,
		.tuner_type   = TUNER_PHILIPS_FM1236_MK3,
		.tda9887_conf = TDA9887_PRESENT |
				TDA9887_PORT1_ACTIVE|
				TDA9887_PORT2_ACTIVE,
		.decoder      = EM28XX_TVP5150,
		.has_msp34xx  = 1,
		/*FIXME: S-Video not tested */
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = MSP_INPUT_DEFAULT,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = MSP_INPUT(MSP_IN_SCART1, MSP_IN_TUNER1,
					MSP_DSP_IN_SCART, MSP_DSP_IN_SCART),
		} },
	},
	[EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900] = {
		.name         = "Hauppauge WinTV HVR 900",
		.vchannels    = 3,
		.tda9887_conf = TDA9887_PRESENT,
		.tuner_type   = TUNER_XC2028,
		.mts_firmware = 1,
		.decoder      = EM28XX_TVP5150,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = 0,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = 1,
		} },
	},
	[EM2880_BOARD_HAUPPAUGE_WINTV_HVR_950] = {
		.name           = "Hauppauge WinTV HVR 950",
		.vchannels      = 3,
		.tda9887_conf   = TDA9887_PRESENT,
		.tuner_type     = TUNER_XC2028,
		.mts_firmware   = 1,
		.has_12mhz_i2s  = 1,
		.decoder        = EM28XX_TVP5150,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = 0,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = 1,
		} },

		/* gpio's 4, 1, 0 */
		.analog_gpio = 0x003d2d,
	},
	[EM2880_BOARD_TERRATEC_HYBRID_XS] = {
		.name         = "Terratec Hybrid XS",
		.vchannels    = 3,
		.tda9887_conf = TDA9887_PRESENT,
		.tuner_type   = TUNER_XC2028,
		.decoder      = EM28XX_TVP5150,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = 0,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = 1,
		} },
	},
	/* maybe there's a reason behind it why Terratec sells the Hybrid XS
	   as Prodigy XS with a different PID, let's keep it separated for now
	   maybe we'll need it lateron */
	[EM2880_BOARD_TERRATEC_PRODIGY_XS] = {
		.name         = "Terratec Prodigy XS",
		.vchannels    = 3,
		.tda9887_conf = TDA9887_PRESENT,
		.tuner_type   = TUNER_XC2028,
		.decoder      = EM28XX_TVP5150,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = 0,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = 1,
		} },
	},
	[EM2820_BOARD_MSI_VOX_USB_2] = {
		.name		   = "MSI VOX USB 2.0",
		.vchannels	   = 3,
		.tuner_type	   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf	   = TDA9887_PRESENT      |
				     TDA9887_PORT1_ACTIVE |
				     TDA9887_PORT2_ACTIVE,
		.max_range_640_480 = 1,

		.decoder           = EM28XX_SAA7114,
		.input             = { {
			.type      = EM28XX_VMUX_TELEVISION,
			.vmux      = SAA7115_COMPOSITE4,
			.amux      = 0,
		}, {
			.type      = EM28XX_VMUX_COMPOSITE1,
			.vmux      = SAA7115_COMPOSITE0,
			.amux      = 1,
		}, {
			.type      = EM28XX_VMUX_SVIDEO,
			.vmux      = SAA7115_SVIDEO3,
			.amux      = 1,
		} },
	},
	[EM2800_BOARD_TERRATEC_CINERGY_200] = {
		.name         = "Terratec Cinergy 200 USB",
		.is_em2800    = 1,
		.vchannels    = 3,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA7113,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = 0,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = 1,
		} },
	},
	[EM2800_BOARD_LEADTEK_WINFAST_USBII] = {
		.name         = "Leadtek Winfast USB II",
		.is_em2800    = 1,
		.vchannels    = 3,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA7113,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = 0,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = 1,
		} },
	},
	[EM2800_BOARD_KWORLD_USB2800] = {
		.name         = "Kworld USB2800",
		.is_em2800    = 1,
		.vchannels    = 3,
		.tuner_type   = TUNER_PHILIPS_ATSC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA7113,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = 0,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = 1,
		} },
	},
	[EM2820_BOARD_PINNACLE_DVC_90] = {
		.name         = "Pinnacle Dazzle DVC 90/DVC 100",
		.vchannels    = 3,
		.tuner_type   = TUNER_ABSENT,
		.decoder      = EM28XX_SAA7113,
		.input          = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = 1,
		} },
	},
	[EM2800_BOARD_VGEAR_POCKETTV] = {
		.name         = "V-Gear PocketTV",
		.is_em2800    = 1,
		.vchannels    = 3,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA7113,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = 0,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = 1,
		} },
	},
	[EM2820_BOARD_PROLINK_PLAYTV_USB2] = {
		.name         = "Pixelview Prolink PlayTV USB 2.0",
		.vchannels    = 3,
		.tda9887_conf = TDA9887_PRESENT,
		.tuner_type   = TUNER_YMEC_TVF_5533MF,
		.decoder      = EM28XX_SAA7113,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
};
const unsigned int em28xx_bcount = ARRAY_SIZE(em28xx_boards);

/* table of devices that work with this driver */
struct usb_device_id em28xx_id_table [] = {
	{ USB_DEVICE(0xeb1a, 0x2750),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2800),
			.driver_info = EM2800_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2820),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2821),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2860),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2861),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2870),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2881),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2883),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0x0ccd, 0x0036),
			.driver_info = EM2820_BOARD_TERRATEC_CINERGY_250 },
	{ USB_DEVICE(0x2304, 0x0208),
			.driver_info = EM2820_BOARD_PINNACLE_USB_2 },
	{ USB_DEVICE(0x2040, 0x4200),
			.driver_info = EM2820_BOARD_HAUPPAUGE_WINTV_USB_2 },
	{ USB_DEVICE(0x2040, 0x4201),
			.driver_info = EM2820_BOARD_HAUPPAUGE_WINTV_USB_2 },
	{ USB_DEVICE(0x2304, 0x0207),
			.driver_info = EM2820_BOARD_PINNACLE_DVC_90 },
	{ USB_DEVICE(0x2304, 0x021a),
			.driver_info = EM2820_BOARD_PINNACLE_DVC_90 },
	{ USB_DEVICE(0x2040, 0x6500),
			.driver_info = EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900 },
	{ USB_DEVICE(0x2040, 0x6502),
			.driver_info = EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900 },
	{ USB_DEVICE(0x2040, 0x6513),
			.driver_info = EM2880_BOARD_HAUPPAUGE_WINTV_HVR_950 },
	{ USB_DEVICE(0x0ccd, 0x0042),
			.driver_info = EM2880_BOARD_TERRATEC_HYBRID_XS },
	{ USB_DEVICE(0x0ccd, 0x0047),
			.driver_info = EM2880_BOARD_TERRATEC_PRODIGY_XS },
	{ },
};
MODULE_DEVICE_TABLE(usb, em28xx_id_table);

/* EEPROM hash table for devices with generic USB IDs */
static struct em28xx_hash_table em28xx_eeprom_hash [] = {
	/* P/N: SA 60002070465 Tuner: TVF7533-MF */
	{0x6ce05a8f, EM2820_BOARD_PROLINK_PLAYTV_USB2, TUNER_YMEC_TVF_5533MF},
};

/* I2C devicelist hash table for devices with generic USB IDs */
static struct em28xx_hash_table em28xx_i2c_hash[] = {
	{0xb06a32c3, EM2800_BOARD_TERRATEC_CINERGY_200, TUNER_LG_PAL_NEW_TAPC},
	{0xf51200e3, EM2800_BOARD_VGEAR_POCKETTV, TUNER_LG_PAL_NEW_TAPC},
};

/* Since em28xx_pre_card_setup() requires a proper dev->model,
 * this won't work for boards with generic PCI IDs
 */
void em28xx_pre_card_setup(struct em28xx *dev)
{
	/* request some modules */
	switch (dev->model) {
	case EM2880_BOARD_TERRATEC_PRODIGY_XS:
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900:
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_950:
	case EM2880_BOARD_TERRATEC_HYBRID_XS:
		em28xx_write_regs(dev, XCLK_REG, "\x27", 1);
		em28xx_write_regs(dev, I2C_CLK_REG, "\x40", 1);
		em28xx_write_regs(dev, 0x08, "\xff", 1);
		em28xx_write_regs(dev, 0x04, "\x00", 1);
		msleep(100);
		em28xx_write_regs(dev, 0x04, "\x08", 1);
		msleep(100);
		em28xx_write_regs(dev, 0x08, "\xff", 1);
		msleep(50);
		em28xx_write_regs(dev, 0x08, "\x2d", 1);
		msleep(50);
		em28xx_write_regs(dev, 0x08, "\x3d", 1);
		break;
	}
}

static int em28xx_tuner_callback(void *ptr, int command, int arg)
{
	int rc = 0;
	struct em28xx *dev = ptr;

	if (dev->tuner_type != TUNER_XC2028)
		return 0;

	switch (command) {
	case XC2028_TUNER_RESET:
	{
		/* GPIO and initialization codes for analog TV and radio
		   This code should be complemented for DTV, since reset
		   codes are different.
		 */

		dev->em28xx_write_regs_req(dev, 0x00, 0x48, "\x00", 1);
		dev->em28xx_write_regs_req(dev, 0x00, 0x12, "\x67", 1);

		if (dev->analog_gpio) {
			char gpio0 = dev->analog_gpio & 0xff;
			char gpio1 = (dev->analog_gpio >> 8) & 0xff;
			char gpio4 = dev->analog_gpio >> 24;

			if (gpio4) {
				dev->em28xx_write_regs(dev, 0x04, &gpio4, 1);
				msleep(140);
			}

			msleep(6);
			dev->em28xx_write_regs(dev, 0x08, &gpio0, 1);
			msleep(10);
			dev->em28xx_write_regs(dev, 0x08, &gpio1, 1);
			msleep(5);
		}

		break;
	}
	}
	return rc;
}

static void em28xx_config_tuner(struct em28xx *dev)
{
	struct v4l2_priv_tun_config  xc2028_cfg;
	struct xc2028_ctrl           ctl;
	struct tuner_setup           tun_setup;
	struct v4l2_frequency        f;

	if (dev->tuner_type == TUNER_ABSENT)
		return;

	tun_setup.mode_mask = T_ANALOG_TV | T_RADIO;
	tun_setup.type = dev->tuner_type;
	tun_setup.addr = dev->tuner_addr;
	tun_setup.tuner_callback = em28xx_tuner_callback;

	em28xx_i2c_call_clients(dev, TUNER_SET_TYPE_ADDR, &tun_setup);

	if (dev->tuner_type == TUNER_XC2028) {
		memset(&ctl, 0, sizeof(ctl));

		ctl.fname   = XC2028_DEFAULT_FIRMWARE;
		ctl.max_len = 64;
		ctl.mts = em28xx_boards[dev->model].mts_firmware;

		xc2028_cfg.tuner = TUNER_XC2028;
		xc2028_cfg.priv  = &ctl;

		em28xx_i2c_call_clients(dev, TUNER_SET_CONFIG, &xc2028_cfg);
	}

	/* configure tuner */
	f.tuner = 0;
	f.type = V4L2_TUNER_ANALOG_TV;
	f.frequency = 9076;     /* just a magic number */
	dev->ctl_freq = f.frequency;
	em28xx_i2c_call_clients(dev, VIDIOC_S_FREQUENCY, &f);
}

static int em28xx_hint_board(struct em28xx *dev)
{
	int i;

	/* HINT method: EEPROM
	 *
	 * This method works only for boards with eeprom.
	 * Uses a hash of all eeprom bytes. The hash should be
	 * unique for a vendor/tuner pair.
	 * There are a high chance that tuners for different
	 * video standards produce different hashes.
	 */
	for (i = 0; i < ARRAY_SIZE(em28xx_eeprom_hash); i++) {
		if (dev->hash == em28xx_eeprom_hash[i].hash) {
			dev->model = em28xx_eeprom_hash[i].model;
			dev->tuner_type = em28xx_eeprom_hash[i].tuner;

			em28xx_errdev("Your board has no unique USB ID.\n");
			em28xx_errdev("A hint were successfully done, "
				      "based on eeprom hash.\n");
			em28xx_errdev("This method is not 100%% failproof.\n");
			em28xx_errdev("If the board were missdetected, "
				      "please email this log to:\n");
			em28xx_errdev("\tV4L Mailing List "
				      " <video4linux-list@redhat.com>\n");
			em28xx_errdev("Board detected as %s\n",
				      em28xx_boards[dev->model].name);

			return 0;
		}
	}

	/* HINT method: I2C attached devices
	 *
	 * This method works for all boards.
	 * Uses a hash of i2c scanned devices.
	 * Devices with the same i2c attached chips will
	 * be considered equal.
	 * This method is less precise than the eeprom one.
	 */

	/* user did not request i2c scanning => do it now */
	if (!dev->i2c_hash)
		em28xx_do_i2c_scan(dev);

	for (i = 0; i < ARRAY_SIZE(em28xx_i2c_hash); i++) {
		if (dev->i2c_hash == em28xx_i2c_hash[i].hash) {
			dev->model = em28xx_i2c_hash[i].model;
			dev->tuner_type = em28xx_i2c_hash[i].tuner;
			em28xx_errdev("Your board has no unique USB ID.\n");
			em28xx_errdev("A hint were successfully done, "
				      "based on i2c devicelist hash.\n");
			em28xx_errdev("This method is not 100%% failproof.\n");
			em28xx_errdev("If the board were missdetected, "
				      "please email this log to:\n");
			em28xx_errdev("\tV4L Mailing List "
				      " <video4linux-list@redhat.com>\n");
			em28xx_errdev("Board detected as %s\n",
				      em28xx_boards[dev->model].name);

			return 0;
		}
	}

	em28xx_errdev("Your board has no unique USB ID and thus need a "
		      "hint to be detected.\n");
	em28xx_errdev("You may try to use card=<n> insmod option to "
		      "workaround that.\n");
	em28xx_errdev("Please send an email with this log to:\n");
	em28xx_errdev("\tV4L Mailing List <video4linux-list@redhat.com>\n");
	em28xx_errdev("Board eeprom hash is 0x%08lx\n", dev->hash);
	em28xx_errdev("Board i2c devicelist hash is 0x%08lx\n", dev->i2c_hash);

	em28xx_errdev("Here is a list of valid choices for the card=<n>"
		      " insmod option:\n");
	for (i = 0; i < em28xx_bcount; i++) {
		em28xx_errdev("    card=%d -> %s\n",
				i, em28xx_boards[i].name);
	}
	return -1;
}


static void em28xx_set_model(struct em28xx *dev)
{
	dev->is_em2800 = em28xx_boards[dev->model].is_em2800;
	dev->has_msp34xx = em28xx_boards[dev->model].has_msp34xx;
	dev->tda9887_conf = em28xx_boards[dev->model].tda9887_conf;
	dev->decoder = em28xx_boards[dev->model].decoder;
	dev->video_inputs = em28xx_boards[dev->model].vchannels;
	dev->analog_gpio = em28xx_boards[dev->model].analog_gpio;
	dev->has_12mhz_i2s = em28xx_boards[dev->model].has_12mhz_i2s;
	dev->max_range_640_480 = em28xx_boards[dev->model].max_range_640_480;
}

/* ----------------------------------------------------------------------- */
void em28xx_set_ir(struct em28xx *dev, struct IR_i2c *ir)
{
	if (disable_ir) {
		ir->get_key = NULL;
		return ;
	}

	/* detect & configure */
	switch (dev->model) {
	case (EM2800_BOARD_UNKNOWN):
		break;
	case (EM2820_BOARD_UNKNOWN):
		break;
	case (EM2800_BOARD_TERRATEC_CINERGY_200):
	case (EM2820_BOARD_TERRATEC_CINERGY_250):
		ir->ir_codes = ir_codes_em_terratec;
		ir->get_key = em28xx_get_key_terratec;
		snprintf(ir->c.name, sizeof(ir->c.name),
			 "i2c IR (EM28XX Terratec)");
		break;
	case (EM2820_BOARD_PINNACLE_USB_2):
		ir->ir_codes = ir_codes_pinnacle_grey;
		ir->get_key = em28xx_get_key_pinnacle_usb_grey;
		snprintf(ir->c.name, sizeof(ir->c.name),
			 "i2c IR (EM28XX Pinnacle PCTV)");
		break;
	case (EM2820_BOARD_HAUPPAUGE_WINTV_USB_2):
		ir->ir_codes = ir_codes_hauppauge_new;
		ir->get_key = em28xx_get_key_em_haup;
		snprintf(ir->c.name, sizeof(ir->c.name),
			 "i2c IR (EM2840 Hauppauge)");
		break;
	case (EM2820_BOARD_MSI_VOX_USB_2):
		break;
	case (EM2800_BOARD_LEADTEK_WINFAST_USBII):
		break;
	case (EM2800_BOARD_KWORLD_USB2800):
		break;
	}
}

void em28xx_card_setup(struct em28xx *dev)
{
	em28xx_set_model(dev);

	dev->tuner_type = em28xx_boards[dev->model].tuner_type;

	/* request some modules */
	switch (dev->model) {
	case EM2820_BOARD_HAUPPAUGE_WINTV_USB_2:
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900:
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_950:
	{
		struct tveeprom tv;
#ifdef CONFIG_MODULES
		request_module("tveeprom");
#endif
		/* Call first TVeeprom */

		dev->i2c_client.addr = 0xa0 >> 1;
		tveeprom_hauppauge_analog(&dev->i2c_client, &tv, dev->eedata);

		dev->tuner_type = tv.tuner_type;

		if (tv.audio_processor == AUDIO_CHIP_MSP34XX) {
			dev->i2s_speed = 2048000;
			dev->has_msp34xx = 1;
		}
#ifdef CONFIG_MODULES
		if (tv.has_ir)
			request_module("ir-kbd-i2c");
#endif
		break;
	}
	case EM2820_BOARD_KWORLD_PVRTV2800RF:
		/* GPIO enables sound on KWORLD PVR TV 2800RF */
		em28xx_write_regs_req(dev, 0x00, 0x08, "\xf9", 1);
		break;
	case EM2820_BOARD_UNKNOWN:
	case EM2800_BOARD_UNKNOWN:
		if (!em28xx_hint_board(dev))
			em28xx_set_model(dev);
	}

	/* Allow override tuner type by a module parameter */
	if (tuner >= 0)
		dev->tuner_type = tuner;

#ifdef CONFIG_MODULES
	/* request some modules */
	if (dev->has_msp34xx)
		request_module("msp3400");
	if (dev->decoder == EM28XX_SAA7113 || dev->decoder == EM28XX_SAA7114)
		request_module("saa7115");
	if (dev->decoder == EM28XX_TVP5150)
		request_module("tvp5150");
	if (dev->tuner_type != TUNER_ABSENT)
		request_module("tuner");
#endif

	em28xx_config_tuner(dev);
}
