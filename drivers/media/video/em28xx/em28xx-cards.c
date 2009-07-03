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
#include <media/tvaudio.h>
#include <media/i2c-addr.h>
#include <media/tveeprom.h>
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>

#include "em28xx.h"

#define DRIVER_NAME         "em28xx"

static int tuner = -1;
module_param(tuner, int, 0444);
MODULE_PARM_DESC(tuner, "tuner type");

static unsigned int disable_ir;
module_param(disable_ir, int, 0444);
MODULE_PARM_DESC(disable_ir, "disable infrared remote support");

static unsigned int disable_usb_speed_check;
module_param(disable_usb_speed_check, int, 0444);
MODULE_PARM_DESC(disable_usb_speed_check,
		 "override min bandwidth requirement of 480M bps");

static unsigned int card[]     = {[0 ... (EM28XX_MAXBOARDS - 1)] = UNSET };
module_param_array(card,  int, NULL, 0444);
MODULE_PARM_DESC(card,     "card type");

/* Bitmask marking allocated devices from 0 to EM28XX_MAXBOARDS */
static unsigned long em28xx_devused;

struct em28xx_hash_table {
	unsigned long hash;
	unsigned int  model;
	unsigned int  tuner;
};

/*
 *  Reset sequences for analog/digital modes
 */

/* Reset for the most [analog] boards */
static struct em28xx_reg_seq default_analog[] = {
	{EM28XX_R08_GPIO,	0x6d,   ~EM_GPIO_4,	10},
	{	-1,		-1,	-1,		-1},
};

/* Reset for the most [digital] boards */
static struct em28xx_reg_seq default_digital[] = {
	{EM28XX_R08_GPIO,	0x6e,	~EM_GPIO_4,	10},
	{	-1,		-1,	-1,		-1},
};

/* Board Hauppauge WinTV HVR 900 analog */
static struct em28xx_reg_seq hauppauge_wintv_hvr_900_analog[] = {
	{EM28XX_R08_GPIO,	0x2d,	~EM_GPIO_4,	10},
	{0x05,			0xff,	0x10,		10},
	{  -1,			-1,	-1,		-1},
};

/* Board Hauppauge WinTV HVR 900 digital */
static struct em28xx_reg_seq hauppauge_wintv_hvr_900_digital[] = {
	{EM28XX_R08_GPIO,	0x2e,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x04,	0x0f,		10},
	{EM2880_R04_GPO,	0x0c,	0x0f,		10},
	{ -1,			-1,	-1,		-1},
};

/* Boards - EM2880 MSI DIGIVOX AD and EM2880_BOARD_MSI_DIGIVOX_AD_II */
static struct em28xx_reg_seq em2880_msi_digivox_ad_analog[] = {
	{EM28XX_R08_GPIO,       0x69,   ~EM_GPIO_4,	 10},
	{	-1,		-1,	-1,		 -1},
};

/* Boards - EM2880 MSI DIGIVOX AD and EM2880_BOARD_MSI_DIGIVOX_AD_II */

/* Board  - EM2870 Kworld 355u
   Analog - No input analog */

/* Board - EM2882 Kworld 315U digital */
static struct em28xx_reg_seq em2882_kworld_315u_digital[] = {
	{EM28XX_R08_GPIO,	0xff,	0xff,		10},
	{EM28XX_R08_GPIO,	0xfe,	0xff,		10},
	{EM2880_R04_GPO,	0x04,	0xff,		10},
	{EM2880_R04_GPO,	0x0c,	0xff,		10},
	{EM28XX_R08_GPIO,	0x7e,	0xff,		10},
	{  -1,			-1,	-1,		-1},
};

static struct em28xx_reg_seq em2882_kworld_315u_tuner_gpio[] = {
	{EM2880_R04_GPO,	0x08,	0xff,		10},
	{EM2880_R04_GPO,	0x0c,	0xff,		10},
	{EM2880_R04_GPO,	0x08,	0xff,		10},
	{EM2880_R04_GPO,	0x0c,	0xff,		10},
	{  -1,			-1,	-1,		-1},
};

static struct em28xx_reg_seq kworld_330u_analog[] = {
	{EM28XX_R08_GPIO,	0x6d,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x00,	0xff,		10},
	{ -1,			-1,	-1,		-1},
};

static struct em28xx_reg_seq kworld_330u_digital[] = {
	{EM28XX_R08_GPIO,	0x6e,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x08,	0xff,		10},
	{ -1,			-1,	-1,		-1},
};

/* Evga inDtube
   GPIO0 - Enable digital power (s5h1409) - low to enable
   GPIO1 - Enable analog power (tvp5150/emp202) - low to enable
   GPIO4 - xc3028 reset
   GOP3  - s5h1409 reset
 */
static struct em28xx_reg_seq evga_indtube_analog[] = {
	{EM28XX_R08_GPIO,	0x79,   0xff,		60},
	{	-1,		-1,	-1,		-1},
};

static struct em28xx_reg_seq evga_indtube_digital[] = {
	{EM28XX_R08_GPIO,	0x7a,	0xff,		 1},
	{EM2880_R04_GPO,	0x04,	0xff,		10},
	{EM2880_R04_GPO,	0x0c,	0xff,		 1},
	{ -1,			-1,	-1,		-1},
};

/* Callback for the most boards */
static struct em28xx_reg_seq default_tuner_gpio[] = {
	{EM28XX_R08_GPIO,	EM_GPIO_4,	EM_GPIO_4,	10},
	{EM28XX_R08_GPIO,	0,		EM_GPIO_4,	10},
	{EM28XX_R08_GPIO,	EM_GPIO_4,	EM_GPIO_4,	10},
	{  -1,			-1,		-1,		-1},
};

/* Mute/unmute */
static struct em28xx_reg_seq compro_unmute_tv_gpio[] = {
	{EM28XX_R08_GPIO,	5,		7,		10},
	{  -1,			-1,		-1,		-1},
};

static struct em28xx_reg_seq compro_unmute_svid_gpio[] = {
	{EM28XX_R08_GPIO,	4,		7,		10},
	{  -1,			-1,		-1,		-1},
};

static struct em28xx_reg_seq compro_mute_gpio[] = {
	{EM28XX_R08_GPIO,	6,		7,		10},
	{  -1,			-1,		-1,		-1},
};

/* Terratec AV350 */
static struct em28xx_reg_seq terratec_av350_mute_gpio[] = {
	{EM28XX_R08_GPIO,	0xff,	0x7f,		10},
	{	-1,		-1,	-1,		-1},
};

static struct em28xx_reg_seq terratec_av350_unmute_gpio[] = {
	{EM28XX_R08_GPIO,	0xff,	0xff,		10},
	{	-1,		-1,	-1,		-1},
};
/*
 *  Board definitions
 */
