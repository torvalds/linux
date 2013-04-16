/*
 * Samsung EXYNOS4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *
 * Author: Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
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
#include <media/v4l2-subdev.h>

#include "fimc-is.h"
#include "fimc-is-sensor.h"

#define DRIVER_NAME "FIMC-IS-SENSOR"

static const char * const sensor_supply_names[] = {
	"svdda",
	"svddio",
};

static const struct v4l2_mbus_framefmt fimc_is_sensor_formats[] = {
	{
		.code = V4L2_MBUS_FMT_SGRBG10_1X10,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.field = V4L2_FIELD_NONE,
	}
};

static struct fimc_is_sensor *sd_to_fimc_is_sensor(struct v4l2_subdev *sd)
{
	return container_of(sd, struct fimc_is_sensor, subdev);
}

static const struct v4l2_mbus_framefmt *find_sensor_format(
	struct v4l2_mbus_framefmt *mf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fimc_is_sensor_formats); i++)
		if (mf->code == fimc_is_sensor_formats[i].code)
			return &fimc_is_sensor_formats[i];

	return &fimc_is_sensor_formats[0];
}

static int fimc_is_sensor_enum_mbus_code(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index >= ARRAY_SIZE(fimc_is_sensor_formats))
		return -EINVAL;

	code->code = fimc_is_sensor_formats[code->index].code;
	return 0;
}

static void fimc_is_sensor_try_format(struct fimc_is_sensor *sensor,
				      struct v4l2_mbus_framefmt *mf)
{
	const struct sensor_drv_data *dd = sensor->drvdata;
	const struct v4l2_mbus_framefmt *fmt;

	fmt = find_sensor_format(mf);
	mf->code = fmt->code;
	v4l_bound_align_image(&mf->width, 16 + 8, dd->width, 0,
			      &mf->height, 12 + 8, dd->height, 0, 0);
}

static struct v4l2_mbus_framefmt *__fimc_is_sensor_get_format(
		struct fimc_is_sensor *sensor, struct v4l2_subdev_fh *fh,
		u32 pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return fh ? v4l2_subdev_get_try_format(fh, pad) : NULL;

	return &sensor->format;
}

static int fimc_is_sensor_set_fmt(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_format *fmt)
{
	struct fimc_is_sensor *sensor = sd_to_fimc_is_sensor(sd);
	struct v4l2_mbus_framefmt *mf;

	fimc_is_sensor_try_format(sensor, &fmt->format);

	mf = __fimc_is_sensor_get_format(sensor, fh, fmt->pad, fmt->which);
	if (mf) {
		mutex_lock(&sensor->lock);
		if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
			*mf = fmt->format;
		mutex_unlock(&sensor->lock);
	}
	return 0;
}

static int fimc_is_sensor_get_fmt(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_format *fmt)
{
	struct fimc_is_sensor *sensor = sd_to_fimc_is_sensor(sd);
	struct v4l2_mbus_framefmt *mf;

	mf = __fimc_is_sensor_get_format(sensor, fh, fmt->pad, fmt->which);

	mutex_lock(&sensor->lock);
	fmt->format = *mf;
	mutex_unlock(&sensor->lock);
	return 0;
}

static struct v4l2_subdev_pad_ops fimc_is_sensor_pad_ops = {
	.enum_mbus_code	= fimc_is_sensor_enum_mbus_code,
	.get_fmt	= fimc_is_sensor_get_fmt,
	.set_fmt	= fimc_is_sensor_set_fmt,
};

static int fimc_is_sensor_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format = v4l2_subdev_get_try_format(fh, 0);

	*format		= fimc_is_sensor_formats[0];
	format->width	= FIMC_IS_SENSOR_DEF_PIX_WIDTH;
	format->height	= FIMC_IS_SENSOR_DEF_PIX_HEIGHT;

	return 0;
}

static const struct v4l2_subdev_internal_ops fimc_is_sensor_sd_internal_ops = {
	.open = fimc_is_sensor_open,
};

static int fimc_is_sensor_s_power(struct v4l2_subdev *sd, int on)
{
	struct fimc_is_sensor *sensor = v4l2_get_subdevdata(sd);
	int gpio = sensor->gpio_reset;
	int ret;

	if (on) {
		ret = pm_runtime_get(sensor->dev);
		if (ret < 0)
			return ret;

		ret = regulator_bulk_enable(SENSOR_NUM_SUPPLIES,
					    sensor->supplies);
		if (ret < 0) {
			pm_runtime_put(sensor->dev);
			return ret;
		}
		if (gpio_is_valid(gpio)) {
			gpio_set_value(gpio, 1);
			usleep_range(600, 800);
			gpio_set_value(gpio, 0);
			usleep_range(10000, 11000);
			gpio_set_value(gpio, 1);
		}

		/* A delay needed for the sensor initialization. */
		msleep(20);
	} else {
		if (gpio_is_valid(gpio))
			gpio_set_value(gpio, 0);

		ret = regulator_bulk_disable(SENSOR_NUM_SUPPLIES,
					     sensor->supplies);
		if (!ret)
			pm_runtime_put(sensor->dev);
	}

	pr_info("%s:%d: on: %d, ret: %d\n", __func__, __LINE__, on, ret);

	return ret;
}

