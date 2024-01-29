// SPDX-License-Identifier: GPL-2.0
/*
 * Hantro VPU codec driver
 *
 * Copyright (C) 2018 Collabora, Ltd.
 * Copyright 2018 Google LLC.
 *	Tomasz Figa <tfiga@chromium.org>
 *
 * Based on s5p-mfc driver by Samsung Electronics Co., Ltd.
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/videodev2.h>
#include <linux/workqueue.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-vmalloc.h>

#include "hantro_v4l2.h"
#include "hantro.h"
#include "hantro_hw.h"

#define DRIVER_NAME "hantro-vpu"

int hantro_debug;
module_param_named(debug, hantro_debug, int, 0644);
MODULE_PARM_DESC(debug,
		 "Debug level - higher value produces more verbose messages");

void *hantro_get_ctrl(struct hantro_ctx *ctx, u32 id)
{
	struct v4l2_ctrl *ctrl;

	ctrl = v4l2_ctrl_find(&ctx->ctrl_handler, id);
	return ctrl ? ctrl->p_cur.p : NULL;
}

dma_addr_t hantro_get_ref(struct hantro_ctx *ctx, u64 ts)
{
	struct vb2_queue *q = v4l2_m2m_get_dst_vq(ctx->fh.m2m_ctx);
	struct vb2_buffer *buf;

	buf = vb2_find_buffer(q, ts);
	if (!buf)
		return 0;
	return hantro_get_dec_buf_addr(ctx, buf);
}

static const struct v4l2_event hantro_eos_event = {
	.type = V4L2_EVENT_EOS
};

static void hantro_job_finish_no_pm(struct hantro_dev *vpu,
				    struct hantro_ctx *ctx,
				    enum vb2_buffer_state result)
{
	struct vb2_v4l2_buffer *src, *dst;

	src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	if (WARN_ON(!src))
		return;
	if (WARN_ON(!dst))
		return;

	src->sequence = ctx->sequence_out++;
	dst->sequence = ctx->sequence_cap++;

	if (v4l2_m2m_is_last_draining_src_buf(ctx->fh.m2m_ctx, src)) {
		dst->flags |= V4L2_BUF_FLAG_LAST;
		v4l2_event_queue_fh(&ctx->fh, &hantro_eos_event);
		v4l2_m2m_mark_stopped(ctx->fh.m2m_ctx);
	}

	v4l2_m2m_buf_done_and_job_finish(ctx->dev->m2m_dev, ctx->fh.m2m_ctx,
					 result);
}

static void hantro_job_finish(struct hantro_dev *vpu,
			      struct hantro_ctx *ctx,
			      enum vb2_buffer_state result)
{
	pm_runtime_mark_last_busy(vpu->dev);
	pm_runtime_put_autosuspend(vpu->dev);

	clk_bulk_disable(vpu->variant->num_clocks, vpu->clocks);

	hantro_job_finish_no_pm(vpu, ctx, result);
}

void hantro_irq_done(struct hantro_dev *vpu,
		     enum vb2_buffer_state result)
{
	struct hantro_ctx *ctx =
		v4l2_m2m_get_curr_priv(vpu->m2m_dev);

	/*
	 * If cancel_delayed_work returns false
	 * the timeout expired. The watchdog is running,
	 * and will take care of finishing the job.
	 */
	if (cancel_delayed_work(&vpu->watchdog_work)) {
		if (result == VB2_BUF_STATE_DONE && ctx->codec_ops->done)
			ctx->codec_ops->done(ctx);
		hantro_job_finish(vpu, ctx, result);
	}
}

void hantro_watchdog(struct work_struct *work)
{
	struct hantro_dev *vpu;
	struct hantro_ctx *ctx;

	vpu = container_of(to_delayed_work(work),
			   struct hantro_dev, watchdog_work);
	ctx = v4l2_m2m_get_curr_priv(vpu->m2m_dev);
	if (ctx) {
		vpu_err("frame processing timed out!\n");
		if (ctx->codec_ops->reset)
			ctx->codec_ops->reset(ctx);
		hantro_job_finish(vpu, ctx, VB2_BUF_STATE_ERROR);
	}
}

void hantro_start_prepare_run(struct hantro_ctx *ctx)
{
	struct vb2_v4l2_buffer *src_buf;

	src_buf = hantro_get_src_buf(ctx);
	v4l2_ctrl_request_setup(src_buf->vb2_buf.req_obj.req,
				&ctx->ctrl_handler);

	if (!ctx->is_encoder && !ctx->dev->variant->late_postproc) {
		if (hantro_needs_postproc(ctx, ctx->vpu_dst_fmt))
			hantro_postproc_enable(ctx);
		else
			hantro_postproc_disable(ctx);
	}
}

void hantro_end_prepare_run(struct hantro_ctx *ctx)
{
	struct vb2_v4l2_buffer *src_buf;

	if (!ctx->is_encoder && ctx->dev->variant->late_postproc) {
		if (hantro_needs_postproc(ctx, ctx->vpu_dst_fmt))
			hantro_postproc_enable(ctx);
		else
			hantro_postproc_disable(ctx);
	}

	src_buf = hantro_get_src_buf(ctx);
	v4l2_ctrl_request_complete(src_buf->vb2_buf.req_obj.req,
				   &ctx->ctrl_handler);

	/* Kick the watchdog. */
	schedule_delayed_work(&ctx->dev->watchdog_work,
			      msecs_to_jiffies(2000));
}

