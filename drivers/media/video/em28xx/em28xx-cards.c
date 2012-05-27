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
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/usb.h>
#include <media/tuner.h>
#include <media/msp3400.h>
#include <media/saa7115.h>
#include <media/tvp5150.h>
#include <media/tvaudio.h>
#include <media/mt9v011.h>
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

/* Bitmask marking allocated devices from 0 to EM28XX_MAXBOARDS - 1 */
static unsigned long em28xx_devused;

struct em28xx_hash_table {
	unsigned long hash;
	unsigned int  model;
	unsigned int  tuner;
};

static void em28xx_pre_card_setup(struct em28xx *dev);

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

/* Board Hauppauge WinTV HVR 900 (R2) digital */
static struct em28xx_reg_seq hauppauge_wintv_hvr_900R2_digital[] = {
	{EM28XX_R08_GPIO,	0x2e,	~EM_GPIO_4,	10},
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

/*
 * KWorld PlusTV 340U and UB435-Q (ATSC) GPIOs map:
 * EM_GPIO_0 - currently unknown
 * EM_GPIO_1 - LED disable/enable (1 = off, 0 = on)
 * EM_GPIO_2 - currently unknown
 * EM_GPIO_3 - currently unknown
 * EM_GPIO_4 - TDA18271HD/C1 tuner (1 = active, 0 = in reset)
 * EM_GPIO_5 - LGDT3304 ATSC/QAM demod (1 = active, 0 = in reset)
 * EM_GPIO_6 - currently unknown
 * EM_GPIO_7 - currently unknown
 */
static struct em28xx_reg_seq kworld_a340_digital[] = {
	{EM28XX_R08_GPIO,	0x6d,		~EM_GPIO_4,	10},
	{ -1,			-1,		-1,		-1},
};

/* Pinnacle Hybrid Pro eb1a:2881 */
static struct em28xx_reg_seq pinnacle_hybrid_pro_analog[] = {
	{EM28XX_R08_GPIO,	0xfd,   ~EM_GPIO_4,	10},
	{	-1,		-1,	-1,		-1},
};

static struct em28xx_reg_seq pinnacle_hybrid_pro_digital[] = {
	{EM28XX_R08_GPIO,	0x6e,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x04,	0xff,	       100},/* zl10353 reset */
	{EM2880_R04_GPO,	0x0c,	0xff,		 1},
	{	-1,		-1,	-1,		-1},
};

static struct em28xx_reg_seq terratec_cinergy_USB_XS_FR_analog[] = {
	{EM28XX_R08_GPIO,	0x6d,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x00,	0xff,		10},
	{ -1,			-1,	-1,		-1},
};

static struct em28xx_reg_seq terratec_cinergy_USB_XS_FR_digital[] = {
	{EM28XX_R08_GPIO,	0x6e,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x08,	0xff,		10},
	{ -1,			-1,	-1,		-1},
};

/* eb1a:2868 Reddo DVB-C USB TV Box
   GPIO4 - CU1216L NIM
   Other GPIOs seems to be don't care. */
static struct em28xx_reg_seq reddo_dvb_c_usb_box[] = {
	{EM28XX_R08_GPIO,	0xfe,	0xff,		10},
	{EM28XX_R08_GPIO,	0xde,	0xff,		10},
	{EM28XX_R08_GPIO,	0xfe,	0xff,		10},
	{EM28XX_R08_GPIO,	0xff,	0xff,		10},
	{EM28XX_R08_GPIO,	0x7f,	0xff,		10},
	{EM28XX_R08_GPIO,	0x6f,	0xff,		10},
	{EM28XX_R08_GPIO,	0xff,	0xff,		10},
	{-1,			-1,	-1,		-1},
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

static struct em28xx_reg_seq silvercrest_reg_seq[] = {
	{EM28XX_R08_GPIO,	0xff,	0xff,		10},
	{EM28XX_R08_GPIO,	0x01,	0xf7,		10},
	{	-1,		-1,	-1,		-1},
};

static struct em28xx_reg_seq vc211a_enable[] = {
	{EM28XX_R08_GPIO,	0xff,	0x07,		10},
	{EM28XX_R08_GPIO,	0xff,	0x0f,		10},
	{EM28XX_R08_GPIO,	0xff,	0x0b,		10},
	{	-1,		-1,	-1,		-1},
};

static struct em28xx_reg_seq dikom_dk300_digital[] = {
	{EM28XX_R08_GPIO,	0x6e,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x08,	0xff,		10},
	{ -1,			-1,	-1,		-1},
};


/* Reset for the most [digital] boards */
static struct em28xx_reg_seq leadership_digital[] = {
	{EM2874_R80_GPIO,	0x70,	0xff,	10},
	{	-1,		-1,	-1,	-1},
};

static struct em28xx_reg_seq leadership_reset[] = {
	{EM2874_R80_GPIO,	0xf0,	0xff,	10},
	{EM2874_R80_GPIO,	0xb0,	0xff,	10},
	{EM2874_R80_GPIO,	0xf0,	0xff,	10},
	{	-1,		-1,	-1,	-1},
};

/* 2013:024f PCTV nanoStick T2 290e
 * GPIO_6 - demod reset
 * GPIO_7 - LED
 */
static struct em28xx_reg_seq pctv_290e[] = {
	{EM2874_R80_GPIO,	0x00,	0xff,		80},
	{EM2874_R80_GPIO,	0x40,	0xff,		80}, /* GPIO_6 = 1 */
	{EM2874_R80_GPIO,	0xc0,	0xff,		80}, /* GPIO_7 = 1 */
	{-1,			-1,	-1,		-1},
};

#if 0
static struct em28xx_reg_seq terratec_h5_gpio[] = {
	{EM28XX_R08_GPIO,	0xff,	0xff,	10},
	{EM2874_R80_GPIO,	0xf6,	0xff,	100},
	{EM2874_R80_GPIO,	0xf2,	0xff,	50},
	{EM2874_R80_GPIO,	0xf6,	0xff,	50},
	{ -1,			-1,	-1,	-1},
};

static struct em28xx_reg_seq terratec_h5_digital[] = {
	{EM2874_R80_GPIO,	0xf6,	0xff,	10},
	{EM2874_R80_GPIO,	0xe6,	0xff,	100},
	{EM2874_R80_GPIO,	0xa6,	0xff,	10},
	{ -1,			-1,	-1,	-1},
};
#endif

/* 2013:024f PCTV DVB-S2 Stick 460e
 * GPIO_0 - POWER_ON
 * GPIO_1 - BOOST
 * GPIO_2 - VUV_LNB (red LED)
 * GPIO_3 - EXT_12V
 * GPIO_4 - INT_DEM (DEMOD GPIO_0)
 * GPIO_5 - INT_LNB
 * GPIO_6 - RESET_DEM
 * GPIO_7 - LED (green LED)
 */
static struct em28xx_reg_seq pctv_460e[] = {
	{EM2874_R80_GPIO, 0x01, 0xff,  50},
	{0x0d,            0xff, 0xff,  50},
	{EM2874_R80_GPIO, 0x41, 0xff,  50}, /* GPIO_6=1 */
	{0x0d,            0x42, 0xff,  50},
	{EM2874_R80_GPIO, 0x61, 0xff,  50}, /* GPIO_5=1 */
	{             -1,   -1,   -1,  -1},
};

#if 0
static struct em28xx_reg_seq hauppauge_930c_gpio[] = {
	{EM2874_R80_GPIO,	0x6f,	0xff,	10},
	{EM2874_R80_GPIO,	0x4f,	0xff,	10}, /* xc5000 reset */
	{EM2874_R80_GPIO,	0x6f,	0xff,	10},
	{EM2874_R80_GPIO,	0x4f,	0xff,	10},
	{ -1,			-1,	-1,	-1},
};

static struct em28xx_reg_seq hauppauge_930c_digital[] = {
	{EM2874_R80_GPIO,	0xf6,	0xff,	10},
	{EM2874_R80_GPIO,	0xe6,	0xff,	100},
	{EM2874_R80_GPIO,	0xa6,	0xff,	10},
	{ -1,			-1,	-1,	-1},
};
#endif

/* 1b80:e425 MaxMedia UB425-TC
 * GPIO_6 - demod reset, 0=active
 * GPIO_7 - LED, 0=active
 */
static struct em28xx_reg_seq maxmedia_ub425_tc[] = {
	{EM2874_R80_GPIO,  0x83,  0xff,  100},
	{EM2874_R80_GPIO,  0xc3,  0xff,  100}, /* GPIO_6 = 1 */
	{EM2874_R80_GPIO,  0x43,  0xff,  000}, /* GPIO_7 = 0 */
	{-1,                 -1,    -1,   -1},
};

/* 2304:0242 PCTV QuatroStick (510e)
 * GPIO_2: decoder reset, 0=active
 * GPIO_4: decoder suspend, 0=active
 * GPIO_6: demod reset, 0=active
 * GPIO_7: LED, 1=active
 */
static struct em28xx_reg_seq pctv_510e[] = {
	{EM2874_R80_GPIO, 0x10, 0xff, 100},
	{EM2874_R80_GPIO, 0x14, 0xff, 100}, /* GPIO_2 = 1 */
	{EM2874_R80_GPIO, 0x54, 0xff, 050}, /* GPIO_6 = 1 */
	{             -1,   -1,   -1,  -1},
};

/* 2013:0251 PCTV QuatroStick nano (520e)
 * GPIO_2: decoder reset, 0=active
 * GPIO_4: decoder suspend, 0=active
 * GPIO_6: demod reset, 0=active
 * GPIO_7: LED, 1=active
 */
static struct em28xx_reg_seq pctv_520e[] = {
	{EM2874_R80_GPIO, 0x10, 0xff, 100},
	{EM2874_R80_GPIO, 0x14, 0xff, 100}, /* GPIO_2 = 1 */
	{EM2874_R80_GPIO, 0x54, 0xff, 050}, /* GPIO_6 = 1 */
	{EM2874_R80_GPIO, 0xd4, 0xff, 000}, /* GPIO_7 = 1 */
	{             -1,   -1,   -1,  -1},
};

/*
 *  Board definitions
 */
struct em28xx_board em28xx_boards[] = {
	[EM2750_BOARD_UNKNOWN] = {
		.name          = "EM2710/EM2750/EM2751 webcam grabber",
		.xclk          = EM28XX_XCLK_FREQUENCY_20MHZ,
		.tuner_type    = TUNER_ABSENT,
		.is_webcam     = 1,
		.input         = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = silvercrest_reg_seq,
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
		.is_webcam     = 1,	/* To enable sensor probe */
	},
	[EM2750_BOARD_DLCW_130] = {
		/* Beijing Huaqi Information Digital Technology Co., Ltd */
		.name          = "Huaqi DLCW-130",
		.valid         = EM28XX_BOARD_NOT_VALIDATED,
		.xclk          = EM28XX_XCLK_FREQUENCY_48MHZ,
		.tuner_type    = TUNER_ABSENT,
		.is_webcam     = 1,
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
		.has_ir_i2c   = 1,
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
		.has_ir_i2c   = 1,
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
		.has_ir_i2c   = 1,
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
		.has_ir_i2c   = 1,
		.tvaudio_addr = 0x58,
		.tda9887_conf = TDA9887_PRESENT |
				TDA9887_PORT2_ACTIVE |
				TDA9887_QSS,
		.decoder      = EM28XX_SAA711X,
		.adecoder     = EM28XX_TVAUDIO,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE4,
			.amux     = EM28XX_AMUX_AUX,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE5,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
			.radio	  = {
			.type     = EM28XX_RADIO,
			.amux     = EM28XX_AMUX_AUX,
			}
	},
	[EM2820_BOARD_VIDEOLOGY_20K14XUSB] = {
		.name         = "Videology 20K14XUSB USB2.0",
		.valid        = EM28XX_BOARD_NOT_VALIDATED,
		.tuner_type   = TUNER_ABSENT,
		.is_webcam    = 1,
		.input        = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = EM28XX_AMUX_VIDEO,
		} },
	},
	[EM2820_BOARD_SILVERCREST_WEBCAM] = {
		.name         = "Silvercrest Webcam 1.3mpix",
		.tuner_type   = TUNER_ABSENT,
		.is_webcam    = 1,
		.input        = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = 0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = silvercrest_reg_seq,
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
		.tuner_type   = TUNER_ABSENT,
		.is_webcam    = 1,
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
	[EM2861_BOARD_GADMEI_UTV330PLUS] = {
		.name         = "Gadmei UTV330+",
		.tuner_type   = TUNER_TNF_5335MF,
		.tda9887_conf = TDA9887_PRESENT,
		.ir_codes     = RC_MAP_GADMEI_RM008Z,
		.decoder      = EM28XX_SAA711X,
		.xclk         = EM28XX_XCLK_FREQUENCY_12MHZ,
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
	[EM2860_BOARD_TVP5150_REFERENCE_DESIGN] = {
		.name          = "EM2860/TVP5150 Reference Design",
		.tuner_type    = TUNER_ABSENT,	/* Capture only device */
		.decoder       = EM28XX_TVP5150,
		.input         = { {
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
		.tuner_type   = TUNER_TNF_5335MF,
		.xclk         = EM28XX_XCLK_I2S_MSB_TIMING |
				EM28XX_XCLK_FREQUENCY_12MHZ,
		.tda9887_conf = TDA9887_PRESENT,
		.decoder      = EM28XX_TVP5150,
		.has_msp34xx  = 1,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = pinnacle_hybrid_pro_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = pinnacle_hybrid_pro_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = pinnacle_hybrid_pro_analog,
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
		.tuner_type   = TUNER_ABSENT,
		.tuner_gpio   = default_tuner_gpio,
		.has_dvb      = 1,
		.dvb_gpio     = default_digital,
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
		.has_msp34xx  = 1,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.has_dvb      = 1,
		.dvb_gpio     = terratec_cinergy_USB_XS_FR_digital,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = terratec_cinergy_USB_XS_FR_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = terratec_cinergy_USB_XS_FR_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = terratec_cinergy_USB_XS_FR_analog,
		} },
	},
	[EM2884_BOARD_TERRATEC_H5] = {
		.name         = "Terratec Cinergy H5",
		.has_dvb      = 1,
#if 0
		.tuner_type   = TUNER_PHILIPS_TDA8290,
		.tuner_addr   = 0x41,
		.dvb_gpio     = terratec_h5_digital, /* FIXME: probably wrong */
		.tuner_gpio   = terratec_h5_gpio,
#else
		.tuner_type   = TUNER_ABSENT,
#endif
		.i2c_speed    = EM2874_I2C_SECONDARY_BUS_SELECT |
				EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
	},
	[EM2884_BOARD_HAUPPAUGE_WINTV_HVR_930C] = {
		.name         = "Hauppauge WinTV HVR 930C",
		.has_dvb      = 1,
#if 0 /* FIXME: Add analog support */
		.tuner_type   = TUNER_XC5000,
		.tuner_addr   = 0x41,
		.dvb_gpio     = hauppauge_930c_digital,
		.tuner_gpio   = hauppauge_930c_gpio,
#else
		.tuner_type   = TUNER_ABSENT,
#endif
		.ir_codes     = RC_MAP_HAUPPAUGE,
		.i2c_speed    = EM2874_I2C_SECONDARY_BUS_SELECT |
				EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
	},
	[EM2884_BOARD_CINERGY_HTC_STICK] = {
		.name         = "Terratec Cinergy HTC Stick",
		.has_dvb      = 1,
#if 0
		.tuner_type   = TUNER_PHILIPS_TDA8290,
		.tuner_addr   = 0x41,
		.dvb_gpio     = terratec_h5_digital, /* FIXME: probably wrong */
		.tuner_gpio   = terratec_h5_gpio,
#endif
		.i2c_speed    = EM2874_I2C_SECONDARY_BUS_SELECT |
				EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
	},
	[EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900] = {
		.name         = "Hauppauge WinTV HVR 900",
		.tda9887_conf = TDA9887_PRESENT,
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.mts_firmware = 1,
		.has_dvb      = 1,
		.dvb_gpio     = hauppauge_wintv_hvr_900_digital,
		.ir_codes     = RC_MAP_HAUPPAUGE,
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
		.has_dvb      = 1,
		.dvb_gpio     = hauppauge_wintv_hvr_900R2_digital,
		.ir_codes     = RC_MAP_HAUPPAUGE,
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
		.ir_codes       = RC_MAP_HAUPPAUGE,
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
		.ir_codes       = RC_MAP_HAUPPAUGE,
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
		.ir_codes       = RC_MAP_PINNACLE_PCTV_HD,
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
		.ir_codes       = RC_MAP_ATI_TV_WONDER_HD_600,
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
		.dvb_gpio       = default_digital,
		.ir_codes       = RC_MAP_TERRATEC_CINERGY_XS,
		.xclk           = EM28XX_XCLK_FREQUENCY_12MHZ, /* NEC IR */
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
		.has_ir_i2c   = 1,
		.tuner_type   = TUNER_LG_TALN,
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
	[EM2800_BOARD_VC211A] = {
		.name         = "Actionmaster/LinXcel/Digitus VC211A",
		.is_em2800    = 1,
		.tuner_type   = TUNER_ABSENT,	/* Capture-only board */
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = vc211a_enable,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = vc211a_enable,
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
		.name         = "Pinnacle Dazzle DVC 90/100/101/107 / Kaiser Baas Video to DVD maker "
			       "/ Kworld DVD Maker 2 / Plextor ConvertX PX-AV100U",
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

	[EM2874_BOARD_LEADERSHIP_ISDBT] = {
		.i2c_speed      = EM2874_I2C_SECONDARY_BUS_SELECT |
				  EM28XX_I2C_CLK_WAIT_ENABLE |
				  EM28XX_I2C_FREQ_100_KHZ,
		.xclk		= EM28XX_XCLK_FREQUENCY_10MHZ,
		.name		= "EM2874 Leadership ISDBT",
		.tuner_type	= TUNER_ABSENT,
		.tuner_gpio     = leadership_reset,
		.dvb_gpio       = leadership_digital,
		.has_dvb	= 1,
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
		.ir_codes	= RC_MAP_KWORLD_315U,
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
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.has_dvb      = 1,
		.dvb_gpio     = pinnacle_hybrid_pro_digital,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = pinnacle_hybrid_pro_analog,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = pinnacle_hybrid_pro_analog,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = pinnacle_hybrid_pro_analog,
		} },
	},
	[EM2882_BOARD_PINNACLE_HYBRID_PRO_330E] = {
		.name         = "Pinnacle Hybrid Pro (330e)",
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.mts_firmware = 1,
		.has_dvb      = 1,
		.dvb_gpio     = hauppauge_wintv_hvr_900R2_digital,
		.ir_codes     = RC_MAP_PINNACLE_PCTV_HD,
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
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.mts_firmware = 1,
		.has_dvb      = 1,
		.dvb_gpio     = kworld_330u_digital,
		.xclk         = EM28XX_XCLK_FREQUENCY_12MHZ, /* NEC IR */
		.ir_codes     = RC_MAP_KWORLD_315U,
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
		.name         = "Terratec Cinnergy Hybrid T USB XS (em2882)",
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.mts_firmware = 1,
		.decoder      = EM28XX_TVP5150,
		.has_dvb      = 1,
		.dvb_gpio     = hauppauge_wintv_hvr_900_digital,
		.ir_codes     = RC_MAP_TERRATEC_CINERGY_XS,
		.xclk         = EM28XX_XCLK_FREQUENCY_12MHZ,
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
	[EM2882_BOARD_DIKOM_DK300] = {
		.name         = "Dikom DK300",
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.mts_firmware = 1,
		.has_dvb      = 1,
		.dvb_gpio     = dikom_dk300_digital,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = TVP5150_COMPOSITE0,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = default_analog,
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
		.ir_codes     = RC_MAP_KAIOMY,
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
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = EM28XX_AMUX_LINE_IN,
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

	[EM2860_BOARD_ELGATO_VIDEO_CAPTURE] = {
		.name         = "Elgato Video Capture",
		.decoder      = EM28XX_SAA711X,
		.tuner_type   = TUNER_ABSENT,   /* Capture only device */
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

	[EM2882_BOARD_EVGA_INDTUBE] = {
		.name         = "Evga inDtube",
		.tuner_type   = TUNER_XC2028,
		.tuner_gpio   = default_tuner_gpio,
		.decoder      = EM28XX_TVP5150,
		.xclk         = EM28XX_XCLK_FREQUENCY_12MHZ, /* NEC IR */
		.mts_firmware = 1,
		.has_dvb      = 1,
		.dvb_gpio     = evga_indtube_digital,
		.ir_codes     = RC_MAP_EVGA_INDTUBE,
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
	/* eb1a:2868 Empia EM2870 + Philips CU1216L NIM (Philips TDA10023 +
	   Infineon TUA6034) */
	[EM2870_BOARD_REDDO_DVB_C_USB_BOX] = {
		.name          = "Reddo DVB-C USB TV Box",
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = reddo_dvb_c_usb_box,
		.has_dvb       = 1,
	},
	/* 1b80:a340 - Empia EM2870, NXP TDA18271HD and LG DT3304, sold
	 * initially as the KWorld PlusTV 340U, then as the UB435-Q.
	 * Early variants have a TDA18271HD/C1, later ones a TDA18271HD/C2 */
	[EM2870_BOARD_KWORLD_A340] = {
		.name       = "KWorld PlusTV 340U or UB435-Q (ATSC)",
		.tuner_type = TUNER_ABSENT,	/* Digital-only TDA18271HD */
		.has_dvb    = 1,
		.dvb_gpio   = kworld_a340_digital,
		.tuner_gpio = default_tuner_gpio,
	},
	/* 2013:024f PCTV nanoStick T2 290e.
	 * Empia EM28174, Sony CXD2820R and NXP TDA18271HD/C2 */
	[EM28174_BOARD_PCTV_290E] = {
		.name          = "PCTV nanoStick T2 290e",
		.i2c_speed      = EM2874_I2C_SECONDARY_BUS_SELECT |
			EM28XX_I2C_CLK_WAIT_ENABLE | EM28XX_I2C_FREQ_100_KHZ,
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = pctv_290e,
		.has_dvb       = 1,
		.ir_codes      = RC_MAP_PINNACLE_PCTV_HD,
	},
	/* 2013:024f PCTV DVB-S2 Stick 460e
	 * Empia EM28174, NXP TDA10071, Conexant CX24118A and Allegro A8293 */
	[EM28174_BOARD_PCTV_460E] = {
		.i2c_speed     = EM2874_I2C_SECONDARY_BUS_SELECT |
			EM28XX_I2C_CLK_WAIT_ENABLE | EM28XX_I2C_FREQ_400_KHZ,
		.name          = "PCTV DVB-S2 Stick (460e)",
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = pctv_460e,
		.has_dvb       = 1,
		.ir_codes      = RC_MAP_PINNACLE_PCTV_HD,
	},
	/* eb1a:5006 Honestech VIDBOX NW03
	 * Empia EM2860, Philips SAA7113, Empia EMP202, No Tuner */
	[EM2860_BOARD_HT_VIDBOX_NW03] = {
		.name                = "Honestech Vidbox NW03",
		.tuner_type          = TUNER_ABSENT,
		.decoder             = EM28XX_SAA711X,
		.input               = { {
			.type     = EM28XX_VMUX_COMPOSITE1,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,  /* S-VIDEO needs confirming */
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	/* 1b80:e425 MaxMedia UB425-TC
	 * Empia EM2874B + Micronas DRX 3913KA2 + NXP TDA18271HDC2 */
	[EM2874_BOARD_MAXMEDIA_UB425_TC] = {
		.name          = "MaxMedia UB425-TC",
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = maxmedia_ub425_tc,
		.has_dvb       = 1,
		.i2c_speed     = EM2874_I2C_SECONDARY_BUS_SELECT |
				EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
	},
	/* 2304:0242 PCTV QuatroStick (510e)
	 * Empia EM2884 + Micronas DRX 3926K + NXP TDA18271HDC2 */
	[EM2884_BOARD_PCTV_510E] = {
		.name          = "PCTV QuatroStick (510e)",
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = pctv_510e,
		.has_dvb       = 1,
		.ir_codes      = RC_MAP_PINNACLE_PCTV_HD,
		.i2c_speed     = EM2874_I2C_SECONDARY_BUS_SELECT |
				EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
	},
	/* 2013:0251 PCTV QuatroStick nano (520e)
	 * Empia EM2884 + Micronas DRX 3926K + NXP TDA18271HDC2 */
	[EM2884_BOARD_PCTV_520E] = {
		.name          = "PCTV QuatroStick nano (520e)",
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = pctv_520e,
		.has_dvb       = 1,
		.ir_codes      = RC_MAP_PINNACLE_PCTV_HD,
		.i2c_speed     = EM2874_I2C_SECONDARY_BUS_SELECT |
				EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
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
	{ USB_DEVICE(0xeb1a, 0x2710),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2820),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2821),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2860),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2861),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2862),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2863),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2870),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2881),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2883),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2868),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2875),
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
	{ USB_DEVICE(0xeb1a, 0xe359),
			.driver_info = EM2870_BOARD_KWORLD_355U },
	{ USB_DEVICE(0x1b80, 0xe302),
			.driver_info = EM2820_BOARD_PINNACLE_DVC_90 }, /* Kaiser Baas Video to DVD maker */
	{ USB_DEVICE(0x1b80, 0xe304),
			.driver_info = EM2820_BOARD_PINNACLE_DVC_90 }, /* Kworld DVD Maker 2 */
	{ USB_DEVICE(0x0ccd, 0x0036),
			.driver_info = EM2820_BOARD_TERRATEC_CINERGY_250 },
	{ USB_DEVICE(0x0ccd, 0x004c),
			.driver_info = EM2880_BOARD_TERRATEC_HYBRID_XS_FR },
	{ USB_DEVICE(0x0ccd, 0x004f),
			.driver_info = EM2860_BOARD_TERRATEC_HYBRID_XS },
	{ USB_DEVICE(0x0ccd, 0x005e),
			.driver_info = EM2882_BOARD_TERRATEC_HYBRID_XS },
	{ USB_DEVICE(0x0ccd, 0x0042),
			.driver_info = EM2882_BOARD_TERRATEC_HYBRID_XS },
	{ USB_DEVICE(0x0ccd, 0x0043),
			.driver_info = EM2870_BOARD_TERRATEC_XS },
	{ USB_DEVICE(0x0ccd, 0x008e),	/* Cinergy HTC USB XS Rev. 1 */
			.driver_info = EM2884_BOARD_TERRATEC_H5 },
	{ USB_DEVICE(0x0ccd, 0x00ac),	/* Cinergy HTC USB XS Rev. 2 */
			.driver_info = EM2884_BOARD_TERRATEC_H5 },
	{ USB_DEVICE(0x0ccd, 0x10a2),	/* H5 Rev. 1 */
			.driver_info = EM2884_BOARD_TERRATEC_H5 },
	{ USB_DEVICE(0x0ccd, 0x10ad),	/* H5 Rev. 2 */
			.driver_info = EM2884_BOARD_TERRATEC_H5 },
	{ USB_DEVICE(0x0ccd, 0x0084),
			.driver_info = EM2860_BOARD_TERRATEC_AV350 },
	{ USB_DEVICE(0x0ccd, 0x0096),
			.driver_info = EM2860_BOARD_TERRATEC_GRABBY },
	{ USB_DEVICE(0x0ccd, 0x10AF),
			.driver_info = EM2860_BOARD_TERRATEC_GRABBY },
	{ USB_DEVICE(0x0ccd, 0x00b2),
			.driver_info = EM2884_BOARD_CINERGY_HTC_STICK },
	{ USB_DEVICE(0x0fd9, 0x0033),
			.driver_info = EM2860_BOARD_ELGATO_VIDEO_CAPTURE},
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
			.driver_info = EM2882_BOARD_PINNACLE_HYBRID_PRO_330E },
	{ USB_DEVICE(0x2304, 0x0227),
			.driver_info = EM2880_BOARD_PINNACLE_PCTV_HD_PRO },
	{ USB_DEVICE(0x0413, 0x6023),
			.driver_info = EM2800_BOARD_LEADTEK_WINFAST_USBII },
	{ USB_DEVICE(0x093b, 0xa003),
		       .driver_info = EM2820_BOARD_PINNACLE_DVC_90 },
	{ USB_DEVICE(0x093b, 0xa005),
			.driver_info = EM2861_BOARD_PLEXTOR_PX_TV100U },
	{ USB_DEVICE(0x04bb, 0x0515),
			.driver_info = EM2820_BOARD_IODATA_GVMVP_SZ },
	{ USB_DEVICE(0xeb1a, 0x50a6),
			.driver_info = EM2860_BOARD_GADMEI_UTV330 },
	{ USB_DEVICE(0x1b80, 0xa340),
			.driver_info = EM2870_BOARD_KWORLD_A340 },
	{ USB_DEVICE(0x2013, 0x024f),
			.driver_info = EM28174_BOARD_PCTV_290E },
	{ USB_DEVICE(0x2013, 0x024c),
			.driver_info = EM28174_BOARD_PCTV_460E },
	{ USB_DEVICE(0x2040, 0x1605),
			.driver_info = EM2884_BOARD_HAUPPAUGE_WINTV_HVR_930C },
	{ USB_DEVICE(0xeb1a, 0x5006),
			.driver_info = EM2860_BOARD_HT_VIDBOX_NW03 },
	{ USB_DEVICE(0x1b80, 0xe309), /* Sveon STV40 */
			.driver_info = EM2860_BOARD_EASYCAP },
	{ USB_DEVICE(0x1b80, 0xe425),
			.driver_info = EM2874_BOARD_MAXMEDIA_UB425_TC },
	{ USB_DEVICE(0x2304, 0x0242),
			.driver_info = EM2884_BOARD_PCTV_510E },
	{ USB_DEVICE(0x2013, 0x0251),
			.driver_info = EM2884_BOARD_PCTV_520E },
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
	{0x166a0441, EM2880_BOARD_EMPIRE_DUAL_TV, TUNER_XC2028},
	{0xcee44a99, EM2882_BOARD_EVGA_INDTUBE, TUNER_XC2028},
	{0xb8846b20, EM2881_BOARD_PINNACLE_HYBRID_PRO, TUNER_XC2028},
	{0x63f653bd, EM2870_BOARD_REDDO_DVB_C_USB_BOX, TUNER_ABSENT},
	{0x4e913442, EM2882_BOARD_DIKOM_DK300, TUNER_XC2028},
};

