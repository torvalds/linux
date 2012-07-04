/*
 * linux/drivers/media/video/s5p-mfc/s5p_mfc_enc.c
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Jeongtae Park	<jtp.park@samsung.com>
 * Kamil Debski		<k.debski@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-core.h>
#include "regs-mfc.h"
#include "s5p_mfc_common.h"
#include "s5p_mfc_debug.h"
#include "s5p_mfc_enc.h"
#include "s5p_mfc_intr.h"
#include "s5p_mfc_opr.h"

static struct s5p_mfc_fmt formats[] = {
	{
		.name = "4:2:0 2 Planes 64x32 Tiles",
		.fourcc = V4L2_PIX_FMT_NV12MT,
		.codec_mode = S5P_FIMV_CODEC_NONE,
		.type = MFC_FMT_RAW,
		.num_planes = 2,
	},
	{
		.name = "4:2:0 2 Planes",
		.fourcc = V4L2_PIX_FMT_NV12M,
		.codec_mode = S5P_FIMV_CODEC_NONE,
		.type = MFC_FMT_RAW,
		.num_planes = 2,
	},
	{
		.name = "H264 Encoded Stream",
		.fourcc = V4L2_PIX_FMT_H264,
		.codec_mode = S5P_FIMV_CODEC_H264_ENC,
		.type = MFC_FMT_ENC,
		.num_planes = 1,
	},
	{
		.name = "MPEG4 Encoded Stream",
		.fourcc = V4L2_PIX_FMT_MPEG4,
		.codec_mode = S5P_FIMV_CODEC_MPEG4_ENC,
		.type = MFC_FMT_ENC,
		.num_planes = 1,
	},
	{
		.name = "H263 Encoded Stream",
		.fourcc = V4L2_PIX_FMT_H263,
		.codec_mode = S5P_FIMV_CODEC_H263_ENC,
		.type = MFC_FMT_ENC,
		.num_planes = 1,
	},
};

#define NUM_FORMATS ARRAY_SIZE(formats)
static struct s5p_mfc_fmt *find_format(struct v4l2_format *f, unsigned int t)
{
	unsigned int i;

	for (i = 0; i < NUM_FORMATS; i++) {
		if (formats[i].fourcc == f->fmt.pix_mp.pixelformat &&
		    formats[i].type == t)
			return &formats[i];
	}
	return NULL;
}

static struct mfc_control controls[] = {
	{
		.id = V4L2_CID_MPEG_VIDEO_GOP_SIZE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE,
		.type = V4L2_CTRL_TYPE_MENU,
		.minimum = V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE,
		.maximum = V4L2_MPEG_VIDEO_MULTI_SICE_MODE_MAX_BYTES,
		.default_value = V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE,
		.menu_skip_mask = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 1,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 1900,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 1900,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_MFC51_VIDEO_PADDING,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Padding Control Enable",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_MFC51_VIDEO_PADDING_YUV,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Padding Color YUV Value",
		.minimum = 0,
		.maximum = (1 << 25) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_BITRATE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 1,
		.maximum = (1 << 30) - 1,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_MFC51_VIDEO_RC_REACTION_COEFF,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "Rate Control Reaction Coeff.",
		.minimum = 1,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_MFC51_VIDEO_FORCE_FRAME_TYPE,
		.type = V4L2_CTRL_TYPE_MENU,
		.name = "Force frame type",
		.minimum = V4L2_MPEG_MFC51_VIDEO_FORCE_FRAME_TYPE_DISABLED,
		.maximum = V4L2_MPEG_MFC51_VIDEO_FORCE_FRAME_TYPE_NOT_CODED,
		.default_value = V4L2_MPEG_MFC51_VIDEO_FORCE_FRAME_TYPE_DISABLED,
		.menu_skip_mask = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_VBV_SIZE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_HEADER_MODE,
		.type = V4L2_CTRL_TYPE_MENU,
		.minimum = V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE,
		.maximum = V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		.default_value = V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE,
		.menu_skip_mask = 0,
	},
	{
		.id = V4L2_CID_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE,
		.type = V4L2_CTRL_TYPE_MENU,
		.name = "Frame Skip Enable",
		.minimum = V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_DISABLED,
		.maximum = V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_BUF_LIMIT,
		.menu_skip_mask = 0,
		.default_value = V4L2_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE_DISABLED,
	},
	{
		.id = V4L2_CID_MPEG_MFC51_VIDEO_RC_FIXED_TARGET_BIT,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Fixed Target Bit Enable",
		.minimum = 0,
		.maximum = 1,
		.default_value = 0,
		.menu_skip_mask = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_B_FRAMES,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = 2,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE,
		.type = V4L2_CTRL_TYPE_MENU,
		.minimum = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
		.maximum = V4L2_MPEG_VIDEO_H264_PROFILE_MULTIVIEW_HIGH,
		.default_value = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
		.menu_skip_mask = ~(
				(1 << V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
				(1 << V4L2_MPEG_VIDEO_H264_PROFILE_MAIN) |
				(1 << V4L2_MPEG_VIDEO_H264_PROFILE_HIGH)
				),
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_LEVEL,
		.type = V4L2_CTRL_TYPE_MENU,
		.minimum = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
		.maximum = V4L2_MPEG_VIDEO_H264_LEVEL_4_0,
		.default_value = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL,
		.type = V4L2_CTRL_TYPE_MENU,
		.minimum = V4L2_MPEG_VIDEO_MPEG4_LEVEL_0,
		.maximum = V4L2_MPEG_VIDEO_MPEG4_LEVEL_5,
		.default_value = V4L2_MPEG_VIDEO_MPEG4_LEVEL_0,
		.menu_skip_mask = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE,
		.type = V4L2_CTRL_TYPE_MENU,
		.minimum = V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_ENABLED,
		.maximum = V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED_AT_SLICE_BOUNDARY,
		.default_value = V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_ENABLED,
		.menu_skip_mask = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = -6,
		.maximum = 6,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = -6,
		.maximum = 6,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE,
		.type = V4L2_CTRL_TYPE_MENU,
		.minimum = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC,
		.maximum = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC,
		.default_value = V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC,
		.menu_skip_mask = 0,
	},
	{
		.id = V4L2_CID_MPEG_MFC51_VIDEO_H264_NUM_REF_PIC_FOR_P,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "The Number of Ref. Pic for P",
		.minimum = 1,
		.maximum = 2,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_MIN_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_MAX_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H263_I_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "H263 I-Frame QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H263_MIN_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "H263 Minimum QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H263_MAX_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "H263 Maximum QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H263_P_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "H263 P frame QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H263_B_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "H263 B frame QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "MPEG4 I-Frame QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_MPEG4_MIN_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "MPEG4 Minimum QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_MPEG4_MAX_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "MPEG4 Maximum QP value",
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "MPEG4 P frame QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_MPEG4_B_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.name = "MPEG4 B frame QP value",
		.minimum = 1,
		.maximum = 31,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_DARK,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "H264 Dark Reg Adaptive RC",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_SMOOTH,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "H264 Smooth Reg Adaptive RC",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_STATIC,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "H264 Static Reg Adaptive RC",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_ACTIVITY,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "H264 Activity Reg Adaptive RC",
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC,
		.type = V4L2_CTRL_TYPE_MENU,
		.minimum = V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_UNSPECIFIED,
		.maximum = V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_EXTENDED,
		.default_value = V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_UNSPECIFIED,
		.menu_skip_mask = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_GOP_CLOSURE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 1,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_I_PERIOD,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = (1 << 16) - 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE,
		.type = V4L2_CTRL_TYPE_MENU,
		.minimum = V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE,
		.maximum = V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_SIMPLE,
		.default_value = V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE,
		.menu_skip_mask = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_MPEG4_QPEL,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
};

#define NUM_CTRLS ARRAY_SIZE(controls)
static const char * const *mfc51_get_menu(u32 id)
{
	static const char * const mfc51_video_frame_skip[] = {
		"Disabled",
		"Level Limit",
		"VBV/CPB Limit",
		NULL,
	};
	static const char * const mfc51_video_force_frame[] = {
		"Disabled",
		"I Frame",
		"Not Coded",
		NULL,
	};
	switch (id) {
	case V4L2_CID_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE:
		return mfc51_video_frame_skip;
	case V4L2_CID_MPEG_MFC51_VIDEO_FORCE_FRAME_TYPE:
		return mfc51_video_force_frame;
	}
	return NULL;
}

static int s5p_mfc_ctx_ready(struct s5p_mfc_ctx *ctx)
{
	mfc_debug(2, "src=%d, dst=%d, state=%d\n",
		  ctx->src_queue_cnt, ctx->dst_queue_cnt, ctx->state);
	/* context is ready to make header */
	if (ctx->state == MFCINST_GOT_INST && ctx->dst_queue_cnt >= 1)
		return 1;
	/* context is ready to encode a frame */
	if (ctx->state == MFCINST_RUNNING &&
		ctx->src_queue_cnt >= 1 && ctx->dst_queue_cnt >= 1)
		return 1;
	/* context is ready to encode remain frames */
	if (ctx->state == MFCINST_FINISHING &&
		ctx->src_queue_cnt >= 1 && ctx->dst_queue_cnt >= 1)
		return 1;
	mfc_debug(2, "ctx is not ready\n");
	return 0;
}

