// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023 Intel Corporation.
 */

#include <asm/unaligned.h>

#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fwnode.h>

#define OV01A10_LINK_FREQ_400MHZ	400000000ULL
#define OV01A10_SCLK			40000000LL
#define OV01A10_DATA_LANES		1

#define OV01A10_REG_CHIP_ID		0x300a
#define OV01A10_CHIP_ID			0x560141

#define OV01A10_REG_MODE_SELECT		0x0100
#define OV01A10_MODE_STANDBY		0x00
#define OV01A10_MODE_STREAMING		0x01

/* pixel array */
#define OV01A10_PIXEL_ARRAY_WIDTH	1296
#define OV01A10_PIXEL_ARRAY_HEIGHT	816
#define OV01A10_ACITVE_WIDTH		1280
#define OV01A10_ACITVE_HEIGHT		800

/* vertical and horizontal timings */
#define OV01A10_REG_VTS			0x380e
#define OV01A10_VTS_DEF			0x0380
#define OV01A10_VTS_MIN			0x0380
#define OV01A10_VTS_MAX			0xffff
#define OV01A10_HTS_DEF			1488

/* exposure controls */
#define OV01A10_REG_EXPOSURE		0x3501
#define OV01A10_EXPOSURE_MIN		4
#define OV01A10_EXPOSURE_MAX_MARGIN	8
#define OV01A10_EXPOSURE_STEP		1

/* analog gain controls */
#define OV01A10_REG_ANALOG_GAIN		0x3508
#define OV01A10_ANAL_GAIN_MIN		0x100
#define OV01A10_ANAL_GAIN_MAX		0xffff
#define OV01A10_ANAL_GAIN_STEP		1

/* digital gain controls */
#define OV01A10_REG_DIGITAL_GAIN_B	0x350a
#define OV01A10_REG_DIGITAL_GAIN_GB	0x3510
#define OV01A10_REG_DIGITAL_GAIN_GR	0x3513
#define OV01A10_REG_DIGITAL_GAIN_R	0x3516
#define OV01A10_DGTL_GAIN_MIN		0
#define OV01A10_DGTL_GAIN_MAX		0x3ffff
#define OV01A10_DGTL_GAIN_STEP		1
#define OV01A10_DGTL_GAIN_DEFAULT	1024

/* test pattern control */
#define OV01A10_REG_TEST_PATTERN	0x4503
#define OV01A10_TEST_PATTERN_ENABLE	BIT(7)
#define OV01A10_LINK_FREQ_400MHZ_INDEX	0

/* flip and mirror control */
#define OV01A10_REG_FORMAT1		0x3820
#define OV01A10_VFLIP_MASK		BIT(4)
#define OV01A10_HFLIP_MASK		BIT(3)

/* window offset */
#define OV01A10_REG_X_WIN		0x3811
#define OV01A10_REG_Y_WIN		0x3813

struct ov01a10_reg {
	u16 address;
	u8 val;
};

struct ov01a10_reg_list {
	u32 num_of_regs;
	const struct ov01a10_reg *regs;
};

struct ov01a10_link_freq_config {
	const struct ov01a10_reg_list reg_list;
};

struct ov01a10_mode {
	u32 width;
	u32 height;
	u32 hts;
	u32 vts_def;
	u32 vts_min;
	u32 link_freq_index;

	const struct ov01a10_reg_list reg_list;
};

static const struct ov01a10_reg mipi_data_rate_720mbps[] = {
	{0x0103, 0x01},
	{0x0302, 0x00},
	{0x0303, 0x06},
	{0x0304, 0x01},
	{0x0305, 0xe0},
	{0x0306, 0x00},
	{0x0308, 0x01},
	{0x0309, 0x00},
	{0x030c, 0x01},
	{0x0322, 0x01},
	{0x0323, 0x06},
	{0x0324, 0x01},
	{0x0325, 0x68},
};

