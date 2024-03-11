// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Techwell TW9900 multi-standard video decoder.
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd.
 * Copyright (C) 2020 Maxime Chevallier <maxime.chevallier@bootlin.com>
 * Copyright (C) 2023 Mehdi Djait <mehdi.djait@bootlin.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-subdev.h>

#define TW9900_REG_CHIP_ID			0x00
#define TW9900_REG_CHIP_STATUS			0x01
#define TW9900_REG_CHIP_STATUS_VDLOSS		BIT(7)
#define TW9900_REG_CHIP_STATUS_HLOCK		BIT(6)
#define TW9900_REG_OUT_FMT_CTL			0x03
#define TW9900_REG_OUT_FMT_CTL_STANDBY		0xA7
#define TW9900_REG_OUT_FMT_CTL_STREAMING	0xA0
#define TW9900_REG_CKHY_HSDLY			0x04
#define TW9900_REG_OUT_CTRL_I			0x05
#define TW9900_REG_ANALOG_CTL			0x06
#define TW9900_REG_CROP_HI			0x07
#define TW9900_REG_VDELAY_LO			0x08
#define TW9900_REG_VACTIVE_LO			0x09
#define TW9900_REG_HACTIVE_LO			0x0B
#define TW9900_REG_CNTRL1			0x0C
#define TW9900_REG_BRIGHT_CTL			0x10
#define TW9900_REG_CONTRAST_CTL			0x11
#define TW9900_REG_VBI_CNTL			0x19
#define TW9900_REG_ANAL_CTL_II			0x1A
#define TW9900_REG_OUT_CTRL_II			0x1B
#define TW9900_REG_STD				0x1C
#define TW9900_REG_STD_AUTO_PROGRESS		BIT(7)
#define TW9900_STDNOW_MASK			GENMASK(6, 4)
#define TW9900_REG_STDR				0x1D
#define TW9900_REG_MISSCNT			0x26
#define TW9900_REG_MISC_CTL_II			0x2F
#define TW9900_REG_VVBI				0x55

#define TW9900_CHIP_ID				0x00
#define TW9900_STD_NTSC_M			0
#define TW9900_STD_PAL_BDGHI			1
#define TW9900_STD_AUTO				7

#define TW9900_VIDEO_POLL_TRIES			20

struct regval {
	u8 addr;
	u8 val;
};

struct tw9900_mode {
	u32 width;
	u32 height;
	u32 std;
	const struct regval *reg_list;
	int n_regs;
};

struct tw9900 {
	struct i2c_client *client;
	struct gpio_desc *reset_gpio;
	struct regulator *regulator;

	struct v4l2_subdev subdev;
	struct v4l2_ctrl_handler hdl;
	struct media_pad pad;

	/* Serialize access to hardware and global state. */
	struct mutex mutex;

	bool streaming;
	const struct tw9900_mode *cur_mode;
};

#define to_tw9900(sd) container_of(sd, struct tw9900, subdev)

static const struct regval tw9900_init_regs[] = {
	{ TW9900_REG_MISC_CTL_II,	0xE6 },
	{ TW9900_REG_MISSCNT,		0x24 },
	{ TW9900_REG_OUT_FMT_CTL,	0xA7 },
	{ TW9900_REG_ANAL_CTL_II,	0x0A },
	{ TW9900_REG_VDELAY_LO,		0x19 },
	{ TW9900_REG_STD,		0x00 },
	{ TW9900_REG_VACTIVE_LO,	0xF0 },
	{ TW9900_REG_STD,		0x07 },
	{ TW9900_REG_CKHY_HSDLY,	0x00 },
	{ TW9900_REG_ANALOG_CTL,	0x80 },
	{ TW9900_REG_CNTRL1,		0xDC },
	{ TW9900_REG_OUT_CTRL_I,	0x98 },
};

static const struct regval tw9900_pal_regs[] = {
	{ TW9900_REG_STD,		0x01 },
};

static const struct regval tw9900_ntsc_regs[] = {
	{ TW9900_REG_OUT_FMT_CTL,	0xA4 },
	{ TW9900_REG_VDELAY_LO,		0x12 },
	{ TW9900_REG_VACTIVE_LO,	0xF0 },
	{ TW9900_REG_CROP_HI,		0x02 },
	{ TW9900_REG_HACTIVE_LO,	0xD0 },
	{ TW9900_REG_VBI_CNTL,		0x01 },
	{ TW9900_REG_STD,		0x00 },
};