static void cleanup_ref_queue(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_buf *mb_entry;
	unsigned long mb_y_addr, mb_c_addr;

	/* move buffers in ref queue to src queue */
	while (!list_empty(&ctx->ref_queue)) {
		mb_entry = list_entry((&ctx->ref_queue)->next,
						struct s5p_mfc_buf, list);
		mb_y_addr = vb2_dma_contig_plane_dma_addr(mb_entry->b, 0);
		mb_c_addr = vb2_dma_contig_plane_dma_addr(mb_entry->b, 1);
		list_del(&mb_entry->list);
		ctx->ref_queue_cnt--;
		list_add_tail(&mb_entry->list, &ctx->src_queue);
		ctx->src_queue_cnt++;
	}
	mfc_debug(2, "enc src count: %d, enc ref count: %d\n",
		  ctx->src_queue_cnt, ctx->ref_queue_cnt);
	INIT_LIST_HEAD(&ctx->ref_queue);
	ctx->ref_queue_cnt = 0;
}

static int enc_pre_seq_start(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *dst_mb;
	unsigned long dst_addr;
	unsigned int dst_size;
	unsigned long flags;

	spin_lock_irqsave(&dev->irqlock, flags);
	dst_mb = list_entry(ctx->dst_queue.next, struct s5p_mfc_buf, list);
	dst_addr = vb2_dma_contig_plane_dma_addr(dst_mb->b, 0);
	dst_size = vb2_plane_size(dst_mb->b, 0);
	s5p_mfc_set_enc_stream_buffer(ctx, dst_addr, dst_size);
	spin_unlock_irqrestore(&dev->irqlock, flags);
	return 0;
}

static int enc_post_seq_start(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc_params *p = &ctx->enc_params;
	struct s5p_mfc_buf *dst_mb;
	unsigned long flags;

	if (p->seq_hdr_mode == V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE) {
		spin_lock_irqsave(&dev->irqlock, flags);
		dst_mb = list_entry(ctx->dst_queue.next,
				struct s5p_mfc_buf, list);
		list_del(&dst_mb->list);
		ctx->dst_queue_cnt--;
		vb2_set_plane_payload(dst_mb->b, 0,
						s5p_mfc_get_enc_strm_size());
		vb2_buffer_done(dst_mb->b, VB2_BUF_STATE_DONE);
		spin_unlock_irqrestore(&dev->irqlock, flags);
	}
	ctx->state = MFCINST_RUNNING;
	if (s5p_mfc_ctx_ready(ctx)) {
		spin_lock_irqsave(&dev->condlock, flags);
		set_bit(ctx->num, &dev->ctx_work_bits);
		spin_unlock_irqrestore(&dev->condlock, flags);
	}
	s5p_mfc_try_run(dev);
	return 0;
}

static int enc_pre_frame_start(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *dst_mb;
	struct s5p_mfc_buf *src_mb;
	unsigned long flags;
	unsigned long src_y_addr, src_c_addr, dst_addr;
	unsigned int dst_size;

	spin_lock_irqsave(&dev->irqlock, flags);
	src_mb = list_entry(ctx->src_queue.next, struct s5p_mfc_buf, list);
	src_y_addr = vb2_dma_contig_plane_dma_addr(src_mb->b, 0);
	src_c_addr = vb2_dma_contig_plane_dma_addr(src_mb->b, 1);
	s5p_mfc_set_enc_frame_buffer(ctx, src_y_addr, src_c_addr);
	spin_unlock_irqrestore(&dev->irqlock, flags);

	spin_lock_irqsave(&dev->irqlock, flags);
	dst_mb = list_entry(ctx->dst_queue.next, struct s5p_mfc_buf, list);
	dst_addr = vb2_dma_contig_plane_dma_addr(dst_mb->b, 0);
	dst_size = vb2_plane_size(dst_mb->b, 0);
	s5p_mfc_set_enc_stream_buffer(ctx, dst_addr, dst_size);
	spin_unlock_irqrestore(&dev->irqlock, flags);

	return 0;
}

