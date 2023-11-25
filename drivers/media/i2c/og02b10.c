// SPDX-License-Identifier: GPL-2.0
/*
 * og02b10 driver
 *
 * Copyright (C) 2023 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X00 first version.
 */
//#define DEBUG

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/rk-camera-module.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>
#include <linux/rk-preisp.h>
#include "../platform/rockchip/isp/rkisp_tb_helper.h"

#include <linux/of_gpio.h>
#include <dt-bindings/gpio/gpio.h>
#include <linux/gpio.h>

#define DRIVER_VERSION KERNEL_VERSION(0, 0x01, 0x00)

/* The base for the og driver controls.
 * We reserve 16 controls for this driver.
 */
#define V4L2_CID_USER_OG_BASE			(V4L2_CID_USER_BASE + 0x10c0)
#define V4L2_CID_OG_LOW_POWER_MODE		(V4L2_CID_USER_OG_BASE + 0)

#define OG02B10_LANES				2
#define MIPI_FREQ_400M				400000000
#define PIXEL_RATE_WITH_400M			(MIPI_FREQ_400M * 2 / 10 * OG02B10_LANES)
#define OG02B10_XVCLK_FREQ			24000000

#define OG02B10_SYS_CLK				80000000
#define OG02B10_VTS_MAX				0x14DDE

#define OG02B10_CHIP_ID0			0x20
#define OG02B10_CHIP_ID1			0xC0
#define OG02B10_CHIP_ID2			0xE0
#define OG02B10_REG_SC_SCCB_ID0			0x0107
#define OG02B10_REG_SC_SCCB_ID1			0x0109
#define OG02B10_REG_SC_SCCB_ID2			0x302B

#define OG02B10_GAIN_MIN			16
#define OG02B10_GAIN_MAX			988
#define OG02B10_GAIN_STEP			1
#define OG02B10_GAIN_DEFAULT			0x80

#define OG02B10_EXPOSURE_MIN			1
#define OG02B10_EXPOSURE_STEP			1

#define OG02B10_REG_GROUP_HOLD			0x3208
#define OG02B10_GROUP0_HOLD_START		0x00
#define OG02B10_GROUP0_HOLD_END			0x10
#define OG02B10_GROUP_LAUNCH			0xA0

#define OG02B10_REG_EXP_H			0x3501
#define OG02B10_REG_EXP_L			0x3502

#define OG02B10_REG_HTS_H			0x380C
#define OG02B10_REG_HTS_L			0x380D
#define OG02B10_REG_VTS_H			0x380E
#define OG02B10_REG_VTS_L			0x380F

#define OG02B10_REG_AGAIN_COARSE		0x3508
#define OG02B10_REG_AGAIN_FINE			0x3509

#define OG02B10_REG_DGAIN_COARSE		0x350A
#define OG02B10_REG_DGAIN_FINE_H		0x350B
#define OG02B10_REG_DGAIN_FINE_L		0x350C

#define OG02B10_REG_CTRL_MODE			0x0100
#define OG02B10_MODE_SW_STANDBY			0x0
#define OG02B10_MODE_STREAMING			BIT(0)

#define OG02B10_REG_SOFTWARE_RESET		0x0103
#define OG02B10_SOFTWARE_RESET_VAL		0x1

#define OG02B10_REG_TEST_PATTERN		0x5E00
#define OG02B10_TEST_PATTERN_DISABLE_VAL	0x00
#define OG02B10_TEST_PATTERN_ENABLE_VAL		0x80

#define OG02B10_REG_FILP			0x3820
#define OG02B10_REG_MIRROR			0x3821
#define OG02B10_FILP_ENABLE_VAL			0x44
#define OG02B10_FILP_DISABLE_VAL		0x00
#define OG02B10_MIRROR_ENABLE_VAL		0x04
#define OG02B10_MIRROR_DISABLE_VAL		0x00


//disable FSIN output, bit operation, set 0x3006[1]=0
//enable  FSIN output, bit operation, set 0x3006[1]=1
#define OG02B10_REG_IO_PAD_OUT_EN		0x3006
#define OG02B10_IO_PAD_OUT_EN_DEFAULT		0x0a
#define OG02B10_FSIN_OUTPUT_ENABLE		BIT(1)

//r_frame_on_num(active frames after wake-up)
#define OG02B10_REG_R_FRAME_ON_NUM		0x303F

#define OG02B10_REG_SCCB_SEL			0x31FF
#define OG02B10_CLOCKLESS_SCCB			0X0

#define OG02B10_REG_LOW_POWER_MODE_CTRL		0x3030
#define OG02B10_LOW_FRAME_RATE_VAL		BIT(4)
#define OG02B10_EXTERNAL_TRIGGER_VAL		BIT(2)

#define OG02B10_REG_POWER_CTRL_OPTIONS		0x4F00
#define OG02B10_NORMAL_MODE			0
#define OG02B10_LOW_POWER_MODE			1

#define OG02B10_NAME				"og02b10"

#define OF_CAMERA_HDR_MODE			"rockchip,camera-hdr-mode"
#define OF_CAMERA_PINCTRL_STATE_DEFAULT		"default"
#define OF_CAMERA_PINCTRL_STATE_MCLK		"mclk"

#define REG_NULL				0xFFFF

static const char * const OG02B10_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OG02B10_NUM_SUPPLIES ARRAY_SIZE(OG02B10_supply_names)

enum og02b10_low_power_mode_em {
	LOW_POWER_MODE_DISABLE = 0,
	LOW_FRAME_RATE_MODE,
	INTERNAL_TRIGGER_MODE,
	EXTERNAL_TRIGGER_MODE,
	LOW_POWER_MODE_MAX
};

struct regval {
	u16 addr;
	u8 val;
};

struct og02b10_mode {
	u32 bus_fmt;
	u32 width;
	u32 height;
	struct v4l2_fract max_fps;
	u32 hts_def;
	u32 vts_def;
	u32 exp_def;
	const struct regval *reg_list;
	u32 hdr_mode;
	u32 vc[PAD_MAX];
};