/* I2C devicelist hash table for devices with generic USB IDs */
static struct em28xx_hash_table em28xx_i2c_hash[] = {
	{0xb06a32c3, EM2800_BOARD_TERRATEC_CINERGY_200, TUNER_LG_PAL_NEW_TAPC},
	{0xf51200e3, EM2800_BOARD_VGEAR_POCKETTV, TUNER_LG_PAL_NEW_TAPC},
	{0x1ba50080, EM2860_BOARD_SAA711X_REFERENCE_DESIGN, TUNER_ABSENT},
	{0x77800080, EM2860_BOARD_TVP5150_REFERENCE_DESIGN, TUNER_ABSENT},
	{0xc51200e3, EM2820_BOARD_GADMEI_TVR200, TUNER_LG_PAL_NEW_TAPC},
	{0x4ba50080, EM2861_BOARD_GADMEI_UTV330PLUS, TUNER_TNF_5335MF},
	{0x6b800080, EM2874_BOARD_LEADERSHIP_ISDBT, TUNER_ABSENT},
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

	if (dev->tuner_type != TUNER_XC2028 && dev->tuner_type != TUNER_XC5000)
		return 0;

	if (command != XC2028_TUNER_RESET && command != XC5000_TUNER_RESET)
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


/* FIXME: Should be replaced by a proper mt9m111 driver */
static int em28xx_initialize_mt9m111(struct em28xx *dev)
{
	int i;
	unsigned char regs[][3] = {
		{ 0x0d, 0x00, 0x01, },  /* reset and use defaults */
		{ 0x0d, 0x00, 0x00, },
		{ 0x0a, 0x00, 0x21, },
		{ 0x21, 0x04, 0x00, },  /* full readout speed, no row/col skipping */
	};

	for (i = 0; i < ARRAY_SIZE(regs); i++)
		i2c_master_send(&dev->i2c_client, &regs[i][0], 3);

	return 0;
}


/* FIXME: Should be replaced by a proper mt9m001 driver */
static int em28xx_initialize_mt9m001(struct em28xx *dev)
{
	int i;
	unsigned char regs[][3] = {
		{ 0x0d, 0x00, 0x01, },
		{ 0x0d, 0x00, 0x00, },
		{ 0x04, 0x05, 0x00, },	/* hres = 1280 */
		{ 0x03, 0x04, 0x00, },  /* vres = 1024 */
		{ 0x20, 0x11, 0x00, },
		{ 0x06, 0x00, 0x10, },
		{ 0x2b, 0x00, 0x24, },
		{ 0x2e, 0x00, 0x24, },
		{ 0x35, 0x00, 0x24, },
		{ 0x2d, 0x00, 0x20, },
		{ 0x2c, 0x00, 0x20, },
		{ 0x09, 0x0a, 0xd4, },
		{ 0x35, 0x00, 0x57, },
	};

	for (i = 0; i < ARRAY_SIZE(regs); i++)
		i2c_master_send(&dev->i2c_client, &regs[i][0], 3);

	return 0;
}

/* HINT method: webcam I2C chips
 *
 * This method works for webcams with Micron sensors
 */
static int em28xx_hint_sensor(struct em28xx *dev)
{
	int rc;
	char *sensor_name;
	unsigned char cmd;
	__be16 version_be;
	u16 version;

	/* Micron sensor detection */
	dev->i2c_client.addr = 0xba >> 1;
	cmd = 0;
	i2c_master_send(&dev->i2c_client, &cmd, 1);
	rc = i2c_master_recv(&dev->i2c_client, (char *)&version_be, 2);
	if (rc != 2)
		return -EINVAL;

	version = be16_to_cpu(version_be);
	switch (version) {
	case 0x8232:		/* mt9v011 640x480 1.3 Mpix sensor */
	case 0x8243:		/* mt9v011 rev B 640x480 1.3 Mpix sensor */
		dev->model = EM2820_BOARD_SILVERCREST_WEBCAM;
		em28xx_set_model(dev);

		sensor_name = "mt9v011";
		dev->em28xx_sensor = EM28XX_MT9V011;
		dev->sensor_xres = 640;
		dev->sensor_yres = 480;
		/*
		 * FIXME: mt9v011 uses I2S speed as xtal clk - at least with
		 * the Silvercrest cam I have here for testing - for higher
		 * resolutions, a high clock cause horizontal artifacts, so we
		 * need to use a lower xclk frequency.
		 * Yet, it would be possible to adjust xclk depending on the
		 * desired resolution, since this affects directly the
		 * frame rate.
		 */
		dev->board.xclk = EM28XX_XCLK_FREQUENCY_4_3MHZ;
		dev->sensor_xtal = 4300000;

		/* probably means GRGB 16 bit bayer */
		dev->vinmode = 0x0d;
		dev->vinctl = 0x00;

		break;

	case 0x143a:    /* MT9M111 as found in the ECS G200 */
		dev->model = EM2750_BOARD_UNKNOWN;
		em28xx_set_model(dev);

		sensor_name = "mt9m111";
		dev->board.xclk = EM28XX_XCLK_FREQUENCY_48MHZ;
		dev->em28xx_sensor = EM28XX_MT9M111;
		em28xx_initialize_mt9m111(dev);
		dev->sensor_xres = 640;
		dev->sensor_yres = 512;

		dev->vinmode = 0x0a;
		dev->vinctl = 0x00;

		break;

	case 0x8431:
		dev->model = EM2750_BOARD_UNKNOWN;
		em28xx_set_model(dev);

		sensor_name = "mt9m001";
		dev->em28xx_sensor = EM28XX_MT9M001;
		em28xx_initialize_mt9m001(dev);
		dev->sensor_xres = 1280;
		dev->sensor_yres = 1024;

		/* probably means BGGR 16 bit bayer */
		dev->vinmode = 0x0c;
		dev->vinctl = 0x00;

		break;
	default:
		printk("Unknown Micron Sensor 0x%04x\n", version);
		return -EINVAL;
	}

	/* Setup webcam defaults */
	em28xx_pre_card_setup(dev);

	em28xx_errdev("Sensor is %s, using model %s entry.\n",
		      sensor_name, em28xx_boards[dev->model].name);

	return 0;
}

/* Since em28xx_pre_card_setup() requires a proper dev->model,
 * this won't work for boards with generic PCI IDs
 */
static void em28xx_pre_card_setup(struct em28xx *dev)
{
	/* Set the initial XCLK and I2C clock values based on the board
	   definition */
	em28xx_write_reg(dev, EM28XX_R0F_XCLK, dev->board.xclk & 0x7f);
	if (!dev->board.is_em2800)
		em28xx_write_reg(dev, EM28XX_R06_I2C_CLK, dev->board.i2c_speed);
	msleep(50);

	/* request some modules */
	switch (dev->model) {
	case EM2861_BOARD_PLEXTOR_PX_TV100U:
		/* Sets the msp34xx I2S speed */
		dev->i2s_speed = 2048000;
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
	case EM2882_BOARD_TERRATEC_HYBRID_XS:
		ctl->demod = XC3028_FE_ZARLINK456;
		break;
	case EM2880_BOARD_TERRATEC_HYBRID_XS:
	case EM2880_BOARD_TERRATEC_HYBRID_XS_FR:
	case EM2881_BOARD_PINNACLE_HYBRID_PRO:
		ctl->demod = XC3028_FE_ZARLINK456;
		break;
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900_R2:
	case EM2882_BOARD_PINNACLE_HYBRID_PRO_330E:
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
	case EM2882_BOARD_DIKOM_DK300:
	case EM2882_BOARD_KWORLD_VS_DVBT:
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

static void em28xx_card_setup(struct em28xx *dev)
{
	/*
	 * If the device can be a webcam, seek for a sensor.
	 * If sensor is not found, then it isn't a webcam.
	 */
	if (dev->board.is_webcam) {
		if (em28xx_hint_sensor(dev) < 0)
			dev->board.is_webcam = 0;
		else
			dev->progressive = 1;
	}

	if (!dev->board.is_webcam) {
		switch (dev->model) {
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
		if (em28xx_hint_board(dev) < 0)
			em28xx_errdev("Board not discovered\n");
		else {
			em28xx_set_model(dev);
			em28xx_pre_card_setup(dev);
		}
		break;
		default:
			em28xx_set_model(dev);
		}
	}

	em28xx_info("Identified as %s (card=%d)\n",
		    dev->board.name, dev->model);

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
#if defined(CONFIG_MODULES) && defined(MODULE)
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
		em28xx_gpio_set(dev, dev->board.tuner_gpio);
		em28xx_set_mode(dev, EM28XX_ANALOG_MODE);
		break;

/*
		 * The Dikom DK300 is detected as an Kworld VS-DVB-T 323UR.
		 *
		 * This occurs because they share identical USB vendor and
		 * product IDs.
		 *
		 * What we do here is look up the EEPROM hash of the Dikom
		 * and if it is found then we decide that we do not have
		 * a Kworld and reset the device to the Dikom instead.
		 *
		 * This solution is only valid if they do not share eeprom
		 * hash identities which has not been determined as yet.
		 */
	case EM2882_BOARD_KWORLD_VS_DVBT:
		if (!em28xx_hint_board(dev))
			em28xx_set_model(dev);

		/* In cases where we had to use a board hint, the call to
		   em28xx_set_mode() in em28xx_pre_card_setup() was a no-op,
		   so make the call now so the analog GPIOs are set properly
		   before probing the i2c bus. */
		em28xx_gpio_set(dev, dev->board.tuner_gpio);
		em28xx_set_mode(dev, EM28XX_ANALOG_MODE);
		break;
	}

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
		v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
			"msp3400", 0, msp3400_addrs);

	if (dev->board.decoder == EM28XX_SAA711X)
		v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
			"saa7115_auto", 0, saa711x_addrs);