static const struct ov01a10_reg sensor_1280x800_setting[] = {
	{0x3002, 0xa1},
	{0x301e, 0xf0},
	{0x3022, 0x01},
	{0x3501, 0x03},
	{0x3502, 0x78},
	{0x3504, 0x0c},
	{0x3508, 0x01},
	{0x3509, 0x00},
	{0x3601, 0xc0},
	{0x3603, 0x71},
	{0x3610, 0x68},
	{0x3611, 0x86},
	{0x3640, 0x10},
	{0x3641, 0x80},
	{0x3642, 0xdc},
	{0x3646, 0x55},
	{0x3647, 0x57},
	{0x364b, 0x00},
	{0x3653, 0x10},
	{0x3655, 0x00},
	{0x3656, 0x00},
	{0x365f, 0x0f},
	{0x3661, 0x45},
	{0x3662, 0x24},
	{0x3663, 0x11},
	{0x3664, 0x07},
	{0x3709, 0x34},
	{0x370b, 0x6f},
	{0x3714, 0x22},
	{0x371b, 0x27},
	{0x371c, 0x67},
	{0x371d, 0xa7},
	{0x371e, 0xe7},
	{0x3730, 0x81},
	{0x3733, 0x10},
	{0x3734, 0x40},
	{0x3737, 0x04},
	{0x3739, 0x1c},
	{0x3767, 0x00},
	{0x376c, 0x81},
	{0x3772, 0x14},
	{0x37c2, 0x04},
	{0x37d8, 0x03},
	{0x37d9, 0x0c},
	{0x37e0, 0x00},
	{0x37e1, 0x08},
	{0x37e2, 0x10},
	{0x37e3, 0x04},
	{0x37e4, 0x04},
	{0x37e5, 0x03},
	{0x37e6, 0x04},
	{0x3800, 0x00},
	{0x3801, 0x00},
	{0x3802, 0x00},
	{0x3803, 0x00},
	{0x3804, 0x05},
	{0x3805, 0x0f},
	{0x3806, 0x03},
	{0x3807, 0x2f},
	{0x3808, 0x05},
	{0x3809, 0x00},
	{0x380a, 0x03},
	{0x380b, 0x20},
	{0x380c, 0x02},
	{0x380d, 0xe8},
	{0x380e, 0x03},
	{0x380f, 0x80},
	{0x3810, 0x00},
	{0x3811, 0x08},
	{0x3812, 0x00},
	{0x3813, 0x08},
	{0x3814, 0x01},
	{0x3815, 0x01},
	{0x3816, 0x01},
	{0x3817, 0x01},
	{0x3820, 0xa0},
	{0x3822, 0x13},
	{0x3832, 0x28},
	{0x3833, 0x10},
	{0x3b00, 0x00},
	{0x3c80, 0x00},
	{0x3c88, 0x02},
	{0x3c8c, 0x07},
	{0x3c8d, 0x40},
	{0x3cc7, 0x80},
	{0x4000, 0xc3},
	{0x4001, 0xe0},
	{0x4003, 0x40},
	{0x4008, 0x02},
	{0x4009, 0x19},
	{0x400a, 0x01},
	{0x400b, 0x6c},
	{0x4011, 0x00},
	{0x4041, 0x00},
	{0x4300, 0xff},
	{0x4301, 0x00},
	{0x4302, 0x0f},
	{0x4503, 0x00},
	{0x4601, 0x50},
	{0x4800, 0x64},
	{0x481f, 0x34},
	{0x4825, 0x33},
	{0x4837, 0x11},
	{0x4881, 0x40},
	{0x4883, 0x01},
	{0x4890, 0x00},
	{0x4901, 0x00},
	{0x4902, 0x00},
	{0x4b00, 0x2a},
	{0x4b0d, 0x00},
	{0x450a, 0x04},
	{0x450b, 0x00},
	{0x5000, 0x65},
	{0x5200, 0x18},
	{0x5004, 0x00},
	{0x5080, 0x40},
	{0x0305, 0xf4},
	{0x0325, 0xc2},
};

static const char * const ov01a10_test_pattern_menu[] = {
	"Disabled",
	"Color Bar",
	"Top-Bottom Darker Color Bar",
	"Right-Left Darker Color Bar",
	"Color Bar type 4",
};

