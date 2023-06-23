/* $XFree86$ */
/* $XdotOrg$ */
/*
 * Mode initializing code (CRT1 section) for
 * for SiS 300/305/540/630/730,
 *     SiS 315/550/[M]650/651/[M]661[FGM]X/[M]74x[GX]/330/[M]76x[GX],
 *     XGI Volari V3XT/V5/V8, Z7
 * (Universal module for Linux kernel framebuffer and X.org/XFree86 4.x)
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
 * Formerly based on non-functional code-fragements for 300 series by SiS, Inc.
 * Used by permission.
 */

#include "init.h"

#ifdef CONFIG_FB_SIS_300
#include "300vtbl.h"
#endif

#ifdef CONFIG_FB_SIS_315
#include "310vtbl.h"
#endif

#if defined(ALLOC_PRAGMA)
#pragma alloc_text(PAGE,SiSSetMode)
#endif

/*********************************************/
/*         POINTER INITIALIZATION            */
/*********************************************/

#if defined(CONFIG_FB_SIS_300) || defined(CONFIG_FB_SIS_315)
static void
InitCommonPointer(struct SiS_Private *SiS_Pr)
{
   SiS_Pr->SiS_SModeIDTable  = SiS_SModeIDTable;
   SiS_Pr->SiS_StResInfo     = SiS_StResInfo;
   SiS_Pr->SiS_ModeResInfo   = SiS_ModeResInfo;
   SiS_Pr->SiS_StandTable    = SiS_StandTable;

   SiS_Pr->SiS_NTSCTiming     = SiS_NTSCTiming;
   SiS_Pr->SiS_PALTiming      = SiS_PALTiming;
   SiS_Pr->SiS_HiTVSt1Timing  = SiS_HiTVSt1Timing;
   SiS_Pr->SiS_HiTVSt2Timing  = SiS_HiTVSt2Timing;

   SiS_Pr->SiS_HiTVExtTiming  = SiS_HiTVExtTiming;
   SiS_Pr->SiS_HiTVGroup3Data = SiS_HiTVGroup3Data;
   SiS_Pr->SiS_HiTVGroup3Simu = SiS_HiTVGroup3Simu;
#if 0
   SiS_Pr->SiS_HiTVTextTiming = SiS_HiTVTextTiming;
   SiS_Pr->SiS_HiTVGroup3Text = SiS_HiTVGroup3Text;
#endif

   SiS_Pr->SiS_StPALData   = SiS_StPALData;
   SiS_Pr->SiS_ExtPALData  = SiS_ExtPALData;
   SiS_Pr->SiS_StNTSCData  = SiS_StNTSCData;
   SiS_Pr->SiS_ExtNTSCData = SiS_ExtNTSCData;
   SiS_Pr->SiS_St1HiTVData = SiS_StHiTVData;
   SiS_Pr->SiS_St2HiTVData = SiS_St2HiTVData;
   SiS_Pr->SiS_ExtHiTVData = SiS_ExtHiTVData;
   SiS_Pr->SiS_St525iData  = SiS_StNTSCData;
   SiS_Pr->SiS_St525pData  = SiS_St525pData;
   SiS_Pr->SiS_St750pData  = SiS_St750pData;
   SiS_Pr->SiS_Ext525iData = SiS_ExtNTSCData;
   SiS_Pr->SiS_Ext525pData = SiS_ExtNTSCData;
   SiS_Pr->SiS_Ext750pData = SiS_Ext750pData;

   SiS_Pr->pSiS_OutputSelect = &SiS_OutputSelect;
   SiS_Pr->pSiS_SoftSetting  = &SiS_SoftSetting;

   SiS_Pr->SiS_LCD1280x720Data      = SiS_LCD1280x720Data;
   SiS_Pr->SiS_StLCD1280x768_2Data  = SiS_StLCD1280x768_2Data;
   SiS_Pr->SiS_ExtLCD1280x768_2Data = SiS_ExtLCD1280x768_2Data;
   SiS_Pr->SiS_LCD1280x800Data      = SiS_LCD1280x800Data;
   SiS_Pr->SiS_LCD1280x800_2Data    = SiS_LCD1280x800_2Data;
   SiS_Pr->SiS_LCD1280x854Data      = SiS_LCD1280x854Data;
   SiS_Pr->SiS_LCD1280x960Data      = SiS_LCD1280x960Data;
   SiS_Pr->SiS_StLCD1400x1050Data   = SiS_StLCD1400x1050Data;
   SiS_Pr->SiS_ExtLCD1400x1050Data  = SiS_ExtLCD1400x1050Data;
   SiS_Pr->SiS_LCD1680x1050Data     = SiS_LCD1680x1050Data;
   SiS_Pr->SiS_StLCD1600x1200Data   = SiS_StLCD1600x1200Data;
   SiS_Pr->SiS_ExtLCD1600x1200Data  = SiS_ExtLCD1600x1200Data;
   SiS_Pr->SiS_NoScaleData          = SiS_NoScaleData;

   SiS_Pr->SiS_LVDS320x240Data_1   = SiS_LVDS320x240Data_1;
   SiS_Pr->SiS_LVDS320x240Data_2   = SiS_LVDS320x240Data_2;
   SiS_Pr->SiS_LVDS640x480Data_1   = SiS_LVDS640x480Data_1;
   SiS_Pr->SiS_LVDS800x600Data_1   = SiS_LVDS800x600Data_1;
   SiS_Pr->SiS_LVDS1024x600Data_1  = SiS_LVDS1024x600Data_1;
   SiS_Pr->SiS_LVDS1024x768Data_1  = SiS_LVDS1024x768Data_1;

   SiS_Pr->SiS_LVDSCRT1320x240_1     = SiS_LVDSCRT1320x240_1;
   SiS_Pr->SiS_LVDSCRT1320x240_2     = SiS_LVDSCRT1320x240_2;
   SiS_Pr->SiS_LVDSCRT1320x240_2_H   = SiS_LVDSCRT1320x240_2_H;
   SiS_Pr->SiS_LVDSCRT1320x240_3     = SiS_LVDSCRT1320x240_3;
   SiS_Pr->SiS_LVDSCRT1320x240_3_H   = SiS_LVDSCRT1320x240_3_H;
   SiS_Pr->SiS_LVDSCRT1640x480_1     = SiS_LVDSCRT1640x480_1;
   SiS_Pr->SiS_LVDSCRT1640x480_1_H   = SiS_LVDSCRT1640x480_1_H;
#if 0
   SiS_Pr->SiS_LVDSCRT11024x600_1    = SiS_LVDSCRT11024x600_1;
   SiS_Pr->SiS_LVDSCRT11024x600_1_H  = SiS_LVDSCRT11024x600_1_H;
   SiS_Pr->SiS_LVDSCRT11024x600_2    = SiS_LVDSCRT11024x600_2;
   SiS_Pr->SiS_LVDSCRT11024x600_2_H  = SiS_LVDSCRT11024x600_2_H;
#endif

   SiS_Pr->SiS_CHTVUNTSCData = SiS_CHTVUNTSCData;
   SiS_Pr->SiS_CHTVONTSCData = SiS_CHTVONTSCData;

   SiS_Pr->SiS_PanelMinLVDS   = Panel_800x600;    /* lowest value LVDS/LCDA */
   SiS_Pr->SiS_PanelMin301    = Panel_1024x768;   /* lowest value 301 */
}
#endif

#ifdef CONFIG_FB_SIS_300
static void
InitTo300Pointer(struct SiS_Private *SiS_Pr)
{
   InitCommonPointer(SiS_Pr);

   SiS_Pr->SiS_VBModeIDTable = SiS300_VBModeIDTable;
   SiS_Pr->SiS_EModeIDTable  = SiS300_EModeIDTable;
   SiS_Pr->SiS_RefIndex      = SiS300_RefIndex;
   SiS_Pr->SiS_CRT1Table     = SiS300_CRT1Table;
   if(SiS_Pr->ChipType == SIS_300) {
      SiS_Pr->SiS_MCLKData_0 = SiS300_MCLKData_300; /* 300 */
   } else {
      SiS_Pr->SiS_MCLKData_0 = SiS300_MCLKData_630; /* 630, 730 */
   }
   SiS_Pr->SiS_VCLKData      = SiS300_VCLKData;
   SiS_Pr->SiS_VBVCLKData    = (struct SiS_VBVCLKData *)SiS300_VCLKData;

   SiS_Pr->SiS_SR15  = SiS300_SR15;

   SiS_Pr->SiS_PanelDelayTbl     = SiS300_PanelDelayTbl;
   SiS_Pr->SiS_PanelDelayTblLVDS = SiS300_PanelDelayTbl;

   SiS_Pr->SiS_ExtLCD1024x768Data   = SiS300_ExtLCD1024x768Data;
   SiS_Pr->SiS_St2LCD1024x768Data   = SiS300_St2LCD1024x768Data;
   SiS_Pr->SiS_ExtLCD1280x1024Data  = SiS300_ExtLCD1280x1024Data;
   SiS_Pr->SiS_St2LCD1280x1024Data  = SiS300_St2LCD1280x1024Data;

   SiS_Pr->SiS_CRT2Part2_1024x768_1  = SiS300_CRT2Part2_1024x768_1;
   SiS_Pr->SiS_CRT2Part2_1024x768_2  = SiS300_CRT2Part2_1024x768_2;
   SiS_Pr->SiS_CRT2Part2_1024x768_3  = SiS300_CRT2Part2_1024x768_3;

   SiS_Pr->SiS_CHTVUPALData  = SiS300_CHTVUPALData;
   SiS_Pr->SiS_CHTVOPALData  = SiS300_CHTVOPALData;
   SiS_Pr->SiS_CHTVUPALMData = SiS_CHTVUNTSCData;    /* not supported on 300 series */
   SiS_Pr->SiS_CHTVOPALMData = SiS_CHTVONTSCData;    /* not supported on 300 series */
   SiS_Pr->SiS_CHTVUPALNData = SiS300_CHTVUPALData;  /* not supported on 300 series */
   SiS_Pr->SiS_CHTVOPALNData = SiS300_CHTVOPALData;  /* not supported on 300 series */
   SiS_Pr->SiS_CHTVSOPALData = SiS300_CHTVSOPALData;

   SiS_Pr->SiS_LVDS848x480Data_1   = SiS300_LVDS848x480Data_1;
   SiS_Pr->SiS_LVDS848x480Data_2   = SiS300_LVDS848x480Data_2;
   SiS_Pr->SiS_LVDSBARCO1024Data_1 = SiS300_LVDSBARCO1024Data_1;
   SiS_Pr->SiS_LVDSBARCO1366Data_1 = SiS300_LVDSBARCO1366Data_1;
   SiS_Pr->SiS_LVDSBARCO1366Data_2 = SiS300_LVDSBARCO1366Data_2;

   SiS_Pr->SiS_PanelType04_1a = SiS300_PanelType04_1a;
   SiS_Pr->SiS_PanelType04_2a = SiS300_PanelType04_2a;
   SiS_Pr->SiS_PanelType04_1b = SiS300_PanelType04_1b;
   SiS_Pr->SiS_PanelType04_2b = SiS300_PanelType04_2b;

   SiS_Pr->SiS_CHTVCRT1UNTSC = SiS300_CHTVCRT1UNTSC;
   SiS_Pr->SiS_CHTVCRT1ONTSC = SiS300_CHTVCRT1ONTSC;
   SiS_Pr->SiS_CHTVCRT1UPAL  = SiS300_CHTVCRT1UPAL;
   SiS_Pr->SiS_CHTVCRT1OPAL  = SiS300_CHTVCRT1OPAL;
   SiS_Pr->SiS_CHTVCRT1SOPAL = SiS300_CHTVCRT1SOPAL;
   SiS_Pr->SiS_CHTVReg_UNTSC = SiS300_CHTVReg_UNTSC;
   SiS_Pr->SiS_CHTVReg_ONTSC = SiS300_CHTVReg_ONTSC;
   SiS_Pr->SiS_CHTVReg_UPAL  = SiS300_CHTVReg_UPAL;
   SiS_Pr->SiS_CHTVReg_OPAL  = SiS300_CHTVReg_OPAL;
   SiS_Pr->SiS_CHTVReg_UPALM = SiS300_CHTVReg_UNTSC;  /* not supported on 300 series */
   SiS_Pr->SiS_CHTVReg_OPALM = SiS300_CHTVReg_ONTSC;  /* not supported on 300 series */
   SiS_Pr->SiS_CHTVReg_UPALN = SiS300_CHTVReg_UPAL;   /* not supported on 300 series */
   SiS_Pr->SiS_CHTVReg_OPALN = SiS300_CHTVReg_OPAL;   /* not supported on 300 series */
   SiS_Pr->SiS_CHTVReg_SOPAL = SiS300_CHTVReg_SOPAL;
   SiS_Pr->SiS_CHTVVCLKUNTSC = SiS300_CHTVVCLKUNTSC;
   SiS_Pr->SiS_CHTVVCLKONTSC = SiS300_CHTVVCLKONTSC;
   SiS_Pr->SiS_CHTVVCLKUPAL  = SiS300_CHTVVCLKUPAL;
   SiS_Pr->SiS_CHTVVCLKOPAL  = SiS300_CHTVVCLKOPAL;
   SiS_Pr->SiS_CHTVVCLKUPALM = SiS300_CHTVVCLKUNTSC;  /* not supported on 300 series */
   SiS_Pr->SiS_CHTVVCLKOPALM = SiS300_CHTVVCLKONTSC;  /* not supported on 300 series */
   SiS_Pr->SiS_CHTVVCLKUPALN = SiS300_CHTVVCLKUPAL;   /* not supported on 300 series */
   SiS_Pr->SiS_CHTVVCLKOPALN = SiS300_CHTVVCLKOPAL;   /* not supported on 300 series */
   SiS_Pr->SiS_CHTVVCLKSOPAL = SiS300_CHTVVCLKSOPAL;
}
#endif

#ifdef CONFIG_FB_SIS_315
static void
InitTo310Pointer(struct SiS_Private *SiS_Pr)
{
   InitCommonPointer(SiS_Pr);

   SiS_Pr->SiS_EModeIDTable  = SiS310_EModeIDTable;
   SiS_Pr->SiS_RefIndex      = SiS310_RefIndex;
   SiS_Pr->SiS_CRT1Table     = SiS310_CRT1Table;
   if(SiS_Pr->ChipType >= SIS_340) {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_340;  /* 340 + XGI */
   } else if(SiS_Pr->ChipType >= SIS_761) {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_761;  /* 761 - preliminary */
   } else if(SiS_Pr->ChipType >= SIS_760) {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_760;  /* 760 */
   } else if(SiS_Pr->ChipType >= SIS_661) {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_660;  /* 661/741 */
   } else if(SiS_Pr->ChipType == SIS_330) {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_330;  /* 330 */
   } else if(SiS_Pr->ChipType > SIS_315PRO) {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_650;  /* 550, 650, 740 */
   } else {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_315;  /* 315 */
   }
   if(SiS_Pr->ChipType >= SIS_340) {
      SiS_Pr->SiS_MCLKData_1 = SiS310_MCLKData_1_340;
   } else {
      SiS_Pr->SiS_MCLKData_1 = SiS310_MCLKData_1;
   }
   SiS_Pr->SiS_VCLKData      = SiS310_VCLKData;
   SiS_Pr->SiS_VBVCLKData    = SiS310_VBVCLKData;

   SiS_Pr->SiS_SR15  = SiS310_SR15;

   SiS_Pr->SiS_PanelDelayTbl     = SiS310_PanelDelayTbl;
   SiS_Pr->SiS_PanelDelayTblLVDS = SiS310_PanelDelayTblLVDS;

   SiS_Pr->SiS_St2LCD1024x768Data   = SiS310_St2LCD1024x768Data;
   SiS_Pr->SiS_ExtLCD1024x768Data   = SiS310_ExtLCD1024x768Data;
   SiS_Pr->SiS_St2LCD1280x1024Data  = SiS310_St2LCD1280x1024Data;
   SiS_Pr->SiS_ExtLCD1280x1024Data  = SiS310_ExtLCD1280x1024Data;

   SiS_Pr->SiS_CRT2Part2_1024x768_1  = SiS310_CRT2Part2_1024x768_1;

   SiS_Pr->SiS_CHTVUPALData  = SiS310_CHTVUPALData;
   SiS_Pr->SiS_CHTVOPALData  = SiS310_CHTVOPALData;
   SiS_Pr->SiS_CHTVUPALMData = SiS310_CHTVUPALMData;
   SiS_Pr->SiS_CHTVOPALMData = SiS310_CHTVOPALMData;
   SiS_Pr->SiS_CHTVUPALNData = SiS310_CHTVUPALNData;
   SiS_Pr->SiS_CHTVOPALNData = SiS310_CHTVOPALNData;
   SiS_Pr->SiS_CHTVSOPALData = SiS310_CHTVSOPALData;

   SiS_Pr->SiS_CHTVCRT1UNTSC = SiS310_CHTVCRT1UNTSC;
   SiS_Pr->SiS_CHTVCRT1ONTSC = SiS310_CHTVCRT1ONTSC;
   SiS_Pr->SiS_CHTVCRT1UPAL  = SiS310_CHTVCRT1UPAL;
   SiS_Pr->SiS_CHTVCRT1OPAL  = SiS310_CHTVCRT1OPAL;
   SiS_Pr->SiS_CHTVCRT1SOPAL = SiS310_CHTVCRT1OPAL;

   SiS_Pr->SiS_CHTVReg_UNTSC = SiS310_CHTVReg_UNTSC;
   SiS_Pr->SiS_CHTVReg_ONTSC = SiS310_CHTVReg_ONTSC;
   SiS_Pr->SiS_CHTVReg_UPAL  = SiS310_CHTVReg_UPAL;
   SiS_Pr->SiS_CHTVReg_OPAL  = SiS310_CHTVReg_OPAL;
   SiS_Pr->SiS_CHTVReg_UPALM = SiS310_CHTVReg_UPALM;
   SiS_Pr->SiS_CHTVReg_OPALM = SiS310_CHTVReg_OPALM;
   SiS_Pr->SiS_CHTVReg_UPALN = SiS310_CHTVReg_UPALN;
   SiS_Pr->SiS_CHTVReg_OPALN = SiS310_CHTVReg_OPALN;
   SiS_Pr->SiS_CHTVReg_SOPAL = SiS310_CHTVReg_OPAL;

   SiS_Pr->SiS_CHTVVCLKUNTSC = SiS310_CHTVVCLKUNTSC;
   SiS_Pr->SiS_CHTVVCLKONTSC = SiS310_CHTVVCLKONTSC;
   SiS_Pr->SiS_CHTVVCLKUPAL  = SiS310_CHTVVCLKUPAL;
   SiS_Pr->SiS_CHTVVCLKOPAL  = SiS310_CHTVVCLKOPAL;
   SiS_Pr->SiS_CHTVVCLKUPALM = SiS310_CHTVVCLKUPALM;
   SiS_Pr->SiS_CHTVVCLKOPALM = SiS310_CHTVVCLKOPALM;
   SiS_Pr->SiS_CHTVVCLKUPALN = SiS310_CHTVVCLKUPALN;
   SiS_Pr->SiS_CHTVVCLKOPALN = SiS310_CHTVVCLKOPALN;
   SiS_Pr->SiS_CHTVVCLKSOPAL = SiS310_CHTVVCLKOPAL;
}
#endif

