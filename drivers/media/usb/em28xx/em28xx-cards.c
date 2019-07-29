// SPDX-License-Identifier: GPL-2.0+
//
// em28xx-cards.c - driver for Empia EM2800/EM2820/2840 USB
//		    video capture devices
//
// Copyright (C) 2005 Ludovico Cavedon <cavedon@sssup.it>
//		      Markus Rechberger <mrechberger@gmail.com>
//		      Mauro Carvalho Chehab <mchehab@kernel.org>
//		      Sascha Sommer <saschasommer@freenet.de>
// Copyright (C) 2012 Frank Schäfer <fschaefer.oss@googlemail.com>
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "em28xx.h"

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/usb.h>
#include <media/tuner.h>
#include <media/drv-intf/msp3400.h>
#include <media/i2c/saa7115.h>
#include <dt-bindings/media/tvp5150.h>
#include <media/i2c/tvaudio.h>
#include <media/tveeprom.h>
#include <media/v4l2-common.h>
#include <sound/ac97_codec.h>

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

static unsigned int card[]     = {[0 ... (EM28XX_MAXBOARDS - 1)] = -1U };
module_param_array(card,  int, NULL, 0444);
MODULE_PARM_DESC(card,     "card type");

static int usb_xfer_mode = -1;
module_param(usb_xfer_mode, int, 0444);
MODULE_PARM_DESC(usb_xfer_mode,
		 "USB transfer mode for frame data (-1 = auto, 0 = prefer isoc, 1 = prefer bulk)");

/* Bitmask marking allocated devices from 0 to EM28XX_MAXBOARDS - 1 */
static DECLARE_BITMAP(em28xx_devused, EM28XX_MAXBOARDS);

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
static const struct em28xx_reg_seq default_analog[] = {
	{EM2820_R08_GPIO_CTRL,	0x6d,   ~EM_GPIO_4,	10},
	{	-1,		-1,	-1,		-1},
};

/* Reset for the most [digital] boards */
static const struct em28xx_reg_seq default_digital[] = {
	{EM2820_R08_GPIO_CTRL,	0x6e,	~EM_GPIO_4,	10},
	{	-1,		-1,	-1,		-1},
};

/* Board :Zolid Hybrid Tv Stick */
static struct em28xx_reg_seq zolid_tuner[] = {
	{EM2820_R08_GPIO_CTRL,		0xfd,		0xff,	100},
	{EM2820_R08_GPIO_CTRL,		0xfe,		0xff,	100},
	{		-1,					-1,			-1,		 -1},
};

static struct em28xx_reg_seq zolid_digital[] = {
	{EM2820_R08_GPIO_CTRL,		0x6a,		0xff,	100},
	{EM2820_R08_GPIO_CTRL,		0x7a,		0xff,	100},
	{EM2880_R04_GPO,			0x04,		0xff,	100},
	{EM2880_R04_GPO,			0x0c,		0xff,	100},
	{	-1,						-1,			-1,		 -1},
};

/* Board Hauppauge WinTV HVR 900 analog */
static const struct em28xx_reg_seq hauppauge_wintv_hvr_900_analog[] = {
	{EM2820_R08_GPIO_CTRL,	0x2d,	~EM_GPIO_4,	10},
	{	0x05,		0xff,	0x10,		10},
	{	-1,		-1,	-1,		-1},
};

/* Board Hauppauge WinTV HVR 900 digital */
static const struct em28xx_reg_seq hauppauge_wintv_hvr_900_digital[] = {
	{EM2820_R08_GPIO_CTRL,	0x2e,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x04,	0x0f,		10},
	{EM2880_R04_GPO,	0x0c,	0x0f,		10},
	{	-1,		-1,	-1,		-1},
};

/* Board Hauppauge WinTV HVR 900 (R2) digital */
static const struct em28xx_reg_seq hauppauge_wintv_hvr_900R2_digital[] = {
	{EM2820_R08_GPIO_CTRL,	0x2e,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x0c,	0x0f,		10},
	{	-1,		-1,	-1,		-1},
};

/* Boards - EM2880 MSI DIGIVOX AD and EM2880_BOARD_MSI_DIGIVOX_AD_II */
static const struct em28xx_reg_seq em2880_msi_digivox_ad_analog[] = {
	{EM2820_R08_GPIO_CTRL,	0x69,   ~EM_GPIO_4,	10},
	{	-1,		-1,	-1,		-1},
};

/* Board - EM2882 Kworld 315U digital */
static const struct em28xx_reg_seq em2882_kworld_315u_digital[] = {
	{EM2820_R08_GPIO_CTRL,	0xff,	0xff,		10},
	{EM2820_R08_GPIO_CTRL,	0xfe,	0xff,		10},
	{EM2880_R04_GPO,	0x04,	0xff,		10},
	{EM2880_R04_GPO,	0x0c,	0xff,		10},
	{EM2820_R08_GPIO_CTRL,	0x7e,	0xff,		10},
	{	-1,		-1,	-1,		-1},
};

static const struct em28xx_reg_seq em2882_kworld_315u_tuner_gpio[] = {
	{EM2880_R04_GPO,	0x08,	0xff,		10},
	{EM2880_R04_GPO,	0x0c,	0xff,		10},
	{EM2880_R04_GPO,	0x08,	0xff,		10},
	{EM2880_R04_GPO,	0x0c,	0xff,		10},
	{	-1,		-1,	-1,		-1},
};

static const struct em28xx_reg_seq kworld_330u_analog[] = {
	{EM2820_R08_GPIO_CTRL,	0x6d,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x00,	0xff,		10},
	{	-1,		-1,	-1,		-1},
};

static const struct em28xx_reg_seq kworld_330u_digital[] = {
	{EM2820_R08_GPIO_CTRL,	0x6e,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x08,	0xff,		10},
	{	-1,		-1,	-1,		-1},
};

/*
 * Evga inDtube
 * GPIO0 - Enable digital power (s5h1409) - low to enable
 * GPIO1 - Enable analog power (tvp5150/emp202) - low to enable
 * GPIO4 - xc3028 reset
 * GOP3  - s5h1409 reset
 */
static const struct em28xx_reg_seq evga_indtube_analog[] = {
	{EM2820_R08_GPIO_CTRL,	0x79,   0xff,		60},
	{	-1,		-1,	-1,		-1},
};

static const struct em28xx_reg_seq evga_indtube_digital[] = {
	{EM2820_R08_GPIO_CTRL,	0x7a,	0xff,		 1},
	{EM2880_R04_GPO,	0x04,	0xff,		10},
	{EM2880_R04_GPO,	0x0c,	0xff,		 1},
	{	-1,		-1,	-1,		-1},
};

/*
 * KWorld PlusTV 340U, UB435-Q and UB435-Q V2 (ATSC) GPIOs map:
 * EM_GPIO_0 - currently unknown
 * EM_GPIO_1 - LED disable/enable (1 = off, 0 = on)
 * EM_GPIO_2 - currently unknown
 * EM_GPIO_3 - currently unknown
 * EM_GPIO_4 - TDA18271HD/C1 tuner (1 = active, 0 = in reset)
 * EM_GPIO_5 - LGDT3304 ATSC/QAM demod (1 = active, 0 = in reset)
 * EM_GPIO_6 - currently unknown
 * EM_GPIO_7 - currently unknown
 */
static const struct em28xx_reg_seq kworld_a340_digital[] = {
	{EM2820_R08_GPIO_CTRL,	0x6d,	~EM_GPIO_4,	10},
	{	-1,		-1,	-1,		-1},
};

static const struct em28xx_reg_seq kworld_ub435q_v3_digital[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0xff,	0xff,	100},
	{EM2874_R80_GPIO_P0_CTRL,	0xfe,	0xff,	100},
	{EM2874_R80_GPIO_P0_CTRL,	0xbe,	0xff,	100},
	{EM2874_R80_GPIO_P0_CTRL,	0xfe,	0xff,	100},
	{	-1,			-1,	-1,	-1},
};

/* Pinnacle Hybrid Pro eb1a:2881 */
static const struct em28xx_reg_seq pinnacle_hybrid_pro_analog[] = {
	{EM2820_R08_GPIO_CTRL,	0xfd,   ~EM_GPIO_4,	10},
	{	-1,		-1,	-1,		-1},
};

static const struct em28xx_reg_seq pinnacle_hybrid_pro_digital[] = {
	{EM2820_R08_GPIO_CTRL,	0x6e,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x04,	0xff,	       100},/* zl10353 reset */
	{EM2880_R04_GPO,	0x0c,	0xff,		 1},
	{	-1,		-1,	-1,		-1},
};

static const struct em28xx_reg_seq terratec_cinergy_USB_XS_FR_analog[] = {
	{EM2820_R08_GPIO_CTRL,	0x6d,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x00,	0xff,		10},
	{	-1,		-1,	-1,		-1},
};

static const struct em28xx_reg_seq terratec_cinergy_USB_XS_FR_digital[] = {
	{EM2820_R08_GPIO_CTRL,	0x6e,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x08,	0xff,		10},
	{	-1,		-1,	-1,		-1},
};

/*
 * PCTV HD Mini (80e) GPIOs
 * 0-5: not used
 * 6:   demod reset, active low
 * 7:   LED on, active high
 */
static const struct em28xx_reg_seq em2874_pctv_80e_digital[] = {
	{EM28XX_R06_I2C_CLK,    0x45,   0xff,		  10}, /*400 KHz*/
	{EM2874_R80_GPIO_P0_CTRL, 0x00,   0xff,		  100},/*Demod reset*/
	{EM2874_R80_GPIO_P0_CTRL, 0x40,   0xff,		  10},
	{  -1,			-1,	-1,		  -1},
};

/*
 * eb1a:2868 Reddo DVB-C USB TV Box
 * GPIO4 - CU1216L NIM
 * Other GPIOs seems to be don't care.
 */
static const struct em28xx_reg_seq reddo_dvb_c_usb_box[] = {
	{EM2820_R08_GPIO_CTRL,	0xfe,	0xff,		10},
	{EM2820_R08_GPIO_CTRL,	0xde,	0xff,		10},
	{EM2820_R08_GPIO_CTRL,	0xfe,	0xff,		10},
	{EM2820_R08_GPIO_CTRL,	0xff,	0xff,		10},
	{EM2820_R08_GPIO_CTRL,	0x7f,	0xff,		10},
	{EM2820_R08_GPIO_CTRL,	0x6f,	0xff,		10},
	{EM2820_R08_GPIO_CTRL,	0xff,	0xff,		10},
	{	-1,		-1,	-1,		-1},
};

/* Callback for the most boards */
static const struct em28xx_reg_seq default_tuner_gpio[] = {
	{EM2820_R08_GPIO_CTRL,	EM_GPIO_4,	EM_GPIO_4,	10},
	{EM2820_R08_GPIO_CTRL,	0,		EM_GPIO_4,	10},
	{EM2820_R08_GPIO_CTRL,	EM_GPIO_4,	EM_GPIO_4,	10},
	{	-1,		-1,		-1,		-1},
};

/* Mute/unmute */
static const struct em28xx_reg_seq compro_unmute_tv_gpio[] = {
	{EM2820_R08_GPIO_CTRL,	5,	7,	10},
	{	-1,		-1,	-1,	-1},
};

static const struct em28xx_reg_seq compro_unmute_svid_gpio[] = {
	{EM2820_R08_GPIO_CTRL,	4,	7,	10},
	{	-1,		-1,	-1,	-1},
};

static const struct em28xx_reg_seq compro_mute_gpio[] = {
	{EM2820_R08_GPIO_CTRL,	6,	7,	10},
	{	-1,		-1,	-1,	-1},
};

/* Terratec AV350 */
static const struct em28xx_reg_seq terratec_av350_mute_gpio[] = {
	{EM2820_R08_GPIO_CTRL,	0xff,	0x7f,		10},
	{	-1,		-1,	-1,		-1},
};

static const struct em28xx_reg_seq terratec_av350_unmute_gpio[] = {
	{EM2820_R08_GPIO_CTRL,	0xff,	0xff,		10},
	{	-1,		-1,	-1,		-1},
};

static const struct em28xx_reg_seq silvercrest_reg_seq[] = {
	{EM2820_R08_GPIO_CTRL,	0xff,	0xff,		10},
	{EM2820_R08_GPIO_CTRL,	0x01,	0xf7,		10},
	{	-1,		-1,	-1,		-1},
};

