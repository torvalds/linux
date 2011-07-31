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

#include <linux/via-core.h>
#include "global.h"
struct res_map_refresh res_map_refresh_tbl[] = {
/*hres, vres, vclock, vmode_refresh*/
	{480, 640, RES_480X640_60HZ_PIXCLOCK, 60},
	{640, 480, RES_640X480_60HZ_PIXCLOCK, 60},
	{640, 480, RES_640X480_75HZ_PIXCLOCK, 75},
	{640, 480, RES_640X480_85HZ_PIXCLOCK, 85},
	{640, 480, RES_640X480_100HZ_PIXCLOCK, 100},
	{640, 480, RES_640X480_120HZ_PIXCLOCK, 120},
	{720, 480, RES_720X480_60HZ_PIXCLOCK, 60},
	{720, 576, RES_720X576_60HZ_PIXCLOCK, 60},
	{800, 480, RES_800X480_60HZ_PIXCLOCK, 60},
	{800, 600, RES_800X600_60HZ_PIXCLOCK, 60},
	{800, 600, RES_800X600_75HZ_PIXCLOCK, 75},
	{800, 600, RES_800X600_85HZ_PIXCLOCK, 85},
	{800, 600, RES_800X600_100HZ_PIXCLOCK, 100},
	{800, 600, RES_800X600_120HZ_PIXCLOCK, 120},
	{848, 480, RES_848X480_60HZ_PIXCLOCK, 60},
	{856, 480, RES_856X480_60HZ_PIXCLOCK, 60},
	{1024, 512, RES_1024X512_60HZ_PIXCLOCK, 60},
	{1024, 600, RES_1024X600_60HZ_PIXCLOCK, 60},
	{1024, 768, RES_1024X768_60HZ_PIXCLOCK, 60},
	{1024, 768, RES_1024X768_75HZ_PIXCLOCK, 75},
	{1024, 768, RES_1024X768_85HZ_PIXCLOCK, 85},
	{1024, 768, RES_1024X768_100HZ_PIXCLOCK, 100},
/*  {1152,864, RES_1152X864_70HZ_PIXCLOCK,  70},*/
	{1152, 864, RES_1152X864_75HZ_PIXCLOCK, 75},
	{1280, 768, RES_1280X768_60HZ_PIXCLOCK, 60},
	{1280, 800, RES_1280X800_60HZ_PIXCLOCK, 60},
	{1280, 960, RES_1280X960_60HZ_PIXCLOCK, 60},
	{1280, 1024, RES_1280X1024_60HZ_PIXCLOCK, 60},
	{1280, 1024, RES_1280X1024_75HZ_PIXCLOCK, 75},
	{1280, 1024, RES_1280X768_85HZ_PIXCLOCK, 85},
	{1440, 1050, RES_1440X1050_60HZ_PIXCLOCK, 60},
	{1600, 1200, RES_1600X1200_60HZ_PIXCLOCK, 60},
	{1600, 1200, RES_1600X1200_75HZ_PIXCLOCK, 75},
	{1280, 720, RES_1280X720_60HZ_PIXCLOCK, 60},
	{1920, 1080, RES_1920X1080_60HZ_PIXCLOCK, 60},
	{1400, 1050, RES_1400X1050_60HZ_PIXCLOCK, 60},
	{1400, 1050, RES_1400X1050_75HZ_PIXCLOCK, 75},
	{1368, 768, RES_1368X768_60HZ_PIXCLOCK, 60},
	{960, 600, RES_960X600_60HZ_PIXCLOCK, 60},
	{1000, 600, RES_1000X600_60HZ_PIXCLOCK, 60},
	{1024, 576, RES_1024X576_60HZ_PIXCLOCK, 60},
	{1088, 612, RES_1088X612_60HZ_PIXCLOCK, 60},
	{1152, 720, RES_1152X720_60HZ_PIXCLOCK, 60},
	{1200, 720, RES_1200X720_60HZ_PIXCLOCK, 60},
	{1200, 900, RES_1200X900_60HZ_PIXCLOCK, 60},
	{1280, 600, RES_1280X600_60HZ_PIXCLOCK, 60},
	{1280, 720, RES_1280X720_50HZ_PIXCLOCK, 50},
	{1280, 768, RES_1280X768_50HZ_PIXCLOCK, 50},
	{1360, 768, RES_1360X768_60HZ_PIXCLOCK, 60},
	{1366, 768, RES_1366X768_50HZ_PIXCLOCK, 50},
	{1366, 768, RES_1366X768_60HZ_PIXCLOCK, 60},
	{1440, 900, RES_1440X900_60HZ_PIXCLOCK, 60},
	{1440, 900, RES_1440X900_75HZ_PIXCLOCK, 75},
	{1600, 900, RES_1600X900_60HZ_PIXCLOCK, 60},
	{1600, 1024, RES_1600X1024_60HZ_PIXCLOCK, 60},
	{1680, 1050, RES_1680X1050_60HZ_PIXCLOCK, 60},
	{1680, 1050, RES_1680X1050_75HZ_PIXCLOCK, 75},
	{1792, 1344, RES_1792X1344_60HZ_PIXCLOCK, 60},
	{1856, 1392, RES_1856X1392_60HZ_PIXCLOCK, 60},
	{1920, 1200, RES_1920X1200_60HZ_PIXCLOCK, 60},
	{1920, 1440, RES_1920X1440_60HZ_PIXCLOCK, 60},
	{1920, 1440, RES_1920X1440_75HZ_PIXCLOCK, 75},
	{2048, 1536, RES_2048X1536_60HZ_PIXCLOCK, 60}
};

struct io_reg CN400_ModeXregs[] = { {VIASR, SR10, 0xFF, 0x01},
{VIASR, SR15, 0x02, 0x02},
{VIASR, SR16, 0xBF, 0x08},
{VIASR, SR17, 0xFF, 0x1F},
{VIASR, SR18, 0xFF, 0x4E},
{VIASR, SR1A, 0xFB, 0x08},
{VIASR, SR1E, 0x0F, 0x01},
{VIASR, SR2A, 0xFF, 0x00},
{VIACR, CR0A, 0xFF, 0x1E},	/* Cursor Start                        */
{VIACR, CR0B, 0xFF, 0x00},	/* Cursor End                          */
{VIACR, CR0E, 0xFF, 0x00},	/* Cursor Location High                */
{VIACR, CR0F, 0xFF, 0x00},	/* Cursor Localtion Low                */
{VIACR, CR32, 0xFF, 0x00},
{VIACR, CR33, 0xFF, 0x00},
{VIACR, CR35, 0xFF, 0x00},
{VIACR, CR36, 0x08, 0x00},
{VIACR, CR69, 0xFF, 0x00},
{VIACR, CR6A, 0xFF, 0x40},
{VIACR, CR6B, 0xFF, 0x00},
{VIACR, CR6C, 0xFF, 0x00},
{VIACR, CR7A, 0xFF, 0x01},	/* LCD Scaling Parameter 1             */
{VIACR, CR7B, 0xFF, 0x02},	/* LCD Scaling Parameter 2             */
{VIACR, CR7C, 0xFF, 0x03},	/* LCD Scaling Parameter 3             */
{VIACR, CR7D, 0xFF, 0x04},	/* LCD Scaling Parameter 4             */
{VIACR, CR7E, 0xFF, 0x07},	/* LCD Scaling Parameter 5             */
{VIACR, CR7F, 0xFF, 0x0A},	/* LCD Scaling Parameter 6             */
{VIACR, CR80, 0xFF, 0x0D},	/* LCD Scaling Parameter 7             */
{VIACR, CR81, 0xFF, 0x13},	/* LCD Scaling Parameter 8             */
{VIACR, CR82, 0xFF, 0x16},	/* LCD Scaling Parameter 9             */
{VIACR, CR83, 0xFF, 0x19},	/* LCD Scaling Parameter 10            */
{VIACR, CR84, 0xFF, 0x1C},	/* LCD Scaling Parameter 11            */
{VIACR, CR85, 0xFF, 0x1D},	/* LCD Scaling Parameter 12            */
{VIACR, CR86, 0xFF, 0x1E},	/* LCD Scaling Parameter 13            */
{VIACR, CR87, 0xFF, 0x1F},	/* LCD Scaling Parameter 14            */
{VIACR, CR88, 0xFF, 0x40},	/* LCD Panel Type                      */
{VIACR, CR89, 0xFF, 0x00},	/* LCD Timing Control 0                */
{VIACR, CR8A, 0xFF, 0x88},	/* LCD Timing Control 1                */
{VIACR, CR8B, 0xFF, 0x69},	/* LCD Power Sequence Control 0        */
{VIACR, CR8C, 0xFF, 0x57},	/* LCD Power Sequence Control 1        */
{VIACR, CR8D, 0xFF, 0x00},	/* LCD Power Sequence Control 2        */
{VIACR, CR8E, 0xFF, 0x7B},	/* LCD Power Sequence Control 3        */
{VIACR, CR8F, 0xFF, 0x03},	/* LCD Power Sequence Control 4        */
{VIACR, CR90, 0xFF, 0x30},	/* LCD Power Sequence Control 5        */
{VIACR, CR91, 0xFF, 0xA0},	/* 24/12 bit LVDS Data off             */
{VIACR, CR96, 0xFF, 0x00},
{VIACR, CR97, 0xFF, 0x00},
{VIACR, CR99, 0xFF, 0x00},
{VIACR, CR9B, 0xFF, 0x00}
};

