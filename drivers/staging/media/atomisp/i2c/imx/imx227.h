#ifndef __IMX227_H__
#define __IMX227_H__

#include "common.h"

#define IMX227_EMBEDDED_DATA_LINE_NUM 2
#define IMX227_OUTPUT_DATA_FORMAT_REG  0x0112
#define IMX227_OUTPUT_FORMAT_RAW10  0x0a0a

/* AE Bracketing Registers */
#define IMX227_BRACKETING_LUT_MODE_BIT_CONTINUE_STREAMING	0x1
#define IMX227_BRACKETING_LUT_MODE_BIT_LOOP_MODE	0x2

#define IMX227_BRACKETING_LUT_CONTROL		0x0E00
#define IMX227_BRACKETING_LUT_MODE		0x0E01
#define IMX227_BRACKETING_LUT_ENTRY_CONTROL	0x0E02

/*
 * The imx135 embedded data info:
 * embedded data line num: 2
 * line 0 effective data size(byte): 76
 * line 1 effective data size(byte): 113
 */
static const uint32_t
imx227_embedded_effective_size[IMX227_EMBEDDED_DATA_LINE_NUM] = {160, 62};

/************************** settings for imx *************************/
/* Full Output Mode */
static struct imx_reg const imx_STILL_6_5M_25fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x6259, 0x06},  /*	latency ctrl	*/
	{IMX_8BIT, 0x9004, 0xd0},  /*	preset_sel	*/
	{IMX_8BIT, 0x9005, 0x3f},  /*	preset_en	*/
	{IMX_8BIT, 0x0136, 0x13},
	{IMX_8BIT, 0x0137, 0x33},
	{IMX_TOK_TERM, 0, 0}
};

/* 4:3 Output Mode */
static struct imx_reg const imx_STILL_5_5M_3X4_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0344, 0x00},
	{IMX_8BIT, 0x0345, 0xb0},
	{IMX_8BIT, 0x0346, 0x00},
	{IMX_8BIT, 0x0347, 0x00},
	{IMX_8BIT, 0x0348, 0x08},
	{IMX_8BIT, 0x0349, 0xaf},
	{IMX_8BIT, 0x034a, 0x0a},
	{IMX_8BIT, 0x034b, 0x9f},
	{IMX_8BIT, 0x034c, 0x08},
	{IMX_8BIT, 0x034d, 0x00},
	{IMX_8BIT, 0x034e, 0x0a},
	{IMX_8BIT, 0x034f, 0xa0},

	{IMX_8BIT, 0x6259, 0x05},  /*	latency ctrl	*/
	{IMX_8BIT, 0x9004, 0xd8},  /*	preset_sel	*/
	{IMX_8BIT, 0x9005, 0x3f},  /*	preset_en	*/
	{IMX_8BIT, 0x0136, 0x13},
	{IMX_8BIT, 0x0137, 0x33},
	{IMX_TOK_TERM, 0, 0}
};

/* Square Output Mode */
static struct imx_reg const imx_STILL_5_7M_1X1_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0344, 0x00},
	{IMX_8BIT, 0x0345, 0x00},
	{IMX_8BIT, 0x0346, 0x00},
	{IMX_8BIT, 0x0347, 0xa0},
	{IMX_8BIT, 0x0348, 0x09},
	{IMX_8BIT, 0x0349, 0x5f},
	{IMX_8BIT, 0x034a, 0x09},
	{IMX_8BIT, 0x034b, 0xff},
	{IMX_8BIT, 0x034c, 0x09},
	{IMX_8BIT, 0x034d, 0x60},
	{IMX_8BIT, 0x034e, 0x09},
	{IMX_8BIT, 0x034f, 0x60},

	{IMX_8BIT, 0x6259, 0x06},  /*	latency ctrl	*/
	{IMX_8BIT, 0x9004, 0xd4},  /*	preset_sel	*/
	{IMX_8BIT, 0x9005, 0x3f},  /*	preset_en	*/
	{IMX_8BIT, 0x0136, 0x13},
	{IMX_8BIT, 0x0137, 0x33},
	{IMX_TOK_TERM, 0, 0}
};

