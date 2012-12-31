/*
* ar0832_main.c - Aptina AR0832 8M Bayer type sensor driver
*
* Copyright (c) 2011, NVIDIA, All Rights Reserved.
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
#include <media/ar0832_main.h>
#include <media/ar0832_focuser.h>
#include <mach/hardware.h>
#include <linux/gpio.h>

DEFINE_MUTEX(ar0832_camera_lock);

#define AR0832_RESET		TEGRA_GPIO_PD2
#define AR0832_PWRDN		TEGRA_GPIO_PD5
#define USE_I2C_DATA_2BYTES

#define POS_LOW 50
#define POS_HIGH 1000
#define SETTLETIME_MS 100
#define FOCAL_LENGTH (3.5f)
#define FNUMBER (2.8f)
#define FPOS_COUNT 1024

struct ar0832_info {
	int mode;
	struct i2c_client *i2c_client;
	struct i2c_client *i2c_client_right;
	struct ar0832_platform_data *pdata;
};

static struct ar0832_info *info;
static struct ar0832_focuser_info *focuser_info;

/* stereo */
static u16 DefaultImageWidth =  1200;
static u16 DefaultImageHeight =  680;
#define UpperByte16to8(x) ((u8)((x&0xFF00)>>8))
#define LowerByte16to8(x) ((u8)(x&0x00FF))

#define ar0832_TABLE_WAIT_MS 0
#define ar0832_TABLE_END 1
#define ar0832_MAX_RETRIES 3
#define USE_CLOCK_MAXIMUM 1
#define USE_DIGITAL_GAIN 0
#define USE_BINNING_MODE 1

static struct ar0832_reg mode_start[] = {
	{ar0832_TABLE_END, 0x0000}
};

static struct ar0832_reg mode_3264X2448[] = {
	{0x301A, 0x0058},	/* RESET_REGISTER */
	{0x301A, 0x0050},	/* RESET_REGISTER */
	{0x0104, 0x0100},	/* GROUPED_PARAMETER_HOLD */
	{0x3064, 0x7800},	/* RESERVED_MFR_3064 */
	{0x31AE, 0x0202},	/* SERIAL_FORMAT */
	{0x31B0, 0x0083},	/* FRAME_PREAMBLE */
	{0x31B2, 0x004D},	/* LINE_PREAMBLE */
	{0x31B4, 0x0E77},	/* MIPI_TIMING_0 */
	{0x31B6, 0x0D20},	/* MIPI_TIMING_1 */
	{0x31B8, 0x020E},	/* MIPI_TIMING_2 */
	{0x31BA, 0x0710},	/* MIPI_TIMING_3 */
	{0x31BC, 0x2A0D},	/* MIPI_TIMING_4 */
	{ar0832_TABLE_WAIT_MS, 0x0005},
	{0x0112, 0x0A0A},	/* LG_CHANGE CCP_DATA_FORMAT */

