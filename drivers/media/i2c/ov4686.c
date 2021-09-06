// SPDX-License-Identifier: GPL-2.0
/*
 * OV4686 driver
 *
 * Copyright (C) 2020 Rockchip Electronics Co., Ltd.
 *
 * V0.0X01.0X01 first version.
 */

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
#include <linux/rk-preisp.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>
#include <linux/pinctrl/consumer.h>

#define DRIVER_VERSION			KERNEL_VERSION(0, 0x01, 0x01)

#ifndef V4L2_CID_DIGITAL_GAIN
#define V4L2_CID_DIGITAL_GAIN		V4L2_CID_GAIN
#endif

#define OV4686_LANES			4
#define OV4686_BITS_PER_SAMPLE		10
#define OV4686_LINK_FREQ_500MHZ		500000000LL
/* pixel rate = link frequency * 2 * lanes / BITS_PER_SAMPLE */
#define OV4686_PIXEL_RATE		(OV4686_LINK_FREQ_500MHZ * 2 * \
					 OV4686_LANES / OV4686_BITS_PER_SAMPLE)
#define OV4686_XVCLK_FREQ		24000000

#define CHIP_ID				0x004688
#define OV4686_REG_CHIP_ID		0x300a

#define OV4686_REG_CTRL_MODE		0x0100
#define OV4686_MODE_SW_STANDBY		0x0
#define OV4686_MODE_STREAMING		BIT(0)

#define OV4686_REG_EXPOSURE		0x3500
#define	OV4686_EXPOSURE_MIN		4
#define	OV4686_EXPOSURE_STEP		1
#define OV4686_VTS_MAX			0x7fff

#define OV4686_REG_GAIN_H		0x3508
#define OV4686_REG_GAIN_L		0x3509//low 7bit  fraction
#define OV4686_GAIN_H_MASK		0x07
#define OV4686_GAIN_H_SHIFT		8
#define OV4686_GAIN_L_MASK		0xff
#define OV4686_GAIN_MIN			0x80
#define OV4686_GAIN_MAX			0x7f8
#define OV4686_GAIN_STEP		1
#define OV4686_GAIN_DEFAULT		0x80

#define OV4686_REG_L_GAIN		0x3508
#define OV4686_REG_M_GAIN		0x350e
#define OV4686_REG_S_GAIN		0x3514
#define OV4686_REG_L_EXP		0x3500
#define OV4686_REG_M_EXP		0x350a
#define OV4686_REG_S_EXP		0x3510

#define OV4686_GROUP_UPDATE_ADDRESS	0x3208
#define OV4686_GROUP_UPDATE_START_DATA	0x00
#define OV4686_GROUP_UPDATE_END_DATA	0x10
#define OV4686_GROUP_UPDATE_LAUNCH	0xA0

#define OV4686_REG_TEST_PATTERN		0x5040
#define OV4686_TEST_PATTERN_ENABLE	0x80
#define OV4686_TEST_PATTERN_DISABLE	0x0

#define OV4686_REG_VTS			0x380e

#define OV4686_VFLIP_REG		0x3820
#define OV4686_HFLIP_REG		0x3821
#define MIRROR_BIT_MASK			(BIT(1) | BIT(2))
#define FLIP_BIT_MASK			(BIT(1) | BIT(2))

#define REG_NULL			0xFFFF

#define OV4686_REG_VALUE_08BIT		1
#define OV4686_REG_VALUE_16BIT		2
#define OV4686_REG_VALUE_24BIT		3

#define OF_CAMERA_PINCTRL_STATE_DEFAULT	"rockchip,camera_default"
#define OF_CAMERA_PINCTRL_STATE_SLEEP	"rockchip,camera_sleep"
#define OF_CAMERA_HDR_MODE		"rockchip,camera-hdr-mode"
#define OV4686_NAME			"ov4686"

static const char * const OV4686_supply_names[] = {
	"avdd",		/* Analog power */
	"dovdd",	/* Digital I/O power */
	"dvdd",		/* Digital core power */
};

#define OV4686_NUM_SUPPLIES ARRAY_SIZE(OV4686_supply_names)

struct regval {
	u16 addr;
	u8 val;
};

struct OV4686_mode {
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

struct OV4686 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct gpio_desc	*pwdn_gpio;
	struct regulator_bulk_data supplies[OV4686_NUM_SUPPLIES];

	struct pinctrl		*pinctrl;
	struct pinctrl_state	*pins_default;
	struct pinctrl_state	*pins_sleep;

	struct v4l2_subdev	subdev;
	struct media_pad	pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl	*exposure;
	struct v4l2_ctrl	*anal_gain;
	struct v4l2_ctrl	*digi_gain;
	struct v4l2_ctrl	*hblank;
	struct v4l2_ctrl	*vblank;
	struct v4l2_ctrl	*test_pattern;
	struct mutex		mutex;
	bool			streaming;
	bool			power_on;
	const struct OV4686_mode *cur_mode;
	u32			module_index;
	const char		*module_facing;
	const char		*module_name;
	const char		*len_name;
	bool			has_init_exp;
	struct preisp_hdrae_exp_s init_hdrae_exp;
};

#define to_OV4686(sd) container_of(sd, struct OV4686, subdev)

/*
 * Xclk 24Mhz
 */
static const struct regval OV4686_global_regs[] = {
	{REG_NULL, 0x00},
};

/*
 * Xclk 24Mhz
 * max_framerate 90fps
 * mipi_datarate per lane 1008Mbps, 4lane
 */
