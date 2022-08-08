// SPDX-License-Identifier: GPL-2.0
/*
 * imx274.c - IMX274 CMOS Image Sensor driver
 *
 * Copyright (C) 2017, Leopard Imaging, Inc.
 *
 * Leon Luo <leonl@leopardimaging.com>
 * Edwin Zou <edwinz@leopardimaging.com>
 * Luca Ceresoli <luca@lucaceresoli.net>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/v4l2-mediabus.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>

/*
 * See "SHR, SVR Setting" in datasheet
 */
#define IMX274_DEFAULT_FRAME_LENGTH		(4550)
#define IMX274_MAX_FRAME_LENGTH			(0x000fffff)

/*
 * See "Frame Rate Adjustment" in datasheet
 */
#define IMX274_PIXCLK_CONST1			(72000000)
#define IMX274_PIXCLK_CONST2			(1000000)

/*
 * The input gain is shifted by IMX274_GAIN_SHIFT to get
 * decimal number. The real gain is
 * (float)input_gain_value / (1 << IMX274_GAIN_SHIFT)
 */
#define IMX274_GAIN_SHIFT			(8)
#define IMX274_GAIN_SHIFT_MASK			((1 << IMX274_GAIN_SHIFT) - 1)

/*
 * See "Analog Gain" and "Digital Gain" in datasheet
 * min gain is 1X
 * max gain is calculated based on IMX274_GAIN_REG_MAX
 */
#define IMX274_GAIN_REG_MAX			(1957)
#define IMX274_MIN_GAIN				(0x01 << IMX274_GAIN_SHIFT)
#define IMX274_MAX_ANALOG_GAIN			((2048 << IMX274_GAIN_SHIFT)\
					/ (2048 - IMX274_GAIN_REG_MAX))
#define IMX274_MAX_DIGITAL_GAIN			(8)
#define IMX274_DEF_GAIN				(20 << IMX274_GAIN_SHIFT)
#define IMX274_GAIN_CONST			(2048) /* for gain formula */

/*
 * 1 line time in us = (HMAX / 72), minimal is 4 lines
 */
#define IMX274_MIN_EXPOSURE_TIME		(4 * 260 / 72)

#define IMX274_MAX_WIDTH			(3840)
#define IMX274_MAX_HEIGHT			(2160)
#define IMX274_MAX_FRAME_RATE			(120)
#define IMX274_MIN_FRAME_RATE			(5)
#define IMX274_DEF_FRAME_RATE			(60)

/*
 * register SHR is limited to (SVR value + 1) x VMAX value - 4
 */
#define IMX274_SHR_LIMIT_CONST			(4)

/*
 * Min and max sensor reset delay (microseconds)
 */
#define IMX274_RESET_DELAY1			(2000)
#define IMX274_RESET_DELAY2			(2200)

/*
 * shift and mask constants
 */
#define IMX274_SHIFT_8_BITS			(8)
#define IMX274_SHIFT_16_BITS			(16)
#define IMX274_MASK_LSB_2_BITS			(0x03)
#define IMX274_MASK_LSB_3_BITS			(0x07)
#define IMX274_MASK_LSB_4_BITS			(0x0f)
#define IMX274_MASK_LSB_8_BITS			(0x00ff)

#define DRIVER_NAME "IMX274"

/*
 * IMX274 register definitions
 */
#define IMX274_SHR_REG_MSB			0x300D /* SHR */
#define IMX274_SHR_REG_LSB			0x300C /* SHR */
#define IMX274_SVR_REG_MSB			0x300F /* SVR */
#define IMX274_SVR_REG_LSB			0x300E /* SVR */
#define IMX274_HTRIM_EN_REG			0x3037
#define IMX274_HTRIM_START_REG_LSB		0x3038
#define IMX274_HTRIM_START_REG_MSB		0x3039
#define IMX274_HTRIM_END_REG_LSB		0x303A
#define IMX274_HTRIM_END_REG_MSB		0x303B
#define IMX274_VWIDCUTEN_REG			0x30DD
#define IMX274_VWIDCUT_REG_LSB			0x30DE
#define IMX274_VWIDCUT_REG_MSB			0x30DF
#define IMX274_VWINPOS_REG_LSB			0x30E0
#define IMX274_VWINPOS_REG_MSB			0x30E1
#define IMX274_WRITE_VSIZE_REG_LSB		0x3130
#define IMX274_WRITE_VSIZE_REG_MSB		0x3131
#define IMX274_Y_OUT_SIZE_REG_LSB		0x3132
#define IMX274_Y_OUT_SIZE_REG_MSB		0x3133
#define IMX274_VMAX_REG_1			0x30FA /* VMAX, MSB */
#define IMX274_VMAX_REG_2			0x30F9 /* VMAX */
#define IMX274_VMAX_REG_3			0x30F8 /* VMAX, LSB */
#define IMX274_HMAX_REG_MSB			0x30F7 /* HMAX */
#define IMX274_HMAX_REG_LSB			0x30F6 /* HMAX */
#define IMX274_ANALOG_GAIN_ADDR_LSB		0x300A /* ANALOG GAIN LSB */
#define IMX274_ANALOG_GAIN_ADDR_MSB		0x300B /* ANALOG GAIN MSB */
#define IMX274_DIGITAL_GAIN_REG			0x3012 /* Digital Gain */
#define IMX274_VFLIP_REG			0x301A /* VERTICAL FLIP */
#define IMX274_TEST_PATTERN_REG			0x303D /* TEST PATTERN */
#define IMX274_STANDBY_REG			0x3000 /* STANDBY */

#define IMX274_TABLE_WAIT_MS			0
#define IMX274_TABLE_END			1

/* regulator supplies */
static const char * const imx274_supply_names[] = {
	"vddl",  /* IF (1.2V) supply */
	"vdig",  /* Digital Core (1.8V) supply */
	"vana",  /* Analog (2.8V) supply */
};

#define IMX274_NUM_SUPPLIES ARRAY_SIZE(imx274_supply_names)

/*
 * imx274 I2C operation related structure
 */
struct reg_8 {
	u16 addr;
	u8 val;
};

static const struct regmap_config imx274_regmap_config = {
	.reg_bits = 16,
	.val_bits = 8,
	.cache_type = REGCACHE_RBTREE,
};

/*
 * Parameters for each imx274 readout mode.
 *
 * These are the values to configure the sensor in one of the
 * implemented modes.
 *
 * @init_regs: registers to initialize the mode
 * @wbin_ratio: width downscale factor (e.g. 3 for 1280; 3 = 3840/1280)
 * @hbin_ratio: height downscale factor (e.g. 3 for 720; 3 = 2160/720)
 * @min_frame_len: Minimum frame length for each mode (see "Frame Rate
 *                 Adjustment (CSI-2)" in the datasheet)
 * @min_SHR: Minimum SHR register value (see "Shutter Setting (CSI-2)" in the
 *           datasheet)
 * @max_fps: Maximum frames per second
 * @nocpiop: Number of clocks per internal offset period (see "Integration Time
 *           in Each Readout Drive Mode (CSI-2)" in the datasheet)
 */
struct imx274_mode {
	const struct reg_8 *init_regs;
	u8 wbin_ratio;
	u8 hbin_ratio;
	int min_frame_len;
	int min_SHR;
	int max_fps;
	int nocpiop;
};

/*
 * imx274 test pattern related structure
 */
enum {
	TEST_PATTERN_DISABLED = 0,
	TEST_PATTERN_ALL_000H,
	TEST_PATTERN_ALL_FFFH,
	TEST_PATTERN_ALL_555H,
	TEST_PATTERN_ALL_AAAH,
	TEST_PATTERN_VSP_5AH, /* VERTICAL STRIPE PATTERN 555H/AAAH */
	TEST_PATTERN_VSP_A5H, /* VERTICAL STRIPE PATTERN AAAH/555H */
	TEST_PATTERN_VSP_05H, /* VERTICAL STRIPE PATTERN 000H/555H */
	TEST_PATTERN_VSP_50H, /* VERTICAL STRIPE PATTERN 555H/000H */
	TEST_PATTERN_VSP_0FH, /* VERTICAL STRIPE PATTERN 000H/FFFH */
	TEST_PATTERN_VSP_F0H, /* VERTICAL STRIPE PATTERN FFFH/000H */
	TEST_PATTERN_H_COLOR_BARS,
	TEST_PATTERN_V_COLOR_BARS,
};