/* Full Frame 1080P Mode (use ISP scaler)*/
static struct imx_reg const imx_VIDEO_4M_9X16_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x6259, 0x05},  /*	latency ctrl	*/
	{IMX_8BIT, 0x9004, 0xdc},  /*	preset_sel	*/
	{IMX_8BIT, 0x9005, 0x3f},  /*	preset_en	*/
	{IMX_8BIT, 0x0136, 0x13},
	{IMX_8BIT, 0x0137, 0x33},
	{IMX_TOK_TERM, 0, 0}
};

/* Cropped 1080P Mode */
static struct imx_reg const imx_VIDEO_2M_9X16_45fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0112, 0x0a},
	{IMX_8BIT, 0x0113, 0x0a},
	{IMX_8BIT, 0x0344, 0x02},
	{IMX_8BIT, 0x0345, 0x8a},
	{IMX_8BIT, 0x0346, 0x01},
	{IMX_8BIT, 0x0347, 0x88},
	{IMX_8BIT, 0x0348, 0x06},
	{IMX_8BIT, 0x0349, 0xd1},
	{IMX_8BIT, 0x034a, 0x09},
	{IMX_8BIT, 0x034b, 0x17},
	{IMX_8BIT, 0x034c, 0x04},
	{IMX_8BIT, 0x034d, 0x48},
	{IMX_8BIT, 0x034e, 0x07},
	{IMX_8BIT, 0x034f, 0x90},

	{IMX_8BIT, 0x0380, 0x00},
	{IMX_8BIT, 0x0381, 0x01},
	{IMX_8BIT, 0x0382, 0x00},
	{IMX_8BIT, 0x0383, 0x01},
	{IMX_8BIT, 0x0384, 0x00},
	{IMX_8BIT, 0x0385, 0x01},
	{IMX_8BIT, 0x0386, 0x00},
	{IMX_8BIT, 0x0387, 0x01},

	{IMX_8BIT, 0x0408, 0x00},
	{IMX_8BIT, 0x0409, 0x00},
	{IMX_8BIT, 0x040a, 0x00},
	{IMX_8BIT, 0x040b, 0x00},
	{IMX_8BIT, 0x040c, 0x04},
	{IMX_8BIT, 0x040d, 0x48},
	{IMX_8BIT, 0x040e, 0x07},
	{IMX_8BIT, 0x040f, 0x90},

	{IMX_8BIT, 0x0900, 0x00},
	{IMX_8BIT, 0x0901, 0x00},

	{IMX_8BIT, 0x6259, 0x05},  /*	latency ctrl	*/
	{IMX_8BIT, 0x9004, 0xdc},  /*	preset_sel	*/
	{IMX_8BIT, 0x9005, 0x3c},  /*	preset_en	*/

	{IMX_8BIT, 0x0136, 0x13},
	{IMX_8BIT, 0x0137, 0x33},
	{IMX_TOK_TERM, 0, 0}
};

/* Moment mode */
static struct imx_reg const imx_VIDEO_1_3M_3X4_60fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x6259, 0x05},  /*	latency ctrl	*/
	{IMX_8BIT, 0x9004, 0xd9},  /*	preset_sel	*/
	{IMX_8BIT, 0x9005, 0x3f},  /*	preset_en	*/
	{IMX_8BIT, 0x0136, 0x13},
	{IMX_8BIT, 0x0137, 0x33},
	{IMX_TOK_TERM, 0, 0}
};

/* High Speed 3:4 mode */
static struct imx_reg const imx_VIDEO_VGA_3X4_120fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x9004, 0xca},  /*	preset_sel	*/
	{IMX_8BIT, 0x9005, 0x3f},  /*	preset_en	*/
	{IMX_8BIT, 0x0136, 0x13},
	{IMX_8BIT, 0x0137, 0x33},
	{IMX_TOK_TERM, 0, 0}
};