	{0x3044, 0x0590},	/* RESERVED_MFR_3044 */
	{0x306E, 0xFC80},	/* DATAPATH_SELECT */
	{0x30B2, 0xC000},	/* RESERVED_MFR_30B2 */
	{0x30D6, 0x0800},	/* RESERVED_MFR_30D6 */
	{0x316C, 0xB42F},	/* RESERVED_MFR_316C */
	{0x316E, 0x869A},	/* RESERVED_MFR_316E */
	{0x3170, 0x210E},	/* RESERVED_MFR_3170 */
	{0x317A, 0x010E},	/* RESERVED_MFR_317A */
	{0x31E0, 0x1FB9},	/* RESERVED_MFR_31E0 */
	{0x31E6, 0x07FC},	/* RESERVED_MFR_31E6 */
	{0x37C0, 0x0000},	/* P_GR_Q5 */
	{0x37C2, 0x0000},	/* P_RD_Q5 */
	{0x37C4, 0x0000},	/* P_BL_Q5 */
	{0x37C6, 0x0000},	/* P_GB_Q5 */
	{0x3E00, 0x0011},	/* RESERVED_MFR_3E00 */
	{0x3E02, 0x8801},	/* RESERVED_MFR_3E02 */
	{0x3E04, 0x2801},	/* RESERVED_MFR_3E04 */
	{0x3E06, 0x8449},	/* RESERVED_MFR_3E06 */
	{0x3E08, 0x6841},	/* RESERVED_MFR_3E08 */
	{0x3E0A, 0x400C},	/* RESERVED_MFR_3E0A */
	{0x3E0C, 0x1001},	/* RESERVED_MFR_3E0C */
	{0x3E0E, 0x2603},	/* RESERVED_MFR_3E0E */
	{0x3E10, 0x4B41},	/* RESERVED_MFR_3E10 */
	{0x3E12, 0x4B24},	/* RESERVED_MFR_3E12 */
	{0x3E14, 0xA3CF},	/* RESERVED_MFR_3E14 */
	{0x3E16, 0x8802},	/* RESERVED_MFR_3E16 */
	{0x3E18, 0x84FF},	/* LG_CHANGE 0x8401 RESERVED_MFR_3E18 */
	{0x3E1A, 0x8601},	/* RESERVED_MFR_3E1A */
	{0x3E1C, 0x8401},	/* RESERVED_MFR_3E1C */
	{0x3E1E, 0x840A},	/* RESERVED_MFR_3E1E */
	{0x3E20, 0xFF00},	/* RESERVED_MFR_3E20 */
	{0x3E22, 0x8401},	/* RESERVED_MFR_3E22 */
	{0x3E24, 0x00FF},	/* RESERVED_MFR_3E24 */
	{0x3E26, 0x0088},	/* RESERVED_MFR_3E26 */
	{0x3E28, 0x2E8A},	/* RESERVED_MFR_3E28 */
	{0x3E30, 0x0000},	/* RESERVED_MFR_3E30 */
	{0x3E32, 0x8801},	/* RESERVED_MFR_3E32 */
	{0x3E34, 0x4029},	/* RESERVED_MFR_3E34 */
	{0x3E36, 0x00FF},	/* RESERVED_MFR_3E36 */
	{0x3E38, 0x8469},	/* RESERVED_MFR_3E38 */
	{0x3E3A, 0x00FF},	/* RESERVED_MFR_3E3A */
	{0x3E3C, 0x2801},	/* RESERVED_MFR_3E3C */
	{0x3E3E, 0x3E2A},	/* RESERVED_MFR_3E3E */
	{0x3E40, 0x1C01},	/* RESERVED_MFR_3E40 */
	{0x3E42, 0xFF84},	/* RESERVED_MFR_3E42 */
	{0x3E44, 0x8401},	/* RESERVED_MFR_3E44 */
	{0x3E46, 0x0C01},	/* RESERVED_MFR_3E46 */
	{0x3E48, 0x8401},	/* RESERVED_MFR_3E48 */
	{0x3E4A, 0x00FF},	/* RESERVED_MFR_3E4A */
	{0x3E4C, 0x8402},	/* RESERVED_MFR_3E4C */
	{0x3E4E, 0x8984},	/* RESERVED_MFR_3E4E */
	{0x3E50, 0x6628},	/* RESERVED_MFR_3E50 */
	{0x3E52, 0x8340},	/* RESERVED_MFR_3E52 */
	{0x3E54, 0x00FF},	/* RESERVED_MFR_3E54 */
	{0x3E56, 0x4A42},	/* RESERVED_MFR_3E56 */
	{0x3E58, 0x2703},	/* RESERVED_MFR_3E58 */
	{0x3E5A, 0x6752},	/* RESERVED_MFR_3E5A */
	{0x3E5C, 0x3F2A},	/* RESERVED_MFR_3E5C */
	{0x3E5E, 0x846A},	/* RESERVED_MFR_3E5E */
	{0x3E60, 0x4C01},	/* RESERVED_MFR_3E60 */
	{0x3E62, 0x8401},	/* RESERVED_MFR_3E62 */
	{0x3E66, 0x3901},	/* RESERVED_MFR_3E66 */
	{0x3E90, 0x2C01},	/* RESERVED_MFR_3E90 */
	{0x3E98, 0x2B02},	/* RESERVED_MFR_3E98 */
	{0x3E92, 0x2A04},	/* RESERVED_MFR_3E92 */
	{0x3E94, 0x2509},	/* RESERVED_MFR_3E94 */
	{0x3E96, 0x0000},	/* RESERVED_MFR_3E96 */
	{0x3E9A, 0x2905},	/* RESERVED_MFR_3E9A */
	{0x3E9C, 0x00FF},	/* RESERVED_MFR_3E9C */
	{0x3ECC, 0x00EB},	/* RESERVED_MFR_3ECC */
	{0x3ED0, 0x1E24},	/* RESERVED_MFR_3ED0 */
	{0x3ED4, 0xAFC4},	/* RESERVED_MFR_3ED4 */
	{0x3ED6, 0x909B},	/* RESERVED_MFR_3ED6 */
	{0x3EE0, 0x2424},	/* RESERVED_MFR_3EE0 */
	{0x3EE2, 0x9797},	/* RESERVED_MFR_3EE2 */
	{0x3EE4, 0xC100},	/* RESERVED_MFR_3EE4 */
	{0x3EE6, 0x0540},	/* RESERVED_MFR_3EE6 */
	{0x3174, 0x8000},	/* RESERVED_MFR_3174 */
	{0x0300, 0x0004},	/* VT_PIX_CLK_DIV */
	{0x0302, 0x0001},	/* VT_SYS_CLK_DIV */
	{0x0304, 0x0002},	/* PRE_PLL_CLK_DIV */
	{0x0306, 0x0040},	/* PLL_MULTIPLIER */
	{0x0308, 0x000A},	/* OP_PIX_CLK_DIV */
	{0x030A, 0x0001},	/* OP_SYS_CLK_DIV */
	{0x3064, 0x7400},	/* RESERVED_MFR_3064 */
	{0x0344, 0x0004},	/* X_ADDR_START */
	{0x0348, 0x0CCB},	/* X_ADDR_END */
	{0x0346, 0x0004},	/* Y_ADDR_START */
	{0x034A, 0x099B},	/* Y_ADDR_END */
	{0x034C, 0x0CC8},	/* X_OUTPUT_SIZE */
	{0x034E, 0x0998},	/* Y_OUTPUT_SIZE */
	{0x3040, 0xC041},	/* READ_MODE */
	{0x306E, 0xFC80},	/* DATAPATH_SELECT */
	{0x0400, 0x0000},	/* SCALING_MODE */
	{0x0404, 0x0010},	/* SCALE_M */
	{0x3178, 0x0000},	/* RESERVED_MFR_3178 */
	{0x3ED0, 0x1E24},	/* RESERVED_MFR_3ED0 */
	{0x0400, 0x0000},	/* SCALING_MODE */
	{0x0404, 0x0010},	/* SCALE_M */
	{0x0342, 0x133C},	/* LINE_LENGTH_PCK */
	{0x0340, 0x0A27},	/* FRAME_LENGTH_LINES */
	{0x0202, 0x0A27},	/* COARSE_INTEGRATION_TIME */
	{0x3014, 0x09DC},	/* FINE_INTEGRATION_TIME_ */
	{0x3010, 0x0078},	/* FINE_CORRECTION */
	{0x301A, 0x8250},	/* RESET_REGISTER */
	{0x301A, 0x8650},	/* RESET_REGISTER */
	{0x301A, 0x8658},	/* RESET_REGISTER */
	/* gain */
	{0x3056, 0x10AA},	/* gain */
	{0x3058, 0x10AA},	/* gain */
	{0x305a, 0x10AA},	/* gain */
	{0x305c, 0x10AA},	/* gain */
	{0x0104, 0x0000},	/* GROUPED_PARAMETER_HOLD */
	{0x301A, 0x065C},	/* RESET_REGISTER */
	{ar0832_TABLE_END, 0x0000}
};

static struct ar0832_reg mode_2880X1620[] = {
	{0x301A, 0x0058},	/* RESET_REGISTER */
	{0x301A, 0x0050},	/* RESET_REGISTER */
	{0x0104, 0x0100},	/* GROUPED_PARAMETER_HOLD */
	{0x3064, 0x7800},	/* RESERVED_MFR_3064 */
	{0x31AE, 0x0202},	/* SERIAL_FORMAT */
	{0x31B0, 0x0083},	/* FRAME_PREAMBLE */
	{0x31B2, 0x004D},	/* LINE_PREAMBLE */
	{0x31B4, 0x0E77},	/* MIPI_TIMING_0 */
	{0x31B6, 0x0D20},	/* MIPI_TIMING_1 */
	{0x31B8, 0x020E},	/* MIPI_TIMING_2 */
	{0x31BA, 0x0710},	/* MIPI_TIMING_3 */
	{0x31BC, 0x2A0D},	/* MIPI_TIMING_4 */
	{ar0832_TABLE_WAIT_MS, 0x0005},
	{0x0112, 0x0A0A},	/* CCP_DATA_FORMAT */