static struct v4l2_subdev_core_ops fimc_is_sensor_core_ops = {
	.s_power = fimc_is_sensor_s_power,
};

static struct v4l2_subdev_ops fimc_is_sensor_subdev_ops = {
	.core = &fimc_is_sensor_core_ops,
	.pad = &fimc_is_sensor_pad_ops,
};

static const struct of_device_id fimc_is_sensor_of_match[];

static int fimc_is_sensor_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct fimc_is_sensor *sensor;
	const struct of_device_id *of_id;
	struct v4l2_subdev *sd;
	int gpio, i, ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	mutex_init(&sensor->lock);
	sensor->gpio_reset = -EINVAL;

	gpio = of_get_gpio_flags(dev->of_node, 0, NULL);
	if (gpio_is_valid(gpio)) {
		ret = gpio_request_one(gpio, GPIOF_OUT_INIT_LOW, DRIVER_NAME);
		if (ret < 0)
			return ret;
	}
	sensor->gpio_reset = gpio;

	for (i = 0; i < SENSOR_NUM_SUPPLIES; i++)
		sensor->supplies[i].supply = sensor_supply_names[i];

	ret = devm_regulator_bulk_get(&client->dev, SENSOR_NUM_SUPPLIES,
				      sensor->supplies);
	if (ret < 0)
		goto err_gpio;

	of_id = of_match_node(fimc_is_sensor_of_match, dev->of_node);
	if (!of_id) {
		ret = -ENODEV;
		goto err_reg;
	}

	sensor->drvdata = of_id->data;
	sensor->dev = dev;

	sd = &sensor->subdev;
	v4l2_i2c_subdev_init(sd, client, &fimc_is_sensor_subdev_ops);
	snprintf(sd->name, sizeof(sd->name), sensor->drvdata->subdev_name);
	sensor->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	sensor->format.code = fimc_is_sensor_formats[0].code;
	sensor->format.width = FIMC_IS_SENSOR_DEF_PIX_WIDTH;
	sensor->format.height = FIMC_IS_SENSOR_DEF_PIX_HEIGHT;

	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&sd->entity, 1, &sensor->pad, 0);
	if (ret < 0)
		goto err_reg;

	v4l2_set_subdevdata(sd, sensor);
	pm_runtime_no_callbacks(dev);
	pm_runtime_enable(dev);

	return 0;
err_reg:
	regulator_bulk_free(SENSOR_NUM_SUPPLIES, sensor->supplies);
err_gpio:
	if (gpio_is_valid(sensor->gpio_reset))
		gpio_free(sensor->gpio_reset);
	return ret;
}

static int fimc_is_sensor_remove(struct i2c_client *client)
{
	struct fimc_is_sensor *sensor;

	regulator_bulk_free(SENSOR_NUM_SUPPLIES, sensor->supplies);
	media_entity_cleanup(&sensor->subdev.entity);

	return 0;
}

static const struct i2c_device_id fimc_is_sensor_ids[] = {
	{ }
};

static const struct sensor_drv_data s5k6a3_drvdata = {
	.id		= FIMC_IS_SENSOR_ID_S5K6A3,
	.subdev_name	= "S5K6A3",
	.width		= S5K6A3_SENSOR_WIDTH,
	.height		= S5K6A3_SENSOR_HEIGHT,
};

static const struct of_device_id fimc_is_sensor_of_match[] = {
	{
		.compatible	= "samsung,s5k6a3",
		.data		= &s5k6a3_drvdata,
	},
	{  }
};

static struct i2c_driver fimc_is_sensor_driver = {
	.driver = {
		.of_match_table	= fimc_is_sensor_of_match,
		.name		= DRIVER_NAME,
		.owner		= THIS_MODULE,
	},
	.probe		= fimc_is_sensor_probe,
	.remove		= fimc_is_sensor_remove,
	.id_table	= fimc_is_sensor_ids,
};

int fimc_is_register_sensor_driver(void)
{
	return i2c_add_driver(&fimc_is_sensor_driver);
}

void fimc_is_unregister_sensor_driver(void)
{
	i2c_del_driver(&fimc_is_sensor_driver);
}

MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_DESCRIPTION("Exynos4x12 FIMC-IS image sensor subdev driver");
MODULE_LICENSE("GPL");
