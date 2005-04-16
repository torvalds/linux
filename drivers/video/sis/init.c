/* $XFree86$ */
/* $XdotOrg$ */
/*
 * Mode initializing code (CRT1 section) for
 * for SiS 300/305/540/630/730 and
 *     SiS 315/550/650/M650/651/661FX/M661FX/740/741(GX)/M741/330/660/M660/760/M760
 * (Universal module for Linux kernel framebuffer and XFree86 4.x)
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
 * Formerly based on non-functional code-fragements for 300 series by SiS, Inc.
 * Used by permission.
 *
 * TW says: This code looks awful, I know. But please don't do anything about
 * this otherwise debugging will be hell.
 * The code is extremely fragile as regards the different chipsets, different
 * video bridges and combinations thereof. If anything is changed, extreme
 * care has to be taken that that change doesn't break it for other chipsets,
 * bridges or combinations thereof.
 * All comments in this file are by me, regardless if they are marked TW or not.
 *
 */
 
#include "init.h"

#ifdef SIS300
#include "300vtbl.h"
#endif

#ifdef SIS315H
#include "310vtbl.h"
#endif

#if defined(ALLOC_PRAGMA)
#pragma alloc_text(PAGE,SiSSetMode)
#endif

/*********************************************/
/*         POINTER INITIALIZATION            */
/*********************************************/

#if defined(SIS300) || defined(SIS315H)
static void
InitCommonPointer(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   SiS_Pr->SiS_StResInfo     = SiS_StResInfo;
   SiS_Pr->SiS_ModeResInfo   = SiS_ModeResInfo;
   SiS_Pr->SiS_StandTable    = SiS_StandTable;

   SiS_Pr->SiS_NTSCPhase     = SiS_NTSCPhase;
   SiS_Pr->SiS_PALPhase      = SiS_PALPhase;
   SiS_Pr->SiS_NTSCPhase2    = SiS_NTSCPhase2;
   SiS_Pr->SiS_PALPhase2     = SiS_PALPhase2;
   SiS_Pr->SiS_PALMPhase     = SiS_PALMPhase;
   SiS_Pr->SiS_PALNPhase     = SiS_PALNPhase;
   SiS_Pr->SiS_PALMPhase2    = SiS_PALMPhase2;
   SiS_Pr->SiS_PALNPhase2    = SiS_PALNPhase2;
   SiS_Pr->SiS_SpecialPhase  = SiS_SpecialPhase;
   SiS_Pr->SiS_SpecialPhaseM = SiS_SpecialPhaseM;
   SiS_Pr->SiS_SpecialPhaseJ = SiS_SpecialPhaseJ;

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
   SiS_Pr->SiS_LCD1280x960Data      = SiS_LCD1280x960Data;
   SiS_Pr->SiS_StLCD1400x1050Data   = SiS_StLCD1400x1050Data;
   SiS_Pr->SiS_ExtLCD1400x1050Data  = SiS_ExtLCD1400x1050Data;
   SiS_Pr->SiS_LCD1680x1050Data     = SiS_LCD1680x1050Data;
   SiS_Pr->SiS_StLCD1600x1200Data   = SiS_StLCD1600x1200Data;
   SiS_Pr->SiS_ExtLCD1600x1200Data  = SiS_ExtLCD1600x1200Data;
   SiS_Pr->SiS_NoScaleData          = SiS_NoScaleData;

   SiS_Pr->SiS_LVDS320x480Data_1   = SiS_LVDS320x480Data_1;
   SiS_Pr->SiS_LVDS800x600Data_1   = SiS_LVDS800x600Data_1;
   SiS_Pr->SiS_LVDS800x600Data_2   = SiS_LVDS800x600Data_2;
   SiS_Pr->SiS_LVDS1024x768Data_1  = SiS_LVDS1024x768Data_1;
   SiS_Pr->SiS_LVDS1024x768Data_2  = SiS_LVDS1024x768Data_2;
   SiS_Pr->SiS_LVDS1280x1024Data_1 = SiS_LVDS1280x1024Data_1;
   SiS_Pr->SiS_LVDS1280x1024Data_2 = SiS_LVDS1280x1024Data_2;
   SiS_Pr->SiS_LVDS1400x1050Data_1 = SiS_LVDS1400x1050Data_1;
   SiS_Pr->SiS_LVDS1400x1050Data_2 = SiS_LVDS1400x1050Data_2;
   SiS_Pr->SiS_LVDS1600x1200Data_1 = SiS_LVDS1600x1200Data_1;
   SiS_Pr->SiS_LVDS1600x1200Data_2 = SiS_LVDS1600x1200Data_2;
   SiS_Pr->SiS_LVDS1280x768Data_1  = SiS_LVDS1280x768Data_1;
   SiS_Pr->SiS_LVDS1280x768Data_2  = SiS_LVDS1280x768Data_2;
   SiS_Pr->SiS_LVDS1024x600Data_1  = SiS_LVDS1024x600Data_1;
   SiS_Pr->SiS_LVDS1024x600Data_2  = SiS_LVDS1024x600Data_2;
   SiS_Pr->SiS_LVDS1152x768Data_1  = SiS_LVDS1152x768Data_1;
   SiS_Pr->SiS_LVDS1152x768Data_2  = SiS_LVDS1152x768Data_2;
   SiS_Pr->SiS_LVDSXXXxXXXData_1   = SiS_LVDSXXXxXXXData_1;
   SiS_Pr->SiS_LVDS1280x960Data_1  = SiS_LVDS1280x960Data_1;
   SiS_Pr->SiS_LVDS1280x960Data_2  = SiS_LVDS1280x960Data_2;
   SiS_Pr->SiS_LVDS640x480Data_1   = SiS_LVDS640x480Data_1;
   SiS_Pr->SiS_LVDS1280x960Data_1  = SiS_LVDS1280x1024Data_1;
   SiS_Pr->SiS_LVDS1280x960Data_2  = SiS_LVDS1280x1024Data_2;
   SiS_Pr->SiS_LVDS640x480Data_1   = SiS_LVDS640x480Data_1;
   SiS_Pr->SiS_LVDS640x480Data_2   = SiS_LVDS640x480Data_2;

   SiS_Pr->SiS_LVDS848x480Data_1   = SiS_LVDS848x480Data_1;
   SiS_Pr->SiS_LVDS848x480Data_2   = SiS_LVDS848x480Data_2;
   SiS_Pr->SiS_LVDSBARCO1024Data_1 = SiS_LVDSBARCO1024Data_1;
   SiS_Pr->SiS_LVDSBARCO1024Data_2 = SiS_LVDSBARCO1024Data_2;
   SiS_Pr->SiS_LVDSBARCO1366Data_1 = SiS_LVDSBARCO1366Data_1;
   SiS_Pr->SiS_LVDSBARCO1366Data_2 = SiS_LVDSBARCO1366Data_2;

   SiS_Pr->SiS_LVDSCRT11280x768_1    = SiS_LVDSCRT11280x768_1;
   SiS_Pr->SiS_LVDSCRT11024x600_1    = SiS_LVDSCRT11024x600_1;
   SiS_Pr->SiS_LVDSCRT11152x768_1    = SiS_LVDSCRT11152x768_1;
   SiS_Pr->SiS_LVDSCRT11280x768_1_H  = SiS_LVDSCRT11280x768_1_H;
   SiS_Pr->SiS_LVDSCRT11024x600_1_H  = SiS_LVDSCRT11024x600_1_H;
   SiS_Pr->SiS_LVDSCRT11152x768_1_H  = SiS_LVDSCRT11152x768_1_H;
   SiS_Pr->SiS_LVDSCRT11280x768_2    = SiS_LVDSCRT11280x768_2;
   SiS_Pr->SiS_LVDSCRT11024x600_2    = SiS_LVDSCRT11024x600_2;
   SiS_Pr->SiS_LVDSCRT11152x768_2    = SiS_LVDSCRT11152x768_2;
   SiS_Pr->SiS_LVDSCRT11280x768_2_H  = SiS_LVDSCRT11280x768_2_H;
   SiS_Pr->SiS_LVDSCRT11024x600_2_H  = SiS_LVDSCRT11024x600_2_H;
   SiS_Pr->SiS_LVDSCRT11152x768_2_H  = SiS_LVDSCRT11152x768_2_H;
   SiS_Pr->SiS_LVDSCRT1320x480_1     = SiS_LVDSCRT1320x480_1;
   SiS_Pr->SiS_LVDSCRT1640x480_1     = SiS_LVDSCRT1640x480_1;
   SiS_Pr->SiS_LVDSCRT1640x480_1_H   = SiS_LVDSCRT1640x480_1_H;
   SiS_Pr->SiS_LVDSCRT1640x480_2     = SiS_LVDSCRT1640x480_2;
   SiS_Pr->SiS_LVDSCRT1640x480_2_H   = SiS_LVDSCRT1640x480_2_H;
   SiS_Pr->SiS_LVDSCRT1640x480_3     = SiS_LVDSCRT1640x480_3;
   SiS_Pr->SiS_LVDSCRT1640x480_3_H   = SiS_LVDSCRT1640x480_3_H;

   SiS_Pr->SiS_CHTVUNTSCData = SiS_CHTVUNTSCData;
   SiS_Pr->SiS_CHTVONTSCData = SiS_CHTVONTSCData;

   SiS_Pr->SiS_CHTVUNTSCDesData = SiS_CHTVUNTSCDesData;
   SiS_Pr->SiS_CHTVONTSCDesData = SiS_CHTVONTSCDesData;
   SiS_Pr->SiS_CHTVUPALDesData  = SiS_CHTVUPALDesData;
   SiS_Pr->SiS_CHTVOPALDesData  = SiS_CHTVOPALDesData;

   SiS_Pr->SiS_PanelMinLVDS   = Panel_800x600;    /* lowest value LVDS/LCDA */
   SiS_Pr->SiS_PanelMin301    = Panel_1024x768;   /* lowest value 301 */
}
#endif