static const struct em28xx_reg_seq vc211a_enable[] = {
	{EM2820_R08_GPIO_CTRL,	0xff,	0x07,		10},
	{EM2820_R08_GPIO_CTRL,	0xff,	0x0f,		10},
	{EM2820_R08_GPIO_CTRL,	0xff,	0x0b,		10},
	{	-1,		-1,	-1,		-1},
};

static const struct em28xx_reg_seq dikom_dk300_digital[] = {
	{EM2820_R08_GPIO_CTRL,	0x6e,	~EM_GPIO_4,	10},
	{EM2880_R04_GPO,	0x08,	0xff,		10},
	{	-1,		-1,	-1,		-1},
};

/* Reset for the most [digital] boards */
static const struct em28xx_reg_seq leadership_digital[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0x70,	0xff,	10},
	{	-1,			-1,	-1,	-1},
};

static const struct em28xx_reg_seq leadership_reset[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0xf0,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0xb0,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0xf0,	0xff,	10},
	{	-1,			-1,	-1,	-1},
};

/*
 * 2013:024f PCTV nanoStick T2 290e
 * GPIO_6 - demod reset
 * GPIO_7 - LED
 */
static const struct em28xx_reg_seq pctv_290e[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0x00,	0xff,	80},
	{EM2874_R80_GPIO_P0_CTRL,	0x40,	0xff,	80}, /* GPIO_6 = 1 */
	{EM2874_R80_GPIO_P0_CTRL,	0xc0,	0xff,	80}, /* GPIO_7 = 1 */
	{	-1,			-1,	-1,	-1},
};

#if 0
static const struct em28xx_reg_seq terratec_h5_gpio[] = {
	{EM2820_R08_GPIO_CTRL,		0xff,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0xf6,	0xff,	100},
	{EM2874_R80_GPIO_P0_CTRL,	0xf2,	0xff,	50},
	{EM2874_R80_GPIO_P0_CTRL,	0xf6,	0xff,	50},
	{	-1,			-1,	-1,	-1},
};

static const struct em28xx_reg_seq terratec_h5_digital[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0xf6,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0xe6,	0xff,	100},
	{EM2874_R80_GPIO_P0_CTRL,	0xa6,	0xff,	10},
	{	-1,			-1,	-1,	-1},
};
#endif

/*
 * 2013:024f PCTV DVB-S2 Stick 460e
 * GPIO_0 - POWER_ON
 * GPIO_1 - BOOST
 * GPIO_2 - VUV_LNB (red LED)
 * GPIO_3 - EXT_12V
 * GPIO_4 - INT_DEM (DEMOD GPIO_0)
 * GPIO_5 - INT_LNB
 * GPIO_6 - RESET_DEM
 * GPIO_7 - LED (green LED)
 */
static const struct em28xx_reg_seq pctv_460e[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0x01,	0xff,	50},
	{	0x0d,			0xff,	0xff,	50},
	{EM2874_R80_GPIO_P0_CTRL,	0x41,	0xff,	50}, /* GPIO_6=1 */
	{	0x0d,			0x42,	0xff,	50},
	{EM2874_R80_GPIO_P0_CTRL,	0x61,	0xff,	50}, /* GPIO_5=1 */
	{	-1,			-1,	-1,	-1},
};

static const struct em28xx_reg_seq c3tech_digital_duo_digital[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0xff,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0xfd,	0xff,	10}, /* xc5000 reset */
	{EM2874_R80_GPIO_P0_CTRL,	0xf9,	0xff,	35},
	{EM2874_R80_GPIO_P0_CTRL,	0xfd,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0xff,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0xfe,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0xbe,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0xfe,	0xff,	20},
	{	-1,			-1,	-1,	-1},
};

/*
 * 2013:0258 PCTV DVB-S2 Stick (461e)
 * GPIO 0 = POWER_ON
 * GPIO 1 = BOOST
 * GPIO 2 = VUV_LNB (red LED)
 * GPIO 3 = #EXT_12V
 * GPIO 4 = INT_DEM
 * GPIO 5 = INT_LNB
 * GPIO 6 = #RESET_DEM
 * GPIO 7 = P07_LED (green LED)
 */
static const struct em28xx_reg_seq pctv_461e[] = {
	{EM2874_R80_GPIO_P0_CTRL,      0x7f, 0xff,    0},
	{0x0d,                 0xff, 0xff,    0},
	{EM2874_R80_GPIO_P0_CTRL,      0x3f, 0xff,  100}, /* reset demod */
	{EM2874_R80_GPIO_P0_CTRL,      0x7f, 0xff,  200}, /* reset demod */
	{0x0d,                 0x42, 0xff,    0},
	{EM2874_R80_GPIO_P0_CTRL,      0xeb, 0xff,    0},
	{EM2874_R5F_TS_ENABLE, 0x84, 0x84,    0}, /* parallel? | null discard */
	{                  -1,   -1,   -1,   -1},
};

#if 0
static const struct em28xx_reg_seq hauppauge_930c_gpio[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0x6f,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0x4f,	0xff,	10}, /* xc5000 reset */
	{EM2874_R80_GPIO_P0_CTRL,	0x6f,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0x4f,	0xff,	10},
	{	-1,			-1,	-1,	-1},
};

static const struct em28xx_reg_seq hauppauge_930c_digital[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0xf6,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0xe6,	0xff,	100},
	{EM2874_R80_GPIO_P0_CTRL,	0xa6,	0xff,	10},
	{	-1,			-1,	-1,	-1},
};
#endif

/*
 * 1b80:e425 MaxMedia UB425-TC
 * 1b80:e1cc Delock 61959
 * GPIO_6 - demod reset, 0=active
 * GPIO_7 - LED, 0=active
 */
static const struct em28xx_reg_seq maxmedia_ub425_tc[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0x83,	0xff,	100},
	{EM2874_R80_GPIO_P0_CTRL,	0xc3,	0xff,	100}, /* GPIO_6 = 1 */
	{EM2874_R80_GPIO_P0_CTRL,	0x43,	0xff,	000}, /* GPIO_7 = 0 */
	{	-1,			-1,	-1,	-1},
};

/*
 * 2304:0242 PCTV QuatroStick (510e)
 * GPIO_2: decoder reset, 0=active
 * GPIO_4: decoder suspend, 0=active
 * GPIO_6: demod reset, 0=active
 * GPIO_7: LED, 1=active
 */
static const struct em28xx_reg_seq pctv_510e[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0x10,	0xff,	100},
	{EM2874_R80_GPIO_P0_CTRL,	0x14,	0xff,	100}, /* GPIO_2 = 1 */
	{EM2874_R80_GPIO_P0_CTRL,	0x54,	0xff,	050}, /* GPIO_6 = 1 */
	{	-1,			-1,	-1,	-1},
};

/*
 * 2013:0251 PCTV QuatroStick nano (520e)
 * GPIO_2: decoder reset, 0=active
 * GPIO_4: decoder suspend, 0=active
 * GPIO_6: demod reset, 0=active
 * GPIO_7: LED, 1=active
 */
static const struct em28xx_reg_seq pctv_520e[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0x10,	0xff,	100},
	{EM2874_R80_GPIO_P0_CTRL,	0x14,	0xff,	100}, /* GPIO_2 = 1 */
	{EM2874_R80_GPIO_P0_CTRL,	0x54,	0xff,	050}, /* GPIO_6 = 1 */
	{EM2874_R80_GPIO_P0_CTRL,	0xd4,	0xff,	000}, /* GPIO_7 = 1 */
	{	-1,			-1,	-1,	-1},
};

/*
 * 1ae7:9003/9004 SpeedLink Vicious And Devine Laplace webcam
 * reg 0x80/0x84:
 * GPIO_0: capturing LED, 0=on, 1=off
 * GPIO_2: AV mute button, 0=pressed, 1=unpressed
 * GPIO 3: illumination button, 0=pressed, 1=unpressed
 * GPIO_6: illumination/flash LED, 0=on, 1=off
 * reg 0x81/0x85:
 * GPIO_7: snapshot button, 0=pressed, 1=unpressed
 */
static const struct em28xx_reg_seq speedlink_vad_laplace_reg_seq[] = {
	{EM2820_R08_GPIO_CTRL,		0xf7,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0xff,	0xb2,	10},
	{	-1,			-1,	-1,	-1},
};

static const struct em28xx_reg_seq pctv_292e[] = {
	{EM2874_R80_GPIO_P0_CTRL,      0xff, 0xff,      0},
	{0x0d,                         0xff, 0xff,    950},
	{EM2874_R80_GPIO_P0_CTRL,      0xbd, 0xff,    100},
	{EM2874_R80_GPIO_P0_CTRL,      0xfd, 0xff,    410},
	{EM2874_R80_GPIO_P0_CTRL,      0x7d, 0xff,    300},
	{EM2874_R80_GPIO_P0_CTRL,      0x7c, 0xff,     60},
	{0x0d,                         0x42, 0xff,     50},
	{EM2874_R5F_TS_ENABLE,         0x85, 0xff,      0},
	{-1,                             -1,   -1,     -1},
};

static const struct em28xx_reg_seq terratec_t2_stick_hd[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0xff,	0xff,	0},
	{0x0d,				0xff,	0xff,	600},
	{EM2874_R80_GPIO_P0_CTRL,	0xfc,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0xbc,	0xff,	100},
	{EM2874_R80_GPIO_P0_CTRL,	0xfc,	0xff,	100},
	{EM2874_R80_GPIO_P0_CTRL,	0x00,	0xff,	300},
	{EM2874_R80_GPIO_P0_CTRL,	0xf8,	0xff,	100},
	{EM2874_R80_GPIO_P0_CTRL,	0xfc,	0xff,	300},
	{0x0d,				0x42,	0xff,	1000},
	{EM2874_R5F_TS_ENABLE,		0x85,	0xff,	0},
	{-1,                             -1,   -1,     -1},
};

static const struct em28xx_reg_seq plex_px_bcud[] = {
	{EM2874_R80_GPIO_P0_CTRL,	0xff,	0xff,	0},
	{0x0d,				0xff,	0xff,	0},
	{EM2874_R50_IR_CONFIG,		0x01,	0xff,	0},
	{EM28XX_R06_I2C_CLK,		0x40,	0xff,	0},
	{EM2874_R80_GPIO_P0_CTRL,	0xfd,	0xff,	100},
	{EM28XX_R12_VINENABLE,		0x20,	0x20,	0},
	{0x0d,				0x42,	0xff,	1000},
	{EM2874_R80_GPIO_P0_CTRL,	0xfc,	0xff,	10},
	{EM2874_R80_GPIO_P0_CTRL,	0xfd,	0xff,	10},
	{0x73,				0xfd,	0xff,	100},
	{-1,				-1,	-1,	-1},
};

/*
 * 2040:0265 Hauppauge WinTV-dualHD DVB Isoc
 * 2040:8265 Hauppauge WinTV-dualHD DVB Bulk
 * 2040:026d Hauppauge WinTV-dualHD ATSC/QAM Isoc
 * 2040:826d Hauppauge WinTV-dualHD ATSC/QAM Bulk
 * reg 0x80/0x84:
 * GPIO_0: Yellow LED tuner 1, 0=on, 1=off
 * GPIO_1: Green LED tuner 1, 0=on, 1=off
 * GPIO_2: Yellow LED tuner 2, 0=on, 1=off
 * GPIO_3: Green LED tuner 2, 0=on, 1=off
 * GPIO_5: Reset #2, 0=active
 * GPIO_6: Reset #1, 0=active
 */
static const struct em28xx_reg_seq hauppauge_dualhd_dvb[] = {
	{EM2874_R80_GPIO_P0_CTRL,      0xff, 0xff,      0},
	{0x0d,                         0xff, 0xff,    200},
	{0x50,                         0x04, 0xff,    300},
	{EM2874_R80_GPIO_P0_CTRL,      0xbf, 0xff,    100}, /* demod 1 reset */
	{EM2874_R80_GPIO_P0_CTRL,      0xff, 0xff,    100},
	{EM2874_R80_GPIO_P0_CTRL,      0xdf, 0xff,    100}, /* demod 2 reset */
	{EM2874_R80_GPIO_P0_CTRL,      0xff, 0xff,    100},
	{EM2874_R5F_TS_ENABLE,         0x00, 0xff,     50}, /* disable TS filters */
	{EM2874_R5D_TS1_PKT_SIZE,      0x05, 0xff,     50},
	{EM2874_R5E_TS2_PKT_SIZE,      0x05, 0xff,     50},
	{-1,                             -1,   -1,     -1},
};

/*
 *  Button definitions
 */
