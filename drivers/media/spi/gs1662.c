/*
 * GS1662 device registration.
 *
 * Copyright (C) 2015-2016 Nexvision
 * Author: Charles-Antoine Couret <charles-antoine.couret@nexvision.fr>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/module.h>

#include <linux/videodev2.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-subdev.h>
#include <media/v4l2-dv-timings.h>
#include <linux/v4l2-dv-timings.h>

#define REG_STATUS			0x04
#define REG_FORCE_FMT			0x06
#define REG_LINES_PER_FRAME		0x12
#define REG_WORDS_PER_LINE		0x13
#define REG_WORDS_PER_ACT_LINE		0x14
#define REG_ACT_LINES_PER_FRAME	0x15

#define MASK_H_LOCK			0x001
#define MASK_V_LOCK			0x002
#define MASK_STD_LOCK			0x004
#define MASK_FORCE_STD			0x020
#define MASK_STD_STATUS		0x3E0

#define GS_WIDTH_MIN			720
#define GS_WIDTH_MAX			2048
#define GS_HEIGHT_MIN			487
#define GS_HEIGHT_MAX			1080
#define GS_PIXELCLOCK_MIN		10519200
#define GS_PIXELCLOCK_MAX		74250000

struct gs {
	struct spi_device *pdev;
	struct v4l2_subdev sd;
	struct v4l2_dv_timings current_timings;
	int enabled;
};

struct gs_reg_fmt {
	u16 reg_value;
	struct v4l2_dv_timings format;
};

struct gs_reg_fmt_custom {
	u16 reg_value;
	__u32 width;
	__u32 height;
	__u64 pixelclock;
	__u32 interlaced;
};

static const struct spi_device_id gs_id[] = {
	{ "gs1662", 0 },
	{ }
};
MODULE_DEVICE_TABLE(spi, gs_id);

static const struct v4l2_dv_timings fmt_cap[] = {
	V4L2_DV_BT_SDI_720X487I60,
	V4L2_DV_BT_CEA_720X576P50,
	V4L2_DV_BT_CEA_1280X720P24,
	V4L2_DV_BT_CEA_1280X720P25,
	V4L2_DV_BT_CEA_1280X720P30,
	V4L2_DV_BT_CEA_1280X720P50,
	V4L2_DV_BT_CEA_1280X720P60,
	V4L2_DV_BT_CEA_1920X1080P24,
	V4L2_DV_BT_CEA_1920X1080P25,
	V4L2_DV_BT_CEA_1920X1080P30,
	V4L2_DV_BT_CEA_1920X1080I50,
	V4L2_DV_BT_CEA_1920X1080I60,
};

static const struct gs_reg_fmt reg_fmt[] = {
	{ 0x00, V4L2_DV_BT_CEA_1280X720P60 },
	{ 0x01, V4L2_DV_BT_CEA_1280X720P60 },
	{ 0x02, V4L2_DV_BT_CEA_1280X720P30 },
	{ 0x03, V4L2_DV_BT_CEA_1280X720P30 },
	{ 0x04, V4L2_DV_BT_CEA_1280X720P50 },
	{ 0x05, V4L2_DV_BT_CEA_1280X720P50 },
	{ 0x06, V4L2_DV_BT_CEA_1280X720P25 },
	{ 0x07, V4L2_DV_BT_CEA_1280X720P25 },
	{ 0x08, V4L2_DV_BT_CEA_1280X720P24 },
	{ 0x09, V4L2_DV_BT_CEA_1280X720P24 },
	{ 0x0A, V4L2_DV_BT_CEA_1920X1080I60 },
	{ 0x0B, V4L2_DV_BT_CEA_1920X1080P30 },

	/* Default value: keep this field before 0xC */
	{ 0x14, V4L2_DV_BT_CEA_1920X1080I50 },
	{ 0x0C, V4L2_DV_BT_CEA_1920X1080I50 },
	{ 0x0D, V4L2_DV_BT_CEA_1920X1080P25 },
	{ 0x0E, V4L2_DV_BT_CEA_1920X1080P25 },
	{ 0x10, V4L2_DV_BT_CEA_1920X1080P24 },
	{ 0x12, V4L2_DV_BT_CEA_1920X1080P24 },
	{ 0x16, V4L2_DV_BT_SDI_720X487I60 },
	{ 0x19, V4L2_DV_BT_SDI_720X487I60 },
	{ 0x18, V4L2_DV_BT_CEA_720X576P50 },
	{ 0x1A, V4L2_DV_BT_CEA_720X576P50 },

	/* Implement following timings before enable it.
	 * Because of we don't have access to these theoretical timings yet.
	 * Workaround: use functions to get and set registers for these formats.
	 */