static const s64 link_freq_menu_items[] = {
	OV01A10_LINK_FREQ_400MHZ,
};

static const struct ov01a10_link_freq_config link_freq_configs[] = {
	[OV01A10_LINK_FREQ_400MHZ_INDEX] = {
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(mipi_data_rate_720mbps),
			.regs = mipi_data_rate_720mbps,
		}
	},
};

static const struct ov01a10_mode supported_modes[] = {
	{
		.width = OV01A10_ACITVE_WIDTH,
		.height = OV01A10_ACITVE_HEIGHT,
		.hts = OV01A10_HTS_DEF,
		.vts_def = OV01A10_VTS_DEF,
		.vts_min = OV01A10_VTS_MIN,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(sensor_1280x800_setting),
			.regs = sensor_1280x800_setting,
		},
		.link_freq_index = OV01A10_LINK_FREQ_400MHZ_INDEX,
	},
};

struct ov01a10 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;

	/* v4l2 controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *exposure;

	const struct ov01a10_mode *cur_mode;
};

static inline struct ov01a10 *to_ov01a10(struct v4l2_subdev *subdev)
{
	return container_of(subdev, struct ov01a10, sd);
}

static int ov01a10_read_reg(struct ov01a10 *ov01a10, u16 reg, u16 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov01a10->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2];
	u8 data_buf[4] = {0};
	int ret = 0;

	if (len > sizeof(data_buf))
		return -EINVAL;

	put_unaligned_be16(reg, addr_buf);
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(addr_buf);
	msgs[0].buf = addr_buf;
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[sizeof(data_buf) - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));

	if (ret != ARRAY_SIZE(msgs))
		return ret < 0 ? ret : -EIO;

	*val = get_unaligned_be32(data_buf);

	return 0;
}

static int ov01a10_write_reg(struct ov01a10 *ov01a10, u16 reg, u16 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov01a10->sd);
	u8 buf[6];
	int ret = 0;

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << 8 * (4 - len), buf + 2);

	ret = i2c_master_send(client, buf, len + 2);
	if (ret != len + 2)
		return ret < 0 ? ret : -EIO;

	return 0;
}

static int ov01a10_write_reg_list(struct ov01a10 *ov01a10,
				  const struct ov01a10_reg_list *r_list)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov01a10->sd);
	unsigned int i;
	int ret = 0;

	for (i = 0; i < r_list->num_of_regs; i++) {
		ret = ov01a10_write_reg(ov01a10, r_list->regs[i].address, 1,
					r_list->regs[i].val);
		if (ret) {
			dev_err_ratelimited(&client->dev,
					    "write reg 0x%4.4x err = %d\n",
					    r_list->regs[i].address, ret);
			return ret;
		}
	}

	return 0;
}

static int ov01a10_update_digital_gain(struct ov01a10 *ov01a10, u32 d_gain)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov01a10->sd);
	u32 real = d_gain << 6;
	int ret = 0;

	ret = ov01a10_write_reg(ov01a10, OV01A10_REG_DIGITAL_GAIN_B, 3, real);
	if (ret) {
		dev_err(&client->dev, "failed to set DIGITAL_GAIN_B\n");
		return ret;
	}

	ret = ov01a10_write_reg(ov01a10, OV01A10_REG_DIGITAL_GAIN_GB, 3, real);
	if (ret) {
		dev_err(&client->dev, "failed to set DIGITAL_GAIN_GB\n");
		return ret;
	}

	ret = ov01a10_write_reg(ov01a10, OV01A10_REG_DIGITAL_GAIN_GR, 3, real);
	if (ret) {
		dev_err(&client->dev, "failed to set DIGITAL_GAIN_GR\n");
		return ret;
	}

	ret = ov01a10_write_reg(ov01a10, OV01A10_REG_DIGITAL_GAIN_R, 3, real);
	if (ret)
		dev_err(&client->dev, "failed to set DIGITAL_GAIN_R\n");

	return ret;
}

static int ov01a10_test_pattern(struct ov01a10 *ov01a10, u32 pattern)
{
	if (!pattern)
		return 0;

	pattern = (pattern - 1) | OV01A10_TEST_PATTERN_ENABLE;

	return ov01a10_write_reg(ov01a10, OV01A10_REG_TEST_PATTERN, 1, pattern);
}

/* for vflip and hflip, use 0x9 as window offset to keep the bayer */
static int ov01a10_set_hflip(struct ov01a10 *ov01a10, u32 hflip)
{
	int ret;
	u32 val, offset;

	offset = hflip ? 0x9 : 0x8;
	ret = ov01a10_write_reg(ov01a10, OV01A10_REG_X_WIN, 1, offset);
	if (ret)
		return ret;

	ret = ov01a10_read_reg(ov01a10, OV01A10_REG_FORMAT1, 1, &val);
	if (ret)
		return ret;

	val = hflip ? val | FIELD_PREP(OV01A10_HFLIP_MASK, 0x1) :
		val & ~OV01A10_HFLIP_MASK;

	return ov01a10_write_reg(ov01a10, OV01A10_REG_FORMAT1, 1, val);
}