static const struct regval OV4686_2688x1520_regs[] = {
	{0x0103, 0x01},
	{0x3638, 0x00},
	{0x0300, 0x00},
	{0x0302, 0x2a},
	{0x0303, 0x00},
	{0x0304, 0x03},
	{0x030b, 0x00},
	{0x030d, 0x1e},
	{0x030e, 0x04},
	{0x030f, 0x01},
	{0x0312, 0x01},
	{0x031e, 0x00},
	{0x3000, 0x20},
	{0x3002, 0x00},
	{0x3018, 0x72},
	{0x3020, 0x93},
	{0x3021, 0x03},
	{0x3022, 0x01},
	{0x3031, 0x0a},
	{0x303f, 0x0c},
	{0x3305, 0xf1},
	{0x3307, 0x04},
	{0x3309, 0x29},
	{0x3500, 0x00},
	{0x3501, 0x5f},
	{0x3502, 0x00},
	{0x3503, 0x04},
	{0x3504, 0x00},
	{0x3505, 0x00},
	{0x3506, 0x00},
	{0x3507, 0x00},
	{0x3508, 0x00},
	{0x3509, 0x80},
	{0x350a, 0x00},
	{0x350b, 0x00},
	{0x350c, 0x00},
	{0x350d, 0x00},
	{0x350e, 0x00},
	{0x350f, 0x80},
	{0x3510, 0x00},
	{0x3511, 0x00},
	{0x3512, 0x00},
	{0x3513, 0x00},
	{0x3514, 0x00},
	{0x3515, 0x80},
	{0x3516, 0x00},
	{0x3517, 0x00},
	{0x3518, 0x00},
	{0x3519, 0x00},
	{0x351a, 0x00},
	{0x351b, 0x80},
	{0x351c, 0x00},
	{0x351d, 0x00},
	{0x351e, 0x00},
	{0x351f, 0x00},
	{0x3520, 0x00},
	{0x3521, 0x80},
	{0x3522, 0x08},
	{0x3524, 0x08},
	{0x3526, 0x08},
	{0x3528, 0x08},
	{0x352a, 0x08},
	{0x3602, 0x00},
	{0x3603, 0x40},
	{0x3604, 0x02},
	{0x3605, 0x00},
	{0x3606, 0x00},
	{0x3607, 0x00},
	{0x3609, 0x12},
	{0x360a, 0x40},
	{0x360c, 0x08},
	{0x360f, 0xe0},
	{0x3608, 0x8f},
	{0x3611, 0x00},
	{0x3613, 0xf7},
	{0x3616, 0x58},
	{0x3619, 0x99},
	{0x361b, 0x60},
	{0x361c, 0x7a},
	{0x361e, 0x79},
	{0x361f, 0x02},
	{0x3632, 0x00},
	{0x3633, 0x10},
	{0x3634, 0x10},
	{0x3635, 0x10},
	{0x3636, 0x10},
	{0x3646, 0x86},
	{0x364a, 0x0b},
	{0x3700, 0x17},
	{0x3701, 0x22},
	{0x3703, 0x10},
	{0x370a, 0x37},
	{0x3705, 0x00},
	{0x3706, 0x63},
	{0x3709, 0x3c},
	{0x370b, 0x01},
	{0x370c, 0x30},
	{0x3710, 0x24},
	{0x3711, 0x0c},
	{0x3716, 0x00},
	{0x3720, 0x28},
	{0x3729, 0x7b},
	{0x372a, 0x84},
	{0x372b, 0xbd},
	{0x372c, 0xbc},
	{0x372e, 0x52},
	{0x373c, 0x0e},
	{0x373e, 0x33},
	{0x3743, 0x10},
	{0x3744, 0x88},
	{0x3745, 0xc0},
	{0x374a, 0x43},
	{0x374c, 0x00},
	{0x374e, 0x23},
	{0x3751, 0x7b},
	{0x3752, 0x84},
	{0x3753, 0xbd},
	{0x3754, 0xbc},
	{0x3756, 0x52},
	{0x375c, 0x00},
	{0x3760, 0x00},
	{0x3761, 0x00},
	{0x3762, 0x00},
	{0x3763, 0x00},
	{0x3764, 0x00},
	{0x3767, 0x04},
	{0x3768, 0x04},
	{0x3769, 0x08},
	{0x376a, 0x08},
	{0x376b, 0x20},
	{0x376c, 0x00},
	{0x376d, 0x00},
	{0x376e, 0x00},
	{0x3773, 0x00},
	{0x3774, 0x51},
	{0x3776, 0xbd},
	{0x3777, 0xbd},
	{0x3781, 0x18},
	{0x3783, 0x25},
	{0x3798, 0x1b},
	{0x3800, 0x00},
	{0x3801, 0x08},
	{0x3802, 0x00},
	{0x3803, 0x04},
	{0x3804, 0x0a},
	{0x3805, 0x97},
	{0x3806, 0x05},
	{0x3807, 0xfb},
	{0x3808, 0x0a},
	{0x3809, 0x80},
	{0x380a, 0x05},
	{0x380b, 0xf0},
	{0x380c, 0x0a},
	{0x380d, 0x14},
	{0x380e, 0x06},
	{0x380f, 0x12},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x04},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3819, 0x01},
	{0x3820, 0x00},
	{0x3821, 0x06},
	{0x3829, 0x00},
	{0x382a, 0x01},
	{0x382b, 0x01},
	{0x382d, 0x7f},
	{0x3830, 0x04},
	{0x3836, 0x01},
	{0x3837, 0x00},
	{0x3841, 0x02},
	{0x3846, 0x08},
	{0x3847, 0x07},
	{0x3d85, 0x36},
	{0x3d8c, 0x71},
	{0x3d8d, 0xcb},
	{0x3f0a, 0x00},
	{0x4000, 0xf1},
	{0x4001, 0x40},
	{0x4002, 0x04},
	{0x4003, 0x14},
	{0x400e, 0x00},
	{0x4011, 0x00},
	{0x401a, 0x00},
	{0x401b, 0x00},
	{0x401c, 0x00},
	{0x401d, 0x00},
	{0x401f, 0x00},
	{0x4020, 0x00},
	{0x4021, 0x10},
	{0x4022, 0x07},
	{0x4023, 0xcf},
	{0x4024, 0x09},
	{0x4025, 0x60},
	{0x4026, 0x09},
	{0x4027, 0x6f},
	{0x4028, 0x00},
	{0x4029, 0x02},
	{0x402a, 0x06},
	{0x402b, 0x04},
	{0x402c, 0x02},
	{0x402d, 0x02},
	{0x402e, 0x0e},
	{0x402f, 0x04},
	{0x4302, 0xff},
	{0x4303, 0xff},
	{0x4304, 0x00},
	{0x4305, 0x00},
	{0x4306, 0x00},
	{0x4308, 0x02},
	{0x4500, 0x6c},
	{0x4501, 0xc4},
	{0x4502, 0x40},
	{0x4503, 0x01},
	{0x4601, 0x04},
	{0x4800, 0x04},
	{0x4813, 0x08},
	{0x481f, 0x40},
	{0x4829, 0x78},
	{0x4837, 0x10},
	{0x4b00, 0x2a},
	{0x4b0d, 0x00},
	{0x4d00, 0x04},
	{0x4d01, 0x42},
	{0x4d02, 0xd1},
	{0x4d03, 0x93},
	{0x4d04, 0xf5},
	{0x4d05, 0xc1},
	{0x5000, 0xd3},
	{0x5001, 0x11},
	{0x5004, 0x00},
	{0x500a, 0x00},
	{0x500b, 0x00},
	{0x5032, 0x00},
	{0x5040, 0x00},
	{0x5050, 0x0c},
	{0x5500, 0x00},
	{0x5501, 0x10},
	{0x5502, 0x01},
	{0x5503, 0x0f},
	{0x8000, 0x00},
	{0x8001, 0x00},
	{0x8002, 0x00},
	{0x8003, 0x00},
	{0x8004, 0x00},
	{0x8005, 0x00},
	{0x8006, 0x00},
	{0x8007, 0x00},
	{0x8008, 0x00},
	{0x3638, 0x00},
	{REG_NULL, 0x00},
};