static const struct tw9900_mode supported_modes[] = {
	{
		.width = 720,
		.height = 480,
		.std = V4L2_STD_NTSC,
		.reg_list = tw9900_ntsc_regs,
		.n_regs = ARRAY_SIZE(tw9900_ntsc_regs),
	},
	{
		.width = 720,
		.height = 576,
		.std = V4L2_STD_PAL,
		.reg_list = tw9900_pal_regs,
		.n_regs = ARRAY_SIZE(tw9900_pal_regs),
	},
};

static int tw9900_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0)
		dev_err(&client->dev, "write reg error: %d\n", ret);

	return ret;
}

static int tw9900_write_array(struct i2c_client *client,
			      const struct regval *regs, int n_regs)
{
	int i, ret = 0;

	for (i = 0; i < n_regs; i++) {
		ret = tw9900_write_reg(client, regs[i].addr, regs[i].val);
		if (ret)
			return ret;
	}

	return 0;
}

static int tw9900_read_reg(struct i2c_client *client, u8 reg)
{
	int ret;

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		dev_err(&client->dev, "read reg error: %d\n", ret);

	return ret;
}

static void tw9900_fill_fmt(const struct tw9900_mode *mode,
			    struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->quantization = V4L2_QUANTIZATION_DEFAULT;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
	fmt->xfer_func = V4L2_MAP_XFER_FUNC_DEFAULT(V4L2_COLORSPACE_SMPTE170M);
	fmt->ycbcr_enc = V4L2_MAP_YCBCR_ENC_DEFAULT(V4L2_COLORSPACE_SMPTE170M);
}

static int tw9900_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct tw9900 *tw9900 = to_tw9900(sd);
	struct v4l2_mbus_framefmt *mbus_fmt = &fmt->format;

	mutex_lock(&tw9900->mutex);
	tw9900_fill_fmt(tw9900->cur_mode, mbus_fmt);
	mutex_unlock(&tw9900->mutex);

	return 0;
}

static int tw9900_set_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_state *sd_state,
			  struct v4l2_subdev_format *fmt)
{
	struct tw9900 *tw9900 = to_tw9900(sd);
	struct v4l2_mbus_framefmt *mbus_fmt = &fmt->format;

	mutex_lock(&tw9900->mutex);

	if (tw9900->streaming) {
		mutex_unlock(&tw9900->mutex);
		return -EBUSY;
	}

	tw9900_fill_fmt(tw9900->cur_mode, mbus_fmt);

	mutex_unlock(&tw9900->mutex);

	return 0;
}

static int tw9900_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_UYVY8_2X8;

	return 0;
}

static int tw9900_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct tw9900 *tw9900 = container_of(ctrl->handler, struct tw9900, hdl);
	int ret;

	if (pm_runtime_suspended(&tw9900->client->dev))
		return 0;

	/* v4l2_ctrl_lock() locks tw9900->mutex. */
	switch (ctrl->id) {
	case V4L2_CID_BRIGHTNESS:
		ret = tw9900_write_reg(tw9900->client, TW9900_REG_BRIGHT_CTL,
				       (u8)ctrl->val);
		break;
	case V4L2_CID_CONTRAST:
		ret = tw9900_write_reg(tw9900->client, TW9900_REG_CONTRAST_CTL,
				       (u8)ctrl->val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int tw9900_s_stream(struct v4l2_subdev *sd, int on)
{
	struct tw9900 *tw9900 = to_tw9900(sd);
	struct i2c_client *client = tw9900->client;
	int ret;

	mutex_lock(&tw9900->mutex);

	if (tw9900->streaming == on) {
		mutex_unlock(&tw9900->mutex);
		return 0;
	}

	mutex_unlock(&tw9900->mutex);

	if (on) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			return ret;

		mutex_lock(&tw9900->mutex);

		ret = __v4l2_ctrl_handler_setup(sd->ctrl_handler);
		if (ret)
			goto err_unlock;

		ret = tw9900_write_array(tw9900->client,
					 tw9900->cur_mode->reg_list,
					 tw9900->cur_mode->n_regs);
		if (ret)
			goto err_unlock;

		ret = tw9900_write_reg(client, TW9900_REG_OUT_FMT_CTL,
				       TW9900_REG_OUT_FMT_CTL_STREAMING);
		if (ret)
			goto err_unlock;

		tw9900->streaming = on;

		mutex_unlock(&tw9900->mutex);

	} else {
		mutex_lock(&tw9900->mutex);

		ret = tw9900_write_reg(client, TW9900_REG_OUT_FMT_CTL,
				       TW9900_REG_OUT_FMT_CTL_STANDBY);
		if (ret)
			goto err_unlock;

		tw9900->streaming = on;

		mutex_unlock(&tw9900->mutex);

		pm_runtime_put(&client->dev);
	}

	return 0;

err_unlock:
	mutex_unlock(&tw9900->mutex);
	pm_runtime_put(&client->dev);

	return ret;
}

static int tw9900_subscribe_event(struct v4l2_subdev *sd,
				  struct v4l2_fh *fh,
				  struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subdev_subscribe(sd, fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subdev_subscribe_event(sd, fh, sub);
	default:
		return -EINVAL;
	}
}

static int tw9900_s_std(struct v4l2_subdev *sd, v4l2_std_id std)
{
	struct tw9900 *tw9900 = to_tw9900(sd);
	const struct tw9900_mode *mode = NULL;
	int i;

	if (!(std & (V4L2_STD_NTSC | V4L2_STD_PAL)))
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++)
		if (supported_modes[i].std & std)
			mode = &supported_modes[i];
	if (!mode)
		return -EINVAL;

	mutex_lock(&tw9900->mutex);
	tw9900->cur_mode = mode;
	mutex_unlock(&tw9900->mutex);

	return 0;
}

