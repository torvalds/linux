/*
 * SiS 300/540/630[S]/730[S]
 * SiS 315[E|PRO]/550/[M]65x/[M]66x[F|M|G]X/[M]74x[GX]/330/[M]76x[GX]
 * XGI V3XT/V5/V8, Z7
 * frame buffer driver for Linux kernels >= 2.4.14 and >=2.6.3
 *
 * Linux kernel specific extensions to init.c/init301.c
 *
 * Copyright (C) 2001-2005 Thomas Winischhofer, Vienna, Austria.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:	Thomas Winischhofer <thomas@winischhofer.net>
 */

#include "osdef.h"
#include "initdef.h"
#include "vgatypes.h"
#include "vstruct.h"

#include <linux/version.h>
#include <linux/types.h>
#include <linux/fb.h>

int		sisfb_mode_rate_to_dclock(struct SiS_Private *SiS_Pr,
			unsigned char modeno, unsigned char rateindex);
int		sisfb_mode_rate_to_ddata(struct SiS_Private *SiS_Pr, unsigned char modeno,
			unsigned char rateindex, struct fb_var_screeninfo *var);
bool		sisfb_gettotalfrommode(struct SiS_Private *SiS_Pr, unsigned char modeno,
			int *htotal, int *vtotal, unsigned char rateindex);

extern bool	SiSInitPtr(struct SiS_Private *SiS_Pr);
extern bool	SiS_SearchModeID(struct SiS_Private *SiS_Pr, unsigned short *ModeNo,
			unsigned short *ModeIdIndex);
extern void	SiS_Generic_ConvertCRData(struct SiS_Private *SiS_Pr, unsigned char *crdata,
			int xres, int yres, struct fb_var_screeninfo *var, bool writeres);

int
sisfb_mode_rate_to_dclock(struct SiS_Private *SiS_Pr, unsigned char modeno,
			unsigned char rateindex)
{
    unsigned short ModeNo = modeno;
    unsigned short ModeIdIndex = 0, ClockIndex = 0;
    unsigned short RRTI = 0;
    int Clock;

    if(!SiSInitPtr(SiS_Pr)) return 65000;

    if(rateindex > 0) rateindex--;

#ifdef SIS315H
    switch(ModeNo) {
    case 0x5a: ModeNo = 0x50; break;
    case 0x5b: ModeNo = 0x56;
    }
#endif

    if(!(SiS_SearchModeID(SiS_Pr, &ModeNo, &ModeIdIndex))) {;
       printk(KERN_ERR "Could not find mode %x\n", ModeNo);
       return 65000;
    }

    RRTI = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].REFindex;

    if(SiS_Pr->SiS_RefIndex[RRTI].Ext_InfoFlag & HaveWideTiming) {
       if(SiS_Pr->SiS_UseWide == 1) {
	  /* Wide screen: Ignore rateindex */
	  ClockIndex = SiS_Pr->SiS_RefIndex[RRTI].Ext_CRTVCLK_WIDE;
       } else {
	  RRTI += rateindex;
	  ClockIndex = SiS_Pr->SiS_RefIndex[RRTI].Ext_CRTVCLK_NORM;
       }
    } else {
       RRTI += rateindex;
       ClockIndex = SiS_Pr->SiS_RefIndex[RRTI].Ext_CRTVCLK;
    }

    Clock = SiS_Pr->SiS_VCLKData[ClockIndex].CLOCK * 1000;

    return Clock;
}