static const struct regval OV4686_linear_regs[] = {
	{0x380c, 0x0a},
	{0x380d, 0x14},
	{0x3841, 0x02},
	{0x4800, 0x04},
	{0x376e, 0x00},
	{REG_NULL, 0x00},
};

static const struct regval OV4686_hdr_x2_regs[] = {
	{0x380c, 0x05},
	{0x380d, 0x10},

	{0x3841, 0x03},
	{0x3846, 0x08},
	{0x3847, 0x04},//04
	{0x4800, 0x0c},
	{0x376e, 0x01},
	{0x3501, 0x10},
	{0x350b, 0x08},
	{0x3511, 0x01},
	{0x3517, 0x00},
	{0x351d, 0x00},

	{0x3841, 0x03},//HDR_2
	{0x3847, 0x06},//HDR_2_ALL
	{REG_NULL, 0x00},
};

static const struct OV4686_mode supported_modes[] = {
	{
		.width = 2688,
		.height = 1520,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x0a18,
		.vts_def = 0x0612,
		.reg_list = OV4686_linear_regs,
		.hdr_mode = NO_HDR,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_0,
	}, {
		.width = 2688,
		.height = 1520,
		.max_fps = {
			.numerator = 10000,
			.denominator = 300000,
		},
		.exp_def = 0x0600,
		.hts_def = 0x0a20,
		.vts_def = 0x0612,
		.reg_list = OV4686_hdr_x2_regs,
		.hdr_mode = HDR_X2,
		.vc[PAD0] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD1] = V4L2_MBUS_CSI2_CHANNEL_0,//L->csi wr0
		.vc[PAD2] = V4L2_MBUS_CSI2_CHANNEL_1,
		.vc[PAD3] = V4L2_MBUS_CSI2_CHANNEL_1,//M->csi wr2
	},
};

static const s64 link_freq_menu_items[] = {
	OV4686_LINK_FREQ_500MHZ
};

static const char * const OV4686_test_pattern_menu[] = {
	"Disabled",
	"Vertical Color Bar Type 1",
	"Vertical Color Bar Type 2",
	"Vertical Color Bar Type 3",
	"Vertical Color Bar Type 4"
};

/* Write registers up to 4 at a time */
static int OV4686_write_reg(struct i2c_client *client, u16 reg,
			    u32 len, u32 val)
{
	u32 buf_i, val_i;
	u8 buf[6];
	u8 *val_p;
	__be32 val_be;

	if (len > 4)
		return -EINVAL;

	buf[0] = reg >> 8;
	buf[1] = reg & 0xff;

	val_be = cpu_to_be32(val);
	val_p = (u8 *)&val_be;
	buf_i = 2;
	val_i = 4 - len;

	while (val_i < 4)
		buf[buf_i++] = val_p[val_i++];

	if (i2c_master_send(client, buf, len + 2) != len + 2)
		return -EIO;

	return 0;
}

static int OV4686_write_array(struct i2c_client *client,
			      const struct regval *regs)
{
	u32 i;
	int ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = OV4686_write_reg(client, regs[i].addr,
				       OV4686_REG_VALUE_08BIT, regs[i].val);

	return ret;
}

/* Read registers up to 4 at a time */
static int OV4686_read_reg(struct i2c_client *client, u16 reg, unsigned int len,
			   u32 *val)
{
	struct i2c_msg msgs[2];
	u8 *data_be_p;
	__be32 data_be = 0;
	__be16 reg_addr_be = cpu_to_be16(reg);
	int ret;

	if (len > 4 || !len)
		return -EINVAL;

	data_be_p = (u8 *)&data_be;
	/* Write register address */
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = 2;
	msgs[0].buf = (u8 *)&reg_addr_be;

	/* Read data from register */
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_be_p[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs))
		return -EIO;

	*val = be32_to_cpu(data_be);

	return 0;
}

static int OV4686_get_reso_dist(const struct OV4686_mode *mode,
				struct v4l2_mbus_framefmt *framefmt)
{
	return abs(mode->width - framefmt->width) +
	       abs(mode->height - framefmt->height);
}

static const struct OV4686_mode *
OV4686_find_best_fit(struct v4l2_subdev_format *fmt)
{
	struct v4l2_mbus_framefmt *framefmt = &fmt->format;
	int dist;
	int cur_best_fit = 0;
	int cur_best_fit_dist = -1;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		dist = OV4686_get_reso_dist(&supported_modes[i], framefmt);
		if (cur_best_fit_dist == -1 || dist < cur_best_fit_dist) {
			cur_best_fit_dist = dist;
			cur_best_fit = i;
		}
	}

	return &supported_modes[cur_best_fit];
}

static int OV4686_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct OV4686 *OV4686 = to_OV4686(sd);
	const struct OV4686_mode *mode;
	s64 h_blank, vblank_def;

	mutex_lock(&OV4686->mutex);

	mode = OV4686_find_best_fit(fmt);
	fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		*v4l2_subdev_get_try_format(sd, cfg, fmt->pad) = fmt->format;
#else
		mutex_unlock(&OV4686->mutex);
		return -ENOTTY;
