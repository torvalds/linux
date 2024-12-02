/* $XFree86$ */
/* $XdotOrg$ */
/*
 * Global definitions for init.c and init301.c
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

#ifndef _INITDEF_
#define _INITDEF_

#define IS_SIS330		(SiS_Pr->ChipType == SIS_330)
#define IS_SIS550		(SiS_Pr->ChipType == SIS_550)
#define IS_SIS650		(SiS_Pr->ChipType == SIS_650)  /* All versions, incl 651, M65x */
#define IS_SIS740		(SiS_Pr->ChipType == SIS_740)
#define IS_SIS651	        (SiS_Pr->SiS_SysFlags & (SF_Is651 | SF_Is652))
#define IS_SISM650	        (SiS_Pr->SiS_SysFlags & (SF_IsM650 | SF_IsM652 | SF_IsM653))
#define IS_SIS65x               (IS_SIS651 || IS_SISM650)       /* Only special versions of 65x */
#define IS_SIS661		(SiS_Pr->ChipType == SIS_661)
#define IS_SIS741		(SiS_Pr->ChipType == SIS_741)
#define IS_SIS660		(SiS_Pr->ChipType == SIS_660)
#define IS_SIS760		(SiS_Pr->ChipType == SIS_760)
#define IS_SIS761		(SiS_Pr->ChipType == SIS_761)
#define IS_SIS661741660760	(IS_SIS661 || IS_SIS741 || IS_SIS660 || IS_SIS760 || IS_SIS761)
#define IS_SIS650740            ((SiS_Pr->ChipType >= SIS_650) && (SiS_Pr->ChipType < SIS_330))
#define IS_SIS550650740         (IS_SIS550 || IS_SIS650740)
#define IS_SIS650740660         (IS_SIS650 || IS_SIS740 || IS_SIS661741660760)
#define IS_SIS550650740660      (IS_SIS550 || IS_SIS650740660)

#define SISGETROMW(x)		(ROMAddr[(x)] | (ROMAddr[(x)+1] << 8))

/* SiS_VBType */
#define VB_SIS301		0x0001
#define VB_SIS301B		0x0002
#define VB_SIS302B		0x0004
#define VB_SIS301LV		0x0008
#define VB_SIS302LV		0x0010
#define VB_SIS302ELV		0x0020
#define VB_SIS301C		0x0040
#define VB_SIS307T		0x0080
#define VB_SIS307LV		0x0100
#define VB_UMC			0x4000
#define VB_NoLCD        	0x8000
#define VB_SIS30xB		(VB_SIS301B | VB_SIS301C | VB_SIS302B | VB_SIS307T)
#define VB_SIS30xC		(VB_SIS301C | VB_SIS307T)
#define VB_SISTMDS		(VB_SIS301 | VB_SIS301B | VB_SIS301C | VB_SIS302B | VB_SIS307T)
#define VB_SISLVDS		(VB_SIS301LV | VB_SIS302LV | VB_SIS302ELV | VB_SIS307LV)
#define VB_SIS30xBLV		(VB_SIS30xB | VB_SISLVDS)
#define VB_SIS30xCLV		(VB_SIS30xC | VB_SIS302ELV | VB_SIS307LV)
#define VB_SISVB		(VB_SIS301 | VB_SIS30xBLV)
#define VB_SISLCDA		(VB_SIS302B | VB_SIS301C  | VB_SIS307T  | VB_SISLVDS)
#define VB_SISTMDSLCDA		(VB_SIS301C | VB_SIS307T)
#define VB_SISPART4SCALER	(VB_SIS301C | VB_SIS307T | VB_SIS302ELV | VB_SIS307LV)
#define VB_SISHIVISION		(VB_SIS301 | VB_SIS301B | VB_SIS302B)
#define VB_SISYPBPR		(VB_SIS301C | VB_SIS307T  | VB_SIS301LV | VB_SIS302LV | VB_SIS302ELV | VB_SIS307LV)
#define VB_SISTAP4SCALER	(VB_SIS301C | VB_SIS307T | VB_SIS302ELV | VB_SIS307LV)
#define VB_SISPART4OVERFLOW	(VB_SIS301C | VB_SIS307T | VB_SIS302LV | VB_SIS302ELV | VB_SIS307LV)
#define VB_SISPWD		(VB_SIS301C | VB_SIS307T | VB_SISLVDS)
#define VB_SISEMI		(VB_SIS302LV | VB_SIS302ELV | VB_SIS307LV)
#define VB_SISPOWER		(VB_SIS301C | VB_SIS307T | VB_SIS302LV | VB_SIS302ELV | VB_SIS307LV)
#define VB_SISDUALLINK		(VB_SIS302LV | VB_SIS302ELV | VB_SIS307T | VB_SIS307LV)
#define VB_SISVGA2		VB_SISTMDS
#define VB_SISRAMDAC202		(VB_SIS301C | VB_SIS307T)

