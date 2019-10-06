// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * sisusb - usb kernel driver for SiS315(E) based USB2VGA dongles
 *
 * Display mode initializing code
 *
 * Copyright (C) 2001-2005 by Thomas Winischhofer, Vienna, Austria
 *
 * If distributed as part of the Linux kernel, this code is licensed under the
 * terms of the GPL v2.
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/spinlock.h>

#include "sisusb.h"
#include "sisusb_init.h"

/*********************************************/
/*         POINTER INITIALIZATION            */
/*********************************************/

static void SiSUSB_InitPtr(struct SiS_Private *SiS_Pr)
{
	SiS_Pr->SiS_ModeResInfo = SiSUSB_ModeResInfo;
	SiS_Pr->SiS_StandTable = SiSUSB_StandTable;

	SiS_Pr->SiS_SModeIDTable = SiSUSB_SModeIDTable;
	SiS_Pr->SiS_EModeIDTable = SiSUSB_EModeIDTable;
	SiS_Pr->SiS_RefIndex = SiSUSB_RefIndex;
	SiS_Pr->SiS_CRT1Table = SiSUSB_CRT1Table;

	SiS_Pr->SiS_VCLKData = SiSUSB_VCLKData;
}

/*********************************************/
/*          HELPER: SetReg, GetReg           */
/*********************************************/

static void
SiS_SetReg(struct SiS_Private *SiS_Pr, unsigned long port,
	   unsigned short index, unsigned short data)
{
	sisusb_setidxreg(SiS_Pr->sisusb, port, index, data);
}

static void
SiS_SetRegByte(struct SiS_Private *SiS_Pr, unsigned long port,
	       unsigned short data)
{
	sisusb_setreg(SiS_Pr->sisusb, port, data);
}

static unsigned char
SiS_GetReg(struct SiS_Private *SiS_Pr, unsigned long port, unsigned short index)
{
	u8 data;

	sisusb_getidxreg(SiS_Pr->sisusb, port, index, &data);

	return data;
}

static unsigned char
SiS_GetRegByte(struct SiS_Private *SiS_Pr, unsigned long port)
{
	u8 data;

	sisusb_getreg(SiS_Pr->sisusb, port, &data);

	return data;
}

static void
SiS_SetRegANDOR(struct SiS_Private *SiS_Pr, unsigned long port,
		unsigned short index, unsigned short DataAND,
		unsigned short DataOR)
{
	sisusb_setidxregandor(SiS_Pr->sisusb, port, index, DataAND, DataOR);
}

static void
SiS_SetRegAND(struct SiS_Private *SiS_Pr, unsigned long port,
	      unsigned short index, unsigned short DataAND)
{
	sisusb_setidxregand(SiS_Pr->sisusb, port, index, DataAND);
}

static void
SiS_SetRegOR(struct SiS_Private *SiS_Pr, unsigned long port,
	     unsigned short index, unsigned short DataOR)
{
	sisusb_setidxregor(SiS_Pr->sisusb, port, index, DataOR);
}

/*********************************************/
/*      HELPER: DisplayOn, DisplayOff        */
/*********************************************/

static void SiS_DisplayOn(struct SiS_Private *SiS_Pr)
{
	SiS_SetRegAND(SiS_Pr, SiS_Pr->SiS_P3c4, 0x01, 0xDF);
}

/*********************************************/
/*        HELPER: Init Port Addresses        */
/*********************************************/