bool
SiSInitPtr(struct SiS_Private *SiS_Pr)
{
   if(SiS_Pr->ChipType < SIS_315H) {
#ifdef CONFIG_FB_SIS_300
      InitTo300Pointer(SiS_Pr);
#else
      return false;
#endif
   } else {
#ifdef CONFIG_FB_SIS_315
      InitTo310Pointer(SiS_Pr);
#else
      return false;
#endif
   }
   return true;
}

/*********************************************/
/*            HELPER: Get ModeID             */
/*********************************************/

static
unsigned short
SiS_GetModeID(int VGAEngine, unsigned int VBFlags, int HDisplay, int VDisplay,
		int Depth, bool FSTN, int LCDwidth, int LCDheight)
{
   unsigned short ModeIndex = 0;

   switch(HDisplay)
   {
	case 320:
		if(VDisplay == 200) ModeIndex = ModeIndex_320x200[Depth];
		else if(VDisplay == 240) {
			if((VBFlags & CRT2_LCD) && (FSTN))
				ModeIndex = ModeIndex_320x240_FSTN[Depth];
			else
				ModeIndex = ModeIndex_320x240[Depth];
		}
		break;
	case 400:
		if((!(VBFlags & CRT1_LCDA)) || ((LCDwidth >= 800) && (LCDheight >= 600))) {
			if(VDisplay == 300) ModeIndex = ModeIndex_400x300[Depth];
		}
		break;
	case 512:
		if((!(VBFlags & CRT1_LCDA)) || ((LCDwidth >= 1024) && (LCDheight >= 768))) {
			if(VDisplay == 384) ModeIndex = ModeIndex_512x384[Depth];
		}
		break;
	case 640:
		if(VDisplay == 480)      ModeIndex = ModeIndex_640x480[Depth];
		else if(VDisplay == 400) ModeIndex = ModeIndex_640x400[Depth];
		break;
	case 720:
		if(VDisplay == 480)      ModeIndex = ModeIndex_720x480[Depth];
		else if(VDisplay == 576) ModeIndex = ModeIndex_720x576[Depth];
		break;
	case 768:
		if(VDisplay == 576) ModeIndex = ModeIndex_768x576[Depth];
		break;
	case 800:
		if(VDisplay == 600)      ModeIndex = ModeIndex_800x600[Depth];
		else if(VDisplay == 480) ModeIndex = ModeIndex_800x480[Depth];
		break;
	case 848:
		if(VDisplay == 480) ModeIndex = ModeIndex_848x480[Depth];
		break;
	case 856:
		if(VDisplay == 480) ModeIndex = ModeIndex_856x480[Depth];
		break;
	case 960:
		if(VGAEngine == SIS_315_VGA) {
			if(VDisplay == 540)      ModeIndex = ModeIndex_960x540[Depth];
			else if(VDisplay == 600) ModeIndex = ModeIndex_960x600[Depth];
		}
		break;
	case 1024:
		if(VDisplay == 576)      ModeIndex = ModeIndex_1024x576[Depth];
		else if(VDisplay == 768) ModeIndex = ModeIndex_1024x768[Depth];
		else if(VGAEngine == SIS_300_VGA) {
			if(VDisplay == 600) ModeIndex = ModeIndex_1024x600[Depth];
		}
		break;
	case 1152:
		if(VDisplay == 864) ModeIndex = ModeIndex_1152x864[Depth];
		if(VGAEngine == SIS_300_VGA) {
			if(VDisplay == 768) ModeIndex = ModeIndex_1152x768[Depth];
		}
		break;
	case 1280:
		switch(VDisplay) {
			case 720:
				ModeIndex = ModeIndex_1280x720[Depth];
				break;
			case 768:
				if(VGAEngine == SIS_300_VGA) {
					ModeIndex = ModeIndex_300_1280x768[Depth];
				} else {
					ModeIndex = ModeIndex_310_1280x768[Depth];
				}
				break;
			case 800:
				if(VGAEngine == SIS_315_VGA) {
					ModeIndex = ModeIndex_1280x800[Depth];
				}
				break;
			case 854:
				if(VGAEngine == SIS_315_VGA) {
					ModeIndex = ModeIndex_1280x854[Depth];
				}
				break;
			case 960:
				ModeIndex = ModeIndex_1280x960[Depth];
				break;
			case 1024:
				ModeIndex = ModeIndex_1280x1024[Depth];
				break;
		}
		break;
	case 1360:
		if(VDisplay == 768) ModeIndex = ModeIndex_1360x768[Depth];
		if(VGAEngine == SIS_300_VGA) {
			if(VDisplay == 1024) ModeIndex = ModeIndex_300_1360x1024[Depth];
		}
		break;
	case 1400:
		if(VGAEngine == SIS_315_VGA) {
			if(VDisplay == 1050) {
				ModeIndex = ModeIndex_1400x1050[Depth];
			}
		}
		break;
	case 1600:
		if(VDisplay == 1200) ModeIndex = ModeIndex_1600x1200[Depth];
		break;
	case 1680:
		if(VGAEngine == SIS_315_VGA) {
			if(VDisplay == 1050) ModeIndex = ModeIndex_1680x1050[Depth];
		}
		break;
	case 1920:
		if(VDisplay == 1440) ModeIndex = ModeIndex_1920x1440[Depth];
		else if(VGAEngine == SIS_315_VGA) {
			if(VDisplay == 1080) ModeIndex = ModeIndex_1920x1080[Depth];
		}
		break;
	case 2048:
		if(VDisplay == 1536) {
			if(VGAEngine == SIS_300_VGA) {
				ModeIndex = ModeIndex_300_2048x1536[Depth];
			} else {
				ModeIndex = ModeIndex_310_2048x1536[Depth];
			}
		}
		break;
   }

   return ModeIndex;
}

unsigned short
SiS_GetModeID_LCD(int VGAEngine, unsigned int VBFlags, int HDisplay, int VDisplay,
		int Depth, bool FSTN, unsigned short CustomT, int LCDwidth, int LCDheight,
		unsigned int VBFlags2)
{
   unsigned short ModeIndex = 0;

   if(VBFlags2 & (VB2_LVDS | VB2_30xBDH)) {

      switch(HDisplay)
      {
	case 320:
	     if((CustomT != CUT_PANEL848) && (CustomT != CUT_PANEL856)) {
		if(VDisplay == 200) {
		   if(!FSTN) ModeIndex = ModeIndex_320x200[Depth];
		} else if(VDisplay == 240) {
		   if(!FSTN) ModeIndex = ModeIndex_320x240[Depth];
		   else if(VGAEngine == SIS_315_VGA) {
		      ModeIndex = ModeIndex_320x240_FSTN[Depth];
		   }
		}
	     }
	     break;
	case 400:
	     if((CustomT != CUT_PANEL848) && (CustomT != CUT_PANEL856)) {
		if(!((VGAEngine == SIS_300_VGA) && (VBFlags2 & VB2_TRUMPION))) {
		   if(VDisplay == 300) ModeIndex = ModeIndex_400x300[Depth];
		}
	     }
	     break;
	case 512:
	     if((CustomT != CUT_PANEL848) && (CustomT != CUT_PANEL856)) {
		if(!((VGAEngine == SIS_300_VGA) && (VBFlags2 & VB2_TRUMPION))) {
		   if(LCDwidth >= 1024 && LCDwidth != 1152 && LCDheight >= 768) {
		      if(VDisplay == 384) {
		         ModeIndex = ModeIndex_512x384[Depth];
		      }
		   }
		}
	     }
	     break;
	case 640:
	     if(VDisplay == 480) ModeIndex = ModeIndex_640x480[Depth];
	     else if(VDisplay == 400) {
		if((CustomT != CUT_PANEL848) && (CustomT != CUT_PANEL856))
		   ModeIndex = ModeIndex_640x400[Depth];
	     }
	     break;
	case 800:
	     if(VDisplay == 600) ModeIndex = ModeIndex_800x600[Depth];
	     break;
	case 848:
	     if(CustomT == CUT_PANEL848) {
	        if(VDisplay == 480) ModeIndex = ModeIndex_848x480[Depth];
	     }
	     break;
	case 856:
	     if(CustomT == CUT_PANEL856) {
	        if(VDisplay == 480) ModeIndex = ModeIndex_856x480[Depth];
	     }
	     break;
	case 1024:
	     if(VDisplay == 768) ModeIndex = ModeIndex_1024x768[Depth];
	     else if(VGAEngine == SIS_300_VGA) {
		if((VDisplay == 600) && (LCDheight == 600)) {
		   ModeIndex = ModeIndex_1024x600[Depth];
		}
	     }
	     break;
	case 1152:
	     if(VGAEngine == SIS_300_VGA) {
		if((VDisplay == 768) && (LCDheight == 768)) {
		   ModeIndex = ModeIndex_1152x768[Depth];
		}
	     }
	     break;
        case 1280:
	     if(VDisplay == 1024) ModeIndex = ModeIndex_1280x1024[Depth];
	     else if(VGAEngine == SIS_315_VGA) {
		if((VDisplay == 768) && (LCDheight == 768)) {
		   ModeIndex = ModeIndex_310_1280x768[Depth];
		}
	     }
	     break;
	case 1360:
	     if(VGAEngine == SIS_300_VGA) {
		if(CustomT == CUT_BARCO1366) {
		   if(VDisplay == 1024) ModeIndex = ModeIndex_300_1360x1024[Depth];
		}
	     }
	     if(CustomT == CUT_PANEL848) {
		if(VDisplay == 768) ModeIndex = ModeIndex_1360x768[Depth];
	     }
	     break;
	case 1400:
	     if(VGAEngine == SIS_315_VGA) {
		if(VDisplay == 1050) ModeIndex = ModeIndex_1400x1050[Depth];
	     }
	     break;
	case 1600:
	     if(VGAEngine == SIS_315_VGA) {
		if(VDisplay == 1200) ModeIndex = ModeIndex_1600x1200[Depth];
	     }
	     break;
      }

   } else if(VBFlags2 & VB2_SISBRIDGE) {

      switch(HDisplay)
      {
	case 320:
	     if(VDisplay == 200)      ModeIndex = ModeIndex_320x200[Depth];
	     else if(VDisplay == 240) ModeIndex = ModeIndex_320x240[Depth];
	     break;
	case 400:
	     if(LCDwidth >= 800 && LCDheight >= 600) {
		if(VDisplay == 300) ModeIndex = ModeIndex_400x300[Depth];
	     }
	     break;
	case 512:
	     if(LCDwidth >= 1024 && LCDheight >= 768 && LCDwidth != 1152) {
		if(VDisplay == 384) ModeIndex = ModeIndex_512x384[Depth];
	     }
	     break;
	case 640:
	     if(VDisplay == 480)      ModeIndex = ModeIndex_640x480[Depth];
	     else if(VDisplay == 400) ModeIndex = ModeIndex_640x400[Depth];
	     break;
	case 720:
	     if(VGAEngine == SIS_315_VGA) {
		if(VDisplay == 480)      ModeIndex = ModeIndex_720x480[Depth];
		else if(VDisplay == 576) ModeIndex = ModeIndex_720x576[Depth];
	     }
	     break;
	case 768:
	     if(VGAEngine == SIS_315_VGA) {
		if(VDisplay == 576) ModeIndex = ModeIndex_768x576[Depth];
	     }
	     break;
	case 800:
	     if(VDisplay == 600) ModeIndex = ModeIndex_800x600[Depth];
	     if(VGAEngine == SIS_315_VGA) {
		if(VDisplay == 480) ModeIndex = ModeIndex_800x480[Depth];
	     }
	     break;
	case 848:
	     if(VGAEngine == SIS_315_VGA) {
		if(VDisplay == 480) ModeIndex = ModeIndex_848x480[Depth];
	     }
	     break;
	case 856:
	     if(VGAEngine == SIS_315_VGA) {
		if(VDisplay == 480) ModeIndex = ModeIndex_856x480[Depth];
	     }
	     break;
	case 960:
	     if(VGAEngine == SIS_315_VGA) {
		if(VDisplay == 540)      ModeIndex = ModeIndex_960x540[Depth];
		else if(VDisplay == 600) ModeIndex = ModeIndex_960x600[Depth];
	     }
	     break;
	case 1024:
	     if(VDisplay == 768) ModeIndex = ModeIndex_1024x768[Depth];
	     if(VGAEngine == SIS_315_VGA) {
		if(VDisplay == 576) ModeIndex = ModeIndex_1024x576[Depth];
	     }
	     break;
	case 1152:
	     if(VGAEngine == SIS_315_VGA) {
		if(VDisplay == 864) ModeIndex = ModeIndex_1152x864[Depth];
	     }
	     break;
	case 1280:
	     switch(VDisplay) {
	     case 720:
		ModeIndex = ModeIndex_1280x720[Depth];
		break;
	     case 768:
		if(VGAEngine == SIS_300_VGA) {
		   ModeIndex = ModeIndex_300_1280x768[Depth];
		} else {
		   ModeIndex = ModeIndex_310_1280x768[Depth];
		}
		break;
	     case 800:
		if(VGAEngine == SIS_315_VGA) {
		   ModeIndex = ModeIndex_1280x800[Depth];
		}
		break;
	     case 854:
		if(VGAEngine == SIS_315_VGA) {
		   ModeIndex = ModeIndex_1280x854[Depth];
		}
		break;
	     case 960:
		ModeIndex = ModeIndex_1280x960[Depth];
		break;
	     case 1024:
		ModeIndex = ModeIndex_1280x1024[Depth];
		break;
	     }
	     break;
	case 1360:
	     if(VGAEngine == SIS_315_VGA) {  /* OVER1280 only? */
		if(VDisplay == 768) ModeIndex = ModeIndex_1360x768[Depth];
	     }
	     break;
	case 1400:
	     if(VGAEngine == SIS_315_VGA) {
		if(VBFlags2 & VB2_LCDOVER1280BRIDGE) {
		   if(VDisplay == 1050) ModeIndex = ModeIndex_1400x1050[Depth];
		}
	     }
	     break;
	case 1600:
	     if(VGAEngine == SIS_315_VGA) {
		if(VBFlags2 & VB2_LCDOVER1280BRIDGE) {
		   if(VDisplay == 1200) ModeIndex = ModeIndex_1600x1200[Depth];
		}
	     }
	     break;
#ifndef VB_FORBID_CRT2LCD_OVER_1600
	case 1680:
	     if(VGAEngine == SIS_315_VGA) {
		if(VBFlags2 & VB2_LCDOVER1280BRIDGE) {
		   if(VDisplay == 1050) ModeIndex = ModeIndex_1680x1050[Depth];
		}
	     }
	     break;
	case 1920:
	     if(VGAEngine == SIS_315_VGA) {
		if(VBFlags2 & VB2_LCDOVER1600BRIDGE) {
		   if(VDisplay == 1440) ModeIndex = ModeIndex_1920x1440[Depth];
		}
	     }
	     break;
	case 2048:
	     if(VGAEngine == SIS_315_VGA) {
		if(VBFlags2 & VB2_LCDOVER1600BRIDGE) {
		   if(VDisplay == 1536) ModeIndex = ModeIndex_310_2048x1536[Depth];
		}
	     }
	     break;
#endif
      }
   }

   return ModeIndex;
}