static int tw9900_get_stream_std(struct tw9900 *tw9900,
				 v4l2_std_id *std)
{
	int cur_std, ret;

	lockdep_assert_held(&tw9900->mutex);

	ret = tw9900_read_reg(tw9900->client, TW9900_REG_STD);
	if (ret < 0) {
		*std = V4L2_STD_UNKNOWN;
		return ret;
	}

	cur_std = FIELD_GET(TW9900_STDNOW_MASK, ret);
	switch (cur_std) {
	case TW9900_STD_NTSC_M:
		*std = V4L2_STD_NTSC;
		break;
	case TW9900_STD_PAL_BDGHI:
		*std = V4L2_STD_PAL;
		break;
	case TW9900_STD_AUTO:
		*std = V4L2_STD_UNKNOWN;
		break;
	default:
		*std = V4L2_STD_UNKNOWN;
		break;
	}

	return 0;
}

static int tw9900_g_std(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct tw9900 *tw9900 = to_tw9900(sd);

	mutex_lock(&tw9900->mutex);
	*std = tw9900->cur_mode->std;
	mutex_unlock(&tw9900->mutex);

	return 0;
}

static int tw9900_start_autodetect(struct tw9900 *tw9900)
{
	int ret;

	lockdep_assert_held(&tw9900->mutex);

	ret = tw9900_write_reg(tw9900->client, TW9900_REG_STDR,
			       BIT(TW9900_STD_NTSC_M) |
			       BIT(TW9900_STD_PAL_BDGHI));
	if (ret)
		return ret;

	ret = tw9900_write_reg(tw9900->client, TW9900_REG_STD,
			       TW9900_STD_AUTO);
	if (ret)
		return ret;

	ret = tw9900_write_reg(tw9900->client, TW9900_REG_STDR,
			       BIT(TW9900_STD_NTSC_M) |
			       BIT(TW9900_STD_PAL_BDGHI) |
			       BIT(TW9900_STD_AUTO));
	if (ret)
		return ret;

	/*
	 * Autodetect takes a while to start, and during the starting sequence
	 * the autodetection status is reported as done.
	 */
	msleep(30);

	return 0;
}

static int tw9900_detect_done(struct tw9900 *tw9900, bool *done)
{
	int ret;

	lockdep_assert_held(&tw9900->mutex);

	ret = tw9900_read_reg(tw9900->client, TW9900_REG_STD);
	if (ret < 0)
		return ret;

	*done = !(ret & TW9900_REG_STD_AUTO_PROGRESS);

	return 0;
}