struct em28xx_board em28xx_boards[] = {
	[EM2750_BOARD_UNKNOWN] = {
		.name          = "Unknown EM2750/EM2751 webcam grabber",
		.xclk          = EM28XX_XCLK_FREQUENCY_48MHZ,
		.tuner_type    = TUNER_ABSENT,	/* This is a webcam */
		.input         = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = EM28XX_AMUX_VIDEO,
		} },
	},
	[EM2800_BOARD_UNKNOWN] = {
		.name         = "Unknown EM2800 video grabber",
		.is_em2800    = 1,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.tuner_type   = TUNER_ABSENT,
		.input        = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2820_BOARD_UNKNOWN] = {
		.name          = "Unknown EM2750/28xx video grabber",
		.tuner_type    = TUNER_ABSENT,
	},
	[EM2750_BOARD_DLCW_130] = {
		/* Beijing Huaqi Information Digital Technology Co., Ltd */
		.name          = "Huaqi DLCW-130",
		.valid         = EM28XX_BOARD_NOT_VALIDATED,
		.xclk          = EM28XX_XCLK_FREQUENCY_48MHZ,
		.tuner_type    = TUNER_ABSENT,	/* This is a webcam */
		.input         = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = EM28XX_AMUX_VIDEO,
		} },
	},
	[EM2820_BOARD_KWORLD_PVRTV2800RF] = {
		.name         = "Kworld PVR TV 2800 RF",
		.tuner_type   = TUNER_TEMIC_PAL,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2820_BOARD_GADMEI_TVR200] = {
		.name         = "Gadmei TVR200",
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
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
	[EM2820_BOARD_TERRATEC_CINERGY_250] = {
		.name         = "Terratec Cinergy 250 USB",
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
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
	[EM2820_BOARD_PINNACLE_USB_2] = {
		.name         = "Pinnacle PCTV USB 2",
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = EM28XX_AMUX_VIDEO,
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
	[EM2820_BOARD_HAUPPAUGE_WINTV_USB_2] = {
		.name         = "Hauppauge WinTV USB 2",
		.tuner_type   = TUNER_PHILIPS_FM1236_MK3,
		.tda9887_conf = TDA9887_PRESENT |
				TDA9887_PORT1_ACTIVE |
				TDA9887_PORT2_ACTIVE,
		.decoder      = EM28XX_TVP5150,
		.has_msp34xx  = 1,
		.input        = { {
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
	[EM2820_BOARD_DLINK_USB_TV] = {
		.name         = "D-Link DUB-T210 TV Tuner",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
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
	[EM2820_BOARD_HERCULES_SMART_TV_USB2] = {
		.name         = "Hercules Smart TV USB 2.0",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
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
	[EM2820_BOARD_PINNACLE_USB_2_FM1216ME] = {
		.name         = "Pinnacle PCTV USB 2 (Philips FM1216ME)",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_PHILIPS_FM1216ME_MK3,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = EM28XX_AMUX_VIDEO,
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
	[EM2820_BOARD_GADMEI_UTV310] = {
		.name         = "Gadmei UTV310",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_TNF_5335MF,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE1,
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
	[EM2820_BOARD_LEADTEK_WINFAST_USBII_DELUXE] = {
		.name         = "Leadtek Winfast USB II Deluxe",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_PHILIPS_FM1216ME_MK3,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = EM28XX_AMUX_VIDEO,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2820_BOARD_VIDEOLOGY_20K14XUSB] = {
		.name         = "Videology 20K14XUSB USB2.0",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_ABSENT,	/* This is a webcam */
		.input        = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = EM28XX_AMUX_VIDEO,
		} },
	},
	[EM2821_BOARD_SUPERCOMP_USB_2] = {
		.name         = "Supercomp USB 2.0 TV",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_PHILIPS_FM1236_MK3,
		.tda9887_conf = TDA9887_PRESENT |
				TDA9887_PORT1_ACTIVE |
				TDA9887_PORT2_ACTIVE,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2821_BOARD_USBGEAR_VD204] = {
		.name         = "Usbgear VD204v9",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_ABSENT,	/* Capture only device */
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type  = EM28XX_VMUX_COMPOSITE1,
			.vmux  = SAA7115_COMPOSITE0,
			.amux  = EM28XX_AMUX_LINE_IN,
		}, {
			.type  = EM28XX_VMUX_SVIDEO,
			.vmux  = SAA7115_SVIDEO3,
			.amux  = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2860_BOARD_NETGMBH_CAM] = {
		/* Beijing Huaqi Information Digital Technology Co., Ltd */
		.name         = "NetGMBH Cam",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_ABSENT,	/* This is a webcam */
		.input        = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = EM28XX_AMUX_VIDEO,
		} },
	},
	[EM2860_BOARD_TYPHOON_DVD_MAKER] = {
		.name         = "Typhoon DVD Maker",
		.decoder      = EM28XX_SAA711X,
		.tuner_type   = TUNER_ABSENT,	/* Capture only device */
		.input        = { {
			.type  = EM28XX_VMUX_COMPOSITE1,
			.vmux  = SAA7115_COMPOSITE0,
			.amux  = EM28XX_AMUX_LINE_IN,
		}, {
			.type  = EM28XX_VMUX_SVIDEO,
			.vmux  = SAA7115_SVIDEO3,
			.amux  = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2860_BOARD_GADMEI_UTV330] = {
		.name         = "Gadmei UTV330",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_TNF_5335MF,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = EM28XX_AMUX_VIDEO,
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
	[EM2860_BOARD_TERRATEC_HYBRID_XS] = {
		.name         = "Terratec Cinergy A Hybrid XS",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,

		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		} },
	},
	[EM2861_BOARD_KWORLD_PVRTV_300U] = {
		.name	      = "KWorld PVRTV 300U",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2861_BOARD_YAKUMO_MOVIE_MIXER] = {
		.name          = "Yakumo MovieMixer",
		.tuner_type    = TUNER_ABSENT,	/* Capture only device */
		.decoder       = EM28XX_TVP5150,
		.input         = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2861_BOARD_PLEXTOR_PX_TV100U] = {
		.name         = "Plextor ConvertX PX-TV100U",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_TNF_5335MF,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},

	/* Those boards with em2870 are DVB Only*/

	[EM2870_BOARD_TERRATEC_XS] = {
		.name         = "Terratec Cinergy T XS",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
	},
	[EM2870_BOARD_TERRATEC_XS_MT2060] = {
		.name         = "Terratec Cinergy T XS (MT2060)",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_ABSENT, /* MT2060 */
	},
	[EM2870_BOARD_KWORLD_350U] = {
		.name         = "Kworld 350 U DVB-T",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
	},
	[EM2870_BOARD_KWORLD_355U] = {
		.name         = "Kworld 355 U DVB-T",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
	},
	[EM2870_BOARD_PINNACLE_PCTV_DVB] = {
		.name         = "Pinnacle PCTV DVB-T",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_ABSENT, /* MT2060 */
		/* djh - I have serious doubts this is right... */
		.xclk         = EM28XX_XCLK_IR_RC5_MODE |
				EM28XX_XCLK_FREQUENCY_10MHZ,
	},
	[EM2870_BOARD_COMPRO_VIDEOMATE] = {
		.name         = "Compro, VideoMate U3",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_ABSENT, /* MT2060 */
	},

	[EM2880_BOARD_TERRATEC_HYBRID_XS_FR] = {
		.name         = "Terratec Hybrid XS Secam",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.has_msp34xx  = 1,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = default_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = default_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = default_analog,
		} },
	},
	[EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900] = {
		.name         = "Hauppauge WinTV HVR 900",
		.tda9887_conf = TDA9887_PRESENT,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.mts_firmware = 1,
		.has_dvb      = 1,
		.dvb_gpio     = hauppauge_wintv_hvr_900_digital,
		.ir_codes     = ir_codes_hauppauge_new,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		} },
	},
	[EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900_R2] = {
		.name         = "Hauppauge WinTV HVR 900 (R2)",
		.tda9887_conf = TDA9887_PRESENT,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.mts_firmware = 1,
		.ir_codes     = ir_codes_hauppauge_new,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		} },
	},
	[EM2883_BOARD_HAUPPAUGE_WINTV_HVR_850] = {
		.name           = "Hauppauge WinTV HVR 850",
		.tuner_type     = TUNER_XC2028,
		.tuner_gpio     = default_tuner_gpio,
		.mts_firmware   = 1,
		.has_dvb        = 1,
		.dvb_gpio       = hauppauge_wintv_hvr_900_digital,
		.ir_codes       = ir_codes_hauppauge_new,
		.decoder        = EM28XX_TVP5150,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		} },
	},
	[EM2883_BOARD_HAUPPAUGE_WINTV_HVR_950] = {
		.name           = "Hauppauge WinTV HVR 950",
		.tuner_type     = TUNER_XC2028,
		.tuner_gpio     = default_tuner_gpio,
		.mts_firmware   = 1,
		.has_dvb        = 1,
		.dvb_gpio       = hauppauge_wintv_hvr_900_digital,
		.ir_codes       = ir_codes_hauppauge_new,
		.decoder        = EM28XX_TVP5150,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		} },
	},
	[EM2880_BOARD_PINNACLE_PCTV_HD_PRO] = {
		.name           = "Pinnacle PCTV HD Pro Stick",
		.tuner_type     = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.mts_firmware   = 1,
		.has_dvb        = 1,
		.dvb_gpio       = hauppauge_wintv_hvr_900_digital,
		.ir_codes       = ir_codes_pinnacle_pctv_hd,
		.decoder        = EM28XX_TVP5150,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		} },
	},
	[EM2880_BOARD_AMD_ATI_TV_WONDER_HD_600] = {
		.name           = "AMD ATI TV Wonder HD 600",
		.tuner_type     = TUNER_XC2028,
		.tuner_gpio     = default_tuner_gpio,
		.mts_firmware   = 1,
		.has_dvb        = 1,
		.dvb_gpio       = hauppauge_wintv_hvr_900_digital,
		.ir_codes       = ir_codes_ati_tv_wonder_hd_600,
		.decoder        = EM28XX_TVP5150,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		} },
	},
	[EM2880_BOARD_TERRATEC_HYBRID_XS] = {
		.name           = "Terratec Hybrid XS",
		.tuner_type     = TUNER_XC2028,
		.tuner_gpio     = default_tuner_gpio,
		.decoder        = EM28XX_TVP5150,
		.has_dvb        = 1,
		.dvb_gpio       = default_analog,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = default_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = default_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = default_analog,
		} },
	},
	/* maybe there's a reason behind it why Terratec sells the Hybrid XS
	   as Prodigy XS with a different PID, let's keep it separated for now
	   maybe we'll need it lateron */
	[EM2880_BOARD_TERRATEC_PRODIGY_XS] = {
		.name         = "Terratec Prodigy XS",
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		} },
	},
	[EM2820_BOARD_MSI_VOX_USB_2] = {
		.name		   = "MSI VOX USB 2.0",
		.tuner_type	   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf	   = TDA9887_PRESENT      |
				     TDA9887_PORT1_ACTIVE |
				     TDA9887_PORT2_ACTIVE,
		.max_range_640_480 = 1,
		.decoder           = EM28XX_SAA711X,
		.input             = { {
			.type      = EM28XX_VMUX_TELEVISION,
			.vmux      = SAA7115_COMPOSITE4,
			.amux      = EM28XX_AMUX_VIDEO,
		}, {
			.type      = EM28XX_VMUX_COMPOSITE1,
			.vmux      = SAA7115_COMPOSITE0,
			.amux      = EM28XX_AMUX_LINE_IN,
		}, {
			.type      = EM28XX_VMUX_SVIDEO,
			.vmux      = SAA7115_SVIDEO3,
			.amux      = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2800_BOARD_TERRATEC_CINERGY_200] = {
		.name         = "Terratec Cinergy 200 USB",
		.is_em2800    = 1,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = EM28XX_AMUX_VIDEO,
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
	[EM2800_BOARD_GRABBEEX_USB2800] = {
		.name       = "eMPIA Technology, Inc. GrabBeeX+ Video Encoder",
		.is_em2800  = 1,
		.decoder    = EM28XX_SAA711X,
		.tuner_type = TUNER_ABSENT, /* capture only board */
		.input      = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2800_BOARD_LEADTEK_WINFAST_USBII] = {
		.name         = "Leadtek Winfast USB II",
		.is_em2800    = 1,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = EM28XX_AMUX_VIDEO,
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
	[EM2800_BOARD_KWORLD_USB2800] = {
		.name         = "Kworld USB2800",
		.is_em2800    = 1,
		.tuner_type   = TUNER_PHILIPS_FCV1236D,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = EM28XX_AMUX_VIDEO,
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
	[EM2820_BOARD_PINNACLE_DVC_90] = {
		.name         = "Pinnacle Dazzle DVC 90/100/101/107 / Kaiser Baas Video to DVD maker",
		.tuner_type   = TUNER_ABSENT, /* capture only board */
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2800_BOARD_VGEAR_POCKETTV] = {
		.name         = "V-Gear PocketTV",
		.is_em2800    = 1,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = EM28XX_AMUX_VIDEO,
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
	[EM2820_BOARD_PROLINK_PLAYTV_BOX4_USB2] = {
		.name         = "Pixelview PlayTV Box 4 USB 2.0",
		.tda9887_conf = TDA9887_PRESENT,
		.tuner_type   = TUNER_YMEC_TVF_5533MF,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = EM28XX_AMUX_VIDEO,
			.aout     = EM28XX_AOUT_MONO | 	/* I2S */
				    EM28XX_AOUT_MASTER,	/* Line out pin */
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
	[EM2820_BOARD_PROLINK_PLAYTV_USB2] = {
		.name         = "SIIG AVTuner-PVR / Pixelview Prolink PlayTV USB 2.0",
		.has_snapshot_button = 1,
		.tda9887_conf = TDA9887_PRESENT,
		.tuner_type   = TUNER_YMEC_TVF_5533MF,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = EM28XX_AMUX_VIDEO,
			.aout     = EM28XX_AOUT_MONO | 	/* I2S */
				    EM28XX_AOUT_MASTER,	/* Line out pin */
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
	[EM2860_BOARD_SAA711X_REFERENCE_DESIGN] = {
		.name                = "EM2860/SAA711X Reference Design",
		.has_snapshot_button = 1,
		.tuner_type          = TUNER_ABSENT,
		.decoder             = EM28XX_SAA711X,
		.input               = { {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
		} },
	},
	[EM2880_BOARD_MSI_DIGIVOX_AD] = {
		.name         = "MSI DigiVox A/D",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = em2880_msi_digivox_ad_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = em2880_msi_digivox_ad_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = em2880_msi_digivox_ad_analog,
		} },
	},
	[EM2880_BOARD_MSI_DIGIVOX_AD_II] = {
		.name         = "MSI DigiVox A/D II",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = em2880_msi_digivox_ad_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = em2880_msi_digivox_ad_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = em2880_msi_digivox_ad_analog,
		} },
	},
	[EM2880_BOARD_KWORLD_DVB_305U] = {
		.name	      = "KWorld DVB-T 305U",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2880_BOARD_KWORLD_DVB_310U] = {
		.name	      = "KWorld DVB-T 310U",
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.has_dvb      = 1,
		.dvb_gpio     = default_digital,
		.mts_firmware = 1,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = default_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = default_analog,
		}, {	/* S-video has not been tested yet */
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = default_analog,
		} },
	},
	[EM2882_BOARD_KWORLD_ATSC_315U] = {
		.name		= "KWorld ATSC 315U HDTV TV Box",
		.valid		= EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type	= TUNER_THOMSON_DTT761X,
		.tuner_gpio	= em2882_kworld_315u_tuner_gpio,
		.tda9887_conf	= TDA9887_PRESENT,
		.decoder	= EM28XX_SAA711X,
		.has_dvb	= 1,
		.dvb_gpio	= em2882_kworld_315u_digital,
		.xclk		= EM28XX_XCLK_FREQUENCY_12MHZ,
		.i2c_speed	= EM28XX_I2C_CLK_WAIT_ENABLE,
		/* Analog mode - still not ready */
		/*.input        = { {
			.type = EM28XX_VMUX_TELEVISION,
			.vmux = SAA7115_COMPOSITE2,
			.amux = EM28XX_AMUX_VIDEO,
			.gpio = em2882_kworld_315u_analog,
			.aout = EM28XX_AOUT_PCM_IN | EM28XX_AOUT_PCM_STEREO,
		}, {
			.type = EM28XX_VMUX_COMPOSITE1,
			.vmux = SAA7115_COMPOSITE0,
			.amux = EM28XX_AMUX_LINE_IN,
			.gpio = em2882_kworld_315u_analog1,
			.aout = EM28XX_AOUT_PCM_IN | EM28XX_AOUT_PCM_STEREO,
		}, {
			.type = EM28XX_VMUX_SVIDEO,
			.vmux = SAA7115_SVIDEO3,
			.amux = EM28XX_AMUX_LINE_IN,
			.gpio = em2882_kworld_315u_analog1,
			.aout = EM28XX_AOUT_PCM_IN | EM28XX_AOUT_PCM_STEREO,
		} }, */
	},
	[EM2880_BOARD_EMPIRE_DUAL_TV] = {
		.name = "Empire dual TV",
		.tuner_type = TUNER_XC2028,
		.tuner_gpio = default_tuner_gpio,
		.has_dvb = 1,
		.dvb_gpio = default_digital,
		.mts_firmware = 1,
		.decoder = EM28XX_TVP5150,
		.input = { {
			.type = EM28XX_VMUX_TELEVISION,
			.vmux = TVP5150_COMPOSITE0,
			.amux = EM28XX_AMUX_VIDEO,
			.gpio = default_analog,
		}, {
			.type = EM28XX_VMUX_COMPOSITE1,
			.vmux = TVP5150_COMPOSITE1,
			.amux = EM28XX_AMUX_LINE_IN,
			.gpio = default_analog,
		}, {
			.type = EM28XX_VMUX_SVIDEO,
			.vmux = TVP5150_SVIDEO,
			.amux = EM28XX_AMUX_LINE_IN,
			.gpio = default_analog,
		} },
	},
	[EM2881_BOARD_DNT_DA2_HYBRID] = {
		.name         = "DNT DA2 Hybrid",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = default_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = default_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = default_analog,
		} },
	},
	[EM2881_BOARD_PINNACLE_HYBRID_PRO] = {
		.name         = "Pinnacle Hybrid Pro",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = default_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = default_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = default_analog,
		} },
	},
	[EM2882_BOARD_PINNACLE_HYBRID_PRO] = {
		.name         = "Pinnacle Hybrid Pro (2)",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.mts_firmware = 1,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		} },
	},
	[EM2882_BOARD_KWORLD_VS_DVBT] = {
		.name         = "Kworld VS-DVB-T 323UR",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2882_BOARD_TERRATEC_HYBRID_XS] = {
		.name         = "Terratec Hybrid XS (em2882)",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = hauppauge_wintv_hvr_900_analog,
		} },
	},
	[EM2883_BOARD_KWORLD_HYBRID_330U] = {
		.name         = "Kworld PlusTV HD Hybrid 330",
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.mts_firmware = 1,
		.has_dvb      = 1,
		.dvb_gpio     = kworld_330u_digital,
		.xclk             = EM28XX_XCLK_FREQUENCY_12MHZ,
		.i2c_speed        = EM28XX_I2C_CLK_WAIT_ENABLE |
				    EM28XX_I2C_EEPROM_ON_BOARD |
				    EM28XX_I2C_EEPROM_KEY_VALID,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = kworld_330u_analog,
			.aout     = EM28XX_AOUT_PCM_IN | EM28XX_AOUT_PCM_STEREO,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = kworld_330u_analog,
			.aout     = EM28XX_AOUT_PCM_IN | EM28XX_AOUT_PCM_STEREO,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = kworld_330u_analog,
		} },
	},
	[EM2820_BOARD_COMPRO_VIDEOMATE_FORYOU] = {
		.name         = "Compro VideoMate ForYou/Stereo",
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tvaudio_addr = 0xb0,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_TVP5150,
		.adecoder     = EM28XX_TVAUDIO,
		.mute_gpio    = compro_mute_gpio,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = compro_unmute_tv_gpio,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = compro_unmute_svid_gpio,
		} },
	},
	[EM2860_BOARD_KAIOMY_TVNPC_U2] = {
		.name	      = "Kaiomy TVnPC U2",
		.vchannels    = 3,
		.tuner_type   = TUNER_XC2028,
		.tuner_addr   = 0x61,
		.mts_firmware = 1,
		.decoder      = EM28XX_TVP5150,
		.tuner_gpio   = default_tuner_gpio,
		.ir_codes     = ir_codes_kaiomy,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,

		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
		.radio		= {
			.type     = EM28XX_RADIO,
			.amux     = EM28XX_AMUX_LINE_IN,
		}
	},
	[EM2860_BOARD_EASYCAP] = {
		.name         = "Easy Cap Capture DC-60",
		.vchannels    = 2,
		.tuner_type   = TUNER_ABSENT,
		.decoder      = EM28XX_SAA711X,
		.input           = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2820_BOARD_IODATA_GVMVP_SZ] = {
		.name       = "IO-DATA GV-MVP/SZ",
		.tuner_type   = TUNER_PHILIPS_FM1236_MK3,
		.tuner_gpio   = default_tuner_gpio,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_TVP5150,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
		}, { /* Composite has not been tested yet */
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_VIDEO,
		}, { /* S-video has not been tested yet */
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_VIDEO,
		} },
	},
	[EM2860_BOARD_TERRATEC_GRABBY] = {
		.name            = "Terratec Grabby",
		.vchannels       = 2,
		.tuner_type      = TUNER_ABSENT,
		.decoder         = EM28XX_SAA711X,
		.xclk            = EM28XX_XCLK_FREQUENCY_12MHZ,
		.input           = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO2,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = EM28XX_AMUX_VIDEO2,
		} },
	},
	[EM2860_BOARD_TERRATEC_AV350] = {
		.name            = "Terratec AV350",
		.vchannels       = 2,
		.tuner_type      = TUNER_ABSENT,
		.decoder         = EM28XX_TVP5150,
		.xclk            = EM28XX_XCLK_FREQUENCY_12MHZ,
		.mute_gpio       = terratec_av350_mute_gpio,
		.input           = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AUDIO_SRC_LINE,
			.gpio     = terratec_av350_unmute_gpio,

		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AUDIO_SRC_LINE,
			.gpio     = terratec_av350_unmute_gpio,
		} },
	},
	[EM2882_BOARD_EVGA_INDTUBE] = {
		.name         = "Evga inDtube",
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.xclk         = EM28XX_XCLK_FREQUENCY_12MHZ, /* NEC IR */
		.mts_firmware = 1,
		.has_dvb      = 1,
		.dvb_gpio     = evga_indtube_digital,
		.ir_codes     = ir_codes_evga_indtube,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = evga_indtube_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = evga_indtube_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = evga_indtube_analog,
		} },
	},
};
const unsigned int em28xx_bcount = ARRAY_SIZE(em28xx_boards);