static const char * const tp_qmenu[] = {
	"Disabled",
	"All 000h Pattern",
	"All FFFh Pattern",
	"All 555h Pattern",
	"All AAAh Pattern",
	"Vertical Stripe (555h / AAAh)",
	"Vertical Stripe (AAAh / 555h)",
	"Vertical Stripe (000h / 555h)",
	"Vertical Stripe (555h / 000h)",
	"Vertical Stripe (000h / FFFh)",
	"Vertical Stripe (FFFh / 000h)",
	"Vertical Color Bars",
	"Horizontal Color Bars",
};

/*
 * All-pixel scan mode (10-bit)
 * imx274 mode1(refer to datasheet) register configuration with
 * 3840x2160 resolution, raw10 data and mipi four lane output
 */
static const struct reg_8 imx274_mode1_3840x2160_raw10[] = {
	{0x3004, 0x01},
	{0x3005, 0x01},
	{0x3006, 0x00},
	{0x3007, 0xa2},

	{0x3018, 0xA2}, /* output XVS, HVS */

	{0x306B, 0x05},
	{0x30E2, 0x01},

	{0x30EE, 0x01},
	{0x3342, 0x0A},
	{0x3343, 0x00},
	{0x3344, 0x16},
	{0x3345, 0x00},
	{0x33A6, 0x01},
	{0x3528, 0x0E},
	{0x3554, 0x1F},
	{0x3555, 0x01},
	{0x3556, 0x01},
	{0x3557, 0x01},
	{0x3558, 0x01},
	{0x3559, 0x00},
	{0x355A, 0x00},
	{0x35BA, 0x0E},
	{0x366A, 0x1B},
	{0x366B, 0x1A},
	{0x366C, 0x19},
	{0x366D, 0x17},
	{0x3A41, 0x08},

	{IMX274_TABLE_END, 0x00}
};

/*
 * Horizontal/vertical 2/2-line binning
 * (Horizontal and vertical weightedbinning, 10-bit)
 * imx274 mode3(refer to datasheet) register configuration with
 * 1920x1080 resolution, raw10 data and mipi four lane output
 */
static const struct reg_8 imx274_mode3_1920x1080_raw10[] = {
	{0x3004, 0x02},
	{0x3005, 0x21},
	{0x3006, 0x00},
	{0x3007, 0xb1},

	{0x3018, 0xA2}, /* output XVS, HVS */

	{0x306B, 0x05},
	{0x30E2, 0x02},

	{0x30EE, 0x01},
	{0x3342, 0x0A},
	{0x3343, 0x00},
	{0x3344, 0x1A},
	{0x3345, 0x00},
	{0x33A6, 0x01},
	{0x3528, 0x0E},
	{0x3554, 0x00},
	{0x3555, 0x01},
	{0x3556, 0x01},
	{0x3557, 0x01},
	{0x3558, 0x01},
	{0x3559, 0x00},
	{0x355A, 0x00},
	{0x35BA, 0x0E},
	{0x366A, 0x1B},
	{0x366B, 0x1A},
	{0x366C, 0x19},
	{0x366D, 0x17},
	{0x3A41, 0x08},

	{IMX274_TABLE_END, 0x00}
};

/*
 * Vertical 2/3 subsampling binning horizontal 3 binning
 * imx274 mode5(refer to datasheet) register configuration with
 * 1280x720 resolution, raw10 data and mipi four lane output
 */
static const struct reg_8 imx274_mode5_1280x720_raw10[] = {
	{0x3004, 0x03},
	{0x3005, 0x31},
	{0x3006, 0x00},
	{0x3007, 0xa9},

	{0x3018, 0xA2}, /* output XVS, HVS */

	{0x306B, 0x05},
	{0x30E2, 0x03},

	{0x30EE, 0x01},
	{0x3342, 0x0A},
	{0x3343, 0x00},
	{0x3344, 0x1B},
	{0x3345, 0x00},
	{0x33A6, 0x01},
	{0x3528, 0x0E},
	{0x3554, 0x00},
	{0x3555, 0x01},
	{0x3556, 0x01},
	{0x3557, 0x01},
	{0x3558, 0x01},
	{0x3559, 0x00},
	{0x355A, 0x00},
	{0x35BA, 0x0E},
	{0x366A, 0x1B},
	{0x366B, 0x19},
	{0x366C, 0x17},
	{0x366D, 0x17},
	{0x3A41, 0x04},

	{IMX274_TABLE_END, 0x00}
};

/*
 * Vertical 2/8 subsampling horizontal 3 binning
 * imx274 mode6(refer to datasheet) register configuration with
 * 1280x540 resolution, raw10 data and mipi four lane output
 */
static const struct reg_8 imx274_mode6_1280x540_raw10[] = {
	{0x3004, 0x04}, /* mode setting */
	{0x3005, 0x31},
	{0x3006, 0x00},
	{0x3007, 0x02}, /* mode setting */

	{0x3018, 0xA2}, /* output XVS, HVS */

	{0x306B, 0x05},
	{0x30E2, 0x04}, /* mode setting */

	{0x30EE, 0x01},
	{0x3342, 0x0A},
	{0x3343, 0x00},
	{0x3344, 0x16},
	{0x3345, 0x00},
	{0x33A6, 0x01},
	{0x3528, 0x0E},
	{0x3554, 0x1F},
	{0x3555, 0x01},
	{0x3556, 0x01},
	{0x3557, 0x01},
	{0x3558, 0x01},
	{0x3559, 0x00},
	{0x355A, 0x00},
	{0x35BA, 0x0E},
	{0x366A, 0x1B},
	{0x366B, 0x1A},
	{0x366C, 0x19},
	{0x366D, 0x17},
	{0x3A41, 0x04},

	{IMX274_TABLE_END, 0x00}
};

/*
 * imx274 first step register configuration for
 * starting stream
 */
static const struct reg_8 imx274_start_1[] = {
	{IMX274_STANDBY_REG, 0x12},

	/* PLRD: clock settings */
	{0x3120, 0xF0},
	{0x3121, 0x00},
	{0x3122, 0x02},
	{0x3129, 0x9C},
	{0x312A, 0x02},
	{0x312D, 0x02},

	{0x310B, 0x00},

	/* PLSTMG */
	{0x304C, 0x00}, /* PLSTMG01 */
	{0x304D, 0x03},
	{0x331C, 0x1A},
	{0x331D, 0x00},
	{0x3502, 0x02},
	{0x3529, 0x0E},
	{0x352A, 0x0E},
	{0x352B, 0x0E},
	{0x3538, 0x0E},
	{0x3539, 0x0E},
	{0x3553, 0x00},
	{0x357D, 0x05},
	{0x357F, 0x05},
	{0x3581, 0x04},
	{0x3583, 0x76},
	{0x3587, 0x01},
	{0x35BB, 0x0E},
	{0x35BC, 0x0E},
	{0x35BD, 0x0E},
	{0x35BE, 0x0E},
	{0x35BF, 0x0E},
	{0x366E, 0x00},
	{0x366F, 0x00},
	{0x3670, 0x00},
	{0x3671, 0x00},

	/* PSMIPI */
	{0x3304, 0x32}, /* PSMIPI1 */
	{0x3305, 0x00},
	{0x3306, 0x32},
	{0x3307, 0x00},
	{0x3590, 0x32},
	{0x3591, 0x00},
	{0x3686, 0x32},
	{0x3687, 0x00},

	{IMX274_TABLE_END, 0x00}
};

/*
 * imx274 second step register configuration for
 * starting stream
 */
static const struct reg_8 imx274_start_2[] = {
	{IMX274_STANDBY_REG, 0x00},
	{0x303E, 0x02}, /* SYS_MODE = 2 */
	{IMX274_TABLE_END, 0x00}
};

/*
 * imx274 third step register configuration for
 * starting stream
 */
static const struct reg_8 imx274_start_3[] = {
	{0x30F4, 0x00},
	{0x3018, 0xA2}, /* XHS VHS OUTPUT */
	{IMX274_TABLE_END, 0x00}
};

/*
 * imx274 register configuration for stopping stream
 */
static const struct reg_8 imx274_stop[] = {
	{IMX274_STANDBY_REG, 0x01},
	{IMX274_TABLE_END, 0x00}
};

/*
 * imx274 disable test pattern register configuration
 */
static const struct reg_8 imx274_tp_disabled[] = {
	{0x303C, 0x00},
	{0x377F, 0x00},
	{0x3781, 0x00},
	{0x370B, 0x00},
	{IMX274_TABLE_END, 0x00}
};