static int ov01a10_set_vflip(struct ov01a10 *ov01a10, u32 vflip)
{
	int ret;
	u32 val, offset;

	offset = vflip ? 0x9 : 0x8;
	ret = ov01a10_write_reg(ov01a10, OV01A10_REG_Y_WIN, 1, offset);
	if (ret)
		return ret;

	ret = ov01a10_read_reg(ov01a10, OV01A10_REG_FORMAT1, 1, &val);
	if (ret)
		return ret;

	val = vflip ? val | FIELD_PREP(OV01A10_VFLIP_MASK, 0x1) :
		val & ~OV01A10_VFLIP_MASK;

	return ov01a10_write_reg(ov01a10, OV01A10_REG_FORMAT1, 1, val);
}

static int ov01a10_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ov01a10 *ov01a10 = container_of(ctrl->handler,
					       struct ov01a10, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ov01a10->sd);
	s64 exposure_max;
	int ret = 0;

	if (ctrl->id == V4L2_CID_VBLANK) {
		exposure_max = ov01a10->cur_mode->height + ctrl->val -
			OV01A10_EXPOSURE_MAX_MARGIN;
		__v4l2_ctrl_modify_range(ov01a10->exposure,
					 ov01a10->exposure->minimum,
					 exposure_max, ov01a10->exposure->step,
					 exposure_max);
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = ov01a10_write_reg(ov01a10, OV01A10_REG_ANALOG_GAIN, 2,
					ctrl->val);
		break;

	case V4L2_CID_DIGITAL_GAIN:
		ret = ov01a10_update_digital_gain(ov01a10, ctrl->val);
		break;

	case V4L2_CID_EXPOSURE:
		ret = ov01a10_write_reg(ov01a10, OV01A10_REG_EXPOSURE, 2,
					ctrl->val);
		break;

	case V4L2_CID_VBLANK:
		ret = ov01a10_write_reg(ov01a10, OV01A10_REG_VTS, 2,
					ov01a10->cur_mode->height + ctrl->val);
		break;

	case V4L2_CID_TEST_PATTERN:
		ret = ov01a10_test_pattern(ov01a10, ctrl->val);
		break;

	case V4L2_CID_HFLIP:
		ov01a10_set_hflip(ov01a10, ctrl->val);
		break;

	case V4L2_CID_VFLIP:
		ov01a10_set_vflip(ov01a10, ctrl->val);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ov01a10_ctrl_ops = {
	.s_ctrl = ov01a10_set_ctrl,
};

static int ov01a10_init_controls(struct ov01a10 *ov01a10)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov01a10->sd);
	struct v4l2_fwnode_device_properties props;
	u32 vblank_min, vblank_max, vblank_default;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	const struct ov01a10_mode *cur_mode;
	s64 exposure_max, h_blank;
	int ret = 0;
	int size;

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		return ret;

	ctrl_hdlr = &ov01a10->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 12);
	if (ret)
		return ret;

	cur_mode = ov01a10->cur_mode;
	size = ARRAY_SIZE(link_freq_menu_items);

	ov01a10->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr,
						    &ov01a10_ctrl_ops,
						    V4L2_CID_LINK_FREQ,
						    size - 1, 0,
						    link_freq_menu_items);
	if (ov01a10->link_freq)
		ov01a10->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ov01a10->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops,
						V4L2_CID_PIXEL_RATE, 0,
						OV01A10_SCLK, 1, OV01A10_SCLK);

	vblank_min = cur_mode->vts_min - cur_mode->height;
	vblank_max = OV01A10_VTS_MAX - cur_mode->height;
	vblank_default = cur_mode->vts_def - cur_mode->height;
	ov01a10->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops,
					    V4L2_CID_VBLANK, vblank_min,
					    vblank_max, 1, vblank_default);

	h_blank = cur_mode->hts - cur_mode->width;
	ov01a10->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops,
					    V4L2_CID_HBLANK, h_blank, h_blank,
					    1, h_blank);
	if (ov01a10->hblank)
		ov01a10->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  OV01A10_ANAL_GAIN_MIN, OV01A10_ANAL_GAIN_MAX,
			  OV01A10_ANAL_GAIN_STEP, OV01A10_ANAL_GAIN_MIN);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  OV01A10_DGTL_GAIN_MIN, OV01A10_DGTL_GAIN_MAX,
			  OV01A10_DGTL_GAIN_STEP, OV01A10_DGTL_GAIN_DEFAULT);

	exposure_max = cur_mode->vts_def - OV01A10_EXPOSURE_MAX_MARGIN;
	ov01a10->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops,
					      V4L2_CID_EXPOSURE,
					      OV01A10_EXPOSURE_MIN,
					      exposure_max,
					      OV01A10_EXPOSURE_STEP,
					      exposure_max);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ov01a10_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ov01a10_test_pattern_menu) - 1,
				     0, 0, ov01a10_test_pattern_menu);

	v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops, V4L2_CID_HFLIP,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(ctrl_hdlr, &ov01a10_ctrl_ops, V4L2_CID_VFLIP,
			  0, 1, 1, 0);

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ov01a10_ctrl_ops,
					      &props);
	if (ret)
		goto fail;

	if (ctrl_hdlr->error) {
		ret = ctrl_hdlr->error;
		goto fail;
	}

	ov01a10->sd.ctrl_handler = ctrl_hdlr;

	return 0;