	{0x3044, 0x0590},	/* RESERVED_MFR_3044 */
	{0x306E, 0xFC80},	/* DATAPATH_SELECT */
	{0x30B2, 0xC000},	/* RESERVED_MFR_30B2 */
	{0x30D6, 0x0800},	/* RESERVED_MFR_30D6 */
	{0x316C, 0xB42F},	/* RESERVED_MFR_316C */
	{0x316E, 0x869A},	/* RESERVED_MFR_316E */
	{0x3170, 0x210E},	/* RESERVED_MFR_3170 */
	{0x317A, 0x010E},	/* RESERVED_MFR_317A */
	{0x31E0, 0x1FB9},	/* RESERVED_MFR_31E0 */
	{0x31E6, 0x07FC},	/* RESERVED_MFR_31E6 */
	{0x37C0, 0x0000},	/* P_GR_Q5 */
	{0x37C2, 0x0000},	/* P_RD_Q5 */
	{0x37C4, 0x0000},	/* P_BL_Q5 */
	{0x37C6, 0x0000},	/* P_GB_Q5 */
	{0x3E00, 0x0011},	/* RESERVED_MFR_3E00 */
	{0x3E02, 0x8801},	/* RESERVED_MFR_3E02 */
	{0x3E04, 0x2801},	/* RESERVED_MFR_3E04 */
	{0x3E06, 0x8449},	/* RESERVED_MFR_3E06 */
	{0x3E08, 0x6841},	/* RESERVED_MFR_3E08 */
	{0x3E0A, 0x400C},	/* RESERVED_MFR_3E0A */
	{0x3E0C, 0x1001},	/* RESERVED_MFR_3E0C */
	{0x3E0E, 0x2603},	/* RESERVED_MFR_3E0E */
	{0x3E10, 0x4B41},	/* RESERVED_MFR_3E10 */
	{0x3E12, 0x4B24},	/* RESERVED_MFR_3E12 */
	{0x3E14, 0xA3CF},	/* RESERVED_MFR_3E14 */
	{0x3E16, 0x8802},	/* RESERVED_MFR_3E16 */
	{0x3E18, 0x84FF},	/* RESERVED_MFR_3E18 */
	{0x3E1A, 0x8601},	/* RESERVED_MFR_3E1A */
	{0x3E1C, 0x8401},	/* RESERVED_MFR_3E1C */
	{0x3E1E, 0x840A},	/* RESERVED_MFR_3E1E */
	{0x3E20, 0xFF00},	/* RESERVED_MFR_3E20 */
	{0x3E22, 0x8401},	/* RESERVED_MFR_3E22 */
	{0x3E24, 0x00FF},	/* RESERVED_MFR_3E24 */
	{0x3E26, 0x0088},	/* RESERVED_MFR_3E26 */
	{0x3E28, 0x2E8A},	/* RESERVED_MFR_3E28 */
	{0x3E30, 0x0000},	/* RESERVED_MFR_3E30 */
	{0x3E32, 0x8801},	/* RESERVED_MFR_3E32 */
	{0x3E34, 0x4029},	/* RESERVED_MFR_3E34 */
	{0x3E36, 0x00FF},	/* RESERVED_MFR_3E36 */
	{0x3E38, 0x8469},	/* RESERVED_MFR_3E38 */
	{0x3E3A, 0x00FF},	/* RESERVED_MFR_3E3A */
	{0x3E3C, 0x2801},	/* RESERVED_MFR_3E3C */
	{0x3E3E, 0x3E2A},	/* RESERVED_MFR_3E3E */
	{0x3E40, 0x1C01},	/* RESERVED_MFR_3E40 */
	{0x3E42, 0xFF84},	/* RESERVED_MFR_3E42 */
	{0x3E44, 0x8401},	/* RESERVED_MFR_3E44 */
	{0x3E46, 0x0C01},	/* RESERVED_MFR_3E46 */
	{0x3E48, 0x8401},	/* RESERVED_MFR_3E48 */
	{0x3E4A, 0x00FF},	/* RESERVED_MFR_3E4A */
	{0x3E4C, 0x8402},	/* RESERVED_MFR_3E4C */
	{0x3E4E, 0x8984},	/* RESERVED_MFR_3E4E */
	{0x3E50, 0x6628},	/* RESERVED_MFR_3E50 */
	{0x3E52, 0x8340},	/* RESERVED_MFR_3E52 */
	{0x3E54, 0x00FF},	/* RESERVED_MFR_3E54 */
	{0x3E56, 0x4A42},	/* RESERVED_MFR_3E56 */
	{0x3E58, 0x2703},	/* RESERVED_MFR_3E58 */
	{0x3E5A, 0x6752},	/* RESERVED_MFR_3E5A */
	{0x3E5C, 0x3F2A},	/* RESERVED_MFR_3E5C */
	{0x3E5E, 0x846A},	/* RESERVED_MFR_3E5E */
	{0x3E60, 0x4C01},	/* RESERVED_MFR_3E60 */
	{0x3E62, 0x8401},	/* RESERVED_MFR_3E62 */
	{0x3E66, 0x3901},	/* RESERVED_MFR_3E66 */
	{0x3E90, 0x2C01},	/* RESERVED_MFR_3E90 */
	{0x3E98, 0x2B02},	/* RESERVED_MFR_3E98 */
	{0x3E92, 0x2A04},	/* RESERVED_MFR_3E92 */
	{0x3E94, 0x2509},	/* RESERVED_MFR_3E94 */
	{0x3E96, 0x0000},	/* RESERVED_MFR_3E96 */
	{0x3E9A, 0x2905},	/* RESERVED_MFR_3E9A */
	{0x3E9C, 0x00FF},	/* RESERVED_MFR_3E9C */
	{0x3ECC, 0x00EB},	/* RESERVED_MFR_3ECC */
	{0x3ED0, 0x1E24},	/* RESERVED_MFR_3ED0 */
	{0x3ED4, 0xAFC4},	/* RESERVED_MFR_3ED4 */
	{0x3ED6, 0x909B},	/* RESERVED_MFR_3ED6 */
	{0x3EE0, 0x2424},	/* RESERVED_MFR_3EE0 */
	{0x3EE2, 0x9797},	/* RESERVED_MFR_3EE2 */
	{0x3EE4, 0xC100},	/* RESERVED_MFR_3EE4 */
	{0x3EE6, 0x0540},	/* RESERVED_MFR_3EE6 */
	{0x3174, 0x8000},	/* RESERVED_MFR_3174 */
	{0x0300, 0x0004},	/* VT_PIX_CLK_DIV */
	{0x0302, 0x0001},	/* VT_SYS_CLK_DIV */
	{0x0304, 0x0002},	/* PRE_PLL_CLK_DIV */
	{0x0306, 0x0040},	/* PLL_MULTIPLIER */
	{0x0308, 0x000A},	/* OP_PIX_CLK_DIV */
	{0x030A, 0x0001},	/* OP_SYS_CLK_DIV */
	{0x3064, 0x7400},	/* RESERVED_MFR_3064 */
	{0x0344, 0x00C8},	/* X_ADDR_START */
	{0x0348, 0x0C07},	/* X_ADDR_END */
	{0x0346, 0x01A6},	/* Y_ADDR_START */
	{0x034A, 0x07F9},	/* Y_ADDR_END */
	{0x034C, 0x0B40},	/* X_OUTPUT_SIZE */
	{0x034E, 0x0654},	/* Y_OUTPUT_SIZE */
	{0x3040, 0xC041},	/* READ_MODE */
	{0x306E, 0xFC80},	/* DATAPATH_SELECT */
	{0x0400, 0x0000},	/* SCALING_MODE */
	{0x0404, 0x0010},	/* SCALE_M */
	{0x3178, 0x0000},	/* RESERVED_MFR_3178 */
	{0x3ED0, 0x1E24},	/* RESERVED_MFR_3ED0 */