/*
 * imx274 test pattern register configuration
 * reg 0x303D defines the test pattern modes
 */
static const struct reg_8 imx274_tp_regs[] = {
	{0x303C, 0x11},
	{0x370E, 0x01},
	{0x377F, 0x01},
	{0x3781, 0x01},
	{0x370B, 0x11},
	{IMX274_TABLE_END, 0x00}
};

/* nocpiop happens to be the same number for the implemented modes */
static const struct imx274_mode imx274_modes[] = {
	{
		/* mode 1, 4K */
		.wbin_ratio = 1, /* 3840 */
		.hbin_ratio = 1, /* 2160 */
		.init_regs = imx274_mode1_3840x2160_raw10,
		.min_frame_len = 4550,
		.min_SHR = 12,
		.max_fps = 60,
		.nocpiop = 112,
	},
	{
		/* mode 3, 1080p */
		.wbin_ratio = 2, /* 1920 */
		.hbin_ratio = 2, /* 1080 */
		.init_regs = imx274_mode3_1920x1080_raw10,
		.min_frame_len = 2310,
		.min_SHR = 8,
		.max_fps = 120,
		.nocpiop = 112,
	},
	{
		/* mode 5, 720p */
		.wbin_ratio = 3, /* 1280 */
		.hbin_ratio = 3, /* 720 */
		.init_regs = imx274_mode5_1280x720_raw10,
		.min_frame_len = 2310,
		.min_SHR = 8,
		.max_fps = 120,
		.nocpiop = 112,
	},
	{
		/* mode 6, 540p */
		.wbin_ratio = 3, /* 1280 */
		.hbin_ratio = 4, /* 540 */
		.init_regs = imx274_mode6_1280x540_raw10,
		.min_frame_len = 2310,
		.min_SHR = 4,
		.max_fps = 120,
		.nocpiop = 112,
	},
};

/*
 * struct imx274_ctrls - imx274 ctrl structure
 * @handler: V4L2 ctrl handler structure
 * @exposure: Pointer to expsure ctrl structure
 * @gain: Pointer to gain ctrl structure
 * @vflip: Pointer to vflip ctrl structure
 * @test_pattern: Pointer to test pattern ctrl structure
 */
struct imx274_ctrls {
	struct v4l2_ctrl_handler handler;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *gain;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *test_pattern;
};

/*
 * struct stim274 - imx274 device structure
 * @sd: V4L2 subdevice structure
 * @pad: Media pad structure
 * @client: Pointer to I2C client
 * @ctrls: imx274 control structure
 * @crop: rect to be captured
 * @compose: compose rect, i.e. output resolution
 * @format: V4L2 media bus frame format structure
 *          (width and height are in sync with the compose rect)
 * @frame_rate: V4L2 frame rate structure
 * @regmap: Pointer to regmap structure
 * @reset_gpio: Pointer to reset gpio
 * @supplies: List of analog and digital supply regulators
 * @inck: Pointer to sensor input clock
 * @lock: Mutex structure
 * @mode: Parameters for the selected readout mode
 */
struct stimx274 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct i2c_client *client;
	struct imx274_ctrls ctrls;
	struct v4l2_rect crop;
	struct v4l2_mbus_framefmt format;
	struct v4l2_fract frame_interval;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[IMX274_NUM_SUPPLIES];
	struct clk *inck;
	struct mutex lock; /* mutex lock for operations */
	const struct imx274_mode *mode;
};

#define IMX274_ROUND(dim, step, flags)			\
	((flags) & V4L2_SEL_FLAG_GE			\
	 ? roundup((dim), (step))			\
	 : ((flags) & V4L2_SEL_FLAG_LE			\
	    ? rounddown((dim), (step))			\
	    : rounddown((dim) + (step) / 2, (step))))

/*
 * Function declaration
 */
static int imx274_set_gain(struct stimx274 *priv, struct v4l2_ctrl *ctrl);
static int imx274_set_exposure(struct stimx274 *priv, int val);
static int imx274_set_vflip(struct stimx274 *priv, int val);
static int imx274_set_test_pattern(struct stimx274 *priv, int val);
static int imx274_set_frame_interval(struct stimx274 *priv,
				     struct v4l2_fract frame_interval);

static inline void msleep_range(unsigned int delay_base)
{
	usleep_range(delay_base * 1000, delay_base * 1000 + 500);
}

/*
 * v4l2_ctrl and v4l2_subdev related operations
 */
static inline struct v4l2_subdev *ctrl_to_sd(struct v4l2_ctrl *ctrl)
{
	return &container_of(ctrl->handler,
			     struct stimx274, ctrls.handler)->sd;
}

static inline struct stimx274 *to_imx274(struct v4l2_subdev *sd)
{
	return container_of(sd, struct stimx274, sd);
}

/*
 * Writing a register table
 *
 * @priv: Pointer to device
 * @table: Table containing register values (with optional delays)
 *
 * This is used to write register table into sensor's reg map.
 *
 * Return: 0 on success, errors otherwise
 */
static int imx274_write_table(struct stimx274 *priv, const struct reg_8 table[])
{
	struct regmap *regmap = priv->regmap;
	int err = 0;
	const struct reg_8 *next;
	u8 val;

	int range_start = -1;
	int range_count = 0;
	u8 range_vals[16];
	int max_range_vals = ARRAY_SIZE(range_vals);

	for (next = table;; next++) {
		if ((next->addr != range_start + range_count) ||
		    (next->addr == IMX274_TABLE_END) ||
		    (next->addr == IMX274_TABLE_WAIT_MS) ||
		    (range_count == max_range_vals)) {
			if (range_count == 1)
				err = regmap_write(regmap,
						   range_start, range_vals[0]);
			else if (range_count > 1)
				err = regmap_bulk_write(regmap, range_start,
							&range_vals[0],
							range_count);
			else
				err = 0;

			if (err)
				return err;

			range_start = -1;
			range_count = 0;

			/* Handle special address values */
			if (next->addr == IMX274_TABLE_END)
				break;

			if (next->addr == IMX274_TABLE_WAIT_MS) {
				msleep_range(next->val);
				continue;
			}
		}

		val = next->val;

		if (range_start == -1)
			range_start = next->addr;

		range_vals[range_count++] = val;
	}
	return 0;
}

static inline int imx274_write_reg(struct stimx274 *priv, u16 addr, u8 val)
{
	int err;

	err = regmap_write(priv->regmap, addr, val);
	if (err)
		dev_err(&priv->client->dev,
			"%s : i2c write failed, %x = %x\n", __func__,
			addr, val);
	else
		dev_dbg(&priv->client->dev,
			"%s : addr 0x%x, val=0x%x\n", __func__,
			addr, val);
	return err;
}

/**
 * imx274_read_mbreg - Read a multibyte register.
 *
 * Uses a bulk read where possible.
 *
 * @priv: Pointer to device structure
 * @addr: Address of the LSB register.  Other registers must be
 *        consecutive, least-to-most significant.
 * @val: Pointer to store the register value (cpu endianness)
 * @nbytes: Number of bytes to read (range: [1..3]).
 *          Other bytes are zet to 0.
 *
 * Return: 0 on success, errors otherwise
 */
static int imx274_read_mbreg(struct stimx274 *priv, u16 addr, u32 *val,
			     size_t nbytes)
{
	__le32 val_le = 0;
	int err;

	err = regmap_bulk_read(priv->regmap, addr, &val_le, nbytes);
	if (err) {
		dev_err(&priv->client->dev,
			"%s : i2c bulk read failed, %x (%zu bytes)\n",
			__func__, addr, nbytes);
	} else {
		*val = le32_to_cpu(val_le);
		dev_dbg(&priv->client->dev,
			"%s : addr 0x%x, val=0x%x (%zu bytes)\n",
			__func__, addr, *val, nbytes);
	}

	return err;
}

/**
 * imx274_write_mbreg - Write a multibyte register.
 *
 * Uses a bulk write where possible.
 *
 * @priv: Pointer to device structure
 * @addr: Address of the LSB register.  Other registers must be
 *        consecutive, least-to-most significant.
 * @val: Value to be written to the register (cpu endianness)
 * @nbytes: Number of bytes to write (range: [1..3])
 */