/* Video Mode Table for VT3314 chipset*/
/* Common Setting for Video Mode */
struct io_reg CN700_ModeXregs[] = { {VIASR, SR10, 0xFF, 0x01},
{VIASR, SR15, 0x02, 0x02},
{VIASR, SR16, 0xBF, 0x08},
{VIASR, SR17, 0xFF, 0x1F},
{VIASR, SR18, 0xFF, 0x4E},
{VIASR, SR1A, 0xFB, 0x82},
{VIASR, SR1B, 0xFF, 0xF0},
{VIASR, SR1F, 0xFF, 0x00},
{VIASR, SR1E, 0xFF, 0x01},
{VIASR, SR22, 0xFF, 0x1F},
{VIASR, SR2A, 0x0F, 0x00},
{VIASR, SR2E, 0xFF, 0xFF},
{VIASR, SR3F, 0xFF, 0xFF},
{VIASR, SR40, 0xF7, 0x00},
{VIASR, CR30, 0xFF, 0x04},
{VIACR, CR32, 0xFF, 0x00},
{VIACR, CR33, 0x7F, 0x00},
{VIACR, CR35, 0xFF, 0x00},
{VIACR, CR36, 0xFF, 0x31},
{VIACR, CR41, 0xFF, 0x80},
{VIACR, CR42, 0xFF, 0x00},
{VIACR, CR55, 0x80, 0x00},
{VIACR, CR5D, 0x80, 0x00},	/*Horizontal Retrace Start bit[11] should be 0*/
{VIACR, CR68, 0xFF, 0x67},	/* Default FIFO For IGA2 */
{VIACR, CR69, 0xFF, 0x00},
{VIACR, CR6A, 0xFD, 0x40},
{VIACR, CR6B, 0xFF, 0x00},
{VIACR, CR6C, 0xFF, 0x00},
{VIACR, CR77, 0xFF, 0x00},	/* LCD scaling Factor */
{VIACR, CR78, 0xFF, 0x00},	/* LCD scaling Factor */
{VIACR, CR79, 0xFF, 0x00},	/* LCD scaling Factor */
{VIACR, CR9F, 0x03, 0x00},	/* LCD scaling Factor */
{VIACR, CR7A, 0xFF, 0x01},	/* LCD Scaling Parameter 1 */
{VIACR, CR7B, 0xFF, 0x02},	/* LCD Scaling Parameter 2 */
{VIACR, CR7C, 0xFF, 0x03},	/* LCD Scaling Parameter 3 */
{VIACR, CR7D, 0xFF, 0x04},	/* LCD Scaling Parameter 4 */
{VIACR, CR7E, 0xFF, 0x07},	/* LCD Scaling Parameter 5 */
{VIACR, CR7F, 0xFF, 0x0A},	/* LCD Scaling Parameter 6 */
{VIACR, CR80, 0xFF, 0x0D},	/* LCD Scaling Parameter 7 */
{VIACR, CR81, 0xFF, 0x13},	/* LCD Scaling Parameter 8 */
{VIACR, CR82, 0xFF, 0x16},	/* LCD Scaling Parameter 9 */
{VIACR, CR83, 0xFF, 0x19},	/* LCD Scaling Parameter 10 */
{VIACR, CR84, 0xFF, 0x1C},	/* LCD Scaling Parameter 11 */
{VIACR, CR85, 0xFF, 0x1D},	/* LCD Scaling Parameter 12 */
{VIACR, CR86, 0xFF, 0x1E},	/* LCD Scaling Parameter 13 */
{VIACR, CR87, 0xFF, 0x1F},	/* LCD Scaling Parameter 14 */
{VIACR, CR88, 0xFF, 0x40},	/* LCD Panel Type */
{VIACR, CR89, 0xFF, 0x00},	/* LCD Timing Control 0 */
{VIACR, CR8A, 0xFF, 0x88},	/* LCD Timing Control 1 */
{VIACR, CR8B, 0xFF, 0x5D},	/* LCD Power Sequence Control 0 */
{VIACR, CR8C, 0xFF, 0x2B},	/* LCD Power Sequence Control 1 */
{VIACR, CR8D, 0xFF, 0x6F},	/* LCD Power Sequence Control 2 */
{VIACR, CR8E, 0xFF, 0x2B},	/* LCD Power Sequence Control 3 */
{VIACR, CR8F, 0xFF, 0x01},	/* LCD Power Sequence Control 4 */
{VIACR, CR90, 0xFF, 0x01},	/* LCD Power Sequence Control 5 */
{VIACR, CR91, 0xFF, 0xA0},	/* 24/12 bit LVDS Data off */
{VIACR, CR96, 0xFF, 0x00},
{VIACR, CR97, 0xFF, 0x00},
{VIACR, CR99, 0xFF, 0x00},
{VIACR, CR9B, 0xFF, 0x00},
{VIACR, CR9D, 0xFF, 0x80},
{VIACR, CR9E, 0xFF, 0x80}
};