	{0x0342, 0x11B8},	/* LINE_LENGTH_PCK */
	{0x0340, 0x06E3},	/* FRAME_LENGTH_LINES */
	{0x0202, 0x06E3},	/* COARSE_INTEGRATION_TIME */
	{0x3014, 0x0BD8},	/* FINE_INTEGRATION_TIME_ */
	{0x3010, 0x0078},	/* FINE_CORRECTION */
	{0x301A, 0x8250},	/* RESET_REGISTER */
	{0x301A, 0x8650},	/* RESET_REGISTER */
	{0x301A, 0x8658},	/* RESET_REGISTER */
	/* gain */
	{0x3056, 0x10AA},	/* gain */
	{0x3058, 0x10AA},	/* gain */
	{0x305a, 0x10AA},	/* gain */
	{0x305c, 0x10AA},	/* gain */
	{0x0104, 0x0000},	/* GROUPED_PARAMETER_HOLD */
	{0x301A, 0x065C},	/* RESET_REGISTER */
	{ar0832_TABLE_END, 0x0000}
};

static struct ar0832_reg mode_1632X1224[] = {
	{0x301A, 0x0058},	/* LG_CHANGE 0x0658 RESET_REGISTER */
	{0x301A, 0x0050},	/* LG_CHANGE 0x0650 RESET_REGISTER */

	/* SC-CHANGE: to-do 8 bit write */
	{0x0104, 0x0100},	/* GROUPED_PARAMETER_HOLD */

	{0x3064, 0x7800},	/* LG_CHANGE 0x7400 RESERVED_MFR_3064 */
	{0x31AE, 0x0202},	/* SERIAL_FORMAT */
	{0x31B0, 0x0083},	/* FRAME_PREAMBLE */
	{0x31B2, 0x004D},	/* LINE_PREAMBLE */
	{0x31B4, 0x0E77},	/* MIPI_TIMING_0 */
	{0x31B6, 0x0D20},	/* MIPI_TIMING_1 */
	{0x31B8, 0x020E},	/* MIPI_TIMING_2 */
	{0x31BA, 0x0710},	/* MIPI_TIMING_3 */
	{0x31BC, 0x2A0D},	/* MIPI_TIMING_4 */
	{ar0832_TABLE_WAIT_MS, 0x0005},
	{0x0112, 0x0A0A},	/* LG_CHANGE CCP_DATA_FORMAT */
	{0x3044, 0x0590},	/* RESERVED_MFR_3044 */
	{0x306E, 0xFC80},	/* DATAPATH_SELECT */
	{0x30B2, 0xC000},	/* RESERVED_MFR_30B2 */
	{0x30D6, 0x0800},	/* RESERVED_MFR_30D6 */
	{0x316C, 0xB42F},	/* RESERVED_MFR_316C */
	{0x316E, 0x869A},	/* RESERVED_MFR_316E */
	{0x3170, 0x210E},	/* RESERVED_MFR_3170 */
	{0x317A, 0x010E},	/* RESERVED_MFR_317A */
	{0x31E0, 0x1FB9},	/* RESERVED_MFR_31E0 */
	{0x31E6, 0x07FC},	/* RESERVED_MFR_31E6 */
	{0x37C0, 0x0000},	/* P_GR_Q5 */
	{0x37C2, 0x0000},	/* P_RD_Q5 */
	{0x37C4, 0x0000},	/* P_BL_Q5 */
	{0x37C6, 0x0000},	/* P_GB_Q5 */
	{0x3E00, 0x0011},	/* RESERVED_MFR_3E00 */
	{0x3E02, 0x8801},	/* RESERVED_MFR_3E02 */
	{0x3E04, 0x2801},	/* RESERVED_MFR_3E04 */
	{0x3E06, 0x8449},	/* RESERVED_MFR_3E06 */
	{0x3E08, 0x6841},	/* RESERVED_MFR_3E08 */
	{0x3E0A, 0x400C},	/* RESERVED_MFR_3E0A */
	{0x3E0C, 0x1001},	/* RESERVED_MFR_3E0C */
	{0x3E0E, 0x2603},	/* RESERVED_MFR_3E0E */
	{0x3E10, 0x4B41},	/* RESERVED_MFR_3E10 */
	{0x3E12, 0x4B24},	/* RESERVED_MFR_3E12 */
	{0x3E14, 0xA3CF},	/* RESERVED_MFR_3E14 */
	{0x3E16, 0x8802},	/* RESERVED_MFR_3E16 */
	{0x3E18, 0x84FF},	/* LG_CHANGE 0x8401 RESERVED_MFR_3E18 */
	{0x3E1A, 0x8601},	/* RESERVED_MFR_3E1A */
	{0x3E1C, 0x8401},	/* RESERVED_MFR_3E1C */
	{0x3E1E, 0x840A},	/* RESERVED_MFR_3E1E */
	{0x3E20, 0xFF00},	/* RESERVED_MFR_3E20 */
	{0x3E22, 0x8401},	/* RESERVED_MFR_3E22 */
	{0x3E24, 0x00FF},	/* RESERVED_MFR_3E24 */
	{0x3E26, 0x0088},	/* RESERVED_MFR_3E26 */
	{0x3E28, 0x2E8A},	/* RESERVED_MFR_3E28 */
	{0x3E30, 0x0000},	/* RESERVED_MFR_3E30 */
	{0x3E32, 0x8801},	/* RESERVED_MFR_3E32 */
	{0x3E34, 0x4029},	/* RESERVED_MFR_3E34 */
	{0x3E36, 0x00FF},	/* RESERVED_MFR_3E36 */
	{0x3E38, 0x8469},	/* RESERVED_MFR_3E38 */
	{0x3E3A, 0x00FF},	/* RESERVED_MFR_3E3A */
	{0x3E3C, 0x2801},	/* RESERVED_MFR_3E3C */
	{0x3E3E, 0x3E2A},	/* RESERVED_MFR_3E3E */
	{0x3E40, 0x1C01},	/* RESERVED_MFR_3E40 */
	{0x3E42, 0xFF84},	/* RESERVED_MFR_3E42 */
	{0x3E44, 0x8401},	/* RESERVED_MFR_3E44 */
	{0x3E46, 0x0C01},	/* RESERVED_MFR_3E46 */
	{0x3E48, 0x8401},	/* RESERVED_MFR_3E48 */
	{0x3E4A, 0x00FF},	/* RESERVED_MFR_3E4A */
	{0x3E4C, 0x8402},	/* RESERVED_MFR_3E4C */
	{0x3E4E, 0x8984},	/* RESERVED_MFR_3E4E */
	{0x3E50, 0x6628},	/* RESERVED_MFR_3E50 */
	{0x3E52, 0x8340},	/* RESERVED_MFR_3E52 */
	{0x3E54, 0x00FF},	/* RESERVED_MFR_3E54 */
	{0x3E56, 0x4A42},	/* RESERVED_MFR_3E56 */
	{0x3E58, 0x2703},	/* RESERVED_MFR_3E58 */
	{0x3E5A, 0x6752},	/* RESERVED_MFR_3E5A */
	{0x3E5C, 0x3F2A},	/* RESERVED_MFR_3E5C */
	{0x3E5E, 0x846A},	/* RESERVED_MFR_3E5E */
	{0x3E60, 0x4C01},	/* RESERVED_MFR_3E60 */
	{0x3E62, 0x8401},	/* RESERVED_MFR_3E62 */
	{0x3E66, 0x3901},	/* RESERVED_MFR_3E66 */
	{0x3E90, 0x2C01},	/* RESERVED_MFR_3E90 */
	{0x3E98, 0x2B02},	/* RESERVED_MFR_3E98 */
	{0x3E92, 0x2A04},	/* RESERVED_MFR_3E92 */
	{0x3E94, 0x2509},	/* RESERVED_MFR_3E94 */
	{0x3E96, 0x0000},	/* RESERVED_MFR_3E96 */
	{0x3E9A, 0x2905},	/* RESERVED_MFR_3E9A */
	{0x3E9C, 0x00FF},	/* RESERVED_MFR_3E9C */
	{0x3ECC, 0x00EB},	/* RESERVED_MFR_3ECC */
	{0x3ED0, 0x1E24},	/* RESERVED_MFR_3ED0 */
	{0x3ED4, 0xAFC4},	/* RESERVED_MFR_3ED4 */
	{0x3ED6, 0x909B},	/* RESERVED_MFR_3ED6 */
	{0x3EE0, 0x2424},	/* RESERVED_MFR_3EE0 */
	{0x3EE2, 0x9797},	/* RESERVED_MFR_3EE2 */
	{0x3EE4, 0xC100},	/* RESERVED_MFR_3EE4 */
	{0x3EE6, 0x0540},	/* RESERVED_MFR_3EE6 */
	{0x3174, 0x8000},	/* RESERVED_MFR_3174 */
	{0x0300, 0x0004},	/* VT_PIX_CLK_DIV */
	{0x0302, 0x0001},	/* VT_SYS_CLK_DIV */
	{0x0304, 0x0002},	/* PRE_PLL_CLK_DIV */