static void SiSUSBRegInit(struct SiS_Private *SiS_Pr, unsigned long BaseAddr)
{
	SiS_Pr->SiS_P3c4 = BaseAddr + 0x14;
	SiS_Pr->SiS_P3d4 = BaseAddr + 0x24;
	SiS_Pr->SiS_P3c0 = BaseAddr + 0x10;
	SiS_Pr->SiS_P3ce = BaseAddr + 0x1e;
	SiS_Pr->SiS_P3c2 = BaseAddr + 0x12;
	SiS_Pr->SiS_P3ca = BaseAddr + 0x1a;
	SiS_Pr->SiS_P3c6 = BaseAddr + 0x16;
	SiS_Pr->SiS_P3c7 = BaseAddr + 0x17;
	SiS_Pr->SiS_P3c8 = BaseAddr + 0x18;
	SiS_Pr->SiS_P3c9 = BaseAddr + 0x19;
	SiS_Pr->SiS_P3cb = BaseAddr + 0x1b;
	SiS_Pr->SiS_P3cc = BaseAddr + 0x1c;
	SiS_Pr->SiS_P3cd = BaseAddr + 0x1d;
	SiS_Pr->SiS_P3da = BaseAddr + 0x2a;
	SiS_Pr->SiS_Part1Port = BaseAddr + SIS_CRT2_PORT_04;
}

/*********************************************/
/*             HELPER: GetSysFlags           */
/*********************************************/

static void SiS_GetSysFlags(struct SiS_Private *SiS_Pr)
{
	SiS_Pr->SiS_MyCR63 = 0x63;
}

/*********************************************/
/*         HELPER: Init PCI & Engines        */
/*********************************************/

static void SiSInitPCIetc(struct SiS_Private *SiS_Pr)
{
	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x20, 0xa1);
	/*  - Enable 2D (0x40)
	 *  - Enable 3D (0x02)
	 *  - Enable 3D vertex command fetch (0x10)
	 *  - Enable 3D command parser (0x08)
	 *  - Enable 3D G/L transformation engine (0x80)
	 */
	SiS_SetRegOR(SiS_Pr, SiS_Pr->SiS_P3c4, 0x1E, 0xDA);
}

/*********************************************/
/*        HELPER: SET SEGMENT REGISTERS      */
/*********************************************/

static void SiS_SetSegRegLower(struct SiS_Private *SiS_Pr, unsigned short value)
{
	unsigned short temp;

	value &= 0x00ff;
	temp = SiS_GetRegByte(SiS_Pr, SiS_Pr->SiS_P3cb) & 0xf0;
	temp |= (value >> 4);
	SiS_SetRegByte(SiS_Pr, SiS_Pr->SiS_P3cb, temp);
	temp = SiS_GetRegByte(SiS_Pr, SiS_Pr->SiS_P3cd) & 0xf0;
	temp |= (value & 0x0f);
	SiS_SetRegByte(SiS_Pr, SiS_Pr->SiS_P3cd, temp);
}

static void SiS_SetSegRegUpper(struct SiS_Private *SiS_Pr, unsigned short value)
{
	unsigned short temp;

	value &= 0x00ff;
	temp = SiS_GetRegByte(SiS_Pr, SiS_Pr->SiS_P3cb) & 0x0f;
	temp |= (value & 0xf0);
	SiS_SetRegByte(SiS_Pr, SiS_Pr->SiS_P3cb, temp);
	temp = SiS_GetRegByte(SiS_Pr, SiS_Pr->SiS_P3cd) & 0x0f;
	temp |= (value << 4);
	SiS_SetRegByte(SiS_Pr, SiS_Pr->SiS_P3cd, temp);
}

static void SiS_SetSegmentReg(struct SiS_Private *SiS_Pr, unsigned short value)
{
	SiS_SetSegRegLower(SiS_Pr, value);
	SiS_SetSegRegUpper(SiS_Pr, value);
}

static void SiS_ResetSegmentReg(struct SiS_Private *SiS_Pr)
{
	SiS_SetSegmentReg(SiS_Pr, 0);
}

static void
SiS_SetSegmentRegOver(struct SiS_Private *SiS_Pr, unsigned short value)
{
	unsigned short temp = value >> 8;

	temp &= 0x07;
	temp |= (temp << 4);
	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x1d, temp);
	SiS_SetSegmentReg(SiS_Pr, value);
}

static void SiS_ResetSegmentRegOver(struct SiS_Private *SiS_Pr)
{
	SiS_SetSegmentRegOver(SiS_Pr, 0);
}

