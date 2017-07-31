/*
 * Copyright (c) 2015-2016 MediaTek Inc.
 * Author: Houlong Wei <houlong.wei@mediatek.com>
 *         Ming Hsiu Tsai <minghsiu.tsai@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/platform_device.h>

#include "mtk_mdp_core.h"
#include "mtk_mdp_regs.h"


#define MDP_COLORFMT_PACK(VIDEO, PLANE, COPLANE, HF, VF, BITS, GROUP, SWAP, ID)\
	(((VIDEO) << 27) | ((PLANE) << 24) | ((COPLANE) << 22) |\
	((HF) << 20) | ((VF) << 18) | ((BITS) << 8) | ((GROUP) << 6) |\
	((SWAP) << 5) | ((ID) << 0))

enum MDP_COLOR_ENUM {
	MDP_COLOR_UNKNOWN = 0,
	MDP_COLOR_NV12 = MDP_COLORFMT_PACK(0, 2, 1, 1, 1, 8, 1, 0, 12),
	MDP_COLOR_I420 = MDP_COLORFMT_PACK(0, 3, 0, 1, 1, 8, 1, 0, 8),
	MDP_COLOR_YV12 = MDP_COLORFMT_PACK(0, 3, 0, 1, 1, 8, 1, 1, 8),
	/* Mediatek proprietary format */
	MDP_COLOR_420_MT21 = MDP_COLORFMT_PACK(5, 2, 1, 1, 1, 256, 1, 0, 12),
};

static int32_t mtk_mdp_map_color_format(int v4l2_format)
{
	switch (v4l2_format) {
	case V4L2_PIX_FMT_NV12M:
	case V4L2_PIX_FMT_NV12:
		return MDP_COLOR_NV12;
	case V4L2_PIX_FMT_MT21C:
		return MDP_COLOR_420_MT21;
	case V4L2_PIX_FMT_YUV420M:
	case V4L2_PIX_FMT_YUV420:
		return MDP_COLOR_I420;
	case V4L2_PIX_FMT_YVU420:
		return MDP_COLOR_YV12;
	}

	mtk_mdp_err("Unknown format 0x%x", v4l2_format);

	return MDP_COLOR_UNKNOWN;
}

void mtk_mdp_hw_set_input_addr(struct mtk_mdp_ctx *ctx,
			       struct mtk_mdp_addr *addr)
{
	struct mdp_buffer *src_buf = &ctx->vpu.vsi->src_buffer;
	int i;

	for (i = 0; i < ARRAY_SIZE(addr->addr); i++)
		src_buf->addr_mva[i] = (uint64_t)addr->addr[i];
}

void mtk_mdp_hw_set_output_addr(struct mtk_mdp_ctx *ctx,
				struct mtk_mdp_addr *addr)
{
	struct mdp_buffer *dst_buf = &ctx->vpu.vsi->dst_buffer;
	int i;

	for (i = 0; i < ARRAY_SIZE(addr->addr); i++)
		dst_buf->addr_mva[i] = (uint64_t)addr->addr[i];
}

void mtk_mdp_hw_set_in_size(struct mtk_mdp_ctx *ctx)
{
	struct mtk_mdp_frame *frame = &ctx->s_frame;
	struct mdp_config *config = &ctx->vpu.vsi->src_config;

	/* Set input pixel offset */
	config->crop_x = frame->crop.left;
	config->crop_y = frame->crop.top;

	/* Set input cropped size */
	config->crop_w = frame->crop.width;
	config->crop_h = frame->crop.height;

	/* Set input original size */
	config->x = 0;
	config->y = 0;
	config->w = frame->width;
	config->h = frame->height;
}

void mtk_mdp_hw_set_in_image_format(struct mtk_mdp_ctx *ctx)
{
	unsigned int i;
	struct mtk_mdp_frame *frame = &ctx->s_frame;
	struct mdp_config *config = &ctx->vpu.vsi->src_config;
	struct mdp_buffer *src_buf = &ctx->vpu.vsi->src_buffer;

	src_buf->plane_num = frame->fmt->num_comp;
	config->format = mtk_mdp_map_color_format(frame->fmt->pixelformat);
	config->w_stride = 0; /* MDP will calculate it by color format. */
	config->h_stride = 0; /* MDP will calculate it by color format. */

	for (i = 0; i < src_buf->plane_num; i++)
		src_buf->plane_size[i] = frame->payload[i];
}

void mtk_mdp_hw_set_out_size(struct mtk_mdp_ctx *ctx)
{
	struct mtk_mdp_frame *frame = &ctx->d_frame;
	struct mdp_config *config = &ctx->vpu.vsi->dst_config;

	config->crop_x = frame->crop.left;
	config->crop_y = frame->crop.top;
	config->crop_w = frame->crop.width;
	config->crop_h = frame->crop.height;
	config->x = 0;
	config->y = 0;
	config->w = frame->width;
	config->h = frame->height;
}

void mtk_mdp_hw_set_out_image_format(struct mtk_mdp_ctx *ctx)
{
	unsigned int i;
	struct mtk_mdp_frame *frame = &ctx->d_frame;
	struct mdp_config *config = &ctx->vpu.vsi->dst_config;
	struct mdp_buffer *dst_buf = &ctx->vpu.vsi->dst_buffer;

	dst_buf->plane_num = frame->fmt->num_comp;
	config->format = mtk_mdp_map_color_format(frame->fmt->pixelformat);
	config->w_stride = 0; /* MDP will calculate it by color format. */
	config->h_stride = 0; /* MDP will calculate it by color format. */
	for (i = 0; i < dst_buf->plane_num; i++)
		dst_buf->plane_size[i] = frame->payload[i];
}

void mtk_mdp_hw_set_rotation(struct mtk_mdp_ctx *ctx)
{
	struct mdp_config_misc *misc = &ctx->vpu.vsi->misc;

	misc->orientation = ctx->ctrls.rotate->val;
	misc->hflip = ctx->ctrls.hflip->val;
	misc->vflip = ctx->ctrls.vflip->val;
}

void mtk_mdp_hw_set_global_alpha(struct mtk_mdp_ctx *ctx)
{
	struct mdp_config_misc *misc = &ctx->vpu.vsi->misc;

	misc->alpha = ctx->ctrls.global_alpha->val;
}
