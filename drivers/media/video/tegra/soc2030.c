/*
 * soc2030.c - soc2030 sensor driver
 *
 * Copyright (C) 2010 Google Inc.
 *
 * Contributors:
 *      Rebecca Schultz Zavin <rebecca@android.com>
 *
 * Leverage OV9640.c
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <media/soc2030.h>

struct soc2030_reg {
	u16 addr;
	u16 val;
};

struct soc2030_info {
	int mode;
	struct i2c_client *i2c_client;
	struct soc2030_platform_data *pdata;
};

#define SOC2030_TABLE_END 1
#define SOC2030_MAX_RETRIES 3
#define SOC2030_POLL_RETRIES 5

static struct soc2030_reg mode_1280x720[] = {
	{0x321C, 0x0000},	/* By Pass TxFIFO = 0 */
	{0x98C, 0x2703},	/* Output Width (A) */
	{0x990, 0x0500},	/*       = 1280 */
	{0x98C, 0x2705},	/* Output Height (A) */
	{0x990, 0x02D0},	/*       = 720 */
	{0x98C, 0x2707},	/* Output Width (B) */
	{0x990, 0x0500},	/*       = 1280 */
	{0x98C, 0x2709},	/* Output Height (B) */
	{0x990, 0x02D0},	/*       = 720 */
	{0x98C, 0x270D},	/* Row Start (A) */
	{0x990, 0x0F6},		/*  = 246 */
	{0x98C, 0x270F},	/* Column Start (A) */
	{0x990, 0x0A6},		/*  = 166 */
	{0x98C, 0x2711},	/* Row End (A) */
	{0x990, 0x3CD},		/* = 973 */
	{0x98C, 0x2713},	/* Column End (A) */
	{0x990, 0x5AD},		/*  = 1453 */
	{0x98C, 0x2715},	/* Row Speed (A) */
	{0x990, 0x0111},	/*       = 273 */
	{0x98C, 0x2717},	/* Read Mode (A) */
	{0x990, 0x0024},	/*       = 36 */
	{0x98C, 0x2719},	/* sensor_fine_correction (A) */
	{0x990, 0x003A},	/*       = 58 */
	{0x98C, 0x271B},	/* sensor_fine_IT_min (A) */
	{0x990, 0x00F6},	/*       = 246 */
	{0x98C, 0x271D},	/* sensor_fine_IT_max_margin (A) */
	{0x990, 0x008B},	/*       = 139 */
	{0x98C, 0x271F},	/* Frame Lines (A) */
	{0x990, 0x032D},	/*       = 813 */
	{0x98C, 0x2721},	/* Line Length (A) */
	{0x990, 0x067C},	/*       = 1660 */
	{0x98C, 0x2723},	/* Row Start (B) */
	{0x990, 0x0F6},		/*  = 246 */
	{0x98C, 0x2725},	/* Column Start (B) */
	{0x990, 0x0A6},		/*  = 166 */
	{0x98C, 0x2727},	/* Row End (B) */
	{0x990, 0x3CD},		/*  = 973 */
	{0x98C, 0x2729},	/* Column End (B) */
	{0x990, 0x5AD},		/*  = 1453 */
	{0x98C, 0x272B},	/* Row Speed (B) */
	{0x990, 0x0111},	/*       = 273 */
	{0x98C, 0x272D},	/* Read Mode (B) */
	{0x990, 0x0024},	/*       = 36 */
	{0x98C, 0x272F},	/* sensor_fine_correction (B) */
	{0x990, 0x003A},	/*       = 58 */
	{0x98C, 0x2731},	/* sensor_fine_IT_min (B) */
	{0x990, 0x00F6},	/*       = 246 */
	{0x98C, 0x2733},	/* sensor_fine_IT_max_margin (B) */
	{0x990, 0x008B},	/*       = 139 */
	{0x98C, 0x2735},	/* Frame Lines (B) */
	{0x990, 0x032D},	/*       = 813 */
	{0x98C, 0x2737},	/* Line Length (B) */
	{0x990, 0x067C},	/*       = 1660 */
	{0x98C, 0x2739},	/* Crop_X0 (A) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x273B},	/* Crop_X1 (A) */
	{0x990, 0x04FF},	/*       = 1279 */
	{0x98C, 0x273D},	/* Crop_Y0 (A) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x273F},	/* Crop_Y1 (A) */
	{0x990, 0x02CF},	/*       = 719 */
	{0x98C, 0x2747},	/* Crop_X0 (B) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x2749},	/* Crop_X1 (B) */
	{0x990, 0x04FF},	/*       = 1279 */
	{0x98C, 0x274B},	/* Crop_Y0 (B) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x274D},	/* Crop_Y1 (B) */
	{0x990, 0x02CF},	/*       = 719 */
	{0x98C, 0x222D},	/* R9 Step */
	{0x990, 0x00CB},	/*       = 203 */
	{0x98C, 0xA408},	/* search_f1_50 */
	{0x990, 0x31},		/*  = 49 */
	{0x98C, 0xA409},	/* search_f2_50 */
	{0x990, 0x33},		/*  = 51 */
	{0x98C, 0xA40A},	/* search_f1_60 */
	{0x990, 0x3C},		/*  = 60 */
	{0x98C, 0xA40B},	/* search_f2_60 */
	{0x990, 0x3E},		/*  = 62 */
	{0x98C, 0x2411},	/* R9_Step_60 (A) */
	{0x990, 0x00CB},	/*       = 203 */
	{0x98C, 0x2413},	/* R9_Step_50 (A) */
	{0x990, 0x00F4},	/*       = 244 */
	{0x98C, 0x2415},	/* R9_Step_60 (B) */
	{0x990, 0x00CB},	/*       = 203 */
	{0x98C, 0x2417},	/* R9_Step_50 (B) */
	{0x990, 0x00F4},	/*       = 244 */
	{0x98C, 0xA404},	/* FD Mode */
	{0x990, 0x10},		/*  = 16 */
	{0x98C, 0xA40D},	/* Stat_min */
	{0x990, 0x02},		/*  = 2 */
	{0x98C, 0xA40E},	/* Stat_max */
	{0x990, 0x03},		/*  = 3 */
	{0x98C, 0xA410},	/* Min_amplitude */
	{0x990, 0x0A},		/*  = 10 */

	/* fix at 30 fps */
	{0x098C, 0xA215},	/*  MCU_ADDRESS [RESERVED_AE_15] */
	{0x0990, 0x0001},	/*  MCU_DATA_0 */
	{0x098C, 0xA20C},	/*  MCU_ADDRESS [AE_MAX_INDEX] */
	{0x0990, 0x0001},	/*  MCU_DATA_0 */

	{SOC2030_TABLE_END, 0x0000}

};