	if (dev->board.decoder == EM28XX_TVP5150)
		v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
			"tvp5150", 0, tvp5150_addrs);

	if (dev->em28xx_sensor == EM28XX_MT9V011) {
		struct mt9v011_platform_data pdata;
		struct i2c_board_info mt9v011_info = {
			.type = "mt9v011",
			.addr = 0xba >> 1,
			.platform_data = &pdata,
		};

		pdata.xtal = dev->sensor_xtal;
		v4l2_i2c_new_subdev_board(&dev->v4l2_dev, &dev->i2c_adap,
				&mt9v011_info, NULL);
	}


	if (dev->board.adecoder == EM28XX_TVAUDIO)
		v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
			"tvaudio", dev->board.tvaudio_addr, NULL);

	if (dev->board.tuner_type != TUNER_ABSENT) {
		int has_demod = (dev->tda9887_conf & TDA9887_PRESENT);

		if (dev->board.radio.type)
			v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
				"tuner", dev->board.radio_addr, NULL);

		if (has_demod)
			v4l2_i2c_new_subdev(&dev->v4l2_dev,
				&dev->i2c_adap, "tuner",
				0, v4l2_i2c_tuner_addrs(ADDRS_DEMOD));
		if (dev->tuner_addr == 0) {
			enum v4l2_i2c_tuner_type type =
				has_demod ? ADDRS_TV_WITH_DEMOD : ADDRS_TV;
			struct v4l2_subdev *sd;

			sd = v4l2_i2c_new_subdev(&dev->v4l2_dev,
				&dev->i2c_adap, "tuner",
				0, v4l2_i2c_tuner_addrs(type));

			if (sd)
				dev->tuner_addr = v4l2_i2c_subdev_addr(sd);
		} else {
			v4l2_i2c_new_subdev(&dev->v4l2_dev, &dev->i2c_adap,
				"tuner", dev->tuner_addr, NULL);
		}
	}

	em28xx_tuner_setup(dev);
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
	if (dev->board.has_ir_i2c && !disable_ir)
		request_module("em28xx-rc");
}

