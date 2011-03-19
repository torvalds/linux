/*****************************************************************************
*                                                                            *
*                                                                            *
*  easycap_low.c                                                             *
*                                                                            *
*                                                                            *
*****************************************************************************/
/*
 *
 *  Copyright (C) 2010 R.M. Thomas  <rmthomas@sciolus.org>
 *
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
/*****************************************************************************/
/*
 *  ACKNOWLEGEMENTS AND REFERENCES
 *  ------------------------------
 *  This driver makes use of register information contained in the Syntek
 *  Semicon DC-1125 driver hosted at
 *               http://sourceforge.net/projects/syntekdriver/.
 *  Particularly useful has been a patch to the latter driver provided by
 *  Ivor Hewitt in January 2009.  The NTSC implementation is taken from the
 *  work of Ben Trask.
*/
/****************************************************************************/

#include "easycap.h"
#include "easycap_debug.h"

/*--------------------------------------------------------------------------*/
const struct stk1160config { int reg; int set; } stk1160configPAL[256] = {
		{0x000, 0x0098},
		{0x002, 0x0093},

		{0x001, 0x0003},
		{0x003, 0x0080},
		{0x00D, 0x0000},
		{0x00F, 0x0002},
		{0x018, 0x0010},
		{0x019, 0x0000},
		{0x01A, 0x0014},
		{0x01B, 0x000E},
		{0x01C, 0x0046},

		{0x100, 0x0033},
		{0x103, 0x0000},
		{0x104, 0x0000},
		{0x105, 0x0000},
		{0x106, 0x0000},

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/*
 *  RESOLUTION 640x480
*/
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
		{0x110, 0x0008},
		{0x111, 0x0000},
		{0x112, 0x0020},
		{0x113, 0x0000},
		{0x114, 0x0508},
		{0x115, 0x0005},
		{0x116, 0x0110},
		{0x117, 0x0001},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

		{0x202, 0x000F},
		{0x203, 0x004A},
		{0x2FF, 0x0000},

		{0xFFF, 0xFFFF}
};
/*--------------------------------------------------------------------------*/
const struct stk1160config stk1160configNTSC[256] = {
		{0x000, 0x0098},
		{0x002, 0x0093},

		{0x001, 0x0003},
		{0x003, 0x0080},
		{0x00D, 0x0000},
		{0x00F, 0x0002},
		{0x018, 0x0010},
		{0x019, 0x0000},
		{0x01A, 0x0014},
		{0x01B, 0x000E},
		{0x01C, 0x0046},

		{0x100, 0x0033},
		{0x103, 0x0000},
		{0x104, 0x0000},
		{0x105, 0x0000},
		{0x106, 0x0000},

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
/*
 *  RESOLUTION 640x480
*/
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
		{0x110, 0x0008},
		{0x111, 0x0000},
		{0x112, 0x0003},
		{0x113, 0x0000},
		{0x114, 0x0508},
		{0x115, 0x0005},
		{0x116, 0x00F3},
		{0x117, 0x0000},
/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

		{0x202, 0x000F},
		{0x203, 0x004A},
		{0x2FF, 0x0000},

		{0xFFF, 0xFFFF}
};
/*--------------------------------------------------------------------------*/
const struct saa7113config { int reg; int set; } saa7113configPAL[256] = {
		{0x01, 0x08},
#if defined(ANTIALIAS)
		{0x02, 0xC0},
#else
		{0x02, 0x80},
#endif /*ANTIALIAS*/
		{0x03, 0x33},
		{0x04, 0x00},
		{0x05, 0x00},
		{0x06, 0xE9},
		{0x07, 0x0D},
		{0x08, 0x38},
		{0x09, 0x00},
		{0x0A, SAA_0A_DEFAULT},
		{0x0B, SAA_0B_DEFAULT},
		{0x0C, SAA_0C_DEFAULT},
		{0x0D, SAA_0D_DEFAULT},
		{0x0E, 0x01},
		{0x0F, 0x36},
		{0x10, 0x00},
		{0x11, 0x0C},
		{0x12, 0xE7},
		{0x13, 0x00},
		{0x15, 0x00},
		{0x16, 0x00},
		{0x40, 0x02},
		{0x41, 0xFF},
		{0x42, 0xFF},
		{0x43, 0xFF},
		{0x44, 0xFF},
		{0x45, 0xFF},
		{0x46, 0xFF},
		{0x47, 0xFF},
		{0x48, 0xFF},
		{0x49, 0xFF},
		{0x4A, 0xFF},
		{0x4B, 0xFF},
		{0x4C, 0xFF},
		{0x4D, 0xFF},
		{0x4E, 0xFF},
		{0x4F, 0xFF},
		{0x50, 0xFF},
		{0x51, 0xFF},
		{0x52, 0xFF},
		{0x53, 0xFF},
		{0x54, 0xFF},
		{0x55, 0xFF},
		{0x56, 0xFF},
		{0x57, 0xFF},
		{0x58, 0x40},
		{0x59, 0x54},
		{0x5A, 0x07},
		{0x5B, 0x83},

		{0xFF, 0xFF}
};
/*--------------------------------------------------------------------------*/
const struct saa7113config saa7113configNTSC[256] = {
		{0x01, 0x08},
#if defined(ANTIALIAS)
		{0x02, 0xC0},
#else
		{0x02, 0x80},
#endif /*ANTIALIAS*/
		{0x03, 0x33},
		{0x04, 0x00},
		{0x05, 0x00},
		{0x06, 0xE9},
		{0x07, 0x0D},
		{0x08, 0x78},
		{0x09, 0x00},
		{0x0A, SAA_0A_DEFAULT},
		{0x0B, SAA_0B_DEFAULT},
		{0x0C, SAA_0C_DEFAULT},
		{0x0D, SAA_0D_DEFAULT},
		{0x0E, 0x01},
		{0x0F, 0x36},
		{0x10, 0x00},
		{0x11, 0x0C},
		{0x12, 0xE7},
		{0x13, 0x00},
		{0x15, 0x00},
		{0x16, 0x00},
		{0x40, 0x82},
		{0x41, 0xFF},
		{0x42, 0xFF},
		{0x43, 0xFF},
		{0x44, 0xFF},
		{0x45, 0xFF},
		{0x46, 0xFF},
		{0x47, 0xFF},
		{0x48, 0xFF},
		{0x49, 0xFF},
		{0x4A, 0xFF},
		{0x4B, 0xFF},
		{0x4C, 0xFF},
		{0x4D, 0xFF},
		{0x4E, 0xFF},
		{0x4F, 0xFF},
		{0x50, 0xFF},
		{0x51, 0xFF},
		{0x52, 0xFF},
		{0x53, 0xFF},
		{0x54, 0xFF},
		{0x55, 0xFF},
		{0x56, 0xFF},
		{0x57, 0xFF},
		{0x58, 0x40},
		{0x59, 0x54},
		{0x5A, 0x0A},
		{0x5B, 0x83},

		{0xFF, 0xFF}
};
/*--------------------------------------------------------------------------*/