/**
 * SetMode Sequence for 1280X720.Phase 0. Sensor Dependent.
 * This sequence should put sensor in streaming mode for 2592x1944
 * This is usually given by the FAE or the sensor vendor.
 */
static struct soc2030_reg mode_1600x1200[] = {
	{0x321C, 0x0000},	/* By Pass TxFIFO = 0 */
	{0x98C, 0x2703},	/* Output Width (A) */
	{0x990, 0x0640},	/*       = 1600 */
	{0x98C, 0x2705},	/* Output Height (A) */
	{0x990, 0x04B0},	/*       = 1200 */
	{0x98C, 0x2707},	/* Output Width (B) */
	{0x990, 0x0640},	/*       = 1600 */
	{0x98C, 0x2709},	/* Output Height (B) */
	{0x990, 0x04B0},	/*       = 1200 */
	{0x98C, 0x270D},	/* Row Start (A) */
	{0x990, 0x004},		/*  = 4 */
	{0x98C, 0x270F},	/* Column Start (A) */
	{0x990, 0x004},		/*  = 4 */
	{0x98C, 0x2711},	/* Row End (A) */
	{0x990, 0x4BB},		/*  = 1211 */
	{0x98C, 0x2713},	/* Column End (A) */
	{0x990, 0x64B},		/*  = 1611 */
	{0x98C, 0x2715},	/* Row Speed (A) */
	{0x990, 0x0111},	/*       = 273 */
	{0x98C, 0x2717},	/* Read Mode (A) */
	{0x990, 0x0024},	/*       = 36 */
	{0x98C, 0x2719},	/* sensor_fine_correction (A) */
	{0x990, 0x003A},	/*       = 58 */
	{0x98C, 0x271B},	/* sensor_fine_IT_min (A) */
	{0x990, 0x00F6},	/*       = 246 */
	{0x98C, 0x271D},	/* sensor_fine_IT_max_margin (A) */
	{0x990, 0x008B},	/*       = 139 */
	{0x98C, 0x271F},	/* Frame Lines (A) */
	{0x990, 0x050D},	/*       = 1293 */
	{0x98C, 0x2721},	/* Line Length (A) */
	{0x990, 0x085A},	/*       = 2138 */
	{0x98C, 0x2723},	/* Row Start (B) */
	{0x990, 0x004},		/*  = 4 */
	{0x98C, 0x2725},	/* Column Start (B) */
	{0x990, 0x004},		/*  = 4 */
	{0x98C, 0x2727},	/* Row End (B) */
	{0x990, 0x4BB},		/*  = 1211 */
	{0x98C, 0x2729},	/* Column End (B) */
	{0x990, 0x64B},		/*  = 1611 */
	{0x98C, 0x272B},	/* Row Speed (B) */
	{0x990, 0x0111},	/*       = 273 */
	{0x98C, 0x272D},	/* Read Mode (B) */
	{0x990, 0x0024},	/*       = 36 */
	{0x98C, 0x272F},	/* sensor_fine_correction (B) */
	{0x990, 0x003A},	/*       = 58 */
	{0x98C, 0x2731},	/* sensor_fine_IT_min (B) */
	{0x990, 0x00F6},	/*       = 246 */
	{0x98C, 0x2733},	/* sensor_fine_IT_max_margin (B) */
	{0x990, 0x008B},	/*       = 139 */
	{0x98C, 0x2735},	/* Frame Lines (B) */
	{0x990, 0x050D},	/*       = 1293 */
	{0x98C, 0x2737},	/* Line Length (B) */
	{0x990, 0x085A},	/*       = 2138 */
	{0x98C, 0x2739},	/* Crop_X0 (A) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x273B},	/* Crop_X1 (A) */
	{0x990, 0x063F},	/*       = 1599 */
	{0x98C, 0x273D},	/* Crop_Y0 (A) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x273F},	/* Crop_Y1 (A) */
	{0x990, 0x04AF},	/*       = 1199 */
	{0x98C, 0x2747},	/* Crop_X0 (B) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x2749},	/* Crop_X1 (B) */
	{0x990, 0x063F},	/*       = 1599 */
	{0x98C, 0x274B},	/* Crop_Y0 (B) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x274D},	/* Crop_Y1 (B) */
	{0x990, 0x04AF},	/*       = 1199 */
	{0x98C, 0x222D},	/* R9 Step */
	{0x990, 0x009E},	/*       = 158 */
	{0x98C, 0xA408},	/* search_f1_50 */
	{0x990, 0x26},		/*  = 38 */
	{0x98C, 0xA409},	/* search_f2_50 */
	{0x990, 0x28},		/*  = 40 */
	{0x98C, 0xA40A},	/* search_f1_60 */
	{0x990, 0x2E},		/*  = 46 */
	{0x98C, 0xA40B},	/* search_f2_60 */
	{0x990, 0x30},		/*  = 48 */
	{0x98C, 0x2411},	/* R9_Step_60 (A) */
	{0x990, 0x009E},	/*       = 158 */
	{0x98C, 0x2413},	/* R9_Step_50 (A) */
	{0x990, 0x00BD},	/*       = 189 */
	{0x98C, 0x2415},	/* R9_Step_60 (B) */
	{0x990, 0x009E},	/*       = 158 */
	{0x98C, 0x2417},	/* R9_Step_50 (B) */
	{0x990, 0x00BD},	/*       = 189 */
	{0x98C, 0xA404},	/* FD Mode */
	{0x990, 0x10},		/*  = 16 */
	{0x98C, 0xA40D},	/* Stat_min */
	{0x990, 0x02},		/*  = 2 */
	{0x98C, 0xA40E},	/* Stat_max */
	{0x990, 0x03},		/*  = 3 */
	{0x98C, 0xA410},	/* Min_amplitude */
	{0x990, 0x0A},		/*  = 10 */

