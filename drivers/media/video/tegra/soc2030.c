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

struct soc2030_info {
	int mode;
	struct i2c_client *i2c_client;
	struct soc2030_platform_data *pdata;
};

/*
 * SetMode Sequence for 1600X1200/800X600 base settings.
 * Phase 0. Sensor Dependent.
 * This sequence should set sensor for Full/Qtr res
 * This is usually given by the FAE or the sensor vendor.
 * 1600X1200 15fps (Max), 800X600 30fps (Max)
 */
static struct soc2030_regs base_mode[] = {
	{WRITE_REG_DATA, 0x321C, 0x0000},	/*By Pass TxFIFO = 0*/
	{WRITE_REG_DATA, 0x098C, 0x2703},	/*Output Width (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0320},	/*      = 800*/
	{WRITE_REG_DATA, 0x098C, 0x2705},	/*Output Height (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0258},	/*      = 600*/
	{WRITE_REG_DATA, 0x098C, 0x2707},	/*Output Width (B)*/
	{WRITE_REG_DATA, 0x0990, 0x0640},	/*      = 1600*/
	{WRITE_REG_DATA, 0x098C, 0x2709},	/*Output Height (B)*/
	{WRITE_REG_DATA, 0x0990, 0x04B0},	/*      = 1200*/
	{WRITE_REG_DATA, 0x098C, 0x270D},	/*Row Start (A)*/
	{WRITE_REG_DATA, 0x0990, 0x000},	/*      = 0*/
	{WRITE_REG_DATA, 0x098C, 0x270F},	/*Column Start (A)*/
	{WRITE_REG_DATA, 0x0990, 0x000},	/*      = 0*/
	{WRITE_REG_DATA, 0x098C, 0x2711},	/*Row End (A)*/
	{WRITE_REG_DATA, 0x0990, 0x4BD},	/*      = 1213*/
	{WRITE_REG_DATA, 0x098C, 0x2713},	/*Column End (A)*/
	{WRITE_REG_DATA, 0x0990, 0x64D},	/*      = 1613*/
	{WRITE_REG_DATA, 0x098C, 0x2715},	/*Row Speed (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0111},	/*      = 273*/
	{WRITE_REG_DATA, 0x098C, 0x2717},	/*Read Mode (A)*/
	{WRITE_REG_DATA, 0x0990, 0x046C},	/*      = 1132*/
	{WRITE_REG_DATA, 0x098C, 0x2719},	/*sensor_fine_correction (A)*/
	{WRITE_REG_DATA, 0x0990, 0x005A},	/*      = 90*/
	{WRITE_REG_DATA, 0x098C, 0x271B},	/*sensor_fine_IT_min (A)*/
	{WRITE_REG_DATA, 0x0990, 0x01BE},	/*      = 446*/
	{WRITE_REG_DATA, 0x098C, 0x271D},	/*sensor_fine_IT_max_margin(A)*/
	{WRITE_REG_DATA, 0x0990, 0x0131},	/*      = 305*/
	{WRITE_REG_DATA, 0x098C, 0x271F},	/*Frame Lines (A)*/
	{WRITE_REG_DATA, 0x0990, 0x02B3},	/*      = 691*/
	{WRITE_REG_DATA, 0x098C, 0x2721},	/*Line Length (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0853},	/*      = 2131*/
	{WRITE_REG_DATA, 0x098C, 0x2723},	/*Row Start (B)*/
	{WRITE_REG_DATA, 0x0990, 0x004},	/*      = 4*/
	{WRITE_REG_DATA, 0x098C, 0x2725},	/*Column Start (B)*/
	{WRITE_REG_DATA, 0x0990, 0x004},	/*      = 4*/
	{WRITE_REG_DATA, 0x098C, 0x2727},	/*Row End (B)*/
	{WRITE_REG_DATA, 0x0990, 0x4BB},	/*      = 1211*/
	{WRITE_REG_DATA, 0x098C, 0x2729},	/*Column End (B)*/
	{WRITE_REG_DATA, 0x0990, 0x64B},	/*      = 1611*/
	{WRITE_REG_DATA, 0x098C, 0x272B},	/*Row Speed (B)*/
	{WRITE_REG_DATA, 0x0990, 0x0111},	/*      = 273*/
	{WRITE_REG_DATA, 0x098C, 0x272D},	/*Read Mode (B)*/
	{WRITE_REG_DATA, 0x0990, 0x0024},	/*      = 36*/
	{WRITE_REG_DATA, 0x098C, 0x272F},	/*sensor_fine_correction (B)*/
	{WRITE_REG_DATA, 0x0990, 0x003A},	/*      = 58*/
	{WRITE_REG_DATA, 0x098C, 0x2731},	/*sensor_fine_IT_min (B)*/
	{WRITE_REG_DATA, 0x0990, 0x00F6},	/*      = 246*/
	{WRITE_REG_DATA, 0x098C, 0x2733},	/*sensor_fine_IT_max_margin(B)*/
	{WRITE_REG_DATA, 0x0990, 0x008B},	/*      = 139*/
	{WRITE_REG_DATA, 0x098C, 0x2735},	/*Frame Lines (B)*/
	{WRITE_REG_DATA, 0x0990, 0x050D},	/*      = 1293*/
	{WRITE_REG_DATA, 0x098C, 0x2737},	/*Line Length (B)*/
	{WRITE_REG_DATA, 0x0990, 0x085A},	/*      = 2138*/
	{WRITE_REG_DATA, 0x098C, 0x2739},	/*Crop_X0 (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0000},	/*      = 0*/
	{WRITE_REG_DATA, 0x098C, 0x273B},	/*Crop_X1 (A)*/
	{WRITE_REG_DATA, 0x0990, 0x031F},	/*      = 799*/
	{WRITE_REG_DATA, 0x098C, 0x273D},	/*Crop_Y0 (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0000},	/*      = 0*/
	{WRITE_REG_DATA, 0x098C, 0x273F},	/*Crop_Y1 (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0257},	/*      = 599*/
	{WRITE_REG_DATA, 0x098C, 0x2747},	/*Crop_X0 (B)*/
	{WRITE_REG_DATA, 0x0990, 0x0000},	/*      = 0*/
	{WRITE_REG_DATA, 0x098C, 0x2749},	/*Crop_X1 (B)*/
	{WRITE_REG_DATA, 0x0990, 0x063F},	/*      = 1599*/
	{WRITE_REG_DATA, 0x098C, 0x274B},	/*Crop_Y0 (B)*/
	{WRITE_REG_DATA, 0x0990, 0x0000},	/*      = 0*/
	{WRITE_REG_DATA, 0x098C, 0x274D},	/*Crop_Y1 (B)*/
	{WRITE_REG_DATA, 0x0990, 0x04AF},	/*      = 1199*/
	{WRITE_REG_DATA, 0x098C, 0x222D},	/*R9 Step*/
	{WRITE_REG_DATA, 0x0990, 0x009E},	/*      = 158*/
	{WRITE_REG_DATA, 0x098C, 0xA408},	/*search_f1_50*/
	{WRITE_REG_DATA, 0x0990, 0x26},	/*      = 38*/
	{WRITE_REG_DATA, 0x098C, 0xA409},	/*search_f2_50*/
	{WRITE_REG_DATA, 0x0990, 0x28},	/*      = 40*/
	{WRITE_REG_DATA, 0x098C, 0xA40A},	/*search_f1_60*/
	{WRITE_REG_DATA, 0x0990, 0x2E},	/*      = 46*/
	{WRITE_REG_DATA, 0x098C, 0xA40B},	/*search_f2_60*/
	{WRITE_REG_DATA, 0x0990, 0x30},	/*      = 48*/
	{WRITE_REG_DATA, 0x098C, 0x2411},	/*R9_Step_60 (A)*/
	{WRITE_REG_DATA, 0x0990, 0x009E},	/*      = 158*/
	{WRITE_REG_DATA, 0x098C, 0x2413},	/*R9_Step_50 (A)*/
	{WRITE_REG_DATA, 0x0990, 0x00BE},	/*      = 190*/
	{WRITE_REG_DATA, 0x098C, 0x2415},	/*R9_Step_60 (B)*/
	{WRITE_REG_DATA, 0x0990, 0x009E},	/*      = 158*/
	{WRITE_REG_DATA, 0x098C, 0x2417},	/*R9_Step_50 (B)*/
	{WRITE_REG_DATA, 0x0990, 0x00BD},	/*      = 189*/
	{WRITE_REG_DATA, 0x098C, 0xA404},	/*FD Mode*/
	{WRITE_REG_DATA, 0x0990, 0x10},	/*      = 16*/
	{WRITE_REG_DATA, 0x098C, 0xA40D},	/*Stat_min*/
	{WRITE_REG_DATA, 0x0990, 0x02},	/*      = 2*/
	{WRITE_REG_DATA, 0x098C, 0xA40E},	/*Stat_max*/
	{WRITE_REG_DATA, 0x0990, 0x03},	/*      = 3*/
	{WRITE_REG_DATA, 0x098C, 0xA410},	/*Min_amplitude*/
	{WRITE_REG_DATA, 0x0990, 0x0A},	/*      = 10*/
	{REG_TABLE_END, 0x0000, 0x0000}
};

