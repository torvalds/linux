// SPDX-License-Identifier: GPL-2.0
/*
 * Cedrus VPU driver
 *
 * Copyright (C) 2016 Florent Revest <florent.revest@free-electrons.com>
 * Copyright (C) 2018 Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 * Copyright (C) 2018 Bootlin
 *
 * Based on the vim2m driver, that is:
 *
 * Copyright (c) 2009-2010 Samsung Electronics Co., Ltd.
 * Pawel Osciak, <pawel@osciak.com>
 * Marek Szyprowski, <m.szyprowski@samsung.com>
 */

#include <media/v4l2-device.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>

#include "cedrus.h"
#include "cedrus_dec.h"
#include "cedrus_hw.h"

void cedrus_device_run(void *priv)
{
	struct cedrus_ctx *ctx = priv;
	struct cedrus_dev *dev = ctx->dev;
	struct cedrus_run run = {};
	struct media_request *src_req;

	run.src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	run.dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	/* Apply request(s) controls if needed. */
	src_req = run.src->vb2_buf.req_obj.req;

	if (src_req)
		v4l2_ctrl_request_setup(src_req, &ctx->hdl);

	switch (ctx->src_fmt.pixelformat) {
	case V4L2_PIX_FMT_MPEG2_SLICE:
		run.mpeg2.sequence = cedrus_find_control_data(ctx,
			V4L2_CID_STATELESS_MPEG2_SEQUENCE);
		run.mpeg2.picture = cedrus_find_control_data(ctx,
			V4L2_CID_STATELESS_MPEG2_PICTURE);
		run.mpeg2.quantisation = cedrus_find_control_data(ctx,
			V4L2_CID_STATELESS_MPEG2_QUANTISATION);
		break;

	case V4L2_PIX_FMT_H264_SLICE:
		run.h264.decode_params = cedrus_find_control_data(ctx,
			V4L2_CID_STATELESS_H264_DECODE_PARAMS);
		run.h264.pps = cedrus_find_control_data(ctx,
			V4L2_CID_STATELESS_H264_PPS);
		run.h264.scaling_matrix = cedrus_find_control_data(ctx,
			V4L2_CID_STATELESS_H264_SCALING_MATRIX);
		run.h264.slice_params = cedrus_find_control_data(ctx,
			V4L2_CID_STATELESS_H264_SLICE_PARAMS);
		run.h264.sps = cedrus_find_control_data(ctx,
			V4L2_CID_STATELESS_H264_SPS);
		run.h264.pred_weights = cedrus_find_control_data(ctx,
			V4L2_CID_STATELESS_H264_PRED_WEIGHTS);
		break;

	case V4L2_PIX_FMT_HEVC_SLICE:
		run.h265.sps = cedrus_find_control_data(ctx,
			V4L2_CID_MPEG_VIDEO_HEVC_SPS);
		run.h265.pps = cedrus_find_control_data(ctx,
			V4L2_CID_MPEG_VIDEO_HEVC_PPS);
		run.h265.slice_params = cedrus_find_control_data(ctx,
			V4L2_CID_MPEG_VIDEO_HEVC_SLICE_PARAMS);
		run.h265.decode_params = cedrus_find_control_data(ctx,
			V4L2_CID_MPEG_VIDEO_HEVC_DECODE_PARAMS);
		run.h265.scaling_matrix = cedrus_find_control_data(ctx,
			V4L2_CID_MPEG_VIDEO_HEVC_SCALING_MATRIX);
		break;

	case V4L2_PIX_FMT_VP8_FRAME:
		run.vp8.frame_params = cedrus_find_control_data(ctx,
			V4L2_CID_STATELESS_VP8_FRAME);
		break;

	default:
		break;
	}

	v4l2_m2m_buf_copy_metadata(run.src, run.dst, true);

	cedrus_dst_format_set(dev, &ctx->dst_fmt);

	dev->dec_ops[ctx->current_codec]->setup(ctx, &run);

	/* Complete request(s) controls if needed. */

	if (src_req)
		v4l2_ctrl_request_complete(src_req, &ctx->hdl);

	dev->dec_ops[ctx->current_codec]->trigger(ctx);
}