struct io_reg KM400_ModeXregs[] = {
	{VIASR, SR10, 0xFF, 0x01},	/* Unlock Register                 */
	{VIASR, SR16, 0xFF, 0x08},	/* Display FIFO threshold Control  */
	{VIASR, SR17, 0xFF, 0x1F},	/* Display FIFO Control            */
	{VIASR, SR18, 0xFF, 0x4E},	/* GFX PREQ threshold              */
	{VIASR, SR1A, 0xFF, 0x0a},	/* GFX PREQ threshold              */
	{VIASR, SR1F, 0xFF, 0x00},	/* Memory Control 0                */
	{VIASR, SR1B, 0xFF, 0xF0},	/* Power Management Control 0      */
	{VIASR, SR1E, 0xFF, 0x01},	/* Power Management Control        */
	{VIASR, SR20, 0xFF, 0x00},	/* Sequencer Arbiter Control 0     */
	{VIASR, SR21, 0xFF, 0x00},	/* Sequencer Arbiter Control 1     */
	{VIASR, SR22, 0xFF, 0x1F},	/* Display Arbiter Control 1       */
	{VIASR, SR2A, 0xFF, 0x00},	/* Power Management Control 5      */
	{VIASR, SR2D, 0xFF, 0xFF},	/* Power Management Control 1      */
	{VIASR, SR2E, 0xFF, 0xFF},	/* Power Management Control 2      */
	{VIACR, CR0A, 0xFF, 0x1E},	/* Cursor Start                    */
	{VIACR, CR0B, 0xFF, 0x00},	/* Cursor End                      */
	{VIACR, CR0E, 0xFF, 0x00},	/* Cursor Location High            */
	{VIACR, CR0F, 0xFF, 0x00},	/* Cursor Localtion Low            */
	{VIACR, CR33, 0xFF, 0x00},
	{VIACR, CR55, 0x80, 0x00},
	{VIACR, CR5D, 0x80, 0x00},
	{VIACR, CR36, 0xFF, 0x01},	/* Power Mangement 3                  */
	{VIACR, CR68, 0xFF, 0x67},	/* Default FIFO For IGA2              */
	{VIACR, CR6A, 0x20, 0x20},	/* Extended FIFO On                   */
	{VIACR, CR7A, 0xFF, 0x01},	/* LCD Scaling Parameter 1            */
	{VIACR, CR7B, 0xFF, 0x02},	/* LCD Scaling Parameter 2            */
	{VIACR, CR7C, 0xFF, 0x03},	/* LCD Scaling Parameter 3            */
	{VIACR, CR7D, 0xFF, 0x04},	/* LCD Scaling Parameter 4            */
	{VIACR, CR7E, 0xFF, 0x07},	/* LCD Scaling Parameter 5            */
	{VIACR, CR7F, 0xFF, 0x0A},	/* LCD Scaling Parameter 6            */
	{VIACR, CR80, 0xFF, 0x0D},	/* LCD Scaling Parameter 7            */
	{VIACR, CR81, 0xFF, 0x13},	/* LCD Scaling Parameter 8            */
	{VIACR, CR82, 0xFF, 0x16},	/* LCD Scaling Parameter 9            */
	{VIACR, CR83, 0xFF, 0x19},	/* LCD Scaling Parameter 10           */
	{VIACR, CR84, 0xFF, 0x1C},	/* LCD Scaling Parameter 11           */
	{VIACR, CR85, 0xFF, 0x1D},	/* LCD Scaling Parameter 12           */
	{VIACR, CR86, 0xFF, 0x1E},	/* LCD Scaling Parameter 13           */
	{VIACR, CR87, 0xFF, 0x1F},	/* LCD Scaling Parameter 14           */
	{VIACR, CR88, 0xFF, 0x40},	/* LCD Panel Type                     */
	{VIACR, CR89, 0xFF, 0x00},	/* LCD Timing Control 0               */
	{VIACR, CR8A, 0xFF, 0x88},	/* LCD Timing Control 1               */
	{VIACR, CR8B, 0xFF, 0x2D},	/* LCD Power Sequence Control 0       */
	{VIACR, CR8C, 0xFF, 0x2D},	/* LCD Power Sequence Control 1       */
	{VIACR, CR8D, 0xFF, 0xC8},	/* LCD Power Sequence Control 2       */
	{VIACR, CR8E, 0xFF, 0x36},	/* LCD Power Sequence Control 3       */
	{VIACR, CR8F, 0xFF, 0x00},	/* LCD Power Sequence Control 4       */
	{VIACR, CR90, 0xFF, 0x10},	/* LCD Power Sequence Control 5       */
	{VIACR, CR91, 0xFF, 0xA0},	/* 24/12 bit LVDS Data off            */
	{VIACR, CR96, 0xFF, 0x03},	/* DVP0        ; DVP0 Clock Skew */
	{VIACR, CR97, 0xFF, 0x03},	/* DFP high    ; DFPH Clock Skew */
	{VIACR, CR99, 0xFF, 0x03},	/* DFP low           ; DFPL Clock Skew*/
	{VIACR, CR9B, 0xFF, 0x07}	/* DVI on DVP1       ; DVP1 Clock Skew*/
};

/* For VT3324: Common Setting for Video Mode */
struct io_reg CX700_ModeXregs[] = { {VIASR, SR10, 0xFF, 0x01},
{VIASR, SR15, 0x02, 0x02},
{VIASR, SR16, 0xBF, 0x08},
{VIASR, SR17, 0xFF, 0x1F},
{VIASR, SR18, 0xFF, 0x4E},
{VIASR, SR1A, 0xFB, 0x08},
{VIASR, SR1B, 0xFF, 0xF0},
{VIASR, SR1E, 0xFF, 0x01},
{VIASR, SR2A, 0xFF, 0x00},
{VIASR, SR2D, 0xFF, 0xFF},	/* VCK and LCK PLL power on.           */
{VIACR, CR0A, 0xFF, 0x1E},	/* Cursor Start                        */
{VIACR, CR0B, 0xFF, 0x00},	/* Cursor End                          */
{VIACR, CR0E, 0xFF, 0x00},	/* Cursor Location High                */
{VIACR, CR0F, 0xFF, 0x00},	/* Cursor Localtion Low                */
{VIACR, CR32, 0xFF, 0x00},
{VIACR, CR33, 0xFF, 0x00},
{VIACR, CR35, 0xFF, 0x00},
{VIACR, CR36, 0x08, 0x00},
{VIACR, CR47, 0xC8, 0x00},	/* Clear VCK Plus. */
{VIACR, CR69, 0xFF, 0x00},
{VIACR, CR6A, 0xFF, 0x40},
{VIACR, CR6B, 0xFF, 0x00},
{VIACR, CR6C, 0xFF, 0x00},
{VIACR, CR7A, 0xFF, 0x01},	/* LCD Scaling Parameter 1             */
{VIACR, CR7B, 0xFF, 0x02},	/* LCD Scaling Parameter 2             */
{VIACR, CR7C, 0xFF, 0x03},	/* LCD Scaling Parameter 3             */
{VIACR, CR7D, 0xFF, 0x04},	/* LCD Scaling Parameter 4             */
{VIACR, CR7E, 0xFF, 0x07},	/* LCD Scaling Parameter 5             */
{VIACR, CR7F, 0xFF, 0x0A},	/* LCD Scaling Parameter 6             */
{VIACR, CR80, 0xFF, 0x0D},	/* LCD Scaling Parameter 7             */
{VIACR, CR81, 0xFF, 0x13},	/* LCD Scaling Parameter 8             */
{VIACR, CR82, 0xFF, 0x16},	/* LCD Scaling Parameter 9             */
{VIACR, CR83, 0xFF, 0x19},	/* LCD Scaling Parameter 10            */
{VIACR, CR84, 0xFF, 0x1C},	/* LCD Scaling Parameter 11            */
{VIACR, CR85, 0xFF, 0x1D},	/* LCD Scaling Parameter 12            */
{VIACR, CR86, 0xFF, 0x1E},	/* LCD Scaling Parameter 13            */
{VIACR, CR87, 0xFF, 0x1F},	/* LCD Scaling Parameter 14            */
{VIACR, CR88, 0xFF, 0x40},	/* LCD Panel Type                      */
{VIACR, CR89, 0xFF, 0x00},	/* LCD Timing Control 0                */
{VIACR, CR8A, 0xFF, 0x88},	/* LCD Timing Control 1                */
{VIACR, CRD4, 0xFF, 0x81},	/* Second power sequence control       */
{VIACR, CR8B, 0xFF, 0x5D},	/* LCD Power Sequence Control 0        */
{VIACR, CR8C, 0xFF, 0x2B},	/* LCD Power Sequence Control 1        */
{VIACR, CR8D, 0xFF, 0x6F},	/* LCD Power Sequence Control 2        */
{VIACR, CR8E, 0xFF, 0x2B},	/* LCD Power Sequence Control 3        */
{VIACR, CR8F, 0xFF, 0x01},	/* LCD Power Sequence Control 4        */
{VIACR, CR90, 0xFF, 0x01},	/* LCD Power Sequence Control 5        */
{VIACR, CR91, 0xFF, 0x80},	/* 24/12 bit LVDS Data off             */
{VIACR, CR96, 0xFF, 0x00},
{VIACR, CR97, 0xFF, 0x00},
{VIACR, CR99, 0xFF, 0x00},
{VIACR, CR9B, 0xFF, 0x00}
};