struct og02b10 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OG02B10_NUM_SUPPLIES];

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_mclk;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*pixel_rate;
	struct v4l2_ctrl	*link_freq;
	struct v4l2_ctrl	*h_flip;
	struct v4l2_ctrl	*v_flip;
	struct v4l2_ctrl	*test_pattern;
	struct v4l2_ctrl	*low_power_mode;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	bool			slave_mode;

	const struct og02b10_mode *cur_mode;
	u32			cfg_num;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};

#define to_og02b10(sd) container_of(sd, struct og02b10, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval og02b10_linear10bit_1600x1200_regs[] = {
	// SOFTWARE RESET(0:off, 1:on)
	{0x0103, 0x01},
	// MODE SELECT(0:software_standby, 1:streaming)
	{0x0100, 0x00},
	//ENTER SAFE MODE(0:Keep, 1:Enter safe mode)
	{0x010c, 0x02},
	//ENTER SOFTWARE STANDBY(0:Keep, 1:Enter software standby)
	{0x010b, 0x01},

	// PLL control
	{0x0300, 0x01},
	{0x0302, 0x32},
	{0x0303, 0x00},
	{0x0304, 0x03},
	{0x0305, 0x02},
	{0x0306, 0x01},
	{0x030d, 0x5a},
	{0x030e, 0x04},

	// SC registers
	{0x3001, 0x02},
	{0x3004, 0x00},
	{0x3005, 0x00},
	{0x3006, 0x0a},
	{0x3011, 0x0d},
	{0x3014, 0x04},
	{0x301c, 0xf0},
	{0x3020, 0x20},
	{0x302c, 0x00},
	{0x302d, 0x00},
	{0x302e, 0x00},
	{0x302f, 0x03},
	{0x3030, 0x10},

	{0x303f, 0x03},

	// SCCB control registers
	{0x3103, 0x00},
	{0x3106, 0x08},
	{0x31ff, 0x01},

	//aec_pk registers
	{0x3501, 0x05},
	{0x3502, 0x7c},
	{0x3506, 0x00},
	{0x3507, 0x00},

	//ana control registers
	{0x3620, 0x67},
	{0x3633, 0x78},
	{0x3662, 0x65},
	{0x3664, 0xb0},
	{0x3666, 0x70},
	{0x3670, 0x68},
	{0x3674, 0x10},
	{0x3675, 0x00},
	{0x367e, 0x90},
	{0x3680, 0x84},
	{0x3683, 0x96},
	{0x36a2, 0x04},
	{0x36a3, 0x80},
	{0x36b0, 0x00},

	//sensor control registers
	{0x3700, 0x35},
	{0x3704, 0x39},
	{0x370a, 0x50},
	{0x3712, 0x00},
	{0x3713, 0x02},
	{0x3778, 0x00},
	{0x379b, 0x01},
	{0x379c, 0x10},

	//timing control registers
	// horizontal start
	{0x3800, 0x00},
	{0x3801, 0x00},
	// vertical start
	{0x3802, 0x00},
	{0x3803, 0x00},
	// horizontal end
	{0x3804, 0x06},
	{0x3805, 0x4f},
	// vertical end
	{0x3806, 0x05},
	{0x3807, 0x23},
	// ISP output
	{0x3808, 0x06},
	{0x3809, 0x40},
	{0x380a, 0x04},
	{0x380b, 0xb0},
	// HTS VTS
	{0x380c, 0x03},
	{0x380d, 0xa8},
	{0x380e, 0x0b},
	{0x380f, 0x10},
	// ISP X/Y WIN
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x08},
	// X/Y odd/even
	{0x3814, 0x11},
	{0x3815, 0x11},
	// HSYNC start/end
	{0x3816, 0x00},
	{0x3817, 0x01},
	{0x3818, 0x00},
	{0x3819, 0x05},
	// vflip
	{0x3820, 0x00},
	{0x3821, 0x00},
	// grp_wr_start/grp_wr_start
	{0x382b, 0x32},
	// hts_global_tx
	{0x382c, 0x0a},
	{0x382d, 0xf8},

	//global shutter control registers
	{0x3881, 0x44},
	{0x3882, 0x02},
	{0x3883, 0x8c},
	{0x3885, 0x07},
	{0x389d, 0x03},
	{0x38a6, 0x00},
	{0x38a7, 0x01},
	{0x38b3, 0x07},
	{0x38b1, 0x00},
	{0x38e5, 0x02},
	{0x38e7, 0x00},
	{0x38e8, 0x00},
	{0x3910, 0xff},
	{0x3911, 0xff},
	{0x3912, 0x08},
	{0x3913, 0x00},
	{0x3914, 0x00},
	{0x3915, 0x00},
	{0x391c, 0x00},
	{0x3920, 0xff},
	{0x3921, 0x80},
	{0x3922, 0x00},
	{0x3923, 0x00},
	{0x3924, 0x05},
	{0x3925, 0x00},
	{0x3926, 0x00},
	{0x3927, 0x00},
	{0x3928, 0x1a},
	{0x392d, 0x03},
	{0x392e, 0xa8},
	{0x392f, 0x08},

	//BLC control registers
	{0x4001, 0x00},
	{0x4003, 0x40},
	{0x4008, 0x04},
	{0x4009, 0x1b},
	{0x400c, 0x04},
	{0x400d, 0x1b},
	{0x4010, 0xf4},
	{0x4011, 0x00},
	{0x4016, 0x00},
	{0x4017, 0x04},
	{0x4042, 0x11},
	{0x4043, 0x70},
	{0x4045, 0x00},

	// TPM registers
	{0x4409, 0x5f},

	// column_sync registers
	{0x4509, 0x00},
	{0x450b, 0x00},

	// VFIFO registers
	{0x4600, 0x00},
	{0x4601, 0xa0},

	// DVP registers
	{0x4708, 0x09},
	{0x470c, 0x81},
	{0x4710, 0x06},
	{0x4711, 0x00},

	// MIPI control registers
	{0x4800, 0x00},
	{0x481f, 0x30},
	{0x4837, 0x14},

	// PSV control registers
	{0x4f00, 0x00},
	{0x4f07, 0x00},
	{0x4f08, 0x03},
	{0x4f09, 0x08},
	{0x4f0c, 0x05},
	{0x4f0d, 0xb4},
	{0x4f10, 0x00},
	{0x4f11, 0x00},
	{0x4f12, 0x07},
	{0x4f13, 0xe2},

	// isp_main control registers
	{0x5000, 0x1f},
	{0x5001, 0x20},
	{0x5026, 0x00},

	// isp_otp_dpc control registers
	{0x5c00, 0x00},
	{0x5c01, 0x2c},
	{0x5c02, 0x00},
	{0x5c03, 0x7f},

	// isp_pre control registers
	{0x5e00, 0x00},
	{0x5e01, 0x41},

	// GLOBAL_SHUTTER_CTRL_31
	{0x38b1, 0x03},

	{REG_NULL, 0x00},
};

