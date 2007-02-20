/* $XFree86$ */
/* $XdotOrg$ */
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

#ifndef _VSTRUCT_H_
#define _VSTRUCT_H_

struct SiS_PanelDelayTbl {
 	unsigned char timer[2];
};

struct SiS_LCDData {
	unsigned short RVBHCMAX;
	unsigned short RVBHCFACT;
	unsigned short VGAHT;
	unsigned short VGAVT;
	unsigned short LCDHT;
	unsigned short LCDVT;
};

struct SiS_TVData {
	unsigned short RVBHCMAX;
	unsigned short RVBHCFACT;
	unsigned short VGAHT;
	unsigned short VGAVT;
	unsigned short TVHDE;
	unsigned short TVVDE;
	unsigned short RVBHRS;
	unsigned char  FlickerMode;
	unsigned short HALFRVBHRS;
	unsigned short RVBHRS2;
	unsigned char  RY1COE;
	unsigned char  RY2COE;
	unsigned char  RY3COE;
	unsigned char  RY4COE;
};

struct SiS_LVDSData {
	unsigned short VGAHT;
	unsigned short VGAVT;
	unsigned short LCDHT;
	unsigned short LCDVT;
};

struct SiS_LVDSDes {
	unsigned short LCDHDES;
	unsigned short LCDVDES;
};

struct SiS_LVDSCRT1Data {
	unsigned char  CR[15];
};

struct SiS_CHTVRegData {
	unsigned char  Reg[16];
};

struct SiS_St {
	unsigned char  St_ModeID;
	unsigned short St_ModeFlag;
	unsigned char  St_StTableIndex;
	unsigned char  St_CRT2CRTC;
	unsigned char  St_ResInfo;
	unsigned char  VB_StTVFlickerIndex;
	unsigned char  VB_StTVEdgeIndex;
	unsigned char  VB_StTVYFilterIndex;
	unsigned char  St_PDC;
};

struct SiS_VBMode {
	unsigned char  ModeID;
	unsigned char  VB_TVDelayIndex;
	unsigned char  VB_TVFlickerIndex;
	unsigned char  VB_TVPhaseIndex;
	unsigned char  VB_TVYFilterIndex;
	unsigned char  VB_LCDDelayIndex;
	unsigned char  _VB_LCDHIndex;
	unsigned char  _VB_LCDVIndex;
};

struct SiS_StandTable_S {
	unsigned char  CRT_COLS;
	unsigned char  ROWS;
	unsigned char  CHAR_HEIGHT;
	unsigned short CRT_LEN;
	unsigned char  SR[4];
	unsigned char  MISC;
	unsigned char  CRTC[0x19];
	unsigned char  ATTR[0x14];
	unsigned char  GRC[9];
};

struct SiS_Ext {
	unsigned char  Ext_ModeID;
	unsigned short Ext_ModeFlag;
	unsigned short Ext_VESAID;
	unsigned char  Ext_RESINFO;
	unsigned char  VB_ExtTVFlickerIndex;
	unsigned char  VB_ExtTVEdgeIndex;
	unsigned char  VB_ExtTVYFilterIndex;
	unsigned char  VB_ExtTVYFilterIndexROM661;
	unsigned char  REFindex;
	char           ROMMODEIDX661;
};

struct SiS_Ext2 {
	unsigned short Ext_InfoFlag;
	unsigned char  Ext_CRT1CRTC;
	unsigned char  Ext_CRTVCLK;
	unsigned char  Ext_CRT2CRTC;
	unsigned char  Ext_CRT2CRTC_NS;
	unsigned char  ModeID;
	unsigned short XRes;
	unsigned short YRes;
	unsigned char  Ext_PDC;
	unsigned char  Ext_FakeCRT2CRTC;
	unsigned char  Ext_FakeCRT2Clk;
	unsigned char  Ext_CRT1CRTC_NORM;
	unsigned char  Ext_CRTVCLK_NORM;
	unsigned char  Ext_CRT1CRTC_WIDE;
	unsigned char  Ext_CRTVCLK_WIDE;
};