#ifdef SIS300
static void
InitTo300Pointer(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   InitCommonPointer(SiS_Pr, HwInfo);

   SiS_Pr->SiS_SModeIDTable  = SiS300_SModeIDTable;
   SiS_Pr->SiS_VBModeIDTable = SiS300_VBModeIDTable;
   SiS_Pr->SiS_EModeIDTable  = SiS300_EModeIDTable;
   SiS_Pr->SiS_RefIndex      = SiS300_RefIndex;
   SiS_Pr->SiS_CRT1Table     = SiS300_CRT1Table;
   if(HwInfo->jChipType == SIS_300) {
      SiS_Pr->SiS_MCLKData_0    = SiS300_MCLKData_300; /* 300 */
   } else {
      SiS_Pr->SiS_MCLKData_0    = SiS300_MCLKData_630; /* 630, 730 */
   }
   SiS_Pr->SiS_VCLKData      = SiS300_VCLKData;
   SiS_Pr->SiS_VBVCLKData    = (SiS_VBVCLKDataStruct *)SiS300_VCLKData;

   SiS_Pr->SiS_SR15  = SiS300_SR15;

#ifdef LINUX_KERNEL
   SiS_Pr->pSiS_SR07 = &SiS300_SR07;
   SiS_Pr->SiS_CR40  = SiS300_CR40;
   SiS_Pr->SiS_CR49  = SiS300_CR49;
   SiS_Pr->pSiS_SR1F = &SiS300_SR1F;
   SiS_Pr->pSiS_SR21 = &SiS300_SR21;
   SiS_Pr->pSiS_SR22 = &SiS300_SR22;
   SiS_Pr->pSiS_SR23 = &SiS300_SR23;
   SiS_Pr->pSiS_SR24 = &SiS300_SR24;
   SiS_Pr->SiS_SR25  = SiS300_SR25;
   SiS_Pr->pSiS_SR31 = &SiS300_SR31;
   SiS_Pr->pSiS_SR32 = &SiS300_SR32;
   SiS_Pr->pSiS_SR33 = &SiS300_SR33;
   SiS_Pr->pSiS_CRT2Data_1_2  = &SiS300_CRT2Data_1_2;
   SiS_Pr->pSiS_CRT2Data_4_D  = &SiS300_CRT2Data_4_D;
   SiS_Pr->pSiS_CRT2Data_4_E  = &SiS300_CRT2Data_4_E;
   SiS_Pr->pSiS_CRT2Data_4_10 = &SiS300_CRT2Data_4_10;
   SiS_Pr->pSiS_RGBSenseData    = &SiS300_RGBSenseData;
   SiS_Pr->pSiS_VideoSenseData  = &SiS300_VideoSenseData;
   SiS_Pr->pSiS_YCSenseData     = &SiS300_YCSenseData;
   SiS_Pr->pSiS_RGBSenseData2   = &SiS300_RGBSenseData2;
   SiS_Pr->pSiS_VideoSenseData2 = &SiS300_VideoSenseData2;
   SiS_Pr->pSiS_YCSenseData2    = &SiS300_YCSenseData2;
#endif

   SiS_Pr->SiS_PanelDelayTbl     = SiS300_PanelDelayTbl;
   SiS_Pr->SiS_PanelDelayTblLVDS = SiS300_PanelDelayTbl;

   SiS_Pr->SiS_ExtLCD1024x768Data   = SiS300_ExtLCD1024x768Data;
   SiS_Pr->SiS_St2LCD1024x768Data   = SiS300_St2LCD1024x768Data;
   SiS_Pr->SiS_ExtLCD1280x1024Data  = SiS300_ExtLCD1280x1024Data;
   SiS_Pr->SiS_St2LCD1280x1024Data  = SiS300_St2LCD1280x1024Data;

   SiS_Pr->SiS_CRT2Part2_1024x768_1  = SiS300_CRT2Part2_1024x768_1;
   SiS_Pr->SiS_CRT2Part2_1280x1024_1 = SiS300_CRT2Part2_1280x1024_1;
   SiS_Pr->SiS_CRT2Part2_1024x768_2  = SiS300_CRT2Part2_1024x768_2;
   SiS_Pr->SiS_CRT2Part2_1280x1024_2 = SiS300_CRT2Part2_1280x1024_2;
   SiS_Pr->SiS_CRT2Part2_1024x768_3  = SiS300_CRT2Part2_1024x768_3;
   SiS_Pr->SiS_CRT2Part2_1280x1024_3 = SiS300_CRT2Part2_1280x1024_3;

   SiS_Pr->SiS_CHTVUPALData  = SiS300_CHTVUPALData;
   SiS_Pr->SiS_CHTVOPALData  = SiS300_CHTVOPALData;
   SiS_Pr->SiS_CHTVUPALMData = SiS_CHTVUNTSCData;    /* not supported on 300 series */
   SiS_Pr->SiS_CHTVOPALMData = SiS_CHTVONTSCData;    /* not supported on 300 series */
   SiS_Pr->SiS_CHTVUPALNData = SiS300_CHTVUPALData;  /* not supported on 300 series */
   SiS_Pr->SiS_CHTVOPALNData = SiS300_CHTVOPALData;  /* not supported on 300 series */
   SiS_Pr->SiS_CHTVSOPALData = SiS300_CHTVSOPALData;

   SiS_Pr->SiS_PanelType00_1 = SiS300_PanelType00_1;
   SiS_Pr->SiS_PanelType01_1 = SiS300_PanelType01_1;
   SiS_Pr->SiS_PanelType02_1 = SiS300_PanelType02_1;
   SiS_Pr->SiS_PanelType03_1 = SiS300_PanelType03_1;
   SiS_Pr->SiS_PanelType04_1 = SiS300_PanelType04_1;
   SiS_Pr->SiS_PanelType05_1 = SiS300_PanelType05_1;
   SiS_Pr->SiS_PanelType06_1 = SiS300_PanelType06_1;
   SiS_Pr->SiS_PanelType07_1 = SiS300_PanelType07_1;
   SiS_Pr->SiS_PanelType08_1 = SiS300_PanelType08_1;
   SiS_Pr->SiS_PanelType09_1 = SiS300_PanelType09_1;
   SiS_Pr->SiS_PanelType0a_1 = SiS300_PanelType0a_1;
   SiS_Pr->SiS_PanelType0b_1 = SiS300_PanelType0b_1;
   SiS_Pr->SiS_PanelType0c_1 = SiS300_PanelType0c_1;
   SiS_Pr->SiS_PanelType0d_1 = SiS300_PanelType0d_1;
   SiS_Pr->SiS_PanelType0e_1 = SiS300_PanelType0e_1;
   SiS_Pr->SiS_PanelType0f_1 = SiS300_PanelType0f_1;
   SiS_Pr->SiS_PanelType00_2 = SiS300_PanelType00_2;
   SiS_Pr->SiS_PanelType01_2 = SiS300_PanelType01_2;
   SiS_Pr->SiS_PanelType02_2 = SiS300_PanelType02_2;
   SiS_Pr->SiS_PanelType03_2 = SiS300_PanelType03_2;
   SiS_Pr->SiS_PanelType04_2 = SiS300_PanelType04_2;
   SiS_Pr->SiS_PanelType05_2 = SiS300_PanelType05_2;
   SiS_Pr->SiS_PanelType06_2 = SiS300_PanelType06_2;
   SiS_Pr->SiS_PanelType07_2 = SiS300_PanelType07_2;
   SiS_Pr->SiS_PanelType08_2 = SiS300_PanelType08_2;
   SiS_Pr->SiS_PanelType09_2 = SiS300_PanelType09_2;
   SiS_Pr->SiS_PanelType0a_2 = SiS300_PanelType0a_2;
   SiS_Pr->SiS_PanelType0b_2 = SiS300_PanelType0b_2;
   SiS_Pr->SiS_PanelType0c_2 = SiS300_PanelType0c_2;
   SiS_Pr->SiS_PanelType0d_2 = SiS300_PanelType0d_2;
   SiS_Pr->SiS_PanelType0e_2 = SiS300_PanelType0e_2;
   SiS_Pr->SiS_PanelType0f_2 = SiS300_PanelType0f_2;
   SiS_Pr->SiS_PanelTypeNS_1 = SiS300_PanelTypeNS_1;
   SiS_Pr->SiS_PanelTypeNS_2 = SiS300_PanelTypeNS_2;

   if(SiS_Pr->SiS_CustomT == CUT_BARCO1366) {
      SiS_Pr->SiS_PanelType04_1 = SiS300_PanelType04_1a;
      SiS_Pr->SiS_PanelType04_2 = SiS300_PanelType04_2a;
   }
   if(SiS_Pr->SiS_CustomT == CUT_BARCO1024) {
      SiS_Pr->SiS_PanelType04_1 = SiS300_PanelType04_1b;
      SiS_Pr->SiS_PanelType04_2 = SiS300_PanelType04_2b;
   }

   SiS_Pr->SiS_LVDSCRT1800x600_1     = SiS300_LVDSCRT1800x600_1;
   SiS_Pr->SiS_LVDSCRT1800x600_1_H   = SiS300_LVDSCRT1800x600_1_H;
   SiS_Pr->SiS_LVDSCRT1800x600_2     = SiS300_LVDSCRT1800x600_2;
   SiS_Pr->SiS_LVDSCRT1800x600_2_H   = SiS300_LVDSCRT1800x600_2_H;
   SiS_Pr->SiS_LVDSCRT11024x768_1    = SiS300_LVDSCRT11024x768_1;
   SiS_Pr->SiS_LVDSCRT11024x768_1_H  = SiS300_LVDSCRT11024x768_1_H;
   SiS_Pr->SiS_LVDSCRT11024x768_2    = SiS300_LVDSCRT11024x768_2;
   SiS_Pr->SiS_LVDSCRT11024x768_2_H  = SiS300_LVDSCRT11024x768_2_H;
   SiS_Pr->SiS_LVDSCRT11280x1024_1   = SiS300_LVDSCRT11280x1024_1;
   SiS_Pr->SiS_LVDSCRT11280x1024_1_H = SiS300_LVDSCRT11280x1024_1_H;
   SiS_Pr->SiS_LVDSCRT11280x1024_2   = SiS300_LVDSCRT11280x1024_2;
   SiS_Pr->SiS_LVDSCRT11280x1024_2_H = SiS300_LVDSCRT11280x1024_2_H;
   SiS_Pr->SiS_LVDSCRT1XXXxXXX_1     = SiS300_LVDSCRT1XXXxXXX_1;
   SiS_Pr->SiS_LVDSCRT1XXXxXXX_1_H   = SiS300_LVDSCRT1XXXxXXX_1_H;

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

#ifdef SIS315H
static void
InitTo310Pointer(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   InitCommonPointer(SiS_Pr, HwInfo);

   SiS_Pr->SiS_SModeIDTable  = SiS310_SModeIDTable;
   SiS_Pr->SiS_EModeIDTable  = SiS310_EModeIDTable;
   SiS_Pr->SiS_RefIndex      = (SiS_Ext2Struct *)SiS310_RefIndex;
   SiS_Pr->SiS_CRT1Table     = SiS310_CRT1Table;
   if(HwInfo->jChipType >= SIS_340) {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_340;  /* 340 */
   } else if(HwInfo->jChipType >= SIS_761) {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_761;  /* 761 - preliminary */
   } else if(HwInfo->jChipType >= SIS_760) {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_760;  /* 760 */
   } else if(HwInfo->jChipType >= SIS_661) {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_660;  /* 661/741 */
   } else if(HwInfo->jChipType == SIS_330) {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_330;  /* 330 */
   } else if(HwInfo->jChipType > SIS_315PRO) {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_650;  /* 550, 650, 740 */
   } else {
      SiS_Pr->SiS_MCLKData_0 = SiS310_MCLKData_0_315;  /* 315 */
   }
   if(HwInfo->jChipType >= SIS_340) {
      SiS_Pr->SiS_MCLKData_1    = SiS310_MCLKData_1_340;
   } else {
      SiS_Pr->SiS_MCLKData_1    = SiS310_MCLKData_1;
   }
   SiS_Pr->SiS_VCLKData      = SiS310_VCLKData;
   SiS_Pr->SiS_VBVCLKData    = SiS310_VBVCLKData;

   SiS_Pr->SiS_SR15  = SiS310_SR15;

#ifdef LINUX_KERNEL
   SiS_Pr->pSiS_SR07 = &SiS310_SR07;
   SiS_Pr->SiS_CR40  = SiS310_CR40;
   SiS_Pr->SiS_CR49  = SiS310_CR49;
   SiS_Pr->pSiS_SR1F = &SiS310_SR1F;
   SiS_Pr->pSiS_SR21 = &SiS310_SR21;
   SiS_Pr->pSiS_SR22 = &SiS310_SR22;
   SiS_Pr->pSiS_SR23 = &SiS310_SR23;
   SiS_Pr->pSiS_SR24 = &SiS310_SR24;
   SiS_Pr->SiS_SR25  = SiS310_SR25;
   SiS_Pr->pSiS_SR31 = &SiS310_SR31;
   SiS_Pr->pSiS_SR32 = &SiS310_SR32;
   SiS_Pr->pSiS_SR33 = &SiS310_SR33;
   SiS_Pr->pSiS_CRT2Data_1_2  = &SiS310_CRT2Data_1_2;
   SiS_Pr->pSiS_CRT2Data_4_D  = &SiS310_CRT2Data_4_D;
   SiS_Pr->pSiS_CRT2Data_4_E  = &SiS310_CRT2Data_4_E;
   SiS_Pr->pSiS_CRT2Data_4_10 = &SiS310_CRT2Data_4_10;
   SiS_Pr->pSiS_RGBSenseData    = &SiS310_RGBSenseData;
   SiS_Pr->pSiS_VideoSenseData  = &SiS310_VideoSenseData;
   SiS_Pr->pSiS_YCSenseData     = &SiS310_YCSenseData;
   SiS_Pr->pSiS_RGBSenseData2   = &SiS310_RGBSenseData2;
   SiS_Pr->pSiS_VideoSenseData2 = &SiS310_VideoSenseData2;
   SiS_Pr->pSiS_YCSenseData2    = &SiS310_YCSenseData2;
#endif

   SiS_Pr->SiS_PanelDelayTbl     = SiS310_PanelDelayTbl;
   SiS_Pr->SiS_PanelDelayTblLVDS = SiS310_PanelDelayTblLVDS;

   SiS_Pr->SiS_St2LCD1024x768Data   = SiS310_St2LCD1024x768Data;
   SiS_Pr->SiS_ExtLCD1024x768Data   = SiS310_ExtLCD1024x768Data;
   SiS_Pr->SiS_St2LCD1280x1024Data  = SiS310_St2LCD1280x1024Data;
   SiS_Pr->SiS_ExtLCD1280x1024Data  = SiS310_ExtLCD1280x1024Data;

   SiS_Pr->SiS_CRT2Part2_1024x768_1  = SiS310_CRT2Part2_1024x768_1;

   SiS_Pr->SiS_PanelType00_1 = SiS310_PanelType00_1;
   SiS_Pr->SiS_PanelType01_1 = SiS310_PanelType01_1;
   SiS_Pr->SiS_PanelType02_1 = SiS310_PanelType02_1;
   SiS_Pr->SiS_PanelType03_1 = SiS310_PanelType03_1;
   SiS_Pr->SiS_PanelType04_1 = SiS310_PanelType04_1;
   SiS_Pr->SiS_PanelType05_1 = SiS310_PanelType05_1;
   SiS_Pr->SiS_PanelType06_1 = SiS310_PanelType06_1;
   SiS_Pr->SiS_PanelType07_1 = SiS310_PanelType07_1;
   SiS_Pr->SiS_PanelType08_1 = SiS310_PanelType08_1;
   SiS_Pr->SiS_PanelType09_1 = SiS310_PanelType09_1;
   SiS_Pr->SiS_PanelType0a_1 = SiS310_PanelType0a_1;
   SiS_Pr->SiS_PanelType0b_1 = SiS310_PanelType0b_1;
   SiS_Pr->SiS_PanelType0c_1 = SiS310_PanelType0c_1;
   SiS_Pr->SiS_PanelType0d_1 = SiS310_PanelType0d_1;
   SiS_Pr->SiS_PanelType0e_1 = SiS310_PanelType0e_1;
   SiS_Pr->SiS_PanelType0f_1 = SiS310_PanelType0f_1;
   SiS_Pr->SiS_PanelType00_2 = SiS310_PanelType00_2;
   SiS_Pr->SiS_PanelType01_2 = SiS310_PanelType01_2;
   SiS_Pr->SiS_PanelType02_2 = SiS310_PanelType02_2;
   SiS_Pr->SiS_PanelType03_2 = SiS310_PanelType03_2;
   SiS_Pr->SiS_PanelType04_2 = SiS310_PanelType04_2;
   SiS_Pr->SiS_PanelType05_2 = SiS310_PanelType05_2;
   SiS_Pr->SiS_PanelType06_2 = SiS310_PanelType06_2;
   SiS_Pr->SiS_PanelType07_2 = SiS310_PanelType07_2;
   SiS_Pr->SiS_PanelType08_2 = SiS310_PanelType08_2;
   SiS_Pr->SiS_PanelType09_2 = SiS310_PanelType09_2;
   SiS_Pr->SiS_PanelType0a_2 = SiS310_PanelType0a_2;
   SiS_Pr->SiS_PanelType0b_2 = SiS310_PanelType0b_2;
   SiS_Pr->SiS_PanelType0c_2 = SiS310_PanelType0c_2;
   SiS_Pr->SiS_PanelType0d_2 = SiS310_PanelType0d_2;
   SiS_Pr->SiS_PanelType0e_2 = SiS310_PanelType0e_2;
   SiS_Pr->SiS_PanelType0f_2 = SiS310_PanelType0f_2;
   SiS_Pr->SiS_PanelTypeNS_1 = SiS310_PanelTypeNS_1;
   SiS_Pr->SiS_PanelTypeNS_2 = SiS310_PanelTypeNS_2;

   SiS_Pr->SiS_CHTVUPALData  = SiS310_CHTVUPALData;
   SiS_Pr->SiS_CHTVOPALData  = SiS310_CHTVOPALData;
   SiS_Pr->SiS_CHTVUPALMData = SiS310_CHTVUPALMData;
   SiS_Pr->SiS_CHTVOPALMData = SiS310_CHTVOPALMData;
   SiS_Pr->SiS_CHTVUPALNData = SiS310_CHTVUPALNData;
   SiS_Pr->SiS_CHTVOPALNData = SiS310_CHTVOPALNData;
   SiS_Pr->SiS_CHTVSOPALData = SiS310_CHTVSOPALData;

   SiS_Pr->SiS_LVDSCRT1800x600_1     = SiS310_LVDSCRT1800x600_1;
   SiS_Pr->SiS_LVDSCRT11024x768_1    = SiS310_LVDSCRT11024x768_1;
   SiS_Pr->SiS_LVDSCRT11280x1024_1   = SiS310_LVDSCRT11280x1024_1;
   SiS_Pr->SiS_LVDSCRT11400x1050_1   = SiS310_LVDSCRT11400x1050_1;
   SiS_Pr->SiS_LVDSCRT11600x1200_1   = SiS310_LVDSCRT11600x1200_1;
   SiS_Pr->SiS_LVDSCRT1800x600_1_H   = SiS310_LVDSCRT1800x600_1_H;
   SiS_Pr->SiS_LVDSCRT11024x768_1_H  = SiS310_LVDSCRT11024x768_1_H;
   SiS_Pr->SiS_LVDSCRT11280x1024_1_H = SiS310_LVDSCRT11280x1024_1_H;
   SiS_Pr->SiS_LVDSCRT11400x1050_1_H = SiS310_LVDSCRT11400x1050_1_H;
   SiS_Pr->SiS_LVDSCRT11600x1200_1_H = SiS310_LVDSCRT11600x1200_1_H;
   SiS_Pr->SiS_LVDSCRT1800x600_2     = SiS310_LVDSCRT1800x600_2;
   SiS_Pr->SiS_LVDSCRT11024x768_2    = SiS310_LVDSCRT11024x768_2;
   SiS_Pr->SiS_LVDSCRT11280x1024_2   = SiS310_LVDSCRT11280x1024_2;
   SiS_Pr->SiS_LVDSCRT11400x1050_2   = SiS310_LVDSCRT11400x1050_2;
   SiS_Pr->SiS_LVDSCRT11600x1200_2   = SiS310_LVDSCRT11600x1200_2;
   SiS_Pr->SiS_LVDSCRT1800x600_2_H   = SiS310_LVDSCRT1800x600_2_H;
   SiS_Pr->SiS_LVDSCRT11024x768_2_H  = SiS310_LVDSCRT11024x768_2_H;
   SiS_Pr->SiS_LVDSCRT11280x1024_2_H = SiS310_LVDSCRT11280x1024_2_H;
   SiS_Pr->SiS_LVDSCRT11400x1050_2_H = SiS310_LVDSCRT11400x1050_2_H;
   SiS_Pr->SiS_LVDSCRT11600x1200_2_H = SiS310_LVDSCRT11600x1200_2_H;
   SiS_Pr->SiS_LVDSCRT1XXXxXXX_1     = SiS310_LVDSCRT1XXXxXXX_1;
   SiS_Pr->SiS_LVDSCRT1XXXxXXX_1_H   = SiS310_LVDSCRT1XXXxXXX_1_H;
   SiS_Pr->SiS_CHTVCRT1UNTSC         = SiS310_CHTVCRT1UNTSC;
   SiS_Pr->SiS_CHTVCRT1ONTSC         = SiS310_CHTVCRT1ONTSC;
   SiS_Pr->SiS_CHTVCRT1UPAL          = SiS310_CHTVCRT1UPAL;
   SiS_Pr->SiS_CHTVCRT1OPAL          = SiS310_CHTVCRT1OPAL;
   SiS_Pr->SiS_CHTVCRT1SOPAL         = SiS310_CHTVCRT1OPAL;

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

static void
SiSInitPtr(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   switch(HwInfo->jChipType) {
#ifdef SIS315H
   case SIS_315H:
   case SIS_315:
   case SIS_315PRO:
   case SIS_550:
   case SIS_650:
   case SIS_740:
   case SIS_330:
   case SIS_661:
   case SIS_741:
   case SIS_660:
   case SIS_760:
   case SIS_761:
   case SIS_340:
      InitTo310Pointer(SiS_Pr, HwInfo);
      break;
#endif
#ifdef SIS300
   case SIS_300:
   case SIS_540:
   case SIS_630:
   case SIS_730:
      InitTo300Pointer(SiS_Pr, HwInfo);
      break;
#endif
   default:
      break;
   }
}

/*********************************************/
/*            HELPER: Get ModeID             */
/*********************************************/

#ifdef LINUX_XF86
USHORT
SiS_GetModeID(int VGAEngine, ULONG VBFlags, int HDisplay, int VDisplay,
              int Depth, BOOLEAN FSTN, int LCDwidth, int LCDheight)
{
   USHORT ModeIndex = 0;

   switch(HDisplay)
   {
     case 320:
     	  if(VDisplay == 200)     ModeIndex = ModeIndex_320x200[Depth];
	  else if(VDisplay == 240) {
	     if(FSTN) ModeIndex = ModeIndex_320x240_FSTN[Depth];
	     else     ModeIndex = ModeIndex_320x240[Depth];
          }
          break;
     case 400:
          if((!(VBFlags & CRT1_LCDA)) || ((LCDwidth >= 800) && (LCDwidth >= 600))) {
             if(VDisplay == 300) ModeIndex = ModeIndex_400x300[Depth];
	  }
          break;
     case 512:
          if((!(VBFlags & CRT1_LCDA)) || ((LCDwidth >= 1024) && (LCDwidth >= 768))) {
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
	     if(VDisplay == 600)   ModeIndex = ModeIndex_1024x600[Depth];
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
          if(VDisplay == 1440)    ModeIndex = ModeIndex_1920x1440[Depth];
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

   return(ModeIndex);
}
#endif

USHORT
SiS_GetModeID_LCD(int VGAEngine, ULONG VBFlags, int HDisplay, int VDisplay,
                  int Depth, BOOLEAN FSTN, USHORT CustomT, int LCDwidth, int LCDheight)
{
   USHORT ModeIndex = 0;

   if(VBFlags & (VB_LVDS | VB_30xBDH)) {

      switch(HDisplay)
      {
	case 320:
	     if(CustomT != CUT_PANEL848) {
     	  	if(VDisplay == 200) ModeIndex = ModeIndex_320x200[Depth];
	  	else if(VDisplay == 240) {
		   if(!FSTN) ModeIndex = ModeIndex_320x240[Depth];
          	   else if(VGAEngine == SIS_315_VGA) {
                      ModeIndex = ModeIndex_320x240_FSTN[Depth];
		   }
		}
	     }
             break;
     	case 400:
	     if(CustomT != CUT_PANEL848) {
	        if(!((VGAEngine == SIS_300_VGA) && (VBFlags & VB_TRUMPION))) {
          	   if(VDisplay == 300) ModeIndex = ModeIndex_400x300[Depth];
		}
	     }
             break;
	case 512:
	     if(CustomT != CUT_PANEL848) {
	        if(!((VGAEngine == SIS_300_VGA) && (VBFlags & VB_TRUMPION))) {
		   if(LCDwidth >= 1024 && LCDwidth != 1152 && LCDheight >= 768) {
		      if(VDisplay == 384) {
		         ModeIndex = ModeIndex_512x384[Depth];
		      }
		   }
		}
	     }
	     break;
	case 640:
	     if(VDisplay == 480)            ModeIndex = ModeIndex_640x480[Depth];
	     else if(VDisplay == 400) {
	        if(CustomT != CUT_PANEL848) ModeIndex = ModeIndex_640x400[Depth];
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

   } else if(VBFlags & VB_SISBRIDGE) {

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
	     case 960:
	        ModeIndex = ModeIndex_1280x960[Depth];
		break;
	     case 1024:
	        ModeIndex = ModeIndex_1280x1024[Depth];
		break;
	     }
	     break;
	case 1360:
	     if(VGAEngine == SIS_315_VGA) {
	        if(VDisplay == 768) ModeIndex = ModeIndex_1360x768[Depth];
	     }
	     break;
	case 1400:
	     if(VGAEngine == SIS_315_VGA) {
	        if(VBFlags & (VB_301C | VB_302LV | VB_302ELV)) {
		   if(VDisplay == 1050) ModeIndex = ModeIndex_1400x1050[Depth];
		}
	     }
	     break;
	case 1600:
	     if(VGAEngine == SIS_315_VGA) {
	        if(VBFlags & (VB_301C | VB_302LV | VB_302ELV)) {
	           if(VDisplay == 1200) ModeIndex = ModeIndex_1600x1200[Depth];
		}
	     }
	     break;
#ifndef VB_FORBID_CRT2LCD_OVER_1600
	case 1680:
	     if(VGAEngine == SIS_315_VGA) {
	        if(VBFlags & (VB_301C | VB_302LV | VB_302ELV)) {
	           if(VDisplay == 1050) ModeIndex = ModeIndex_1680x1050[Depth];
		}
	     }
	     break;
#endif
      }
   }

   return ModeIndex;
}

USHORT
SiS_GetModeID_TV(int VGAEngine, ULONG VBFlags, int HDisplay, int VDisplay, int Depth)
{
   USHORT ModeIndex = 0;

   if(VBFlags & VB_CHRONTEL) {

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

   } else if(VBFlags & VB_SISTVBRIDGE) {

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
	        if((VBFlags & TV_HIVISION) || ((VBFlags & TV_YPBPR) && (VBFlags & TV_YPBPR1080I))) {
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
		if(VBFlags & (VB_301B|VB_301C|VB_302B|VB_301LV|VB_302LV|VB_302ELV)) {
		   ModeIndex = ModeIndex_1024x768[Depth];
		}
	     } else if(VDisplay == 576) {
	        if((VBFlags & TV_HIVISION) || ((VBFlags & TV_YPBPR) && (VBFlags & TV_YPBPR1080I))) {
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

USHORT
SiS_GetModeID_VGA2(int VGAEngine, ULONG VBFlags, int HDisplay, int VDisplay, int Depth)
{
   USHORT ModeIndex = 0;

   if(!(VBFlags & (VB_301|VB_301B|VB_301C|VB_302B))) return 0;

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
		if(VDisplay == 384) ModeIndex = ModeIndex_512x384[Depth];
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
		if(VDisplay == 768)      ModeIndex = ModeIndex_1024x768[Depth];
		else if(VDisplay == 576) ModeIndex = ModeIndex_1024x576[Depth];
		break;
	case 1152:
	        if(VDisplay == 864)    ModeIndex = ModeIndex_1152x864[Depth];
		else if(VGAEngine == SIS_300_VGA) {
		   if(VDisplay == 768) ModeIndex = ModeIndex_1152x768[Depth];
		}
		break;
	case 1280:
	        if(VDisplay == 768) {
		   if(VGAEngine == SIS_300_VGA) {
		      ModeIndex = ModeIndex_300_1280x768[Depth];
		   } else {
		      ModeIndex = ModeIndex_310_1280x768[Depth];
		   }
		} else if(VDisplay == 1024) ModeIndex = ModeIndex_1280x1024[Depth];
		else if(VDisplay == 720)    ModeIndex = ModeIndex_1280x720[Depth];
		else if(VDisplay == 800)    ModeIndex = ModeIndex_1280x800[Depth];
		else if(VDisplay == 960)    ModeIndex = ModeIndex_1280x960[Depth];
		break;
        case 1360:
	        if(VDisplay == 768) ModeIndex = ModeIndex_1360x768[Depth];
                break;
        case 1400:
		if(VGAEngine == SIS_315_VGA) {
	           if(VDisplay == 1050) ModeIndex = ModeIndex_1400x1050[Depth];
		}
		break;
	case 1600:
		if(VGAEngine == SIS_315_VGA) {
		   if(VBFlags & (VB_301B|VB_301C|VB_302B)) {
	              if(VDisplay == 1200) ModeIndex = ModeIndex_1600x1200[Depth];
		   }
		}
		break;
	case 1680:
		if(VGAEngine == SIS_315_VGA) {
		   if(VBFlags & (VB_301B|VB_301C|VB_302B)) {
	              if(VDisplay == 1050) ModeIndex = ModeIndex_1680x1050[Depth];
		   }
		}
		break;
   }

   return ModeIndex;
}


/*********************************************/
/*          HELPER: SetReg, GetReg           */
/*********************************************/

void
SiS_SetReg(SISIOADDRESS port, USHORT index, USHORT data)
{
   OutPortByte(port,index);
   OutPortByte(port + 1,data);
}

void
SiS_SetRegByte(SISIOADDRESS port, USHORT data)
{
   OutPortByte(port,data);
}

void
SiS_SetRegShort(SISIOADDRESS port, USHORT data)
{
   OutPortWord(port,data);
}

void
SiS_SetRegLong(SISIOADDRESS port, ULONG data)
{
   OutPortLong(port,data);
}

UCHAR
SiS_GetReg(SISIOADDRESS port, USHORT index)
{
   OutPortByte(port,index);
   return(InPortByte(port + 1));
}

UCHAR
SiS_GetRegByte(SISIOADDRESS port)
{
   return(InPortByte(port));
}

USHORT
SiS_GetRegShort(SISIOADDRESS port)
{
   return(InPortWord(port));
}

ULONG
SiS_GetRegLong(SISIOADDRESS port)
{
   return(InPortLong(port));
}

void
SiS_SetRegANDOR(SISIOADDRESS Port,USHORT Index,USHORT DataAND,USHORT DataOR)
{
  USHORT temp;

  temp = SiS_GetReg(Port,Index);
  temp = (temp & (DataAND)) | DataOR;
  SiS_SetReg(Port,Index,temp);
}

void
SiS_SetRegAND(SISIOADDRESS Port,USHORT Index,USHORT DataAND)
{
  USHORT temp;

  temp = SiS_GetReg(Port,Index);
  temp &= DataAND;
  SiS_SetReg(Port,Index,temp);
}

void
SiS_SetRegOR(SISIOADDRESS Port,USHORT Index,USHORT DataOR)
{
  USHORT temp;

  temp = SiS_GetReg(Port,Index);
  temp |= DataOR;
  SiS_SetReg(Port,Index,temp);
}

/*********************************************/
/*      HELPER: DisplayOn, DisplayOff        */
/*********************************************/

void
SiS_DisplayOn(SiS_Private *SiS_Pr)
{
   SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x01,0xDF);
}

void
SiS_DisplayOff(SiS_Private *SiS_Pr)
{
   SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x01,0x20);
}


/*********************************************/
/*        HELPER: Init Port Addresses        */
/*********************************************/

void
SiSRegInit(SiS_Private *SiS_Pr, SISIOADDRESS BaseAddr)
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
   SiS_Pr->SiS_P3cd = BaseAddr + 0x1d;
   SiS_Pr->SiS_P3da = BaseAddr + 0x2a;
   SiS_Pr->SiS_Part1Port = BaseAddr + SIS_CRT2_PORT_04;     /* Digital video interface registers (LCD) */
   SiS_Pr->SiS_Part2Port = BaseAddr + SIS_CRT2_PORT_10;     /* 301 TV Encoder registers */
   SiS_Pr->SiS_Part3Port = BaseAddr + SIS_CRT2_PORT_12;     /* 301 Macrovision registers */
   SiS_Pr->SiS_Part4Port = BaseAddr + SIS_CRT2_PORT_14;     /* 301 VGA2 (and LCD) registers */
   SiS_Pr->SiS_Part5Port = BaseAddr + SIS_CRT2_PORT_14 + 2; /* 301 palette address port registers */
   SiS_Pr->SiS_DDC_Port = BaseAddr + 0x14;                  /* DDC Port ( = P3C4, SR11/0A) */
   SiS_Pr->SiS_VidCapt = BaseAddr + SIS_VIDEO_CAPTURE;
   SiS_Pr->SiS_VidPlay = BaseAddr + SIS_VIDEO_PLAYBACK;
}

/*********************************************/
/*             HELPER: GetSysFlags           */
/*********************************************/

static void
SiS_GetSysFlags(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   unsigned char cr5f, temp1, temp2;

   /* 661 and newer: NEVER write non-zero to SR11[7:4] */
   /* (SR11 is used for DDC and in enable/disablebridge) */
   SiS_Pr->SiS_SensibleSR11 = FALSE;
   SiS_Pr->SiS_MyCR63 = 0x63;
   if(HwInfo->jChipType >= SIS_330) {
      SiS_Pr->SiS_MyCR63 = 0x53;
      if(HwInfo->jChipType >= SIS_661) {
         SiS_Pr->SiS_SensibleSR11 = TRUE;
      }
   }

   /* You should use the macros, not these flags directly */

   SiS_Pr->SiS_SysFlags = 0;
   if(HwInfo->jChipType == SIS_650) {
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
	       SiS_Pr->SiS_SysFlags |= SF_IsM650;  break;
	    case 0xa0:
	    case 0xb0:
	    case 0xe0:
	       SiS_Pr->SiS_SysFlags |= SF_Is651;   break;
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
	       SiS_Pr->SiS_SysFlags |= SF_Is652;  break;
	    default:
	       SiS_Pr->SiS_SysFlags |= SF_IsM650; break;
	 }
      }
   }
   if(HwInfo->jChipType == SIS_760) {
      temp1 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x78);
      if(temp1 & 0x30) SiS_Pr->SiS_SysFlags |= SF_760LFB;
   }
}

/*********************************************/
/*         HELPER: Init PCI & Engines        */
/*********************************************/

static void
SiSInitPCIetc(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   switch(HwInfo->jChipType) {
   case SIS_300:
   case SIS_540:
   case SIS_630:
   case SIS_730:
      /* Set - PCI LINEAR ADDRESSING ENABLE (0x80)
       *     - RELOCATED VGA IO  (0x20)
       *     - MMIO ENABLE (0x1)
       */
      SiS_SetReg(SiS_Pr->SiS_P3c4,0x20,0xa1);
      /*  - Enable 2D (0x40)
       *  - Enable 3D (0x02)
       *  - Enable 3D Vertex command fetch (0x10) ?
       *  - Enable 3D command parser (0x08) ?
       */
      SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x5A);
      break;
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
      SiS_SetReg(SiS_Pr->SiS_P3c4,0x20,0xa1);
      /*  - Enable 2D (0x40)
       *  - Enable 3D (0x02)
       *  - Enable 3D vertex command fetch (0x10)
       *  - Enable 3D command parser (0x08)
       *  - Enable 3D G/L transformation engine (0x80)
       */
      SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0xDA);
      break;
   case SIS_550:
      SiS_SetReg(SiS_Pr->SiS_P3c4,0x20,0xa1);
      /* No 3D engine ! */
      /*  - Enable 2D (0x40)
       */
      SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x1E,0x40);
   }
}

/*********************************************/
/*             HELPER: SetLVDSetc            */
/*********************************************/

void
SiSSetLVDSetc(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   USHORT temp;

   SiS_Pr->SiS_IF_DEF_LVDS = 0;
   SiS_Pr->SiS_IF_DEF_TRUMPION = 0;
   SiS_Pr->SiS_IF_DEF_CH70xx = 0;
   SiS_Pr->SiS_IF_DEF_DSTN = 0;
   SiS_Pr->SiS_IF_DEF_FSTN = 0;
   SiS_Pr->SiS_IF_DEF_CONEX = 0;

   SiS_Pr->SiS_ChrontelInit = 0;

   /* Check for SiS30x first */
   temp = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x00);
   if((temp == 1) || (temp == 2)) return;

   switch(HwInfo->jChipType) {
#ifdef SIS300
   case SIS_540:
   case SIS_630:
   case SIS_730:
      	temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x37);
      	temp = (temp & 0x0E) >> 1;
      	if((temp >= 2) && (temp <= 5)) 	SiS_Pr->SiS_IF_DEF_LVDS = 1;
      	if(temp == 3)   		SiS_Pr->SiS_IF_DEF_TRUMPION = 1;
      	if((temp == 4) || (temp == 5)) {
		/* Save power status (and error check) - UNUSED */
		SiS_Pr->SiS_Backup70xx = SiS_GetCH700x(SiS_Pr, 0x0e);
		SiS_Pr->SiS_IF_DEF_CH70xx = 1;
        }
	break;