/* Binned 720P mode */
static struct imx_reg const imx_VIDEO_1M_9X16_60fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0112, 0x0a},
	{IMX_8BIT, 0x0113, 0x0a},
	{IMX_8BIT, 0x0344, 0x01},
	{IMX_8BIT, 0x0345, 0xd0},
	{IMX_8BIT, 0x0346, 0x00},
	{IMX_8BIT, 0x0347, 0x40},
	{IMX_8BIT, 0x0348, 0x07},
	{IMX_8BIT, 0x0349, 0x8f},
	{IMX_8BIT, 0x034a, 0x0a},
	{IMX_8BIT, 0x034b, 0x5f},
	{IMX_8BIT, 0x034c, 0x02},
	{IMX_8BIT, 0x034d, 0xe0},
	{IMX_8BIT, 0x034e, 0x05},
	{IMX_8BIT, 0x034f, 0x10},

	{IMX_8BIT, 0x0380, 0x00},
	{IMX_8BIT, 0x0381, 0x01},
	{IMX_8BIT, 0x0382, 0x00},
	{IMX_8BIT, 0x0383, 0x01},
	{IMX_8BIT, 0x0384, 0x00},
	{IMX_8BIT, 0x0385, 0x01},
	{IMX_8BIT, 0x0386, 0x00},
	{IMX_8BIT, 0x0387, 0x01},

	{IMX_8BIT, 0x0408, 0x00},
	{IMX_8BIT, 0x0409, 0x00},
	{IMX_8BIT, 0x040a, 0x00},
	{IMX_8BIT, 0x040b, 0x00},
	{IMX_8BIT, 0x040c, 0x02},
	{IMX_8BIT, 0x040d, 0xe0},
	{IMX_8BIT, 0x040e, 0x05},
	{IMX_8BIT, 0x040f, 0x10},

	{IMX_8BIT, 0x0900, 0x01},
	{IMX_8BIT, 0x0901, 0x22},

	{IMX_8BIT, 0x6259, 0x05},  /*	latency ctrl	*/
	{IMX_8BIT, 0x9004, 0xdd},  /*	preset_sel	*/
	{IMX_8BIT, 0x9005, 0x3c},  /*	preset_en	*/
	{IMX_8BIT, 0x0136, 0x13},
	{IMX_8BIT, 0x0137, 0x33},
	{IMX_TOK_TERM, 0, 0}
};

/* Binned 496x868 mode */
static struct imx_reg const imx_VIDEO_496x868_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0112, 0x0a},
	{IMX_8BIT, 0x0113, 0x0a},
	{IMX_8BIT, 0x0344, 0x02},
	{IMX_8BIT, 0x0345, 0xc0},
	{IMX_8BIT, 0x0346, 0x01},
	{IMX_8BIT, 0x0347, 0xec},
	{IMX_8BIT, 0x0348, 0x06},
	{IMX_8BIT, 0x0349, 0x9f},
	{IMX_8BIT, 0x034a, 0x08},
	{IMX_8BIT, 0x034b, 0xb3},
	{IMX_8BIT, 0x034c, 0x01},
	{IMX_8BIT, 0x034d, 0xf0},
	{IMX_8BIT, 0x034e, 0x03},
	{IMX_8BIT, 0x034f, 0x64},

	{IMX_8BIT, 0x0380, 0x00},
	{IMX_8BIT, 0x0381, 0x01},
	{IMX_8BIT, 0x0382, 0x00},
	{IMX_8BIT, 0x0383, 0x01},
	{IMX_8BIT, 0x0384, 0x00},
	{IMX_8BIT, 0x0385, 0x01},
	{IMX_8BIT, 0x0386, 0x00},
	{IMX_8BIT, 0x0387, 0x01},

	{IMX_8BIT, 0x0408, 0x00},
	{IMX_8BIT, 0x0409, 0x00},
	{IMX_8BIT, 0x040a, 0x00},
	{IMX_8BIT, 0x040b, 0x00},
	{IMX_8BIT, 0x040c, 0x01},
	{IMX_8BIT, 0x040d, 0xf0},
	{IMX_8BIT, 0x040e, 0x03},
	{IMX_8BIT, 0x040f, 0x64},

	{IMX_8BIT, 0x0900, 0x01},
	{IMX_8BIT, 0x0901, 0x22},

	{IMX_8BIT, 0x6259, 0x05},  /*	latency ctrl	*/
	{IMX_8BIT, 0x9004, 0xdd},  /*	preset_sel	*/
	{IMX_8BIT, 0x9005, 0x3c},  /*	preset_en	*/
	{IMX_8BIT, 0x0136, 0x13},
	{IMX_8BIT, 0x0137, 0x33},
	{IMX_TOK_TERM, 0, 0}
};