/* VBInfo */
#define SetSimuScanMode         0x0001   /* CR 30 */
#define SwitchCRT2              0x0002
#define SetCRT2ToAVIDEO         0x0004
#define SetCRT2ToSVIDEO         0x0008
#define SetCRT2ToSCART          0x0010
#define SetCRT2ToLCD            0x0020
#define SetCRT2ToRAMDAC         0x0040
#define SetCRT2ToHiVision       0x0080   		/* for SiS bridge */
#define SetCRT2ToCHYPbPr       	SetCRT2ToHiVision	/* for Chrontel   */
#define SetNTSCTV               0x0000   /* CR 31 */
#define SetPALTV                0x0100   		/* Deprecated here, now in TVMode */
#define SetInSlaveMode          0x0200
#define SetNotSimuMode          0x0400
#define SetNotSimuTVMode        SetNotSimuMode
#define SetDispDevSwitch        0x0800
#define SetCRT2ToYPbPr525750    0x0800
#define LoadDACFlag             0x1000
#define DisableCRT2Display      0x2000
#define DriverMode              0x4000
#define HotKeySwitch            0x8000
#define SetCRT2ToLCDA           0x8000

/* v-- Needs change in sis_vga.c if changed (GPIO) --v */
#define SetCRT2ToTV             (SetCRT2ToYPbPr525750|SetCRT2ToHiVision|SetCRT2ToSCART|SetCRT2ToSVIDEO|SetCRT2ToAVIDEO)
#define SetCRT2ToTVNoYPbPrHiVision (SetCRT2ToSCART | SetCRT2ToSVIDEO | SetCRT2ToAVIDEO)
#define SetCRT2ToTVNoHiVision  	(SetCRT2ToYPbPr525750 | SetCRT2ToSCART | SetCRT2ToSVIDEO | SetCRT2ToAVIDEO)

/* SiS_ModeType */
#define ModeText                0x00
#define ModeCGA                 0x01
#define ModeEGA                 0x02
#define ModeVGA                 0x03
#define Mode15Bpp               0x04
#define Mode16Bpp               0x05
#define Mode24Bpp               0x06
#define Mode32Bpp               0x07

#define ModeTypeMask            0x07
#define IsTextMode              0x07

#define DACInfoFlag             0x0018
#define MemoryInfoFlag          0x01E0
#define MemorySizeShift         5

/* modeflag */
#define Charx8Dot               0x0200
#define LineCompareOff          0x0400
#define CRT2Mode                0x0800
#define HalfDCLK                0x1000
#define NoSupportSimuTV         0x2000
#define NoSupportLCDScale	0x4000 /* SiS bridge: No scaling possible (no matter what panel) */
#define DoubleScanMode          0x8000