#endif
#ifdef SIS315H
   case SIS_550:
   case SIS_650:
   case SIS_740:
   case SIS_330:
        temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x37);
      	temp = (temp & 0x0E) >> 1;
      	if((temp >= 2) && (temp <= 3)) 	SiS_Pr->SiS_IF_DEF_LVDS = 1;
      	if(temp == 3)  			SiS_Pr->SiS_IF_DEF_CH70xx = 2;
        break;
   case SIS_661:
   case SIS_741:
   case SIS_660:
   case SIS_760:
   case SIS_761:
   case SIS_340:
        temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
      	temp = (temp & 0xe0) >> 5;
      	if((temp >= 2) && (temp <= 3)) 	SiS_Pr->SiS_IF_DEF_LVDS = 1;
      	if(temp == 3)  			SiS_Pr->SiS_IF_DEF_CH70xx = 2;
	if(temp == 4)  			SiS_Pr->SiS_IF_DEF_CONEX = 1;  /* Not yet supported */
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
SiS_SetEnableDstn(SiS_Private *SiS_Pr, int enable)
{
   SiS_Pr->SiS_IF_DEF_DSTN = enable ? 1 : 0;
}

void
SiS_SetEnableFstn(SiS_Private *SiS_Pr, int enable)
{
   SiS_Pr->SiS_IF_DEF_FSTN = enable ? 1 : 0;
}

/*********************************************/
/*        HELPER: Determine ROM usage        */
/*********************************************/

BOOLEAN
SiSDetermineROMLayout661(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   UCHAR  *ROMAddr  = HwInfo->pjVirtualRomBase;
   USHORT romversoffs, romvmaj = 1, romvmin = 0;

   if(HwInfo->jChipType >= SIS_761) {
      /* I very much assume 761 and 340 will use new layout */
      return TRUE;
   } else if(HwInfo->jChipType >= SIS_661) {
      if((ROMAddr[0x1a] == 'N') &&
         (ROMAddr[0x1b] == 'e') &&
         (ROMAddr[0x1c] == 'w') &&
         (ROMAddr[0x1d] == 'V')) {
	 return TRUE;
      }
      romversoffs = ROMAddr[0x16] | (ROMAddr[0x17] << 8);
      if(romversoffs) {
	 if((ROMAddr[romversoffs+1] == '.') || (ROMAddr[romversoffs+4] == '.')) {
	    romvmaj = ROMAddr[romversoffs] - '0';
	    romvmin = ((ROMAddr[romversoffs+2] -'0') * 10) + (ROMAddr[romversoffs+3] - '0');
	 }
      }
      if((romvmaj != 0) || (romvmin >= 92)) {
	 return TRUE;
      }
   } else if(IS_SIS650740) {
      if((ROMAddr[0x1a] == 'N') &&
         (ROMAddr[0x1b] == 'e') &&
         (ROMAddr[0x1c] == 'w') &&
         (ROMAddr[0x1d] == 'V')) {
	 return TRUE;
      }
   }
   return FALSE;
}

static void
SiSDetermineROMUsage(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   UCHAR  *ROMAddr  = HwInfo->pjVirtualRomBase;
   USHORT romptr = 0;

   SiS_Pr->SiS_UseROM = FALSE;
   SiS_Pr->SiS_ROMNew = FALSE;

   if((ROMAddr) && (HwInfo->UseROM)) {
      if(HwInfo->jChipType == SIS_300) {
         /* 300: We check if the code starts below 0x220 by
	  * checking the jmp instruction at the beginning
	  * of the BIOS image.
	  */
	 if((ROMAddr[3] == 0xe9) && ((ROMAddr[5] << 8) | ROMAddr[4]) > 0x21a)
	    SiS_Pr->SiS_UseROM = TRUE;
      } else if(HwInfo->jChipType < SIS_315H) {
	 /* Sony's VAIO BIOS 1.09 follows the standard, so perhaps
	  * the others do as well
	  */
	 SiS_Pr->SiS_UseROM = TRUE;
      } else {
         /* 315/330 series stick to the standard(s) */
	 SiS_Pr->SiS_UseROM = TRUE;
	 if((SiS_Pr->SiS_ROMNew = SiSDetermineROMLayout661(SiS_Pr, HwInfo))) {
	    SiS_Pr->SiS_EMIOffset = 14;
	    SiS_Pr->SiS661LCD2TableSize = 36;
	    /* Find out about LCD data table entry size */
	    if((romptr = SISGETROMW(0x0102))) {
	       if(ROMAddr[romptr + (32 * 16)] == 0xff)
	          SiS_Pr->SiS661LCD2TableSize = 32;
	       else if(ROMAddr[romptr + (34 * 16)] == 0xff)
	          SiS_Pr->SiS661LCD2TableSize = 34;
	       else if(ROMAddr[romptr + (36 * 16)] == 0xff)	   /* 0.94 */
	          SiS_Pr->SiS661LCD2TableSize = 36;
	       else if( (ROMAddr[romptr + (38 * 16)] == 0xff) ||   /* 2.00.00 - 2.02.00 */
	       	 	(ROMAddr[0x6F] & 0x01) ) {		   /* 2.03.00+ */
		  SiS_Pr->SiS661LCD2TableSize = 38;
		  SiS_Pr->SiS_EMIOffset = 16;
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
SiS_SetSegRegLower(SiS_Private *SiS_Pr, USHORT value)
{
   USHORT temp;

   value &= 0x00ff;
   temp = SiS_GetRegByte(SiS_Pr->SiS_P3cb) & 0xf0;
   temp |= (value >> 4);
   SiS_SetRegByte(SiS_Pr->SiS_P3cb, temp);
   temp = SiS_GetRegByte(SiS_Pr->SiS_P3cd) & 0xf0;
   temp |= (value & 0x0f);
   SiS_SetRegByte(SiS_Pr->SiS_P3cd, temp);
}

static void
SiS_SetSegRegUpper(SiS_Private *SiS_Pr, USHORT value)
{
   USHORT temp;

   value &= 0x00ff;
   temp = SiS_GetRegByte(SiS_Pr->SiS_P3cb) & 0x0f;
   temp |= (value & 0xf0);
   SiS_SetRegByte(SiS_Pr->SiS_P3cb, temp);
   temp = SiS_GetRegByte(SiS_Pr->SiS_P3cd) & 0x0f;
   temp |= (value << 4);
   SiS_SetRegByte(SiS_Pr->SiS_P3cd, temp);
}

static void
SiS_SetSegmentReg(SiS_Private *SiS_Pr, USHORT value)
{
   SiS_SetSegRegLower(SiS_Pr, value);
   SiS_SetSegRegUpper(SiS_Pr, value);
}

static void
SiS_ResetSegmentReg(SiS_Private *SiS_Pr)
{
   SiS_SetSegmentReg(SiS_Pr, 0);
}

static void
SiS_SetSegmentRegOver(SiS_Private *SiS_Pr, USHORT value)
{
   USHORT temp = value >> 8;

   temp &= 0x07;
   temp |= (temp << 4);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x1d,temp);
   SiS_SetSegmentReg(SiS_Pr, value);
}

static void
SiS_ResetSegmentRegOver(SiS_Private *SiS_Pr)
{
   SiS_SetSegmentRegOver(SiS_Pr, 0);
}

static void
SiS_ResetSegmentRegisters(SiS_Private *SiS_Pr,PSIS_HW_INFO HwInfo)
{
   if((IS_SIS65x) || (HwInfo->jChipType >= SIS_661)) {
      SiS_ResetSegmentReg(SiS_Pr);
      SiS_ResetSegmentRegOver(SiS_Pr);
   }
}

/*********************************************/
/*             HELPER: GetVBType             */
/*********************************************/

void
SiS_GetVBType(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  USHORT flag=0, rev=0, nolcd=0, p4_0f, p4_25, p4_27;

  SiS_Pr->SiS_VBType = 0;

  if((SiS_Pr->SiS_IF_DEF_LVDS) || (SiS_Pr->SiS_IF_DEF_CONEX))
     return;

  flag = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x00);

  if(flag > 3) return;

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
	else 	 	 SiS_Pr->SiS_VBType = VB_SIS301C;  /* VB_SIS302ELV; */
     } else if(rev >= 0xD0) {
	SiS_Pr->SiS_VBType = VB_SIS301LV;
     }
  }
  if(SiS_Pr->SiS_VBType & (VB_301C | VB_301LV | VB_302LV | VB_302ELV)) {
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

#ifdef LINUX_KERNEL
static BOOLEAN
SiS_CheckMemorySize(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
                    USHORT ModeNo, USHORT ModeIdIndex)
{
  USHORT AdapterMemSize = HwInfo->ulVideoMemorySize / (1024*1024);
  USHORT memorysize,modeflag;

  if(SiS_Pr->UseCustomMode) {
     modeflag = SiS_Pr->CModeFlag;
  } else {
     if(ModeNo <= 0x13) {
        modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     } else {
        modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     }
  }

  memorysize = modeflag & MemoryInfoFlag;
  memorysize >>= MemorySizeShift;		/* Get required memory size */
  memorysize++;

  if(AdapterMemSize < memorysize) return FALSE;
  return TRUE;
}
#endif

/*********************************************/
/*           HELPER: Get DRAM type           */
/*********************************************/

#ifdef SIS315H
static UCHAR
SiS_Get310DRAMType(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   UCHAR data, temp;

   if((*SiS_Pr->pSiS_SoftSetting) & SoftDRAMType) {
     data = (*SiS_Pr->pSiS_SoftSetting) & 0x03;
   } else {
     if(HwInfo->jChipType >= SIS_340) {
        /* TODO */
	data = 0;
     } if(HwInfo->jChipType >= SIS_661) {
        data = SiS_GetReg(SiS_Pr->SiS_P3d4,0x78) & 0x07;
	if(SiS_Pr->SiS_ROMNew) {
	   data = ((SiS_GetReg(SiS_Pr->SiS_P3d4,0x78) & 0xc0) >> 6);
	}
     } else if(IS_SIS550650740) {
        data = SiS_GetReg(SiS_Pr->SiS_P3c4,0x13) & 0x07;
     } else {	/* 315, 330 */
        data = SiS_GetReg(SiS_Pr->SiS_P3c4,0x3a) & 0x03;
        if(HwInfo->jChipType == SIS_330) {
	   if(data > 1) {
	      temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x5f) & 0x30;
	      switch(temp) {
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

static USHORT
SiS_GetMCLK(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  UCHAR  *ROMAddr = HwInfo->pjVirtualRomBase;
  USHORT index;

  index = SiS_Get310DRAMType(SiS_Pr, HwInfo);
  if(HwInfo->jChipType >= SIS_661) {
     if(SiS_Pr->SiS_ROMNew) {
        return((USHORT)(SISGETROMW((0x90 + (index * 5) + 3))));
     }
     return(SiS_Pr->SiS_MCLKData_0[index].CLOCK);
  } else if(index >= 4) {
     index -= 4;
     return(SiS_Pr->SiS_MCLKData_1[index].CLOCK);
  } else {
     return(SiS_Pr->SiS_MCLKData_0[index].CLOCK);
  }
}
#endif

/*********************************************/
/*           HELPER: ClearBuffer             */
/*********************************************/

#ifdef LINUX_KERNEL
static void
SiS_ClearBuffer(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo, USHORT ModeNo)
{
  UCHAR SISIOMEMTYPE *VideoMemoryAddress = HwInfo->pjVideoMemoryAddress;
  ULONG  AdapterMemorySize = HwInfo->ulVideoMemorySize;
  USHORT SISIOMEMTYPE *pBuffer;
  int i;

  if(SiS_Pr->SiS_ModeType >= ModeEGA) {
     if(ModeNo > 0x13) {
        SiS_SetMemory(VideoMemoryAddress, AdapterMemorySize, 0);
     } else {
        pBuffer = (USHORT SISIOMEMTYPE *)VideoMemoryAddress;
        for(i=0; i<0x4000; i++) writew(0x0000, &pBuffer[i]);
     }
  } else {
     if(SiS_Pr->SiS_ModeType < ModeCGA) {
        pBuffer = (USHORT SISIOMEMTYPE *)VideoMemoryAddress;
        for(i=0; i<0x4000; i++) writew(0x0720, &pBuffer[i]);
     } else {
        SiS_SetMemory(VideoMemoryAddress, 0x8000, 0);
     }
  }
}
#endif

/*********************************************/
/*           HELPER: SearchModeID            */
/*********************************************/

BOOLEAN
SiS_SearchModeID(SiS_Private *SiS_Pr, USHORT *ModeNo, USHORT *ModeIdIndex)
{
   UCHAR VGAINFO = SiS_Pr->SiS_VGAINFO;

   if(*ModeNo <= 0x13) {

      if((*ModeNo) <= 0x05) (*ModeNo) |= 0x01;

      for(*ModeIdIndex = 0; ;(*ModeIdIndex)++) {
         if(SiS_Pr->SiS_SModeIDTable[*ModeIdIndex].St_ModeID == (*ModeNo)) break;
         if(SiS_Pr->SiS_SModeIDTable[*ModeIdIndex].St_ModeID == 0xFF)   return FALSE;
      }

      if(*ModeNo == 0x07) {
          if(VGAINFO & 0x10) (*ModeIdIndex)++;   /* 400 lines */
          /* else 350 lines */
      }
      if(*ModeNo <= 0x03) {
         if(!(VGAINFO & 0x80)) (*ModeIdIndex)++;
         if(VGAINFO & 0x10)    (*ModeIdIndex)++; /* 400 lines  */
         /* else 350 lines  */
      }
      /* else 200 lines  */

   } else {

      for(*ModeIdIndex = 0; ;(*ModeIdIndex)++) {
         if(SiS_Pr->SiS_EModeIDTable[*ModeIdIndex].Ext_ModeID == (*ModeNo)) break;
         if(SiS_Pr->SiS_EModeIDTable[*ModeIdIndex].Ext_ModeID == 0xFF)      return FALSE;
      }

   }
   return TRUE;
}

/*********************************************/
/*            HELPER: GetModePtr             */
/*********************************************/

UCHAR
SiS_GetModePtr(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex)
{
   UCHAR index;

   if(ModeNo <= 0x13) {
      index = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_StTableIndex;
   } else {
      if(SiS_Pr->SiS_ModeType <= ModeEGA) index = 0x1B;
      else index = 0x0F;
   }
   return index;
}

/*********************************************/
/*           HELPER: LowModeTests            */
/*********************************************/

static BOOLEAN
SiS_DoLowModeTest(SiS_Private *SiS_Pr, USHORT ModeNo, PSIS_HW_INFO HwInfo)
{
    USHORT temp,temp1,temp2;

    if((ModeNo != 0x03) && (ModeNo != 0x10) && (ModeNo != 0x12))
       return(TRUE);
    temp = SiS_GetReg(SiS_Pr->SiS_P3d4,0x11);
    SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x11,0x80);
    temp1 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x00);
    SiS_SetReg(SiS_Pr->SiS_P3d4,0x00,0x55);
    temp2 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x00);
    SiS_SetReg(SiS_Pr->SiS_P3d4,0x00,temp1);
    SiS_SetReg(SiS_Pr->SiS_P3d4,0x11,temp);
    if((HwInfo->jChipType >= SIS_315H) ||
       (HwInfo->jChipType == SIS_300)) {
       if(temp2 == 0x55) return(FALSE);
       else return(TRUE);
    } else {
       if(temp2 != 0x55) return(TRUE);
       else {
          SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x35,0x01);
          return(FALSE);
       }
    }
}

static void
SiS_SetLowModeTest(SiS_Private *SiS_Pr, USHORT ModeNo, PSIS_HW_INFO HwInfo)
{
    if(SiS_DoLowModeTest(SiS_Pr, ModeNo, HwInfo)) {
       SiS_Pr->SiS_SetFlag |= LowModeTests;
    }
}

/*********************************************/
/*            HELPER: ENABLE CRT1            */
/*********************************************/

static void
SiS_SetupCR5x(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
    if(IS_SIS650) {
       if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
	  SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x51,0x1f);
	  if(IS_SIS651) SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x51,0x20);
	  SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x56,0xe7);
       }
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
SiS_HandleCRT1(SiS_Private *SiS_Pr)
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

USHORT
SiS_GetColorDepth(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex)
{
  USHORT ColorDepth[6] = { 1, 2, 4, 4, 6, 8};
  SHORT  index;
  USHORT modeflag;

  /* Do NOT check UseCustomMode, will skrew up FIFO */
  if(ModeNo == 0xfe) {
     modeflag = SiS_Pr->CModeFlag;
  } else {
     if(ModeNo <= 0x13)
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     else
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  index = (modeflag & ModeTypeMask) - ModeEGA;
  if(index < 0) index = 0;
  return(ColorDepth[index]);
}

/*********************************************/
/*             HELPER: GetOffset             */
/*********************************************/

USHORT
SiS_GetOffset(SiS_Private *SiS_Pr,USHORT ModeNo,USHORT ModeIdIndex,
              USHORT RefreshRateTableIndex,PSIS_HW_INFO HwInfo)
{
  USHORT xres, temp, colordepth, infoflag;

  if(SiS_Pr->UseCustomMode) {
     infoflag = SiS_Pr->CInfoFlag;
     xres = SiS_Pr->CHDisplay;
  } else {
     infoflag = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag;
     xres = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].XRes;
  }

  colordepth = SiS_GetColorDepth(SiS_Pr,ModeNo,ModeIdIndex);

  temp = xres / 16;
  if(infoflag & InterlaceMode) temp <<= 1;
  temp *= colordepth;
  if(xres % 16) {
     colordepth >>= 1;
     temp += colordepth;
  }

  return(temp);
}

/*********************************************/
/*                   SEQ                     */
/*********************************************/

static void
SiS_SetSeqRegs(SiS_Private *SiS_Pr, USHORT StandTableIndex, PSIS_HW_INFO HwInfo)
{
   UCHAR SRdata;
   USHORT i;

   SiS_SetReg(SiS_Pr->SiS_P3c4,0x00,0x03);           	/* Set SR0  */

   SRdata = SiS_Pr->SiS_StandTable[StandTableIndex].SR[0];

   if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
         SRdata |= 0x01;
      }
      if(HwInfo->jChipType >= SIS_661) {
         if(SiS_Pr->SiS_VBInfo & (SetCRT2ToLCD | SetCRT2ToTV)) {
	    if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
               SRdata |= 0x01;          		/* 8 dot clock  */
            }
	 }
      } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
         if(SiS_Pr->SiS_VBType & VB_NoLCD) {
	    if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
               SRdata |= 0x01;          		/* 8 dot clock  */
            }
	 }
      }
   }

   if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
      if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
         if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
            if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
               SRdata |= 0x01;        			/* 8 dot clock  */
            }
         }
      }
      if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
         if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
            SRdata |= 0x01;          			/* 8 dot clock  */
         }
      }
   }

   SRdata |= 0x20;                			/* screen off  */

   SiS_SetReg(SiS_Pr->SiS_P3c4,0x01,SRdata);

   for(i = 2; i <= 4; i++) {
      SRdata = SiS_Pr->SiS_StandTable[StandTableIndex].SR[i-1];
      SiS_SetReg(SiS_Pr->SiS_P3c4,i,SRdata);
   }
}

/*********************************************/
/*                  MISC                     */
/*********************************************/

static void
SiS_SetMiscRegs(SiS_Private *SiS_Pr, USHORT StandTableIndex, PSIS_HW_INFO HwInfo)
{
   UCHAR Miscdata;

   Miscdata = SiS_Pr->SiS_StandTable[StandTableIndex].MISC;

   if(HwInfo->jChipType < SIS_661) {
      if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
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
SiS_SetCRTCRegs(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
                USHORT StandTableIndex)
{
  UCHAR CRTCdata;
  USHORT i;

  SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x11,0x7f);                       /* Unlock CRTC */

  for(i = 0; i <= 0x18; i++) {
     CRTCdata = SiS_Pr->SiS_StandTable[StandTableIndex].CRTC[i];
     SiS_SetReg(SiS_Pr->SiS_P3d4,i,CRTCdata);                     /* Set CRTC(3d4) */
  }
  if(HwInfo->jChipType >= SIS_661) {
     SiS_SetupCR5x(SiS_Pr, HwInfo);
     for(i = 0x13; i <= 0x14; i++) {
        CRTCdata = SiS_Pr->SiS_StandTable[StandTableIndex].CRTC[i];
        SiS_SetReg(SiS_Pr->SiS_P3d4,i,CRTCdata);
     }
  } else if( ( (HwInfo->jChipType == SIS_630) ||
               (HwInfo->jChipType == SIS_730) )  &&
             (HwInfo->jChipRevision >= 0x30) ) {       	   /* for 630S0 */
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
SiS_SetATTRegs(SiS_Private *SiS_Pr, USHORT StandTableIndex,
               PSIS_HW_INFO HwInfo)
{
   UCHAR ARdata;
   USHORT i;

   for(i = 0; i <= 0x13; i++) {
      ARdata = SiS_Pr->SiS_StandTable[StandTableIndex].ATTR[i];
#if 0
      if((i <= 0x0f) || (i == 0x11)) {
         if(ds:489 & 0x08) {
	    continue;
         }
      }
#endif
      if(i == 0x13) {
         /* Pixel shift. If screen on LCD or TV is shifted left or right,
          * this might be the cause.
          */
         if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
            if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)  ARdata=0;
         }
         if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
            if(SiS_Pr->SiS_IF_DEF_CH70xx != 0) {
               if(SiS_Pr->SiS_VBInfo & SetCRT2ToTV) {
                  if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) ARdata=0;
               }
            }
         }
	 if(HwInfo->jChipType >= SIS_661) {
	    if(SiS_Pr->SiS_VBInfo & (SetCRT2ToTV | SetCRT2ToLCD)) {
	       if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) ARdata=0;
	    }
	 } else if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) {
            if(HwInfo->jChipType >= SIS_315H) {
	       if(IS_SIS550650740660) {
	          /* 315, 330 don't do this */
	          if(SiS_Pr->SiS_VBType & VB_SIS301B302B) {
	             if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) ARdata=0;
	          } else {
	             ARdata = 0;
	          }
	       }
	    } else {
               if(SiS_Pr->SiS_VBInfo & SetInSlaveMode)  ARdata=0;
	    }
         }
      }
      SiS_GetRegByte(SiS_Pr->SiS_P3da);                         /* reset 3da  */
      SiS_SetRegByte(SiS_Pr->SiS_P3c0,i);                       /* set index  */
      SiS_SetRegByte(SiS_Pr->SiS_P3c0,ARdata);                  /* set data   */
   }
   SiS_GetRegByte(SiS_Pr->SiS_P3da);                            /* reset 3da  */
   SiS_SetRegByte(SiS_Pr->SiS_P3c0,0x14);                       /* set index  */
   SiS_SetRegByte(SiS_Pr->SiS_P3c0,0x00);                       /* set data   */

   SiS_GetRegByte(SiS_Pr->SiS_P3da);
   SiS_SetRegByte(SiS_Pr->SiS_P3c0,0x20);			/* Enable Attribute  */
   SiS_GetRegByte(SiS_Pr->SiS_P3da);
}

/*********************************************/
/*                   GRC                     */
/*********************************************/