	/* fix at 30 fps */
	{0x098C, 0xA215},	/*  MCU_ADDRESS [RESERVED_AE_15] */
	{0x0990, 0x0001},	/*  MCU_DATA_0 */
	{0x098C, 0xA20C},	/*  MCU_ADDRESS [AE_MAX_INDEX] */
	{0x0990, 0x0001},	/*  MCU_DATA_0 */

	{SOC2030_TABLE_END, 0x0000}

};

/**
 * SetMode Sequence for 1280X720.Phase 0. Sensor Dependent.
 * This sequence should put sensor in streaming mode for 2592x1944
 * This is usually given by the FAE or the sensor vendor.
 */
static struct soc2030_reg mode_800x600[] = {
	{0x321C, 0x0000},	/* By Pass TxFIFO = 0 */
	{0x98C, 0x2703},	/* Output Width (A) */
	{0x990, 0x0320},	/*       = 800 */
	{0x98C, 0x2705},	/* Output Height (A) */
	{0x990, 0x0258},	/*       = 600 */
	{0x98C, 0x2707},	/* Output Width (B) */
	{0x990, 0x0640},	/*       = 1600 */
	{0x98C, 0x2709},	/* Output Height (B) */
	{0x990, 0x04B0},	/*       = 1200 */
	{0x98C, 0x270D},	/* Row Start (A) */
	{0x990, 0x000},		/*  = 0 */
	{0x98C, 0x270F},	/* Column Start (A) */
	{0x990, 0x000},		/*  = 0 */
	{0x98C, 0x2711},	/* Row End (A) */
	{0x990, 0x4BD},		/*  = 1213 */
	{0x98C, 0x2713},	/* Column End (A) */
	{0x990, 0x64D},		/*  = 1613 */
	{0x98C, 0x2715},	/* Row Speed (A) */
	{0x990, 0x0111},	/*       = 273 */
	{0x98C, 0x2717},	/* Read Mode (A) */
	{0x990, 0x046C},	/*       = 1132 */
	{0x98C, 0x2719},	/* sensor_fine_correction (A) */
	{0x990, 0x005A},	/*       = 90 */
	{0x98C, 0x271B},	/* sensor_fine_IT_min (A) */
	{0x990, 0x01BE},	/*       = 446 */
	{0x98C, 0x271D},	/* sensor_fine_IT_max_margin (A) */
	{0x990, 0x0131},	/*       = 305 */
	{0x98C, 0x271F},	/* Frame Lines (A) */
	{0x990, 0x02B3},	/*       = 691 */
	{0x98C, 0x2721},	/* Line Length (A) */
	{0x990, 0x0853},	/*       = 2131 */
	{0x98C, 0x2723},	/* Row Start (B) */
	{0x990, 0x004},		/*  = 4 */
	{0x98C, 0x2725},	/* Column Start (B) */
	{0x990, 0x004},		/*  = 4 */
	{0x98C, 0x2727},	/* Row End (B) */
	{0x990, 0x4BB},		/*  = 1211 */
	{0x98C, 0x2729},	/* Column End (B) */
	{0x990, 0x64B},		/*  = 1611 */
	{0x98C, 0x272B},	/* Row Speed (B) */
	{0x990, 0x0111},	/*       = 273 */
	{0x98C, 0x272D},	/* Read Mode (B) */
	{0x990, 0x0024},	/*       = 36 */
	{0x98C, 0x272F},	/* sensor_fine_correction (B) */
	{0x990, 0x003A},	/*       = 58 */
	{0x98C, 0x2731},	/* sensor_fine_IT_min (B) */
	{0x990, 0x00F6},	/*       = 246 */
	{0x98C, 0x2733},	/* sensor_fine_IT_max_margin (B) */
	{0x990, 0x008B},	/*       = 139 */
	{0x98C, 0x2735},	/* Frame Lines (B) */
	{0x990, 0x050D},	/*       = 1293 */
	{0x98C, 0x2737},	/* Line Length (B) */
	{0x990, 0x085A},	/*       = 2138 */
	{0x98C, 0x2739},	/* Crop_X0 (A) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x273B},	/* Crop_X1 (A) */
	{0x990, 0x031F},	/*       = 799 */
	{0x98C, 0x273D},	/* Crop_Y0 (A) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x273F},	/* Crop_Y1 (A) */
	{0x990, 0x0257},	/*       = 599 */
	{0x98C, 0x2747},	/* Crop_X0 (B) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x2749},	/* Crop_X1 (B) */
	{0x990, 0x063F},	/*       = 1599 */
	{0x98C, 0x274B},	/* Crop_Y0 (B) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x274D},	/* Crop_Y1 (B) */
	{0x990, 0x04AF},	/*       = 1199 */
	{0x98C, 0x222D},	/* R9 Step */
	{0x990, 0x009E},	/*       = 158 */
	{0x98C, 0xA408},	/* search_f1_50 */
	{0x990, 0x26},		/*  = 38 */
	{0x98C, 0xA409},	/* search_f2_50 */
	{0x990, 0x28},		/*  = 40 */
	{0x98C, 0xA40A},	/* search_f1_60 */
	{0x990, 0x2E},		/*  = 46 */
	{0x98C, 0xA40B},	/* search_f2_60 */
	{0x990, 0x30},		/*  = 48 */
	{0x98C, 0x2411},	/* R9_Step_60 (A) */
	{0x990, 0x009E},	/*       = 158 */
	{0x98C, 0x2413},	/* R9_Step_50 (A) */
	{0x990, 0x00BE},	/*       = 190 */
	{0x98C, 0x2415},	/* R9_Step_60 (B) */
	{0x990, 0x009E},	/*       = 158 */
	{0x98C, 0x2417},	/* R9_Step_50 (B) */
	{0x990, 0x00BD},	/*       = 189 */
	{0x98C, 0xA404},	/* FD Mode */
	{0x990, 0x10},		/*  = 16 */
	{0x98C, 0xA40D},	/* Stat_min */
	{0x990, 0x02},		/*  = 2 */
	{0x98C, 0xA40E},	/* Stat_max */
	{0x990, 0x03},		/*  = 3 */
	{0x98C, 0xA410},	/* Min_amplitude */
	{0x990, 0x0A},		/*  = 10 */
#if 0
	/* fix at 30 fps */
	{0x098C, 0xA215},	/*  MCU_ADDRESS [RESERVED_AE_15] */
	{0x0990, 0x0001},	/*  MCU_DATA_0 */
	{0x098C, 0xA20C},	/*  MCU_ADDRESS [AE_MAX_INDEX] */
	{0x0990, 0x0001},	/*  MCU_DATA_0 */
#endif