unsigned short
SiS_GetModeID_TV(int VGAEngine, unsigned int VBFlags, int HDisplay, int VDisplay, int Depth,
			unsigned int VBFlags2)
{
   unsigned short ModeIndex = 0;

   if(VBFlags2 & VB2_CHRONTEL) {

      switch(HDisplay)
      {
	case 512:
	     if(VGAEngine == SIS_315_VGA) {
		if(VDisplay == 384) ModeIndex = ModeIndex_512x384[Depth];
	     }
	     break;
	case 640:
	     if(VDisplay == 480)      ModeIndex = ModeIndex_640x480[Depth];
	     else if(VDisplay == 400) ModeIndex = ModeIndex_640x400[Depth];
	     break;
	case 800:
	     if(VDisplay == 600) ModeIndex = ModeIndex_800x600[Depth];
	     break;
	case 1024:
	     if(VGAEngine == SIS_315_VGA) {
		if(VDisplay == 768) ModeIndex = ModeIndex_1024x768[Depth];
	     }
	     break;
      }

   } else if(VBFlags2 & VB2_SISTVBRIDGE) {

      switch(HDisplay)
      {
	case 320:
	     if(VDisplay == 200)      ModeIndex = ModeIndex_320x200[Depth];
	     else if(VDisplay == 240) ModeIndex = ModeIndex_320x240[Depth];
	     break;
	case 400:
	     if(VDisplay == 300) ModeIndex = ModeIndex_400x300[Depth];
	     break;
	case 512:
	     if( ((VBFlags & TV_YPBPR) && (VBFlags & (TV_YPBPR750P | TV_YPBPR1080I))) ||
		 (VBFlags & TV_HIVISION) 					      ||
		 ((!(VBFlags & (TV_YPBPR | TV_PALM))) && (VBFlags & TV_PAL)) ) {
		if(VDisplay == 384) ModeIndex = ModeIndex_512x384[Depth];
	     }
	     break;
	case 640:
	     if(VDisplay == 480)      ModeIndex = ModeIndex_640x480[Depth];
	     else if(VDisplay == 400) ModeIndex = ModeIndex_640x400[Depth];
	     break;
	case 720:
	     if((!(VBFlags & TV_HIVISION)) && (!((VBFlags & TV_YPBPR) && (VBFlags & TV_YPBPR1080I)))) {
		if(VDisplay == 480) {
		   ModeIndex = ModeIndex_720x480[Depth];
		} else if(VDisplay == 576) {
		   if( ((VBFlags & TV_YPBPR) && (VBFlags & TV_YPBPR750P)) ||
		       ((!(VBFlags & (TV_YPBPR | TV_PALM))) && (VBFlags & TV_PAL)) )
		      ModeIndex = ModeIndex_720x576[Depth];
		}
	     }
             break;
	case 768:
	     if((!(VBFlags & TV_HIVISION)) && (!((VBFlags & TV_YPBPR) && (VBFlags & TV_YPBPR1080I)))) {
		if( ((VBFlags & TV_YPBPR) && (VBFlags & TV_YPBPR750P)) ||
		    ((!(VBFlags & (TV_YPBPR | TV_PALM))) && (VBFlags & TV_PAL)) ) {
		   if(VDisplay == 576) ModeIndex = ModeIndex_768x576[Depth];
		}
             }
	     break;
	case 800:
	     if(VDisplay == 600) ModeIndex = ModeIndex_800x600[Depth];
	     else if(VDisplay == 480) {
		if(!((VBFlags & TV_YPBPR) && (VBFlags & TV_YPBPR750P))) {
		   ModeIndex = ModeIndex_800x480[Depth];
		}
	     }
	     break;
	case 960:
	     if(VGAEngine == SIS_315_VGA) {
		if(VDisplay == 600) {
		   if((VBFlags & TV_HIVISION) || ((VBFlags & TV_YPBPR) && (VBFlags & TV_YPBPR1080I))) {
		      ModeIndex = ModeIndex_960x600[Depth];
		   }
		}
	     }
	     break;
	case 1024:
	     if(VDisplay == 768) {
		if(VBFlags2 & VB2_30xBLV) {
		   ModeIndex = ModeIndex_1024x768[Depth];
		}
	     } else if(VDisplay == 576) {
		if( (VBFlags & TV_HIVISION) ||
		    ((VBFlags & TV_YPBPR) && (VBFlags & TV_YPBPR1080I)) ||
		    ((VBFlags2 & VB2_30xBLV) &&
		     ((!(VBFlags & (TV_YPBPR | TV_PALM))) && (VBFlags & TV_PAL))) ) {
		   ModeIndex = ModeIndex_1024x576[Depth];
		}
	     }
	     break;
	case 1280:
	     if(VDisplay == 720) {
		if((VBFlags & TV_HIVISION) ||
		   ((VBFlags & TV_YPBPR) && (VBFlags & (TV_YPBPR1080I | TV_YPBPR750P)))) {
		   ModeIndex = ModeIndex_1280x720[Depth];
		}
	     } else if(VDisplay == 1024) {
		if((VBFlags & TV_HIVISION) ||
		   ((VBFlags & TV_YPBPR) && (VBFlags & TV_YPBPR1080I))) {
		   ModeIndex = ModeIndex_1280x1024[Depth];
		}
	     }
	     break;
      }
   }
   return ModeIndex;
}

unsigned short
SiS_GetModeID_VGA2(int VGAEngine, unsigned int VBFlags, int HDisplay, int VDisplay, int Depth,
			unsigned int VBFlags2)
{
   if(!(VBFlags2 & VB2_SISVGA2BRIDGE)) return 0;

   if(HDisplay >= 1920) return 0;

   switch(HDisplay)
   {
	case 1600:
		if(VDisplay == 1200) {
			if(VGAEngine != SIS_315_VGA) return 0;
			if(!(VBFlags2 & VB2_30xB)) return 0;
		}
		break;
	case 1680:
		if(VDisplay == 1050) {
			if(VGAEngine != SIS_315_VGA) return 0;
			if(!(VBFlags2 & VB2_30xB)) return 0;
		}
		break;
   }

   return SiS_GetModeID(VGAEngine, 0, HDisplay, VDisplay, Depth, false, 0, 0);
}


/*********************************************/
/*          HELPER: SetReg, GetReg           */
/*********************************************/

void
SiS_SetReg(SISIOADDRESS port, u8 index, u8 data)
{
	outb(index, port);
	outb(data, port + 1);
}

void
SiS_SetRegByte(SISIOADDRESS port, u8 data)
{
	outb(data, port);
}

void
SiS_SetRegShort(SISIOADDRESS port, u16 data)
{
	outw(data, port);
}

void
SiS_SetRegLong(SISIOADDRESS port, u32 data)
{
	outl(data, port);
}

u8
SiS_GetReg(SISIOADDRESS port, u8 index)
{
	outb(index, port);
	return inb(port + 1);
}

u8
SiS_GetRegByte(SISIOADDRESS port)
{
	return inb(port);
}

u16
SiS_GetRegShort(SISIOADDRESS port)
{
	return inw(port);
}

u32
SiS_GetRegLong(SISIOADDRESS port)
{
	return inl(port);
}

void
SiS_SetRegANDOR(SISIOADDRESS Port, u8 Index, u8 DataAND, u8 DataOR)
{
   u8 temp;

   temp = SiS_GetReg(Port, Index);
   temp = (temp & (DataAND)) | DataOR;
   SiS_SetReg(Port, Index, temp);
}

void
SiS_SetRegAND(SISIOADDRESS Port, u8 Index, u8 DataAND)
{
   u8 temp;

   temp = SiS_GetReg(Port, Index);
   temp &= DataAND;
   SiS_SetReg(Port, Index, temp);
}

void
SiS_SetRegOR(SISIOADDRESS Port, u8 Index, u8 DataOR)
{
   u8 temp;

   temp = SiS_GetReg(Port, Index);
   temp |= DataOR;
   SiS_SetReg(Port, Index, temp);
}

/*********************************************/
/*      HELPER: DisplayOn, DisplayOff        */
/*********************************************/

void
SiS_DisplayOn(struct SiS_Private *SiS_Pr)
{
   SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x01,0xDF);
}

void
SiS_DisplayOff(struct SiS_Private *SiS_Pr)
{
   SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x01,0x20);
}


/*********************************************/
/*        HELPER: Init Port Addresses        */
/*********************************************/

void
SiSRegInit(struct SiS_Private *SiS_Pr, SISIOADDRESS BaseAddr)
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
   SiS_Pr->SiS_Part2Port = BaseAddr + SIS_CRT2_PORT_10;
   SiS_Pr->SiS_Part3Port = BaseAddr + SIS_CRT2_PORT_12;
   SiS_Pr->SiS_Part4Port = BaseAddr + SIS_CRT2_PORT_14;
   SiS_Pr->SiS_Part5Port = BaseAddr + SIS_CRT2_PORT_14 + 2;
   SiS_Pr->SiS_DDC_Port  = BaseAddr + 0x14;
   SiS_Pr->SiS_VidCapt   = BaseAddr + SIS_VIDEO_CAPTURE;
   SiS_Pr->SiS_VidPlay   = BaseAddr + SIS_VIDEO_PLAYBACK;
}

/*********************************************/
/*             HELPER: GetSysFlags           */
/*********************************************/

static void
SiS_GetSysFlags(struct SiS_Private *SiS_Pr)
{
   unsigned char cr5f, temp1, temp2;

   /* 661 and newer: NEVER write non-zero to SR11[7:4] */
   /* (SR11 is used for DDC and in enable/disablebridge) */
   SiS_Pr->SiS_SensibleSR11 = false;
   SiS_Pr->SiS_MyCR63 = 0x63;
   if(SiS_Pr->ChipType >= SIS_330) {
      SiS_Pr->SiS_MyCR63 = 0x53;
      if(SiS_Pr->ChipType >= SIS_661) {
         SiS_Pr->SiS_SensibleSR11 = true;
      }
   }

   /* You should use the macros, not these flags directly */

   SiS_Pr->SiS_SysFlags = 0;
   if(SiS_Pr->ChipType == SIS_650) {
      cr5f = SiS_GetReg(SiS_Pr->SiS_P3d4,0x5f) & 0xf0;
      SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x5c,0x07);
      temp1 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x5c) & 0xf8;
      SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x5c,0xf8);
      temp2 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x5c) & 0xf8;
      if((!temp1) || (temp2)) {
	 switch(cr5f) {
	    case 0x80:
	    case 0x90:
	    case 0xc0:
	       SiS_Pr->SiS_SysFlags |= SF_IsM650;
	       break;
	    case 0xa0:
	    case 0xb0:
	    case 0xe0:
	       SiS_Pr->SiS_SysFlags |= SF_Is651;
	       break;
	 }
      } else {
	 switch(cr5f) {
	    case 0x90:
	       temp1 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x5c) & 0xf8;
	       switch(temp1) {
		  case 0x00: SiS_Pr->SiS_SysFlags |= SF_IsM652; break;
		  case 0x40: SiS_Pr->SiS_SysFlags |= SF_IsM653; break;
		  default:   SiS_Pr->SiS_SysFlags |= SF_IsM650; break;
	       }
	       break;
	    case 0xb0:
	       SiS_Pr->SiS_SysFlags |= SF_Is652;
	       break;
	    default:
	       SiS_Pr->SiS_SysFlags |= SF_IsM650;
	       break;
	 }
      }
   }

   if(SiS_Pr->ChipType >= SIS_760 && SiS_Pr->ChipType <= SIS_761) {
      if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x78) & 0x30) {
         SiS_Pr->SiS_SysFlags |= SF_760LFB;
      }
      if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x79) & 0xf0) {
         SiS_Pr->SiS_SysFlags |= SF_760UMA;
      }
   }
}

/*********************************************/
/*         HELPER: Init PCI & Engines        */
/*********************************************/

static void
SiSInitPCIetc(struct SiS_Private *SiS_Pr)
{
   switch(SiS_Pr->ChipType) {
#ifdef CONFIG_FB_SIS_300
   case SIS_300:
   case SIS_540:
   case SIS_630:
   case SIS_730:
      /* Set - PCI LINEAR ADDRESSING ENABLE (0x80)
       *     - RELOCATED VGA IO ENABLED (0x20)
       *     - MMIO ENABLED (0x01)
       * Leave other bits untouched.
       */
      SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x20,0xa1);
      /*  - Enable 2D (0x40)
       *  - Enable 3D (0x02)
       *  - Enable 3D Vertex command fetch (0x10) ?
       *  - Enable 3D command parser (0x08) ?
       */
      SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x5A);
      break;
#endif
#ifdef CONFIG_FB_SIS_315
   case SIS_315H:
   case SIS_315:
   case SIS_315PRO:
   case SIS_650:
   case SIS_740:
   case SIS_330:
   case SIS_661:
   case SIS_741:
   case SIS_660:
   case SIS_760:
   case SIS_761:
   case SIS_340:
   case XGI_40:
      /* See above */
      SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x20,0xa1);
      /*  - Enable 3D G/L transformation engine (0x80)
       *  - Enable 2D (0x40)
       *  - Enable 3D vertex command fetch (0x10)
       *  - Enable 3D command parser (0x08)
       *  - Enable 3D (0x02)
       */
      SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0xDA);
      break;
   case XGI_20:
   case SIS_550:
      /* See above */
      SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x20,0xa1);
      /* No 3D engine ! */
      /*  - Enable 2D (0x40)
       *  - disable 3D
       */
      SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x1E,0x60,0x40);
      break;
#endif
   default:
      break;
   }
}

/*********************************************/
/*             HELPER: SetLVDSetc            */
/*********************************************/

static
void
SiSSetLVDSetc(struct SiS_Private *SiS_Pr)
{
   unsigned short temp;

   SiS_Pr->SiS_IF_DEF_LVDS = 0;
   SiS_Pr->SiS_IF_DEF_TRUMPION = 0;
   SiS_Pr->SiS_IF_DEF_CH70xx = 0;
   SiS_Pr->SiS_IF_DEF_CONEX = 0;

   SiS_Pr->SiS_ChrontelInit = 0;

   if(SiS_Pr->ChipType == XGI_20) return;

   /* Check for SiS30x first */
   temp = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x00);
   if((temp == 1) || (temp == 2)) return;

   switch(SiS_Pr->ChipType) {
#ifdef CONFIG_FB_SIS_300
   case SIS_540:
   case SIS_630:
   case SIS_730:
	temp = (SiS_GetReg(SiS_Pr->SiS_P3d4,0x37) & 0x0e) >> 1;
	if((temp >= 2) && (temp <= 5))	SiS_Pr->SiS_IF_DEF_LVDS = 1;
	if(temp == 3)			SiS_Pr->SiS_IF_DEF_TRUMPION = 1;
	if((temp == 4) || (temp == 5)) {
		/* Save power status (and error check) - UNUSED */
		SiS_Pr->SiS_Backup70xx = SiS_GetCH700x(SiS_Pr, 0x0e);
		SiS_Pr->SiS_IF_DEF_CH70xx = 1;
	}
	break;
#endif
#ifdef CONFIG_FB_SIS_315
   case SIS_550:
   case SIS_650:
   case SIS_740:
   case SIS_330:
	temp = (SiS_GetReg(SiS_Pr->SiS_P3d4,0x37) & 0x0e) >> 1;
	if((temp >= 2) && (temp <= 3))	SiS_Pr->SiS_IF_DEF_LVDS = 1;
	if(temp == 3)			SiS_Pr->SiS_IF_DEF_CH70xx = 2;
	break;
   case SIS_661:
   case SIS_741:
   case SIS_660:
   case SIS_760:
   case SIS_761:
   case SIS_340:
   case XGI_20:
   case XGI_40:
	temp = (SiS_GetReg(SiS_Pr->SiS_P3d4,0x38) & 0xe0) >> 5;
	if((temp >= 2) && (temp <= 3)) 	SiS_Pr->SiS_IF_DEF_LVDS = 1;
	if(temp == 3)			SiS_Pr->SiS_IF_DEF_CH70xx = 2;
	if(temp == 4)			SiS_Pr->SiS_IF_DEF_CONEX = 1;  /* Not yet supported */
	break;
#endif
   default:
	break;
   }
}

/*********************************************/
/*          HELPER: Enable DSTN/FSTN         */
/*********************************************/

void
SiS_SetEnableDstn(struct SiS_Private *SiS_Pr, int enable)
{
   SiS_Pr->SiS_IF_DEF_DSTN = enable ? 1 : 0;
}

void
SiS_SetEnableFstn(struct SiS_Private *SiS_Pr, int enable)
{
   SiS_Pr->SiS_IF_DEF_FSTN = enable ? 1 : 0;
}

/*********************************************/
/*            HELPER: Get modeflag           */
/*********************************************/

unsigned short
SiS_GetModeFlag(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short ModeIdIndex)
{
   if(SiS_Pr->UseCustomMode) {
      return SiS_Pr->CModeFlag;
   } else if(ModeNo <= 0x13) {
      return SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
   } else {
      return SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
   }
}

/*********************************************/
/*        HELPER: Determine ROM usage        */
/*********************************************/

bool
SiSDetermineROMLayout661(struct SiS_Private *SiS_Pr)
{
   unsigned char  *ROMAddr  = SiS_Pr->VirtualRomBase;
   unsigned short romversoffs, romvmaj = 1, romvmin = 0;

   if(SiS_Pr->ChipType >= XGI_20) {
      /* XGI ROMs don't qualify */
      return false;
   } else if(SiS_Pr->ChipType >= SIS_761) {
      /* I very much assume 761, 340 and newer will use new layout */
      return true;
   } else if(SiS_Pr->ChipType >= SIS_661) {
      if((ROMAddr[0x1a] == 'N') &&
	 (ROMAddr[0x1b] == 'e') &&
	 (ROMAddr[0x1c] == 'w') &&
	 (ROMAddr[0x1d] == 'V')) {
	 return true;
      }
      romversoffs = ROMAddr[0x16] | (ROMAddr[0x17] << 8);
      if(romversoffs) {
	 if((ROMAddr[romversoffs+1] == '.') || (ROMAddr[romversoffs+4] == '.')) {
	    romvmaj = ROMAddr[romversoffs] - '0';
	    romvmin = ((ROMAddr[romversoffs+2] -'0') * 10) + (ROMAddr[romversoffs+3] - '0');
	 }
      }
      if((romvmaj != 0) || (romvmin >= 92)) {
	 return true;
      }
   } else if(IS_SIS650740) {
      if((ROMAddr[0x1a] == 'N') &&
	 (ROMAddr[0x1b] == 'e') &&
	 (ROMAddr[0x1c] == 'w') &&
	 (ROMAddr[0x1d] == 'V')) {
	 return true;
      }
   }
   return false;
}