static const struct em28xx_button std_snapshot_button[] = {
	{
		.role         = EM28XX_BUTTON_SNAPSHOT,
		.reg_r        = EM28XX_R0C_USBSUSP,
		.reg_clearing = EM28XX_R0C_USBSUSP,
		.mask         = EM28XX_R0C_USBSUSP_SNAPSHOT,
		.inverted     = 0,
	},
	{-1, 0, 0, 0, 0},
};

static const struct em28xx_button speedlink_vad_laplace_buttons[] = {
	{
		.role     = EM28XX_BUTTON_SNAPSHOT,
		.reg_r    = EM2874_R85_GPIO_P1_STATE,
		.mask     = 0x80,
		.inverted = 1,
	},
	{
		.role     = EM28XX_BUTTON_ILLUMINATION,
		.reg_r    = EM2874_R84_GPIO_P0_STATE,
		.mask     = 0x08,
		.inverted = 1,
	},
	{-1, 0, 0, 0, 0},
};

/*
 *  LED definitions
 */
static struct em28xx_led speedlink_vad_laplace_leds[] = {
	{
		.role      = EM28XX_LED_ANALOG_CAPTURING,
		.gpio_reg  = EM2874_R80_GPIO_P0_CTRL,
		.gpio_mask = 0x01,
		.inverted  = 1,
	},
	{
		.role      = EM28XX_LED_ILLUMINATION,
		.gpio_reg  = EM2874_R80_GPIO_P0_CTRL,
		.gpio_mask = 0x40,
		.inverted  = 1,
	},
	{-1, 0, 0, 0},
};

static struct em28xx_led kworld_ub435q_v3_leds[] = {
	{
		.role      = EM28XX_LED_DIGITAL_CAPTURING,
		.gpio_reg  = EM2874_R80_GPIO_P0_CTRL,
		.gpio_mask = 0x80,
		.inverted  = 1,
	},
	{-1, 0, 0, 0},
};

static struct em28xx_led pctv_80e_leds[] = {
	{
		.role      = EM28XX_LED_DIGITAL_CAPTURING,
		.gpio_reg  = EM2874_R80_GPIO_P0_CTRL,
		.gpio_mask = 0x80,
		.inverted  = 0,
	},
	{-1, 0, 0, 0},
};

static struct em28xx_led terratec_grabby_leds[] = {
	{
		.role      = EM28XX_LED_ANALOG_CAPTURING,
		.gpio_reg  = EM2820_R08_GPIO_CTRL,
		.gpio_mask = EM_GPIO_3,
		.inverted  = 1,
	},
	{-1, 0, 0, 0},
};

static struct em28xx_led hauppauge_dualhd_leds[] = {
	{
		.role      = EM28XX_LED_DIGITAL_CAPTURING,
		.gpio_reg  = EM2874_R80_GPIO_P0_CTRL,
		.gpio_mask = EM_GPIO_1,
		.inverted  = 1,
	},
	{
		.role      = EM28XX_LED_DIGITAL_CAPTURING_TS2,
		.gpio_reg  = EM2874_R80_GPIO_P0_CTRL,
		.gpio_mask = EM_GPIO_3,
		.inverted  = 1,
	},
	{-1, 0, 0, 0},
};

/*
 *  Board definitions
 */
