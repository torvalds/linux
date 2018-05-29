// SPDX-License-Identifier: GPL-2.0
/*
 * adv7181 driver
 *
 * Copyright (C) 2018 Fuzhou Rockchip Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/sysfs.h>
#include <media/media-entity.h>
#include <media/v4l2-async.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-subdev.h>

#define REG_CHIP_ID			0x11
#define CHIP_ID				0x20

#define REG_SC_CTRL_MODE		0x03
#define     SC_CTRL_MODE_STANDBY	0x4c
#define     SC_CTRL_MODE_STREAMING	0x0c

#define REG_NULL			0xFF

#define ADV7181_XVCLK_FREQ		24000000
#define ADV7181_LANES			1
#define ADV7181_BITS_PER_SAMPLE		10

#define ADV7181_SKIP_TOP		24

static const char * const adv7181_supply_names[] = {
	"dvdd",
	"dvddio",
};

#define ADV7181_NUM_SUPPLIES ARRAY_SIZE(adv7181_supply_names)

struct regval {
	u8 addr;
	u8 val;
};

struct adv7181_mode {
	u32 width;
	u32 height;
	const struct regval *reg_list;
};

struct adv7181 {
	struct i2c_client	*client;
	struct clk		*xvclk;
	struct gpio_desc	*reset_gpio;
	struct regulator_bulk_data supplies[ADV7181_NUM_SUPPLIES];

	bool			streaming;
	struct mutex		mutex; /* lock to serialize v4l2 callback */
	struct v4l2_subdev	subdev;
	struct media_pad	pad;

	int			skip_top;

	const struct adv7181_mode *cur_mode;
};

#define to_adv7181(sd) container_of(sd, struct adv7181, subdev)

/* PLL settings bases on 28M xvclk, resolution 720x480 30fps*/
static struct regval adv7181_cvbs_30fps[] = {
	{0x00, 0x0B},
	{0x04, 0x77},
	{0x17, 0x41},
	{0x1D, 0x47},
	{0x31, 0x02},
	{0x3A, 0x17},
	{0x3B, 0x81},
	{0x3D, 0xA2},
	{0x3E, 0x6A},
	{0x3F, 0xA0},
	{0x86, 0x0B},
	{0xF3, 0x01},
	{0xF9, 0x03},
	{0x0E, 0x80},
	{0x52, 0x46},
	{0x54, 0x80},
	{0x7F, 0xFF},
	{0x81, 0x30},
	{0x90, 0xC9},
	{0x91, 0x40},
	{0x92, 0x3C},
	{0x93, 0xCA},
	{0x94, 0xD5},
	{0xB1, 0xFF},
	{0xB6, 0x08},
	{0xC0, 0x9A},
	{0xCF, 0x50},
	{0xD0, 0x4E},
	{0xD1, 0xB9},
	{0xD6, 0xDD},
	{0xD7, 0xE2},
	{0xE5, 0x51},
	{0xF6, 0x3B},
	{0x0E, 0x00},
	{0x03, 0x4C},
	{REG_NULL, 0x0},
};

static const struct adv7181_mode supported_modes[] = {
	{
		.width = 720,
		.height = 480,
		.reg_list = adv7181_cvbs_30fps,
	},
};

static int adv7181_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);

	if (ret < 0)
		dev_err(&client->dev, "write reg error: %d\n", ret);

	return ret;
}

static int adv7181_write_array(struct i2c_client *client,
			       const struct regval *regs)
{
	int i, ret = 0;

	for (i = 0; ret == 0 && regs[i].addr != REG_NULL; i++)
		ret = adv7181_write_reg(client, regs[i].addr, regs[i].val);

	return ret;
}

static inline u8 adv7181_read_reg(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

static void adv7181_fill_fmt(const struct adv7181_mode *mode,
			     struct v4l2_mbus_framefmt *fmt)
{
	fmt->code = MEDIA_BUS_FMT_UYVY8_2X8;
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->field = V4L2_FIELD_NONE;
	fmt->colorspace = V4L2_COLORSPACE_SMPTE170M;
}

static int adv7181_set_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct adv7181 *adv7181 = to_adv7181(sd);
	struct v4l2_mbus_framefmt *mbus_fmt = &fmt->format;

	/* only one mode supported for now */
	adv7181_fill_fmt(adv7181->cur_mode, mbus_fmt);

	return 0;
}

static int adv7181_get_fmt(struct v4l2_subdev *sd,
			   struct v4l2_subdev_pad_config *cfg,
			   struct v4l2_subdev_format *fmt)
{
	struct adv7181 *adv7181 = to_adv7181(sd);
	struct v4l2_mbus_framefmt *mbus_fmt = &fmt->format;

	adv7181_fill_fmt(adv7181->cur_mode, mbus_fmt);

	return 0;
}

static int adv7181_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_UYVY8_2X8;

	return 0;
}

