// SPDX-License-Identifier: GPL-2.0

#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <linux/module.h>

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_dec.h"
#include "mtk_vcodec_intr.h"
#include "mtk_vcodec_util.h"
#include "mtk_vcodec_dec_pm.h"
#include "vdec_drv_if.h"

/**
 * struct mtk_stateless_control  - CID control type
 * @cfg: control configuration
 * @codec_type: codec type (V4L2 pixel format) for CID control type
 */
struct mtk_stateless_control {
	struct v4l2_ctrl_config cfg;
	int codec_type;
};

static const struct mtk_stateless_control mtk_stateless_controls[] = {
	{
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_SPS,
		},
		.codec_type = V4L2_PIX_FMT_H264_SLICE,
	},
	{
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_PPS,
		},
		.codec_type = V4L2_PIX_FMT_H264_SLICE,
	},
	{
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_SCALING_MATRIX,
		},
		.codec_type = V4L2_PIX_FMT_H264_SLICE,
	},
	{
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_DECODE_PARAMS,
		},
		.codec_type = V4L2_PIX_FMT_H264_SLICE,
	},
	{
		.cfg = {
			.id = V4L2_CID_MPEG_VIDEO_H264_PROFILE,
			.def = V4L2_MPEG_VIDEO_H264_PROFILE_MAIN,
			.max = V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
			.menu_skip_mask =
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
				BIT(V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED),
		},
		.codec_type = V4L2_PIX_FMT_H264_SLICE,
	},
	{
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_DECODE_MODE,
			.min = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
			.def = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
			.max = V4L2_STATELESS_H264_DECODE_MODE_FRAME_BASED,
		},
		.codec_type = V4L2_PIX_FMT_H264_SLICE,
	},
	{
		.cfg = {
			.id = V4L2_CID_STATELESS_H264_START_CODE,
			.min = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
			.def = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
			.max = V4L2_STATELESS_H264_START_CODE_ANNEX_B,
		},
		.codec_type = V4L2_PIX_FMT_H264_SLICE,
	},
	{
		.cfg = {
			.id = V4L2_CID_STATELESS_VP8_FRAME,
		},
		.codec_type = V4L2_PIX_FMT_VP8_FRAME,
	},
	{
		.cfg = {
			.id = V4L2_CID_MPEG_VIDEO_VP8_PROFILE,
			.min = V4L2_MPEG_VIDEO_VP8_PROFILE_0,
			.def = V4L2_MPEG_VIDEO_VP8_PROFILE_0,
			.max = V4L2_MPEG_VIDEO_VP8_PROFILE_3,
		},
		.codec_type = V4L2_PIX_FMT_VP8_FRAME,
	},
	{
		.cfg = {
			.id = V4L2_CID_STATELESS_VP9_FRAME,
		},
		.codec_type = V4L2_PIX_FMT_VP9_FRAME,
	},
	{
		.cfg = {
			.id = V4L2_CID_MPEG_VIDEO_VP9_PROFILE,
			.min = V4L2_MPEG_VIDEO_VP9_PROFILE_0,
			.def = V4L2_MPEG_VIDEO_VP9_PROFILE_0,
			.max = V4L2_MPEG_VIDEO_VP9_PROFILE_3,
		},
		.codec_type = V4L2_PIX_FMT_VP9_FRAME,
	},
};

#define NUM_CTRLS ARRAY_SIZE(mtk_stateless_controls)

static struct mtk_video_fmt mtk_video_formats[5];

static struct mtk_video_fmt default_out_format;
static struct mtk_video_fmt default_cap_format;
static unsigned int num_formats;

static const struct v4l2_frmsize_stepwise stepwise_fhd = {
	.min_width = MTK_VDEC_MIN_W,
	.max_width = MTK_VDEC_MAX_W,
	.step_width = 16,
	.min_height = MTK_VDEC_MIN_H,
	.max_height = MTK_VDEC_MAX_H,
	.step_height = 16
};

static void mtk_vdec_stateless_cap_to_disp(struct mtk_vcodec_ctx *ctx, int error,
					   struct media_request *src_buf_req)
{
	struct vb2_v4l2_buffer *vb2_dst;
	enum vb2_buffer_state state;