const struct em28xx_board em28xx_boards[] = {
	[EM2750_BOARD_UNKNOWN] = {
		.name          = "EM2710/EM2750/EM2751 webcam grabber",
		.xclk          = EM28XX_XCLK_FREQUENCY_20MHZ,
		.tuner_type    = TUNER_ABSENT,
		.is_webcam     = 1,
		.input         = { {
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
	[EM2882_BOARD_ZOLID_HYBRID_TV_STICK] = {
		.name			= ":ZOLID HYBRID TV STICK",
		.tuner_type		= TUNER_XC2028,
		.tuner_gpio		= zolid_tuner,
		.decoder		= EM28XX_TVP5150,
		.xclk			= EM28XX_XCLK_FREQUENCY_12MHZ,
		.mts_firmware	= 1,
		.has_dvb		= 1,
		.dvb_gpio		= zolid_digital,
	},
	[EM2750_BOARD_DLCW_130] = {
		/* Beijing Huaqi Information Digital Technology Co., Ltd */
		.name          = "Huaqi DLCW-130",
		.valid         = EM28XX_BOARD_NOT_VALIDATED,
		.xclk          = EM28XX_XCLK_FREQUENCY_48MHZ,
		.tuner_type    = TUNER_ABSENT,
		.is_webcam     = 1,
		.input         = { {
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.amux     = EM28XX_AMUX_VIDEO,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
			.vmux     = 0,
			.amux     = EM28XX_AMUX_VIDEO,
		} },
	},
	[EM2820_BOARD_SILVERCREST_WEBCAM] = {
		.name         = "Silvercrest Webcam 1.3mpix",
		.tuner_type   = TUNER_ABSENT,
		.is_webcam    = 1,
		.input        = { {
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type  = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
			.vmux     = 0,
			.amux     = EM28XX_AMUX_VIDEO,
		} },
	},
	[EM2860_BOARD_TYPHOON_DVD_MAKER] = {
		.name         = "Typhoon DVD Maker",
		.decoder      = EM28XX_SAA711X,
		.tuner_type   = TUNER_ABSENT,	/* Capture only device */
		.input        = { {
			.type  = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
		.xclk         = EM28XX_XCLK_IR_RC5_MODE |
				EM28XX_XCLK_FREQUENCY_12MHZ,
		.i2c_speed    = EM28XX_I2C_CLK_WAIT_ENABLE,
		.tuner_type   = TUNER_ABSENT, /* MT2060 */
		.has_dvb      = 1,
		.tuner_gpio   = default_tuner_gpio,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
		.def_i2c_bus  = 1,
		.i2c_speed    = EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
	},
	[EM2884_BOARD_TERRATEC_H6] = {
		.name         = "Terratec Cinergy H6 rev. 2",
		.has_dvb      = 1,
		.ir_codes     = RC_MAP_NEC_TERRATEC_CINERGY_XS,
#if 0
		.tuner_type   = TUNER_PHILIPS_TDA8290,
		.tuner_addr   = 0x41,
		.dvb_gpio     = terratec_h5_digital, /* FIXME: probably wrong */
		.tuner_gpio   = terratec_h5_gpio,
#else
		.tuner_type   = TUNER_ABSENT,
#endif
		.def_i2c_bus  = 1,
		.i2c_speed    = EM28XX_I2C_CLK_WAIT_ENABLE |
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
		.def_i2c_bus  = 1,
		.i2c_speed    = EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
	},
	[EM2884_BOARD_C3TECH_DIGITAL_DUO] = {
		.name         = "C3 Tech Digital Duo HDTV/SDTV USB",
		.has_dvb      = 1,
		/* FIXME: Add analog support - need a saa7136 driver */
		.tuner_type = TUNER_ABSENT,	/* Digital-only TDA18271HD */
		.ir_codes     = RC_MAP_EMPTY,
		.def_i2c_bus  = 1,
		.i2c_speed    = EM28XX_I2C_CLK_WAIT_ENABLE,
		.dvb_gpio     = c3tech_digital_duo_digital,
	},
	[EM2884_BOARD_CINERGY_HTC_STICK] = {
		.name         = "Terratec Cinergy HTC Stick",
		.has_dvb      = 1,
		.ir_codes     = RC_MAP_NEC_TERRATEC_CINERGY_XS,
		.tuner_type   = TUNER_ABSENT,
		.def_i2c_bus  = 1,
		.i2c_speed    = EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
	},
	[EM2884_BOARD_ELGATO_EYETV_HYBRID_2008] = {
		.name         = "Elgato EyeTV Hybrid 2008 INT",
		.has_dvb      = 1,
		.ir_codes     = RC_MAP_NEC_TERRATEC_CINERGY_XS,
		.tuner_type   = TUNER_ABSENT,
		.def_i2c_bus  = 1,
		.i2c_speed    = EM28XX_I2C_CLK_WAIT_ENABLE |
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
	/*
	 * maybe there's a reason behind it why Terratec sells the Hybrid XS
	 * as Prodigy XS with a different PID, let's keep it separated for now
	 * maybe we'll need it later on
	 */
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type      = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2820_BOARD_PINNACLE_DVC_90] = {
		.name	      = "Pinnacle Dazzle DVC 90/100/101/107 / Kaiser Baas Video to DVD maker / Kworld DVD Maker 2 / Plextor ConvertX PX-AV100U",
		.tuner_type   = TUNER_ABSENT, /* capture only board */
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.aout     = EM28XX_AOUT_MONO |	/* I2S */
				    EM28XX_AOUT_MASTER,	/* Line out pin */
		}, {
			.type     = EM28XX_VMUX_COMPOSITE,
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
		.buttons = std_snapshot_button,
		.tda9887_conf = TDA9887_PRESENT,
		.tuner_type   = TUNER_YMEC_TVF_5533MF,
		.tuner_addr   = 0x60,
		.decoder      = EM28XX_SAA711X,
		.input        = { {
			.type     = EM28XX_VMUX_TELEVISION,
			.vmux     = SAA7115_COMPOSITE2,
			.amux     = EM28XX_AMUX_VIDEO,
			.aout     = EM28XX_AOUT_MONO |	/* I2S */
				    EM28XX_AOUT_MASTER,	/* Line out pin */
		}, {
			.type     = EM28XX_VMUX_COMPOSITE,
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
		.buttons = std_snapshot_button,
		.tuner_type          = TUNER_ABSENT,
		.decoder             = EM28XX_SAA711X,
		.input               = { {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
		}, {
			.type     = EM28XX_VMUX_COMPOSITE,
			.vmux     = SAA7115_COMPOSITE0,
		} },
	},

	[EM2874_BOARD_LEADERSHIP_ISDBT] = {
		.def_i2c_bus	= 1,
		.i2c_speed      = EM28XX_I2C_CLK_WAIT_ENABLE |
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
#if 0
		/* FIXME: Analog mode - still not ready */
		.input        = { {
			.type = EM28XX_VMUX_TELEVISION,
			.vmux = SAA7115_COMPOSITE2,
			.amux = EM28XX_AMUX_VIDEO,
			.gpio = em2882_kworld_315u_analog,
			.aout = EM28XX_AOUT_PCM_IN | EM28XX_AOUT_PCM_STEREO,
		}, {
			.type = EM28XX_VMUX_COMPOSITE,
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
		} },
#endif
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
			.type = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	[EM2882_BOARD_TERRATEC_HYBRID_XS] = {
		.name         = "Terratec Cinergy Hybrid T USB XS (em2882)",
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
		.buttons         = std_snapshot_button,
		.leds            = terratec_grabby_leds,
	},
	[EM2860_BOARD_TERRATEC_AV350] = {
		.name            = "Terratec AV350",
		.vchannels       = 2,
		.tuner_type      = TUNER_ABSENT,
		.decoder         = EM28XX_TVP5150,
		.xclk            = EM28XX_XCLK_FREQUENCY_12MHZ,
		.mute_gpio       = terratec_av350_mute_gpio,
		.input           = { {
			.type     = EM28XX_VMUX_COMPOSITE,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = terratec_av350_unmute_gpio,

		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
			.gpio     = terratec_av350_unmute_gpio,
		} },
	},

	[EM2860_BOARD_ELGATO_VIDEO_CAPTURE] = {
		.name         = "Elgato Video Capture",
		.decoder      = EM28XX_SAA711X,
		.tuner_type   = TUNER_ABSENT,   /* Capture only device */
		.input        = { {
			.type  = EM28XX_VMUX_COMPOSITE,
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
			.type     = EM28XX_VMUX_COMPOSITE,
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
	/*
	 * eb1a:2868 Empia EM2870 + Philips CU1216L NIM
	 * (Philips TDA10023 + Infineon TUA6034)
	 */
	[EM2870_BOARD_REDDO_DVB_C_USB_BOX] = {
		.name          = "Reddo DVB-C USB TV Box",
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = reddo_dvb_c_usb_box,
		.has_dvb       = 1,
	},
	/*
	 * 1b80:a340 - Empia EM2870, NXP TDA18271HD and LG DT3304, sold
	 * initially as the KWorld PlusTV 340U, then as the UB435-Q.
	 * Early variants have a TDA18271HD/C1, later ones a TDA18271HD/C2
	 */
	[EM2870_BOARD_KWORLD_A340] = {
		.name       = "KWorld PlusTV 340U or UB435-Q (ATSC)",
		.tuner_type = TUNER_ABSENT,	/* Digital-only TDA18271HD */
		.has_dvb    = 1,
		.dvb_gpio   = kworld_a340_digital,
		.tuner_gpio = default_tuner_gpio,
	},
	/*
	 * 2013:024f PCTV nanoStick T2 290e.
	 * Empia EM28174, Sony CXD2820R and NXP TDA18271HD/C2
	 */
	[EM28174_BOARD_PCTV_290E] = {
		.name          = "PCTV nanoStick T2 290e",
		.def_i2c_bus   = 1,
		.i2c_speed     = EM28XX_I2C_CLK_WAIT_ENABLE |
				 EM28XX_I2C_FREQ_100_KHZ,
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = pctv_290e,
		.has_dvb       = 1,
		.ir_codes      = RC_MAP_PINNACLE_PCTV_HD,
	},
	/*
	 * 2013:024f PCTV DVB-S2 Stick 460e
	 * Empia EM28174, NXP TDA10071, Conexant CX24118A and Allegro A8293
	 */
	[EM28174_BOARD_PCTV_460E] = {
		.def_i2c_bus   = 1,
		.i2c_speed     = EM28XX_I2C_CLK_WAIT_ENABLE |
				 EM28XX_I2C_FREQ_400_KHZ,
		.name          = "PCTV DVB-S2 Stick (460e)",
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = pctv_460e,
		.has_dvb       = 1,
		.ir_codes      = RC_MAP_PINNACLE_PCTV_HD,
	},
	/*
	 * eb1a:5006 Honestech VIDBOX NW03
	 * Empia EM2860, Philips SAA7113, Empia EMP202, No Tuner
	 */
	[EM2860_BOARD_HT_VIDBOX_NW03] = {
		.name                = "Honestech Vidbox NW03",
		.tuner_type          = TUNER_ABSENT,
		.decoder             = EM28XX_SAA711X,
		.input               = { {
			.type     = EM28XX_VMUX_COMPOSITE,
			.vmux     = SAA7115_COMPOSITE0,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = SAA7115_SVIDEO3,  /* S-VIDEO needs check */
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	/*
	 * 1b80:e425 MaxMedia UB425-TC
	 * Empia EM2874B + Micronas DRX 3913KA2 + NXP TDA18271HDC2
	 */
	[EM2874_BOARD_MAXMEDIA_UB425_TC] = {
		.name          = "MaxMedia UB425-TC",
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = maxmedia_ub425_tc,
		.has_dvb       = 1,
		.ir_codes      = RC_MAP_REDDO,
		.def_i2c_bus   = 1,
		.i2c_speed     = EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
	},
	/*
	 * 2304:0242 PCTV QuatroStick (510e)
	 * Empia EM2884 + Micronas DRX 3926K + NXP TDA18271HDC2
	 */
	[EM2884_BOARD_PCTV_510E] = {
		.name          = "PCTV QuatroStick (510e)",
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = pctv_510e,
		.has_dvb       = 1,
		.ir_codes      = RC_MAP_PINNACLE_PCTV_HD,
		.def_i2c_bus   = 1,
		.i2c_speed     = EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
	},
	/*
	 * 2013:0251 PCTV QuatroStick nano (520e)
	 * Empia EM2884 + Micronas DRX 3926K + NXP TDA18271HDC2
	 */
	[EM2884_BOARD_PCTV_520E] = {
		.name          = "PCTV QuatroStick nano (520e)",
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = pctv_520e,
		.has_dvb       = 1,
		.ir_codes      = RC_MAP_PINNACLE_PCTV_HD,
		.def_i2c_bus   = 1,
		.i2c_speed     = EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
	},
	[EM2884_BOARD_TERRATEC_HTC_USB_XS] = {
		.name         = "Terratec Cinergy HTC USB XS",
		.has_dvb      = 1,
		.ir_codes     = RC_MAP_NEC_TERRATEC_CINERGY_XS,
		.tuner_type   = TUNER_ABSENT,
		.def_i2c_bus  = 1,
		.i2c_speed    = EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
	},
	/*
	 * 1b80:e1cc Delock 61959
	 * Empia EM2874B + Micronas DRX 3913KA2 + NXP TDA18271HDC2
	 * mostly the same as MaxMedia UB-425-TC but different remote
	 */
	[EM2874_BOARD_DELOCK_61959] = {
		.name          = "Delock 61959",
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = maxmedia_ub425_tc,
		.has_dvb       = 1,
		.ir_codes      = RC_MAP_DELOCK_61959,
		.def_i2c_bus   = 1,
		.i2c_speed     = EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_400_KHZ,
	},
	/*
	 * 1b80:e346 KWorld USB ATSC TV Stick UB435-Q V2
	 * Empia EM2874B + LG DT3305 + NXP TDA18271HDC2
	 */
	[EM2874_BOARD_KWORLD_UB435Q_V2] = {
		.name		= "KWorld USB ATSC TV Stick UB435-Q V2",
		.tuner_type	= TUNER_ABSENT,
		.has_dvb	= 1,
		.dvb_gpio	= kworld_a340_digital,
		.tuner_gpio	= default_tuner_gpio,
		.def_i2c_bus	= 1,
	},
	/*
	 * 1b80:e34c KWorld USB ATSC TV Stick UB435-Q V3
	 * Empia EM2874B + LG DT3305 + NXP TDA18271HDC2
	 */
	[EM2874_BOARD_KWORLD_UB435Q_V3] = {
		.name		= "KWorld USB ATSC TV Stick UB435-Q V3",
		.tuner_type	= TUNER_ABSENT,
		.has_dvb	= 1,
		.tuner_gpio	= kworld_ub435q_v3_digital,
		.def_i2c_bus	= 1,
		.i2c_speed      = EM28XX_I2C_CLK_WAIT_ENABLE |
				  EM28XX_I2C_FREQ_100_KHZ,
		.leds = kworld_ub435q_v3_leds,
	},
	[EM2874_BOARD_PCTV_HD_MINI_80E] = {
		.name         = "Pinnacle PCTV HD Mini",
		.tuner_type   = TUNER_ABSENT,
		.has_dvb      = 1,
		.dvb_gpio     = em2874_pctv_80e_digital,
		.decoder      = EM28XX_NODECODER,
		.ir_codes     = RC_MAP_PINNACLE_PCTV_HD,
		.leds         = pctv_80e_leds,
	},
	/*
	 * 1ae7:9003/9004 SpeedLink Vicious And Devine Laplace webcam
	 * Empia EM2765 + OmniVision OV2640
	 */
	[EM2765_BOARD_SPEEDLINK_VAD_LAPLACE] = {
		.name         = "SpeedLink Vicious And Devine Laplace webcam",
		.xclk         = EM28XX_XCLK_FREQUENCY_24MHZ,
		.i2c_speed    = EM28XX_I2C_CLK_WAIT_ENABLE |
				EM28XX_I2C_FREQ_100_KHZ,
		.def_i2c_bus  = 1,
		.tuner_type   = TUNER_ABSENT,
		.is_webcam    = 1,
		.input        = { {
			.type     = EM28XX_VMUX_COMPOSITE,
			.amux     = EM28XX_AMUX_VIDEO,
			.gpio     = speedlink_vad_laplace_reg_seq,
		} },
		.buttons = speedlink_vad_laplace_buttons,
		.leds = speedlink_vad_laplace_leds,
	},
	/*
	 * 2013:0258 PCTV DVB-S2 Stick (461e)
	 * Empia EM28178, Montage M88DS3103, Montage M88TS2022, Allegro A8293
	 */
	[EM28178_BOARD_PCTV_461E] = {
		.def_i2c_bus   = 1,
		.i2c_speed     = EM28XX_I2C_CLK_WAIT_ENABLE |
				 EM28XX_I2C_FREQ_400_KHZ,
		.name          = "PCTV DVB-S2 Stick (461e)",
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = pctv_461e,
		.has_dvb       = 1,
		.ir_codes      = RC_MAP_PINNACLE_PCTV_HD,
	},
	/*
	 * 2013:025f PCTV tripleStick (292e).
	 * Empia EM28178, Silicon Labs Si2168, Silicon Labs Si2157
	 */
	[EM28178_BOARD_PCTV_292E] = {
		.name          = "PCTV tripleStick (292e)",
		.def_i2c_bus   = 1,
		.i2c_speed     = EM28XX_I2C_CLK_WAIT_ENABLE |
				 EM28XX_I2C_FREQ_400_KHZ,
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = pctv_292e,
		.has_dvb       = 1,
		.ir_codes      = RC_MAP_PINNACLE_PCTV_HD,
	},
	[EM2861_BOARD_LEADTEK_VC100] = {
		.name          = "Leadtek VC100",
		.tuner_type    = TUNER_ABSENT,	/* Capture only device */
		.decoder       = EM28XX_TVP5150,
		.input         = { {
			.type     = EM28XX_VMUX_COMPOSITE,
			.vmux     = TVP5150_COMPOSITE1,
			.amux     = EM28XX_AMUX_LINE_IN,
		}, {
			.type     = EM28XX_VMUX_SVIDEO,
			.vmux     = TVP5150_SVIDEO,
			.amux     = EM28XX_AMUX_LINE_IN,
		} },
	},
	/*
	 * eb1a:8179 Terratec Cinergy T2 Stick HD.
	 * Empia EM28178, Silicon Labs Si2168, Silicon Labs Si2146
	 */
	[EM28178_BOARD_TERRATEC_T2_STICK_HD] = {
		.name          = "Terratec Cinergy T2 Stick HD",
		.def_i2c_bus   = 1,
		.i2c_speed     = EM28XX_I2C_CLK_WAIT_ENABLE |
				 EM28XX_I2C_FREQ_400_KHZ,
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = terratec_t2_stick_hd,
		.has_dvb       = 1,
		.ir_codes      = RC_MAP_TERRATEC_SLIM_2,
	},

	/*
	 * 3275:0085 PLEX PX-BCUD.
	 * Empia EM28178, TOSHIBA TC90532XBG, Sharp QM1D1C0042
	 */
	[EM28178_BOARD_PLEX_PX_BCUD] = {
		.name          = "PLEX PX-BCUD",
		.xclk          = EM28XX_XCLK_FREQUENCY_4_3MHZ,
		.def_i2c_bus   = 1,
		.i2c_speed     = EM28XX_I2C_CLK_WAIT_ENABLE,
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = plex_px_bcud,
		.has_dvb       = 1,
	},
	/*
	 * 2040:0265 Hauppauge WinTV-dualHD (DVB version) Isoc.
	 * 2040:8265 Hauppauge WinTV-dualHD (DVB version) Bulk.
	 * Empia EM28274, 2x Silicon Labs Si2168, 2x Silicon Labs Si2157
	 */
	[EM28174_BOARD_HAUPPAUGE_WINTV_DUALHD_DVB] = {
		.name          = "Hauppauge WinTV-dualHD DVB",
		.def_i2c_bus   = 1,
		.i2c_speed     = EM28XX_I2C_CLK_WAIT_ENABLE |
				 EM28XX_I2C_FREQ_400_KHZ,
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = hauppauge_dualhd_dvb,
		.has_dvb       = 1,
		.has_dual_ts   = 1,
		.ir_codes      = RC_MAP_HAUPPAUGE,
		.leds          = hauppauge_dualhd_leds,
	},
	/*
	 * 2040:026d Hauppauge WinTV-dualHD (model 01595 - ATSC/QAM) Isoc.
	 * 2040:826d Hauppauge WinTV-dualHD (model 01595 - ATSC/QAM) Bulk.
	 * Empia EM28274, 2x LG LGDT3306A, 2x Silicon Labs Si2157
	 */
	[EM28174_BOARD_HAUPPAUGE_WINTV_DUALHD_01595] = {
		.name          = "Hauppauge WinTV-dualHD 01595 ATSC/QAM",
		.def_i2c_bus   = 1,
		.i2c_speed     = EM28XX_I2C_CLK_WAIT_ENABLE |
				 EM28XX_I2C_FREQ_400_KHZ,
		.tuner_type    = TUNER_ABSENT,
		.tuner_gpio    = hauppauge_dualhd_dvb,
		.has_dvb       = 1,
		.has_dual_ts   = 1,
		.ir_codes      = RC_MAP_HAUPPAUGE,
		.leds          = hauppauge_dualhd_leds,
	},
};
EXPORT_SYMBOL_GPL(em28xx_boards);

static const unsigned int em28xx_bcount = ARRAY_SIZE(em28xx_boards);

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
	{ USB_DEVICE(0xeb1a, 0x2883), /* used by :Zolid Hybrid Tv Stick */
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2868),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2875),
			.driver_info = EM2820_BOARD_UNKNOWN },
	{ USB_DEVICE(0xeb1a, 0x2885), /* MSI Digivox Trio */
			.driver_info = EM2884_BOARD_TERRATEC_H5 },
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
	{ USB_DEVICE(0x1b80, 0xe302), /* Kaiser Baas Video to DVD maker */
			.driver_info = EM2820_BOARD_PINNACLE_DVC_90 },
	{ USB_DEVICE(0x1b80, 0xe304), /* Kworld DVD Maker 2 */
			.driver_info = EM2820_BOARD_PINNACLE_DVC_90 },
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
			.driver_info = EM2870_BOARD_TERRATEC_XS_MT2060 },
	{ USB_DEVICE(0x0ccd, 0x008e),	/* Cinergy HTC USB XS Rev. 1 */
			.driver_info = EM2884_BOARD_TERRATEC_HTC_USB_XS },
	{ USB_DEVICE(0x0ccd, 0x00ac),	/* Cinergy HTC USB XS Rev. 2 */
			.driver_info = EM2884_BOARD_TERRATEC_HTC_USB_XS },
	{ USB_DEVICE(0x0ccd, 0x10a2),	/* H5 Rev. 1 */
			.driver_info = EM2884_BOARD_TERRATEC_H5 },
	{ USB_DEVICE(0x0ccd, 0x10ad),	/* H5 Rev. 2 */
			.driver_info = EM2884_BOARD_TERRATEC_H5 },
	{ USB_DEVICE(0x0ccd, 0x10b6),	/* H5 Rev. 3 */
			.driver_info = EM2884_BOARD_TERRATEC_H5 },
	{ USB_DEVICE(0x0ccd, 0x10b2),	/* H6 */
			.driver_info = EM2884_BOARD_TERRATEC_H6 },
	{ USB_DEVICE(0x0ccd, 0x0084),
			.driver_info = EM2860_BOARD_TERRATEC_AV350 },
	{ USB_DEVICE(0x0ccd, 0x0096),
			.driver_info = EM2860_BOARD_TERRATEC_GRABBY },
	{ USB_DEVICE(0x0ccd, 0x10AF),
			.driver_info = EM2860_BOARD_TERRATEC_GRABBY },
	{ USB_DEVICE(0x0ccd, 0x00b2),
			.driver_info = EM2884_BOARD_CINERGY_HTC_STICK },
	{ USB_DEVICE(0x0fd9, 0x0018),
			.driver_info = EM2884_BOARD_ELGATO_EYETV_HYBRID_2008 },
	{ USB_DEVICE(0x0fd9, 0x0033),
			.driver_info = EM2860_BOARD_ELGATO_VIDEO_CAPTURE },
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
	{ USB_DEVICE(0x2040, 0x0265),
			.driver_info = EM28174_BOARD_HAUPPAUGE_WINTV_DUALHD_DVB },
	{ USB_DEVICE(0x2040, 0x8265),
			.driver_info = EM28174_BOARD_HAUPPAUGE_WINTV_DUALHD_DVB },
	{ USB_DEVICE(0x2040, 0x026d),
			.driver_info = EM28174_BOARD_HAUPPAUGE_WINTV_DUALHD_01595 },
	{ USB_DEVICE(0x2040, 0x826d),
			.driver_info = EM28174_BOARD_HAUPPAUGE_WINTV_DUALHD_01595 },
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
	{ USB_DEVICE(0x2304, 0x023f),
			.driver_info = EM2874_BOARD_PCTV_HD_MINI_80E },
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
	{ USB_DEVICE(0x1b80, 0xe346),
			.driver_info = EM2874_BOARD_KWORLD_UB435Q_V2 },
	{ USB_DEVICE(0x1b80, 0xe34c),
			.driver_info = EM2874_BOARD_KWORLD_UB435Q_V3 },
	{ USB_DEVICE(0x2013, 0x024f),
			.driver_info = EM28174_BOARD_PCTV_290E },
	{ USB_DEVICE(0x2013, 0x024c),
			.driver_info = EM28174_BOARD_PCTV_460E },
	{ USB_DEVICE(0x2040, 0x1605),
			.driver_info = EM2884_BOARD_HAUPPAUGE_WINTV_HVR_930C },
	{ USB_DEVICE(0x1b80, 0xe755),
			.driver_info = EM2884_BOARD_C3TECH_DIGITAL_DUO },
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
	{ USB_DEVICE(0x1b80, 0xe1cc),
			.driver_info = EM2874_BOARD_DELOCK_61959 },
	{ USB_DEVICE(0x1ae7, 0x9003),
			.driver_info = EM2765_BOARD_SPEEDLINK_VAD_LAPLACE },
	{ USB_DEVICE(0x1ae7, 0x9004),
			.driver_info = EM2765_BOARD_SPEEDLINK_VAD_LAPLACE },
	{ USB_DEVICE(0x2013, 0x0258),
			.driver_info = EM28178_BOARD_PCTV_461E },
	{ USB_DEVICE(0x2013, 0x025f),
			.driver_info = EM28178_BOARD_PCTV_292E },
	{ USB_DEVICE(0x2013, 0x0264), /* Hauppauge WinTV-soloHD 292e SE */
			.driver_info = EM28178_BOARD_PCTV_292E },
	{ USB_DEVICE(0x2040, 0x0264), /* Hauppauge WinTV-soloHD Isoc */
			.driver_info = EM28178_BOARD_PCTV_292E },
	{ USB_DEVICE(0x2040, 0x8264), /* Hauppauge OEM Generic WinTV-soloHD Bulk */
			.driver_info = EM28178_BOARD_PCTV_292E },
	{ USB_DEVICE(0x2040, 0x8268), /* Hauppauge Retail WinTV-soloHD Bulk */
			.driver_info = EM28178_BOARD_PCTV_292E },
	{ USB_DEVICE(0x0413, 0x6f07),
			.driver_info = EM2861_BOARD_LEADTEK_VC100 },
	{ USB_DEVICE(0xeb1a, 0x8179),
			.driver_info = EM28178_BOARD_TERRATEC_T2_STICK_HD },
	{ USB_DEVICE(0x3275, 0x0085),
			.driver_info = EM28178_BOARD_PLEX_PX_BCUD },
	{ USB_DEVICE(0xeb1a, 0x5051), /* Ion Video 2 PC MKII / Startech svid2usb23 / Raygo R12-41373 */
			.driver_info = EM2860_BOARD_TVP5150_REFERENCE_DESIGN },
	{ },
};
MODULE_DEVICE_TABLE(usb, em28xx_id_table);

/*
 * EEPROM hash table for devices with generic USB IDs
 */
static const struct em28xx_hash_table em28xx_eeprom_hash[] = {
	/* P/N: SA 60002070465 Tuner: TVF7533-MF */
	{0x6ce05a8f, EM2820_BOARD_PROLINK_PLAYTV_USB2, TUNER_YMEC_TVF_5533MF},
	{0x72cc5a8b, EM2820_BOARD_PROLINK_PLAYTV_BOX4_USB2, TUNER_YMEC_TVF_5533MF},
	{0x966a0441, EM2880_BOARD_KWORLD_DVB_310U, TUNER_XC2028},
	{0x166a0441, EM2880_BOARD_EMPIRE_DUAL_TV, TUNER_XC2028},
	{0xcee44a99, EM2882_BOARD_EVGA_INDTUBE, TUNER_XC2028},
	{0xb8846b20, EM2881_BOARD_PINNACLE_HYBRID_PRO, TUNER_XC2028},
	{0x63f653bd, EM2870_BOARD_REDDO_DVB_C_USB_BOX, TUNER_ABSENT},
	{0x4e913442, EM2882_BOARD_DIKOM_DK300, TUNER_XC2028},
	{0x85dd871e, EM2882_BOARD_ZOLID_HYBRID_TV_STICK, TUNER_XC2028},
};

/* I2C devicelist hash table for devices with generic USB IDs */
static const struct em28xx_hash_table em28xx_i2c_hash[] = {
	{0xb06a32c3, EM2800_BOARD_TERRATEC_CINERGY_200, TUNER_LG_PAL_NEW_TAPC},
	{0xf51200e3, EM2800_BOARD_VGEAR_POCKETTV, TUNER_LG_PAL_NEW_TAPC},
	{0x1ba50080, EM2860_BOARD_SAA711X_REFERENCE_DESIGN, TUNER_ABSENT},
	{0x77800080, EM2860_BOARD_TVP5150_REFERENCE_DESIGN, TUNER_ABSENT},
	{0xc51200e3, EM2820_BOARD_GADMEI_TVR200, TUNER_LG_PAL_NEW_TAPC},
	{0x4ba50080, EM2861_BOARD_GADMEI_UTV330PLUS, TUNER_TNF_5335MF},
	{0x6b800080, EM2874_BOARD_LEADERSHIP_ISDBT, TUNER_ABSENT},
	{0x27e10080, EM2882_BOARD_ZOLID_HYBRID_TV_STICK, TUNER_XC2028},
};

/* NOTE: introduce a separate hash table for devices with 16 bit eeproms */

int em28xx_tuner_callback(void *ptr, int component, int command, int arg)
{
	struct em28xx_i2c_bus *i2c_bus = ptr;
	struct em28xx *dev = i2c_bus->dev;
	int rc = 0;

	if (dev->tuner_type != TUNER_XC2028 && dev->tuner_type != TUNER_XC5000)
		return 0;

	if (command != XC2028_TUNER_RESET && command != XC5000_TUNER_RESET)
		return 0;

	rc = em28xx_gpio_set(dev, dev->board.tuner_gpio);

	return rc;
}
EXPORT_SYMBOL_GPL(em28xx_tuner_callback);

static inline void em28xx_set_xclk_i2c_speed(struct em28xx *dev)
{
	const struct em28xx_board *board = &em28xx_boards[dev->model];
	u8 xclk = board->xclk, i2c_speed = board->i2c_speed;

	/*
	 * Those are the default values for the majority of boards
	 * Use those values if not specified otherwise at boards entry
	 */
	if (!xclk)
		xclk = EM28XX_XCLK_IR_RC5_MODE |
		       EM28XX_XCLK_FREQUENCY_12MHZ;

	em28xx_write_reg(dev, EM28XX_R0F_XCLK, xclk);

	if (!i2c_speed)
		i2c_speed = EM28XX_I2C_CLK_WAIT_ENABLE |
			    EM28XX_I2C_FREQ_100_KHZ;

	dev->i2c_speed = i2c_speed & 0x03;

	if (!dev->board.is_em2800)
		em28xx_write_reg(dev, EM28XX_R06_I2C_CLK, i2c_speed);
	msleep(50);
}

static inline void em28xx_set_model(struct em28xx *dev)
{
	dev->board = em28xx_boards[dev->model];
	dev->has_msp34xx = dev->board.has_msp34xx;
	dev->is_webcam = dev->board.is_webcam;

	em28xx_set_xclk_i2c_speed(dev);

	/* Should be initialized early, for I2C to work */
	dev->def_i2c_bus = dev->board.def_i2c_bus;
}

/*
 * Wait until AC97_RESET reports the expected value reliably before proceeding.
 * We also check that two unrelated registers accesses don't return the same
 * value to avoid premature return.
 * This procedure helps ensuring AC97 register accesses are reliable.
 */
static int em28xx_wait_until_ac97_features_equals(struct em28xx *dev,
						  int expected_feat)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(2000);
	int feat, powerdown;

	while (time_is_after_jiffies(timeout)) {
		feat = em28xx_read_ac97(dev, AC97_RESET);
		if (feat < 0)
			return feat;

		powerdown = em28xx_read_ac97(dev, AC97_POWERDOWN);
		if (powerdown < 0)
			return powerdown;

		if (feat == expected_feat && feat != powerdown)
			return 0;

		msleep(50);
	}

	dev_warn(&dev->intf->dev, "AC97 registers access is not reliable !\n");
	return -ETIMEDOUT;
}

/*
 * Since em28xx_pre_card_setup() requires a proper dev->model,
 * this won't work for boards with generic PCI IDs
 */
static void em28xx_pre_card_setup(struct em28xx *dev)
{
	/*
	 * Set the initial XCLK and I2C clock values based on the board
	 * definition
	 */
	em28xx_set_xclk_i2c_speed(dev);

	/* request some modules */
	switch (dev->model) {
	case EM2861_BOARD_PLEXTOR_PX_TV100U:
		/* Sets the msp34xx I2S speed */
		dev->i2s_speed = 2048000;
		break;
	case EM2861_BOARD_KWORLD_PVRTV_300U:
	case EM2880_BOARD_KWORLD_DVB_305U:
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0x6d);
		usleep_range(10000, 11000);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0x7d);
		usleep_range(10000, 11000);
		break;
	case EM2870_BOARD_COMPRO_VIDEOMATE:
		/*
		 * TODO: someone can do some cleanup here...
		 *	 not everything's needed
		 */
		em28xx_write_reg(dev, EM2880_R04_GPO, 0x00);
		usleep_range(10000, 11000);
		em28xx_write_reg(dev, EM2880_R04_GPO, 0x01);
		usleep_range(10000, 11000);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xfd);
		msleep(70);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xfc);
		msleep(70);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xdc);
		msleep(70);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xfc);
		msleep(70);
		break;
	case EM2870_BOARD_TERRATEC_XS_MT2060:
		/*
		 * this device needs some gpio writes to get the DVB-T
		 * demod work
		 */
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xfe);
		msleep(70);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xde);
		msleep(70);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xfe);
		msleep(70);
		break;
	case EM2870_BOARD_PINNACLE_PCTV_DVB:
		/*
		 * this device needs some gpio writes to get the
		 * DVB-T demod work
		 */
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xfe);
		msleep(70);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xde);
		msleep(70);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xfe);
		msleep(70);
		break;
	case EM2820_BOARD_GADMEI_UTV310:
	case EM2820_BOARD_MSI_VOX_USB_2:
		/* enables audio for that devices */
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xfd);
		break;

	case EM2882_BOARD_KWORLD_ATSC_315U:
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xff);
		usleep_range(10000, 11000);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xfe);
		usleep_range(10000, 11000);
		em28xx_write_reg(dev, EM2880_R04_GPO, 0x00);
		usleep_range(10000, 11000);
		em28xx_write_reg(dev, EM2880_R04_GPO, 0x08);
		usleep_range(10000, 11000);
		break;

	case EM2860_BOARD_KAIOMY_TVNPC_U2:
		em28xx_write_regs(dev, EM28XX_R0F_XCLK, "\x07", 1);
		em28xx_write_regs(dev, EM28XX_R06_I2C_CLK, "\x40", 1);
		em28xx_write_regs(dev, 0x0d, "\x42", 1);
		em28xx_write_regs(dev, 0x08, "\xfd", 1);
		usleep_range(10000, 11000);
		em28xx_write_regs(dev, 0x08, "\xff", 1);
		usleep_range(10000, 11000);
		em28xx_write_regs(dev, 0x08, "\x7f", 1);
		usleep_range(10000, 11000);
		em28xx_write_regs(dev, 0x08, "\x6b", 1);

		break;
	case EM2860_BOARD_EASYCAP:
		em28xx_write_regs(dev, 0x08, "\xf8", 1);
		break;

	case EM2820_BOARD_IODATA_GVMVP_SZ:
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xff);
		msleep(70);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xf7);
		usleep_range(10000, 11000);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xfe);
		msleep(70);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xfd);
		msleep(70);
		break;

	case EM2860_BOARD_TERRATEC_GRABBY:
		/*
		 * HACK?: Ensure AC97 register reading is reliable before
		 * proceeding. In practice, this will wait about 1.6 seconds.
		 */
		em28xx_wait_until_ac97_features_equals(dev, 0x6a90);
		break;
	}

	em28xx_gpio_set(dev, dev->board.tuner_gpio);
	em28xx_set_mode(dev, EM28XX_ANALOG_MODE);

	/* Unlock device */
	em28xx_set_mode(dev, EM28XX_SUSPEND);
}