/*
 * SetMode Sequence for context A (800X600, preview).
 * Phase 0. Sensor Dependent.
 * This is usually given by the FAE or the sensor vendor.
 * 800X600 15fps fixed
 */
static struct soc2030_regs mode_800x600[] = {
	{WRITE_REG_DATA, 0x098C, 0xA115},
	{WRITE_REG_DATA, 0x0990, 0x0000},
	{WRITE_REG_DATA, 0x098C, 0xA103},
	{WRITE_REG_DATA, 0x0990, 0x0001},
	{WRITE_REG_DATA, 0x098C, 0xA215},
	{WRITE_REG_DATA, 0x0990, 0x0001},
	{REG_TABLE_END, 0x0000, 0x0000}
};

/*
 * SetMode Sequence for context B (1600X1200, capture).
 * Phase 0. Sensor Dependent.
 * This is usually given by the FAE or the sensor vendor.
 * 1600X1200 15fps (Max)
 */
static struct soc2030_regs mode_1600x1200[] = {
	{WRITE_REG_DATA, 0x098C, 0xA115},
	{WRITE_REG_DATA, 0x0990, 0x0072},
	{WRITE_REG_DATA, 0x098C, 0xA103},
	{WRITE_REG_DATA, 0x0990, 0x0002},
	{REG_TABLE_END, 0x0000, 0x0000}
};