/* table of devices that work with this driver */
struct usb_device_id em28xx_id_table[] = {
	{ USB_DEVICE(0xeb1a, 0x2750),
			.driver_info = EM2750_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2751),
			.driver_info = EM2750_BOARD_UNKNOWN },
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
	{ USB_DEVICE(0xeb1a, 0xe300),
			.driver_info = EM2861_BOARD_KWORLD_PVRTV_300U },
	{ USB_DEVICE(0xeb1a, 0xe303),
			.driver_info = EM2860_BOARD_KAIOMY_TVNPC_U2 },
	{ USB_DEVICE(0xeb1a, 0xe305),
			.driver_info = EM2880_BOARD_KWORLD_DVB_305U },
	{ USB_DEVICE(0xeb1a, 0xe310),
			.driver_info = EM2880_BOARD_MSI_DIGIVOX_AD },
	{ USB_DEVICE(0xeb1a, 0xa313),
		.driver_info = EM2882_BOARD_KWORLD_ATSC_315U },
	{ USB_DEVICE(0xeb1a, 0xa316),
			.driver_info = EM2883_BOARD_KWORLD_HYBRID_330U },
	{ USB_DEVICE(0xeb1a, 0xe320),
			.driver_info = EM2880_BOARD_MSI_DIGIVOX_AD_II },
	{ USB_DEVICE(0xeb1a, 0xe323),
			.driver_info = EM2882_BOARD_KWORLD_VS_DVBT },
	{ USB_DEVICE(0xeb1a, 0xe350),
			.driver_info = EM2870_BOARD_KWORLD_350U },
	{ USB_DEVICE(0xeb1a, 0xe355),
			.driver_info = EM2870_BOARD_KWORLD_355U },
	{ USB_DEVICE(0xeb1a, 0x2801),
			.driver_info = EM2800_BOARD_GRABBEEX_USB2800 },
	{ USB_DEVICE(0xeb1a, 0xe357),
			.driver_info = EM2870_BOARD_KWORLD_355U },
	{ USB_DEVICE(0x1b80, 0xe302),
			.driver_info = EM2820_BOARD_PINNACLE_DVC_90 }, /* Kaiser Baas Video to DVD maker */
	{ USB_DEVICE(0x0ccd, 0x0036),
			.driver_info = EM2820_BOARD_TERRATEC_CINERGY_250 },
	{ USB_DEVICE(0x0ccd, 0x004c),
			.driver_info = EM2880_BOARD_TERRATEC_HYBRID_XS_FR },
	{ USB_DEVICE(0x0ccd, 0x004f),
			.driver_info = EM2860_BOARD_TERRATEC_HYBRID_XS },
	{ USB_DEVICE(0x0ccd, 0x005e),
			.driver_info = EM2882_BOARD_TERRATEC_HYBRID_XS },
	{ USB_DEVICE(0x0ccd, 0x0042),
			.driver_info = EM2880_BOARD_TERRATEC_HYBRID_XS },
	{ USB_DEVICE(0x0ccd, 0x0043),
			.driver_info = EM2870_BOARD_TERRATEC_XS },
	{ USB_DEVICE(0x0ccd, 0x0047),
			.driver_info = EM2880_BOARD_TERRATEC_PRODIGY_XS },
	{ USB_DEVICE(0x0ccd, 0x0084),
			.driver_info = EM2860_BOARD_TERRATEC_AV350 },
	{ USB_DEVICE(0x0ccd, 0x0096),
			.driver_info = EM2860_BOARD_TERRATEC_GRABBY },
	{ USB_DEVICE(0x185b, 0x2870),
			.driver_info = EM2870_BOARD_COMPRO_VIDEOMATE },
	{ USB_DEVICE(0x185b, 0x2041),
			.driver_info = EM2820_BOARD_COMPRO_VIDEOMATE_FORYOU },
	{ USB_DEVICE(0x2040, 0x4200),
			.driver_info = EM2820_BOARD_HAUPPAUGE_WINTV_USB_2 },
	{ USB_DEVICE(0x2040, 0x4201),
			.driver_info = EM2820_BOARD_HAUPPAUGE_WINTV_USB_2 },
	{ USB_DEVICE(0x2040, 0x6500),
			.driver_info = EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900 },
	{ USB_DEVICE(0x2040, 0x6502),
			.driver_info = EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900_R2 },
	{ USB_DEVICE(0x2040, 0x6513), /* HCW HVR-980 */
			.driver_info = EM2883_BOARD_HAUPPAUGE_WINTV_HVR_950 },
	{ USB_DEVICE(0x2040, 0x6517), /* HP  HVR-950 */
			.driver_info = EM2883_BOARD_HAUPPAUGE_WINTV_HVR_950 },
	{ USB_DEVICE(0x2040, 0x651b), /* RP  HVR-950 */
			.driver_info = EM2883_BOARD_HAUPPAUGE_WINTV_HVR_950 },
	{ USB_DEVICE(0x2040, 0x651f),
			.driver_info = EM2883_BOARD_HAUPPAUGE_WINTV_HVR_850 },
	{ USB_DEVICE(0x0438, 0xb002),
			.driver_info = EM2880_BOARD_AMD_ATI_TV_WONDER_HD_600 },
	{ USB_DEVICE(0x2001, 0xf112),
			.driver_info = EM2820_BOARD_DLINK_USB_TV },
	{ USB_DEVICE(0x2304, 0x0207),
			.driver_info = EM2820_BOARD_PINNACLE_DVC_90 },
	{ USB_DEVICE(0x2304, 0x0208),
			.driver_info = EM2820_BOARD_PINNACLE_USB_2 },
	{ USB_DEVICE(0x2304, 0x021a),
			.driver_info = EM2820_BOARD_PINNACLE_DVC_90 },
	{ USB_DEVICE(0x2304, 0x0226),
			.driver_info = EM2882_BOARD_PINNACLE_HYBRID_PRO },
	{ USB_DEVICE(0x2304, 0x0227),
			.driver_info = EM2880_BOARD_PINNACLE_PCTV_HD_PRO },
	{ USB_DEVICE(0x0413, 0x6023),
			.driver_info = EM2800_BOARD_LEADTEK_WINFAST_USBII },
	{ USB_DEVICE(0x093b, 0xa005),
			.driver_info = EM2861_BOARD_PLEXTOR_PX_TV100U },
	{ USB_DEVICE(0x04bb, 0x0515),
			.driver_info = EM2820_BOARD_IODATA_GVMVP_SZ },
	{ },
};
MODULE_DEVICE_TABLE(usb, em28xx_id_table);