	{0x0306, 0x0040},	/* PLL_MULTIPLIER */

	{0x0308, 0x000A},	/* OP_PIX_CLK_DIV */
	{0x030A, 0x0001},	/* OP_SYS_CLK_DIV */
	{ar0832_TABLE_WAIT_MS, 0x0001}, /* waitmsec 1 */

	{0x3064, 0x7400},	/* RESERVED_MFR_3064 */

	{0x0104, 0x0100},	/* GROUPED_PARAMETER_HOLD */

	{0x0344, 0x0008},	/* X_ADDR_START */
	{0x0348, 0x0CC9},	/* X_ADDR_END */
	{0x0346, 0x0008},	/* Y_ADDR_START */
	{0x034A, 0x0999},	/* Y_ADDR_END */
	{0x034C, 0x0660},	/* X_OUTPUT_SIZE */
	{0x034E, 0x04C8},	/* Y_OUTPUT_SIZE */
	{0x3040, 0xC4C3},	/* READ_MODE */
	{0x306E, 0xFC80},	/* DATAPATH_SELECT */
	{0x3178, 0x0000},	/* RESERVED_MFR_3178 */
	{0x3ED0, 0x1E24},	/* RESERVED_MFR_3ED0 */
	{0x0400, 0x0002},	/* SCALING_MODE */
	{0x0404, 0x0010},	/* SCALE_M */
	{0x0342, 0x101A},	/* LINE_LENGTH_PCK */
	{0x0340, 0x0610},	/* FRAME_LENGTH_LINES */
	{0x0202, 0x0557},	/* COARSE_INTEGRATION_TIME */
	{0x3014, 0x0988},	/* FINE_INTEGRATION_TIME_ */
	{0x3010, 0x0130},	/* FINE_CORRECTION */
	{0x301A, 0x8250},	/* LGE_CHANGE 0x8650 RESET_REGISTER */
	{0x301A, 0x8650},	/* RESET_REGISTER */
	{0x301A, 0x8658},	/* RESET_REGISTER */

	/* gain */
	{0x3056, 0x10AA},	/* gain */
	{0x3058, 0x10AA},	/* gain */
	{0x305a, 0x10AA},	/* gain */
	{0x305c, 0x10AA},	/* gain */

	/* todo 8-bit write */
	{0x0104, 0x0000},	/* GROUPED_PARAMETER_HOLD */

	{0x301A, 0x065C},	/* RESET_REGISTER */
	{ar0832_TABLE_END, 0x0000}
};

static struct ar0832_reg mode_end[] = {
	{ar0832_TABLE_END, 0x0000}
};

enum {
	ar0832_MODE_3264X2448,
	ar0832_MODE_2880X1620,
	ar0832_MODE_1632X1224,
};

static struct ar0832_reg *mode_table[] = {
	[ar0832_MODE_3264X2448] = mode_3264X2448,
	[ar0832_MODE_2880X1620] = mode_2880X1620,
	[ar0832_MODE_1632X1224] = mode_1632X1224,
};


/* 2 regs to program frame length */
static inline void ar0832_get_frame_length_regs(struct ar0832_reg *regs,
				u32 frame_length)
{
	regs->addr = 0x0340;
	regs->val = (frame_length >> 8) & 0xff;
	(regs + 1)->addr = 0x0341;
	(regs + 1)->val = (frame_length) & 0xff;
}


static inline void ar0832_get_coarse_time_regs(struct ar0832_reg *regs,
				u32 coarse_time)
{
	regs->addr = 0x0202;
	regs->val = (coarse_time >> 8) & 0xff;
	(regs + 1)->addr = 0x0203;
	(regs + 1)->val = (coarse_time) & 0xff;
}

static inline void ar0832_get_focuser_vcm_control_regs(struct ar0832_reg *regs,
				u16 value)
{
	regs->addr = 0x30F0;
	regs->val = (value >> 8) & 0xff;
	(regs + 1)->addr = 0x30F1;
	(regs + 1)->val = (value) & 0xff;
}

static inline void ar0832_get_focuser_vcm_step_time_regs
	(struct ar0832_reg *regs, u16 value)
{
	regs->addr = 0x30F4;
	regs->val = (value >> 8) & 0xff;
	(regs + 1)->addr = 0x30F5;
	(regs + 1)->val = (value) & 0xff;
}