/* Infoflag */
#define SupportTV               0x0008
#define SupportTV1024           0x0800
#define SupportCHTV 		0x0800
#define Support64048060Hz       0x0800  /* Special for 640x480 LCD */
#define SupportHiVision         0x0010
#define SupportYPbPr750p        0x1000
#define SupportLCD              0x0020
#define SupportRAMDAC2          0x0040	/* All           (<= 100Mhz) */
#define SupportRAMDAC2_135      0x0100  /* All except DH (<= 135Mhz) */
#define SupportRAMDAC2_162      0x0200  /* B, C          (<= 162Mhz) */
#define SupportRAMDAC2_202      0x0400  /* C             (<= 202Mhz) */
#define InterlaceMode           0x0080
#define SyncPP                  0x0000
#define HaveWideTiming		0x2000	/* Have specific wide- and non-wide timing */
#define SyncPN                  0x4000
#define SyncNP                  0x8000
#define SyncNN                  0xc000

/* SetFlag */
#define ProgrammingCRT2         0x0001
#define LowModeTests		0x0002
/* #define TVSimuMode           0x0002 - deprecated */
/* #define RPLLDIV2XO           0x0004 - deprecated */
#define LCDVESATiming           0x0008
#define EnableLVDSDDA           0x0010
#define SetDispDevSwitchFlag    0x0020
#define CheckWinDos             0x0040
#define SetDOSMode              0x0080

/* TVMode flag */
#define TVSetPAL		0x0001
#define TVSetNTSCJ		0x0002
#define TVSetPALM		0x0004
#define TVSetPALN		0x0008
#define TVSetCHOverScan		0x0010
#define TVSetYPbPr525i		0x0020 /* new 0x10 */
#define TVSetYPbPr525p		0x0040 /* new 0x20 */
#define TVSetYPbPr750p		0x0080 /* new 0x40 */
#define TVSetHiVision		0x0100 /* new 0x80; = 1080i, software-wise identical */
#define TVSetTVSimuMode		0x0200 /* new 0x200, prev. 0x800 */
#define TVRPLLDIV2XO		0x0400 /* prev 0x1000 */
#define TVSetNTSC1024		0x0800 /* new 0x100, prev. 0x2000 */
#define TVSet525p1024		0x1000 /* TW */
#define TVAspect43		0x2000
#define TVAspect169		0x4000
#define TVAspect43LB		0x8000

/* YPbPr flag (>=315, <661; converted to TVMode) */
#define YPbPr525p               0x0001
#define YPbPr750p               0x0002
#define YPbPr525i               0x0004
#define YPbPrHiVision           0x0008
#define YPbPrModeMask           (YPbPr750p | YPbPr525p | YPbPr525i | YPbPrHiVision)

/* SysFlags (to identify special versions) */
#define SF_Is651                0x0001
#define SF_IsM650               0x0002
#define SF_Is652		0x0004
#define SF_IsM652		0x0008
#define SF_IsM653		0x0010
#define SF_IsM661		0x0020
#define SF_IsM741		0x0040
#define SF_IsM760		0x0080
#define SF_760UMA		0x4000  /* 76x: We have UMA */
#define SF_760LFB		0x8000  /* 76x: We have LFB */

/* CR32 (Newer 630, and 315 series)

   [0]   VB connected with CVBS
   [1]   VB connected with SVHS
   [2]   VB connected with SCART
   [3]   VB connected with LCD
   [4]   VB connected with CRT2 (secondary VGA)
   [5]   CRT1 monitor is connected
   [6]   VB connected with Hi-Vision TV
   [7]   <= 330: VB connected with DVI combo connector
         >= 661: VB connected to YPbPr
*/

/* CR35 (300 series only) */
#define TVOverScan              0x10
#define TVOverScanShift         4

/* CR35 (661 series only)
   [0]    1 = PAL, 0 = NTSC
   [1]    1 = NTSC-J (if D0 = 0)
   [2]    1 = PALM (if D0 = 1)
   [3]    1 = PALN (if D0 = 1)
   [4]    1 = Overscan (Chrontel only)
   [7:5]  (only if D2 in CR38 is set)
	  000  525i
	  001  525p
	  010  750p
	  011  1080i (or HiVision on 301, 301B)
*/