/*
 * The width and height must be configured to be
 * the same as the current output resolution of the sensor.
 * The input width of the isp needs to be 16 aligned.
 * The input height of the isp needs to be 8 aligned.
 * If the width or height does not meet the alignment rules,
 * you can configure the cropping parameters with the following function to
 * crop out the appropriate resolution.
 * struct v4l2_subdev_pad_ops {
 *    .get_selection
 * }
 */
static const struct og02b10_mode supported_modes[] = {
	{
		.bus_fmt = MEDIA_BUS_FMT_SBGGR10_1X10,
		.width = 1600,
		.height = 1200,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		/*
		 * line_time = h_size / pclk
		 *	    = hts_def  / 80Mhz = 0x3a8 / 80Mhz = 0.0117ms
		 *
		 * exposure time = int_t * line_time
		 *		= exp_def * line_time
		 *		= 0x02ea * 0.0117ms
		 *		= 8.7282ms
		 * max exposure time = vts_def * line_time
		 *		    = 0x0b10 * 0.0117ms
		 *		    = 33.1344ms
		 */
		.exp_def = 0x02ea,
		.hts_def = 0x03a8,
		.vts_def = 0x0b10,
		.reg_list = og02b10_linear10bit_1600x1200_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	},
};

static const s64 link_freq_menu_items[] = {
	MIPI_FREQ_400M,
};

static int __og02b10_power_on(struct og02b10 *og02b10);

static int og02b10_check_sensor_id(struct og02b10 *og02b10,
				   struct i2c_client *client);

/* sensor register write */
static int og02b10_write_reg(struct i2c_client *client, u16 reg, u8 val)
{
	struct i2c_msg msg;
	u8 buf[3];
	int ret;

	buf[0] = (reg >> 8) & 0xFF;
	buf[1] = reg & 0xFF;
	buf[2] = val;

	msg.addr = client->addr;
	msg.flags = client->flags;
	msg.buf = buf;
	msg.len = sizeof(buf);

	ret = i2c_transfer(client->adapter, &msg, 1);
	if (ret >= 0)
		return 0;

	dev_err(&client->dev,
		"og02b10 write reg(0x%x val:0x%x) failed !\n", reg, val);

	return ret;
}

static int og02b10_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	int i, ret = 0;

	i = 0;
	while (regs[i].addr != REG_NULL) {
		ret = og02b10_write_reg(client, regs[i].addr, regs[i].val);
		if (ret) {
			dev_err(&client->dev, "%s failed !\n", __func__);
			break;
		}
		i++;
	}

	return ret;
}

/* sensor register read */
static int og02b10_read_reg(struct i2c_client *client, u16 reg, u8 *val)
{
	struct i2c_msg msg[2];
	u8 buf[2];
	int ret;

	buf[0] = (reg >> 8) & 0xFF;
	buf[1] = reg & 0xFF;

	msg[0].addr = client->addr;
	msg[0].flags = client->flags;
	msg[0].buf = buf;
	msg[0].len = sizeof(buf);

	msg[1].addr = client->addr;
	msg[1].flags = client->flags | I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = 1;

	ret = i2c_transfer(client->adapter, msg, 2);
	if (ret >= 0) {
		*val = buf[0];
		return 0;
	}

	dev_err(&client->dev,
		"og02b10 read reg(0x%x val:0x%x) failed !\n", reg, *val);

	return ret;
}

static int og02b10_get_reso_dist(const struct og02b10_mode *mode,
				 struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
		abs(mode->height - framefmt->height);
}

static const struct og02b10_mode *
og02b10_find_best_fit(struct og02b10 *og02b10, struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < og02b10->cfg_num; i++) {
		dist = og02b10_get_reso_dist(&supported_modes[i], framefmt);
		if ((cur_best_fit_dist == -1 || dist <= cur_best_fit_dist) &&
		    (supported_modes[i].bus_fmt == framefmt->code)) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int og02b10_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct og02b10 *og02b10 = to_og02b10(sd);
	const struct og02b10_mode *mode;
	s64 h_blank, v_blank;
	u64 dst_link_freq = 0;
	u64 dst_pixel_rate = 0;

	mutex_lock(&og02b10->mutex);
	mode = og02b10_find_best_fit(og02b10, fmt);
	fmt->format.code = mode->bus_fmt;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&og02b10->mutex);
		return -ENOTTY;
#endif
	} else {
		og02b10->cur_mode = mode;
		h_blank = mode->hts_def;
		__v4l2_ctrl_modify_range(og02b10->hblank, h_blank,
					 h_blank, 1, h_blank);

		v_blank = mode->vts_def;
		__v4l2_ctrl_modify_range(og02b10->vblank, v_blank,
					 v_blank, 1, v_blank);
		if (mode->hdr_mode == NO_HDR) {
			if (mode->bus_fmt == MEDIA_BUS_FMT_SBGGR10_1X10) {
				dst_link_freq = 0;
				dst_pixel_rate = PIXEL_RATE_WITH_400M;
			}
		}
		__v4l2_ctrl_s_ctrl_int64(og02b10->pixel_rate,
					 dst_pixel_rate);
		__v4l2_ctrl_s_ctrl(og02b10->link_freq,
				   dst_link_freq);
	}

	mutex_unlock(&og02b10->mutex);

	return 0;
}