static void
SiSDetermineROMUsage(struct SiS_Private *SiS_Pr)
{
   unsigned char  *ROMAddr  = SiS_Pr->VirtualRomBase;
   unsigned short romptr = 0;

   SiS_Pr->SiS_UseROM = false;
   SiS_Pr->SiS_ROMNew = false;
   SiS_Pr->SiS_PWDOffset = 0;

   if(SiS_Pr->ChipType >= XGI_20) return;

   if((ROMAddr) && (SiS_Pr->UseROM)) {
      if(SiS_Pr->ChipType == SIS_300) {
	 /* 300: We check if the code starts below 0x220 by
	  * checking the jmp instruction at the beginning
	  * of the BIOS image.
	  */
	 if((ROMAddr[3] == 0xe9) && ((ROMAddr[5] << 8) | ROMAddr[4]) > 0x21a)
	    SiS_Pr->SiS_UseROM = true;
      } else if(SiS_Pr->ChipType < SIS_315H) {
	 /* Sony's VAIO BIOS 1.09 follows the standard, so perhaps
	  * the others do as well
	  */
	 SiS_Pr->SiS_UseROM = true;
      } else {
	 /* 315/330 series stick to the standard(s) */
	 SiS_Pr->SiS_UseROM = true;
	 if((SiS_Pr->SiS_ROMNew = SiSDetermineROMLayout661(SiS_Pr))) {
	    SiS_Pr->SiS_EMIOffset = 14;
	    SiS_Pr->SiS_PWDOffset = 17;
	    SiS_Pr->SiS661LCD2TableSize = 36;
	    /* Find out about LCD data table entry size */
	    if((romptr = SISGETROMW(0x0102))) {
	       if(ROMAddr[romptr + (32 * 16)] == 0xff)
		  SiS_Pr->SiS661LCD2TableSize = 32;
	       else if(ROMAddr[romptr + (34 * 16)] == 0xff)
		  SiS_Pr->SiS661LCD2TableSize = 34;
	       else if(ROMAddr[romptr + (36 * 16)] == 0xff)	   /* 0.94, 2.05.00+ */
		  SiS_Pr->SiS661LCD2TableSize = 36;
	       else if( (ROMAddr[romptr + (38 * 16)] == 0xff) ||   /* 2.00.00 - 2.02.00 */
		 	(ROMAddr[0x6F] & 0x01) ) {		   /* 2.03.00 - <2.05.00 */
		  SiS_Pr->SiS661LCD2TableSize = 38;		   /* UMC data layout abandoned at 2.05.00 */
		  SiS_Pr->SiS_EMIOffset = 16;
		  SiS_Pr->SiS_PWDOffset = 19;
	       }
	    }
	 }
      }
   }
}

/*********************************************/
/*        HELPER: SET SEGMENT REGISTERS      */
/*********************************************/

static void
SiS_SetSegRegLower(struct SiS_Private *SiS_Pr, unsigned short value)
{
   unsigned short temp;

   value &= 0x00ff;
   temp = SiS_GetRegByte(SiS_Pr->SiS_P3cb) & 0xf0;
   temp |= (value >> 4);
   SiS_SetRegByte(SiS_Pr->SiS_P3cb, temp);
   temp = SiS_GetRegByte(SiS_Pr->SiS_P3cd) & 0xf0;
   temp |= (value & 0x0f);
   SiS_SetRegByte(SiS_Pr->SiS_P3cd, temp);
}

static void
SiS_SetSegRegUpper(struct SiS_Private *SiS_Pr, unsigned short value)
{
   unsigned short temp;

   value &= 0x00ff;
   temp = SiS_GetRegByte(SiS_Pr->SiS_P3cb) & 0x0f;
   temp |= (value & 0xf0);
   SiS_SetRegByte(SiS_Pr->SiS_P3cb, temp);
   temp = SiS_GetRegByte(SiS_Pr->SiS_P3cd) & 0x0f;
   temp |= (value << 4);
   SiS_SetRegByte(SiS_Pr->SiS_P3cd, temp);
}

static void
SiS_SetSegmentReg(struct SiS_Private *SiS_Pr, unsigned short value)
{
   SiS_SetSegRegLower(SiS_Pr, value);
   SiS_SetSegRegUpper(SiS_Pr, value);
}

static void
SiS_ResetSegmentReg(struct SiS_Private *SiS_Pr)
{
   SiS_SetSegmentReg(SiS_Pr, 0);
}

static void
SiS_SetSegmentRegOver(struct SiS_Private *SiS_Pr, unsigned short value)
{
   unsigned short temp = value >> 8;

   temp &= 0x07;
   temp |= (temp << 4);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x1d,temp);
   SiS_SetSegmentReg(SiS_Pr, value);
}

static void
SiS_ResetSegmentRegOver(struct SiS_Private *SiS_Pr)
{
   SiS_SetSegmentRegOver(SiS_Pr, 0);
}

static void
SiS_ResetSegmentRegisters(struct SiS_Private *SiS_Pr)
{
   if((IS_SIS65x) || (SiS_Pr->ChipType >= SIS_661)) {
      SiS_ResetSegmentReg(SiS_Pr);
      SiS_ResetSegmentRegOver(SiS_Pr);
   }
}

/*********************************************/
/*             HELPER: GetVBType             */
/*********************************************/

static
void
SiS_GetVBType(struct SiS_Private *SiS_Pr)
{
   unsigned short flag = 0, rev = 0, nolcd = 0;
   unsigned short p4_0f, p4_25, p4_27;

   SiS_Pr->SiS_VBType = 0;

   if((SiS_Pr->SiS_IF_DEF_LVDS) || (SiS_Pr->SiS_IF_DEF_CONEX))
      return;

   if(SiS_Pr->ChipType == XGI_20)
      return;

   flag = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x00);

   if(flag > 3)
      return;

   rev = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x01);

   if(flag >= 2) {
      SiS_Pr->SiS_VBType = VB_SIS302B;
   } else if(flag == 1) {
      if(rev >= 0xC0) {
	 SiS_Pr->SiS_VBType = VB_SIS301C;
      } else if(rev >= 0xB0) {
	 SiS_Pr->SiS_VBType = VB_SIS301B;
	 /* Check if 30xB DH version (no LCD support, use Panel Link instead) */
	 nolcd = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x23);
	 if(!(nolcd & 0x02)) SiS_Pr->SiS_VBType |= VB_NoLCD;
      } else {
	 SiS_Pr->SiS_VBType = VB_SIS301;
      }
   }
   if(SiS_Pr->SiS_VBType & (VB_SIS301B | VB_SIS301C | VB_SIS302B)) {
      if(rev >= 0xE0) {
	 flag = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x39);
	 if(flag == 0xff) SiS_Pr->SiS_VBType = VB_SIS302LV;
	 else 	 	  SiS_Pr->SiS_VBType = VB_SIS301C;  /* VB_SIS302ELV; */
      } else if(rev >= 0xD0) {
	 SiS_Pr->SiS_VBType = VB_SIS301LV;
      }
   }
   if(SiS_Pr->SiS_VBType & (VB_SIS301C | VB_SIS301LV | VB_SIS302LV | VB_SIS302ELV)) {
      p4_0f = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x0f);
      p4_25 = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x25);
      p4_27 = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x27);
      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x0f,0x7f);
      SiS_SetRegOR(SiS_Pr->SiS_Part4Port,0x25,0x08);
      SiS_SetRegAND(SiS_Pr->SiS_Part4Port,0x27,0xfd);
      if(SiS_GetReg(SiS_Pr->SiS_Part4Port,0x26) & 0x08) {
         SiS_Pr->SiS_VBType |= VB_UMC;
      }
      SiS_SetReg(SiS_Pr->SiS_Part4Port,0x27,p4_27);
      SiS_SetReg(SiS_Pr->SiS_Part4Port,0x25,p4_25);
      SiS_SetReg(SiS_Pr->SiS_Part4Port,0x0f,p4_0f);
   }
}

/*********************************************/
/*           HELPER: Check RAM size          */
/*********************************************/

static bool
SiS_CheckMemorySize(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short ModeIdIndex)
{
   unsigned short AdapterMemSize = SiS_Pr->VideoMemorySize / (1024*1024);
   unsigned short modeflag = SiS_GetModeFlag(SiS_Pr, ModeNo, ModeIdIndex);
   unsigned short memorysize = ((modeflag & MemoryInfoFlag) >> MemorySizeShift) + 1;

   if(!AdapterMemSize) return true;

   if(AdapterMemSize < memorysize) return false;
   return true;
}

/*********************************************/
/*           HELPER: Get DRAM type           */
/*********************************************/

#ifdef CONFIG_FB_SIS_315
static unsigned char
SiS_Get310DRAMType(struct SiS_Private *SiS_Pr)
{
   unsigned char data;

   if((*SiS_Pr->pSiS_SoftSetting) & SoftDRAMType) {
      data = (*SiS_Pr->pSiS_SoftSetting) & 0x03;
   } else {
      if(SiS_Pr->ChipType >= XGI_20) {
         /* Do I need this? SR17 seems to be zero anyway... */
	 data = 0;
      } else if(SiS_Pr->ChipType >= SIS_340) {
	 /* TODO */
	 data = 0;
      } else if(SiS_Pr->ChipType >= SIS_661) {
	 if(SiS_Pr->SiS_ROMNew) {
	    data = ((SiS_GetReg(SiS_Pr->SiS_P3d4,0x78) & 0xc0) >> 6);
	 } else {
	    data = SiS_GetReg(SiS_Pr->SiS_P3d4,0x78) & 0x07;
	 }
      } else if(IS_SIS550650740) {
	 data = SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x07;
      } else {	/* 315, 330 */
	 data = SiS_GetReg(SiS_Pr->SiS_P3c4,0x3a) & 0x03;
	 if(SiS_Pr->ChipType == SIS_330) {
	    if(data > 1) {
	       switch(SiS_GetReg(SiS_Pr->SiS_P3d4,0x5f) & 0x30) {
	       case 0x00: data = 1; break;
	       case 0x10: data = 3; break;
	       case 0x20: data = 3; break;
	       case 0x30: data = 2; break;
	       }
	    } else {
	       data = 0;
	    }
	 }
      }
   }

   return data;
}

static unsigned short
SiS_GetMCLK(struct SiS_Private *SiS_Pr)
{
   unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
   unsigned short index;

   index = SiS_Get310DRAMType(SiS_Pr);
   if(SiS_Pr->ChipType >= SIS_661) {
      if(SiS_Pr->SiS_ROMNew) {
	 return((unsigned short)(SISGETROMW((0x90 + (index * 5) + 3))));
      }
      return(SiS_Pr->SiS_MCLKData_0[index].CLOCK);
   } else if(index >= 4) {
      return(SiS_Pr->SiS_MCLKData_1[index - 4].CLOCK);
   } else {
      return(SiS_Pr->SiS_MCLKData_0[index].CLOCK);
   }
}
#endif

/*********************************************/
/*           HELPER: ClearBuffer             */
/*********************************************/

static void
SiS_ClearBuffer(struct SiS_Private *SiS_Pr, unsigned short ModeNo)
{
   unsigned char  SISIOMEMTYPE *memaddr = SiS_Pr->VideoMemoryAddress;
   unsigned int   memsize = SiS_Pr->VideoMemorySize;
   unsigned short SISIOMEMTYPE *pBuffer;
   int i;

   if(!memaddr || !memsize) return;

   if(SiS_Pr->SiS_ModeType >= ModeEGA) {
      if(ModeNo > 0x13) {
	 memset_io(memaddr, 0, memsize);
      } else {
	 pBuffer = (unsigned short SISIOMEMTYPE *)memaddr;
	 for(i = 0; i < 0x4000; i++) writew(0x0000, &pBuffer[i]);
      }
   } else if(SiS_Pr->SiS_ModeType < ModeCGA) {
      pBuffer = (unsigned short SISIOMEMTYPE *)memaddr;
      for(i = 0; i < 0x4000; i++) writew(0x0720, &pBuffer[i]);
   } else {
      memset_io(memaddr, 0, 0x8000);
   }
}

/*********************************************/
/*           HELPER: SearchModeID            */
/*********************************************/

bool
SiS_SearchModeID(struct SiS_Private *SiS_Pr, unsigned short *ModeNo,
		unsigned short *ModeIdIndex)
{
   unsigned char VGAINFO = SiS_Pr->SiS_VGAINFO;

   if((*ModeNo) <= 0x13) {

      if((*ModeNo) <= 0x05) (*ModeNo) |= 0x01;

      for((*ModeIdIndex) = 0; ;(*ModeIdIndex)++) {
	 if(SiS_Pr->SiS_SModeIDTable[(*ModeIdIndex)].St_ModeID == (*ModeNo)) break;
	 if(SiS_Pr->SiS_SModeIDTable[(*ModeIdIndex)].St_ModeID == 0xFF) return false;
      }

      if((*ModeNo) == 0x07) {
	  if(VGAINFO & 0x10) (*ModeIdIndex)++;   /* 400 lines */
	  /* else 350 lines */
      }
      if((*ModeNo) <= 0x03) {
	 if(!(VGAINFO & 0x80)) (*ModeIdIndex)++;
	 if(VGAINFO & 0x10)    (*ModeIdIndex)++; /* 400 lines  */
	 /* else 350 lines  */
      }
      /* else 200 lines  */

   } else {

      for((*ModeIdIndex) = 0; ;(*ModeIdIndex)++) {
	 if(SiS_Pr->SiS_EModeIDTable[(*ModeIdIndex)].Ext_ModeID == (*ModeNo)) break;
	 if(SiS_Pr->SiS_EModeIDTable[(*ModeIdIndex)].Ext_ModeID == 0xFF) return false;
      }

   }
   return true;
}

/*********************************************/
/*            HELPER: GetModePtr             */
/*********************************************/

unsigned short
SiS_GetModePtr(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
   unsigned short index;

   if(ModeNo <= 0x13) {
      index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_StTableIndex;
   } else {
      if(SiS_Pr->SiS_ModeType <= ModeEGA) index = 0x1B;
      else index = 0x0F;
   }
   return index;
}

/*********************************************/
/*         HELPERS: Get some indices         */
/*********************************************/

unsigned short
SiS_GetRefCRTVCLK(struct SiS_Private *SiS_Pr, unsigned short Index, int UseWide)
{
   if(SiS_Pr->SiS_RefIndex[Index].Ext_InfoFlag & HaveWideTiming) {
      if(UseWide == 1) {
         return SiS_Pr->SiS_RefIndex[Index].Ext_CRTVCLK_WIDE;
      } else {
         return SiS_Pr->SiS_RefIndex[Index].Ext_CRTVCLK_NORM;
      }
   } else {
      return SiS_Pr->SiS_RefIndex[Index].Ext_CRTVCLK;
   }
}

unsigned short
SiS_GetRefCRT1CRTC(struct SiS_Private *SiS_Pr, unsigned short Index, int UseWide)
{
   if(SiS_Pr->SiS_RefIndex[Index].Ext_InfoFlag & HaveWideTiming) {
      if(UseWide == 1) {
         return SiS_Pr->SiS_RefIndex[Index].Ext_CRT1CRTC_WIDE;
      } else {
         return SiS_Pr->SiS_RefIndex[Index].Ext_CRT1CRTC_NORM;
      }
   } else {
      return SiS_Pr->SiS_RefIndex[Index].Ext_CRT1CRTC;
   }
}

/*********************************************/
/*           HELPER: LowModeTests            */
/*********************************************/

static bool
SiS_DoLowModeTest(struct SiS_Private *SiS_Pr, unsigned short ModeNo)
{
   unsigned short temp, temp1, temp2;

   if((ModeNo != 0x03) && (ModeNo != 0x10) && (ModeNo != 0x12))
      return true;
   temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x11);
   SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x11,0x80);
   temp1 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x00);
   SiS_SetReg(SiS_Pr->SiS_P3d4,0x00,0x55);
   temp2 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x00);
   SiS_SetReg(SiS_Pr->SiS_P3d4,0x00,temp1);
   SiS_SetReg(SiS_Pr->SiS_P3d4,0x11,temp);
   if((SiS_Pr->ChipType >= SIS_315H) ||
      (SiS_Pr->ChipType == SIS_300)) {
      if(temp2 == 0x55) return false;
      else return true;
   } else {
      if(temp2 != 0x55) return true;
      else {
	 SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x35,0x01);
	 return false;
      }
   }
}

static void
SiS_SetLowModeTest(struct SiS_Private *SiS_Pr, unsigned short ModeNo)
{
   if(SiS_DoLowModeTest(SiS_Pr, ModeNo)) {
      SiS_Pr->SiS_SetFlag |= LowModeTests;
   }
}

/*********************************************/
/*        HELPER: OPEN/CLOSE CRT1 CRTC       */
/*********************************************/

static void
SiS_OpenCRTC(struct SiS_Private *SiS_Pr)
{
   if(IS_SIS650) {
      SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x51,0x1f);
      if(IS_SIS651) SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x51,0x20);
      SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x56,0xe7);
   } else if(IS_SIS661741660760) {
      SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x61,0xf7);
      SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x51,0x1f);
      SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x56,0xe7);
      if(!SiS_Pr->SiS_ROMNew) {
	 SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x3a,0xef);
      }
   }
}

static void
SiS_CloseCRTC(struct SiS_Private *SiS_Pr)
{
#if 0 /* This locks some CRTC registers. We don't want that. */
   unsigned short temp1 = 0, temp2 = 0;

   if(IS_SIS661741660760) {
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
         temp1 = 0xa0; temp2 = 0x08;
      }
      SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x51,0x1f,temp1);
      SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x56,0xe7,temp2);
   }
#endif
}

static void
SiS_HandleCRT1(struct SiS_Private *SiS_Pr)
{
   /* Enable CRT1 gating */
   SiS_SetRegAND(SiS_Pr->SiS_P3d4,SiS_Pr->SiS_MyCR63,0xbf);
#if 0
   if(!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x15) & 0x01)) {
      if((SiS_GetReg(SiS_Pr->SiS_P3c4,0x15) & 0x0a) ||
         (SiS_GetReg(SiS_Pr->SiS_P3c4,0x16) & 0x01)) {
         SiS_SetRegOR(SiS_Pr->SiS_P3d4,SiS_Pr->SiS_MyCR63,0x40);
      }
   }
#endif
}

/*********************************************/
/*           HELPER: GetColorDepth           */
/*********************************************/

unsigned short
SiS_GetColorDepth(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short ModeIdIndex)
{
   static const unsigned short ColorDepth[6] = { 1, 2, 4, 4, 6, 8 };
   unsigned short modeflag;
   short index;

   /* Do NOT check UseCustomMode, will skrew up FIFO */
   if(ModeNo == 0xfe) {
      modeflag = SiS_Pr->CModeFlag;
   } else if(ModeNo <= 0x13) {
      modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
   } else {
      modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
   }

   index = (modeflag & ModeTypeMask) - ModeEGA;
   if(index < 0) index = 0;
   return ColorDepth[index];
}

