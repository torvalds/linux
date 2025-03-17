// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Vitalii Skorkin <nikroksm@mail.ru>
 */

#include <linux/unaligned.h>

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>

#define to_s5k5e9(_sd) container_of(_sd, struct s5k5e9, sd)

struct s5k5e9_reg {
	u16 address;
	u32 val;
};

struct s5k5e9_reg_list {
	u32 num_of_regs;
	const struct s5k5e9_reg *regs;
};

struct s5k5e9_mode {
	u32 width;
	u32 height;
	u32 hts;
	u32 vts;
	s64 link_freq;
	u32 lane_count;
	u32 depth;
	const struct s5k5e9_reg_list reg_list;
	u32 mbus_code;
};

static const struct s5k5e9_reg s5k5e9_regs[] = {
	{0x0100, 0x0000},
	{0x3b45, 0x0001},
	{0x0b05, 0x0001},
	{0x392f, 0x0001},
	{0x3930, 0x0000},
	{0x3924, 0x007f},
	{0x3925, 0x00fd},
	{0x3c08, 0x00ff},
	{0x3c09, 0x00ff},
	{0x3c0a, 0x0005},
	{0x3c31, 0x00ff},
	{0x3c32, 0x00ff},
	{0x3290, 0x0010},
	{0x3200, 0x0001},
	{0x3074, 0x0006},
	{0x3075, 0x002f},
	{0x308a, 0x0020},
	{0x308b, 0x0008},
	{0x308c, 0x000b},
	{0x3081, 0x0007},
	{0x307b, 0x0085},
	{0x307a, 0x000a},
	{0x3079, 0x000a},
	{0x306e, 0x0071},
	{0x306f, 0x0028},
	{0x301f, 0x0020},
	{0x3012, 0x004e},
	{0x306b, 0x009a},
	{0x3091, 0x0016},
	{0x30c4, 0x0006},
	{0x306a, 0x0079},
	{0x30b0, 0x00ff},
	{0x306d, 0x0008},
	{0x3084, 0x0016},
	{0x3070, 0x000f},
	{0x30c2, 0x0005},
	{0x3069, 0x0087},
	{0x3c0f, 0x0000},
	{0x3083, 0x0014},
	{0x3080, 0x0008},
	{0x3c34, 0x00ea},
	{0x3c35, 0x005c},
};

static const struct s5k5e9_reg s5k5e9_2592x1944_2lane_regs[] = {
	{0x0100, 0x0000},
	{0x0136, 0x0013},
	{0x0137, 0x0033},
	{0x0305, 0x0003},
	{0x0306, 0x0000},
	{0x0307, 0x0059},
	{0x030d, 0x0003},
	{0x030e, 0x0000},
	{0x030f, 0x0089},
	{0x3c1f, 0x0000},
	{0x3c17, 0x0000},
	{0x0112, 0x000a},
	{0x0113, 0x000a},
	{0x0114, 0x0001},
	{0x0820, 0x0003},
	{0x0821, 0x006c},
	{0x0822, 0x0000},
	{0x0823, 0x0000},
	{0x3929, 0x000f},
	{0x0344, 0x0000},
	{0x0345, 0x0008},
	{0x0346, 0x0000},
	{0x0347, 0x0008},
	{0x0348, 0x000a},
	{0x0349, 0x0027},
	{0x034a, 0x0007},
	{0x034b, 0x009f},
	{0x034c, 0x000a},
	{0x034d, 0x0020},
	{0x034e, 0x0007},
	{0x034f, 0x0098},
	{0x0900, 0x0000},
	{0x0901, 0x0000},
	{0x0381, 0x0001},
	{0x0383, 0x0001},
	{0x0385, 0x0001},
	{0x0387, 0x0001},
	{0x0101, 0x0000},
	{0x0340, 0x0007},
	{0x0341, 0x00ee},
	{0x0342, 0x000c},
	{0x0343, 0x0028},
	{0x0200, 0x000b},
	{0x0201, 0x009c},
	{0x0202, 0x0000},
	{0x0203, 0x0002},
	{0x30b8, 0x002e},
	{0x30ba, 0x0036},
};

