/*
 * Support for Sony IMX camera sensor.
 *
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 */

#ifndef __IMX132_H__
#define __IMX132_H__
#include "common.h"

/********************** registers define ********************************/
#define IMX132_RGLANESEL			0x3301	/* Number of lanes */
#define IMX132_RGLANESEL_1LANE			0x01
#define IMX132_RGLANESEL_2LANES			0x00
#define IMX132_RGLANESEL_4LANES			0x03

#define IMX132_2LANES_GAINFACT			2096	/* 524/256 * 2^10 */
#define IMX132_2LANES_GAINFACT_SHIFT		10

/********************** settings for imx from vendor*********************/
static struct imx_reg imx132_1080p_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* Global Settings */
	{IMX_8BIT, 0x3087, 0x53},
	{IMX_8BIT, 0x308B, 0x5A},
	{IMX_8BIT, 0x3094, 0x11},
	{IMX_8BIT, 0x309D, 0xA4},
	{IMX_8BIT, 0x30AA, 0x01},
	{IMX_8BIT, 0x30C6, 0x00},
	{IMX_8BIT, 0x30C7, 0x00},
	{IMX_8BIT, 0x3118, 0x2F},
	{IMX_8BIT, 0x312A, 0x00},
	{IMX_8BIT, 0x312B, 0x0B},
	{IMX_8BIT, 0x312C, 0x0B},
	{IMX_8BIT, 0x312D, 0x13},
	/* PLL setting */
	{IMX_8BIT, 0x0305, 0x02},
	{IMX_8BIT, 0x0307, 0x50},
	{IMX_8BIT, 0x30A4, 0x02},
	{IMX_8BIT, 0x303C, 0x3C},
	/* Mode setting */
	{IMX_8BIT, 0x0344, 0x00},
	{IMX_8BIT, 0x0345, 0x14},
	{IMX_8BIT, 0x0346, 0x00},
	{IMX_8BIT, 0x0347, 0x32},
	{IMX_8BIT, 0x0348, 0x07},
	{IMX_8BIT, 0x0349, 0xA3},
	{IMX_8BIT, 0x034A, 0x04},
	{IMX_8BIT, 0x034B, 0x79},
	{IMX_8BIT, 0x034C, 0x07},
	{IMX_8BIT, 0x034D, 0x90},
	{IMX_8BIT, 0x034E, 0x04},
	{IMX_8BIT, 0x034F, 0x48},
	{IMX_8BIT, 0x0381, 0x01},
	{IMX_8BIT, 0x0383, 0x01},
	{IMX_8BIT, 0x0385, 0x01},
	{IMX_8BIT, 0x0387, 0x01},
	{IMX_8BIT, 0x303D, 0x10},
	{IMX_8BIT, 0x303E, 0x5A},
	{IMX_8BIT, 0x3040, 0x00},
	{IMX_8BIT, 0x3041, 0x00},
	{IMX_8BIT, 0x3048, 0x00},
	{IMX_8BIT, 0x304C, 0x2F},
	{IMX_8BIT, 0x304D, 0x02},
	{IMX_8BIT, 0x3064, 0x92},
	{IMX_8BIT, 0x306A, 0x10},
	{IMX_8BIT, 0x309B, 0x00},
	{IMX_8BIT, 0x309E, 0x41},
	{IMX_8BIT, 0x30A0, 0x10},
	{IMX_8BIT, 0x30A1, 0x0B},
	{IMX_8BIT, 0x30B2, 0x00},
	{IMX_8BIT, 0x30D5, 0x00},
	{IMX_8BIT, 0x30D6, 0x00},
	{IMX_8BIT, 0x30D7, 0x00},
	{IMX_8BIT, 0x30D8, 0x00},
	{IMX_8BIT, 0x30D9, 0x00},
	{IMX_8BIT, 0x30DA, 0x00},
	{IMX_8BIT, 0x30DB, 0x00},
	{IMX_8BIT, 0x30DC, 0x00},
	{IMX_8BIT, 0x30DD, 0x00},
	{IMX_8BIT, 0x30DE, 0x00},
	{IMX_8BIT, 0x3102, 0x0C},
	{IMX_8BIT, 0x3103, 0x33},
	{IMX_8BIT, 0x3104, 0x18},
	{IMX_8BIT, 0x3105, 0x00},
	{IMX_8BIT, 0x3106, 0x65},
	{IMX_8BIT, 0x3107, 0x00},
	{IMX_8BIT, 0x3108, 0x06},
	{IMX_8BIT, 0x3109, 0x04},
	{IMX_8BIT, 0x310A, 0x04},
	{IMX_8BIT, 0x315C, 0x3D},
	{IMX_8BIT, 0x315D, 0x3C},
	{IMX_8BIT, 0x316E, 0x3E},
	{IMX_8BIT, 0x316F, 0x3D},
	/* Global timing */
	{IMX_8BIT, 0x3304, 0x07}, /* RGTLPX[5:0] TLPX */
	{IMX_8BIT, 0x3305, 0x06}, /* RGTCLKPREPARE[3:0] TCLK-PREPARE */
	{IMX_8BIT, 0x3306, 0x19}, /* RGTCLKZERO[5:0] TCLK-ZERO */
	{IMX_8BIT, 0x3307, 0x03}, /* RGTCLKPRE[5:0] TCLK-PRE */
	{IMX_8BIT, 0x3308, 0x0F}, /* RGTCLKPOST[5:0] TCLK-POST */
	{IMX_8BIT, 0x3309, 0x07}, /* RGTCLKTRAIL[3:0] TCLK-TRAIL */
	{IMX_8BIT, 0x330A, 0x0C}, /* RGTHSEXIT[5:0] THS-EXIT */
	{IMX_8BIT, 0x330B, 0x06}, /* RGTHSPREPARE[3:0] THS-PREPARE */
	{IMX_8BIT, 0x330C, 0x0B}, /* RGTHSZERO[5:0] THS-ZERO */
	{IMX_8BIT, 0x330D, 0x07}, /* RGTHSTRAIL[3:0] THS-TRAIL */
	{IMX_8BIT, 0x330E, 0x03},
	{IMX_8BIT, 0x3318, 0x62},
	{IMX_8BIT, 0x3322, 0x09},
	{IMX_8BIT, 0x3342, 0x00},
	{IMX_8BIT, 0x3348, 0xE0},

	{IMX_TOK_TERM, 0, 0},
};