#endif
	} else {
		OV4686->cur_mode = mode;
		h_blank = mode->hts_def - mode->width;
		__v4l2_ctrl_modify_range(OV4686->hblank, h_blank,
					 h_blank, 1, h_blank);
		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(OV4686->vblank, vblank_def,
					 OV4686_VTS_MAX - mode->height,
					 1, vblank_def);
	}

	mutex_unlock(&OV4686->mutex);

	return 0;
}

static int OV4686_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct OV4686 *OV4686 = to_OV4686(sd);
	const struct OV4686_mode *mode = OV4686->cur_mode;

	mutex_lock(&OV4686->mutex);
	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
		fmt->format = *v4l2_subdev_get_try_format(sd, cfg, fmt->pad);
#else
		mutex_unlock(&OV4686->mutex);
		return -ENOTTY;
#endif
	} else {
		fmt->format.width = mode->width;
		fmt->format.height = mode->height;
		fmt->format.code = MEDIA_BUS_FMT_SBGGR10_1X10;
		fmt->format.field = V4L2_FIELD_NONE;
		/* format info: width/height/data type/virctual channel */
		if (fmt->pad < PAD_MAX && mode->hdr_mode != NO_HDR)
			fmt->reserved[0] = mode->vc[fmt->pad];
		else
			fmt->reserved[0] = mode->vc[PAD0];
	}
	mutex_unlock(&OV4686->mutex);

	return 0;
}

static int OV4686_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_pad_config *cfg,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index != 0)
		return -EINVAL;
	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int OV4686_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_pad_config *cfg,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width  = supported_modes[fse->index].width;
	fse->max_width  = supported_modes[fse->index].width;
	fse->max_height = supported_modes[fse->index].height;
	fse->min_height = supported_modes[fse->index].height;

	return 0;
}

static int OV4686_enable_test_pattern(struct OV4686 *OV4686, u32 pattern)
{
	u32 val;

	if (pattern)
		val = (pattern - 1) | OV4686_TEST_PATTERN_ENABLE;
	else
		val = OV4686_TEST_PATTERN_DISABLE;

	return OV4686_write_reg(OV4686->client, OV4686_REG_TEST_PATTERN,
				OV4686_REG_VALUE_08BIT, val);
}

static int OV4686_g_frame_interval(struct v4l2_subdev *sd,
				   struct v4l2_subdev_frame_interval *fi)
{
	struct OV4686 *OV4686 = to_OV4686(sd);
	const struct OV4686_mode *mode = OV4686->cur_mode;

	mutex_lock(&OV4686->mutex);
	fi->interval = mode->max_fps;
	mutex_unlock(&OV4686->mutex);

	return 0;
}

static int OV4686_g_mbus_config(struct v4l2_subdev *sd, unsigned int pad_id,
				struct v4l2_mbus_config *config)
{
	struct OV4686 *OV4686 = to_OV4686(sd);
	const struct OV4686_mode *mode = OV4686->cur_mode;
	u32 val = 1 << (OV4686_LANES - 1) |
		V4L2_MBUS_CSI2_CHANNEL_0 |
		V4L2_MBUS_CSI2_CONTINUOUS_CLOCK;

	if (mode->hdr_mode != NO_HDR)
		val |= V4L2_MBUS_CSI2_CHANNEL_1;
	if (mode->hdr_mode == HDR_X3)
		val |= V4L2_MBUS_CSI2_CHANNEL_2;

	config->type = V4L2_MBUS_CSI2_DPHY;
	config->flags = val;

	return 0;
}

static void OV4686_get_module_inf(struct OV4686 *OV4686,
				  struct rkmodule_inf *inf)
{
	memset(inf, 0, sizeof(*inf));
	strlcpy(inf->base.sensor, OV4686_NAME, sizeof(inf->base.sensor));
	strlcpy(inf->base.module, OV4686->module_name,
		sizeof(inf->base.module));
	strlcpy(inf->base.lens, OV4686->len_name, sizeof(inf->base.lens));
}

static int OV4686_set_hdrae(struct OV4686 *OV4686,
			    struct preisp_hdrae_exp_s *ae)
{
	int ret = 0;
	u32 l_exp = ae->long_exp_reg;
	u32 m_exp = ae->middle_exp_reg;
	u32 s_exp = ae->short_exp_reg;
	u32 l_gain = ae->long_gain_reg;
	u32 m_gain = ae->middle_gain_reg;
	u32 s_gain = ae->short_gain_reg;

	if (!OV4686->has_init_exp && !OV4686->streaming) {
		OV4686->init_hdrae_exp = *ae;
		OV4686->has_init_exp = true;
		dev_dbg(&OV4686->client->dev, "OV4686 don't stream, record exp for hdr!\n");
		return ret;
	}

	dev_dbg(&OV4686->client->dev,
		"rev exp req: L_exp: 0x%x, 0x%x, M_exp: 0x%x, 0x%x S_exp: 0x%x, 0x%x\n",
		l_exp, l_gain, m_exp, m_gain, s_exp, s_gain);

	if (l_exp < 3)
		l_exp = 3;
	if (m_exp < 3)
		m_exp = 3;
	if (s_exp < 3)
		s_exp = 3;

	if (OV4686->cur_mode->hdr_mode == HDR_X2) {
		l_gain = m_gain;
		l_exp = m_exp;
		m_gain = s_gain;
		m_exp =	s_exp;
	}

	ret = OV4686_write_reg(OV4686->client, OV4686_GROUP_UPDATE_ADDRESS,
		OV4686_REG_VALUE_08BIT, OV4686_GROUP_UPDATE_START_DATA);