static int imx274_write_mbreg(struct stimx274 *priv, u16 addr, u32 val,
			      size_t nbytes)
{
	__le32 val_le = cpu_to_le32(val);
	int err;

	err = regmap_bulk_write(priv->regmap, addr, &val_le, nbytes);
	if (err)
		dev_err(&priv->client->dev,
			"%s : i2c bulk write failed, %x = %x (%zu bytes)\n",
			__func__, addr, val, nbytes);
	else
		dev_dbg(&priv->client->dev,
			"%s : addr 0x%x, val=0x%x (%zu bytes)\n",
			__func__, addr, val, nbytes);
	return err;
}

/*
 * Set mode registers to start stream.
 * @priv: Pointer to device structure
 *
 * Return: 0 on success, errors otherwise
 */
static int imx274_mode_regs(struct stimx274 *priv)
{
	int err = 0;

	err = imx274_write_table(priv, imx274_start_1);
	if (err)
		return err;

	err = imx274_write_table(priv, priv->mode->init_regs);

	return err;
}

/*
 * imx274_start_stream - Function for starting stream per mode index
 * @priv: Pointer to device structure
 *
 * Return: 0 on success, errors otherwise
 */
static int imx274_start_stream(struct stimx274 *priv)
{
	int err = 0;

	err = __v4l2_ctrl_handler_setup(&priv->ctrls.handler);
	if (err) {
		dev_err(&priv->client->dev, "Error %d setup controls\n", err);
		return err;
	}

	/*
	 * Refer to "Standby Cancel Sequence when using CSI-2" in
	 * imx274 datasheet, it should wait 10ms or more here.
	 * give it 1 extra ms for margin
	 */
	msleep_range(11);
	err = imx274_write_table(priv, imx274_start_2);
	if (err)
		return err;

	/*
	 * Refer to "Standby Cancel Sequence when using CSI-2" in
	 * imx274 datasheet, it should wait 7ms or more here.
	 * give it 1 extra ms for margin
	 */
	msleep_range(8);
	err = imx274_write_table(priv, imx274_start_3);
	if (err)
		return err;

	return 0;
}

/*
 * imx274_reset - Function called to reset the sensor
 * @priv: Pointer to device structure
 * @rst: Input value for determining the sensor's end state after reset
 *
 * Set the senor in reset and then
 * if rst = 0, keep it in reset;
 * if rst = 1, bring it out of reset.
 *
 */
static void imx274_reset(struct stimx274 *priv, int rst)
{
	gpiod_set_value_cansleep(priv->reset_gpio, 0);
	usleep_range(IMX274_RESET_DELAY1, IMX274_RESET_DELAY2);
	gpiod_set_value_cansleep(priv->reset_gpio, !!rst);
	usleep_range(IMX274_RESET_DELAY1, IMX274_RESET_DELAY2);
}

static int imx274_power_on(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct stimx274 *imx274 = to_imx274(sd);
	int ret;

	/* keep sensor in reset before power on */
	imx274_reset(imx274, 0);

	ret = clk_prepare_enable(imx274->inck);
	if (ret) {
		dev_err(&imx274->client->dev,
			"Failed to enable input clock: %d\n", ret);
		return ret;
	}

	ret = regulator_bulk_enable(IMX274_NUM_SUPPLIES, imx274->supplies);
	if (ret) {
		dev_err(&imx274->client->dev,
			"Failed to enable regulators: %d\n", ret);
		goto fail_reg;
	}

	udelay(2);
	imx274_reset(imx274, 1);

	return 0;

fail_reg:
	clk_disable_unprepare(imx274->inck);
	return ret;
}

static int imx274_power_off(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct stimx274 *imx274 = to_imx274(sd);

	imx274_reset(imx274, 0);

	regulator_bulk_disable(IMX274_NUM_SUPPLIES, imx274->supplies);

	clk_disable_unprepare(imx274->inck);

	return 0;
}

static int imx274_regulators_get(struct device *dev, struct stimx274 *imx274)
{
	unsigned int i;

	for (i = 0; i < IMX274_NUM_SUPPLIES; i++)
		imx274->supplies[i].supply = imx274_supply_names[i];

	return devm_regulator_bulk_get(dev, IMX274_NUM_SUPPLIES,
					imx274->supplies);
}

/**
 * imx274_s_ctrl - This is used to set the imx274 V4L2 controls
 * @ctrl: V4L2 control to be set
 *
 * This function is used to set the V4L2 controls for the imx274 sensor.
 *
 * Return: 0 on success, errors otherwise
 */
static int imx274_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct v4l2_subdev *sd = ctrl_to_sd(ctrl);
	struct stimx274 *imx274 = to_imx274(sd);
	int ret = -EINVAL;

	if (!pm_runtime_get_if_in_use(&imx274->client->dev))
		return 0;

	dev_dbg(&imx274->client->dev,
		"%s : s_ctrl: %s, value: %d\n", __func__,
		ctrl->name, ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		dev_dbg(&imx274->client->dev,
			"%s : set V4L2_CID_EXPOSURE\n", __func__);
		ret = imx274_set_exposure(imx274, ctrl->val);
		break;

	case V4L2_CID_GAIN:
		dev_dbg(&imx274->client->dev,
			"%s : set V4L2_CID_GAIN\n", __func__);
		ret = imx274_set_gain(imx274, ctrl);
		break;

	case V4L2_CID_VFLIP:
		dev_dbg(&imx274->client->dev,
			"%s : set V4L2_CID_VFLIP\n", __func__);
		ret = imx274_set_vflip(imx274, ctrl->val);
		break;

	case V4L2_CID_TEST_PATTERN:
		dev_dbg(&imx274->client->dev,
			"%s : set V4L2_CID_TEST_PATTERN\n", __func__);
		ret = imx274_set_test_pattern(imx274, ctrl->val);
		break;
	}

	pm_runtime_put(&imx274->client->dev);

	return ret;
}

static int imx274_binning_goodness(struct stimx274 *imx274,
				   int w, int ask_w,
				   int h, int ask_h, u32 flags)
{
	struct device *dev = &imx274->client->dev;
	const int goodness = 100000;
	int val = 0;

	if (flags & V4L2_SEL_FLAG_GE) {
		if (w < ask_w)
			val -= goodness;
		if (h < ask_h)
			val -= goodness;
	}

	if (flags & V4L2_SEL_FLAG_LE) {
		if (w > ask_w)
			val -= goodness;
		if (h > ask_h)
			val -= goodness;
	}

	val -= abs(w - ask_w);
	val -= abs(h - ask_h);

	dev_dbg(dev, "%s: ask %dx%d, size %dx%d, goodness %d\n",
		__func__, ask_w, ask_h, w, h, val);

	return val;
}

/**
 * __imx274_change_compose - Helper function to change binning and set both
 *	compose and format.
 *
 * We have two entry points to change binning: set_fmt and
 * set_selection(COMPOSE). Both have to compute the new output size
 * and set it in both the compose rect and the frame format size. We
 * also need to do the same things after setting cropping to restore
 * 1:1 binning.
 *
 * This function contains the common code for these three cases, it
 * has many arguments in order to accommodate the needs of all of
 * them.
 *
 * Must be called with imx274->lock locked.
 *
 * @imx274: The device object
 * @cfg:    The pad config we are editing for TRY requests
 * @which:  V4L2_SUBDEV_FORMAT_ACTIVE or V4L2_SUBDEV_FORMAT_TRY from the caller
 * @width:  Input-output parameter: set to the desired width before
 *          the call, contains the chosen value after returning successfully
 * @height: Input-output parameter for height (see @width)
 * @flags:  Selection flags from struct v4l2_subdev_selection, or 0 if not
 *          available (when called from set_fmt)
 */