/*
 * EEPROM hash table for devices with generic USB IDs
 */
static struct em28xx_hash_table em28xx_eeprom_hash[] = {
	/* P/N: SA 60002070465 Tuner: TVF7533-MF */
	{0x6ce05a8f, EM2820_BOARD_PROLINK_PLAYTV_USB2, TUNER_YMEC_TVF_5533MF},
	{0x72cc5a8b, EM2820_BOARD_PROLINK_PLAYTV_BOX4_USB2, TUNER_YMEC_TVF_5533MF},
	{0x966a0441, EM2880_BOARD_KWORLD_DVB_310U, TUNER_XC2028},
	{0x9567eb1a, EM2880_BOARD_EMPIRE_DUAL_TV, TUNER_XC2028},
	{0xcee44a99, EM2882_BOARD_EVGA_INDTUBE, TUNER_XC2028},
};

/* I2C devicelist hash table for devices with generic USB IDs */
static struct em28xx_hash_table em28xx_i2c_hash[] = {
	{0xb06a32c3, EM2800_BOARD_TERRATEC_CINERGY_200, TUNER_LG_PAL_NEW_TAPC},
	{0xf51200e3, EM2800_BOARD_VGEAR_POCKETTV, TUNER_LG_PAL_NEW_TAPC},
	{0x1ba50080, EM2860_BOARD_SAA711X_REFERENCE_DESIGN, TUNER_ABSENT},
	{0xc51200e3, EM2820_BOARD_GADMEI_TVR200, TUNER_LG_PAL_NEW_TAPC},
};