/*********************************************/
/*             HELPER: GetOffset             */
/*********************************************/

unsigned short
SiS_GetOffset(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short ModeIdIndex, unsigned short RRTI)
{
   unsigned short xres, temp, colordepth, infoflag;

   if(SiS_Pr->UseCustomMode) {
      infoflag = SiS_Pr->CInfoFlag;
      xres = SiS_Pr->CHDisplay;
   } else {
      infoflag = SiS_Pr->SiS_RefIndex[RRTI].Ext_InfoFlag;
      xres = SiS_Pr->SiS_RefIndex[RRTI].XRes;
   }

   colordepth = SiS_GetColorDepth(SiS_Pr, ModeNo, ModeIdIndex);

   temp = xres / 16;
   if(infoflag & InterlaceMode) temp <<= 1;
   temp *= colordepth;
   if(xres % 16) temp += (colordepth >> 1);

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

   SiS_SetReg(SiS_Pr->SiS_P3c4,0x00,0x03);

   /* or "display off"  */
   SRdata = SiS_Pr->SiS_StandTable[StandTableIndex].SR[0] | 0x20;

   /* determine whether to force x8 dotclock */
   if((SiS_Pr->SiS_VBType & VB_SISVB) || (SiS_Pr->SiS_IF_DEF_LVDS)) {

      if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToTV)) {
         if(SiS_Pr->SiS_VBInfo & SetInSlaveMode)    SRdata |= 0x01;
      } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) SRdata |= 0x01;

   }

   SiS_SetReg(SiS_Pr->SiS_P3c4,0x01,SRdata);

   for(i = 2; i <= 4; i++) {
      SRdata = SiS_Pr->SiS_StandTable[StandTableIndex].SR[i - 1];
      SiS_SetReg(SiS_Pr->SiS_P3c4,i,SRdata);
   }
}

/*********************************************/
/*                  MISC                     */
/*********************************************/

static void
SiS_SetMiscRegs(struct SiS_Private *SiS_Pr, unsigned short StandTableIndex)
{
   unsigned char Miscdata;

   Miscdata = SiS_Pr->SiS_StandTable[StandTableIndex].MISC;

   if(SiS_Pr->ChipType < SIS_661) {
      if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	   Miscdata |= 0x0C;
	 }
      }
   }

   SiS_SetRegByte(SiS_Pr->SiS_P3c2,Miscdata);
}

/*********************************************/
/*                  CRTC                     */
/*********************************************/

static void
SiS_SetCRTCRegs(struct SiS_Private *SiS_Pr, unsigned short StandTableIndex)
{
   unsigned char  CRTCdata;
   unsigned short i;

   /* Unlock CRTC */
   SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x11,0x7f);

   for(i = 0; i <= 0x18; i++) {
      CRTCdata = SiS_Pr->SiS_StandTable[StandTableIndex].CRTC[i];
      SiS_SetReg(SiS_Pr->SiS_P3d4,i,CRTCdata);
   }

   if(SiS_Pr->ChipType >= SIS_661) {
      SiS_OpenCRTC(SiS_Pr);
      for(i = 0x13; i <= 0x14; i++) {
	 CRTCdata = SiS_Pr->SiS_StandTable[StandTableIndex].CRTC[i];
	 SiS_SetReg(SiS_Pr->SiS_P3d4,i,CRTCdata);
      }
   } else if( ( (SiS_Pr->ChipType == SIS_630) ||
	        (SiS_Pr->ChipType == SIS_730) )  &&
	      (SiS_Pr->ChipRevision >= 0x30) ) {
      if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
	 if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToTV)) {
	    SiS_SetReg(SiS_Pr->SiS_P3d4,0x18,0xFE);
	 }
      }
   }
}

/*********************************************/
/*                   ATT                     */
/*********************************************/

static void
SiS_SetATTRegs(struct SiS_Private *SiS_Pr, unsigned short StandTableIndex)
{
   unsigned char  ARdata;
   unsigned short i;

   for(i = 0; i <= 0x13; i++) {
      ARdata = SiS_Pr->SiS_StandTable[StandTableIndex].ATTR[i];

      if(i == 0x13) {
	 /* Pixel shift. If screen on LCD or TV is shifted left or right,
	  * this might be the cause.
	  */
	 if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
	    if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) ARdata = 0;
	 }
	 if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
	    if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
	       if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
		  if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) ARdata = 0;
	       }
	    }
	 }
	 if(SiS_Pr->ChipType >= SIS_661) {
	    if(SiS_Pr->SiS_VBInfo & (SetCRT2ToTV | SetCRT2ToLCD)) {
	       if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) ARdata = 0;
	    }
	 } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
	    if(SiS_Pr->ChipType >= SIS_315H) {
	       if(IS_SIS550650740660) {
		  /* 315, 330 don't do this */
		  if(SiS_Pr->SiS_VBType & VB_SIS30xB) {
		     if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) ARdata = 0;
		  } else {
		     ARdata = 0;
		  }
	       }
	    } else {
	       if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) ARdata = 0;
	    }
	 }
      }
      SiS_GetRegByte(SiS_Pr->SiS_P3da);		/* reset 3da  */
      SiS_SetRegByte(SiS_Pr->SiS_P3c0,i);	/* set index  */
      SiS_SetRegByte(SiS_Pr->SiS_P3c0,ARdata);	/* set data   */
   }

   SiS_GetRegByte(SiS_Pr->SiS_P3da);		/* reset 3da  */
   SiS_SetRegByte(SiS_Pr->SiS_P3c0,0x14);	/* set index  */
   SiS_SetRegByte(SiS_Pr->SiS_P3c0,0x00);	/* set data   */

   SiS_GetRegByte(SiS_Pr->SiS_P3da);
   SiS_SetRegByte(SiS_Pr->SiS_P3c0,0x20);	/* Enable Attribute  */
   SiS_GetRegByte(SiS_Pr->SiS_P3da);
}

/*********************************************/
/*                   GRC                     */
/*********************************************/

static void
SiS_SetGRCRegs(struct SiS_Private *SiS_Pr, unsigned short StandTableIndex)
{
   unsigned char  GRdata;
   unsigned short i;

   for(i = 0; i <= 0x08; i++) {
      GRdata = SiS_Pr->SiS_StandTable[StandTableIndex].GRC[i];
      SiS_SetReg(SiS_Pr->SiS_P3ce,i,GRdata);
   }

   if(SiS_Pr->SiS_ModeType > ModeVGA) {
      /* 256 color disable */
      SiS_SetRegAND(SiS_Pr->SiS_P3ce,0x05,0xBF);
   }
}

/*********************************************/
/*          CLEAR EXTENDED REGISTERS         */
/*********************************************/

static void
SiS_ClearExt1Regs(struct SiS_Private *SiS_Pr, unsigned short ModeNo)
{
   unsigned short i;

   for(i = 0x0A; i <= 0x0E; i++) {
      SiS_SetReg(SiS_Pr->SiS_P3c4,i,0x00);
   }

   if(SiS_Pr->ChipType >= SIS_315H) {
      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x37,0xFE);
      if(ModeNo <= 0x13) {
	 if(ModeNo == 0x06 || ModeNo >= 0x0e) {
	    SiS_SetReg(SiS_Pr->SiS_P3c4,0x0e,0x20);
	 }
      }
   }
}

/*********************************************/
/*                 RESET VCLK                */
/*********************************************/

static void
SiS_ResetCRT1VCLK(struct SiS_Private *SiS_Pr)
{
   if(SiS_Pr->ChipType >= SIS_315H) {
      if(SiS_Pr->ChipType < SIS_661) {
	 if(SiS_Pr->SiS_IF_DEF_LVDS == 0) return;
      }
   } else {
      if((SiS_Pr->SiS_IF_DEF_LVDS == 0) &&
	 (!(SiS_Pr->SiS_VBType & VB_SIS30xBLV)) ) {
	 return;
      }
   }

   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x31,0xcf,0x20);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2B,SiS_Pr->SiS_VCLKData[1].SR2B);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2C,SiS_Pr->SiS_VCLKData[1].SR2C);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2D,0x80);
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x31,0xcf,0x10);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2B,SiS_Pr->SiS_VCLKData[0].SR2B);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2C,SiS_Pr->SiS_VCLKData[0].SR2C);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2D,0x80);
}

/*********************************************/
/*                  SYNC                     */
/*********************************************/

static void
SiS_SetCRT1Sync(struct SiS_Private *SiS_Pr, unsigned short RRTI)
{
   unsigned short sync;

   if(SiS_Pr->UseCustomMode) {
      sync = SiS_Pr->CInfoFlag >> 8;
   } else {
      sync = SiS_Pr->SiS_RefIndex[RRTI].Ext_InfoFlag >> 8;
   }

   sync &= 0xC0;
   sync |= 0x2f;
   SiS_SetRegByte(SiS_Pr->SiS_P3c2,sync);
}

/*********************************************/
/*                  CRTC/2                   */
/*********************************************/

static void
SiS_SetCRT1CRTC(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short ModeIdIndex, unsigned short RRTI)
{
   unsigned short temp, i, j, modeflag;
   unsigned char  *crt1data = NULL;

   modeflag = SiS_GetModeFlag(SiS_Pr, ModeNo, ModeIdIndex);

   if(SiS_Pr->UseCustomMode) {

      crt1data = &SiS_Pr->CCRT1CRTC[0];

   } else {

      temp = SiS_GetRefCRT1CRTC(SiS_Pr, RRTI, SiS_Pr->SiS_UseWide);

      /* Alternate for 1600x1200 LCDA */
      if((temp == 0x20) && (SiS_Pr->Alternate1600x1200)) temp = 0x57;

      crt1data = (unsigned char *)&SiS_Pr->SiS_CRT1Table[temp].CR[0];

   }

   /* unlock cr0-7 */
   SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x11,0x7f);

   for(i = 0, j = 0; i <= 7; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_P3d4,j,crt1data[i]);
   }
   for(j = 0x10; i <= 10; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_P3d4,j,crt1data[i]);
   }
   for(j = 0x15; i <= 12; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_P3d4,j,crt1data[i]);
   }
   for(j = 0x0A; i <= 15; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_P3c4,j,crt1data[i]);
   }

   SiS_SetReg(SiS_Pr->SiS_P3c4,0x0E,crt1data[16] & 0xE0);

   temp = (crt1data[16] & 0x01) << 5;
   if(modeflag & DoubleScanMode) temp |= 0x80;
   SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x09,0x5F,temp);

   if(SiS_Pr->SiS_ModeType > ModeVGA) {
      SiS_SetReg(SiS_Pr->SiS_P3d4,0x14,0x4F);
   }

#ifdef CONFIG_FB_SIS_315
   if(SiS_Pr->ChipType == XGI_20) {
      SiS_SetReg(SiS_Pr->SiS_P3d4,0x04,crt1data[4] - 1);
      if(!(temp = crt1data[5] & 0x1f)) {
         SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x0c,0xfb);
      }
      SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x05,0xe0,((temp - 1) & 0x1f));
      temp = (crt1data[16] >> 5) + 3;
      if(temp > 7) temp -= 7;
      SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0e,0x1f,(temp << 5));
   }
#endif
}

/*********************************************/
/*               OFFSET & PITCH              */
/*********************************************/
/*  (partly overruled by SetPitch() in XF86) */
/*********************************************/

static void
SiS_SetCRT1Offset(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short ModeIdIndex, unsigned short RRTI)
{
   unsigned short temp, DisplayUnit, infoflag;

   if(SiS_Pr->UseCustomMode) {
      infoflag = SiS_Pr->CInfoFlag;
   } else {
      infoflag = SiS_Pr->SiS_RefIndex[RRTI].Ext_InfoFlag;
   }

   DisplayUnit = SiS_GetOffset(SiS_Pr, ModeNo, ModeIdIndex, RRTI);

   temp = (DisplayUnit >> 8) & 0x0f;
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0E,0xF0,temp);

   SiS_SetReg(SiS_Pr->SiS_P3d4,0x13,DisplayUnit & 0xFF);

   if(infoflag & InterlaceMode) DisplayUnit >>= 1;

   DisplayUnit <<= 5;
   temp = (DisplayUnit >> 8) + 1;
   if(DisplayUnit & 0xff) temp++;
   if(SiS_Pr->ChipType == XGI_20) {
      if(ModeNo == 0x4a || ModeNo == 0x49) temp--;
   }
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x10,temp);
}

/*********************************************/
/*                  VCLK                     */
/*********************************************/

static void
SiS_SetCRT1VCLK(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short ModeIdIndex, unsigned short RRTI)
{
   unsigned short index = 0, clka, clkb;

   if(SiS_Pr->UseCustomMode) {
      clka = SiS_Pr->CSR2B;
      clkb = SiS_Pr->CSR2C;
   } else {
      index = SiS_GetVCLK2Ptr(SiS_Pr, ModeNo, ModeIdIndex, RRTI);
      if((SiS_Pr->SiS_VBType & VB_SIS30xBLV) &&
	 (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
	 /* Alternate for 1600x1200 LCDA */
	 if((index == 0x21) && (SiS_Pr->Alternate1600x1200)) index = 0x72;
	 clka = SiS_Pr->SiS_VBVCLKData[index].Part4_A;
	 clkb = SiS_Pr->SiS_VBVCLKData[index].Part4_B;
      } else {
	 clka = SiS_Pr->SiS_VCLKData[index].SR2B;
	 clkb = SiS_Pr->SiS_VCLKData[index].SR2C;
      }
   }

   SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x31,0xCF);

   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2b,clka);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2c,clkb);

   if(SiS_Pr->ChipType >= SIS_315H) {
#ifdef CONFIG_FB_SIS_315
      SiS_SetReg(SiS_Pr->SiS_P3c4,0x2D,0x01);
      if(SiS_Pr->ChipType == XGI_20) {
         unsigned short mf = SiS_GetModeFlag(SiS_Pr, ModeNo, ModeIdIndex);
	 if(mf & HalfDCLK) {
	    SiS_SetReg(SiS_Pr->SiS_P3c4,0x2b,SiS_GetReg(SiS_Pr->SiS_P3c4,0x2b));
	    clkb = SiS_GetReg(SiS_Pr->SiS_P3c4,0x2c);
	    clkb = (((clkb & 0x1f) << 1) + 1) | (clkb & 0xe0);
	    SiS_SetReg(SiS_Pr->SiS_P3c4,0x2c,clkb);
	 }
      }
#endif
   } else {
      SiS_SetReg(SiS_Pr->SiS_P3c4,0x2D,0x80);
   }
}

/*********************************************/
/*                  FIFO                     */
/*********************************************/

#ifdef CONFIG_FB_SIS_300
void
SiS_GetFIFOThresholdIndex300(struct SiS_Private *SiS_Pr, unsigned short *idx1,
		unsigned short *idx2)
{
   unsigned short temp1, temp2;
   static const unsigned char ThTiming[8] = {
		1, 2, 2, 3, 0, 1, 1, 2
   };

   temp1 = temp2 = (SiS_GetReg(SiS_Pr->SiS_P3c4,0x18) & 0x62) >> 1;
   (*idx2) = (unsigned short)(ThTiming[((temp2 >> 3) | temp1) & 0x07]);
   (*idx1) = (unsigned short)(SiS_GetReg(SiS_Pr->SiS_P3c4,0x16) >> 6) & 0x03;
   (*idx1) |= (unsigned short)(((SiS_GetReg(SiS_Pr->SiS_P3c4,0x14) >> 4) & 0x0c));
   (*idx1) <<= 1;
}

static unsigned short
SiS_GetFIFOThresholdA300(unsigned short idx1, unsigned short idx2)
{
   static const unsigned char ThLowA[8 * 3] = {
		61, 3,52, 5,68, 7,100,11,
		43, 3,42, 5,54, 7, 78,11,
		34, 3,37, 5,47, 7, 67,11
   };

   return (unsigned short)((ThLowA[idx1 + 1] * idx2) + ThLowA[idx1]);
}

unsigned short
SiS_GetFIFOThresholdB300(unsigned short idx1, unsigned short idx2)
{
   static const unsigned char ThLowB[8 * 3] = {
		81, 4,72, 6,88, 8,120,12,
		55, 4,54, 6,66, 8, 90,12,
		42, 4,45, 6,55, 8, 75,12
   };

   return (unsigned short)((ThLowB[idx1 + 1] * idx2) + ThLowB[idx1]);
}

static unsigned short
SiS_DoCalcDelay(struct SiS_Private *SiS_Pr, unsigned short MCLK, unsigned short VCLK,
		unsigned short colordepth, unsigned short key)
{
   unsigned short idx1, idx2;
   unsigned int   longtemp = VCLK * colordepth;

   SiS_GetFIFOThresholdIndex300(SiS_Pr, &idx1, &idx2);

   if(key == 0) {
      longtemp *= SiS_GetFIFOThresholdA300(idx1, idx2);
   } else {
      longtemp *= SiS_GetFIFOThresholdB300(idx1, idx2);
   }
   idx1 = longtemp % (MCLK * 16);
   longtemp /= (MCLK * 16);
   if(idx1) longtemp++;
   return (unsigned short)longtemp;
}

static unsigned short
SiS_CalcDelay(struct SiS_Private *SiS_Pr, unsigned short VCLK,
		unsigned short colordepth, unsigned short MCLK)
{
   unsigned short temp1, temp2;

   temp2 = SiS_DoCalcDelay(SiS_Pr, MCLK, VCLK, colordepth, 0);
   temp1 = SiS_DoCalcDelay(SiS_Pr, MCLK, VCLK, colordepth, 1);
   if(temp1 < 4) temp1 = 4;
   temp1 -= 4;
   if(temp2 < temp1) temp2 = temp1;
   return temp2;
}

