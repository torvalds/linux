// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2022 Intel Corporation.

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define OV08D10_SCLK			144000000ULL
#define OV08D10_XVCLK_19_2		19200000
#define OV08D10_ROWCLK			36000
#define OV08D10_DATA_LANES		2
#define OV08D10_RGB_DEPTH		10

#define OV08D10_REG_PAGE		0xfd
#define OV08D10_REG_GLOBAL_EFFECTIVE		0x01
#define OV08D10_REG_CHIP_ID_0		0x00
#define OV08D10_REG_CHIP_ID_1		0x01
#define OV08D10_ID_MASK			GENMASK(15, 0)
#define OV08D10_CHIP_ID			0x5608

#define OV08D10_REG_MODE_SELECT		0xa0
#define OV08D10_MODE_STANDBY		0x00
#define OV08D10_MODE_STREAMING		0x01

/* vertical-timings from sensor */
#define OV08D10_REG_VTS_H		0x05
#define OV08D10_REG_VTS_L		0x06
#define OV08D10_VTS_MAX			0x7fff

/* Exposure controls from sensor */
#define OV08D10_REG_EXPOSURE_H		0x02
#define OV08D10_REG_EXPOSURE_M		0x03
#define OV08D10_REG_EXPOSURE_L		0x04
#define	OV08D10_EXPOSURE_MIN		6
#define OV08D10_EXPOSURE_MAX_MARGIN	6
#define	OV08D10_EXPOSURE_STEP		1

/* Analog gain controls from sensor */
#define OV08D10_REG_ANALOG_GAIN		0x24
#define	OV08D10_ANAL_GAIN_MIN		128
#define	OV08D10_ANAL_GAIN_MAX		2047
#define	OV08D10_ANAL_GAIN_STEP		1

/* Digital gain controls from sensor */
#define OV08D10_REG_MWB_DGAIN_C		0x21
#define OV08D10_REG_MWB_DGAIN_F		0x22
#define OV08D10_DGTL_GAIN_MIN		0
#define OV08D10_DGTL_GAIN_MAX		4095
#define OV08D10_DGTL_GAIN_STEP		1
#define OV08D10_DGTL_GAIN_DEFAULT	1024

/* Test Pattern Control */
#define OV08D10_REG_TEST_PATTERN		0x12
#define OV08D10_TEST_PATTERN_ENABLE		0x01
#define OV08D10_TEST_PATTERN_DISABLE		0x00

/* Flip Mirror Controls from sensor */
#define OV08D10_REG_FLIP_OPT			0x32
#define OV08D10_REG_FLIP_MASK			0x3

#define to_ov08d10(_sd)		container_of(_sd, struct ov08d10, sd)

struct ov08d10_reg {
	u8 address;
	u8 val;
};

struct ov08d10_reg_list {
	u32 num_of_regs;
	const struct ov08d10_reg *regs;
};

struct ov08d10_link_freq_config {
	const struct ov08d10_reg_list reg_list;
};

struct ov08d10_mode {
	/* Frame width in pixels */
	u32 width;

	/* Frame height in pixels */
	u32 height;

	/* Horizontal timining size */
	u32 hts;

	/* Default vertical timining size */
	u32 vts_def;

	/* Min vertical timining size */
	u32 vts_min;

	/* Link frequency needed for this resolution */
	u32 link_freq_index;

	/* Sensor register settings for this resolution */
	const struct ov08d10_reg_list reg_list;

	/* Number of data lanes */
	u8 data_lanes;
};

/* 3280x2460, 3264x2448 need 720Mbps/lane, 2 lanes */
static const struct ov08d10_reg mipi_data_rate_720mbps[] = {
	{0xfd, 0x00},
	{0x11, 0x2a},
	{0x14, 0x43},
	{0x1a, 0x04},
	{0x1b, 0xe1},
	{0x1e, 0x13},
	{0xb7, 0x02}
};

/* 1632x1224 needs 360Mbps/lane, 2 lanes */
static const struct ov08d10_reg mipi_data_rate_360mbps[] = {
	{0xfd, 0x00},
	{0x1a, 0x04},
	{0x1b, 0xe1},
	{0x1d, 0x00},
	{0x1c, 0x19},
	{0x11, 0x2a},
	{0x14, 0x54},
	{0x1e, 0x13},
	{0xb7, 0x02}
};

