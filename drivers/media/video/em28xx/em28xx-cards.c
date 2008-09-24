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
#include <media/v4l2-common.h>
#include <media/v4l2-chip-ident.h>

#include "em28xx.h"

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

struct em28xx_board em28xx_boards[] = {
	[EM2750_BOARD_UNKNOWN] = {
		.name          = "Unknown EM2750/EM2751 webcam grabber",
		.vchannels     = 1,
		.input         = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = 0,
		} },
	},
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
	[EM2750_BOARD_DLCW_130] = {
		/* Beijing Huaqi Information Digital Technology Co., Ltd */
		.name          = "Huaqi DLCW-130",
		.valid         = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels     = 1,
		.input         = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = 0,
		} },
	},
	[EM2800_BOARD_KWORLD_USB2800] = {
		.name         = "Kworld USB2800",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.is_em2800    = 1,
		.vchannels    = 3,
		.tuner_type   = TUNER_PHILIPS_FCV1236D,
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
	[EM2820_BOARD_DLINK_USB_TV] = {
		.name         = "D-Link DUB-T210 TV Tuner",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
		.is_em2800    = 0,
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
	[EM2820_BOARD_HERCULES_SMART_TV_USB2] = {
		.name         = "Hercules Smart TV USB 2.0",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA7113,
		.input        = { {
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
	[EM2820_BOARD_PINNACLE_USB_2_FM1216ME] = {
		.name         = "Pinnacle PCTV USB 2 (Philips FM1216ME)",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
		.is_em2800    = 0,
		.tuner_type   = TUNER_PHILIPS_FM1216ME_MK3,
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
	[EM2820_BOARD_GADMEI_UTV310] = {
		.name         = "Gadmei UTV310",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
		.tuner_type   = TUNER_TNF_5335MF,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA7113,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE1,
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
	[EM2820_BOARD_LEADTEK_WINFAST_USBII_DELUXE] = {
		.name         = "Leadtek Winfast USB II Deluxe",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
		.tuner_type   = TUNER_PHILIPS_FM1216ME_MK3,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_SAA7114,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = 2,
			.amux     = 0,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = 9,
			.amux     = 1,
		} },
	},
	[EM2820_BOARD_PINNACLE_DVC_100] = {
		.name         = "Pinnacle Dazzle DVC 100",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
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
	[EM2820_BOARD_VIDEOLOGY_20K14XUSB] = {
		.name          = "Videology 20K14XUSB USB2.0",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels     = 1,
		.input         = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = 0,
		} },
	},
	[EM2821_BOARD_PROLINK_PLAYTV_USB2] = {
		.name         = "SIIG AVTuner-PVR/Prolink PlayTV USB 2.0",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
		.is_em2800    = 0,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,	/* unknown? */
		.tda9887_conf = TDA9887_PRESENT,	/* unknown? */
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
	[EM2821_BOARD_SUPERCOMP_USB_2] = {
		.name         = "Supercomp USB 2.0 TV",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
		.is_em2800    = 0,
		.tuner_type   = TUNER_PHILIPS_FM1236_MK3,
		.tda9887_conf = TDA9887_PRESENT |
				TDA9887_PORT1_ACTIVE |
				TDA9887_PORT2_ACTIVE,
		.decoder      = EM28XX_SAA7113,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = 1,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = 0,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = 1,
		} },
	},
	[EM2821_BOARD_USBGEAR_VD204] = {
		.name          = "Usbgear VD204v9",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels     = 2,
		.decoder       = EM28XX_SAA7113,
		.input          = { {
			.type  = EM28XX_VMUX_COMPOSITE1,
			.vmux  = SAA7115_COMPOSITE0,
			.amux  = 1,
		}, {
			.type  = EM28XX_VMUX_SVIDEO,
			.vmux  = SAA7115_SVIDEO3,
			.amux  = 1,
		} },
	},
	[EM2860_BOARD_NETGMBH_CAM] = {
		/* Beijing Huaqi Information Digital Technology Co., Ltd */
		.name          = "NetGMBH Cam",
		.valid       = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels     = 1,
		.input         = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = 0,
		} },
	},
	[EM2860_BOARD_TYPHOON_DVD_MAKER] = {
		.name          = "Typhoon DVD Maker",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels     = 2,
		.decoder       = EM28XX_SAA7113,
		.input          = { {
			.type  = EM28XX_VMUX_COMPOSITE1,
			.vmux  = SAA7115_COMPOSITE0,
			.amux  = 1,
		}, {
			.type  = EM28XX_VMUX_SVIDEO,
			.vmux  = SAA7115_SVIDEO3,
			.amux  = 1,
		} },
	},
	[EM2860_BOARD_GADMEI_UTV330] = {
		.name         = "Gadmei UTV330",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
		.tuner_type   = TUNER_TNF_5335MF,
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
	[EM2860_BOARD_TERRATEC_HYBRID_XS] = {
		.name         = "Terratec Cinergy A Hybrid XS",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
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
	[EM2861_BOARD_KWORLD_PVRTV_300U] = {
		.name	      = "KWorld PVRTV 300U",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
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
	[EM2861_BOARD_YAKUMO_MOVIE_MIXER] = {
		.name          = "Yakumo MovieMixer",
		.valid       = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels     = 1,
		.decoder       = EM28XX_TVP5150,
		.input         = { {
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
	[EM2861_BOARD_PLEXTOR_PX_TV100U] = {
		.name         = "Plextor ConvertX PX-TV100U",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
		.tuner_type   = TUNER_TNF_5335MF,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_TVP5150,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = 1,
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
	[EM2870_BOARD_TERRATEC_XS] = {
		.name         = "Terratec Cinergy T XS",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_XC2028,
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
	},
	[EM2870_BOARD_KWORLD_355U] = {
		.name         = "Kworld 355 U DVB-T",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
	},
	[EM2870_BOARD_PINNACLE_PCTV_DVB] = {
		.name         = "Pinnacle PCTV DVB-T",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_ABSENT, /* MT2060 */
	},
	[EM2870_BOARD_COMPRO_VIDEOMATE] = {
		.name         = "Compro, VideoMate U3",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_ABSENT, /* MT2060 */
	},
	[EM2880_BOARD_TERRATEC_HYBRID_XS_FR] = {
		.name         = "Terratec Hybrid XS Secam",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
		.has_msp34xx  = 1,
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
	[EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900] = {
		.name         = "Hauppauge WinTV HVR 900",
		.vchannels    = 3,
		.tda9887_conf = TDA9887_PRESENT,
		.tuner_type   = TUNER_XC2028,
		.mts_firmware = 1,
		.has_dvb        = 1,
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
	[EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900_R2] = {
		.name         = "Hauppauge WinTV HVR 900 (R2)",
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
	[EM2883_BOARD_HAUPPAUGE_WINTV_HVR_950] = {
		.name           = "Hauppauge WinTV HVR 950",
		.vchannels      = 3,
		.tda9887_conf   = TDA9887_PRESENT,
		.tuner_type     = TUNER_XC2028,
		.mts_firmware   = 1,
		.has_12mhz_i2s  = 1,
		.has_dvb        = 1,
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
	},
	[EM2880_BOARD_PINNACLE_PCTV_HD_PRO] = {
		.name           = "Pinnacle PCTV HD Pro Stick",
		.vchannels      = 3,
		.tda9887_conf   = TDA9887_PRESENT,
		.tuner_type     = TUNER_XC2028,
		.mts_firmware   = 1,
		.has_12mhz_i2s  = 1,
		.has_dvb        = 1,
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
	},
	[EM2880_BOARD_AMD_ATI_TV_WONDER_HD_600] = {
		.name           = "AMD ATI TV Wonder HD 600",
		.vchannels      = 3,
		.tda9887_conf   = TDA9887_PRESENT,
		.tuner_type     = TUNER_XC2028,
		.mts_firmware   = 1,
		.has_12mhz_i2s  = 1,
		.has_dvb        = 1,
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
	},
	[EM2880_BOARD_TERRATEC_HYBRID_XS] = {
		.name         = "Terratec Hybrid XS",
		.vchannels    = 3,
		.tda9887_conf = TDA9887_PRESENT,
		.tuner_type   = TUNER_XC2028,
		.decoder      = EM28XX_TVP5150,
		.has_dvb        = 1,
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
	[EM2800_BOARD_GRABBEEX_USB2800] = {
		.name         = "eMPIA Technology, Inc. GrabBeeX+ Video Encoder",
		.is_em2800    = 1,
		.vchannels    = 2,
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
		.tuner_type   = TUNER_PHILIPS_FCV1236D,
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
	[EM2860_BOARD_POINTNIX_INTRAORAL_CAMERA] = {
		.name         = "PointNix Intra-Oral Camera",
		.has_snapshot_button = 1,
		.vchannels    = 1,
		.tda9887_conf = TDA9887_PRESENT,
		.tuner_type   = TUNER_ABSENT,
		.decoder      = EM28XX_SAA7113,
		.input          = { {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = 0,
		} },
	},
	[EM2880_BOARD_MSI_DIGIVOX_AD] = {
		.name         = "MSI DigiVox A/D",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
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
	[EM2880_BOARD_MSI_DIGIVOX_AD_II] = {
		.name         = "MSI DigiVox A/D II",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
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
	[EM2880_BOARD_KWORLD_DVB_305U] = {
		.name	      = "KWorld DVB-T 305U",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
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
	[EM2880_BOARD_KWORLD_DVB_310U] = {
		.name	      = "KWorld DVB-T 310U",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
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
	[EM2881_BOARD_DNT_DA2_HYBRID] = {
		.name         = "DNT DA2 Hybrid",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
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
	[EM2881_BOARD_PINNACLE_HYBRID_PRO] = {
		.name         = "Pinnacle Hybrid Pro",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
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
	[EM2882_BOARD_PINNACLE_HYBRID_PRO] = {
		.name         = "Pinnacle Hybrid Pro (2)",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
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
	[EM2882_BOARD_KWORLD_VS_DVBT] = {
		.name         = "Kworld VS-DVB-T 323UR",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
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
	[EM2882_BOARD_TERRATEC_HYBRID_XS] = {
		.name         = "Terratec Hybrid XS (em2882)",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
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
	[EM2883_BOARD_KWORLD_HYBRID_A316] = {
		.name         = "Kworld PlusTV HD Hybrid 330",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.vchannels    = 3,
		.is_em2800    = 0,
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
	[EM2820_BOARD_COMPRO_VIDEOMATE_FORYOU] = {
		.name         = "Compro VideoMate ForYou/Stereo",
		.vchannels    = 2,
		.tuner_type   = TUNER_LG_PAL_NEW_TAPC,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_TVP5150,
		.input          = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
};
const unsigned int em28xx_bcount = ARRAY_SIZE(em28xx_boards);

/* table of devices that work with this driver */
struct usb_device_id em28xx_id_table [] = {
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
	{ USB_DEVICE(0xeb1a, 0xe305),
			.driver_info = EM2880_BOARD_KWORLD_DVB_305U },
	{ USB_DEVICE(0xeb1a, 0xe310),
			.driver_info = EM2880_BOARD_MSI_DIGIVOX_AD },
	{ USB_DEVICE(0xeb1a, 0xa316),
			.driver_info = EM2883_BOARD_KWORLD_HYBRID_A316 },
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
	{ USB_DEVICE(0x2040, 0x651f), /* HCW HVR-850 */
			.driver_info = EM2883_BOARD_HAUPPAUGE_WINTV_HVR_950 },
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
	{ },
};
MODULE_DEVICE_TABLE(usb, em28xx_id_table);

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
static struct em28xx_reg_seq em2880_msi_digivox_ad_digital[] = {
	{EM28XX_R08_GPIO,	0x6a,	~EM_GPIO_4,	10},
	{	-1,		-1,	-1,		-1},
};

/* Board  - EM2870 Kworld 355u
   Analog - No input analog */
static struct em28xx_reg_seq em2870_kworld_355u_digital[] = {
	{EM2880_R04_GPO,	0x01,	0xff,		10},
	{  -1,			-1,	-1,		-1},
};

/* Callback for the most boards */
static struct em28xx_reg_seq default_callback[] = {
	{EM28XX_R08_GPIO,	EM_GPIO_4,	EM_GPIO_4,	10},
	{EM28XX_R08_GPIO,	0,		EM_GPIO_4,	10},
	{EM28XX_R08_GPIO,	EM_GPIO_4,	EM_GPIO_4,	10},
	{  -1,			-1,		-1,		-1},
};

/* Callback for EM2882 TERRATEC HYBRID XS */
static struct em28xx_reg_seq em2882_terratec_hybrid_xs_digital[] = {
	{EM28XX_R08_GPIO,       0x2e,   0xff,		   6},
	{EM28XX_R08_GPIO,       0x3e,   ~EM_GPIO_4,	   6},
	{EM2880_R04_GPO,        0x04,   0xff,		  10},
	{EM2880_R04_GPO,        0x0c,   0xff,		  10},
	{  -1,			-1,	-1,		  -1},
};

/*
 * EEPROM hash table for devices with generic USB IDs
 */
static struct em28xx_hash_table em28xx_eeprom_hash [] = {
	/* P/N: SA 60002070465 Tuner: TVF7533-MF */
	{0x6ce05a8f, EM2820_BOARD_PROLINK_PLAYTV_USB2, TUNER_YMEC_TVF_5533MF},
};

/* I2C devicelist hash table for devices with generic USB IDs */
static struct em28xx_hash_table em28xx_i2c_hash[] = {
	{0xb06a32c3, EM2800_BOARD_TERRATEC_CINERGY_200, TUNER_LG_PAL_NEW_TAPC},
	{0xf51200e3, EM2800_BOARD_VGEAR_POCKETTV, TUNER_LG_PAL_NEW_TAPC},
	{0x1ba50080, EM2860_BOARD_POINTNIX_INTRAORAL_CAMERA, TUNER_ABSENT},
};

int em28xx_tuner_callback(void *ptr, int command, int arg)
{
	int rc = 0;
	struct em28xx *dev = ptr;

	if (dev->tuner_type != TUNER_XC2028)
		return 0;

	if (command != XC2028_TUNER_RESET)
		return 0;

	if (dev->mode == EM28XX_ANALOG_MODE)
		rc = em28xx_gpio_set(dev, dev->tun_analog_gpio);
	else
		rc = em28xx_gpio_set(dev, dev->tun_digital_gpio);

	return rc;
}
EXPORT_SYMBOL_GPL(em28xx_tuner_callback);

static void em28xx_set_model(struct em28xx *dev)
{
	dev->is_em2800 = em28xx_boards[dev->model].is_em2800;
	dev->has_msp34xx = em28xx_boards[dev->model].has_msp34xx;
	dev->tda9887_conf = em28xx_boards[dev->model].tda9887_conf;
	dev->decoder = em28xx_boards[dev->model].decoder;
	dev->video_inputs = em28xx_boards[dev->model].vchannels;
	dev->has_12mhz_i2s = em28xx_boards[dev->model].has_12mhz_i2s;
	dev->max_range_640_480 = em28xx_boards[dev->model].max_range_640_480;
	dev->has_dvb = em28xx_boards[dev->model].has_dvb;
	dev->has_snapshot_button = em28xx_boards[dev->model].has_snapshot_button;
	dev->valid = em28xx_boards[dev->model].valid;
}

/* Since em28xx_pre_card_setup() requires a proper dev->model,
 * this won't work for boards with generic PCI IDs
 */
void em28xx_pre_card_setup(struct em28xx *dev)
{
	int rc;

	rc = em28xx_read_reg(dev, EM2880_R04_GPO);
	if (rc >= 0)
		dev->reg_gpo = rc;

	dev->wait_after_write = 5;
	rc = em28xx_read_reg(dev, EM28XX_R0A_CHIPID);
	if (rc > 0) {
		switch (rc) {
		case CHIP_ID_EM2860:
			em28xx_info("chip ID is em2860\n");
			break;
		case CHIP_ID_EM2883:
			em28xx_info("chip ID is em2882/em2883\n");
			dev->wait_after_write = 0;
			break;
		default:
			em28xx_info("em28xx chip ID = %d\n", rc);
		}
	}
	em28xx_set_model(dev);

	/* request some modules */
	switch (dev->model) {
	case EM2880_BOARD_TERRATEC_PRODIGY_XS:
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900:
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900_R2:
	case EM2860_BOARD_TERRATEC_HYBRID_XS:
	case EM2883_BOARD_HAUPPAUGE_WINTV_HVR_950:
	case EM2880_BOARD_PINNACLE_PCTV_HD_PRO:
	case EM2882_BOARD_PINNACLE_HYBRID_PRO:
	case EM2883_BOARD_KWORLD_HYBRID_A316:
	case EM2880_BOARD_AMD_ATI_TV_WONDER_HD_600:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK,    "\x27", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		msleep(50);

		/* Sets GPO/GPIO sequences for this device */
		dev->analog_gpio      = hauppauge_wintv_hvr_900_analog;
		dev->digital_gpio     = hauppauge_wintv_hvr_900_digital;
		dev->tun_analog_gpio  = default_callback;
		dev->tun_digital_gpio = default_callback;
		break;

	case EM2882_BOARD_TERRATEC_HYBRID_XS:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK,    "\x27", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		msleep(50);

		/* should be added ir_codes here */

		/* Sets GPO/GPIO sequences for this device */
		dev->analog_gpio      = hauppauge_wintv_hvr_900_analog;
		dev->digital_gpio     = hauppauge_wintv_hvr_900_digital;
		dev->tun_analog_gpio  = default_callback;
		dev->tun_digital_gpio = em2882_terratec_hybrid_xs_digital;
		break;

	case EM2880_BOARD_TERRATEC_HYBRID_XS_FR:
	case EM2880_BOARD_TERRATEC_HYBRID_XS:
	case EM2870_BOARD_TERRATEC_XS:
	case EM2881_BOARD_PINNACLE_HYBRID_PRO:
	case EM2880_BOARD_KWORLD_DVB_310U:
	case EM2870_BOARD_KWORLD_350U:
	case EM2881_BOARD_DNT_DA2_HYBRID:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK,    "\x27", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		msleep(50);

		/* NOTE: EM2881_DNT_DA2_HYBRID spend 140 msleep for digital
			 and analog commands. If this commands doesn't work,
			 add this timer. */

		/* Sets GPO/GPIO sequences for this device */
		dev->analog_gpio      = default_analog;
		dev->digital_gpio     = default_digital;
		dev->tun_analog_gpio  = default_callback;
		dev->tun_digital_gpio = default_callback;
		break;

	case EM2880_BOARD_MSI_DIGIVOX_AD:
	case EM2880_BOARD_MSI_DIGIVOX_AD_II:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK,    "\x27", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		msleep(50);

		/* Sets GPO/GPIO sequences for this device */
		dev->analog_gpio      = em2880_msi_digivox_ad_analog;
		dev->digital_gpio     = em2880_msi_digivox_ad_digital;
		dev->tun_analog_gpio  = default_callback;
		dev->tun_digital_gpio = default_callback;
		break;

	case EM2750_BOARD_UNKNOWN:
	case EM2750_BOARD_DLCW_130:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK, "\x0a", 1);
		break;

	case EM2861_BOARD_PLEXTOR_PX_TV100U:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK, "\x27", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		/* FIXME guess */
		/* Turn on analog audio output */
		em28xx_write_regs_req(dev, 0x00, 0x08, "\xfd", 1);
		break;

	case EM2861_BOARD_KWORLD_PVRTV_300U:
	case EM2880_BOARD_KWORLD_DVB_305U:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK, "\x27", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x4c", 1);
		msleep(10);
		em28xx_write_regs(dev, 0x08, "\x6d", 1);
		msleep(10);
		em28xx_write_regs(dev, 0x08, "\x7d", 1);
		msleep(10);
		break;

	case EM2870_BOARD_KWORLD_355U:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK, "\x27", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		msleep(50);

		/* Sets GPO/GPIO sequences for this device */
		dev->digital_gpio     = em2870_kworld_355u_digital;
		break;

	case EM2870_BOARD_COMPRO_VIDEOMATE:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK, "\x27", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		/* TODO: someone can do some cleanup here...
			 not everything's needed */
		em28xx_write_regs(dev, 0x04, "\x00", 1);
		msleep(10);
		em28xx_write_regs(dev, 0x04, "\x01", 1);
		msleep(10);
		em28xx_write_regs(dev, 0x08, "\xfd", 1);
		mdelay(70);
		em28xx_write_regs(dev, 0x08, "\xfc", 1);
		mdelay(70);
		em28xx_write_regs(dev, 0x08, "\xdc", 1);
		mdelay(70);
		em28xx_write_regs(dev, 0x08, "\xfc", 1);
		mdelay(70);
		break;

	case EM2870_BOARD_TERRATEC_XS_MT2060:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK, "\x27", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		/* this device needs some gpio writes to get the DVB-T
		   demod work */
		em28xx_write_regs(dev, 0x08, "\xfe", 1);
		mdelay(70);
		em28xx_write_regs(dev, 0x08, "\xde", 1);
		mdelay(70);
		dev->em28xx_write_regs(dev, 0x08, "\xfe", 1);
		mdelay(70);
		break;

	case EM2870_BOARD_PINNACLE_PCTV_DVB:
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		/* this device needs some gpio writes to get the
		   DVB-T demod work */
		em28xx_write_regs(dev, 0x08, "\xfe", 1);
		mdelay(70);
		em28xx_write_regs(dev, 0x08, "\xde", 1);
		mdelay(70);
		em28xx_write_regs(dev, 0x08, "\xfe", 1);
		mdelay(70);
		/* switch em2880 rc protocol */
		em28xx_write_regs(dev, EM28XX_R0F_XCLK, "\x22", 1);
		/* should be added ir_codes here */
		break;

	case EM2820_BOARD_GADMEI_UTV310:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK, "\x27", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		/* Turn on analog audio output */
		em28xx_write_regs_req(dev, 0x00, 0x08, "\xfd", 1);
		break;

	case EM2860_BOARD_GADMEI_UTV330:
		/* Turn on IR */
		em28xx_write_regs(dev, EM28XX_R0F_XCLK, "\x07", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		/* should be added ir_codes here */
		break;

	case EM2820_BOARD_MSI_VOX_USB_2:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK, "\x27", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		/* enables audio for that device */
		em28xx_write_regs_req(dev, 0x00, 0x08, "\xfd", 1);
		break;
	}

	em28xx_gpio_set(dev, dev->tun_analog_gpio);
	em28xx_set_mode(dev, EM28XX_ANALOG_MODE);

	/* Unlock device */
	em28xx_set_mode(dev, EM28XX_MODE_UNDEFINED);
}

static void em28xx_setup_xc3028(struct em28xx *dev, struct xc2028_ctrl *ctl)
{
	memset(ctl, 0, sizeof(*ctl));

	ctl->fname   = XC2028_DEFAULT_FIRMWARE;
	ctl->max_len = 64;
	ctl->mts = em28xx_boards[dev->model].mts_firmware;

	switch (dev->model) {
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
	case EM2883_BOARD_HAUPPAUGE_WINTV_HVR_950:
	case EM2880_BOARD_PINNACLE_PCTV_HD_PRO:
	case EM2880_BOARD_AMD_ATI_TV_WONDER_HD_600:
		/* FIXME: Better to specify the needed IF */
		ctl->demod = XC3028_FE_DEFAULT;
		break;
	default:
		ctl->demod = XC3028_FE_OREN538;
	}
}

static void em28xx_config_tuner(struct em28xx *dev)
{
	struct v4l2_priv_tun_config  xc2028_cfg;
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
		struct xc2028_ctrl           ctl;

		em28xx_setup_xc3028(dev, &ctl);

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
	case (EM2800_BOARD_GRABBEEX_USB2800):
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
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900_R2:
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
		break;
	}

	if (dev->has_snapshot_button)
		em28xx_register_snapshot_button(dev);

	if (dev->valid == EM28XX_BOARD_NOT_VALIDATED) {
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