/* Hangout mode */
static struct imx_reg const imx_PREVIEW_374X652_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0112, 0x0a},
	{IMX_8BIT, 0x0113, 0x0a},
	{IMX_8BIT, 0x0344, 0x01},
	{IMX_8BIT, 0x0345, 0xc0},
	{IMX_8BIT, 0x0346, 0x00},
	{IMX_8BIT, 0x0347, 0x30},
	{IMX_8BIT, 0x0348, 0x07},
	{IMX_8BIT, 0x0349, 0x9f},
	{IMX_8BIT, 0x034a, 0x0a},
	{IMX_8BIT, 0x034b, 0x6f},
	{IMX_8BIT, 0x034c, 0x01},
	{IMX_8BIT, 0x034d, 0x78},
	{IMX_8BIT, 0x034e, 0x02},
	{IMX_8BIT, 0x034f, 0x90},

	{IMX_8BIT, 0x0380, 0x00},
	{IMX_8BIT, 0x0381, 0x01},
	{IMX_8BIT, 0x0382, 0x00},
	{IMX_8BIT, 0x0383, 0x03},
	{IMX_8BIT, 0x0384, 0x00},
	{IMX_8BIT, 0x0385, 0x01},
	{IMX_8BIT, 0x0386, 0x00},
	{IMX_8BIT, 0x0387, 0x03},

	{IMX_8BIT, 0x0408, 0x00},
	{IMX_8BIT, 0x0409, 0x00},
	{IMX_8BIT, 0x040a, 0x00},
	{IMX_8BIT, 0x040b, 0x02},
	{IMX_8BIT, 0x040c, 0x01},
	{IMX_8BIT, 0x040d, 0x76},
	{IMX_8BIT, 0x040e, 0x02},
	{IMX_8BIT, 0x040f, 0x8c},

	{IMX_8BIT, 0x0900, 0x01},
	{IMX_8BIT, 0x0901, 0x22},

	{IMX_8BIT, 0x6259, 0x05},  /*	latency ctrl	*/
	{IMX_8BIT, 0x9004, 0xde},  /*	preset_sel	*/
	{IMX_8BIT, 0x9005, 0x3c},  /*	preset_en	*/
	{IMX_8BIT, 0x0136, 0x13},
	{IMX_8BIT, 0x0137, 0x33},
	{IMX_TOK_TERM, 0, 0}
};

static struct imx_reg const imx_VIDEO_NHD_9X16_30fps[] = {
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0112, 0x0a},
	{IMX_8BIT, 0x0113, 0x0a},
	{IMX_8BIT, 0x0344, 0x01},
	{IMX_8BIT, 0x0345, 0xc0},
	{IMX_8BIT, 0x0346, 0x00},
	{IMX_8BIT, 0x0347, 0x30},
	{IMX_8BIT, 0x0348, 0x07},
	{IMX_8BIT, 0x0349, 0x9f},
	{IMX_8BIT, 0x034a, 0x0a},
	{IMX_8BIT, 0x034b, 0x6f},
	{IMX_8BIT, 0x034c, 0x01},
	{IMX_8BIT, 0x034d, 0x78},
	{IMX_8BIT, 0x034e, 0x02},
	{IMX_8BIT, 0x034f, 0x90},

	{IMX_8BIT, 0x0380, 0x00},
	{IMX_8BIT, 0x0381, 0x01},
	{IMX_8BIT, 0x0382, 0x00},
	{IMX_8BIT, 0x0383, 0x03},
	{IMX_8BIT, 0x0384, 0x00},
	{IMX_8BIT, 0x0385, 0x01},
	{IMX_8BIT, 0x0386, 0x00},
	{IMX_8BIT, 0x0387, 0x03},

	{IMX_8BIT, 0x0408, 0x00},
	{IMX_8BIT, 0x0409, 0x00},
	{IMX_8BIT, 0x040a, 0x00},
	{IMX_8BIT, 0x040b, 0x00},
	{IMX_8BIT, 0x040c, 0x01},
	{IMX_8BIT, 0x040d, 0x78},
	{IMX_8BIT, 0x040e, 0x02},
	{IMX_8BIT, 0x040f, 0x90},

	{IMX_8BIT, 0x0900, 0x01},
	{IMX_8BIT, 0x0901, 0x22},

	{IMX_8BIT, 0x6259, 0x05},  /*	latency ctrl	*/
	{IMX_8BIT, 0x9004, 0xde},  /*	preset_sel	*/
	{IMX_8BIT, 0x9005, 0x3c},  /*	preset_en	*/
	{IMX_8BIT, 0x0136, 0x13},
	{IMX_8BIT, 0x0137, 0x33},
	{IMX_TOK_TERM, 0, 0}
};