static const struct ov08d10_reg lane_2_mode_3280x2460[] = {
	/* 3280x2460 resolution */
	{0xfd, 0x01},
	{0x12, 0x00},
	{0x03, 0x12},
	{0x04, 0x58},
	{0x07, 0x05},
	{0x21, 0x02},
	{0x24, 0x30},
	{0x33, 0x03},
	{0x01, 0x03},
	{0x19, 0x10},
	{0x42, 0x55},
	{0x43, 0x00},
	{0x47, 0x07},
	{0x48, 0x08},
	{0xb2, 0x7f},
	{0xb3, 0x7b},
	{0xbd, 0x08},
	{0xd2, 0x57},
	{0xd3, 0x10},
	{0xd4, 0x08},
	{0xd5, 0x08},
	{0xd6, 0x06},
	{0xb1, 0x00},
	{0xb4, 0x00},
	{0xb7, 0x0a},
	{0xbc, 0x44},
	{0xbf, 0x48},
	{0xc1, 0x10},
	{0xc3, 0x24},
	{0xc8, 0x03},
	{0xc9, 0xf8},
	{0xe1, 0x33},
	{0xe2, 0xbb},
	{0x51, 0x0c},
	{0x52, 0x0a},
	{0x57, 0x8c},
	{0x59, 0x09},
	{0x5a, 0x08},
	{0x5e, 0x10},
	{0x60, 0x02},
	{0x6d, 0x5c},
	{0x76, 0x16},
	{0x7c, 0x11},
	{0x90, 0x28},
	{0x91, 0x16},
	{0x92, 0x1c},
	{0x93, 0x24},
	{0x95, 0x48},
	{0x9c, 0x06},
	{0xca, 0x0c},
	{0xce, 0x0d},
	{0xfd, 0x01},
	{0xc0, 0x00},
	{0xdd, 0x18},
	{0xde, 0x19},
	{0xdf, 0x32},
	{0xe0, 0x70},
	{0xfd, 0x01},
	{0xc2, 0x05},
	{0xd7, 0x88},
	{0xd8, 0x77},
	{0xd9, 0x00},
	{0xfd, 0x07},
	{0x00, 0xf8},
	{0x01, 0x2b},
	{0x05, 0x40},
	{0x08, 0x06},
	{0x09, 0x11},
	{0x28, 0x6f},
	{0x2a, 0x20},
	{0x2b, 0x05},
	{0x5e, 0x10},
	{0x52, 0x00},
	{0x53, 0x7c},
	{0x54, 0x00},
	{0x55, 0x7c},
	{0x56, 0x00},
	{0x57, 0x7c},
	{0x58, 0x00},
	{0x59, 0x7c},
	{0xfd, 0x02},
	{0x9a, 0x30},
	{0xa8, 0x02},
	{0xfd, 0x02},
	{0xa1, 0x01},
	{0xa2, 0x09},
	{0xa3, 0x9c},
	{0xa5, 0x00},
	{0xa6, 0x0c},
	{0xa7, 0xd0},
	{0xfd, 0x00},
	{0x24, 0x01},
	{0xc0, 0x16},
	{0xc1, 0x08},
	{0xc2, 0x30},
	{0x8e, 0x0c},
	{0x8f, 0xd0},
	{0x90, 0x09},
	{0x91, 0x9c},
	{0xfd, 0x05},
	{0x04, 0x40},
	{0x07, 0x00},
	{0x0d, 0x01},
	{0x0f, 0x01},
	{0x10, 0x00},
	{0x11, 0x00},
	{0x12, 0x0c},
	{0x13, 0xcf},
	{0x14, 0x00},
	{0x15, 0x00},
	{0xfd, 0x00},
	{0x20, 0x0f},
	{0xe7, 0x03},
	{0xe7, 0x00}
};

static const struct ov08d10_reg lane_2_mode_3264x2448[] = {
	/* 3264x2448 resolution */
	{0xfd, 0x01},
	{0x12, 0x00},
	{0x03, 0x12},
	{0x04, 0x58},
	{0x07, 0x05},
	{0x21, 0x02},
	{0x24, 0x30},
	{0x33, 0x03},
	{0x01, 0x03},
	{0x19, 0x10},
	{0x42, 0x55},
	{0x43, 0x00},
	{0x47, 0x07},
	{0x48, 0x08},
	{0xb2, 0x7f},
	{0xb3, 0x7b},
	{0xbd, 0x08},
	{0xd2, 0x57},
	{0xd3, 0x10},
	{0xd4, 0x08},
	{0xd5, 0x08},
	{0xd6, 0x06},
	{0xb1, 0x00},
	{0xb4, 0x00},
	{0xb7, 0x0a},
	{0xbc, 0x44},
	{0xbf, 0x48},
	{0xc1, 0x10},
	{0xc3, 0x24},
	{0xc8, 0x03},
	{0xc9, 0xf8},
	{0xe1, 0x33},
	{0xe2, 0xbb},
	{0x51, 0x0c},
	{0x52, 0x0a},
	{0x57, 0x8c},
	{0x59, 0x09},
	{0x5a, 0x08},
	{0x5e, 0x10},
	{0x60, 0x02},
	{0x6d, 0x5c},
	{0x76, 0x16},
	{0x7c, 0x11},
	{0x90, 0x28},
	{0x91, 0x16},
	{0x92, 0x1c},
	{0x93, 0x24},
	{0x95, 0x48},
	{0x9c, 0x06},
	{0xca, 0x0c},
	{0xce, 0x0d},
	{0xfd, 0x01},
	{0xc0, 0x00},
	{0xdd, 0x18},
	{0xde, 0x19},
	{0xdf, 0x32},
	{0xe0, 0x70},
	{0xfd, 0x01},
	{0xc2, 0x05},
	{0xd7, 0x88},
	{0xd8, 0x77},
	{0xd9, 0x00},
	{0xfd, 0x07},
	{0x00, 0xf8},
	{0x01, 0x2b},
	{0x05, 0x40},
	{0x08, 0x06},
	{0x09, 0x11},
	{0x28, 0x6f},
	{0x2a, 0x20},
	{0x2b, 0x05},
	{0x5e, 0x10},
	{0x52, 0x00},
	{0x53, 0x7c},
	{0x54, 0x00},
	{0x55, 0x7c},
	{0x56, 0x00},
	{0x57, 0x7c},
	{0x58, 0x00},
	{0x59, 0x7c},
	{0xfd, 0x02},
	{0x9a, 0x30},
	{0xa8, 0x02},
	{0xfd, 0x02},
	{0xa1, 0x09},
	{0xa2, 0x09},
	{0xa3, 0x90},
	{0xa5, 0x08},
	{0xa6, 0x0c},
	{0xa7, 0xc0},
	{0xfd, 0x00},
	{0x24, 0x01},
	{0xc0, 0x16},
	{0xc1, 0x08},
	{0xc2, 0x30},
	{0x8e, 0x0c},
	{0x8f, 0xc0},
	{0x90, 0x09},
	{0x91, 0x90},
	{0xfd, 0x05},
	{0x04, 0x40},
	{0x07, 0x00},
	{0x0d, 0x01},
	{0x0f, 0x01},
	{0x10, 0x00},
	{0x11, 0x00},
	{0x12, 0x0c},
	{0x13, 0xcf},
	{0x14, 0x00},
	{0x15, 0x00},
	{0xfd, 0x00},
	{0x20, 0x0f},
	{0xe7, 0x03},
	{0xe7, 0x00}
};