	if (error)
		state = VB2_BUF_STATE_ERROR;
	else
		state = VB2_BUF_STATE_DONE;

	vb2_dst = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
	if (vb2_dst) {
		v4l2_m2m_buf_done(vb2_dst, state);
		mtk_v4l2_debug(2, "free frame buffer id:%d to done list",
			       vb2_dst->vb2_buf.index);
	} else {
		mtk_v4l2_err("dst buffer is NULL");
	}

	if (src_buf_req)
		v4l2_ctrl_request_complete(src_buf_req, &ctx->ctrl_hdl);
}

static struct vdec_fb *vdec_get_cap_buffer(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_video_dec_buf *framebuf;
	struct vb2_v4l2_buffer *vb2_v4l2;
	struct vb2_buffer *dst_buf;
	struct vdec_fb *pfb;

	vb2_v4l2 = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	if (!vb2_v4l2) {
		mtk_v4l2_debug(1, "[%d] dst_buf empty!!", ctx->id);
		return NULL;
	}

	dst_buf = &vb2_v4l2->vb2_buf;
	framebuf = container_of(vb2_v4l2, struct mtk_video_dec_buf, m2m_buf.vb);

	pfb = &framebuf->frame_buffer;
	pfb->base_y.va = vb2_plane_vaddr(dst_buf, 0);
	pfb->base_y.dma_addr = vb2_dma_contig_plane_dma_addr(dst_buf, 0);
	pfb->base_y.size = ctx->q_data[MTK_Q_DATA_DST].sizeimage[0];

	if (ctx->q_data[MTK_Q_DATA_DST].fmt->num_planes == 2) {
		pfb->base_c.va = vb2_plane_vaddr(dst_buf, 1);
		pfb->base_c.dma_addr =
			vb2_dma_contig_plane_dma_addr(dst_buf, 1);
		pfb->base_c.size = ctx->q_data[MTK_Q_DATA_DST].sizeimage[1];
	}
	mtk_v4l2_debug(1, "id=%d Framebuf  pfb=%p VA=%p Y_DMA=%pad C_DMA=%pad Size=%zx frame_count = %d",
		       dst_buf->index, pfb, pfb->base_y.va, &pfb->base_y.dma_addr,
		       &pfb->base_c.dma_addr, pfb->base_y.size, ctx->decoded_frame_cnt);

	return pfb;
}

static void vb2ops_vdec_buf_request_complete(struct vb2_buffer *vb)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &ctx->ctrl_hdl);
}