	{SOC2030_TABLE_END, 0x0000}

};

/**
 * SetMode Sequence for 1280X720.Phase 0. Sensor Dependent.
 * This sequence should put sensor in streaming mode for 2592x1944
 * This is usually given by the FAE or the sensor vendor.
 */
static struct soc2030_reg mode_816x616[] = {
	{0x321C, 0x0000},	/* By Pass TxFIFO = 0 */
	{0x98C, 0x2703},	/* Output Width (A) */
	{0x990, 0x0330},	/*       = 816 */
	{0x98C, 0x2705},	/* Output Height (A) */
	{0x990, 0x0268},	/*       = 616 */
	{0x98C, 0x2707},	/* Output Width (B) */
	{0x990, 0x0640},	/*       = 1600 */
	{0x98C, 0x2709},	/* Output Height (B) */
	{0x990, 0x04B0},	/*       = 1200 */
	{0x98C, 0x270D},	/* Row Start (A) */
	{0x990, 0x12A},		/*  = 298 */
	{0x98C, 0x270F},	/* Column Start (A) */
	{0x990, 0x18E},		/*  = 398 */
	{0x98C, 0x2711},	/* Row End (A) */
	{0x990, 0x399},		/*  = 921 */
	{0x98C, 0x2713},	/* Column End (A) */
	{0x990, 0x4C5},		/*  = 1221 */
	{0x98C, 0x2715},	/* Row Speed (A) */
	{0x990, 0x0111},	/*       = 273 */
	{0x98C, 0x2717},	/* Read Mode (A) */
	{0x990, 0x0024},	/*       = 36 */
	{0x98C, 0x2719},	/* sensor_fine_correction (A) */
	{0x990, 0x003A},	/*       = 58 */
	{0x98C, 0x271B},	/* sensor_fine_IT_min (A) */
	{0x990, 0x00F6},	/*       = 246 */
	{0x98C, 0x271D},	/* sensor_fine_IT_max_margin (A) */
	{0x990, 0x008B},	/*       = 139 */
	{0x98C, 0x271F},	/* Frame Lines (A) */
	{0x990, 0x02C5},	/*       = 709 */
	{0x98C, 0x2721},	/* Line Length (A) */
	{0x990, 0x081D},	/*       = 2077 */
	{0x98C, 0x2723},	/* Row Start (B) */
	{0x990, 0x004},		/*  = 4 */
	{0x98C, 0x2725},	/* Column Start (B) */
	{0x990, 0x004},		/*  = 4 */
	{0x98C, 0x2727},	/* Row End (B) */
	{0x990, 0x4BB},		/*  = 1211 */
	{0x98C, 0x2729},	/* Column End (B) */
	{0x990, 0x64B},		/*  = 1611 */
	{0x98C, 0x272B},	/* Row Speed (B) */
	{0x990, 0x0111},	/*       = 273 */
	{0x98C, 0x272D},	/* Read Mode (B) */
	{0x990, 0x0024},	/*       = 36 */
	{0x98C, 0x272F},	/* sensor_fine_correction (B) */
	{0x990, 0x003A},	/*       = 58 */
	{0x98C, 0x2731},	/* sensor_fine_IT_min (B) */
	{0x990, 0x00F6},	/*       = 246 */
	{0x98C, 0x2733},	/* sensor_fine_IT_max_margin (B) */
	{0x990, 0x008B},	/*       = 139 */
	{0x98C, 0x2735},	/* Frame Lines (B) */
	{0x990, 0x050D},	/*       = 1293 */
	{0x98C, 0x2737},	/* Line Length (B) */
	{0x990, 0x085A},	/*       = 2138 */
	{0x98C, 0x2739},	/* Crop_X0 (A) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x273B},	/* Crop_X1 (A) */
	{0x990, 0x032F},	/*       = 815 */
	{0x98C, 0x273D},	/* Crop_Y0 (A) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x273F},	/* Crop_Y1 (A) */
	{0x990, 0x0267},	/*       = 615 */
	{0x98C, 0x2747},	/* Crop_X0 (B) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x2749},	/* Crop_X1 (B) */
	{0x990, 0x063F},	/*       = 1599 */
	{0x98C, 0x274B},	/* Crop_Y0 (B) */
	{0x990, 0x0000},	/*       = 0 */
	{0x98C, 0x274D},	/* Crop_Y1 (B) */
	{0x990, 0x04AF},	/*       = 1199 */
	{0x98C, 0x222D},	/* R9 Step */
	{0x990, 0x00A2},	/*       = 162 */
	{0x98C, 0xA408},	/* search_f1_50 */
	{0x990, 0x27},		/*  = 39 */
	{0x98C, 0xA409},	/* search_f2_50 */
	{0x990, 0x29},		/*  = 41 */
	{0x98C, 0xA40A},	/* search_f1_60 */
	{0x990, 0x2F},		/*  = 47 */
	{0x98C, 0xA40B},	/* search_f2_60 */
	{0x990, 0x31},		/*  = 49 */
	{0x98C, 0x2411},	/* R9_Step_60 (A) */
	{0x990, 0x00A2},	/*       = 162 */
	{0x98C, 0x2413},	/* R9_Step_50 (A) */
	{0x990, 0x00C3},	/*       = 195 */
	{0x98C, 0x2415},	/* R9_Step_60 (B) */
	{0x990, 0x009E},	/*       = 158 */
	{0x98C, 0x2417},	/* R9_Step_50 (B) */
	{0x990, 0x00BD},	/*       = 189 */
	{0x98C, 0xA404},	/* FD Mode */
	{0x990, 0x10},		/*  = 16 */
	{0x98C, 0xA40D},	/* Stat_min */
	{0x990, 0x02},		/*  = 2 */
	{0x98C, 0xA40E},	/* Stat_max */
	{0x990, 0x03},		/*  = 3 */
	{0x98C, 0xA410},	/* Min_amplitude */
	{0x990, 0x0A},		/*  = 10 */