	ret |= OV4686_write_reg(OV4686->client, OV4686_REG_L_GAIN,
		OV4686_REG_VALUE_16BIT, l_gain);
	ret |= OV4686_write_reg(OV4686->client, OV4686_REG_L_EXP,
		OV4686_REG_VALUE_24BIT, l_exp << 4);
	ret |= OV4686_write_reg(OV4686->client, OV4686_REG_M_GAIN,
		OV4686_REG_VALUE_16BIT, m_gain);
	ret |= OV4686_write_reg(OV4686->client, OV4686_REG_M_EXP,
		OV4686_REG_VALUE_24BIT, m_exp << 4);
	if (OV4686->cur_mode->hdr_mode == HDR_X3) {
		ret |= OV4686_write_reg(OV4686->client, OV4686_REG_S_GAIN,
			OV4686_REG_VALUE_16BIT, s_gain);
		ret |= OV4686_write_reg(OV4686->client, OV4686_REG_S_EXP,
			OV4686_REG_VALUE_24BIT, s_exp << 4);
	}
	ret |= OV4686_write_reg(OV4686->client, OV4686_GROUP_UPDATE_ADDRESS,
		OV4686_REG_VALUE_08BIT, OV4686_GROUP_UPDATE_END_DATA);
	ret |= OV4686_write_reg(OV4686->client, OV4686_GROUP_UPDATE_ADDRESS,
		OV4686_REG_VALUE_08BIT, OV4686_GROUP_UPDATE_LAUNCH);
	return ret;
}

static long OV4686_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct OV4686 *OV4686 = to_OV4686(sd);
	struct rkmodule_hdr_cfg *hdr;
	u32 i, h, w;
	long ret = 0;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		OV4686_get_module_inf(OV4686, (struct rkmodule_inf *)arg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		hdr->esp.mode = HDR_NORMAL_VC;
		hdr->hdr_mode = OV4686->cur_mode->hdr_mode;
		break;
	case RKMODULE_SET_HDR_CFG:
		hdr = (struct rkmodule_hdr_cfg *)arg;
		w = OV4686->cur_mode->width;
		h = OV4686->cur_mode->height;
		for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
			if (w == supported_modes[i].width &&
			    h == supported_modes[i].height &&
			    supported_modes[i].hdr_mode == hdr->hdr_mode) {
				OV4686->cur_mode = &supported_modes[i];
				break;
			}
		}
		if (i == ARRAY_SIZE(supported_modes)) {
			dev_err(&OV4686->client->dev,
				"not find hdr mode:%d %dx%d config\n",
				hdr->hdr_mode, w, h);
			ret = -EINVAL;
		} else {
			w = OV4686->cur_mode->hts_def - OV4686->cur_mode->width;
			h = OV4686->cur_mode->vts_def - OV4686->cur_mode->height;
			__v4l2_ctrl_modify_range(OV4686->hblank, w, w, 1, w);
			__v4l2_ctrl_modify_range(OV4686->vblank, h,
				OV4686_VTS_MAX - OV4686->cur_mode->height, 1, h);
		}
		break;
	case PREISP_CMD_SET_HDRAE_EXP:
		return OV4686_set_hdrae(OV4686, arg);
	case RKMODULE_SET_QUICK_STREAM:

		stream = *((u32 *)arg);

		if (stream)
			ret = OV4686_write_reg(OV4686->client, OV4686_REG_CTRL_MODE,
				OV4686_REG_VALUE_08BIT, OV4686_MODE_STREAMING);
		else
			ret = OV4686_write_reg(OV4686->client, OV4686_REG_CTRL_MODE,
				OV4686_REG_VALUE_08BIT, OV4686_MODE_SW_STANDBY);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}

#ifdef CONFIG_COMPAT
static long OV4686_compat_ioctl32(struct v4l2_subdev *sd,
				  unsigned int cmd, unsigned long arg)
{
	void __user *up = compat_ptr(arg);
	struct rkmodule_inf *inf;
	struct rkmodule_awb_cfg *cfg;
	struct rkmodule_hdr_cfg *hdr;
	struct preisp_hdrae_exp_s *hdrae;
	long ret;
	u32 stream = 0;

	switch (cmd) {
	case RKMODULE_GET_MODULE_INFO:
		inf = kzalloc(sizeof(*inf), GFP_KERNEL);
		if (!inf) {
			ret = -ENOMEM;
			return ret;
		}

		ret = OV4686_ioctl(sd, cmd, inf);
		if (!ret)
			ret = copy_to_user(up, inf, sizeof(*inf));
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
			ret = OV4686_ioctl(sd, cmd, cfg);
		kfree(cfg);
		break;
	case RKMODULE_GET_HDR_CFG:
		hdr = kzalloc(sizeof(*hdr), GFP_KERNEL);
		if (!hdr) {
			ret = -ENOMEM;
			return ret;
		}

		ret = OV4686_ioctl(sd, cmd, hdr);
		if (!ret)
			ret = copy_to_user(up, hdr, sizeof(*hdr));
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
			ret = OV4686_ioctl(sd, cmd, hdr);
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
			ret = OV4686_ioctl(sd, cmd, hdrae);
		kfree(hdrae);
		break;
	case RKMODULE_SET_QUICK_STREAM:
		ret = copy_from_user(&stream, up, sizeof(u32));
		if (!ret)
			ret = OV4686_ioctl(sd, cmd, &stream);
		break;
	default:
		ret = -ENOIOCTLCMD;
		break;
	}

	return ret;
}
#endif

static int __OV4686_start_stream(struct OV4686 *OV4686)
{
	int ret;

	ret = OV4686_write_array(OV4686->client, OV4686_2688x1520_regs);
	ret |= OV4686_write_array(OV4686->client, OV4686->cur_mode->reg_list);
	if (ret)
		return ret;

	/* In case these controls are set before streaming */
	ret = __v4l2_ctrl_handler_setup(&OV4686->ctrl_handler);
	if (ret)
		return ret;
	if (OV4686->has_init_exp && OV4686->cur_mode->hdr_mode != NO_HDR) {
		ret = OV4686_ioctl(&OV4686->subdev,
				   PREISP_CMD_SET_HDRAE_EXP,
				   &OV4686->init_hdrae_exp);
		if (ret) {
			dev_err(&OV4686->client->dev,
				"init exp fail in hdr mode\n");
			return ret;
		}
	}

	return OV4686_write_reg(OV4686->client, OV4686_REG_CTRL_MODE,
				OV4686_REG_VALUE_08BIT, OV4686_MODE_STREAMING);
}

static int __OV4686_stop_stream(struct OV4686 *OV4686)
{
	OV4686->has_init_exp = false;
	return OV4686_write_reg(OV4686->client, OV4686_REG_CTRL_MODE,
				OV4686_REG_VALUE_08BIT, OV4686_MODE_SW_STANDBY);
}