static int enc_post_frame_start(struct s5p_mfc_ctx *ctx)
{
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_buf *mb_entry;
	unsigned long enc_y_addr, enc_c_addr;
	unsigned long mb_y_addr, mb_c_addr;
	int slice_type;
	unsigned int strm_size;
	unsigned long flags;

	slice_type = s5p_mfc_get_enc_slice_type();
	strm_size = s5p_mfc_get_enc_strm_size();
	mfc_debug(2, "Encoded slice type: %d", slice_type);
	mfc_debug(2, "Encoded stream size: %d", strm_size);
	mfc_debug(2, "Display order: %d",
		  mfc_read(dev, S5P_FIMV_ENC_SI_PIC_CNT));
	spin_lock_irqsave(&dev->irqlock, flags);
	if (slice_type >= 0) {
		s5p_mfc_get_enc_frame_buffer(ctx, &enc_y_addr, &enc_c_addr);
		list_for_each_entry(mb_entry, &ctx->src_queue, list) {
			mb_y_addr = vb2_dma_contig_plane_dma_addr(mb_entry->b, 0);
			mb_c_addr = vb2_dma_contig_plane_dma_addr(mb_entry->b, 1);
			if ((enc_y_addr == mb_y_addr) &&
						(enc_c_addr == mb_c_addr)) {
				list_del(&mb_entry->list);
				ctx->src_queue_cnt--;
				vb2_buffer_done(mb_entry->b,
							VB2_BUF_STATE_DONE);
				break;
			}
		}
		list_for_each_entry(mb_entry, &ctx->ref_queue, list) {
			mb_y_addr = vb2_dma_contig_plane_dma_addr(mb_entry->b, 0);
			mb_c_addr = vb2_dma_contig_plane_dma_addr(mb_entry->b, 1);
			if ((enc_y_addr == mb_y_addr) &&
						(enc_c_addr == mb_c_addr)) {
				list_del(&mb_entry->list);
				ctx->ref_queue_cnt--;
				vb2_buffer_done(mb_entry->b,
							VB2_BUF_STATE_DONE);
				break;
			}
		}
	}
	if ((ctx->src_queue_cnt > 0) && (ctx->state == MFCINST_RUNNING)) {
		mb_entry = list_entry(ctx->src_queue.next, struct s5p_mfc_buf,
									list);
		if (mb_entry->used) {
			list_del(&mb_entry->list);
			ctx->src_queue_cnt--;
			list_add_tail(&mb_entry->list, &ctx->ref_queue);
			ctx->ref_queue_cnt++;
		}
		mfc_debug(2, "enc src count: %d, enc ref count: %d\n",
			  ctx->src_queue_cnt, ctx->ref_queue_cnt);
	}
	if (strm_size > 0) {
		/* at least one more dest. buffers exist always  */
		mb_entry = list_entry(ctx->dst_queue.next, struct s5p_mfc_buf,
									list);
		list_del(&mb_entry->list);
		ctx->dst_queue_cnt--;
		switch (slice_type) {
		case S5P_FIMV_ENC_SI_SLICE_TYPE_I:
			mb_entry->b->v4l2_buf.flags |= V4L2_BUF_FLAG_KEYFRAME;
			break;
		case S5P_FIMV_ENC_SI_SLICE_TYPE_P:
			mb_entry->b->v4l2_buf.flags |= V4L2_BUF_FLAG_PFRAME;
			break;
		case S5P_FIMV_ENC_SI_SLICE_TYPE_B:
			mb_entry->b->v4l2_buf.flags |= V4L2_BUF_FLAG_BFRAME;
			break;
		}
		vb2_set_plane_payload(mb_entry->b, 0, strm_size);
		vb2_buffer_done(mb_entry->b, VB2_BUF_STATE_DONE);
	}
	spin_unlock_irqrestore(&dev->irqlock, flags);
	if ((ctx->src_queue_cnt == 0) || (ctx->dst_queue_cnt == 0)) {
		spin_lock(&dev->condlock);
		clear_bit(ctx->num, &dev->ctx_work_bits);
		spin_unlock(&dev->condlock);
	}
	return 0;
}

static struct s5p_mfc_codec_ops encoder_codec_ops = {
	.pre_seq_start		= enc_pre_seq_start,
	.post_seq_start		= enc_post_seq_start,
	.pre_frame_start	= enc_pre_frame_start,
	.post_frame_start	= enc_post_frame_start,
};

/* Query capabilities of the device */
static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct s5p_mfc_dev *dev = video_drvdata(file);

	strncpy(cap->driver, dev->plat_dev->name, sizeof(cap->driver) - 1);
	strncpy(cap->card, dev->plat_dev->name, sizeof(cap->card) - 1);
	cap->bus_info[0] = 0;
	cap->version = KERNEL_VERSION(1, 0, 0);
	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE_MPLANE
			  | V4L2_CAP_VIDEO_OUTPUT_MPLANE
			  | V4L2_CAP_STREAMING;
	return 0;
}

static int vidioc_enum_fmt(struct v4l2_fmtdesc *f, bool mplane, bool out)
{
	struct s5p_mfc_fmt *fmt;
	int i, j = 0;

	for (i = 0; i < ARRAY_SIZE(formats); ++i) {
		if (mplane && formats[i].num_planes == 1)
			continue;
		else if (!mplane && formats[i].num_planes > 1)
			continue;
		if (out && formats[i].type != MFC_FMT_RAW)
			continue;
		else if (!out && formats[i].type != MFC_FMT_ENC)
			continue;
		if (j == f->index) {
			fmt = &formats[i];
			strlcpy(f->description, fmt->name,
				sizeof(f->description));
			f->pixelformat = fmt->fourcc;
			return 0;
		}
		++j;
	}
	return -EINVAL;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *pirv,
				   struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(f, false, false);
}

static int vidioc_enum_fmt_vid_cap_mplane(struct file *file, void *pirv,
					  struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(f, true, false);
}

static int vidioc_enum_fmt_vid_out(struct file *file, void *prov,
				   struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(f, false, true);
}

static int vidioc_enum_fmt_vid_out_mplane(struct file *file, void *prov,
					  struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(f, true, true);
}

static int vidioc_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;

	mfc_debug(2, "f->type = %d ctx->state = %d\n", f->type, ctx->state);
	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		/* This is run on output (encoder dest) */
		pix_fmt_mp->width = 0;
		pix_fmt_mp->height = 0;
		pix_fmt_mp->field = V4L2_FIELD_NONE;
		pix_fmt_mp->pixelformat = ctx->dst_fmt->fourcc;
		pix_fmt_mp->num_planes = ctx->dst_fmt->num_planes;

		pix_fmt_mp->plane_fmt[0].bytesperline = ctx->enc_dst_buf_size;
		pix_fmt_mp->plane_fmt[0].sizeimage = ctx->enc_dst_buf_size;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		/* This is run on capture (encoder src) */
		pix_fmt_mp->width = ctx->img_width;
		pix_fmt_mp->height = ctx->img_height;

		pix_fmt_mp->field = V4L2_FIELD_NONE;
		pix_fmt_mp->pixelformat = ctx->src_fmt->fourcc;
		pix_fmt_mp->num_planes = ctx->src_fmt->num_planes;

		pix_fmt_mp->plane_fmt[0].bytesperline = ctx->buf_width;
		pix_fmt_mp->plane_fmt[0].sizeimage = ctx->luma_size;
		pix_fmt_mp->plane_fmt[1].bytesperline = ctx->buf_width;
		pix_fmt_mp->plane_fmt[1].sizeimage = ctx->chroma_size;
	} else {
		mfc_err("invalid buf type\n");
		return -EINVAL;
	}
	return 0;
}