#if 0
	{ 0x0F, V4L2_DV_BT_XXX_1920X1080I25 }, /* SMPTE 274M */
	{ 0x11, V4L2_DV_BT_XXX_1920X1080I24 }, /* SMPTE 274M */
	{ 0x13, V4L2_DV_BT_XXX_1920X1080I25 }, /* SMPTE 274M */
	{ 0x15, V4L2_DV_BT_XXX_1920X1035I60 }, /* SMPTE 260M */
	{ 0x17, V4L2_DV_BT_SDI_720X507I60 }, /* SMPTE 125M */
	{ 0x1B, V4L2_DV_BT_SDI_720X507I60 }, /* SMPTE 125M */
	{ 0x1C, V4L2_DV_BT_XXX_2048X1080P25 }, /* SMPTE 428.1M */
#endif
};

static const struct v4l2_dv_timings_cap gs_timings_cap = {
	.type = V4L2_DV_BT_656_1120,
	/* keep this initialization for compatibility with GCC < 4.4.6 */
	.reserved = { 0 },
	V4L2_INIT_BT_TIMINGS(GS_WIDTH_MIN, GS_WIDTH_MAX, GS_HEIGHT_MIN,
			     GS_HEIGHT_MAX, GS_PIXELCLOCK_MIN, GS_PIXELCLOCK_MAX,
			     V4L2_DV_BT_STD_CEA861 | V4L2_DV_BT_STD_SDI,
			     V4L2_DV_BT_CAP_PROGRESSIVE
			     | V4L2_DV_BT_CAP_INTERLACED)
};

static int gs_read_register(struct spi_device *spi, u16 addr, u16 *value)
{
	int ret;
	u16 buf_addr = (0x8000 | (0x0FFF & addr));
	u16 buf_value = 0;
	struct spi_message msg;
	struct spi_transfer tx[] = {
		{
			.tx_buf = &buf_addr,
			.len = 2,
			.delay_usecs = 1,
		}, {
			.rx_buf = &buf_value,
			.len = 2,
			.delay_usecs = 1,
		},
	};

	spi_message_init(&msg);
	spi_message_add_tail(&tx[0], &msg);
	spi_message_add_tail(&tx[1], &msg);
	ret = spi_sync(spi, &msg);

	*value = buf_value;

	return ret;
}

static int gs_write_register(struct spi_device *spi, u16 addr, u16 value)
{
	int ret;
	u16 buf_addr = addr;
	u16 buf_value = value;
	struct spi_message msg;
	struct spi_transfer tx[] = {
		{
			.tx_buf = &buf_addr,
			.len = 2,
			.delay_usecs = 1,
		}, {
			.tx_buf = &buf_value,
			.len = 2,
			.delay_usecs = 1,
		},
	};

	spi_message_init(&msg);
	spi_message_add_tail(&tx[0], &msg);
	spi_message_add_tail(&tx[1], &msg);
	ret = spi_sync(spi, &msg);

	return ret;
}

#ifdef CONFIG_VIDEO_ADV_DEBUG
static int gs_g_register(struct v4l2_subdev *sd,
		  struct v4l2_dbg_register *reg)
{
	struct spi_device *spi = v4l2_get_subdevdata(sd);
	u16 val;
	int ret;