static int __imx274_change_compose(struct stimx274 *imx274,
				   struct v4l2_subdev_pad_config *cfg,
				   u32 which,
				   u32 *width,
				   u32 *height,
				   u32 flags)
{
	struct device *dev = &imx274->client->dev;
	const struct v4l2_rect *cur_crop;
	struct v4l2_mbus_framefmt *tgt_fmt;
	unsigned int i;
	const struct imx274_mode *best_mode = &imx274_modes[0];
	int best_goodness = INT_MIN;

	if (which == V4L2_SUBDEV_FORMAT_TRY) {
		cur_crop = &cfg->try_crop;
		tgt_fmt = &cfg->try_fmt;
	} else {
		cur_crop = &imx274->crop;
		tgt_fmt = &imx274->format;
	}

	for (i = 0; i < ARRAY_SIZE(imx274_modes); i++) {
		u8 wratio = imx274_modes[i].wbin_ratio;
		u8 hratio = imx274_modes[i].hbin_ratio;

		int goodness = imx274_binning_goodness(
			imx274,
			cur_crop->width / wratio, *width,
			cur_crop->height / hratio, *height,
			flags);

		if (goodness >= best_goodness) {
			best_goodness = goodness;
			best_mode = &imx274_modes[i];
		}
	}

	*width = cur_crop->width / best_mode->wbin_ratio;
	*height = cur_crop->height / best_mode->hbin_ratio;

	if (which == V4L2_SUBDEV_FORMAT_ACTIVE)
		imx274->mode = best_mode;

	dev_dbg(dev, "%s: selected %ux%u binning\n",
		__func__, best_mode->wbin_ratio, best_mode->hbin_ratio);

	tgt_fmt->width = *width;
	tgt_fmt->height = *height;
	tgt_fmt->field = V4L2_FIELD_NONE;

	return 0;
}

/**
 * imx274_get_fmt - Get the pad format
 * @sd: Pointer to V4L2 Sub device structure
 * @cfg: Pointer to sub device pad information structure
 * @fmt: Pointer to pad level media bus format
 *
 * This function is used to get the pad format information.
 *
 * Return: 0 on success
 */
static int imx274_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct stimx274 *imx274 = to_imx274(sd);

	mutex_lock(&imx274->lock);
	fmt->format = imx274->format;
	mutex_unlock(&imx274->lock);
	return 0;
}

/**
 * imx274_set_fmt - This is used to set the pad format
 * @sd: Pointer to V4L2 Sub device structure
 * @cfg: Pointer to sub device pad information structure
 * @format: Pointer to pad level media bus format
 *
 * This function is used to set the pad format.
 *
 * Return: 0 on success
 */
static int imx274_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *format)
{
	struct v4l2_mbus_framefmt *fmt = &format->format;
	struct stimx274 *imx274 = to_imx274(sd);
	int err = 0;

	mutex_lock(&imx274->lock);

	err = __imx274_change_compose(imx274, cfg, format->which,
				      &fmt->width, &fmt->height, 0);

	if (err)
		goto out;

	/*
	 * __imx274_change_compose already set width and height in the
	 * applicable format, but we need to keep all other format
	 * values, so do a full copy here
	 */
	fmt->field = V4L2_FIELD_NONE;
	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		cfg->try_fmt = *fmt;
	else
		imx274->format = *fmt;

out:
	mutex_unlock(&imx274->lock);

	return err;
}

static int imx274_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct stimx274 *imx274 = to_imx274(sd);
	const struct v4l2_rect *src_crop;
	const struct v4l2_mbus_framefmt *src_fmt;
	int ret = 0;

	if (sel->pad != 0)
		return -EINVAL;

	if (sel->target == V4L2_SEL_TGT_CROP_BOUNDS) {
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = IMX274_MAX_WIDTH;
		sel->r.height = IMX274_MAX_HEIGHT;
		return 0;
	}

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY) {
		src_crop = &cfg->try_crop;
		src_fmt = &cfg->try_fmt;
	} else {
		src_crop = &imx274->crop;
		src_fmt = &imx274->format;
	}

	mutex_lock(&imx274->lock);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *src_crop;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = src_crop->width;
		sel->r.height = src_crop->height;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = src_fmt->width;
		sel->r.height = src_fmt->height;
		break;
	default:
		ret = -EINVAL;
	}

	mutex_unlock(&imx274->lock);

	return ret;
}

static int imx274_set_selection_crop(struct stimx274 *imx274,
				     struct v4l2_subdev_pad_config *cfg,
				     struct v4l2_subdev_selection *sel)
{
	struct v4l2_rect *tgt_crop;
	struct v4l2_rect new_crop;
	bool size_changed;

	/*
	 * h_step could be 12 or 24 depending on the binning. But we
	 * won't know the binning until we choose the mode later in
	 * __imx274_change_compose(). Thus let's be safe and use the
	 * most conservative value in all cases.
	 */
	const u32 h_step = 24;

	new_crop.width = min_t(u32,
			       IMX274_ROUND(sel->r.width, h_step, sel->flags),
			       IMX274_MAX_WIDTH);

	/* Constraint: HTRIMMING_END - HTRIMMING_START >= 144 */
	if (new_crop.width < 144)
		new_crop.width = 144;

	new_crop.left = min_t(u32,
			      IMX274_ROUND(sel->r.left, h_step, 0),
			      IMX274_MAX_WIDTH - new_crop.width);

	new_crop.height = min_t(u32,
				IMX274_ROUND(sel->r.height, 2, sel->flags),
				IMX274_MAX_HEIGHT);

	new_crop.top = min_t(u32, IMX274_ROUND(sel->r.top, 2, 0),
			     IMX274_MAX_HEIGHT - new_crop.height);

	sel->r = new_crop;

	if (sel->which == V4L2_SUBDEV_FORMAT_TRY)
		tgt_crop = &cfg->try_crop;
	else
		tgt_crop = &imx274->crop;

	mutex_lock(&imx274->lock);

	size_changed = (new_crop.width != tgt_crop->width ||
			new_crop.height != tgt_crop->height);

	/* __imx274_change_compose needs the new size in *tgt_crop */
	*tgt_crop = new_crop;

	/* if crop size changed then reset the output image size */
	if (size_changed)
		__imx274_change_compose(imx274, cfg, sel->which,
					&new_crop.width, &new_crop.height,
					sel->flags);

	mutex_unlock(&imx274->lock);

	return 0;
}

static int imx274_set_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_pad_config *cfg,
				struct v4l2_subdev_selection *sel)
{
	struct stimx274 *imx274 = to_imx274(sd);

	if (sel->pad != 0)
		return -EINVAL;

	if (sel->target == V4L2_SEL_TGT_CROP)
		return imx274_set_selection_crop(imx274, cfg, sel);

	if (sel->target == V4L2_SEL_TGT_COMPOSE) {
		int err;

		mutex_lock(&imx274->lock);
		err =  __imx274_change_compose(imx274, cfg, sel->which,
					       &sel->r.width, &sel->r.height,
					       sel->flags);
		mutex_unlock(&imx274->lock);

		/*
		 * __imx274_change_compose already set width and
		 * height in set->r, we still need to set top-left
		 */
		if (!err) {
			sel->r.top = 0;
			sel->r.left = 0;
		}

		return err;
	}

	return -EINVAL;
}

static int imx274_apply_trimming(struct stimx274 *imx274)
{
	u32 h_start;
	u32 h_end;
	u32 hmax;
	u32 v_cut;
	s32 v_pos;
	u32 write_v_size;
	u32 y_out_size;
	int err;

	h_start = imx274->crop.left + 12;
	h_end = h_start + imx274->crop.width;

	/* Use the minimum allowed value of HMAX */
	/* Note: except in mode 1, (width / 16 + 23) is always < hmax_min */
	/* Note: 260 is the minimum HMAX in all implemented modes */
	hmax = max_t(u32, 260, (imx274->crop.width) / 16 + 23);

	/* invert v_pos if VFLIP */
	v_pos = imx274->ctrls.vflip->cur.val ?
		(-imx274->crop.top / 2) : (imx274->crop.top / 2);
	v_cut = (IMX274_MAX_HEIGHT - imx274->crop.height) / 2;
	write_v_size = imx274->crop.height + 22;
	y_out_size   = imx274->crop.height;

	err = imx274_write_mbreg(imx274, IMX274_HMAX_REG_LSB, hmax, 2);
	if (!err)
		err = imx274_write_mbreg(imx274, IMX274_HTRIM_EN_REG, 1, 1);
	if (!err)
		err = imx274_write_mbreg(imx274, IMX274_HTRIM_START_REG_LSB,
					 h_start, 2);
	if (!err)
		err = imx274_write_mbreg(imx274, IMX274_HTRIM_END_REG_LSB,
					 h_end, 2);
	if (!err)
		err = imx274_write_mbreg(imx274, IMX274_VWIDCUTEN_REG, 1, 1);
	if (!err)
		err = imx274_write_mbreg(imx274, IMX274_VWIDCUT_REG_LSB,
					 v_cut, 2);
	if (!err)
		err = imx274_write_mbreg(imx274, IMX274_VWINPOS_REG_LSB,
					 v_pos, 2);
	if (!err)
		err = imx274_write_mbreg(imx274, IMX274_WRITE_VSIZE_REG_LSB,
					 write_v_size, 2);
	if (!err)
		err = imx274_write_mbreg(imx274, IMX274_Y_OUT_SIZE_REG_LSB,
					 y_out_size, 2);

	return err;
}