static void device_run(void *priv)
{
	struct hantro_ctx *ctx = priv;
	struct vb2_v4l2_buffer *src, *dst;
	int ret;

	src = hantro_get_src_buf(ctx);
	dst = hantro_get_dst_buf(ctx);

	ret = pm_runtime_resume_and_get(ctx->dev->dev);
	if (ret < 0)
		goto err_cancel_job;

	ret = clk_bulk_enable(ctx->dev->variant->num_clocks, ctx->dev->clocks);
	if (ret)
		goto err_cancel_job;

	v4l2_m2m_buf_copy_metadata(src, dst, true);

	if (ctx->codec_ops->run(ctx))
		goto err_cancel_job;

	return;

err_cancel_job:
	hantro_job_finish_no_pm(ctx->dev, ctx, VB2_BUF_STATE_ERROR);
}

static const struct v4l2_m2m_ops vpu_m2m_ops = {
	.device_run = device_run,
};

static int
queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	struct hantro_ctx *ctx = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->drv_priv = ctx;
	src_vq->ops = &hantro_queue_ops;
	src_vq->mem_ops = &vb2_dma_contig_memops;

	/*
	 * Driver does mostly sequential access, so sacrifice TLB efficiency
	 * for faster allocation. Also, no CPU access on the source queue,
	 * so no kernel mapping needed.
	 */
	src_vq->dma_attrs = DMA_ATTR_ALLOC_SINGLE_PAGES |
			    DMA_ATTR_NO_KERNEL_MAPPING;
	src_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock = &ctx->dev->vpu_mutex;
	src_vq->dev = ctx->dev->v4l2_dev.dev;
	src_vq->supports_requests = true;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->bidirectional = true;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->dma_attrs = DMA_ATTR_ALLOC_SINGLE_PAGES;
	/*
	 * The Kernel needs access to the JPEG destination buffer for the
	 * JPEG encoder to fill in the JPEG headers.
	 */
	if (!ctx->is_encoder) {
		dst_vq->dma_attrs |= DMA_ATTR_NO_KERNEL_MAPPING;
		dst_vq->max_num_buffers = MAX_POSTPROC_BUFFERS;
	}

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->drv_priv = ctx;
	dst_vq->ops = &hantro_queue_ops;
	dst_vq->buf_struct_size = sizeof(struct hantro_decoded_buffer);
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock = &ctx->dev->vpu_mutex;
	dst_vq->dev = ctx->dev->v4l2_dev.dev;

	return vb2_queue_init(dst_vq);
}

static int hantro_try_ctrl(struct v4l2_ctrl *ctrl)
{
	if (ctrl->id == V4L2_CID_STATELESS_H264_SPS) {
		const struct v4l2_ctrl_h264_sps *sps = ctrl->p_new.p_h264_sps;

		if (sps->chroma_format_idc > 1)
			/* Only 4:0:0 and 4:2:0 are supported */
			return -EINVAL;
		if (sps->bit_depth_luma_minus8 != sps->bit_depth_chroma_minus8)
			/* Luma and chroma bit depth mismatch */
			return -EINVAL;
		if (sps->bit_depth_luma_minus8 != 0)
			/* Only 8-bit is supported */
			return -EINVAL;
	} else if (ctrl->id == V4L2_CID_STATELESS_HEVC_SPS) {
		const struct v4l2_ctrl_hevc_sps *sps = ctrl->p_new.p_hevc_sps;

		if (sps->bit_depth_luma_minus8 != 0 && sps->bit_depth_luma_minus8 != 2)
			/* Only 8-bit and 10-bit are supported */
			return -EINVAL;
	} else if (ctrl->id == V4L2_CID_STATELESS_VP9_FRAME) {
		const struct v4l2_ctrl_vp9_frame *dec_params = ctrl->p_new.p_vp9_frame;

		/* We only support profile 0 */
		if (dec_params->profile != 0)
			return -EINVAL;
	} else if (ctrl->id == V4L2_CID_STATELESS_AV1_SEQUENCE) {
		const struct v4l2_ctrl_av1_sequence *sequence = ctrl->p_new.p_av1_sequence;

		if (sequence->bit_depth != 8 && sequence->bit_depth != 10)
			return -EINVAL;
	}

	return 0;
}

static int hantro_jpeg_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hantro_ctx *ctx;

	ctx = container_of(ctrl->handler,
			   struct hantro_ctx, ctrl_handler);

	vpu_debug(1, "s_ctrl: id = %d, val = %d\n", ctrl->id, ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_JPEG_COMPRESSION_QUALITY:
		ctx->jpeg_quality = ctrl->val;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int hantro_vp9_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hantro_ctx *ctx;

	ctx = container_of(ctrl->handler,
			   struct hantro_ctx, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_STATELESS_VP9_FRAME: {
		int bit_depth = ctrl->p_new.p_vp9_frame->bit_depth;

		if (ctx->bit_depth == bit_depth)
			return 0;

		return hantro_reset_raw_fmt(ctx, bit_depth, HANTRO_AUTO_POSTPROC);
	}
	default:
		return -EINVAL;
	}

	return 0;
}