static struct imx_reg imx132_1456x1096_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* Global Settings */
	{IMX_8BIT, 0x3087, 0x53},
	{IMX_8BIT, 0x308B, 0x5A},
	{IMX_8BIT, 0x3094, 0x11},
	{IMX_8BIT, 0x309D, 0xA4},
	{IMX_8BIT, 0x30AA, 0x01},
	{IMX_8BIT, 0x30C6, 0x00},
	{IMX_8BIT, 0x30C7, 0x00},
	{IMX_8BIT, 0x3118, 0x2F},
	{IMX_8BIT, 0x312A, 0x00},
	{IMX_8BIT, 0x312B, 0x0B},
	{IMX_8BIT, 0x312C, 0x0B},
	{IMX_8BIT, 0x312D, 0x13},
	/* PLL setting */
	{IMX_8BIT, 0x0305, 0x02},
	{IMX_8BIT, 0x0307, 0x50},
	{IMX_8BIT, 0x30A4, 0x02},
	{IMX_8BIT, 0x303C, 0x3C},
	/* Mode setting */
	{IMX_8BIT, 0x0344, 0x01},
	{IMX_8BIT, 0x0345, 0x04},
	{IMX_8BIT, 0x0346, 0x00},
	{IMX_8BIT, 0x0347, 0x32},
	{IMX_8BIT, 0x0348, 0x06},
	{IMX_8BIT, 0x0349, 0xB3},
	{IMX_8BIT, 0x034A, 0x04},
	{IMX_8BIT, 0x034B, 0x79},
	{IMX_8BIT, 0x034C, 0x05},
	{IMX_8BIT, 0x034D, 0xB0},
	{IMX_8BIT, 0x034E, 0x04},
	{IMX_8BIT, 0x034F, 0x48},
	{IMX_8BIT, 0x0381, 0x01},
	{IMX_8BIT, 0x0383, 0x01},
	{IMX_8BIT, 0x0385, 0x01},
	{IMX_8BIT, 0x0387, 0x01},
	{IMX_8BIT, 0x303D, 0x10},
	{IMX_8BIT, 0x303E, 0x5A},
	{IMX_8BIT, 0x3040, 0x00},
	{IMX_8BIT, 0x3041, 0x00},
	{IMX_8BIT, 0x3048, 0x00},
	{IMX_8BIT, 0x304C, 0x2F},
	{IMX_8BIT, 0x304D, 0x02},
	{IMX_8BIT, 0x3064, 0x92},
	{IMX_8BIT, 0x306A, 0x10},
	{IMX_8BIT, 0x309B, 0x00},
	{IMX_8BIT, 0x309E, 0x41},
	{IMX_8BIT, 0x30A0, 0x10},
	{IMX_8BIT, 0x30A1, 0x0B},
	{IMX_8BIT, 0x30B2, 0x00},
	{IMX_8BIT, 0x30D5, 0x00},
	{IMX_8BIT, 0x30D6, 0x00},
	{IMX_8BIT, 0x30D7, 0x00},
	{IMX_8BIT, 0x30D8, 0x00},
	{IMX_8BIT, 0x30D9, 0x00},
	{IMX_8BIT, 0x30DA, 0x00},
	{IMX_8BIT, 0x30DB, 0x00},
	{IMX_8BIT, 0x30DC, 0x00},
	{IMX_8BIT, 0x30DD, 0x00},
	{IMX_8BIT, 0x30DE, 0x00},
	{IMX_8BIT, 0x3102, 0x0C},
	{IMX_8BIT, 0x3103, 0x33},
	{IMX_8BIT, 0x3104, 0x18},
	{IMX_8BIT, 0x3105, 0x00},
	{IMX_8BIT, 0x3106, 0x65},
	{IMX_8BIT, 0x3107, 0x00},
	{IMX_8BIT, 0x3108, 0x06},
	{IMX_8BIT, 0x3109, 0x04},
	{IMX_8BIT, 0x310A, 0x04},
	{IMX_8BIT, 0x315C, 0x3D},
	{IMX_8BIT, 0x315D, 0x3C},
	{IMX_8BIT, 0x316E, 0x3E},
	{IMX_8BIT, 0x316F, 0x3D},
	/* Global timing */
	{IMX_8BIT, 0x3304, 0x07}, /* RGTLPX[5:0] TLPX */
	{IMX_8BIT, 0x3305, 0x06}, /* RGTCLKPREPARE[3:0] TCLK-PREPARE */
	{IMX_8BIT, 0x3306, 0x19}, /* RGTCLKZERO[5:0] TCLK-ZERO */
	{IMX_8BIT, 0x3307, 0x03}, /* RGTCLKPRE[5:0] TCLK-PRE */
	{IMX_8BIT, 0x3308, 0x0F}, /* RGTCLKPOST[5:0] TCLK-POST */
	{IMX_8BIT, 0x3309, 0x07}, /* RGTCLKTRAIL[3:0] TCLK-TRAIL */
	{IMX_8BIT, 0x330A, 0x0C}, /* RGTHSEXIT[5:0] THS-EXIT */
	{IMX_8BIT, 0x330B, 0x06}, /* RGTHSPREPARE[3:0] THS-PREPARE */
	{IMX_8BIT, 0x330C, 0x0B}, /* RGTHSZERO[5:0] THS-ZERO */
	{IMX_8BIT, 0x330D, 0x07}, /* RGTHSTRAIL[3:0] THS-TRAIL */
	{IMX_8BIT, 0x330E, 0x03},
	{IMX_8BIT, 0x3318, 0x62},
	{IMX_8BIT, 0x3322, 0x09},
	{IMX_8BIT, 0x3342, 0x00},
	{IMX_8BIT, 0x3348, 0xE0},

	{IMX_TOK_TERM, 0, 0},
};