/* I2C possible address to saa7115, tvp5150, msp3400, tvaudio */
static unsigned short saa711x_addrs[] = {
	0x4a >> 1, 0x48 >> 1,   /* SAA7111, SAA7111A and SAA7113 */
	0x42 >> 1, 0x40 >> 1,   /* SAA7114, SAA7115 and SAA7118 */
	I2C_CLIENT_END };

static unsigned short tvp5150_addrs[] = {
	0xb8 >> 1,
	0xba >> 1,
	I2C_CLIENT_END
};

static unsigned short msp3400_addrs[] = {
	0x80 >> 1,
	0x88 >> 1,
	I2C_CLIENT_END
};

int em28xx_tuner_callback(void *ptr, int component, int command, int arg)
{
	int rc = 0;
	struct em28xx *dev = ptr;

	if (dev->tuner_type != TUNER_XC2028)
		return 0;

	if (command != XC2028_TUNER_RESET)
		return 0;

	rc = em28xx_gpio_set(dev, dev->board.tuner_gpio);

	return rc;
}
EXPORT_SYMBOL_GPL(em28xx_tuner_callback);

static inline void em28xx_set_model(struct em28xx *dev)
{
	memcpy(&dev->board, &em28xx_boards[dev->model], sizeof(dev->board));

	/* Those are the default values for the majority of boards
	   Use those values if not specified otherwise at boards entry
	 */
	if (!dev->board.xclk)
		dev->board.xclk = EM28XX_XCLK_IR_RC5_MODE |
				  EM28XX_XCLK_FREQUENCY_12MHZ;

	if (!dev->board.i2c_speed)
		dev->board.i2c_speed = EM28XX_I2C_CLK_WAIT_ENABLE |
				       EM28XX_I2C_FREQ_100_KHZ;
}

/* Since em28xx_pre_card_setup() requires a proper dev->model,
 * this won't work for boards with generic PCI IDs
 */
void em28xx_pre_card_setup(struct em28xx *dev)
{
	int rc;

	em28xx_set_model(dev);

	em28xx_info("Identified as %s (card=%d)\n",
		    dev->board.name, dev->model);

	/* Set the default GPO/GPIO for legacy devices */
	dev->reg_gpo_num = EM2880_R04_GPO;
	dev->reg_gpio_num = EM28XX_R08_GPIO;

	dev->wait_after_write = 5;

	/* Based on the Chip ID, set the device configuration */
	rc = em28xx_read_reg(dev, EM28XX_R0A_CHIPID);
	if (rc > 0) {
		dev->chip_id = rc;

		switch (dev->chip_id) {
		case CHIP_ID_EM2750:
			em28xx_info("chip ID is em2750\n");
			break;
		case CHIP_ID_EM2820:
			em28xx_info("chip ID is em2820\n");
			break;
		case CHIP_ID_EM2840:
			em28xx_info("chip ID is em2840\n");
			break;
		case CHIP_ID_EM2860:
			em28xx_info("chip ID is em2860\n");
			break;
		case CHIP_ID_EM2870:
			em28xx_info("chip ID is em2870\n");
			dev->wait_after_write = 0;
			break;
		case CHIP_ID_EM2874:
			em28xx_info("chip ID is em2874\n");
			dev->reg_gpio_num = EM2874_R80_GPIO;
			dev->wait_after_write = 0;
			break;
		case CHIP_ID_EM2883:
			em28xx_info("chip ID is em2882/em2883\n");
			dev->wait_after_write = 0;
			break;
		default:
			em28xx_info("em28xx chip ID = %d\n", dev->chip_id);
		}
	}

	/* Prepopulate cached GPO register content */
	rc = em28xx_read_reg(dev, dev->reg_gpo_num);
	if (rc >= 0)
		dev->reg_gpo = rc;

	/* Set the initial XCLK and I2C clock values based on the board
	   definition */
	em28xx_write_reg(dev, EM28XX_R0F_XCLK, dev->board.xclk & 0x7f);
	em28xx_write_reg(dev, EM28XX_R06_I2C_CLK, dev->board.i2c_speed);
	msleep(50);

	/* request some modules */
	switch (dev->model) {
	case EM2861_BOARD_PLEXTOR_PX_TV100U:
		/* FIXME guess */
		/* Turn on analog audio output */
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xfd);
		break;
	case EM2861_BOARD_KWORLD_PVRTV_300U:
	case EM2880_BOARD_KWORLD_DVB_305U:
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0x6d);
		msleep(10);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0x7d);
		msleep(10);
		break;
	case EM2870_BOARD_COMPRO_VIDEOMATE:
		/* TODO: someone can do some cleanup here...
			 not everything's needed */
		em28xx_write_reg(dev, EM2880_R04_GPO, 0x00);
		msleep(10);
		em28xx_write_reg(dev, EM2880_R04_GPO, 0x01);
		msleep(10);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xfd);
		mdelay(70);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xfc);
		mdelay(70);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xdc);
		mdelay(70);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xfc);
		mdelay(70);
		break;
	case EM2870_BOARD_TERRATEC_XS_MT2060:
		/* this device needs some gpio writes to get the DVB-T
		   demod work */
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xfe);
		mdelay(70);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xde);
		mdelay(70);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xfe);
		mdelay(70);
		break;
	case EM2870_BOARD_PINNACLE_PCTV_DVB:
		/* this device needs some gpio writes to get the
		   DVB-T demod work */
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xfe);
		mdelay(70);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xde);
		mdelay(70);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xfe);
		mdelay(70);
		break;
	case EM2820_BOARD_GADMEI_UTV310:
	case EM2820_BOARD_MSI_VOX_USB_2:
		/* enables audio for that devices */
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xfd);
		break;

	case EM2882_BOARD_KWORLD_ATSC_315U:
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xff);
		msleep(10);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xfe);
		msleep(10);
		em28xx_write_reg(dev, EM2880_R04_GPO, 0x00);
		msleep(10);
		em28xx_write_reg(dev, EM2880_R04_GPO, 0x08);
		msleep(10);
		break;

	case EM2860_BOARD_KAIOMY_TVNPC_U2:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK, "\x07", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		em28xx_write_regs(dev, 0x0d, "\x42", 1);
		em28xx_write_regs(dev, 0x08, "\xfd", 1);
		msleep(10);
		em28xx_write_regs(dev, 0x08, "\xff", 1);
		msleep(10);
		em28xx_write_regs(dev, 0x08, "\x7f", 1);
		msleep(10);
		em28xx_write_regs(dev, 0x08, "\x6b", 1);

		break;
	case EM2860_BOARD_EASYCAP:
		em28xx_write_regs(dev, 0x08, "\xf8", 1);
		break;

	case EM2820_BOARD_IODATA_GVMVP_SZ:
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xff);
		msleep(70);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xf7);
		msleep(10);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xfe);
		msleep(70);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xfd);
		msleep(70);
		break;
	}

	em28xx_gpio_set(dev, dev->board.tuner_gpio);
	em28xx_set_mode(dev, EM28XX_ANALOG_MODE);

	/* Unlock device */
	em28xx_set_mode(dev, EM28XX_SUSPEND);
}