static const struct ov08d10_reg lane_2_mode_1632x1224[] = {
	/* 1640x1232 resolution */
	{0xfd, 0x01},
	{0x1a, 0x0a},
	{0x1b, 0x08},
	{0x2a, 0x01},
	{0x2b, 0x9a},
	{0xfd, 0x01},
	{0x12, 0x00},
	{0x03, 0x05},
	{0x04, 0xe2},
	{0x07, 0x05},
	{0x21, 0x02},
	{0x24, 0x30},
	{0x33, 0x03},
	{0x31, 0x06},
	{0x33, 0x03},
	{0x01, 0x03},
	{0x19, 0x10},
	{0x42, 0x55},
	{0x43, 0x00},
	{0x47, 0x07},
	{0x48, 0x08},
	{0xb2, 0x7f},
	{0xb3, 0x7b},
	{0xbd, 0x08},
	{0xd2, 0x57},
	{0xd3, 0x10},
	{0xd4, 0x08},
	{0xd5, 0x08},
	{0xd6, 0x06},
	{0xb1, 0x00},
	{0xb4, 0x00},
	{0xb7, 0x0a},
	{0xbc, 0x44},
	{0xbf, 0x48},
	{0xc1, 0x10},
	{0xc3, 0x24},
	{0xc8, 0x03},
	{0xc9, 0xf8},
	{0xe1, 0x33},
	{0xe2, 0xbb},
	{0x51, 0x0c},
	{0x52, 0x0a},
	{0x57, 0x8c},
	{0x59, 0x09},
	{0x5a, 0x08},
	{0x5e, 0x10},
	{0x60, 0x02},
	{0x6d, 0x5c},
	{0x76, 0x16},
	{0x7c, 0x1a},
	{0x90, 0x28},
	{0x91, 0x16},
	{0x92, 0x1c},
	{0x93, 0x24},
	{0x95, 0x48},
	{0x9c, 0x06},
	{0xca, 0x0c},
	{0xce, 0x0d},
	{0xfd, 0x01},
	{0xc0, 0x00},
	{0xdd, 0x18},
	{0xde, 0x19},
	{0xdf, 0x32},
	{0xe0, 0x70},
	{0xfd, 0x01},
	{0xc2, 0x05},
	{0xd7, 0x88},
	{0xd8, 0x77},
	{0xd9, 0x00},
	{0xfd, 0x07},
	{0x00, 0xf8},
	{0x01, 0x2b},
	{0x05, 0x40},
	{0x08, 0x03},
	{0x09, 0x08},
	{0x28, 0x6f},
	{0x2a, 0x20},
	{0x2b, 0x05},
	{0x2c, 0x01},
	{0x50, 0x02},
	{0x51, 0x03},
	{0x5e, 0x00},
	{0x52, 0x00},
	{0x53, 0x7c},
	{0x54, 0x00},
	{0x55, 0x7c},
	{0x56, 0x00},
	{0x57, 0x7c},
	{0x58, 0x00},
	{0x59, 0x7c},
	{0xfd, 0x02},
	{0x9a, 0x30},
	{0xa8, 0x02},
	{0xfd, 0x02},
	{0xa9, 0x04},
	{0xaa, 0xd0},
	{0xab, 0x06},
	{0xac, 0x68},
	{0xa1, 0x09},
	{0xa2, 0x04},
	{0xa3, 0xc8},
	{0xa5, 0x04},
	{0xa6, 0x06},
	{0xa7, 0x60},
	{0xfd, 0x05},
	{0x06, 0x80},
	{0x18, 0x06},
	{0x19, 0x68},
	{0xfd, 0x00},
	{0x24, 0x01},
	{0xc0, 0x16},
	{0xc1, 0x08},
	{0xc2, 0x30},
	{0x8e, 0x06},
	{0x8f, 0x60},
	{0x90, 0x04},
	{0x91, 0xc8},
	{0x93, 0x0e},
	{0x94, 0x77},
	{0x95, 0x77},
	{0x96, 0x10},
	{0x98, 0x88},
	{0x9c, 0x1a},
	{0xfd, 0x05},
	{0x04, 0x40},
	{0x07, 0x99},
	{0x0d, 0x03},
	{0x0f, 0x03},
	{0x10, 0x00},
	{0x11, 0x00},
	{0x12, 0x0c},
	{0x13, 0xcf},
	{0x14, 0x00},
	{0x15, 0x00},
	{0xfd, 0x00},
	{0x20, 0x0f},
	{0xe7, 0x03},
	{0xe7, 0x00},
};