static struct imx_reg imx132_1636x1096_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* Global Settings */
	{IMX_8BIT, 0x3087, 0x53},
	{IMX_8BIT, 0x308B, 0x5A},
	{IMX_8BIT, 0x3094, 0x11},
	{IMX_8BIT, 0x309D, 0xA4},
	{IMX_8BIT, 0x30AA, 0x01},
	{IMX_8BIT, 0x30C6, 0x00},
	{IMX_8BIT, 0x30C7, 0x00},
	{IMX_8BIT, 0x3118, 0x2F},
	{IMX_8BIT, 0x312A, 0x00},
	{IMX_8BIT, 0x312B, 0x0B},
	{IMX_8BIT, 0x312C, 0x0B},
	{IMX_8BIT, 0x312D, 0x13},
	/* PLL setting */
	{IMX_8BIT, 0x0305, 0x02},
	{IMX_8BIT, 0x0307, 0x50},
	{IMX_8BIT, 0x30A4, 0x02},
	{IMX_8BIT, 0x303C, 0x3C},
	/* Mode setting */
	{IMX_8BIT, 0x0344, 0x00},
	{IMX_8BIT, 0x0345, 0xAA},
	{IMX_8BIT, 0x0346, 0x00},
	{IMX_8BIT, 0x0347, 0x32},
	{IMX_8BIT, 0x0348, 0x07},
	{IMX_8BIT, 0x0349, 0x0D},
	{IMX_8BIT, 0x034A, 0x04},
	{IMX_8BIT, 0x034B, 0x79},
	{IMX_8BIT, 0x034C, 0x06},
	{IMX_8BIT, 0x034D, 0x64},
	{IMX_8BIT, 0x034E, 0x04},
	{IMX_8BIT, 0x034F, 0x48},
	{IMX_8BIT, 0x0381, 0x01},
	{IMX_8BIT, 0x0383, 0x01},
	{IMX_8BIT, 0x0385, 0x01},
	{IMX_8BIT, 0x0387, 0x01},
	{IMX_8BIT, 0x303D, 0x10},
	{IMX_8BIT, 0x303E, 0x5A},
	{IMX_8BIT, 0x3040, 0x00},
	{IMX_8BIT, 0x3041, 0x00},
	{IMX_8BIT, 0x3048, 0x00},
	{IMX_8BIT, 0x304C, 0x2F},
	{IMX_8BIT, 0x304D, 0x02},
	{IMX_8BIT, 0x3064, 0x92},
	{IMX_8BIT, 0x306A, 0x10},
	{IMX_8BIT, 0x309B, 0x00},
	{IMX_8BIT, 0x309E, 0x41},
	{IMX_8BIT, 0x30A0, 0x10},
	{IMX_8BIT, 0x30A1, 0x0B},
	{IMX_8BIT, 0x30B2, 0x00},
	{IMX_8BIT, 0x30D5, 0x00},
	{IMX_8BIT, 0x30D6, 0x00},
	{IMX_8BIT, 0x30D7, 0x00},
	{IMX_8BIT, 0x30D8, 0x00},
	{IMX_8BIT, 0x30D9, 0x00},
	{IMX_8BIT, 0x30DA, 0x00},
	{IMX_8BIT, 0x30DB, 0x00},
	{IMX_8BIT, 0x30DC, 0x00},
	{IMX_8BIT, 0x30DD, 0x00},
	{IMX_8BIT, 0x30DE, 0x00},
	{IMX_8BIT, 0x3102, 0x0C},
	{IMX_8BIT, 0x3103, 0x33},
	{IMX_8BIT, 0x3104, 0x18},
	{IMX_8BIT, 0x3105, 0x00},
	{IMX_8BIT, 0x3106, 0x65},
	{IMX_8BIT, 0x3107, 0x00},
	{IMX_8BIT, 0x3108, 0x06},
	{IMX_8BIT, 0x3109, 0x04},
	{IMX_8BIT, 0x310A, 0x04},
	{IMX_8BIT, 0x315C, 0x3D},
	{IMX_8BIT, 0x315D, 0x3C},
	{IMX_8BIT, 0x316E, 0x3E},
	{IMX_8BIT, 0x316F, 0x3D},
	/* Global timing */
	{IMX_8BIT, 0x3304, 0x07}, /* RGTLPX[5:0] TLPX */
	{IMX_8BIT, 0x3305, 0x06}, /* RGTCLKPREPARE[3:0] TCLK-PREPARE */
	{IMX_8BIT, 0x3306, 0x19}, /* RGTCLKZERO[5:0] TCLK-ZERO */
	{IMX_8BIT, 0x3307, 0x03}, /* RGTCLKPRE[5:0] TCLK-PRE */
	{IMX_8BIT, 0x3308, 0x0F}, /* RGTCLKPOST[5:0] TCLK-POST */
	{IMX_8BIT, 0x3309, 0x07}, /* RGTCLKTRAIL[3:0] TCLK-TRAIL */
	{IMX_8BIT, 0x330A, 0x0C}, /* RGTHSEXIT[5:0] THS-EXIT */
	{IMX_8BIT, 0x330B, 0x06}, /* RGTHSPREPARE[3:0] THS-PREPARE */
	{IMX_8BIT, 0x330C, 0x0B}, /* RGTHSZERO[5:0] THS-ZERO */
	{IMX_8BIT, 0x330D, 0x07}, /* RGTHSTRAIL[3:0] THS-TRAIL */
	{IMX_8BIT, 0x330E, 0x03},
	{IMX_8BIT, 0x3318, 0x62},
	{IMX_8BIT, 0x3322, 0x09},
	{IMX_8BIT, 0x3342, 0x00},
	{IMX_8BIT, 0x3348, 0xE0},

	{IMX_TOK_TERM, 0, 0},
};