static void mtk_vdec_worker(struct work_struct *work)
{
	struct mtk_vcodec_ctx *ctx =
		container_of(work, struct mtk_vcodec_ctx, decode_work);
	struct mtk_vcodec_dev *dev = ctx->dev;
	struct vb2_v4l2_buffer *vb2_v4l2_src;
	struct vb2_buffer *vb2_src;
	struct mtk_vcodec_mem *bs_src;
	struct mtk_video_dec_buf *dec_buf_src;
	struct media_request *src_buf_req;
	enum vb2_buffer_state state;
	bool res_chg = false;
	int ret;

	vb2_v4l2_src = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	if (!vb2_v4l2_src) {
		v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
		mtk_v4l2_debug(1, "[%d] no available source buffer", ctx->id);
		return;
	}

	vb2_src = &vb2_v4l2_src->vb2_buf;
	dec_buf_src = container_of(vb2_v4l2_src, struct mtk_video_dec_buf,
				   m2m_buf.vb);
	bs_src = &dec_buf_src->bs_buffer;

	mtk_v4l2_debug(3, "[%d] (%d) id=%d, vb=%p", ctx->id,
		       vb2_src->vb2_queue->type, vb2_src->index, vb2_src);

	bs_src->va = vb2_plane_vaddr(vb2_src, 0);
	bs_src->dma_addr = vb2_dma_contig_plane_dma_addr(vb2_src, 0);
	bs_src->size = (size_t)vb2_src->planes[0].bytesused;
	if (!bs_src->va) {
		v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
		mtk_v4l2_err("[%d] id=%d source buffer is NULL", ctx->id,
			     vb2_src->index);
		return;
	}

	mtk_v4l2_debug(3, "[%d] Bitstream VA=%p DMA=%pad Size=%zx vb=%p",
		       ctx->id, bs_src->va, &bs_src->dma_addr, bs_src->size, vb2_src);
	/* Apply request controls. */
	src_buf_req = vb2_src->req_obj.req;
	if (src_buf_req)
		v4l2_ctrl_request_setup(src_buf_req, &ctx->ctrl_hdl);
	else
		mtk_v4l2_err("vb2 buffer media request is NULL");

	ret = vdec_if_decode(ctx, bs_src, NULL, &res_chg);
	if (ret) {
		mtk_v4l2_err(" <===[%d], src_buf[%d] sz=0x%zx pts=%llu vdec_if_decode() ret=%d res_chg=%d===>",
			     ctx->id, vb2_src->index, bs_src->size,
			     vb2_src->timestamp, ret, res_chg);
		if (ret == -EIO) {
			mutex_lock(&ctx->lock);
			dec_buf_src->error = true;
			mutex_unlock(&ctx->lock);
		}
	}

	state = ret ? VB2_BUF_STATE_ERROR : VB2_BUF_STATE_DONE;
	if (!IS_VDEC_LAT_ARCH(dev->vdec_pdata->hw_arch) ||
	    ctx->current_codec == V4L2_PIX_FMT_VP8_FRAME) {
		v4l2_m2m_buf_done_and_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx, state);
		if (src_buf_req)
			v4l2_ctrl_request_complete(src_buf_req, &ctx->ctrl_hdl);
	} else {
		if (ret != -EAGAIN) {
			v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
			v4l2_m2m_buf_done(vb2_v4l2_src, state);
		}
		v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
	}
}

static void vb2ops_vdec_stateless_buf_queue(struct vb2_buffer *vb)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vb2_v4l2 = to_vb2_v4l2_buffer(vb);

	mtk_v4l2_debug(3, "[%d] (%d) id=%d, vb=%p", ctx->id, vb->vb2_queue->type, vb->index, vb);

	mutex_lock(&ctx->lock);
	v4l2_m2m_buf_queue(ctx->m2m_ctx, vb2_v4l2);
	mutex_unlock(&ctx->lock);
	if (vb->vb2_queue->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return;

	/* If an OUTPUT buffer, we may need to update the state */
	if (ctx->state == MTK_STATE_INIT) {
		ctx->state = MTK_STATE_HEADER;
		mtk_v4l2_debug(1, "Init driver from init to header.");
	} else {
		mtk_v4l2_debug(3, "[%d] already init driver %d", ctx->id, ctx->state);
	}
}

static int mtk_vdec_flush_decoder(struct mtk_vcodec_ctx *ctx)
{
	bool res_chg;

	return vdec_if_decode(ctx, NULL, NULL, &res_chg);
}

static int mtk_vcodec_dec_ctrls_setup(struct mtk_vcodec_ctx *ctx)
{
	unsigned int i;

	v4l2_ctrl_handler_init(&ctx->ctrl_hdl, NUM_CTRLS);
	if (ctx->ctrl_hdl.error) {
		mtk_v4l2_err("v4l2_ctrl_handler_init failed\n");
		return ctx->ctrl_hdl.error;
	}

	for (i = 0; i < NUM_CTRLS; i++) {
		struct v4l2_ctrl_config cfg = mtk_stateless_controls[i].cfg;

		v4l2_ctrl_new_custom(&ctx->ctrl_hdl, &cfg, NULL);
		if (ctx->ctrl_hdl.error) {
			mtk_v4l2_err("Adding control %d failed %d", i, ctx->ctrl_hdl.error);
			return ctx->ctrl_hdl.error;
		}
	}

	v4l2_ctrl_handler_setup(&ctx->ctrl_hdl);

	return 0;
}