/*
 * SetMode Sequence for 720P in context A (1280X720).
 * Phase 0. Sensor Dependent.
 * This is usually given by the FAE or the sensor vendor.
 * 1280X720 30fps (Fixed)
 */
static struct soc2030_regs mode_1280x720[] = {
	{WRITE_REG_DATA, 0x321C, 0x0000},	/*By Pass TxFIFO = 0*/
	{WRITE_REG_DATA, 0x098C, 0x2703},	/*Output Width (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0500},	/*      = 1280*/
	{WRITE_REG_DATA, 0x098C, 0x2705},	/*Output Height (A)*/
	{WRITE_REG_DATA, 0x0990, 0x02D0},	/*      = 720*/
	{WRITE_REG_DATA, 0x098C, 0x2707},	/*Output Width (B)*/
	{WRITE_REG_DATA, 0x0990, 0x0500},	/*      = 1280*/
	{WRITE_REG_DATA, 0x098C, 0x2709},	/*Output Height (B)*/
	{WRITE_REG_DATA, 0x0990, 0x02D0},	/*      = 720*/
	{WRITE_REG_DATA, 0x098C, 0x270D},	/*Row Start (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0F6},	/*      = 246*/
	{WRITE_REG_DATA, 0x098C, 0x270F},	/*Column Start (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0A6},	/*      = 166*/
	{WRITE_REG_DATA, 0x098C, 0x2711},	/*Row End (A)*/
	{WRITE_REG_DATA, 0x0990, 0x3CD},	/*      = 973*/
	{WRITE_REG_DATA, 0x098C, 0x2713},	/*Column End (A)*/
	{WRITE_REG_DATA, 0x0990, 0x5AD},	/*      = 1453*/
	{WRITE_REG_DATA, 0x098C, 0x2715},	/*Row Speed (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0111},	/*      = 273*/
	{WRITE_REG_DATA, 0x098C, 0x2717},	/*Read Mode (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0024},	/*      = 36*/
	{WRITE_REG_DATA, 0x098C, 0x2719},	/*sensor_fine_correction (A)*/
	{WRITE_REG_DATA, 0x0990, 0x003A},	/*      = 58*/
	{WRITE_REG_DATA, 0x098C, 0x271B},	/*sensor_fine_IT_min (A)*/
	{WRITE_REG_DATA, 0x0990, 0x00F6},	/*      = 246*/
	{WRITE_REG_DATA, 0x098C, 0x271D},	/*sensor_fine_IT_max_margin(A)*/
	{WRITE_REG_DATA, 0x0990, 0x008B},	/*      = 139*/
	{WRITE_REG_DATA, 0x098C, 0x271F},	/*Frame Lines (A)*/
	{WRITE_REG_DATA, 0x0990, 0x032D},	/*      = 813*/
	{WRITE_REG_DATA, 0x098C, 0x2721},	/*Line Length (A)*/
	{WRITE_REG_DATA, 0x0990, 0x067C},	/*      = 1660*/
	{WRITE_REG_DATA, 0x098C, 0x2723},	/*Row Start (B)*/
	{WRITE_REG_DATA, 0x0990, 0x0F6},	/*      = 246*/
	{WRITE_REG_DATA, 0x098C, 0x2725},	/*Column Start (B)*/
	{WRITE_REG_DATA, 0x0990, 0x0A6},	/*      = 166*/
	{WRITE_REG_DATA, 0x098C, 0x2727},	/*Row End (B)*/
	{WRITE_REG_DATA, 0x0990, 0x3CD},	/*      = 973*/
	{WRITE_REG_DATA, 0x098C, 0x2729},	/*Column End (B)*/
	{WRITE_REG_DATA, 0x0990, 0x5AD},	/*      = 1453*/
	{WRITE_REG_DATA, 0x098C, 0x272B},	/*Row Speed (B)*/
	{WRITE_REG_DATA, 0x0990, 0x0111},	/*      = 273*/
	{WRITE_REG_DATA, 0x098C, 0x272D},	/*Read Mode (B)*/
	{WRITE_REG_DATA, 0x0990, 0x0024},	/*      = 36*/
	{WRITE_REG_DATA, 0x098C, 0x272F},	/*sensor_fine_correction (B)*/
	{WRITE_REG_DATA, 0x0990, 0x003A},	/*      = 58*/
	{WRITE_REG_DATA, 0x098C, 0x2731},	/*sensor_fine_IT_min (B)*/
	{WRITE_REG_DATA, 0x0990, 0x00F6},	/*      = 246*/
	{WRITE_REG_DATA, 0x098C, 0x2733},	/*sensor_fine_IT_max_margin(B)*/
	{WRITE_REG_DATA, 0x0990, 0x008B},	/*      = 139*/
	{WRITE_REG_DATA, 0x098C, 0x2735},	/*Frame Lines (B)*/
	{WRITE_REG_DATA, 0x0990, 0x032D},	/*      = 813*/
	{WRITE_REG_DATA, 0x098C, 0x2737},	/*Line Length (B)*/
	{WRITE_REG_DATA, 0x0990, 0x067C},	/*      = 1660*/
	{WRITE_REG_DATA, 0x098C, 0x2739},	/*Crop_X0 (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0000},	/*      = 0*/
	{WRITE_REG_DATA, 0x098C, 0x273B},	/*Crop_X1 (A)*/
	{WRITE_REG_DATA, 0x0990, 0x04FF},	/*      = 1279*/
	{WRITE_REG_DATA, 0x098C, 0x273D},	/*Crop_Y0 (A)*/
	{WRITE_REG_DATA, 0x0990, 0x0000},	/*      = 0*/
	{WRITE_REG_DATA, 0x098C, 0x273F},	/*Crop_Y1 (A)*/
	{WRITE_REG_DATA, 0x0990, 0x02CF},	/*      = 719*/
	{WRITE_REG_DATA, 0x098C, 0x2747},	/*Crop_X0 (B)*/
	{WRITE_REG_DATA, 0x0990, 0x0000},	/*      = 0*/
	{WRITE_REG_DATA, 0x098C, 0x2749},	/*Crop_X1 (B)*/
	{WRITE_REG_DATA, 0x0990, 0x04FF},	/*      = 1279*/
	{WRITE_REG_DATA, 0x098C, 0x274B},	/*Crop_Y0 (B)*/
	{WRITE_REG_DATA, 0x0990, 0x0000},	/*      = 0*/
	{WRITE_REG_DATA, 0x098C, 0x274D},	/*Crop_Y1 (B)*/
	{WRITE_REG_DATA, 0x0990, 0x02CF},	/*      = 719*/
	{WRITE_REG_DATA, 0x098C, 0x222D},	/*R9 Step*/
	{WRITE_REG_DATA, 0x0990, 0x00CB},	/*      = 203*/
	{WRITE_REG_DATA, 0x098C, 0xA408},	/*search_f1_50*/
	{WRITE_REG_DATA, 0x0990, 0x31},	/*      = 49*/
	{WRITE_REG_DATA, 0x098C, 0xA409},	/*search_f2_50*/
	{WRITE_REG_DATA, 0x0990, 0x33},	/*      = 51*/
	{WRITE_REG_DATA, 0x098C, 0xA40A},	/*search_f1_60*/
	{WRITE_REG_DATA, 0x0990, 0x3C},	/*      = 60*/
	{WRITE_REG_DATA, 0x098C, 0xA40B},	/*search_f2_60*/
	{WRITE_REG_DATA, 0x0990, 0x3E},	/*      = 62*/
	{WRITE_REG_DATA, 0x098C, 0x2411},	/*R9_Step_60 (A)*/
	{WRITE_REG_DATA, 0x0990, 0x00CB},	/*      = 203*/
	{WRITE_REG_DATA, 0x098C, 0x2413},	/*R9_Step_50 (A)*/
	{WRITE_REG_DATA, 0x0990, 0x00F4},	/*      = 244*/
	{WRITE_REG_DATA, 0x098C, 0x2415},	/*R9_Step_60 (B)*/
	{WRITE_REG_DATA, 0x0990, 0x00CB},	/*      = 203*/
	{WRITE_REG_DATA, 0x098C, 0x2417},	/*R9_Step_50 (B)*/
	{WRITE_REG_DATA, 0x0990, 0x00F4},	/*      = 244*/
	{WRITE_REG_DATA, 0x098C, 0xA404},	/*FD Mode*/
	{WRITE_REG_DATA, 0x0990, 0x10},	/*      = 16*/
	{WRITE_REG_DATA, 0x098C, 0xA40D},	/*Stat_min*/
	{WRITE_REG_DATA, 0x0990, 0x02},	/*      = 2*/
	{WRITE_REG_DATA, 0x098C, 0xA40E},	/*Stat_max*/
	{WRITE_REG_DATA, 0x0990, 0x03},	/*      = 3*/
	{WRITE_REG_DATA, 0x098C, 0xA410},	/*Min_amplitude*/
	{WRITE_REG_DATA, 0x0990, 0x0A},	/*      = 10*/