static inline void ar0832_get_focuser_data_regs(struct ar0832_reg *regs,
				u16 value)
{
	regs->addr = 0x30F2;
	regs->val = (value >> 8) & 0xff;
	(regs + 1)->addr = 0x30F3;
	(regs + 1)->val = (value) & 0xff;
}


static inline void ar0832_get_gain_reg(struct ar0832_reg *regs, u16 gain)
{
	regs->addr = 0x0205;
	regs->val = gain;
}

static int ar0832_write_reg(struct i2c_client *client, u16 addr, u8 val)
{
	int err;
	struct i2c_msg msg;
	unsigned char data[3];
	int retry = 0;

	if (!client->adapter)
		return -ENODEV;

	data[0] = (u8) (addr >> 8);;
	data[1] = (u8) (addr & 0xff);
	data[2] = (u8) (val & 0xff);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = 3;
	msg.buf = data;

	do {
		err = i2c_transfer(client->adapter, &msg, 1);
		if (err > 0)
			return 0;
		retry++;
		pr_err("ar0832: i2c transfer failed, retrying %x %x\n",
			addr, val);
		/*
		why usleep_range() instead of msleep() ?
		Read Documentation/timers/timers-howto.txt
		*/
		usleep_range(3000, 3500);
	} while (retry < ar0832_MAX_RETRIES);

	return err;
}


static int ar0832_2bytes_write_reg_helper(
			struct ar0832_info *info, u16 addr, u16 val)
{
	int ret;

	pr_info("[%s] (0x%04x)(0x%04x)\n", __func__, addr, val);
	ret = ar0832_write_reg(info->i2c_client, addr, (val >> 8 & 0xff));
	if (addr != 0x104)
		ret = ar0832_write_reg(info->i2c_client, addr+1, (val & 0xff));
	else
		pr_info("[%s] this global parameter hold\n", __func__);
	return ret;
}

static int ar0832_write_reg_helper(struct ar0832_info *info, u16 addr, u16 val)
{
	int ret;

	ret = ar0832_write_reg(info->i2c_client, addr, val);
	return ret;
}

static int ar0832_focuser_write(struct i2c_client *client, u16 value)
{
	int count;
	struct i2c_msg msg[1];
	unsigned char data[2];
	int retry = 0;

	if (!client->adapter)
		return -ENODEV;

	data[0] = (u8) ((value >> 4) & 0x3F);
	data[1] = (u8) ((value & 0xF) << 4);
	/* Slew rate control (8 steps, 50us) */
	data[1] = (data[1] & 0xF0) | 0x05;

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].len = ARRAY_SIZE(data);
	msg[0].buf = data;

	do {
		count = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
		if (count == ARRAY_SIZE(msg))
			return 0;
		retry++;
		pr_err("ar0832_focuser: i2c transfer failed, retrying %x\n",
				value);
		usleep_range(3000, 3500);
	} while (retry <= 3);
	return -EIO;
}

static int ar0832_focuser_write_helper(
			struct ar0832_focuser_info *info, u16 value)
{
	int ret;
	ret = ar0832_focuser_write(info->i2c_client,  value);
	return ret;
}

static int ar0832_write_table(struct ar0832_info *info,
		const struct ar0832_reg table[],
		const struct ar0832_reg override_list[],
		int num_override_regs)
{
	int err;
	const struct ar0832_reg *next;

	for (next = table; next->addr != ar0832_TABLE_END; next++) {
		if (next->addr ==  ar0832_TABLE_WAIT_MS) {
			usleep_range(next->val*1000, next->val*1000 + 500);
			continue;
		}
		err = ar0832_2bytes_write_reg_helper(info, next->addr,
						     next->val);
		if (err)
			return err;
	}
	return 0;
}

static int ar0832_set_frame_length(struct ar0832_info *info, u32 frame_length)
{
	struct ar0832_reg reg_list[2];
	int i = 0;
	int ret;

	pr_info("[%s] (0x%08x)\n", __func__,  frame_length);

	    ar0832_get_frame_length_regs(reg_list, frame_length);
	ret = ar0832_write_reg_helper(info, 0x0104, 0x01);
	if (ret)
		return ret;

	for (i = 0; i < 2; i++) {
		ret = ar0832_write_reg_helper(info, reg_list[i].addr,
				reg_list[i].val);
		if (ret)
			return ret;
	}
	ret = ar0832_write_reg_helper(info, 0x0104, 0x00);
	if (ret)
		return ret;

	return 0;
}

static int ar0832_set_coarse_time(struct ar0832_info *info, u32 coarse_time)
{
	int ret;

	struct ar0832_reg reg_list[2];
	int i = 0;

	pr_info("[%s] (0x%08x)\n", __func__,  coarse_time);
	ar0832_get_coarse_time_regs(reg_list, coarse_time);
	ret = ar0832_write_reg_helper(info, 0x0104, 0x01);
	if (ret)
		return ret;

	for (i = 0; i < 2; i++) {
		ret = ar0832_write_reg_helper(info, reg_list[i].addr,
				reg_list[i].val);
		if (ret)
			return ret;
	}
	ret = ar0832_write_reg_helper(info, 0x0104, 0x00);
	if (ret)
		return ret;

	return 0;
}

static int ar0832_set_gain(struct ar0832_info *info, __u16 gain)
{
	int ret = 0;
	/*struct ar0832_reg reg_list_analog;
#if USE_DIGITAL_GAIN
	struct ar0832_reg reg_list_digtal[8];
	u16 i;
#endif

	pr_info("[%s] (0x%08x)\n", __func__,  gain->AnalogGain);

	ar0832_get_gain_reg(&reg_list_analog, gain->AnalogGain);
	ret = ar0832_write_reg_helper(info, 0x0104, 0x01);
	if (ret)
		return ret;
	ret = ar0832_write_reg_helper(info, reg_list_analog.addr,
						reg_list_analog.val);
	if (ret)
		return ret;
#if USE_DIGITAL_GAIN
	for(i=0; i<4; i++) {
		reg_list_digtal[i*2].addr = 0x020E + i*2;
		reg_list_digtal[i*2].val = gain->DigitalGain_Upper;
		reg_list_digtal[i*2+1].addr = 0x020F + i*2;
		reg_list_digtal[i*2+1].val = gain->DigitalGain_Lower;
		ret = ar0832_write_reg_helper(info, reg_list_digtal[i*2].addr,
						reg_list_digtal[i*2].val);
	}
	if (ret)
		return ret;
#endif
	ret = ar0832_write_reg_helper(info, 0x0104, 0x00);
	if (ret)
		return ret;
*/
	ret |= ar0832_write_reg_helper(info, 0x0104, 0x01);
	/* Gain Registers Start */
	ret |= ar0832_write_reg_helper(info, 0x3056, gain);
	ret |= ar0832_write_reg_helper(info, 0x3058, gain);
	ret |= ar0832_write_reg_helper(info, 0x305A, gain);
	ret |= ar0832_write_reg_helper(info, 0x305C, gain);
	/* Gain register End */
	ret |= ar0832_write_reg_helper(info, 0x0104, 0x00);

	return ret;
}