static void request_modules(struct em28xx *dev)
{
	INIT_WORK(&dev->request_module_wk, request_module_async);
	schedule_work(&dev->request_module_wk);
}

static void flush_request_modules(struct em28xx *dev)
{
	flush_work_sync(&dev->request_module_wk);
}
#else
#define request_modules(dev)
#define flush_request_modules(dev)
#endif /* CONFIG_MODULES */

/*
 * em28xx_release_resources()
 * unregisters the v4l2,i2c and usb devices
 * called when the device gets disconnected or at module unload
*/
void em28xx_release_resources(struct em28xx *dev)
{
	/*FIXME: I2C IR should be disconnected */

	em28xx_release_analog_resources(dev);

	em28xx_i2c_unregister(dev);

	v4l2_device_unregister(&dev->v4l2_dev);

	usb_put_dev(dev->udev);

	/* Mark device as unused */
	clear_bit(dev->devno, &em28xx_devused);
};

/*
 * em28xx_init_dev()
 * allocates and inits the device structs, registers i2c bus and v4l device
 */
static int em28xx_init_dev(struct em28xx *dev, struct usb_device *udev,
			   struct usb_interface *interface,
			   int minor)
{
	int retval;

	dev->udev = udev;
	mutex_init(&dev->ctrl_urb_lock);
	spin_lock_init(&dev->slock);

	dev->em28xx_write_regs = em28xx_write_regs;
	dev->em28xx_read_reg = em28xx_read_reg;
	dev->em28xx_read_reg_req_len = em28xx_read_reg_req_len;
	dev->em28xx_write_regs_req = em28xx_write_regs_req;
	dev->em28xx_read_reg_req = em28xx_read_reg_req;
	dev->board.is_em2800 = em28xx_boards[dev->model].is_em2800;

	em28xx_set_model(dev);

	/* Set the default GPO/GPIO for legacy devices */
	dev->reg_gpo_num = EM2880_R04_GPO;
	dev->reg_gpio_num = EM28XX_R08_GPIO;

	dev->wait_after_write = 5;

	/* Based on the Chip ID, set the device configuration */
	retval = em28xx_read_reg(dev, EM28XX_R0A_CHIPID);
	if (retval > 0) {
		dev->chip_id = retval;

		switch (dev->chip_id) {
		case CHIP_ID_EM2800:
			em28xx_info("chip ID is em2800\n");
			break;
		case CHIP_ID_EM2710:
			em28xx_info("chip ID is em2710\n");
			break;
		case CHIP_ID_EM2750:
			em28xx_info("chip ID is em2750\n");
			break;
		case CHIP_ID_EM2820:
			em28xx_info("chip ID is em2820 (or em2710)\n");
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
		case CHIP_ID_EM28174:
			em28xx_info("chip ID is em28174\n");
			dev->reg_gpio_num = EM2874_R80_GPIO;
			dev->wait_after_write = 0;
			break;
		case CHIP_ID_EM2883:
			em28xx_info("chip ID is em2882/em2883\n");
			dev->wait_after_write = 0;
			break;
		case CHIP_ID_EM2884:
			em28xx_info("chip ID is em2884\n");
			dev->reg_gpio_num = EM2874_R80_GPIO;
			dev->wait_after_write = 0;
			break;
		default:
			em28xx_info("em28xx chip ID = %d\n", dev->chip_id);
		}
	}

	if (dev->is_audio_only) {
		retval = em28xx_audio_setup(dev);
		if (retval)
			return -ENODEV;
		em28xx_init_extension(dev);

		return 0;
	}

	/* Prepopulate cached GPO register content */
	retval = em28xx_read_reg(dev, dev->reg_gpo_num);
	if (retval >= 0)
		dev->reg_gpo = retval;

	em28xx_pre_card_setup(dev);

	if (!dev->board.is_em2800) {
		/* Resets I2C speed */
		retval = em28xx_write_reg(dev, EM28XX_R06_I2C_CLK, dev->board.i2c_speed);
		if (retval < 0) {
			em28xx_errdev("%s: em28xx_write_reg failed!"
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
	retval = em28xx_i2c_register(dev);
	if (retval < 0) {
		em28xx_errdev("%s: em28xx_i2c_register - error [%d]!\n",
			__func__, retval);
		goto unregister_dev;
	}

	/*
	 * Default format, used for tvp5150 or saa711x output formats
	 */
	dev->vinmode = 0x10;
	dev->vinctl  = EM28XX_VINCTRL_INTERLACED |
		       EM28XX_VINCTRL_CCIR656_ENABLE;

	/* Do board specific init and eeprom reading */
	em28xx_card_setup(dev);

	/* Configure audio */
	retval = em28xx_audio_setup(dev);
	if (retval < 0) {
		em28xx_errdev("%s: Error while setting audio - error [%d]!\n",
			__func__, retval);
		goto fail;
	}

	/* wake i2c devices */
	em28xx_wake_i2c(dev);

	/* init video dma queues */
	INIT_LIST_HEAD(&dev->vidq.active);
	INIT_LIST_HEAD(&dev->vbiq.active);

	if (dev->board.has_msp34xx) {
		/* Send a reset to other chips via gpio */
		retval = em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xf7);
		if (retval < 0) {
			em28xx_errdev("%s: em28xx_write_reg - "
				      "msp34xx(1) failed! error [%d]\n",
				      __func__, retval);
			goto fail;
		}
		msleep(3);

		retval = em28xx_write_reg(dev, EM28XX_R08_GPIO, 0xff);
		if (retval < 0) {
			em28xx_errdev("%s: em28xx_write_reg - "
				      "msp34xx(2) failed! error [%d]\n",
				      __func__, retval);
			goto fail;
		}
		msleep(3);
	}

	retval = em28xx_register_analog_devices(dev);
	if (retval < 0) {
		goto fail;
	}

	/* Save some power by putting tuner to sleep */
	v4l2_device_call_all(&dev->v4l2_dev, 0, core, s_power, 0);

	return 0;

fail:
	em28xx_i2c_unregister(dev);

unregister_dev:
	v4l2_device_unregister(&dev->v4l2_dev);

	return retval;
}

/* high bandwidth multiplier, as encoded in highspeed endpoint descriptors */
#define hb_mult(wMaxPacketSize) (1 + (((wMaxPacketSize) >> 11) & 0x03))

/*
 * em28xx_usb_probe()
 * checks for supported devices
 */
static int em28xx_usb_probe(struct usb_interface *interface,
			    const struct usb_device_id *id)
{
	struct usb_device *udev;
	struct em28xx *dev = NULL;
	int retval;
	bool has_audio = false, has_video = false, has_dvb = false;
	int i, nr;
	const int ifnum = interface->altsetting[0].desc.bInterfaceNumber;
	char *speed;

	udev = usb_get_dev(interface_to_usbdev(interface));

	/* Check to see next free device and mark as used */
	do {
		nr = find_first_zero_bit(&em28xx_devused, EM28XX_MAXBOARDS);
		if (nr >= EM28XX_MAXBOARDS) {
			/* No free device slots */
			printk(DRIVER_NAME ": Supports only %i em28xx boards.\n",
					EM28XX_MAXBOARDS);
			retval = -ENOMEM;
			goto err_no_slot;
		}
	} while (test_and_set_bit(nr, &em28xx_devused));

	/* Don't register audio interfaces */
	if (interface->altsetting[0].desc.bInterfaceClass == USB_CLASS_AUDIO) {
		em28xx_err(DRIVER_NAME " audio device (%04x:%04x): "
			"interface %i, class %i\n",
			le16_to_cpu(udev->descriptor.idVendor),
			le16_to_cpu(udev->descriptor.idProduct),
			ifnum,
			interface->altsetting[0].desc.bInterfaceClass);

		retval = -ENODEV;
		goto err;
	}

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		em28xx_err(DRIVER_NAME ": out of memory!\n");
		retval = -ENOMEM;
		goto err;
	}

	/* compute alternate max packet sizes */
	dev->alt_max_pkt_size = kmalloc(sizeof(dev->alt_max_pkt_size[0]) *
					interface->num_altsetting, GFP_KERNEL);
	if (dev->alt_max_pkt_size == NULL) {
		em28xx_errdev("out of memory!\n");
		kfree(dev);
		retval = -ENOMEM;
		goto err;
	}

	/* Get endpoints */
	for (i = 0; i < interface->num_altsetting; i++) {
		int ep;

		for (ep = 0; ep < interface->altsetting[i].desc.bNumEndpoints; ep++) {
			const struct usb_endpoint_descriptor *e;
			int sizedescr, size;

			e = &interface->altsetting[i].endpoint[ep].desc;

			sizedescr = le16_to_cpu(e->wMaxPacketSize);
			size = sizedescr & 0x7ff;

			if (udev->speed == USB_SPEED_HIGH)
				size = size * hb_mult(sizedescr);

			if (usb_endpoint_xfer_isoc(e) &&
			    usb_endpoint_dir_in(e)) {
				switch (e->bEndpointAddress) {
				case EM28XX_EP_AUDIO:
					has_audio = true;
					break;
				case EM28XX_EP_ANALOG:
					has_video = true;
					dev->alt_max_pkt_size[i] = size;
					break;
				case EM28XX_EP_DIGITAL:
					has_dvb = true;
					if (size > dev->dvb_max_pkt_size) {
						dev->dvb_max_pkt_size = size;
						dev->dvb_alt = i;
					}
					break;
				}
			}
		}
	}

	if (!(has_audio || has_video || has_dvb)) {
		retval = -ENODEV;
		goto err_free;
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

	printk(KERN_INFO DRIVER_NAME
		": New device %s %s @ %s Mbps "
		"(%04x:%04x, interface %d, class %d)\n",
		udev->manufacturer ? udev->manufacturer : "",
		udev->product ? udev->product : "",
		speed,
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct),
		ifnum,
		interface->altsetting->desc.bInterfaceNumber);

	if (has_audio)
		printk(KERN_INFO DRIVER_NAME
		       ": Audio Vendor Class interface %i found\n",
		       ifnum);
	if (has_video)
		printk(KERN_INFO DRIVER_NAME
		       ": Video interface %i found\n",
		       ifnum);
	if (has_dvb)
		printk(KERN_INFO DRIVER_NAME
		       ": DVB interface %i found\n",
		       ifnum);

	/*
	 * Make sure we have 480 Mbps of bandwidth, otherwise things like
	 * video stream wouldn't likely work, since 12 Mbps is generally
	 * not enough even for most Digital TV streams.
	 */
	if (udev->speed != USB_SPEED_HIGH && disable_usb_speed_check == 0) {
		printk(DRIVER_NAME ": Device initialization failed.\n");
		printk(DRIVER_NAME ": Device must be connected to a high-speed"
		       " USB 2.0 port.\n");
		retval = -ENODEV;
		goto err_free;
	}

	snprintf(dev->name, sizeof(dev->name), "em28xx #%d", nr);
	dev->devno = nr;
	dev->model = id->driver_info;
	dev->alt   = -1;
	dev->is_audio_only = has_audio && !(has_video || has_dvb);
	dev->has_alsa_audio = has_audio;
	dev->audio_ifnum = ifnum;

	/* Checks if audio is provided by some interface */
	for (i = 0; i < udev->config->desc.bNumInterfaces; i++) {
		struct usb_interface *uif = udev->config->interface[i];
		if (uif->altsetting[0].desc.bInterfaceClass == USB_CLASS_AUDIO) {
			dev->has_audio_class = 1;
			break;
		}
	}

	dev->num_alt = interface->num_altsetting;

	if ((card[nr] >= 0) && (card[nr] < em28xx_bcount))
		dev->model = card[nr];

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* allocate device struct */
	mutex_init(&dev->lock);
	mutex_lock(&dev->lock);
	retval = em28xx_init_dev(dev, udev, interface, nr);
	if (retval) {
		goto unlock_and_free;
	}

	if (has_dvb) {
		/* pre-allocate DVB isoc transfer buffers */
		retval = em28xx_alloc_isoc(dev, EM28XX_DIGITAL_MODE,
					   EM28XX_DVB_MAX_PACKETS,
					   EM28XX_DVB_NUM_BUFS,
					   dev->dvb_max_pkt_size);
		if (retval) {
			goto unlock_and_free;
		}
	}

	request_modules(dev);

	/* Should be the last thing to do, to avoid newer udev's to
	   open the device before fully initializing it
	 */
	mutex_unlock(&dev->lock);

	/*
	 * These extensions can be modules. If the modules are already
	 * loaded then we can initialise the device now, otherwise we
	 * will initialise it when the modules load instead.
	 */
	em28xx_init_extension(dev);

	return 0;

unlock_and_free:
	mutex_unlock(&dev->lock);

err_free:
	kfree(dev->alt_max_pkt_size);
	kfree(dev);

err:
	clear_bit(nr, &em28xx_devused);

err_no_slot:
	usb_put_dev(udev);
	return retval;
}

/*
 * em28xx_usb_disconnect()
 * called when the device gets disconnected
 * video device will be unregistered on v4l2_close in case it is still open
 */
static void em28xx_usb_disconnect(struct usb_interface *interface)
{
	struct em28xx *dev;

	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	if (!dev)
		return;

	if (dev->is_audio_only) {
		mutex_lock(&dev->lock);
		em28xx_close_extension(dev);
		mutex_unlock(&dev->lock);
		return;
	}

	em28xx_info("disconnecting %s\n", dev->vdev->name);

	flush_request_modules(dev);

	/* wait until all current v4l2 io is finished then deallocate
	   resources */
	mutex_lock(&dev->lock);

	v4l2_device_disconnect(&dev->v4l2_dev);

	if (dev->users) {
		em28xx_warn
		    ("device %s is open! Deregistration and memory "
		     "deallocation are deferred on close.\n",
		     video_device_node_name(dev->vdev));

		dev->state |= DEV_MISCONFIGURED;
		em28xx_uninit_isoc(dev, dev->mode);
		dev->state |= DEV_DISCONNECTED;
	} else {
		dev->state |= DEV_DISCONNECTED;
		em28xx_release_resources(dev);
	}

	/* free DVB isoc buffers */
	em28xx_uninit_isoc(dev, EM28XX_DIGITAL_MODE);

	mutex_unlock(&dev->lock);

	em28xx_close_extension(dev);

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

module_usb_driver(em28xx_usb_driver);