/* CR37
   [0]   Set 24/18 bit (0/1) RGB to LVDS/TMDS transmitter (set by BIOS)
   [3:1] External chip
         300 series:
	    001   SiS301 (never seen)
	    010   LVDS
	    011   LVDS + Tumpion Zurac
	    100   LVDS + Chrontel 7005
	    110   Chrontel 7005
	  315/330 series
	    001   SiS30x (never seen)
	    010   LVDS
	    011   LVDS + Chrontel 7019
	  660 series [2:1] only:
	     reserved (chip type now in CR38)
	  All other combinations reserved
   [3]    661 only: Pass 1:1 data
   [4]    LVDS: 0: Panel Link expands / 1: Panel Link does not expand
          30x:  0: Bridge scales      / 1: Bridge does not scale = Panel scales (if possible)
   [5]    LCD polarity select
          0: VESA DMT Standard
	  1: EDID 2.x defined
   [6]    LCD horizontal polarity select
          0: High active
	  1: Low active
   [7]    LCD vertical polarity select
          0: High active
	  1: Low active
*/

/* CR37: LCDInfo */
#define LCDRGB18Bit           0x0001
#define LCDNonExpanding       0x0010
#define LCDSync               0x0020
#define LCDPass11             0x0100   /* 0: center screen, 1: Pass 1:1 data */
#define LCDDualLink	      0x0200

#define DontExpandLCD	      LCDNonExpanding
#define LCDNonExpandingShift       4
#define DontExpandLCDShift    LCDNonExpandingShift
#define LCDSyncBit            0x00e0
#define LCDSyncShift               6

/* CR38 (315 series) */
#define EnableDualEdge 		0x01
#define SetToLCDA		0x02   /* LCD channel A (301C/302B/30x(E)LV and 650+LVDS only) */
#define EnableCHScart           0x04   /* Scart on Ch7019 (unofficial definition - TW) */
#define EnableCHYPbPr           0x08   /* YPbPr on Ch7019 (480i HDTV); only on 650/Ch7019 systems */
#define EnableSiSYPbPr          0x08   /* Enable YPbPr mode (30xLV/301C only) */
#define EnableYPbPr525i         0x00   /* Enable 525i YPbPr mode (30xLV/301C only) (mask 0x30) */
#define EnableYPbPr525p         0x10   /* Enable 525p YPbPr mode (30xLV/301C only) (mask 0x30) */
#define EnableYPbPr750p         0x20   /* Enable 750p YPbPr mode (30xLV/301C only) (mask 0x30) */
#define EnableYPbPr1080i        0x30   /* Enable 1080i YPbPr mode (30xLV/301C only) (mask 0x30) */
#define EnablePALM              0x40   /* 1 = Set PALM */
#define EnablePALN              0x80   /* 1 = Set PALN */
#define EnableNTSCJ             EnablePALM  /* Not BIOS */

/* CR38 (661 and later)
  D[7:5]  000 No VB
          001 301 series VB
	  010 LVDS
	  011 Chrontel 7019
	  100 Conexant
  D2      Enable YPbPr output (see CR35)
  D[1:0]  LCDA (like before)
*/

#define EnablePALMN             0x40   /* Romflag: 1 = Allow PALM/PALN */

/* CR39 (650 only) */
#define LCDPass1_1		0x01   /* 0: center screen, 1: pass 1:1 data output  */
#define Enable302LV_DualLink    0x04   /* 302LV only; enable dual link */

/* CR39 (661 and later)
   D[7]   LVDS (SiS or third party)
   D[1:0] YPbPr Aspect Ratio
          00 4:3 letterbox
	  01 4:3
	  10 16:9
	  11 4:3
*/

/* CR3B (651+301C)
   D[1:0] YPbPr Aspect Ratio
          ?
*/

