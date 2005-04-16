/* $XFree86$ */
/* $XdotOrg$ */
/*
 * General structure definitions for universal mode switching modules
 *
 * Copyright (C) 2001-2004 by Thomas Winischhofer, Vienna, Austria
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

#ifndef _VSTRUCT_
#define _VSTRUCT_

typedef struct _SiS_PanelDelayTblStruct
{
 	UCHAR timer[2];
} SiS_PanelDelayTblStruct;

typedef struct _SiS_LCDDataStruct
{
	USHORT RVBHCMAX;
	USHORT RVBHCFACT;
	USHORT VGAHT;
	USHORT VGAVT;
	USHORT LCDHT;
	USHORT LCDVT;
} SiS_LCDDataStruct;

typedef struct _SiS_TVDataStruct
{
	USHORT RVBHCMAX;
	USHORT RVBHCFACT;
	USHORT VGAHT;
	USHORT VGAVT;
	USHORT TVHDE;
	USHORT TVVDE;
	USHORT RVBHRS;
	UCHAR  FlickerMode;
	USHORT HALFRVBHRS;
	UCHAR  RY1COE;
	UCHAR  RY2COE;
	UCHAR  RY3COE;
	UCHAR  RY4COE;
} SiS_TVDataStruct;

typedef struct _SiS_LVDSDataStruct
{
	USHORT VGAHT;
	USHORT VGAVT;
	USHORT LCDHT;
	USHORT LCDVT;
} SiS_LVDSDataStruct;

typedef struct _SiS_LVDSDesStruct
{
	USHORT LCDHDES;
	USHORT LCDVDES;
} SiS_LVDSDesStruct;

typedef struct _SiS_LVDSCRT1DataStruct
{
	UCHAR  CR[15];
} SiS_LVDSCRT1DataStruct;

typedef struct _SiS_LCDACRT1DataStruct
{
	UCHAR  CR[17];
} SiS_LCDACRT1DataStruct;

typedef struct _SiS_CHTVRegDataStruct
{
	UCHAR  Reg[16];
} SiS_CHTVRegDataStruct;

typedef struct _SiS_StStruct
{
	UCHAR  St_ModeID;
	USHORT St_ModeFlag;
	UCHAR  St_StTableIndex;
	UCHAR  St_CRT2CRTC;
	UCHAR  St_ResInfo;
	UCHAR  VB_StTVFlickerIndex;
	UCHAR  VB_StTVEdgeIndex;
	UCHAR  VB_StTVYFilterIndex;
	UCHAR  St_PDC;
} SiS_StStruct;

typedef struct _SiS_VBModeStruct
{
	UCHAR  ModeID;
	UCHAR  VB_TVDelayIndex;
	UCHAR  VB_TVFlickerIndex;
	UCHAR  VB_TVPhaseIndex;
	UCHAR  VB_TVYFilterIndex;
	UCHAR  VB_LCDDelayIndex;
	UCHAR  _VB_LCDHIndex;
	UCHAR  _VB_LCDVIndex;
} SiS_VBModeStruct;

typedef struct _SiS_StandTableStruct
{
	UCHAR  CRT_COLS;
	UCHAR  ROWS;
	UCHAR  CHAR_HEIGHT;
	USHORT CRT_LEN;
	UCHAR  SR[4];
	UCHAR  MISC;
	UCHAR  CRTC[0x19];
	UCHAR  ATTR[0x14];
	UCHAR  GRC[9];
} SiS_StandTableStruct;

typedef struct _SiS_ExtStruct
{
	UCHAR  Ext_ModeID;
	USHORT Ext_ModeFlag;
	USHORT Ext_VESAID;
	UCHAR  Ext_RESINFO;
	UCHAR  VB_ExtTVFlickerIndex;
	UCHAR  VB_ExtTVEdgeIndex;
	UCHAR  VB_ExtTVYFilterIndex;
	UCHAR  VB_ExtTVYFilterIndexROM661;
	UCHAR  REFindex;
	CHAR   ROMMODEIDX661;
} SiS_ExtStruct;

typedef struct _SiS_Ext2Struct
{
	USHORT Ext_InfoFlag;
	UCHAR  Ext_CRT1CRTC;
	UCHAR  Ext_CRTVCLK;
	UCHAR  Ext_CRT2CRTC;
	UCHAR  Ext_CRT2CRTC_NS;
	UCHAR  ModeID;
	USHORT XRes;
	USHORT YRes;
	UCHAR  Ext_PDC;
} SiS_Ext2Struct;

typedef struct _SiS_Part2PortTblStruct
{
 	UCHAR  CR[12];
} SiS_Part2PortTblStruct;

typedef struct _SiS_CRT1TableStruct
{
	UCHAR  CR[17];
} SiS_CRT1TableStruct;

typedef struct _SiS_MCLKDataStruct
{
	UCHAR  SR28,SR29,SR2A;
	USHORT CLOCK;
} SiS_MCLKDataStruct;

typedef struct _SiS_VCLKDataStruct
{
	UCHAR  SR2B,SR2C;
	USHORT CLOCK;
} SiS_VCLKDataStruct;

typedef struct _SiS_VBVCLKDataStruct
{
	UCHAR  Part4_A,Part4_B;
	USHORT CLOCK;
} SiS_VBVCLKDataStruct;

typedef struct _SiS_StResInfoStruct
{
	USHORT HTotal;
	USHORT VTotal;
} SiS_StResInfoStruct;

typedef struct _SiS_ModeResInfoStruct
{
	USHORT HTotal;
	USHORT VTotal;
	UCHAR  XChar;
	UCHAR  YChar;
} SiS_ModeResInfoStruct;



typedef UCHAR DRAM4Type[4];

/* Defines for SiS_CustomT */
/* Never change these for sisfb compatibility */
#define CUT_NONE          0
#define CUT_FORCENONE     1
#define CUT_BARCO1366     2
#define CUT_BARCO1024     3
#define CUT_COMPAQ1280    4
#define CUT_COMPAQ12802   5
#define CUT_PANEL848      6
#define CUT_CLEVO1024     7
#define CUT_CLEVO10242    8
#define CUT_CLEVO1400     9
#define CUT_CLEVO14002    10
#define CUT_UNIWILL1024   11
#define CUT_ASUSL3000D    12
#define CUT_UNIWILL10242  13
#define CUT_ACER1280      14
#define CUT_COMPAL1400_1  15
#define CUT_COMPAL1400_2  16
#define CUT_ASUSA2H_1     17
#define CUT_ASUSA2H_2     18

