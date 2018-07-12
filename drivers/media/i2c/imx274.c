/*
 * imx274.c - IMX274 CMOS Image Sensor driver
 *
 * Copyright (C) 2017, Leopard Imaging, Inc.
 *
 * Leon Luo <leonl@leopardimaging.com>
 * Edwin Zou <edwinz@leopardimaging.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
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

#define IMX274_DEFAULT_MODE			IMX274_MODE_3840X2160
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
 * Constants for sensor reset delay
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
#define IMX274_FRAME_LENGTH_ADDR_1		0x30FA /* VMAX, MSB */
#define IMX274_FRAME_LENGTH_ADDR_2		0x30F9 /* VMAX */
#define IMX274_FRAME_LENGTH_ADDR_3		0x30F8 /* VMAX, LSB */
#define IMX274_SVR_REG_MSB			0x300F /* SVR */
#define IMX274_SVR_REG_LSB			0x300E /* SVR */
#define IMX274_HMAX_REG_MSB			0x30F7 /* HMAX */
#define IMX274_HMAX_REG_LSB			0x30F6 /* HMAX */
#define IMX274_COARSE_TIME_ADDR_MSB		0x300D /* SHR */
#define IMX274_COARSE_TIME_ADDR_LSB		0x300C /* SHR */
#define IMX274_ANALOG_GAIN_ADDR_LSB		0x300A /* ANALOG GAIN LSB */
#define IMX274_ANALOG_GAIN_ADDR_MSB		0x300B /* ANALOG GAIN MSB */
#define IMX274_DIGITAL_GAIN_REG			0x3012 /* Digital Gain */
#define IMX274_VFLIP_REG			0x301A /* VERTICAL FLIP */
#define IMX274_TEST_PATTERN_REG			0x303D /* TEST PATTERN */
#define IMX274_STANDBY_REG			0x3000 /* STANDBY */

#define IMX274_TABLE_WAIT_MS			0
#define IMX274_TABLE_END			1

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

enum imx274_mode {
	IMX274_MODE_3840X2160,
	IMX274_MODE_1920X1080,
	IMX274_MODE_1280X720,

	IMX274_MODE_START_STREAM_1,
	IMX274_MODE_START_STREAM_2,
	IMX274_MODE_START_STREAM_3,
	IMX274_MODE_START_STREAM_4,
	IMX274_MODE_STOP_STREAM
};

/*
 * imx274 format related structure
 */
struct imx274_frmfmt {
	u32 mbus_code;
	enum v4l2_colorspace colorspace;
	struct v4l2_frmsize_discrete size;
	enum imx274_mode mode;
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
	"Horizontal Color Bars",
	"Vertical Color Bars",
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
	{0x3007, 0x02},

	{0x3018, 0xA2}, /* output XVS, HVS */

	{0x306B, 0x05},
	{0x30E2, 0x01},
	{0x30F6, 0x07}, /* HMAX, 263 */
	{0x30F7, 0x01}, /* HMAX */

	{0x30dd, 0x01}, /* crop to 2160 */
	{0x30de, 0x06},
	{0x30df, 0x00},
	{0x30e0, 0x12},
	{0x30e1, 0x00},
	{0x3037, 0x01}, /* to crop to 3840 */
	{0x3038, 0x0c},
	{0x3039, 0x00},
	{0x303a, 0x0c},
	{0x303b, 0x0f},

	{0x30EE, 0x01},
	{0x3130, 0x86},
	{0x3131, 0x08},
	{0x3132, 0x7E},
	{0x3133, 0x08},
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
	{0x3007, 0x11},

	{0x3018, 0xA2}, /* output XVS, HVS */

	{0x306B, 0x05},
	{0x30E2, 0x02},

	{0x30F6, 0x04}, /* HMAX, 260 */
	{0x30F7, 0x01}, /* HMAX */

	{0x30dd, 0x01}, /* to crop to 1920x1080 */
	{0x30de, 0x05},
	{0x30df, 0x00},
	{0x30e0, 0x04},
	{0x30e1, 0x00},
	{0x3037, 0x01},
	{0x3038, 0x0c},
	{0x3039, 0x00},
	{0x303a, 0x0c},
	{0x303b, 0x0f},