struct io_reg VX855_ModeXregs[] = {
{VIASR, SR10, 0xFF, 0x01},
{VIASR, SR15, 0x02, 0x02},
{VIASR, SR16, 0xBF, 0x08},
{VIASR, SR17, 0xFF, 0x1F},
{VIASR, SR18, 0xFF, 0x4E},
{VIASR, SR1A, 0xFB, 0x08},
{VIASR, SR1B, 0xFF, 0xF0},
{VIASR, SR1E, 0x07, 0x01},
{VIASR, SR2A, 0xF0, 0x00},
{VIASR, SR58, 0xFF, 0x00},
{VIASR, SR59, 0xFF, 0x00},
{VIASR, SR2D, 0xFF, 0xFF},	/* VCK and LCK PLL power on.           */
{VIACR, CR09, 0xFF, 0x00},	/* Initial CR09=0*/
{VIACR, CR11, 0x8F, 0x00},	/* IGA1 initial  Vertical end       */
{VIACR, CR17, 0x7F, 0x00}, 	/* IGA1 CRT Mode control init   */
{VIACR, CR0A, 0xFF, 0x1E},	/* Cursor Start                        */
{VIACR, CR0B, 0xFF, 0x00},	/* Cursor End                          */
{VIACR, CR0E, 0xFF, 0x00},	/* Cursor Location High                */
{VIACR, CR0F, 0xFF, 0x00},	/* Cursor Localtion Low                */
{VIACR, CR32, 0xFF, 0x00},
{VIACR, CR33, 0x7F, 0x00},
{VIACR, CR35, 0xFF, 0x00},
{VIACR, CR36, 0x08, 0x00},
{VIACR, CR69, 0xFF, 0x00},
{VIACR, CR6A, 0xFD, 0x60},
{VIACR, CR6B, 0xFF, 0x00},
{VIACR, CR6C, 0xFF, 0x00},
{VIACR, CR7A, 0xFF, 0x01},          /* LCD Scaling Parameter 1             */
{VIACR, CR7B, 0xFF, 0x02},          /* LCD Scaling Parameter 2             */
{VIACR, CR7C, 0xFF, 0x03},          /* LCD Scaling Parameter 3             */
{VIACR, CR7D, 0xFF, 0x04},          /* LCD Scaling Parameter 4             */
{VIACR, CR7E, 0xFF, 0x07},          /* LCD Scaling Parameter 5             */
{VIACR, CR7F, 0xFF, 0x0A},          /* LCD Scaling Parameter 6             */
{VIACR, CR80, 0xFF, 0x0D},          /* LCD Scaling Parameter 7             */
{VIACR, CR81, 0xFF, 0x13},          /* LCD Scaling Parameter 8             */
{VIACR, CR82, 0xFF, 0x16},          /* LCD Scaling Parameter 9             */
{VIACR, CR83, 0xFF, 0x19},          /* LCD Scaling Parameter 10            */
{VIACR, CR84, 0xFF, 0x1C},          /* LCD Scaling Parameter 11            */
{VIACR, CR85, 0xFF, 0x1D},          /* LCD Scaling Parameter 12            */
{VIACR, CR86, 0xFF, 0x1E},          /* LCD Scaling Parameter 13            */
{VIACR, CR87, 0xFF, 0x1F},          /* LCD Scaling Parameter 14            */
{VIACR, CR88, 0xFF, 0x40},          /* LCD Panel Type                      */
{VIACR, CR89, 0xFF, 0x00},          /* LCD Timing Control 0                */
{VIACR, CR8A, 0xFF, 0x88},          /* LCD Timing Control 1                */
{VIACR, CRD4, 0xFF, 0x81},          /* Second power sequence control       */
{VIACR, CR91, 0xFF, 0x80},          /* 24/12 bit LVDS Data off             */
{VIACR, CR96, 0xFF, 0x00},
{VIACR, CR97, 0xFF, 0x00},
{VIACR, CR99, 0xFF, 0x00},
{VIACR, CR9B, 0xFF, 0x00},
{VIACR, CRD2, 0xFF, 0xFF}           /* TMDS/LVDS control register.         */
};

/* Video Mode Table */
/* Common Setting for Video Mode */
struct io_reg CLE266_ModeXregs[] = { {VIASR, SR1E, 0xF0, 0x00},
{VIASR, SR2A, 0x0F, 0x00},
{VIASR, SR15, 0x02, 0x02},
{VIASR, SR16, 0xBF, 0x08},
{VIASR, SR17, 0xFF, 0x1F},
{VIASR, SR18, 0xFF, 0x4E},
{VIASR, SR1A, 0xFB, 0x08},

{VIACR, CR32, 0xFF, 0x00},
{VIACR, CR35, 0xFF, 0x00},
{VIACR, CR36, 0x08, 0x00},
{VIACR, CR6A, 0xFF, 0x80},
{VIACR, CR6A, 0xFF, 0xC0},

{VIACR, CR55, 0x80, 0x00},
{VIACR, CR5D, 0x80, 0x00},

{VIAGR, GR20, 0xFF, 0x00},
{VIAGR, GR21, 0xFF, 0x00},
{VIAGR, GR22, 0xFF, 0x00},
	/* LCD Parameters */
{VIACR, CR7A, 0xFF, 0x01},	/* LCD Parameter 1 */
{VIACR, CR7B, 0xFF, 0x02},	/* LCD Parameter 2 */
{VIACR, CR7C, 0xFF, 0x03},	/* LCD Parameter 3 */
{VIACR, CR7D, 0xFF, 0x04},	/* LCD Parameter 4 */
{VIACR, CR7E, 0xFF, 0x07},	/* LCD Parameter 5 */
{VIACR, CR7F, 0xFF, 0x0A},	/* LCD Parameter 6 */
{VIACR, CR80, 0xFF, 0x0D},	/* LCD Parameter 7 */
{VIACR, CR81, 0xFF, 0x13},	/* LCD Parameter 8 */
{VIACR, CR82, 0xFF, 0x16},	/* LCD Parameter 9 */
{VIACR, CR83, 0xFF, 0x19},	/* LCD Parameter 10 */
{VIACR, CR84, 0xFF, 0x1C},	/* LCD Parameter 11 */
{VIACR, CR85, 0xFF, 0x1D},	/* LCD Parameter 12 */
{VIACR, CR86, 0xFF, 0x1E},	/* LCD Parameter 13 */
{VIACR, CR87, 0xFF, 0x1F},	/* LCD Parameter 14 */

};

/* Mode:1024X768 */
struct io_reg PM1024x768[] = { {VIASR, 0x16, 0xBF, 0x0C},
{VIASR, 0x18, 0xFF, 0x4C}
};

struct patch_table res_patch_table[] = {
	{ARRAY_SIZE(PM1024x768), PM1024x768}
};

/* struct VPITTable {
	unsigned char  Misc;
	unsigned char  SR[StdSR];
	unsigned char  CR[StdCR];
	unsigned char  GR[StdGR];
	unsigned char  AR[StdAR];
 };*/

struct VPITTable VPIT = {
	/* Msic */
	0xC7,
	/* Sequencer */
	{0x01, 0x0F, 0x00, 0x0E},
	/* Graphic Controller */
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F, 0xFF},
	/* Attribute Controller */
	{0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	 0x01, 0x00, 0x0F, 0x00}
};

/********************/
/* Mode Table       */
/********************/

/* 480x640 */
struct crt_mode_table CRTM480x640[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_25_175M, M480X640_R60_HSP, M480X640_R60_VSP,
	 {624, 480, 480, 144, 504, 48, 663, 640, 640, 23, 641, 3} }	/* GTF*/
};