static void em28xx_setup_xc3028(struct em28xx *dev, struct xc2028_ctrl *ctl)
{
	memset(ctl, 0, sizeof(*ctl));

	ctl->fname   = XC2028_DEFAULT_FIRMWARE;
	ctl->max_len = 64;
	ctl->mts = em28xx_boards[dev->model].mts_firmware;

	switch (dev->model) {
	case EM2880_BOARD_EMPIRE_DUAL_TV:
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900:
		ctl->demod = XC3028_FE_ZARLINK456;
		break;
	case EM2880_BOARD_TERRATEC_HYBRID_XS:
		ctl->demod = XC3028_FE_ZARLINK456;
		break;
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900_R2:
		/* djh - Not sure which demod we need here */
		ctl->demod = XC3028_FE_DEFAULT;
		break;
	case EM2880_BOARD_AMD_ATI_TV_WONDER_HD_600:
		ctl->demod = XC3028_FE_DEFAULT;
		ctl->fname = XC3028L_DEFAULT_FIRMWARE;
		break;
	case EM2883_BOARD_HAUPPAUGE_WINTV_HVR_850:
	case EM2883_BOARD_HAUPPAUGE_WINTV_HVR_950:
	case EM2880_BOARD_PINNACLE_PCTV_HD_PRO:
		/* FIXME: Better to specify the needed IF */
		ctl->demod = XC3028_FE_DEFAULT;
		break;
	case EM2883_BOARD_KWORLD_HYBRID_330U:
		ctl->demod = XC3028_FE_CHINA;
		ctl->fname = XC2028_DEFAULT_FIRMWARE;
		break;
	case EM2882_BOARD_EVGA_INDTUBE:
		ctl->demod = XC3028_FE_CHINA;
		ctl->fname = XC3028L_DEFAULT_FIRMWARE;
		break;
	default:
		ctl->demod = XC3028_FE_OREN538;
	}
}

static void em28xx_tuner_setup(struct em28xx *dev)
{
	struct tuner_setup           tun_setup;
	struct v4l2_frequency        f;

	if (dev->tuner_type == TUNER_ABSENT)
		return;

	memset(&tun_setup, 0, sizeof(tun_setup));

	tun_setup.mode_mask = T_ANALOG_TV | T_RADIO;
	tun_setup.tuner_callback = em28xx_tuner_callback;

	if (dev->board.radio.type) {
		tun_setup.type = dev->board.radio.type;
		tun_setup.addr = dev->board.radio_addr;

		v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_type_addr, &tun_setup);
	}

	if ((dev->tuner_type != TUNER_ABSENT) && (dev->tuner_type)) {
		tun_setup.type   = dev->tuner_type;
		tun_setup.addr   = dev->tuner_addr;

		v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_type_addr, &tun_setup);
	}

	if (dev->tda9887_conf) {
		struct v4l2_priv_tun_config tda9887_cfg;

		tda9887_cfg.tuner = TUNER_TDA9887;
		tda9887_cfg.priv = &dev->tda9887_conf;

		v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_config, &tda9887_cfg);
	}

	if (dev->tuner_type == TUNER_XC2028) {
		struct v4l2_priv_tun_config  xc2028_cfg;
		struct xc2028_ctrl           ctl;

		memset(&xc2028_cfg, 0, sizeof(xc2028_cfg));
		memset(&ctl, 0, sizeof(ctl));

		em28xx_setup_xc3028(dev, &ctl);

		xc2028_cfg.tuner = TUNER_XC2028;
		xc2028_cfg.priv  = &ctl;

		v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_config, &xc2028_cfg);
	}

	/* configure tuner */
	f.tuner = 0;
	f.type = V4L2_TUNER_ANALOG_TV;
	f.frequency = 9076;     /* just a magic number */
	dev->ctl_freq = f.frequency;
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_frequency, &f);
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
				      " <linux-media@vger.kernel.org>\n");
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
				      " <linux-media@vger.kernel.org>\n");
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
	em28xx_errdev("\tV4L Mailing List <linux-media@vger.kernel.org>\n");
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

/* ----------------------------------------------------------------------- */
void em28xx_register_i2c_ir(struct em28xx *dev)
{
	struct i2c_board_info info;
	struct IR_i2c_init_data init_data;
	const unsigned short addr_list[] = {
		 0x30, 0x47, I2C_CLIENT_END
	};

	if (disable_ir)
		return;

	memset(&info, 0, sizeof(struct i2c_board_info));
	memset(&init_data, 0, sizeof(struct IR_i2c_init_data));
	strlcpy(info.type, "ir_video", I2C_NAME_SIZE);

	/* detect & configure */
	switch (dev->model) {
	case (EM2800_BOARD_UNKNOWN):
		break;
	case (EM2820_BOARD_UNKNOWN):
		break;
	case (EM2800_BOARD_TERRATEC_CINERGY_200):
	case (EM2820_BOARD_TERRATEC_CINERGY_250):
		init_data.ir_codes = ir_codes_em_terratec;
		init_data.get_key = em28xx_get_key_terratec;
		init_data.name = "i2c IR (EM28XX Terratec)";
		break;
	case (EM2820_BOARD_PINNACLE_USB_2):
		init_data.ir_codes = ir_codes_pinnacle_grey;
		init_data.get_key = em28xx_get_key_pinnacle_usb_grey;
		init_data.name = "i2c IR (EM28XX Pinnacle PCTV)";
		break;
	case (EM2820_BOARD_HAUPPAUGE_WINTV_USB_2):
		init_data.ir_codes = ir_codes_hauppauge_new;
		init_data.get_key = em28xx_get_key_em_haup;
		init_data.name = "i2c IR (EM2840 Hauppauge)";
		break;
	case (EM2820_BOARD_MSI_VOX_USB_2):
		break;
	case (EM2800_BOARD_LEADTEK_WINFAST_USBII):
		break;
	case (EM2800_BOARD_KWORLD_USB2800):
		break;
	case (EM2800_BOARD_GRABBEEX_USB2800):
		break;
	}

	if (init_data.name)
		info.platform_data = &init_data;
	i2c_new_probed_device(&dev->i2c_adap, &info, addr_list);
}