static struct s5k5e9_mode s5k5e9_modes[] = {
	{
		.width = 2592,
		.height = 1944,
		.hts = 3112,
		.vts = 2030,
		.link_freq = 480000000,
		.lane_count = 2,
		.depth = 10,
		.reg_list = {
			.num_of_regs = ARRAY_SIZE(s5k5e9_2592x1944_2lane_regs),
			.regs = s5k5e9_2592x1944_2lane_regs,
		},
		.mbus_code = MEDIA_BUS_FMT_SGRBG10_1X10,
	},
};

static const char * const s5k5e9_supply_names[] = {
	"vana",
	"vdig",
	"vio",
};

struct s5k5e9 {
	struct clk *xvclk;
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *pixel_rate;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *exposure;
	struct s5k5e9_mode *cur_mode;
	struct regulator_bulk_data supplies[ARRAY_SIZE(s5k5e9_supply_names)];
	struct gpio_desc *reset_gpio;
};

static int s5k5e9_write(struct s5k5e9 *s5k5e9, u16 reg, u16 len, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k5e9->sd);
	u8 buf[6];

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, buf);
	put_unaligned_be32(val << 8 * (4 - len), buf + 2);
	if (i2c_master_send(client, buf, len + 2) != len + 2) {
		dev_err(&client->dev,
			"Cannot write register %u!\n", reg);
		return -EIO;
	}

	return 0;
}

static int s5k5e9_write_reg_list(struct s5k5e9 *s5k5e9, const struct s5k5e9_reg_list *reg_list)
{
	int ret = 0;

	for (unsigned int i = 0; i < reg_list->num_of_regs; i++)
		ret = s5k5e9_write(s5k5e9, reg_list->regs[i].address, 1, reg_list->regs[i].val);

	return ret;
}

static int s5k5e9_read(struct s5k5e9 *s5k5e9, u16 reg, u16 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k5e9->sd);
	struct i2c_msg msgs[2];
	u8 addr_buf[2];
	u8 data_buf[4] = {0};
	int ret;

	if (len > 4)
		return -EINVAL;

	put_unaligned_be16(reg, addr_buf);
	msgs[0].addr = client->addr;
	msgs[0].flags = 0;
	msgs[0].len = sizeof(addr_buf);
	msgs[0].buf = addr_buf;
	msgs[1].addr = client->addr;
	msgs[1].flags = I2C_M_RD;
	msgs[1].len = len;
	msgs[1].buf = &data_buf[4 - len];

	ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
	if (ret != ARRAY_SIZE(msgs)) {
		dev_err(&client->dev,
			"Cannot read register %u!\n", reg);
		return -EIO;
	}

	*val = get_unaligned_be32(data_buf);

	return 0;
}

static int s5k5e9_start_stream(struct s5k5e9 *s5k5e9,
				   struct v4l2_subdev_state *state)
{
	int ret;
	const struct s5k5e9_reg_list regs = {
		.num_of_regs = ARRAY_SIZE(s5k5e9_regs),
		.regs = s5k5e9_regs,
	};

	ret = s5k5e9_write_reg_list(s5k5e9, &regs);
	if (ret)
		return ret;

	ret = s5k5e9_write_reg_list(s5k5e9, &s5k5e9->cur_mode->reg_list);
	if (ret)
		return ret;

	ret = __v4l2_ctrl_handler_setup(&s5k5e9->ctrl_handler);
	if (ret)
		return ret;

	ret = s5k5e9_write(s5k5e9, 0x0100, 1, 0x01);
	if (ret)
		return ret;

	return 0;
}

static int s5k5e9_stop_stream(struct s5k5e9 *s5k5e9)
{
	int ret;

	ret = s5k5e9_write(s5k5e9, 0x0100, 1, 0x00);
	if (ret)
		return ret;

	return 0;
}