	/* fix at 30 fps */
	{0x098C, 0xA215},	/*  MCU_ADDRESS [RESERVED_AE_15] */
	{0x0990, 0x0001},	/*  MCU_DATA_0 */
	{0x098C, 0xA20C},	/*  MCU_ADDRESS [AE_MAX_INDEX] */
	{0x0990, 0x0001},	/*  MCU_DATA_0 */

	{SOC2030_TABLE_END, 0x0000}

};

static struct soc2030_reg pll_table[] = {
	{0x001e, 0x0707},	/*  Pad Slew rate */
	{0x0014, 0x21F9},	/*  PLL_CONTROL */
	{0x0010, 0x011B},	/*  PLL_DIVIDERS */
	{0x0012, 0x10F7},	/*  PLL_P_DIVIDERS */
	{0x0014, 0x21FB},	/*  PLL_CONTROL */
	{0x0014, 0x20FB},	/*  PLL_CONTROL */
	{SOC2030_TABLE_END, 0x0000}
};

enum {
	SOC2030_MODE_800x600,
	SOC2030_MODE_1600x1200,
};

static struct soc2030_reg *mode_table[] = {
	[SOC2030_MODE_800x600] = mode_800x600,
	[SOC2030_MODE_1600x1200] = mode_1600x1200,
};