/**
 * imx274_g_frame_interval - Get the frame interval
 * @sd: Pointer to V4L2 Sub device structure
 * @fi: Pointer to V4l2 Sub device frame interval structure
 *
 * This function is used to get the frame interval.
 *
 * Return: 0 on success
 */
static int imx274_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct stimx274 *imx274 = to_imx274(sd);

	fi->interval = imx274->frame_interval;
	dev_dbg(&imx274->client->dev, "%s frame rate = %d / %d\n",
		__func__, imx274->frame_interval.numerator,
		imx274->frame_interval.denominator);

	return 0;
}

/**
 * imx274_s_frame_interval - Set the frame interval
 * @sd: Pointer to V4L2 Sub device structure
 * @fi: Pointer to V4l2 Sub device frame interval structure
 *
 * This function is used to set the frame intervavl.
 *
 * Return: 0 on success
 */
static int imx274_s_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct stimx274 *imx274 = to_imx274(sd);
	struct v4l2_ctrl *ctrl = imx274->ctrls.exposure;
	int min, max, def;
	int ret;

	mutex_lock(&imx274->lock);
	ret = imx274_set_frame_interval(imx274, fi->interval);

	if (!ret) {
		fi->interval = imx274->frame_interval;

		/*
		 * exposure time range is decided by frame interval
		 * need to update it after frame interval changes
		 */
		min = IMX274_MIN_EXPOSURE_TIME;
		max = fi->interval.numerator * 1000000
			/ fi->interval.denominator;
		def = max;
		ret = __v4l2_ctrl_modify_range(ctrl, min, max, 1, def);
		if (ret) {
			dev_err(&imx274->client->dev,
				"Exposure ctrl range update failed\n");
			goto unlock;
		}

		/* update exposure time accordingly */
		imx274_set_exposure(imx274, ctrl->val);

		dev_dbg(&imx274->client->dev, "set frame interval to %uus\n",
			fi->interval.numerator * 1000000
			/ fi->interval.denominator);
	}

unlock:
	mutex_unlock(&imx274->lock);

	return ret;
}

/**
 * imx274_load_default - load default control values
 * @priv: Pointer to device structure
 *
 * Return: 0 on success, errors otherwise
 */
static void imx274_load_default(struct stimx274 *priv)
{
	/* load default control values */
	priv->frame_interval.numerator = 1;
	priv->frame_interval.denominator = IMX274_DEF_FRAME_RATE;
	priv->ctrls.exposure->val = 1000000 / IMX274_DEF_FRAME_RATE;
	priv->ctrls.gain->val = IMX274_DEF_GAIN;
	priv->ctrls.vflip->val = 0;
	priv->ctrls.test_pattern->val = TEST_PATTERN_DISABLED;
}

/**
 * imx274_s_stream - It is used to start/stop the streaming.
 * @sd: V4L2 Sub device
 * @on: Flag (True / False)
 *
 * This function controls the start or stop of streaming for the
 * imx274 sensor.
 *
 * Return: 0 on success, errors otherwise
 */
static int imx274_s_stream(struct v4l2_subdev *sd, int on)
{
	struct stimx274 *imx274 = to_imx274(sd);
	int ret = 0;

	dev_dbg(&imx274->client->dev, "%s : %s, mode index = %td\n", __func__,
		on ? "Stream Start" : "Stream Stop",
		imx274->mode - &imx274_modes[0]);

	mutex_lock(&imx274->lock);

	if (on) {
		ret = pm_runtime_get_sync(&imx274->client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&imx274->client->dev);
			mutex_unlock(&imx274->lock);
			return ret;
		}

		/* load mode registers */
		ret = imx274_mode_regs(imx274);
		if (ret)
			goto fail;

		ret = imx274_apply_trimming(imx274);
		if (ret)
			goto fail;

		/*
		 * update frame rate & expsoure. if the last mode is different,
		 * HMAX could be changed. As the result, frame rate & exposure
		 * are changed.
		 * gain is not affected.
		 */
		ret = imx274_set_frame_interval(imx274,
						imx274->frame_interval);
		if (ret)
			goto fail;

		/* start stream */
		ret = imx274_start_stream(imx274);
		if (ret)
			goto fail;
	} else {
		/* stop stream */
		ret = imx274_write_table(imx274, imx274_stop);
		if (ret)
			goto fail;

		pm_runtime_put(&imx274->client->dev);
	}

	mutex_unlock(&imx274->lock);
	dev_dbg(&imx274->client->dev, "%s : Done\n", __func__);
	return 0;

fail:
	pm_runtime_put(&imx274->client->dev);
	mutex_unlock(&imx274->lock);
	dev_err(&imx274->client->dev, "s_stream failed\n");
	return ret;
}

/*
 * imx274_get_frame_length - Function for obtaining current frame length
 * @priv: Pointer to device structure
 * @val: Pointer to obainted value
 *
 * frame_length = vmax x (svr + 1), in unit of hmax.
 *
 * Return: 0 on success
 */
static int imx274_get_frame_length(struct stimx274 *priv, u32 *val)
{
	int err;
	u32 svr;
	u32 vmax;

	err = imx274_read_mbreg(priv, IMX274_SVR_REG_LSB, &svr, 2);
	if (err)
		goto fail;

	err = imx274_read_mbreg(priv, IMX274_VMAX_REG_3, &vmax, 3);
	if (err)
		goto fail;

	*val = vmax * (svr + 1);

	return 0;

fail:
	dev_err(&priv->client->dev, "%s error = %d\n", __func__, err);
	return err;
}

static int imx274_clamp_coarse_time(struct stimx274 *priv, u32 *val,
				    u32 *frame_length)
{
	int err;

	err = imx274_get_frame_length(priv, frame_length);
	if (err)
		return err;

	if (*frame_length < priv->mode->min_frame_len)
		*frame_length =  priv->mode->min_frame_len;

	*val = *frame_length - *val; /* convert to raw shr */
	if (*val > *frame_length - IMX274_SHR_LIMIT_CONST)
		*val = *frame_length - IMX274_SHR_LIMIT_CONST;
	else if (*val < priv->mode->min_SHR)
		*val = priv->mode->min_SHR;

	return 0;
}

/*
 * imx274_set_digital gain - Function called when setting digital gain
 * @priv: Pointer to device structure
 * @dgain: Value of digital gain.
 *
 * Digital gain has only 4 steps: 1x, 2x, 4x, and 8x
 *
 * Return: 0 on success
 */
static int imx274_set_digital_gain(struct stimx274 *priv, u32 dgain)
{
	u8 reg_val;

	reg_val = ffs(dgain);

	if (reg_val)
		reg_val--;

	reg_val = clamp(reg_val, (u8)0, (u8)3);

	return imx274_write_reg(priv, IMX274_DIGITAL_GAIN_REG,
				reg_val & IMX274_MASK_LSB_4_BITS);
}

/*
 * imx274_set_gain - Function called when setting gain
 * @priv: Pointer to device structure
 * @val: Value of gain. the real value = val << IMX274_GAIN_SHIFT;
 * @ctrl: v4l2 control pointer
 *
 * Set the gain based on input value.
 * The caller should hold the mutex lock imx274->lock if necessary
 *
 * Return: 0 on success
 */