static int OV4686_s_stream(struct v4l2_subdev *sd, int on)
{
	struct OV4686 *OV4686 = to_OV4686(sd);
	struct i2c_client *client = OV4686->client;
	int ret = 0;

	mutex_lock(&OV4686->mutex);
	on = !!on;
	if (on == OV4686->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = __OV4686_start_stream(OV4686);
		if (ret) {
			v4l2_err(sd, "start stream failed while write regs\n");
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		__OV4686_stop_stream(OV4686);
		pm_runtime_put(&client->dev);
	}

	OV4686->streaming = on;

unlock_and_return:
	mutex_unlock(&OV4686->mutex);

	return ret;
}

static int OV4686_s_power(struct v4l2_subdev *sd, int on)
{
	struct OV4686 *OV4686 = to_OV4686(sd);
	struct i2c_client *client = OV4686->client;
	int ret = 0;

	mutex_lock(&OV4686->mutex);

	/* If the power state is not modified - no work to do. */
	if (OV4686->power_on == !!on)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = OV4686_write_array(OV4686->client, OV4686_global_regs);
		if (ret) {
			v4l2_err(sd, "could not set init registers\n");
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		OV4686->power_on = true;
	} else {
		pm_runtime_put(&client->dev);
		OV4686->power_on = false;
	}

unlock_and_return:
	mutex_unlock(&OV4686->mutex);

	return ret;
}

/* Calculate the delay in us by clock rate and clock cycles */
static inline u32 OV4686_cal_delay(u32 cycles)
{
	return DIV_ROUND_UP(cycles, OV4686_XVCLK_FREQ / 1000 / 1000);
}

static int __OV4686_power_on(struct OV4686 *OV4686)
{
	int ret;
	u32 delay_us;
	struct device *dev = &OV4686->client->dev;

	if (!IS_ERR_OR_NULL(OV4686->pins_default)) {
		ret = pinctrl_select_state(OV4686->pinctrl,
					   OV4686->pins_default);
		if (ret < 0)
			dev_err(dev, "could not set pins\n");
	}
	ret = clk_set_rate(OV4686->xvclk, OV4686_XVCLK_FREQ);
	if (ret < 0)
		dev_warn(dev, "Failed to set xvclk rate (24MHz)\n");
	if (clk_get_rate(OV4686->xvclk) != OV4686_XVCLK_FREQ)
		dev_warn(dev, "xvclk mismatched, modes are based on 24MHz\n");
	ret = clk_prepare_enable(OV4686->xvclk);
	if (ret < 0) {
		dev_err(dev, "Failed to enable xvclk\n");
		return ret;
	}
	if (!IS_ERR(OV4686->reset_gpio))
		gpiod_set_value_cansleep(OV4686->reset_gpio, 0);

	ret = regulator_bulk_enable(OV4686_NUM_SUPPLIES, OV4686->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	if (!IS_ERR(OV4686->reset_gpio))
		gpiod_set_value_cansleep(OV4686->reset_gpio, 1);

	usleep_range(500, 1000);
	if (!IS_ERR(OV4686->pwdn_gpio))
		gpiod_set_value_cansleep(OV4686->pwdn_gpio, 1);

	/* 8192 cycles prior to first SCCB transaction */
	delay_us = OV4686_cal_delay(8192);
	usleep_range(delay_us, delay_us * 2);

	return 0;

disable_clk:
	clk_disable_unprepare(OV4686->xvclk);

	return ret;
}

static void __OV4686_power_off(struct OV4686 *OV4686)
{
	int ret;
	struct device *dev = &OV4686->client->dev;

	if (!IS_ERR(OV4686->pwdn_gpio))
		gpiod_set_value_cansleep(OV4686->pwdn_gpio, 0);
	clk_disable_unprepare(OV4686->xvclk);
	if (!IS_ERR(OV4686->reset_gpio))
		gpiod_set_value_cansleep(OV4686->reset_gpio, 0);
	if (!IS_ERR_OR_NULL(OV4686->pins_sleep)) {
		ret = pinctrl_select_state(OV4686->pinctrl,
					   OV4686->pins_sleep);
		if (ret < 0)
			dev_dbg(dev, "could not set pins\n");
	}
	regulator_bulk_disable(OV4686_NUM_SUPPLIES, OV4686->supplies);
}

static int OV4686_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct OV4686 *OV4686 = to_OV4686(sd);

	return __OV4686_power_on(OV4686);
}

static int OV4686_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct OV4686 *OV4686 = to_OV4686(sd);

	__OV4686_power_off(OV4686);

	return 0;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int OV4686_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct OV4686 *OV4686 = to_OV4686(sd);
	struct v4l2_mbus_framefmt *try_fmt =
				v4l2_subdev_get_try_format(sd, fh->pad, 0);
	const struct OV4686_mode *def_mode = &supported_modes[0];

	mutex_lock(&OV4686->mutex);
	/* Initialize try_fmt */
	try_fmt->width = def_mode->width;
	try_fmt->height = def_mode->height;
	try_fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;//grbg
	try_fmt->field = V4L2_FIELD_NONE;

	mutex_unlock(&OV4686->mutex);
	/* No crop or compose */

	return 0;
}
#endif