	ret = gs_read_register(spi, reg->reg & 0xFFFF, &val);
	reg->val = val;
	reg->size = 2;
	return ret;
}

static int gs_s_register(struct v4l2_subdev *sd,
		  const struct v4l2_dbg_register *reg)
{
	struct spi_device *spi = v4l2_get_subdevdata(sd);

	return gs_write_register(spi, reg->reg & 0xFFFF, reg->val & 0xFFFF);
}
#endif

static int gs_status_format(u16 status, struct v4l2_dv_timings *timings)
{
	int std = (status & MASK_STD_STATUS) >> 5;
	int i;

	for (i = 0; i < ARRAY_SIZE(reg_fmt); i++) {
		if (reg_fmt[i].reg_value == std) {
			*timings = reg_fmt[i].format;
			return 0;
		}
	}

	return -ERANGE;
}

static u16 get_register_timings(struct v4l2_dv_timings *timings)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(reg_fmt); i++) {
		if (v4l2_match_dv_timings(timings, &reg_fmt[i].format, 0, false))
			return reg_fmt[i].reg_value | MASK_FORCE_STD;
	}

	return 0x0;
}

static inline struct gs *to_gs(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gs, sd);
}

static int gs_s_dv_timings(struct v4l2_subdev *sd,
		    struct v4l2_dv_timings *timings)
{
	struct gs *gs = to_gs(sd);
	int reg_value;

	reg_value = get_register_timings(timings);
	if (reg_value == 0x0)
		return -EINVAL;

	gs->current_timings = *timings;
	return 0;
}

static int gs_g_dv_timings(struct v4l2_subdev *sd,
		    struct v4l2_dv_timings *timings)
{
	struct gs *gs = to_gs(sd);

	*timings = gs->current_timings;
	return 0;
}

static int gs_query_dv_timings(struct v4l2_subdev *sd,
			struct v4l2_dv_timings *timings)
{
	struct gs *gs = to_gs(sd);
	struct v4l2_dv_timings fmt;
	u16 reg_value, i;
	int ret;

	if (gs->enabled)
		return -EBUSY;

	/* Check if the component detect a line, a frame or something else
	 * which looks like a video signal activity.*/
	for (i = 0; i < 4; i++) {
		gs_read_register(gs->pdev, REG_LINES_PER_FRAME + i, &reg_value);
		if (reg_value)
			break;
	}

	/* If no register reports a video signal */
	if (i >= 4)
		return -ENOLINK;

	gs_read_register(gs->pdev, REG_STATUS, &reg_value);
	if (!(reg_value & MASK_H_LOCK) || !(reg_value & MASK_V_LOCK))
		return -ENOLCK;
	if (!(reg_value & MASK_STD_LOCK))
		return -ERANGE;

	ret = gs_status_format(reg_value, &fmt);

	if (ret < 0)
		return ret;

	*timings = fmt;
	return 0;
}

static int gs_enum_dv_timings(struct v4l2_subdev *sd,
		       struct v4l2_enum_dv_timings *timings)
{
	if (timings->index >= ARRAY_SIZE(fmt_cap))
		return -EINVAL;

	if (timings->pad != 0)
		return -EINVAL;

	timings->timings = fmt_cap[timings->index];
	return 0;
}

static int gs_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct gs *gs = to_gs(sd);
	int reg_value;

	if (gs->enabled == enable)
		return 0;

	gs->enabled = enable;

	if (enable) {
		/* To force the specific format */
		reg_value = get_register_timings(&gs->current_timings);
		return gs_write_register(gs->pdev, REG_FORCE_FMT, reg_value);
	} else {
		/* To renable auto-detection mode */
		return gs_write_register(gs->pdev, REG_FORCE_FMT, 0x0);
	}
}