/* 640x480*/
struct crt_mode_table CRTM640x480[] = {
	/*r_rate,vclk,hsp,vsp */
	/*HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_25_175M, M640X480_R60_HSP, M640X480_R60_VSP,
	 {800, 640, 648, 144, 656, 96, 525, 480, 480, 45, 490, 2} },
	{REFRESH_75, CLK_31_500M, M640X480_R75_HSP, M640X480_R75_VSP,
	 {840, 640, 640, 200, 656, 64, 500, 480, 480, 20, 481, 3} },
	{REFRESH_85, CLK_36_000M, M640X480_R85_HSP, M640X480_R85_VSP,
	 {832, 640, 640, 192, 696, 56, 509, 480, 480, 29, 481, 3} },
	{REFRESH_100, CLK_43_163M, M640X480_R100_HSP, M640X480_R100_VSP,
	 {848, 640, 640, 208, 680, 64, 509, 480, 480, 29, 481, 3} }, /*GTF*/
	    {REFRESH_120, CLK_52_406M, M640X480_R120_HSP,
	     M640X480_R120_VSP,
	     {848, 640, 640, 208, 680, 64, 515, 480, 480, 35, 481,
	      3} } /*GTF*/
};

/*720x480 (GTF)*/
struct crt_mode_table CRTM720x480[] = {
	/*r_rate,vclk,hsp,vsp      */
	/*HT, HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_26_880M, M720X480_R60_HSP, M720X480_R60_VSP,
	 {896, 720, 720, 176, 736, 72, 497, 480, 480, 17, 481, 3} }

};

/*720x576 (GTF)*/
struct crt_mode_table CRTM720x576[] = {
	/*r_rate,vclk,hsp,vsp */
	/*HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_32_668M, M720X576_R60_HSP, M720X576_R60_VSP,
	 {912, 720, 720, 192, 744, 72, 597, 576, 576, 21, 577, 3} }
};

/* 800x480 (CVT) */
struct crt_mode_table CRTM800x480[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_29_581M, M800X480_R60_HSP, M800X480_R60_VSP,
	 {992, 800, 800, 192, 824, 72, 500, 480, 480, 20, 483, 7} }
};

/* 800x600*/
struct crt_mode_table CRTM800x600[] = {
	/*r_rate,vclk,hsp,vsp     */
	/*HT,   HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_40_000M, M800X600_R60_HSP, M800X600_R60_VSP,
	 {1056, 800, 800, 256, 840, 128, 628, 600, 600, 28, 601, 4} },
	{REFRESH_75, CLK_49_500M, M800X600_R75_HSP, M800X600_R75_VSP,
	 {1056, 800, 800, 256, 816, 80, 625, 600, 600, 25, 601, 3} },
	{REFRESH_85, CLK_56_250M, M800X600_R85_HSP, M800X600_R85_VSP,
	 {1048, 800, 800, 248, 832, 64, 631, 600, 600, 31, 601, 3} },
	{REFRESH_100, CLK_68_179M, M800X600_R100_HSP, M800X600_R100_VSP,
	 {1072, 800, 800, 272, 848, 88, 636, 600, 600, 36, 601, 3} },
	{REFRESH_120, CLK_83_950M, M800X600_R120_HSP,
		  M800X600_R120_VSP,
		  {1088, 800, 800, 288, 856, 88, 643, 600, 600, 43, 601,
		   3} }
};

/* 848x480 (CVT) */
struct crt_mode_table CRTM848x480[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_31_500M, M848X480_R60_HSP, M848X480_R60_VSP,
	 {1056, 848, 848, 208, 872, 80, 500, 480, 480, 20, 483, 5} }
};

/*856x480 (GTF) convert to 852x480*/
struct crt_mode_table CRTM852x480[] = {
	/*r_rate,vclk,hsp,vsp     */
	/*HT,   HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_31_728M, M852X480_R60_HSP, M852X480_R60_VSP,
	{1064, 856, 856, 208, 872, 88, 497, 480, 480, 17, 481, 3} }
};

/*1024x512 (GTF)*/
struct crt_mode_table CRTM1024x512[] = {
	/*r_rate,vclk,hsp,vsp     */
	/*HT,   HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_41_291M, M1024X512_R60_HSP, M1024X512_R60_VSP,
	 {1296, 1024, 1024, 272, 1056, 104, 531, 512, 512, 19, 513, 3} }

};

/* 1024x600*/
struct crt_mode_table CRTM1024x600[] = {
	/*r_rate,vclk,hsp,vsp */
	/*HT,  HA,   HBS,  HBE, HSS,  HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_48_875M, M1024X600_R60_HSP, M1024X600_R60_VSP,
	 {1312, 1024, 1024, 288, 1064, 104, 622, 600, 600, 22, 601, 3} },
};

/* 1024x768*/
struct crt_mode_table CRTM1024x768[] = {
	/*r_rate,vclk,hsp,vsp */
	/*HT,  HA,   HBS,  HBE, HSS,  HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_65_000M, M1024X768_R60_HSP, M1024X768_R60_VSP,
	{1344, 1024, 1024, 320, 1048, 136, 806, 768, 768, 38, 771, 6} },
	{REFRESH_75, CLK_78_750M, M1024X768_R75_HSP, M1024X768_R75_VSP,
	{1312, 1024, 1024, 288, 1040, 96, 800, 768, 768, 32, 769, 3} },
	{REFRESH_85, CLK_94_500M, M1024X768_R85_HSP, M1024X768_R85_VSP,
	{1376, 1024, 1024, 352, 1072, 96, 808, 768, 768, 40, 769, 3} },
	{REFRESH_100, CLK_113_309M, M1024X768_R100_HSP, M1024X768_R100_VSP,
	{1392, 1024, 1024, 368, 1096, 112, 814, 768, 768, 46, 769, 3} }
};

/* 1152x864*/
struct crt_mode_table CRTM1152x864[] = {
	/*r_rate,vclk,hsp,vsp      */
	/*HT,  HA,   HBS,  HBE, HSS,  HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_75, CLK_108_000M, M1152X864_R75_HSP, M1152X864_R75_VSP,
	 {1600, 1152, 1152, 448, 1216, 128, 900, 864, 864, 36, 865, 3} }

};

/* 1280x720 (HDMI 720P)*/
struct crt_mode_table CRTM1280x720[] = {
	/*r_rate,vclk,hsp,vsp */
	/*HT,  HA,   HBS,  HBE, HSS,  HSE, VT,  VA,  VBS, VBE, VSS, VSE      */
	{REFRESH_60, CLK_74_481M, M1280X720_R60_HSP, M1280X720_R60_VSP,
	 {1648, 1280, 1280, 368, 1392, 40, 750, 720, 720, 30, 725, 5} },
	{REFRESH_50, CLK_60_466M, M1280X720_R50_HSP, M1280X720_R50_VSP,
	 {1632, 1280, 1280, 352, 1328, 128, 741, 720, 720, 21, 721, 3} }
};

/*1280x768 (GTF)*/
struct crt_mode_table CRTM1280x768[] = {
	/*r_rate,vclk,hsp,vsp     */
	/*HT,  HA,   HBS,  HBE, HSS,  HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_80_136M, M1280X768_R60_HSP, M1280X768_R60_VSP,
	 {1680, 1280, 1280, 400, 1344, 136, 795, 768, 768, 27, 769, 3} },
	{REFRESH_50, CLK_65_178M, M1280X768_R50_HSP, M1280X768_R50_VSP,
	 {1648, 1280, 1280, 368, 1336, 128, 791, 768, 768, 23, 769, 3} }
};

/* 1280x800 (CVT) */
struct crt_mode_table CRTM1280x800[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_83_375M, M1280X800_R60_HSP, M1280X800_R60_VSP,
	 {1680, 1280, 1280, 400, 1352, 128, 831, 800, 800, 31, 803, 6} }
};

/*1280x960*/
struct crt_mode_table CRTM1280x960[] = {
	/*r_rate,vclk,hsp,vsp */
	/*HT,  HA,   HBS,  HBE, HSS,  HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_108_000M, M1280X960_R60_HSP, M1280X960_R60_VSP,
	 {1800, 1280, 1280, 520, 1376, 112, 1000, 960, 960, 40, 961, 3} }
};

/* 1280x1024*/
struct crt_mode_table CRTM1280x1024[] = {
	/*r_rate,vclk,,hsp,vsp */
	/*HT,  HA,   HBS,  HBE, HSS,  HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_108_000M, M1280X1024_R60_HSP, M1280X1024_R60_VSP,
	 {1688, 1280, 1280, 408, 1328, 112, 1066, 1024, 1024, 42, 1025,
	  3} },
	{REFRESH_75, CLK_135_000M, M1280X1024_R75_HSP, M1280X1024_R75_VSP,
	 {1688, 1280, 1280, 408, 1296, 144, 1066, 1024, 1024, 42, 1025,
	  3} },
	{REFRESH_85, CLK_157_500M, M1280X1024_R85_HSP, M1280X1024_R85_VSP,
	 {1728, 1280, 1280, 448, 1344, 160, 1072, 1024, 1024, 48, 1025, 3} }
};

/* 1368x768 (GTF) */
struct crt_mode_table CRTM1368x768[] = {
	/* r_rate,  vclk, hsp, vsp */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_85_860M, M1368X768_R60_HSP, M1368X768_R60_VSP,
	 {1800, 1368, 1368, 432, 1440, 144, 795, 768, 768, 27, 769, 3} }
};

/*1440x1050 (GTF)*/
struct crt_mode_table CRTM1440x1050[] = {
	/*r_rate,vclk,hsp,vsp      */
	/*HT,  HA,   HBS,  HBE, HSS,  HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_125_104M, M1440X1050_R60_HSP, M1440X1050_R60_VSP,
	 {1936, 1440, 1440, 496, 1536, 152, 1077, 1040, 1040, 37, 1041, 3} }
};

/* 1600x1200*/
struct crt_mode_table CRTM1600x1200[] = {
	/*r_rate,vclk,hsp,vsp */
	/*HT,  HA,   HBS,  HBE, HSS,  HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_162_000M, M1600X1200_R60_HSP, M1600X1200_R60_VSP,
	 {2160, 1600, 1600, 560, 1664, 192, 1250, 1200, 1200, 50, 1201,
	  3} },
	{REFRESH_75, CLK_202_500M, M1600X1200_R75_HSP, M1600X1200_R75_VSP,
	 {2160, 1600, 1600, 560, 1664, 192, 1250, 1200, 1200, 50, 1201, 3} }

};

/* 1680x1050 (CVT) */
struct crt_mode_table CRTM1680x1050[] = {
	/* r_rate,          vclk,              hsp,             vsp  */
	/* HT,  HA,  HBS, HBE, HSS, HSE,    VT,  VA,  VBS, VBE,  VSS, VSE */
	{REFRESH_60, CLK_146_760M, M1680x1050_R60_HSP, M1680x1050_R60_VSP,
	 {2240, 1680, 1680, 560, 1784, 176, 1089, 1050, 1050, 39, 1053,
	  6} },
	{REFRESH_75, CLK_187_000M, M1680x1050_R75_HSP, M1680x1050_R75_VSP,
	 {2272, 1680, 1680, 592, 1800, 176, 1099, 1050, 1050, 49, 1053, 6} }
};

/* 1680x1050 (CVT Reduce Blanking) */
struct crt_mode_table CRTM1680x1050_RB[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE,    VT,  VA,  VBS, VBE,  VSS, VSE */
	{REFRESH_60, CLK_119_000M, M1680x1050_RB_R60_HSP,
	 M1680x1050_RB_R60_VSP,
	 {1840, 1680, 1680, 160, 1728, 32, 1080, 1050, 1050, 30, 1053, 6} }
};

/* 1920x1080 (CVT)*/
struct crt_mode_table CRTM1920x1080[] = {
	/*r_rate,vclk,hsp,vsp */
	/*HT,  HA,   HBS,  HBE, HSS,  HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_172_798M, M1920X1080_R60_HSP, M1920X1080_R60_VSP,
	 {2576, 1920, 1920, 656, 2048, 200, 1120, 1080, 1080, 40, 1083, 5} }
};

/* 1920x1080 (CVT with Reduce Blanking) */
struct crt_mode_table CRTM1920x1080_RB[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_138_400M, M1920X1080_RB_R60_HSP,
	 M1920X1080_RB_R60_VSP,
	 {2080, 1920, 1920, 160, 1968, 32, 1111, 1080, 1080, 31, 1083, 5} }
};

/* 1920x1440*/
struct crt_mode_table CRTM1920x1440[] = {
	/*r_rate,vclk,hsp,vsp */
	/*HT,  HA,   HBS,  HBE, HSS,  HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_234_000M, M1920X1440_R60_HSP, M1920X1440_R60_VSP,
	 {2600, 1920, 1920, 680, 2048, 208, 1500, 1440, 1440, 60, 1441,
	  3} },
	{REFRESH_75, CLK_297_500M, M1920X1440_R75_HSP, M1920X1440_R75_VSP,
	 {2640, 1920, 1920, 720, 2064, 224, 1500, 1440, 1440, 60, 1441, 3} }
};

/* 1400x1050 (CVT) */
struct crt_mode_table CRTM1400x1050[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE,  HSS, HSE,   VT,  VA,  VBS, VBE,  VSS, VSE */
	{REFRESH_60, CLK_121_750M, M1400X1050_R60_HSP, M1400X1050_R60_VSP,
	 {1864, 1400, 1400, 464, 1488, 144, 1089, 1050, 1050, 39, 1053,
	  4} },
	{REFRESH_75, CLK_156_000M, M1400X1050_R75_HSP, M1400X1050_R75_VSP,
	 {1896, 1400, 1400, 496, 1504, 144, 1099, 1050, 1050, 49, 1053, 4} }
};

/* 1400x1050 (CVT Reduce Blanking) */
struct crt_mode_table CRTM1400x1050_RB[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE,  HSS, HSE,   VT,  VA,  VBS, VBE,  VSS, VSE */
	{REFRESH_60, CLK_101_000M, M1400X1050_RB_R60_HSP,
	 M1400X1050_RB_R60_VSP,
	 {1560, 1400, 1400, 160, 1448, 32, 1080, 1050, 1050, 30, 1053, 4} }
};

/* 960x600 (CVT) */
struct crt_mode_table CRTM960x600[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_45_250M, M960X600_R60_HSP, M960X600_R60_VSP,
	 {1216, 960, 960, 256, 992, 96, 624, 600, 600, 24, 603, 6} }
};

/* 1000x600 (GTF) */
struct crt_mode_table CRTM1000x600[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_48_000M, M1000X600_R60_HSP, M1000X600_R60_VSP,
	 {1288, 1000, 1000, 288, 1040, 104, 622, 600, 600, 22, 601, 3} }
};

/* 1024x576 (GTF) */
struct crt_mode_table CRTM1024x576[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_46_996M, M1024X576_R60_HSP, M1024X576_R60_VSP,
	 {1312, 1024, 1024, 288, 1064, 104, 597, 576, 576, 21, 577, 3} }
};

/* 1088x612 (CVT) */
struct crt_mode_table CRTM1088x612[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_52_977M, M1088X612_R60_HSP, M1088X612_R60_VSP,
	 {1392, 1088, 1088, 304, 1136, 104, 636, 612, 612, 24, 615, 5} }
};

/* 1152x720 (CVT) */
struct crt_mode_table CRTM1152x720[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_66_750M, M1152X720_R60_HSP, M1152X720_R60_VSP,
	 {1488, 1152, 1152, 336, 1208, 112, 748, 720, 720, 28, 723, 6} }
};

/* 1200x720 (GTF) */
struct crt_mode_table CRTM1200x720[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_70_159M, M1200X720_R60_HSP, M1200X720_R60_VSP,
	 {1568, 1200, 1200, 368, 1256, 128, 746, 720, 720, 26, 721, 3} }
};

/* 1200x900 (DCON) */
struct crt_mode_table DCON1200x900[] = {
	/* r_rate,          vclk,               hsp,               vsp   */
	{REFRESH_60, CLK_57_275M, M1200X900_R60_HSP, M1200X900_R60_VSP,
	/* The correct htotal is 1240, but this doesn't raster on VX855. */
	/* Via suggested changing to a multiple of 16, hence 1264.       */
	/*  HT,   HA,  HBS, HBE,  HSS, HSE,  VT,  VA, VBS, VBE, VSS, VSE */
	 {1264, 1200, 1200,  64, 1211,  32, 912, 900, 900,  12, 901, 10} }
};

/* 1280x600 (GTF) */
struct crt_mode_table CRTM1280x600[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE,  HSS, HSE, VT,  VA,  VBS, VBE,  VSS, VSE */
	{REFRESH_60, CLK_61_500M, M1280x600_R60_HSP, M1280x600_R60_VSP,
	 {1648, 1280, 1280, 368, 1336, 128, 622, 600, 600, 22, 601, 3} }
};

/* 1360x768 (CVT) */
struct crt_mode_table CRTM1360x768[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_84_750M, M1360X768_R60_HSP, M1360X768_R60_VSP,
	 {1776, 1360, 1360, 416, 1432, 136, 798, 768, 768, 30, 771, 5} }
};

/* 1360x768 (CVT Reduce Blanking) */
struct crt_mode_table CRTM1360x768_RB[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_72_000M, M1360X768_RB_R60_HSP,
	 M1360X768_RB_R60_VSP,
	 {1520, 1360, 1360, 160, 1408, 32, 790, 768, 768, 22, 771, 5} }
};

/* 1366x768 (GTF) */
struct crt_mode_table CRTM1366x768[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_85_860M, M1368X768_R60_HSP, M1368X768_R60_VSP,
	 {1800, 1368, 1368, 432, 1440, 144, 795, 768, 768, 27, 769, 3} },
	{REFRESH_50, CLK_69_924M, M1368X768_R50_HSP, M1368X768_R50_VSP,
	 {1768, 1368, 1368, 400, 1424, 144, 791, 768, 768, 23, 769, 3} }
};

/* 1440x900 (CVT) */
struct crt_mode_table CRTM1440x900[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_106_500M, M1440X900_R60_HSP, M1440X900_R60_VSP,
	 {1904, 1440, 1440, 464, 1520, 152, 934, 900, 900, 34, 903, 6} },
	{REFRESH_75, CLK_136_700M, M1440X900_R75_HSP, M1440X900_R75_VSP,
	 {1936, 1440, 1440, 496, 1536, 152, 942, 900, 900, 42, 903, 6} }
};