/* CR79 (315/330 series only; not 661 and later)
   [3-0] Notify driver
         0001 Mode Switch event (set by BIOS)
	 0010 Epansion On/Off event
	 0011 TV UnderScan/OverScan event
	 0100 Set Brightness event
	 0101 Set Contrast event
	 0110 Set Mute event
	 0111 Set Volume Up/Down event
   [4]   Enable Backlight Control by BIOS/driver
         (set by driver; set means that the BIOS should
	 not touch the backlight registers because eg.
	 the driver already switched off the backlight)
   [5]   PAL/NTSC (set by BIOS)
   [6]   Expansion On/Off (set by BIOS; copied to CR32[4])
   [7]   TV UnderScan/OverScan (set by BIOS)
*/

/* CR7C - 661 and later
   [7]   DualEdge enabled (or: to be enabled)
   [6]   CRT2 = TV/LCD/VGA enabled (or: to be enabled)
   [5]   Init done (set at end of SiS_Init)
   {4]   LVDS LCD capabilities
   [3]   LVDS LCD capabilities
   [2]   LVDS LCD capabilities (PWD)
   [1]   LVDS LCD capabilities (PWD)
   [0]   LVDS=1, TMDS=0 (SiS or third party)
*/

/* CR7E - 661 and later
   VBType:
   [7] LVDS (third party)
   [3] 301C
   [2] 302LV
   [1] 301LV
   [0] 301B
*/

/* LCDResInfo */
#define Panel300_800x600        0x01	/* CR36 */
#define Panel300_1024x768       0x02
#define Panel300_1280x1024      0x03
#define Panel300_1280x960       0x04
#define Panel300_640x480        0x05
#define Panel300_1024x600       0x06
#define Panel300_1152x768       0x07
#define Panel300_1280x768       0x0a
#define Panel300_Custom		0x0f
#define Panel300_Barco1366      0x10

#define Panel310_800x600        0x01
#define Panel310_1024x768       0x02
#define Panel310_1280x1024      0x03
#define Panel310_640x480        0x04
#define Panel310_1024x600       0x05
#define Panel310_1152x864       0x06
#define Panel310_1280x960       0x07
#define Panel310_1152x768       0x08	/* LVDS only */
#define Panel310_1400x1050      0x09
#define Panel310_1280x768       0x0a
#define Panel310_1600x1200      0x0b
#define Panel310_320x240_2      0x0c    /* xSTN */
#define Panel310_320x240_3      0x0d    /* xSTN */
#define Panel310_320x240_1      0x0e    /* xSTN - This is fake, can be any */
#define Panel310_Custom		0x0f

#define Panel661_800x600        0x01
#define Panel661_1024x768       0x02
#define Panel661_1280x1024      0x03
#define Panel661_640x480        0x04
#define Panel661_1024x600       0x05
#define Panel661_1152x864       0x06
#define Panel661_1280x960       0x07
#define Panel661_1280x854       0x08
#define Panel661_1400x1050      0x09
#define Panel661_1280x768       0x0a
#define Panel661_1600x1200      0x0b
#define Panel661_1280x800       0x0c
#define Panel661_1680x1050      0x0d
#define Panel661_1280x720       0x0e
#define Panel661_Custom		0x0f