static struct imx_reg const imx227_init_settings[] = {
	{IMX_8BIT, 0x0100, 0x00},  /*   mode_select     */
	GROUPED_PARAMETER_HOLD_ENABLE,
	{IMX_8BIT, 0x0306, 0x00},
	{IMX_8BIT, 0x0307, 0xBB},
	{IMX_8BIT, 0x030E, 0x03},
	{IMX_8BIT, 0x030F, 0x0D},
	{IMX_8BIT, 0x463b, 0x30},
	{IMX_8BIT, 0x463e, 0x05},
	{IMX_8BIT, 0x4612, 0x66},
	{IMX_8BIT, 0x4815, 0x65},
	{IMX_8BIT, 0x4991, 0x00},
	{IMX_8BIT, 0x4992, 0x01},
	{IMX_8BIT, 0x4993, 0xff},
	{IMX_8BIT, 0x458b, 0x00},
	{IMX_8BIT, 0x452a, 0x02},
	{IMX_8BIT, 0x4a7c, 0x00},
	{IMX_8BIT, 0x4a7d, 0x1c},
	{IMX_8BIT, 0x4a7e, 0x00},
	{IMX_8BIT, 0x4a7f, 0x17},
	{IMX_8BIT, 0x462C, 0x2E},
	{IMX_8BIT, 0x461B, 0x28},
	{IMX_8BIT, 0x4663, 0x29},
	{IMX_8BIT, 0x461A, 0x7C},
	{IMX_8BIT, 0x4619, 0x28},
	{IMX_8BIT, 0x4667, 0x22},
	{IMX_8BIT, 0x466B, 0x23},
	{IMX_8BIT, 0x40AD, 0xFF},
	{IMX_8BIT, 0x40BE, 0x00},
	{IMX_8BIT, 0x40BF, 0x6E},
	{IMX_8BIT, 0x40CE, 0x00},
	{IMX_8BIT, 0x40CF, 0x0A},
	{IMX_8BIT, 0x40CA, 0x00},
	{IMX_8BIT, 0x40CB, 0x1F},
	{IMX_8BIT, 0x4D16, 0x00},
	{IMX_8BIT, 0x6204, 0x01},
	{IMX_8BIT, 0x6209, 0x00},
	{IMX_8BIT, 0x621F, 0x01},
	{IMX_8BIT, 0x621E, 0x10},
	GROUPED_PARAMETER_HOLD_DISABLE,
	{IMX_TOK_TERM, 0, 0}
};

/* TODO settings of preview/still/video will be updated with new use case */
struct imx_resolution imx227_res_preview[] = {
	{
		.desc = "imx_PREVIEW_374X652_30fps",
		.regs = imx_PREVIEW_374X652_30fps,
		.width = 374,
		.height = 652,
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x0E70,
				.lines_per_frame = 0x0C0A,
			},
			{
			}
		},
	},
	{
		.desc = "imx_VIDEO_496x868_30fps",
		.regs = imx_VIDEO_496x868_30fps,
		.width = 496,
		.height = 868,
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x0E70,
				.lines_per_frame = 0x0C08,
			},
			{
			}
		},
	},
	{
		.desc = "imx_STILL_5_5M_3X4_30fps",
		.regs = imx_STILL_5_5M_3X4_30fps,
		.width = 2048,
		.height = 2720,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x0ED8,
				.lines_per_frame = 0x0BB8,
			},
			{
			}
		},

	},
	{
		.desc = "imx_STILL_5_7M_1X1_30fps",
		.regs = imx_STILL_5_7M_1X1_30fps,
		.width = 2400,
		.height = 2400,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x1130,
				.lines_per_frame = 0x0A1E,
			},
			{
			}
		},

	},
	{
		.desc = "imx_STILL_6_5M_25fps",
		.regs = imx_STILL_6_5M_25fps,
		.width = 2400,
		.height = 2720,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 25,
				.pixels_per_line = 0x1130,
				.lines_per_frame = 0x0C24,
			},
			{
			}
		},
	}
};

