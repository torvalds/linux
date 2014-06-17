/*
   cx231xx-pcb-cfg.h - driver for Conexant
		Cx23100/101/102 USB video capture devices

   Copyright (C) 2008 <srinivasa.deevi at conexant dot com>

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

#ifndef _PCB_CONFIG_H_
#define _PCB_CONFIG_H_

#include <linux/init.h>
#include <linux/module.h>

/***************************************************************************
				* Class Information *
***************************************************************************/
#define CLASS_DEFAULT       0xFF

enum VENDOR_REQUEST_TYPE {
	/* Set/Get I2C */
	VRT_SET_I2C0 = 0x0,
	VRT_SET_I2C1 = 0x1,
	VRT_SET_I2C2 = 0x2,
	VRT_GET_I2C0 = 0x4,
	VRT_GET_I2C1 = 0x5,
	VRT_GET_I2C2 = 0x6,

	/* Set/Get GPIO */
	VRT_SET_GPIO = 0x8,
	VRT_GET_GPIO = 0x9,

	/* Set/Get GPIE */
	VRT_SET_GPIE = 0xA,
	VRT_GET_GPIE = 0xB,

	/* Set/Get Register Control/Status */
	VRT_SET_REGISTER = 0xC,
	VRT_GET_REGISTER = 0xD,

	/* Get Extended Compat ID Descriptor */
	VRT_GET_EXTCID_DESC = 0xFF,
};

enum BYTE_ENABLE_MASK {
	ENABLE_ONE_BYTE = 0x1,
	ENABLE_TWE_BYTE = 0x3,
	ENABLE_THREE_BYTE = 0x7,
	ENABLE_FOUR_BYTE = 0xF,
};

#define SPEED_MASK      0x1
enum USB_SPEED{
	FULL_SPEED = 0x0,	/* 0: full speed */
	HIGH_SPEED = 0x1	/* 1: high speed */
};

#define TS_MASK         0x6
enum TS_PORT{
	NO_TS_PORT = 0x0,	/* 2'b00: Neither port used. PCB not a Hybrid,
				   only offers Analog TV or Video */
	TS1_PORT = 0x4,		/* 2'b10: TS1 Input (Hybrid mode :
				Digital or External Analog/Compressed source) */
	TS1_TS2_PORT = 0x6,	/* 2'b11: TS1 & TS2 Inputs
				(Dual inputs from Digital and/or
				External Analog/Compressed sources) */
	TS1_EXT_CLOCK = 0x6,	/* 2'b11: TS1 & TS2 as selector
						to external clock */
	TS1VIP_TS2_PORT = 0x2	/* 2'b01: TS1 used as 656/VIP Output,
				   TS2 Input (from Compressor) */
};

#define EAVP_MASK       0x8
enum EAV_PRESENT{
	NO_EXTERNAL_AV = 0x0,	/* 0: No External A/V inputs
						(no need for i2s blcok),
						Analog Tuner must be present */
	EXTERNAL_AV = 0x8	/* 1: External A/V inputs
						present (requires i2s blk) */
};

#define ATM_MASK        0x30
enum AT_MODE{
	DIF_TUNER = 0x30,	/* 2'b11: IF Tuner (requires use of DIF) */
	BASEBAND_SOUND = 0x20,	/* 2'b10: Baseband Composite &
						Sound-IF Signals present */
	NO_TUNER = 0x10		/* 2'b0x: No Analog Tuner present */
};

#define PWR_SEL_MASK    0x40
enum POWE_TYPE{
	SELF_POWER = 0x0,	/* 0: self power */
	BUS_POWER = 0x40	/* 1: bus power */
};

enum USB_POWE_TYPE{
	USB_SELF_POWER = 0,
	USB_BUS_POWER
};

#define BO_0_MASK       0x80
enum AVDEC_STATUS{
	AVDEC_DISABLE = 0x0,	/* 0: A/V Decoder Disabled */
	AVDEC_ENABLE = 0x80	/* 1: A/V Decoder Enabled */
};