static int hantro_hevc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hantro_ctx *ctx;

	ctx = container_of(ctrl->handler,
			   struct hantro_ctx, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_STATELESS_HEVC_SPS: {
		const struct v4l2_ctrl_hevc_sps *sps = ctrl->p_new.p_hevc_sps;
		int bit_depth = sps->bit_depth_luma_minus8 + 8;

		if (ctx->bit_depth == bit_depth)
			return 0;

		return hantro_reset_raw_fmt(ctx, bit_depth, HANTRO_AUTO_POSTPROC);
	}
	default:
		return -EINVAL;
	}

	return 0;
}

static int hantro_av1_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct hantro_ctx *ctx;

	ctx = container_of(ctrl->handler,
			   struct hantro_ctx, ctrl_handler);

	switch (ctrl->id) {
	case V4L2_CID_STATELESS_AV1_SEQUENCE:
	{
		int bit_depth = ctrl->p_new.p_av1_sequence->bit_depth;
		bool need_postproc = HANTRO_AUTO_POSTPROC;

		if (ctrl->p_new.p_av1_sequence->flags
		    & V4L2_AV1_SEQUENCE_FLAG_FILM_GRAIN_PARAMS_PRESENT)
			need_postproc = HANTRO_FORCE_POSTPROC;

		if (ctx->bit_depth == bit_depth &&
		    ctx->need_postproc == need_postproc)
			return 0;

		return hantro_reset_raw_fmt(ctx, bit_depth, need_postproc);
	}
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops hantro_ctrl_ops = {
	.try_ctrl = hantro_try_ctrl,
};

static const struct v4l2_ctrl_ops hantro_jpeg_ctrl_ops = {
	.s_ctrl = hantro_jpeg_s_ctrl,
};

static const struct v4l2_ctrl_ops hantro_vp9_ctrl_ops = {
	.s_ctrl = hantro_vp9_s_ctrl,
};

static const struct v4l2_ctrl_ops hantro_hevc_ctrl_ops = {
	.try_ctrl = hantro_try_ctrl,
	.s_ctrl = hantro_hevc_s_ctrl,
};

static const struct v4l2_ctrl_ops hantro_av1_ctrl_ops = {
	.try_ctrl = hantro_try_ctrl,
	.s_ctrl = hantro_av1_s_ctrl,
};

#define HANTRO_JPEG_ACTIVE_MARKERS	(V4L2_JPEG_ACTIVE_MARKER_APP0 | \
					 V4L2_JPEG_ACTIVE_MARKER_COM | \
					 V4L2_JPEG_ACTIVE_MARKER_DQT | \
					 V4L2_JPEG_ACTIVE_MARKER_DHT)