int
sisfb_mode_rate_to_ddata(struct SiS_Private *SiS_Pr, unsigned char modeno,
			unsigned char rateindex, struct fb_var_screeninfo *var)
{
    unsigned short ModeNo = modeno;
    unsigned short ModeIdIndex = 0, index = 0, RRTI = 0;
    int            j;

    if(!SiSInitPtr(SiS_Pr)) return 0;

    if(rateindex > 0) rateindex--;

#ifdef SIS315H
    switch(ModeNo) {
       case 0x5a: ModeNo = 0x50; break;
       case 0x5b: ModeNo = 0x56;
    }
#endif

    if(!(SiS_SearchModeID(SiS_Pr, &ModeNo, &ModeIdIndex))) return 0;

    RRTI = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].REFindex;
    if(SiS_Pr->SiS_RefIndex[RRTI].Ext_InfoFlag & HaveWideTiming) {
       if(SiS_Pr->SiS_UseWide == 1) {
	  /* Wide screen: Ignore rateindex */
	  index = SiS_Pr->SiS_RefIndex[RRTI].Ext_CRT1CRTC_WIDE;
       } else {
	  RRTI += rateindex;
	  index = SiS_Pr->SiS_RefIndex[RRTI].Ext_CRT1CRTC_NORM;
       }
    } else {
       RRTI += rateindex;
       index = SiS_Pr->SiS_RefIndex[RRTI].Ext_CRT1CRTC;
    }

    SiS_Generic_ConvertCRData(SiS_Pr,
			(unsigned char *)&SiS_Pr->SiS_CRT1Table[index].CR[0],
			SiS_Pr->SiS_RefIndex[RRTI].XRes,
			SiS_Pr->SiS_RefIndex[RRTI].YRes,
			var, false);

    if(SiS_Pr->SiS_RefIndex[RRTI].Ext_InfoFlag & 0x8000)
       var->sync &= ~FB_SYNC_VERT_HIGH_ACT;
    else
       var->sync |= FB_SYNC_VERT_HIGH_ACT;

    if(SiS_Pr->SiS_RefIndex[RRTI].Ext_InfoFlag & 0x4000)
       var->sync &= ~FB_SYNC_HOR_HIGH_ACT;
    else
       var->sync |= FB_SYNC_HOR_HIGH_ACT;

    var->vmode = FB_VMODE_NONINTERLACED;
    if(SiS_Pr->SiS_RefIndex[RRTI].Ext_InfoFlag & 0x0080)
       var->vmode = FB_VMODE_INTERLACED;
    else {
       j = 0;
       while(SiS_Pr->SiS_EModeIDTable[j].Ext_ModeID != 0xff) {
	  if(SiS_Pr->SiS_EModeIDTable[j].Ext_ModeID ==
	                  SiS_Pr->SiS_RefIndex[RRTI].ModeID) {
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

bool
sisfb_gettotalfrommode(struct SiS_Private *SiS_Pr, unsigned char modeno, int *htotal,
			int *vtotal, unsigned char rateindex)
{
    unsigned short ModeNo = modeno;
    unsigned short ModeIdIndex = 0, CRT1Index = 0;
    unsigned short RRTI = 0;
    unsigned char  sr_data, cr_data, cr_data2;

    if(!SiSInitPtr(SiS_Pr)) return false;

    if(rateindex > 0) rateindex--;

#ifdef SIS315H
    switch(ModeNo) {
       case 0x5a: ModeNo = 0x50; break;
       case 0x5b: ModeNo = 0x56;
    }
#endif

    if(!(SiS_SearchModeID(SiS_Pr, &ModeNo, &ModeIdIndex))) return false;

    RRTI = SiS_Pr->SiS_EModeIDTable[ModeIdIndex].REFindex;
    if(SiS_Pr->SiS_RefIndex[RRTI].Ext_InfoFlag & HaveWideTiming) {
       if(SiS_Pr->SiS_UseWide == 1) {
	  /* Wide screen: Ignore rateindex */
	  CRT1Index = SiS_Pr->SiS_RefIndex[RRTI].Ext_CRT1CRTC_WIDE;
       } else {
	  RRTI += rateindex;
	  CRT1Index = SiS_Pr->SiS_RefIndex[RRTI].Ext_CRT1CRTC_NORM;
       }
    } else {
       RRTI += rateindex;
       CRT1Index = SiS_Pr->SiS_RefIndex[RRTI].Ext_CRT1CRTC;
    }

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

    if(SiS_Pr->SiS_RefIndex[RRTI].Ext_InfoFlag & InterlaceMode)
       *vtotal *= 2;

    return true;
}



