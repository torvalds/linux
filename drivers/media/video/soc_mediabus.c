/*
 * soc-camera media bus helper routines
 *
 * Copyright (C) 2009, Guennadi Liakhovetski <g.liakhovetski@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>

#include <media/v4l2-device.h>
#include <media/v4l2-mediabus.h>
#include <media/soc_mediabus.h>

static const struct soc_mbus_lookup mbus_fmt[] = {
{
	.code = V4L2_MBUS_FMT_YUYV8_2X8,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_YUYV,
		.name			= "YUYV",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_YVYU8_2X8,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_YVYU,
		.name			= "YVYU",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_UYVY8_2X8,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_UYVY,
		.name			= "UYVY",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_VYUY8_2X8,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_VYUY,
		.name			= "VYUY",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_RGB555_2X8_PADHI_LE,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_RGB555,
		.name			= "RGB555",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_RGB555_2X8_PADHI_BE,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_RGB555X,
		.name			= "RGB555X",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_RGB565_2X8_LE,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_RGB565,
		.name			= "RGB565",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_RGB565_2X8_BE,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_RGB565X,
		.name			= "RGB565X",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_SBGGR8_1X8,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_SBGGR8,
		.name			= "Bayer 8 BGGR",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_SBGGR10_1X10,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_SBGGR10,
		.name			= "Bayer 10 BGGR",
		.bits_per_sample	= 10,
		.packing		= SOC_MBUS_PACKING_EXTEND16,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_Y8_1X8,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_GREY,
		.name			= "Grey",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_NONE,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_Y10_1X10,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_Y10,
		.name			= "Grey 10bit",
		.bits_per_sample	= 10,
		.packing		= SOC_MBUS_PACKING_EXTEND16,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_LE,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_SBGGR10,
		.name			= "Bayer 10 BGGR",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_SBGGR10_2X8_PADLO_LE,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_SBGGR10,
		.name			= "Bayer 10 BGGR",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADLO,
		.order			= SOC_MBUS_ORDER_LE,
	},
}, {
	.code = V4L2_MBUS_FMT_SBGGR10_2X8_PADHI_BE,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_SBGGR10,
		.name			= "Bayer 10 BGGR",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADHI,
		.order			= SOC_MBUS_ORDER_BE,
	},
}, {
	.code = V4L2_MBUS_FMT_SBGGR10_2X8_PADLO_BE,
	.fmt = {
		.fourcc			= V4L2_PIX_FMT_SBGGR10,
		.name			= "Bayer 10 BGGR",
		.bits_per_sample	= 8,
		.packing		= SOC_MBUS_PACKING_2X8_PADLO,
		.order			= SOC_MBUS_ORDER_BE,
	},
}, {
	.code = V4L2_MBUS_FMT_JPEG_1X8,
	.fmt = {
		.fourcc                 = V4L2_PIX_FMT_JPEG,
		.name                   = "JPEG",
		.bits_per_sample        = 8,
		.packing                = SOC_MBUS_PACKING_VARIABLE,
		.order                  = SOC_MBUS_ORDER_LE,
	},
},
};

int soc_mbus_samples_per_pixel(const struct soc_mbus_pixelfmt *mf)
{
	switch (mf->packing) {
	case SOC_MBUS_PACKING_NONE:
	case SOC_MBUS_PACKING_EXTEND16:
		return 1;
	case SOC_MBUS_PACKING_2X8_PADHI:
	case SOC_MBUS_PACKING_2X8_PADLO:
		return 2;
	case SOC_MBUS_PACKING_VARIABLE:
		return 0;
	}
	return -EINVAL;
}
EXPORT_SYMBOL(soc_mbus_samples_per_pixel);

s32 soc_mbus_bytes_per_line(u32 width, const struct soc_mbus_pixelfmt *mf)
{
	switch (mf->packing) {
	case SOC_MBUS_PACKING_NONE:
		return width * mf->bits_per_sample / 8;
	case SOC_MBUS_PACKING_2X8_PADHI:
	case SOC_MBUS_PACKING_2X8_PADLO:
	case SOC_MBUS_PACKING_EXTEND16:
		return width * 2;
	case SOC_MBUS_PACKING_VARIABLE:
		return 0;
	}
	return -EINVAL;
}
EXPORT_SYMBOL(soc_mbus_bytes_per_line);

const struct soc_mbus_pixelfmt *soc_mbus_find_fmtdesc(
	enum v4l2_mbus_pixelcode code,
	const struct soc_mbus_lookup *lookup,
	int n)
{
	int i;

	for (i = 0; i < n; i++)
		if (lookup[i].code == code)
			return &lookup[i].fmt;

	return NULL;
}
EXPORT_SYMBOL(soc_mbus_find_fmtdesc);

const struct soc_mbus_pixelfmt *soc_mbus_get_fmtdesc(
	enum v4l2_mbus_pixelcode code)
{
	return soc_mbus_find_fmtdesc(code, mbus_fmt, ARRAY_SIZE(mbus_fmt));
}
EXPORT_SYMBOL(soc_mbus_get_fmtdesc);

static int __init soc_mbus_init(void)
{
	return 0;
}

static void __exit soc_mbus_exit(void)
{
}

module_init(soc_mbus_init);
module_exit(soc_mbus_exit);

MODULE_DESCRIPTION("soc-camera media bus interface");
MODULE_AUTHOR("Guennadi Liakhovetski <g.liakhovetski@gmx.de>");
MODULE_LICENSE("GPL v2");