fail:
	v4l2_ctrl_handler_free(ctrl_hdlr);

	return ret;
}

static void ov01a10_update_pad_format(const struct ov01a10_mode *mode,
				      struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = MEDIA_BUS_FMT_SBGGR10_1X10;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_RAW;
}

static int ov01a10_start_streaming(struct ov01a10 *ov01a10)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov01a10->sd);
	const struct ov01a10_reg_list *reg_list;
	int link_freq_index;
	int ret = 0;

	link_freq_index = ov01a10->cur_mode->link_freq_index;
	reg_list = &link_freq_configs[link_freq_index].reg_list;
	ret = ov01a10_write_reg_list(ov01a10, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set plls\n");
		return ret;
	}

	reg_list = &ov01a10->cur_mode->reg_list;
	ret = ov01a10_write_reg_list(ov01a10, reg_list);
	if (ret) {
		dev_err(&client->dev, "failed to set mode\n");
		return ret;
	}

	ret = __v4l2_ctrl_handler_setup(ov01a10->sd.ctrl_handler);
	if (ret)
		return ret;

	ret = ov01a10_write_reg(ov01a10, OV01A10_REG_MODE_SELECT, 1,
				OV01A10_MODE_STREAMING);
	if (ret)
		dev_err(&client->dev, "failed to start streaming\n");

	return ret;
}

static void ov01a10_stop_streaming(struct ov01a10 *ov01a10)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov01a10->sd);
	int ret = 0;

	ret = ov01a10_write_reg(ov01a10, OV01A10_REG_MODE_SELECT, 1,
				OV01A10_MODE_STANDBY);
	if (ret)
		dev_err(&client->dev, "failed to stop streaming\n");
}