static int vidioc_try_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct s5p_mfc_fmt *fmt;
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		fmt = find_format(f, MFC_FMT_ENC);
		if (!fmt) {
			mfc_err("failed to try output format\n");
			return -EINVAL;
		}

		if (pix_fmt_mp->plane_fmt[0].sizeimage == 0) {
			mfc_err("must be set encoding output size\n");
			return -EINVAL;
		}

		pix_fmt_mp->plane_fmt[0].bytesperline =
			pix_fmt_mp->plane_fmt[0].sizeimage;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		fmt = find_format(f, MFC_FMT_RAW);
		if (!fmt) {
			mfc_err("failed to try output format\n");
			return -EINVAL;
		}

		if (fmt->num_planes != pix_fmt_mp->num_planes) {
			mfc_err("failed to try output format\n");
			return -EINVAL;
		}
		v4l_bound_align_image(&pix_fmt_mp->width, 8, 1920, 1,
			&pix_fmt_mp->height, 4, 1080, 1, 0);
	} else {
		mfc_err("invalid buf type\n");
		return -EINVAL;
	}
	return 0;
}

static int vidioc_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct s5p_mfc_dev *dev = video_drvdata(file);
	struct s5p_mfc_ctx *ctx = fh_to_ctx(priv);
	struct s5p_mfc_fmt *fmt;
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;
	unsigned long flags;
	int ret = 0;

	ret = vidioc_try_fmt(file, priv, f);
	if (ret)
		return ret;
	if (ctx->vq_src.streaming || ctx->vq_dst.streaming) {
		v4l2_err(&dev->v4l2_dev, "%s queue busy\n", __func__);
		ret = -EBUSY;
		goto out;
	}
	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		fmt = find_format(f, MFC_FMT_ENC);
		if (!fmt) {
			mfc_err("failed to set capture format\n");
			return -EINVAL;
		}
		ctx->state = MFCINST_INIT;
		ctx->dst_fmt = fmt;
		ctx->codec_mode = ctx->dst_fmt->codec_mode;
		ctx->enc_dst_buf_size =	pix_fmt_mp->plane_fmt[0].sizeimage;
		pix_fmt_mp->plane_fmt[0].bytesperline = 0;
		ctx->dst_bufs_cnt = 0;
		ctx->capture_state = QUEUE_FREE;
		s5p_mfc_alloc_instance_buffer(ctx);
		spin_lock_irqsave(&dev->condlock, flags);
		set_bit(ctx->num, &dev->ctx_work_bits);
		spin_unlock_irqrestore(&dev->condlock, flags);
		s5p_mfc_clean_ctx_int_flags(ctx);
		s5p_mfc_try_run(dev);
		if (s5p_mfc_wait_for_done_ctx(ctx, \
				S5P_FIMV_R2H_CMD_OPEN_INSTANCE_RET, 1)) {
				/* Error or timeout */
			mfc_err("Error getting instance from hardware\n");
			s5p_mfc_release_instance_buffer(ctx);
			ret = -EIO;
			goto out;
		}
		mfc_debug(2, "Got instance number: %d\n", ctx->inst_no);
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		fmt = find_format(f, MFC_FMT_RAW);
		if (!fmt) {
			mfc_err("failed to set output format\n");
			return -EINVAL;
		}
		if (fmt->num_planes != pix_fmt_mp->num_planes) {
			mfc_err("failed to set output format\n");
			ret = -EINVAL;
			goto out;
		}
		ctx->src_fmt = fmt;
		ctx->img_width = pix_fmt_mp->width;
		ctx->img_height = pix_fmt_mp->height;
		mfc_debug(2, "codec number: %d\n", ctx->src_fmt->codec_mode);
		mfc_debug(2, "fmt - w: %d, h: %d, ctx - w: %d, h: %d\n",
			pix_fmt_mp->width, pix_fmt_mp->height,
			ctx->img_width, ctx->img_height);
		if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12M) {
			ctx->buf_width = ALIGN(ctx->img_width,
							S5P_FIMV_NV12M_HALIGN);
			ctx->luma_size = ALIGN(ctx->img_width,
				S5P_FIMV_NV12M_HALIGN) * ALIGN(ctx->img_height,
				S5P_FIMV_NV12M_LVALIGN);
			ctx->chroma_size = ALIGN(ctx->img_width,
				S5P_FIMV_NV12M_HALIGN) * ALIGN((ctx->img_height
				>> 1), S5P_FIMV_NV12M_CVALIGN);

			ctx->luma_size = ALIGN(ctx->luma_size,
							S5P_FIMV_NV12M_SALIGN);
			ctx->chroma_size = ALIGN(ctx->chroma_size,
							S5P_FIMV_NV12M_SALIGN);

			pix_fmt_mp->plane_fmt[0].sizeimage = ctx->luma_size;
			pix_fmt_mp->plane_fmt[0].bytesperline = ctx->buf_width;
			pix_fmt_mp->plane_fmt[1].sizeimage = ctx->chroma_size;
			pix_fmt_mp->plane_fmt[1].bytesperline = ctx->buf_width;

		} else if (ctx->src_fmt->fourcc == V4L2_PIX_FMT_NV12MT) {
			ctx->buf_width = ALIGN(ctx->img_width,
							S5P_FIMV_NV12MT_HALIGN);
			ctx->luma_size = ALIGN(ctx->img_width,
				S5P_FIMV_NV12MT_HALIGN)	* ALIGN(ctx->img_height,
				S5P_FIMV_NV12MT_VALIGN);
			ctx->chroma_size = ALIGN(ctx->img_width,
				S5P_FIMV_NV12MT_HALIGN) * ALIGN((ctx->img_height
				>> 1), S5P_FIMV_NV12MT_VALIGN);
			ctx->luma_size = ALIGN(ctx->luma_size,
							S5P_FIMV_NV12MT_SALIGN);
			ctx->chroma_size = ALIGN(ctx->chroma_size,
							S5P_FIMV_NV12MT_SALIGN);

			pix_fmt_mp->plane_fmt[0].sizeimage = ctx->luma_size;
			pix_fmt_mp->plane_fmt[0].bytesperline = ctx->buf_width;
			pix_fmt_mp->plane_fmt[1].sizeimage = ctx->chroma_size;
			pix_fmt_mp->plane_fmt[1].bytesperline = ctx->buf_width;
		}
		ctx->src_bufs_cnt = 0;
		ctx->output_state = QUEUE_FREE;
	} else {
		mfc_err("invalid buf type\n");
		return -EINVAL;
	}