static int ar0832_set_mode(struct ar0832_info *info, struct ar0832_mode *mode)
{
	int sensor_mode;
	int err;
	int i;
	int ret;
	struct ar0832_reg reg_list[5];

	if (mode->xres == 3264 && mode->yres == 2448)
		sensor_mode = ar0832_MODE_3264X2448;
	else if (mode->xres == 2880 && mode->yres == 1620)
		sensor_mode = ar0832_MODE_2880X1620;
	else if (mode->xres == 1632 && mode->yres == 1224)
		sensor_mode = ar0832_MODE_1632X1224;
	else {
		pr_err("%s: invalid resolution supplied to set mode %d %d\n",
			__func__, mode->xres, mode->yres);
		return -EINVAL;
	}

	/* get a list of override regs for the asking frame length,    */
	/* coarse integration time, and gain.*/
	err = ar0832_write_table(info, mode_start, NULL, 0);
	if (err)
		return err;

	err = ar0832_write_table(info, mode_table[sensor_mode], NULL, 0);
	if (err)
		return err;

	/* When we change the resolution */
#if 0
	ar0832_get_frame_length_regs(reg_list, mode->frame_length);
	for (i = 0; i < 2; i++) {
		ret = ar0832_write_reg_helper(info, reg_list[i].addr,
			reg_list[i].val);
		if (ret)
			return ret;
	}

	ar0832_get_coarse_time_regs(reg_list + 2, mode->coarse_time);
	for (i = 0; i < 2; i++) {
		ret = ar0832_write_reg_helper(info, reg_list[i+2].addr,
				reg_list[i+2].val);
		if (ret)
			return ret;
	}
	ret = ar0832_set_gain(info, &(mode->gain));
	if (ret)
		return ret;
#endif

	err = ar0832_write_table(info, mode_end, NULL, 0);
	if (err)
		return err;

	info->mode = sensor_mode;
	pr_info("%s: end\n", __func__);
	return 0;
}

static int ar0832_get_status(struct ar0832_info *info, u8 *status)
{
	int err = 0;

	*status = 0;
	/* FixMe */
	/*
	err = ar0832_read_reg(info->i2c_client, 0x001, status);
	*/
	pr_info("%s: %u %d\n", __func__, *status, err);
	return err;
}
static int ar0832_set_region(struct ar0832_info *info,
			     struct ar0832_stereo_region *region)
{
	u16 image_width = region->image_end.x - region->image_start.x+1;
	u16 image_height = region->image_end.y - region->image_start.y+1;
	struct i2c_client *i2c_client;
	pr_info("%s: %d\n", __func__, region->camer_index);
	if (region->camer_index == 0)
		i2c_client = info->i2c_client;
	else if (region->camer_index == 1)
		i2c_client = info->i2c_client_right;
	else
		return -1;
	pr_info("%s: width = %d  height = %d\n", __func__, image_width,
						image_height);
#if 0
	ar0832_write_reg(i2c_client, 0x0104, 1);
	ar0832_write_reg(i2c_client, 0x0346,
			 UpperByte16to8(region->image_start.y));
	/* Y_ADDR START  LOWER BYTE */
	ar0832_write_reg(i2c_client, 0x0347,
			 LowerByte16to8(region->image_start.y));
	/* Y_OUT SIZE UPPER BYTE */
	ar0832_write_reg(i2c_client, 0x034E,
			 UpperByte16to8(DefaultImageHeight));
	/* Y_OUT SIZE LOWER BYTE */
	ar0832_write_reg(i2c_client, 0x034F,
			 LowerByte16to8(DefaultImageHeight));
	/* Y_ADDR_END UPPER BYTE */
	ar0832_write_reg(i2c_client, 0x034A,
			 UpperByte16to8(region->image_end.y));
	/* Y_ADDR_END LOWER BYTE */
	ar0832_write_reg(i2c_client, 0x034B,
			 LowerByte16to8(region->image_end.y));
	/* X_ADDR_START UPPER BYTE */
	ar0832_write_reg(i2c_client, 0x0344,
			 UpperByte16to8(region->image_start.x));
	/* X_ADDR START LOWER BYTE */
	ar0832_write_reg(i2c_client, 0x0345,
			 LowerByte16to8(region->image_start.x));
	/* X_SIZE UPPER BYTE */
	ar0832_write_reg(i2c_client, 0x034C,
			 UpperByte16to8(DefaultImageWidth));
	/* X_SIZE LOWER BYTE */
	ar0832_write_reg(i2c_client, 0x034D,
			 LowerByte16to8(DefaultImageWidth));
	/* X_ADDR_END UPPER BYTE */
	ar0832_write_reg(i2c_client, 0x0348,
			 UpperByte16to8(region->image_end.x));
	/* X_ADDR_END LOWER BYTE */
	ar0832_write_reg(i2c_client, 0x0349,
			 LowerByte16to8(region->image_end.x));
	ar0832_write_reg(i2c_client, 0x0104, 0);
#endif
	return 0;
}

static int ar0832_power_on(void)
{
	pr_info("%s: ++\n", __func__);
	info->pdata->power_on();

	return 0;
}