static const struct hantro_ctrl controls[] = {
	{
		.codec = HANTRO_JPEG_ENCODER,
		.cfg = {
			.id = V4L2_CID_JPEG_COMPRESSION_QUALITY,
			.min = 5,
			.max = 100,
			.step = 1,
			.def = 50,
			.ops = &hantro_jpeg_ctrl_ops,
		},
	}, {
		.codec = HANTRO_JPEG_ENCODER,
		.cfg = {
			.id = V4L2_CID_JPEG_ACTIVE_MARKER,
			.max = HANTRO_JPEG_ACTIVE_MARKERS,
			.def = HANTRO_JPEG_ACTIVE_MARKERS,
			/*
			 * Changing the set of active markers/segments also
			 * messes up the alignment of the JPEG header, which
			 * is needed to allow the hardware to write directly
			 * to the output buffer. Implementing this introduces
			 * a lot of complexity for little gain, as the markers
			 * enabled is already the minimum required set.
			 */
			.flags = V4L2_CTRL_FLAG_READ_ONLY,
		},
	}, {
		.codec = HANTRO_MPEG2_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_MPEG2_SEQUENCE,
		},
	}, {
		.codec = HANTRO_MPEG2_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_MPEG2_PICTURE,
		},
	}, {
		.codec = HANTRO_MPEG2_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_MPEG2_QUANTISATION,
		},
	}, {
		.codec = HANTRO_VP8_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_VP8_FRAME,
		},
	}, {
		.codec = HANTRO_H264_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_DECODE_PARAMS,
		},
	}, {
		.codec = HANTRO_H264_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_SPS,
			.ops = &hantro_ctrl_ops,
		},
	}, {
		.codec = HANTRO_H264_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_PPS,
		},
	}, {
		.codec = HANTRO_H264_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_SCALING_MATRIX,
		},
	}, {
		.codec = HANTRO_H264_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_DECODE_MODE,
			.min = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
			.def = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
			.max = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
		},
	}, {
		.codec = HANTRO_H264_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_START_CODE,
			.min = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
			.def = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
			.max = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
		},
	}, {
		.codec = HANTRO_H264_DECODER,
		.cfg = {
			.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE,
			.min = V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE,
			.max = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
			.menu_skip_mask =
			BIT(V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED),
			.def = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN,
		}
	}, {
		.codec = HANTRO_HEVC_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_HEVC_DECODE_MODE,
			.min = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED,
			.max = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED,
			.def = V4L2_STATELESS_HEVC_DECODE_MODE_FRAME_BASED,
		},
	}, {
		.codec = HANTRO_HEVC_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_HEVC_START_CODE,
			.min = V4L2_STATELESS_HEVC_START_CODE_ANNEX_B,
			.max = V4L2_STATELESS_HEVC_START_CODE_ANNEX_B,
			.def = V4L2_STATELESS_HEVC_START_CODE_ANNEX_B,
		},
	}, {
		.codec = HANTRO_HEVC_DECODER,
		.cfg = {
			.id = V4L2_CID_MPEG_VIDEO_HEVC_PROFILE,
			.min = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
			.max = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10,
			.def = V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN,
		},
	}, {
		.codec = HANTRO_HEVC_DECODER,
		.cfg = {
			.id = V4L2_CID_MPEG_VIDEO_HEVC_LEVEL,
			.min = V4L2_MPEG_VIDEO_HEVC_LEVEL_1,
			.max = V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1,
		},
	}, {
		.codec = HANTRO_HEVC_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_HEVC_SPS,
			.ops = &hantro_hevc_ctrl_ops,
		},
	}, {
		.codec = HANTRO_HEVC_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_HEVC_PPS,
		},
	}, {
		.codec = HANTRO_HEVC_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_HEVC_DECODE_PARAMS,
		},
	}, {
		.codec = HANTRO_HEVC_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_HEVC_SCALING_MATRIX,
		},
	}, {
		.codec = HANTRO_VP9_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_VP9_FRAME,
			.ops = &hantro_vp9_ctrl_ops,
		},
	}, {
		.codec = HANTRO_VP9_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_VP9_COMPRESSED_HDR,
		},
	}, {
		.codec = HANTRO_AV1_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_AV1_FRAME,
		},
	}, {
		.codec = HANTRO_AV1_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_AV1_TILE_GROUP_ENTRY,
			.dims = { V4L2_AV1_MAX_TILE_COUNT },
		},
	}, {
		.codec = HANTRO_AV1_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_AV1_SEQUENCE,
			.ops = &hantro_av1_ctrl_ops,
		},
	}, {
		.codec = HANTRO_AV1_DECODER,
		.cfg = {
			.id = V4L2_CID_STATELESS_AV1_FILM_GRAIN,
		},
	},
};

static int hantro_ctrls_setup(struct hantro_dev *vpu,
			      struct hantro_ctx *ctx,
			      int allowed_codecs)
{
	int i, num_ctrls = ARRAY_SIZE(controls);

	v4l2_ctrl_handler_init(&ctx->ctrl_handler, num_ctrls);

	for (i = 0; i < num_ctrls; i++) {
		if (!(allowed_codecs & controls[i].codec))
			continue;

		v4l2_ctrl_new_custom(&ctx->ctrl_handler,
				     &controls[i].cfg, NULL);
		if (ctx->ctrl_handler.error) {
			vpu_err("Adding control (%d) failed %d\n",
				controls[i].cfg.id,
				ctx->ctrl_handler.error);
			v4l2_ctrl_handler_free(&ctx->ctrl_handler);
			return ctx->ctrl_handler.error;
		}
	}
	return v4l2_ctrl_handler_setup(&ctx->ctrl_handler);
}

/*
 * V4L2 file operations.
 */

static int hantro_open(struct file *filp)
{
	struct hantro_dev *vpu = video_drvdata(filp);
	struct video_device *vdev = video_devdata(filp);
	struct hantro_func *func = hantro_vdev_to_func(vdev);
	struct hantro_ctx *ctx;
	int allowed_codecs, ret;

	/*
	 * We do not need any extra locking here, because we operate only
	 * on local data here, except reading few fields from dev, which
	 * do not change through device's lifetime (which is guaranteed by
	 * reference on module from open()) and V4L2 internal objects (such
	 * as vdev and ctx->fh), which have proper locking done in respective
	 * helper functions used here.
	 */

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = vpu;
	if (func->id == MEDIA_ENT_F_PROC_VIDEO_ENCODER) {
		allowed_codecs = vpu->variant->codec & HANTRO_ENCODERS;
		ctx->is_encoder = true;
	} else if (func->id == MEDIA_ENT_F_PROC_VIDEO_DECODER) {
		allowed_codecs = vpu->variant->codec & HANTRO_DECODERS;
		ctx->is_encoder = false;
	} else {
		ret = -ENODEV;
		goto err_ctx_free;
	}

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(vpu->m2m_dev, ctx, queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto err_ctx_free;
	}

	v4l2_fh_init(&ctx->fh, vdev);
	filp->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	hantro_reset_fmts(ctx);

	ret = hantro_ctrls_setup(vpu, ctx, allowed_codecs);
	if (ret) {
		vpu_err("Failed to set up controls\n");
		goto err_fh_free;
	}
	ctx->fh.ctrl_handler = &ctx->ctrl_handler;

	return 0;

err_fh_free:
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
err_ctx_free:
	kfree(ctx);
	return ret;
}