static int og02b10_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct og02b10 *og02b10 = to_og02b10(sd);
	const struct og02b10_mode *mode = og02b10->cur_mode;

	mutex_lock(&og02b10->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		v4l2_info(sd, "get format try.\n");
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&og02b10->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = mode->bus_fmt;
		fmt->format.field = V4L2_FIELD_NONE;
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&og02b10->mutex);

	v4l2_info(sd, "get format, width: %d, height: %d, code: %d, field: %d.\n",
		  fmt->format.width, fmt->format.height,
		  fmt->format.code, fmt->format.field);

	return 0;
}

static int og02b10_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	struct og02b10 *og02b10 = to_og02b10(sd);

	if (code->index != 0)
		return -EINVAL;
	code->code = og02b10->cur_mode->bus_fmt;

	return 0;
}

static int og02b10_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	struct og02b10 *og02b10 = to_og02b10(sd);

	if (fse->index >= og02b10->cfg_num)
		return -EINVAL;

	if (fse->code != supported_modes[fse->index].bus_fmt)
		return -EINVAL;

	fse->min_width	= supported_modes[fse->index].width;
	fse->max_width	= supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int og02b10_g_frame_interval(struct v4l2_subdev *sd,
				    struct v4l2_subdev_frame_interval *fi)
{
	struct og02b10 *og02b10 = to_og02b10(sd);
	const struct og02b10_mode *mode = og02b10->cur_mode;

	fi->interval = mode->max_fps;

	return 0;
}

static int og02b10_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad,
		struct v4l2_mbus_config *config)
{
	struct og02b10 *og02b10 = to_og02b10(sd);
	const struct og02b10_mode *mode = og02b10->cur_mode;
	u32 val = 0;

