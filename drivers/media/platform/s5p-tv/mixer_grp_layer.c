/*
 * Samsung TV Mixer driver
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * Tomasz Stanislawski, <t.stanislaws@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundiation. either version 2 of the License,
 * or (at your option) any later version
 */

#include "mixer.h"

#include <media/videobuf2-dma-contig.h>

/* FORMAT DEFINITIONS */

static const struct mxr_format mxr_fb_fmt_rgb565 = {
	.name = "RGB565",
	.fourcc = V4L2_PIX_FMT_RGB565,
	.colorspace = V4L2_COLORSPACE_SRGB,
	.num_planes = 1,
	.plane = {
		{ .width = 1, .height = 1, .size = 2 },
	},
	.num_subframes = 1,
	.cookie = 4,
};

static const struct mxr_format mxr_fb_fmt_argb1555 = {
	.name = "ARGB1555",
	.num_planes = 1,
	.fourcc = V4L2_PIX_FMT_RGB555,
	.colorspace = V4L2_COLORSPACE_SRGB,
	.plane = {
		{ .width = 1, .height = 1, .size = 2 },
	},
	.num_subframes = 1,
	.cookie = 5,
};

static const struct mxr_format mxr_fb_fmt_argb4444 = {
	.name = "ARGB4444",
	.num_planes = 1,
	.fourcc = V4L2_PIX_FMT_RGB444,
	.colorspace = V4L2_COLORSPACE_SRGB,
	.plane = {
		{ .width = 1, .height = 1, .size = 2 },
	},
	.num_subframes = 1,
	.cookie = 6,
};

static const struct mxr_format mxr_fb_fmt_argb8888 = {
	.name = "ARGB8888",
	.fourcc = V4L2_PIX_FMT_BGR32,
	.colorspace = V4L2_COLORSPACE_SRGB,
	.num_planes = 1,
	.plane = {
		{ .width = 1, .height = 1, .size = 4 },
	},
	.num_subframes = 1,
	.cookie = 7,
};

static const struct mxr_format *mxr_graph_format[] = {
	&mxr_fb_fmt_rgb565,
	&mxr_fb_fmt_argb1555,
	&mxr_fb_fmt_argb4444,
	&mxr_fb_fmt_argb8888,
};

/* AUXILIARY CALLBACKS */

static void mxr_graph_layer_release(struct mxr_layer *layer)
{
	mxr_base_layer_unregister(layer);
	mxr_base_layer_release(layer);
}

static void mxr_graph_buffer_set(struct mxr_layer *layer,
	struct mxr_buffer *buf)
{
	dma_addr_t addr = 0;

	if (buf)
		addr = vb2_dma_contig_plane_dma_addr(&buf->vb, 0);
	mxr_reg_graph_buffer(layer->mdev, layer->idx, addr);
}

static void mxr_graph_stream_set(struct mxr_layer *layer, int en)
{
	mxr_reg_graph_layer_stream(layer->mdev, layer->idx, en);
}

static void mxr_graph_format_set(struct mxr_layer *layer)
{
	mxr_reg_graph_format(layer->mdev, layer->idx,
		layer->fmt, &layer->geo);
}

static inline unsigned int closest(unsigned int x, unsigned int a,
	unsigned int b, unsigned long flags)
{
	unsigned int mid = (a + b) / 2;

	/* choosing closest value with constraints according to table:
	 * -------------+-----+-----+-----+-------+
	 * flags	|  0  |  LE |  GE | LE|GE |
	 * -------------+-----+-----+-----+-------+
	 * x <= a	|  a  |  a  |  a  |   a   |
	 * a < x <= mid	|  a  |  a  |  b  |   a   |
	 * mid < x < b	|  b  |  a  |  b  |   b   |
	 * b <= x	|  b  |  b  |  b  |   b   |
	 * -------------+-----+-----+-----+-------+
	 */

	/* remove all non-constraint flags */
	flags &= V4L2_SEL_FLAG_LE | V4L2_SEL_FLAG_GE;

	if (x <= a)
		return  a;
	if (x >= b)
		return b;
	if (flags == V4L2_SEL_FLAG_LE)
		return a;
	if (flags == V4L2_SEL_FLAG_GE)
		return b;
	if (x <= mid)
		return a;
	return b;
}