static int OV4686_enum_frame_interval(struct v4l2_subdev *sd,
				       struct v4l2_subdev_pad_config *cfg,
				       struct v4l2_subdev_frame_interval_enum *fie)
{
	if (fie->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fie->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fie->width = supported_modes[fie->index].width;
	fie->height = supported_modes[fie->index].height;
	fie->interval = supported_modes[fie->index].max_fps;
	fie->reserved[0] = supported_modes[fie->index].hdr_mode;
	return 0;
}

static const struct dev_pm_ops OV4686_pm_ops = {
	SET_RUNTIME_PM_OPS(OV4686_runtime_suspend,
			   OV4686_runtime_resume, NULL)
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops OV4686_internal_ops = {
	.open = OV4686_open,
};
#endif

static const struct v4l2_subdev_core_ops OV4686_core_ops = {
	.s_power = OV4686_s_power,
	.ioctl = OV4686_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl32 = OV4686_compat_ioctl32,
#endif
};

static const struct v4l2_subdev_video_ops OV4686_video_ops = {
	.s_stream = OV4686_s_stream,
	.g_frame_interval = OV4686_g_frame_interval,
};

static const struct v4l2_subdev_pad_ops OV4686_pad_ops = {
	.enum_mbus_code = OV4686_enum_mbus_code,
	.enum_frame_size = OV4686_enum_frame_sizes,
	.enum_frame_interval = OV4686_enum_frame_interval,
	.get_fmt = OV4686_get_fmt,
	.set_fmt = OV4686_set_fmt,
	.get_mbus_config = OV4686_g_mbus_config,
};

static const struct v4l2_subdev_ops OV4686_subdev_ops = {
	.core	= &OV4686_core_ops,
	.video	= &OV4686_video_ops,
	.pad	= &OV4686_pad_ops,
};

static int OV4686_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct OV4686 *OV4686 = container_of(ctrl->handler,
					     struct OV4686, ctrl_handler);
	struct i2c_client *client = OV4686->client;
	s64 max;
	int ret = 0;
	u32 val = 0;

	/* Propagate change of current control to all related controls */
	switch (ctrl->id) {
	case V4L2_CID_VBLANK:
		/* Update max exposure while meeting expected vblanking */
		max = OV4686->cur_mode->height + ctrl->val - 4;
		__v4l2_ctrl_modify_range(OV4686->exposure,
					 OV4686->exposure->minimum, max,
					 OV4686->exposure->step,
					 OV4686->exposure->default_value);
		break;
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		/* 4 least significant bits of expsoure are fractional part */
		ret = OV4686_write_reg(OV4686->client, OV4686_REG_EXPOSURE,
				       OV4686_REG_VALUE_24BIT, ctrl->val << 4);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = OV4686_write_reg(OV4686->client, OV4686_REG_GAIN_H,
				       OV4686_REG_VALUE_08BIT,
				       (ctrl->val >> OV4686_GAIN_H_SHIFT) & OV4686_GAIN_H_MASK);
		ret |= OV4686_write_reg(OV4686->client, OV4686_REG_GAIN_L,
				       OV4686_REG_VALUE_08BIT,
				       ctrl->val & OV4686_GAIN_L_MASK);
		break;
	case V4L2_CID_VBLANK:
		ret = OV4686_write_reg(OV4686->client, OV4686_REG_VTS,
				       OV4686_REG_VALUE_16BIT,
				       ctrl->val + OV4686->cur_mode->height);
		break;
	case V4L2_CID_TEST_PATTERN:
		ret = OV4686_enable_test_pattern(OV4686, ctrl->val);
		break;
	case V4L2_CID_HFLIP:
		ret = OV4686_read_reg(OV4686->client, OV4686_HFLIP_REG,
				       OV4686_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= MIRROR_BIT_MASK;
		else
			val &= ~MIRROR_BIT_MASK;
		ret = OV4686_write_reg(OV4686->client, OV4686_HFLIP_REG,
					OV4686_REG_VALUE_08BIT,
					val);
		break;
	case V4L2_CID_VFLIP:
		ret = OV4686_read_reg(OV4686->client, OV4686_VFLIP_REG,
				       OV4686_REG_VALUE_08BIT,
				       &val);
		if (ctrl->val)
			val |= FLIP_BIT_MASK;
		else
			val &= ~FLIP_BIT_MASK;
		ret = OV4686_write_reg(OV4686->client, OV4686_VFLIP_REG,
					OV4686_REG_VALUE_08BIT,
					val);
		break;
	default:
		dev_warn(&client->dev, "%s Unhandled id:0x%x, val:0x%x\n",
			 __func__, ctrl->id, ctrl->val);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops OV4686_ctrl_ops = {
	.s_ctrl = OV4686_set_ctrl,
};

static int OV4686_initialize_controls(struct OV4686 *OV4686)
{
	const struct OV4686_mode *mode;
	struct v4l2_ctrl_handler *handler;
	struct v4l2_ctrl *ctrl;
	s64 exposure_max, vblank_def;
	u32 h_blank;
	int ret;

	handler = &OV4686->ctrl_handler;
	mode = OV4686->cur_mode;
	ret = v4l2_ctrl_handler_init(handler, 9);
	if (ret)
		return ret;
	handler->lock = &OV4686->mutex;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
				      0, 0, link_freq_menu_items);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, OV4686_PIXEL_RATE, 1, OV4686_PIXEL_RATE);

	h_blank = mode->hts_def - mode->width;
	OV4686->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
				h_blank, h_blank, 1, h_blank);
	if (OV4686->hblank)
		OV4686->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	vblank_def = mode->vts_def - mode->height;
	OV4686->vblank = v4l2_ctrl_new_std(handler, &OV4686_ctrl_ops,
				V4L2_CID_VBLANK, vblank_def,
				OV4686_VTS_MAX - mode->height,
				1, vblank_def);

	exposure_max = mode->vts_def - 4;
	OV4686->exposure = v4l2_ctrl_new_std(handler, &OV4686_ctrl_ops,
				V4L2_CID_EXPOSURE, OV4686_EXPOSURE_MIN,
				exposure_max, OV4686_EXPOSURE_STEP,
				mode->exp_def);

	OV4686->anal_gain = v4l2_ctrl_new_std(handler, &OV4686_ctrl_ops,
				V4L2_CID_ANALOGUE_GAIN, OV4686_GAIN_MIN,
				OV4686_GAIN_MAX, OV4686_GAIN_STEP,
				OV4686_GAIN_DEFAULT);

	OV4686->test_pattern = v4l2_ctrl_new_std_menu_items(handler,
				&OV4686_ctrl_ops, V4L2_CID_TEST_PATTERN,
				ARRAY_SIZE(OV4686_test_pattern_menu) - 1,
				0, 0, OV4686_test_pattern_menu);
	v4l2_ctrl_new_std(handler, &OV4686_ctrl_ops,
				V4L2_CID_HFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std(handler, &OV4686_ctrl_ops,
				V4L2_CID_VFLIP, 0, 1, 1, 0);

	if (handler->error) {
		ret = handler->error;
		dev_err(&OV4686->client->dev,
			"Failed to init controls(%d)\n", ret);
		goto err_free_handler;
	}

	OV4686->subdev.ctrl_handler = handler;
	OV4686->has_init_exp = false;

	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int OV4686_check_sensor_id(struct OV4686 *OV4686,
				  struct i2c_client *client)
{
	struct device *dev = &OV4686->client->dev;
	u32 id = 0;
	int ret;

	ret = OV4686_read_reg(client, OV4686_REG_CHIP_ID,
			      OV4686_REG_VALUE_16BIT, &id);
	if (id != CHIP_ID) {
		dev_err(dev, "Unexpected sensor id(%06x), ret(%d)\n", id, ret);
		return -ENODEV;
	}

	dev_info(dev, "Detected OV%06x sensor\n", CHIP_ID);

	return 0;
}

static int OV4686_configure_regulators(struct OV4686 *OV4686)
{
	unsigned int i;

	for (i = 0; i < OV4686_NUM_SUPPLIES; i++)
		OV4686->supplies[i].supply = OV4686_supply_names[i];

	return devm_regulator_bulk_get(&OV4686->client->dev,
				       OV4686_NUM_SUPPLIES,
				       OV4686->supplies);
}

static int OV4686_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct OV4686 *OV4686;
	struct v4l2_subdev *sd;
	char facing[2];
	int ret;
	u32 i, hdr_mode = 0;

	dev_info(dev, "driver version: %02x.%02x.%02x",
		DRIVER_VERSION >> 16,
		(DRIVER_VERSION & 0xff00) >> 8,
		DRIVER_VERSION & 0x00ff);

	OV4686 = devm_kzalloc(dev, sizeof(*OV4686), GFP_KERNEL);
	if (!OV4686)
		return -ENOMEM;

	of_property_read_u32(node, OF_CAMERA_HDR_MODE, &hdr_mode);
	ret = of_property_read_u32(node, RKMODULE_CAMERA_MODULE_INDEX,
				   &OV4686->module_index);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_FACING,
				       &OV4686->module_facing);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_MODULE_NAME,
				       &OV4686->module_name);
	ret |= of_property_read_string(node, RKMODULE_CAMERA_LENS_NAME,
				       &OV4686->len_name);
	if (ret) {
		dev_err(dev, "could not get module information!\n");
		return -EINVAL;
	}