static int fops_media_request_validate(struct media_request *mreq)
{
	const unsigned int buffer_cnt = vb2_request_buffer_cnt(mreq);

	switch (buffer_cnt) {
	case 1:
		/* We expect exactly one buffer with the request */
		break;
	case 0:
		mtk_v4l2_debug(1, "No buffer provided with the request");
		return -ENOENT;
	default:
		mtk_v4l2_debug(1, "Too many buffers (%d) provided with the request",
			       buffer_cnt);
		return -EINVAL;
	}

	return vb2_request_validate(mreq);
}

const struct media_device_ops mtk_vcodec_media_ops = {
	.req_validate	= fops_media_request_validate,
	.req_queue	= v4l2_m2m_request_queue,
};

static void mtk_vcodec_add_formats(unsigned int fourcc,
				   struct mtk_vcodec_ctx *ctx)
{
	struct mtk_vcodec_dev *dev = ctx->dev;
	const struct mtk_vcodec_dec_pdata *pdata = dev->vdec_pdata;
	int count_formats = *pdata->num_formats;

	switch (fourcc) {
	case V4L2_PIX_FMT_H264_SLICE:
	case V4L2_PIX_FMT_VP8_FRAME:
	case V4L2_PIX_FMT_VP9_FRAME:
		mtk_video_formats[count_formats].fourcc = fourcc;
		mtk_video_formats[count_formats].type = MTK_FMT_DEC;
		mtk_video_formats[count_formats].num_planes = 1;
		mtk_video_formats[count_formats].frmsize = stepwise_fhd;

		if (!(ctx->dev->dec_capability & VCODEC_CAPABILITY_4K_DISABLED) &&
		    fourcc != V4L2_PIX_FMT_VP8_FRAME) {
			mtk_video_formats[count_formats].frmsize.max_width =
				VCODEC_DEC_4K_CODED_WIDTH;
			mtk_video_formats[count_formats].frmsize.max_height =
				VCODEC_DEC_4K_CODED_HEIGHT;
		}
		break;
	case V4L2_PIX_FMT_MM21:
	case V4L2_PIX_FMT_MT21C:
		mtk_video_formats[count_formats].fourcc = fourcc;
		mtk_video_formats[count_formats].type = MTK_FMT_FRAME;
		mtk_video_formats[count_formats].num_planes = 2;
		break;
	default:
		mtk_v4l2_err("Can not add unsupported format type");
		return;
	}

	num_formats++;
	mtk_v4l2_debug(3, "num_formats: %d dec_capability: 0x%x",
		       count_formats, ctx->dev->dec_capability);
}

static void mtk_vcodec_get_supported_formats(struct mtk_vcodec_ctx *ctx)
{
	int cap_format_count = 0, out_format_count = 0;

	if (num_formats)
		return;

	if (ctx->dev->dec_capability & MTK_VDEC_FORMAT_MT21C) {
		mtk_vcodec_add_formats(V4L2_PIX_FMT_MT21C, ctx);
		cap_format_count++;
	}
	if (ctx->dev->dec_capability & MTK_VDEC_FORMAT_MM21) {
		mtk_vcodec_add_formats(V4L2_PIX_FMT_MM21, ctx);
		cap_format_count++;
	}
	if (ctx->dev->dec_capability & MTK_VDEC_FORMAT_H264_SLICE) {
		mtk_vcodec_add_formats(V4L2_PIX_FMT_H264_SLICE, ctx);
		out_format_count++;
	}
	if (ctx->dev->dec_capability & MTK_VDEC_FORMAT_VP8_FRAME) {
		mtk_vcodec_add_formats(V4L2_PIX_FMT_VP8_FRAME, ctx);
		out_format_count++;
	}
	if (ctx->dev->dec_capability & MTK_VDEC_FORMAT_VP9_FRAME) {
		mtk_vcodec_add_formats(V4L2_PIX_FMT_VP9_FRAME, ctx);
		out_format_count++;
	}

	if (cap_format_count)
		default_cap_format = mtk_video_formats[cap_format_count - 1];
	if (out_format_count)
		default_out_format =
			mtk_video_formats[cap_format_count + out_format_count - 1];
}