static struct imx_reg imx132_1336x1096_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* Global Settings */
	{IMX_8BIT, 0x3087, 0x53},
	{IMX_8BIT, 0x308B, 0x5A},
	{IMX_8BIT, 0x3094, 0x11},
	{IMX_8BIT, 0x309D, 0xA4},
	{IMX_8BIT, 0x30AA, 0x01},
	{IMX_8BIT, 0x30C6, 0x00},
	{IMX_8BIT, 0x30C7, 0x00},
	{IMX_8BIT, 0x3118, 0x2F},
	{IMX_8BIT, 0x312A, 0x00},
	{IMX_8BIT, 0x312B, 0x0B},
	{IMX_8BIT, 0x312C, 0x0B},
	{IMX_8BIT, 0x312D, 0x13},
	/* PLL setting */
	{IMX_8BIT, 0x0305, 0x02},
	{IMX_8BIT, 0x0307, 0x50},
	{IMX_8BIT, 0x30A4, 0x02},
	{IMX_8BIT, 0x303C, 0x3C},
	/* Mode setting */
	{IMX_8BIT, 0x0344, 0x01},
	{IMX_8BIT, 0x0345, 0x2C},
	{IMX_8BIT, 0x0346, 0x00},
	{IMX_8BIT, 0x0347, 0x32},
	{IMX_8BIT, 0x0348, 0x06},
	{IMX_8BIT, 0x0349, 0x77},
	{IMX_8BIT, 0x034A, 0x04},
	{IMX_8BIT, 0x034B, 0x79},
	{IMX_8BIT, 0x034C, 0x05},
	{IMX_8BIT, 0x034D, 0x38},
	{IMX_8BIT, 0x034E, 0x04},
	{IMX_8BIT, 0x034F, 0x48},
	{IMX_8BIT, 0x0381, 0x01},
	{IMX_8BIT, 0x0383, 0x01},
	{IMX_8BIT, 0x0385, 0x01},
	{IMX_8BIT, 0x0387, 0x01},
	{IMX_8BIT, 0x303D, 0x10},
	{IMX_8BIT, 0x303E, 0x5A},
	{IMX_8BIT, 0x3040, 0x00},
	{IMX_8BIT, 0x3041, 0x00},
	{IMX_8BIT, 0x3048, 0x00},
	{IMX_8BIT, 0x304C, 0x2F},
	{IMX_8BIT, 0x304D, 0x02},
	{IMX_8BIT, 0x3064, 0x92},
	{IMX_8BIT, 0x306A, 0x10},
	{IMX_8BIT, 0x309B, 0x00},
	{IMX_8BIT, 0x309E, 0x41},
	{IMX_8BIT, 0x30A0, 0x10},
	{IMX_8BIT, 0x30A1, 0x0B},
	{IMX_8BIT, 0x30B2, 0x00},
	{IMX_8BIT, 0x30D5, 0x00},
	{IMX_8BIT, 0x30D6, 0x00},
	{IMX_8BIT, 0x30D7, 0x00},
	{IMX_8BIT, 0x30D8, 0x00},
	{IMX_8BIT, 0x30D9, 0x00},
	{IMX_8BIT, 0x30DA, 0x00},
	{IMX_8BIT, 0x30DB, 0x00},
	{IMX_8BIT, 0x30DC, 0x00},
	{IMX_8BIT, 0x30DD, 0x00},
	{IMX_8BIT, 0x30DE, 0x00},
	{IMX_8BIT, 0x3102, 0x0C},
	{IMX_8BIT, 0x3103, 0x33},
	{IMX_8BIT, 0x3104, 0x18},
	{IMX_8BIT, 0x3105, 0x00},
	{IMX_8BIT, 0x3106, 0x65},
	{IMX_8BIT, 0x3107, 0x00},
	{IMX_8BIT, 0x3108, 0x06},
	{IMX_8BIT, 0x3109, 0x04},
	{IMX_8BIT, 0x310A, 0x04},
	{IMX_8BIT, 0x315C, 0x3D},
	{IMX_8BIT, 0x315D, 0x3C},
	{IMX_8BIT, 0x316E, 0x3E},
	{IMX_8BIT, 0x316F, 0x3D},
	/* Global timing */
	{IMX_8BIT, 0x3304, 0x07}, /* RGTLPX[5:0] TLPX */
	{IMX_8BIT, 0x3305, 0x06}, /* RGTCLKPREPARE[3:0] TCLK-PREPARE */
	{IMX_8BIT, 0x3306, 0x19}, /* RGTCLKZERO[5:0] TCLK-ZERO */
	{IMX_8BIT, 0x3307, 0x03}, /* RGTCLKPRE[5:0] TCLK-PRE */
	{IMX_8BIT, 0x3308, 0x0F}, /* RGTCLKPOST[5:0] TCLK-POST */
	{IMX_8BIT, 0x3309, 0x07}, /* RGTCLKTRAIL[3:0] TCLK-TRAIL */
	{IMX_8BIT, 0x330A, 0x0C}, /* RGTHSEXIT[5:0] THS-EXIT */
	{IMX_8BIT, 0x330B, 0x06}, /* RGTHSPREPARE[3:0] THS-PREPARE */
	{IMX_8BIT, 0x330C, 0x0B}, /* RGTHSZERO[5:0] THS-ZERO */
	{IMX_8BIT, 0x330D, 0x07}, /* RGTHSTRAIL[3:0] THS-TRAIL */
	{IMX_8BIT, 0x330E, 0x03},
	{IMX_8BIT, 0x3318, 0x62},
	{IMX_8BIT, 0x3322, 0x09},
	{IMX_8BIT, 0x3342, 0x00},
	{IMX_8BIT, 0x3348, 0xE0},

	{IMX_TOK_TERM, 0, 0},
};