/* 1440x900 (CVT Reduce Blanking) */
struct crt_mode_table CRTM1440x900_RB[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_88_750M, M1440X900_RB_R60_HSP,
	 M1440X900_RB_R60_VSP,
	 {1600, 1440, 1440, 160, 1488, 32, 926, 900, 900, 26, 903, 6} }
};

/* 1600x900 (CVT) */
struct crt_mode_table CRTM1600x900[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_118_840M, M1600X900_R60_HSP, M1600X900_R60_VSP,
	 {2112, 1600, 1600, 512, 1688, 168, 934, 900, 900, 34, 903, 5} }
};

/* 1600x900 (CVT Reduce Blanking) */
struct crt_mode_table CRTM1600x900_RB[] = {
	/* r_rate,        vclk,           hsp,        vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_97_750M, M1600X900_RB_R60_HSP,
	 M1600X900_RB_R60_VSP,
	 {1760, 1600, 1600, 160, 1648, 32, 926, 900, 900, 26, 903, 5} }
};

/* 1600x1024 (GTF) */
struct crt_mode_table CRTM1600x1024[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE,  HSS, HSE,   VT,  VA,  VBS, VBE,  VSS, VSE */
	{REFRESH_60, CLK_136_700M, M1600X1024_R60_HSP, M1600X1024_R60_VSP,
	 {2144, 1600, 1600, 544, 1704, 168, 1060, 1024, 1024, 36, 1025, 3} }
};

/* 1792x1344 (DMT) */
struct crt_mode_table CRTM1792x1344[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE,  HSS, HSE,   VT,  VA,  VBS, VBE,  VSS, VSE */
	{REFRESH_60, CLK_204_000M, M1792x1344_R60_HSP, M1792x1344_R60_VSP,
	 {2448, 1792, 1792, 656, 1920, 200, 1394, 1344, 1344, 50, 1345, 3} }
};

/* 1856x1392 (DMT) */
struct crt_mode_table CRTM1856x1392[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE,  HSS, HSE,   VT,  VA,  VBS, VBE,  VSS, VSE */
	{REFRESH_60, CLK_218_500M, M1856x1392_R60_HSP, M1856x1392_R60_VSP,
	 {2528, 1856, 1856, 672, 1952, 224, 1439, 1392, 1392, 47, 1393, 3} }
};

/* 1920x1200 (CVT) */
struct crt_mode_table CRTM1920x1200[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_193_295M, M1920X1200_R60_HSP, M1920X1200_R60_VSP,
	 {2592, 1920, 1920, 672, 2056, 200, 1245, 1200, 1200, 45, 1203, 6} }
};