static void
SiS_SetCRT1FIFO_300(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short RefreshRateTableIndex)
{
   unsigned short ThresholdLow = 0;
   unsigned short temp, index, VCLK, MCLK, colorth;
   static const unsigned short colortharray[6] = { 1, 1, 2, 2, 3, 4 };

   if(ModeNo > 0x13) {

      /* Get VCLK  */
      if(SiS_Pr->UseCustomMode) {
	 VCLK = SiS_Pr->CSRClock;
      } else {
	 index = SiS_GetRefCRTVCLK(SiS_Pr, RefreshRateTableIndex, SiS_Pr->SiS_UseWide);
	 VCLK = SiS_Pr->SiS_VCLKData[index].CLOCK;
      }

      /* Get half colordepth */
      colorth = colortharray[(SiS_Pr->SiS_ModeType - ModeEGA)];

      /* Get MCLK  */
      index = SiS_GetReg(SiS_Pr->SiS_P3c4,0x3A) & 0x07;
      MCLK = SiS_Pr->SiS_MCLKData_0[index].CLOCK;

      temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35) & 0xc3;
      SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x16,0x3c,temp);

      do {
	 ThresholdLow = SiS_CalcDelay(SiS_Pr, VCLK, colorth, MCLK) + 1;
	 if(ThresholdLow < 0x13) break;
	 SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x16,0xfc);
	 ThresholdLow = 0x13;
	 temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x16) >> 6;
	 if(!temp) break;
	 SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x16,0x3f,((temp - 1) << 6));
      } while(0);

   } else ThresholdLow = 2;

   /* Write CRT/CPU threshold low, CRT/Engine threshold high */
   temp = (ThresholdLow << 4) | 0x0f;
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x08,temp);

   temp = (ThresholdLow & 0x10) << 1;
   if(ModeNo > 0x13) temp |= 0x40;
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0f,0x9f,temp);

   /* What is this? */
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x3B,0x09);

   /* Write CRT/CPU threshold high */
   temp = ThresholdLow + 3;
   if(temp > 0x0f) temp = 0x0f;
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x09,temp);
}

unsigned short
SiS_GetLatencyFactor630(struct SiS_Private *SiS_Pr, unsigned short index)
{
   static const unsigned char LatencyFactor[] = {
		97, 88, 86, 79, 77,  0,       /* 64  bit    BQ=2   */
		 0, 87, 85, 78, 76, 54,       /* 64  bit    BQ=1   */
		97, 88, 86, 79, 77,  0,       /* 128 bit    BQ=2   */
		 0, 79, 77, 70, 68, 48,       /* 128 bit    BQ=1   */
		80, 72, 69, 63, 61,  0,       /* 64  bit    BQ=2   */
		 0, 70, 68, 61, 59, 37,       /* 64  bit    BQ=1   */
		86, 77, 75, 68, 66,  0,       /* 128 bit    BQ=2   */
		 0, 68, 66, 59, 57, 37        /* 128 bit    BQ=1   */
   };
   static const unsigned char LatencyFactor730[] = {
		 69, 63, 61,
		 86, 79, 77,
		103, 96, 94,
		120,113,111,
		137,130,128
   };

   if(SiS_Pr->ChipType == SIS_730) {
      return (unsigned short)LatencyFactor730[index];
   } else {
      return (unsigned short)LatencyFactor[index];
   }
}

static unsigned short
SiS_CalcDelay2(struct SiS_Private *SiS_Pr, unsigned char key)
{
   unsigned short index;

   if(SiS_Pr->ChipType == SIS_730) {
      index = ((key & 0x0f) * 3) + ((key & 0xc0) >> 6);
   } else {
      index = (key & 0xe0) >> 5;
      if(key & 0x10)    index +=  6;
      if(!(key & 0x01)) index += 24;
      if(SiS_GetReg(SiS_Pr->SiS_P3c4,0x14) & 0x80) index += 12;
   }
   return SiS_GetLatencyFactor630(SiS_Pr, index);
}

static void
SiS_SetCRT1FIFO_630(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
                    unsigned short RefreshRateTableIndex)
{
   unsigned short  ThresholdLow = 0;
   unsigned short  i, data, VCLK, MCLK16, colorth = 0;
   unsigned int    templ, datal;
   const unsigned char *queuedata = NULL;
   static const unsigned char FQBQData[21] = {
		0x01,0x21,0x41,0x61,0x81,
		0x31,0x51,0x71,0x91,0xb1,
		0x00,0x20,0x40,0x60,0x80,
		0x30,0x50,0x70,0x90,0xb0,
		0xff
   };
   static const unsigned char FQBQData730[16] = {
		0x34,0x74,0xb4,
		0x23,0x63,0xa3,
		0x12,0x52,0x92,
		0x01,0x41,0x81,
		0x00,0x40,0x80,
		0xff
   };
   static const unsigned short colortharray[6] = {
		1, 1, 2, 2, 3, 4
   };

   i = 0;

	if (SiS_Pr->ChipType == SIS_730)
		queuedata = &FQBQData730[0];
	else
		queuedata = &FQBQData[0];

   if(ModeNo > 0x13) {

      /* Get VCLK  */
      if(SiS_Pr->UseCustomMode) {
	 VCLK = SiS_Pr->CSRClock;
      } else {
	 data = SiS_GetRefCRTVCLK(SiS_Pr, RefreshRateTableIndex, SiS_Pr->SiS_UseWide);
	 VCLK = SiS_Pr->SiS_VCLKData[data].CLOCK;
      }

      /* Get MCLK * 16 */
      data = SiS_GetReg(SiS_Pr->SiS_P3c4,0x1A) & 0x07;
      MCLK16 = SiS_Pr->SiS_MCLKData_0[data].CLOCK * 16;

      /* Get half colordepth */
      colorth = colortharray[(SiS_Pr->SiS_ModeType - ModeEGA)];

      do {
	 templ = SiS_CalcDelay2(SiS_Pr, queuedata[i]) * VCLK * colorth;

	 datal = templ % MCLK16;
	 templ = (templ / MCLK16) + 1;
	 if(datal) templ++;

	 if(templ > 0x13) {
	    if(queuedata[i + 1] == 0xFF) {
	       ThresholdLow = 0x13;
	       break;
	    }
	    i++;
	 } else {
	    ThresholdLow = templ;
	    break;
	 }
      } while(queuedata[i] != 0xFF);

   } else {

      if(SiS_Pr->ChipType != SIS_730) i = 9;
      ThresholdLow = 0x02;

   }

   /* Write CRT/CPU threshold low, CRT/Engine threshold high */
   data = ((ThresholdLow & 0x0f) << 4) | 0x0f;
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x08,data);

   data = (ThresholdLow & 0x10) << 1;
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0F,0xDF,data);

   /* What is this? */
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x3B,0x09);

   /* Write CRT/CPU threshold high (gap = 3) */
   data = ThresholdLow + 3;
   if(data > 0x0f) data = 0x0f;
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x09,0x80,data);

  /* Write foreground and background queue */
   templ = sisfb_read_nbridge_pci_dword(SiS_Pr, 0x50);

   if(SiS_Pr->ChipType == SIS_730) {

      templ &= 0xfffff9ff;
      templ |= ((queuedata[i] & 0xc0) << 3);

   } else {

      templ &= 0xf0ffffff;
      if( (ModeNo <= 0x13) &&
          (SiS_Pr->ChipType == SIS_630) &&
	  (SiS_Pr->ChipRevision >= 0x30) ) {
	 templ |= 0x0b000000;
      } else {
         templ |= ((queuedata[i] & 0xf0) << 20);
      }

   }

   sisfb_write_nbridge_pci_dword(SiS_Pr, 0x50, templ);
   templ = sisfb_read_nbridge_pci_dword(SiS_Pr, 0xA0);

   /* GUI grant timer (PCI config 0xA3) */
   if(SiS_Pr->ChipType == SIS_730) {

      templ &= 0x00ffffff;
      datal = queuedata[i] << 8;
      templ |= (((datal & 0x0f00) | ((datal & 0x3000) >> 8)) << 20);

   } else {

      templ &= 0xf0ffffff;
      templ |= ((queuedata[i] & 0x0f) << 24);

   }

   sisfb_write_nbridge_pci_dword(SiS_Pr, 0xA0, templ);
}
#endif /* CONFIG_FB_SIS_300 */

#ifdef CONFIG_FB_SIS_315
static void
SiS_SetCRT1FIFO_310(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
   unsigned short modeflag;

   /* disable auto-threshold */
   SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x3D,0xFE);

   modeflag = SiS_GetModeFlag(SiS_Pr, ModeNo, ModeIdIndex);

   SiS_SetReg(SiS_Pr->SiS_P3c4,0x08,0xAE);
   SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x09,0xF0);
   if(ModeNo > 0x13) {
      if(SiS_Pr->ChipType >= XGI_20) {
	 SiS_SetReg(SiS_Pr->SiS_P3c4,0x08,0x34);
	 SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x3D,0x01);
      } else if(SiS_Pr->ChipType >= SIS_661) {
	 if(!(modeflag & HalfDCLK)) {
	    SiS_SetReg(SiS_Pr->SiS_P3c4,0x08,0x34);
	    SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x3D,0x01);
	 }
      } else {
	 if((!(modeflag & DoubleScanMode)) || (!(modeflag & HalfDCLK))) {
	    SiS_SetReg(SiS_Pr->SiS_P3c4,0x08,0x34);
	    SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x3D,0x01);
	 }
      }
   }
}
#endif

/*********************************************/
/*              MODE REGISTERS               */
/*********************************************/

static void
SiS_SetVCLKState(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short RefreshRateTableIndex, unsigned short ModeIdIndex)
{
   unsigned short data = 0, VCLK = 0, index = 0;

   if(ModeNo > 0x13) {
      if(SiS_Pr->UseCustomMode) {
         VCLK = SiS_Pr->CSRClock;
      } else {
         index = SiS_GetVCLK2Ptr(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
         VCLK = SiS_Pr->SiS_VCLKData[index].CLOCK;
      }
   }

   if(SiS_Pr->ChipType < SIS_315H) {
#ifdef CONFIG_FB_SIS_300
      if(VCLK > 150) data |= 0x80;
      SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x07,0x7B,data);

      data = 0x00;
      if(VCLK >= 150) data |= 0x08;
      SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x32,0xF7,data);
#endif
   } else if(SiS_Pr->ChipType < XGI_20) {
#ifdef CONFIG_FB_SIS_315
      if(VCLK >= 166) data |= 0x0c;
      SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x32,0xf3,data);

      if(VCLK >= 166) {
         SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1f,0xe7);
      }
#endif
   } else {
#ifdef CONFIG_FB_SIS_315
      if(VCLK >= 200) data |= 0x0c;
      if(SiS_Pr->ChipType == XGI_20) data &= ~0x04;
      SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x32,0xf3,data);
      if(SiS_Pr->ChipType != XGI_20) {
         data = SiS_GetReg(SiS_Pr->SiS_P3c4,0x1f) & 0xe7;
	 if(VCLK < 200) data |= 0x10;
	 SiS_SetReg(SiS_Pr->SiS_P3c4,0x1f,data);
      }
#endif
   }

   /* DAC speed */
   if(SiS_Pr->ChipType >= SIS_661) {

      SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x07,0xE8,0x10);

   } else {

      data = 0x03;
      if(VCLK >= 260)      data = 0x00;
      else if(VCLK >= 160) data = 0x01;
      else if(VCLK >= 135) data = 0x02;

      if(SiS_Pr->ChipType == SIS_540) {
         /* Was == 203 or < 234 which made no sense */
         if (VCLK < 234) data = 0x02;
      }

      if(SiS_Pr->ChipType < SIS_315H) {
         SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x07,0xFC,data);
      } else {
         if(SiS_Pr->ChipType > SIS_315PRO) {
            if(ModeNo > 0x13) data &= 0xfc;
         }
         SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x07,0xF8,data);
      }

   }
}

static void
SiS_SetCRT1ModeRegs(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short ModeIdIndex, unsigned short RRTI)
{
   unsigned short data, infoflag = 0, modeflag;
#ifdef CONFIG_FB_SIS_315
   unsigned char  *ROMAddr  = SiS_Pr->VirtualRomBase;
   unsigned short data2, data3;
#endif

   modeflag = SiS_GetModeFlag(SiS_Pr, ModeNo, ModeIdIndex);

   if(SiS_Pr->UseCustomMode) {
      infoflag = SiS_Pr->CInfoFlag;
   } else {
      if(ModeNo > 0x13) {
	 infoflag = SiS_Pr->SiS_RefIndex[RRTI].Ext_InfoFlag;
      }
   }

   /* Disable DPMS */
   SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1F,0x3F);

   data = 0;
   if(ModeNo > 0x13) {
      if(SiS_Pr->SiS_ModeType > ModeEGA) {
         data |= 0x02;
         data |= ((SiS_Pr->SiS_ModeType - ModeVGA) << 2);
      }
      if(infoflag & InterlaceMode) data |= 0x20;
   }
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x06,0xC0,data);

   if(SiS_Pr->ChipType != SIS_300) {
      data = 0;
      if(infoflag & InterlaceMode) {
	 /* data = (Hsync / 8) - ((Htotal / 8) / 2) + 3 */
	 int hrs = (SiS_GetReg(SiS_Pr->SiS_P3d4,0x04) |
		    ((SiS_GetReg(SiS_Pr->SiS_P3c4,0x0b) & 0xc0) << 2)) - 3;
	 int hto = (SiS_GetReg(SiS_Pr->SiS_P3d4,0x00) |
		    ((SiS_GetReg(SiS_Pr->SiS_P3c4,0x0b) & 0x03) << 8)) + 5;
	 data = hrs - (hto >> 1) + 3;
      }
      SiS_SetReg(SiS_Pr->SiS_P3d4,0x19,data);
      SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x1a,0xFC,((data >> 8) & 0x03));
   }

   if(modeflag & HalfDCLK) {
      SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x01,0x08);
   }

   data = 0;
   if(modeflag & LineCompareOff) data = 0x08;
   if(SiS_Pr->ChipType == SIS_300) {
      SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0F,0xF7,data);
   } else {
      if(SiS_Pr->ChipType >= XGI_20) data |= 0x20;
      if(SiS_Pr->SiS_ModeType == ModeEGA) {
	 if(ModeNo > 0x13) {
	    data |= 0x40;
	 }
      }
      SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0F,0xB7,data);
   }

#ifdef CONFIG_FB_SIS_315
   if(SiS_Pr->ChipType >= SIS_315H) {
      SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x31,0xfb);
   }

   if(SiS_Pr->ChipType == SIS_315PRO) {

      data = SiS_Pr->SiS_SR15[(2 * 4) + SiS_Get310DRAMType(SiS_Pr)];
      if(SiS_Pr->SiS_ModeType == ModeText) {
	 data &= 0xc7;
      } else {
	 data2 = SiS_GetOffset(SiS_Pr, ModeNo, ModeIdIndex, RRTI) >> 1;
	 if(infoflag & InterlaceMode) data2 >>= 1;
	 data3 = SiS_GetColorDepth(SiS_Pr, ModeNo, ModeIdIndex) >> 1;
	 if(data3) data2 /= data3;
	 if(data2 >= 0x50) {
	    data &= 0x0f;
	    data |= 0x50;
	 }
      }
      SiS_SetReg(SiS_Pr->SiS_P3c4,0x17,data);

   } else if((SiS_Pr->ChipType == SIS_330) || (SiS_Pr->SiS_SysFlags & SF_760LFB)) {

      data = SiS_Get310DRAMType(SiS_Pr);
      if(SiS_Pr->ChipType == SIS_330) {
	 data = SiS_Pr->SiS_SR15[(2 * 4) + data];
      } else {
	 if(SiS_Pr->SiS_ROMNew)	     data = ROMAddr[0xf6];
	 else if(SiS_Pr->SiS_UseROM) data = ROMAddr[0x100 + data];
	 else			     data = 0xba;
      }
      if(SiS_Pr->SiS_ModeType <= ModeEGA) {
	 data &= 0xc7;
      } else {
	 if(SiS_Pr->UseCustomMode) {
	    data2 = SiS_Pr->CSRClock;
	 } else {
	    data2 = SiS_GetVCLK2Ptr(SiS_Pr, ModeNo, ModeIdIndex, RRTI);
	    data2 = SiS_Pr->SiS_VCLKData[data2].CLOCK;
	 }

	 data3 = SiS_GetColorDepth(SiS_Pr, ModeNo, ModeIdIndex) >> 1;
	 if(data3) data2 *= data3;

	 data2 = ((unsigned int)(SiS_GetMCLK(SiS_Pr) * 1024)) / data2;

	 if(SiS_Pr->ChipType == SIS_330) {
	    if(SiS_Pr->SiS_ModeType != Mode16Bpp) {
	       if     (data2 >= 0x19c) data = 0xba;
	       else if(data2 >= 0x140) data = 0x7a;
	       else if(data2 >= 0x101) data = 0x3a;
	       else if(data2 >= 0xf5)  data = 0x32;
	       else if(data2 >= 0xe2)  data = 0x2a;
	       else if(data2 >= 0xc4)  data = 0x22;
	       else if(data2 >= 0xac)  data = 0x1a;
	       else if(data2 >= 0x9e)  data = 0x12;
	       else if(data2 >= 0x8e)  data = 0x0a;
	       else                    data = 0x02;
	    } else {
	       if(data2 >= 0x127)      data = 0xba;
	       else                    data = 0x7a;
	    }
	 } else {  /* 76x+LFB */
	    if     (data2 >= 0x190) data = 0xba;
	    else if(data2 >= 0xff)  data = 0x7a;
	    else if(data2 >= 0xd3)  data = 0x3a;
	    else if(data2 >= 0xa9)  data = 0x1a;
	    else if(data2 >= 0x93)  data = 0x0a;
	    else                    data = 0x02;
	 }
      }
      SiS_SetReg(SiS_Pr->SiS_P3c4,0x17,data);

   }
      /* XGI: Nothing. */
      /* TODO: Check SiS340 */
#endif

   data = 0x60;
   if(SiS_Pr->SiS_ModeType != ModeText) {
      data ^= 0x60;
      if(SiS_Pr->SiS_ModeType != ModeEGA) {
         data ^= 0xA0;
      }
   }
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x21,0x1F,data);

   SiS_SetVCLKState(SiS_Pr, ModeNo, RRTI, ModeIdIndex);