/****************************************************************************/
int
confirm_resolution(struct usb_device *p)
{
__u8 get0, get1, get2, get3, get4, get5, get6, get7;

if (NULL == p)
	return -ENODEV;
GET(p, 0x0110, &get0);
GET(p, 0x0111, &get1);
GET(p, 0x0112, &get2);
GET(p, 0x0113, &get3);
GET(p, 0x0114, &get4);
GET(p, 0x0115, &get5);
GET(p, 0x0116, &get6);
GET(p, 0x0117, &get7);
JOT(8,  "0x%03X, 0x%03X, " \
	"0x%03X, 0x%03X, " \
	"0x%03X, 0x%03X, " \
	"0x%03X, 0x%03X\n", \
	get0, get1, get2, get3, get4, get5, get6, get7);
JOT(8,  "....cf PAL_720x526: " \
	"0x%03X, 0x%03X, " \
	"0x%03X, 0x%03X, " \
	"0x%03X, 0x%03X, " \
	"0x%03X, 0x%03X\n", \
	0x000, 0x000, 0x001, 0x000, 0x5A0, 0x005, 0x121, 0x001);
JOT(8,  "....cf PAL_704x526: " \
	"0x%03X, 0x%03X, " \
	"0x%03X, 0x%03X, " \
	"0x%03X, 0x%03X, " \
	"0x%03X, 0x%03X\n", \
	0x004, 0x000, 0x001, 0x000, 0x584, 0x005, 0x121, 0x001);
JOT(8,  "....cf VGA_640x480: " \
	"0x%03X, 0x%03X, " \
	"0x%03X, 0x%03X, " \
	"0x%03X, 0x%03X, " \
	"0x%03X, 0x%03X\n", \
	0x008, 0x000, 0x020, 0x000, 0x508, 0x005, 0x110, 0x001);
return 0;
}
/****************************************************************************/
int
confirm_stream(struct usb_device *p)
{
__u16 get2;
__u8 igot;

if (NULL == p)
	return -ENODEV;
GET(p, 0x0100, &igot);  get2 = 0x80 & igot;
if (0x80 == get2)
	JOT(8, "confirm_stream:  OK\n");
else
	JOT(8, "confirm_stream:  STUCK\n");
return 0;
}
/****************************************************************************/
int
setup_stk(struct usb_device *p, bool ntsc)
{
int i0;

if (NULL == p)
	return -ENODEV;
i0 = 0;
if (true == ntsc) {
	while (0xFFF != stk1160configNTSC[i0].reg) {
		SET(p, stk1160configNTSC[i0].reg, stk1160configNTSC[i0].set);
		i0++;
	}
} else {
	while (0xFFF != stk1160configPAL[i0].reg) {
		SET(p, stk1160configPAL[i0].reg, stk1160configPAL[i0].set);
		i0++;
	}
}

write_300(p);

return 0;
}
/****************************************************************************/
int
setup_saa(struct usb_device *p, bool ntsc)
{
int i0, ir;

if (NULL == p)
	return -ENODEV;
i0 = 0;
if (true == ntsc) {
	while (0xFF != saa7113configNTSC[i0].reg) {
		ir = write_saa(p, saa7113configNTSC[i0].reg, \
					saa7113configNTSC[i0].set);
		i0++;
	}
} else {
	while (0xFF != saa7113configPAL[i0].reg) {
		ir = write_saa(p, saa7113configPAL[i0].reg, \
					saa7113configPAL[i0].set);
		i0++;
	}
}
return 0;
}
/****************************************************************************/
int
write_000(struct usb_device *p, __u16 set2, __u16 set0)
{
__u8 igot0, igot2;

if (NULL == p)
	return -ENODEV;
GET(p, 0x0002, &igot2);
GET(p, 0x0000, &igot0);
SET(p, 0x0002, set2);
SET(p, 0x0000, set0);
return 0;
}
/****************************************************************************/
int
write_saa(struct usb_device *p, __u16 reg0, __u16 set0)
{
if (NULL == p)
	return -ENODEV;
SET(p, 0x200, 0x00);
SET(p, 0x204, reg0);
SET(p, 0x205, set0);
SET(p, 0x200, 0x01);
return wait_i2c(p);
}
/****************************************************************************/
/*--------------------------------------------------------------------------*/
/*
 *  REGISTER 500:  SETTING VALUE TO 0x008B READS FROM VT1612A (?)
 *  REGISTER 500:  SETTING VALUE TO 0x008C WRITES TO  VT1612A
 *  REGISTER 502:  LEAST SIGNIFICANT BYTE OF VALUE TO SET
 *  REGISTER 503:  MOST SIGNIFICANT BYTE OF VALUE TO SET
 *  REGISTER 504:  TARGET ADDRESS ON VT1612A
 */