static int s5k5e9_s_stream(struct v4l2_subdev *sd, int on)
{
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	struct s5k5e9 *s5k5e9 = to_s5k5e9(sd);
	struct v4l2_subdev_state *state;
	int ret = 0;

	state = v4l2_subdev_lock_and_get_active_state(sd);

	if (on) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto unlock_and_return;

		ret = s5k5e9_start_stream(s5k5e9, state);
		if (ret) {
			dev_err(&client->dev, "Failed to start streaming\n");
			pm_runtime_put_sync(&client->dev);
			goto unlock_and_return;
		}
	} else {
		s5k5e9_stop_stream(s5k5e9);
		pm_runtime_mark_last_busy(&client->dev);
		pm_runtime_put_autosuspend(&client->dev);
	}

unlock_and_return:
	v4l2_subdev_unlock_state(state);

	return ret;
}

static int s5k5e9_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *state,
			  struct v4l2_subdev_format *fmt)
{
	struct s5k5e9 *s5k5e9 = to_s5k5e9(sd);
	struct s5k5e9_mode *mode;
	u64 pixel_rate;
	u32 v_blank;
	u32 h_blank;

	mode = v4l2_find_nearest_size(s5k5e9_modes, ARRAY_SIZE(s5k5e9_modes),
					  width, height, fmt->format.width,
					  fmt->format.height);

	fmt->format.code = mode->mbus_code;
	fmt->format.width = mode->width;
	fmt->format.height = mode->height;
	fmt->format.field = V4L2_FIELD_NONE;

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY) {
		*v4l2_subdev_state_get_format(state, 0) =  fmt->format;
	} else {
		s5k5e9->cur_mode = mode;
		pixel_rate = mode->link_freq * 2 * mode->lane_count / mode->depth;
		__v4l2_ctrl_s_ctrl_int64(s5k5e9->pixel_rate, pixel_rate);
		/* Update limits and set FPS to default */
		v_blank = mode->vts - mode->height;
		__v4l2_ctrl_modify_range(s5k5e9->vblank, v_blank,
					 0xffff - mode->height,
					 1, v_blank);
		__v4l2_ctrl_s_ctrl(s5k5e9->vblank, v_blank);
		h_blank = mode->hts - mode->width;
		__v4l2_ctrl_modify_range(s5k5e9->hblank, h_blank,
					 h_blank, 1, h_blank);
	}

	return 0;
}

static int s5k5e9_get_selection(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_selection *sel)
{
	struct s5k5e9 *s5k5e9 = to_s5k5e9(sd);

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_state_get_crop(sd_state, sel->pad);
		return 0;

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = s5k5e9->cur_mode->width;
		sel->r.height = s5k5e9->cur_mode->height;
		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = s5k5e9->cur_mode->width;
		sel->r.height = s5k5e9->cur_mode->height;
		return 0;
	}

	return -EINVAL;
}

static int s5k5e9_enum_frame_sizes(struct v4l2_subdev *sd,
				   struct v4l2_subdev_state *state,
				   struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(s5k5e9_modes))
		return -EINVAL;

	if (fse->code != s5k5e9_modes[fse->index].mbus_code)
		return -EINVAL;

	fse->min_width  = s5k5e9_modes[fse->index].width;
	fse->max_width  = s5k5e9_modes[fse->index].width;
	fse->max_height = s5k5e9_modes[fse->index].height;
	fse->min_height = s5k5e9_modes[fse->index].height;

	return 0;
}

static int s5k5e9_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	struct s5k5e9 *s5k5e9 = to_s5k5e9(sd);

	if (code->index != 0)
		return -EINVAL;

	code->code = s5k5e9->cur_mode->mbus_code;

	return 0;
}

static int s5k5e9_init_state(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state)
{
	struct s5k5e9 *s5k5e9 = to_s5k5e9(sd);
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.format = {
			.width = s5k5e9->cur_mode->width,
			.height = s5k5e9->cur_mode->height,
		},
	};

	s5k5e9_set_fmt(sd, sd_state, &fmt);

	return 0;
}