void em28xx_card_setup(struct em28xx *dev)
{
	em28xx_set_model(dev);

	dev->tuner_type = em28xx_boards[dev->model].tuner_type;
	if (em28xx_boards[dev->model].tuner_addr)
		dev->tuner_addr = em28xx_boards[dev->model].tuner_addr;

	if (em28xx_boards[dev->model].tda9887_conf)
		dev->tda9887_conf = em28xx_boards[dev->model].tda9887_conf;

	/* request some modules */
	switch (dev->model) {
	case EM2820_BOARD_HAUPPAUGE_WINTV_USB_2:
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900:
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900_R2:
	case EM2883_BOARD_HAUPPAUGE_WINTV_HVR_850:
	case EM2883_BOARD_HAUPPAUGE_WINTV_HVR_950:
	{
		struct tveeprom tv;
#ifdef CONFIG_MODULES
		request_module("tveeprom");
#endif
		/* Call first TVeeprom */

		dev->i2c_client.addr = 0xa0 >> 1;
		tveeprom_hauppauge_analog(&dev->i2c_client, &tv, dev->eedata);

		dev->tuner_type = tv.tuner_type;

		if (tv.audio_processor == V4L2_IDENT_MSPX4XX) {
			dev->i2s_speed = 2048000;
			dev->board.has_msp34xx = 1;
		}
#ifdef CONFIG_MODULES
		if (tv.has_ir)
			request_module("ir-kbd-i2c");
#endif
		break;
	}
	case EM2882_BOARD_KWORLD_ATSC_315U:
		em28xx_write_reg(dev, 0x0d, 0x42);
		msleep(10);
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xfd);
		msleep(10);
		break;
	case EM2820_BOARD_KWORLD_PVRTV2800RF:
		/* GPIO enables sound on KWORLD PVR TV 2800RF */
		em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xf9);
		break;
	case EM2820_BOARD_UNKNOWN:
	case EM2800_BOARD_UNKNOWN:
		/*
		 * The K-WORLD DVB-T 310U is detected as an MSI Digivox AD.
		 *
		 * This occurs because they share identical USB vendor and
		 * product IDs.
		 *
		 * What we do here is look up the EEPROM hash of the K-WORLD
		 * and if it is found then we decide that we do not have
		 * a DIGIVOX and reset the device to the K-WORLD instead.
		 *
		 * This solution is only valid if they do not share eeprom
		 * hash identities which has not been determined as yet.
		 */
	case EM2880_BOARD_MSI_DIGIVOX_AD:
		if (!em28xx_hint_board(dev))
			em28xx_set_model(dev);

		/* In cases where we had to use a board hint, the call to
		   em28xx_set_mode() in em28xx_pre_card_setup() was a no-op,
		   so make the call now so the analog GPIOs are set properly
		   before probing the i2c bus. */
		em28xx_set_mode(dev, EM28XX_ANALOG_MODE);
		break;
	}

	if (dev->board.has_snapshot_button)
		em28xx_register_snapshot_button(dev);

	if (dev->board.valid == EM28XX_BOARD_NOT_VALIDATED) {
		em28xx_errdev("\n\n");
		em28xx_errdev("The support for this board weren't "
			      "valid yet.\n");
		em28xx_errdev("Please send a report of having this working\n");
		em28xx_errdev("not to V4L mailing list (and/or to other "
				"addresses)\n\n");
	}

	/* Allow override tuner type by a module parameter */
	if (tuner >= 0)
		dev->tuner_type = tuner;

	/* request some modules */
	if (dev->board.has_msp34xx)
		v4l2_i2c_new_probed_subdev(&dev->v4l2_dev, &dev->i2c_adap,
			"msp3400", "msp3400", msp3400_addrs);

	if (dev->board.decoder == EM28XX_SAA711X)
		v4l2_i2c_new_probed_subdev(&dev->v4l2_dev, &dev->i2c_adap,
			"saa7115", "saa7115_auto", saa711x_addrs);

	if (dev->board.decoder == EM28XX_TVP5150)
		v4l2_i2c_new_probed_subdev(&dev->v4l2_dev, &dev->i2c_adap,
			"tvp5150", "tvp5150", tvp5150_addrs);

	if (dev->board.adecoder == EM28XX_TVAUDIO)
		v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
			"tvaudio", "tvaudio", dev->board.tvaudio_addr);

	if (dev->board.tuner_type != TUNER_ABSENT) {
		int has_demod = (dev->tda9887_conf & TDA9887_PRESENT);

		if (dev->board.radio.type)
			v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
				"tuner", "tuner", dev->board.radio_addr);

		if (has_demod)
			v4l2_i2c_new_probed_subdev(&dev->v4l2_dev,
				&dev->i2c_adap, "tuner", "tuner",
				v4l2_i2c_tuner_addrs(ADDRS_DEMOD));
		if (dev->tuner_addr == 0) {
			enum v4l2_i2c_tuner_type type =
				has_demod ? ADDRS_TV_WITH_DEMOD : ADDRS_TV;
			struct v4l2_subdev *sd;

			sd = v4l2_i2c_new_probed_subdev(&dev->v4l2_dev,
				&dev->i2c_adap, "tuner", "tuner",
				v4l2_i2c_tuner_addrs(type));

			if (sd)
				dev->tuner_addr = v4l2_i2c_subdev_addr(sd);
		} else {
			v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
				"tuner", "tuner", dev->tuner_addr);
		}
	}

	em28xx_tuner_setup(dev);
	em28xx_ir_init(dev);
}


#if defined(CONFIG_MODULES) && defined(MODULE)
static void request_module_async(struct work_struct *work)
{
	struct em28xx *dev = container_of(work,
			     struct em28xx, request_module_wk);

	if (dev->has_audio_class)
		request_module("snd-usb-audio");
	else if (dev->has_alsa_audio)
		request_module("em28xx-alsa");

	if (dev->board.has_dvb)
		request_module("em28xx-dvb");
}

static void request_modules(struct em28xx *dev)
{
	INIT_WORK(&dev->request_module_wk, request_module_async);
	schedule_work(&dev->request_module_wk);
}
#else
#define request_modules(dev)
#endif /* CONFIG_MODULES */

/*
 * em28xx_realease_resources()
 * unregisters the v4l2,i2c and usb devices
 * called when the device gets disconected or at module unload
*/
void em28xx_release_resources(struct em28xx *dev)
{
	if (dev->sbutton_input_dev)
		em28xx_deregister_snapshot_button(dev);

	if (dev->ir)
		em28xx_ir_fini(dev);

	/*FIXME: I2C IR should be disconnected */

	em28xx_release_analog_resources(dev);

	em28xx_remove_from_devlist(dev);

	em28xx_i2c_unregister(dev);

	v4l2_device_unregister(&dev->v4l2_dev);

	usb_put_dev(dev->udev);

	/* Mark device as unused */
	em28xx_devused &= ~(1 << dev->devno);
};

/*
 * em28xx_init_dev()
 * allocates and inits the device structs, registers i2c bus and v4l device
 */
static int em28xx_init_dev(struct em28xx **devhandle, struct usb_device *udev,
			   struct usb_interface *interface,
			   int minor)
{
	struct em28xx *dev = *devhandle;
	int retval = -ENOMEM;
	int errCode;

	dev->udev = udev;
	mutex_init(&dev->ctrl_urb_lock);
	spin_lock_init(&dev->slock);
	init_waitqueue_head(&dev->open);
	init_waitqueue_head(&dev->wait_frame);
	init_waitqueue_head(&dev->wait_stream);

	dev->em28xx_write_regs = em28xx_write_regs;
	dev->em28xx_read_reg = em28xx_read_reg;
	dev->em28xx_read_reg_req_len = em28xx_read_reg_req_len;
	dev->em28xx_write_regs_req = em28xx_write_regs_req;
	dev->em28xx_read_reg_req = em28xx_read_reg_req;
	dev->board.is_em2800 = em28xx_boards[dev->model].is_em2800;

	em28xx_pre_card_setup(dev);

	if (!dev->board.is_em2800) {
		/* Sets I2C speed to 100 KHz */
		retval = em28xx_write_reg(dev, EM28XX_R06_I2C_CLK, 0x40);
		if (retval < 0) {
			em28xx_errdev("%s: em28xx_write_regs_req failed!"
				      " retval [%d]\n",
				      __func__, retval);
			return retval;
		}
	}

	retval = v4l2_device_register(&interface->dev, &dev->v4l2_dev);
	if (retval < 0) {
		em28xx_errdev("Call to v4l2_device_register() failed!\n");
		return retval;
	}

	/* register i2c bus */
	errCode = em28xx_i2c_register(dev);
	if (errCode < 0) {
		v4l2_device_unregister(&dev->v4l2_dev);
		em28xx_errdev("%s: em28xx_i2c_register - errCode [%d]!\n",
			__func__, errCode);
		return errCode;
	}

	/* Do board specific init and eeprom reading */
	em28xx_card_setup(dev);

	/* Configure audio */
	errCode = em28xx_audio_setup(dev);
	if (errCode < 0) {
		v4l2_device_unregister(&dev->v4l2_dev);
		em28xx_errdev("%s: Error while setting audio - errCode [%d]!\n",
			__func__, errCode);
	}

	/* wake i2c devices */
	em28xx_wake_i2c(dev);

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	INIT_LIST_HEAD(&dev->vidq.queued);


	if (dev->board.has_msp34xx) {
		/* Send a reset to other chips via gpio */
		errCode = em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xf7);
		if (errCode < 0) {
			em28xx_errdev("%s: em28xx_write_regs_req - "
				      "msp34xx(1) failed! errCode [%d]\n",
				      __func__, errCode);
			return errCode;
		}
		msleep(3);

		errCode = em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xff);
		if (errCode < 0) {
			em28xx_errdev("%s: em28xx_write_regs_req - "
				      "msp34xx(2) failed! errCode [%d]\n",
				      __func__, errCode);
			return errCode;
		}
		msleep(3);
	}

	em28xx_add_into_devlist(dev);

	retval = em28xx_register_analog_devices(dev);
	if (retval < 0) {
		em28xx_release_resources(dev);
		goto fail_reg_devices;
	}

	em28xx_init_extension(dev);

	/* Save some power by putting tuner to sleep */
	v4l2_device_call_all(&dev->v4l2_dev, 0, tuner, s_standby);

	return 0;