static int hantro_release(struct file *filp)
{
	struct hantro_ctx *ctx =
		container_of(filp->private_data, struct hantro_ctx, fh);

	/*
	 * No need for extra locking because this was the last reference
	 * to this file.
	 */
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	v4l2_ctrl_handler_free(&ctx->ctrl_handler);
	kfree(ctx);

	return 0;
}

static const struct v4l2_file_operations hantro_fops = {
	.owner = THIS_MODULE,
	.open = hantro_open,
	.release = hantro_release,
	.poll = v4l2_m2m_fop_poll,
	.unlocked_ioctl = video_ioctl2,
	.mmap = v4l2_m2m_fop_mmap,
};

static const struct of_device_id of_hantro_match[] = {
#ifdef CONFIG_VIDEO_HANTRO_ROCKCHIP
	{ .compatible = "rockchip,px30-vpu",   .data = &px30_vpu_variant, },
	{ .compatible = "rockchip,rk3036-vpu", .data = &rk3036_vpu_variant, },
	{ .compatible = "rockchip,rk3066-vpu", .data = &rk3066_vpu_variant, },
	{ .compatible = "rockchip,rk3288-vpu", .data = &rk3288_vpu_variant, },
	{ .compatible = "rockchip,rk3328-vpu", .data = &rk3328_vpu_variant, },
	{ .compatible = "rockchip,rk3399-vpu", .data = &rk3399_vpu_variant, },
	{ .compatible = "rockchip,rk3568-vepu", .data = &rk3568_vepu_variant, },
	{ .compatible = "rockchip,rk3568-vpu", .data = &rk3568_vpu_variant, },
	{ .compatible = "rockchip,rk3588-av1-vpu", .data = &rk3588_vpu981_variant, },
#endif
#ifdef CONFIG_VIDEO_HANTRO_IMX8M
	{ .compatible = "nxp,imx8mm-vpu-g1", .data = &imx8mm_vpu_g1_variant, },
	{ .compatible = "nxp,imx8mq-vpu", .data = &imx8mq_vpu_variant, },
	{ .compatible = "nxp,imx8mq-vpu-g1", .data = &imx8mq_vpu_g1_variant },
	{ .compatible = "nxp,imx8mq-vpu-g2", .data = &imx8mq_vpu_g2_variant },
#endif
#ifdef CONFIG_VIDEO_HANTRO_SAMA5D4
	{ .compatible = "microchip,sama5d4-vdec", .data = &sama5d4_vdec_variant, },
#endif
#ifdef CONFIG_VIDEO_HANTRO_SUNXI
	{ .compatible = "allwinner,sun50i-h6-vpu-g2", .data = &sunxi_vpu_variant, },
#endif
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, of_hantro_match);

static int hantro_register_entity(struct media_device *mdev,
				  struct media_entity *entity,
				  const char *entity_name,
				  struct media_pad *pads, int num_pads,
				  int function, struct video_device *vdev)
{
	char *name;
	int ret;

	entity->obj_type = MEDIA_ENTITY_TYPE_BASE;
	if (function == MEDIA_ENT_F_IO_V4L) {
		entity->info.dev.major = VIDEO_MAJOR;
		entity->info.dev.minor = vdev->minor;
	}

	name = devm_kasprintf(mdev->dev, GFP_KERNEL, "%s-%s", vdev->name,
			      entity_name);
	if (!name)
		return -ENOMEM;

	entity->name = name;
	entity->function = function;

	ret = media_entity_pads_init(entity, num_pads, pads);
	if (ret)
		return ret;

	ret = media_device_register_entity(mdev, entity);
	if (ret)
		return ret;

	return 0;
}

static int hantro_attach_func(struct hantro_dev *vpu,
			      struct hantro_func *func)
{
	struct media_device *mdev = &vpu->mdev;
	struct media_link *link;
	int ret;

	/* Create the three encoder entities with their pads */
	func->source_pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = hantro_register_entity(mdev, &func->vdev.entity, "source",
				     &func->source_pad, 1, MEDIA_ENT_F_IO_V4L,
				     &func->vdev);
	if (ret)
		return ret;

	func->proc_pads[0].flags = MEDIA_PAD_FL_SINK;
	func->proc_pads[1].flags = MEDIA_PAD_FL_SOURCE;
	ret = hantro_register_entity(mdev, &func->proc, "proc",
				     func->proc_pads, 2, func->id,
				     &func->vdev);
	if (ret)
		goto err_rel_entity0;

	func->sink_pad.flags = MEDIA_PAD_FL_SINK;
	ret = hantro_register_entity(mdev, &func->sink, "sink",
				     &func->sink_pad, 1, MEDIA_ENT_F_IO_V4L,
				     &func->vdev);
	if (ret)
		goto err_rel_entity1;