out:
	mfc_debug_leave();
	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
					  struct v4l2_requestbuffers *reqbufs)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(priv);
	int ret = 0;

	/* if memory is not mmp or userptr return error */
	if ((reqbufs->memory != V4L2_MEMORY_MMAP) &&
		(reqbufs->memory != V4L2_MEMORY_USERPTR))
		return -EINVAL;
	if (reqbufs->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		if (ctx->capture_state != QUEUE_FREE) {
			mfc_err("invalid capture state: %d\n",
							ctx->capture_state);
			return -EINVAL;
		}
		ret = vb2_reqbufs(&ctx->vq_dst, reqbufs);
		if (ret != 0) {
			mfc_err("error in vb2_reqbufs() for E(D)\n");
			return ret;
		}
		ctx->capture_state = QUEUE_BUFS_REQUESTED;
		ret = s5p_mfc_alloc_codec_buffers(ctx);
		if (ret) {
			mfc_err("Failed to allocate encoding buffers\n");
			reqbufs->count = 0;
			ret = vb2_reqbufs(&ctx->vq_dst, reqbufs);
			return -ENOMEM;
		}
	} else if (reqbufs->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (ctx->output_state != QUEUE_FREE) {
			mfc_err("invalid output state: %d\n",
							ctx->output_state);
			return -EINVAL;
		}
		ret = vb2_reqbufs(&ctx->vq_src, reqbufs);
		if (ret != 0) {
			mfc_err("error in vb2_reqbufs() for E(S)\n");
			return ret;
		}
		ctx->output_state = QUEUE_BUFS_REQUESTED;
	} else {
		mfc_err("invalid buf type\n");
		return -EINVAL;
	}
	return ret;
}

static int vidioc_querybuf(struct file *file, void *priv,
						   struct v4l2_buffer *buf)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(priv);
	int ret = 0;

	/* if memory is not mmp or userptr return error */
	if ((buf->memory != V4L2_MEMORY_MMAP) &&
		(buf->memory != V4L2_MEMORY_USERPTR))
		return -EINVAL;
	if (buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		if (ctx->state != MFCINST_GOT_INST) {
			mfc_err("invalid context state: %d\n", ctx->state);
			return -EINVAL;
		}
		ret = vb2_querybuf(&ctx->vq_dst, buf);
		if (ret != 0) {
			mfc_err("error in vb2_querybuf() for E(D)\n");
			return ret;
		}
		buf->m.planes[0].m.mem_offset += DST_QUEUE_OFF_BASE;
	} else if (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = vb2_querybuf(&ctx->vq_src, buf);
		if (ret != 0) {
			mfc_err("error in vb2_querybuf() for E(S)\n");
			return ret;
		}
	} else {
		mfc_err("invalid buf type\n");
		return -EINVAL;
	}
	return ret;
}

/* Queue a buffer */
static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(priv);

	if (ctx->state == MFCINST_ERROR) {
		mfc_err("Call on QBUF after unrecoverable error\n");
		return -EIO;
	}
	if (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return vb2_qbuf(&ctx->vq_src, buf);
	else if (buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return vb2_qbuf(&ctx->vq_dst, buf);
	return -EINVAL;
}

/* Dequeue a buffer */
static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(priv);

	if (ctx->state == MFCINST_ERROR) {
		mfc_err("Call on DQBUF after unrecoverable error\n");
		return -EIO;
	}
	if (buf->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return vb2_dqbuf(&ctx->vq_src, buf, file->f_flags & O_NONBLOCK);
	else if (buf->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return vb2_dqbuf(&ctx->vq_dst, buf, file->f_flags & O_NONBLOCK);
	return -EINVAL;
}

/* Stream on */
static int vidioc_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type type)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(priv);

	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return vb2_streamon(&ctx->vq_src, type);
	else if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return vb2_streamon(&ctx->vq_dst, type);
	return -EINVAL;
}

/* Stream off, which equals to a pause */
static int vidioc_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(priv);

	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return vb2_streamoff(&ctx->vq_src, type);
	else if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return vb2_streamoff(&ctx->vq_dst, type);
	return -EINVAL;
}

static inline int h264_level(enum v4l2_mpeg_video_h264_level lvl)
{
	static unsigned int t[V4L2_MPEG_VIDEO_H264_LEVEL_4_0 + 1] = {
		/* V4L2_MPEG_VIDEO_H264_LEVEL_1_0   */ 10,
		/* V4L2_MPEG_VIDEO_H264_LEVEL_1B    */ 9,
		/* V4L2_MPEG_VIDEO_H264_LEVEL_1_1   */ 11,
		/* V4L2_MPEG_VIDEO_H264_LEVEL_1_2   */ 12,
		/* V4L2_MPEG_VIDEO_H264_LEVEL_1_3   */ 13,
		/* V4L2_MPEG_VIDEO_H264_LEVEL_2_0   */ 20,
		/* V4L2_MPEG_VIDEO_H264_LEVEL_2_1   */ 21,
		/* V4L2_MPEG_VIDEO_H264_LEVEL_2_2   */ 22,
		/* V4L2_MPEG_VIDEO_H264_LEVEL_3_0   */ 30,
		/* V4L2_MPEG_VIDEO_H264_LEVEL_3_1   */ 31,
		/* V4L2_MPEG_VIDEO_H264_LEVEL_3_2   */ 32,
		/* V4L2_MPEG_VIDEO_H264_LEVEL_4_0   */ 40,
	};
	return t[lvl];
}

static inline int mpeg4_level(enum v4l2_mpeg_video_mpeg4_level lvl)
{
	static unsigned int t[V4L2_MPEG_VIDEO_MPEG4_LEVEL_5 + 1] = {
		/* V4L2_MPEG_VIDEO_MPEG4_LEVEL_0    */ 0,
		/* V4L2_MPEG_VIDEO_MPEG4_LEVEL_0B   */ 9,
		/* V4L2_MPEG_VIDEO_MPEG4_LEVEL_1    */ 1,
		/* V4L2_MPEG_VIDEO_MPEG4_LEVEL_2    */ 2,
		/* V4L2_MPEG_VIDEO_MPEG4_LEVEL_3    */ 3,
		/* V4L2_MPEG_VIDEO_MPEG4_LEVEL_3B   */ 7,
		/* V4L2_MPEG_VIDEO_MPEG4_LEVEL_4    */ 4,
		/* V4L2_MPEG_VIDEO_MPEG4_LEVEL_5    */ 5,
	};
	return t[lvl];
}

static inline int vui_sar_idc(enum v4l2_mpeg_video_h264_vui_sar_idc sar)
{
	static unsigned int t[V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_EXTENDED + 1] = {
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_UNSPECIFIED     */ 0,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_1x1             */ 1,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_12x11           */ 2,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_10x11           */ 3,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_16x11           */ 4,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_40x33           */ 5,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_24x11           */ 6,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_20x11           */ 7,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_32x11           */ 8,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_80x33           */ 9,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_18x11           */ 10,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_15x11           */ 11,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_64x33           */ 12,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_160x99          */ 13,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_4x3             */ 14,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_3x2             */ 15,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_2x1             */ 16,
		/* V4L2_MPEG_VIDEO_H264_VUI_SAR_IDC_EXTENDED        */ 255,
	};
	return t[sar];
}