/*--------------------------------------------------------------------------*/
int
write_vt(struct usb_device *p, __u16 reg0, __u16 set0)
{
__u8 igot;
__u16 got502, got503;
__u16 set502, set503;

if (NULL == p)
	return -ENODEV;
SET(p, 0x0504, reg0);
SET(p, 0x0500, 0x008B);

GET(p, 0x0502, &igot);  got502 = (0xFF & igot);
GET(p, 0x0503, &igot);  got503 = (0xFF & igot);

JOT(16, "write_vt(., 0x%04X, 0x%04X): was 0x%04X\n", \
					reg0, set0, ((got503 << 8) | got502));

set502 =  (0x00FF & set0);
set503 = ((0xFF00 & set0) >> 8);

SET(p, 0x0504, reg0);
SET(p, 0x0502, set502);
SET(p, 0x0503, set503);
SET(p, 0x0500, 0x008C);

return 0;
}
/****************************************************************************/
/*--------------------------------------------------------------------------*/
/*
 *  REGISTER 500:  SETTING VALUE TO 0x008B READS FROM VT1612A (?)
 *  REGISTER 500:  SETTING VALUE TO 0x008C WRITES TO  VT1612A
 *  REGISTER 502:  LEAST SIGNIFICANT BYTE OF VALUE TO GET
 *  REGISTER 503:  MOST SIGNIFICANT BYTE OF VALUE TO GET
 *  REGISTER 504:  TARGET ADDRESS ON VT1612A
 */
/*--------------------------------------------------------------------------*/
int
read_vt(struct usb_device *p, __u16 reg0)
{
__u8 igot;
__u16 got502, got503;

if (NULL == p)
	return -ENODEV;
SET(p, 0x0504, reg0);
SET(p, 0x0500, 0x008B);

GET(p, 0x0502, &igot);  got502 = (0xFF & igot);
GET(p, 0x0503, &igot);  got503 = (0xFF & igot);

JOT(16, "read_vt(., 0x%04X): has 0x%04X\n", reg0, ((got503 << 8) | got502));

return (got503 << 8) | got502;
}
/****************************************************************************/
/*--------------------------------------------------------------------------*/
/*
 *  THESE APPEAR TO HAVE NO EFFECT ON EITHER VIDEO OR AUDIO.
 */
/*--------------------------------------------------------------------------*/
int
write_300(struct usb_device *p)
{
if (NULL == p)
	return -ENODEV;
SET(p, 0x300, 0x0012);
SET(p, 0x350, 0x002D);
SET(p, 0x351, 0x0001);
SET(p, 0x352, 0x0000);
SET(p, 0x353, 0x0000);
SET(p, 0x300, 0x0080);
return 0;
}
/****************************************************************************/
/*--------------------------------------------------------------------------*/
/*
 *  NOTE: THE FOLLOWING IS NOT CHECKED:
 *  REGISTER 0x0F, WHICH IS INVOLVED IN CHROMINANCE AUTOMATIC GAIN CONTROL.
 */