#define Panel_800x600           0x01	/* Unified values */
#define Panel_1024x768          0x02    /* MUST match BIOS values from 0-e */
#define Panel_1280x1024         0x03
#define Panel_640x480           0x04
#define Panel_1024x600          0x05
#define Panel_1152x864          0x06
#define Panel_1280x960          0x07
#define Panel_1152x768          0x08	/* LVDS only */
#define Panel_1400x1050         0x09
#define Panel_1280x768          0x0a    /* 30xB/C and LVDS only (BIOS: all) */
#define Panel_1600x1200         0x0b
#define Panel_1280x800		0x0c    /* 661etc (TMDS) */
#define Panel_1680x1050         0x0d    /* 661etc  */
#define Panel_1280x720		0x0e    /* 661etc  */
#define Panel_Custom		0x0f	/* MUST BE 0x0f (for DVI DDC detection) */
#define Panel_320x240_1         0x10    /* SiS 550 xSTN */
#define Panel_Barco1366         0x11
#define Panel_848x480		0x12
#define Panel_320x240_2		0x13    /* SiS 550 xSTN */
#define Panel_320x240_3		0x14    /* SiS 550 xSTN */
#define Panel_1280x768_2        0x15	/* 30xLV */
#define Panel_1280x768_3        0x16    /* (unused) */
#define Panel_1280x800_2	0x17    /* 30xLV */
#define Panel_856x480		0x18
#define Panel_1280x854		0x19	/* 661etc */

/* Index in ModeResInfo table */
#define SIS_RI_320x200    0
#define SIS_RI_320x240    1
#define SIS_RI_320x400    2
#define SIS_RI_400x300    3
#define SIS_RI_512x384    4
#define SIS_RI_640x400    5
#define SIS_RI_640x480    6
#define SIS_RI_800x600    7
#define SIS_RI_1024x768   8
#define SIS_RI_1280x1024  9
#define SIS_RI_1600x1200 10
#define SIS_RI_1920x1440 11
#define SIS_RI_2048x1536 12
#define SIS_RI_720x480   13
#define SIS_RI_720x576   14
#define SIS_RI_1280x960  15
#define SIS_RI_800x480   16
#define SIS_RI_1024x576  17
#define SIS_RI_1280x720  18
#define SIS_RI_856x480   19
#define SIS_RI_1280x768  20
#define SIS_RI_1400x1050 21
#define SIS_RI_1152x864  22  /* Up to here SiS conforming */
#define SIS_RI_848x480   23
#define SIS_RI_1360x768  24
#define SIS_RI_1024x600  25
#define SIS_RI_1152x768  26
#define SIS_RI_768x576   27
#define SIS_RI_1360x1024 28
#define SIS_RI_1680x1050 29
#define SIS_RI_1280x800  30
#define SIS_RI_1920x1080 31
#define SIS_RI_960x540   32
#define SIS_RI_960x600   33
#define SIS_RI_1280x854  34

/* CR5F */
#define IsM650                  0x80

/* Timing data */
#define NTSCHT                  1716
#define NTSC2HT                 1920
#define NTSCVT                  525
#define PALHT                   1728
#define PALVT                   625
#define StHiTVHT                892
#define StHiTVVT                1126
#define StHiTextTVHT            1000
#define StHiTextTVVT            1126
#define ExtHiTVHT               2100
#define ExtHiTVVT               1125

/* Indices in (VB)VCLKData tables */

#define VCLK28                  0x00   /* Index in VCLKData table (300 and 315) */
#define VCLK40                  0x04   /* Index in VCLKData table (300 and 315) */
#define VCLK65_300              0x09   /* Index in VCLKData table (300) */
#define VCLK108_2_300           0x14   /* Index in VCLKData table (300) */
#define VCLK81_300		0x3f   /* Index in VCLKData table (300) */
#define VCLK108_3_300           0x42   /* Index in VCLKData table (300) */
#define VCLK100_300             0x43   /* Index in VCLKData table (300) */
#define VCLK34_300              0x3d   /* Index in VCLKData table (300) */
#define VCLK_CUSTOM_300		0x47