static int tw9900_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	struct tw9900 *tw9900 = to_tw9900(sd);
	bool done = false;
	int i, ret;

	mutex_lock(&tw9900->mutex);

	if (tw9900->streaming) {
		mutex_unlock(&tw9900->mutex);
		return -EBUSY;
	}

	mutex_unlock(&tw9900->mutex);

	ret = pm_runtime_resume_and_get(&tw9900->client->dev);
	if (ret < 0)
		return ret;

	mutex_lock(&tw9900->mutex);

	ret = tw9900_start_autodetect(tw9900);
	if (ret)
		goto out_unlock;

	for (i = 0; i < TW9900_VIDEO_POLL_TRIES; i++) {
		ret = tw9900_detect_done(tw9900, &done);
		if (ret)
			goto out_unlock;

		if (done)
			break;

		msleep(20);
	}

	if (!done) {
		ret = -ETIMEDOUT;
		goto out_unlock;
	}

	ret = tw9900_get_stream_std(tw9900, std);

out_unlock:
	mutex_unlock(&tw9900->mutex);
	pm_runtime_put(&tw9900->client->dev);

	return ret;
}

static int tw9900_g_tvnorms(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	*std = V4L2_STD_NTSC | V4L2_STD_PAL;

	return 0;
}

static int tw9900_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct tw9900 *tw9900 = to_tw9900(sd);
	int ret;

	mutex_lock(&tw9900->mutex);

	if (tw9900->streaming) {
		mutex_unlock(&tw9900->mutex);
		return -EBUSY;
	}

	mutex_unlock(&tw9900->mutex);

	*status = V4L2_IN_ST_NO_SIGNAL;

	ret = pm_runtime_resume_and_get(&tw9900->client->dev);
	if (ret < 0)
		return ret;

	mutex_lock(&tw9900->mutex);
	ret = tw9900_read_reg(tw9900->client, TW9900_REG_CHIP_STATUS);
	mutex_unlock(&tw9900->mutex);

	pm_runtime_put(&tw9900->client->dev);

	if (ret < 0)
		return ret;

	*status = ret & TW9900_REG_CHIP_STATUS_HLOCK ? 0 : V4L2_IN_ST_NO_SIGNAL;

	return 0;
}

static const struct v4l2_subdev_core_ops tw9900_core_ops = {
	.subscribe_event	= tw9900_subscribe_event,
	.unsubscribe_event	= v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_video_ops tw9900_video_ops = {
	.s_std		= tw9900_s_std,
	.g_std		= tw9900_g_std,
	.querystd	= tw9900_querystd,
	.g_tvnorms	= tw9900_g_tvnorms,
	.g_input_status = tw9900_g_input_status,
	.s_stream	= tw9900_s_stream,
};

static const struct v4l2_subdev_pad_ops tw9900_pad_ops = {
	.enum_mbus_code	= tw9900_enum_mbus_code,
	.get_fmt	= tw9900_get_fmt,
	.set_fmt	= tw9900_set_fmt,
};

static const struct v4l2_subdev_ops tw9900_subdev_ops = {
	.core	= &tw9900_core_ops,
	.video	= &tw9900_video_ops,
	.pad	= &tw9900_pad_ops,
};

static const struct v4l2_ctrl_ops tw9900_ctrl_ops = {
	.s_ctrl	= tw9900_s_ctrl,
};

static int tw9900_check_id(struct tw9900 *tw9900,
			   struct i2c_client *client)
{
	struct device *dev = &tw9900->client->dev;
	int ret;

	ret = pm_runtime_resume_and_get(&tw9900->client->dev);
	if (ret < 0)
		return ret;

	mutex_lock(&tw9900->mutex);
	ret = tw9900_read_reg(client, TW9900_CHIP_ID);
	mutex_unlock(&tw9900->mutex);

	pm_runtime_put(&tw9900->client->dev);

	if (ret < 0)
		return ret;

	if (ret != TW9900_CHIP_ID) {
		dev_err(dev, "Unexpected decoder id %#x\n", ret);
		return -ENODEV;
	}

	return 0;
}

static int tw9900_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tw9900 *tw9900 = to_tw9900(sd);
	int ret;

	mutex_lock(&tw9900->mutex);

	if (tw9900->reset_gpio)
		gpiod_set_value_cansleep(tw9900->reset_gpio, 1);

	ret = regulator_enable(tw9900->regulator);
	if (ret < 0) {
		mutex_unlock(&tw9900->mutex);
		return ret;
	}

	usleep_range(50000, 52000);

	if (tw9900->reset_gpio)
		gpiod_set_value_cansleep(tw9900->reset_gpio, 0);

	usleep_range(1000, 2000);

	ret = tw9900_write_array(tw9900->client, tw9900_init_regs,
				 ARRAY_SIZE(tw9900_init_regs));

	mutex_unlock(&tw9900->mutex);

	/* This sleep is needed for the Horizontal Sync PLL to lock. */
	msleep(300);

	return ret;
}