static void
SiS_SetGRCRegs(SiS_Private *SiS_Pr, USHORT StandTableIndex)
{
   UCHAR GRdata;
   USHORT i;

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
SiS_ClearExt1Regs(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo, USHORT ModeNo)
{
  USHORT i;

  for(i = 0x0A; i <= 0x0E; i++) {
     SiS_SetReg(SiS_Pr->SiS_P3c4,i,0x00);
  }

  if(HwInfo->jChipType >= SIS_315H) {
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
SiS_ResetCRT1VCLK(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   if(HwInfo->jChipType >= SIS_315H) {
      if(HwInfo->jChipType < SIS_661) {
         if(SiS_Pr->SiS_IF_DEF_LVDS == 0) return;
      }
   } else {
      if((SiS_Pr->SiS_IF_DEF_LVDS == 0) &&
         (!(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV)) ) {
	 return;
      }
   }

   if(HwInfo->jChipType >= SIS_315H) {
      SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x31,0xCF,0x20);
   } else {
      SiS_SetReg(SiS_Pr->SiS_P3c4,0x31,0x20);
   }
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2B,SiS_Pr->SiS_VCLKData[1].SR2B);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2C,SiS_Pr->SiS_VCLKData[1].SR2C);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2D,0x80);
   if(HwInfo->jChipType >= SIS_315H) {
      SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x31,0xcf,0x10);
   } else {
      SiS_SetReg(SiS_Pr->SiS_P3c4,0x31,0x10);
   }
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2B,SiS_Pr->SiS_VCLKData[0].SR2B);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2C,SiS_Pr->SiS_VCLKData[0].SR2C);
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x2D,0x80);
}

/*********************************************/
/*                  SYNC                     */
/*********************************************/

static void
SiS_SetCRT1Sync(SiS_Private *SiS_Pr, USHORT RefreshRateTableIndex)
{
  USHORT sync;

  if(SiS_Pr->UseCustomMode) {
     sync = SiS_Pr->CInfoFlag >> 8;
  } else {
     sync = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag >> 8;
  }

  sync &= 0xC0;
  sync |= 0x2f;
  SiS_SetRegByte(SiS_Pr->SiS_P3c2,sync);
}

/*********************************************/
/*                  CRTC/2                   */
/*********************************************/

static void
SiS_SetCRT1CRTC(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                USHORT RefreshRateTableIndex,
		PSIS_HW_INFO HwInfo)
{
  UCHAR  index;
  USHORT temp,i,j,modeflag;

  SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x11,0x7f);		/* unlock cr0-7 */

  if(SiS_Pr->UseCustomMode) {

     modeflag = SiS_Pr->CModeFlag;

     for(i=0,j=0;i<=7;i++,j++) {
        SiS_SetReg(SiS_Pr->SiS_P3d4,j,SiS_Pr->CCRT1CRTC[i]);
     }
     for(j=0x10;i<=10;i++,j++) {
        SiS_SetReg(SiS_Pr->SiS_P3d4,j,SiS_Pr->CCRT1CRTC[i]);
     }
     for(j=0x15;i<=12;i++,j++) {
        SiS_SetReg(SiS_Pr->SiS_P3d4,j,SiS_Pr->CCRT1CRTC[i]);
     }
     for(j=0x0A;i<=15;i++,j++) {
        SiS_SetReg(SiS_Pr->SiS_P3c4,j,SiS_Pr->CCRT1CRTC[i]);
     }

     temp = SiS_Pr->CCRT1CRTC[16] & 0xE0;
     SiS_SetReg(SiS_Pr->SiS_P3c4,0x0E,temp);

     temp = (SiS_Pr->CCRT1CRTC[16] & 0x01) << 5;
     if(modeflag & DoubleScanMode) temp |= 0x80;
     SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x09,0x5F,temp);

  } else {

     if(ModeNo <= 0x13) {
        modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
     } else {
        modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
     }

     index = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;

     for(i=0,j=0;i<=7;i++,j++) {
        SiS_SetReg(SiS_Pr->SiS_P3d4,j,SiS_Pr->SiS_CRT1Table[index].CR[i]);
     }
     for(j=0x10;i<=10;i++,j++) {
        SiS_SetReg(SiS_Pr->SiS_P3d4,j,SiS_Pr->SiS_CRT1Table[index].CR[i]);
     }
     for(j=0x15;i<=12;i++,j++) {
        SiS_SetReg(SiS_Pr->SiS_P3d4,j,SiS_Pr->SiS_CRT1Table[index].CR[i]);
     }
     for(j=0x0A;i<=15;i++,j++) {
        SiS_SetReg(SiS_Pr->SiS_P3c4,j,SiS_Pr->SiS_CRT1Table[index].CR[i]);
     }

     temp = SiS_Pr->SiS_CRT1Table[index].CR[16] & 0xE0;
     SiS_SetReg(SiS_Pr->SiS_P3c4,0x0E,temp);

     temp = ((SiS_Pr->SiS_CRT1Table[index].CR[16]) & 0x01) << 5;
     if(modeflag & DoubleScanMode)  temp |= 0x80;
     SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x09,0x5F,temp);

  }

  if(SiS_Pr->SiS_ModeType > ModeVGA) SiS_SetReg(SiS_Pr->SiS_P3d4,0x14,0x4F);
}

/*********************************************/
/*               OFFSET & PITCH              */
/*********************************************/
/*  (partly overruled by SetPitch() in XF86) */
/*********************************************/

static void
SiS_SetCRT1Offset(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                  USHORT RefreshRateTableIndex,
		  PSIS_HW_INFO HwInfo)
{
   USHORT temp, DisplayUnit, infoflag;

   if(SiS_Pr->UseCustomMode) {
      infoflag = SiS_Pr->CInfoFlag;
   } else {
      infoflag = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag;
   }

   DisplayUnit = SiS_GetOffset(SiS_Pr,ModeNo,ModeIdIndex,
                     	       RefreshRateTableIndex,HwInfo);

   temp = (DisplayUnit >> 8) & 0x0f;
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0E,0xF0,temp);

   temp = DisplayUnit & 0xFF;
   SiS_SetReg(SiS_Pr->SiS_P3d4,0x13,temp);

   if(infoflag & InterlaceMode) DisplayUnit >>= 1;

   DisplayUnit <<= 5;
   temp = (DisplayUnit & 0xff00) >> 8;
   if(DisplayUnit & 0xff) temp++;
   temp++;
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x10,temp);
}

/*********************************************/
/*                  VCLK                     */
/*********************************************/

static void
SiS_SetCRT1VCLK(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                PSIS_HW_INFO HwInfo, USHORT RefreshRateTableIndex)
{
  USHORT  index=0, clka, clkb;

  if(SiS_Pr->UseCustomMode) {
     clka = SiS_Pr->CSR2B;
     clkb = SiS_Pr->CSR2C;
  } else {
     index = SiS_GetVCLK2Ptr(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);
     if((SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) && (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
        clka = SiS_Pr->SiS_VBVCLKData[index].Part4_A;
	clkb = SiS_Pr->SiS_VBVCLKData[index].Part4_B;
     } else {
        clka = SiS_Pr->SiS_VCLKData[index].SR2B;
	clkb = SiS_Pr->SiS_VCLKData[index].SR2C;
     }
  }

  if(HwInfo->jChipType >= SIS_315H) {
     SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x31,0xCF);
  } else {
     SiS_SetReg(SiS_Pr->SiS_P3c4,0x31,0x00);
  }

  SiS_SetReg(SiS_Pr->SiS_P3c4,0x2B,clka);
  SiS_SetReg(SiS_Pr->SiS_P3c4,0x2C,clkb);

  if(HwInfo->jChipType >= SIS_315H) {
     SiS_SetReg(SiS_Pr->SiS_P3c4,0x2D,0x01);
  } else {
     SiS_SetReg(SiS_Pr->SiS_P3c4,0x2D,0x80);
  }
}

/*********************************************/
/*                  FIFO                     */
/*********************************************/

#ifdef SIS300
static USHORT
SiS_DoCalcDelay(SiS_Private *SiS_Pr, USHORT MCLK, USHORT VCLK, USHORT colordepth, USHORT key)
{
  const UCHAR ThLowA[]   = { 61, 3,52, 5,68, 7,100,11,
                             43, 3,42, 5,54, 7, 78,11,
                             34, 3,37, 5,47, 7, 67,11 };

  const UCHAR ThLowB[]   = { 81, 4,72, 6,88, 8,120,12,
                             55, 4,54, 6,66, 8, 90,12,
                             42, 4,45, 6,55, 8, 75,12 };

  const UCHAR ThTiming[] = {  1, 2, 2, 3, 0, 1,  1, 2 };

  USHORT tempah, tempal, tempcl, tempbx, temp;
  ULONG  longtemp;

  tempah = SiS_GetReg(SiS_Pr->SiS_P3c4,0x18);
  tempah &= 0x62;
  tempah >>= 1;
  tempal = tempah;
  tempah >>= 3;
  tempal |= tempah;
  tempal &= 0x07;
  tempcl = ThTiming[tempal];
  tempbx = SiS_GetReg(SiS_Pr->SiS_P3c4,0x16);
  tempbx >>= 6;
  tempah = SiS_GetReg(SiS_Pr->SiS_P3c4,0x14);
  tempah >>= 4;
  tempah &= 0x0c;
  tempbx |= tempah;
  tempbx <<= 1;
  if(key == 0) {
     tempal = ThLowA[tempbx + 1];
     tempal *= tempcl;
     tempal += ThLowA[tempbx];
  } else {
     tempal = ThLowB[tempbx + 1];
     tempal *= tempcl;
     tempal += ThLowB[tempbx];
  }
  longtemp = tempal * VCLK * colordepth;
  temp = longtemp % (MCLK * 16);
  longtemp /= (MCLK * 16);
  if(temp) longtemp++;
  return((USHORT)longtemp);
}

static USHORT
SiS_CalcDelay(SiS_Private *SiS_Pr, USHORT VCLK, USHORT colordepth, USHORT MCLK)
{
  USHORT tempax, tempbx;

  tempbx = SiS_DoCalcDelay(SiS_Pr, MCLK, VCLK, colordepth, 0);
  tempax = SiS_DoCalcDelay(SiS_Pr, MCLK, VCLK, colordepth, 1);
  if(tempax < 4) tempax = 4;
  tempax -= 4;
  if(tempbx < tempax) tempbx = tempax;
  return(tempbx);
}

static void
SiS_SetCRT1FIFO_300(SiS_Private *SiS_Pr, USHORT ModeNo, PSIS_HW_INFO HwInfo,
                    USHORT RefreshRateTableIndex)
{
  USHORT  ThresholdLow = 0;
  USHORT  index, VCLK, MCLK, colorth=0;
  USHORT  tempah, temp;

  if(ModeNo > 0x13) {

     if(SiS_Pr->UseCustomMode) {
        VCLK = SiS_Pr->CSRClock;
     } else {
        index = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
        index &= 0x3F;
        VCLK = SiS_Pr->SiS_VCLKData[index].CLOCK;             /* Get VCLK  */
     }

     switch (SiS_Pr->SiS_ModeType - ModeEGA) {     /* Get half colordepth */
        case 0 : colorth = 1; break;
        case 1 : colorth = 1; break;
        case 2 : colorth = 2; break;
        case 3 : colorth = 2; break;
        case 4 : colorth = 3; break;
        case 5 : colorth = 4; break;
     }

     index = SiS_GetReg(SiS_Pr->SiS_P3c4,0x3A);
     index &= 0x07;
     MCLK = SiS_Pr->SiS_MCLKData_0[index].CLOCK;           /* Get MCLK  */

     tempah = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35);
     tempah &= 0xc3;
     SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x16,0x3c,tempah);

     do {
        ThresholdLow = SiS_CalcDelay(SiS_Pr, VCLK, colorth, MCLK);
        ThresholdLow++;
        if(ThresholdLow < 0x13) break;
        SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x16,0xfc);
        ThresholdLow = 0x13;
        tempah = SiS_GetReg(SiS_Pr->SiS_P3c4,0x16);
        tempah >>= 6;
        if(!(tempah)) break;
        tempah--;
        tempah <<= 6;
        SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x16,0x3f,tempah);
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

static USHORT
SiS_CalcDelay2(SiS_Private *SiS_Pr, UCHAR key, PSIS_HW_INFO HwInfo)
{
  USHORT data,index;
  const UCHAR  LatencyFactor[] = {
   	97, 88, 86, 79, 77, 00,       /*; 64  bit    BQ=2   */
        00, 87, 85, 78, 76, 54,       /*; 64  bit    BQ=1   */
        97, 88, 86, 79, 77, 00,       /*; 128 bit    BQ=2   */
        00, 79, 77, 70, 68, 48,       /*; 128 bit    BQ=1   */
        80, 72, 69, 63, 61, 00,       /*; 64  bit    BQ=2   */
        00, 70, 68, 61, 59, 37,       /*; 64  bit    BQ=1   */
        86, 77, 75, 68, 66, 00,       /*; 128 bit    BQ=2   */
        00, 68, 66, 59, 57, 37        /*; 128 bit    BQ=1   */
  };
  const UCHAR  LatencyFactor730[] = {
         69, 63, 61,
	 86, 79, 77,
	103, 96, 94,
	120,113,111,
	137,130,128,    /* --- Table ends with this entry, data below */
	137,130,128,	/* to avoid using illegal values              */
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
	137,130,128,
  };

  if(HwInfo->jChipType == SIS_730) {
     index = ((key & 0x0f) * 3) + ((key & 0xC0) >> 6);
     data = LatencyFactor730[index];
  } else {
     index = (key & 0xE0) >> 5;
     if(key & 0x10) index +=6;
     if(!(key & 0x01)) index += 24;
     data = SiS_GetReg(SiS_Pr->SiS_P3c4,0x14);
     if(data & 0x0080) index += 12;
     data = LatencyFactor[index];
  }
  return(data);
}

static void
SiS_SetCRT1FIFO_630(SiS_Private *SiS_Pr, USHORT ModeNo,
 		    PSIS_HW_INFO HwInfo,
                    USHORT RefreshRateTableIndex)
{
  USHORT  i,index,data,VCLK,MCLK,colorth=0;
  ULONG   B,eax,bl,data2;
  USHORT  ThresholdLow=0;
  UCHAR   FQBQData[]= {
  	0x01,0x21,0x41,0x61,0x81,
        0x31,0x51,0x71,0x91,0xb1,
        0x00,0x20,0x40,0x60,0x80,
        0x30,0x50,0x70,0x90,0xb0,
	0xFF
  };
  UCHAR   FQBQData730[]= {
        0x34,0x74,0xb4,
	0x23,0x63,0xa3,
	0x12,0x52,0x92,
	0x01,0x41,0x81,
	0x00,0x40,0x80,
	0xff
  };

  i=0;
  if(ModeNo > 0x13) {
    if(SiS_Pr->UseCustomMode) {
       VCLK = SiS_Pr->CSRClock;
    } else {
       index = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
       index &= 0x3F;
       VCLK = SiS_Pr->SiS_VCLKData[index].CLOCK;          /* Get VCLK  */
    }

    index = SiS_GetReg(SiS_Pr->SiS_P3c4,0x1A);
    index &= 0x07;
    MCLK = SiS_Pr->SiS_MCLKData_0[index].CLOCK;           /* Get MCLK  */

    data2 = SiS_Pr->SiS_ModeType - ModeEGA;	  /* Get half colordepth */
    switch (data2) {
        case 0 : colorth = 1; break;
        case 1 : colorth = 1; break;
        case 2 : colorth = 2; break;
        case 3 : colorth = 2; break;
        case 4 : colorth = 3; break;
        case 5 : colorth = 4; break;
    }

    if(HwInfo->jChipType == SIS_730) {

       do {
          B = SiS_CalcDelay2(SiS_Pr, FQBQData730[i], HwInfo) * VCLK * colorth;
	  bl = B / (MCLK * 16);

          if(B == bl * 16 * MCLK) {
             bl = bl + 1;
          } else {
             bl = bl + 2;
          }

          if(bl > 0x13) {
             if(FQBQData730[i+1] == 0xFF) {
                ThresholdLow = 0x13;
                break;
             }
             i++;
          } else {
             ThresholdLow = bl;
             break;
          }
       } while(FQBQData730[i] != 0xFF);

    } else {

       do {
          B = SiS_CalcDelay2(SiS_Pr, FQBQData[i], HwInfo) * VCLK * colorth;
          bl = B / (MCLK * 16);

          if(B == bl * 16 * MCLK) {
             bl = bl + 1;
          } else {
             bl = bl + 2;
          }

          if(bl > 0x13) {
             if(FQBQData[i+1] == 0xFF) {
                ThresholdLow = 0x13;
                break;
             }
             i++;
          } else {
             ThresholdLow = bl;
             break;
          }
       } while(FQBQData[i] != 0xFF);
    }
  }
  else {
    if(HwInfo->jChipType == SIS_730) {
    } else {
      i = 9;
    }
    ThresholdLow = 0x02;
  }

  /* Write foreground and background queue */
  if(HwInfo->jChipType == SIS_730) {

     data2 = FQBQData730[i];
     data2 = (data2 & 0xC0) >> 5;
     data2 <<= 8;

#ifdef LINUX_KERNEL
     SiS_SetRegLong(0xcf8,0x80000050);
     eax = SiS_GetRegLong(0xcfc);
     eax &= 0xfffff9ff;
     eax |= data2;
     SiS_SetRegLong(0xcfc,eax);
#else
     /* We use pci functions X offers. We use pcitag 0, because
      * we want to read/write to the host bridge (which is always
      * 00:00.0 on 630, 730 and 540), not the VGA device.
      */
     eax = pciReadLong(0x00000000, 0x50);
     eax &= 0xfffff9ff;
     eax |= data2;
     pciWriteLong(0x00000000, 0x50, eax);
#endif

     /* Write GUI grant timer (PCI config 0xA3) */
     data2 = FQBQData730[i] << 8;
     data2 = (data2 & 0x0f00) | ((data2 & 0x3000) >> 8);
     data2 <<= 20;

#ifdef LINUX_KERNEL
     SiS_SetRegLong(0xcf8,0x800000A0);
     eax = SiS_GetRegLong(0xcfc);
     eax &= 0x00ffffff;
     eax |= data2;
     SiS_SetRegLong(0xcfc,eax);
#else
     eax = pciReadLong(0x00000000, 0xA0);
     eax &= 0x00ffffff;
     eax |= data2;
     pciWriteLong(0x00000000, 0xA0, eax);
#endif

  } else {

     data2 = FQBQData[i];
     data2 = (data2 & 0xf0) >> 4;
     data2 <<= 24;

#ifdef LINUX_KERNEL
     SiS_SetRegLong(0xcf8,0x80000050);
     eax = SiS_GetRegLong(0xcfc);
     eax &= 0xf0ffffff;
     eax |= data2;
     SiS_SetRegLong(0xcfc,eax);
#else
     eax = pciReadLong(0x00000000, 0x50);
     eax &= 0xf0ffffff;
     eax |= data2;
     pciWriteLong(0x00000000, 0x50, eax);
#endif

     /* Write GUI grant timer (PCI config 0xA3) */
     data2 = FQBQData[i];
     data2 &= 0x0f;
     data2 <<= 24;

#ifdef LINUX_KERNEL
     SiS_SetRegLong(0xcf8,0x800000A0);
     eax = SiS_GetRegLong(0xcfc);
     eax &= 0xf0ffffff;
     eax |= data2;
     SiS_SetRegLong(0xcfc,eax);
#else
     eax = pciReadLong(0x00000000, 0xA0);
     eax &= 0xf0ffffff;
     eax |= data2;
     pciWriteLong(0x00000000, 0xA0, eax);
#endif

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
}
#endif

#ifdef SIS315H
static void
SiS_SetCRT1FIFO_310(SiS_Private *SiS_Pr, USHORT ModeNo, USHORT ModeIdIndex,
                    PSIS_HW_INFO HwInfo)
{
  USHORT modeflag;

  /* disable auto-threshold */
  SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x3D,0xFE);

  if(SiS_Pr->UseCustomMode) {
     modeflag = SiS_Pr->CModeFlag;
  } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
  }

  SiS_SetReg(SiS_Pr->SiS_P3c4,0x08,0xAE);
  SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x09,0xF0);
  if(ModeNo > 0x13) {
     if(HwInfo->jChipType >= SIS_661) {
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
SiS_SetVCLKState(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
                 USHORT ModeNo, USHORT RefreshRateTableIndex,
                 USHORT ModeIdIndex)
{
  USHORT data=0, VCLK=0, index=0;

  if(ModeNo > 0x13) {
     if(SiS_Pr->UseCustomMode) {
        VCLK = SiS_Pr->CSRClock;
     } else {
        index = SiS_GetVCLK2Ptr(SiS_Pr,ModeNo,ModeIdIndex,
	                      RefreshRateTableIndex,HwInfo);
        VCLK = SiS_Pr->SiS_VCLKData[index].CLOCK;
     }
  }

  if(HwInfo->jChipType < SIS_315H) {

     if(VCLK > 150) data |= 0x80;
     SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x07,0x7B,data);

     data = 0x00;
     if(VCLK >= 150) data |= 0x08;
     SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x32,0xF7,data);

  } else {

     if(VCLK >= 166) data |= 0x0c;
     SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x32,0xf3,data);

     if(VCLK >= 166) {
        SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x1f,0xe7);
     }
  }

  /* DAC speed */
  if(HwInfo->jChipType >= SIS_661) {

     SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x07,0xE8,0x10);

  } else {

     data = 0x03;
     if((VCLK >= 135) && (VCLK < 160))      data = 0x02;
     else if((VCLK >= 160) && (VCLK < 260)) data = 0x01;
     else if(VCLK >= 260)                   data = 0x00;

     if(HwInfo->jChipType == SIS_540) {
        if((VCLK == 203) || (VCLK < 234))   data = 0x02;
     }

     if(HwInfo->jChipType < SIS_315H) {
        SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x07,0xFC,data);
     } else {
        if(HwInfo->jChipType > SIS_315PRO) {
           if(ModeNo > 0x13) data &= 0xfc;
        }
        SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x07,0xF8,data);
     }

  }
}

static void
SiS_SetCRT1ModeRegs(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
                    USHORT ModeNo,USHORT ModeIdIndex,USHORT RefreshRateTableIndex)
{
  USHORT data,infoflag=0,modeflag;
  USHORT resindex,xres;
#ifdef SIS315H
  USHORT data2,data3;
  ULONG  longdata;
  UCHAR  *ROMAddr  = HwInfo->pjVirtualRomBase;
#endif

  if(SiS_Pr->UseCustomMode) {
     modeflag = SiS_Pr->CModeFlag;
     infoflag = SiS_Pr->CInfoFlag;
     xres = SiS_Pr->CHDisplay;
  } else {
     resindex = SiS_GetResInfo(SiS_Pr,ModeNo,ModeIdIndex);
     if(ModeNo > 0x13) {
    	modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
    	infoflag = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag;
	xres = SiS_Pr->SiS_ModeResInfo[resindex].HTotal;
     } else {
    	modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
	xres = SiS_Pr->SiS_StResInfo[resindex].HTotal;
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

  if(HwInfo->jChipType != SIS_300) {
     data = 0;
     if(infoflag & InterlaceMode) {
        if(xres <= 800)       data = 0x0020;
        else if(xres <= 1024) data = 0x0035;
        else                  data = 0x0048;
     }
     SiS_SetReg(SiS_Pr->SiS_P3d4,0x19,(data & 0xFF));
     SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x1a,0xFC,(data >> 8));
  }

  if(modeflag & HalfDCLK) {
     SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x01,0x08);
  }

  data = 0;
  if(modeflag & LineCompareOff) data = 0x08;
  if(HwInfo->jChipType == SIS_300) {
     SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0F,0xF7,data);
  } else {
     SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0F,0xB7,data);
     if(SiS_Pr->SiS_ModeType == ModeEGA) {
        if(ModeNo > 0x13) {
  	   SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x0F,0x40);
        }
     }
  }

  if(HwInfo->jChipType >= SIS_661) {
     SiS_SetRegAND(SiS_Pr->SiS_P3c4,0x31,0xfb);
  }