static int em28xx_hint_board(struct em28xx *dev)
{
	int i;

	if (dev->is_webcam) {
		if (dev->em28xx_sensor == EM28XX_MT9V011) {
			dev->model = EM2820_BOARD_SILVERCREST_WEBCAM;
		} else if (dev->em28xx_sensor == EM28XX_MT9M001 ||
			   dev->em28xx_sensor == EM28XX_MT9M111) {
			dev->model = EM2750_BOARD_UNKNOWN;
		}
		/* FIXME: IMPROVE ! */

		return 0;
	}

	/*
	 * HINT method: EEPROM
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

			dev_err(&dev->intf->dev,
				"Your board has no unique USB ID.\n"
				"A hint were successfully done, based on eeprom hash.\n"
				"This method is not 100%% failproof.\n"
				"If the board were misdetected, please email this log to:\n"
				"\tV4L Mailing List  <linux-media@vger.kernel.org>\n"
				"Board detected as %s\n",
			       em28xx_boards[dev->model].name);

			return 0;
		}
	}

	/*
	 * HINT method: I2C attached devices
	 *
	 * This method works for all boards.
	 * Uses a hash of i2c scanned devices.
	 * Devices with the same i2c attached chips will
	 * be considered equal.
	 * This method is less precise than the eeprom one.
	 */

	/* user did not request i2c scanning => do it now */
	if (!dev->i2c_hash)
		em28xx_do_i2c_scan(dev, dev->def_i2c_bus);

	for (i = 0; i < ARRAY_SIZE(em28xx_i2c_hash); i++) {
		if (dev->i2c_hash == em28xx_i2c_hash[i].hash) {
			dev->model = em28xx_i2c_hash[i].model;
			dev->tuner_type = em28xx_i2c_hash[i].tuner;
			dev_err(&dev->intf->dev,
				"Your board has no unique USB ID.\n"
				"A hint were successfully done, based on i2c devicelist hash.\n"
				"This method is not 100%% failproof.\n"
				"If the board were misdetected, please email this log to:\n"
				"\tV4L Mailing List  <linux-media@vger.kernel.org>\n"
				"Board detected as %s\n",
				em28xx_boards[dev->model].name);

			return 0;
		}
	}

	dev_err(&dev->intf->dev,
		"Your board has no unique USB ID and thus need a hint to be detected.\n"
		"You may try to use card=<n> insmod option to workaround that.\n"
		"Please send an email with this log to:\n"
		"\tV4L Mailing List <linux-media@vger.kernel.org>\n"
		"Board eeprom hash is 0x%08lx\n"
		"Board i2c devicelist hash is 0x%08lx\n",
		dev->hash, dev->i2c_hash);

	dev_err(&dev->intf->dev,
		"Here is a list of valid choices for the card=<n> insmod option:\n");
	for (i = 0; i < em28xx_bcount; i++) {
		dev_err(&dev->intf->dev,
			"    card=%d -> %s\n", i, em28xx_boards[i].name);
	}
	return -1;
}