static void mtk_init_vdec_params(struct mtk_vcodec_ctx *ctx)
{
	struct vb2_queue *src_vq;

	src_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
				 V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

	if (!ctx->dev->vdec_pdata->is_subdev_supported)
		ctx->dev->dec_capability |=
			MTK_VDEC_FORMAT_H264_SLICE | MTK_VDEC_FORMAT_MM21;
	mtk_vcodec_get_supported_formats(ctx);

	/* Support request api for output plane */
	src_vq->supports_requests = true;
	src_vq->requires_requests = true;
}

static int vb2ops_vdec_out_buf_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;
	return 0;
}

static struct vb2_ops mtk_vdec_request_vb2_ops = {
	.queue_setup	= vb2ops_vdec_queue_setup,
	.wait_prepare	= vb2_ops_wait_prepare,
	.wait_finish	= vb2_ops_wait_finish,
	.start_streaming	= vb2ops_vdec_start_streaming,
	.stop_streaming	= vb2ops_vdec_stop_streaming,

	.buf_queue	= vb2ops_vdec_stateless_buf_queue,
	.buf_out_validate = vb2ops_vdec_out_buf_validate,
	.buf_init	= vb2ops_vdec_buf_init,
	.buf_prepare	= vb2ops_vdec_buf_prepare,
	.buf_finish	= vb2ops_vdec_buf_finish,
	.buf_request_complete = vb2ops_vdec_buf_request_complete,
};

const struct mtk_vcodec_dec_pdata mtk_vdec_8183_pdata = {
	.init_vdec_params = mtk_init_vdec_params,
	.ctrls_setup = mtk_vcodec_dec_ctrls_setup,
	.vdec_vb2_ops = &mtk_vdec_request_vb2_ops,
	.vdec_formats = mtk_video_formats,
	.num_formats = &num_formats,
	.default_out_fmt = &default_out_format,
	.default_cap_fmt = &default_cap_format,
	.uses_stateless_api = true,
	.worker = mtk_vdec_worker,
	.flush_decoder = mtk_vdec_flush_decoder,
	.cap_to_disp = mtk_vdec_stateless_cap_to_disp,
	.get_cap_buffer = vdec_get_cap_buffer,
	.is_subdev_supported = false,
	.hw_arch = MTK_VDEC_PURE_SINGLE_CORE,
};

/* This platform data is used for one lat and one core architecture. */
const struct mtk_vcodec_dec_pdata mtk_lat_sig_core_pdata = {
	.init_vdec_params = mtk_init_vdec_params,
	.ctrls_setup = mtk_vcodec_dec_ctrls_setup,
	.vdec_vb2_ops = &mtk_vdec_request_vb2_ops,
	.vdec_formats = mtk_video_formats,
	.num_formats = &num_formats,
	.default_out_fmt = &default_out_format,
	.default_cap_fmt = &default_cap_format,
	.uses_stateless_api = true,
	.worker = mtk_vdec_worker,
	.flush_decoder = mtk_vdec_flush_decoder,
	.cap_to_disp = mtk_vdec_stateless_cap_to_disp,
	.get_cap_buffer = vdec_get_cap_buffer,
	.is_subdev_supported = true,
	.hw_arch = MTK_VDEC_LAT_SINGLE_CORE,
};

const struct mtk_vcodec_dec_pdata mtk_vdec_single_core_pdata = {
	.init_vdec_params = mtk_init_vdec_params,
	.ctrls_setup = mtk_vcodec_dec_ctrls_setup,
	.vdec_vb2_ops = &mtk_vdec_request_vb2_ops,
	.vdec_formats = mtk_video_formats,
	.num_formats = &num_formats,
	.default_out_fmt = &default_out_format,
	.default_cap_fmt = &default_cap_format,
	.uses_stateless_api = true,
	.worker = mtk_vdec_worker,
	.flush_decoder = mtk_vdec_flush_decoder,
	.cap_to_disp = mtk_vdec_stateless_cap_to_disp,
	.get_cap_buffer = vdec_get_cap_buffer,
	.is_subdev_supported = true,
	.hw_arch = MTK_VDEC_PURE_SINGLE_CORE,
};