/* 1920x1200 (CVT with Reduce Blanking) */
struct crt_mode_table CRTM1920x1200_RB[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE, HSS, HSE, VT,  VA,  VBS, VBE, VSS, VSE */
	{REFRESH_60, CLK_153_920M, M1920X1200_RB_R60_HSP,
	 M1920X1200_RB_R60_VSP,
	 {2080, 1920, 1920, 160, 1968, 32, 1235, 1200, 1200, 35, 1203, 6} }
};

/* 2048x1536 (CVT) */
struct crt_mode_table CRTM2048x1536[] = {
	/* r_rate,          vclk,              hsp,             vsp   */
	/* HT,  HA,  HBS, HBE,  HSS, HSE,   VT,  VA,  VBS, VBE,  VSS, VSE */
	{REFRESH_60, CLK_267_250M, M2048x1536_R60_HSP, M2048x1536_R60_VSP,
	 {2800, 2048, 2048, 752, 2200, 224, 1592, 1536, 1536, 56, 1539, 4} }
};

struct VideoModeTable viafb_modes[] = {
	/* Display : 480x640 (GTF) */
	{CRTM480x640, ARRAY_SIZE(CRTM480x640)},

	/* Display : 640x480 */
	{CRTM640x480, ARRAY_SIZE(CRTM640x480)},

	/* Display : 720x480 (GTF) */
	{CRTM720x480, ARRAY_SIZE(CRTM720x480)},

	/* Display : 720x576 (GTF) */
	{CRTM720x576, ARRAY_SIZE(CRTM720x576)},

	/* Display : 800x600 */
	{CRTM800x600, ARRAY_SIZE(CRTM800x600)},

	/* Display : 800x480 (CVT) */
	{CRTM800x480, ARRAY_SIZE(CRTM800x480)},

	/* Display : 848x480 (CVT) */
	{CRTM848x480, ARRAY_SIZE(CRTM848x480)},

	/* Display : 852x480 (GTF) */
	{CRTM852x480, ARRAY_SIZE(CRTM852x480)},

	/* Display : 1024x512 (GTF) */
	{CRTM1024x512, ARRAY_SIZE(CRTM1024x512)},

	/* Display : 1024x600 */
	{CRTM1024x600, ARRAY_SIZE(CRTM1024x600)},

	/* Display : 1024x768 */
	{CRTM1024x768, ARRAY_SIZE(CRTM1024x768)},

	/* Display : 1152x864 */
	{CRTM1152x864, ARRAY_SIZE(CRTM1152x864)},

	/* Display : 1280x768 (GTF) */
	{CRTM1280x768, ARRAY_SIZE(CRTM1280x768)},