static int s5k5e9_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5k5e9 *s5k5e9 = container_of(ctrl->handler,
						 struct s5k5e9, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&s5k5e9->sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_state *state;
	u32 exposure_max;
	int ret;

	state = v4l2_subdev_get_locked_active_state(&s5k5e9->sd);
	format = v4l2_subdev_state_get_format(state, 0);

	/* Propagate change of current control to all related controls */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = s5k5e9->cur_mode->height + ctrl->val - 2;
		__v4l2_ctrl_modify_range(s5k5e9->exposure,
					 s5k5e9->exposure->minimum,
					 exposure_max, s5k5e9->exposure->step,
					 exposure_max);
	}

	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = s5k5e9_write(s5k5e9, 0x0104, 1, 0x01);
		ret = s5k5e9_write(s5k5e9, 0x0202, 2, ctrl->val);
		ret = s5k5e9_write(s5k5e9, 0x0104, 1, 0x00);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		ret = s5k5e9_write(s5k5e9, 0x0104, 1, 0x01);
		ret = s5k5e9_write(s5k5e9, 0x0204, 2, ctrl->val);
		ret = s5k5e9_write(s5k5e9, 0x0104, 1, 0x00);
		break;
	case V4L2_CID_VBLANK:
		ret = s5k5e9_write(s5k5e9, 0x0104, 1, 0x01);
		ret = s5k5e9_write(s5k5e9, 0x0340, 2, s5k5e9->cur_mode->height + ctrl->val);
		ret = s5k5e9_write(s5k5e9, 0x0104, 1, 0x00);
		break;
	default:
		ret = -EINVAL;
		dev_warn(&client->dev, "%s Unhandled id: 0x%x\n",
			 __func__, ctrl->id);
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_subdev_core_ops s5k5e9_core_ops = { };

static const struct v4l2_subdev_video_ops s5k5e9_video_ops = {
	.s_stream = s5k5e9_s_stream,
};

static const struct v4l2_subdev_pad_ops s5k5e9_pad_ops = {
	.enum_mbus_code = s5k5e9_enum_mbus_code,
	.enum_frame_size = s5k5e9_enum_frame_sizes,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = s5k5e9_set_fmt,
	.get_selection = s5k5e9_get_selection,
};

static const struct v4l2_subdev_ops s5k5e9_subdev_ops = {
	.core	= &s5k5e9_core_ops,
	.video	= &s5k5e9_video_ops,
	.pad	= &s5k5e9_pad_ops,
};

static const struct v4l2_subdev_internal_ops s5k5e9_internal_ops = {
	.init_state = s5k5e9_init_state,
};

static const struct v4l2_ctrl_ops s5k5e9_ctrl_ops = {
	.s_ctrl = s5k5e9_set_ctrl,
};

static int s5k5e9_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5k5e9 *s5k5e9 = to_s5k5e9(sd);
	int ret;

	gpiod_set_value_cansleep(s5k5e9->reset_gpio, 0);
	usleep_range(1000, 2000);

	ret = regulator_bulk_enable(ARRAY_SIZE(s5k5e9_supply_names),
					s5k5e9->supplies);
	if (ret) {
		dev_err(dev, "failed to enable regulators\n");
		return ret;
	}
	usleep_range(1000, 2000);

	ret = clk_prepare_enable(s5k5e9->xvclk);
	if (ret) {
		dev_err(dev, "Failed to enable xvclk\n");
		goto disable_regulator;
	}
	usleep_range(10000, 11000);

	gpiod_set_value_cansleep(s5k5e9->reset_gpio, 1);
	usleep_range(18000, 19000);

	return 0;

disable_regulator:
	regulator_bulk_disable(ARRAY_SIZE(s5k5e9_supply_names),
				   s5k5e9->supplies);
	return ret;
};

static int s5k5e9_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct s5k5e9 *s5k5e9 = to_s5k5e9(sd);

	clk_disable_unprepare(s5k5e9->xvclk);
	usleep_range(1000, 2000);

	gpiod_set_value_cansleep(s5k5e9->reset_gpio, 0);
	usleep_range(1000, 2000);

	regulator_bulk_disable(ARRAY_SIZE(s5k5e9_supply_names),
					  s5k5e9->supplies);
	return 0;
};