static const char * const ov08d10_test_pattern_menu[] = {
	"Disabled",
	"Standard Color Bar",
};

struct ov08d10 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;

	struct clk		*xvclk;

	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *exposure;

	/* Current mode */
	const struct ov08d10_mode *cur_mode;

	/* To serialize asynchronus callbacks */
	struct mutex mutex;

	/* Streaming on/off */
	bool streaming;

	/* lanes index */
	u8 nlanes;

	const struct ov08d10_lane_cfg *priv_lane;
	u8 modes_size;
};

struct ov08d10_lane_cfg {
	const s64 link_freq_menu[2];
	const struct ov08d10_link_freq_config link_freq_configs[2];
	const struct ov08d10_mode sp_modes[3];
};

static const struct ov08d10_lane_cfg lane_cfg_2 = {
	{
		720000000,
		360000000,
	},
	{{
		.reg_list = {
			.num_of_regs =
				ARRAY_SIZE(mipi_data_rate_720mbps),
			.regs = mipi_data_rate_720mbps,
		}
	},
	{
		.reg_list = {
			.num_of_regs =
				ARRAY_SIZE(mipi_data_rate_360mbps),
			.regs = mipi_data_rate_360mbps,
		}
	}},
	{{
		.width = 3280,
		.height = 2460,
		.hts = 1840,
		.vts_def = 2504,
		.vts_min = 2504,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(lane_2_mode_3280x2460),
			.regs = lane_2_mode_3280x2460,
		},
		.link_freq_index = 0,
		.data_lanes = 2,
	},
	{
		.width = 3264,
		.height = 2448,
		.hts = 1840,
		.vts_def = 2504,
		.vts_min = 2504,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(lane_2_mode_3264x2448),
			.regs = lane_2_mode_3264x2448,
		},
		.link_freq_index = 0,
		.data_lanes = 2,
	},
	{
		.width = 1632,
		.height = 1224,
		.hts = 1912,
		.vts_def = 3736,
		.vts_min = 3736,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(lane_2_mode_1632x1224),
			.regs = lane_2_mode_1632x1224,
		},
		.link_freq_index = 1,
		.data_lanes = 2,
	}}
};

static u32 ov08d10_get_format_code(struct ov08d10 *ov08d10)
{
	static const u32 codes[2][2] = {
		{ MEDIA_BUS_FMT_SGRBG10_1X10, MEDIA_BUS_FMT_SRGGB10_1X10},
		{ MEDIA_BUS_FMT_SBGGR10_1X10, MEDIA_BUS_FMT_SGBRG10_1X10},
	};

	return codes[ov08d10->vflip->val][ov08d10->hflip->val];
}

static unsigned int ov08d10_modes_num(const struct ov08d10 *ov08d10)
{
	unsigned int i, count = 0;

	for (i = 0; i < ARRAY_SIZE(ov08d10->priv_lane->sp_modes); i++) {
		if (ov08d10->priv_lane->sp_modes[i].width == 0)
			break;
		count++;
	}

	return count;
}

static u64 to_rate(const s64 *link_freq_menu,
		   u32 f_index, u8 nlanes)
{
	u64 pixel_rate = link_freq_menu[f_index] * 2 * nlanes;

	do_div(pixel_rate, OV08D10_RGB_DEPTH);

	return pixel_rate;
}

static u64 to_pixels_per_line(const s64 *link_freq_menu, u32 hts,
			      u32 f_index, u8 nlanes)
{
	u64 ppl = hts * to_rate(link_freq_menu, f_index, nlanes);

	do_div(ppl, OV08D10_SCLK);

	return ppl;
}

static int ov08d10_write_reg_list(struct ov08d10 *ov08d10,
				  const struct ov08d10_reg_list *r_list)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08d10->sd);
	unsigned int i;
	int ret;

	for (i = 0; i < r_list->num_of_regs; i++) {
		ret = i2c_smbus_write_byte_data(client, r_list->regs[i].address,
						r_list->regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "failed to write reg 0x%2.2x. error = %d",
					    r_list->regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static int ov08d10_update_analog_gain(struct ov08d10 *ov08d10, u32 a_gain)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08d10->sd);
	u8 val;
	int ret;

	val = ((a_gain >> 3) & 0xFF);
	/* CIS control registers */
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x01);
	if (ret < 0)
		return ret;

	/* update AGAIN */
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_ANALOG_GAIN, val);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(client,
					 OV08D10_REG_GLOBAL_EFFECTIVE, 0x01);
}

static int ov08d10_update_digital_gain(struct ov08d10 *ov08d10, u32 d_gain)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08d10->sd);
	u8 val;
	int ret;

	d_gain = (d_gain >> 1);
	/* CIS control registers */
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x01);
	if (ret < 0)
		return ret;

	val = ((d_gain >> 8) & 0x3F);
	/* update DGAIN */
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_MWB_DGAIN_C, val);
	if (ret < 0)
		return ret;

	val = d_gain & 0xFF;
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_MWB_DGAIN_F, val);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(client,
					 OV08D10_REG_GLOBAL_EFFECTIVE, 0x01);
}