	{WRITE_REG_DATA, 0x098C, 0xa103},
	{WRITE_REG_DATA, 0x0990, 0x0006},
	{POLL_VAR_DATA, 0xa103, 0x0000},
	{WRITE_REG_DATA, 0x098C, 0xa103},
	{WRITE_REG_DATA, 0x0990, 0x0005},
	{POLL_VAR_DATA, 0xa103, 0x0000},

	{WRITE_REG_DATA, 0x098C, 0xA115},
	{WRITE_REG_DATA, 0x0990, 0x0000},
	{WRITE_REG_DATA, 0x098C, 0xA103},
	{WRITE_REG_DATA, 0x0990, 0x0001},
	{WRITE_REG_DATA, 0x098C, 0xA215},
	{WRITE_REG_DATA, 0x0990, 0x0001},
	{REG_TABLE_END, 0x0000, 0x0000}
};

/*
 * SetMode Sequence for PLL. Phase 0. Sensor Dependent.
 * This sequence should configure the PLL.
 * This is usually given by the FAE or the sensor vendor.
 * 24MCLK_81SCLK
 */
static struct soc2030_regs pll_table[] = {
	{WRITE_REG_DATA, 0x001e, 0x0503},	/*Pad Slew rate*/
	{WRITE_REG_DATA, 0x0014, 0x21F9},	/*PLL_CONTROL*/
	{WRITE_REG_DATA, 0x0010, 0x011B},	/*PLL_DIVIDERS*/
	{WRITE_REG_DATA, 0x0012, 0x10F7},	/*PLL_P_DIVIDERS*/
	{WRITE_REG_DATA, 0x0014, 0x21FB},	/*PLL_CONTROL*/
	{WRITE_REG_DATA, 0x0014, 0x20FB},	/*PLL_CONTROL*/
	{REG_TABLE_END, 0x0000, 0x0000}

};