#ifdef SIS315H
  if(HwInfo->jChipType == SIS_315PRO) {

     data = SiS_Get310DRAMType(SiS_Pr, HwInfo);
     data = SiS_Pr->SiS_SR15[2][data];
     if(SiS_Pr->SiS_ModeType == ModeText) {
        data &= 0xc7;
     } else {
        data2 = SiS_GetOffset(SiS_Pr,ModeNo,ModeIdIndex,
                              RefreshRateTableIndex,HwInfo);
	data2 >>= 1;
	if(infoflag & InterlaceMode) data2 >>= 1;
	data3 = SiS_GetColorDepth(SiS_Pr,ModeNo,ModeIdIndex) >> 1;
	if(!data3) data3++;
	data2 /= data3;
	if(data2 >= 0x50) {
	   data &= 0x0f;
	   data |= 0x50;
	}
     }
     SiS_SetReg(SiS_Pr->SiS_P3c4,0x17,data);

  } else if( (HwInfo->jChipType == SIS_330) ||
             ((HwInfo->jChipType == SIS_760) && (SiS_Pr->SiS_SysFlags & SF_760LFB))) {

     data = SiS_Get310DRAMType(SiS_Pr, HwInfo);
     if(HwInfo->jChipType == SIS_330) {
        data = SiS_Pr->SiS_SR15[2][data];
     } else {
        if(SiS_Pr->SiS_ROMNew) 	    data = ROMAddr[0xf6];
        else if(SiS_Pr->SiS_UseROM) data = ROMAddr[0x100 + data];
	else                        data = 0xba;
     }
     if(SiS_Pr->SiS_ModeType <= ModeEGA) {
        data &= 0xc7;
     } else {
        if(SiS_Pr->UseCustomMode) {
	   data2 = SiS_Pr->CSRClock;
	} else {
           data2 = SiS_GetVCLK2Ptr(SiS_Pr,ModeNo,ModeIdIndex,
                                   RefreshRateTableIndex,HwInfo);
           data2 = SiS_Pr->SiS_VCLKData[data2].CLOCK;
	}

	data3 = SiS_GetColorDepth(SiS_Pr,ModeNo,ModeIdIndex) >> 1;
	if(data3) data2 *= data3;

	longdata = SiS_GetMCLK(SiS_Pr, HwInfo) * 1024;

	data2 = longdata / data2;

	if(HwInfo->jChipType == SIS_330) {
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
	} else {  /* 760+LFB */
	   if     (data2 >= 0x190) data = 0xba;
	   else if(data2 >= 0xff)  data = 0x7a;
	   else if(data2 >= 0xd3)  data = 0x3a;
	   else if(data2 >= 0xa9)  data = 0x1a;
	   else if(data2 >= 0x93)  data = 0x0a;
	   else                    data = 0x02;
	}
     }
     SiS_SetReg(SiS_Pr->SiS_P3c4,0x17,data);
  } else if(HwInfo->jChipType == SIS_340) {
     /* TODO */
  }
#endif

  data = 0x60;
  if(SiS_Pr->SiS_ModeType != ModeText) {
     data ^= 0x60;
     if(SiS_Pr->SiS_ModeType != ModeEGA) {
        data ^= 0xA0;
     }
  }
  SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x21,0x1F,data);

  SiS_SetVCLKState(SiS_Pr, HwInfo, ModeNo, RefreshRateTableIndex, ModeIdIndex);

#ifdef SIS315H
  if(HwInfo->jChipType >= SIS_315H) {
     if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x31) & 0x40) {
        SiS_SetReg(SiS_Pr->SiS_P3d4,0x52,0x2c);
     } else {
        SiS_SetReg(SiS_Pr->SiS_P3d4,0x52,0x6c);
     }
  }
#endif
}

/*********************************************/
/*                 LOAD DAC                  */
/*********************************************/

#if 0
static void
SiS_ClearDAC(SiS_Private *SiS_Pr, ULONG port)
{
   int i;

   OutPortByte(port, 0);
   port++;
   for (i=0; i < (256 * 3); i++) {
      OutPortByte(port, 0);
   }
}
#endif

static void
SiS_WriteDAC(SiS_Private *SiS_Pr, SISIOADDRESS DACData, USHORT shiftflag,
             USHORT dl, USHORT ah, USHORT al, USHORT dh)
{
  USHORT temp,bh,bl;

  bh = ah;
  bl = al;
  if(dl != 0) {
     temp = bh;
     bh = dh;
     dh = temp;
     if(dl == 1) {
        temp = bl;
        bl = dh;
        dh = temp;
     } else {
        temp = bl;
        bl = bh;
        bh = temp;
     }
  }
  if(shiftflag) {
     dh <<= 2;
     bh <<= 2;
     bl <<= 2;
  }
  SiS_SetRegByte(DACData,(USHORT)dh);
  SiS_SetRegByte(DACData,(USHORT)bh);
  SiS_SetRegByte(DACData,(USHORT)bl);
}

void
SiS_LoadDAC(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
            USHORT ModeNo, USHORT ModeIdIndex)
{
   USHORT data,data2;
   USHORT time,i,j,k,m,n,o;
   USHORT si,di,bx,dl,al,ah,dh;
   USHORT shiftflag;
   SISIOADDRESS DACAddr, DACData;
   const USHORT *table = NULL;

   if(ModeNo <= 0x13) {
      data = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
   } else {
      if(SiS_Pr->UseCustomMode) {
	 data = SiS_Pr->CModeFlag;
      } else {
         data = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
      }
   }

   data &= DACInfoFlag;
   time = 64;
   if(data == 0x00) table = SiS_MDA_DAC;
   if(data == 0x08) table = SiS_CGA_DAC;
   if(data == 0x10) table = SiS_EGA_DAC;
   if(data == 0x18) {
      time = 256;
      table = SiS_VGA_DAC;
   }
   if(time == 256) j = 16;
   else            j = time;

   if( ( (SiS_Pr->SiS_VBInfo & SetCRT2ToLCD) &&        /* 301B-DH LCD */
         (SiS_Pr->SiS_VBType & VB_NoLCD) )        ||
       (SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)       ||   /* LCDA */
       (!(SiS_Pr->SiS_SetFlag & ProgrammingCRT2)) ) {  /* Programming CRT1 */
      DACAddr = SiS_Pr->SiS_P3c8;
      DACData = SiS_Pr->SiS_P3c9;
      shiftflag = 0;
      SiS_SetRegByte(SiS_Pr->SiS_P3c6,0xFF);
   } else {
      shiftflag = 1;
      DACAddr = SiS_Pr->SiS_Part5Port;
      DACData = SiS_Pr->SiS_Part5Port + 1;
   }

   SiS_SetRegByte(DACAddr,0x00);

   for(i=0; i<j; i++) {
      data = table[i];
      for(k=0; k<3; k++) {
	data2 = 0;
	if(data & 0x01) data2 = 0x2A;
	if(data & 0x02) data2 += 0x15;
	if(shiftflag) data2 <<= 2;
	SiS_SetRegByte(DACData, data2);
	data >>= 2;
      }
   }

   if(time == 256) {
      for(i = 16; i < 32; i++) {
   	 data = table[i];
	 if(shiftflag) data <<= 2;
	 for(k = 0; k < 3; k++) SiS_SetRegByte(DACData, data);
      }
      si = 32;
      for(m = 0; m < 9; m++) {
         di = si;
         bx = si + 4;
         dl = 0;
         for(n = 0; n < 3; n++) {
  	    for(o = 0; o < 5; o++) {
	       dh = table[si];
	       ah = table[di];
	       al = table[bx];
	       si++;
	       SiS_WriteDAC(SiS_Pr, DACData, shiftflag, dl, ah, al, dh);
	    }
	    si -= 2;
	    for(o = 0; o < 3; o++) {
	       dh = table[bx];
	       ah = table[di];
	       al = table[si];
	       si--;
	       SiS_WriteDAC(SiS_Pr, DACData, shiftflag, dl, ah, al, dh);
	    }
	    dl++;
	 }            /* for n < 3 */
	 si += 5;
      }               /* for m < 9 */
   }
}

/*********************************************/
/*         SET CRT1 REGISTER GROUP           */
/*********************************************/

static void
SiS_SetCRT1Group(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
                 USHORT ModeNo, USHORT ModeIdIndex)
{
  USHORT  StandTableIndex,RefreshRateTableIndex;

  SiS_Pr->SiS_CRT1Mode = ModeNo;
  StandTableIndex = SiS_GetModePtr(SiS_Pr, ModeNo, ModeIdIndex);
  if(SiS_Pr->SiS_SetFlag & LowModeTests) {
     if(SiS_Pr->SiS_VBInfo & (SetSimuScanMode | SwitchCRT2)) {
        SiS_DisableBridge(SiS_Pr, HwInfo);
     }
  }

  SiS_ResetSegmentRegisters(SiS_Pr, HwInfo);

  SiS_SetSeqRegs(SiS_Pr, StandTableIndex, HwInfo);
  SiS_SetMiscRegs(SiS_Pr, StandTableIndex, HwInfo);
  SiS_SetCRTCRegs(SiS_Pr, HwInfo, StandTableIndex);
  SiS_SetATTRegs(SiS_Pr, StandTableIndex, HwInfo);
  SiS_SetGRCRegs(SiS_Pr, StandTableIndex);
  SiS_ClearExt1Regs(SiS_Pr, HwInfo, ModeNo);
  SiS_ResetCRT1VCLK(SiS_Pr, HwInfo);

  SiS_Pr->SiS_SelectCRT2Rate = 0;
  SiS_Pr->SiS_SetFlag &= (~ProgrammingCRT2);

#ifdef LINUX_XF86
  xf86DrvMsgVerb(0, X_PROBED, 4, "(init: VBType=0x%04x, VBInfo=0x%04x)\n",
                    SiS_Pr->SiS_VBType, SiS_Pr->SiS_VBInfo);
#endif

  if(SiS_Pr->SiS_VBInfo & SetSimuScanMode) {
     if(SiS_Pr->SiS_VBInfo & SetInSlaveMode) {
        SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
     }
  }

  if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
     SiS_Pr->SiS_SetFlag |= ProgrammingCRT2;
  }

  RefreshRateTableIndex = SiS_GetRatePtr(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);

  if(!(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA)) {
     SiS_Pr->SiS_SetFlag &= ~ProgrammingCRT2;
  }

  if(RefreshRateTableIndex != 0xFFFF) {
     SiS_SetCRT1Sync(SiS_Pr, RefreshRateTableIndex);
     SiS_SetCRT1CRTC(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);
     SiS_SetCRT1Offset(SiS_Pr, ModeNo, ModeIdIndex, RefreshRateTableIndex, HwInfo);
     SiS_SetCRT1VCLK(SiS_Pr, ModeNo, ModeIdIndex, HwInfo, RefreshRateTableIndex);
  }

#ifdef SIS300
  if(HwInfo->jChipType == SIS_300) {
     SiS_SetCRT1FIFO_300(SiS_Pr, ModeNo,HwInfo,RefreshRateTableIndex);
  } else if((HwInfo->jChipType == SIS_630) ||
            (HwInfo->jChipType == SIS_730) ||
            (HwInfo->jChipType == SIS_540)) {
     SiS_SetCRT1FIFO_630(SiS_Pr, ModeNo, HwInfo, RefreshRateTableIndex);
  }
#endif
#ifdef SIS315H
  if(HwInfo->jChipType >= SIS_315H) {
     SiS_SetCRT1FIFO_310(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);
  }
#endif

  SiS_SetCRT1ModeRegs(SiS_Pr, HwInfo, ModeNo, ModeIdIndex, RefreshRateTableIndex);

  SiS_LoadDAC(SiS_Pr, HwInfo, ModeNo, ModeIdIndex);

#ifdef LINUX_KERNEL
  if(SiS_Pr->SiS_flag_clearbuffer) {
     SiS_ClearBuffer(SiS_Pr,HwInfo,ModeNo);
  }
#endif

  if(!(SiS_Pr->SiS_VBInfo & (SetSimuScanMode | SwitchCRT2 | SetCRT2ToLCDA))) {
     SiS_WaitRetrace1(SiS_Pr);
     SiS_DisplayOn(SiS_Pr);
  }
}

/*********************************************/
/*       HELPER: VIDEO BRIDGE PROG CLK       */
/*********************************************/

static void
SiS_ResetVB(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   UCHAR  *ROMAddr  = HwInfo->pjVirtualRomBase;
   USHORT temp;

   /* VB programming clock */
   if(SiS_Pr->SiS_UseROM) {
      if(HwInfo->jChipType < SIS_330) {
         temp = ROMAddr[VB310Data_1_2_Offset] | 0x40;
	 if(SiS_Pr->SiS_ROMNew) temp = ROMAddr[0x80] | 0x40;
	 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x02,temp);
      } else if(HwInfo->jChipType >= SIS_661) {
         temp = ROMAddr[0x7e] | 0x40;
         if(SiS_Pr->SiS_ROMNew) temp = ROMAddr[0x80] | 0x40;
	 SiS_SetReg(SiS_Pr->SiS_Part1Port,0x02,temp);
      }
   }
}

/*********************************************/
/*         HELPER: SET VIDEO REGISTERS       */
/*********************************************/

static void
SiS_StrangeStuff(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
   if((IS_SIS651) || (IS_SISM650)) {
      SiS_SetReg(SiS_Pr->SiS_VidCapt, 0x3f, 0x00);   /* Fiddle with capture regs */
      SiS_SetReg(SiS_Pr->SiS_VidCapt, 0x00, 0x00);
      SiS_SetReg(SiS_Pr->SiS_VidPlay, 0x00, 0x86);   /* (BIOS does NOT unlock) */
      SiS_SetRegAND(SiS_Pr->SiS_VidPlay, 0x30, 0xfe); /* Fiddle with video regs */
      SiS_SetRegAND(SiS_Pr->SiS_VidPlay, 0x3f, 0xef);
   }
   /* !!! This does not support modes < 0x13 !!! */
}

/*********************************************/
/*         XFree86: SET SCREEN PITCH         */
/*********************************************/

#ifdef LINUX_XF86
static void
SiS_SetPitchCRT1(SiS_Private *SiS_Pr, ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   UShort HDisplay = pSiS->scrnPitch >> 3;

   SiS_SetReg(SiS_Pr->SiS_P3d4,0x13,(HDisplay & 0xFF));
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0E,0xF0,(HDisplay>>8));
}

static void
SiS_SetPitchCRT2(SiS_Private *SiS_Pr, ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   UShort HDisplay = pSiS->scrnPitch2 >> 3;

    /* Unlock CRT2 */
   if(pSiS->VGAEngine == SIS_315_VGA)
     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x2F, 0x01);
   else
     SiS_SetRegOR(SiS_Pr->SiS_Part1Port,0x24, 0x01);

   SiS_SetReg(SiS_Pr->SiS_Part1Port,0x07,(HDisplay & 0xFF));
   SiS_SetRegANDOR(SiS_Pr->SiS_Part1Port,0x09,0xF0,(HDisplay >> 8));
}

static void
SiS_SetPitch(SiS_Private *SiS_Pr, ScrnInfoPtr pScrn)
{
   SISPtr pSiS = SISPTR(pScrn);
   BOOLEAN isslavemode = FALSE;

   if( (pSiS->VBFlags & VB_VIDEOBRIDGE) &&
       ( ((pSiS->VGAEngine == SIS_300_VGA) &&
          (SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0xa0) == 0x20) ||
         ((pSiS->VGAEngine == SIS_315_VGA) &&
	  (SiS_GetReg(SiS_Pr->SiS_Part1Port,0x00) & 0x50) == 0x10) ) ) {
      isslavemode = TRUE;
   }

   /* We need to set pitch for CRT1 if bridge is in slave mode, too */
   if((pSiS->VBFlags & DISPTYPE_DISP1) || (isslavemode)) {
      SiS_SetPitchCRT1(SiS_Pr, pScrn);
   }
   /* We must not set the pitch for CRT2 if bridge is in slave mode */
   if((pSiS->VBFlags & DISPTYPE_DISP2) && (!isslavemode)) {
      SiS_SetPitchCRT2(SiS_Pr, pScrn);
   }
}
#endif

/*********************************************/
/*                 SiSSetMode()              */
/*********************************************/

#ifdef LINUX_XF86
/* We need pScrn for setting the pitch correctly */
BOOLEAN
SiSSetMode(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,ScrnInfoPtr pScrn,USHORT ModeNo, BOOLEAN dosetpitch)
#else
BOOLEAN
SiSSetMode(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,USHORT ModeNo)
#endif
{
   USHORT  ModeIdIndex;
   SISIOADDRESS BaseAddr = HwInfo->ulIOAddress;
   unsigned char backupreg=0;
#ifdef LINUX_KERNEL
   USHORT  KeepLockReg;
   ULONG   temp;

   SiS_Pr->UseCustomMode = FALSE;
   SiS_Pr->CRT1UsesCustomMode = FALSE;
#endif

   if(SiS_Pr->UseCustomMode) {
      ModeNo = 0xfe;
   }

   SiSInitPtr(SiS_Pr, HwInfo);
   SiSRegInit(SiS_Pr, BaseAddr);
   SiS_GetSysFlags(SiS_Pr, HwInfo);

#if defined(LINUX_XF86) && (defined(i386) || defined(__i386) || defined(__i386__) || defined(__AMD64__))
   if(pScrn) SiS_Pr->SiS_VGAINFO = SiS_GetSetBIOSScratch(pScrn, 0x489, 0xff);
   else
#endif
         SiS_Pr->SiS_VGAINFO = 0x11;

   SiSInitPCIetc(SiS_Pr, HwInfo);
   SiSSetLVDSetc(SiS_Pr, HwInfo);
   SiSDetermineROMUsage(SiS_Pr, HwInfo);

   SiS_Pr->SiS_flag_clearbuffer = 0;

   if(!SiS_Pr->UseCustomMode) {
#ifdef LINUX_KERNEL
      if(!(ModeNo & 0x80)) SiS_Pr->SiS_flag_clearbuffer = 1;
#endif
      ModeNo &= 0x7f;
   }

#ifdef LINUX_KERNEL
   KeepLockReg = SiS_GetReg(SiS_Pr->SiS_P3c4,0x05);
#endif
   SiS_SetReg(SiS_Pr->SiS_P3c4,0x05,0x86);

   SiS_UnLockCRT2(SiS_Pr, HwInfo);

   if(!SiS_Pr->UseCustomMode) {
      if(!(SiS_SearchModeID(SiS_Pr, &ModeNo, &ModeIdIndex))) return FALSE;
   } else {
      ModeIdIndex = 0;
   }

   SiS_GetVBType(SiS_Pr, HwInfo);

   /* Init/restore some VB registers */

   if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
      if(HwInfo->jChipType >= SIS_315H) {
         SiS_ResetVB(SiS_Pr, HwInfo);
	 SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x32,0x10);
	 SiS_SetRegOR(SiS_Pr->SiS_Part2Port,0x00,0x0c);
         backupreg = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
      } else {
         backupreg = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35);
      }
   }

   /* Get VB information (connectors, connected devices) */
   SiS_GetVBInfo(SiS_Pr, ModeNo, ModeIdIndex, HwInfo, (SiS_Pr->UseCustomMode) ? 0 : 1);
   SiS_SetYPbPr(SiS_Pr, HwInfo);
   SiS_SetTVMode(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);
   SiS_GetLCDResInfo(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);
   SiS_SetLowModeTest(SiS_Pr, ModeNo, HwInfo);

#ifdef LINUX_KERNEL
   /* 3. Check memory size (Kernel framebuffer driver only) */
   temp = SiS_CheckMemorySize(SiS_Pr, HwInfo, ModeNo, ModeIdIndex);
   if(!temp) return(0);
#endif

   if(HwInfo->jChipType >= SIS_315H) {
      SiS_SetupCR5x(SiS_Pr, HwInfo);
   }

   if(SiS_Pr->UseCustomMode) {
      SiS_Pr->CRT1UsesCustomMode = TRUE;
      SiS_Pr->CSRClock_CRT1 = SiS_Pr->CSRClock;
      SiS_Pr->CModeFlag_CRT1 = SiS_Pr->CModeFlag;
   } else {
      SiS_Pr->CRT1UsesCustomMode = FALSE;
   }

   /* Set mode on CRT1 */
   if( (SiS_Pr->SiS_VBInfo & (SetSimuScanMode | SetCRT2ToLCDA)) ||
       (!(SiS_Pr->SiS_VBInfo & SwitchCRT2)) ) {
      SiS_SetCRT1Group(SiS_Pr, HwInfo, ModeNo, ModeIdIndex);
   }

   /* Set mode on CRT2 */
   if(SiS_Pr->SiS_VBInfo & (SetSimuScanMode | SwitchCRT2 | SetCRT2ToLCDA)) {
      if( (SiS_Pr->SiS_VBType & VB_SISVB)    ||
          (SiS_Pr->SiS_IF_DEF_LVDS     == 1) ||
          (SiS_Pr->SiS_IF_DEF_CH70xx   != 0) ||
          (SiS_Pr->SiS_IF_DEF_TRUMPION != 0) ) {
         SiS_SetCRT2Group(SiS_Pr, HwInfo, ModeNo);
      }
   }

   SiS_HandleCRT1(SiS_Pr);

   SiS_StrangeStuff(SiS_Pr, HwInfo);

   SiS_DisplayOn(SiS_Pr);
   SiS_SetRegByte(SiS_Pr->SiS_P3c6,0xFF);

   if(HwInfo->jChipType >= SIS_315H) {
      if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
         if(!(SiS_IsDualEdge(SiS_Pr, HwInfo))) {
	    SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xfb);
	 }
      }
   }

   if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
      if(HwInfo->jChipType >= SIS_315H) {
         if(!SiS_Pr->SiS_ROMNew) {
	    if(SiS_IsVAMode(SiS_Pr,HwInfo)) {
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
      } else if((HwInfo->jChipType == SIS_630) ||
                (HwInfo->jChipType == SIS_730)) {
         SiS_SetReg(SiS_Pr->SiS_P3d4,0x35,backupreg);
      }
   }

#ifdef LINUX_XF86
   if(pScrn) {
      /* SetPitch: Adapt to virtual size & position */
      if((ModeNo > 0x13) && (dosetpitch)) {
         SiS_SetPitch(SiS_Pr, pScrn);
      }

      /* Backup/Set ModeNo in BIOS scratch area */
      SiS_GetSetModeID(pScrn, ModeNo);
   }
#endif

#ifdef LINUX_KERNEL  /* We never lock registers in XF86 */
   if(KeepLockReg == 0xA1) SiS_SetReg(SiS_Pr->SiS_P3c4,0x05,0x86);
   else SiS_SetReg(SiS_Pr->SiS_P3c4,0x05,0x00);
#endif

   return TRUE;
}

/*********************************************/
/*          XFree86: SiSBIOSSetMode()        */
/*           for non-Dual-Head mode          */
/*********************************************/

#ifdef LINUX_XF86
BOOLEAN
SiSBIOSSetMode(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo, ScrnInfoPtr pScrn,
               DisplayModePtr mode, BOOLEAN IsCustom)
{
   SISPtr pSiS = SISPTR(pScrn);
   UShort ModeNo = 0;

   SiS_Pr->UseCustomMode = FALSE;

   if((IsCustom) && (SiS_CheckBuildCustomMode(pScrn, mode, pSiS->VBFlags))) {

      xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 3, "Setting custom mode %dx%d\n",
	 	SiS_Pr->CHDisplay,
		(mode->Flags & V_INTERLACE ? SiS_Pr->CVDisplay * 2 :
		   (mode->Flags & V_DBLSCAN ? SiS_Pr->CVDisplay / 2 :
		      SiS_Pr->CVDisplay)));

   } else {

      /* Don't need vbflags here; checks done earlier */
      ModeNo = SiS_GetModeNumber(pScrn, mode, 0);
      if(!ModeNo) return FALSE;

      xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 3, "Setting standard mode 0x%x\n", ModeNo);

   }

   return(SiSSetMode(SiS_Pr, HwInfo, pScrn, ModeNo, TRUE));
}