	if (mode->hdr_mode == NO_HDR)
		val = 1 << (OG02B10_LANES - 1) |
		      V4L2_MBUS_CSI2_CHANNEL_0 |
		      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	if (mode->hdr_mode == HDR_X2)
		val = 1 << (OG02B10_LANES - 1) |
		      V4L2_MBUS_CSI2_CHANNEL_0 |
		      V4L2_MBUS_CSI2_CONTINUOUS_CLOCK |
		      V4L2_MBUS_CSI2_CHANNEL_1;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void og02b10_get_module_inf(struct og02b10 *og02b10,
		  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strscpy(inf->base.sensor, OG02B10_NAME, sizeof(inf->base.sensor));
	strscpy(inf->base.module, og02b10->module_name,
	sizeof(inf->base.module));
	strscpy(inf->base.lens, og02b10->len_name, sizeof(inf->base.lens));
}

static long og02b10_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct og02b10 *og02b10 = to_og02b10(sd);
	struct rkmodule_hdr_cfg *hdr_cfg;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_SET_HDR_CFG:
		v4l2_info(sd, "cmd: RKMODULE_SET_HDR_CFG\n");
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		if (hdr_cfg->hdr_mode != 0)
			ret = -1;
		break;
	case RKMODULE_GET_MODULE_INFO:
		v4l2_info(sd, "cmd: RKMODULE_GET_MODULE_INFO\n");
		og02b10_get_module_inf(og02b10, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		v4l2_info(sd, "cmd: RKMODULE_GET_HDR_CFG\n");
		hdr_cfg = (struct rkmodule_hdr_cfg *)arg;
		hdr_cfg->hdr_mode = og02b10->cur_mode->hdr_mode;
		hdr_cfg->esp.mode = HDR_NORMAL_VC;
		break;
	case RKMODULE_SET_QUICK_STREAM:
		v4l2_info(sd, "cmd: RKMODULE_SET_QUICK_STREAM\n");
		stream = *((u32 *)arg);
		if (stream)
			ret = og02b10_write_reg(og02b10->client, OG02B10_REG_CTRL_MODE,
						OG02B10_MODE_STREAMING);
		else
			ret = og02b10_write_reg(og02b10->client, OG02B10_REG_CTRL_MODE,
						OG02B10_MODE_SW_STANDBY);
		break;
	default:
		v4l2_info(sd, "cmd missing\n");
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long og02b10_compat_ioctl32(struct v4l2_subdev *sd,
		  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	long ret;
	u32 cg = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = og02b10_ioctl(sd, cmd, inf);
		if (!ret) {
			ret = copy_to_user(up, inf, sizeof(*inf));
			if (ret)
				ret = -EFAULT;
		}
		kfree(inf);
		break;
	case RKMODULE_AWB_CFG:
		cfg = kzalloc(sizeof(*cfg), GFP_KERNEL);
		if (!cfg) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(cfg, up, sizeof(*cfg));
		if (!ret)
			ret = og02b10_ioctl(sd, cmd, cfg);
		else
			ret = -EFAULT;
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = og02b10_ioctl(sd, cmd, hdr);
		if (!ret) {
			ret = copy_to_user(up, hdr, sizeof(*hdr));
			if (ret)
				ret = -EFAULT;
		}
		kfree(hdr);
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(hdr, up, sizeof(*hdr));
		if (!ret)
			ret = og02b10_ioctl(sd, cmd, hdr);
		else
			ret = -EFAULT;
		kfree(hdr);
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		hdrae = kzalloc(sizeof(*hdrae), GFP_KERNEL);
		if (!hdrae) {
			ret = -ENOMEM;
			return ret;
		}

		ret = copy_from_user(hdrae, up, sizeof(*hdrae));
		if (!ret)
			ret = og02b10_ioctl(sd, cmd, hdrae);
		else
			ret = -EFAULT;
		kfree(hdrae);
		break;
	case RKMODULE_SET_CONVERSION_GAIN:
		ret = copy_from_user(&cg, up, sizeof(cg));
		if (!ret)
			ret = og02b10_ioctl(sd, cmd, &cg);
		else
			ret = -EFAULT;
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = og02b10_ioctl(sd, cmd, &stream);
		else
			ret = -EFAULT;
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __og02b10_start_stream(struct og02b10 *og02b10)
{
	int ret;

	ret = og02b10_write_array(og02b10->client, og02b10->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&og02b10->ctrl_handler);
	if (ret)
		return ret;

	usleep_range(1000, 5000);

	if (!og02b10->slave_mode)
		ret = og02b10_write_reg(og02b10->client,
					OG02B10_REG_CTRL_MODE,
					OG02B10_MODE_STREAMING);

	return ret;
}

static int __og02b10_stop_stream(struct og02b10 *og02b10)
{
	og02b10->has_init_exp = false;
	og02b10->slave_mode = false;
	return og02b10_write_reg(og02b10->client, OG02B10_REG_CTRL_MODE, OG02B10_MODE_SW_STANDBY);
}

static int og02b10_s_stream(struct v4l2_subdev *sd, int on)
{
	struct og02b10 *og02b10 = to_og02b10(sd);
	struct i2c_client *client = og02b10->client;
	int ret = 0;

	mutex_lock(&og02b10->mutex);
	on = !!on;
	if (on == og02b10->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			v4l2_err(sd, "pm runtime get sync failed\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __og02b10_start_stream(og02b10);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__og02b10_stop_stream(og02b10);
		pm_runtime_put(&client->dev);
	}

	og02b10->streaming = on;

unlock_and_return:
	mutex_unlock(&og02b10->mutex);

	return ret;
}

static int og02b10_s_power(struct v4l2_subdev *sd, int on)
{
	struct og02b10 *og02b10 = to_og02b10(sd);
	struct i2c_client *client = og02b10->client;
	int ret = 0;

	mutex_lock(&og02b10->mutex);

	/* If the power state is not modified - no work to do. */
	if (og02b10->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		usleep_range(100, 200);

		og02b10->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		og02b10->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&og02b10->mutex);

	return ret;
}

static int __og02b10_power_on(struct og02b10 *og02b10)
{
	int ret = 0;
	struct device *dev = &og02b10->client->dev;

	ret = clk_set_rate(og02b10->xvclk, OG02B10_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(og02b10->xvclk) != OG02B10_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(og02b10->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}

	ret = regulator_bulk_enable(OG02B10_NUM_SUPPLIES, og02b10->supplies);
	if (ret < 0)
		dev_err(dev, "Failed to enable regulators\n");

	if (!IS_ERR(og02b10->pwdn_gpio)) {
		ret = gpiod_direction_output(og02b10->pwdn_gpio, 1);
		if (ret < 0)
			dev_err(dev, "Failed to set pwdn gpio\n");
		usleep_range(3000, 5000);
	}

	if (!IS_ERR(og02b10->reset_gpio)) {
		ret = gpiod_direction_output(og02b10->reset_gpio, 1);
		if (ret < 0)
			dev_err(dev, "Failed to set reset gpio\n");
		usleep_range(3000, 5000);
	}

	return ret;
}

static void __og02b10_power_off(struct og02b10 *og02b10)
{
	struct device *dev = &og02b10->client->dev;
	int ret = 0;

	if (!IS_ERR(og02b10->reset_gpio)) {
		ret = gpiod_direction_output(og02b10->reset_gpio, 0);
		if (ret < 0)
			dev_err(dev, "Failed to set reset gpio\n");
		usleep_range(500, 1000);
	}
	if (!IS_ERR(og02b10->pwdn_gpio)) {
		ret = gpiod_direction_output(og02b10->pwdn_gpio, 0);
		if (ret < 0)
			dev_err(dev, "Failed to set pwdn gpio\n");
		usleep_range(500, 1000);
	}
	clk_disable_unprepare(og02b10->xvclk);
	regulator_bulk_disable(OG02B10_NUM_SUPPLIES, og02b10->supplies);
}

static int og02b10_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct og02b10 *og02b10 = to_og02b10(sd);

	return __og02b10_power_on(og02b10);
}

static int og02b10_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct og02b10 *og02b10 = to_og02b10(sd);

	__og02b10_power_off(og02b10);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int og02b10_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct og02b10 *og02b10 = to_og02b10(sd);
	struct v4l2_mbus_framefmt *try_fmt =
		v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct og02b10_mode *def_mode = &supported_modes[0];

	mutex_lock(&og02b10->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = def_mode->bus_fmt;
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&og02b10->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int og02b10_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	struct og02b10 *og02b10 = to_og02b10(sd);

	if (fie->index >= og02b10->cfg_num)
		return -EINVAL;

	fie->code = supported_modes[fie->index].bus_fmt;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops og02b10_pm_ops = {
	SET_RUNTIME_PM_OPS(og02b10_runtime_suspend,
			   og02b10_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops og02b10_internal_ops = {
	.open = og02b10_open,
};
#endif

static const struct v4l2_subdev_core_ops og02b10_core_ops = {
	.s_power = og02b10_s_power,
	.ioctl = og02b10_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = og02b10_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops og02b10_video_ops = {
	.s_stream = og02b10_s_stream,
	.g_frame_interval = og02b10_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops og02b10_pad_ops = {
	.enum_mbus_code = og02b10_enum_mbus_code,
	.enum_frame_size = og02b10_enum_frame_sizes,
	.enum_frame_interval = og02b10_enum_frame_interval,
	.get_fmt = og02b10_get_fmt,
	.set_fmt = og02b10_set_fmt,
	.get_mbus_config = og02b10_g_mbus_config,
};

static const struct v4l2_subdev_ops og02b10_subdev_ops = {
	.core = &og02b10_core_ops,
	.video = &og02b10_video_ops,
	.pad = &og02b10_pad_ops,
};

static int og02b10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct og02b10 *og02b10 = container_of(ctrl->handler,
			 struct og02b10, ctrl_handler);
	struct i2c_client *client = og02b10->client;
	int ret = 0;
	u8 again = 0, dgain = 1;
	u8 again_fin = 0, dgain_fin_l = 0, dgain_fin_h = 0;
	u32 dgain_fin = 0;

	u16 vts_val = 0;
	s64 max;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = og02b10->cur_mode->height + ctrl->val - 12;
		__v4l2_ctrl_modify_range(og02b10->exposure,
					 og02b10->exposure->minimum, max,
					 og02b10->exposure->step,
					 og02b10->exposure->default_value);
		break;
	}

	dev_dbg(&client->dev, "set ctrl %#x\n", ctrl->id);
	if (!pm_runtime_get_if_in_use(&client->dev)) {
		dev_dbg(&client->dev, "powe is not on!!!!\n");
		return 0;
	}

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = og02b10_write_reg(og02b10->client,
					 OG02B10_REG_GROUP_HOLD, OG02B10_GROUP0_HOLD_START);
		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_EXP_H, (ctrl->val >> 8) & 0xFF);
		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_EXP_L, ctrl->val & 0xFF);
		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_GROUP_HOLD, OG02B10_GROUP0_HOLD_END);
		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_GROUP_HOLD, OG02B10_GROUP_LAUNCH);
		dev_dbg(&client->dev, "set exposure %#x(%d)\n", ctrl->val, ctrl->val);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		/* val = again * dgain
		 * again = 1 ~ 15.5
		 * dgain = 1 ~ 3.98
		 *
		 * val = rounddown(again * dgain * 16 + 0.5)
		 *
		 * val:
		 * 1 ~ 248     again: val / 16
		 *	       dgain: 1
		 * 248 ~ 988   again: 15.5
		 *	       dgain: val / 248
		 *
		 * again_value	      again_fine_step_value
		 * 1.0000 ~ 1.9375	  1/16
		 * 2.000  ~ 3.875	  1/8
		 * 4.00	  ~ 7.75	  1/4
		 * 8.0	  ~ 15.5	  1/2
		 *
		 * dgain_value	      dgain_fine_step_value
		 * 1 ~ 3.98		 1/1024
		 */
		if (ctrl->val > 248) {
			again = 15;
			again_fin = 1;

			dgain = ctrl->val / 248;
			dgain_fin = ((ctrl->val - dgain * 248) << 10) / 248;
			dgain_fin_h = (dgain_fin >> 2) & 0xff;
			dgain_fin_l = dgain_fin & 0x3;
		} else {
			again = ctrl->val >> 4;
			if (again == 1)
				again_fin = ctrl->val & 0xf;
			else if (again >= 2 && again < 4)
				again_fin = (ctrl->val & 0xf) >> 1;
			else if (again >= 4 && again < 8)
				again_fin = (ctrl->val & 0xf) >> 2;
			else if (again >= 8)
				again_fin = (ctrl->val & 0xf) >> 3;

			dgain = 1;
			dgain_fin_l = 0;
			dgain_fin_h = 0;
		}

		ret = og02b10_write_reg(og02b10->client,
					OG02B10_REG_GROUP_HOLD, OG02B10_GROUP0_HOLD_START);

		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_AGAIN_COARSE, again);
		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_AGAIN_FINE, (again_fin << 4));

		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_DGAIN_COARSE, dgain);
		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_DGAIN_FINE_H, dgain_fin_h);
		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_DGAIN_FINE_L, dgain_fin_l);

		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_GROUP_HOLD, OG02B10_GROUP0_HOLD_END);
		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_GROUP_HOLD, OG02B10_GROUP_LAUNCH);

		dev_dbg(&client->dev,
			"set gain %#x, again = %#x(%u), again fine = %#x(%u), dgain = %#x(%u), dgain fine = %#x(%u)\n",
			ctrl->val, again, again, again_fin, again_fin,
			dgain, dgain, dgain_fin, dgain_fin);
		break;
	case V4L2_CID_VBLANK:
		vts_val = og02b10->cur_mode->height + ctrl->val;
		ret = og02b10_write_reg(og02b10->client,
					OG02B10_REG_GROUP_HOLD, OG02B10_GROUP0_HOLD_START);
		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_VTS_H, (vts_val >> 8) & 0xFF);
		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_VTS_L, vts_val & 0xFF);
		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_GROUP_HOLD, OG02B10_GROUP0_HOLD_END);
		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_GROUP_HOLD, OG02B10_GROUP_LAUNCH);
		dev_dbg(&client->dev, "set vblank %#x\n", vts_val);
		break;
	case V4L2_CID_TEST_PATTERN:
		if (ctrl->val)
			ret = og02b10_write_reg(og02b10->client,
						OG02B10_REG_TEST_PATTERN,
						OG02B10_TEST_PATTERN_ENABLE_VAL);
		else
			ret = og02b10_write_reg(og02b10->client,
						OG02B10_REG_TEST_PATTERN,
						OG02B10_TEST_PATTERN_DISABLE_VAL);
		dev_dbg(&client->dev, "set test pattern %#x\n", ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		if (ctrl->val) {
			ret = og02b10_write_reg(og02b10->client,
						OG02B10_REG_MIRROR, OG02B10_MIRROR_ENABLE_VAL);
		} else {
			ret = og02b10_write_reg(og02b10->client,
						OG02B10_REG_MIRROR, OG02B10_MIRROR_DISABLE_VAL);
		}
		dev_dbg(&client->dev, "set mirror %#x\n", ctrl->val);
		break;
	case V4L2_CID_VFLIP:
		if (ctrl->val) {
			ret = og02b10_write_reg(og02b10->client,
						OG02B10_REG_FILP, OG02B10_FILP_ENABLE_VAL);
		} else {
			ret = og02b10_write_reg(og02b10->client,
						OG02B10_REG_FILP, OG02B10_FILP_DISABLE_VAL);
		}
		dev_dbg(&client->dev, "set flip %#x\n", ctrl->val);
		break;
	case V4L2_CID_OG_LOW_POWER_MODE:
		/* slave mode
		 * C0 3006 00 02 ;disable FSIN output, bit operation, set 0x3006[1]=0
		 * C0 303F 01 ;r_frame_on_num. (active frames after wake-up)
		 * C0 31FF 00 ;Change to Clockless SCCB
		 * C0 0100 00
		 * ;FSIN trigger wake => active certain frames => sleep
		 * C0 3030 10
		 * C0 3030 84
		 */
		ret = og02b10_write_reg(og02b10->client,
					OG02B10_REG_GROUP_HOLD,
					OG02B10_GROUP0_HOLD_START);

		if (ctrl->val == EXTERNAL_TRIGGER_MODE) {
			if (!og02b10->slave_mode) {
				ret |= og02b10_write_reg(og02b10->client,
							 OG02B10_REG_IO_PAD_OUT_EN,
							 OG02B10_IO_PAD_OUT_EN_DEFAULT &
							 (~OG02B10_FSIN_OUTPUT_ENABLE));
				ret |= og02b10_write_reg(og02b10->client,
							 OG02B10_REG_R_FRAME_ON_NUM, 0x1);
				ret |= og02b10_write_reg(og02b10->client,
							 OG02B10_REG_SCCB_SEL,
							 OG02B10_CLOCKLESS_SCCB);

				if (og02b10->streaming) {
					ret |= og02b10_write_reg(og02b10->client,
								 OG02B10_REG_CTRL_MODE,
								 OG02B10_MODE_SW_STANDBY);
				}

				// FSIN trigger wake => active certain frames => sleep
				ret |= og02b10_write_reg(og02b10->client,
							 OG02B10_REG_LOW_POWER_MODE_CTRL,
							 OG02B10_LOW_FRAME_RATE_VAL);
				ret |= og02b10_write_reg(og02b10->client,
							 OG02B10_REG_LOW_POWER_MODE_CTRL,
							 OG02B10_EXTERNAL_TRIGGER_VAL | 0x80);
				if (ret == 0)
					og02b10->slave_mode = true;
			}
		} else if (ctrl->val == LOW_POWER_MODE_DISABLE) {
			if (og02b10->slave_mode) {
				ret |= og02b10_write_reg(og02b10->client,
							 OG02B10_REG_IO_PAD_OUT_EN,
							 OG02B10_IO_PAD_OUT_EN_DEFAULT);
				ret |= og02b10_write_reg(og02b10->client,
							 OG02B10_REG_R_FRAME_ON_NUM, 0x3);
				ret |= og02b10_write_reg(og02b10->client,
							 OG02B10_REG_SCCB_SEL, 0x1);
				if (ret == 0)
					og02b10->slave_mode = false;
			}
		} else {
			dev_warn(&client->dev, "Not support low power mode %#x\n", ctrl->val);
		}

		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_GROUP_HOLD, OG02B10_GROUP0_HOLD_END);
		ret |= og02b10_write_reg(og02b10->client,
					 OG02B10_REG_GROUP_HOLD, OG02B10_GROUP_LAUNCH);

		dev_dbg(&client->dev, "set low power mode %#x\n", ctrl->val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id: %#x, val :%#x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops og02b10_ctrl_ops = {
	.s_ctrl = og02b10_set_ctrl,
};