typedef struct _SiS_Private
{
#ifdef LINUX_KERNEL
        SISIOADDRESS RelIO;
#endif
	SISIOADDRESS SiS_P3c4;
	SISIOADDRESS SiS_P3d4;
	SISIOADDRESS SiS_P3c0;
	SISIOADDRESS SiS_P3ce;
	SISIOADDRESS SiS_P3c2;
	SISIOADDRESS SiS_P3ca;
	SISIOADDRESS SiS_P3c6;
	SISIOADDRESS SiS_P3c7;
	SISIOADDRESS SiS_P3c8;
	SISIOADDRESS SiS_P3c9;
	SISIOADDRESS SiS_P3cb;
	SISIOADDRESS SiS_P3cd;
	SISIOADDRESS SiS_P3da;
	SISIOADDRESS SiS_Part1Port;
	SISIOADDRESS SiS_Part2Port;
	SISIOADDRESS SiS_Part3Port;
	SISIOADDRESS SiS_Part4Port;
	SISIOADDRESS SiS_Part5Port;
	SISIOADDRESS SiS_VidCapt;
	SISIOADDRESS SiS_VidPlay;
	USHORT SiS_IF_DEF_LVDS;
	USHORT SiS_IF_DEF_CH70xx;
	USHORT SiS_IF_DEF_CONEX;
	USHORT SiS_IF_DEF_TRUMPION;
	USHORT SiS_IF_DEF_DSTN;
	USHORT SiS_IF_DEF_FSTN;
	USHORT SiS_SysFlags;
	UCHAR  SiS_VGAINFO;
#ifdef LINUX_XF86
        USHORT SiS_CP1, SiS_CP2, SiS_CP3, SiS_CP4;
#endif
	BOOLEAN SiS_UseROM;
	BOOLEAN SiS_ROMNew;
	BOOLEAN SiS_NeedRomModeData;
	BOOLEAN PanelSelfDetected;
	int     SiS_CHOverScan;
	BOOLEAN SiS_CHSOverScan;
	BOOLEAN SiS_ChSW;
	BOOLEAN SiS_UseLCDA;
	int     SiS_UseOEM;
	ULONG   SiS_CustomT;
	USHORT  SiS_Backup70xx;
	BOOLEAN HaveEMI;
	BOOLEAN HaveEMILCD;
	BOOLEAN OverruleEMI;
	UCHAR  EMI_30,EMI_31,EMI_32,EMI_33;
	USHORT SiS_EMIOffset;
	SHORT  PDC, PDCA;
	UCHAR  SiS_MyCR63;
	USHORT SiS_CRT1Mode;
	USHORT SiS_flag_clearbuffer;
	int    SiS_RAMType;
	UCHAR  SiS_ChannelAB;
	UCHAR  SiS_DataBusWidth;
	USHORT SiS_ModeType;
	USHORT SiS_VBInfo;
	USHORT SiS_TVMode;
	USHORT SiS_LCDResInfo;
	USHORT SiS_LCDTypeInfo;
	USHORT SiS_LCDInfo;
	USHORT SiS_LCDInfo661;
	USHORT SiS_VBType;
	USHORT SiS_VBExtInfo;
	USHORT SiS_YPbPr;
	USHORT SiS_SelectCRT2Rate;
	USHORT SiS_SetFlag;
	USHORT SiS_RVBHCFACT;
	USHORT SiS_RVBHCMAX;
	USHORT SiS_RVBHRS;
	USHORT SiS_VGAVT;
	USHORT SiS_VGAHT;
	USHORT SiS_VT;
	USHORT SiS_HT;
	USHORT SiS_VGAVDE;
	USHORT SiS_VGAHDE;
	USHORT SiS_VDE;
	USHORT SiS_HDE;
	USHORT SiS_NewFlickerMode;
	USHORT SiS_RY1COE;
	USHORT SiS_RY2COE;
	USHORT SiS_RY3COE;
	USHORT SiS_RY4COE;
	USHORT SiS_LCDHDES;
	USHORT SiS_LCDVDES;
	USHORT SiS_DDC_Port;
	USHORT SiS_DDC_Index;
	USHORT SiS_DDC_Data;
	USHORT SiS_DDC_NData;
	USHORT SiS_DDC_Clk;
	USHORT SiS_DDC_NClk;
	USHORT SiS_DDC_DeviceAddr;
	USHORT SiS_DDC_ReadAddr;
	USHORT SiS_DDC_SecAddr;
	USHORT SiS_ChrontelInit;
	BOOLEAN SiS_SensibleSR11;
	USHORT SiS661LCD2TableSize;

	USHORT SiS_PanelMinLVDS;
	USHORT SiS_PanelMin301;

	const SiS_StStruct          *SiS_SModeIDTable;
	const SiS_StandTableStruct  *SiS_StandTable;
	const SiS_ExtStruct         *SiS_EModeIDTable;
	const SiS_Ext2Struct        *SiS_RefIndex;
	const SiS_VBModeStruct      *SiS_VBModeIDTable;
	const SiS_CRT1TableStruct   *SiS_CRT1Table;
	const SiS_MCLKDataStruct    *SiS_MCLKData_0;
	const SiS_MCLKDataStruct    *SiS_MCLKData_1;
	SiS_VCLKDataStruct    	    *SiS_VCLKData;
	SiS_VBVCLKDataStruct        *SiS_VBVCLKData;
	const SiS_StResInfoStruct   *SiS_StResInfo;
	const SiS_ModeResInfoStruct *SiS_ModeResInfo;

	const UCHAR                 *pSiS_OutputSelect;
	const UCHAR                 *pSiS_SoftSetting;

	const DRAM4Type *SiS_SR15; /* pointer : point to array */
#ifdef LINUX_KERNEL
	UCHAR *pSiS_SR07;
	const DRAM4Type *SiS_CR40; /* pointer : point to array */
	UCHAR *SiS_CR49;
	UCHAR *SiS_SR25;
	UCHAR *pSiS_SR1F;
	UCHAR *pSiS_SR21;
	UCHAR *pSiS_SR22;
	UCHAR *pSiS_SR23;
	UCHAR *pSiS_SR24;
	UCHAR *pSiS_SR31;
	UCHAR *pSiS_SR32;
	UCHAR *pSiS_SR33;
	UCHAR *pSiS_CRT2Data_1_2;
	UCHAR *pSiS_CRT2Data_4_D;
	UCHAR *pSiS_CRT2Data_4_E;
	UCHAR *pSiS_CRT2Data_4_10;
	const USHORT *pSiS_RGBSenseData;
	const USHORT *pSiS_VideoSenseData;
	const USHORT *pSiS_YCSenseData;
	const USHORT *pSiS_RGBSenseData2;
	const USHORT *pSiS_VideoSenseData2;
	const USHORT *pSiS_YCSenseData2;
#endif

	const SiS_PanelDelayTblStruct *SiS_PanelDelayTbl;
	const SiS_PanelDelayTblStruct *SiS_PanelDelayTblLVDS;

	/* SiS bridge */

	const UCHAR *SiS_NTSCPhase;
	const UCHAR *SiS_PALPhase;
	const UCHAR *SiS_NTSCPhase2;
	const UCHAR *SiS_PALPhase2;
	const UCHAR *SiS_PALMPhase;
	const UCHAR *SiS_PALNPhase;
	const UCHAR *SiS_PALMPhase2;
	const UCHAR *SiS_PALNPhase2;
	const UCHAR *SiS_SpecialPhase;
	const UCHAR *SiS_SpecialPhaseM;
	const UCHAR *SiS_SpecialPhaseJ;
	const SiS_LCDDataStruct  *SiS_ExtLCD1024x768Data;
	const SiS_LCDDataStruct  *SiS_St2LCD1024x768Data;
	const SiS_LCDDataStruct  *SiS_LCD1280x720Data;
	const SiS_LCDDataStruct  *SiS_StLCD1280x768_2Data;
	const SiS_LCDDataStruct  *SiS_ExtLCD1280x768_2Data;
	const SiS_LCDDataStruct  *SiS_LCD1280x800Data;
	const SiS_LCDDataStruct  *SiS_LCD1280x800_2Data;
	const SiS_LCDDataStruct  *SiS_LCD1280x960Data;
	const SiS_LCDDataStruct  *SiS_ExtLCD1280x1024Data;
	const SiS_LCDDataStruct  *SiS_St2LCD1280x1024Data;
	const SiS_LCDDataStruct  *SiS_StLCD1400x1050Data;
	const SiS_LCDDataStruct  *SiS_ExtLCD1400x1050Data;
	const SiS_LCDDataStruct  *SiS_StLCD1600x1200Data;
	const SiS_LCDDataStruct  *SiS_ExtLCD1600x1200Data;
	const SiS_LCDDataStruct  *SiS_LCD1680x1050Data;
	const SiS_LCDDataStruct  *SiS_NoScaleData;
	const SiS_TVDataStruct   *SiS_StPALData;
	const SiS_TVDataStruct   *SiS_ExtPALData;
	const SiS_TVDataStruct   *SiS_StNTSCData;
	const SiS_TVDataStruct   *SiS_ExtNTSCData;
	const SiS_TVDataStruct   *SiS_St1HiTVData;
	const SiS_TVDataStruct   *SiS_St2HiTVData;
	const SiS_TVDataStruct   *SiS_ExtHiTVData;
	const SiS_TVDataStruct   *SiS_St525iData;
	const SiS_TVDataStruct   *SiS_St525pData;
	const SiS_TVDataStruct   *SiS_St750pData;
	const SiS_TVDataStruct   *SiS_Ext525iData;
	const SiS_TVDataStruct   *SiS_Ext525pData;
	const SiS_TVDataStruct   *SiS_Ext750pData;
	const UCHAR *SiS_NTSCTiming;
	const UCHAR *SiS_PALTiming;
	const UCHAR *SiS_HiTVExtTiming;
	const UCHAR *SiS_HiTVSt1Timing;
	const UCHAR *SiS_HiTVSt2Timing;
	const UCHAR *SiS_HiTVGroup3Data;
	const UCHAR *SiS_HiTVGroup3Simu;
#if 0
	const UCHAR *SiS_HiTVTextTiming;
	const UCHAR *SiS_HiTVGroup3Text;
#endif

	const SiS_Part2PortTblStruct *SiS_CRT2Part2_1024x768_1;
	const SiS_Part2PortTblStruct *SiS_CRT2Part2_1280x1024_1;
	const SiS_Part2PortTblStruct *SiS_CRT2Part2_1024x768_2;
	const SiS_Part2PortTblStruct *SiS_CRT2Part2_1280x1024_2;
	const SiS_Part2PortTblStruct *SiS_CRT2Part2_1024x768_3;
	const SiS_Part2PortTblStruct *SiS_CRT2Part2_1280x1024_3;

	/* LVDS, Chrontel */

	const SiS_LVDSDataStruct  *SiS_LVDS800x600Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDS800x600Data_2;
	const SiS_LVDSDataStruct  *SiS_LVDS1024x768Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDS1024x768Data_2;
	const SiS_LVDSDataStruct  *SiS_LVDS1280x1024Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDS1280x1024Data_2;
	const SiS_LVDSDataStruct  *SiS_LVDS1280x960Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDS1280x960Data_2;
	const SiS_LVDSDataStruct  *SiS_LVDS1400x1050Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDS1400x1050Data_2;
	const SiS_LVDSDataStruct  *SiS_LVDS1600x1200Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDS1600x1200Data_2;
	const SiS_LVDSDataStruct  *SiS_LVDS1280x768Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDS1280x768Data_2;
	const SiS_LVDSDataStruct  *SiS_LVDS1024x600Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDS1024x600Data_2;
	const SiS_LVDSDataStruct  *SiS_LVDS1152x768Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDS1152x768Data_2;
	const SiS_LVDSDataStruct  *SiS_LVDS640x480Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDS640x480Data_2;
	const SiS_LVDSDataStruct  *SiS_LVDS320x480Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDSXXXxXXXData_1;
	const SiS_LVDSDataStruct  *SiS_LVDSBARCO1366Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDSBARCO1366Data_2;
	const SiS_LVDSDataStruct  *SiS_LVDSBARCO1024Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDSBARCO1024Data_2;
	const SiS_LVDSDataStruct  *SiS_LVDS848x480Data_1;
	const SiS_LVDSDataStruct  *SiS_LVDS848x480Data_2;
	const SiS_LVDSDataStruct  *SiS_CHTVUNTSCData;
	const SiS_LVDSDataStruct  *SiS_CHTVONTSCData;
	const SiS_LVDSDataStruct  *SiS_CHTVUPALData;
	const SiS_LVDSDataStruct  *SiS_CHTVOPALData;
	const SiS_LVDSDataStruct  *SiS_CHTVUPALMData;
	const SiS_LVDSDataStruct  *SiS_CHTVOPALMData;
	const SiS_LVDSDataStruct  *SiS_CHTVUPALNData;
	const SiS_LVDSDataStruct  *SiS_CHTVOPALNData;
	const SiS_LVDSDataStruct  *SiS_CHTVSOPALData;

	const SiS_LVDSDesStruct  *SiS_PanelType00_1;
	const SiS_LVDSDesStruct  *SiS_PanelType01_1;
	const SiS_LVDSDesStruct  *SiS_PanelType02_1;
	const SiS_LVDSDesStruct  *SiS_PanelType03_1;
	const SiS_LVDSDesStruct  *SiS_PanelType04_1;
	const SiS_LVDSDesStruct  *SiS_PanelType05_1;
	const SiS_LVDSDesStruct  *SiS_PanelType06_1;
	const SiS_LVDSDesStruct  *SiS_PanelType07_1;
	const SiS_LVDSDesStruct  *SiS_PanelType08_1;
	const SiS_LVDSDesStruct  *SiS_PanelType09_1;
	const SiS_LVDSDesStruct  *SiS_PanelType0a_1;
	const SiS_LVDSDesStruct  *SiS_PanelType0b_1;
	const SiS_LVDSDesStruct  *SiS_PanelType0c_1;
	const SiS_LVDSDesStruct  *SiS_PanelType0d_1;
	const SiS_LVDSDesStruct  *SiS_PanelType0e_1;
	const SiS_LVDSDesStruct  *SiS_PanelType0f_1;
	const SiS_LVDSDesStruct  *SiS_PanelTypeNS_1;
	const SiS_LVDSDesStruct  *SiS_PanelType00_2;
	const SiS_LVDSDesStruct  *SiS_PanelType01_2;
	const SiS_LVDSDesStruct  *SiS_PanelType02_2;
	const SiS_LVDSDesStruct  *SiS_PanelType03_2;
	const SiS_LVDSDesStruct  *SiS_PanelType04_2;
	const SiS_LVDSDesStruct  *SiS_PanelType05_2;
	const SiS_LVDSDesStruct  *SiS_PanelType06_2;
	const SiS_LVDSDesStruct  *SiS_PanelType07_2;
	const SiS_LVDSDesStruct  *SiS_PanelType08_2;
	const SiS_LVDSDesStruct  *SiS_PanelType09_2;
	const SiS_LVDSDesStruct  *SiS_PanelType0a_2;
	const SiS_LVDSDesStruct  *SiS_PanelType0b_2;
	const SiS_LVDSDesStruct  *SiS_PanelType0c_2;
	const SiS_LVDSDesStruct  *SiS_PanelType0d_2;
	const SiS_LVDSDesStruct  *SiS_PanelType0e_2;
	const SiS_LVDSDesStruct  *SiS_PanelType0f_2;
	const SiS_LVDSDesStruct  *SiS_PanelTypeNS_2;
	const SiS_LVDSDesStruct  *SiS_CHTVUNTSCDesData;
	const SiS_LVDSDesStruct  *SiS_CHTVONTSCDesData;
	const SiS_LVDSDesStruct  *SiS_CHTVUPALDesData;
	const SiS_LVDSDesStruct  *SiS_CHTVOPALDesData;

	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1800x600_1;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x768_1;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11280x1024_1;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11400x1050_1;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11280x768_1;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x600_1;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11152x768_1;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11600x1200_1;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1800x600_1_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x768_1_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11280x1024_1_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11400x1050_1_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11280x768_1_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x600_1_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11152x768_1_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11600x1200_1_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1800x600_2;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x768_2;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11280x1024_2;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11400x1050_2;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11280x768_2;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x600_2;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11152x768_2;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11600x1200_2;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1800x600_2_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x768_2_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11280x1024_2_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11400x1050_2_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11280x768_2_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11024x600_2_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11152x768_2_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT11600x1200_2_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1XXXxXXX_1;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1XXXxXXX_1_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1640x480_1;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1640x480_1_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1640x480_2;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1640x480_2_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1640x480_3;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1640x480_3_H;
	const SiS_LVDSCRT1DataStruct  *SiS_LVDSCRT1320x480_1;
	const SiS_LVDSCRT1DataStruct  *SiS_CHTVCRT1UNTSC;
	const SiS_LVDSCRT1DataStruct  *SiS_CHTVCRT1ONTSC;
	const SiS_LVDSCRT1DataStruct  *SiS_CHTVCRT1UPAL;
	const SiS_LVDSCRT1DataStruct  *SiS_CHTVCRT1OPAL;
	const SiS_LVDSCRT1DataStruct  *SiS_CHTVCRT1SOPAL;

	const SiS_CHTVRegDataStruct *SiS_CHTVReg_UNTSC;
	const SiS_CHTVRegDataStruct *SiS_CHTVReg_ONTSC;
	const SiS_CHTVRegDataStruct *SiS_CHTVReg_UPAL;
	const SiS_CHTVRegDataStruct *SiS_CHTVReg_OPAL;
	const SiS_CHTVRegDataStruct *SiS_CHTVReg_UPALM;
	const SiS_CHTVRegDataStruct *SiS_CHTVReg_OPALM;
	const SiS_CHTVRegDataStruct *SiS_CHTVReg_UPALN;
	const SiS_CHTVRegDataStruct *SiS_CHTVReg_OPALN;
	const SiS_CHTVRegDataStruct *SiS_CHTVReg_SOPAL;

	const UCHAR *SiS_CHTVVCLKUNTSC;
	const UCHAR *SiS_CHTVVCLKONTSC;
	const UCHAR *SiS_CHTVVCLKUPAL;
	const UCHAR *SiS_CHTVVCLKOPAL;
	const UCHAR *SiS_CHTVVCLKUPALM;
	const UCHAR *SiS_CHTVVCLKOPALM;
	const UCHAR *SiS_CHTVVCLKUPALN;
	const UCHAR *SiS_CHTVVCLKOPALN;
	const UCHAR *SiS_CHTVVCLKSOPAL;

	USHORT  PanelXRes, PanelHT;
	USHORT  PanelYRes, PanelVT;
	USHORT  PanelHRS,  PanelHRE;
  	USHORT 	PanelVRS,  PanelVRE;
	USHORT  PanelVCLKIdx300;
	USHORT  PanelVCLKIdx315;

	BOOLEAN UseCustomMode;
	BOOLEAN CRT1UsesCustomMode;
	USHORT  CHDisplay;
	USHORT  CHSyncStart;
	USHORT  CHSyncEnd;
	USHORT  CHTotal;
	USHORT  CHBlankStart;
	USHORT  CHBlankEnd;
	USHORT  CVDisplay;
	USHORT  CVSyncStart;
	USHORT  CVSyncEnd;
	USHORT  CVTotal;
	USHORT  CVBlankStart;
	USHORT  CVBlankEnd;
	ULONG   CDClock;
	ULONG   CFlags;   
	UCHAR   CCRT1CRTC[17];
	UCHAR   CSR2B;
	UCHAR   CSR2C;
	USHORT  CSRClock;
	USHORT  CSRClock_CRT1;
	USHORT  CModeFlag;
	USHORT  CModeFlag_CRT1;
	USHORT  CInfoFlag;

	int	LVDSHL;
	
	BOOLEAN Backup;
	UCHAR Backup_Mode;
	UCHAR Backup_14;
	UCHAR Backup_15;
	UCHAR Backup_16;
	UCHAR Backup_17;
	UCHAR Backup_18;
	UCHAR Backup_19;
	UCHAR Backup_1a;
	UCHAR Backup_1b;
	UCHAR Backup_1c;
	UCHAR Backup_1d;
	
	int     UsePanelScaler;
	int	CenterScreen;

	USHORT  CP_Vendor, CP_Product;
	BOOLEAN CP_HaveCustomData;
	int     CP_PreferredX, CP_PreferredY, CP_PreferredIndex;
	int	CP_MaxX, CP_MaxY, CP_MaxClock;
	UCHAR   CP_PrefSR2B, CP_PrefSR2C;
	USHORT  CP_PrefClock;
	BOOLEAN CP_Supports64048075;
	int     CP_HDisplay[7], CP_VDisplay[7];	/* For Custom LCD panel dimensions */
    	int     CP_HTotal[7], CP_VTotal[7];
    	int     CP_HSyncStart[7], CP_VSyncStart[7];
    	int     CP_HSyncEnd[7], CP_VSyncEnd[7];
	int     CP_HBlankStart[7], CP_VBlankStart[7];
	int     CP_HBlankEnd[7], CP_VBlankEnd[7];
    	int     CP_Clock[7];
	BOOLEAN CP_DataValid[7];
	BOOLEAN CP_HSync_P[7], CP_VSync_P[7], CP_SyncValid[7];
} SiS_Private;

#endif