/*********************************************/
/*       XFree86: SiSBIOSSetModeCRT2()       */
/*           for Dual-Head modes             */
/*********************************************/
BOOLEAN
SiSBIOSSetModeCRT2(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo, ScrnInfoPtr pScrn,
               DisplayModePtr mode, BOOLEAN IsCustom)
{
   USHORT  ModeIdIndex;
   SISIOADDRESS BaseAddr = HwInfo->ulIOAddress;
   UShort  ModeNo   = 0;
   unsigned char backupreg=0;
   SISPtr  pSiS     = SISPTR(pScrn);
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
#endif

   SiS_Pr->UseCustomMode = FALSE;

   /* Remember: Custom modes for CRT2 are ONLY supported
    *     -) on the 30x/B/C, and
    *     -) if CRT2 is LCD or VGA, or CRT1 is LCDA
    */

   if((IsCustom) && (SiS_CheckBuildCustomMode(pScrn, mode, pSiS->VBFlags))) {

	 ModeNo = 0xfe;

   } else {

         ModeNo = SiS_GetModeNumber(pScrn, mode, 0);
         if(!ModeNo) return FALSE;

   }

   SiSRegInit(SiS_Pr, BaseAddr);
   SiSInitPtr(SiS_Pr, HwInfo);
   SiS_GetSysFlags(SiS_Pr, HwInfo);
#if (defined(i386) || defined(__i386) || defined(__i386__) || defined(__AMD64__))
   SiS_Pr->SiS_VGAINFO = SiS_GetSetBIOSScratch(pScrn, 0x489, 0xff);
#else
   SiS_Pr->SiS_VGAINFO = 0x11;
#endif
   SiSInitPCIetc(SiS_Pr, HwInfo);
   SiSSetLVDSetc(SiS_Pr, HwInfo);
   SiSDetermineROMUsage(SiS_Pr, HwInfo);

   /* Save mode info so we can set it from within SetMode for CRT1 */
#ifdef SISDUALHEAD
   if(pSiS->DualHeadMode) {
      pSiSEnt->CRT2ModeNo = ModeNo;
      pSiSEnt->CRT2DMode = mode;
      pSiSEnt->CRT2IsCustom = IsCustom;
      pSiSEnt->CRT2CR30 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
      pSiSEnt->CRT2CR31 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x31);
      pSiSEnt->CRT2CR35 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35);
      pSiSEnt->CRT2CR38 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
#if 0
      /* We can't set CRT2 mode before CRT1 mode is set */
      if(pSiSEnt->CRT1ModeNo == -1) {
    	 xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 3,
		"Setting CRT2 mode delayed until after setting CRT1 mode\n");
   	 return TRUE;
      }
#endif
      pSiSEnt->CRT2ModeSet = TRUE;
   }
#endif

   /* We don't clear the buffer in X */
   SiS_Pr->SiS_flag_clearbuffer=0;

   if(SiS_Pr->UseCustomMode) {

      USHORT temptemp = SiS_Pr->CVDisplay;

      if(SiS_Pr->CModeFlag & DoubleScanMode)     temptemp >>= 1;
      else if(SiS_Pr->CInfoFlag & InterlaceMode) temptemp <<= 1;

      xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 3,
	  "Setting custom mode %dx%d on CRT2\n",
	  SiS_Pr->CHDisplay, temptemp);

   } else {

      xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 3,
   	  "Setting standard mode 0x%x on CRT2\n", ModeNo);

   }

   SiS_SetReg(SiS_Pr->SiS_P3c4,0x05,0x86);

   SiS_UnLockCRT2(SiS_Pr, HwInfo);

   if(!SiS_Pr->UseCustomMode) {
      if(!(SiS_SearchModeID(SiS_Pr, &ModeNo, &ModeIdIndex))) return FALSE;
   } else {
      ModeIdIndex = 0;
   }

   SiS_GetVBType(SiS_Pr, HwInfo);

   if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
      if(HwInfo->jChipType >= SIS_315H) {
	 SiS_ResetVB(SiS_Pr, HwInfo);
	 SiS_SetRegOR(SiS_Pr->SiS_P3c4,0x32,0x10);
	 SiS_SetRegOR(SiS_Pr->SiS_Part2Port,0x00,0x0c);
         backupreg = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
      } else {
         backupreg = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35);
      }
   }

   /* Get VB information (connectors, connected devices) */
   if(!SiS_Pr->UseCustomMode) {
      SiS_GetVBInfo(SiS_Pr, ModeNo, ModeIdIndex, HwInfo, 1);
   } else {
      /* If this is a custom mode, we don't check the modeflag for CRT2Mode */
      SiS_GetVBInfo(SiS_Pr, ModeNo, ModeIdIndex, HwInfo, 0);
   }
   SiS_SetYPbPr(SiS_Pr, HwInfo);
   SiS_SetTVMode(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);
   SiS_GetLCDResInfo(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);
   SiS_SetLowModeTest(SiS_Pr, ModeNo, HwInfo);

   /* Set mode on CRT2 */
   if( (SiS_Pr->SiS_VBType & VB_SISVB)    ||
       (SiS_Pr->SiS_IF_DEF_LVDS     == 1) ||
       (SiS_Pr->SiS_IF_DEF_CH70xx   != 0) ||
       (SiS_Pr->SiS_IF_DEF_TRUMPION != 0) ) {
      SiS_SetCRT2Group(SiS_Pr, HwInfo, ModeNo);
   }

   SiS_StrangeStuff(SiS_Pr, HwInfo);

   SiS_DisplayOn(SiS_Pr);
   SiS_SetRegByte(SiS_Pr->SiS_P3c6,0xFF);

   if(HwInfo->jChipType >= SIS_315H) {
      if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
         if(!(SiS_IsDualEdge(SiS_Pr, HwInfo))) {
	    SiS_SetRegAND(SiS_Pr->SiS_Part1Port,0x13,0xfb);
	 }
      }
   }

   if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
      if(HwInfo->jChipType >= SIS_315H) {
         if(!SiS_Pr->SiS_ROMNew) {
	    if(SiS_IsVAMode(SiS_Pr,HwInfo)) {
	       SiS_SetRegOR(SiS_Pr->SiS_P3d4,0x35,0x01);
	    } else {
	       SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x35,0xFE);
	    }
	 }

	 SiS_SetReg(SiS_Pr->SiS_P3d4,0x38,backupreg);

	 if(SiS_GetReg(SiS_Pr->SiS_P3d4,0x30) & SetCRT2ToLCD) {
	    SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x38,0xfc);
	 }
      } else if((HwInfo->jChipType == SIS_630) ||
                (HwInfo->jChipType == SIS_730)) {
         SiS_SetReg(SiS_Pr->SiS_P3d4,0x35,backupreg);
      }
   }

   /* SetPitch: Adapt to virtual size & position */
   SiS_SetPitchCRT2(SiS_Pr, pScrn);

   return TRUE;
}

/*********************************************/
/*       XFree86: SiSBIOSSetModeCRT1()       */
/*           for Dual-Head modes             */
/*********************************************/

BOOLEAN
SiSBIOSSetModeCRT1(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo, ScrnInfoPtr pScrn,
                   DisplayModePtr mode, BOOLEAN IsCustom)
{
   SISPtr  pSiS = SISPTR(pScrn);
   SISIOADDRESS BaseAddr = HwInfo->ulIOAddress;
   USHORT  ModeIdIndex, ModeNo=0;
   UCHAR backupreg=0;
#ifdef SISDUALHEAD
   SISEntPtr pSiSEnt = pSiS->entityPrivate;
   UCHAR backupcr30, backupcr31, backupcr38, backupcr35, backupp40d=0;
   BOOLEAN backupcustom;
#endif

   SiS_Pr->UseCustomMode = FALSE;

   if((IsCustom) && (SiS_CheckBuildCustomMode(pScrn, mode, pSiS->VBFlags))) {

         USHORT temptemp = SiS_Pr->CVDisplay;

         if(SiS_Pr->CModeFlag & DoubleScanMode)     temptemp >>= 1;
         else if(SiS_Pr->CInfoFlag & InterlaceMode) temptemp <<= 1;

         xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 3,
	 	"Setting custom mode %dx%d on CRT1\n",
	 	SiS_Pr->CHDisplay, temptemp);
	 ModeNo = 0xfe;

   } else {

         ModeNo = SiS_GetModeNumber(pScrn, mode, 0);
         if(!ModeNo) return FALSE;

         xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 3,
	 	"Setting standard mode 0x%x on CRT1\n", ModeNo);
   }

   SiSInitPtr(SiS_Pr, HwInfo);
   SiSRegInit(SiS_Pr, BaseAddr);
   SiS_GetSysFlags(SiS_Pr, HwInfo);
#if (defined(i386) || defined(__i386) || defined(__i386__) || defined(__AMD64__))
   SiS_Pr->SiS_VGAINFO = SiS_GetSetBIOSScratch(pScrn, 0x489, 0xff);
#else
   SiS_Pr->SiS_VGAINFO = 0x11;
#endif
   SiSInitPCIetc(SiS_Pr, HwInfo);
   SiSSetLVDSetc(SiS_Pr, HwInfo);
   SiSDetermineROMUsage(SiS_Pr, HwInfo);

   /* We don't clear the buffer in X */
   SiS_Pr->SiS_flag_clearbuffer = 0;

   SiS_SetReg(SiS_Pr->SiS_P3c4,0x05,0x86);

   SiS_UnLockCRT2(SiS_Pr, HwInfo);

   if(!SiS_Pr->UseCustomMode) {
      if(!(SiS_SearchModeID(SiS_Pr, &ModeNo, &ModeIdIndex))) return FALSE;
   } else {
      ModeIdIndex = 0;
   }

   /* Determine VBType */
   SiS_GetVBType(SiS_Pr, HwInfo);

   if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
      if(HwInfo->jChipType >= SIS_315H) {
         backupreg = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
      } else {
         backupreg = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35);
      }
   }

   /* Get VB information (connectors, connected devices) */
   /* (We don't care if the current mode is a CRT2 mode) */
   SiS_GetVBInfo(SiS_Pr, ModeNo, ModeIdIndex, HwInfo, 0);
   SiS_SetYPbPr(SiS_Pr, HwInfo);
   SiS_SetTVMode(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);
   SiS_GetLCDResInfo(SiS_Pr, ModeNo, ModeIdIndex, HwInfo);
   SiS_SetLowModeTest(SiS_Pr, ModeNo, HwInfo);

   if(HwInfo->jChipType >= SIS_315H) {
      SiS_SetupCR5x(SiS_Pr, HwInfo);
   }

   /* Set mode on CRT1 */
   SiS_SetCRT1Group(SiS_Pr, HwInfo, ModeNo, ModeIdIndex);
   if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
      SiS_SetCRT2Group(SiS_Pr, HwInfo, ModeNo);
   }

   /* SetPitch: Adapt to virtual size & position */
   SiS_SetPitchCRT1(SiS_Pr, pScrn);

#ifdef SISDUALHEAD
   if(pSiS->DualHeadMode) {
      pSiSEnt->CRT1ModeNo = ModeNo;
      pSiSEnt->CRT1DMode = mode;
   }
#endif

   if(SiS_Pr->UseCustomMode) {
      SiS_Pr->CRT1UsesCustomMode = TRUE;
      SiS_Pr->CSRClock_CRT1 = SiS_Pr->CSRClock;
      SiS_Pr->CModeFlag_CRT1 = SiS_Pr->CModeFlag;
   } else {
      SiS_Pr->CRT1UsesCustomMode = FALSE;
   }

   /* Reset CRT2 if changing mode on CRT1 */
#ifdef SISDUALHEAD
   if(pSiS->DualHeadMode) {
      if(pSiSEnt->CRT2ModeNo != -1) {
         xf86DrvMsgVerb(pScrn->scrnIndex, X_INFO, 3,
				"(Re-)Setting mode for CRT2\n");
	 backupcustom = SiS_Pr->UseCustomMode;
	 backupcr30 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x30);
	 backupcr31 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x31);
	 backupcr35 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x35);
	 backupcr38 = SiS_GetReg(SiS_Pr->SiS_P3d4,0x38);
	 if(SiS_Pr->SiS_VBType & VB_SISVB) {
	    /* Backup LUT-enable */
	    if(pSiSEnt->CRT2ModeSet) {
	       backupp40d = SiS_GetReg(SiS_Pr->SiS_Part4Port,0x0d) & 0x08;
	    }
	 }
	 if(SiS_Pr->SiS_VBInfo & SetCRT2ToLCDA) {
	    SiS_SetReg(SiS_Pr->SiS_P3d4,0x30,pSiSEnt->CRT2CR30);
	    SiS_SetReg(SiS_Pr->SiS_P3d4,0x31,pSiSEnt->CRT2CR31);
	    SiS_SetReg(SiS_Pr->SiS_P3d4,0x35,pSiSEnt->CRT2CR35);
	    SiS_SetReg(SiS_Pr->SiS_P3d4,0x38,pSiSEnt->CRT2CR38);
	 }
	 SiSBIOSSetModeCRT2(SiS_Pr, HwInfo, pSiSEnt->pScrn_1,
			    pSiSEnt->CRT2DMode, pSiSEnt->CRT2IsCustom);
         SiS_SetReg(SiS_Pr->SiS_P3d4,0x30,backupcr30);
	 SiS_SetReg(SiS_Pr->SiS_P3d4,0x31,backupcr31);
	 SiS_SetReg(SiS_Pr->SiS_P3d4,0x35,backupcr35);
	 SiS_SetReg(SiS_Pr->SiS_P3d4,0x38,backupcr38);
	 if(SiS_Pr->SiS_VBType & VB_SISVB) {
	    SiS_SetRegANDOR(SiS_Pr->SiS_Part4Port,0x0d, ~0x08, backupp40d);
	 }
	 SiS_Pr->UseCustomMode = backupcustom;
      }
   }
#endif

   /* Warning: From here, the custom mode entries in SiS_Pr are
    * possibly overwritten
    */

   SiS_HandleCRT1(SiS_Pr);

   SiS_StrangeStuff(SiS_Pr, HwInfo);

   SiS_DisplayOn(SiS_Pr);
   SiS_SetRegByte(SiS_Pr->SiS_P3c6,0xFF);

   if(SiS_Pr->SiS_VBType & VB_SIS301BLV302BLV) {
      if(HwInfo->jChipType >= SIS_315H) {
	 SiS_SetReg(SiS_Pr->SiS_P3d4,0x38,backupreg);
      } else if((HwInfo->jChipType == SIS_630) ||
                (HwInfo->jChipType == SIS_730)) {
         SiS_SetReg(SiS_Pr->SiS_P3d4,0x35,backupreg);
      }
   }

   /* Backup/Set ModeNo in BIOS scratch area */
   SiS_GetSetModeID(pScrn,ModeNo);

   return TRUE;
}
#endif /* Linux_XF86 */


#ifdef LINUX_XF86
BOOLEAN
SiS_GetPanelID(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo)
{
  const USHORT PanelTypeTable300[16] = {
      0xc101, 0xc117, 0x0121, 0xc135, 0xc142, 0xc152, 0xc162, 0xc072,
      0xc181, 0xc192, 0xc1a1, 0xc1b6, 0xc1c2, 0xc0d2, 0xc1e2, 0xc1f2
  };
  const USHORT PanelTypeTable31030x[16] = {
      0xc102, 0xc112, 0x0122, 0xc132, 0xc142, 0xc152, 0xc169, 0xc179,
      0x0189, 0xc192, 0xc1a2, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
  };
  const USHORT PanelTypeTable310LVDS[16] = {
      0xc111, 0xc122, 0xc133, 0xc144, 0xc155, 0xc166, 0xc177, 0xc188,
      0xc199, 0xc0aa, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000
  };
  USHORT tempax,tempbx,temp;

  if(HwInfo->jChipType < SIS_315H) {

     tempax = SiS_GetReg(SiS_Pr->SiS_P3c4,0x18);
     tempbx = tempax & 0x0F;
     if(!(tempax & 0x10)){
        if(SiS_Pr->SiS_IF_DEF_LVDS == 1){
           tempbx = 0;
           temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x38);
           if(temp & 0x40) tempbx |= 0x08;
           if(temp & 0x20) tempbx |= 0x02;
           if(temp & 0x01) tempbx |= 0x01;
           temp = SiS_GetReg(SiS_Pr->SiS_P3c4,0x39);
           if(temp & 0x80) tempbx |= 0x04;
        } else {
           return 0;
        }
     }
     tempbx = PanelTypeTable300[tempbx];
     tempbx |= LCDSync;
     temp = tempbx & 0x00FF;
     SiS_SetReg(SiS_Pr->SiS_P3d4,0x36,temp);
     temp = (tempbx & 0xFF00) >> 8;
     SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x37,~(LCDSyncBit|LCDRGB18Bit),temp);

  } else {

     if(HwInfo->jChipType >= SIS_661) return 0;

     tempax = SiS_GetReg(SiS_Pr->SiS_P3c4,0x1a);
     tempax &= 0x1e;
     tempax >>= 1;
     if(SiS_Pr->SiS_IF_DEF_LVDS == 1) {
        if(tempax == 0) {
           /* TODO: Include HUGE detection routine
	            (Probably not worth bothering)
	    */
           return 0;
        }
        temp = tempax & 0xff;
        tempax--;
        tempbx = PanelTypeTable310LVDS[tempax];
     } else {
        tempbx = PanelTypeTable31030x[tempax];
        temp = tempbx & 0xff;
     }
     SiS_SetReg(SiS_Pr->SiS_P3d4,0x36,temp);
     tempbx = (tempbx & 0xff00) >> 8;
     temp = tempbx & 0xc1;
     SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x37,~(LCDSyncBit|LCDRGB18Bit),temp);
     if(SiS_Pr->SiS_VBType & VB_SISVB) {
        temp = tempbx & 0x04;
        SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x39,0xfb,temp);
     }

  }
  return 1;
}
#endif

#ifndef GETBITSTR
#define BITMASK(h,l)    	(((unsigned)(1U << ((h)-(l)+1))-1)<<(l))
#define GENMASK(mask)   	BITMASK(1?mask,0?mask)
#define GETBITS(var,mask)   	(((var) & GENMASK(mask)) >> (0?mask))
#define GETBITSTR(val,from,to)  ((GETBITS(val,from)) << (0?to))
#endif

static void
SiS_CalcCRRegisters(SiS_Private *SiS_Pr, int depth)
{
   SiS_Pr->CCRT1CRTC[0]  =  ((SiS_Pr->CHTotal >> 3) - 5) & 0xff;		/* CR0 */
   SiS_Pr->CCRT1CRTC[1]  =  (SiS_Pr->CHDisplay >> 3) - 1;			/* CR1 */
   SiS_Pr->CCRT1CRTC[2]  =  (SiS_Pr->CHBlankStart >> 3) - 1;			/* CR2 */
   SiS_Pr->CCRT1CRTC[3]  =  (((SiS_Pr->CHBlankEnd >> 3) - 1) & 0x1F) | 0x80;	/* CR3 */
   SiS_Pr->CCRT1CRTC[4]  =  (SiS_Pr->CHSyncStart >> 3) + 3;			/* CR4 */
   SiS_Pr->CCRT1CRTC[5]  =  ((((SiS_Pr->CHBlankEnd >> 3) - 1) & 0x20) << 2) |	/* CR5 */
       			    (((SiS_Pr->CHSyncEnd >> 3) + 3) & 0x1F);

   SiS_Pr->CCRT1CRTC[6]  =  (SiS_Pr->CVTotal - 2) & 0xFF;			/* CR6 */
   SiS_Pr->CCRT1CRTC[7]  =  (((SiS_Pr->CVTotal - 2) & 0x100) >> 8)		/* CR7 */
 	 		  | (((SiS_Pr->CVDisplay - 1) & 0x100) >> 7)
	 		  | ((SiS_Pr->CVSyncStart & 0x100) >> 6)
	 	  	  | (((SiS_Pr->CVBlankStart - 1) & 0x100) >> 5)
			  | 0x10
	 		  | (((SiS_Pr->CVTotal - 2) & 0x200)   >> 4)
	 		  | (((SiS_Pr->CVDisplay - 1) & 0x200) >> 3)
	 		  | ((SiS_Pr->CVSyncStart & 0x200) >> 2);

   SiS_Pr->CCRT1CRTC[16] = ((((SiS_Pr->CVBlankStart - 1) & 0x200) >> 4) >> 5); 	/* CR9 */

   if(depth != 8) {
      if(SiS_Pr->CHDisplay >= 1600)      SiS_Pr->CCRT1CRTC[16] |= 0x60;		/* SRE */
      else if(SiS_Pr->CHDisplay >= 640)  SiS_Pr->CCRT1CRTC[16] |= 0x40;
   }

#if 0
   if (mode->VScan >= 32)
	regp->CRTC[9] |= 0x1F;
   else if (mode->VScan > 1)
	regp->CRTC[9] |= mode->VScan - 1;
#endif

   SiS_Pr->CCRT1CRTC[8] =  (SiS_Pr->CVSyncStart     ) & 0xFF;			/* CR10 */
   SiS_Pr->CCRT1CRTC[9] =  ((SiS_Pr->CVSyncEnd      ) & 0x0F) | 0x80;		/* CR11 */
   SiS_Pr->CCRT1CRTC[10] = (SiS_Pr->CVDisplay    - 1) & 0xFF;			/* CR12 */
   SiS_Pr->CCRT1CRTC[11] = (SiS_Pr->CVBlankStart - 1) & 0xFF;			/* CR15 */
   SiS_Pr->CCRT1CRTC[12] = (SiS_Pr->CVBlankEnd   - 1) & 0xFF;			/* CR16 */

   SiS_Pr->CCRT1CRTC[13] =							/* SRA */
                        GETBITSTR((SiS_Pr->CVTotal     -2), 10:10, 0:0) |
                        GETBITSTR((SiS_Pr->CVDisplay   -1), 10:10, 1:1) |
                        GETBITSTR((SiS_Pr->CVBlankStart-1), 10:10, 2:2) |
                        GETBITSTR((SiS_Pr->CVSyncStart   ), 10:10, 3:3) |
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
SiS_CalcLCDACRT1Timing(SiS_Private *SiS_Pr,USHORT ModeNo,USHORT ModeIdIndex)
{
   USHORT modeflag, tempax, tempbx, VGAHDE = SiS_Pr->SiS_VGAHDE;
   int i,j;

   /* 1:1 data: use data set by setcrt1crtc() */
   if(SiS_Pr->SiS_LCDInfo & LCDPass11) return;

   if(ModeNo <= 0x13) {
     modeflag = SiS_Pr->SiS_SModeIDTable[ModeIdIndex].St_ModeFlag;
   } else if(SiS_Pr->UseCustomMode) {
     modeflag = SiS_Pr->CModeFlag;
   } else {
     modeflag = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].Ext_ModeFlag;
   }

   if(modeflag & HalfDCLK) VGAHDE >>= 1;

   SiS_Pr->CHDisplay = VGAHDE;
   SiS_Pr->CHBlankStart = VGAHDE;

   SiS_Pr->CVDisplay = SiS_Pr->SiS_VGAVDE;
   SiS_Pr->CVBlankStart = SiS_Pr->SiS_VGAVDE;

   tempbx = SiS_Pr->PanelHT - SiS_Pr->PanelXRes;
   tempax = SiS_Pr->SiS_VGAHDE;  /* not /2 ! */
   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
      tempax = SiS_Pr->PanelXRes;
   }
   tempbx += tempax;
   if(modeflag & HalfDCLK) tempbx -= VGAHDE;
   SiS_Pr->CHTotal = SiS_Pr->CHBlankEnd = tempbx;

   tempax = VGAHDE;
   tempbx = SiS_Pr->CHTotal;
   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
      tempbx = SiS_Pr->PanelXRes;
      if(modeflag & HalfDCLK) tempbx >>= 1;
      tempax += ((tempbx - tempax) >> 1);
   }

   tempax += SiS_Pr->PanelHRS;
   SiS_Pr->CHSyncStart = tempax;
   tempax += SiS_Pr->PanelHRE;
   SiS_Pr->CHSyncEnd = tempax;

   tempbx = SiS_Pr->PanelVT - SiS_Pr->PanelYRes;
   tempax = SiS_Pr->SiS_VGAVDE;
   if(SiS_Pr->SiS_LCDInfo & DontExpandLCD) {
      tempax = SiS_Pr->PanelYRes;
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

   SiS_CalcCRRegisters(SiS_Pr, 8);
   SiS_Pr->CCRT1CRTC[16] &= ~0xE0;

   SiS_SetRegAND(SiS_Pr->SiS_P3d4,0x11,0x7f);

   for(i=0,j=0;i<=7;i++,j++) {
      SiS_SetReg(SiS_Pr->SiS_P3d4,j,SiS_Pr->CCRT1CRTC[i]);
   }
   for(j=0x10;i<=10;i++,j++) {
      SiS_SetReg(SiS_Pr->SiS_P3d4,j,SiS_Pr->CCRT1CRTC[i]);
   }
   for(j=0x15;i<=12;i++,j++) {
      SiS_SetReg(SiS_Pr->SiS_P3d4,j,SiS_Pr->CCRT1CRTC[i]);
   }
   for(j=0x0A;i<=15;i++,j++) {
      SiS_SetReg(SiS_Pr->SiS_P3c4,j,SiS_Pr->CCRT1CRTC[i]);
   }

   tempax = SiS_Pr->CCRT1CRTC[16] & 0xE0;
   SiS_SetRegANDOR(SiS_Pr->SiS_P3c4,0x0E,0x1F,tempax);

   tempax = (SiS_Pr->CCRT1CRTC[16] & 0x01) << 5;
   if(modeflag & DoubleScanMode) tempax |= 0x80;
   SiS_SetRegANDOR(SiS_Pr->SiS_P3d4,0x09,0x5F,tempax);

#ifdef TWDEBUG
   xf86DrvMsg(0, X_INFO, "%d %d %d %d  %d %d %d %d  (%d %d %d %d)\n",
       	SiS_Pr->CHDisplay, SiS_Pr->CHSyncStart, SiS_Pr->CHSyncEnd, SiS_Pr->CHTotal,
	SiS_Pr->CVDisplay, SiS_Pr->CVSyncStart, SiS_Pr->CVSyncEnd, SiS_Pr->CVTotal,
	SiS_Pr->CHBlankStart, SiS_Pr->CHBlankEnd, SiS_Pr->CVBlankStart, SiS_Pr->CVBlankEnd);

   xf86DrvMsg(0, X_INFO, " {{0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,\n",
   	SiS_Pr->CCRT1CRTC[0], SiS_Pr->CCRT1CRTC[1],
	SiS_Pr->CCRT1CRTC[2], SiS_Pr->CCRT1CRTC[3],
	SiS_Pr->CCRT1CRTC[4], SiS_Pr->CCRT1CRTC[5],
	SiS_Pr->CCRT1CRTC[6], SiS_Pr->CCRT1CRTC[7]);
   xf86DrvMsg(0, X_INFO, "   0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,\n",
   	SiS_Pr->CCRT1CRTC[8], SiS_Pr->CCRT1CRTC[9],
	SiS_Pr->CCRT1CRTC[10], SiS_Pr->CCRT1CRTC[11],
	SiS_Pr->CCRT1CRTC[12], SiS_Pr->CCRT1CRTC[13],
	SiS_Pr->CCRT1CRTC[14], SiS_Pr->CCRT1CRTC[15]);
   xf86DrvMsg(0, X_INFO, "   0x%02x}},\n", SiS_Pr->CCRT1CRTC[16]);
#endif
}

