/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/* $XFree86$ */
/* $XdotOrg$ */
/*
 * Data and prototypes for init.c
 *
 * Copyright (C) 2001-2005 by Thomas Winischhofer, Vienna, Austria
 *
 * If distributed as part of the Linux kernel, the following license terms
 * apply:
 *
 * * This program is free software; you can redistribute it and/or modify
 * * it under the terms of the GNU General Public License as published by
 * * the Free Software Foundation; either version 2 of the named License,
 * * or any later version.
 * *
 * * This program is distributed in the hope that it will be useful,
 * * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * * GNU General Public License for more details.
 * *
 * * You should have received a copy of the GNU General Public License
 * * along with this program; if not, write to the Free Software
 * * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Otherwise, the following license terms apply:
 *
 * * Redistribution and use in source and binary forms, with or without
 * * modification, are permitted provided that the following conditions
 * * are met:
 * * 1) Redistributions of source code must retain the above copyright
 * *    notice, this list of conditions and the following disclaimer.
 * * 2) Redistributions in binary form must reproduce the above copyright
 * *    notice, this list of conditions and the following disclaimer in the
 * *    documentation and/or other materials provided with the distribution.
 * * 3) The name of the author may not be used to endorse or promote products
 * *    derived from this software without specific prior written permission.
 * *
 * * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Author:	Thomas Winischhofer <thomas@winischhofer.net>
 *
 */

#ifndef _SISUSB_INIT_H_
#define _SISUSB_INIT_H_

/* SiS_ModeType */
#define ModeText		0x00
#define ModeCGA			0x01
#define ModeEGA			0x02
#define ModeVGA			0x03
#define Mode15Bpp		0x04
#define Mode16Bpp		0x05
#define Mode24Bpp		0x06
#define Mode32Bpp		0x07

#define ModeTypeMask		0x07
#define IsTextMode		0x07

#define DACInfoFlag		0x0018
#define MemoryInfoFlag		0x01E0
#define MemorySizeShift		5

/* modeflag */
#define Charx8Dot		0x0200
#define LineCompareOff		0x0400
#define CRT2Mode		0x0800
#define HalfDCLK		0x1000
#define NoSupportSimuTV		0x2000
#define NoSupportLCDScale	0x4000	/* SiS bridge: No scaling possible (no matter what panel) */
#define DoubleScanMode		0x8000

/* Infoflag */
#define SupportTV		0x0008
#define SupportTV1024		0x0800
#define SupportCHTV		0x0800
#define Support64048060Hz	0x0800	/* Special for 640x480 LCD */
#define SupportHiVision		0x0010
#define SupportYPbPr750p	0x1000
#define SupportLCD		0x0020
#define SupportRAMDAC2		0x0040	/* All           (<= 100Mhz) */
#define SupportRAMDAC2_135	0x0100	/* All except DH (<= 135Mhz) */
#define SupportRAMDAC2_162	0x0200	/* B, C          (<= 162Mhz) */
#define SupportRAMDAC2_202	0x0400	/* C             (<= 202Mhz) */
#define InterlaceMode		0x0080
#define SyncPP			0x0000
#define SyncPN			0x4000
#define SyncNP			0x8000
#define SyncNN			0xc000

/* SetFlag */
#define ProgrammingCRT2		0x0001
#define LowModeTests		0x0002
#define LCDVESATiming		0x0008
#define EnableLVDSDDA		0x0010
#define SetDispDevSwitchFlag	0x0020
#define CheckWinDos		0x0040
#define SetDOSMode		0x0080

/* Index in ModeResInfo table */
#define SIS_RI_320x200		0
#define SIS_RI_320x240		1
#define SIS_RI_320x400		2
#define SIS_RI_400x300		3
#define SIS_RI_512x384		4
#define SIS_RI_640x400		5
#define SIS_RI_640x480		6
#define SIS_RI_800x600		7
#define SIS_RI_1024x768		8
#define SIS_RI_1280x1024	9
#define SIS_RI_1600x1200	10
#define SIS_RI_1920x1440	11
#define SIS_RI_2048x1536	12
#define SIS_RI_720x480		13
#define SIS_RI_720x576		14
#define SIS_RI_1280x960		15
#define SIS_RI_800x480		16
#define SIS_RI_1024x576		17
#define SIS_RI_1280x720		18
#define SIS_RI_856x480		19
#define SIS_RI_1280x768		20
#define SIS_RI_1400x1050	21
#define SIS_RI_1152x864		22	/* Up to here SiS conforming */
#define SIS_RI_848x480		23
#define SIS_RI_1360x768		24
#define SIS_RI_1024x600		25
#define SIS_RI_1152x768		26
#define SIS_RI_768x576		27
#define SIS_RI_1360x1024	28
#define SIS_RI_1680x1050	29
#define SIS_RI_1280x800		30
#define SIS_RI_1920x1080	31
#define SIS_RI_960x540		32
#define SIS_RI_960x600		33

#define SIS_VIDEO_CAPTURE	0x00 - 0x30
#define SIS_VIDEO_PLAYBACK	0x02 - 0x30
#define SIS_CRT2_PORT_04	0x04 - 0x30

int SiSUSBSetMode(struct SiS_Private *SiS_Pr, unsigned short ModeNo);
int SiSUSBSetVESAMode(struct SiS_Private *SiS_Pr, unsigned short VModeNo);

extern int sisusb_setreg(struct sisusb_usb_data *sisusb, u32 port, u8 data);
extern int sisusb_getreg(struct sisusb_usb_data *sisusb, u32 port, u8 * data);
extern int sisusb_setidxreg(struct sisusb_usb_data *sisusb, u32 port,
			    u8 index, u8 data);
extern int sisusb_getidxreg(struct sisusb_usb_data *sisusb, u32 port,
			    u8 index, u8 * data);
extern int sisusb_setidxregandor(struct sisusb_usb_data *sisusb, u32 port,
				 u8 idx, u8 myand, u8 myor);
extern int sisusb_setidxregor(struct sisusb_usb_data *sisusb, u32 port,
			      u8 index, u8 myor);
extern int sisusb_setidxregand(struct sisusb_usb_data *sisusb, u32 port,
			       u8 idx, u8 myand);

void sisusb_delete(struct kref *kref);
int sisusb_writeb(struct sisusb_usb_data *sisusb, u32 adr, u8 data);
int sisusb_readb(struct sisusb_usb_data *sisusb, u32 adr, u8 * data);
int sisusb_copy_memory(struct sisusb_usb_data *sisusb, u8 *src,
		       u32 dest, int length);
int sisusb_reset_text_mode(struct sisusb_usb_data *sisusb, int init);
int sisusbcon_do_font_op(struct sisusb_usb_data *sisusb, int set, int slot,
			 u8 * arg, int cmapsz, int ch512, int dorecalc,
			 struct vc_data *c, int fh, int uplock);
void sisusb_set_cursor(struct sisusb_usb_data *sisusb, unsigned int location);
int sisusb_console_init(struct sisusb_usb_data *sisusb, int first, int last);
void sisusb_console_exit(struct sisusb_usb_data *sisusb);
void sisusb_init_concode(void);

#endif