#ifdef CONFIG_FB_SIS_315
   if(((SiS_Pr->ChipType >= SIS_315H) && (SiS_Pr->ChipType < SIS_661)) ||
       (SiS_Pr->ChipType == XGI_40)) {
      if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x31) & 0x40) {
         SiS_SetReg(SiS_Pr->SiS_P3d4,0x52,0x2c);
      } else {
         SiS_SetReg(SiS_Pr->SiS_P3d4,0x52,0x6c);
      }
   } else if(SiS_Pr->ChipType == XGI_20) {
      if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x31) & 0x40) {
         SiS_SetReg(SiS_Pr->SiS_P3d4,0x52,0x33);
      } else {
         SiS_SetReg(SiS_Pr->SiS_P3d4,0x52,0x73);
      }
      SiS_SetReg(SiS_Pr->SiS_P3d4,0x51,0x02);
   }
#endif
}

#ifdef CONFIG_FB_SIS_315
static void
SiS_SetupDualChip(struct SiS_Private *SiS_Pr)
{
#if 0
   /* TODO: Find out about IOAddress2 */
   SISIOADDRESS P2_3c2 = SiS_Pr->IOAddress2 + 0x12;
   SISIOADDRESS P2_3c4 = SiS_Pr->IOAddress2 + 0x14;
   SISIOADDRESS P2_3ce = SiS_Pr->IOAddress2 + 0x1e;
   int i;

   if((SiS_Pr->ChipRevision != 0) ||
      (!(SiS_GetReg(SiS_Pr->SiS_P3c4,0x3a) & 0x04)))
      return;

   for(i = 0; i <= 4; i++) {					/* SR00 - SR04 */
      SiS_SetReg(P2_3c4,i,SiS_GetReg(SiS_Pr->SiS_P3c4,i));
   }
   for(i = 0; i <= 8; i++) {					/* GR00 - GR08 */
      SiS_SetReg(P2_3ce,i,SiS_GetReg(SiS_Pr->SiS_P3ce,i));
   }
   SiS_SetReg(P2_3c4,0x05,0x86);
   SiS_SetReg(P2_3c4,0x06,SiS_GetReg(SiS_Pr->SiS_P3c4,0x06));	/* SR06 */
   SiS_SetReg(P2_3c4,0x21,SiS_GetReg(SiS_Pr->SiS_P3c4,0x21));	/* SR21 */
   SiS_SetRegByte(P2_3c2,SiS_GetRegByte(SiS_Pr->SiS_P3cc));	/* MISC */
   SiS_SetReg(P2_3c4,0x05,0x00);
#endif
}
#endif

/*********************************************/
/*                 LOAD DAC                  */
/*********************************************/

static void
SiS_WriteDAC(struct SiS_Private *SiS_Pr, SISIOADDRESS DACData, unsigned short shiftflag,
             unsigned short dl, unsigned short ah, unsigned short al, unsigned short dh)
{
   unsigned short d1, d2, d3;

   switch(dl) {
   case  0: d1 = dh; d2 = ah; d3 = al; break;
   case  1: d1 = ah; d2 = al; d3 = dh; break;
   default: d1 = al; d2 = dh; d3 = ah;
   }
   SiS_SetRegByte(DACData, (d1 << shiftflag));
   SiS_SetRegByte(DACData, (d2 << shiftflag));
   SiS_SetRegByte(DACData, (d3 << shiftflag));
}

void
SiS_LoadDAC(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
   unsigned short data, data2, time, i, j, k, m, n, o;
   unsigned short si, di, bx, sf;
   SISIOADDRESS DACAddr, DACData;
   const unsigned char *table = NULL;

   data = SiS_GetModeFlag(SiS_Pr, ModeNo, ModeIdIndex) & DACInfoFlag;

   j = time = 64;
   if(data == 0x00)      table = SiS_MDA_DAC;
   else if(data == 0x08) table = SiS_CGA_DAC;
   else if(data == 0x10) table = SiS_EGA_DAC;
   else if(data == 0x18) {
      j = 16;
      time = 256;
      table = SiS_VGA_DAC;
   }

   if( ( (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) &&        /* 301B-DH LCD */
         (SiS_Pr->SiS_VBType & VB_NoLCD) )        ||
       (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)       ||   /* LCDA */
       (!(SiS_Pr->SiS_SetFlag & ProgrammingCRT2)) ) {  /* Programming CRT1 */
      SiS_SetRegByte(SiS_Pr->SiS_P3c6,0xFF);
      DACAddr = SiS_Pr->SiS_P3c8;
      DACData = SiS_Pr->SiS_P3c9;
      sf = 0;
   } else {
      DACAddr = SiS_Pr->SiS_Part5Port;
      DACData = SiS_Pr->SiS_Part5Port + 1;
      sf = 2;
   }

   SiS_SetRegByte(DACAddr,0x00);

   for(i = 0; i < j; i++) {
      data = table[i];
      for(k = 0; k < 3; k++) {
	data2 = 0;
	if(data & 0x01) data2 += 0x2A;
	if(data & 0x02) data2 += 0x15;
	SiS_SetRegByte(DACData, (data2 << sf));
	data >>= 2;
      }
   }

   if(time == 256) {
      for(i = 16; i < 32; i++) {
	 data = table[i] << sf;
	 for(k = 0; k < 3; k++) SiS_SetRegByte(DACData, data);
      }
      si = 32;
      for(m = 0; m < 9; m++) {
	 di = si;
	 bx = si + 4;
	 for(n = 0; n < 3; n++) {
	    for(o = 0; o < 5; o++) {
	       SiS_WriteDAC(SiS_Pr, DACData, sf, n, table[di], table[bx], table[si]);
	       si++;
	    }
	    si -= 2;
	    for(o = 0; o < 3; o++) {
	       SiS_WriteDAC(SiS_Pr, DACData, sf, n, table[di], table[si], table[bx]);
	       si--;
	    }
	 }            /* for n < 3 */
	 si += 5;
      }               /* for m < 9 */
   }
}

/*********************************************/
/*         SET CRT1 REGISTER GROUP           */
/*********************************************/

static void
SiS_SetCRT1Group(struct SiS_Private *SiS_Pr, unsigned short ModeNo, unsigned short ModeIdIndex)
{
   unsigned short StandTableIndex, RefreshRateTableIndex;

   SiS_Pr->SiS_CRT1Mode = ModeNo;

   StandTableIndex = SiS_GetModePtr(SiS_Pr, ModeNo, ModeIdIndex);

   if(SiS_Pr->SiS_SetFlag & LowModeTests) {
      if(SiS_Pr->SiS_VBInfo & (SetSimuScanMode | SwitchCRT2)) {
         SiS_DisableBridge(SiS_Pr);
      }
   }

   SiS_ResetSegmentRegisters(SiS_Pr);

   SiS_SetSeqRegs(SiS_Pr, StandTableIndex);
   SiS_SetMiscRegs(SiS_Pr, StandTableIndex);
   SiS_SetCRTCRegs(SiS_Pr, StandTableIndex);
   SiS_SetATTRegs(SiS_Pr, StandTableIndex);
   SiS_SetGRCRegs(SiS_Pr, StandTableIndex);
   SiS_ClearExt1Regs(SiS_Pr, ModeNo);
   SiS_ResetCRT1VCLK(SiS_Pr);

   SiS_Pr->SiS_SelectCRT2Rate = 0;
   SiS_Pr->SiS_SetFlag &= (~ProgrammingCRT2);

   if(SiS_Pr->SiS_VBInfo & SetSimuScanMode) {
      if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
         SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
      }
   }

   if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
      SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
   }

   RefreshRateTableIndex = SiS_GetRatePtr(SiS_Pr, ModeNo, ModeIdIndex);

   if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
      SiS_Pr->SiS_SetFlag &= ~ProgrammingCRT2;
   }

   if(RefreshRateTableIndex != 0xFFFF) {
      SiS_SetCRT1Sync(SiS_Pr, RefreshRateTableIndex);
      SiS_SetCRT1CRTC(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
      SiS_SetCRT1Offset(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
      SiS_SetCRT1VCLK(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);
   }

   switch(SiS_Pr->ChipType) {
#ifdef CONFIG_FB_SIS_300
   case SIS_300:
      SiS_SetCRT1FIFO_300(SiS_Pr, ModeNo, RefreshRateTableIndex);
      break;
   case SIS_540:
   case SIS_630:
   case SIS_730:
      SiS_SetCRT1FIFO_630(SiS_Pr, ModeNo, RefreshRateTableIndex);
      break;
#endif
   default:
#ifdef CONFIG_FB_SIS_315
      if(SiS_Pr->ChipType == XGI_20) {
         unsigned char sr2b = 0, sr2c = 0;
         switch(ModeNo) {
	 case 0x00:
	 case 0x01: sr2b = 0x4e; sr2c = 0xe9; break;
	 case 0x04:
	 case 0x05:
	 case 0x0d: sr2b = 0x1b; sr2c = 0xe3; break;
	 }
	 if(sr2b) {
            SiS_SetReg(SiS_Pr->SiS_P3c4,0x2b,sr2b);
	    SiS_SetReg(SiS_Pr->SiS_P3c4,0x2c,sr2c);
	    SiS_SetRegByte(SiS_Pr->SiS_P3c2,(SiS_GetRegByte(SiS_Pr->SiS_P3cc) | 0x0c));
	 }
      }
      SiS_SetCRT1FIFO_310(SiS_Pr, ModeNo, ModeIdIndex);
#endif
      break;
   }

   SiS_SetCRT1ModeRegs(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex);

#ifdef CONFIG_FB_SIS_315
   if(SiS_Pr->ChipType == XGI_40) {
      SiS_SetupDualChip(SiS_Pr);
   }
#endif

   SiS_LoadDAC(SiS_Pr, ModeNo, ModeIdIndex);

   if(SiS_Pr->SiS_flag_clearbuffer) {
      SiS_ClearBuffer(SiS_Pr, ModeNo);
   }

   if(!(SiS_Pr->SiS_VBInfo & (SetSimuScanMode | SwitchCRT2 | SetCRT2ToLCDA))) {
      SiS_WaitRetrace1(SiS_Pr);
      SiS_DisplayOn(SiS_Pr);
   }
}

/*********************************************/
/*       HELPER: VIDEO BRIDGE PROG CLK       */
/*********************************************/

static void
SiS_InitVB(struct SiS_Private *SiS_Pr)
{
   unsigned char *ROMAddr = SiS_Pr->VirtualRomBase;

   SiS_Pr->Init_P4_0E = 0;
   if(SiS_Pr->SiS_ROMNew) {
      SiS_Pr->Init_P4_0E = ROMAddr[0x82];
   } else if(SiS_Pr->ChipType >= XGI_40) {
      if(SiS_Pr->SiS_XGIROM) {
         SiS_Pr->Init_P4_0E = ROMAddr[0x80];
      }
   }
}

static void
SiS_ResetVB(struct SiS_Private *SiS_Pr)
{
#ifdef CONFIG_FB_SIS_315
   unsigned char  *ROMAddr = SiS_Pr->VirtualRomBase;
   unsigned short temp;

   /* VB programming clock */
   if(SiS_Pr->SiS_UseROM) {
      if(SiS_Pr->ChipType < SIS_330) {
	 temp = ROMAddr[VB310Data_1_2_Offset] | 0x40;
	 if(SiS_Pr->SiS_ROMNew) temp = ROMAddr[0x80] | 0x40;
	 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x02,temp);
      } else if(SiS_Pr->ChipType >= SIS_661 && SiS_Pr->ChipType < XGI_20) {
	 temp = ROMAddr[0x7e] | 0x40;
	 if(SiS_Pr->SiS_ROMNew) temp = ROMAddr[0x80] | 0x40;
	 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x02,temp);
      }
   } else if(SiS_Pr->ChipType >= XGI_40) {
      temp = 0x40;
      if(SiS_Pr->SiS_XGIROM) temp |= ROMAddr[0x7e];
      /* Can we do this on any chipset? */
      SiS_SetReg(SiS_Pr->SiS_Part1Port,0x02,temp);
   }
#endif
}

/*********************************************/
/*    HELPER: SET VIDEO/CAPTURE REGISTERS    */
/*********************************************/

static void
SiS_StrangeStuff(struct SiS_Private *SiS_Pr)
{
   /* SiS65x and XGI set up some sort of "lock mode" for text
    * which locks CRT2 in some way to CRT1 timing. Disable
    * this here.
    */
#ifdef CONFIG_FB_SIS_315
   if((IS_SIS651) || (IS_SISM650) ||
      SiS_Pr->ChipType == SIS_340 ||
      SiS_Pr->ChipType == XGI_40) {
      SiS_SetReg(SiS_Pr->SiS_VidCapt, 0x3f, 0x00);   /* Fiddle with capture regs */
      SiS_SetReg(SiS_Pr->SiS_VidCapt, 0x00, 0x00);
      SiS_SetReg(SiS_Pr->SiS_VidPlay, 0x00, 0x86);   /* (BIOS does NOT unlock) */
      SiS_SetRegAND(SiS_Pr->SiS_VidPlay, 0x30, 0xfe); /* Fiddle with video regs */
      SiS_SetRegAND(SiS_Pr->SiS_VidPlay, 0x3f, 0xef);
   }
   /* !!! This does not support modes < 0x13 !!! */
#endif
}

/*********************************************/
/*     HELPER: SET AGP TIMING FOR SiS760     */
/*********************************************/

static void
SiS_Handle760(struct SiS_Private *SiS_Pr)
{
#ifdef CONFIG_FB_SIS_315
   unsigned int somebase;
   unsigned char temp1, temp2, temp3;

   if( (SiS_Pr->ChipType != SIS_760)                         ||
       ((SiS_GetReg(SiS_Pr->SiS_P3d4, 0x5c) & 0xf8) != 0x80) ||
       (!(SiS_Pr->SiS_SysFlags & SF_760LFB))                 ||
       (!(SiS_Pr->SiS_SysFlags & SF_760UMA)) )
      return;

   somebase = sisfb_read_mio_pci_word(SiS_Pr, 0x74);
   somebase &= 0xffff;

   if(somebase == 0) return;

   temp3 = SiS_GetRegByte((somebase + 0x85)) & 0xb7;

   if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x31) & 0x40) {
      temp1 = 0x21;
      temp2 = 0x03;
      temp3 |= 0x08;
   } else {
      temp1 = 0x25;
      temp2 = 0x0b;
   }

   sisfb_write_nbridge_pci_byte(SiS_Pr, 0x7e, temp1);
   sisfb_write_nbridge_pci_byte(SiS_Pr, 0x8d, temp2);

   SiS_SetRegByte((somebase + 0x85), temp3);
#endif
}

/*********************************************/
/*                 SiSSetMode()              */
/*********************************************/

bool
SiSSetMode(struct SiS_Private *SiS_Pr, unsigned short ModeNo)
{
   SISIOADDRESS BaseAddr = SiS_Pr->IOAddress;
   unsigned short RealModeNo, ModeIdIndex;
   unsigned char  backupreg = 0;
   unsigned short KeepLockReg;

   SiS_Pr->UseCustomMode = false;
   SiS_Pr->CRT1UsesCustomMode = false;

   SiS_Pr->SiS_flag_clearbuffer = 0;

   if(SiS_Pr->UseCustomMode) {
      ModeNo = 0xfe;
   } else {
      if(!(ModeNo & 0x80)) SiS_Pr->SiS_flag_clearbuffer = 1;
      ModeNo &= 0x7f;
   }

   /* Don't use FSTN mode for CRT1 */
   RealModeNo = ModeNo;
   if(ModeNo == 0x5b) ModeNo = 0x56;

   SiSInitPtr(SiS_Pr);
   SiSRegInit(SiS_Pr, BaseAddr);
   SiS_GetSysFlags(SiS_Pr);

   SiS_Pr->SiS_VGAINFO = 0x11;

   KeepLockReg = SiS_GetReg(SiS_Pr->SiS_P3c4,0x05);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x05,0x86);

   SiSInitPCIetc(SiS_Pr);
   SiSSetLVDSetc(SiS_Pr);
   SiSDetermineROMUsage(SiS_Pr);

   SiS_UnLockCRT2(SiS_Pr);

   if(!SiS_Pr->UseCustomMode) {
      if(!(SiS_SearchModeID(SiS_Pr, &ModeNo, &ModeIdIndex))) return false;
   } else {
      ModeIdIndex = 0;
   }

   SiS_GetVBType(SiS_Pr);

   /* Init/restore some VB registers */
   SiS_InitVB(SiS_Pr);
   if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
      if(SiS_Pr->ChipType >= SIS_315H) {
         SiS_ResetVB(SiS_Pr);
	 SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x32,0x10);
	 SiS_SetRegOR(SiS_Pr->SiS_Part2Port,0x00,0x0c);
         backupreg = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
      } else {
         backupreg = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35);
      }
   }

   /* Get VB information (connectors, connected devices) */
   SiS_GetVBInfo(SiS_Pr, ModeNo, ModeIdIndex, (SiS_Pr->UseCustomMode) ? 0 : 1);
   SiS_SetYPbPr(SiS_Pr);
   SiS_SetTVMode(SiS_Pr, ModeNo, ModeIdIndex);
   SiS_GetLCDResInfo(SiS_Pr, ModeNo, ModeIdIndex);
   SiS_SetLowModeTest(SiS_Pr, ModeNo);

   /* Check memory size (kernel framebuffer driver only) */
   if(!SiS_CheckMemorySize(SiS_Pr, ModeNo, ModeIdIndex)) {
      return false;
   }

   SiS_OpenCRTC(SiS_Pr);

   if(SiS_Pr->UseCustomMode) {
      SiS_Pr->CRT1UsesCustomMode = true;
      SiS_Pr->CSRClock_CRT1 = SiS_Pr->CSRClock;
      SiS_Pr->CModeFlag_CRT1 = SiS_Pr->CModeFlag;
   } else {
      SiS_Pr->CRT1UsesCustomMode = false;
   }

   /* Set mode on CRT1 */
   if( (SiS_Pr->SiS_VBInfo & (SetSimuScanMode | SetCRT2ToLCDA)) ||
       (!(SiS_Pr->SiS_VBInfo & SwitchCRT2)) ) {
      SiS_SetCRT1Group(SiS_Pr, ModeNo, ModeIdIndex);
   }

   /* Set mode on CRT2 */
   if(SiS_Pr->SiS_VBInfo & (SetSimuScanMode | SwitchCRT2 | SetCRT2ToLCDA)) {
      if( (SiS_Pr->SiS_VBType & VB_SISVB)    ||
	  (SiS_Pr->SiS_IF_DEF_LVDS     == 1) ||
	  (SiS_Pr->SiS_IF_DEF_CH70xx   != 0) ||
	  (SiS_Pr->SiS_IF_DEF_TRUMPION != 0) ) {
	 SiS_SetCRT2Group(SiS_Pr, RealModeNo);
      }
   }

   SiS_HandleCRT1(SiS_Pr);

   SiS_StrangeStuff(SiS_Pr);

   SiS_DisplayOn(SiS_Pr);
   SiS_SetRegByte(SiS_Pr->SiS_P3c6,0xFF);