fail_reg_devices:
	return retval;
}

/*
 * em28xx_usb_probe()
 * checks for supported devices
 */
static int em28xx_usb_probe(struct usb_interface *interface,
			    const struct usb_device_id *id)
{
	const struct usb_endpoint_descriptor *endpoint;
	struct usb_device *udev;
	struct usb_interface *uif;
	struct em28xx *dev = NULL;
	int retval;
	int i, nr, ifnum, isoc_pipe;
	char *speed;
	char descr[255] = "";

	udev = usb_get_dev(interface_to_usbdev(interface));
	ifnum = interface->altsetting[0].desc.bInterfaceNumber;

	/* Check to see next free device and mark as used */
	nr = find_first_zero_bit(&em28xx_devused, EM28XX_MAXBOARDS);
	em28xx_devused |= 1<<nr;

	/* Don't register audio interfaces */
	if (interface->altsetting[0].desc.bInterfaceClass == USB_CLASS_AUDIO) {
		em28xx_err(DRIVER_NAME " audio device (%04x:%04x): "
			"interface %i, class %i\n",
			le16_to_cpu(udev->descriptor.idVendor),
			le16_to_cpu(udev->descriptor.idProduct),
			ifnum,
			interface->altsetting[0].desc.bInterfaceClass);

		em28xx_devused &= ~(1<<nr);
		retval = -ENODEV;
		goto err;
	}

	endpoint = &interface->cur_altsetting->endpoint[0].desc;

	/* check if the device has the iso in endpoint at the correct place */
	if (usb_endpoint_xfer_isoc(endpoint)
	    &&
	    (interface->altsetting[1].endpoint[0].desc.wMaxPacketSize == 940)) {
		/* It's a newer em2874/em2875 device */
		isoc_pipe = 0;
	} else {
		int check_interface = 1;
		isoc_pipe = 1;
		endpoint = &interface->cur_altsetting->endpoint[1].desc;
		if (!usb_endpoint_xfer_isoc(endpoint))
			check_interface = 0;

		if (usb_endpoint_dir_out(endpoint))
			check_interface = 0;

		if (!check_interface) {
			em28xx_err(DRIVER_NAME " video device (%04x:%04x): "
				"interface %i, class %i found.\n",
				le16_to_cpu(udev->descriptor.idVendor),
				le16_to_cpu(udev->descriptor.idProduct),
				ifnum,
				interface->altsetting[0].desc.bInterfaceClass);

			em28xx_err(DRIVER_NAME " This is an anciliary "
				"interface not used by the driver\n");

			em28xx_devused &= ~(1<<nr);
			retval = -ENODEV;
			goto err;
		}
	}

	switch (udev->speed) {
	case USB_SPEED_LOW:
		speed = "1.5";
		break;
	case USB_SPEED_UNKNOWN:
	case USB_SPEED_FULL:
		speed = "12";
		break;
	case USB_SPEED_HIGH:
		speed = "480";
		break;
	default:
		speed = "unknown";
	}

	if (udev->manufacturer)
		strlcpy(descr, udev->manufacturer, sizeof(descr));

	if (udev->product) {
		if (*descr)
			strlcat(descr, " ", sizeof(descr));
		strlcat(descr, udev->product, sizeof(descr));
	}
	if (*descr)
		strlcat(descr, " ", sizeof(descr));

	printk(DRIVER_NAME ": New device %s@ %s Mbps "
		"(%04x:%04x, interface %d, class %d)\n",
		descr,
		speed,
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct),
		ifnum,
		interface->altsetting->desc.bInterfaceNumber);

	/*
	 * Make sure we have 480 Mbps of bandwidth, otherwise things like
	 * video stream wouldn't likely work, since 12 Mbps is generally
	 * not enough even for most Digital TV streams.
	 */
	if (udev->speed != USB_SPEED_HIGH && disable_usb_speed_check == 0) {
		printk(DRIVER_NAME ": Device initialization failed.\n");
		printk(DRIVER_NAME ": Device must be connected to a high-speed"
		       " USB 2.0 port.\n");
		em28xx_devused &= ~(1<<nr);
		retval = -ENODEV;
		goto err;
	}

	if (nr >= EM28XX_MAXBOARDS) {
		printk(DRIVER_NAME ": Supports only %i em28xx boards.\n",
				EM28XX_MAXBOARDS);
		em28xx_devused &= ~(1<<nr);
		retval = -ENOMEM;
		goto err;
	}

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		em28xx_err(DRIVER_NAME ": out of memory!\n");
		em28xx_devused &= ~(1<<nr);
		retval = -ENOMEM;
		goto err;
	}

	snprintf(dev->name, 29, "em28xx #%d", nr);
	dev->devno = nr;
	dev->model = id->driver_info;
	dev->alt   = -1;

	/* Checks if audio is provided by some interface */
	for (i = 0; i < udev->config->desc.bNumInterfaces; i++) {
		uif = udev->config->interface[i];
		if (uif->altsetting[0].desc.bInterfaceClass == USB_CLASS_AUDIO) {
			dev->has_audio_class = 1;
			break;
		}
	}

	/* compute alternate max packet sizes */
	uif = udev->actconfig->interface[0];

	dev->num_alt = uif->num_altsetting;
	dev->alt_max_pkt_size = kmalloc(32 * dev->num_alt, GFP_KERNEL);

	if (dev->alt_max_pkt_size == NULL) {
		em28xx_errdev("out of memory!\n");
		em28xx_devused &= ~(1<<nr);
		kfree(dev);
		retval = -ENOMEM;
		goto err;
	}

	for (i = 0; i < dev->num_alt ; i++) {
		u16 tmp = le16_to_cpu(uif->altsetting[i].endpoint[isoc_pipe].desc.wMaxPacketSize);
		dev->alt_max_pkt_size[i] =
		    (tmp & 0x07ff) * (((tmp & 0x1800) >> 11) + 1);
	}

	if ((card[nr] >= 0) && (card[nr] < em28xx_bcount))
		dev->model = card[nr];

	/* allocate device struct */
	mutex_init(&dev->lock);
	mutex_lock(&dev->lock);
	retval = em28xx_init_dev(&dev, udev, interface, nr);
	if (retval) {
		em28xx_devused &= ~(1<<dev->devno);
		kfree(dev);
		goto err;
	}

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	request_modules(dev);

	/* Should be the last thing to do, to avoid newer udev's to
	   open the device before fully initializing it
	 */
	mutex_unlock(&dev->lock);

	return 0;

err:
	return retval;
}

/*
 * em28xx_usb_disconnect()
 * called when the device gets diconencted
 * video device will be unregistered on v4l2_close in case it is still open
 */
static void em28xx_usb_disconnect(struct usb_interface *interface)
{
	struct em28xx *dev;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	if (!dev)
		return;

	em28xx_info("disconnecting %s\n", dev->vdev->name);

	/* wait until all current v4l2 io is finished then deallocate
	   resources */
	mutex_lock(&dev->lock);

	wake_up_interruptible_all(&dev->open);

	v4l2_device_disconnect(&dev->v4l2_dev);

	if (dev->users) {
		em28xx_warn
		    ("device /dev/video%d is open! Deregistration and memory "
		     "deallocation are deferred on close.\n",
				dev->vdev->num);

		dev->state |= DEV_MISCONFIGURED;
		em28xx_uninit_isoc(dev);
		dev->state |= DEV_DISCONNECTED;
		wake_up_interruptible(&dev->wait_frame);
		wake_up_interruptible(&dev->wait_stream);
	} else {
		dev->state |= DEV_DISCONNECTED;
		em28xx_release_resources(dev);
	}

	em28xx_close_extension(dev);

	mutex_unlock(&dev->lock);

	if (!dev->users) {
		kfree(dev->alt_max_pkt_size);
		kfree(dev);
	}
}

static struct usb_driver em28xx_usb_driver = {
	.name = "em28xx",
	.probe = em28xx_usb_probe,
	.disconnect = em28xx_usb_disconnect,
	.id_table = em28xx_id_table,
};

static int __init em28xx_module_init(void)
{
	int result;

	/* register this driver with the USB subsystem */
	result = usb_register(&em28xx_usb_driver);
	if (result)
		em28xx_err(DRIVER_NAME
			   " usb_register failed. Error number %d.\n", result);

	printk(KERN_INFO DRIVER_NAME " driver loaded\n");

	return result;
}

static void __exit em28xx_module_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&em28xx_usb_driver);
}

module_init(em28xx_module_init);
module_exit(em28xx_module_exit);