static int ov01a10_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ov01a10 *ov01a10 = to_ov01a10(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct v4l2_subdev_state *state;
	int ret = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto unlock;

		ret = ov01a10_start_streaming(ov01a10);
		if (ret) {
			pm_runtime_put(&client->dev);
			goto unlock;
		}
	} else {
		ov01a10_stop_streaming(ov01a10);
		pm_runtime_put(&client->dev);
	}

unlock:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static int ov01a10_set_format(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *sd_state,
			      struct v4l2_subdev_format *fmt)
{
	struct ov01a10 *ov01a10 = to_ov01a10(sd);
	const struct ov01a10_mode *mode;
	struct v4l2_mbus_framefmt *format;
	s32 vblank_def, h_blank;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes), width,
				      height, fmt->format.width,
				      fmt->format.height);

	ov01a10_update_pad_format(mode, &fmt->format);

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE) {
		ov01a10->cur_mode = mode;
		__v4l2_ctrl_s_ctrl(ov01a10->link_freq, mode->link_freq_index);
		__v4l2_ctrl_s_ctrl_int64(ov01a10->pixel_rate, OV01A10_SCLK);

		vblank_def = mode->vts_def - mode->height;
		__v4l2_ctrl_modify_range(ov01a10->vblank,
					 mode->vts_min - mode->height,
					 OV01A10_VTS_MAX - mode->height, 1,
					 vblank_def);
		__v4l2_ctrl_s_ctrl(ov01a10->vblank, vblank_def);
		h_blank = mode->hts - mode->width;
		__v4l2_ctrl_modify_range(ov01a10->hblank, h_blank, h_blank, 1,
					 h_blank);
	}

	format = v4l2_subdev_state_get_format(sd_state, fmt->stream);
	*format = fmt->format;

	return 0;
}

static int ov01a10_init_state(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.format = {
			.width = OV01A10_ACITVE_WIDTH,
			.height = OV01A10_ACITVE_HEIGHT,
		},
	};

	ov01a10_set_format(sd, state, &fmt);

	return 0;
}

static int ov01a10_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SBGGR10_1X10;

	return 0;
}

static int ov01a10_enum_frame_size(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *sd_state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes) ||
	    fse->code != MEDIA_BUS_FMT_SBGGR10_1X10)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

static int ov01a10_get_selection(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_selection *sel)
{
	if (sel->which != V4L2_SUBDEV_FORMAT_ACTIVE)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_NATIVE_SIZE:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = OV01A10_PIXEL_ARRAY_WIDTH;
		sel->r.height = OV01A10_PIXEL_ARRAY_HEIGHT;
		return 0;
	case V4L2_SEL_TGT_CROP:
	case V4L2_SEL_TGT_CROP_DEFAULT:
		sel->r.top = (OV01A10_PIXEL_ARRAY_HEIGHT -
			      OV01A10_ACITVE_HEIGHT) / 2;
		sel->r.left = (OV01A10_PIXEL_ARRAY_WIDTH -
			       OV01A10_ACITVE_WIDTH) / 2;
		sel->r.width = OV01A10_ACITVE_WIDTH;
		sel->r.height = OV01A10_ACITVE_HEIGHT;
		return 0;
	}

	return -EINVAL;
}