/*--------------------------------------------------------------------------*/
int
check_saa(struct usb_device *p, bool ntsc)
{
int i0, ir, rc;

if (NULL == p)
	return -ENODEV;
i0 = 0;
rc = 0;
if (true == ntsc) {
	while (0xFF != saa7113configNTSC[i0].reg) {
		if (0x0F == saa7113configNTSC[i0].reg) {
			i0++;
			continue;
		}

		ir = read_saa(p, saa7113configNTSC[i0].reg);
		if (ir != saa7113configNTSC[i0].set) {
			SAY("SAA register 0x%02X has 0x%02X, " \
						"expected 0x%02X\n", \
						saa7113configNTSC[i0].reg, \
						ir, saa7113configNTSC[i0].set);
			rc--;
		}
		i0++;
	}
} else {
	while (0xFF != saa7113configPAL[i0].reg) {
		if (0x0F == saa7113configPAL[i0].reg) {
			i0++;
			continue;
		}

		ir = read_saa(p, saa7113configPAL[i0].reg);
		if (ir != saa7113configPAL[i0].set) {
			SAY("SAA register 0x%02X has 0x%02X, " \
						"expected 0x%02X\n", \
						saa7113configPAL[i0].reg, \
						ir, saa7113configPAL[i0].set);
			rc--;
		}
		i0++;
	}
}
if (-8 > rc)
	return rc;
else
	return 0;
}
/****************************************************************************/
int
merit_saa(struct usb_device *p)
{
int rc;

if (NULL == p)
	return -ENODEV;
rc = read_saa(p, 0x1F);
if ((0 > rc) || (0x02 & rc))
	return 1 ;
else
	return 0;
}
/****************************************************************************/
int
ready_saa(struct usb_device *p)
{
int j, rc, rate;
const int max = 5, marktime = PATIENCE/5;
/*--------------------------------------------------------------------------*/
/*
 *   RETURNS    0     FOR INTERLACED       50 Hz
 *              1     FOR NON-INTERLACED   50 Hz
 *              2     FOR INTERLACED       60 Hz
 *              3     FOR NON-INTERLACED   60 Hz
*/
/*--------------------------------------------------------------------------*/
if (NULL == p)
	return -ENODEV;
j = 0;
while (max > j) {
	rc = read_saa(p, 0x1F);
	if (0 <= rc) {
		if (0 == (0x40 & rc))
			break;
		if (1 == (0x01 & rc))
			break;
	}
	msleep(marktime);
	j++;
}
if (max == j)
	return -1;
else {
	if (0x20 & rc) {
		rate = 2;
		JOT(8, "hardware detects 60 Hz\n");
	} else {
		rate = 0;
		JOT(8, "hardware detects 50 Hz\n");
	}
	if (0x80 & rc)
		JOT(8, "hardware detects interlacing\n");
	else {
		rate++;
		JOT(8, "hardware detects no interlacing\n");
	}
}
return 0;
}
/****************************************************************************/
/*--------------------------------------------------------------------------*/
/*
 *  NOTE: THE FOLLOWING ARE NOT CHECKED:
 *  REGISTERS 0x000, 0x002:  FUNCTIONALITY IS NOT KNOWN
 *  REGISTER  0x100:  ACCEPT ALSO (0x80 | stk1160config....[.].set)
 */