static int ar0832_focuser_set_config(struct ar0832_focuser_info *info)
{
	struct ar0832_reg reg_list[2];
	int ret;
	int i = 0;
	u8 vcm_slew = 5;
	/*
	bit15(0x80) means that VCM driver enable bit.
	bit3(0x08) means that keep VCM(AF position)
	while sensor is in soft standby mode during mode transitions.
	*/
	u16 vcm_control_data = (0x80 << 8 | (0x08 | (vcm_slew & 0x07)));
	/* Gene's code for AF */
	u16 vcm_step_time = 4096;

	pr_info("[%s] vcm_control_data (0x%08x)\n", __func__,
						vcm_control_data);
	ar0832_get_focuser_vcm_control_regs(reg_list, vcm_control_data);
	for (i = 0; i < 2; i++) {
		ret = ar0832_focuser_write_helper(info, reg_list[i].val);
		if (ret)
			return ret;
	}

	pr_info("[%s] vcm_control_data (0x%08x)\n", __func__,  vcm_step_time);
	ar0832_get_focuser_vcm_step_time_regs(reg_list, vcm_step_time);
	for (i = 0; i < 2; i++) {
		ret = ar0832_focuser_write_helper(info,	reg_list[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

static int ar0832_focuser_set_position(struct ar0832_focuser_info *info, u32 position)
{
	int ret;
	struct ar0832_reg reg_list[2];
	int i = 0;

	if (position < info->config.pos_low ||
		position > info->config.pos_high)
		return -EINVAL;

	pr_info("[%s] (0x%08x)\n", __func__,  position);
	ar0832_get_focuser_data_regs(reg_list, position);

	for (i = 0; i < 2; i++) {
		ret = ar0832_focuser_write_helper(info,	reg_list[i].val);
		if (ret)
			return ret;
	}
	return 0;
}

static long ar0832_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	int err;
	struct ar0832_info *info = file->private_data;

	pr_info("ar0832_ioctl : cmd = %d\n", cmd);

	switch (cmd) {
	case AR0832_IOCTL_SET_POWER_ON:
		pr_info("AR0832_IOCTL_SET_POWER_ON\n");
		return ar0832_power_on();
	case AR0832_IOCTL_SET_MODE:
	{
		struct ar0832_mode mode;
		pr_info("AR0832_IOCTL_SET_MODE\n");
		if (copy_from_user(&mode,
			(const void __user *)arg,
			sizeof(struct ar0832_mode))) {
			pr_info("%s %d\n", __func__, __LINE__);
			return -EFAULT;
		}
		mutex_lock(&ar0832_camera_lock);
		err = ar0832_set_mode(info, &mode);
		if (focuser_info->focuser_init_flag == false) {
			ar0832_focuser_set_config(focuser_info);
			focuser_info->focuser_init_flag = true;
		}
		mutex_unlock(&ar0832_camera_lock);
		return err;
	}
	case AR0832_IOCTL_SET_FRAME_LENGTH:
		mutex_lock(&ar0832_camera_lock);
		err = ar0832_set_frame_length(info, (u32)arg);
		mutex_unlock(&ar0832_camera_lock);
		return err;
	case AR0832_IOCTL_SET_COARSE_TIME:
		mutex_lock(&ar0832_camera_lock);
		err = ar0832_set_coarse_time(info, (u32)arg);
		mutex_unlock(&ar0832_camera_lock);
		return err;
	case AR0832_IOCTL_SET_GAIN:
	{
		mutex_lock(&ar0832_camera_lock);
		err = ar0832_set_gain(info, (u16)arg);
		mutex_unlock(&ar0832_camera_lock);
		return err;
	}
	case AR0832_IOCTL_GET_STATUS:
	{
		u8 status;
		pr_info("AR0832_IOCTL_GET_STATUS\n");
		err = ar0832_get_status(info, &status);
		if (err)
			return err;
		if (copy_to_user((void __user *)arg, &status,
				 2)) {
			pr_info("%s %d\n", __func__, __LINE__);
			return -EFAULT;
		}
		return 0;
	}
	case AR0832_IOCTL_SET_SENSOR_REGION:
	{
		struct ar0832_stereo_region region;
		pr_info("AR0832_IOCTL_SET_SENSOR_REGION\n");
		if (copy_from_user(&region,
			(const void __user *)arg,
			sizeof(struct ar0832_stereo_region))) {
			pr_info("%s %d\n", __func__, __LINE__);
			return -EFAULT;
		}
		/* TO-DO */
		/*
		mutex_lock(&ar0832_camera_lock);
		err = ar0832_set_region(info, &region);
		mutex_unlock(&ar0832_camera_lock);
		*/
		return err;
	}

	case AR0832_FOCUSER_IOCTL_GET_CONFIG:
	{
		pr_info("%s AR0832_FOCUSER_IOCTL_GET_CONFIG\n", __func__);
		if (copy_to_user((void __user *) arg,
				 &focuser_info->config,
				 sizeof(focuser_info->config))) {
			pr_err("%s: 0x%x\n", __func__, __LINE__);
			return -EFAULT;
		}
		return 0;
	}
	case AR0832_FOCUSER_IOCTL_SET_POSITION:
		pr_info("%s AR0832_FOCUSER_IOCTL_SET_POSITION\n", __func__);
		mutex_lock(&ar0832_camera_lock);
		err = ar0832_focuser_set_position(focuser_info, (u32) arg);
		mutex_unlock(&ar0832_camera_lock);
		return err;
	case AR0832_FOCUSER_IOCTL_SET_MODE:
		pr_info("%s AR0832_FOCUSER_IOCTL_SET_MODE\n", __func__);
		return 0;

	default:
		pr_info("(error) %s NONE IOCTL\n", __func__);
		return -EINVAL;
	}
	return 0;
}

static int ar0832_open(struct inode *inode, struct file *file)
{
	pr_info("%s: ++\n", __func__);
	file->private_data = info;
	focuser_info->focuser_init_flag = false;
	return 0;
}

static int ar0832_release(struct inode *inode, struct file *file)
{
	pr_info("%s: ++\n", __func__);

	file->private_data = NULL;
	info->pdata->power_off();
	return 0;
}

static const struct file_operations ar0832_fileops = {
	.owner = THIS_MODULE,
	.open = ar0832_open,
	.unlocked_ioctl = ar0832_ioctl,
	.release = ar0832_release,
};

static struct miscdevice ar0832_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "ar0832",
	.fops = &ar0832_fileops,
	.mode = S_IRWXUGO
};

static int ar0832_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int err;
	pr_info("ar0832: probing sensor.(id:%s)\n", id->name);

	info = kzalloc(sizeof(struct ar0832_info), GFP_KERNEL);
	focuser_info = kzalloc(sizeof(struct ar0832_focuser_info), GFP_KERNEL);
	if (!info || !focuser_info) {
		pr_err("ar0832: Unable to allocate memory!\n");
		return -ENOMEM;
	}

	err = misc_register(&ar0832_device);
	if (err) {
		pr_err("ar0832: Unable to register misc device!\n");
		kfree(info);
		kfree(focuser_info);
		return err;
	}

	/* sensor */
	info->pdata = client->dev.platform_data;
	info->i2c_client = client;

	/* focuser */
	focuser_info->i2c_client = client;
	focuser_info->config.settle_time = SETTLETIME_MS;
	/* FIX-ME */
	/*
	focuser_info->config.focal_length = FOCAL_LENGTH;
	focuser_info->config.fnumber = FNUMBER;
	*/
	focuser_info->config.pos_low = POS_LOW;
	focuser_info->config.pos_high = POS_HIGH;

	i2c_set_clientdata(client, info);

	return 0;
}

static int ar0832_remove(struct i2c_client *client)
{
	misc_deregister(&ar0832_device);
	kfree(info);
	kfree(focuser_info);
	return 0;
}

static const struct i2c_device_id ar0832_id[] = {
	{ "ar0832", 0 },
};

static struct i2c_driver ar0832_i2c_driver = {
	.probe = ar0832_probe,
	.remove = ar0832_remove,
	.id_table = ar0832_id,
	.driver = {
		.name = "ar0832",
		.owner = THIS_MODULE,
	},
};

static int __init ar0832_init(void)
{
	pr_info("%s: ++\n", __func__);
	return i2c_add_driver(&ar0832_i2c_driver);
}

static void __exit ar0832_exit(void)
{
	i2c_del_driver(&ar0832_i2c_driver);
}

module_init(ar0832_init);
module_exit(ar0832_exit);