static int ov08d10_set_exposure(struct ov08d10 *ov08d10, u32 exposure)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08d10->sd);
	u8 val;
	u8 hts_h, hts_l;
	u32 hts, cur_vts, exp_cal;
	int ret;

	cur_vts = ov08d10->cur_mode->vts_def;
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x01);
	if (ret < 0)
		return ret;

	hts_h = i2c_smbus_read_byte_data(client, 0x37);
	hts_l = i2c_smbus_read_byte_data(client, 0x38);
	hts = ((hts_h << 8) | (hts_l));
	exp_cal = 66 * OV08D10_ROWCLK / hts;
	exposure = exposure * exp_cal / (cur_vts - OV08D10_EXPOSURE_MAX_MARGIN);
	/* CIS control registers */
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x01);
	if (ret < 0)
		return ret;

	/* update exposure */
	val = ((exposure >> 16) & 0xFF);
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_EXPOSURE_H, val);
	if (ret < 0)
		return ret;

	val = ((exposure >> 8) & 0xFF);
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_EXPOSURE_M, val);
	if (ret < 0)
		return ret;

	val = exposure & 0xFF;
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_EXPOSURE_L, val);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(client,
					 OV08D10_REG_GLOBAL_EFFECTIVE, 0x01);
}

static int ov08d10_set_vblank(struct ov08d10 *ov08d10, u32 vblank)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08d10->sd);
	u8 val;
	int ret;

	/* CIS control registers */
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x01);
	if (ret < 0)
		return ret;

	val = ((vblank >> 8) & 0xFF);
	/* update vblank */
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_VTS_H, val);
	if (ret < 0)
		return ret;

	val = vblank & 0xFF;
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_VTS_L, val);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(client,
					 OV08D10_REG_GLOBAL_EFFECTIVE, 0x01);
}

static int ov08d10_test_pattern(struct ov08d10 *ov08d10, u32 pattern)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08d10->sd);
	u8 val;
	int ret;

	if (pattern)
		val = OV08D10_TEST_PATTERN_ENABLE;
	else
		val = OV08D10_TEST_PATTERN_DISABLE;

	/* CIS control registers */
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x01);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(client,
					OV08D10_REG_TEST_PATTERN, val);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(client,
					 OV08D10_REG_GLOBAL_EFFECTIVE, 0x01);
}

static int ov08d10_set_ctrl_flip(struct ov08d10 *ov08d10, u32 ctrl_val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08d10->sd);
	u8 val;
	int ret;

	/* System control registers */
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x01);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(client, OV08D10_REG_FLIP_OPT);
	if (ret < 0)
		return ret;

	val = ret | (ctrl_val & OV08D10_REG_FLIP_MASK);

	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x01);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_FLIP_OPT, val);

	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(client,
					 OV08D10_REG_GLOBAL_EFFECTIVE, 0x01);
}

static int ov08d10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov08d10 *ov08d10 = container_of(ctrl->handler,
					     struct ov08d10, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ov08d10->sd);
	s64 exposure_max;
	int ret;

	/* Propagate change of current control to all related controls */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = ov08d10->cur_mode->height + ctrl->val -
			       OV08D10_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(ov08d10->exposure,
					 ov08d10->exposure->minimum,
					 exposure_max, ov08d10->exposure->step,
					 exposure_max);
	}

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov08d10_update_analog_gain(ov08d10, ctrl->val);
		break;

	case V4L2_CID_DIGITAL_GAIN:
		ret = ov08d10_update_digital_gain(ov08d10, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE:
		ret = ov08d10_set_exposure(ov08d10, ctrl->val);
		break;

	case V4L2_CID_VBLANK:
		ret = ov08d10_set_vblank(ov08d10, ctrl->val);
		break;

	case V4L2_CID_TEST_PATTERN:
		ret = ov08d10_test_pattern(ov08d10, ctrl->val);
		break;

	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		ret = ov08d10_set_ctrl_flip(ov08d10,
					    ov08d10->hflip->val |
					    ov08d10->vflip->val << 1);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov08d10_ctrl_ops = {
	.s_ctrl = ov08d10_set_ctrl,
};

static int ov08d10_init_controls(struct ov08d10 *ov08d10)
{
	struct v4l2_ctrl_handler *ctrl_hdlr;
	u8 link_freq_size;
	s64 exposure_max;
	s64 vblank_def;
	s64 vblank_min;
	s64 h_blank;
	s64 pixel_rate_max;
	const struct ov08d10_mode *mode;
	int ret;

	ctrl_hdlr = &ov08d10->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 8);
	if (ret)
		return ret;

	ctrl_hdlr->lock = &ov08d10->mutex;
	link_freq_size = ARRAY_SIZE(ov08d10->priv_lane->link_freq_menu);
	ov08d10->link_freq =
		v4l2_ctrl_new_int_menu(ctrl_hdlr, &ov08d10_ctrl_ops,
				       V4L2_CID_LINK_FREQ,
				       link_freq_size - 1,
				       0,
				       ov08d10->priv_lane->link_freq_menu);
	if (ov08d10->link_freq)
		ov08d10->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate_max = to_rate(ov08d10->priv_lane->link_freq_menu, 0,
				 ov08d10->cur_mode->data_lanes);
	ov08d10->pixel_rate =
		v4l2_ctrl_new_std(ctrl_hdlr, &ov08d10_ctrl_ops,
				  V4L2_CID_PIXEL_RATE, 0, pixel_rate_max, 1,
				  pixel_rate_max);

	mode = ov08d10->cur_mode;
	vblank_def = mode->vts_def - mode->height;
	vblank_min = mode->vts_min - mode->height;
	ov08d10->vblank =
		v4l2_ctrl_new_std(ctrl_hdlr, &ov08d10_ctrl_ops,
				  V4L2_CID_VBLANK, vblank_min,
				  OV08D10_VTS_MAX - mode->height, 1,
				  vblank_def);

	h_blank = to_pixels_per_line(ov08d10->priv_lane->link_freq_menu,
				     mode->hts, mode->link_freq_index,
				     mode->data_lanes) -
				     mode->width;
	ov08d10->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov08d10_ctrl_ops,
					    V4L2_CID_HBLANK, h_blank, h_blank,
					    1, h_blank);
	if (ov08d10->hblank)
		ov08d10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &ov08d10_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV08D10_ANAL_GAIN_MIN, OV08D10_ANAL_GAIN_MAX,
			  OV08D10_ANAL_GAIN_STEP, OV08D10_ANAL_GAIN_MIN);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov08d10_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV08D10_DGTL_GAIN_MIN, OV08D10_DGTL_GAIN_MAX,
			  OV08D10_DGTL_GAIN_STEP, OV08D10_DGTL_GAIN_DEFAULT);

	exposure_max = mode->vts_def - OV08D10_EXPOSURE_MAX_MARGIN;
	ov08d10->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ov08d10_ctrl_ops,
					      V4L2_CID_EXPOSURE,
					      OV08D10_EXPOSURE_MIN,
					      exposure_max,
					      OV08D10_EXPOSURE_STEP,
					      exposure_max);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov08d10_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov08d10_test_pattern_menu) - 1,
				     0, 0, ov08d10_test_pattern_menu);

	ov08d10->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &ov08d10_ctrl_ops,
					   V4L2_CID_HFLIP, 0, 1, 1, 0);
	ov08d10->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &ov08d10_ctrl_ops,
					   V4L2_CID_VFLIP, 0, 1, 1, 0);
	if (ctrl_hdlr->error)
		return ctrl_hdlr->error;

	ov08d10->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static void ov08d10_update_pad_format(struct ov08d10 *ov08d10,
				      const struct ov08d10_mode *mode,
				      struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = ov08d10_get_format_code(ov08d10);
	fmt->field = V4L2_FIELD_NONE;
}