static int s5p_mfc_enc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct s5p_mfc_ctx *ctx = ctrl_to_ctx(ctrl);
	struct s5p_mfc_dev *dev = ctx->dev;
	struct s5p_mfc_enc_params *p = &ctx->enc_params;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		p->gop_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:
		p->slice_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB:
		p->slice_mb = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_BYTES:
		p->slice_bit = ctrl->val * 8;
		break;
	case V4L2_CID_MPEG_VIDEO_CYCLIC_INTRA_REFRESH_MB:
		p->intra_refresh_mb = ctrl->val;
		break;
	case V4L2_CID_MPEG_MFC51_VIDEO_PADDING:
		p->pad = ctrl->val;
		break;
	case V4L2_CID_MPEG_MFC51_VIDEO_PADDING_YUV:
		p->pad_luma = (ctrl->val >> 16) & 0xff;
		p->pad_cb = (ctrl->val >> 8) & 0xff;
		p->pad_cr = (ctrl->val >> 0) & 0xff;
		break;
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
		p->rc_frame = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		p->rc_bitrate = ctrl->val;
		break;
	case V4L2_CID_MPEG_MFC51_VIDEO_RC_REACTION_COEFF:
		p->rc_reaction_coeff = ctrl->val;
		break;
	case V4L2_CID_MPEG_MFC51_VIDEO_FORCE_FRAME_TYPE:
		ctx->force_frame_type = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_VBV_SIZE:
		p->vbv_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE:
		p->codec.h264.cpb_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
		p->seq_hdr_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE:
		p->frame_skip_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_MFC51_VIDEO_RC_FIXED_TARGET_BIT:
		p->fixed_target_bit = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
		p->num_b_frame = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_H264_PROFILE_MAIN:
			p->codec.h264.profile =
					S5P_FIMV_ENC_PROFILE_H264_MAIN;
			break;
		case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH:
			p->codec.h264.profile =
					S5P_FIMV_ENC_PROFILE_H264_HIGH;
			break;
		case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE:
			p->codec.h264.profile =
				S5P_FIMV_ENC_PROFILE_H264_BASELINE;
			break;
		default:
			ret = -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		p->codec.h264.level_v4l2 = ctrl->val;
		p->codec.h264.level = h264_level(ctrl->val);
		if (p->codec.h264.level < 0) {
			mfc_err("Level number is wrong\n");
			ret = p->codec.h264.level;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_LEVEL:
		p->codec.mpeg4.level_v4l2 = ctrl->val;
		p->codec.mpeg4.level = mpeg4_level(ctrl->val);
		if (p->codec.mpeg4.level < 0) {
			mfc_err("Level number is wrong\n");
			ret = p->codec.mpeg4.level;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:
		p->codec.h264.loop_filter_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA:
		p->codec.h264.loop_filter_alpha = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA:
		p->codec.h264.loop_filter_beta = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
		p->codec.h264.entropy_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_MFC51_VIDEO_H264_NUM_REF_PIC_FOR_P:
		p->codec.h264.num_ref_pic_4p = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM:
		p->codec.h264._8x8_transform = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:
		p->codec.h264.rc_mb = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:
		p->codec.h264.rc_frame_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
		p->codec.h264.rc_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
		p->codec.h264.rc_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP:
		p->codec.h264.rc_p_frame_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP:
		p->codec.h264.rc_b_frame_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_I_FRAME_QP:
	case V4L2_CID_MPEG_VIDEO_H263_I_FRAME_QP:
		p->codec.mpeg4.rc_frame_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_MIN_QP:
	case V4L2_CID_MPEG_VIDEO_H263_MIN_QP:
		p->codec.mpeg4.rc_min_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_MAX_QP:
	case V4L2_CID_MPEG_VIDEO_H263_MAX_QP:
		p->codec.mpeg4.rc_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_P_FRAME_QP:
	case V4L2_CID_MPEG_VIDEO_H263_P_FRAME_QP:
		p->codec.mpeg4.rc_p_frame_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_B_FRAME_QP:
	case V4L2_CID_MPEG_VIDEO_H263_B_FRAME_QP:
		p->codec.mpeg4.rc_b_frame_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_DARK:
		p->codec.h264.rc_mb_dark = ctrl->val;
		break;
	case V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_SMOOTH:
		p->codec.h264.rc_mb_smooth = ctrl->val;
		break;
	case V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_STATIC:
		p->codec.h264.rc_mb_static = ctrl->val;
		break;
	case V4L2_CID_MPEG_MFC51_VIDEO_H264_ADAPTIVE_RC_ACTIVITY:
		p->codec.h264.rc_mb_activity = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_ENABLE:
		p->codec.h264.vui_sar = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_VUI_SAR_IDC:
		p->codec.h264.vui_sar_idc = vui_sar_idc(ctrl->val);
		break;
	case V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_WIDTH:
		p->codec.h264.vui_ext_sar_width = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_VUI_EXT_SAR_HEIGHT:
		p->codec.h264.vui_ext_sar_height = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_CLOSURE:
		p->codec.h264.open_gop = !ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_PERIOD:
		p->codec.h264.open_gop_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_PROFILE:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_MPEG4_PROFILE_SIMPLE:
			p->codec.mpeg4.profile =
				S5P_FIMV_ENC_PROFILE_MPEG4_SIMPLE;
			break;
		case V4L2_MPEG_VIDEO_MPEG4_PROFILE_ADVANCED_SIMPLE:
			p->codec.mpeg4.profile =
			S5P_FIMV_ENC_PROFILE_MPEG4_ADVANCED_SIMPLE;
			break;
		default:
			ret = -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_MPEG4_QPEL:
		p->codec.mpeg4.quarter_pixel = ctrl->val;
		break;
	default:
		v4l2_err(&dev->v4l2_dev, "Invalid control, id=%d, val=%d\n",
							ctrl->id, ctrl->val);
		ret = -EINVAL;
	}
	return ret;
}

static const struct v4l2_ctrl_ops s5p_mfc_enc_ctrl_ops = {
	.s_ctrl = s5p_mfc_enc_s_ctrl,
};

static int vidioc_s_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *a)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(priv);

	if (a->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ctx->enc_params.rc_framerate_num =
					a->parm.output.timeperframe.denominator;
		ctx->enc_params.rc_framerate_denom =
					a->parm.output.timeperframe.numerator;
	} else {
		mfc_err("Setting FPS is only possible for the output queue\n");
		return -EINVAL;
	}
	return 0;
}

static int vidioc_g_parm(struct file *file, void *priv,
			 struct v4l2_streamparm *a)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(priv);

	if (a->type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
		a->parm.output.timeperframe.denominator =
					ctx->enc_params.rc_framerate_num;
		a->parm.output.timeperframe.numerator =
					ctx->enc_params.rc_framerate_denom;
	} else {
		mfc_err("Setting FPS is only possible for the output queue\n");
		return -EINVAL;
	}
	return 0;
}