static void SiS_ResetSegmentRegisters(struct SiS_Private *SiS_Pr)
{
	SiS_ResetSegmentReg(SiS_Pr);
	SiS_ResetSegmentRegOver(SiS_Pr);
}

/*********************************************/
/*           HELPER: SearchModeID            */
/*********************************************/

static int
SiS_SearchModeID(struct SiS_Private *SiS_Pr, unsigned short *ModeNo,
		 unsigned short *ModeIdIndex)
{
	if ((*ModeNo) <= 0x13) {

		if ((*ModeNo) != 0x03)
			return 0;

		(*ModeIdIndex) = 0;

	} else {

		for (*ModeIdIndex = 0;; (*ModeIdIndex)++) {

			if (SiS_Pr->SiS_EModeIDTable[*ModeIdIndex].Ext_ModeID ==
			    (*ModeNo))
				break;

			if (SiS_Pr->SiS_EModeIDTable[*ModeIdIndex].Ext_ModeID ==
			    0xFF)
				return 0;
		}

	}

	return 1;
}

/*********************************************/
/*            HELPER: ENABLE CRT1            */
/*********************************************/

static void SiS_HandleCRT1(struct SiS_Private *SiS_Pr)
{
	/* Enable CRT1 gating */
	SiS_SetRegAND(SiS_Pr, SiS_Pr->SiS_P3d4, SiS_Pr->SiS_MyCR63, 0xbf);
}

/*********************************************/
/*           HELPER: GetColorDepth           */
/*********************************************/

static unsigned short
SiS_GetColorDepth(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		  unsigned short ModeIdIndex)
{
	static const unsigned short ColorDepth[6] = { 1, 2, 4, 4, 6, 8 };
	unsigned short modeflag;
	short index;

	if (ModeNo <= 0x13) {
		modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
	} else {
		modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
	}

	index = (modeflag & ModeTypeMask) - ModeEGA;
	if (index < 0)
		index = 0;
	return ColorDepth[index];
}

/*********************************************/
/*             HELPER: GetOffset             */
/*********************************************/

static unsigned short
SiS_GetOffset(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
	      unsigned short ModeIdIndex, unsigned short rrti)
{
	unsigned short xres, temp, colordepth, infoflag;

	infoflag = SiS_Pr->SiS_RefIndex[rrti].Ext_InfoFlag;
	xres = SiS_Pr->SiS_RefIndex[rrti].XRes;

	colordepth = SiS_GetColorDepth(SiS_Pr, ModeNo, ModeIdIndex);

	temp = xres / 16;

	if (infoflag & InterlaceMode)
		temp <<= 1;

	temp *= colordepth;

	if (xres % 16)
		temp += (colordepth >> 1);

	return temp;
}

/*********************************************/
/*                   SEQ                     */
/*********************************************/

static void
SiS_SetSeqRegs(struct SiS_Private *SiS_Pr, unsigned short StandTableIndex)
{
	unsigned char SRdata;
	int i;

	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x00, 0x03);

	SRdata = SiS_Pr->SiS_StandTable[StandTableIndex].SR[0] | 0x20;
	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x01, SRdata);

	for (i = 2; i <= 4; i++) {
		SRdata = SiS_Pr->SiS_StandTable[StandTableIndex].SR[i - 1];
		SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, i, SRdata);
	}
}

/*********************************************/
/*                  MISC                     */
/*********************************************/

static void
SiS_SetMiscRegs(struct SiS_Private *SiS_Pr, unsigned short StandTableIndex)
{
	unsigned char Miscdata = SiS_Pr->SiS_StandTable[StandTableIndex].MISC;

	SiS_SetRegByte(SiS_Pr, SiS_Pr->SiS_P3c2, Miscdata);
}

/*********************************************/
/*                  CRTC                     */
/*********************************************/

static void
SiS_SetCRTCRegs(struct SiS_Private *SiS_Pr, unsigned short StandTableIndex)
{
	unsigned char CRTCdata;
	unsigned short i;

	SiS_SetRegAND(SiS_Pr, SiS_Pr->SiS_P3d4, 0x11, 0x7f);

	for (i = 0; i <= 0x18; i++) {
		CRTCdata = SiS_Pr->SiS_StandTable[StandTableIndex].CRTC[i];
		SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3d4, i, CRTCdata);
	}
}