#define VCLK65_315              0x0b   /* Indices in (VB)VCLKData table (315) */
#define VCLK108_2_315           0x19
#define VCLK81_315		0x5b
#define VCLK162_315             0x5e
#define VCLK108_3_315           0x45
#define VCLK100_315             0x46
#define VCLK34_315              0x55
#define VCLK68_315		0x0d
#define VCLK_1280x800_315_2	0x5c
#define VCLK121_315		0x5d
#define VCLK130_315		0x72
#define VCLK_1280x720		0x5f
#define VCLK_1280x768_2		0x60
#define VCLK_1280x768_3		0x61   /* (unused?) */
#define VCLK_CUSTOM_315		0x62
#define VCLK_1280x720_2		0x63
#define VCLK_720x480		0x67
#define VCLK_720x576		0x68
#define VCLK_768x576		0x68
#define VCLK_848x480		0x65
#define VCLK_856x480		0x66
#define VCLK_800x480		0x65
#define VCLK_1024x576		0x51
#define VCLK_1152x864		0x64
#define VCLK_1360x768		0x58
#define VCLK_1280x800_315	0x6c
#define VCLK_1280x854		0x76

#define TVCLKBASE_300		0x21   /* Indices on TV clocks in VCLKData table (300) */
#define TVCLKBASE_315	        0x3a   /* Indices on TV clocks in (VB)VCLKData table (315) */
#define TVVCLKDIV2              0x00   /* Index relative to TVCLKBASE */
#define TVVCLK                  0x01   /* Index relative to TVCLKBASE */
#define HiTVVCLKDIV2            0x02   /* Index relative to TVCLKBASE */
#define HiTVVCLK                0x03   /* Index relative to TVCLKBASE */
#define HiTVSimuVCLK            0x04   /* Index relative to TVCLKBASE */
#define HiTVTextVCLK            0x05   /* Index relative to TVCLKBASE */
#define YPbPr750pVCLK		0x25   /* Index relative to TVCLKBASE; was 0x0f NOT relative */

/* ------------------------------ */

#define SetSCARTOutput          0x01

#define HotPlugFunction         0x08

#define StStructSize            0x06

#define SIS_VIDEO_CAPTURE       0x00 - 0x30
#define SIS_VIDEO_PLAYBACK      0x02 - 0x30
#define SIS_CRT2_PORT_04        0x04 - 0x30
#define SIS_CRT2_PORT_10        0x10 - 0x30
#define SIS_CRT2_PORT_12        0x12 - 0x30
#define SIS_CRT2_PORT_14        0x14 - 0x30

#define ADR_CRT2PtrData         0x20E
#define offset_Zurac            0x210   /* TW: Trumpion Zurac data pointer */
#define ADR_LVDSDesPtrData      0x212
#define ADR_LVDSCRT1DataPtr     0x214
#define ADR_CHTVVCLKPtr         0x216
#define ADR_CHTVRegDataPtr      0x218

#define LCDDataLen              8
#define HiTVDataLen             12
#define TVDataLen               16

#define LVDSDataLen             6
#define LVDSDesDataLen          3
#define ActiveNonExpanding      0x40
#define ActiveNonExpandingShift 6
#define ActivePAL               0x20
#define ActivePALShift          5
#define ModeSwitchStatus        0x0F
#define SoftTVType              0x40
#define SoftSettingAddr         0x52
#define ModeSettingAddr         0x53

#define _PanelType00             0x00
#define _PanelType01             0x08
#define _PanelType02             0x10
#define _PanelType03             0x18
#define _PanelType04             0x20
#define _PanelType05             0x28
#define _PanelType06             0x30
#define _PanelType07             0x38
#define _PanelType08             0x40
#define _PanelType09             0x48
#define _PanelType0A             0x50
#define _PanelType0B             0x58
#define _PanelType0C             0x60
#define _PanelType0D             0x68
#define _PanelType0E             0x70
#define _PanelType0F             0x78

#define PRIMARY_VGA       	0     /* 1: SiS is primary vga 0:SiS is secondary vga */