/*
 * SetMode Sequence for errata. Phase 0. Sensor Dependent.
 * This is usually given by the FAE or the sensor vendor.
 */
static struct soc2030_regs SetRev2ErrataSequence[] = {
	{WRITE_REG_DATA, 0x3084, 0x240C},
	{WRITE_REG_DATA, 0x3092, 0x0A4C},
	{WRITE_REG_DATA, 0x3094, 0x4C4C},
	{WRITE_REG_DATA, 0x3096, 0x4C54},
	{REG_TABLE_END, 0x0000, 0x0000}

};


/*
 * SetMode Sequence for LCC. Phase 0. Sensor Dependent.
 * This is usually given by the FAE or the sensor vendor.
 */
static struct soc2030_regs SetLensCorrectionSequence[] = {
	{WRITE_REG_DATA, 0x3210, 0x01B8},	/*Enable gamma/sharpen/ccm/LC*/
	{REG_TABLE_END, 0x0000, 0x0000}

};
/*
 * SetMode Sequence for low light. Phase 0. Sensor Dependent.
 * This is usually given by the FAE or the sensor vendor.
 */
static struct soc2030_regs SetLowLightSequence[] = {
	{WRITE_REG_DATA, 0x098C, 0x2B28},	/*MCU_ADDRESS[HG_LL_BRTNSTRT]*/
	{WRITE_REG_DATA, 0x0990, 0x35E8},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0x2B2A},	/*MCU_ADDRESS [HG_LL_BRTNSSTP]*/
	{WRITE_REG_DATA, 0x0990, 0xB3B0},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0xAB20},	/*MCU_ADDRESS [HG_LL_SAT1]*/
	{WRITE_REG_DATA, 0x0990, 0x004B},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0xAB24},	/*MCU_ADDRESS [HG_LL_SAT2]*/
	{WRITE_REG_DATA, 0x0990, 0x0000},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0xAB25},	/*MCU_ADDRESS[HG_LL_INTRPTHR2]*/
	{WRITE_REG_DATA, 0x0990, 0x00FF},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0xAB30},	/*MCU_ADDRESS [HG_NR_STOP_R]*/
	{WRITE_REG_DATA, 0x0990, 0x00FF},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0xAB31},	/*MCU_ADDRESS [HG_NR_STOP_G]*/
	{WRITE_REG_DATA, 0x0990, 0x00FF},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0xAB32},	/*MCU_ADDRESS [HG_NR_STOP_B]*/
	{WRITE_REG_DATA, 0x0990, 0x00FF},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0xAB33},	/*MCU_ADDRESS [HG_NR_STOP_OL]*/
	{WRITE_REG_DATA, 0x0990, 0x0057},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0xAB34},	/*MCU_ADDRESS[HG_NR_GAINSTRT]*/
	{WRITE_REG_DATA, 0x0990, 0x0080},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0xAB35},	/*MCU_ADDRESS [HG_NR_GAINSTP]*/
	{WRITE_REG_DATA, 0x0990, 0x00FF},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0xAB36},	/*MCU_ADDRESS[HG_CLSTERDC_TH]*/
	{WRITE_REG_DATA, 0x0990, 0x0014},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0xAB37},	/*MCU_ADDR[HG_GAMA_MORPH_CTRL]*/
	{WRITE_REG_DATA, 0x0990, 0x0003},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0x2B38},	/*MCU_ADDR[HG_GAMASTARTMORPH]*/
	{WRITE_REG_DATA, 0x0990, 0x32C8},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0x2B3A},	/*MCU_ADDRESS[HG_GAMASTPMORPH]*/
	{WRITE_REG_DATA, 0x0990, 0x7918},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0x2B62},	/*MCU_ADDRESS[HG_FTB_STRT_BM]*/
	{WRITE_REG_DATA, 0x0990, 0xFFFE},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0x2B64},	/*MCU_ADDRESS[HG_FTB_STP_BM]*/
	{WRITE_REG_DATA, 0x0990, 0xFFFF},	/*MCU_DATA_0 {SEQ_END,0x0000}*/
	{REG_TABLE_END, 0x0000, 0x0000}

};
/*
 * SetMode Sequence for CCM. Phase 0. Sensor Dependent.
 * This is usually given by the FAE or the sensor vendor.
 */
static struct soc2030_regs SetCCMCommonSequence[] = {
	{WRITE_REG_DATA, 0x098c, 0xA11F},	/*turn on AWB in preview*/
	{WRITE_REG_DATA, 0x0990, 0x0001},
	{WRITE_REG_DATA, 0x098c, 0xA20B},	/*AE_MIN_INDEX*/
	{WRITE_REG_DATA, 0x0990, 0x0000},
	{WRITE_REG_DATA, 0x098c, 0xA20C},	/*AE_MAX_INDEX*/
	{WRITE_REG_DATA, 0x0990, 0x0008},
	{REG_TABLE_END, 0x0000, 0x0000}

};

/*
 * SetMode Sequence for AWB. Phase 0. Sensor Dependent.
 * Place your module specific tuning here.
 * This is usually given by the FAE or the sensor vendor.
 */