#ifdef CONFIG_FB_SIS_315
   if(SiS_Pr->ChipType >= SIS_315H) {
      if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
	 if(!(SiS_IsDualEdge(SiS_Pr))) {
	    SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xfb);
	 }
      }
   }
#endif

   if(SiS_Pr->SiS_VBType & VB_SIS30xBLV) {
      if(SiS_Pr->ChipType >= SIS_315H) {
#ifdef CONFIG_FB_SIS_315
	 if(!SiS_Pr->SiS_ROMNew) {
	    if(SiS_IsVAMode(SiS_Pr)) {
	       SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x35,0x01);
	    } else {
	       SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x35,0xFE);
	    }
	 }

	 SiS_SetReg(SiS_Pr->SiS_P3d4,0x38,backupreg);

	 if((IS_SIS650) && (SiS_GetReg(SiS_Pr->SiS_P3d4,0x30) & 0xfc)) {
	    if((ModeNo == 0x03) || (ModeNo == 0x10)) {
	       SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x51,0x80);
	       SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x56,0x08);
	    }
	 }

	 if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x30) & SetCRT2ToLCD) {
	    SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x38,0xfc);
	 }
#endif
      } else if((SiS_Pr->ChipType == SIS_630) ||
	        (SiS_Pr->ChipType == SIS_730)) {
	 SiS_SetReg(SiS_Pr->SiS_P3d4,0x35,backupreg);
      }
   }

   SiS_CloseCRTC(SiS_Pr);

   SiS_Handle760(SiS_Pr);

   /* We never lock registers in XF86 */
   if(KeepLockReg != 0xA1) SiS_SetReg(SiS_Pr->SiS_P3c4,0x05,0x00);

   return true;
}

#ifndef GETBITSTR
#define GENBITSMASK(mask)   	GENMASK(1?mask,0?mask)
#define GETBITS(var,mask)   	(((var) & GENBITSMASK(mask)) >> (0?mask))
#define GETBITSTR(val,from,to)  ((GETBITS(val,from)) << (0?to))
#endif

void
SiS_CalcCRRegisters(struct SiS_Private *SiS_Pr, int depth)
{
   int x = 1; /* Fix sync */

   SiS_Pr->CCRT1CRTC[0]  =  ((SiS_Pr->CHTotal >> 3) - 5) & 0xff;		/* CR0 */
   SiS_Pr->CCRT1CRTC[1]  =  (SiS_Pr->CHDisplay >> 3) - 1;			/* CR1 */
   SiS_Pr->CCRT1CRTC[2]  =  (SiS_Pr->CHBlankStart >> 3) - 1;			/* CR2 */
   SiS_Pr->CCRT1CRTC[3]  =  (((SiS_Pr->CHBlankEnd >> 3) - 1) & 0x1F) | 0x80;	/* CR3 */
   SiS_Pr->CCRT1CRTC[4]  =  (SiS_Pr->CHSyncStart >> 3) + 3;			/* CR4 */
   SiS_Pr->CCRT1CRTC[5]  =  ((((SiS_Pr->CHBlankEnd >> 3) - 1) & 0x20) << 2) |	/* CR5 */
			    (((SiS_Pr->CHSyncEnd >> 3) + 3) & 0x1F);

   SiS_Pr->CCRT1CRTC[6]  =  (SiS_Pr->CVTotal       - 2) & 0xFF;			/* CR6 */
   SiS_Pr->CCRT1CRTC[7]  =  (((SiS_Pr->CVTotal     - 2) & 0x100) >> 8)		/* CR7 */
			  | (((SiS_Pr->CVDisplay   - 1) & 0x100) >> 7)
			  | (((SiS_Pr->CVSyncStart - x) & 0x100) >> 6)
			  | (((SiS_Pr->CVBlankStart- 1) & 0x100) >> 5)
			  | 0x10
			  | (((SiS_Pr->CVTotal     - 2) & 0x200) >> 4)
			  | (((SiS_Pr->CVDisplay   - 1) & 0x200) >> 3)
			  | (((SiS_Pr->CVSyncStart - x) & 0x200) >> 2);

   SiS_Pr->CCRT1CRTC[16] = ((((SiS_Pr->CVBlankStart - 1) & 0x200) >> 4) >> 5); 	/* CR9 */

   if(depth != 8) {
      if(SiS_Pr->CHDisplay >= 1600)      SiS_Pr->CCRT1CRTC[16] |= 0x60;		/* SRE */
      else if(SiS_Pr->CHDisplay >= 640)  SiS_Pr->CCRT1CRTC[16] |= 0x40;
   }

   SiS_Pr->CCRT1CRTC[8] =  (SiS_Pr->CVSyncStart  - x) & 0xFF;			/* CR10 */
   SiS_Pr->CCRT1CRTC[9] =  ((SiS_Pr->CVSyncEnd   - x) & 0x0F) | 0x80;		/* CR11 */
   SiS_Pr->CCRT1CRTC[10] = (SiS_Pr->CVDisplay    - 1) & 0xFF;			/* CR12 */
   SiS_Pr->CCRT1CRTC[11] = (SiS_Pr->CVBlankStart - 1) & 0xFF;			/* CR15 */
   SiS_Pr->CCRT1CRTC[12] = (SiS_Pr->CVBlankEnd   - 1) & 0xFF;			/* CR16 */

   SiS_Pr->CCRT1CRTC[13] =							/* SRA */
			GETBITSTR((SiS_Pr->CVTotal     -2), 10:10, 0:0) |
			GETBITSTR((SiS_Pr->CVDisplay   -1), 10:10, 1:1) |
			GETBITSTR((SiS_Pr->CVBlankStart-1), 10:10, 2:2) |
			GETBITSTR((SiS_Pr->CVSyncStart -x), 10:10, 3:3) |
			GETBITSTR((SiS_Pr->CVBlankEnd  -1),   8:8, 4:4) |
			GETBITSTR((SiS_Pr->CVSyncEnd     ),   4:4, 5:5) ;

   SiS_Pr->CCRT1CRTC[14] =							/* SRB */
			GETBITSTR((SiS_Pr->CHTotal      >> 3) - 5, 9:8, 1:0) |
			GETBITSTR((SiS_Pr->CHDisplay    >> 3) - 1, 9:8, 3:2) |
			GETBITSTR((SiS_Pr->CHBlankStart >> 3) - 1, 9:8, 5:4) |
			GETBITSTR((SiS_Pr->CHSyncStart  >> 3) + 3, 9:8, 7:6) ;


   SiS_Pr->CCRT1CRTC[15] =							/* SRC */
			GETBITSTR((SiS_Pr->CHBlankEnd >> 3) - 1, 7:6, 1:0) |
			GETBITSTR((SiS_Pr->CHSyncEnd  >> 3) + 3, 5:5, 2:2) ;
}

void
SiS_CalcLCDACRT1Timing(struct SiS_Private *SiS_Pr, unsigned short ModeNo,
		unsigned short ModeIdIndex)
{
   unsigned short modeflag, tempax, tempbx = 0, remaining = 0;
   unsigned short VGAHDE = SiS_Pr->SiS_VGAHDE;
   int i, j;

   /* 1:1 data: use data set by setcrt1crtc() */
   if(SiS_Pr->SiS_LCDInfo & LCDPass11) return;

   modeflag = SiS_GetModeFlag(SiS_Pr, ModeNo, ModeIdIndex);

   if(modeflag & HalfDCLK) VGAHDE >>= 1;

   SiS_Pr->CHDisplay = VGAHDE;
   SiS_Pr->CHBlankStart = VGAHDE;

   SiS_Pr->CVDisplay = SiS_Pr->SiS_VGAVDE;
   SiS_Pr->CVBlankStart = SiS_Pr->SiS_VGAVDE;

   if(SiS_Pr->ChipType < SIS_315H) {
#ifdef CONFIG_FB_SIS_300
      tempbx = SiS_Pr->SiS_VGAHT;
      if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
         tempbx = SiS_Pr->PanelHT;
      }
      if(modeflag & HalfDCLK) tempbx >>= 1;
      remaining = tempbx % 8;
#endif
   } else {
#ifdef CONFIG_FB_SIS_315
      /* OK for LCDA, LVDS */
      tempbx = SiS_Pr->PanelHT - SiS_Pr->PanelXRes;
      tempax = SiS_Pr->SiS_VGAHDE;  /* not /2 ! */
      if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
         tempax = SiS_Pr->PanelXRes;
      }
      tempbx += tempax;
      if(modeflag & HalfDCLK) tempbx -= VGAHDE;
#endif
   }
   SiS_Pr->CHTotal = SiS_Pr->CHBlankEnd = tempbx;

   if(SiS_Pr->ChipType < SIS_315H) {
#ifdef CONFIG_FB_SIS_300
      if(SiS_Pr->SiS_VGAHDE == SiS_Pr->PanelXRes) {
	 SiS_Pr->CHSyncStart = SiS_Pr->SiS_VGAHDE + ((SiS_Pr->PanelHRS + 1) & ~1);
	 SiS_Pr->CHSyncEnd = SiS_Pr->CHSyncStart + SiS_Pr->PanelHRE;
	 if(modeflag & HalfDCLK) {
	    SiS_Pr->CHSyncStart >>= 1;
	    SiS_Pr->CHSyncEnd >>= 1;
	 }
      } else if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
	 tempax = (SiS_Pr->PanelXRes - SiS_Pr->SiS_VGAHDE) >> 1;
	 tempbx = (SiS_Pr->PanelHRS + 1) & ~1;
	 if(modeflag & HalfDCLK) {
	    tempax >>= 1;
	    tempbx >>= 1;
	 }
	 SiS_Pr->CHSyncStart = (VGAHDE + tempax + tempbx + 7) & ~7;
	 tempax = SiS_Pr->PanelHRE + 7;
	 if(modeflag & HalfDCLK) tempax >>= 1;
	 SiS_Pr->CHSyncEnd = (SiS_Pr->CHSyncStart + tempax) & ~7;
      } else {
	 SiS_Pr->CHSyncStart = SiS_Pr->SiS_VGAHDE;
	 if(modeflag & HalfDCLK) {
	    SiS_Pr->CHSyncStart >>= 1;
	    tempax = ((SiS_Pr->CHTotal - SiS_Pr->CHSyncStart) / 3) << 1;
	    SiS_Pr->CHSyncEnd = SiS_Pr->CHSyncStart + tempax;
	 } else {
	    SiS_Pr->CHSyncEnd = (SiS_Pr->CHSyncStart + (SiS_Pr->CHTotal / 10) + 7) & ~7;
	    SiS_Pr->CHSyncStart += 8;
	 }
      }
#endif
   } else {
#ifdef CONFIG_FB_SIS_315
      tempax = VGAHDE;
      if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
	 tempbx = SiS_Pr->PanelXRes;
	 if(modeflag & HalfDCLK) tempbx >>= 1;
	 tempax += ((tempbx - tempax) >> 1);
      }
      tempax += SiS_Pr->PanelHRS;
      SiS_Pr->CHSyncStart = tempax;
      tempax += SiS_Pr->PanelHRE;
      SiS_Pr->CHSyncEnd = tempax;
#endif
   }

   tempbx = SiS_Pr->PanelVT - SiS_Pr->PanelYRes;
   tempax = SiS_Pr->SiS_VGAVDE;
   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
      tempax = SiS_Pr->PanelYRes;
   } else if(SiS_Pr->ChipType < SIS_315H) {
#ifdef CONFIG_FB_SIS_300
      /* Stupid hack for 640x400/320x200 */
      if(SiS_Pr->SiS_LCDResInfo == Panel_1024x768) {
	 if((tempax + tempbx) == 438) tempbx += 16;
      } else if((SiS_Pr->SiS_LCDResInfo == Panel_800x600) ||
		(SiS_Pr->SiS_LCDResInfo == Panel_1024x600)) {
	 tempax = 0;
	 tempbx = SiS_Pr->SiS_VGAVT;
      }
#endif
   }
   SiS_Pr->CVTotal = SiS_Pr->CVBlankEnd = tempbx + tempax;

   tempax = SiS_Pr->SiS_VGAVDE;
   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
      tempax += (SiS_Pr->PanelYRes - tempax) >> 1;
   }
   tempax += SiS_Pr->PanelVRS;
   SiS_Pr->CVSyncStart = tempax;
   tempax += SiS_Pr->PanelVRE;
   SiS_Pr->CVSyncEnd = tempax;
   if(SiS_Pr->ChipType < SIS_315H) {
      SiS_Pr->CVSyncStart--;
      SiS_Pr->CVSyncEnd--;
   }

   SiS_CalcCRRegisters(SiS_Pr, 8);
   SiS_Pr->CCRT1CRTC[15] &= ~0xF8;
   SiS_Pr->CCRT1CRTC[15] |= (remaining << 4);
   SiS_Pr->CCRT1CRTC[16] &= ~0xE0;

   SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x11,0x7f);

   for(i = 0, j = 0; i <= 7; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_P3d4,j,SiS_Pr->CCRT1CRTC[i]);
   }
   for(j = 0x10; i <= 10; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_P3d4,j,SiS_Pr->CCRT1CRTC[i]);
   }
   for(j = 0x15; i <= 12; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_P3d4,j,SiS_Pr->CCRT1CRTC[i]);
   }
   for(j = 0x0A; i <= 15; i++, j++) {
      SiS_SetReg(SiS_Pr->SiS_P3c4,j,SiS_Pr->CCRT1CRTC[i]);
   }

   tempax = SiS_Pr->CCRT1CRTC[16] & 0xE0;
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0E,0x1F,tempax);

   tempax = (SiS_Pr->CCRT1CRTC[16] & 0x01) << 5;
   if(modeflag & DoubleScanMode) tempax |= 0x80;
   SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x09,0x5F,tempax);

}

void
SiS_Generic_ConvertCRData(struct SiS_Private *SiS_Pr, unsigned char *crdata,
			int xres, int yres,
			struct fb_var_screeninfo *var, bool writeres
)
{
   unsigned short HRE, HBE, HRS, HDE;
   unsigned short VRE, VBE, VRS, VDE;
   unsigned char  sr_data, cr_data;
   int            B, C, D, E, F, temp;

   sr_data = crdata[14];

   /* Horizontal display enable end */
   HDE = crdata[1] | ((unsigned short)(sr_data & 0x0C) << 6);
   E = HDE + 1;

   /* Horizontal retrace (=sync) start */
   HRS = crdata[4] | ((unsigned short)(sr_data & 0xC0) << 2);
   F = HRS - E - 3;

   sr_data = crdata[15];
   cr_data = crdata[5];

   /* Horizontal blank end */
   HBE = (crdata[3] & 0x1f) |
         ((unsigned short)(cr_data & 0x80) >> 2) |
         ((unsigned short)(sr_data & 0x03) << 6);

   /* Horizontal retrace (=sync) end */
   HRE = (cr_data & 0x1f) | ((sr_data & 0x04) << 3);

   temp = HBE - ((E - 1) & 255);
   B = (temp > 0) ? temp : (temp + 256);

   temp = HRE - ((E + F + 3) & 63);
   C = (temp > 0) ? temp : (temp + 64);

   D = B - F - C;

   if(writeres) var->xres = xres = E * 8;
   var->left_margin = D * 8;
   var->right_margin = F * 8;
   var->hsync_len = C * 8;

   /* Vertical */
   sr_data = crdata[13];
   cr_data = crdata[7];

   /* Vertical display enable end */
   VDE = crdata[10] |
	 ((unsigned short)(cr_data & 0x02) << 7) |
	 ((unsigned short)(cr_data & 0x40) << 3) |
	 ((unsigned short)(sr_data & 0x02) << 9);
   E = VDE + 1;

   /* Vertical retrace (=sync) start */
   VRS = crdata[8] |
	 ((unsigned short)(cr_data & 0x04) << 6) |
	 ((unsigned short)(cr_data & 0x80) << 2) |
	 ((unsigned short)(sr_data & 0x08) << 7);
   F = VRS + 1 - E;

   /* Vertical blank end */
   VBE = crdata[12] | ((unsigned short)(sr_data & 0x10) << 4);
   temp = VBE - ((E - 1) & 511);
   B = (temp > 0) ? temp : (temp + 512);

   /* Vertical retrace (=sync) end */
   VRE = (crdata[9] & 0x0f) | ((sr_data & 0x20) >> 1);
   temp = VRE - ((E + F - 1) & 31);
   C = (temp > 0) ? temp : (temp + 32);

   D = B - F - C;

   if(writeres) var->yres = yres = E;
   var->upper_margin = D;
   var->lower_margin = F;
   var->vsync_len = C;

   if((xres == 320) && ((yres == 200) || (yres == 240))) {
	/* Terrible hack, but correct CRTC data for
	 * these modes only produces a black screen...
	 * (HRE is 0, leading into a too large C and
	 * a negative D. The CRT controller does not
	 * seem to like correcting HRE to 50)
	 */
      var->left_margin = (400 - 376);
      var->right_margin = (328 - 320);
      var->hsync_len = (376 - 328);

   }

}