static const struct v4l2_ioctl_ops s5p_mfc_enc_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_cap_mplane = vidioc_enum_fmt_vid_cap_mplane,
	.vidioc_enum_fmt_vid_out = vidioc_enum_fmt_vid_out,
	.vidioc_enum_fmt_vid_out_mplane = vidioc_enum_fmt_vid_out_mplane,
	.vidioc_g_fmt_vid_cap_mplane = vidioc_g_fmt,
	.vidioc_g_fmt_vid_out_mplane = vidioc_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane = vidioc_try_fmt,
	.vidioc_try_fmt_vid_out_mplane = vidioc_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane = vidioc_s_fmt,
	.vidioc_s_fmt_vid_out_mplane = vidioc_s_fmt,
	.vidioc_reqbufs = vidioc_reqbufs,
	.vidioc_querybuf = vidioc_querybuf,
	.vidioc_qbuf = vidioc_qbuf,
	.vidioc_dqbuf = vidioc_dqbuf,
	.vidioc_streamon = vidioc_streamon,
	.vidioc_streamoff = vidioc_streamoff,
	.vidioc_s_parm = vidioc_s_parm,
	.vidioc_g_parm = vidioc_g_parm,
};

static int check_vb_with_fmt(struct s5p_mfc_fmt *fmt, struct vb2_buffer *vb)
{
	int i;

	if (!fmt)
		return -EINVAL;
	if (fmt->num_planes != vb->num_planes) {
		mfc_err("invalid plane number for the format\n");
		return -EINVAL;
	}
	for (i = 0; i < fmt->num_planes; i++) {
		if (!vb2_dma_contig_plane_dma_addr(vb, i)) {
			mfc_err("failed to get plane cookie\n");
			return -EINVAL;
		}
		mfc_debug(2, "index: %d, plane[%d] cookie: 0x%08zx",
				vb->v4l2_buf.index, i,
				vb2_dma_contig_plane_dma_addr(vb, i));
	}
	return 0;
}

static int s5p_mfc_queue_setup(struct vb2_queue *vq,
			const struct v4l2_format *fmt,
			unsigned int *buf_count, unsigned int *plane_count,
			unsigned int psize[], void *allocators[])
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(vq->drv_priv);

	if (ctx->state != MFCINST_GOT_INST) {
		mfc_err("inavlid state: %d\n", ctx->state);
		return -EINVAL;
	}
	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		if (ctx->dst_fmt)
			*plane_count = ctx->dst_fmt->num_planes;
		else
			*plane_count = MFC_ENC_CAP_PLANE_COUNT;
		if (*buf_count < 1)
			*buf_count = 1;
		if (*buf_count > MFC_MAX_BUFFERS)
			*buf_count = MFC_MAX_BUFFERS;
		psize[0] = ctx->enc_dst_buf_size;
		allocators[0] = ctx->dev->alloc_ctx[MFC_BANK1_ALLOC_CTX];
	} else if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		if (ctx->src_fmt)
			*plane_count = ctx->src_fmt->num_planes;
		else
			*plane_count = MFC_ENC_OUT_PLANE_COUNT;

		if (*buf_count < 1)
			*buf_count = 1;
		if (*buf_count > MFC_MAX_BUFFERS)
			*buf_count = MFC_MAX_BUFFERS;
		psize[0] = ctx->luma_size;
		psize[1] = ctx->chroma_size;
		allocators[0] = ctx->dev->alloc_ctx[MFC_BANK2_ALLOC_CTX];
		allocators[1] = ctx->dev->alloc_ctx[MFC_BANK2_ALLOC_CTX];
	} else {
		mfc_err("inavlid queue type: %d\n", vq->type);
		return -EINVAL;
	}
	return 0;
}

static void s5p_mfc_unlock(struct vb2_queue *q)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(q->drv_priv);
	struct s5p_mfc_dev *dev = ctx->dev;

	mutex_unlock(&dev->mfc_mutex);
}

static void s5p_mfc_lock(struct vb2_queue *q)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(q->drv_priv);
	struct s5p_mfc_dev *dev = ctx->dev;

	mutex_lock(&dev->mfc_mutex);
}

static int s5p_mfc_buf_init(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct s5p_mfc_ctx *ctx = fh_to_ctx(vq->drv_priv);
	unsigned int i;
	int ret;

	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		ret = check_vb_with_fmt(ctx->dst_fmt, vb);
		if (ret < 0)
			return ret;
		i = vb->v4l2_buf.index;
		ctx->dst_bufs[i].b = vb;
		ctx->dst_bufs[i].cookie.stream =
					vb2_dma_contig_plane_dma_addr(vb, 0);
		ctx->dst_bufs_cnt++;
	} else if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = check_vb_with_fmt(ctx->src_fmt, vb);
		if (ret < 0)
			return ret;
		i = vb->v4l2_buf.index;
		ctx->src_bufs[i].b = vb;
		ctx->src_bufs[i].cookie.raw.luma =
					vb2_dma_contig_plane_dma_addr(vb, 0);
		ctx->src_bufs[i].cookie.raw.chroma =
					vb2_dma_contig_plane_dma_addr(vb, 1);
		ctx->src_bufs_cnt++;
	} else {
		mfc_err("inavlid queue type: %d\n", vq->type);
		return -EINVAL;
	}
	return 0;
}

static int s5p_mfc_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct s5p_mfc_ctx *ctx = fh_to_ctx(vq->drv_priv);
	int ret;

	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		ret = check_vb_with_fmt(ctx->dst_fmt, vb);
		if (ret < 0)
			return ret;
		mfc_debug(2, "plane size: %ld, dst size: %d\n",
			vb2_plane_size(vb, 0), ctx->enc_dst_buf_size);
		if (vb2_plane_size(vb, 0) < ctx->enc_dst_buf_size) {
			mfc_err("plane size is too small for capture\n");
			return -EINVAL;
		}
	} else if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = check_vb_with_fmt(ctx->src_fmt, vb);
		if (ret < 0)
			return ret;
		mfc_debug(2, "plane size: %ld, luma size: %d\n",
			vb2_plane_size(vb, 0), ctx->luma_size);
		mfc_debug(2, "plane size: %ld, chroma size: %d\n",
			vb2_plane_size(vb, 1), ctx->chroma_size);
		if (vb2_plane_size(vb, 0) < ctx->luma_size ||
		    vb2_plane_size(vb, 1) < ctx->chroma_size) {
			mfc_err("plane size is too small for output\n");
			return -EINVAL;
		}
	} else {
		mfc_err("inavlid queue type: %d\n", vq->type);
		return -EINVAL;
	}
	return 0;
}

static int s5p_mfc_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct s5p_mfc_ctx *ctx = fh_to_ctx(q->drv_priv);
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned long flags;

	v4l2_ctrl_handler_setup(&ctx->ctrl_handler);
	/* If context is ready then dev = work->data;schedule it to run */
	if (s5p_mfc_ctx_ready(ctx)) {
		spin_lock_irqsave(&dev->condlock, flags);
		set_bit(ctx->num, &dev->ctx_work_bits);
		spin_unlock_irqrestore(&dev->condlock, flags);
	}
	s5p_mfc_try_run(dev);
	return 0;
}