#ifdef LINUX_XF86

void
SiS_MakeClockRegs(ScrnInfoPtr pScrn, int clock, UCHAR *p2b, UCHAR *p2c)
{
   int          out_n, out_dn, out_div, out_sbit, out_scale;
   unsigned int vclk[5];

#define Midx         0
#define Nidx         1
#define VLDidx       2
#define Pidx         3
#define PSNidx       4

   if(SiS_compute_vclk(clock, &out_n, &out_dn, &out_div, &out_sbit, &out_scale)) {
      (*p2b) = (out_div == 2) ? 0x80 : 0x00;
      (*p2b) |= ((out_n - 1) & 0x7f);
      (*p2c) = (out_dn - 1) & 0x1f;
      (*p2c) |= (((out_scale - 1) & 3) << 5);
      (*p2c) |= ((out_sbit & 0x01) << 7);
#ifdef TWDEBUG
      xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Clock %d: n %d dn %d div %d sb %d sc %d\n",
        	 clock, out_n, out_dn, out_div, out_sbit, out_scale);
#endif
   } else {
      SiSCalcClock(pScrn, clock, 2, vclk);
      (*p2b) = (vclk[VLDidx] == 2) ? 0x80 : 0x00;
      (*p2b) |= (vclk[Midx] - 1) & 0x7f;
      (*p2c) = (vclk[Nidx] - 1) & 0x1f;
      if(vclk[Pidx] <= 4) {
         /* postscale 1,2,3,4 */
         (*p2c) |= ((vclk[Pidx] - 1) & 3) << 5;
      } else {
         /* postscale 6,8 */
         (*p2c) |= (((vclk[Pidx] / 2) - 1) & 3) << 5;
	 (*p2c) |= 0x80;
      }
#ifdef TWDEBUG
      xf86DrvMsg(pScrn->scrnIndex, X_INFO, "Clock %d: n %d dn %d div %d sc %d\n",
        	 clock, vclk[Midx], vclk[Nidx], vclk[VLDidx], vclk[Pidx]);
#endif
   }
}

#endif

/* ================ XFREE86/X.ORG ================= */

/* Helper functions */

#ifdef LINUX_XF86

USHORT
SiS_CheckBuildCustomMode(ScrnInfoPtr pScrn, DisplayModePtr mode, int VBFlags)
{
   SISPtr pSiS = SISPTR(pScrn);
   int    depth = pSiS->CurrentLayout.bitsPerPixel;

   pSiS->SiS_Pr->CModeFlag = 0;
   
   pSiS->SiS_Pr->CDClock = mode->Clock;

   pSiS->SiS_Pr->CHDisplay = mode->HDisplay;
   pSiS->SiS_Pr->CHSyncStart = mode->HSyncStart;
   pSiS->SiS_Pr->CHSyncEnd = mode->HSyncEnd;
   pSiS->SiS_Pr->CHTotal = mode->HTotal;

   pSiS->SiS_Pr->CVDisplay = mode->VDisplay;
   pSiS->SiS_Pr->CVSyncStart = mode->VSyncStart;
   pSiS->SiS_Pr->CVSyncEnd = mode->VSyncEnd;
   pSiS->SiS_Pr->CVTotal = mode->VTotal;

   pSiS->SiS_Pr->CFlags = mode->Flags;

   if(pSiS->SiS_Pr->CFlags & V_INTERLACE) {
      pSiS->SiS_Pr->CVDisplay >>= 1;
      pSiS->SiS_Pr->CVSyncStart >>= 1;
      pSiS->SiS_Pr->CVSyncEnd >>= 1;
      pSiS->SiS_Pr->CVTotal >>= 1;
   }
   if(pSiS->SiS_Pr->CFlags & V_DBLSCAN) {
      /* pSiS->SiS_Pr->CDClock <<= 1; */
      pSiS->SiS_Pr->CVDisplay <<= 1;
      pSiS->SiS_Pr->CVSyncStart <<= 1;
      pSiS->SiS_Pr->CVSyncEnd <<= 1;
      pSiS->SiS_Pr->CVTotal <<= 1;
   }

   pSiS->SiS_Pr->CHBlankStart = pSiS->SiS_Pr->CHDisplay;
   pSiS->SiS_Pr->CHBlankEnd = pSiS->SiS_Pr->CHTotal;
   pSiS->SiS_Pr->CVBlankStart = pSiS->SiS_Pr->CVSyncStart - 1;
   pSiS->SiS_Pr->CVBlankEnd = pSiS->SiS_Pr->CVTotal;

   SiS_MakeClockRegs(pScrn, pSiS->SiS_Pr->CDClock, &pSiS->SiS_Pr->CSR2B, &pSiS->SiS_Pr->CSR2C);

   pSiS->SiS_Pr->CSRClock = (pSiS->SiS_Pr->CDClock / 1000) + 1;

   SiS_CalcCRRegisters(pSiS->SiS_Pr, depth);

   switch(depth) {
   case 8:  pSiS->SiS_Pr->CModeFlag |= 0x223b; break;
   case 16: pSiS->SiS_Pr->CModeFlag |= 0x227d; break;
   case 32: pSiS->SiS_Pr->CModeFlag |= 0x22ff; break;
   default: return 0;
   }

   if(pSiS->SiS_Pr->CFlags & V_DBLSCAN)
      pSiS->SiS_Pr->CModeFlag |= DoubleScanMode;

   if((pSiS->SiS_Pr->CVDisplay >= 1024)	||
      (pSiS->SiS_Pr->CVTotal >= 1024)   ||
      (pSiS->SiS_Pr->CHDisplay >= 1024))
      pSiS->SiS_Pr->CModeFlag |= LineCompareOff;

   if(pSiS->SiS_Pr->CFlags & V_CLKDIV2)
      pSiS->SiS_Pr->CModeFlag |= HalfDCLK;

   pSiS->SiS_Pr->CInfoFlag = 0x0007;

   if(pSiS->SiS_Pr->CFlags & V_NHSYNC)
      pSiS->SiS_Pr->CInfoFlag |= 0x4000;

   if(pSiS->SiS_Pr->CFlags & V_NVSYNC)
      pSiS->SiS_Pr->CInfoFlag |= 0x8000;

   if(pSiS->SiS_Pr->CFlags & V_INTERLACE)
      pSiS->SiS_Pr->CInfoFlag |= InterlaceMode;

   pSiS->SiS_Pr->UseCustomMode = TRUE;
#ifdef TWDEBUG
   xf86DrvMsg(0, X_INFO, "Custom mode %dx%d:\n",
   	pSiS->SiS_Pr->CHDisplay,pSiS->SiS_Pr->CVDisplay);
   xf86DrvMsg(0, X_INFO, "Modeflag %04x, Infoflag %04x\n",
   	pSiS->SiS_Pr->CModeFlag, pSiS->SiS_Pr->CInfoFlag);
   xf86DrvMsg(0, X_INFO, " {{0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,\n",
   	pSiS->SiS_Pr->CCRT1CRTC[0], pSiS->SiS_Pr->CCRT1CRTC[1],
	pSiS->SiS_Pr->CCRT1CRTC[2], pSiS->SiS_Pr->CCRT1CRTC[3],
	pSiS->SiS_Pr->CCRT1CRTC[4], pSiS->SiS_Pr->CCRT1CRTC[5],
	pSiS->SiS_Pr->CCRT1CRTC[6], pSiS->SiS_Pr->CCRT1CRTC[7]);
   xf86DrvMsg(0, X_INFO, "  0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,\n",
   	pSiS->SiS_Pr->CCRT1CRTC[8], pSiS->SiS_Pr->CCRT1CRTC[9],
	pSiS->SiS_Pr->CCRT1CRTC[10], pSiS->SiS_Pr->CCRT1CRTC[11],
	pSiS->SiS_Pr->CCRT1CRTC[12], pSiS->SiS_Pr->CCRT1CRTC[13],
	pSiS->SiS_Pr->CCRT1CRTC[14], pSiS->SiS_Pr->CCRT1CRTC[15]);
   xf86DrvMsg(0, X_INFO, "  0x%02x}},\n", pSiS->SiS_Pr->CCRT1CRTC[16]);
   xf86DrvMsg(0, X_INFO, "Clock: 0x%02x, 0x%02x, %d\n",
   	pSiS->SiS_Pr->CSR2B, pSiS->SiS_Pr->CSR2C, pSiS->SiS_Pr->CSRClock);
#endif
   return 1;
}

int
SiS_FindPanelFromDB(SISPtr pSiS, USHORT panelvendor, USHORT panelproduct, int *maxx, int *maxy, int *prefx, int *prefy)
{
   int i, j;
   BOOLEAN done = FALSE;

   i = 0;
   while((!done) && (SiS_PlasmaTable[i].vendor) && panelvendor) {
      if(SiS_PlasmaTable[i].vendor == panelvendor) {
         for(j=0; j<SiS_PlasmaTable[i].productnum; j++) {
	    if(SiS_PlasmaTable[i].product[j] == panelproduct) {
	       if(SiS_PlasmaTable[i].maxx && SiS_PlasmaTable[i].maxy) {
	          (*maxx) = (int)SiS_PlasmaTable[i].maxx;
		  (*maxy) = (int)SiS_PlasmaTable[i].maxy;
		  (*prefx) = (int)SiS_PlasmaTable[i].prefx;
		  (*prefy) = (int)SiS_PlasmaTable[i].prefy;
		  done = TRUE;
		  xf86DrvMsg(pSiS->pScrn->scrnIndex, X_PROBED,
	       	        "Identified %s, correcting max X res %d, max Y res %d\n",
			 SiS_PlasmaTable[i].plasmaname,
		         SiS_PlasmaTable[i].maxx, SiS_PlasmaTable[i].maxy);
	          break;
	       }
 	    }
	 }
      }
      i++;
   }
   return (done) ? 1 : 0;
}

/* Build a list of supported modes:
 * Built-in modes for which we have all data are M_T_DEFAULT,
 * modes derived from DDC or database data are M_T_BUILTIN
 */