	{0x30EE, 0x01},
	{0x3130, 0x4E},
	{0x3131, 0x04},
	{0x3132, 0x46},
	{0x3133, 0x04},
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
	{0x3007, 0x09},

	{0x3018, 0xA2}, /* output XVS, HVS */

	{0x306B, 0x05},
	{0x30E2, 0x03},

	{0x30F6, 0x04}, /* HMAX, 260 */
	{0x30F7, 0x01}, /* HMAX */

	{0x30DD, 0x01},
	{0x30DE, 0x07},
	{0x30DF, 0x00},
	{0x40E0, 0x04},
	{0x30E1, 0x00},
	{0x3030, 0xD4},
	{0x3031, 0x02},
	{0x3032, 0xD0},
	{0x3033, 0x02},

	{0x30EE, 0x01},
	{0x3130, 0xE2},
	{0x3131, 0x02},
	{0x3132, 0xDE},
	{0x3133, 0x02},
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
 * imx274 first step register configuration for
 * starting stream
 */
static const struct reg_8 imx274_start_1[] = {
	{IMX274_STANDBY_REG, 0x12},
	{IMX274_TABLE_END, 0x00}
};

/*
 * imx274 second step register configuration for
 * starting stream
 */
static const struct reg_8 imx274_start_2[] = {
	{0x3120, 0xF0}, /* clock settings */
	{0x3121, 0x00}, /* clock settings */
	{0x3122, 0x02}, /* clock settings */
	{0x3129, 0x9C}, /* clock settings */
	{0x312A, 0x02}, /* clock settings */
	{0x312D, 0x02}, /* clock settings */

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
 * imx274 third step register configuration for
 * starting stream
 */
static const struct reg_8 imx274_start_3[] = {
	{IMX274_STANDBY_REG, 0x00},
	{0x303E, 0x02}, /* SYS_MODE = 2 */
	{IMX274_TABLE_END, 0x00}
};

/*
 * imx274 forth step register configuration for
 * starting stream
 */
static const struct reg_8 imx274_start_4[] = {
	{0x30F4, 0x00},
	{0x3018, 0xA2}, /* XHS VHS OUTUPT */
	{IMX274_TABLE_END, 0x00}
};

/*
 * imx274 register configuration for stoping stream
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

static const struct reg_8 *mode_table[] = {
	[IMX274_MODE_3840X2160]		= imx274_mode1_3840x2160_raw10,
	[IMX274_MODE_1920X1080]		= imx274_mode3_1920x1080_raw10,
	[IMX274_MODE_1280X720]		= imx274_mode5_1280x720_raw10,

	[IMX274_MODE_START_STREAM_1]	= imx274_start_1,
	[IMX274_MODE_START_STREAM_2]	= imx274_start_2,
	[IMX274_MODE_START_STREAM_3]	= imx274_start_3,
	[IMX274_MODE_START_STREAM_4]	= imx274_start_4,
	[IMX274_MODE_STOP_STREAM]	= imx274_stop,
};

/*
 * imx274 format related structure
 */
static const struct imx274_frmfmt imx274_formats[] = {
	{MEDIA_BUS_FMT_SRGGB10_1X10, V4L2_COLORSPACE_SRGB, {3840, 2160},
		IMX274_MODE_3840X2160},
	{MEDIA_BUS_FMT_SRGGB10_1X10, V4L2_COLORSPACE_SRGB, {1920, 1080},
		IMX274_MODE_1920X1080},
	{MEDIA_BUS_FMT_SRGGB10_1X10, V4L2_COLORSPACE_SRGB, {1280, 720},
		IMX274_MODE_1280X720},
};

/*
 * minimal frame length for each mode
 * refer to datasheet section "Frame Rate Adjustment (CSI-2)"
 */
static const int min_frame_len[] = {
	4550, /* mode 1, 4K */
	2310, /* mode 3, 1080p */
	2310 /* mode 5, 720p */
};

/*
 * minimal numbers of SHR register
 * refer to datasheet table "Shutter Setting (CSI-2)"
 */
static const int min_SHR[] = {
	12, /* mode 1, 4K */
	8, /* mode 3, 1080p */
	8 /* mode 5, 720p */
};

static const int max_frame_rate[] = {
	60, /* mode 1 , 4K */
	120, /* mode 3, 1080p */
	120 /* mode 5, 720p */
};

/*
 * Number of clocks per internal offset period
 * a constant based on mode
 * refer to section "Integration Time in Each Readout Drive Mode (CSI-2)"
 * in the datasheet
 * for the implemented 3 modes, it happens to be the same number
 */
static const int nocpiop[] = {
	112, /* mode 1 , 4K */
	112, /* mode 3, 1080p */
	112 /* mode 5, 720p */
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
 * @pd: Media pad structure
 * @client: Pointer to I2C client
 * @ctrls: imx274 control structure
 * @format: V4L2 media bus frame format structure
 * @frame_rate: V4L2 frame rate structure
 * @regmap: Pointer to regmap structure
 * @reset_gpio: Pointer to reset gpio
 * @lock: Mutex structure
 * @mode_index: Resolution mode index
 */
struct stimx274 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct i2c_client *client;
	struct imx274_ctrls ctrls;
	struct v4l2_mbus_framefmt format;
	struct v4l2_fract frame_interval;
	struct regmap *regmap;
	struct gpio_desc *reset_gpio;
	struct mutex lock; /* mutex lock for operations */
	u32 mode_index;
};

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
 * imx274_regmap_util_write_table_8 - Function for writing register table
 * @regmap: Pointer to device reg map structure
 * @table: Table containing register values
 * @wait_ms_addr: Flag for performing delay
 * @end_addr: Flag for incating end of table
 *
 * This is used to write register table into sensor's reg map.
 *
 * Return: 0 on success, errors otherwise
 */
static int imx274_regmap_util_write_table_8(struct regmap *regmap,
					    const struct reg_8 table[],
					    u16 wait_ms_addr, u16 end_addr)
{
	int err = 0;
	const struct reg_8 *next;
	u8 val;

	int range_start = -1;
	int range_count = 0;
	u8 range_vals[16];
	int max_range_vals = ARRAY_SIZE(range_vals);

	for (next = table;; next++) {
		if ((next->addr != range_start + range_count) ||
		    (next->addr == end_addr) ||
		    (next->addr == wait_ms_addr) ||
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
			if (next->addr == end_addr)
				break;

			if (next->addr == wait_ms_addr) {
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

static inline int imx274_read_reg(struct stimx274 *priv, u16 addr, u8 *val)
{
	int err;

	err = regmap_read(priv->regmap, addr, (unsigned int *)val);
	if (err)
		dev_err(&priv->client->dev,
			"%s : i2c read failed, addr = %x\n", __func__, addr);
	else
		dev_dbg(&priv->client->dev,
			"%s : addr 0x%x, val=0x%x\n", __func__,
			addr, *val);
	return err;
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

static int imx274_write_table(struct stimx274 *priv, const struct reg_8 table[])
{
	return imx274_regmap_util_write_table_8(priv->regmap,
		table, IMX274_TABLE_WAIT_MS, IMX274_TABLE_END);
}

/*
 * imx274_mode_regs - Function for set mode registers per mode index
 * @priv: Pointer to device structure
 * @mode: Mode index value
 *
 * This is used to start steam per mode index.
 * mode = 0, start stream for sensor Mode 1: 4K/raw10
 * mode = 1, start stream for sensor Mode 3: 1080p/raw10
 * mode = 2, start stream for sensor Mode 5: 720p/raw10
 *
 * Return: 0 on success, errors otherwise
 */
static int imx274_mode_regs(struct stimx274 *priv, int mode)
{
	int err = 0;

	err = imx274_write_table(priv, mode_table[IMX274_MODE_START_STREAM_1]);
	if (err)
		return err;

	err = imx274_write_table(priv, mode_table[IMX274_MODE_START_STREAM_2]);
	if (err)
		return err;

	err = imx274_write_table(priv, mode_table[mode]);

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

	/*
	 * Refer to "Standby Cancel Sequence when using CSI-2" in
	 * imx274 datasheet, it should wait 10ms or more here.
	 * give it 1 extra ms for margin
	 */
	msleep_range(11);
	err = imx274_write_table(priv, mode_table[IMX274_MODE_START_STREAM_3]);
	if (err)
		return err;

	/*
	 * Refer to "Standby Cancel Sequence when using CSI-2" in
	 * imx274 datasheet, it should wait 7ms or more here.
	 * give it 1 extra ms for margin
	 */
	msleep_range(8);
	err = imx274_write_table(priv, mode_table[IMX274_MODE_START_STREAM_4]);
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

	return ret;
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
	struct i2c_client *client = imx274->client;
	int index;

	dev_dbg(&client->dev,
		"%s: width = %d height = %d code = %d mbus_code = %d\n",
		__func__, fmt->width, fmt->height, fmt->code,
		imx274_formats[imx274->mode_index].mbus_code);

	mutex_lock(&imx274->lock);

	for (index = 0; index < ARRAY_SIZE(imx274_formats); index++) {
		if (imx274_formats[index].size.width == fmt->width &&
		    imx274_formats[index].size.height == fmt->height)
			break;
	}

	if (index >= ARRAY_SIZE(imx274_formats)) {
		/* default to first format */
		index = 0;
	}

	imx274->mode_index = index;

	if (fmt->width > IMX274_MAX_WIDTH)
		fmt->width = IMX274_MAX_WIDTH;
	if (fmt->height > IMX274_MAX_HEIGHT)
		fmt->height = IMX274_MAX_HEIGHT;
	fmt->width = fmt->width & (~IMX274_MASK_LSB_2_BITS);
	fmt->height = fmt->height & (~IMX274_MASK_LSB_2_BITS);
	fmt->field = V4L2_FIELD_NONE;

	if (format->which == V4L2_SUBDEV_FORMAT_TRY)
		cfg->try_fmt = *fmt;
	else
		imx274->format = *fmt;

	mutex_unlock(&imx274->lock);
	return 0;
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
		/*
		 * exposure time range is decided by frame interval
		 * need to update it after frame interal changes
		 */
		min = IMX274_MIN_EXPOSURE_TIME;
		max = fi->interval.numerator * 1000000
			/ fi->interval.denominator;
		def = max;
		if (__v4l2_ctrl_modify_range(ctrl, min, max, 1, def)) {
			dev_err(&imx274->client->dev,
				"Exposure ctrl range update failed\n");
			goto unlock;
		}

		/* update exposure time accordingly */
		imx274_set_exposure(imx274, imx274->ctrls.exposure->val);

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
static int imx274_load_default(struct stimx274 *priv)
{
	int ret;

	/* load default control values */
	priv->frame_interval.numerator = 1;
	priv->frame_interval.denominator = IMX274_DEF_FRAME_RATE;
	priv->ctrls.exposure->val = 1000000 / IMX274_DEF_FRAME_RATE;
	priv->ctrls.gain->val = IMX274_DEF_GAIN;
	priv->ctrls.vflip->val = 0;
	priv->ctrls.test_pattern->val = TEST_PATTERN_DISABLED;

	/* update frame rate */
	ret = imx274_set_frame_interval(priv,
					priv->frame_interval);
	if (ret)
		return ret;

	/* update exposure time */
	ret = v4l2_ctrl_s_ctrl(priv->ctrls.exposure, priv->ctrls.exposure->val);
	if (ret)
		return ret;

	/* update gain */
	ret = v4l2_ctrl_s_ctrl(priv->ctrls.gain, priv->ctrls.gain->val);
	if (ret)
		return ret;

	/* update vflip */
	ret = v4l2_ctrl_s_ctrl(priv->ctrls.vflip, priv->ctrls.vflip->val);
	if (ret)
		return ret;

	return 0;
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

	dev_dbg(&imx274->client->dev, "%s : %s, mode index = %d\n", __func__,
		on ? "Stream Start" : "Stream Stop", imx274->mode_index);

	mutex_lock(&imx274->lock);

	if (on) {
		/* load mode registers */
		ret = imx274_mode_regs(imx274, imx274->mode_index);
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

		/* update exposure time */
		ret = __v4l2_ctrl_s_ctrl(imx274->ctrls.exposure,
					 imx274->ctrls.exposure->val);
		if (ret)
			goto fail;

		/* start stream */
		ret = imx274_start_stream(imx274);
		if (ret)
			goto fail;
	} else {
		/* stop stream */
		ret = imx274_write_table(imx274,
					 mode_table[IMX274_MODE_STOP_STREAM]);
		if (ret)
			goto fail;
	}

	mutex_unlock(&imx274->lock);
	dev_dbg(&imx274->client->dev,
		"%s : Done: mode = %d\n", __func__, imx274->mode_index);
	return 0;

fail:
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
	u16 svr;
	u32 vmax;
	u8 reg_val[3];

	/* svr */
	err = imx274_read_reg(priv, IMX274_SVR_REG_LSB, &reg_val[0]);
	if (err)
		goto fail;

	err = imx274_read_reg(priv, IMX274_SVR_REG_MSB, &reg_val[1]);
	if (err)
		goto fail;

	svr = (reg_val[1] << IMX274_SHIFT_8_BITS) + reg_val[0];

	/* vmax */
	err = imx274_read_reg(priv, IMX274_FRAME_LENGTH_ADDR_3, &reg_val[0]);
	if (err)
		goto fail;

	err = imx274_read_reg(priv, IMX274_FRAME_LENGTH_ADDR_2, &reg_val[1]);
	if (err)
		goto fail;

	err = imx274_read_reg(priv, IMX274_FRAME_LENGTH_ADDR_1, &reg_val[2]);
	if (err)
		goto fail;

	vmax = ((reg_val[2] & IMX274_MASK_LSB_3_BITS) << IMX274_SHIFT_16_BITS)
		+ (reg_val[1] << IMX274_SHIFT_8_BITS) + reg_val[0];

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

	if (*frame_length < min_frame_len[priv->mode_index])
		*frame_length = min_frame_len[priv->mode_index];

	*val = *frame_length - *val; /* convert to raw shr */
	if (*val > *frame_length - IMX274_SHR_LIMIT_CONST)
		*val = *frame_length - IMX274_SHR_LIMIT_CONST;
	else if (*val < min_SHR[priv->mode_index])
		*val = min_SHR[priv->mode_index];

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

static inline void imx274_calculate_gain_regs(struct reg_8 regs[2], u16 gain)
{
	regs->addr = IMX274_ANALOG_GAIN_ADDR_MSB;
	regs->val = (gain >> IMX274_SHIFT_8_BITS) & IMX274_MASK_LSB_3_BITS;

	(regs + 1)->addr = IMX274_ANALOG_GAIN_ADDR_LSB;
	(regs + 1)->val = (gain) & IMX274_MASK_LSB_8_BITS;
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
	struct reg_8 reg_list[2];
	int err;
	u32 gain, analog_gain, digital_gain, gain_reg;
	int i;

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

	imx274_calculate_gain_regs(reg_list, (u16)gain_reg);

	for (i = 0; i < ARRAY_SIZE(reg_list); i++) {
		err = imx274_write_reg(priv, reg_list[i].addr,
				       reg_list[i].val);
		if (err)
			goto fail;
	}

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

static inline void imx274_calculate_coarse_time_regs(struct reg_8 regs[2],
						     u32 coarse_time)
{
	regs->addr = IMX274_COARSE_TIME_ADDR_MSB;
	regs->val = (coarse_time >> IMX274_SHIFT_8_BITS)
			& IMX274_MASK_LSB_8_BITS;
	(regs + 1)->addr = IMX274_COARSE_TIME_ADDR_LSB;
	(regs + 1)->val = (coarse_time) & IMX274_MASK_LSB_8_BITS;
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
	struct reg_8 reg_list[2];
	int err;
	u32 coarse_time, frame_length;
	int i;

	coarse_time = *val;

	/* convert exposure_time to appropriate SHR value */
	err = imx274_clamp_coarse_time(priv, &coarse_time, &frame_length);
	if (err)
		goto fail;

	/* prepare SHR registers */
	imx274_calculate_coarse_time_regs(reg_list, coarse_time);

	/* write to SHR registers */
	for (i = 0; i < ARRAY_SIZE(reg_list); i++) {
		err = imx274_write_reg(priv, reg_list[i].addr,
				       reg_list[i].val);
		if (err)
			goto fail;
	}

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
	u16 hmax;
	u8 reg_val[2];
	u32 coarse_time; /* exposure time in unit of line (HMAX)*/

	dev_dbg(&priv->client->dev,
		"%s : EXPOSURE control input = %d\n", __func__, val);

	/* step 1: convert input exposure_time (val) into number of 1[HMAX] */

	/* obtain HMAX value */
	err = imx274_read_reg(priv, IMX274_HMAX_REG_LSB, &reg_val[0]);
	if (err)
		goto fail;
	err = imx274_read_reg(priv, IMX274_HMAX_REG_MSB, &reg_val[1]);
	if (err)
		goto fail;
	hmax = (reg_val[1] << IMX274_SHIFT_8_BITS) + reg_val[0];
	if (hmax == 0) {
		err = -EINVAL;
		goto fail;
	}

	coarse_time = (IMX274_PIXCLK_CONST1 / IMX274_PIXCLK_CONST2 * val
			- nocpiop[priv->mode_index]) / hmax;

	/* step 2: convert exposure_time into SHR value */

	/* set SHR */
	err = imx274_set_coarse_time(priv, &coarse_time);
	if (err)
		goto fail;

	priv->ctrls.exposure->val =
			(coarse_time * hmax + nocpiop[priv->mode_index])
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

static inline void imx274_calculate_frame_length_regs(struct reg_8 regs[3],
						      u32 frame_length)
{
	regs->addr = IMX274_FRAME_LENGTH_ADDR_1;
	regs->val = (frame_length >> IMX274_SHIFT_16_BITS)
			& IMX274_MASK_LSB_4_BITS;
	(regs + 1)->addr = IMX274_FRAME_LENGTH_ADDR_2;
	(regs + 1)->val = (frame_length >> IMX274_SHIFT_8_BITS)
			& IMX274_MASK_LSB_8_BITS;
	(regs + 2)->addr = IMX274_FRAME_LENGTH_ADDR_3;
	(regs + 2)->val = (frame_length) & IMX274_MASK_LSB_8_BITS;
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
	struct reg_8 reg_list[3];
	int err;
	u32 frame_length;
	int i;

	dev_dbg(&priv->client->dev, "%s : input length = %d\n",
		__func__, val);

	frame_length = (u32)val;

	imx274_calculate_frame_length_regs(reg_list, frame_length);
	for (i = 0; i < ARRAY_SIZE(reg_list); i++) {
		err = imx274_write_reg(priv, reg_list[i].addr,
				       reg_list[i].val);
		if (err)
			goto fail;
	}

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
	u16 svr;
	u16 hmax;
	u8 reg_val[2];

	dev_dbg(&priv->client->dev, "%s: input frame interval = %d / %d",
		__func__, frame_interval.numerator,
		frame_interval.denominator);

	if (frame_interval.numerator == 0) {
		err = -EINVAL;
		goto fail;
	}

	req_frame_rate = (u32)(frame_interval.denominator
				/ frame_interval.numerator);

	/* boundary check */
	if (req_frame_rate > max_frame_rate[priv->mode_index]) {
		frame_interval.numerator = 1;
		frame_interval.denominator =
					max_frame_rate[priv->mode_index];
	} else if (req_frame_rate < IMX274_MIN_FRAME_RATE) {
		frame_interval.numerator = 1;
		frame_interval.denominator = IMX274_MIN_FRAME_RATE;
	}

	/*
	 * VMAX = 1/frame_rate x 72M / (SVR+1) / HMAX
	 * frame_length (i.e. VMAX) = (frame_interval) x 72M /(SVR+1) / HMAX
	 */

	/* SVR */
	err = imx274_read_reg(priv, IMX274_SVR_REG_LSB, &reg_val[0]);
	if (err)
		goto fail;
	err = imx274_read_reg(priv, IMX274_SVR_REG_MSB, &reg_val[1]);
	if (err)
		goto fail;
	svr = (reg_val[1] << IMX274_SHIFT_8_BITS) + reg_val[0];
	dev_dbg(&priv->client->dev,
		"%s : register SVR = %d\n", __func__, svr);

	/* HMAX */
	err = imx274_read_reg(priv, IMX274_HMAX_REG_LSB, &reg_val[0]);
	if (err)
		goto fail;
	err = imx274_read_reg(priv, IMX274_HMAX_REG_MSB, &reg_val[1]);
	if (err)
		goto fail;
	hmax = (reg_val[1] << IMX274_SHIFT_8_BITS) + reg_val[0];
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

static int imx274_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct v4l2_subdev *sd;
	struct stimx274 *imx274;
	int ret;

	/* initialize imx274 */
	imx274 = devm_kzalloc(&client->dev, sizeof(*imx274), GFP_KERNEL);
	if (!imx274)
		return -ENOMEM;

	mutex_init(&imx274->lock);

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
	strlcpy(sd->name, DRIVER_NAME, sizeof(sd->name));
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

	/* pull sensor out of reset */
	imx274_reset(imx274, 1);

	/* initialize controls */
	ret = v4l2_ctrl_handler_init(&imx274->ctrls.handler, 2);
	if (ret < 0) {
		dev_err(&client->dev,
			"%s : ctrl handler init Failed\n", __func__);
		goto err_me;
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

	/* setup default controls */
	ret = v4l2_ctrl_handler_setup(&imx274->ctrls.handler);
	if (ret) {
		dev_err(&client->dev,
			"Error %d setup default controls\n", ret);
		goto err_ctrls;
	}

	/* initialize format */
	imx274->mode_index = IMX274_MODE_3840X2160;
	imx274->format.width = imx274_formats[0].size.width;
	imx274->format.height = imx274_formats[0].size.height;
	imx274->format.field = V4L2_FIELD_NONE;
	imx274->format.code = MEDIA_BUS_FMT_SRGGB10_1X10;
	imx274->format.colorspace = V4L2_COLORSPACE_SRGB;
	imx274->frame_interval.numerator = 1;
	imx274->frame_interval.denominator = IMX274_DEF_FRAME_RATE;

	/* load default control values */
	ret = imx274_load_default(imx274);
	if (ret) {
		dev_err(&client->dev,
			"%s : imx274_load_default failed %d\n",
			__func__, ret);
		goto err_ctrls;
	}

	/* register subdevice */
	ret = v4l2_async_register_subdev(sd);
	if (ret < 0) {
		dev_err(&client->dev,
			"%s : v4l2_async_register_subdev failed %d\n",
			__func__, ret);
		goto err_ctrls;
	}

	dev_info(&client->dev, "imx274 : imx274 probe success !\n");
	return 0;

err_ctrls:
	v4l2_ctrl_handler_free(&imx274->ctrls.handler);
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

	/* stop stream */
	imx274_write_table(imx274, mode_table[IMX274_MODE_STOP_STREAM]);

	v4l2_async_unregister_subdev(sd);
	v4l2_ctrl_handler_free(&imx274->ctrls.handler);
	media_entity_cleanup(&sd->entity);
	mutex_destroy(&imx274->lock);
	return 0;
}

static struct i2c_driver imx274_i2c_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.of_match_table	= imx274_of_id_table,
	},
	.probe		= imx274_probe,
	.remove		= imx274_remove,
	.id_table	= imx274_id,
};

module_i2c_driver(imx274_i2c_driver);

MODULE_AUTHOR("Leon Luo <leonl@leopardimaging.com>");
MODULE_DESCRIPTION("IMX274 CMOS Image Sensor driver");
MODULE_LICENSE("GPL v2");