static void em28xx_card_setup(struct em28xx *dev)
{
	int i, j, idx;
	bool duplicate_entry;

	/*
	 * If the device can be a webcam, seek for a sensor.
	 * If sensor is not found, then it isn't a webcam.
	 */
	if (dev->is_webcam) {
		em28xx_detect_sensor(dev);
		if (dev->em28xx_sensor == EM28XX_NOSENSOR)
			/* NOTE: error/unknown sensor/no sensor */
			dev->is_webcam = 0;
	}

	switch (dev->model) {
	case EM2750_BOARD_UNKNOWN:
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
		if (em28xx_hint_board(dev) < 0) {
			dev_err(&dev->intf->dev, "Board not discovered\n");
		} else {
			em28xx_set_model(dev);
			em28xx_pre_card_setup(dev);
		}
		break;
	default:
		em28xx_set_model(dev);
	}

	dev_info(&dev->intf->dev, "Identified as %s (card=%d)\n",
		 dev->board.name, dev->model);

	dev->tuner_type = em28xx_boards[dev->model].tuner_type;

	/* request some modules */
	switch (dev->model) {
	case EM2820_BOARD_HAUPPAUGE_WINTV_USB_2:
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900:
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900_R2:
	case EM2883_BOARD_HAUPPAUGE_WINTV_HVR_850:
	case EM2883_BOARD_HAUPPAUGE_WINTV_HVR_950:
	case EM2884_BOARD_HAUPPAUGE_WINTV_HVR_930C:
	case EM28174_BOARD_HAUPPAUGE_WINTV_DUALHD_DVB:
	case EM28174_BOARD_HAUPPAUGE_WINTV_DUALHD_01595:
	{
		struct tveeprom tv;

		if (!dev->eedata)
			break;
#if defined(CONFIG_MODULES) && defined(MODULE)
		request_module("tveeprom");
#endif
		/* Call first TVeeprom */

		tveeprom_hauppauge_analog(&tv, dev->eedata);

		dev->tuner_type = tv.tuner_type;

		if (tv.audio_processor == TVEEPROM_AUDPROC_MSP) {
			dev->i2s_speed = 2048000;
			dev->has_msp34xx = 1;
		}
		break;
	}
	case EM2882_BOARD_KWORLD_ATSC_315U:
		em28xx_write_reg(dev, 0x0d, 0x42);
		usleep_range(10000, 11000);
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xfd);
		usleep_range(10000, 11000);
		break;
	case EM2820_BOARD_KWORLD_PVRTV2800RF:
		/* GPIO enables sound on KWORLD PVR TV 2800RF */
		em28xx_write_reg(dev, EM2820_R08_GPIO_CTRL, 0xf9);
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

		/*
		 * In cases where we had to use a board hint, the call to
		 * em28xx_set_mode() in em28xx_pre_card_setup() was a no-op,
		 * so make the call now so the analog GPIOs are set properly
		 * before probing the i2c bus.
		 */
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

		/*
		 * In cases where we had to use a board hint, the call to
		 * em28xx_set_mode() in em28xx_pre_card_setup() was a no-op,
		 * so make the call now so the analog GPIOs are set properly
		 * before probing the i2c bus.
		 */
		em28xx_gpio_set(dev, dev->board.tuner_gpio);
		em28xx_set_mode(dev, EM28XX_ANALOG_MODE);
		break;
	}

	if (dev->board.valid == EM28XX_BOARD_NOT_VALIDATED) {
		dev_err(&dev->intf->dev,
			"\n\n"
			"The support for this board weren't valid yet.\n"
			"Please send a report of having this working\n"
			"not to V4L mailing list (and/or to other addresses)\n\n");
	}

	/* Free eeprom data memory */
	kfree(dev->eedata);
	dev->eedata = NULL;

	/* Allow override tuner type by a module parameter */
	if (tuner >= 0)
		dev->tuner_type = tuner;

	/*
	 * Dynamically generate a list of valid audio inputs for this
	 * specific board, mapping them via enum em28xx_amux.
	 */

	idx = 0;
	for (i = 0; i < MAX_EM28XX_INPUT; i++) {
		if (!INPUT(i)->type)
			continue;

		/* Skip already mapped audio inputs */
		duplicate_entry = false;
		for (j = 0; j < idx; j++) {
			if (INPUT(i)->amux == dev->amux_map[j]) {
				duplicate_entry = true;
				break;
			}
		}
		if (duplicate_entry)
			continue;

		dev->amux_map[idx++] = INPUT(i)->amux;
	}
	for (; idx < MAX_EM28XX_INPUT; idx++)
		dev->amux_map[idx] = EM28XX_AMUX_UNUSED;
}