struct SiS_Part2PortTbl {
 	unsigned char  CR[12];
};

struct SiS_CRT1Table {
	unsigned char  CR[17];
};

struct SiS_MCLKData {
	unsigned char  SR28,SR29,SR2A;
	unsigned short CLOCK;
};

struct SiS_VCLKData {
	unsigned char  SR2B,SR2C;
	unsigned short CLOCK;
};

struct SiS_VBVCLKData {
	unsigned char  Part4_A,Part4_B;
	unsigned short CLOCK;
};

struct SiS_StResInfo_S {
	unsigned short HTotal;
	unsigned short VTotal;
};

struct SiS_ModeResInfo_S {
	unsigned short HTotal;
	unsigned short VTotal;
	unsigned char  XChar;
	unsigned char  YChar;
};

/* Defines for SiS_CustomT */
/* Never change these for sisfb compatibility */
#define CUT_NONE		 0
#define CUT_FORCENONE		 1
#define CUT_BARCO1366		 2
#define CUT_BARCO1024		 3
#define CUT_COMPAQ1280		 4
#define CUT_COMPAQ12802		 5
#define CUT_PANEL848		 6
#define CUT_CLEVO1024		 7
#define CUT_CLEVO10242		 8
#define CUT_CLEVO1400		 9
#define CUT_CLEVO14002		10
#define CUT_UNIWILL1024		11
#define CUT_ASUSL3000D		12
#define CUT_UNIWILL10242	13
#define CUT_ACER1280		14
#define CUT_COMPAL1400_1	15
#define CUT_COMPAL1400_2	16
#define CUT_ASUSA2H_1		17
#define CUT_ASUSA2H_2		18
#define CUT_UNKNOWNLCD		19
#define CUT_AOP8060		20
#define CUT_PANEL856		21