static int adv7181_enum_frame_sizes(struct v4l2_subdev *sd,
				    struct v4l2_subdev_pad_config *cfg,
				    struct v4l2_subdev_frame_size_enum *fse)
{
	u32 index = fse->index;

	if (index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	fse->code = MEDIA_BUS_FMT_UYVY8_2X8;

	fse->min_width  = supported_modes[index].width;
	fse->max_width  = supported_modes[index].width;
	fse->max_height = supported_modes[index].height;
	fse->min_height = supported_modes[index].height;

	return 0;
}

static int adv7181_g_skip_top_lines(struct v4l2_subdev *sd, u32 *lines)
{
	struct adv7181 *adv7181 = to_adv7181(sd);

	*lines = adv7181->skip_top;

	return 0;
}

static int adv7181_querystd(struct v4l2_subdev *sd, v4l2_std_id *std)
{
	/* Only NTSC now */
	*std = V4L2_STD_NTSC;

	return 0;
}

static int __adv7181_power_on(struct adv7181 *adv7181)
{
	int ret;
	struct device *dev = &adv7181->client->dev;

	if (!IS_ERR(adv7181->xvclk)) {
		ret = clk_prepare_enable(adv7181->xvclk);
		if (ret < 0) {
			dev_err(dev, "Failed to enable xvclk\n");
			return ret;
		}
	}

	gpiod_set_value_cansleep(adv7181->reset_gpio, 1);

	ret = regulator_bulk_enable(ADV7181_NUM_SUPPLIES, adv7181->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators\n");
		goto disable_clk;
	}

	gpiod_set_value_cansleep(adv7181->reset_gpio, 0);

	return 0;

disable_clk:
	if (!IS_ERR(adv7181->xvclk))
		clk_disable_unprepare(adv7181->xvclk);

	return ret;
}

static void __adv7181_power_off(struct adv7181 *adv7181)
{
	if (!IS_ERR(adv7181->xvclk))
		clk_disable_unprepare(adv7181->xvclk);
	gpiod_set_value_cansleep(adv7181->reset_gpio, 1);
	regulator_bulk_disable(ADV7181_NUM_SUPPLIES, adv7181->supplies);
}

static int adv7181_s_stream(struct v4l2_subdev *sd, int on)
{
	struct adv7181 *adv7181 = to_adv7181(sd);
	struct i2c_client *client = adv7181->client;
	int ret = 0;

	mutex_lock(&adv7181->mutex);

	on = !!on;
	if (on == adv7181->streaming)
		goto unlock_and_return;

	if (on) {
		ret = pm_runtime_get_sync(&adv7181->client->dev);
		if (ret < 0) {
			pm_runtime_put_noidle(&client->dev);
			goto unlock_and_return;
		}

		ret = adv7181_write_array(adv7181->client,
					  adv7181->cur_mode->reg_list);
		if (ret) {
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}

		ret = adv7181_write_reg(client, REG_SC_CTRL_MODE,
					SC_CTRL_MODE_STREAMING);
		if (ret) {
			pm_runtime_put(&client->dev);
			goto unlock_and_return;
		}
	} else {
		adv7181_write_reg(client, REG_SC_CTRL_MODE,
				  SC_CTRL_MODE_STANDBY);
		pm_runtime_put(&client->dev);
	}

	adv7181->streaming = on;

unlock_and_return:
	mutex_unlock(&adv7181->mutex);

	return ret;
}

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static int adv7181_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct adv7181 *adv7181 = to_adv7181(sd);
	struct v4l2_mbus_framefmt *try_fmt;

	mutex_lock(&adv7181->mutex);

	try_fmt = v4l2_subdev_get_try_format(sd, fh->pad, 0);
	/* Initialize try_fmt */
	adv7181_fill_fmt(&supported_modes[0], try_fmt);

	mutex_unlock(&adv7181->mutex);

	return 0;
}
#endif

static int adv7181_runtime_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7181 *adv7181 = to_adv7181(sd);

	return __adv7181_power_on(adv7181);
}

static int adv7181_runtime_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7181 *adv7181 = to_adv7181(sd);

	__adv7181_power_off(adv7181);

	return 0;
}

static const struct dev_pm_ops adv7181_pm_ops = {
	SET_RUNTIME_PM_OPS(adv7181_runtime_suspend,
			   adv7181_runtime_resume, NULL)
};

static const struct v4l2_subdev_video_ops adv7181_video_ops = {
	.s_stream = adv7181_s_stream,
	.querystd = adv7181_querystd,
};

static const struct v4l2_subdev_pad_ops adv7181_pad_ops = {
	.enum_mbus_code = adv7181_enum_mbus_code,
	.enum_frame_size = adv7181_enum_frame_sizes,
	.get_fmt = adv7181_get_fmt,
	.set_fmt = adv7181_set_fmt,
};

static struct v4l2_subdev_sensor_ops adv7181_sensor_ops = {
	.g_skip_top_lines	= adv7181_g_skip_top_lines,
};