	/* Connect the three entities */
	ret = media_create_pad_link(&func->vdev.entity, 0, &func->proc, 0,
				    MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);
	if (ret)
		goto err_rel_entity2;

	ret = media_create_pad_link(&func->proc, 1, &func->sink, 0,
				    MEDIA_LNK_FL_IMMUTABLE |
				    MEDIA_LNK_FL_ENABLED);
	if (ret)
		goto err_rm_links0;

	/* Create video interface */
	func->intf_devnode = media_devnode_create(mdev, MEDIA_INTF_T_V4L_VIDEO,
						  0, VIDEO_MAJOR,
						  func->vdev.minor);
	if (!func->intf_devnode) {
		ret = -ENOMEM;
		goto err_rm_links1;
	}

	/* Connect the two DMA engines to the interface */
	link = media_create_intf_link(&func->vdev.entity,
				      &func->intf_devnode->intf,
				      MEDIA_LNK_FL_IMMUTABLE |
				      MEDIA_LNK_FL_ENABLED);
	if (!link) {
		ret = -ENOMEM;
		goto err_rm_devnode;
	}

	link = media_create_intf_link(&func->sink, &func->intf_devnode->intf,
				      MEDIA_LNK_FL_IMMUTABLE |
				      MEDIA_LNK_FL_ENABLED);
	if (!link) {
		ret = -ENOMEM;
		goto err_rm_devnode;
	}
	return 0;

err_rm_devnode:
	media_devnode_remove(func->intf_devnode);

err_rm_links1:
	media_entity_remove_links(&func->sink);

err_rm_links0:
	media_entity_remove_links(&func->proc);
	media_entity_remove_links(&func->vdev.entity);

err_rel_entity2:
	media_device_unregister_entity(&func->sink);

err_rel_entity1:
	media_device_unregister_entity(&func->proc);

err_rel_entity0:
	media_device_unregister_entity(&func->vdev.entity);
	return ret;
}

static void hantro_detach_func(struct hantro_func *func)
{
	media_devnode_remove(func->intf_devnode);
	media_entity_remove_links(&func->sink);
	media_entity_remove_links(&func->proc);
	media_entity_remove_links(&func->vdev.entity);
	media_device_unregister_entity(&func->sink);
	media_device_unregister_entity(&func->proc);
	media_device_unregister_entity(&func->vdev.entity);
}

static int hantro_add_func(struct hantro_dev *vpu, unsigned int funcid)
{
	const struct of_device_id *match;
	struct hantro_func *func;
	struct video_device *vfd;
	int ret;

	match = of_match_node(of_hantro_match, vpu->dev->of_node);
	func = devm_kzalloc(vpu->dev, sizeof(*func), GFP_KERNEL);
	if (!func) {
		v4l2_err(&vpu->v4l2_dev, "Failed to allocate video device\n");
		return -ENOMEM;
	}

	func->id = funcid;

	vfd = &func->vdev;
	vfd->fops = &hantro_fops;
	vfd->release = video_device_release_empty;
	vfd->lock = &vpu->vpu_mutex;
	vfd->v4l2_dev = &vpu->v4l2_dev;
	vfd->vfl_dir = VFL_DIR_M2M;
	vfd->device_caps = V4L2_CAP_STREAMING | V4L2_CAP_VIDEO_M2M_MPLANE;
	vfd->ioctl_ops = &hantro_ioctl_ops;
	strscpy(vfd->name, match->compatible, sizeof(vfd->name));
	strlcat(vfd->name, funcid == MEDIA_ENT_F_PROC_VIDEO_ENCODER ?
		"-enc" : "-dec", sizeof(vfd->name));

	if (funcid == MEDIA_ENT_F_PROC_VIDEO_ENCODER) {
		vpu->encoder = func;
		v4l2_disable_ioctl(vfd, VIDIOC_TRY_DECODER_CMD);
		v4l2_disable_ioctl(vfd, VIDIOC_DECODER_CMD);
	} else {
		vpu->decoder = func;
		v4l2_disable_ioctl(vfd, VIDIOC_TRY_ENCODER_CMD);
		v4l2_disable_ioctl(vfd, VIDIOC_ENCODER_CMD);
	}

	video_set_drvdata(vfd, vpu);

	ret = video_register_device(vfd, VFL_TYPE_VIDEO, -1);
	if (ret) {
		v4l2_err(&vpu->v4l2_dev, "Failed to register video device\n");
		return ret;
	}

	ret = hantro_attach_func(vpu, func);
	if (ret) {
		v4l2_err(&vpu->v4l2_dev,
			 "Failed to attach functionality to the media device\n");
		goto err_unreg_dev;
	}

	v4l2_info(&vpu->v4l2_dev, "registered %s as /dev/video%d\n", vfd->name,
		  vfd->num);

	return 0;

err_unreg_dev:
	video_unregister_device(vfd);
	return ret;
}

static int hantro_add_enc_func(struct hantro_dev *vpu)
{
	if (!vpu->variant->enc_fmts)
		return 0;

	return hantro_add_func(vpu, MEDIA_ENT_F_PROC_VIDEO_ENCODER);
}