/*********************************************/
/*                   ATT                     */
/*********************************************/

static void
SiS_SetATTRegs(struct SiS_Private *SiS_Pr, unsigned short StandTableIndex)
{
	unsigned char ARdata;
	unsigned short i;

	for (i = 0; i <= 0x13; i++) {
		ARdata = SiS_Pr->SiS_StandTable[StandTableIndex].ATTR[i];
		SiS_GetRegByte(SiS_Pr, SiS_Pr->SiS_P3da);
		SiS_SetRegByte(SiS_Pr, SiS_Pr->SiS_P3c0, i);
		SiS_SetRegByte(SiS_Pr, SiS_Pr->SiS_P3c0, ARdata);
	}
	SiS_GetRegByte(SiS_Pr, SiS_Pr->SiS_P3da);
	SiS_SetRegByte(SiS_Pr, SiS_Pr->SiS_P3c0, 0x14);
	SiS_SetRegByte(SiS_Pr, SiS_Pr->SiS_P3c0, 0x00);

	SiS_GetRegByte(SiS_Pr, SiS_Pr->SiS_P3da);
	SiS_SetRegByte(SiS_Pr, SiS_Pr->SiS_P3c0, 0x20);
	SiS_GetRegByte(SiS_Pr, SiS_Pr->SiS_P3da);
}

/*********************************************/
/*                   GRC                     */
/*********************************************/

static void
SiS_SetGRCRegs(struct SiS_Private *SiS_Pr, unsigned short StandTableIndex)
{
	unsigned char GRdata;
	unsigned short i;

	for (i = 0; i <= 0x08; i++) {
		GRdata = SiS_Pr->SiS_StandTable[StandTableIndex].GRC[i];
		SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3ce, i, GRdata);
	}

	if (SiS_Pr->SiS_ModeType > ModeVGA) {
		/* 256 color disable */
		SiS_SetRegAND(SiS_Pr, SiS_Pr->SiS_P3ce, 0x05, 0xBF);
	}
}

/*********************************************/
/*          CLEAR EXTENDED REGISTERS         */
/*********************************************/

static void SiS_ClearExt1Regs(struct SiS_Private *SiS_Pr, unsigned short ModeNo)
{
	int i;

	for (i = 0x0A; i <= 0x0E; i++) {
		SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, i, 0x00);
	}

	SiS_SetRegAND(SiS_Pr, SiS_Pr->SiS_P3c4, 0x37, 0xFE);
}

/*********************************************/
/*              Get rate index               */
/*********************************************/

static unsigned short
SiS_GetRatePtr(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
	       unsigned short ModeIdIndex)
{
	unsigned short rrti, i, index, temp;

	if (ModeNo <= 0x13)
		return 0xFFFF;

	index = SiS_GetReg(SiS_Pr, SiS_Pr->SiS_P3d4, 0x33) & 0x0F;
	if (index > 0)
		index--;

	rrti = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].REFindex;
	ModeNo = SiS_Pr->SiS_RefIndex[rrti].ModeID;

	i = 0;
	do {
		if (SiS_Pr->SiS_RefIndex[rrti + i].ModeID != ModeNo)
			break;

		temp =
		    SiS_Pr->SiS_RefIndex[rrti + i].Ext_InfoFlag & ModeTypeMask;
		if (temp < SiS_Pr->SiS_ModeType)
			break;

		i++;
		index--;
	} while (index != 0xFFFF);

	i--;

	return (rrti + i);
}

/*********************************************/
/*                  SYNC                     */
/*********************************************/

static void SiS_SetCRT1Sync(struct SiS_Private *SiS_Pr, unsigned short rrti)
{
	unsigned short sync = SiS_Pr->SiS_RefIndex[rrti].Ext_InfoFlag >> 8;
	sync &= 0xC0;
	sync |= 0x2f;
	SiS_SetRegByte(SiS_Pr, SiS_Pr->SiS_P3c2, sync);
}