struct imx_resolution imx227_res_still[] = {
	{
		.desc = "imx_STILL_5_5M_3X4_30fps",
		.regs = imx_STILL_5_5M_3X4_30fps,
		.width = 2048,
		.height = 2720,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 6,
				.pixels_per_line = 0x2130,
				.lines_per_frame = 0x1A22,
			},
			{
				.fps = 30,
				.pixels_per_line = 0x0ED8,
				.lines_per_frame = 0x0BB8,
			},
			{
			}
		},

	},
	{
		.desc = "imx_STILL_5_7M_1X1_30fps",
		.regs = imx_STILL_5_7M_1X1_30fps,
		.width = 2400,
		.height = 2400,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 6,
				.pixels_per_line = 0x266E,
				.lines_per_frame = 0x1704,
			},
			{
				.fps = 30,
				.pixels_per_line = 0x1130,
				.lines_per_frame = 0x0A1E,
			},
			{
			}
		},

	},
	{
		.desc = "imx_STILL_6_5M_25fps",
		.regs = imx_STILL_6_5M_25fps,
		.width = 2400,
		.height = 2720,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 25,
				.pixels_per_line = 0x1130,
				.lines_per_frame = 0x0C24,
			},
			{
			}
		},
	},
};

struct imx_resolution imx227_res_video[] = {
	{
		.desc = "imx_VIDEO_4M_9X16_30fps",
		.regs = imx_VIDEO_4M_9X16_30fps,
		.width = 1536,
		.height = 2720,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x0E70,
				.lines_per_frame = 0x0C08,
			},
			{
			}
		},

	},
	{
		.desc = "imx_VIDEO_2M_9X16_45fps",
		.regs = imx_VIDEO_2M_9X16_45fps,
		.width = 1096,
		.height = 1936,
		.bin_factor_x = 0,
		.bin_factor_y = 0,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x0E70,
				.lines_per_frame = 0x0C08,
			},
			{
				.fps = 45,
				.pixels_per_line = 0x0E70,
				.lines_per_frame = 0x0800,
			},
			{
			}
		},

	},
	{
		.desc = "imx_VIDEO_1_3M_3X4_60fps",
		.regs = imx_VIDEO_1_3M_3X4_60fps,
		.width = 1024,
		.height = 1360,
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 60,
				.pixels_per_line = 0x0E70,
				.lines_per_frame = 0x0604,
			},
			{
			}
		},
	},
	{
		.desc = "imx_VIDEO_496x868_30fps",
		.regs = imx_VIDEO_496x868_30fps,
		.width = 496,
		.height = 868,
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x0E70,
				.lines_per_frame = 0x0C08,
			},
			{
			}
		},
	},
	{
		.desc = "imx_VIDEO_1M_9X16_60fps",
		.regs = imx_VIDEO_1M_9X16_60fps,
		.width = 736,
		.height = 1296,
		.bin_factor_x = 1,
		.bin_factor_y = 1,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 60,
				.pixels_per_line = 0x0E70,
				.lines_per_frame = 0x0604,
			},
			{
				.fps = 30,
				.pixels_per_line = 0x0E70,
				.lines_per_frame = 0x0C10,
			},
			{
			}
		},
	},
	{
		.desc = "imx_VIDEO_VGA_3X4_120fps",
		.regs = imx_VIDEO_VGA_3X4_120fps,
		.width = 512,
		.height = 680,
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 120,
				.pixels_per_line = 0x0E70,
				.lines_per_frame = 0x0302,
			},
			{
				.fps = 30,
				.pixels_per_line = 0x0E70,
				.lines_per_frame = 0x0C08,
			},
			{
			}
		},
	},
	{
		.desc = "imx_VIDEO_NHD_9X16_30fps",
		.regs = imx_VIDEO_NHD_9X16_30fps,
		.width = 376,
		.height = 656,
		.bin_factor_x = 2,
		.bin_factor_y = 2,
		.mipi_freq = 499000,
		.used = 0,
		.fps_options = {
			{
				.fps = 30,
				.pixels_per_line = 0x0E70,
				.lines_per_frame = 0x0C0A,
			},
			{
			}
		},
	},
};

#endif /* __IMX227_H__ */