static int hantro_add_dec_func(struct hantro_dev *vpu)
{
	if (!vpu->variant->dec_fmts)
		return 0;

	return hantro_add_func(vpu, MEDIA_ENT_F_PROC_VIDEO_DECODER);
}

static void hantro_remove_func(struct hantro_dev *vpu,
			       unsigned int funcid)
{
	struct hantro_func *func;

	if (funcid == MEDIA_ENT_F_PROC_VIDEO_ENCODER)
		func = vpu->encoder;
	else
		func = vpu->decoder;

	if (!func)
		return;

	hantro_detach_func(func);
	video_unregister_device(&func->vdev);
}

static void hantro_remove_enc_func(struct hantro_dev *vpu)
{
	hantro_remove_func(vpu, MEDIA_ENT_F_PROC_VIDEO_ENCODER);
}

static void hantro_remove_dec_func(struct hantro_dev *vpu)
{
	hantro_remove_func(vpu, MEDIA_ENT_F_PROC_VIDEO_DECODER);
}

static const struct media_device_ops hantro_m2m_media_ops = {
	.req_validate = vb2_request_validate,
	.req_queue = v4l2_m2m_request_queue,
};

static int hantro_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	struct hantro_dev *vpu;
	int num_bases;
	int i, ret;

	vpu = devm_kzalloc(&pdev->dev, sizeof(*vpu), GFP_KERNEL);
	if (!vpu)
		return -ENOMEM;

	vpu->dev = &pdev->dev;
	vpu->pdev = pdev;
	mutex_init(&vpu->vpu_mutex);
	spin_lock_init(&vpu->irqlock);

	match = of_match_node(of_hantro_match, pdev->dev.of_node);
	vpu->variant = match->data;

	/*
	 * Support for nxp,imx8mq-vpu is kept for backwards compatibility
	 * but it's deprecated. Please update your DTS file to use
	 * nxp,imx8mq-vpu-g1 or nxp,imx8mq-vpu-g2 instead.
	 */
	if (of_device_is_compatible(pdev->dev.of_node, "nxp,imx8mq-vpu"))
		dev_warn(&pdev->dev, "%s compatible is deprecated\n",
			 match->compatible);

	INIT_DELAYED_WORK(&vpu->watchdog_work, hantro_watchdog);

	vpu->clocks = devm_kcalloc(&pdev->dev, vpu->variant->num_clocks,
				   sizeof(*vpu->clocks), GFP_KERNEL);
	if (!vpu->clocks)
		return -ENOMEM;

	if (vpu->variant->num_clocks > 1) {
		for (i = 0; i < vpu->variant->num_clocks; i++)
			vpu->clocks[i].id = vpu->variant->clk_names[i];

		ret = devm_clk_bulk_get(&pdev->dev, vpu->variant->num_clocks,
					vpu->clocks);
		if (ret)
			return ret;
	} else {
		/*
		 * If the driver has a single clk, chances are there will be no
		 * actual name in the DT bindings.
		 */
		vpu->clocks[0].clk = devm_clk_get(&pdev->dev, NULL);
		if (IS_ERR(vpu->clocks[0].clk))
			return PTR_ERR(vpu->clocks[0].clk);
	}

	vpu->resets = devm_reset_control_array_get_optional_exclusive(&pdev->dev);
	if (IS_ERR(vpu->resets))
		return PTR_ERR(vpu->resets);

	num_bases = vpu->variant->num_regs ?: 1;
	vpu->reg_bases = devm_kcalloc(&pdev->dev, num_bases,
				      sizeof(*vpu->reg_bases), GFP_KERNEL);
	if (!vpu->reg_bases)
		return -ENOMEM;

	for (i = 0; i < num_bases; i++) {
		vpu->reg_bases[i] = vpu->variant->reg_names ?
		      devm_platform_ioremap_resource_byname(pdev, vpu->variant->reg_names[i]) :
		      devm_platform_ioremap_resource(pdev, 0);
		if (IS_ERR(vpu->reg_bases[i]))
			return PTR_ERR(vpu->reg_bases[i]);
	}
	vpu->enc_base = vpu->reg_bases[0] + vpu->variant->enc_offset;
	vpu->dec_base = vpu->reg_bases[0] + vpu->variant->dec_offset;

	/**
	 * TODO: Eventually allow taking advantage of full 64-bit address space.
	 * Until then we assume the MSB portion of buffers' base addresses is
	 * always 0 due to this masking operation.
	 */
	ret = dma_set_coherent_mask(vpu->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(vpu->dev, "Could not set DMA coherent mask.\n");
		return ret;
	}
	vb2_dma_contig_set_max_seg_size(&pdev->dev, DMA_BIT_MASK(32));

	for (i = 0; i < vpu->variant->num_irqs; i++) {
		const char *irq_name;
		int irq;

		if (!vpu->variant->irqs[i].handler)
			continue;

		if (vpu->variant->num_irqs > 1) {
			irq_name = vpu->variant->irqs[i].name;
			irq = platform_get_irq_byname(vpu->pdev, irq_name);
		} else {
			/*
			 * If the driver has a single IRQ, chances are there
			 * will be no actual name in the DT bindings.
			 */
			irq_name = "default";
			irq = platform_get_irq(vpu->pdev, 0);
		}
		if (irq < 0)
			return irq;

		ret = devm_request_irq(vpu->dev, irq,
				       vpu->variant->irqs[i].handler, 0,
				       dev_name(vpu->dev), vpu);
		if (ret) {
			dev_err(vpu->dev, "Could not request %s IRQ.\n",
				irq_name);
			return ret;
		}
	}

	if (vpu->variant->init) {
		ret = vpu->variant->init(vpu);
		if (ret) {
			dev_err(&pdev->dev, "Failed to init VPU hardware\n");
			return ret;
		}
	}

	pm_runtime_set_autosuspend_delay(vpu->dev, 100);
	pm_runtime_use_autosuspend(vpu->dev);
	pm_runtime_enable(vpu->dev);

	ret = reset_control_deassert(vpu->resets);
	if (ret) {
		dev_err(&pdev->dev, "Failed to deassert resets\n");
		goto err_pm_disable;
	}

	ret = clk_bulk_prepare(vpu->variant->num_clocks, vpu->clocks);
	if (ret) {
		dev_err(&pdev->dev, "Failed to prepare clocks\n");
		goto err_rst_assert;
	}

	ret = v4l2_device_register(&pdev->dev, &vpu->v4l2_dev);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register v4l2 device\n");
		goto err_clk_unprepare;
	}
	platform_set_drvdata(pdev, vpu);

	vpu->m2m_dev = v4l2_m2m_init(&vpu_m2m_ops);
	if (IS_ERR(vpu->m2m_dev)) {
		v4l2_err(&vpu->v4l2_dev, "Failed to init mem2mem device\n");
		ret = PTR_ERR(vpu->m2m_dev);
		goto err_v4l2_unreg;
	}

	vpu->mdev.dev = vpu->dev;
	strscpy(vpu->mdev.model, DRIVER_NAME, sizeof(vpu->mdev.model));
	media_device_init(&vpu->mdev);
	vpu->mdev.ops = &hantro_m2m_media_ops;
	vpu->v4l2_dev.mdev = &vpu->mdev;

	ret = hantro_add_enc_func(vpu);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register encoder\n");
		goto err_m2m_rel;
	}

	ret = hantro_add_dec_func(vpu);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register decoder\n");
		goto err_rm_enc_func;
	}

	ret = media_device_register(&vpu->mdev);
	if (ret) {
		v4l2_err(&vpu->v4l2_dev, "Failed to register mem2mem media device\n");
		goto err_rm_dec_func;
	}

	return 0;