/*********************************************/
/*                  CRTC/2                   */
/*********************************************/

static void
SiS_SetCRT1CRTC(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short ModeIdIndex, unsigned short rrti)
{
	unsigned char index;
	unsigned short temp, i, j, modeflag;

	SiS_SetRegAND(SiS_Pr, SiS_Pr->SiS_P3d4, 0x11, 0x7f);

	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;

	index = SiS_Pr->SiS_RefIndex[rrti].Ext_CRT1CRTC;

	for (i = 0, j = 0; i <= 7; i++, j++) {
		SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3d4, j,
			   SiS_Pr->SiS_CRT1Table[index].CR[i]);
	}
	for (j = 0x10; i <= 10; i++, j++) {
		SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3d4, j,
			   SiS_Pr->SiS_CRT1Table[index].CR[i]);
	}
	for (j = 0x15; i <= 12; i++, j++) {
		SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3d4, j,
			   SiS_Pr->SiS_CRT1Table[index].CR[i]);
	}
	for (j = 0x0A; i <= 15; i++, j++) {
		SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, j,
			   SiS_Pr->SiS_CRT1Table[index].CR[i]);
	}

	temp = SiS_Pr->SiS_CRT1Table[index].CR[16] & 0xE0;
	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x0E, temp);

	temp = ((SiS_Pr->SiS_CRT1Table[index].CR[16]) & 0x01) << 5;
	if (modeflag & DoubleScanMode)
		temp |= 0x80;
	SiS_SetRegANDOR(SiS_Pr, SiS_Pr->SiS_P3d4, 0x09, 0x5F, temp);

	if (SiS_Pr->SiS_ModeType > ModeVGA)
		SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3d4, 0x14, 0x4F);
}

/*********************************************/
/*               OFFSET & PITCH              */
/*********************************************/
/*  (partly overruled by SetPitch() in XF86) */
/*********************************************/

static void
SiS_SetCRT1Offset(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		  unsigned short ModeIdIndex, unsigned short rrti)
{
	unsigned short du = SiS_GetOffset(SiS_Pr, ModeNo, ModeIdIndex, rrti);
	unsigned short infoflag = SiS_Pr->SiS_RefIndex[rrti].Ext_InfoFlag;
	unsigned short temp;

	temp = (du >> 8) & 0x0f;
	SiS_SetRegANDOR(SiS_Pr, SiS_Pr->SiS_P3c4, 0x0E, 0xF0, temp);

	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3d4, 0x13, (du & 0xFF));

	if (infoflag & InterlaceMode)
		du >>= 1;

	du <<= 5;
	temp = (du >> 8) & 0xff;
	if (du & 0xff)
		temp++;
	temp++;
	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x10, temp);
}

/*********************************************/
/*                  VCLK                     */
/*********************************************/

static void
SiS_SetCRT1VCLK(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short rrti)
{
	unsigned short index = SiS_Pr->SiS_RefIndex[rrti].Ext_CRTVCLK;
	unsigned short clka = SiS_Pr->SiS_VCLKData[index].SR2B;
	unsigned short clkb = SiS_Pr->SiS_VCLKData[index].SR2C;

	SiS_SetRegAND(SiS_Pr, SiS_Pr->SiS_P3c4, 0x31, 0xCF);

	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x2B, clka);
	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x2C, clkb);
	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x2D, 0x01);
}

/*********************************************/
/*                  FIFO                     */
/*********************************************/

static void
SiS_SetCRT1FIFO_310(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		    unsigned short mi)
{
	unsigned short modeflag = SiS_Pr->SiS_EModeIDTable[mi].Ext_ModeFlag;

	/* disable auto-threshold */
	SiS_SetRegAND(SiS_Pr, SiS_Pr->SiS_P3c4, 0x3D, 0xFE);

	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x08, 0xAE);
	SiS_SetRegAND(SiS_Pr, SiS_Pr->SiS_P3c4, 0x09, 0xF0);

	if (ModeNo <= 0x13)
		return;

	if ((!(modeflag & DoubleScanMode)) || (!(modeflag & HalfDCLK))) {
		SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x08, 0x34);
		SiS_SetRegOR(SiS_Pr, SiS_Pr->SiS_P3c4, 0x3D, 0x01);
	}
}