static int soc2030_read_reg(struct i2c_client *client, u16 addr, u16 *val)
{
	int err;
	struct i2c_msg msg[2];
	unsigned char data[4];

	if (!client->adapter)
		return -ENODEV;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = 2;
	msg[0].buf = data;
	data[0] = (u8) (addr >> 8);
	data[1] = (u8) (addr & 0xff);

	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = 2;
	msg[1].buf = data + 2;
	err = i2c_transfer(client->adapter, msg, 2);

	if (err != 2)
		return -EIO;

	*val = ((u16)(data[2] << 8)) | data[3];

	return 0;
}

static int soc2030_write_reg(struct i2c_client *client, u16 addr, u16 val)
{
	int err;
	struct i2c_msg msg;
	unsigned char data[4];
	int retry = 0;

	if (!client->adapter)
		return -ENODEV;

	data[0] = (u8) (addr >> 8);
	data[1] = (u8) (addr & 0xff);
	data[2] = (u8) (val >> 8);
	data[3] = (u8) (val & 0xff);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 4;
	msg.buf = data;

	do {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err == 1)
			return 0;
		retry++;
		pr_err("soc2030: i2c transfer failed, retrying %x %x\n",
		       addr, val);
		msleep(3);
	} while (retry <= SOC2030_MAX_RETRIES);

	return retry > SOC2030_MAX_RETRIES ? -EIO : err;
}

static int soc2030_write_table(struct i2c_client *client,
			       const struct soc2030_reg table[])
{
	int err;
	const struct soc2030_reg *next;

	for (next = table; next->addr != SOC2030_TABLE_END; next++) {
		err = soc2030_write_reg(client, next->addr, next->val);
		if (err)
			return err;
	}
	return 0;
}