static int gs_g_input_status(struct v4l2_subdev *sd, u32 *status)
{
	struct gs *gs = to_gs(sd);
	u16 reg_value, i;
	int ret;

	/* Check if the component detect a line, a frame or something else
	 * which looks like a video signal activity.*/
	for (i = 0; i < 4; i++) {
		ret = gs_read_register(gs->pdev,
				       REG_LINES_PER_FRAME + i, &reg_value);
		if (reg_value)
			break;
		if (ret) {
			*status = V4L2_IN_ST_NO_POWER;
			return ret;
		}
	}

	/* If no register reports a video signal */
	if (i >= 4)
		*status |= V4L2_IN_ST_NO_SIGNAL;

	ret = gs_read_register(gs->pdev, REG_STATUS, &reg_value);
	if (!(reg_value & MASK_H_LOCK))
		*status |=  V4L2_IN_ST_NO_H_LOCK;
	if (!(reg_value & MASK_V_LOCK))
		*status |=  V4L2_IN_ST_NO_V_LOCK;
	if (!(reg_value & MASK_STD_LOCK))
		*status |=  V4L2_IN_ST_NO_STD_LOCK;

	return ret;
}

static int gs_dv_timings_cap(struct v4l2_subdev *sd,
			     struct v4l2_dv_timings_cap *cap)
{
	if (cap->pad != 0)
		return -EINVAL;

	*cap = gs_timings_cap;
	return 0;
}

/* V4L2 core operation handlers */
static const struct v4l2_subdev_core_ops gs_core_ops = {
#ifdef CONFIG_VIDEO_ADV_DEBUG
	.g_register = gs_g_register,
	.s_register = gs_s_register,
#endif
};

static const struct v4l2_subdev_video_ops gs_video_ops = {
	.s_dv_timings = gs_s_dv_timings,
	.g_dv_timings = gs_g_dv_timings,
	.s_stream = gs_s_stream,
	.g_input_status = gs_g_input_status,
	.query_dv_timings = gs_query_dv_timings,
};

static const struct v4l2_subdev_pad_ops gs_pad_ops = {
	.enum_dv_timings= gs_enum_dv_timings,
	.dv_timings_cap = gs_dv_timings_cap,
};

/* V4L2 top level operation handlers */
static const struct v4l2_subdev_ops gs_ops = {
	.core = &gs_core_ops,
	.video = &gs_video_ops,
	.pad = &gs_pad_ops,
};

static int gs_probe(struct spi_device *spi)
{
	int ret;
	struct gs *gs;
	struct v4l2_subdev *sd;

	gs = devm_kzalloc(&spi->dev, sizeof(struct gs), GFP_KERNEL);
	if (!gs)
		return -ENOMEM;

	gs->pdev = spi;
	sd = &gs->sd;

	spi->mode = SPI_MODE_0;
	spi->irq = -1;
	spi->max_speed_hz = 10000000;
	spi->bits_per_word = 16;
	ret = spi_setup(spi);
	v4l2_spi_subdev_init(sd, spi, &gs_ops);

	gs->current_timings = reg_fmt[0].format;
	gs->enabled = 0;

	/* Set H_CONFIG to SMPTE timings */
	gs_write_register(spi, 0x0, 0x300);

	return ret;
}

static int gs_remove(struct spi_device *spi)
{
	struct v4l2_subdev *sd = spi_get_drvdata(spi);
	struct gs *gs = to_gs(sd);

	v4l2_device_unregister_subdev(sd);
	kfree(gs);
	return 0;
}

static struct spi_driver gs_driver = {
	.driver = {
		.name		= "gs1662",
		.owner		= THIS_MODULE,
	},

	.probe		= gs_probe,
	.remove		= gs_remove,
	.id_table	= gs_id,
};

module_spi_driver(gs_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Charles-Antoine Couret <charles-antoine.couret@nexvision.fr>");
MODULE_DESCRIPTION("Gennum GS1662 HD/SD-SDI Serializer driver");