/*--------------------------------------------------------------------------*/
int
check_stk(struct usb_device *p, bool ntsc)
{
int i0, ir;

if (NULL == p)
	return -ENODEV;
i0 = 0;
if (true == ntsc) {
	while (0xFFF != stk1160configNTSC[i0].reg) {
		if (0x000 == stk1160configNTSC[i0].reg) {
			i0++; continue;
		}
		if (0x002 == stk1160configNTSC[i0].reg) {
			i0++; continue;
		}
		ir = read_stk(p, stk1160configNTSC[i0].reg);
		if (0x100 == stk1160configNTSC[i0].reg) {
			if ((ir != (0xFF & stk1160configNTSC[i0].set)) && \
					(ir != (0x80 | (0xFF & \
					stk1160configNTSC[i0].set))) && \
					(0xFFFF != \
					stk1160configNTSC[i0].set)) {
				SAY("STK register 0x%03X has 0x%02X, " \
						"expected 0x%02X\n", \
						stk1160configNTSC[i0].reg, \
						ir, stk1160configNTSC[i0].set);
				}
			i0++; continue;
			}
		if ((ir != (0xFF & stk1160configNTSC[i0].set)) && \
				(0xFFFF != stk1160configNTSC[i0].set)) {
			SAY("STK register 0x%03X has 0x%02X, " \
						"expected 0x%02X\n", \
						stk1160configNTSC[i0].reg, \
						ir, stk1160configNTSC[i0].set);
		}
		i0++;
	}
} else {
	while (0xFFF != stk1160configPAL[i0].reg) {
		if (0x000 == stk1160configPAL[i0].reg) {
			i0++; continue;
		}
		if (0x002 == stk1160configPAL[i0].reg) {
			i0++; continue;
		}
		ir = read_stk(p, stk1160configPAL[i0].reg);
		if (0x100 == stk1160configPAL[i0].reg) {
			if ((ir != (0xFF & stk1160configPAL[i0].set)) && \
					(ir != (0x80 | (0xFF & \
					stk1160configPAL[i0].set))) && \
					(0xFFFF != \
					stk1160configPAL[i0].set)) {
				SAY("STK register 0x%03X has 0x%02X, " \
						"expected 0x%02X\n", \
						stk1160configPAL[i0].reg, \
						ir, stk1160configPAL[i0].set);
				}
			i0++; continue;
			}
		if ((ir != (0xFF & stk1160configPAL[i0].set)) && \
				(0xFFFF != stk1160configPAL[i0].set)) {
			SAY("STK register 0x%03X has 0x%02X, " \
						"expected 0x%02X\n", \
						stk1160configPAL[i0].reg, \
						ir, stk1160configPAL[i0].set);
		}
		i0++;
	}
}
return 0;
}
/****************************************************************************/
int
read_saa(struct usb_device *p, __u16 reg0)
{
__u8 igot;

if (NULL == p)
	return -ENODEV;
SET(p, 0x208, reg0);
SET(p, 0x200, 0x20);
if (0 != wait_i2c(p))
	return -1;
igot = 0;
GET(p, 0x0209, &igot);
return igot;
}
/****************************************************************************/
int
read_stk(struct usb_device *p, __u32 reg0)
{
__u8 igot;

if (NULL == p)
	return -ENODEV;
igot = 0;
GET(p, reg0, &igot);
return igot;
}
/****************************************************************************/
/*--------------------------------------------------------------------------*/
/*
 *    HARDWARE    USERSPACE INPUT NUMBER   PHYSICAL INPUT   DRIVER input VALUE
 *
 *  CVBS+S-VIDEO           0 or 1              CVBS                 1
 *   FOUR-CVBS             0 or 1              CVBS1                1
 *   FOUR-CVBS                2                CVBS2                2
 *   FOUR-CVBS                3                CVBS3                3
 *   FOUR-CVBS                4                CVBS4                4
 *  CVBS+S-VIDEO              5               S-VIDEO               5
 *
 *  WHEN 5==input THE ARGUMENT mode MUST ALSO BE SUPPLIED:
 *
 *     mode  7   => GAIN TO BE SET EXPLICITLY USING REGISTER 0x05 (UNTESTED)
 *     mode  9   => USE AUTOMATIC GAIN CONTROL (DEFAULT)
 *
*/
/*---------------------------------------------------------------------------*/
int
select_input(struct usb_device *p, int input, int mode)
{
int ir;

if (NULL == p)
	return -ENODEV;
stop_100(p);
switch (input) {
case 0:
case 1: {
	if (0 != write_saa(p, 0x02, 0x80)) {
		SAY("ERROR: failed to set SAA register 0x02 for input %i\n", \
									input);
	}
	SET(p, 0x0000, 0x0098);
	SET(p, 0x0002, 0x0078);
	break;
}
case 2: {
	if (0 != write_saa(p, 0x02, 0x80)) {
		SAY("ERROR: failed to set SAA register 0x02 for input %i\n", \
									input);
	}
	SET(p, 0x0000, 0x0090);
	SET(p, 0x0002, 0x0078);
	break;
}
case 3: {
	if (0 != write_saa(p, 0x02, 0x80)) {
		SAY("ERROR: failed to set SAA register 0x02 for input %i\n", \
									input);
	}
	SET(p, 0x0000, 0x0088);
	SET(p, 0x0002, 0x0078);
	break;
}
case 4: {
	if (0 != write_saa(p, 0x02, 0x80)) {
		SAY("ERROR: failed to set SAA register 0x02 for input %i\n", \
									input);
	}
	SET(p, 0x0000, 0x0080);
	SET(p, 0x0002, 0x0078);
	break;
}
case 5: {
	if (9 != mode)
		mode = 7;
	switch (mode) {
	case 7: {
		if (0 != write_saa(p, 0x02, 0x87)) {
			SAY("ERROR: failed to set SAA register 0x02 " \
						"for input %i\n", input);
		}
		if (0 != write_saa(p, 0x05, 0xFF)) {
			SAY("ERROR: failed to set SAA register 0x05 " \
						"for input %i\n", input);
		}
		break;
	}
	case 9: {
		if (0 != write_saa(p, 0x02, 0x89)) {
			SAY("ERROR: failed to set SAA register 0x02 " \
						"for input %i\n", input);
		}
		if (0 != write_saa(p, 0x05, 0x00)) {
			SAY("ERROR: failed to set SAA register 0x05 " \
						"for input %i\n", input);
		}
	break;
	}
	default: {
		SAY("MISTAKE:  bad mode: %i\n", mode);
		return -1;
	}
	}
	if (0 != write_saa(p, 0x04, 0x00)) {
		SAY("ERROR: failed to set SAA register 0x04 for input %i\n", \
									input);
	}
	if (0 != write_saa(p, 0x09, 0x80)) {
		SAY("ERROR: failed to set SAA register 0x09 for input %i\n", \
									input);
	}
	SET(p, 0x0002, 0x0093);
	break;
}
default: {
	SAY("ERROR:  bad input: %i\n", input);
	return -1;
}
}
ir = read_stk(p, 0x00);
JOT(8, "STK register 0x00 has 0x%02X\n", ir);
ir = read_saa(p, 0x02);
JOT(8, "SAA register 0x02 has 0x%02X\n", ir);

start_100(p);

return 0;
}
/****************************************************************************/
int
set_resolution(struct usb_device *p, \
				__u16 set0, __u16 set1, __u16 set2, __u16 set3)
{
__u16 u0x0111, u0x0113, u0x0115, u0x0117;

if (NULL == p)
	return -ENODEV;
u0x0111 = ((0xFF00 & set0) >> 8);
u0x0113 = ((0xFF00 & set1) >> 8);
u0x0115 = ((0xFF00 & set2) >> 8);
u0x0117 = ((0xFF00 & set3) >> 8);

SET(p, 0x0110, (0x00FF & set0));
SET(p, 0x0111, u0x0111);
SET(p, 0x0112, (0x00FF & set1));
SET(p, 0x0113, u0x0113);
SET(p, 0x0114, (0x00FF & set2));
SET(p, 0x0115, u0x0115);
SET(p, 0x0116, (0x00FF & set3));
SET(p, 0x0117, u0x0117);

return 0;
}
/****************************************************************************/
int
start_100(struct usb_device *p)
{
__u16 get116, get117, get0;
__u8 igot116, igot117, igot;

if (NULL == p)
	return -ENODEV;
GET(p, 0x0116, &igot116);
get116 = igot116;
GET(p, 0x0117, &igot117);
get117 = igot117;
SET(p, 0x0116, 0x0000);
SET(p, 0x0117, 0x0000);

GET(p, 0x0100, &igot);
get0 = igot;
SET(p, 0x0100, (0x80 | get0));

SET(p, 0x0116, get116);
SET(p, 0x0117, get117);

return 0;
}
/****************************************************************************/
int
stop_100(struct usb_device *p)
{
__u16 get0;
__u8 igot;

if (NULL == p)
	return -ENODEV;
GET(p, 0x0100, &igot);
get0 = igot;
SET(p, 0x0100, (0x7F & get0));
return 0;
}
/****************************************************************************/
/*--------------------------------------------------------------------------*/
/*
 *  FUNCTION wait_i2c() RETURNS 0 ON SUCCESS
*/
/*--------------------------------------------------------------------------*/
int
wait_i2c(struct usb_device *p)
{
__u16 get0;
__u8 igot;
const int max = 2;
int k;

if (NULL == p)
	return -ENODEV;
for (k = 0;  k < max;  k++) {
	GET(p, 0x0201, &igot);  get0 = igot;
	switch (get0) {
	case 0x04:
	case 0x01: {
		return 0;
	}
	case 0x00: {
		msleep(20);
		continue;
	}
	default: {
		return get0 - 1;
	}
	}
}
return -1;
}
/****************************************************************************/
int
regset(struct usb_device *pusb_device, __u16 index, __u16 value)
{
__u16 igot;
int rc0, rc1;

if (!pusb_device)
	return -ENODEV;
rc1 = 0;  igot = 0;
rc0 = usb_control_msg(pusb_device, usb_sndctrlpipe(pusb_device, 0), \
		(__u8)0x01, \
		(__u8)(USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE), \
		(__u16)value, \
		(__u16)index, \
		(void *)NULL, \
		(__u16)0, \
		(int)500);

#if defined(NOREADBACK)
#
#else
rc1 = usb_control_msg(pusb_device, usb_rcvctrlpipe(pusb_device, 0), \
		(__u8)0x00, \
		(__u8)(USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE), \
		(__u16)0x00, \
		(__u16)index, \
		(void *)&igot, \
		(__u16)sizeof(__u16), \
		(int)50000);
igot = 0xFF & igot;
switch (index) {
case 0x000:
case 0x500:
case 0x502:
case 0x503:
case 0x504:
case 0x506:
case 0x507: {
	break;
}
case 0x204:
case 0x205:
case 0x350:
case 0x351: {
	if (0 != (0xFF & igot)) {
		JOT(8, "unexpected 0x%02X for STK register 0x%03X\n", \
								igot, index);
	}
break;
}
default: {
	if ((0xFF & value) != (0xFF & igot)) {
		JOT(8, "unexpected 0x%02X != 0x%02X " \
					"for STK register 0x%03X\n", \
					igot, value, index);
	}
break;
}
}
#endif /* ! NOREADBACK*/

return (0 > rc0) ? rc0 : rc1;
}
/*****************************************************************************/
int
regget(struct usb_device *pusb_device, __u16 index, void *pvoid)
{
int ir;

if (!pusb_device)
	return -ENODEV;
ir = usb_control_msg(pusb_device, usb_rcvctrlpipe(pusb_device, 0), \
		(__u8)0x00, \
		(__u8)(USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE), \
		(__u16)0x00, \
		(__u16)index, \
		(void *)pvoid, \
		sizeof(__u8), \
		(int)50000);
return 0xFF & ir;
}
/*****************************************************************************/
int
wakeup_device(struct usb_device *pusb_device)
{
if (!pusb_device)
	return -ENODEV;
return usb_control_msg(pusb_device, usb_sndctrlpipe(pusb_device, 0), \
		(__u8)USB_REQ_SET_FEATURE, \
		(__u8)(USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE), \
		USB_DEVICE_REMOTE_WAKEUP, \
		(__u16)0, \
		(void *) NULL, \
		(__u16)0, \
		(int)50000);
}
/*****************************************************************************/
int
audio_setup(struct easycap *peasycap)
{
struct usb_device *pusb_device;
unsigned char buffer[1];
int rc, id1, id2;
/*---------------------------------------------------------------------------*/
/*
 *                                IMPORTANT:
 *  THE MESSAGE OF TYPE (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE)
 *  CAUSES MUTING IF THE VALUE 0x0100 IS SENT.
 *  TO ENABLE AUDIO  THE VALUE 0x0200 MUST BE SENT.
 */
/*---------------------------------------------------------------------------*/
const __u8 request = 0x01;
const __u8 requesttype = \
		(__u8)(USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE);
const __u16 value_unmute = 0x0200;
const __u16 index = 0x0301;
const __u16 length = 1;

if (NULL == peasycap)
	return -EFAULT;

pusb_device = peasycap->pusb_device;
if (NULL == pusb_device)
	return -ENODEV;

JOM(8, "%02X %02X %02X %02X %02X %02X %02X %02X\n",	\
			requesttype, request,		\
			(0x00FF & value_unmute),	\
			(0xFF00 & value_unmute) >> 8,	\
			(0x00FF & index),		\
			(0xFF00 & index) >> 8,		\
			(0x00FF & length),		\
			(0xFF00 & length) >> 8);

buffer[0] = 0x01;

rc = usb_control_msg(pusb_device, usb_sndctrlpipe(pusb_device, 0),	\
			(__u8)request,					\
			(__u8)requesttype,				\
			(__u16)value_unmute,				\
			(__u16)index,					\
			(void *)&buffer[0],				\
			(__u16)length,					\
			(int)50000);

JOT(8, "0x%02X=buffer\n", *((__u8 *) &buffer[0]));
if (rc != (int)length)
	SAY("ERROR: usb_control_msg returned %i\n", rc);

/*--------------------------------------------------------------------------*/
/*
 *  REGISTER 500:  SETTING VALUE TO 0x0094 RESETS AUDIO CONFIGURATION ???
 *  REGISTER 506:  ANALOGUE AUDIO ATTENTUATOR ???
 *                 FOR THE CVBS+S-VIDEO HARDWARE:
 *                    SETTING VALUE TO 0x0000 GIVES QUIET SOUND.
 *                    THE UPPER BYTE SEEMS TO HAVE NO EFFECT.
 *                 FOR THE FOUR-CVBS HARDWARE:
 *                    SETTING VALUE TO 0x0000 SEEMS TO HAVE NO EFFECT.
 *  REGISTER 507:  ANALOGUE AUDIO PREAMPLIFIER ON/OFF ???
 *                 FOR THE CVBS-S-VIDEO HARDWARE:
 *                    SETTING VALUE TO 0x0001 GIVES VERY LOUD, DISTORTED SOUND.
 *                    THE UPPER BYTE SEEMS TO HAVE NO EFFECT.
 */
/*--------------------------------------------------------------------------*/
SET(pusb_device, 0x0500, 0x0094);
SET(pusb_device, 0x0500, 0x008C);
SET(pusb_device, 0x0506, 0x0001);
SET(pusb_device, 0x0507, 0x0000);
id1 = read_vt(pusb_device, 0x007C);
id2 = read_vt(pusb_device, 0x007E);
SAM("0x%04X:0x%04X is audio vendor id\n", id1, id2);
/*---------------------------------------------------------------------------*/
/*
 *  SELECT AUDIO SOURCE "LINE IN" AND SET THE AUDIO GAIN.
*/
/*---------------------------------------------------------------------------*/
if (31 < easycap_gain)
	easycap_gain = 31;
if (0 > easycap_gain)
	easycap_gain = 0;
if (0 != audio_gainset(pusb_device, (__s8)easycap_gain))
	SAY("ERROR: audio_gainset() failed\n");
check_vt(pusb_device);
return 0;
}
/*****************************************************************************/
int
check_vt(struct usb_device *pusb_device)
{
int igot;

if (!pusb_device)
	return -ENODEV;
igot = read_vt(pusb_device, 0x0002);
if (0 > igot)
	SAY("ERROR: failed to read VT1612A register 0x02\n");
if (0x8000 & igot)
	SAY("register 0x%02X muted\n", 0x02);

igot = read_vt(pusb_device, 0x000E);
if (0 > igot)
	SAY("ERROR: failed to read VT1612A register 0x0E\n");
if (0x8000 & igot)
	SAY("register 0x%02X muted\n", 0x0E);

igot = read_vt(pusb_device, 0x0010);
if (0 > igot)
	SAY("ERROR: failed to read VT1612A register 0x10\n");
if (0x8000 & igot)
	SAY("register 0x%02X muted\n", 0x10);

igot = read_vt(pusb_device, 0x0012);
if (0 > igot)
	SAY("ERROR: failed to read VT1612A register 0x12\n");
if (0x8000 & igot)
	SAY("register 0x%02X muted\n", 0x12);

igot = read_vt(pusb_device, 0x0014);
if (0 > igot)
	SAY("ERROR: failed to read VT1612A register 0x14\n");
if (0x8000 & igot)
	SAY("register 0x%02X muted\n", 0x14);

igot = read_vt(pusb_device, 0x0016);
if (0 > igot)
	SAY("ERROR: failed to read VT1612A register 0x16\n");
if (0x8000 & igot)
	SAY("register 0x%02X muted\n", 0x16);

igot = read_vt(pusb_device, 0x0018);
if (0 > igot)
	SAY("ERROR: failed to read VT1612A register 0x18\n");
if (0x8000 & igot)
	SAY("register 0x%02X muted\n", 0x18);

igot = read_vt(pusb_device, 0x001C);
if (0 > igot)
	SAY("ERROR: failed to read VT1612A register 0x1C\n");
if (0x8000 & igot)
	SAY("register 0x%02X muted\n", 0x1C);

return 0;
}
/*****************************************************************************/
/*---------------------------------------------------------------------------*/
/*  NOTE:  THIS DOES INCREASE THE VOLUME DRAMATICALLY:
 *                      audio_gainset(pusb_device, 0x000F);
 *
 *       loud        dB  register 0x10      dB register 0x1C    dB total
 *         0               -34.5                   0             -34.5
 *        ..                ....                   .              ....
 *        15                10.5                   0              10.5
 *        16                12.0                   0              12.0
 *        17                12.0                   1.5            13.5
 *        ..                ....                  ....            ....
 *        31                12.0                  22.5            34.5
*/
/*---------------------------------------------------------------------------*/
int
audio_gainset(struct usb_device *pusb_device, __s8 loud)
{
int igot;
__u8 u8;
__u16 mute;

if (NULL == pusb_device)
	return -ENODEV;
if (0 > loud)
	loud = 0;
if (31 < loud)
	loud = 31;

write_vt(pusb_device, 0x0002, 0x8000);
/*---------------------------------------------------------------------------*/
igot = read_vt(pusb_device, 0x000E);
if (0 > igot) {
	SAY("ERROR: failed to read VT1612A register 0x0E\n");
	mute = 0x0000;
} else
	mute = 0x8000 & ((unsigned int)igot);
mute = 0;

if (16 > loud)
	u8 = 0x01 | (0x001F & (((__u8)(15 - loud)) << 1));
else
	u8 = 0;

JOT(8, "0x%04X=(mute|u8) for VT1612A register 0x0E\n", mute | u8);
write_vt(pusb_device, 0x000E, (mute | u8));
/*---------------------------------------------------------------------------*/
igot = read_vt(pusb_device, 0x0010);
if (0 > igot) {
	SAY("ERROR: failed to read VT1612A register 0x10\n");
	mute = 0x0000;
} else
	mute = 0x8000 & ((unsigned int)igot);
mute = 0;

JOT(8, "0x%04X=(mute|u8|(u8<<8)) for VT1612A register 0x10,...0x18\n", \
							mute | u8 | (u8 << 8));
write_vt(pusb_device, 0x0010, (mute | u8 | (u8 << 8)));
write_vt(pusb_device, 0x0012, (mute | u8 | (u8 << 8)));
write_vt(pusb_device, 0x0014, (mute | u8 | (u8 << 8)));
write_vt(pusb_device, 0x0016, (mute | u8 | (u8 << 8)));
write_vt(pusb_device, 0x0018, (mute | u8 | (u8 << 8)));
/*---------------------------------------------------------------------------*/
igot = read_vt(pusb_device, 0x001C);
if (0 > igot) {
	SAY("ERROR: failed to read VT1612A register 0x1C\n");
	mute = 0x0000;
} else
	mute = 0x8000 & ((unsigned int)igot);
mute = 0;

if (16 <= loud)
	u8 = 0x000F & (__u8)(loud - 16);
else
	u8 = 0;

JOT(8, "0x%04X=(mute|u8|(u8<<8)) for VT1612A register 0x1C\n", \
							mute | u8 | (u8 << 8));
write_vt(pusb_device, 0x001C, (mute | u8 | (u8 << 8)));
write_vt(pusb_device, 0x001A, 0x0404);
write_vt(pusb_device, 0x0002, 0x0000);
return 0;
}
/*****************************************************************************/
int
audio_gainget(struct usb_device *pusb_device)
{
int igot;

if (NULL == pusb_device)
	return -ENODEV;
igot = read_vt(pusb_device, 0x001C);
if (0 > igot)
	SAY("ERROR: failed to read VT1612A register 0x1C\n");
return igot;
}
/*****************************************************************************/