	OV4686->client = client;
	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (hdr_mode == supported_modes[i].hdr_mode) {
			OV4686->cur_mode = &supported_modes[i];
			break;
		}
	}
	if (i == ARRAY_SIZE(supported_modes))
		OV4686->cur_mode = &supported_modes[0];

	OV4686->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(OV4686->xvclk)) {
		dev_err(dev, "Failed to get xvclk\n");
		return -EINVAL;
	}

	OV4686->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(OV4686->reset_gpio))
		dev_warn(dev, "Failed to get reset-gpios\n");

	OV4686->pwdn_gpio = devm_gpiod_get(dev, "pwdn", GPIOD_OUT_LOW);
	if (IS_ERR(OV4686->pwdn_gpio))
		dev_warn(dev, "Failed to get pwdn-gpios\n");

	OV4686->pinctrl = devm_pinctrl_get(dev);
	if (!IS_ERR(OV4686->pinctrl)) {
		OV4686->pins_default =
			pinctrl_lookup_state(OV4686->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_DEFAULT);
		if (IS_ERR(OV4686->pins_default))
			dev_err(dev, "could not get default pinstate\n");

		OV4686->pins_sleep =
			pinctrl_lookup_state(OV4686->pinctrl,
					     OF_CAMERA_PINCTRL_STATE_SLEEP);
		if (IS_ERR(OV4686->pins_sleep))
			dev_err(dev, "could not get sleep pinstate\n");
	} else {
		dev_err(dev, "no pinctrl\n");
	}

	ret = OV4686_configure_regulators(OV4686);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&OV4686->mutex);

	sd = &OV4686->subdev;
	v4l2_i2c_subdev_init(sd, client, &OV4686_subdev_ops);
	ret = OV4686_initialize_controls(OV4686);
	if (ret)
		goto err_destroy_mutex;

	ret = __OV4686_power_on(OV4686);
	if (ret)
		goto err_free_handler;

	ret = OV4686_check_sensor_id(OV4686, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	sd->internal_ops = &OV4686_internal_ops;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		     V4L2_SUBDEV_FL_HAS_EVENTS;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	OV4686->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &OV4686->pad);
	if (ret < 0)
		goto err_power_off;
#endif

	memset(facing, 0, sizeof(facing));
	if (strcmp(OV4686->module_facing, "back") == 0)
		facing[0] = 'b';
	else
		facing[0] = 'f';

	snprintf(sd->name, sizeof(sd->name), "m%02d_%s_%s %s",
		 OV4686->module_index, facing,
		 OV4686_NAME, dev_name(sd->dev));
	ret = v4l2_async_register_subdev_sensor_common(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_clean_entity;
	}

	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	return 0;

err_clean_entity:
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
err_power_off:
	__OV4686_power_off(OV4686);
err_free_handler:
	v4l2_ctrl_handler_free(&OV4686->ctrl_handler);
err_destroy_mutex:
	mutex_destroy(&OV4686->mutex);

	return ret;
}

static int OV4686_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct OV4686 *OV4686 = to_OV4686(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	v4l2_ctrl_handler_free(&OV4686->ctrl_handler);
	mutex_destroy(&OV4686->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__OV4686_power_off(OV4686);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id OV4686_of_match[] = {
	{ .compatible = "ovti,OV4686" },
	{},
};
MODULE_DEVICE_TABLE(of, OV4686_of_match);
#endif

static const struct i2c_device_id OV4686_match_id[] = {
	{ "ovti,OV4686", 0 },
	{ },
};

static struct i2c_driver OV4686_i2c_driver = {
	.driver = {
		.name = OV4686_NAME,
		.pm = &OV4686_pm_ops,
		.of_match_table = of_match_ptr(OV4686_of_match),
	},
	.probe		= &OV4686_probe,
	.remove		= &OV4686_remove,
	.id_table	= OV4686_match_id,
};

static int __init sensor_mod_init(void)
{
	return i2c_add_driver(&OV4686_i2c_driver);
}

static void __exit sensor_mod_exit(void)
{
	i2c_del_driver(&OV4686_i2c_driver);
}

device_initcall_sync(sensor_mod_init);
module_exit(sensor_mod_exit);

MODULE_DESCRIPTION("OmniVision OV4686 sensor driver");
MODULE_LICENSE("GPL v2");