struct SiS_Private
{
	unsigned char			ChipType;
	unsigned char			ChipRevision;
#ifdef SIS_XORG_XF86
	PCITAG				PciTag;
#endif
#ifdef SIS_LINUX_KERNEL
	void				*ivideo;
#endif
	unsigned char 			*VirtualRomBase;
	bool				UseROM;
#ifdef SIS_LINUX_KERNEL
	unsigned char SISIOMEMTYPE	*VideoMemoryAddress;
	unsigned int			VideoMemorySize;
#endif
	SISIOADDRESS			IOAddress;
	SISIOADDRESS			IOAddress2;  /* For dual chip XGI volari */

#ifdef SIS_LINUX_KERNEL
	SISIOADDRESS			RelIO;
#endif
	SISIOADDRESS			SiS_P3c4;
	SISIOADDRESS			SiS_P3d4;
	SISIOADDRESS			SiS_P3c0;
	SISIOADDRESS			SiS_P3ce;
	SISIOADDRESS			SiS_P3c2;
	SISIOADDRESS			SiS_P3ca;
	SISIOADDRESS			SiS_P3c6;
	SISIOADDRESS			SiS_P3c7;
	SISIOADDRESS			SiS_P3c8;
	SISIOADDRESS			SiS_P3c9;
	SISIOADDRESS			SiS_P3cb;
	SISIOADDRESS			SiS_P3cc;
	SISIOADDRESS			SiS_P3cd;
	SISIOADDRESS			SiS_P3da;
	SISIOADDRESS			SiS_Part1Port;
	SISIOADDRESS			SiS_Part2Port;
	SISIOADDRESS			SiS_Part3Port;
	SISIOADDRESS			SiS_Part4Port;
	SISIOADDRESS			SiS_Part5Port;
	SISIOADDRESS			SiS_VidCapt;
	SISIOADDRESS			SiS_VidPlay;
	unsigned short			SiS_IF_DEF_LVDS;
	unsigned short			SiS_IF_DEF_CH70xx;
	unsigned short			SiS_IF_DEF_CONEX;
	unsigned short			SiS_IF_DEF_TRUMPION;
	unsigned short			SiS_IF_DEF_DSTN;
	unsigned short			SiS_IF_DEF_FSTN;
	unsigned short			SiS_SysFlags;
	unsigned char			SiS_VGAINFO;
#ifdef SIS_XORG_XF86
	unsigned short			SiS_CP1, SiS_CP2, SiS_CP3, SiS_CP4;
#endif
	bool				SiS_UseROM;
	bool				SiS_ROMNew;
	bool				SiS_XGIROM;
	bool				SiS_NeedRomModeData;
	bool				PanelSelfDetected;
	bool				DDCPortMixup;
	int				SiS_CHOverScan;
	bool				SiS_CHSOverScan;
	bool				SiS_ChSW;
	bool				SiS_UseLCDA;
	int				SiS_UseOEM;
	unsigned int			SiS_CustomT;
	int				SiS_UseWide, SiS_UseWideCRT2;
	int				SiS_TVBlue;
	unsigned short			SiS_Backup70xx;
	bool				HaveEMI;
	bool				HaveEMILCD;
	bool				OverruleEMI;
	unsigned char			EMI_30,EMI_31,EMI_32,EMI_33;
	unsigned short			SiS_EMIOffset;
	unsigned short			SiS_PWDOffset;
	short				PDC, PDCA;
	unsigned char			SiS_MyCR63;
	unsigned short			SiS_CRT1Mode;
	unsigned short			SiS_flag_clearbuffer;
	int				SiS_RAMType;
	unsigned char			SiS_ChannelAB;
	unsigned char			SiS_DataBusWidth;
	unsigned short			SiS_ModeType;
	unsigned short			SiS_VBInfo;
	unsigned short			SiS_TVMode;
	unsigned short			SiS_LCDResInfo;
	unsigned short			SiS_LCDTypeInfo;
	unsigned short			SiS_LCDInfo;
	unsigned short			SiS_LCDInfo661;
	unsigned short			SiS_VBType;
	unsigned short			SiS_VBExtInfo;
	unsigned short			SiS_YPbPr;
	unsigned short			SiS_SelectCRT2Rate;
	unsigned short			SiS_SetFlag;
	unsigned short			SiS_RVBHCFACT;
	unsigned short			SiS_RVBHCMAX;
	unsigned short			SiS_RVBHRS;
	unsigned short			SiS_RVBHRS2;
	unsigned short			SiS_VGAVT;
	unsigned short			SiS_VGAHT;
	unsigned short			SiS_VT;
	unsigned short			SiS_HT;
	unsigned short			SiS_VGAVDE;
	unsigned short			SiS_VGAHDE;
	unsigned short			SiS_VDE;
	unsigned short			SiS_HDE;
	unsigned short			SiS_NewFlickerMode;
	unsigned short			SiS_RY1COE;
	unsigned short			SiS_RY2COE;
	unsigned short			SiS_RY3COE;
	unsigned short			SiS_RY4COE;
	unsigned short			SiS_LCDHDES;
	unsigned short			SiS_LCDVDES;
	unsigned short			SiS_DDC_Port;
	unsigned short			SiS_DDC_Index;
	unsigned short			SiS_DDC_Data;
	unsigned short			SiS_DDC_NData;
	unsigned short			SiS_DDC_Clk;
	unsigned short			SiS_DDC_NClk;
	unsigned short			SiS_DDC_DeviceAddr;
	unsigned short			SiS_DDC_ReadAddr;
	unsigned short			SiS_DDC_SecAddr;
	unsigned short			SiS_ChrontelInit;
	bool				SiS_SensibleSR11;
	unsigned short			SiS661LCD2TableSize;

	unsigned short			SiS_PanelMinLVDS;
	unsigned short			SiS_PanelMin301;

