/*
 * GSPCA Endpoints (formerly known as AOX) se401 USB Camera sub Driver
 *
 * Copyright (C) 2011 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on the v4l1 se401 driver which is:
 *
 * Copyright (c) 2000 Jeroen B. Vreeken (pe1rxq@amsat.org)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#define SE401_REQ_GET_CAMERA_DESCRIPTOR		0x06
#define SE401_REQ_START_CONTINUOUS_CAPTURE	0x41
#define SE401_REQ_STOP_CONTINUOUS_CAPTURE	0x42
#define SE401_REQ_CAPTURE_FRAME			0x43
#define SE401_REQ_GET_BRT			0x44
#define SE401_REQ_SET_BRT			0x45
#define SE401_REQ_GET_WIDTH			0x4c
#define SE401_REQ_SET_WIDTH			0x4d
#define SE401_REQ_GET_HEIGHT			0x4e
#define SE401_REQ_SET_HEIGHT			0x4f
#define SE401_REQ_GET_OUTPUT_MODE		0x50
#define SE401_REQ_SET_OUTPUT_MODE		0x51
#define SE401_REQ_GET_EXT_FEATURE		0x52
#define SE401_REQ_SET_EXT_FEATURE		0x53
#define SE401_REQ_CAMERA_POWER			0x56
#define SE401_REQ_LED_CONTROL			0x57
#define SE401_REQ_BIOS				0xff

#define SE401_BIOS_READ				0x07

#define SE401_FORMAT_BAYER	0x40

/* Hyundai hv7131b registers
   7121 and 7141 should be the same (haven't really checked...) */
/* Mode registers: */
#define HV7131_REG_MODE_A		0x00
#define HV7131_REG_MODE_B		0x01
#define HV7131_REG_MODE_C		0x02
/* Frame registers: */
#define HV7131_REG_FRSU		0x10
#define HV7131_REG_FRSL		0x11
#define HV7131_REG_FCSU		0x12
#define HV7131_REG_FCSL		0x13
#define HV7131_REG_FWHU		0x14
#define HV7131_REG_FWHL		0x15
#define HV7131_REG_FWWU		0x16
#define HV7131_REG_FWWL		0x17
/* Timing registers: */
#define HV7131_REG_THBU		0x20
#define HV7131_REG_THBL		0x21
#define HV7131_REG_TVBU		0x22
#define HV7131_REG_TVBL		0x23
#define HV7131_REG_TITU		0x25
#define HV7131_REG_TITM		0x26
#define HV7131_REG_TITL		0x27
#define HV7131_REG_TMCD		0x28
/* Adjust Registers: */
#define HV7131_REG_ARLV		0x30
#define HV7131_REG_ARCG		0x31
#define HV7131_REG_AGCG		0x32
#define HV7131_REG_ABCG		0x33
#define HV7131_REG_APBV		0x34
#define HV7131_REG_ASLP		0x54
/* Offset Registers: */
#define HV7131_REG_OFSR		0x50
#define HV7131_REG_OFSG		0x51
#define HV7131_REG_OFSB		0x52
/* REset level statistics registers: */
#define HV7131_REG_LOREFNOH	0x57
#define HV7131_REG_LOREFNOL	0x58
#define HV7131_REG_HIREFNOH	0x59
#define HV7131_REG_HIREFNOL	0x5a

/* se401 registers */
#define SE401_OPERATINGMODE	0x2000