static int imx274_set_gain(struct stimx274 *priv, struct v4l2_ctrl *ctrl)
{
	int err;
	u32 gain, analog_gain, digital_gain, gain_reg;

	gain = (u32)(ctrl->val);

	dev_dbg(&priv->client->dev,
		"%s : input gain = %d.%d\n", __func__,
		gain >> IMX274_GAIN_SHIFT,
		((gain & IMX274_GAIN_SHIFT_MASK) * 100) >> IMX274_GAIN_SHIFT);

	if (gain > IMX274_MAX_DIGITAL_GAIN * IMX274_MAX_ANALOG_GAIN)
		gain = IMX274_MAX_DIGITAL_GAIN * IMX274_MAX_ANALOG_GAIN;
	else if (gain < IMX274_MIN_GAIN)
		gain = IMX274_MIN_GAIN;

	if (gain <= IMX274_MAX_ANALOG_GAIN)
		digital_gain = 1;
	else if (gain <= IMX274_MAX_ANALOG_GAIN * 2)
		digital_gain = 2;
	else if (gain <= IMX274_MAX_ANALOG_GAIN * 4)
		digital_gain = 4;
	else
		digital_gain = IMX274_MAX_DIGITAL_GAIN;

	analog_gain = gain / digital_gain;

	dev_dbg(&priv->client->dev,
		"%s : digital gain = %d, analog gain = %d.%d\n",
		__func__, digital_gain, analog_gain >> IMX274_GAIN_SHIFT,
		((analog_gain & IMX274_GAIN_SHIFT_MASK) * 100)
		>> IMX274_GAIN_SHIFT);

	err = imx274_set_digital_gain(priv, digital_gain);
	if (err)
		goto fail;

	/* convert to register value, refer to imx274 datasheet */
	gain_reg = (u32)IMX274_GAIN_CONST -
		(IMX274_GAIN_CONST << IMX274_GAIN_SHIFT) / analog_gain;
	if (gain_reg > IMX274_GAIN_REG_MAX)
		gain_reg = IMX274_GAIN_REG_MAX;

	err = imx274_write_mbreg(priv, IMX274_ANALOG_GAIN_ADDR_LSB, gain_reg,
				 2);
	if (err)
		goto fail;

	if (IMX274_GAIN_CONST - gain_reg == 0) {
		err = -EINVAL;
		goto fail;
	}

	/* convert register value back to gain value */
	ctrl->val = (IMX274_GAIN_CONST << IMX274_GAIN_SHIFT)
			/ (IMX274_GAIN_CONST - gain_reg) * digital_gain;

	dev_dbg(&priv->client->dev,
		"%s : GAIN control success, gain_reg = %d, new gain = %d\n",
		__func__, gain_reg, ctrl->val);

	return 0;

fail:
	dev_err(&priv->client->dev, "%s error = %d\n", __func__, err);
	return err;
}

/*
 * imx274_set_coarse_time - Function called when setting SHR value
 * @priv: Pointer to device structure
 * @val: Value for exposure time in number of line_length, or [HMAX]
 *
 * Set SHR value based on input value.
 *
 * Return: 0 on success
 */
static int imx274_set_coarse_time(struct stimx274 *priv, u32 *val)
{
	int err;
	u32 coarse_time, frame_length;

	coarse_time = *val;

	/* convert exposure_time to appropriate SHR value */
	err = imx274_clamp_coarse_time(priv, &coarse_time, &frame_length);
	if (err)
		goto fail;

	err = imx274_write_mbreg(priv, IMX274_SHR_REG_LSB, coarse_time, 2);
	if (err)
		goto fail;

	*val = frame_length - coarse_time;
	return 0;

fail:
	dev_err(&priv->client->dev, "%s error = %d\n", __func__, err);
	return err;
}

/*
 * imx274_set_exposure - Function called when setting exposure time
 * @priv: Pointer to device structure
 * @val: Variable for exposure time, in the unit of micro-second
 *
 * Set exposure time based on input value.
 * The caller should hold the mutex lock imx274->lock if necessary
 *
 * Return: 0 on success
 */
static int imx274_set_exposure(struct stimx274 *priv, int val)
{
	int err;
	u32 hmax;
	u32 coarse_time; /* exposure time in unit of line (HMAX)*/

	dev_dbg(&priv->client->dev,
		"%s : EXPOSURE control input = %d\n", __func__, val);

	/* step 1: convert input exposure_time (val) into number of 1[HMAX] */

	err = imx274_read_mbreg(priv, IMX274_HMAX_REG_LSB, &hmax, 2);
	if (err)
		goto fail;

	if (hmax == 0) {
		err = -EINVAL;
		goto fail;
	}

	coarse_time = (IMX274_PIXCLK_CONST1 / IMX274_PIXCLK_CONST2 * val
			- priv->mode->nocpiop) / hmax;

	/* step 2: convert exposure_time into SHR value */

	/* set SHR */
	err = imx274_set_coarse_time(priv, &coarse_time);
	if (err)
		goto fail;

	priv->ctrls.exposure->val =
			(coarse_time * hmax + priv->mode->nocpiop)
			/ (IMX274_PIXCLK_CONST1 / IMX274_PIXCLK_CONST2);

	dev_dbg(&priv->client->dev,
		"%s : EXPOSURE control success\n", __func__);
	return 0;

fail:
	dev_err(&priv->client->dev, "%s error = %d\n", __func__, err);

	return err;
}

/*
 * imx274_set_vflip - Function called when setting vertical flip
 * @priv: Pointer to device structure
 * @val: Value for vflip setting
 *
 * Set vertical flip based on input value.
 * val = 0: normal, no vertical flip
 * val = 1: vertical flip enabled
 * The caller should hold the mutex lock imx274->lock if necessary
 *
 * Return: 0 on success
 */
static int imx274_set_vflip(struct stimx274 *priv, int val)
{
	int err;

	err = imx274_write_reg(priv, IMX274_VFLIP_REG, val);
	if (err) {
		dev_err(&priv->client->dev, "VFLIP control error\n");
		return err;
	}

	dev_dbg(&priv->client->dev,
		"%s : VFLIP control success\n", __func__);

	return 0;
}

/*
 * imx274_set_test_pattern - Function called when setting test pattern
 * @priv: Pointer to device structure
 * @val: Variable for test pattern
 *
 * Set to different test patterns based on input value.
 *
 * Return: 0 on success
 */
static int imx274_set_test_pattern(struct stimx274 *priv, int val)
{
	int err = 0;

	if (val == TEST_PATTERN_DISABLED) {
		err = imx274_write_table(priv, imx274_tp_disabled);
	} else if (val <= TEST_PATTERN_V_COLOR_BARS) {
		err = imx274_write_reg(priv, IMX274_TEST_PATTERN_REG, val - 1);
		if (!err)
			err = imx274_write_table(priv, imx274_tp_regs);
	} else {
		err = -EINVAL;
	}

	if (!err)
		dev_dbg(&priv->client->dev,
			"%s : TEST PATTERN control success\n", __func__);
	else
		dev_err(&priv->client->dev, "%s error = %d\n", __func__, err);

	return err;
}

/*
 * imx274_set_frame_length - Function called when setting frame length
 * @priv: Pointer to device structure
 * @val: Variable for frame length (= VMAX, i.e. vertical drive period length)
 *
 * Set frame length based on input value.
 *
 * Return: 0 on success
 */
static int imx274_set_frame_length(struct stimx274 *priv, u32 val)
{
	int err;
	u32 frame_length;

	dev_dbg(&priv->client->dev, "%s : input length = %d\n",
		__func__, val);

	frame_length = (u32)val;

	err = imx274_write_mbreg(priv, IMX274_VMAX_REG_3, frame_length, 3);
	if (err)
		goto fail;

	return 0;

fail:
	dev_err(&priv->client->dev, "%s error = %d\n", __func__, err);
	return err;
}

/*
 * imx274_set_frame_interval - Function called when setting frame interval
 * @priv: Pointer to device structure
 * @frame_interval: Variable for frame interval
 *
 * Change frame interval by updating VMAX value
 * The caller should hold the mutex lock imx274->lock if necessary
 *
 * Return: 0 on success
 */
static int imx274_set_frame_interval(struct stimx274 *priv,
				     struct v4l2_fract frame_interval)
{
	int err;
	u32 frame_length, req_frame_rate;
	u32 svr;
	u32 hmax;

	dev_dbg(&priv->client->dev, "%s: input frame interval = %d / %d",
		__func__, frame_interval.numerator,
		frame_interval.denominator);

	if (frame_interval.numerator == 0 || frame_interval.denominator == 0) {
		frame_interval.denominator = IMX274_DEF_FRAME_RATE;
		frame_interval.numerator = 1;
	}

	req_frame_rate = (u32)(frame_interval.denominator
				/ frame_interval.numerator);

	/* boundary check */
	if (req_frame_rate > priv->mode->max_fps) {
		frame_interval.numerator = 1;
		frame_interval.denominator = priv->mode->max_fps;
	} else if (req_frame_rate < IMX274_MIN_FRAME_RATE) {
		frame_interval.numerator = 1;
		frame_interval.denominator = IMX274_MIN_FRAME_RATE;
	}

	/*
	 * VMAX = 1/frame_rate x 72M / (SVR+1) / HMAX
	 * frame_length (i.e. VMAX) = (frame_interval) x 72M /(SVR+1) / HMAX
	 */