static inline unsigned int do_center(unsigned int center,
	unsigned int size, unsigned int upper, unsigned int flags)
{
	unsigned int lower;

	if (flags & MXR_NO_OFFSET)
		return 0;

	lower = center - min(center, size / 2);
	return min(lower, upper - size);
}

static void mxr_graph_fix_geometry(struct mxr_layer *layer,
	enum mxr_geometry_stage stage, unsigned long flags)
{
	struct mxr_geometry *geo = &layer->geo;
	struct mxr_crop *src = &geo->src;
	struct mxr_crop *dst = &geo->dst;
	unsigned int x_center, y_center;

	switch (stage) {

	case MXR_GEOMETRY_SINK: /* nothing to be fixed here */
		flags = 0;
		/* fall through */

	case MXR_GEOMETRY_COMPOSE:
		/* remember center of the area */
		x_center = dst->x_offset + dst->width / 2;
		y_center = dst->y_offset + dst->height / 2;
		/* round up/down to 2 multiple depending on flags */
		if (flags & V4L2_SEL_FLAG_LE) {
			dst->width = round_down(dst->width, 2);
			dst->height = round_down(dst->height, 2);
		} else {
			dst->width = round_up(dst->width, 2);
			dst->height = round_up(dst->height, 2);
		}
		/* assure that compose rect is inside display area */
		dst->width = min(dst->width, dst->full_width);
		dst->height = min(dst->height, dst->full_height);

		/* ensure that compose is reachable using 2x scaling */
		dst->width = min(dst->width, 2 * src->full_width);
		dst->height = min(dst->height, 2 * src->full_height);

		/* setup offsets */
		dst->x_offset = do_center(x_center, dst->width,
			dst->full_width, flags);
		dst->y_offset = do_center(y_center, dst->height,
			dst->full_height, flags);
		flags = 0;
		/* fall through */

	case MXR_GEOMETRY_CROP:
		/* remember center of the area */
		x_center = src->x_offset + src->width / 2;
		y_center = src->y_offset + src->height / 2;
		/* ensure that cropping area lies inside the buffer */
		if (src->full_width < dst->width)
			src->width = dst->width / 2;
		else
			src->width = closest(src->width, dst->width / 2,
				dst->width, flags);

		if (src->width == dst->width)
			geo->x_ratio = 0;
		else
			geo->x_ratio = 1;

		if (src->full_height < dst->height)
			src->height = dst->height / 2;
		else
			src->height = closest(src->height, dst->height / 2,
				dst->height, flags);

		if (src->height == dst->height)
			geo->y_ratio = 0;
		else
			geo->y_ratio = 1;

		/* setup offsets */
		src->x_offset = do_center(x_center, src->width,
			src->full_width, flags);
		src->y_offset = do_center(y_center, src->height,
			src->full_height, flags);
		flags = 0;
		/* fall through */
	case MXR_GEOMETRY_SOURCE:
		src->full_width = clamp_val(src->full_width,
			src->width + src->x_offset, 32767);
		src->full_height = clamp_val(src->full_height,
			src->height + src->y_offset, 2047);
	}
}

/* PUBLIC API */

struct mxr_layer *mxr_graph_layer_create(struct mxr_device *mdev, int idx)
{
	struct mxr_layer *layer;
	int ret;
	struct mxr_layer_ops ops = {
		.release = mxr_graph_layer_release,
		.buffer_set = mxr_graph_buffer_set,
		.stream_set = mxr_graph_stream_set,
		.format_set = mxr_graph_format_set,
		.fix_geometry = mxr_graph_fix_geometry,
	};
	char name[32];

	sprintf(name, "graph%d", idx);

	layer = mxr_base_layer_create(mdev, idx, name, &ops);
	if (layer == NULL) {
		mxr_err(mdev, "failed to initialize layer(%d) base\n", idx);
		goto fail;
	}

	layer->fmt_array = mxr_graph_format;
	layer->fmt_array_size = ARRAY_SIZE(mxr_graph_format);

	ret = mxr_base_layer_register(layer);
	if (ret)
		goto fail_layer;

	return layer;

fail_layer:
	mxr_base_layer_release(layer);

fail:
	return NULL;
}

