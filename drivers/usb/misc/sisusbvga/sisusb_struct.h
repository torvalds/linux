/*
 * General structure definitions for universal mode switching modules
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
 * Author: 	Thomas Winischhofer <thomas@winischhofer.net>
 *
 */

#ifndef _SISUSB_STRUCT_H_
#define _SISUSB_STRUCT_H_

struct SiS_St {
	unsigned char	St_ModeID;
	unsigned short	St_ModeFlag;
	unsigned char	St_StTableIndex;
	unsigned char	St_CRT2CRTC;
	unsigned char	St_ResInfo;
	unsigned char	VB_StTVFlickerIndex;
	unsigned char	VB_StTVEdgeIndex;
	unsigned char	VB_StTVYFilterIndex;
	unsigned char	St_PDC;
};

struct SiS_StandTable
{
	unsigned char	CRT_COLS;
	unsigned char	ROWS;
	unsigned char	CHAR_HEIGHT;
	unsigned short	CRT_LEN;
	unsigned char	SR[4];
	unsigned char	MISC;
	unsigned char	CRTC[0x19];
	unsigned char	ATTR[0x14];
	unsigned char	GRC[9];
};

struct SiS_StResInfo_S {
	unsigned short	HTotal;
	unsigned short	VTotal;
};

struct SiS_Ext
{
	unsigned char	Ext_ModeID;
	unsigned short	Ext_ModeFlag;
	unsigned short	Ext_VESAID;
	unsigned char	Ext_RESINFO;
	unsigned char	VB_ExtTVFlickerIndex;
	unsigned char	VB_ExtTVEdgeIndex;
	unsigned char	VB_ExtTVYFilterIndex;
	unsigned char	VB_ExtTVYFilterIndexROM661;
	unsigned char	REFindex;
	char		ROMMODEIDX661;
};

struct SiS_Ext2
{
	unsigned short	Ext_InfoFlag;
	unsigned char	Ext_CRT1CRTC;
	unsigned char	Ext_CRTVCLK;
	unsigned char	Ext_CRT2CRTC;
	unsigned char	Ext_CRT2CRTC_NS;
	unsigned char	ModeID;
	unsigned short	XRes;
	unsigned short	YRes;
	unsigned char	Ext_PDC;
	unsigned char	Ext_FakeCRT2CRTC;
	unsigned char	Ext_FakeCRT2Clk;
};

struct SiS_CRT1Table
{
	unsigned char	CR[17];
};

struct SiS_VCLKData
{
	unsigned char	SR2B,SR2C;
	unsigned short	CLOCK;
};

struct SiS_ModeResInfo
{
	unsigned short	HTotal;
	unsigned short	VTotal;
	unsigned char	XChar;
	unsigned char	YChar;
};

struct SiS_Private
{
	void *sisusb;

	unsigned long IOAddress;

	unsigned long SiS_P3c4;
	unsigned long SiS_P3d4;
	unsigned long SiS_P3c0;
	unsigned long SiS_P3ce;
	unsigned long SiS_P3c2;
	unsigned long SiS_P3ca;
	unsigned long SiS_P3c6;
	unsigned long SiS_P3c7;
	unsigned long SiS_P3c8;
	unsigned long SiS_P3c9;
	unsigned long SiS_P3cb;
	unsigned long SiS_P3cc;
	unsigned long SiS_P3cd;
	unsigned long SiS_P3da;
	unsigned long SiS_Part1Port;

	unsigned char	SiS_MyCR63;
	unsigned short	SiS_CRT1Mode;
	unsigned short	SiS_ModeType;
	unsigned short	SiS_SetFlag;

	const struct SiS_StandTable	*SiS_StandTable;
	const struct SiS_St		*SiS_SModeIDTable;
	const struct SiS_Ext		*SiS_EModeIDTable;
	const struct SiS_Ext2		*SiS_RefIndex;
	const struct SiS_CRT1Table	*SiS_CRT1Table;
	struct SiS_VCLKData		*SiS_VCLKData;
	const struct SiS_ModeResInfo	*SiS_ModeResInfo;
};

#endif