	const struct SiS_St		*SiS_SModeIDTable;
	const struct SiS_StandTable_S	*SiS_StandTable;
	const struct SiS_Ext		*SiS_EModeIDTable;
	const struct SiS_Ext2		*SiS_RefIndex;
	const struct SiS_VBMode		*SiS_VBModeIDTable;
	const struct SiS_CRT1Table	*SiS_CRT1Table;
	const struct SiS_MCLKData	*SiS_MCLKData_0;
	const struct SiS_MCLKData	*SiS_MCLKData_1;
	struct SiS_VCLKData		*SiS_VCLKData;
	struct SiS_VBVCLKData		*SiS_VBVCLKData;
	const struct SiS_StResInfo_S	*SiS_StResInfo;
	const struct SiS_ModeResInfo_S	*SiS_ModeResInfo;

	const unsigned char		*pSiS_OutputSelect;
	const unsigned char		*pSiS_SoftSetting;

	const unsigned char		*SiS_SR15;

	const struct SiS_PanelDelayTbl	*SiS_PanelDelayTbl;
	const struct SiS_PanelDelayTbl	*SiS_PanelDelayTblLVDS;

	/* SiS bridge */

	const struct SiS_LCDData	*SiS_ExtLCD1024x768Data;
	const struct SiS_LCDData	*SiS_St2LCD1024x768Data;
	const struct SiS_LCDData	*SiS_LCD1280x720Data;
	const struct SiS_LCDData	*SiS_StLCD1280x768_2Data;
	const struct SiS_LCDData	*SiS_ExtLCD1280x768_2Data;
	const struct SiS_LCDData	*SiS_LCD1280x800Data;
	const struct SiS_LCDData	*SiS_LCD1280x800_2Data;
	const struct SiS_LCDData	*SiS_LCD1280x854Data;
	const struct SiS_LCDData	*SiS_LCD1280x960Data;
	const struct SiS_LCDData	*SiS_ExtLCD1280x1024Data;
	const struct SiS_LCDData	*SiS_St2LCD1280x1024Data;
	const struct SiS_LCDData	*SiS_StLCD1400x1050Data;
	const struct SiS_LCDData	*SiS_ExtLCD1400x1050Data;
	const struct SiS_LCDData	*SiS_StLCD1600x1200Data;
	const struct SiS_LCDData	*SiS_ExtLCD1600x1200Data;
	const struct SiS_LCDData	*SiS_LCD1680x1050Data;
	const struct SiS_LCDData	*SiS_NoScaleData;
	const struct SiS_TVData		*SiS_StPALData;
	const struct SiS_TVData		*SiS_ExtPALData;
	const struct SiS_TVData		*SiS_StNTSCData;
	const struct SiS_TVData		*SiS_ExtNTSCData;
	const struct SiS_TVData		*SiS_St1HiTVData;
	const struct SiS_TVData		*SiS_St2HiTVData;
	const struct SiS_TVData		*SiS_ExtHiTVData;
	const struct SiS_TVData		*SiS_St525iData;
	const struct SiS_TVData		*SiS_St525pData;
	const struct SiS_TVData		*SiS_St750pData;
	const struct SiS_TVData		*SiS_Ext525iData;
	const struct SiS_TVData		*SiS_Ext525pData;
	const struct SiS_TVData		*SiS_Ext750pData;
	const unsigned char		*SiS_NTSCTiming;
	const unsigned char		*SiS_PALTiming;
	const unsigned char		*SiS_HiTVExtTiming;
	const unsigned char		*SiS_HiTVSt1Timing;
	const unsigned char		*SiS_HiTVSt2Timing;
	const unsigned char		*SiS_HiTVGroup3Data;
	const unsigned char		*SiS_HiTVGroup3Simu;
#if 0
	const unsigned char		*SiS_HiTVTextTiming;
	const unsigned char		*SiS_HiTVGroup3Text;
#endif