/********************** settings for imx - reference *********************/
static struct imx_reg const imx132_init_settings[] = {
	/* sw reset */
	{ IMX_8BIT, 0x0100, 0x00 },
	{ IMX_8BIT, 0x0103, 0x01 },
	{ IMX_TOK_DELAY, 0, 5},
	{ IMX_8BIT, 0x0103, 0x00 },
	GROUPED_PARAMETER_HOLD_ENABLE,
	/* Global Settings */
	{IMX_8BIT, 0x3087, 0x53},
	{IMX_8BIT, 0x308B, 0x5A},
	{IMX_8BIT, 0x3094, 0x11},
	{IMX_8BIT, 0x309D, 0xA4},
	{IMX_8BIT, 0x30AA, 0x01},
	{IMX_8BIT, 0x30C6, 0x00},
	{IMX_8BIT, 0x30C7, 0x00},
	{IMX_8BIT, 0x3118, 0x2F},
	{IMX_8BIT, 0x312A, 0x00},
	{IMX_8BIT, 0x312B, 0x0B},
	{IMX_8BIT, 0x312C, 0x0B},
	{IMX_8BIT, 0x312D, 0x13},
	GROUPED_PARAMETER_HOLD_DISABLE,
	{ IMX_TOK_TERM, 0, 0}
};

