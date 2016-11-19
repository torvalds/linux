/*
 * Samsung S5K6A3 image sensor driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <media/v4l2-async.h>
#include <media/v4l2-subdev.h>

#define S5K6A3_SENSOR_MAX_WIDTH		1412
#define S5K6A3_SENSOR_MAX_HEIGHT	1412
#define S5K6A3_SENSOR_MIN_WIDTH		32
#define S5K6A3_SENSOR_MIN_HEIGHT	32

#define S5K6A3_DEFAULT_WIDTH		1296
#define S5K6A3_DEFAULT_HEIGHT		732

#define S5K6A3_DRV_NAME			"S5K6A3"
#define S5K6A3_CLK_NAME			"extclk"
#define S5K6A3_DEFAULT_CLK_FREQ		24000000U

enum {
	S5K6A3_SUPP_VDDA,
	S5K6A3_SUPP_VDDIO,
	S5K6A3_SUPP_AFVDD,
	S5K6A3_NUM_SUPPLIES,
};

/**
 * struct s5k6a3 - fimc-is sensor data structure
 * @dev: pointer to this I2C client device structure
 * @subdev: the image sensor's v4l2 subdev
 * @pad: subdev media source pad
 * @supplies: image sensor's voltage regulator supplies
 * @gpio_reset: GPIO connected to the sensor's reset pin
 * @lock: mutex protecting the structure's members below
 * @format: media bus format at the sensor's source pad
 */
struct s5k6a3 {
	struct device *dev;
	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct regulator_bulk_data supplies[S5K6A3_NUM_SUPPLIES];
	int gpio_reset;
	struct mutex lock;
	struct v4l2_mbus_framefmt format;
	struct clk *clock;
	u32 clock_frequency;
	int power_count;
};

static const char * const s5k6a3_supply_names[] = {
	[S5K6A3_SUPP_VDDA]	= "svdda",
	[S5K6A3_SUPP_VDDIO]	= "svddio",
	[S5K6A3_SUPP_AFVDD]	= "afvdd",
};

static inline struct s5k6a3 *sd_to_s5k6a3(struct v4l2_subdev *sd)
{
	return container_of(sd, struct s5k6a3, subdev);
}

static const struct v4l2_mbus_framefmt s5k6a3_formats[] = {
	{
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.field = V4L2_FIELD_NONE,
	}
};

static const struct v4l2_mbus_framefmt *find_sensor_format(
	struct v4l2_mbus_framefmt *mf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(s5k6a3_formats); i++)
		if (mf->code == s5k6a3_formats[i].code)
			return &s5k6a3_formats[i];

	return &s5k6a3_formats[0];
}

static int s5k6a3_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(s5k6a3_formats))
		return -EINVAL;

	code->code = s5k6a3_formats[code->index].code;
	return 0;
}

static void s5k6a3_try_format(struct v4l2_mbus_framefmt *mf)
{
	const struct v4l2_mbus_framefmt *fmt;

	fmt = find_sensor_format(mf);
	mf->code = fmt->code;
	mf->field = V4L2_FIELD_NONE;
	v4l_bound_align_image(&mf->width, S5K6A3_SENSOR_MIN_WIDTH,
			      S5K6A3_SENSOR_MAX_WIDTH, 0,
			      &mf->height, S5K6A3_SENSOR_MIN_HEIGHT,
			      S5K6A3_SENSOR_MAX_HEIGHT, 0, 0);
}

static struct v4l2_mbus_framefmt *__s5k6a3_get_format(
		struct s5k6a3 *sensor, struct v4l2_subdev_pad_config *cfg,
		u32 pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return cfg ? v4l2_subdev_get_try_format(&sensor->subdev, cfg, pad) : NULL;

	return &sensor->format;
}

static int s5k6a3_set_fmt(struct v4l2_subdev *sd,
				  struct v4l2_subdev_pad_config *cfg,
				  struct v4l2_subdev_format *fmt)
{
	struct s5k6a3 *sensor = sd_to_s5k6a3(sd);
	struct v4l2_mbus_framefmt *mf;

	s5k6a3_try_format(&fmt->format);

	mf = __s5k6a3_get_format(sensor, cfg, fmt->pad, fmt->which);
	if (mf) {
		mutex_lock(&sensor->lock);
		*mf = fmt->format;
		mutex_unlock(&sensor->lock);
	}
	return 0;
}

static int s5k6a3_get_fmt(struct v4l2_subdev *sd,
			  struct v4l2_subdev_pad_config *cfg,
			  struct v4l2_subdev_format *fmt)
{
	struct s5k6a3 *sensor = sd_to_s5k6a3(sd);
	struct v4l2_mbus_framefmt *mf;

	mf = __s5k6a3_get_format(sensor, cfg, fmt->pad, fmt->which);

	mutex_lock(&sensor->lock);
	fmt->format = *mf;
	mutex_unlock(&sensor->lock);
	return 0;
}

static struct v4l2_subdev_pad_ops s5k6a3_pad_ops = {
	.enum_mbus_code	= s5k6a3_enum_mbus_code,
	.get_fmt	= s5k6a3_get_fmt,
	.set_fmt	= s5k6a3_set_fmt,
};

static int s5k6a3_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format = v4l2_subdev_get_try_format(sd, fh->pad, 0);

	*format		= s5k6a3_formats[0];
	format->width	= S5K6A3_DEFAULT_WIDTH;
	format->height	= S5K6A3_DEFAULT_HEIGHT;

	return 0;
}

static const struct v4l2_subdev_internal_ops s5k6a3_sd_internal_ops = {
	.open = s5k6a3_open,
};