static int ov08d10_start_streaming(struct ov08d10 *ov08d10)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08d10->sd);
	const struct ov08d10_reg_list *reg_list;
	int link_freq_index, ret;

	link_freq_index = ov08d10->cur_mode->link_freq_index;
	reg_list =
	    &ov08d10->priv_lane->link_freq_configs[link_freq_index].reg_list;

	/* soft reset */
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x00);
	if (ret < 0) {
		dev_err(&client->dev, "failed to reset sensor");
		return ret;
	}
	ret = i2c_smbus_write_byte_data(client, 0x20, 0x0e);
	if (ret < 0) {
		dev_err(&client->dev, "failed to reset sensor");
		return ret;
	}
	usleep_range(3000, 4000);
	ret = i2c_smbus_write_byte_data(client, 0x20, 0x0b);
	if (ret < 0) {
		dev_err(&client->dev, "failed to reset sensor");
		return ret;
	}

	/* update sensor setting */
	ret = ov08d10_write_reg_list(ov08d10, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set plls");
		return ret;
	}

	reg_list = &ov08d10->cur_mode->reg_list;
	ret = ov08d10_write_reg_list(ov08d10, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set mode");
		return ret;
	}

	ret = __v4l2_ctrl_handler_setup(ov08d10->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x00);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_MODE_SELECT,
					OV08D10_MODE_STREAMING);
	if (ret < 0)
		return ret;

	return i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x01);
}

static void ov08d10_stop_streaming(struct ov08d10 *ov08d10)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08d10->sd);
	int ret;

	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x00);
	if (ret < 0) {
		dev_err(&client->dev, "failed to stop streaming");
		return;
	}
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_MODE_SELECT,
					OV08D10_MODE_STANDBY);
	if (ret < 0) {
		dev_err(&client->dev, "failed to stop streaming");
		return;
	}

	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x01);
	if (ret < 0) {
		dev_err(&client->dev, "failed to stop streaming");
		return;
	}
}

static int ov08d10_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov08d10 *ov08d10 = to_ov08d10(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	if (ov08d10->streaming == enable)
		return 0;

	mutex_lock(&ov08d10->mutex);
	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0) {
			mutex_unlock(&ov08d10->mutex);
			return ret;
		}

		ret = ov08d10_start_streaming(ov08d10);
		if (ret) {
			enable = 0;
			ov08d10_stop_streaming(ov08d10);
			pm_runtime_put(&client->dev);
		}
	} else {
		ov08d10_stop_streaming(ov08d10);
		pm_runtime_put(&client->dev);
	}

	ov08d10->streaming = enable;

	/* vflip and hflip cannot change during streaming */
	__v4l2_ctrl_grab(ov08d10->vflip, enable);
	__v4l2_ctrl_grab(ov08d10->hflip, enable);

	mutex_unlock(&ov08d10->mutex);

	return ret;
}

static int __maybe_unused ov08d10_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov08d10 *ov08d10 = to_ov08d10(sd);

	mutex_lock(&ov08d10->mutex);
	if (ov08d10->streaming)
		ov08d10_stop_streaming(ov08d10);

	mutex_unlock(&ov08d10->mutex);

	return 0;
}