/*********************************************/
/*              MODE REGISTERS               */
/*********************************************/

static void
SiS_SetVCLKState(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		 unsigned short rrti)
{
	unsigned short data = 0, VCLK = 0, index = 0;

	if (ModeNo > 0x13) {
		index = SiS_Pr->SiS_RefIndex[rrti].Ext_CRTVCLK;
		VCLK = SiS_Pr->SiS_VCLKData[index].CLOCK;
	}

	if (VCLK >= 166)
		data |= 0x0c;
	SiS_SetRegANDOR(SiS_Pr, SiS_Pr->SiS_P3c4, 0x32, 0xf3, data);

	if (VCLK >= 166)
		SiS_SetRegAND(SiS_Pr, SiS_Pr->SiS_P3c4, 0x1f, 0xe7);

	/* DAC speed */
	data = 0x03;
	if (VCLK >= 260)
		data = 0x00;
	else if (VCLK >= 160)
		data = 0x01;
	else if (VCLK >= 135)
		data = 0x02;

	SiS_SetRegANDOR(SiS_Pr, SiS_Pr->SiS_P3c4, 0x07, 0xF8, data);
}

static void
SiS_SetCRT1ModeRegs(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		    unsigned short ModeIdIndex, unsigned short rrti)
{
	unsigned short data, infoflag = 0, modeflag;

	if (ModeNo <= 0x13)
		modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
	else {
		modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
		infoflag = SiS_Pr->SiS_RefIndex[rrti].Ext_InfoFlag;
	}

	/* Disable DPMS */
	SiS_SetRegAND(SiS_Pr, SiS_Pr->SiS_P3c4, 0x1F, 0x3F);

	data = 0;
	if (ModeNo > 0x13) {
		if (SiS_Pr->SiS_ModeType > ModeEGA) {
			data |= 0x02;
			data |= ((SiS_Pr->SiS_ModeType - ModeVGA) << 2);
		}
		if (infoflag & InterlaceMode)
			data |= 0x20;
	}
	SiS_SetRegANDOR(SiS_Pr, SiS_Pr->SiS_P3c4, 0x06, 0xC0, data);

	data = 0;
	if (infoflag & InterlaceMode) {
		/* data = (Hsync / 8) - ((Htotal / 8) / 2) + 3 */
		unsigned short hrs =
		    (SiS_GetReg(SiS_Pr, SiS_Pr->SiS_P3d4, 0x04) |
		     ((SiS_GetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x0b) & 0xc0) << 2))
		    - 3;
		unsigned short hto =
		    (SiS_GetReg(SiS_Pr, SiS_Pr->SiS_P3d4, 0x00) |
		     ((SiS_GetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x0b) & 0x03) << 8))
		    + 5;
		data = hrs - (hto >> 1) + 3;
	}
	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3d4, 0x19, (data & 0xFF));
	SiS_SetRegANDOR(SiS_Pr, SiS_Pr->SiS_P3d4, 0x1a, 0xFC, (data >> 8));

	if (modeflag & HalfDCLK)
		SiS_SetRegOR(SiS_Pr, SiS_Pr->SiS_P3c4, 0x01, 0x08);

	data = 0;
	if (modeflag & LineCompareOff)
		data = 0x08;
	SiS_SetRegANDOR(SiS_Pr, SiS_Pr->SiS_P3c4, 0x0F, 0xB7, data);

	if ((SiS_Pr->SiS_ModeType == ModeEGA) && (ModeNo > 0x13))
		SiS_SetRegOR(SiS_Pr, SiS_Pr->SiS_P3c4, 0x0F, 0x40);

	SiS_SetRegAND(SiS_Pr, SiS_Pr->SiS_P3c4, 0x31, 0xfb);

	data = 0x60;
	if (SiS_Pr->SiS_ModeType != ModeText) {
		data ^= 0x60;
		if (SiS_Pr->SiS_ModeType != ModeEGA)
			data ^= 0xA0;
	}
	SiS_SetRegANDOR(SiS_Pr, SiS_Pr->SiS_P3c4, 0x21, 0x1F, data);

	SiS_SetVCLKState(SiS_Pr, ModeNo, rrti);

	if (SiS_GetReg(SiS_Pr, SiS_Pr->SiS_P3d4, 0x31) & 0x40)
		SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3d4, 0x52, 0x2c);
	else
		SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3d4, 0x52, 0x6c);
}