static int soc2030_write_bits(struct i2c_client *client, u16 addr, u16 val,
			      u16 mask)
{
	u16 rval, wval;
	int err;

	err = soc2030_read_reg(client, addr, &rval);
	if (err) {
		pr_err("soc2030: error reading from %x\n", addr);
		return err;
	}
	wval = ((~mask) & rval) | val;
	err = soc2030_write_reg(client, addr, wval);
	return err;
}

static int soc2030_clear_bits(struct i2c_client *client, u16 addr, u16 bits)
{
	return soc2030_write_bits(client, addr, 0, bits);
}

static int soc2030_set_bits(struct i2c_client *client, u16 addr, u16 bits)
{
	return soc2030_write_bits(client, addr, bits, bits);
}

static int soc2030_poll(struct i2c_client *client, u16 addr, u16 expected,
			u16 mask)
{
	u16 val;
	int try, err;

	for (try = 0; try < SOC2030_POLL_RETRIES; try++) {
		err = soc2030_read_reg(client, addr, &val);
		if (err)
			return err;
		if (expected == (val & mask)) {
			pr_info("poll success %x: %x == %x & %x\n", addr,
				expected, val, mask);
			return 0;
		}
		msleep(50);
	}
	pr_err("soc2030: poll for %x == ([%x]=%x) & %x failed\n", expected,
	       addr, val, mask);
	return -EIO;

}

static int soc2030_poll_bit_set(struct i2c_client *client, u16 addr, u16 bit)
{
	return soc2030_poll(client, addr, bit, bit);
}

static int soc2030_poll_bit_clear(struct i2c_client *client, u16 addr, u16 bit)
{
	return soc2030_poll(client, addr, 0, bit);
}

static int soc2030_write_xdma_reg(struct i2c_client *client, u16 addr, u16 val)
{
	int err;

	err = soc2030_write_reg(client, 0x098c, addr);
	if (err)
		return err;
	err = soc2030_write_reg(client, 0x0990, val);
	if (err)
		return err;
	return 0;
}

static int soc2030_read_xdma_reg(struct i2c_client *client, u16 addr, u16 *val)
{
	int err;

	err = soc2030_write_reg(client, 0x098c, addr);
	if (err)
		return err;
	err = soc2030_read_reg(client, 0x0990, val);
	if (err)
		return err;
	return 0;
}

static int soc2030_poll_xdma_reg(struct i2c_client *client, u16 addr,
				 u16 expected)
{
	int try, err;
	u16 val;

	for (try = 0; try < SOC2030_POLL_RETRIES; try++) {
		err = soc2030_read_xdma_reg(client, addr, &val);
		if (err)
			return err;
		if (expected == val)
			return 0;
		msleep(50);
	}
	pr_err("soc2030: xdma poll for %x == ([%x]=%x) failed\n", expected,
	       addr, val);
	return -EINVAL;
}

static int soc2030_set_mode(struct soc2030_info *info,
			    struct soc2030_mode *mode)
{
	int sensor_mode, err;

	if (mode->xres == 800 && mode->yres == 600)
		sensor_mode = SOC2030_MODE_800x600;
	else if (mode->xres == 1600 && mode->yres == 1200)
		sensor_mode = SOC2030_MODE_1600x1200;
	else {
		pr_err("%s: invalid resolution supplied to set mode %d %d\n",
		       __func__, mode->xres, mode->yres);
		return -EINVAL;
	}

	/* reset */
	soc2030_set_bits(info->i2c_client, 0x001a, 0x3);
	msleep(10);
	soc2030_clear_bits(info->i2c_client, 0x001a, 0x1);

	/* write the pll table */
	soc2030_write_table(info->i2c_client, pll_table);

	/* wait for pll lock */
	err = soc2030_poll_bit_set(info->i2c_client, 0x0014, 0x8000);
	if (err)
		return err;

	/* enable the pll */
	soc2030_clear_bits(info->i2c_client, 0x0014, 0x0001);
	/* enable parallel output */
	soc2030_set_bits(info->i2c_client, 0x001a, 0x200);
	/* disable mipi */
	soc2030_clear_bits(info->i2c_client, 0x001a, 0x4);
	/* disable mcu */
	soc2030_set_bits(info->i2c_client, 0x0018, 0x4);
	/* leave standby */
	soc2030_clear_bits(info->i2c_client, 0x0018, 0x1);
	/* wait to complete leave standby */
	soc2030_poll_bit_clear(info->i2c_client, 0x0018, 0x4000);

	err = soc2030_write_table(info->i2c_client, mode_table[sensor_mode]);
	if (err)
		return err;

	/* invert the pixel clock */
	soc2030_write_xdma_reg(info->i2c_client, 0x2755, 0x0200);
	soc2030_write_xdma_reg(info->i2c_client, 0x2757, 0x0200);

	/* enable mcu */
	soc2030_clear_bits(info->i2c_client, 0x0018, 0x4);