static struct soc2030_regs SetCCMAutoSequence[] = {

	{REG_TABLE_END, 0x0000, 0x0000}
};

/*
 * SetMode Sequence for noise optimizations.
 * Phase 0. Sensor Dependent.
 * Place your module specific tuning here.
 * This is usually given by the FAE or the sensor vendor.
 */
static struct soc2030_regs SetDenoiseSequence[] = {
	{REG_TABLE_END, 0x0000, 0x0000}

};

/*
 * SetMode Sequence for vendor's patch.
 * Phase 0. Sensor Dependent.
 * This is usually given by the FAE or the sensor vendor.
 * K25A_REV03_PATCH01_REV3
 */
static struct soc2030_regs SetRev3PatchSequence[] = {
	{WRITE_REG_DATA, 0x098C, 0x0415},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0xF601},
	{WRITE_REG_DATA, 0x0992, 0x42C1},
	{WRITE_REG_DATA, 0x0994, 0x0326},
	{WRITE_REG_DATA, 0x0996, 0x11F6},
	{WRITE_REG_DATA, 0x0998, 0x0143},
	{WRITE_REG_DATA, 0x099A, 0xC104},
	{WRITE_REG_DATA, 0x099C, 0x260A},
	{WRITE_REG_DATA, 0x099E, 0xCC04},
	{WRITE_REG_DATA, 0x098C, 0x0425},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0x33BD},
	{WRITE_REG_DATA, 0x0992, 0xA362},
	{WRITE_REG_DATA, 0x0994, 0xBD04},
	{WRITE_REG_DATA, 0x0996, 0x3339},
	{WRITE_REG_DATA, 0x0998, 0xC6FF},
	{WRITE_REG_DATA, 0x099A, 0xF701},
	{WRITE_REG_DATA, 0x099C, 0x6439},
	{WRITE_REG_DATA, 0x099E, 0xFE01},
	{WRITE_REG_DATA, 0x098C, 0x0435},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0x6918},
	{WRITE_REG_DATA, 0x0992, 0xCE03},
	{WRITE_REG_DATA, 0x0994, 0x25CC},
	{WRITE_REG_DATA, 0x0996, 0x0013},
	{WRITE_REG_DATA, 0x0998, 0xBDC2},
	{WRITE_REG_DATA, 0x099A, 0xB8CC},
	{WRITE_REG_DATA, 0x099C, 0x0489},
	{WRITE_REG_DATA, 0x099E, 0xFD03},
	{WRITE_REG_DATA, 0x098C, 0x0445},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0x27CC},
	{WRITE_REG_DATA, 0x0992, 0x0325},
	{WRITE_REG_DATA, 0x0994, 0xFD01},
	{WRITE_REG_DATA, 0x0996, 0x69FE},
	{WRITE_REG_DATA, 0x0998, 0x02BD},
	{WRITE_REG_DATA, 0x099A, 0x18CE},
	{WRITE_REG_DATA, 0x099C, 0x0339},
	{WRITE_REG_DATA, 0x099E, 0xCC00},
	{WRITE_REG_DATA, 0x098C, 0x0455},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0x11BD},
	{WRITE_REG_DATA, 0x0992, 0xC2B8},
	{WRITE_REG_DATA, 0x0994, 0xCC04},
	{WRITE_REG_DATA, 0x0996, 0xC8FD},
	{WRITE_REG_DATA, 0x0998, 0x0347},
	{WRITE_REG_DATA, 0x099A, 0xCC03},
	{WRITE_REG_DATA, 0x099C, 0x39FD},
	{WRITE_REG_DATA, 0x099E, 0x02BD},
	{WRITE_REG_DATA, 0x098C, 0x0465},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0xDE00},
	{WRITE_REG_DATA, 0x0992, 0x18CE},
	{WRITE_REG_DATA, 0x0994, 0x00C2},
	{WRITE_REG_DATA, 0x0996, 0xCC00},
	{WRITE_REG_DATA, 0x0998, 0x37BD},
	{WRITE_REG_DATA, 0x099A, 0xC2B8},
	{WRITE_REG_DATA, 0x099C, 0xCC04},
	{WRITE_REG_DATA, 0x099E, 0xEFDD},
	{WRITE_REG_DATA, 0x098C, 0x0475},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0xE6CC},
	{WRITE_REG_DATA, 0x0992, 0x00C2},
	{WRITE_REG_DATA, 0x0994, 0xDD00},
	{WRITE_REG_DATA, 0x0996, 0xC601},
	{WRITE_REG_DATA, 0x0998, 0xF701},
	{WRITE_REG_DATA, 0x099A, 0x64C6},
	{WRITE_REG_DATA, 0x099C, 0x03F7},
	{WRITE_REG_DATA, 0x099E, 0x0165},
	{WRITE_REG_DATA, 0x098C, 0x0485},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0x7F01},
	{WRITE_REG_DATA, 0x0992, 0x6639},
	{WRITE_REG_DATA, 0x0994, 0x3C3C},
	{WRITE_REG_DATA, 0x0996, 0x3C34},
	{WRITE_REG_DATA, 0x0998, 0xCC32},
	{WRITE_REG_DATA, 0x099A, 0x3EBD},
	{WRITE_REG_DATA, 0x099C, 0xA558},
	{WRITE_REG_DATA, 0x099E, 0x30ED},
	{WRITE_REG_DATA, 0x098C, 0x0495},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0x04BD},
	{WRITE_REG_DATA, 0x0992, 0xB2D7},
	{WRITE_REG_DATA, 0x0994, 0x30E7},
	{WRITE_REG_DATA, 0x0996, 0x06CC},
	{WRITE_REG_DATA, 0x0998, 0x323E},
	{WRITE_REG_DATA, 0x099A, 0xED00},
	{WRITE_REG_DATA, 0x099C, 0xEC04},
	{WRITE_REG_DATA, 0x099E, 0xBDA5},
	{WRITE_REG_DATA, 0x098C, 0x04A5},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0x44CC},
	{WRITE_REG_DATA, 0x0992, 0x3244},
	{WRITE_REG_DATA, 0x0994, 0xBDA5},
	{WRITE_REG_DATA, 0x0996, 0x585F},
	{WRITE_REG_DATA, 0x0998, 0x30ED},
	{WRITE_REG_DATA, 0x099A, 0x02CC},
	{WRITE_REG_DATA, 0x099C, 0x3244},
	{WRITE_REG_DATA, 0x099E, 0xED00},
	{WRITE_REG_DATA, 0x098C, 0x04B5},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0xF601},
	{WRITE_REG_DATA, 0x0992, 0xD54F},
	{WRITE_REG_DATA, 0x0994, 0xEA03},
	{WRITE_REG_DATA, 0x0996, 0xAA02},
	{WRITE_REG_DATA, 0x0998, 0xBDA5},
	{WRITE_REG_DATA, 0x099A, 0x4430},
	{WRITE_REG_DATA, 0x099C, 0xE606},
	{WRITE_REG_DATA, 0x099E, 0x3838},
	{WRITE_REG_DATA, 0x098C, 0x04C5},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0x3831},
	{WRITE_REG_DATA, 0x0992, 0x39BD},
	{WRITE_REG_DATA, 0x0994, 0xD661},
	{WRITE_REG_DATA, 0x0996, 0xF602},
	{WRITE_REG_DATA, 0x0998, 0xF4C1},
	{WRITE_REG_DATA, 0x099A, 0x0126},
	{WRITE_REG_DATA, 0x099C, 0x0BFE},
	{WRITE_REG_DATA, 0x099E, 0x02BD},
	{WRITE_REG_DATA, 0x098C, 0x04D5},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0xEE10},
	{WRITE_REG_DATA, 0x0992, 0xFC02},
	{WRITE_REG_DATA, 0x0994, 0xF5AD},
	{WRITE_REG_DATA, 0x0996, 0x0039},
	{WRITE_REG_DATA, 0x0998, 0xF602},
	{WRITE_REG_DATA, 0x099A, 0xF4C1},
	{WRITE_REG_DATA, 0x099C, 0x0226},
	{WRITE_REG_DATA, 0x099E, 0x0AFE},
	{WRITE_REG_DATA, 0x098C, 0x04E5},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0x02BD},
	{WRITE_REG_DATA, 0x0992, 0xEE10},
	{WRITE_REG_DATA, 0x0994, 0xFC02},
	{WRITE_REG_DATA, 0x0996, 0xF7AD},
	{WRITE_REG_DATA, 0x0998, 0x0039},
	{WRITE_REG_DATA, 0x099A, 0x3CBD},
	{WRITE_REG_DATA, 0x099C, 0xB059},
	{WRITE_REG_DATA, 0x099E, 0xCC00},
	{WRITE_REG_DATA, 0x098C, 0x04F5},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0x28BD},
	{WRITE_REG_DATA, 0x0992, 0xA558},
	{WRITE_REG_DATA, 0x0994, 0x8300},
	{WRITE_REG_DATA, 0x0996, 0x0027},
	{WRITE_REG_DATA, 0x0998, 0x0BCC},
	{WRITE_REG_DATA, 0x099A, 0x0026},
	{WRITE_REG_DATA, 0x099C, 0x30ED},
	{WRITE_REG_DATA, 0x099E, 0x00C6},
	{WRITE_REG_DATA, 0x098C, 0x0505},	/*MCU_ADDRESS*/
	{WRITE_REG_DATA, 0x0990, 0x03BD},
	{WRITE_REG_DATA, 0x0992, 0xA544},
	{WRITE_REG_DATA, 0x0994, 0x3839},
	{WRITE_REG_DATA, 0x098C, 0x2006},	/*MCU_ADDRESS [MON_ARG1]*/
	{WRITE_REG_DATA, 0x0990, 0x0415},	/*MCU_DATA_0*/
	{WRITE_REG_DATA, 0x098C, 0xA005},	/*MCU_ADDRESS [MON_CMD]*/
	{WRITE_REG_DATA, 0x0990, 0x0001},	/*MCU_DATA_0*/
	{DELAY_MS, 0x0000, 100},
	{REG_TABLE_END, 0x0000, 0x0000}
};