#define BIOSIDCodeAddr          0x235  /* Offsets to ptrs in BIOS image */
#define OEMUtilIDCodeAddr       0x237
#define VBModeIDTableAddr       0x239
#define OEMTVPtrAddr            0x241
#define PhaseTableAddr          0x243
#define NTSCFilterTableAddr     0x245
#define PALFilterTableAddr      0x247
#define OEMLCDPtr_1Addr         0x249
#define OEMLCDPtr_2Addr         0x24B
#define LCDHPosTable_1Addr      0x24D
#define LCDHPosTable_2Addr      0x24F
#define LCDVPosTable_1Addr      0x251
#define LCDVPosTable_2Addr      0x253
#define OEMLCDPIDTableAddr      0x255

#define VBModeStructSize        5
#define PhaseTableSize          4
#define FilterTableSize         4
#define LCDHPosTableSize        7
#define LCDVPosTableSize        5
#define OEMLVDSPIDTableSize     4
#define LVDSHPosTableSize       4
#define LVDSVPosTableSize       6

#define VB_ModeID               0
#define VB_TVTableIndex         1
#define VB_LCDTableIndex        2
#define VB_LCDHIndex            3
#define VB_LCDVIndex            4

#define OEMLCDEnable            0x0001
#define OEMLCDDelayEnable       0x0002
#define OEMLCDPOSEnable         0x0004
#define OEMTVEnable             0x0100
#define OEMTVDelayEnable        0x0200
#define OEMTVFlickerEnable      0x0400
#define OEMTVPhaseEnable        0x0800
#define OEMTVFilterEnable       0x1000

#define OEMLCDPanelIDSupport    0x0080

/*
  =============================================================
   		  for 315 series (old data layout)
  =============================================================
*/
#define SoftDRAMType        0x80
#define SoftSetting_OFFSET  0x52
#define SR07_OFFSET  0x7C
#define SR15_OFFSET  0x7D
#define SR16_OFFSET  0x81
#define SR17_OFFSET  0x85
#define SR19_OFFSET  0x8D
#define SR1F_OFFSET  0x99
#define SR21_OFFSET  0x9A
#define SR22_OFFSET  0x9B
#define SR23_OFFSET  0x9C
#define SR24_OFFSET  0x9D
#define SR25_OFFSET  0x9E
#define SR31_OFFSET  0x9F
#define SR32_OFFSET  0xA0
#define SR33_OFFSET  0xA1

#define CR40_OFFSET  0xA2
#define SR25_1_OFFSET  0xF6
#define CR49_OFFSET  0xF7

#define VB310Data_1_2_Offset  0xB6
#define VB310Data_4_D_Offset  0xB7
#define VB310Data_4_E_Offset  0xB8
#define VB310Data_4_10_Offset 0xBB

#define RGBSenseDataOffset    0xBD
#define YCSenseDataOffset     0xBF
#define VideoSenseDataOffset  0xC1
#define OutputSelectOffset    0xF3

#define ECLK_MCLK_DISTANCE  0x14
#define VBIOSTablePointerStart    0x100
#define StandTablePtrOffset       VBIOSTablePointerStart+0x02
#define EModeIDTablePtrOffset     VBIOSTablePointerStart+0x04
#define CRT1TablePtrOffset        VBIOSTablePointerStart+0x06
#define ScreenOffsetPtrOffset     VBIOSTablePointerStart+0x08
#define VCLKDataPtrOffset         VBIOSTablePointerStart+0x0A
#define MCLKDataPtrOffset         VBIOSTablePointerStart+0x0E
#define CRT2PtrDataPtrOffset      VBIOSTablePointerStart+0x10
#define TVAntiFlickPtrOffset      VBIOSTablePointerStart+0x12
#define TVDelayPtr1Offset         VBIOSTablePointerStart+0x14
#define TVPhaseIncrPtr1Offset     VBIOSTablePointerStart+0x16
#define TVYFilterPtr1Offset       VBIOSTablePointerStart+0x18
#define LCDDelayPtr1Offset        VBIOSTablePointerStart+0x20
#define TVEdgePtr1Offset          VBIOSTablePointerStart+0x24
#define CRT2Delay1Offset          VBIOSTablePointerStart+0x28

#endif
