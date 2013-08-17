/*
 * Samsung EXYNOS4412 FIMC-ISP driver
 *
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 * Sylwester Nawrocki <s.nawrocki@samsung.com>
 * Younghwan Joo <yhwan.joo@samsung.com>
 *
 * All rights reserved.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/memory.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/videodev2.h>
#include <media/v4l2-subdev.h>
#include <media/exynos_fimc_is.h>

#include "fimc-is.h"
#include "fimc-is-sensor.h"

const char * const fimc_is_get_sensor_name(struct fimc_is_sensor_info *info)
{
	enum fimc_is_sensor_id id = info->sensor_id;

	if ((id >= FIMC_IS_SENSOR_ID_S5K3H2) && (id < FIMC_IS_SENSOR_ID_END))
		return info->sensor_name;
	else
		return "ERROR";
}

static const char * const sensor_supply_names[] = {
	"svdda",
	"svddio",
};

static const struct sensor_pix_format fimc_is_sensor_formats[] = {
	{
		.code = V4L2_MBUS_FMT_SGRBG10_1X10,
	}
};

static struct fimc_is_sensor *sd_to_fimc_is_sensor(struct v4l2_subdev *sd)
{
	return container_of(sd, struct fimc_is_sensor, subdev);
}

static const struct sensor_pix_format *
			find_sensor_format(struct v4l2_mbus_framefmt *mf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(fimc_is_sensor_formats); i++)
		if (mf->code == fimc_is_sensor_formats[i].code)
			return &fimc_is_sensor_formats[i];
	return NULL;
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

static struct sensor_pix_format const *fimc_is_sensor_try_format(
	struct v4l2_mbus_framefmt *mf)
{
	struct sensor_pix_format const *sensor_fmt;

	sensor_fmt = find_sensor_format(mf);
	if (sensor_fmt == NULL)
		sensor_fmt = &fimc_is_sensor_formats[0];

	mf->code = sensor_fmt->code;
	return sensor_fmt;
}

static struct v4l2_mbus_framefmt *__fimc_is_sensor_get_format(
		struct fimc_is_sensor *state, struct v4l2_subdev_fh *fh,
		u32 pad, enum v4l2_subdev_format_whence which)
{
	if (which == V4L2_SUBDEV_FORMAT_TRY)
		return fh ? v4l2_subdev_get_try_format(fh, pad) : NULL;

	return &state->format;
}

static int fimc_is_sensor_set_fmt(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_format *fmt)
{
	struct fimc_is_sensor *state = sd_to_fimc_is_sensor(sd);
	struct sensor_pix_format const *sensor_fmt;
	struct v4l2_mbus_framefmt *mf;

	v4l2_info(sd, "%s: w: %d, h: %d\n", __func__,
					fmt->format.width, fmt->format.height);
	mf = __fimc_is_sensor_get_format(state, fh, fmt->pad, fmt->which);

	sensor_fmt = fimc_is_sensor_try_format(&fmt->format);
	if (mf) {
		mutex_lock(&state->lock);
		*mf = fmt->format;
		mutex_unlock(&state->lock);
	}
	return 0;
}

static int fimc_is_sensor_get_fmt(struct v4l2_subdev *sd,
				  struct v4l2_subdev_fh *fh,
				  struct v4l2_subdev_format *fmt)
{
	struct fimc_is_sensor *state = sd_to_fimc_is_sensor(sd);
	struct v4l2_mbus_framefmt *mf;

	mf = __fimc_is_sensor_get_format(state, fh, fmt->pad, fmt->which);
	if (!mf)
		return -EINVAL;

	mutex_lock(&state->lock);
	fmt->format = *mf;
	mutex_unlock(&state->lock);
	return 0;
}

static struct v4l2_subdev_pad_ops fimc_is_sensor_pad_ops = {
	.enum_mbus_code = fimc_is_sensor_enum_mbus_code,
	.get_fmt = fimc_is_sensor_get_fmt,
	.set_fmt = fimc_is_sensor_set_fmt,
};

static int fimc_is_sensor_open(struct v4l2_subdev *sd,
						struct v4l2_subdev_fh *fh)
{
	struct v4l2_mbus_framefmt *format = v4l2_subdev_get_try_format(fh, 0);

	format->colorspace = V4L2_COLORSPACE_JPEG;
	format->code = fimc_is_sensor_formats[0].code;
	format->width = FIMC_IS_SENSOR_DEF_PIX_WIDTH;
	format->height = FIMC_IS_SENSOR_DEF_PIX_HEIGHT;
	format->field = V4L2_FIELD_NONE;

	return 0;
}

static const struct v4l2_subdev_internal_ops fimc_is_sensor_sd_internal_ops = {
	.open = fimc_is_sensor_open,
};

static int fimc_is_sensor_s_power(struct v4l2_subdev *sd, int on)
{
	struct fimc_is_sensor *sensor = v4l2_get_subdevdata(sd);
	struct fimc_is *is = sensor->is;
	int gpio = sensor->board_info.gpio_reset;
	int i;

	v4l2_info(sd, "%s:%d: on: %d\n", __func__, __LINE__, on);

	if (on) {
		if (gpio_is_valid(gpio)) {
			gpio_set_value(gpio, 1);
			usleep_range(600, 800);

			gpio_set_value(gpio, 0);
			usleep_range(10600, 10800);

			gpio_set_value(gpio, 1);
		}
	} else {
		if (gpio_is_valid(gpio))
			gpio_set_value(gpio, 0);
	}

	for (i = 0; i < is->pdata->num_sensors; i++) {
		if (is->sensor[i].id == sensor->id)
			is->sensor_index = i;
	}
	return 0;
}

static struct v4l2_subdev_core_ops fimc_is_sensor_core_ops = {
	.s_power = fimc_is_sensor_s_power,
};

static int fimc_is_sensor_s_stream(struct v4l2_subdev *sd, int on)
{
	v4l2_info(sd, "%s:%d: on: %d\n", __func__, __LINE__, on);

	return 0;
}

static struct v4l2_subdev_video_ops fimc_is_sensor_video_ops = {
	.s_stream = fimc_is_sensor_s_stream,
};

static struct v4l2_subdev_ops fimc_is_sensor_subdev_ops = {
	.core = &fimc_is_sensor_core_ops,
	.pad = &fimc_is_sensor_pad_ops,
	.video = &fimc_is_sensor_video_ops,
};

int fimc_is_sensor_subdev_create(struct fimc_is_sensor *sensor,
				 struct fimc_is_sensor_info *inf)
{
	struct v4l2_subdev *sd = &sensor->subdev;
	int ret;

	if (WARN(inf == NULL, "Null sensor info\n"))
		return -EINVAL;

	mutex_init(&sensor->lock);
	sensor->board_info.gpio_reset = -EINVAL;

	ret = gpio_request_one(inf->gpio_reset, GPIOF_OUT_INIT_LOW,
			       inf->sensor_name);
	if (ret < 0)
		return ret;
	sensor->board_info.gpio_reset = inf->gpio_reset;

	v4l2_subdev_init(sd, &fimc_is_sensor_subdev_ops);
	sensor->subdev.owner = THIS_MODULE;
	strlcpy(sd->name, inf->sensor_name, sizeof(sd->name));
	sensor->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sensor->sensor_fmt = &fimc_is_sensor_formats[0];

	sensor->format.code = fimc_is_sensor_formats[0].code;
	sensor->format.width = FIMC_IS_SENSOR_DEF_PIX_WIDTH;
	sensor->format.height = FIMC_IS_SENSOR_DEF_PIX_HEIGHT;
	sensor->id = inf->sensor_id;
	sensor->i2c_ch = inf->i2c_id - 1;

	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_init(&sd->entity, 1, &sensor->pad, 0);
	if (ret < 0)
		goto err_me;

	v4l2_set_subdevdata(sd, sensor);

	return 0;

err_me:
	if (gpio_is_valid(sensor->board_info.gpio_reset))
		gpio_free(sensor->board_info.gpio_reset);
	return ret;
}

void fimc_is_sensor_subdev_destroy(struct fimc_is_sensor *sensor)
{
	regulator_bulk_free(SENSOR_NUM_SUPPLIES, sensor->supplies);
	media_entity_cleanup(&sensor->subdev.entity);
}

MODULE_AUTHOR("Sylwester Nawrocki <s.nawrocki@samsung.com>");
MODULE_DESCRIPTION("FIMC-IS image sensor subdev driver");
MODULE_LICENSE("GPL");
