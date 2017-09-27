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

struct io_reg CN400_ModeXregs[] = { {VIASR, SR10, 0xFF, 0x01},
{VIASR, SR15, 0x02, 0x02},
{VIASR, SR16, 0xBF, 0x08},
{VIASR, SR17, 0xFF, 0x1F},
{VIASR, SR18, 0xFF, 0x4E},
{VIASR, SR1A, 0xFB, 0x08},
{VIASR, SR1E, 0x0F, 0x01},
{VIASR, SR2A, 0xFF, 0x00},
{VIACR, CR32, 0xFF, 0x00},
{VIACR, CR33, 0xFF, 0x00},
{VIACR, CR35, 0xFF, 0x00},
{VIACR, CR36, 0x08, 0x00},
{VIACR, CR69, 0xFF, 0x00},
{VIACR, CR6A, 0xFF, 0x40},
{VIACR, CR6B, 0xFF, 0x00},
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
{VIACR, CR77, 0xFF, 0x00},	/* LCD scaling Factor */
{VIACR, CR78, 0xFF, 0x00},	/* LCD scaling Factor */
{VIACR, CR79, 0xFF, 0x00},	/* LCD scaling Factor */
{VIACR, CR9F, 0x03, 0x00},	/* LCD scaling Factor */
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
	{VIACR, CR33, 0xFF, 0x00},
	{VIACR, CR55, 0x80, 0x00},
	{VIACR, CR5D, 0x80, 0x00},
	{VIACR, CR36, 0xFF, 0x01},	/* Power Mangement 3                  */
	{VIACR, CR68, 0xFF, 0x67},	/* Default FIFO For IGA2              */
	{VIACR, CR6A, 0x20, 0x20},	/* Extended FIFO On                   */
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
{VIASR, SR2D, 0xC0, 0xC0},	/* delayed E3_ECK */
{VIACR, CR32, 0xFF, 0x00},
{VIACR, CR33, 0xFF, 0x00},
{VIACR, CR35, 0xFF, 0x00},
{VIACR, CR36, 0x08, 0x00},
{VIACR, CR47, 0xC8, 0x00},	/* Clear VCK Plus. */
{VIACR, CR69, 0xFF, 0x00},
{VIACR, CR6A, 0xFF, 0x40},
{VIACR, CR6B, 0xFF, 0x00},
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
{VIASR, SR2D, 0xC0, 0xC0},	/* delayed E3_ECK */
{VIACR, CR32, 0xFF, 0x00},
{VIACR, CR33, 0x7F, 0x00},
{VIACR, CR35, 0xFF, 0x00},
{VIACR, CR36, 0x08, 0x00},
{VIACR, CR69, 0xFF, 0x00},
{VIACR, CR6A, 0xFD, 0x60},
{VIACR, CR6B, 0xFF, 0x00},
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

static const struct fb_videomode viafb_modes[] = {
	{NULL, 60, 480, 640, 40285, 72, 24, 19, 1, 48, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 640, 480, 39682, 48, 16, 33, 10, 96, 2, 0, 0, 0},
	{NULL, 75, 640, 480, 31746, 120, 16, 16, 1, 64, 3, 0, 0, 0},
	{NULL, 85, 640, 480, 27780, 80, 56, 25, 1, 56, 3, 0, 0, 0},
	{NULL, 100, 640, 480, 23167, 104, 40, 25, 1, 64, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 120, 640, 480, 19081, 104, 40, 31, 1, 64, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 720, 480, 37426, 88, 16, 13, 1, 72, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 720, 576, 30611, 96, 24, 17, 1, 72, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 800, 600, 25131, 88, 40, 23, 1, 128, 4, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 75, 800, 600, 20202, 160, 16, 21, 1, 80, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 85, 800, 600, 17790, 152, 32, 27, 1, 64, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 100, 800, 600, 14667, 136, 48, 32, 1, 88, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 120, 800, 600, 11911, 144, 56, 39, 1, 88, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 800, 480, 33602, 96, 24, 10, 3, 72, 7, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 848, 480, 31565, 104, 24, 12, 3, 80, 5, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 856, 480, 31517, 104, 16, 13, 1, 88, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1024, 512, 24218, 136, 32, 15, 1, 104, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1024, 600, 20423, 144, 40, 18, 1, 104, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1024, 768, 15385, 160, 24, 29, 3, 136, 6, 0, 0, 0},
	{NULL, 75, 1024, 768, 12703, 176, 16, 28, 1, 96, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 85, 1024, 768, 10581, 208, 48, 36, 1, 96, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 100, 1024, 768, 8825, 184, 72, 42, 1, 112, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 75, 1152, 864, 9259, 256, 64, 32, 1, 128, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1280, 768, 12478, 200, 64, 23, 1, 136, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 50, 1280, 768, 15342, 184, 56, 19, 1, 128, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 960, 600, 21964, 128, 32, 15, 3, 96, 6, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1000, 600, 20803, 144, 40, 18, 1, 104, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1024, 576, 21278, 144, 40, 17, 1, 104, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1088, 612, 18825, 152, 48, 16, 3, 104, 5, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1152, 720, 14974, 168, 56, 19, 3, 112, 6, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1200, 720, 14248, 184, 56, 22, 1, 128, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 49, 1200, 900, 17703, 21, 11, 1, 1, 32, 10, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1280, 600, 16259, 184, 56, 18, 1, 128, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1280, 800, 11938, 200, 72, 22, 3, 128, 6, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1280, 960, 9259, 312, 96, 36, 1, 112, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1280, 1024, 9262, 248, 48, 38, 1, 112, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 75, 1280, 1024, 7409, 248, 16, 38, 1, 144, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 85, 1280, 1024, 6351, 224, 64, 44, 1, 160, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1360, 768, 11759, 208, 72, 22, 3, 136, 5, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1368, 768, 11646, 216, 72, 23, 1, 144, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 50, 1368, 768, 14301, 200, 56, 19, 1, 144, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1368, 768, 11646, 216, 72, 23, 1, 144, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1440, 900, 9372, 232, 80, 25, 3, 152, 6, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 75, 1440, 900, 7311, 248, 96, 33, 3, 152, 6, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1440, 1040, 7993, 248, 96, 33, 1, 152, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1600, 900, 8449, 256, 88, 26, 3, 168, 5, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1600, 1024, 7333, 272, 104, 32, 1, 168, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1600, 1200, 6172, 304, 64, 46, 1, 192, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 75, 1600, 1200, 4938, 304, 64, 46, 1, 192, 3, FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1680, 1050, 6832, 280, 104, 30, 3, 176, 6, 0, 0, 0},
	{NULL, 75, 1680, 1050, 5339, 296, 120, 40, 3, 176, 6, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1792, 1344, 4883, 328, 128, 46, 1, 200, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1856, 1392, 4581, 352, 96, 43, 1, 224, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1920, 1440, 4273, 344, 128, 56, 1, 208, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 75, 1920, 1440, 3367, 352, 144, 56, 1, 224, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 2048, 1536, 3738, 376, 152, 49, 3, 224, 4, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1280, 720, 13484, 216, 112, 20, 5, 40, 5, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 50, 1280, 720, 16538, 176, 48, 17, 1, 128, 3, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1920, 1080, 5776, 328, 128, 32, 3, 200, 5, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1920, 1200, 5164, 336, 136, 36, 3, 200, 6, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 60, 1400, 1050, 8210, 232, 88, 32, 3, 144, 4, FB_SYNC_VERT_HIGH_ACT, 0, 0},
	{NULL, 75, 1400, 1050, 6398, 248, 104, 42, 3, 144, 4, FB_SYNC_VERT_HIGH_ACT, 0, 0} };

static const struct fb_videomode viafb_rb_modes[] = {
	{NULL, 60, 1360, 768, 13879, 80, 48, 14, 3, 32, 5, FB_SYNC_HOR_HIGH_ACT, 0, 0},
	{NULL, 60, 1440, 900, 11249, 80, 48, 17, 3, 32, 6, FB_SYNC_HOR_HIGH_ACT, 0, 0},
	{NULL, 60, 1400, 1050, 9892, 80, 48, 23, 3, 32, 4, FB_SYNC_HOR_HIGH_ACT, 0, 0},
	{NULL, 60, 1600, 900, 10226, 80, 48, 18, 3, 32, 5, FB_SYNC_HOR_HIGH_ACT, 0, 0},
	{NULL, 60, 1680, 1050, 8387, 80, 48, 21, 3, 32, 6, FB_SYNC_HOR_HIGH_ACT, 0, 0},
	{NULL, 60, 1920, 1080, 7212, 80, 48, 23, 3, 32, 5, FB_SYNC_HOR_HIGH_ACT, 0, 0},
	{NULL, 60, 1920, 1200, 6488, 80, 48, 26, 3, 32, 6, FB_SYNC_HOR_HIGH_ACT, 0, 0} };

int NUM_TOTAL_CN400_ModeXregs = ARRAY_SIZE(CN400_ModeXregs);
int NUM_TOTAL_CN700_ModeXregs = ARRAY_SIZE(CN700_ModeXregs);
int NUM_TOTAL_KM400_ModeXregs = ARRAY_SIZE(KM400_ModeXregs);
int NUM_TOTAL_CX700_ModeXregs = ARRAY_SIZE(CX700_ModeXregs);
int NUM_TOTAL_VX855_ModeXregs = ARRAY_SIZE(VX855_ModeXregs);
int NUM_TOTAL_CLE266_ModeXregs = ARRAY_SIZE(CLE266_ModeXregs);
int NUM_TOTAL_PATCH_MODE = ARRAY_SIZE(res_patch_table);


static const struct fb_videomode *get_best_mode(
	const struct fb_videomode *modes, int n,
	int hres, int vres, int refresh)
{
	const struct fb_videomode *best = NULL;
	int i;

	for (i = 0; i < n; i++) {
		if (modes[i].xres != hres || modes[i].yres != vres)
			continue;

		if (!best || abs(modes[i].refresh - refresh) <
			abs(best->refresh - refresh))
			best = &modes[i];
	}

	return best;
}

const struct fb_videomode *viafb_get_best_mode(int hres, int vres, int refresh)
{
	return get_best_mode(viafb_modes, ARRAY_SIZE(viafb_modes),
		hres, vres, refresh);
}

const struct fb_videomode *viafb_get_best_rb_mode(int hres, int vres,
	int refresh)
{
	return get_best_mode(viafb_rb_modes, ARRAY_SIZE(viafb_rb_modes),
		hres, vres, refresh);
}
