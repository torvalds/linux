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

#if defined(CONFIG_VIDEOBUF2_CMA_PHYS)
#include <media/videobuf2-cma-phys.h>
#elif defined(CONFIG_VIDEOBUF2_ION)
#include <media/videobuf2-ion.h>
#endif

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
	struct mxr_device *mdev = layer->mdev;
	dma_addr_t addr = 0;

	if (buf)
		addr = mdev->vb2->plane_addr(&buf->vb, 0);
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

static void mxr_graph_fix_geometry(struct mxr_layer *layer)
{
	struct mxr_geometry *geo = &layer->geo;

	mxr_dbg(layer->mdev, "%s start\n", __func__);
	/* limit to boundary size */
	geo->src.full_width = clamp_val(geo->src.full_width, 1, 32767);
	geo->src.full_height = clamp_val(geo->src.full_height, 1, 2047);

	/* limit to coordinate of source x, y */
	geo->src.x_offset = clamp_val(geo->src.x_offset, 0,
			geo->src.full_width - 1);
	geo->src.y_offset = clamp_val(geo->src.y_offset, 0,
			geo->src.full_height - 1);

	/* limit to boundary size of crop width, height */
	geo->src.width = clamp_val(geo->src.width, 1,
			geo->src.full_width - geo->src.x_offset);
	geo->src.height = clamp_val(geo->src.height, 1,
			geo->src.full_height - geo->src.y_offset);

	/* dst full resolution and TV display size are same */

	geo->dst.x_offset = clamp_val(geo->dst.x_offset, 0,
			geo->dst.full_width - 1);
	geo->dst.y_offset = clamp_val(geo->dst.y_offset, 0,
			geo->dst.full_height - 1);

	/* mixer scale-up is unuseful. so no use it */
	geo->dst.width = clamp_val(geo->src.width, 1,
			geo->dst.full_width - geo->dst.x_offset);
	geo->dst.height = clamp_val(geo->src.height, 1,
			geo->dst.full_height - geo->dst.y_offset);
}

/* PUBLIC API */

struct mxr_layer *mxr_graph_layer_create(struct mxr_device *mdev, int cur_mxr,
		int idx, int nr)
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

	sprintf(name, "mxr%d_graph%d", cur_mxr, idx);

	layer = mxr_base_layer_create(mdev, idx, name, &ops);
	if (layer == NULL) {
		mxr_err(mdev, "failed to initialize layer(%d) base\n", idx);
		goto fail;
	}

	layer->fmt_array = mxr_graph_format;
	layer->fmt_array_size = ARRAY_SIZE(mxr_graph_format);
	layer->minor = nr;
	layer->type = MXR_LAYER_TYPE_GRP;

	ret = mxr_base_layer_register(layer);
	if (ret)
		goto fail_layer;

	layer->cur_mxr = cur_mxr;
	return layer;

fail_layer:
	mxr_base_layer_release(layer);

fail:
	return NULL;
}