static int __maybe_unused ov08d10_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov08d10 *ov08d10 = to_ov08d10(sd);
	int ret;

	mutex_lock(&ov08d10->mutex);

	if (ov08d10->streaming) {
		ret = ov08d10_start_streaming(ov08d10);
		if (ret) {
			ov08d10->streaming = false;
			ov08d10_stop_streaming(ov08d10);
			mutex_unlock(&ov08d10->mutex);
			return ret;
		}
	}

	mutex_unlock(&ov08d10->mutex);

	return 0;
}

static int ov08d10_set_format(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *fmt)
{
	struct ov08d10 *ov08d10 = to_ov08d10(sd);
	const struct ov08d10_mode *mode;
	s32 vblank_def, h_blank;
	s64 pixel_rate;

	mode = v4l2_find_nearest_size(ov08d10->priv_lane->sp_modes,
				      ov08d10->modes_size,
				      width, height, fmt->format.width,
				      fmt->format.height);

	mutex_lock(&ov08d10->mutex);
	ov08d10_update_pad_format(ov08d10, mode, &fmt->format);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_get_try_format(sd, sd_state, fmt->pad) =
								fmt->format;
	} else {
		ov08d10->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(ov08d10->link_freq, mode->link_freq_index);
		pixel_rate = to_rate(ov08d10->priv_lane->link_freq_menu,
				     mode->link_freq_index,
				     ov08d10->cur_mode->data_lanes);
		__v4l2_ctrl_s_ctrl_int64(ov08d10->pixel_rate, pixel_rate);

		/* Update limits and set FPS to default */
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov08d10->vblank,
					 mode->vts_min - mode->height,
					 OV08D10_VTS_MAX - mode->height, 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(ov08d10->vblank, vblank_def);
		h_blank = to_pixels_per_line(ov08d10->priv_lane->link_freq_menu,
					     mode->hts,
					     mode->link_freq_index,
					     ov08d10->cur_mode->data_lanes)
					     - mode->width;
		__v4l2_ctrl_modify_range(ov08d10->hblank, h_blank, h_blank, 1,
					 h_blank);
	}

	mutex_unlock(&ov08d10->mutex);

	return 0;
}

static int ov08d10_get_format(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *fmt)
{
	struct ov08d10 *ov08d10 = to_ov08d10(sd);

	mutex_lock(&ov08d10->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		fmt->format = *v4l2_subdev_get_try_format(&ov08d10->sd,
							  sd_state,
							  fmt->pad);
	else
		ov08d10_update_pad_format(ov08d10, ov08d10->cur_mode,
					  &fmt->format);

	mutex_unlock(&ov08d10->mutex);

	return 0;
}

static int ov08d10_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct ov08d10 *ov08d10 = to_ov08d10(sd);

	if (code->index > 0)
		return -EINVAL;

	mutex_lock(&ov08d10->mutex);
	code->code = ov08d10_get_format_code(ov08d10);
	mutex_unlock(&ov08d10->mutex);

	return 0;
}

static int ov08d10_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	struct ov08d10 *ov08d10 = to_ov08d10(sd);

	if (fse->index >= ov08d10->modes_size)
		return -EINVAL;

	mutex_lock(&ov08d10->mutex);
	if (fse->code != ov08d10_get_format_code(ov08d10)) {
		mutex_unlock(&ov08d10->mutex);
		return -EINVAL;
	}
	mutex_unlock(&ov08d10->mutex);

	fse->min_width = ov08d10->priv_lane->sp_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = ov08d10->priv_lane->sp_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int ov08d10_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct ov08d10 *ov08d10 = to_ov08d10(sd);

	mutex_lock(&ov08d10->mutex);
	ov08d10_update_pad_format(ov08d10, &ov08d10->priv_lane->sp_modes[0],
				  v4l2_subdev_get_try_format(sd, fh->state, 0));
	mutex_unlock(&ov08d10->mutex);

	return 0;
}

static const struct v4l2_subdev_video_ops ov08d10_video_ops = {
	.s_stream = ov08d10_set_stream,
};

static const struct v4l2_subdev_pad_ops ov08d10_pad_ops = {
	.set_fmt = ov08d10_set_format,
	.get_fmt = ov08d10_get_format,
	.enum_mbus_code = ov08d10_enum_mbus_code,
	.enum_frame_size = ov08d10_enum_frame_size,
};

static const struct v4l2_subdev_ops ov08d10_subdev_ops = {
	.video = &ov08d10_video_ops,
	.pad = &ov08d10_pad_ops,
};

static const struct v4l2_subdev_internal_ops ov08d10_internal_ops = {
	.open = ov08d10_open,
};

static int ov08d10_identify_module(struct ov08d10 *ov08d10)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov08d10->sd);
	u32 val;
	u16 chip_id;
	int ret;

	/* System control registers */
	ret = i2c_smbus_write_byte_data(client, OV08D10_REG_PAGE, 0x00);
	if (ret < 0)
		return ret;

	/* Validate the chip ID */
	ret = i2c_smbus_read_byte_data(client, OV08D10_REG_CHIP_ID_0);
	if (ret < 0)
		return ret;

	val = ret << 8;

	ret = i2c_smbus_read_byte_data(client, OV08D10_REG_CHIP_ID_1);
	if (ret < 0)
		return ret;

	chip_id = val | ret;

	if ((chip_id & OV08D10_ID_MASK) != OV08D10_CHIP_ID) {
		dev_err(&client->dev, "unexpected sensor id(0x%04x)\n",
			chip_id);
		return -EINVAL;
	}

	return 0;
}