	err = imx274_read_mbreg(priv, IMX274_SVR_REG_LSB, &svr, 2);
	if (err)
		goto fail;

	dev_dbg(&priv->client->dev,
		"%s : register SVR = %d\n", __func__, svr);

	err = imx274_read_mbreg(priv, IMX274_HMAX_REG_LSB, &hmax, 2);
	if (err)
		goto fail;

	dev_dbg(&priv->client->dev,
		"%s : register HMAX = %d\n", __func__, hmax);

	if (hmax == 0 || frame_interval.denominator == 0) {
		err = -EINVAL;
		goto fail;
	}

	frame_length = IMX274_PIXCLK_CONST1 / (svr + 1) / hmax
					* frame_interval.numerator
					/ frame_interval.denominator;

	err = imx274_set_frame_length(priv, frame_length);
	if (err)
		goto fail;

	priv->frame_interval = frame_interval;
	return 0;

fail:
	dev_err(&priv->client->dev, "%s error = %d\n", __func__, err);
	return err;
}

static const struct v4l2_subdev_pad_ops imx274_pad_ops = {
	.get_fmt = imx274_get_fmt,
	.set_fmt = imx274_set_fmt,
	.get_selection = imx274_get_selection,
	.set_selection = imx274_set_selection,
};

static const struct v4l2_subdev_video_ops imx274_video_ops = {
	.g_frame_interval = imx274_g_frame_interval,
	.s_frame_interval = imx274_s_frame_interval,
	.s_stream = imx274_s_stream,
};

static const struct v4l2_subdev_ops imx274_subdev_ops = {
	.pad = &imx274_pad_ops,
	.video = &imx274_video_ops,
};

static const struct v4l2_ctrl_ops imx274_ctrl_ops = {
	.s_ctrl	= imx274_s_ctrl,
};

static const struct of_device_id imx274_of_id_table[] = {
	{ .compatible = "sony,imx274" },
	{ }
};
MODULE_DEVICE_TABLE(of, imx274_of_id_table);

static const struct i2c_device_id imx274_id[] = {
	{ "IMX274", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, imx274_id);

static int imx274_probe(struct i2c_client *client)
{
	struct v4l2_subdev *sd;
	struct stimx274 *imx274;
	int ret;

	/* initialize imx274 */
	imx274 = devm_kzalloc(&client->dev, sizeof(*imx274), GFP_KERNEL);
	if (!imx274)
		return -ENOMEM;

	mutex_init(&imx274->lock);

	imx274->inck = devm_clk_get_optional(&client->dev, "inck");
	if (IS_ERR(imx274->inck))
		return PTR_ERR(imx274->inck);

	ret = imx274_regulators_get(&client->dev, imx274);
	if (ret) {
		dev_err(&client->dev,
			"Failed to get power regulators, err: %d\n", ret);
		return ret;
	}

	/* initialize format */
	imx274->mode = &imx274_modes[0];
	imx274->crop.width = IMX274_MAX_WIDTH;
	imx274->crop.height = IMX274_MAX_HEIGHT;
	imx274->format.width = imx274->crop.width / imx274->mode->wbin_ratio;
	imx274->format.height = imx274->crop.height / imx274->mode->hbin_ratio;
	imx274->format.field = V4L2_FIELD_NONE;
	imx274->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	imx274->format.colorspace = V4L2_COLORSPACE_SRGB;
	imx274->frame_interval.numerator = 1;
	imx274->frame_interval.denominator = IMX274_DEF_FRAME_RATE;

	/* initialize regmap */
	imx274->regmap = devm_regmap_init_i2c(client, &imx274_regmap_config);
	if (IS_ERR(imx274->regmap)) {
		dev_err(&client->dev,
			"regmap init failed: %ld\n", PTR_ERR(imx274->regmap));
		ret = -ENODEV;
		goto err_regmap;
	}

	/* initialize subdevice */
	imx274->client = client;
	sd = &imx274->sd;
	v4l2_i2c_subdev_init(sd, client, &imx274_subdev_ops);
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE | V4L2_SUBDEV_FL_HAS_EVENTS;

	/* initialize subdev media pad */
	imx274->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &imx274->pad);
	if (ret < 0) {
		dev_err(&client->dev,
			"%s : media entity init Failed %d\n", __func__, ret);
		goto err_regmap;
	}

	/* initialize sensor reset gpio */
	imx274->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
						     GPIOD_OUT_HIGH);
	if (IS_ERR(imx274->reset_gpio)) {
		if (PTR_ERR(imx274->reset_gpio) != -EPROBE_DEFER)
			dev_err(&client->dev, "Reset GPIO not setup in DT");
		ret = PTR_ERR(imx274->reset_gpio);
		goto err_me;
	}

	/* power on the sensor */
	ret = imx274_power_on(&client->dev);
	if (ret < 0) {
		dev_err(&client->dev,
			"%s : imx274 power on failed\n", __func__);
		goto err_me;
	}

	/* initialize controls */
	ret = v4l2_ctrl_handler_init(&imx274->ctrls.handler, 4);
	if (ret < 0) {
		dev_err(&client->dev,
			"%s : ctrl handler init Failed\n", __func__);
		goto err_power_off;
	}

	imx274->ctrls.handler.lock = &imx274->lock;

	/* add new controls */
	imx274->ctrls.test_pattern = v4l2_ctrl_new_std_menu_items(
		&imx274->ctrls.handler, &imx274_ctrl_ops,
		V4L2_CID_TEST_PATTERN,
		ARRAY_SIZE(tp_qmenu) - 1, 0, 0, tp_qmenu);

	imx274->ctrls.gain = v4l2_ctrl_new_std(
		&imx274->ctrls.handler,
		&imx274_ctrl_ops,
		V4L2_CID_GAIN, IMX274_MIN_GAIN,
		IMX274_MAX_DIGITAL_GAIN * IMX274_MAX_ANALOG_GAIN, 1,
		IMX274_DEF_GAIN);

	imx274->ctrls.exposure = v4l2_ctrl_new_std(
		&imx274->ctrls.handler,
		&imx274_ctrl_ops,
		V4L2_CID_EXPOSURE, IMX274_MIN_EXPOSURE_TIME,
		1000000 / IMX274_DEF_FRAME_RATE, 1,
		IMX274_MIN_EXPOSURE_TIME);

	imx274->ctrls.vflip = v4l2_ctrl_new_std(
		&imx274->ctrls.handler,
		&imx274_ctrl_ops,
		V4L2_CID_VFLIP, 0, 1, 1, 0);

	imx274->sd.ctrl_handler = &imx274->ctrls.handler;
	if (imx274->ctrls.handler.error) {
		ret = imx274->ctrls.handler.error;
		goto err_ctrls;
	}

	/* load default control values */
	imx274_load_default(imx274);

	/* register subdevice */
	ret = v4l2_async_register_subdev(sd);
	if (ret < 0) {
		dev_err(&client->dev,
			"%s : v4l2_async_register_subdev failed %d\n",
			__func__, ret);
		goto err_ctrls;
	}

	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

	dev_info(&client->dev, "imx274 : imx274 probe success !\n");
	return 0;

err_ctrls:
	v4l2_ctrl_handler_free(&imx274->ctrls.handler);
err_power_off:
	imx274_power_off(&client->dev);
err_me:
	media_entity_cleanup(&sd->entity);
err_regmap:
	mutex_destroy(&imx274->lock);
	return ret;
}

static int imx274_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct stimx274 *imx274 = to_imx274(sd);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		imx274_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);

	v4l2_async_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&imx274->ctrls.handler);

	media_entity_cleanup(&sd->entity);
	mutex_destroy(&imx274->lock);
	return 0;
}

static const struct dev_pm_ops imx274_pm_ops = {
	SET_RUNTIME_PM_OPS(imx274_power_off, imx274_power_on, NULL)
};

static struct i2c_driver imx274_i2c_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.pm = &imx274_pm_ops,
		.of_match_table	= imx274_of_id_table,
	},
	.probe_new	= imx274_probe,
	.remove		= imx274_remove,
	.id_table	= imx274_id,
};

module_i2c_driver(imx274_i2c_driver);

MODULE_AUTHOR("Leon Luo <leonl@leopardimaging.com>");
MODULE_DESCRIPTION("IMX274 CMOS Image Sensor driver");
MODULE_LICENSE("GPL v2");