static int __s5k6a3_power_on(struct s5k6a3 *sensor)
{
	int i = S5K6A3_SUPP_VDDA;
	int ret;

	ret = clk_set_rate(sensor->clock, sensor->clock_frequency);
	if (ret < 0)
		return ret;

	ret = pm_runtime_get(sensor->dev);
	if (ret < 0)
		return ret;

	ret = regulator_enable(sensor->supplies[i].consumer);
	if (ret < 0)
		goto error_rpm_put;

	ret = clk_prepare_enable(sensor->clock);
	if (ret < 0)
		goto error_reg_dis;

	for (i++; i < S5K6A3_NUM_SUPPLIES; i++) {
		ret = regulator_enable(sensor->supplies[i].consumer);
		if (ret < 0)
			goto error_reg_dis;
	}

	gpio_set_value(sensor->gpio_reset, 1);
	usleep_range(600, 800);
	gpio_set_value(sensor->gpio_reset, 0);
	usleep_range(600, 800);
	gpio_set_value(sensor->gpio_reset, 1);

	/* Delay needed for the sensor initialization */
	msleep(20);
	return 0;

error_reg_dis:
	for (--i; i >= 0; --i)
		regulator_disable(sensor->supplies[i].consumer);
error_rpm_put:
	pm_runtime_put(sensor->dev);
	return ret;
}

static int __s5k6a3_power_off(struct s5k6a3 *sensor)
{
	int i;

	gpio_set_value(sensor->gpio_reset, 0);

	for (i = S5K6A3_NUM_SUPPLIES - 1; i >= 0; i--)
		regulator_disable(sensor->supplies[i].consumer);

	clk_disable_unprepare(sensor->clock);
	pm_runtime_put(sensor->dev);
	return 0;
}

static int s5k6a3_s_power(struct v4l2_subdev *sd, int on)
{
	struct s5k6a3 *sensor = sd_to_s5k6a3(sd);
	int ret = 0;

	mutex_lock(&sensor->lock);

	if (sensor->power_count == !on) {
		if (on)
			ret = __s5k6a3_power_on(sensor);
		else
			ret = __s5k6a3_power_off(sensor);

		if (ret == 0)
			sensor->power_count += on ? 1 : -1;
	}

	mutex_unlock(&sensor->lock);
	return ret;
}

static struct v4l2_subdev_core_ops s5k6a3_core_ops = {
	.s_power = s5k6a3_s_power,
};

static struct v4l2_subdev_ops s5k6a3_subdev_ops = {
	.core = &s5k6a3_core_ops,
	.pad = &s5k6a3_pad_ops,
};

static int s5k6a3_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct s5k6a3 *sensor;
	struct v4l2_subdev *sd;
	int gpio, i, ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	mutex_init(&sensor->lock);
	sensor->gpio_reset = -EINVAL;
	sensor->clock = ERR_PTR(-EINVAL);
	sensor->dev = dev;

	sensor->clock = devm_clk_get(sensor->dev, S5K6A3_CLK_NAME);
	if (IS_ERR(sensor->clock))
		return PTR_ERR(sensor->clock);

	gpio = of_get_gpio_flags(dev->of_node, 0, NULL);
	if (!gpio_is_valid(gpio))
		return gpio;

	ret = devm_gpio_request_one(dev, gpio, GPIOF_OUT_INIT_LOW,
						S5K6A3_DRV_NAME);
	if (ret < 0)
		return ret;

	sensor->gpio_reset = gpio;

	if (of_property_read_u32(dev->of_node, "clock-frequency",
				 &sensor->clock_frequency)) {
		sensor->clock_frequency = S5K6A3_DEFAULT_CLK_FREQ;
		dev_info(dev, "using default %u Hz clock frequency\n",
					sensor->clock_frequency);
	}

	for (i = 0; i < S5K6A3_NUM_SUPPLIES; i++)
		sensor->supplies[i].supply = s5k6a3_supply_names[i];

	ret = devm_regulator_bulk_get(&client->dev, S5K6A3_NUM_SUPPLIES,
				      sensor->supplies);
	if (ret < 0)
		return ret;

	sd = &sensor->subdev;
	v4l2_i2c_subdev_init(sd, client, &s5k6a3_subdev_ops);
	sensor->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->internal_ops = &s5k6a3_sd_internal_ops;

	sensor->format.code = s5k6a3_formats[0].code;
	sensor->format.width = S5K6A3_DEFAULT_WIDTH;
	sensor->format.height = S5K6A3_DEFAULT_HEIGHT;

	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 1, &sensor->pad);
	if (ret < 0)
		return ret;

	pm_runtime_no_callbacks(dev);
	pm_runtime_enable(dev);

	ret = v4l2_async_register_subdev(sd);

	if (ret < 0) {
		pm_runtime_disable(&client->dev);
		media_entity_cleanup(&sd->entity);
	}

	return ret;
}

static int s5k6a3_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);

	pm_runtime_disable(&client->dev);
	v4l2_async_unregister_subdev(sd);
	media_entity_cleanup(&sd->entity);
	return 0;
}

static const struct i2c_device_id s5k6a3_ids[] = {
	{ }
};
MODULE_DEVICE_TABLE(i2c, s5k6a3_ids);

#ifdef CONFIG_OF
static const struct of_device_id s5k6a3_of_match[] = {
	{ .compatible = "samsung,s5k6a3" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s5k6a3_of_match);
#endif

static struct i2c_driver s5k6a3_driver = {
	.driver = {
		.of_match_table	= of_match_ptr(s5k6a3_of_match),
		.name		= S5K6A3_DRV_NAME,
	},
	.probe		= s5k6a3_probe,
	.remove		= s5k6a3_remove,
	.id_table	= s5k6a3_ids,
};

module_i2c_driver(s5k6a3_driver);

MODULE_DESCRIPTION("S5K6A3 image sensor subdev driver");
MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_LICENSE("GPL v2");