DisplayModePtr
SiSBuildBuiltInModeList(ScrnInfoPtr pScrn, BOOLEAN includelcdmodes, BOOLEAN isfordvi)
{
   SISPtr         pSiS = SISPTR(pScrn);
   unsigned short VRE, VBE, VRS, VBS, VDE, VT;
   unsigned short HRE, HBE, HRS, HBS, HDE, HT;
   unsigned char  sr_data, cr_data, cr_data2, cr_data3;
   unsigned char  sr2b, sr2c;
   float          num, denum, postscalar, divider;
   int            A, B, C, D, E, F, temp, i, j, k, l, index, vclkindex;
   DisplayModePtr new = NULL, current = NULL, first = NULL;
   BOOLEAN        done = FALSE;
#if 0
   DisplayModePtr backup = NULL;
#endif

   pSiS->backupmodelist = NULL;
   pSiS->AddedPlasmaModes = FALSE;

   /* Initialize our pointers */
   if(pSiS->VGAEngine == SIS_300_VGA) {
#ifdef SIS300
      InitTo300Pointer(pSiS->SiS_Pr, &pSiS->sishw_ext);
#else
      return NULL;
#endif
   } else if(pSiS->VGAEngine == SIS_315_VGA) {
#ifdef SIS315H
      InitTo310Pointer(pSiS->SiS_Pr, &pSiS->sishw_ext);
#else
      return NULL;
#endif
   } else return NULL;

   i = 0;
   while(pSiS->SiS_Pr->SiS_RefIndex[i].Ext_InfoFlag != 0xFFFF) {

      index = pSiS->SiS_Pr->SiS_RefIndex[i].Ext_CRT1CRTC;

      /* 0x5a (320x240) is a pure FTSN mode, not DSTN! */
      if((!pSiS->FSTN) &&
	 (pSiS->SiS_Pr->SiS_RefIndex[i].ModeID == 0x5a))  {
           i++;
      	   continue;
      }
      if((pSiS->FSTN) &&
         (pSiS->SiS_Pr->SiS_RefIndex[i].XRes == 320) &&
	 (pSiS->SiS_Pr->SiS_RefIndex[i].YRes == 240) &&
	 (pSiS->SiS_Pr->SiS_RefIndex[i].ModeID != 0x5a)) {
	   i++;
	   continue;
      }

      if(!(new = xalloc(sizeof(DisplayModeRec)))) return first;
      memset(new, 0, sizeof(DisplayModeRec));
      if(!(new->name = xalloc(10))) {
      		xfree(new);
		return first;
      }
      if(!first) first = new;
      if(current) {
         current->next = new;
	 new->prev = current;
      }

      current = new;

      sprintf(current->name, "%dx%d", pSiS->SiS_Pr->SiS_RefIndex[i].XRes,
                                      pSiS->SiS_Pr->SiS_RefIndex[i].YRes);

      current->status = MODE_OK;

      current->type = M_T_DEFAULT;

      vclkindex = pSiS->SiS_Pr->SiS_RefIndex[i].Ext_CRTVCLK;
      if(pSiS->VGAEngine == SIS_300_VGA) vclkindex &= 0x3F;

      sr2b = pSiS->SiS_Pr->SiS_VCLKData[vclkindex].SR2B;
      sr2c = pSiS->SiS_Pr->SiS_VCLKData[vclkindex].SR2C;

      divider = (sr2b & 0x80) ? 2.0 : 1.0;
      postscalar = (sr2c & 0x80) ?
              ( (((sr2c >> 5) & 0x03) == 0x02) ? 6.0 : 8.0) : (((sr2c >> 5) & 0x03) + 1.0);
      num = (sr2b & 0x7f) + 1.0;
      denum = (sr2c & 0x1f) + 1.0;

#ifdef TWDEBUG
      xf86DrvMsg(0, X_INFO, "------------\n");
      xf86DrvMsg(0, X_INFO, "sr2b: %x sr2c %x div %f ps %f num %f denum %f\n",
         sr2b, sr2c, divider, postscalar, num, denum);
#endif

      current->Clock = (int)(14318 * (divider / postscalar) * (num / denum));

      sr_data = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[14];
	/* inSISIDXREG(SISSR, 0x0b, sr_data); */

      cr_data = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[0];
	/* inSISIDXREG(SISCR, 0x00, cr_data); */

      /* Horizontal total */
      HT = (cr_data & 0xff) |
           ((unsigned short) (sr_data & 0x03) << 8);
      A = HT + 5;

      cr_data = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[1];
	/* inSISIDXREG(SISCR, 0x01, cr_data); */

      /* Horizontal display enable end */
      HDE = (cr_data & 0xff) |
            ((unsigned short) (sr_data & 0x0C) << 6);
      E = HDE + 1;  /* 0x80 0x64 */

      cr_data = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[4];
	/* inSISIDXREG(SISCR, 0x04, cr_data); */

      /* Horizontal retrace (=sync) start */
      HRS = (cr_data & 0xff) |
            ((unsigned short) (sr_data & 0xC0) << 2);
      F = HRS - E - 3;  /* 0x06 0x06 */

      cr_data = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[2];
	/* inSISIDXREG(SISCR, 0x02, cr_data); */

      /* Horizontal blank start */
      HBS = (cr_data & 0xff) |
            ((unsigned short) (sr_data & 0x30) << 4);

      sr_data = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[15];
	/* inSISIDXREG(SISSR, 0x0c, sr_data); */

      cr_data = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[3];
	/* inSISIDXREG(SISCR, 0x03, cr_data);  */

      cr_data2 = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[5];
	/* inSISIDXREG(SISCR, 0x05, cr_data2); */

      /* Horizontal blank end */
      HBE = (cr_data & 0x1f) |
            ((unsigned short) (cr_data2 & 0x80) >> 2) |
	    ((unsigned short) (sr_data & 0x03) << 6);

      /* Horizontal retrace (=sync) end */
      HRE = (cr_data2 & 0x1f) | ((sr_data & 0x04) << 3);

      temp = HBE - ((E - 1) & 255);
      B = (temp > 0) ? temp : (temp + 256);

      temp = HRE - ((E + F + 3) & 63);
      C = (temp > 0) ? temp : (temp + 64); /* 0x0b 0x0b */

      D = B - F - C;

      if((pSiS->SiS_Pr->SiS_RefIndex[i].XRes == 320) &&
	 ((pSiS->SiS_Pr->SiS_RefIndex[i].YRes == 200) ||
	  (pSiS->SiS_Pr->SiS_RefIndex[i].YRes == 240))) {

	 /* Terrible hack, but correct CRTC data for
	  * these modes only produces a black screen...
	  * (HRE is 0, leading into a too large C and
	  * a negative D. The CRT controller does not
	  * seem to like correcting HRE to 50
	  */
	 current->HDisplay   = 320;
         current->HSyncStart = 328;
         current->HSyncEnd   = 376;
         current->HTotal     = 400;

      } else {

         current->HDisplay   = (E * 8);
         current->HSyncStart = (E * 8) + (F * 8);
         current->HSyncEnd   = (E * 8) + (F * 8) + (C * 8);
         current->HTotal     = (E * 8) + (F * 8) + (C * 8) + (D * 8);

      }

#ifdef TWDEBUG
      xf86DrvMsg(0, X_INFO,
        "H: A %d B %d C %d D %d E %d F %d  HT %d HDE %d HRS %d HBS %d HBE %d HRE %d\n",
      	A, B, C, D, E, F, HT, HDE, HRS, HBS, HBE, HRE);
#endif

      sr_data = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[13];
	/* inSISIDXREG(SISSR, 0x0A, sr_data); */

      cr_data = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[6];
        /* inSISIDXREG(SISCR, 0x06, cr_data); */

      cr_data2 = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[7];
        /* inSISIDXREG(SISCR, 0x07, cr_data2);  */

      /* Vertical total */
      VT = (cr_data & 0xFF) |
           ((unsigned short) (cr_data2 & 0x01) << 8) |
	   ((unsigned short)(cr_data2 & 0x20) << 4) |
	   ((unsigned short) (sr_data & 0x01) << 10);
      A = VT + 2;

      cr_data = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[10];
	/* inSISIDXREG(SISCR, 0x12, cr_data);  */

      /* Vertical display enable end */
      VDE = (cr_data & 0xff) |
            ((unsigned short) (cr_data2 & 0x02) << 7) |
	    ((unsigned short) (cr_data2 & 0x40) << 3) |
	    ((unsigned short) (sr_data & 0x02) << 9);
      E = VDE + 1;

      cr_data = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[8];
	/* inSISIDXREG(SISCR, 0x10, cr_data); */

      /* Vertical retrace (=sync) start */
      VRS = (cr_data & 0xff) |
            ((unsigned short) (cr_data2 & 0x04) << 6) |
	    ((unsigned short) (cr_data2 & 0x80) << 2) |
	    ((unsigned short) (sr_data & 0x08) << 7);
      F = VRS + 1 - E;

      cr_data =  pSiS->SiS_Pr->SiS_CRT1Table[index].CR[11];
	/* inSISIDXREG(SISCR, 0x15, cr_data);  */

      cr_data3 = (pSiS->SiS_Pr->SiS_CRT1Table[index].CR[16] & 0x01) << 5;
	/* inSISIDXREG(SISCR, 0x09, cr_data3);  */

      /* Vertical blank start */
      VBS = (cr_data & 0xff) |
            ((unsigned short) (cr_data2 & 0x08) << 5) |
	    ((unsigned short) (cr_data3 & 0x20) << 4) |
	    ((unsigned short) (sr_data & 0x04) << 8);

      cr_data =  pSiS->SiS_Pr->SiS_CRT1Table[index].CR[12];
	/* inSISIDXREG(SISCR, 0x16, cr_data); */

      /* Vertical blank end */
      VBE = (cr_data & 0xff) |
            ((unsigned short) (sr_data & 0x10) << 4);
      temp = VBE - ((E - 1) & 511);
      B = (temp > 0) ? temp : (temp + 512);

      cr_data = pSiS->SiS_Pr->SiS_CRT1Table[index].CR[9];
	/* inSISIDXREG(SISCR, 0x11, cr_data); */

      /* Vertical retrace (=sync) end */
      VRE = (cr_data & 0x0f) | ((sr_data & 0x20) >> 1);
      temp = VRE - ((E + F - 1) & 31);
      C = (temp > 0) ? temp : (temp + 32);

      D = B - F - C;

      current->VDisplay   = VDE + 1;
      current->VSyncStart = VRS + 1;
      current->VSyncEnd   = ((VRS & ~0x1f) | VRE) + 1;
      if(VRE <= (VRS & 0x1f)) current->VSyncEnd += 32;
      current->VTotal     = E + D + C + F;

#if 0
      current->VDisplay   = E;
      current->VSyncStart = E + D;
      current->VSyncEnd   = E + D + C;
      current->VTotal     = E + D + C + F;
#endif

#ifdef TWDEBUG
      xf86DrvMsg(0, X_INFO,
        "V: A %d B %d C %d D %d E %d F %d  VT %d VDE %d VRS %d VBS %d VBE %d VRE %d\n",
      	A, B, C, D, E, F, VT, VDE, VRS, VBS, VBE, VRE);
#endif

      if(pSiS->SiS_Pr->SiS_RefIndex[i].Ext_InfoFlag & 0x4000)
          current->Flags |= V_NHSYNC;
      else
          current->Flags |= V_PHSYNC;

      if(pSiS->SiS_Pr->SiS_RefIndex[i].Ext_InfoFlag & 0x8000)
      	  current->Flags |= V_NVSYNC;
      else
          current->Flags |= V_PVSYNC;

      if(pSiS->SiS_Pr->SiS_RefIndex[i].Ext_InfoFlag & 0x0080)
          current->Flags |= V_INTERLACE;

      j = 0;
      while(pSiS->SiS_Pr->SiS_EModeIDTable[j].Ext_ModeID != 0xff) {
          if(pSiS->SiS_Pr->SiS_EModeIDTable[j].Ext_ModeID ==
	                  pSiS->SiS_Pr->SiS_RefIndex[i].ModeID) {
              if(pSiS->SiS_Pr->SiS_EModeIDTable[j].Ext_ModeFlag & DoubleScanMode) {
	      	  current->Flags |= V_DBLSCAN;
              }
	      break;
          }
	  j++;
      }

      if(current->Flags & V_INTERLACE) {
         current->VDisplay <<= 1;
	 current->VSyncStart <<= 1;
	 current->VSyncEnd <<= 1;
	 current->VTotal <<= 1;
	 current->VTotal |= 1;
      }
      if(current->Flags & V_DBLSCAN) {
         current->Clock >>= 1;
	 current->VDisplay >>= 1;
	 current->VSyncStart >>= 1;
	 current->VSyncEnd >>= 1;
	 current->VTotal >>= 1;
      }

#ifdef TWDEBUG
      xf86DrvMsg(pScrn->scrnIndex, X_INFO,
      	"Built-in: %s %.2f %d %d %d %d %d %d %d %d\n",
	current->name, (float)current->Clock / 1000,
	current->HDisplay, current->HSyncStart, current->HSyncEnd, current->HTotal,
	current->VDisplay, current->VSyncStart, current->VSyncEnd, current->VTotal);
#else
        (void)VBS;  (void)HBS;  (void)A;
#endif

      i++;
   }

   /* Add non-standard LCD modes for panel's detailed timings */

   if(!includelcdmodes) return first;

   if(pSiS->SiS_Pr->CP_Vendor) {
      xf86DrvMsg(0, X_INFO, "Checking database for vendor %x, product %x\n",
         pSiS->SiS_Pr->CP_Vendor, pSiS->SiS_Pr->CP_Product);
   }

   i = 0;
   while((!done) && (SiS_PlasmaTable[i].vendor) && (pSiS->SiS_Pr->CP_Vendor)) {

     if(SiS_PlasmaTable[i].vendor == pSiS->SiS_Pr->CP_Vendor) {

        for(j=0; j<SiS_PlasmaTable[i].productnum; j++) {

	    if(SiS_PlasmaTable[i].product[j] == pSiS->SiS_Pr->CP_Product) {

	       xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
	       	  "Identified %s panel, adding specific modes\n",
		  SiS_PlasmaTable[i].plasmaname);

	       for(k=0; k<SiS_PlasmaTable[i].modenum; k++) {

	          if(isfordvi) {
		     if(!(SiS_PlasmaTable[i].plasmamodes[k] & 0x80)) continue;
		  } else {
		     if(!(SiS_PlasmaTable[i].plasmamodes[k] & 0x40)) continue;
		  }

		  l = SiS_PlasmaTable[i].plasmamodes[k] & 0x3f;

		  if(pSiS->VBFlags & (VB_301|VB_301B|VB_302B|VB_301LV)) {
		     if(isfordvi) {
		        if(SiS_PlasmaMode[l].VDisplay > 1024) continue;
		     }
		  }

	          if(!(new = xalloc(sizeof(DisplayModeRec)))) return first;

                  memset(new, 0, sizeof(DisplayModeRec));
                  if(!(new->name = xalloc(12))) {
      		     xfree(new);
		     return first;
                  }
                  if(!first) first = new;
                  if(current) {
                     current->next = new;
	             new->prev = current;
                  }

                  current = new;

		  pSiS->AddedPlasmaModes = TRUE;

		  strcpy(current->name, SiS_PlasmaMode[l].name);
	          /* sprintf(current->name, "%dx%d", SiS_PlasmaMode[l].HDisplay,
                                                  SiS_PlasmaMode[l].VDisplay); */

                  current->status = MODE_OK;

                  current->type = M_T_BUILTIN;

		  current->Clock = SiS_PlasmaMode[l].clock;
            	  current->SynthClock = current->Clock;

                  current->HDisplay   = SiS_PlasmaMode[l].HDisplay;
                  current->HSyncStart = current->HDisplay + SiS_PlasmaMode[l].HFrontPorch;
                  current->HSyncEnd   = current->HSyncStart + SiS_PlasmaMode[l].HSyncWidth;
                  current->HTotal     = SiS_PlasmaMode[l].HTotal;

		  current->VDisplay   = SiS_PlasmaMode[l].VDisplay;
                  current->VSyncStart = current->VDisplay + SiS_PlasmaMode[l].VFrontPorch;
                  current->VSyncEnd   = current->VSyncStart + SiS_PlasmaMode[l].VSyncWidth;
                  current->VTotal     = SiS_PlasmaMode[l].VTotal;

                  current->CrtcHDisplay = current->HDisplay;
                  current->CrtcHBlankStart = current->HSyncStart;
                  current->CrtcHSyncStart = current->HSyncStart;
                  current->CrtcHSyncEnd = current->HSyncEnd;
                  current->CrtcHBlankEnd = current->HSyncEnd;
                  current->CrtcHTotal = current->HTotal;

                  current->CrtcVDisplay = current->VDisplay;
                  current->CrtcVBlankStart = current->VSyncStart;
                  current->CrtcVSyncStart = current->VSyncStart;
                  current->CrtcVSyncEnd = current->VSyncEnd;
                  current->CrtcVBlankEnd = current->VSyncEnd;
                  current->CrtcVTotal = current->VTotal;

                  if(SiS_PlasmaMode[l].SyncFlags & SIS_PL_HSYNCP)
                     current->Flags |= V_PHSYNC;
                  else
                     current->Flags |= V_NHSYNC;

                  if(SiS_PlasmaMode[l].SyncFlags & SIS_PL_VSYNCP)
                     current->Flags |= V_PVSYNC;
                  else
                     current->Flags |= V_NVSYNC;

		  if(current->HDisplay > pSiS->LCDwidth)
		     pSiS->LCDwidth = pSiS->SiS_Pr->CP_MaxX = current->HDisplay;
	          if(current->VDisplay > pSiS->LCDheight)
		     pSiS->LCDheight = pSiS->SiS_Pr->CP_MaxY = current->VDisplay;

		  xf86DrvMsg(pScrn->scrnIndex, X_PROBED,
		  	"\tAdding \"%s\" to list of built-in modes\n", current->name);

               }
	       done = TRUE;
	       break;
	    }
	}
     }

     i++;

   }

   if(pSiS->SiS_Pr->CP_HaveCustomData) {

      for(i=0; i<7; i++) {

         if(pSiS->SiS_Pr->CP_DataValid[i]) {

            if(!(new = xalloc(sizeof(DisplayModeRec)))) return first;

            memset(new, 0, sizeof(DisplayModeRec));
            if(!(new->name = xalloc(10))) {
      		xfree(new);
		return first;
            }
            if(!first) first = new;
            if(current) {
               current->next = new;
	       new->prev = current;
            }

            current = new;

            sprintf(current->name, "%dx%d", pSiS->SiS_Pr->CP_HDisplay[i],
                                            pSiS->SiS_Pr->CP_VDisplay[i]);

            current->status = MODE_OK;

            current->type = M_T_BUILTIN;

            current->Clock = pSiS->SiS_Pr->CP_Clock[i];
            current->SynthClock = current->Clock;

            current->HDisplay   = pSiS->SiS_Pr->CP_HDisplay[i];
            current->HSyncStart = pSiS->SiS_Pr->CP_HSyncStart[i];
            current->HSyncEnd   = pSiS->SiS_Pr->CP_HSyncEnd[i];
            current->HTotal     = pSiS->SiS_Pr->CP_HTotal[i];

            current->VDisplay   = pSiS->SiS_Pr->CP_VDisplay[i];
            current->VSyncStart = pSiS->SiS_Pr->CP_VSyncStart[i];
            current->VSyncEnd   = pSiS->SiS_Pr->CP_VSyncEnd[i];
            current->VTotal     = pSiS->SiS_Pr->CP_VTotal[i];

            current->CrtcHDisplay = current->HDisplay;
            current->CrtcHBlankStart = pSiS->SiS_Pr->CP_HBlankStart[i];
            current->CrtcHSyncStart = current->HSyncStart;
            current->CrtcHSyncEnd = current->HSyncEnd;
            current->CrtcHBlankEnd = pSiS->SiS_Pr->CP_HBlankEnd[i];
            current->CrtcHTotal = current->HTotal;

            current->CrtcVDisplay = current->VDisplay;
            current->CrtcVBlankStart = pSiS->SiS_Pr->CP_VBlankStart[i];
            current->CrtcVSyncStart = current->VSyncStart;
            current->CrtcVSyncEnd = current->VSyncEnd;
            current->CrtcVBlankEnd = pSiS->SiS_Pr->CP_VBlankEnd[i];
            current->CrtcVTotal = current->VTotal;

	    if(pSiS->SiS_Pr->CP_SyncValid[i]) {
               if(pSiS->SiS_Pr->CP_HSync_P[i])
                  current->Flags |= V_PHSYNC;
               else
                  current->Flags |= V_NHSYNC;

               if(pSiS->SiS_Pr->CP_VSync_P[i])
                  current->Flags |= V_PVSYNC;
               else
                  current->Flags |= V_NVSYNC;
	    } else {
	       /* No sync data? Use positive sync... */
	       current->Flags |= V_PHSYNC;
	       current->Flags |= V_PVSYNC;
	    }
         }
      }
   }

   return first;

}

/* Translate a mode number into the VESA pendant */
int
SiSTranslateToVESA(ScrnInfoPtr pScrn, int modenumber)
{
   SISPtr pSiS = SISPTR(pScrn);
   int    i = 0;

   /* Initialize our pointers */
   if(pSiS->VGAEngine == SIS_300_VGA) {
#ifdef SIS300
	InitTo300Pointer(pSiS->SiS_Pr, &pSiS->sishw_ext);
#else
	return -1;
#endif
   } else if(pSiS->VGAEngine == SIS_315_VGA) {
#ifdef SIS315H
       	InitTo310Pointer(pSiS->SiS_Pr, &pSiS->sishw_ext);
#else
	return -1;
#endif
   } else return -1;

   if(modenumber <= 0x13) return modenumber;

#ifdef SIS315H
   if(pSiS->ROM661New) {
      while(SiS_EModeIDTable661[i].Ext_ModeID != 0xff) {
         if(SiS_EModeIDTable661[i].Ext_ModeID == modenumber) {
            return (int)SiS_EModeIDTable661[i].Ext_VESAID;
         }
         i++;
      }
   } else {
#endif
      while(pSiS->SiS_Pr->SiS_EModeIDTable[i].Ext_ModeID != 0xff) {
         if(pSiS->SiS_Pr->SiS_EModeIDTable[i].Ext_ModeID == modenumber) {
            return (int)pSiS->SiS_Pr->SiS_EModeIDTable[i].Ext_VESAID;
         }
         i++;
      }
#ifdef SIS315H
   }
#endif
   return -1;
}

/* Translate a new BIOS mode number into the driver's pendant */
int
SiSTranslateToOldMode(int modenumber)
{
#ifdef SIS315H
   int    i = 0;

   while(SiS_EModeIDTable661[i].Ext_ModeID != 0xff) {
      if(SiS_EModeIDTable661[i].Ext_ModeID == modenumber) {
         if(SiS_EModeIDTable661[i].Ext_MyModeID)
            return (int)SiS_EModeIDTable661[i].Ext_MyModeID;
	 else
	    return modenumber;
      }
      i++;
   }
#endif
   return modenumber;
}

#endif  /* Xfree86 */

#ifdef LINUX_KERNEL
int
sisfb_mode_rate_to_dclock(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
			  unsigned char modeno, unsigned char rateindex)
{
    USHORT ModeNo = modeno;
    USHORT ModeIdIndex = 0, ClockIndex = 0;
    USHORT RefreshRateTableIndex = 0;
    int    Clock;

    if(HwInfo->jChipType < SIS_315H) {
#ifdef SIS300
       InitTo300Pointer(SiS_Pr, HwInfo);
#else
       return 65 * 1000;
#endif
    } else {
#ifdef SIS315H
       InitTo310Pointer(SiS_Pr, HwInfo);
#else
       return 65 * 1000;
#endif
    }

    if(!(SiS_SearchModeID(SiS_Pr, &ModeNo, &ModeIdIndex))) {;
    	printk(KERN_ERR "Could not find mode %x\n", ModeNo);
    	return 65 * 1000;
    }

    RefreshRateTableIndex = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].REFindex;
    RefreshRateTableIndex += (rateindex - 1);
    ClockIndex = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;
    if(HwInfo->jChipType < SIS_315H) {
       ClockIndex &= 0x3F;
    }
    Clock = SiS_Pr->SiS_VCLKData[ClockIndex].CLOCK * 1000;
    
    return(Clock);
}

BOOLEAN
sisfb_gettotalfrommode(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
		       unsigned char modeno, int *htotal, int *vtotal, unsigned char rateindex)
{
    USHORT ModeNo = modeno;
    USHORT ModeIdIndex = 0, CRT1Index = 0;
    USHORT RefreshRateTableIndex = 0;
    unsigned char  sr_data, cr_data, cr_data2;

    if(HwInfo->jChipType < SIS_315H) {
#ifdef SIS300
       InitTo300Pointer(SiS_Pr, HwInfo);
#else
       return FALSE;
#endif
    } else {
#ifdef SIS315H
       InitTo310Pointer(SiS_Pr, HwInfo);
#else
       return FALSE;
#endif
    }

    if(!(SiS_SearchModeID(SiS_Pr, &ModeNo, &ModeIdIndex))) return FALSE;

    RefreshRateTableIndex = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].REFindex;
    RefreshRateTableIndex += (rateindex - 1);
    CRT1Index = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;

    sr_data = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[14];
    cr_data = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[0];
    *htotal = (((cr_data & 0xff) | ((unsigned short) (sr_data & 0x03) << 8)) + 5) * 8;

    sr_data = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[13];
    cr_data = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[6];
    cr_data2 = SiS_Pr->SiS_CRT1Table[CRT1Index].CR[7];
    *vtotal = ((cr_data & 0xFF) |
               ((unsigned short)(cr_data2 & 0x01) <<  8) |
	       ((unsigned short)(cr_data2 & 0x20) <<  4) |
	       ((unsigned short)(sr_data  & 0x01) << 10)) + 2;

    if(SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag & InterlaceMode)
       *vtotal *= 2;

    return TRUE;
}

int
sisfb_mode_rate_to_ddata(SiS_Private *SiS_Pr, PSIS_HW_INFO HwInfo,
			 unsigned char modeno, unsigned char rateindex,
			 struct fb_var_screeninfo *var)
{
    USHORT ModeNo = modeno;
    USHORT ModeIdIndex = 0, index = 0;
    USHORT RefreshRateTableIndex = 0;
    unsigned short VRE, VBE, VRS, VBS, VDE, VT;
    unsigned short HRE, HBE, HRS, HBS, HDE, HT;
    unsigned char  sr_data, cr_data, cr_data2, cr_data3;
    int            A, B, C, D, E, F, temp, j;
   
    if(HwInfo->jChipType < SIS_315H) {
#ifdef SIS300
       InitTo300Pointer(SiS_Pr, HwInfo);
#else
       return 0;
#endif
    } else {
#ifdef SIS315H
       InitTo310Pointer(SiS_Pr, HwInfo);
#else
       return 0;
#endif
    }

    if(!(SiS_SearchModeID(SiS_Pr, &ModeNo, &ModeIdIndex))) return 0;

    RefreshRateTableIndex = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].REFindex;
    RefreshRateTableIndex += (rateindex - 1);
    index = SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;

    sr_data = SiS_Pr->SiS_CRT1Table[index].CR[14];

    cr_data = SiS_Pr->SiS_CRT1Table[index].CR[0];

    /* Horizontal total */
    HT = (cr_data & 0xff) |
         ((unsigned short) (sr_data & 0x03) << 8);
    A = HT + 5;

    cr_data = SiS_Pr->SiS_CRT1Table[index].CR[1];
	
    /* Horizontal display enable end */
    HDE = (cr_data & 0xff) |
          ((unsigned short) (sr_data & 0x0C) << 6);
    E = HDE + 1;

    cr_data = SiS_Pr->SiS_CRT1Table[index].CR[4];
	
    /* Horizontal retrace (=sync) start */
    HRS = (cr_data & 0xff) |
          ((unsigned short) (sr_data & 0xC0) << 2);
    F = HRS - E - 3;

    cr_data = SiS_Pr->SiS_CRT1Table[index].CR[2];
	
    /* Horizontal blank start */
    HBS = (cr_data & 0xff) |
          ((unsigned short) (sr_data & 0x30) << 4);

    sr_data = SiS_Pr->SiS_CRT1Table[index].CR[15];
	
    cr_data = SiS_Pr->SiS_CRT1Table[index].CR[3];

    cr_data2 = SiS_Pr->SiS_CRT1Table[index].CR[5];
	
    /* Horizontal blank end */
    HBE = (cr_data & 0x1f) |
          ((unsigned short) (cr_data2 & 0x80) >> 2) |
	  ((unsigned short) (sr_data & 0x03) << 6);

    /* Horizontal retrace (=sync) end */
    HRE = (cr_data2 & 0x1f) | ((sr_data & 0x04) << 3);

    temp = HBE - ((E - 1) & 255);
    B = (temp > 0) ? temp : (temp + 256);

    temp = HRE - ((E + F + 3) & 63);
    C = (temp > 0) ? temp : (temp + 64);

    D = B - F - C;

    if((SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].XRes == 320) &&
       ((SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].YRes == 200) ||
	(SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].YRes == 240))) {

	 /* Terrible hack, but the correct CRTC data for
	  * these modes only produces a black screen...
	  */
       var->left_margin = (400 - 376);
       var->right_margin = (328 - 320);
       var->hsync_len = (376 - 328);

    } else {

       var->left_margin = D * 8;
       var->right_margin = F * 8;
       var->hsync_len = C * 8;

    }

    sr_data = SiS_Pr->SiS_CRT1Table[index].CR[13];

    cr_data = SiS_Pr->SiS_CRT1Table[index].CR[6];

    cr_data2 = SiS_Pr->SiS_CRT1Table[index].CR[7];

    /* Vertical total */
    VT = (cr_data & 0xFF) |
         ((unsigned short) (cr_data2 & 0x01) << 8) |
	 ((unsigned short)(cr_data2 & 0x20) << 4) |
	 ((unsigned short) (sr_data & 0x01) << 10);
    A = VT + 2;

    cr_data = SiS_Pr->SiS_CRT1Table[index].CR[10];
	
    /* Vertical display enable end */
    VDE = (cr_data & 0xff) |
          ((unsigned short) (cr_data2 & 0x02) << 7) |
	  ((unsigned short) (cr_data2 & 0x40) << 3) |
	  ((unsigned short) (sr_data & 0x02) << 9);
    E = VDE + 1;

    cr_data = SiS_Pr->SiS_CRT1Table[index].CR[8];

    /* Vertical retrace (=sync) start */
    VRS = (cr_data & 0xff) |
          ((unsigned short) (cr_data2 & 0x04) << 6) |
	  ((unsigned short) (cr_data2 & 0x80) << 2) |
	  ((unsigned short) (sr_data & 0x08) << 7);
    F = VRS + 1 - E;

    cr_data =  SiS_Pr->SiS_CRT1Table[index].CR[11];

    cr_data3 = (SiS_Pr->SiS_CRT1Table[index].CR[16] & 0x01) << 5;

    /* Vertical blank start */
    VBS = (cr_data & 0xff) |
          ((unsigned short) (cr_data2 & 0x08) << 5) |
	  ((unsigned short) (cr_data3 & 0x20) << 4) |
	  ((unsigned short) (sr_data & 0x04) << 8);

    cr_data =  SiS_Pr->SiS_CRT1Table[index].CR[12];

    /* Vertical blank end */
    VBE = (cr_data & 0xff) |
          ((unsigned short) (sr_data & 0x10) << 4);
    temp = VBE - ((E - 1) & 511);
    B = (temp > 0) ? temp : (temp + 512);

    cr_data = SiS_Pr->SiS_CRT1Table[index].CR[9];

    /* Vertical retrace (=sync) end */
    VRE = (cr_data & 0x0f) | ((sr_data & 0x20) >> 1);
    temp = VRE - ((E + F - 1) & 31);
    C = (temp > 0) ? temp : (temp + 32);

    D = B - F - C;
      
    var->upper_margin = D;
    var->lower_margin = F;
    var->vsync_len = C;

    if(SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag & 0x8000)
       var->sync &= ~FB_SYNC_VERT_HIGH_ACT;
    else
       var->sync |= FB_SYNC_VERT_HIGH_ACT;

    if(SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag & 0x4000)       
       var->sync &= ~FB_SYNC_HOR_HIGH_ACT;
    else
       var->sync |= FB_SYNC_HOR_HIGH_ACT;
		
    var->vmode = FB_VMODE_NONINTERLACED;
    if(SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].Ext_InfoFlag & 0x0080)
       var->vmode = FB_VMODE_INTERLACED;
    else {
       j = 0;
       while(SiS_Pr->SiS_EModeIDTable[j].Ext_ModeID != 0xff) {
          if(SiS_Pr->SiS_EModeIDTable[j].Ext_ModeID ==
	                  SiS_Pr->SiS_RefIndex[RefreshRateTableIndex].ModeID) {
              if(SiS_Pr->SiS_EModeIDTable[j].Ext_ModeFlag & DoubleScanMode) {
	      	  var->vmode = FB_VMODE_DOUBLE;
              }
	      break;
          }
	  j++;
       }
    }       

    if((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
#if 0  /* Do this? */
       var->upper_margin <<= 1;
       var->lower_margin <<= 1;
       var->vsync_len <<= 1;
#endif
    } else if((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
       var->upper_margin >>= 1;
       var->lower_margin >>= 1;
       var->vsync_len >>= 1;
    }

    return 1;       
}			  

#endif