static const struct v4l2_subdev_ops adv7181_subdev_ops = {
	.video	= &adv7181_video_ops,
	.pad	= &adv7181_pad_ops,
	.sensor = &adv7181_sensor_ops,
};

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
static const struct v4l2_subdev_internal_ops adv7181_internal_ops = {
	.open = adv7181_open,
};
#endif

static int adv7181_check_sensor_id(struct adv7181 *adv7181,
				  struct i2c_client *client)
{
	struct device *dev = &adv7181->client->dev;
	u8 id;

	id = adv7181_read_reg(client, REG_CHIP_ID);

	if (id != CHIP_ID) {
		dev_err(dev, "Wrong camera sensor id(%04x)\n", id);
		return -EINVAL;
	}

	dev_info(dev, "Detected ADV7181 (%04x) sensor\n", CHIP_ID);

	return 0;
}

static int adv7181_configure_regulators(struct adv7181 *adv7181)
{
	u32 i;

	for (i = 0; i < ADV7181_NUM_SUPPLIES; i++)
		adv7181->supplies[i].supply = adv7181_supply_names[i];

	return devm_regulator_bulk_get(&adv7181->client->dev,
				       ADV7181_NUM_SUPPLIES,
				       adv7181->supplies);
}

static int adv7181_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct adv7181 *adv7181;
	int ret;

	adv7181 = devm_kzalloc(dev, sizeof(*adv7181), GFP_KERNEL);
	if (!adv7181)
		return -ENOMEM;

	adv7181->skip_top = ADV7181_SKIP_TOP;

	adv7181->client = client;
	adv7181->cur_mode = &supported_modes[0];

	adv7181->xvclk = devm_clk_get(dev, "xvclk");
	if (!IS_ERR(adv7181->xvclk)) {
		ret = clk_set_rate(adv7181->xvclk, ADV7181_XVCLK_FREQ);
		if (ret < 0) {
			dev_err(dev, "Failed to set xvclk rate (24MHz)\n");
			return ret;
		}
		if (clk_get_rate(adv7181->xvclk) != ADV7181_XVCLK_FREQ)
			dev_warn(dev, "xvclk mismatched, it requires 24MHz\n");
	}

	adv7181->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(adv7181->reset_gpio)) {
		dev_err(dev, "Failed to get reset-gpios\n");
		return -EINVAL;
	}

	ret = adv7181_configure_regulators(adv7181);
	if (ret) {
		dev_err(dev, "Failed to get power regulators\n");
		return ret;
	}

	mutex_init(&adv7181->mutex);
	v4l2_i2c_subdev_init(&adv7181->subdev, client, &adv7181_subdev_ops);

	ret = __adv7181_power_on(adv7181);
	if (ret)
		goto err_destroy_mutex;

	ret = adv7181_check_sensor_id(adv7181, client);
	if (ret)
		goto err_power_off;

#ifdef CONFIG_VIDEO_V4L2_SUBDEV_API
	adv7181->subdev.internal_ops = &adv7181_internal_ops;
	adv7181->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
#endif
#if defined(CONFIG_MEDIA_CONTROLLER)
	adv7181->pad.flags = MEDIA_PAD_FL_SOURCE;
	adv7181->subdev.entity.type = MEDIA_ENT_T_V4L2_SUBDEV_SENSOR;
	ret = media_entity_init(&adv7181->subdev.entity, 1, &adv7181->pad, 0);
	if (ret < 0)
		goto err_power_off;
#endif

	ret = v4l2_async_register_subdev(&adv7181->subdev);
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
	media_entity_cleanup(&adv7181->subdev.entity);
#endif
err_power_off:
	__adv7181_power_off(adv7181);
err_destroy_mutex:
	mutex_destroy(&adv7181->mutex);

	return ret;
}

static int adv7181_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct adv7181 *adv7181 = to_adv7181(sd);

	v4l2_async_unregister_subdev(sd);
#if defined(CONFIG_MEDIA_CONTROLLER)
	media_entity_cleanup(&sd->entity);
#endif
	mutex_destroy(&adv7181->mutex);

	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev))
		__adv7181_power_off(adv7181);
	pm_runtime_set_suspended(&client->dev);

	return 0;
}

static const struct i2c_device_id adv7181_id[] = {
	{"adv7181", 0},
	{},
};

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id adv7181_of_match[] = {
	{ .compatible = "adi,adv7181" },
	{},
};
MODULE_DEVICE_TABLE(of, adv7181_of_match);
#endif

static struct i2c_driver adv7181_i2c_driver = {
	.driver = {
		.name = "adv7181",
		.pm = &adv7181_pm_ops,
		.of_match_table = adv7181_of_match
	},
	.probe		= adv7181_probe,
	.remove		= adv7181_remove,
	.id_table	= adv7181_id,
};

module_i2c_driver(adv7181_i2c_driver);

MODULE_DESCRIPTION("adv7181 sensor driver");
MODULE_LICENSE("GPL v2");