static const struct v4l2_ctrl_config og02b10_test_pattern_ctrl_cfg = {
	.ops = &og02b10_ctrl_ops,
	.id = V4L2_CID_TEST_PATTERN,
	.name = "Test Pattern",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = 0x0,
	.max = 0x1,
	.step = 0x1,
	.def = 0x0,
};

static const struct v4l2_ctrl_config og02b10_low_power_mode_ctrl_cfg = {
	.ops = &og02b10_ctrl_ops,
	.id = V4L2_CID_OG_LOW_POWER_MODE,
	.name = "low power mode",
	.type = V4L2_CTRL_TYPE_INTEGER,
	.min = LOW_POWER_MODE_DISABLE,
	.max = LOW_POWER_MODE_MAX - 1,
	.step = 0x1,
	.def = LOW_POWER_MODE_DISABLE,
};

static int og02b10_initialize_controls(struct og02b10 *og02b10)
{
	const struct og02b10_mode *mode;
	struct v4l2_ctrl_handler *handler;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;
	u64 dst_link_freq = 0;
	u64 dst_pixel_rate = 0;

	handler = &og02b10->ctrl_handler;
	mode = og02b10->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 10);
	if (ret)
		return ret;
	handler->lock = &og02b10->mutex;

	og02b10->link_freq = v4l2_ctrl_new_int_menu(handler, NULL,
						    V4L2_CID_LINK_FREQ,
						    1, 0, link_freq_menu_items);

	if (og02b10->cur_mode->bus_fmt == MEDIA_BUS_FMT_SBGGR10_1X10) {
		dst_link_freq = 0;
		dst_pixel_rate = PIXEL_RATE_WITH_400M;
	}
	/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
	og02b10->pixel_rate = v4l2_ctrl_new_std(handler, NULL,
						V4L2_CID_PIXEL_RATE,
						0, PIXEL_RATE_WITH_400M,
						1, dst_pixel_rate);

	__v4l2_ctrl_s_ctrl(og02b10->link_freq,
			   dst_link_freq);

	h_blank = mode->hts_def;
	og02b10->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					    h_blank, h_blank, 1, h_blank);
	if (og02b10->hblank)
		og02b10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	og02b10->vblank = v4l2_ctrl_new_std(handler, &og02b10_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_def,
					    OG02B10_VTS_MAX - mode->height, 1, vblank_def);

	exposure_max = mode->vts_def - 12;
	og02b10->exposure = v4l2_ctrl_new_std(handler, &og02b10_ctrl_ops,
					      V4L2_CID_EXPOSURE, OG02B10_EXPOSURE_MIN,
					      exposure_max, OG02B10_EXPOSURE_STEP,
					      mode->exp_def);

	og02b10->anal_gain = v4l2_ctrl_new_std(handler, &og02b10_ctrl_ops,
					       V4L2_CID_ANALOGUE_GAIN, OG02B10_GAIN_MIN,
					       OG02B10_GAIN_MAX, OG02B10_GAIN_STEP,
					       OG02B10_GAIN_DEFAULT);

	og02b10->h_flip = v4l2_ctrl_new_std(handler, &og02b10_ctrl_ops,
					    V4L2_CID_HFLIP, 0, 1, 1, 0);

	og02b10->v_flip = v4l2_ctrl_new_std(handler, &og02b10_ctrl_ops,
					    V4L2_CID_VFLIP, 0, 1, 1, 0);

	og02b10->test_pattern = v4l2_ctrl_new_custom(handler, &og02b10_test_pattern_ctrl_cfg, NULL);

	og02b10->low_power_mode = v4l2_ctrl_new_custom(handler, &og02b10_low_power_mode_ctrl_cfg, NULL);
	if (handler->error) {
		ret = handler->error;
		dev_err(&og02b10->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	og02b10->subdev.ctrl_handler = handler;
	og02b10->has_init_exp = false;
	og02b10->slave_mode = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int og02b10_check_sensor_id(struct og02b10 *og02b10,
		  struct i2c_client *client)
{
	struct device *dev = &og02b10->client->dev;
	u8 id0 = 0, id1 = 0, id2 = 0;
	int ret;

	ret = og02b10_read_reg(client, OG02B10_REG_SC_SCCB_ID0, &id0);
	if (id0 != OG02B10_CHIP_ID0) {
		dev_err(dev, "Unexpected sensor SCCB ID: id0(%06x), ret(%d)\n", id0, ret);
		return -ENODEV;
	}

	ret = og02b10_read_reg(client, OG02B10_REG_SC_SCCB_ID1, &id1);
	if (id1 != OG02B10_CHIP_ID1) {
		dev_err(dev, "Unexpected sensor SCCB ID: id1(%06x), ret(%d)\n", id0, ret);
		return -ENODEV;
	}

	ret = og02b10_read_reg(client, OG02B10_REG_SC_SCCB_ID2, &id2);
	if (id2 != OG02B10_CHIP_ID2) {
		dev_err(dev, "Unexpected sensor SCCB ID: id2(%06x), ret(%d)\n", id0, ret);
		return -ENODEV;
	}

	dev_info(dev,
		 "Detected OG02B10 sensor success, id0(%06x), id1(%06x), id2(%06x).\n",
		 id0, id1, id2);
	return 0;
}

static int og02b10_configure_regulators(struct og02b10 *og02b10)
{
	unsigned int i;

	for (i = 0; i < OG02B10_NUM_SUPPLIES; i++)
		og02b10->supplies[i].supply = OG02B10_supply_names[i];

	return devm_regulator_bulk_get(&og02b10->client->dev,
				       OG02B10_NUM_SUPPLIES,
				       og02b10->supplies);
}

static int og02b10_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct og02b10 *og02b10;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		 DRIVER_VERSION >> 16,
		 (DRIVER_VERSION & 0xff00) >> 8,
		 DRIVER_VERSION & 0x00ff);

	og02b10 = devm_kzalloc(dev, sizeof(*og02b10), GFP_KERNEL);
	if (!og02b10)
		return -ENOMEM;

	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &og02b10->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &og02b10->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &og02b10->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &og02b10->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	ret = of_property_read_u32(node, OF_CAMERA_HDR_MODE,
				   &hdr_mode);
	if (ret) {
		hdr_mode = NO_HDR;
		dev_warn(dev, " Get hdr mode failed! no hdr default\n");
	}

	og02b10->cfg_num = ARRAY_SIZE(supported_modes);
	for (i = 0; i < og02b10->cfg_num; i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			og02b10->cur_mode = &supported_modes[i];
			break;
		}
	}
	og02b10->client = client;
	og02b10->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(og02b10->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	og02b10->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(og02b10->pinctrl)) {
		og02b10->pins_default = pinctrl_lookup_state(og02b10->pinctrl,
							     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(og02b10->pins_default)) {
			dev_err(dev, "could not get default pinstate\n");
		} else {
			ret = pinctrl_select_state(og02b10->pinctrl, og02b10->pins_default);
			if (ret < 0)
				dev_err(dev, "could not set default pins\n");
		}

		og02b10->pins_mclk = pinctrl_lookup_state(og02b10->pinctrl,
							  OF_CAMERA_PINCTRL_STATE_MCLK);
		if (IS_ERR(og02b10->pins_mclk)) {
			dev_err(dev, "could not get mclk pinstate\n");
		} else {
			if (!IS_ERR_OR_NULL(og02b10->pins_mclk)) {
				ret = pinctrl_select_state(og02b10->pinctrl, og02b10->pins_mclk);
				if (ret < 0)
					dev_err(dev, "could not set mclk pins\n");
			}
		}
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	og02b10->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(og02b10->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	og02b10->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(og02b10->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	ret = og02b10_configure_regulators(og02b10);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	og02b10->slave_mode = false;
	mutex_init(&og02b10->mutex);

	sd = &og02b10->subdev;
	v4l2_i2c_subdev_init(sd, client, &og02b10_subdev_ops);
	ret = og02b10_initialize_controls(og02b10);
	if (ret)
		goto err_destroy_mutex;

	ret = __og02b10_power_on(og02b10);
	if (ret)
		goto err_free_handler;

	ret = og02b10_check_sensor_id(og02b10, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &og02b10_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	og02b10->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &og02b10->pad);
	if (ret < 0) {
		dev_err(dev, "media entity pads init failed.\n");
		goto err_power_off;
	}
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(og02b10->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 og02b10->module_index, facing,
		 OG02B10_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	dev_info(dev, "probe success\n");
	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__og02b10_power_off(og02b10);
err_free_handler:
	v4l2_ctrl_handler_free(&og02b10->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&og02b10->mutex);

	return ret;
}

static int og02b10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct og02b10 *og02b10 = to_og02b10(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&og02b10->ctrl_handler);
	mutex_destroy(&og02b10->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__og02b10_power_off(og02b10);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id og02b10_of_match[] = {
	{ .compatible = "ovti,og02b10" },
	{},
};
MODULE_DEVICE_TABLE(of, og02b10_of_match);
#endif

static const struct i2c_device_id og02b10_match_id[] = {
	{ "ovti,og02b10", 0 },
	{ },
};

static struct i2c_driver og02b10_i2c_driver = {
	.driver = {
		.name = OG02B10_NAME,
		.pm = &og02b10_pm_ops,
		.of_match_table = of_match_ptr(og02b10_of_match),
	},
	.probe = &og02b10_probe,
	.remove = &og02b10_remove,
	.id_table = og02b10_match_id,
};

#ifdef CONFIG_ROCKCHIP_THUNDER_BOOT
module_i2c_driver(og02b10_i2c_driver);
#else
static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&og02b10_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&og02b10_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);
#endif

MODULE_DESCRIPTION("OmniVision og02b10 sensor driver");
MODULE_LICENSE("GPL");