/*********************************************/
/*                 LOAD DAC                  */
/*********************************************/

static void
SiS_WriteDAC(struct SiS_Private *SiS_Pr, unsigned long DACData,
	     unsigned short shiftflag, unsigned short dl, unsigned short ah,
	     unsigned short al, unsigned short dh)
{
	unsigned short d1, d2, d3;

	switch (dl) {
	case 0:
		d1 = dh;
		d2 = ah;
		d3 = al;
		break;
	case 1:
		d1 = ah;
		d2 = al;
		d3 = dh;
		break;
	default:
		d1 = al;
		d2 = dh;
		d3 = ah;
	}
	SiS_SetRegByte(SiS_Pr, DACData, (d1 << shiftflag));
	SiS_SetRegByte(SiS_Pr, DACData, (d2 << shiftflag));
	SiS_SetRegByte(SiS_Pr, DACData, (d3 << shiftflag));
}

static void
SiS_LoadDAC(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
	    unsigned short mi)
{
	unsigned short data, data2, time, i, j, k, m, n, o;
	unsigned short si, di, bx, sf;
	unsigned long DACAddr, DACData;
	const unsigned char *table = NULL;

	if (ModeNo < 0x13)
		data = SiS_Pr->SiS_SModeIDTable[mi].St_ModeFlag;
	else
		data = SiS_Pr->SiS_EModeIDTable[mi].Ext_ModeFlag;

	data &= DACInfoFlag;

	j = time = 64;
	if (data == 0x00)
		table = SiS_MDA_DAC;
	else if (data == 0x08)
		table = SiS_CGA_DAC;
	else if (data == 0x10)
		table = SiS_EGA_DAC;
	else {
		j = 16;
		time = 256;
		table = SiS_VGA_DAC;
	}

	DACAddr = SiS_Pr->SiS_P3c8;
	DACData = SiS_Pr->SiS_P3c9;
	sf = 0;
	SiS_SetRegByte(SiS_Pr, SiS_Pr->SiS_P3c6, 0xFF);

	SiS_SetRegByte(SiS_Pr, DACAddr, 0x00);

	for (i = 0; i < j; i++) {
		data = table[i];
		for (k = 0; k < 3; k++) {
			data2 = 0;
			if (data & 0x01)
				data2 += 0x2A;
			if (data & 0x02)
				data2 += 0x15;
			SiS_SetRegByte(SiS_Pr, DACData, (data2 << sf));
			data >>= 2;
		}
	}

	if (time == 256) {
		for (i = 16; i < 32; i++) {
			data = table[i] << sf;
			for (k = 0; k < 3; k++)
				SiS_SetRegByte(SiS_Pr, DACData, data);
		}
		si = 32;
		for (m = 0; m < 9; m++) {
			di = si;
			bx = si + 4;
			for (n = 0; n < 3; n++) {
				for (o = 0; o < 5; o++) {
					SiS_WriteDAC(SiS_Pr, DACData, sf, n,
						     table[di], table[bx],
						     table[si]);
					si++;
				}
				si -= 2;
				for (o = 0; o < 3; o++) {
					SiS_WriteDAC(SiS_Pr, DACData, sf, n,
						     table[di], table[si],
						     table[bx]);
					si--;
				}
			}
			si += 5;
		}
	}
}