static struct soc2030_mode modes[] = {
	{800, 600, 30, mode_800x600},
	{1600, 1200, 15, mode_1600x1200},
	{1280, 720, 30, mode_1280x720},
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
		msleep(SOC2030_POLL_WAITMS);
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
		msleep(SOC2030_POLL_WAITMS);
	}
	pr_err("soc2030: xdma poll for %x == ([%x]=%x) failed\n", expected,
	       addr, val);
	return -EINVAL;
}

static int soc2030_write_table(struct i2c_client *client,
			       const struct soc2030_regs table[])
{
	int err = -EIO;
	const struct soc2030_regs *next;

	for (next = table; next->op != REG_TABLE_END; next++) {

		switch (next->op) {
		case WRITE_REG_DATA:
		{
			err = soc2030_write_reg(client, next->addr,
				next->val);
			if (err)
				return err;
			break;
		}
		case WRITE_REG_BIT_H:
		{
			err = soc2030_set_bits(client, next->addr,
				next->val);
			if (err)
				return err;
			break;
		}
		case WRITE_REG_BIT_L:
		{
			err = soc2030_clear_bits(client, next->addr,
				next->val);
			if (err)
				return err;
			break;
		}
		case POLL_REG_DATA:
		{
			err = soc2030_poll(client, next->addr,
				next->val, 0xFFFF);
			if (err)
				return err;
			break;
		}
		case POLL_REG_BIT_H:
		{
			err = soc2030_poll_bit_set(client, next->addr,
				next->val);
			if (err)
				return err;
			break;
		}
		case POLL_REG_BIT_L:
		{
			err = soc2030_poll_bit_clear(client, next->addr,
				next->val);
			if (err)
				return err;
			break;
		}
		case POLL_VAR_DATA:
		{
			err = soc2030_poll_xdma_reg(client, next->addr,
				next->val);
			if (err)
				return err;
			break;
		}
		case DELAY_MS:
		{
			msleep(next->val);
			break;
		}
		default:
			return err;
		}
	}
	return 0;
}