err_rm_dec_func:
	hantro_remove_dec_func(vpu);
err_rm_enc_func:
	hantro_remove_enc_func(vpu);
err_m2m_rel:
	media_device_cleanup(&vpu->mdev);
	v4l2_m2m_release(vpu->m2m_dev);
err_v4l2_unreg:
	v4l2_device_unregister(&vpu->v4l2_dev);
err_clk_unprepare:
	clk_bulk_unprepare(vpu->variant->num_clocks, vpu->clocks);
err_rst_assert:
	reset_control_assert(vpu->resets);
err_pm_disable:
	pm_runtime_dont_use_autosuspend(vpu->dev);
	pm_runtime_disable(vpu->dev);
	return ret;
}

static void hantro_remove(struct platform_device *pdev)
{
	struct hantro_dev *vpu = platform_get_drvdata(pdev);

	v4l2_info(&vpu->v4l2_dev, "Removing %s\n", pdev->name);

	media_device_unregister(&vpu->mdev);
	hantro_remove_dec_func(vpu);
	hantro_remove_enc_func(vpu);
	media_device_cleanup(&vpu->mdev);
	v4l2_m2m_release(vpu->m2m_dev);
	v4l2_device_unregister(&vpu->v4l2_dev);
	clk_bulk_unprepare(vpu->variant->num_clocks, vpu->clocks);
	reset_control_assert(vpu->resets);
	pm_runtime_dont_use_autosuspend(vpu->dev);
	pm_runtime_disable(vpu->dev);
}

#ifdef CONFIG_PM
static int hantro_runtime_resume(struct device *dev)
{
	struct hantro_dev *vpu = dev_get_drvdata(dev);

	if (vpu->variant->runtime_resume)
		return vpu->variant->runtime_resume(vpu);

	return 0;
}
#endif

static const struct dev_pm_ops hantro_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(NULL, hantro_runtime_resume, NULL)
};

static struct platform_driver hantro_driver = {
	.probe = hantro_probe,
	.remove_new = hantro_remove,
	.driver = {
		   .name = DRIVER_NAME,
		   .of_match_table = of_hantro_match,
		   .pm = &hantro_pm_ops,
	},
};
module_platform_driver(hantro_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Alpha Lin <Alpha.Lin@Rock-Chips.com>");
MODULE_AUTHOR("Tomasz Figa <tfiga@chromium.org>");
MODULE_AUTHOR("Ezequiel Garcia <ezequiel@collabora.com>");
MODULE_DESCRIPTION("Hantro VPU codec driver");