struct imx_resolution imx132_res_preview[] = {
	{
		.desc = "imx132_1080p_30fps",
		.regs = imx132_1080p_30fps,
		.width = 1936,
		.height = 1096,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08F2,
				.lines_per_frame = 0x045C,
			},
			{
			}
		},
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 384000,
	},
};

struct imx_resolution imx132_res_still[] = {
	{
		.desc = "imx132_1080p_30fps",
		.regs = imx132_1080p_30fps,
		.width = 1936,
		.height = 1096,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08F2,
				.lines_per_frame = 0x045C,
			},
			{
			}
		},
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 384000,
	},
};

struct imx_resolution imx132_res_video[] = {
	{
		.desc = "imx132_1336x1096_30fps",
		.regs = imx132_1336x1096_30fps,
		.width = 1336,
		.height = 1096,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08F2,
				.lines_per_frame = 0x045C,
			},
			{
			}
		},
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 384000,
	},
	{
		.desc = "imx132_1456x1096_30fps",
		.regs = imx132_1456x1096_30fps,
		.width = 1456,
		.height = 1096,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08F2,
				.lines_per_frame = 0x045C,
			},
			{
			}
		},
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 384000,
	},
	{
		.desc = "imx132_1636x1096_30fps",
		.regs = imx132_1636x1096_30fps,
		.width = 1636,
		.height = 1096,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08F2,
				.lines_per_frame = 0x045C,
			},
			{
			}
		},
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 384000,
	},
	{
		.desc = "imx132_1080p_30fps",
		.regs = imx132_1080p_30fps,
		.width = 1936,
		.height = 1096,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x08F2,
				.lines_per_frame = 0x045C,
			},
			{
			}
		},
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.used = 0,
		.skip_frames = 2,
		.mipi_freq = 384000,
	},
};
#endif