#define BO_1_MASK       0x100

#define BUSPOWER_MASK   0xC4	/* for Polaris spec 0.8 */
#define SELFPOWER_MASK  0x86

/***************************************************************************/
#define NOT_DECIDE_YET  0xFE
#define NOT_SUPPORTED   0xFF

/***************************************************************************
				* for mod field use *
***************************************************************************/
#define MOD_DIGITAL     0x1
#define MOD_ANALOG      0x2
#define MOD_DIF         0x4
#define MOD_EXTERNAL    0x8
#define CAP_ALL_MOD     0x0f

/***************************************************************************
				* source define *
***************************************************************************/
#define SOURCE_DIGITAL          0x1
#define SOURCE_ANALOG           0x2
#define SOURCE_DIF              0x4
#define SOURCE_EXTERNAL         0x8
#define SOURCE_TS_BDA			0x10
#define SOURCE_TS_ENCODE		0x20
#define SOURCE_TS_EXTERNAL   	0x40

/***************************************************************************
				* interface information define *
***************************************************************************/
struct INTERFACE_INFO {
	u8 interrupt_index;
	u8 ts1_index;
	u8 ts2_index;
	u8 audio_index;
	u8 video_index;
	u8 vanc_index;		/* VBI */
	u8 hanc_index;		/* Sliced CC */
	u8 ir_index;
};

enum INDEX_INTERFACE_INFO{
	INDEX_INTERRUPT = 0x0,
	INDEX_TS1,
	INDEX_TS2,
	INDEX_AUDIO,
	INDEX_VIDEO,
	INDEX_VANC,
	INDEX_HANC,
	INDEX_IR,
};

/***************************************************************************
				* configuration information define *
***************************************************************************/
struct CONFIG_INFO {
	u8 config_index;
	struct INTERFACE_INFO interface_info;
};

struct pcb_config {
	u8 index;
	u8 type;		/* bus power or self power,
					   self power--0, bus_power--1 */
	u8 speed;		/* usb speed, 2.0--1, 1.1--0 */
	u8 mode;		/* digital , anlog, dif or external A/V */
	u32 ts1_source;		/* three source -- BDA,External,encode */
	u32 ts2_source;
	u32 analog_source;
	u8 digital_index;	/* bus-power used */
	u8 analog_index;	/* bus-power used */
	u8 dif_index;		/* bus-power used */
	u8 external_index;	/* bus-power used */
	u8 config_num;		/* current config num, 0,1,2,
						   for self-power, always 0 */
	struct CONFIG_INFO hs_config_info[3];
	struct CONFIG_INFO fs_config_info[3];
};

enum INDEX_PCB_CONFIG{
	INDEX_SELFPOWER_DIGITAL_ONLY = 0x0,
	INDEX_SELFPOWER_DUAL_DIGITAL,
	INDEX_SELFPOWER_ANALOG_ONLY,
	INDEX_SELFPOWER_DUAL,
	INDEX_SELFPOWER_TRIPLE,
	INDEX_SELFPOWER_COMPRESSOR,
	INDEX_BUSPOWER_DIGITAL_ONLY,
	INDEX_BUSPOWER_ANALOG_ONLY,
	INDEX_BUSPOWER_DIF_ONLY,
	INDEX_BUSPOWER_EXTERNAL_ONLY,
	INDEX_BUSPOWER_EXTERNAL_ANALOG,
	INDEX_BUSPOWER_EXTERNAL_DIF,
	INDEX_BUSPOWER_EXTERNAL_DIGITAL,
	INDEX_BUSPOWER_DIGITAL_ANALOG,
	INDEX_BUSPOWER_DIGITAL_DIF,
	INDEX_BUSPOWER_DIGITAL_ANALOG_EXTERNAL,
	INDEX_BUSPOWER_DIGITAL_DIF_EXTERNAL,
};

/***************************************************************************/
struct cx231xx;

u32 initialize_cx231xx(struct cx231xx *p_dev);

#endif