static int tw9900_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tw9900 *tw9900 = to_tw9900(sd);

	mutex_lock(&tw9900->mutex);

	if (tw9900->reset_gpio)
		gpiod_set_value_cansleep(tw9900->reset_gpio, 1);

	regulator_disable(tw9900->regulator);

	mutex_unlock(&tw9900->mutex);

	return 0;
}

static int tw9900_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct v4l2_ctrl_handler *hdl;
	struct tw9900 *tw9900;
	int ret = 0;

	tw9900 = devm_kzalloc(dev, sizeof(*tw9900), GFP_KERNEL);
	if (!tw9900)
		return -ENOMEM;

	tw9900->client = client;
	tw9900->cur_mode = &supported_modes[0];

	tw9900->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						     GPIOD_OUT_LOW);
	if (IS_ERR(tw9900->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(tw9900->reset_gpio),
				     "Failed to get reset gpio\n");

	tw9900->regulator = devm_regulator_get(&tw9900->client->dev, "vdd");
	if (IS_ERR(tw9900->regulator))
		return dev_err_probe(dev, PTR_ERR(tw9900->regulator),
				     "Failed to get power regulator\n");

	v4l2_i2c_subdev_init(&tw9900->subdev, client, &tw9900_subdev_ops);
	tw9900->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
				V4L2_SUBDEV_FL_HAS_EVENTS;

	mutex_init(&tw9900->mutex);

	hdl = &tw9900->hdl;

	ret = v4l2_ctrl_handler_init(hdl, 2);
	if (ret)
		goto err_destory_mutex;

	hdl->lock = &tw9900->mutex;

	v4l2_ctrl_new_std(hdl, &tw9900_ctrl_ops, V4L2_CID_BRIGHTNESS,
			  -128, 127, 1, 0);
	v4l2_ctrl_new_std(hdl, &tw9900_ctrl_ops, V4L2_CID_CONTRAST,
			  0, 255, 1, 0x60);

	tw9900->subdev.ctrl_handler = hdl;
	if (hdl->error) {
		ret = hdl->error;
		goto err_free_handler;
	}

	tw9900->pad.flags = MEDIA_PAD_FL_SOURCE;
	tw9900->subdev.entity.function = MEDIA_ENT_F_DV_DECODER;

	ret = media_entity_pads_init(&tw9900->subdev.entity, 1, &tw9900->pad);
	if (ret < 0)
		goto err_free_handler;

	pm_runtime_set_suspended(dev);
	pm_runtime_enable(dev);

	ret = tw9900_check_id(tw9900, client);
	if (ret)
		goto err_disable_pm;

	ret = v4l2_async_register_subdev(&tw9900->subdev);
	if (ret) {
		dev_err(dev, "v4l2 async register subdev failed\n");
		goto err_disable_pm;
	}

	return 0;

err_disable_pm:
	pm_runtime_disable(dev);
	media_entity_cleanup(&tw9900->subdev.entity);
err_free_handler:
	v4l2_ctrl_handler_free(hdl);
err_destory_mutex:
	mutex_destroy(&tw9900->mutex);

	return ret;
}

static void tw9900_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct tw9900 *tw9900 = to_tw9900(sd);

	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	v4l2_ctrl_handler_free(sd->ctrl_handler);

	pm_runtime_disable(&client->dev);

	mutex_destroy(&tw9900->mutex);
}

static const struct dev_pm_ops tw9900_pm_ops = {
	.runtime_suspend = tw9900_runtime_suspend,
	.runtime_resume = tw9900_runtime_resume,
};

static const struct i2c_device_id tw9900_id[] = {
	{ "tw9900", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tw9900_id);

static const struct of_device_id tw9900_of_match[] = {
	{ .compatible = "techwell,tw9900" },
	{},
};
MODULE_DEVICE_TABLE(of, tw9900_of_match);

static struct i2c_driver tw9900_i2c_driver = {
	.driver = {
		.name		= "tw9900",
		.pm		= &tw9900_pm_ops,
		.of_match_table	= tw9900_of_match,
	},
	.probe	  = tw9900_probe,
	.remove	  = tw9900_remove,
	.id_table = tw9900_id,
};

module_i2c_driver(tw9900_i2c_driver);

MODULE_DESCRIPTION("tw9900 decoder driver");
MODULE_LICENSE("GPL");