static int ov08d10_get_hwcfg(struct ov08d10 *ov08d10, struct device *dev)
{
	struct fwnode_handle *ep;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY
	};
	u32 xvclk_rate;
	unsigned int i, j;
	int ret;

	if (!fwnode)
		return -ENXIO;

	ret = fwnode_property_read_u32(fwnode, "clock-frequency", &xvclk_rate);
	if (ret)
		return ret;

	if (xvclk_rate != OV08D10_XVCLK_19_2)
		dev_warn(dev, "external clock rate %u is unsupported",
			 xvclk_rate);

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep)
		return -ENXIO;

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return ret;

	/* Get number of data lanes */
	if (bus_cfg.bus.mipi_csi2.num_data_lanes != 2) {
		dev_err(dev, "number of CSI2 data lanes %d is not supported",
			bus_cfg.bus.mipi_csi2.num_data_lanes);
		ret = -EINVAL;
		goto check_hwcfg_error;
	}

	dev_dbg(dev, "Using %u data lanes\n", ov08d10->cur_mode->data_lanes);

	ov08d10->priv_lane = &lane_cfg_2;
	ov08d10->modes_size = ov08d10_modes_num(ov08d10);

	if (!bus_cfg.nr_of_link_frequencies) {
		dev_err(dev, "no link frequencies defined");
		ret = -EINVAL;
		goto check_hwcfg_error;
	}

	for (i = 0; i < ARRAY_SIZE(ov08d10->priv_lane->link_freq_menu); i++) {
		for (j = 0; j < bus_cfg.nr_of_link_frequencies; j++) {
			if (ov08d10->priv_lane->link_freq_menu[i] ==
			    bus_cfg.link_frequencies[j])
				break;
		}

		if (j == bus_cfg.nr_of_link_frequencies) {
			dev_err(dev, "no link frequency %lld supported",
				ov08d10->priv_lane->link_freq_menu[i]);
			ret = -EINVAL;
			goto check_hwcfg_error;
		}
	}

check_hwcfg_error:
	v4l2_fwnode_endpoint_free(&bus_cfg);

	return ret;
}

static int ov08d10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ov08d10 *ov08d10 = to_ov08d10(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);
	pm_runtime_disable(&client->dev);
	mutex_destroy(&ov08d10->mutex);

	return 0;
}

static int ov08d10_probe(struct i2c_client *client)
{
	struct ov08d10 *ov08d10;
	int ret;

	ov08d10 = devm_kzalloc(&client->dev, sizeof(*ov08d10), GFP_KERNEL);
	if (!ov08d10)
		return -ENOMEM;

	ret = ov08d10_get_hwcfg(ov08d10, &client->dev);
	if (ret) {
		dev_err(&client->dev, "failed to get HW configuration: %d",
			ret);
		return ret;
	}

	v4l2_i2c_subdev_init(&ov08d10->sd, client, &ov08d10_subdev_ops);

	ret = ov08d10_identify_module(ov08d10);
	if (ret) {
		dev_err(&client->dev, "failed to find sensor: %d", ret);
		return ret;
	}

	mutex_init(&ov08d10->mutex);
	ov08d10->cur_mode = &ov08d10->priv_lane->sp_modes[0];
	ret = ov08d10_init_controls(ov08d10);
	if (ret) {
		dev_err(&client->dev, "failed to init controls: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ov08d10->sd.internal_ops = &ov08d10_internal_ops;
	ov08d10->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	ov08d10->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov08d10->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&ov08d10->sd.entity, 1, &ov08d10->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init entity pads: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ret = v4l2_async_register_subdev_sensor(&ov08d10->sd);
	if (ret < 0) {
		dev_err(&client->dev, "failed to register V4L2 subdev: %d",
			ret);
		goto probe_error_media_entity_cleanup;
	}

	/*
	 * Device is already turned on by i2c-core with ACPI domain PM.
	 * Enable runtime PM and turn off the device.
	 */
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	return 0;

probe_error_media_entity_cleanup:
	media_entity_cleanup(&ov08d10->sd.entity);

probe_error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(ov08d10->sd.ctrl_handler);
	mutex_destroy(&ov08d10->mutex);

	return ret;
}

static const struct dev_pm_ops ov08d10_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ov08d10_suspend, ov08d10_resume)
};

#ifdef CONFIG_ACPI
static const struct acpi_device_id ov08d10_acpi_ids[] = {
	{ "OVTI08D1" },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(acpi, ov08d10_acpi_ids);
#endif

static struct i2c_driver ov08d10_i2c_driver = {
	.driver = {
		.name = "ov08d10",
		.pm = &ov08d10_pm_ops,
		.acpi_match_table = ACPI_PTR(ov08d10_acpi_ids),
	},
	.probe_new = ov08d10_probe,
	.remove = ov08d10_remove,
};

module_i2c_driver(ov08d10_i2c_driver);

MODULE_AUTHOR("Su, Jimmy <jimmy.su@intel.com>");
MODULE_DESCRIPTION("OmniVision ov08d10 sensor driver");
MODULE_LICENSE("GPL v2");