void em28xx_setup_xc3028(struct em28xx *dev, struct xc2028_ctrl *ctl)
{
	memset(ctl, 0, sizeof(*ctl));

	ctl->fname   = XC2028_DEFAULT_FIRMWARE;
	ctl->max_len = 64;
	ctl->mts = em28xx_boards[dev->model].mts_firmware;

	switch (dev->model) {
	case EM2880_BOARD_EMPIRE_DUAL_TV:
	case EM2880_BOARD_HAUPPAUGE_WINTV_HVR_900:
	case EM2882_BOARD_TERRATEC_HYBRID_XS:
	case EM2880_BOARD_TERRATEC_HYBRID_XS:
	case EM2880_BOARD_TERRATEC_HYBRID_XS_FR:
	case EM2881_BOARD_PINNACLE_HYBRID_PRO:
	case EM2882_BOARD_ZOLID_HYBRID_TV_STICK:
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
EXPORT_SYMBOL_GPL(em28xx_setup_xc3028);

static void request_module_async(struct work_struct *work)
{
	struct em28xx *dev = container_of(work,
			     struct em28xx, request_module_wk);

	/*
	 * The em28xx extensions can be modules or builtin. If the
	 * modules are already loaded or are built in, those extensions
	 * can be initialised right now. Otherwise, the module init
	 * code will do it.
	 */

	/*
	 * Devices with an audio-only intf also have a V4L/DVB/RC
	 * intf. Don't register extensions twice on those devices.
	 */
	if (dev->is_audio_only) {
#if defined(CONFIG_MODULES) && defined(MODULE)
		request_module("em28xx-alsa");
#endif
		return;
	}

	em28xx_init_extension(dev);

#if defined(CONFIG_MODULES) && defined(MODULE)
	if (dev->has_video)
		request_module("em28xx-v4l");
	if (dev->usb_audio_type == EM28XX_USB_AUDIO_CLASS)
		request_module("snd-usb-audio");
	else if (dev->usb_audio_type == EM28XX_USB_AUDIO_VENDOR)
		request_module("em28xx-alsa");
	if (dev->board.has_dvb)
		request_module("em28xx-dvb");
	if (dev->board.buttons ||
	    ((dev->board.ir_codes || dev->board.has_ir_i2c) && !disable_ir))
		request_module("em28xx-rc");
#endif /* CONFIG_MODULES */
}

static void request_modules(struct em28xx *dev)
{
	INIT_WORK(&dev->request_module_wk, request_module_async);
	schedule_work(&dev->request_module_wk);
}

static void flush_request_modules(struct em28xx *dev)
{
	flush_work(&dev->request_module_wk);
}

static int em28xx_media_device_init(struct em28xx *dev,
				    struct usb_device *udev)
{
#ifdef CONFIG_MEDIA_CONTROLLER
	struct media_device *mdev;

	mdev = kzalloc(sizeof(*mdev), GFP_KERNEL);
	if (!mdev)
		return -ENOMEM;

	if (udev->product)
		media_device_usb_init(mdev, udev, udev->product);
	else if (udev->manufacturer)
		media_device_usb_init(mdev, udev, udev->manufacturer);
	else
		media_device_usb_init(mdev, udev, dev_name(&dev->intf->dev));

	dev->media_dev = mdev;
#endif
	return 0;
}

static void em28xx_unregister_media_device(struct em28xx *dev)
{
#ifdef CONFIG_MEDIA_CONTROLLER
	if (dev->media_dev) {
		media_device_unregister(dev->media_dev);
		media_device_cleanup(dev->media_dev);
		kfree(dev->media_dev);
		dev->media_dev = NULL;
	}
#endif
}

/*
 * em28xx_release_resources()
 * unregisters the v4l2,i2c and usb devices
 * called when the device gets disconnected or at module unload
 */
static void em28xx_release_resources(struct em28xx *dev)
{
	struct usb_device *udev = interface_to_usbdev(dev->intf);

	/*FIXME: I2C IR should be disconnected */

	mutex_lock(&dev->lock);

	em28xx_unregister_media_device(dev);

	if (dev->def_i2c_bus)
		em28xx_i2c_unregister(dev, 1);
	em28xx_i2c_unregister(dev, 0);

	if (dev->ts == PRIMARY_TS)
		usb_put_dev(udev);

	/* Mark device as unused */
	clear_bit(dev->devno, em28xx_devused);

	mutex_unlock(&dev->lock);
};

/**
 * em28xx_free_device() - Free em28xx device
 *
 * @ref: struct kref for em28xx device
 *
 * This is called when all extensions and em28xx core unregisters a device
 */
void em28xx_free_device(struct kref *ref)
{
	struct em28xx *dev = kref_to_dev(ref);

	dev_info(&dev->intf->dev, "Freeing device\n");

	if (!dev->disconnected)
		em28xx_release_resources(dev);

	if (dev->ts == PRIMARY_TS)
		kfree(dev->alt_max_pkt_size_isoc);

	kfree(dev);
}
EXPORT_SYMBOL_GPL(em28xx_free_device);

/*
 * em28xx_init_dev()
 * allocates and inits the device structs, registers i2c bus and v4l device
 */
static int em28xx_init_dev(struct em28xx *dev, struct usb_device *udev,
			   struct usb_interface *intf,
			   int minor)
{
	int retval;
	const char *chip_name = NULL;

	dev->intf = intf;
	mutex_init(&dev->ctrl_urb_lock);
	spin_lock_init(&dev->slock);

	dev->em28xx_write_regs = em28xx_write_regs;
	dev->em28xx_read_reg = em28xx_read_reg;
	dev->em28xx_read_reg_req_len = em28xx_read_reg_req_len;
	dev->em28xx_write_regs_req = em28xx_write_regs_req;
	dev->em28xx_read_reg_req = em28xx_read_reg_req;
	dev->board.is_em2800 = em28xx_boards[dev->model].is_em2800;

	em28xx_set_model(dev);

	dev->wait_after_write = 5;

	/* Based on the Chip ID, set the device configuration */
	retval = em28xx_read_reg(dev, EM28XX_R0A_CHIPID);
	if (retval > 0) {
		dev->chip_id = retval;

		switch (dev->chip_id) {
		case CHIP_ID_EM2800:
			chip_name = "em2800";
			break;
		case CHIP_ID_EM2710:
			chip_name = "em2710";
			break;
		case CHIP_ID_EM2750:
			chip_name = "em2750";
			break;
		case CHIP_ID_EM2765:
			chip_name = "em2765";
			dev->wait_after_write = 0;
			dev->is_em25xx = 1;
			dev->eeprom_addrwidth_16bit = 1;
			break;
		case CHIP_ID_EM2820:
			chip_name = "em2710/2820";
			if (le16_to_cpu(udev->descriptor.idVendor) == 0xeb1a) {
				__le16 idProd = udev->descriptor.idProduct;

				if (le16_to_cpu(idProd) == 0x2710)
					chip_name = "em2710";
				else if (le16_to_cpu(idProd) == 0x2820)
					chip_name = "em2820";
			}
			/* NOTE: the em2820 is used in webcams, too ! */
			break;
		case CHIP_ID_EM2840:
			chip_name = "em2840";
			break;
		case CHIP_ID_EM2860:
			chip_name = "em2860";
			break;
		case CHIP_ID_EM2870:
			chip_name = "em2870";
			dev->wait_after_write = 0;
			break;
		case CHIP_ID_EM2874:
			chip_name = "em2874";
			dev->wait_after_write = 0;
			dev->eeprom_addrwidth_16bit = 1;
			break;
		case CHIP_ID_EM28174:
			chip_name = "em28174";
			dev->wait_after_write = 0;
			dev->eeprom_addrwidth_16bit = 1;
			break;
		case CHIP_ID_EM28178:
			chip_name = "em28178";
			dev->wait_after_write = 0;
			dev->eeprom_addrwidth_16bit = 1;
			break;
		case CHIP_ID_EM2883:
			chip_name = "em2882/3";
			dev->wait_after_write = 0;
			break;
		case CHIP_ID_EM2884:
			chip_name = "em2884";
			dev->wait_after_write = 0;
			dev->eeprom_addrwidth_16bit = 1;
			break;
		}
	}
	if (!chip_name)
		dev_info(&dev->intf->dev,
			 "unknown em28xx chip ID (%d)\n", dev->chip_id);
	else
		dev_info(&dev->intf->dev, "chip ID is %s\n", chip_name);

	em28xx_media_device_init(dev, udev);

	if (dev->is_audio_only) {
		retval = em28xx_audio_setup(dev);
		if (retval)
			return -ENODEV;
		em28xx_init_extension(dev);

		return 0;
	}

	em28xx_pre_card_setup(dev);

	rt_mutex_init(&dev->i2c_bus_lock);

	/* register i2c bus 0 */
	if (dev->board.is_em2800)
		retval = em28xx_i2c_register(dev, 0, EM28XX_I2C_ALGO_EM2800);
	else
		retval = em28xx_i2c_register(dev, 0, EM28XX_I2C_ALGO_EM28XX);
	if (retval < 0) {
		dev_err(&dev->intf->dev,
			"%s: em28xx_i2c_register bus 0 - error [%d]!\n",
		       __func__, retval);
		return retval;
	}

	/* register i2c bus 1 */
	if (dev->def_i2c_bus) {
		if (dev->is_em25xx)
			retval = em28xx_i2c_register(dev, 1,
						     EM28XX_I2C_ALGO_EM25XX_BUS_B);
		else
			retval = em28xx_i2c_register(dev, 1,
						     EM28XX_I2C_ALGO_EM28XX);
		if (retval < 0) {
			dev_err(&dev->intf->dev,
				"%s: em28xx_i2c_register bus 1 - error [%d]!\n",
				__func__, retval);

			em28xx_i2c_unregister(dev, 0);

			return retval;
		}
	}

	/* Do board specific init and eeprom reading */
	em28xx_card_setup(dev);

	return 0;
}

static int em28xx_duplicate_dev(struct em28xx *dev)
{
	int nr;
	struct em28xx *sec_dev = kzalloc(sizeof(*sec_dev), GFP_KERNEL);

	if (!sec_dev) {
		dev->dev_next = NULL;
		return -ENOMEM;
	}
	memcpy(sec_dev, dev, sizeof(*sec_dev));
	/* Check to see next free device and mark as used */
	do {
		nr = find_first_zero_bit(em28xx_devused, EM28XX_MAXBOARDS);
		if (nr >= EM28XX_MAXBOARDS) {
			/* No free device slots */
			dev_warn(&dev->intf->dev, ": Supports only %i em28xx boards.\n",
				 EM28XX_MAXBOARDS);
			kfree(sec_dev);
			dev->dev_next = NULL;
			return -ENOMEM;
		}
	} while (test_and_set_bit(nr, em28xx_devused));
	sec_dev->devno = nr;
	snprintf(sec_dev->name, 28, "em28xx #%d", nr);
	sec_dev->dev_next = NULL;
	dev->dev_next = sec_dev;
	return 0;
}

/* high bandwidth multiplier, as encoded in highspeed endpoint descriptors */
#define hb_mult(wMaxPacketSize) (1 + (((wMaxPacketSize) >> 11) & 0x03))

static void em28xx_check_usb_descriptor(struct em28xx *dev,
					struct usb_device *udev,
					struct usb_interface *intf,
					int alt, int ep,
					bool *has_vendor_audio,
					bool *has_video,
					bool *has_dvb)
{
	const struct usb_endpoint_descriptor *e;
	int sizedescr, size;

	/*
	 * NOTE:
	 *
	 * Old logic with support for isoc transfers only was:
	 *  0x82	isoc		=> analog
	 *  0x83	isoc		=> audio
	 *  0x84	isoc		=> digital
	 *
	 * New logic with support for bulk transfers
	 *  0x82	isoc		=> analog
	 *  0x82	bulk		=> analog
	 *  0x83	isoc*		=> audio
	 *  0x84	isoc		=> digital
	 *  0x84	bulk		=> analog or digital**
	 *  0x85	isoc		=> digital TS2
	 *  0x85	bulk		=> digital TS2
	 * (*: audio should always be isoc)
	 * (**: analog, if ep 0x82 is isoc, otherwise digital)
	 *
	 * The new logic preserves backwards compatibility and
	 * reflects the endpoint configurations we have seen
	 * so far. But there might be devices for which this
	 * logic is not sufficient...
	 */

	e = &intf->altsetting[alt].endpoint[ep].desc;

	if (!usb_endpoint_dir_in(e))
		return;

	sizedescr = le16_to_cpu(e->wMaxPacketSize);
	size = sizedescr & 0x7ff;

	if (udev->speed == USB_SPEED_HIGH)
		size = size * hb_mult(sizedescr);

	/* Only inspect input endpoints */

	switch (e->bEndpointAddress) {
	case 0x82:
		*has_video = true;
		if (usb_endpoint_xfer_isoc(e)) {
			dev->analog_ep_isoc = e->bEndpointAddress;
			dev->alt_max_pkt_size_isoc[alt] = size;
		} else if (usb_endpoint_xfer_bulk(e)) {
			dev->analog_ep_bulk = e->bEndpointAddress;
		}
		return;
	case 0x83:
		if (usb_endpoint_xfer_isoc(e))
			*has_vendor_audio = true;
		else
			dev_err(&intf->dev,
				"error: skipping audio endpoint 0x83, because it uses bulk transfers !\n");
		return;
	case 0x84:
		if (*has_video && (usb_endpoint_xfer_bulk(e))) {
			dev->analog_ep_bulk = e->bEndpointAddress;
		} else {
			if (usb_endpoint_xfer_isoc(e)) {
				if (size > dev->dvb_max_pkt_size_isoc) {
					/*
					 * 2) some manufacturers (e.g. Terratec)
					 * disable endpoints by setting
					 * wMaxPacketSize to 0 bytes for all
					 * alt settings. So far, we've seen
					 * this for DVB isoc endpoints only.
					 */
					*has_dvb = true;
					dev->dvb_ep_isoc = e->bEndpointAddress;
					dev->dvb_max_pkt_size_isoc = size;
					dev->dvb_alt_isoc = alt;
				}
			} else {
				*has_dvb = true;
				dev->dvb_ep_bulk = e->bEndpointAddress;
			}
		}
		return;
	case 0x85:
		if (usb_endpoint_xfer_isoc(e)) {
			if (size > dev->dvb_max_pkt_size_isoc_ts2) {
				dev->dvb_ep_isoc_ts2 = e->bEndpointAddress;
				dev->dvb_max_pkt_size_isoc_ts2 = size;
				dev->dvb_alt_isoc = alt;
			}
		} else {
			dev->dvb_ep_bulk_ts2 = e->bEndpointAddress;
		}
		return;
	}
}

/*
 * em28xx_usb_probe()
 * checks for supported devices
 */
static int em28xx_usb_probe(struct usb_interface *intf,
			    const struct usb_device_id *id)
{
	struct usb_device *udev;
	struct em28xx *dev = NULL;
	int retval;
	bool has_vendor_audio = false, has_video = false, has_dvb = false;
	int i, nr, try_bulk;
	const int ifnum = intf->altsetting[0].desc.bInterfaceNumber;
	char *speed;

	udev = usb_get_dev(interface_to_usbdev(intf));

	/* Check to see next free device and mark as used */
	do {
		nr = find_first_zero_bit(em28xx_devused, EM28XX_MAXBOARDS);
		if (nr >= EM28XX_MAXBOARDS) {
			/* No free device slots */
			dev_err(&intf->dev,
				"Driver supports up to %i em28xx boards.\n",
			       EM28XX_MAXBOARDS);
			retval = -ENOMEM;
			goto err_no_slot;
		}
	} while (test_and_set_bit(nr, em28xx_devused));

	/* Don't register audio interfaces */
	if (intf->altsetting[0].desc.bInterfaceClass == USB_CLASS_AUDIO) {
		dev_info(&intf->dev,
			"audio device (%04x:%04x): interface %i, class %i\n",
			le16_to_cpu(udev->descriptor.idVendor),
			le16_to_cpu(udev->descriptor.idProduct),
			ifnum,
			intf->altsetting[0].desc.bInterfaceClass);

		retval = -ENODEV;
		goto err;
	}

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		retval = -ENOMEM;
		goto err;
	}

	/* compute alternate max packet sizes */
	dev->alt_max_pkt_size_isoc = kcalloc(intf->num_altsetting,
					     sizeof(dev->alt_max_pkt_size_isoc[0]),
					     GFP_KERNEL);
	if (!dev->alt_max_pkt_size_isoc) {
		kfree(dev);
		retval = -ENOMEM;
		goto err;
	}

	/* Get endpoints */
	for (i = 0; i < intf->num_altsetting; i++) {
		int ep;

		for (ep = 0;
		     ep < intf->altsetting[i].desc.bNumEndpoints;
		     ep++)
			em28xx_check_usb_descriptor(dev, udev, intf,
						    i, ep,
						    &has_vendor_audio,
						    &has_video,
						    &has_dvb);
	}

	if (!(has_vendor_audio || has_video || has_dvb)) {
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

	dev_info(&intf->dev,
		"New device %s %s @ %s Mbps (%04x:%04x, interface %d, class %d)\n",
		udev->manufacturer ? udev->manufacturer : "",
		udev->product ? udev->product : "",
		speed,
		le16_to_cpu(udev->descriptor.idVendor),
		le16_to_cpu(udev->descriptor.idProduct),
		ifnum,
		intf->altsetting->desc.bInterfaceNumber);

	/*
	 * Make sure we have 480 Mbps of bandwidth, otherwise things like
	 * video stream wouldn't likely work, since 12 Mbps is generally
	 * not enough even for most Digital TV streams.
	 */
	if (udev->speed != USB_SPEED_HIGH && disable_usb_speed_check == 0) {
		dev_err(&intf->dev, "Device initialization failed.\n");
		dev_err(&intf->dev,
			"Device must be connected to a high-speed USB 2.0 port.\n");
		retval = -ENODEV;
		goto err_free;
	}

	dev->devno = nr;
	dev->model = id->driver_info;
	dev->alt   = -1;
	dev->is_audio_only = has_vendor_audio && !(has_video || has_dvb);
	dev->has_video = has_video;
	dev->ifnum = ifnum;

	dev->ts = PRIMARY_TS;
	snprintf(dev->name, 28, "em28xx");
	dev->dev_next = NULL;

	if (has_vendor_audio) {
		dev_info(&intf->dev,
			"Audio interface %i found (Vendor Class)\n", ifnum);
		dev->usb_audio_type = EM28XX_USB_AUDIO_VENDOR;
	}
	/* Checks if audio is provided by a USB Audio Class intf */
	for (i = 0; i < udev->config->desc.bNumInterfaces; i++) {
		struct usb_interface *uif = udev->config->interface[i];

		if (uif->altsetting[0].desc.bInterfaceClass == USB_CLASS_AUDIO) {
			if (has_vendor_audio)
				dev_err(&intf->dev,
					"em28xx: device seems to have vendor AND usb audio class interfaces !\n"
					"\t\tThe vendor interface will be ignored. Please contact the developers <linux-media@vger.kernel.org>\n");
			dev->usb_audio_type = EM28XX_USB_AUDIO_CLASS;
			break;
		}
	}

	if (has_video)
		dev_info(&intf->dev, "Video interface %i found:%s%s\n",
			ifnum,
			dev->analog_ep_bulk ? " bulk" : "",
			dev->analog_ep_isoc ? " isoc" : "");
	if (has_dvb)
		dev_info(&intf->dev, "DVB interface %i found:%s%s\n",
			ifnum,
			dev->dvb_ep_bulk ? " bulk" : "",
			dev->dvb_ep_isoc ? " isoc" : "");

	dev->num_alt = intf->num_altsetting;

	if ((unsigned int)card[nr] < em28xx_bcount)
		dev->model = card[nr];

	/* save our data pointer in this intf device */
	usb_set_intfdata(intf, dev);

	/* allocate device struct and check if the device is a webcam */
	mutex_init(&dev->lock);
	retval = em28xx_init_dev(dev, udev, intf, nr);
	if (retval)
		goto err_free;

	if (usb_xfer_mode < 0) {
		if (dev->is_webcam)
			try_bulk = 1;
		else
			try_bulk = 0;
	} else {
		try_bulk = usb_xfer_mode > 0;
	}

	/* Disable V4L2 if the device doesn't have a decoder or image sensor */
	if (has_video &&
	    dev->board.decoder == EM28XX_NODECODER &&
	    dev->em28xx_sensor == EM28XX_NOSENSOR) {
		dev_err(&intf->dev,
			"Currently, V4L2 is not supported on this model\n");
		has_video = false;
		dev->has_video = false;
	}

	if (dev->board.has_dual_ts &&
	    (dev->tuner_type != TUNER_ABSENT || INPUT(0)->type)) {
		/*
		 * The logic with sets alternate is not ready for dual-tuners
		 * which analog modes.
		 */
		dev_err(&intf->dev,
			"We currently don't support analog TV or stream capture on dual tuners.\n");
		has_video = false;
	}

	/* Select USB transfer types to use */
	if (has_video) {
		if (!dev->analog_ep_isoc || (try_bulk && dev->analog_ep_bulk))
			dev->analog_xfer_bulk = 1;
		dev_info(&intf->dev, "analog set to %s mode.\n",
			dev->analog_xfer_bulk ? "bulk" : "isoc");
	}
	if (has_dvb) {
		if (!dev->dvb_ep_isoc || (try_bulk && dev->dvb_ep_bulk))
			dev->dvb_xfer_bulk = 1;
		dev_info(&intf->dev, "dvb set to %s mode.\n",
			dev->dvb_xfer_bulk ? "bulk" : "isoc");
	}

	if (dev->board.has_dual_ts && em28xx_duplicate_dev(dev) == 0) {
		dev->dev_next->ts = SECONDARY_TS;
		dev->dev_next->alt   = -1;
		dev->dev_next->is_audio_only = has_vendor_audio &&
						!(has_video || has_dvb);
		dev->dev_next->has_video = false;
		dev->dev_next->ifnum = ifnum;
		dev->dev_next->model = id->driver_info;

		mutex_init(&dev->dev_next->lock);
		retval = em28xx_init_dev(dev->dev_next, udev, intf,
					 dev->dev_next->devno);
		if (retval)
			goto err_free;

		dev->dev_next->board.ir_codes = NULL; /* No IR for 2nd tuner */
		dev->dev_next->board.has_ir_i2c = 0; /* No IR for 2nd tuner */

		if (usb_xfer_mode < 0) {
			if (dev->dev_next->is_webcam)
				try_bulk = 1;
			else
				try_bulk = 0;
		} else {
			try_bulk = usb_xfer_mode > 0;
		}

		/* Select USB transfer types to use */
		if (has_dvb) {
			if (!dev->dvb_ep_isoc_ts2 ||
			    (try_bulk && dev->dvb_ep_bulk_ts2))
				dev->dev_next->dvb_xfer_bulk = 1;
			dev_info(&dev->intf->dev, "dvb ts2 set to %s mode.\n",
				 dev->dev_next->dvb_xfer_bulk ? "bulk" : "isoc");
		}

		dev->dev_next->dvb_ep_isoc = dev->dvb_ep_isoc_ts2;
		dev->dev_next->dvb_ep_bulk = dev->dvb_ep_bulk_ts2;
		dev->dev_next->dvb_max_pkt_size_isoc = dev->dvb_max_pkt_size_isoc_ts2;
		dev->dev_next->dvb_alt_isoc = dev->dvb_alt_isoc;

		/* Configuare hardware to support TS2*/
		if (dev->dvb_xfer_bulk) {
			/* The ep4 and ep5 are configuared for BULK */
			em28xx_write_reg(dev, 0x0b, 0x96);
			mdelay(100);
			em28xx_write_reg(dev, 0x0b, 0x80);
			mdelay(100);
		} else {
			/* The ep4 and ep5 are configuared for ISO */
			em28xx_write_reg(dev, 0x0b, 0x96);
			mdelay(100);
			em28xx_write_reg(dev, 0x0b, 0x82);
			mdelay(100);
		}

		kref_init(&dev->dev_next->ref);
	}

	kref_init(&dev->ref);

	request_modules(dev);

	/*
	 * Do it at the end, to reduce dynamic configuration changes during
	 * the device init. Yet, as request_modules() can be async, the
	 * topology will likely change after the load of the em28xx subdrivers.
	 */
#ifdef CONFIG_MEDIA_CONTROLLER
	retval = media_device_register(dev->media_dev);
#endif

	return 0;

err_free:
	kfree(dev->alt_max_pkt_size_isoc);
	kfree(dev);

err:
	clear_bit(nr, em28xx_devused);

err_no_slot:
	usb_put_dev(udev);
	return retval;
}

/*
 * em28xx_usb_disconnect()
 * called when the device gets disconnected
 * video device will be unregistered on v4l2_close in case it is still open
 */
static void em28xx_usb_disconnect(struct usb_interface *intf)
{
	struct em28xx *dev;

	dev = usb_get_intfdata(intf);
	usb_set_intfdata(intf, NULL);

	if (!dev)
		return;

	if (dev->dev_next) {
		dev->dev_next->disconnected = 1;
		dev_info(&dev->intf->dev, "Disconnecting %s\n",
			 dev->dev_next->name);
		flush_request_modules(dev->dev_next);
	}

	dev->disconnected = 1;

	dev_info(&dev->intf->dev, "Disconnecting %s\n", dev->name);

	flush_request_modules(dev);

	em28xx_close_extension(dev);

	if (dev->dev_next)
		em28xx_release_resources(dev->dev_next);
	em28xx_release_resources(dev);

	if (dev->dev_next) {
		kref_put(&dev->dev_next->ref, em28xx_free_device);
		dev->dev_next = NULL;
	}
	kref_put(&dev->ref, em28xx_free_device);
}

static int em28xx_usb_suspend(struct usb_interface *intf,
			      pm_message_t message)
{
	struct em28xx *dev;

	dev = usb_get_intfdata(intf);
	if (!dev)
		return 0;
	em28xx_suspend_extension(dev);
	return 0;
}

static int em28xx_usb_resume(struct usb_interface *intf)
{
	struct em28xx *dev;

	dev = usb_get_intfdata(intf);
	if (!dev)
		return 0;
	em28xx_resume_extension(dev);
	return 0;
}

static struct usb_driver em28xx_usb_driver = {
	.name = "em28xx",
	.probe = em28xx_usb_probe,
	.disconnect = em28xx_usb_disconnect,
	.suspend = em28xx_usb_suspend,
	.resume = em28xx_usb_resume,
	.reset_resume = em28xx_usb_resume,
	.id_table = em28xx_id_table,
};

module_usb_driver(em28xx_usb_driver);