static int s5k5e9_init_ctrls(struct s5k5e9 *s5k5e9)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k5e9->sd);
	struct v4l2_ctrl_handler *handler = &s5k5e9->ctrl_handler;
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl *ctrl;
	struct s5k5e9_mode *mode = s5k5e9->cur_mode;
	u64 pixel_rate;
	u32 h_blank;
	u32 v_blank;
	u32 exposure_max;
	int ret;
	static s64 link_freq[] = {
		0
	};
	link_freq[0] = mode->link_freq;

	ret = v4l2_ctrl_handler_init(handler, 5);
	if (ret)
		return ret;

	ctrl = v4l2_ctrl_new_int_menu(handler, NULL, V4L2_CID_LINK_FREQ,
					  ARRAY_SIZE(link_freq) - 1, 0, link_freq);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	pixel_rate = mode->link_freq * 2 * mode->lane_count / mode->depth;
	s5k5e9->pixel_rate = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_PIXEL_RATE,
			  0, pixel_rate, 1, pixel_rate);

	h_blank = mode->hts - mode->width;
	s5k5e9->hblank = v4l2_ctrl_new_std(handler, NULL, V4L2_CID_HBLANK,
					   h_blank, h_blank, 1, h_blank);
	if (s5k5e9->hblank)
		s5k5e9->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v_blank = mode->vts - mode->height;
	s5k5e9->vblank = v4l2_ctrl_new_std(handler, &s5k5e9_ctrl_ops,
					   V4L2_CID_VBLANK, v_blank,
					   0xffff - mode->height,
					   1, v_blank);

	exposure_max = mode->vts - 2;
	s5k5e9->exposure = v4l2_ctrl_new_std(handler, &s5k5e9_ctrl_ops,
						 V4L2_CID_EXPOSURE,
						 0,
						 exposure_max, 1,
						 exposure_max);

	v4l2_ctrl_new_std(handler, &s5k5e9_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  32, 1024, 1, 1024);

	if (handler->error) {
		ret = handler->error;
		goto err_free_handler;
	}

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		goto err_free_handler;

	ret = v4l2_ctrl_new_fwnode_properties(handler, &s5k5e9_ctrl_ops,
						  &props);
	if (ret)
		goto err_free_handler;

	s5k5e9->sd.ctrl_handler = handler;

	return 0;

err_free_handler:
	dev_err(&client->dev, "Failed to init controls: %d\n", ret);
	v4l2_ctrl_handler_free(handler);

	return ret;
}

static int s5k5e9_check_sensor_id(struct s5k5e9 *s5k5e9)
{
	struct i2c_client *client = v4l2_get_subdevdata(&s5k5e9->sd);
	u32 id = 0;
	int ret;

	ret = s5k5e9_read(s5k5e9, 0x0000, 2, &id);
	if (ret)
		return ret;

	if (id != 0x559b) {
		dev_err(&client->dev, "Chip ID mismatch: expected 0x%x, got 0x%x\n", 0x559b, id);
		return -ENODEV;
	}

	dev_info(&client->dev, "Detected s5k5e9 sensor\n");
	return 0;
}

static int s5k5e9_parse_of(struct s5k5e9 *s5k5e9)
{
	struct v4l2_fwnode_endpoint vep = { .bus_type = V4L2_MBUS_CSI2_DPHY };
	struct i2c_client *client = v4l2_get_subdevdata(&s5k5e9->sd);
	struct device *dev = &client->dev;
	struct fwnode_handle *endpoint;
	int ret;

	endpoint = fwnode_graph_get_next_endpoint(dev_fwnode(dev), NULL);
	if (!endpoint) {
		dev_err(dev, "Failed to get endpoint\n");
		return -EINVAL;
	}

	ret = v4l2_fwnode_endpoint_parse(endpoint, &vep);
	fwnode_handle_put(endpoint);
	if (ret) {
		dev_err(dev, "Failed to parse endpoint: %d\n", ret);
		return ret;
	}

	for (unsigned int i = 0; i < ARRAY_SIZE(s5k5e9_modes); i++) {
		struct s5k5e9_mode *mode = &s5k5e9_modes[i];

		if (mode->lane_count != vep.bus.mipi_csi2.num_data_lanes)
			continue;

		s5k5e9->cur_mode = mode;
		break;
	}

	if (!s5k5e9->cur_mode) {
		dev_err(dev, "Unsupported number of data lanes %u\n",
			vep.bus.mipi_csi2.num_data_lanes);
		return -EINVAL;
	}

	return 0;
}