	/* Display : 960x600 (CVT) */
	{CRTM960x600, ARRAY_SIZE(CRTM960x600)},

	/* Display : 1000x600 (GTF) */
	{CRTM1000x600, ARRAY_SIZE(CRTM1000x600)},

	/* Display : 1024x576 (GTF) */
	{CRTM1024x576, ARRAY_SIZE(CRTM1024x576)},

	/* Display : 1088x612 (GTF) */
	{CRTM1088x612, ARRAY_SIZE(CRTM1088x612)},

	/* Display : 1152x720 (CVT) */
	{CRTM1152x720, ARRAY_SIZE(CRTM1152x720)},

	/* Display : 1200x720 (GTF) */
	{CRTM1200x720, ARRAY_SIZE(CRTM1200x720)},

	/* Display : 1200x900 (DCON) */
	{DCON1200x900, ARRAY_SIZE(DCON1200x900)},

	/* Display : 1280x600 (GTF) */
	{CRTM1280x600, ARRAY_SIZE(CRTM1280x600)},

	/* Display : 1280x800 (CVT) */
	{CRTM1280x800, ARRAY_SIZE(CRTM1280x800)},

	/* Display : 1280x960 */
	{CRTM1280x960, ARRAY_SIZE(CRTM1280x960)},

	/* Display : 1280x1024 */
	{CRTM1280x1024, ARRAY_SIZE(CRTM1280x1024)},

	/* Display : 1360x768 (CVT) */
	{CRTM1360x768, ARRAY_SIZE(CRTM1360x768)},

	/* Display : 1366x768 */
	{CRTM1366x768, ARRAY_SIZE(CRTM1366x768)},

	/* Display : 1368x768 (GTF) */
	{CRTM1368x768, ARRAY_SIZE(CRTM1368x768)},

	/* Display : 1440x900 (CVT) */
	{CRTM1440x900, ARRAY_SIZE(CRTM1440x900)},

	/* Display : 1440x1050 (GTF) */
	{CRTM1440x1050, ARRAY_SIZE(CRTM1440x1050)},

	/* Display : 1600x900 (CVT) */
	{CRTM1600x900, ARRAY_SIZE(CRTM1600x900)},

	/* Display : 1600x1024 (GTF) */
	{CRTM1600x1024, ARRAY_SIZE(CRTM1600x1024)},

	/* Display : 1600x1200 */
	{CRTM1600x1200, ARRAY_SIZE(CRTM1600x1200)},

	/* Display : 1680x1050 (CVT) */
	{CRTM1680x1050, ARRAY_SIZE(CRTM1680x1050)},

	/* Display : 1792x1344 (DMT) */
	{CRTM1792x1344, ARRAY_SIZE(CRTM1792x1344)},

	/* Display : 1856x1392 (DMT) */
	{CRTM1856x1392, ARRAY_SIZE(CRTM1856x1392)},

	/* Display : 1920x1440 */
	{CRTM1920x1440, ARRAY_SIZE(CRTM1920x1440)},

	/* Display : 2048x1536 */
	{CRTM2048x1536, ARRAY_SIZE(CRTM2048x1536)},

	/* Display : 1280x720 */
	{CRTM1280x720, ARRAY_SIZE(CRTM1280x720)},

	/* Display : 1920x1080 (CVT) */
	{CRTM1920x1080, ARRAY_SIZE(CRTM1920x1080)},

	/* Display : 1920x1200 (CVT) */
	{CRTM1920x1200, ARRAY_SIZE(CRTM1920x1200)},

	/* Display : 1400x1050 (CVT) */
	{CRTM1400x1050, ARRAY_SIZE(CRTM1400x1050)}
};

struct VideoModeTable viafb_rb_modes[] = {
	/* Display : 1360x768 (CVT Reduce Blanking) */
	{CRTM1360x768_RB, ARRAY_SIZE(CRTM1360x768_RB)},

	/* Display : 1440x900 (CVT Reduce Blanking) */
	{CRTM1440x900_RB, ARRAY_SIZE(CRTM1440x900_RB)},

	/* Display : 1400x1050 (CVT Reduce Blanking) */
	{CRTM1400x1050_RB, ARRAY_SIZE(CRTM1400x1050_RB)},

	/* Display : 1600x900 (CVT Reduce Blanking) */
	{CRTM1600x900_RB, ARRAY_SIZE(CRTM1600x900_RB)},

	/* Display : 1680x1050 (CVT Reduce Blanking) */
	{CRTM1680x1050_RB, ARRAY_SIZE(CRTM1680x1050_RB)},

	/* Display : 1920x1080 (CVT Reduce Blanking) */
	{CRTM1920x1080_RB, ARRAY_SIZE(CRTM1920x1080_RB)},

	/* Display : 1920x1200 (CVT Reduce Blanking) */
	{CRTM1920x1200_RB, ARRAY_SIZE(CRTM1920x1200_RB)}
};

struct crt_mode_table CEAM1280x720[] = {
	{REFRESH_60, CLK_74_270M, M1280X720_CEA_R60_HSP,
	 M1280X720_CEA_R60_VSP,
	 /* HT,    HA,   HBS,  HBE,  HSS, HSE,  VT,   VA,  VBS, VBE, VSS, VSE */
	 {1650, 1280, 1280, 370, 1390, 40, 750, 720, 720, 30, 725, 5} }
};
struct crt_mode_table CEAM1920x1080[] = {
	{REFRESH_60, CLK_148_500M, M1920X1080_CEA_R60_HSP,
	 M1920X1080_CEA_R60_VSP,
	 /* HT,    HA,   HBS,  HBE,  HSS, HSE,  VT,  VA, VBS, VBE,  VSS, VSE */
	 {2200, 1920, 1920, 300, 2008, 44, 1125, 1080, 1080, 45, 1084, 5} }
};
struct VideoModeTable CEA_HDMI_Modes[] = {
	/* Display : 1280x720 */
	{CEAM1280x720, ARRAY_SIZE(CEAM1280x720)},
	{CEAM1920x1080, ARRAY_SIZE(CEAM1920x1080)}
};

int NUM_TOTAL_RES_MAP_REFRESH = ARRAY_SIZE(res_map_refresh_tbl);
int NUM_TOTAL_CEA_MODES = ARRAY_SIZE(CEA_HDMI_Modes);
int NUM_TOTAL_CN400_ModeXregs = ARRAY_SIZE(CN400_ModeXregs);
int NUM_TOTAL_CN700_ModeXregs = ARRAY_SIZE(CN700_ModeXregs);
int NUM_TOTAL_KM400_ModeXregs = ARRAY_SIZE(KM400_ModeXregs);
int NUM_TOTAL_CX700_ModeXregs = ARRAY_SIZE(CX700_ModeXregs);
int NUM_TOTAL_VX855_ModeXregs = ARRAY_SIZE(VX855_ModeXregs);
int NUM_TOTAL_CLE266_ModeXregs = ARRAY_SIZE(CLE266_ModeXregs);
int NUM_TOTAL_PATCH_MODE = ARRAY_SIZE(res_patch_table);


struct VideoModeTable *viafb_get_mode(int hres, int vres)
{
	u32 i;
	for (i = 0; i < ARRAY_SIZE(viafb_modes); i++)
		if (viafb_modes[i].mode_array &&
			viafb_modes[i].crtc[0].crtc.hor_addr == hres &&
			viafb_modes[i].crtc[0].crtc.ver_addr == vres)
			return &viafb_modes[i];

	return NULL;
}

struct VideoModeTable *viafb_get_rb_mode(int hres, int vres)
{
	u32 i;
	for (i = 0; i < ARRAY_SIZE(viafb_rb_modes); i++)
		if (viafb_rb_modes[i].mode_array &&
			viafb_rb_modes[i].crtc[0].crtc.hor_addr == hres &&
			viafb_rb_modes[i].crtc[0].crtc.ver_addr == vres)
			return &viafb_rb_modes[i];

	return NULL;
}