static int s5p_mfc_stop_streaming(struct vb2_queue *q)
{
	unsigned long flags;
	struct s5p_mfc_ctx *ctx = fh_to_ctx(q->drv_priv);
	struct s5p_mfc_dev *dev = ctx->dev;

	if ((ctx->state == MFCINST_FINISHING ||
		ctx->state == MFCINST_RUNNING) &&
		dev->curr_ctx == ctx->num && dev->hw_lock) {
		ctx->state = MFCINST_ABORT;
		s5p_mfc_wait_for_done_ctx(ctx, S5P_FIMV_R2H_CMD_FRAME_DONE_RET,
					  0);
	}
	ctx->state = MFCINST_FINISHED;
	spin_lock_irqsave(&dev->irqlock, flags);
	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		s5p_mfc_cleanup_queue(&ctx->dst_queue, &ctx->vq_dst);
		INIT_LIST_HEAD(&ctx->dst_queue);
		ctx->dst_queue_cnt = 0;
	}
	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		cleanup_ref_queue(ctx);
		s5p_mfc_cleanup_queue(&ctx->src_queue, &ctx->vq_src);
		INIT_LIST_HEAD(&ctx->src_queue);
		ctx->src_queue_cnt = 0;
	}
	spin_unlock_irqrestore(&dev->irqlock, flags);
	return 0;
}

static void s5p_mfc_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct s5p_mfc_ctx *ctx = fh_to_ctx(vq->drv_priv);
	struct s5p_mfc_dev *dev = ctx->dev;
	unsigned long flags;
	struct s5p_mfc_buf *mfc_buf;

	if (ctx->state == MFCINST_ERROR) {
		vb2_buffer_done(vb, VB2_BUF_STATE_ERROR);
		cleanup_ref_queue(ctx);
		return;
	}
	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		mfc_buf = &ctx->dst_bufs[vb->v4l2_buf.index];
		mfc_buf->used = 0;
		/* Mark destination as available for use by MFC */
		spin_lock_irqsave(&dev->irqlock, flags);
		list_add_tail(&mfc_buf->list, &ctx->dst_queue);
		ctx->dst_queue_cnt++;
		spin_unlock_irqrestore(&dev->irqlock, flags);
	} else if (vq->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		mfc_buf = &ctx->src_bufs[vb->v4l2_buf.index];
		mfc_buf->used = 0;
		spin_lock_irqsave(&dev->irqlock, flags);
		if (vb->v4l2_planes[0].bytesused == 0) {
			mfc_debug(1, "change state to FINISHING\n");
			ctx->state = MFCINST_FINISHING;
			vb2_buffer_done(vb, VB2_BUF_STATE_DONE);
			cleanup_ref_queue(ctx);
		} else {
			list_add_tail(&mfc_buf->list, &ctx->src_queue);
			ctx->src_queue_cnt++;
		}
		spin_unlock_irqrestore(&dev->irqlock, flags);
	} else {
		mfc_err("unsupported buffer type (%d)\n", vq->type);
	}
	if (s5p_mfc_ctx_ready(ctx)) {
		spin_lock_irqsave(&dev->condlock, flags);
		set_bit(ctx->num, &dev->ctx_work_bits);
		spin_unlock_irqrestore(&dev->condlock, flags);
	}
	s5p_mfc_try_run(dev);
}

static struct vb2_ops s5p_mfc_enc_qops = {
	.queue_setup		= s5p_mfc_queue_setup,
	.wait_prepare		= s5p_mfc_unlock,
	.wait_finish		= s5p_mfc_lock,
	.buf_init		= s5p_mfc_buf_init,
	.buf_prepare		= s5p_mfc_buf_prepare,
	.start_streaming	= s5p_mfc_start_streaming,
	.stop_streaming		= s5p_mfc_stop_streaming,
	.buf_queue		= s5p_mfc_buf_queue,
};

struct s5p_mfc_codec_ops *get_enc_codec_ops(void)
{
	return &encoder_codec_ops;
}

struct vb2_ops *get_enc_queue_ops(void)
{
	return &s5p_mfc_enc_qops;
}

const struct v4l2_ioctl_ops *get_enc_v4l2_ioctl_ops(void)
{
	return &s5p_mfc_enc_ioctl_ops;
}

#define IS_MFC51_PRIV(x) ((V4L2_CTRL_ID2CLASS(x) == V4L2_CTRL_CLASS_MPEG) \
						&& V4L2_CTRL_DRIVER_PRIV(x))

int s5p_mfc_enc_ctrls_setup(struct s5p_mfc_ctx *ctx)
{
	struct v4l2_ctrl_config cfg;
	int i;

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, NUM_CTRLS);
	if (ctx->ctrl_handler.error) {
		mfc_err("v4l2_ctrl_handler_init failed\n");
		return ctx->ctrl_handler.error;
	}
	for (i = 0; i < NUM_CTRLS; i++) {
		if (IS_MFC51_PRIV(controls[i].id)) {
			cfg.ops = &s5p_mfc_enc_ctrl_ops;
			cfg.id = controls[i].id;
			cfg.min = controls[i].minimum;
			cfg.max = controls[i].maximum;
			cfg.def = controls[i].default_value;
			cfg.name = controls[i].name;
			cfg.type = controls[i].type;
			cfg.flags = 0;

			if (cfg.type == V4L2_CTRL_TYPE_MENU) {
				cfg.step = 0;
				cfg.menu_skip_mask = cfg.menu_skip_mask;
				cfg.qmenu = mfc51_get_menu(cfg.id);
			} else {
				cfg.step = controls[i].step;
				cfg.menu_skip_mask = 0;
			}
			ctx->ctrls[i] = v4l2_ctrl_new_custom(&ctx->ctrl_handler,
					&cfg, NULL);
		} else {
			if (controls[i].type == V4L2_CTRL_TYPE_MENU) {
				ctx->ctrls[i] = v4l2_ctrl_new_std_menu(
					&ctx->ctrl_handler,
					&s5p_mfc_enc_ctrl_ops, controls[i].id,
					controls[i].maximum, 0,
					controls[i].default_value);
			} else {
				ctx->ctrls[i] = v4l2_ctrl_new_std(
					&ctx->ctrl_handler,
					&s5p_mfc_enc_ctrl_ops, controls[i].id,
					controls[i].minimum,
					controls[i].maximum, controls[i].step,
					controls[i].default_value);
			}
		}
		if (ctx->ctrl_handler.error) {
			mfc_err("Adding control (%d) failed\n", i);
			return ctx->ctrl_handler.error;
		}
		if (controls[i].is_volatile && ctx->ctrls[i])
			ctx->ctrls[i]->flags |= V4L2_CTRL_FLAG_VOLATILE;
	}
	return 0;
}

void s5p_mfc_enc_ctrls_delete(struct s5p_mfc_ctx *ctx)
{
	int i;

	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	for (i = 0; i < NUM_CTRLS; i++)
		ctx->ctrls[i] = NULL;
}