/*********************************************/
/*         SET CRT1 REGISTER GROUP           */
/*********************************************/

static void
SiS_SetCRT1Group(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		 unsigned short ModeIdIndex)
{
	unsigned short StandTableIndex, rrti;

	SiS_Pr->SiS_CRT1Mode = ModeNo;

	if (ModeNo <= 0x13)
		StandTableIndex = 0;
	else
		StandTableIndex = 1;

	SiS_ResetSegmentRegisters(SiS_Pr);
	SiS_SetSeqRegs(SiS_Pr, StandTableIndex);
	SiS_SetMiscRegs(SiS_Pr, StandTableIndex);
	SiS_SetCRTCRegs(SiS_Pr, StandTableIndex);
	SiS_SetATTRegs(SiS_Pr, StandTableIndex);
	SiS_SetGRCRegs(SiS_Pr, StandTableIndex);
	SiS_ClearExt1Regs(SiS_Pr, ModeNo);

	rrti = SiS_GetRatePtr(SiS_Pr, ModeNo, ModeIdIndex);

	if (rrti != 0xFFFF) {
		SiS_SetCRT1Sync(SiS_Pr, rrti);
		SiS_SetCRT1CRTC(SiS_Pr, ModeNo, ModeIdIndex, rrti);
		SiS_SetCRT1Offset(SiS_Pr, ModeNo, ModeIdIndex, rrti);
		SiS_SetCRT1VCLK(SiS_Pr, ModeNo, rrti);
	}

	SiS_SetCRT1FIFO_310(SiS_Pr, ModeNo, ModeIdIndex);

	SiS_SetCRT1ModeRegs(SiS_Pr, ModeNo, ModeIdIndex, rrti);

	SiS_LoadDAC(SiS_Pr, ModeNo, ModeIdIndex);

	SiS_DisplayOn(SiS_Pr);
}

/*********************************************/
/*                 SiSSetMode()              */
/*********************************************/

int SiSUSBSetMode(struct SiS_Private *SiS_Pr, unsigned short ModeNo)
{
	unsigned short ModeIdIndex;
	unsigned long BaseAddr = SiS_Pr->IOAddress;

	SiSUSB_InitPtr(SiS_Pr);
	SiSUSBRegInit(SiS_Pr, BaseAddr);
	SiS_GetSysFlags(SiS_Pr);

	if (!(SiS_SearchModeID(SiS_Pr, &ModeNo, &ModeIdIndex)))
		return 0;

	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3c4, 0x05, 0x86);

	SiSInitPCIetc(SiS_Pr);

	ModeNo &= 0x7f;

	SiS_Pr->SiS_ModeType =
	    SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag & ModeTypeMask;

	SiS_Pr->SiS_SetFlag = LowModeTests;

	/* Set mode on CRT1 */
	SiS_SetCRT1Group(SiS_Pr, ModeNo, ModeIdIndex);

	SiS_HandleCRT1(SiS_Pr);

	SiS_DisplayOn(SiS_Pr);
	SiS_SetRegByte(SiS_Pr, SiS_Pr->SiS_P3c6, 0xFF);

	/* Store mode number */
	SiS_SetReg(SiS_Pr, SiS_Pr->SiS_P3d4, 0x34, ModeNo);

	return 1;
}

int SiSUSBSetVESAMode(struct SiS_Private *SiS_Pr, unsigned short VModeNo)
{
	unsigned short ModeNo = 0;
	int i;

	SiSUSB_InitPtr(SiS_Pr);

	if (VModeNo == 0x03) {

		ModeNo = 0x03;

	} else {

		i = 0;
		do {

			if (SiS_Pr->SiS_EModeIDTable[i].Ext_VESAID == VModeNo) {
				ModeNo = SiS_Pr->SiS_EModeIDTable[i].Ext_ModeID;
				break;
			}

		} while (SiS_Pr->SiS_EModeIDTable[i++].Ext_ModeID != 0xff);

	}

	if (!ModeNo)
		return 0;

	return SiSUSBSetMode(SiS_Pr, ModeNo);
}