static int s5k5e9_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct s5k5e9 *s5k5e9;
	struct v4l2_subdev *sd;
	int ret;

	s5k5e9 = devm_kzalloc(dev, sizeof(*s5k5e9), GFP_KERNEL);
	if (!s5k5e9)
		return -ENOMEM;

	s5k5e9->xvclk = devm_clk_get(dev, "xvclk");
	if (IS_ERR(s5k5e9->xvclk))
		return dev_err_probe(dev, PTR_ERR(s5k5e9->xvclk),
					 "Failed to get xvclk\n");

	s5k5e9->reset_gpio = devm_gpiod_get(dev, "reset",
							 GPIOD_OUT_LOW);
	if (IS_ERR(s5k5e9->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(s5k5e9->reset_gpio),
					 "Failed to get reset gpio\n");

	v4l2_i2c_subdev_init(&s5k5e9->sd, client, &s5k5e9_subdev_ops);
	s5k5e9->sd.internal_ops = &s5k5e9_internal_ops;

	for (unsigned int i = 0; i < ARRAY_SIZE(s5k5e9_supply_names); i++)
		s5k5e9->supplies[i].supply = s5k5e9_supply_names[i];

	ret = devm_regulator_bulk_get(&client->dev,
					   ARRAY_SIZE(s5k5e9_supply_names),
					   s5k5e9->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ret = s5k5e9_parse_of(s5k5e9);
	if (ret)
		return ret;

	ret = s5k5e9_init_ctrls(s5k5e9);
	if (ret)
		return ret;

	sd = &s5k5e9->sd;
	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	s5k5e9->pad.flags = MEDIA_PAD_FL_SOURCE;
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	ret = media_entity_pads_init(&sd->entity, 1, &s5k5e9->pad);
	if (ret < 0)
		goto err_free_handler;

	sd->state_lock = s5k5e9->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(sd);
	if (ret < 0) {
		dev_err(&client->dev, "Subdev initialization error %d\n", ret);
		goto err_clean_entity;
	}

	ret = s5k5e9_power_on(dev);
	if (ret)
		goto err_clean_entity;

	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);

	ret = s5k5e9_check_sensor_id(s5k5e9);
	if (ret)
		goto err_power_off;

	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	ret = v4l2_async_register_subdev_sensor(sd);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_power_off;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

err_power_off:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
	s5k5e9_power_off(dev);
err_clean_entity:
	media_entity_cleanup(&sd->entity);
err_free_handler:
	v4l2_ctrl_handler_free(&s5k5e9->ctrl_handler);

	return ret;
};

static void s5k5e9_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct s5k5e9 *s5k5e9 = to_s5k5e9(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(&s5k5e9->ctrl_handler);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		s5k5e9_power_off(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static const struct dev_pm_ops s5k5e9_pm_ops = {
	SET_RUNTIME_PM_OPS(s5k5e9_power_off, s5k5e9_power_on, NULL)
};

static const struct of_device_id s5k5e9_of_match[] = {
	{ .compatible = "samsung,s5k5e9" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s5k5e9_of_match);

static struct i2c_driver s5k5e9_i2c_driver = {
	.driver = {
		.of_match_table = s5k5e9_of_match,
		.pm = &s5k5e9_pm_ops,
		.name = "s5k5e9",
	},
	.probe  = s5k5e9_probe,
	.remove = s5k5e9_remove,
};

module_i2c_driver(s5k5e9_i2c_driver)

MODULE_DESCRIPTION("Samsung S5K5E9 image sensor subdev driver");
MODULE_LICENSE("GPL");