	const struct SiS_Part2PortTbl	*SiS_CRT2Part2_1024x768_1;
	const struct SiS_Part2PortTbl	*SiS_CRT2Part2_1024x768_2;
	const struct SiS_Part2PortTbl	*SiS_CRT2Part2_1024x768_3;

	/* LVDS, Chrontel */

	const struct SiS_LVDSData	*SiS_LVDS320x240Data_1;
	const struct SiS_LVDSData	*SiS_LVDS320x240Data_2;
	const struct SiS_LVDSData	*SiS_LVDS640x480Data_1;
	const struct SiS_LVDSData	*SiS_LVDS800x600Data_1;
	const struct SiS_LVDSData	*SiS_LVDS1024x600Data_1;
	const struct SiS_LVDSData	*SiS_LVDS1024x768Data_1;
	const struct SiS_LVDSData	*SiS_LVDSBARCO1366Data_1;
	const struct SiS_LVDSData	*SiS_LVDSBARCO1366Data_2;
	const struct SiS_LVDSData	*SiS_LVDSBARCO1024Data_1;
	const struct SiS_LVDSData	*SiS_LVDS848x480Data_1;
	const struct SiS_LVDSData	*SiS_LVDS848x480Data_2;
	const struct SiS_LVDSData	*SiS_CHTVUNTSCData;
	const struct SiS_LVDSData	*SiS_CHTVONTSCData;
	const struct SiS_LVDSData	*SiS_CHTVUPALData;
	const struct SiS_LVDSData	*SiS_CHTVOPALData;
	const struct SiS_LVDSData	*SiS_CHTVUPALMData;
	const struct SiS_LVDSData	*SiS_CHTVOPALMData;
	const struct SiS_LVDSData	*SiS_CHTVUPALNData;
	const struct SiS_LVDSData	*SiS_CHTVOPALNData;
	const struct SiS_LVDSData	*SiS_CHTVSOPALData;

	const struct SiS_LVDSDes	*SiS_PanelType04_1a;
	const struct SiS_LVDSDes	*SiS_PanelType04_2a;
	const struct SiS_LVDSDes	*SiS_PanelType04_1b;
	const struct SiS_LVDSDes	*SiS_PanelType04_2b;

	const struct SiS_LVDSCRT1Data	*SiS_LVDSCRT1320x240_1;
	const struct SiS_LVDSCRT1Data	*SiS_LVDSCRT1320x240_2;
	const struct SiS_LVDSCRT1Data	*SiS_LVDSCRT1320x240_2_H;
	const struct SiS_LVDSCRT1Data	*SiS_LVDSCRT1320x240_3;
	const struct SiS_LVDSCRT1Data	*SiS_LVDSCRT1320x240_3_H;
	const struct SiS_LVDSCRT1Data	*SiS_LVDSCRT1640x480_1;
	const struct SiS_LVDSCRT1Data	*SiS_LVDSCRT1640x480_1_H;
	const struct SiS_LVDSCRT1Data	*SiS_CHTVCRT1UNTSC;
	const struct SiS_LVDSCRT1Data	*SiS_CHTVCRT1ONTSC;
	const struct SiS_LVDSCRT1Data	*SiS_CHTVCRT1UPAL;
	const struct SiS_LVDSCRT1Data	*SiS_CHTVCRT1OPAL;
	const struct SiS_LVDSCRT1Data	*SiS_CHTVCRT1SOPAL;

	const struct SiS_CHTVRegData	*SiS_CHTVReg_UNTSC;
	const struct SiS_CHTVRegData	*SiS_CHTVReg_ONTSC;
	const struct SiS_CHTVRegData	*SiS_CHTVReg_UPAL;
	const struct SiS_CHTVRegData	*SiS_CHTVReg_OPAL;
	const struct SiS_CHTVRegData	*SiS_CHTVReg_UPALM;
	const struct SiS_CHTVRegData	*SiS_CHTVReg_OPALM;
	const struct SiS_CHTVRegData	*SiS_CHTVReg_UPALN;
	const struct SiS_CHTVRegData	*SiS_CHTVReg_OPALN;
	const struct SiS_CHTVRegData	*SiS_CHTVReg_SOPAL;