static const struct v4l2_subdev_core_ops ov01a10_core_ops = {
	.log_status = v4l2_ctrl_subdev_log_status,
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops ov01a10_video_ops = {
	.s_stream = ov01a10_set_stream,
};

static const struct v4l2_subdev_pad_ops ov01a10_pad_ops = {
	.set_fmt = ov01a10_set_format,
	.get_fmt = v4l2_subdev_get_fmt,
	.get_selection = ov01a10_get_selection,
	.enum_mbus_code = ov01a10_enum_mbus_code,
	.enum_frame_size = ov01a10_enum_frame_size,
};

static const struct v4l2_subdev_ops ov01a10_subdev_ops = {
	.core = &ov01a10_core_ops,
	.video = &ov01a10_video_ops,
	.pad = &ov01a10_pad_ops,
};

static const struct v4l2_subdev_internal_ops ov01a10_internal_ops = {
	.init_state = ov01a10_init_state,
};

static const struct media_entity_operations ov01a10_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static int ov01a10_identify_module(struct ov01a10 *ov01a10)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ov01a10->sd);
	int ret;
	u32 val;

	ret = ov01a10_read_reg(ov01a10, OV01A10_REG_CHIP_ID, 3, &val);
	if (ret)
		return ret;

	if (val != OV01A10_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%x\n",
			OV01A10_CHIP_ID, val);
		return -EIO;
	}

	return 0;
}

static void ov01a10_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static int ov01a10_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ov01a10 *ov01a10;
	int ret = 0;

	ov01a10 = devm_kzalloc(dev, sizeof(*ov01a10), GFP_KERNEL);
	if (!ov01a10)
		return -ENOMEM;

	v4l2_i2c_subdev_init(&ov01a10->sd, client, &ov01a10_subdev_ops);
	ov01a10->sd.internal_ops = &ov01a10_internal_ops;

	ret = ov01a10_identify_module(ov01a10);
	if (ret)
		return dev_err_probe(dev, ret,
				     "failed to find sensor\n");

	ov01a10->cur_mode = &supported_modes[0];

	ret = ov01a10_init_controls(ov01a10);
	if (ret) {
		dev_err(dev, "failed to init controls: %d\n", ret);
		return ret;
	}

	ov01a10->sd.state_lock = ov01a10->ctrl_handler.lock;
	ov01a10->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
		V4L2_SUBDEV_FL_HAS_EVENTS;
	ov01a10->sd.entity.ops = &ov01a10_subdev_entity_ops;
	ov01a10->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ov01a10->pad.flags = MEDIA_PAD_FL_SOURCE;

	ret = media_entity_pads_init(&ov01a10->sd.entity, 1, &ov01a10->pad);
	if (ret) {
		dev_err(dev, "Failed to init entity pads: %d\n", ret);
		goto err_handler_free;
	}

	ret = v4l2_subdev_init_finalize(&ov01a10->sd);
	if (ret) {
		dev_err(dev, "Failed to allocate subdev state: %d\n", ret);
		goto err_media_entity_cleanup;
	}

	/*
	 * Device is already turned on by i2c-core with ACPI domain PM.
	 * Enable runtime PM and turn off the device.
	 */
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(dev);
	pm_runtime_idle(dev);

	ret = v4l2_async_register_subdev_sensor(&ov01a10->sd);
	if (ret < 0) {
		dev_err(dev, "Failed to register subdev: %d\n", ret);
		goto err_pm_disable;
	}

	return 0;

err_pm_disable:
	pm_runtime_disable(dev);
	pm_runtime_set_suspended(&client->dev);

err_media_entity_cleanup:
	media_entity_cleanup(&ov01a10->sd.entity);

err_handler_free:
	v4l2_ctrl_handler_free(ov01a10->sd.ctrl_handler);

	return ret;
}

#ifdef CONFIG_ACPI
static const struct acpi_device_id ov01a10_acpi_ids[] = {
	{ "OVTI01A0" },
	{ }
};

MODULE_DEVICE_TABLE(acpi, ov01a10_acpi_ids);
#endif

static struct i2c_driver ov01a10_i2c_driver = {
	.driver = {
		.name = "ov01a10",
		.acpi_match_table = ACPI_PTR(ov01a10_acpi_ids),
	},
	.probe = ov01a10_probe,
	.remove = ov01a10_remove,
};

module_i2c_driver(ov01a10_i2c_driver);

MODULE_AUTHOR("Bingbu Cao <bingbu.cao@intel.com>");
MODULE_AUTHOR("Wang Yating <yating.wang@intel.com>");
MODULE_DESCRIPTION("OmniVision OV01A10 sensor driver");
MODULE_LICENSE("GPL");