	/* wait for preview state */
	soc2030_poll_xdma_reg(info->i2c_client, 0xa104, 0x3);

	/* refresh the sequencer mode */
	soc2030_write_xdma_reg(info->i2c_client, 0xa103, 0x0006);

	/* wait for cmd complete */
	soc2030_poll_xdma_reg(info->i2c_client, 0xa103, 0x0);

	/* refresh sequencer state */
	soc2030_write_xdma_reg(info->i2c_client, 0xa103, 0x0005);

	/* wait for cmd complete */
	soc2030_poll_xdma_reg(info->i2c_client, 0xa103, 0x0);

	info->mode = sensor_mode;
	return 0;
}

static int soc2030_get_status(struct soc2030_info *info, u16 *status)
{
	int err;

	*status = 0;
	err = soc2030_read_xdma_reg(info->i2c_client, 0x0, status);
	if (err)
		return err;
	err = soc2030_read_xdma_reg(info->i2c_client, 0x2104, status + 1);
	if (err)
		return err;
	err = soc2030_read_xdma_reg(info->i2c_client, 0x2703, status + 2);
	if (err)
		return err;
	err = soc2030_read_xdma_reg(info->i2c_client, 0x2705, status + 3);
	if (err)
		return err;
	err = soc2030_read_xdma_reg(info->i2c_client, 0x2737, status + 4);
	if (err)
		return err;
	pr_info("%s: [0]=%x [2104]=%x [2703]=%x [2705]=%x [2737]=%x\n"
		, __func__, status[0], status[1], status[2], status[3],
		status[4]);
	return 0;
}


static int soc2030_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	struct soc2030_info *info = file->private_data;

	switch (cmd) {
	case SOC2030_IOCTL_SET_MODE:
	{
		struct soc2030_mode mode;
		if (copy_from_user(&mode,
				   (const void __user *)arg,
				   sizeof(struct soc2030_mode))) {
			pr_info("%s: Error copying from user\n", __func__);
			return -EFAULT;
		}

		return soc2030_set_mode(info, &mode);
	}
	case SOC2030_IOCTL_GET_STATUS:
	{
		int err;
		u16 status[5];

		err = soc2030_get_status(info, status);
		if (err)
			return err;
		if (copy_to_user((void __user *)arg, &status,
				 10)) {
			pr_info("%s: Error copying to user\n", __func__);
			return -EFAULT;
		}
		return 0;
	}
	default:
		return -EINVAL;
	}
	return 0;
}

static struct soc2030_info *info;

static int soc2030_open(struct inode *inode, struct file *file)
{
	file->private_data = info;
	if (info->pdata && info->pdata->power_on)
		info->pdata->power_on();
	return 0;
}

int soc2030_release(struct inode *inode, struct file *file)
{
	if (info->pdata && info->pdata->power_off)
		info->pdata->power_off();
	file->private_data = NULL;
	return 0;
}


static const struct file_operations soc2030_fileops = {
	.owner = THIS_MODULE,
	.open = soc2030_open,
	.ioctl = soc2030_ioctl,
	.release = soc2030_release,
};

static struct miscdevice soc2030_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "soc2030",
	.fops = &soc2030_fileops,
};

static int soc2030_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	int err;

	pr_info("soc2030: probing sensor.\n");

	info = kzalloc(sizeof(struct soc2030_info), GFP_KERNEL);
	if (!info) {
		pr_err("soc2030: Unable to allocate memory!\n");
		return -ENOMEM;
	}

	err = misc_register(&soc2030_device);
	if (err) {
		pr_err("soc2030: Unable to register misc device!\n");
		kfree(info);
		return err;
	}

	info->pdata = client->dev.platform_data;
	info->i2c_client = client;

	i2c_set_clientdata(client, info);
	return 0;
}

static int soc2030_remove(struct i2c_client *client)
{
	struct soc2030_info *info;
	info = i2c_get_clientdata(client);
	misc_deregister(&soc2030_device);
	kfree(info);
	return 0;
}

static const struct i2c_device_id soc2030_id[] = {
	{ "soc2030", 0 },
	{ },
};

MODULE_DEVICE_TABLE(i2c, soc2030_id);

static struct i2c_driver soc2030_i2c_driver = {
	.driver = {
		.name = "soc2030",
		.owner = THIS_MODULE,
	},
	.probe = soc2030_probe,
	.remove = soc2030_remove,
	.id_table = soc2030_id,
};

static int __init soc2030_init(void)
{
	return i2c_add_driver(&soc2030_i2c_driver);
}

static void __exit soc2030_exit(void)
{
	i2c_del_driver(&soc2030_i2c_driver);
}

module_init(soc2030_init);
module_exit(soc2030_exit);