	const unsigned char		*SiS_CHTVVCLKUNTSC;
	const unsigned char		*SiS_CHTVVCLKONTSC;
	const unsigned char		*SiS_CHTVVCLKUPAL;
	const unsigned char		*SiS_CHTVVCLKOPAL;
	const unsigned char		*SiS_CHTVVCLKUPALM;
	const unsigned char		*SiS_CHTVVCLKOPALM;
	const unsigned char		*SiS_CHTVVCLKUPALN;
	const unsigned char		*SiS_CHTVVCLKOPALN;
	const unsigned char		*SiS_CHTVVCLKSOPAL;

	unsigned short			PanelXRes, PanelHT;
	unsigned short			PanelYRes, PanelVT;
	unsigned short			PanelHRS,  PanelHRE;
	unsigned short			PanelVRS,  PanelVRE;
	unsigned short			PanelVCLKIdx300;
	unsigned short			PanelVCLKIdx315;
	bool				Alternate1600x1200;

	bool				UseCustomMode;
	bool				CRT1UsesCustomMode;
	unsigned short			CHDisplay;
	unsigned short			CHSyncStart;
	unsigned short			CHSyncEnd;
	unsigned short			CHTotal;
	unsigned short			CHBlankStart;
	unsigned short			CHBlankEnd;
	unsigned short			CVDisplay;
	unsigned short			CVSyncStart;
	unsigned short			CVSyncEnd;
	unsigned short			CVTotal;
	unsigned short			CVBlankStart;
	unsigned short			CVBlankEnd;
	unsigned int			CDClock;
	unsigned int			CFlags;
	unsigned char			CCRT1CRTC[17];
	unsigned char			CSR2B;
	unsigned char			CSR2C;
	unsigned short			CSRClock;
	unsigned short			CSRClock_CRT1;
	unsigned short			CModeFlag;
	unsigned short			CModeFlag_CRT1;
	unsigned short			CInfoFlag;

	int				LVDSHL;

	bool				Backup;
	unsigned char			Backup_Mode;
	unsigned char			Backup_14;
	unsigned char			Backup_15;
	unsigned char			Backup_16;
	unsigned char			Backup_17;
	unsigned char			Backup_18;
	unsigned char			Backup_19;
	unsigned char			Backup_1a;
	unsigned char			Backup_1b;
	unsigned char			Backup_1c;
	unsigned char			Backup_1d;

	unsigned char			Init_P4_0E;

	int				UsePanelScaler;
	int				CenterScreen;

	unsigned short			CP_Vendor, CP_Product;
	bool				CP_HaveCustomData;
	int				CP_PreferredX, CP_PreferredY, CP_PreferredIndex;
	int				CP_MaxX, CP_MaxY, CP_MaxClock;
	unsigned char			CP_PrefSR2B, CP_PrefSR2C;
	unsigned short			CP_PrefClock;
	bool				CP_Supports64048075;
	int				CP_HDisplay[7], CP_VDisplay[7];	/* For Custom LCD panel dimensions */
	int				CP_HTotal[7], CP_VTotal[7];
	int				CP_HSyncStart[7], CP_VSyncStart[7];
	int				CP_HSyncEnd[7], CP_VSyncEnd[7];
	int				CP_HBlankStart[7], CP_VBlankStart[7];
	int				CP_HBlankEnd[7], CP_VBlankEnd[7];
	int				CP_Clock[7];
	bool				CP_DataValid[7];
	bool				CP_HSync_P[7], CP_VSync_P[7], CP_SyncValid[7];
};

#endif