static int soc2030_set_mode(struct soc2030_info *info,
			    struct soc2030_mode *mode)
{
	int sensor_mode, err;
	int index;
	int mode_count;

	mode_count = ARRAY_SIZE(modes);
	for (index = 0; index < mode_count; index++) {
		if ((mode->fps == modes[index].fps) &&
			(mode->xres == modes[index].xres) &&
			(mode->yres == modes[index].yres))
			break;
	}
	if (index == mode_count) {
		pr_err("%s: invalid resolution supplied to set mode %d %d\n",
		       __func__, mode->xres, mode->yres);
		return -EINVAL;
	}
	sensor_mode = index;

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

	err = soc2030_write_table(info->i2c_client, base_mode);
	if (err)
		return err;

	/* load errata settings */
	err = soc2030_write_table(info->i2c_client, SetRev2ErrataSequence);
	if (err)
		return err;

	/* load lens correction */
	err = soc2030_write_table(info->i2c_client, SetLensCorrectionSequence);
	if (err)
		return err;

	/* low light optimization settings */
	err = soc2030_write_table(info->i2c_client, SetLowLightSequence);
	if (err)
		return err;

	/* Base denoise settings (for all resolutions) */
	err = soc2030_write_table(info->i2c_client, SetDenoiseSequence);
	if (err)
		return err;

	/* white balance common settings */
	err = soc2030_write_table(info->i2c_client, SetCCMCommonSequence);
	if (err)
		return err;

	/* auto white balance settings */
	err = soc2030_write_table(info->i2c_client, SetCCMAutoSequence);
	if (err)
		return err;

	/* load patch */
	err = soc2030_write_table(info->i2c_client, SetRev3PatchSequence);
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

	/* wait to complete leave standby */
	soc2030_poll_bit_clear(info->i2c_client, 0x0018, 0x4000);

	/* enable bin summing for preview */
	soc2030_set_bits(info->i2c_client, 0x3040, 0x1000);

	/* set context */
	err = soc2030_write_table(info->i2c_client, modes[sensor_mode].regset);
	if (err)
		return err;

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


static long soc2030_ioctl(struct file *file,
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
	case SOC2030_IOCTL_SET_PRIVATE:
	{
		int err;
		int size = SOC2030_MAX_PRIVATE_SIZE *
			sizeof(struct soc2030_regs);
		struct soc2030_regs *reg_sequence =
			kzalloc(size, GFP_KERNEL);

		if (NULL == reg_sequence) {
			pr_info("%s: Error allocating memory\n", __func__);
			return -ENOMEM;
		}

		if (copy_from_user(reg_sequence,
				   (const void __user *)arg, size)) {
			pr_info("%s: Error copying from user\n", __func__);
			kfree(reg_sequence);
			return -EFAULT;
		}
		err = soc2030_write_table(info->i2c_client, reg_sequence);
		kfree(reg_sequence);
		if (err)
			return -EINVAL;
		return 0;
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
	case SOC2030_IOCTL_GET_MODES:
	{
		if (copy_to_user((void __user *)arg, &modes,
				 sizeof(modes))) {
			pr_info("%s: Error copying to user\n", __func__);
			return -EFAULT;
		}
		return 0;
	}
	case SOC2030_IOCTL_GET_NUM_MODES:
	{
		unsigned int num_modes = ARRAY_SIZE(modes);
		if (copy_to_user((void __user *)arg, &num_modes,
				 sizeof(num_modes))) {
			pr_info("%s: Error copying to user\n", __func__);
			return -EFAULT;
		}
		return 0;
	}
	default:
		return -ENOTTY;
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
	.unlocked_ioctl = soc2030_ioctl,
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

