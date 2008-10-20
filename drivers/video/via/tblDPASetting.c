/*
 * Copyright 1998-2008 VIA Technologies, Inc. All Rights Reserved.
 * Copyright 2001-2008 S3 Graphics, Inc. All Rights Reserved.

 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation;
 * either version 2, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTIES OR REPRESENTATIONS; without even
 * the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.See the GNU General Public License
 * for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "global.h"
/* For VT3324: */
struct VT1636_DPA_SETTING VT1636_DPA_SETTING_TBL_VT3324[] = {
	/* Panel ID,       CLK_SEL_ST1[09],    CLK_SEL_ST2[08] */
	{LCD_PANEL_ID0_640X480, 0x00, 0x00},	/* For 640x480   */
	{LCD_PANEL_ID1_800X600, 0x00, 0x00},	/* For 800x600   */
	{LCD_PANEL_ID2_1024X768, 0x00, 0x00},	/* For 1024x768  */
	{LCD_PANEL_ID3_1280X768, 0x00, 0x00},	/* For 1280x768  */
	{LCD_PANEL_ID4_1280X1024, 0x00, 0x00},	/* For 1280x1024 */
	{LCD_PANEL_ID5_1400X1050, 0x00, 0x00},	/* For 1400x1050 */
	{LCD_PANEL_ID6_1600X1200, 0x0B, 0x03}	/* For 1600x1200 */
};

struct GFX_DPA_SETTING GFX_DPA_SETTING_TBL_VT3324[] = {
/*  ClkRange, DVP0, DVP0DataDriving,  DVP0ClockDriving, DVP1,
					DVP1Driving, DFPHigh, DFPLow */
/*  CR96, SR2A[5], SR1B[1], SR2A[4], SR1E[2], CR9B,
					SR65,        CR97,    CR99   */
	/* LCK/VCK < 30000000 will use this value */
	{DPA_CLK_RANGE_30M, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
	 0x00},
	/* 30000000 < LCK/VCK < 50000000 will use this value */
	{DPA_CLK_RANGE_30_50M, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00,
	 0x00},
	/* 50000000 < LCK/VCK < 70000000 will use this value */
	{DPA_CLK_RANGE_50_70M, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
	 0x00},
	/* 70000000 < LCK/VCK < 100000000 will use this value */
	{DPA_CLK_RANGE_70_100M, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
	 0x00},
	/* 100000000 < LCK/VCK < 15000000 will use this value */
	{DPA_CLK_RANGE_100_150M, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00,
	 0x00},
	/* 15000000 < LCK/VCK will use this value */
	{DPA_CLK_RANGE_150M, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x0E, 0x00,
	 0x00},
};

/* For VT3327: */
struct VT1636_DPA_SETTING VT1636_DPA_SETTING_TBL_VT3327[] = {
	/* Panel ID,       CLK_SEL_ST1[09],    CLK_SEL_ST2[08] */
	{LCD_PANEL_ID0_640X480, 0x00, 0x00},	/* For 640x480   */
	{LCD_PANEL_ID1_800X600, 0x00, 0x00},	/* For 800x600   */
	{LCD_PANEL_ID2_1024X768, 0x00, 0x00},	/* For 1024x768  */
	{LCD_PANEL_ID3_1280X768, 0x00, 0x00},	/* For 1280x768  */
	{LCD_PANEL_ID4_1280X1024, 0x00, 0x00},	/* For 1280x1024 */
	{LCD_PANEL_ID5_1400X1050, 0x00, 0x00},	/* For 1400x1050 */
	{LCD_PANEL_ID6_1600X1200, 0x00, 0x00}	/* For 1600x1200 */
};

struct GFX_DPA_SETTING GFX_DPA_SETTING_TBL_VT3327[] = {
/*  ClkRange,DVP0, DVP0DataDriving,  DVP0ClockDriving, DVP1,
					DVP1Driving, DFPHigh,   DFPLow */
/*   CR96, SR2A[5], SR1B[1], SR2A[4], SR1E[2], CR9B,
					SR65,        CR97,      CR99   */
/* LCK/VCK < 30000000 will use this value */
{DPA_CLK_RANGE_30M, 0x07, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x08, 0x01},
/* 30000000 < LCK/VCK < 50000000 will use this value */
{DPA_CLK_RANGE_30_50M, 0x07, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x08, 0x01},
/* 50000000 < LCK/VCK < 70000000 will use this value */
{DPA_CLK_RANGE_50_70M, 0x06, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x08, 0x01},
/* 70000000 < LCK/VCK < 100000000 will use this value */
{DPA_CLK_RANGE_70_100M, 0x03, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x08, 0x03},
/* 100000000 < LCK/VCK < 15000000 will use this value */
{DPA_CLK_RANGE_100_150M, 0x03, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x01, 0x02},
/* 15000000 < LCK/VCK will use this value */
{DPA_CLK_RANGE_150M, 0x00, 0x20, 0x00, 0x10, 0x00, 0x03, 0x00, 0x0D, 0x03},
};

/* For VT3364: */
struct GFX_DPA_SETTING GFX_DPA_SETTING_TBL_VT3364[] = {
/*  ClkRange,DVP0, DVP0DataDriving,  DVP0ClockDriving, DVP1,
					DVP1Driving, DFPHigh,   DFPLow */
/*   CR96, SR2A[5], SR1B[1], SR2A[4], SR1E[2], CR9B,
					SR65,        CR97,      CR99   */
/* LCK/VCK < 30000000 will use this value */
{DPA_CLK_RANGE_30M, 0x07, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x08},
/* 30000000 < LCK/VCK < 50000000 will use this value */
{DPA_CLK_RANGE_30_50M, 0x07, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x08},
/* 50000000 < LCK/VCK < 70000000 will use this value */
{DPA_CLK_RANGE_50_70M, 0x07, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x08},
/* 70000000 < LCK/VCK < 100000000 will use this value */
{DPA_CLK_RANGE_70_100M, 0x07, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x08},
/* 100000000 < LCK/VCK < 15000000 will use this value */
{DPA_CLK_RANGE_100_150M, 0x03, 0x00, 0x02, 0x00, 0x00, 0x03, 0x00, 0x00, 0x08},
/* 15000000 < LCK/VCK will use this value */
{DPA_CLK_RANGE_150M, 0x01, 0x00, 0x02, 0x10, 0x00, 0x03, 0x00, 0x00, 0x08},
};
