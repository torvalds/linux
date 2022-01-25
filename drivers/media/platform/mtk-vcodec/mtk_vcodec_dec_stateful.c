// SPDX-License-Identifier: GPL-2.0

#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_dec.h"
#include "mtk_vcodec_intr.h"
#include "mtk_vcodec_util.h"
#include "mtk_vcodec_dec_pm.h"
#include "vdec_drv_if.h"

static const struct mtk_video_fmt mtk_video_formats[] = {
	{
		.fourcc = V4L2_PIX_FMT_H264,
		.type = MTK_FMT_DEC,
		.num_planes = 1,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,
	},
	{
		.fourcc = V4L2_PIX_FMT_VP8,
		.type = MTK_FMT_DEC,
		.num_planes = 1,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,
	},
	{
		.fourcc = V4L2_PIX_FMT_VP9,
		.type = MTK_FMT_DEC,
		.num_planes = 1,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,
	},
	{
		.fourcc = V4L2_PIX_FMT_MT21C,
		.type = MTK_FMT_FRAME,
		.num_planes = 2,
	},
};

#define NUM_FORMATS ARRAY_SIZE(mtk_video_formats)
#define DEFAULT_OUT_FMT_IDX 0
#define DEFAULT_CAP_FMT_IDX 3

static const struct mtk_codec_framesizes mtk_vdec_framesizes[] = {
	{
		.fourcc = V4L2_PIX_FMT_H264,
		.stepwise = { MTK_VDEC_MIN_W, MTK_VDEC_MAX_W, 16,
			      MTK_VDEC_MIN_H, MTK_VDEC_MAX_H, 16 },
	},
	{
		.fourcc = V4L2_PIX_FMT_VP8,
		.stepwise = { MTK_VDEC_MIN_W, MTK_VDEC_MAX_W, 16,
			      MTK_VDEC_MIN_H, MTK_VDEC_MAX_H, 16 },
	},
	{
		.fourcc = V4L2_PIX_FMT_VP9,
		.stepwise = { MTK_VDEC_MIN_W, MTK_VDEC_MAX_W, 16,
			      MTK_VDEC_MIN_H, MTK_VDEC_MAX_H, 16 },
	},
};

#define NUM_SUPPORTED_FRAMESIZE ARRAY_SIZE(mtk_vdec_framesizes)

/*
 * This function tries to clean all display buffers, the buffers will return
 * in display order.
 * Note the buffers returned from codec driver may still be in driver's
 * reference list.
 */
static struct vb2_buffer *get_display_buffer(struct mtk_vcodec_ctx *ctx)
{
	struct vdec_fb *disp_frame_buffer = NULL;
	struct mtk_video_dec_buf *dstbuf;
	struct vb2_v4l2_buffer *vb;

	mtk_v4l2_debug(3, "[%d]", ctx->id);
	if (vdec_if_get_param(ctx, GET_PARAM_DISP_FRAME_BUFFER,
			      &disp_frame_buffer)) {
		mtk_v4l2_err("[%d]Cannot get param : GET_PARAM_DISP_FRAME_BUFFER", ctx->id);
		return NULL;
	}

	if (!disp_frame_buffer) {
		mtk_v4l2_debug(3, "No display frame buffer");
		return NULL;
	}

	dstbuf = container_of(disp_frame_buffer, struct mtk_video_dec_buf,
			      frame_buffer);
	vb = &dstbuf->m2m_buf.vb;
	mutex_lock(&ctx->lock);
	if (dstbuf->used) {
		vb2_set_plane_payload(&vb->vb2_buf, 0, ctx->picinfo.fb_sz[0]);
		if (ctx->q_data[MTK_Q_DATA_DST].fmt->num_planes == 2)
			vb2_set_plane_payload(&vb->vb2_buf, 1,
					      ctx->picinfo.fb_sz[1]);

		mtk_v4l2_debug(2, "[%d]status=%x queue id=%d to done_list %d",
			       ctx->id, disp_frame_buffer->status,
			       vb->vb2_buf.index, dstbuf->queued_in_vb2);

		v4l2_m2m_buf_done(vb, VB2_BUF_STATE_DONE);
		ctx->decoded_frame_cnt++;
	}
	mutex_unlock(&ctx->lock);
	return &vb->vb2_buf;
}

/*
 * This function tries to clean all capture buffers that are not used as
 * reference buffers by codec driver any more
 * In this case, we need re-queue buffer to vb2 buffer if user space
 * already returns this buffer to v4l2 or this buffer is just the output of
 * previous sps/pps/resolution change decode, or do nothing if user
 * space still owns this buffer
 */
static struct vb2_buffer *get_free_buffer(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_video_dec_buf *dstbuf;
	struct vdec_fb *free_frame_buffer = NULL;
	struct vb2_v4l2_buffer *vb;

	if (vdec_if_get_param(ctx, GET_PARAM_FREE_FRAME_BUFFER,
			      &free_frame_buffer)) {
		mtk_v4l2_err("[%d] Error!! Cannot get param", ctx->id);
		return NULL;
	}
	if (!free_frame_buffer) {
		mtk_v4l2_debug(3, " No free frame buffer");
		return NULL;
	}

	mtk_v4l2_debug(3, "[%d] tmp_frame_addr = 0x%p", ctx->id,
		       free_frame_buffer);

	dstbuf = container_of(free_frame_buffer, struct mtk_video_dec_buf,
			      frame_buffer);
	vb = &dstbuf->m2m_buf.vb;

	mutex_lock(&ctx->lock);
	if (dstbuf->used) {
		if (dstbuf->queued_in_vb2 && dstbuf->queued_in_v4l2 &&
		    free_frame_buffer->status == FB_ST_FREE) {
			/*
			 * After decode sps/pps or non-display buffer, we don't
			 * need to return capture buffer to user space, but
			 * just re-queue this capture buffer to vb2 queue.
			 * This reduce overheads that dq/q unused capture
			 * buffer. In this case, queued_in_vb2 = true.
			 */
			mtk_v4l2_debug(2, "[%d]status=%x queue id=%d to rdy_queue %d",
				       ctx->id, free_frame_buffer->status,
				       vb->vb2_buf.index, dstbuf->queued_in_vb2);
			v4l2_m2m_buf_queue(ctx->m2m_ctx, vb);
		} else if (!dstbuf->queued_in_vb2 && dstbuf->queued_in_v4l2) {
			/*
			 * If buffer in v4l2 driver but not in vb2 queue yet,
			 * and we get this buffer from free_list, it means
			 * that codec driver do not use this buffer as
			 * reference buffer anymore. We should q buffer to vb2
			 * queue, so later work thread could get this buffer
			 * for decode. In this case, queued_in_vb2 = false
			 * means this buffer is not from previous decode
			 * output.
			 */
			mtk_v4l2_debug(2,
				       "[%d]status=%x queue id=%d to rdy_queue",
				       ctx->id, free_frame_buffer->status,
				       vb->vb2_buf.index);
			v4l2_m2m_buf_queue(ctx->m2m_ctx, vb);
			dstbuf->queued_in_vb2 = true;
		} else {
			/*
			 * Codec driver do not need to reference this capture
			 * buffer and this buffer is not in v4l2 driver.
			 * Then we don't need to do any thing, just add log when
			 * we need to debug buffer flow.
			 * When this buffer q from user space, it could
			 * directly q to vb2 buffer
			 */
			mtk_v4l2_debug(3, "[%d]status=%x err queue id=%d %d %d",
				       ctx->id, free_frame_buffer->status,
				       vb->vb2_buf.index, dstbuf->queued_in_vb2,
				       dstbuf->queued_in_v4l2);
		}
		dstbuf->used = false;
	}
	mutex_unlock(&ctx->lock);
	return &vb->vb2_buf;
}

static void clean_display_buffer(struct mtk_vcodec_ctx *ctx)
{
	while (get_display_buffer(ctx))
		;
}

static void clean_free_buffer(struct mtk_vcodec_ctx *ctx)
{
	while (get_free_buffer(ctx))
		;
}

static void mtk_vdec_queue_res_chg_event(struct mtk_vcodec_ctx *ctx)
{
	static const struct v4l2_event ev_src_ch = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};

	mtk_v4l2_debug(1, "[%d]", ctx->id);
	v4l2_event_queue_fh(&ctx->fh, &ev_src_ch);
}

static int mtk_vdec_flush_decoder(struct mtk_vcodec_ctx *ctx)
{
	bool res_chg;
	int ret;

	ret = vdec_if_decode(ctx, NULL, NULL, &res_chg);
	if (ret)
		mtk_v4l2_err("DecodeFinal failed, ret=%d", ret);

	clean_display_buffer(ctx);
	clean_free_buffer(ctx);

	return 0;
}

static void mtk_vdec_update_fmt(struct mtk_vcodec_ctx *ctx,
				unsigned int pixelformat)
{
	const struct mtk_video_fmt *fmt;
	struct mtk_q_data *dst_q_data;
	unsigned int k;

	dst_q_data = &ctx->q_data[MTK_Q_DATA_DST];
	for (k = 0; k < NUM_FORMATS; k++) {
		fmt = &mtk_video_formats[k];
		if (fmt->fourcc == pixelformat) {
			mtk_v4l2_debug(1, "Update cap fourcc(%d -> %d)",
				       dst_q_data->fmt->fourcc, pixelformat);
			dst_q_data->fmt = fmt;
			return;
		}
	}

	mtk_v4l2_err("Cannot get fourcc(%d), using init value", pixelformat);
}

static int mtk_vdec_pic_info_update(struct mtk_vcodec_ctx *ctx)
{
	unsigned int dpbsize = 0;
	int ret;

	if (vdec_if_get_param(ctx, GET_PARAM_PIC_INFO,
			      &ctx->last_decoded_picinfo)) {
		mtk_v4l2_err("[%d]Error!! Cannot get param : GET_PARAM_PICTURE_INFO ERR", ctx->id);
		return -EINVAL;
	}

	if (ctx->last_decoded_picinfo.pic_w == 0 ||
	    ctx->last_decoded_picinfo.pic_h == 0 ||
	    ctx->last_decoded_picinfo.buf_w == 0 ||
	    ctx->last_decoded_picinfo.buf_h == 0) {
		mtk_v4l2_err("Cannot get correct pic info");
		return -EINVAL;
	}

	if (ctx->last_decoded_picinfo.cap_fourcc != ctx->picinfo.cap_fourcc &&
	    ctx->picinfo.cap_fourcc != 0)
		mtk_vdec_update_fmt(ctx, ctx->picinfo.cap_fourcc);

	if (ctx->last_decoded_picinfo.pic_w == ctx->picinfo.pic_w ||
	    ctx->last_decoded_picinfo.pic_h == ctx->picinfo.pic_h)
		return 0;

	mtk_v4l2_debug(1, "[%d]-> new(%d,%d), old(%d,%d), real(%d,%d)", ctx->id,
		       ctx->last_decoded_picinfo.pic_w,
		       ctx->last_decoded_picinfo.pic_h, ctx->picinfo.pic_w,
		       ctx->picinfo.pic_h, ctx->last_decoded_picinfo.buf_w,
		       ctx->last_decoded_picinfo.buf_h);

	ret = vdec_if_get_param(ctx, GET_PARAM_DPB_SIZE, &dpbsize);
	if (dpbsize == 0)
		mtk_v4l2_err("Incorrect dpb size, ret=%d", ret);

	ctx->dpb_size = dpbsize;

	return ret;
}

static void mtk_vdec_worker(struct work_struct *work)
{
	struct mtk_vcodec_ctx *ctx =
		container_of(work, struct mtk_vcodec_ctx, decode_work);
	struct mtk_vcodec_dev *dev = ctx->dev;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct mtk_vcodec_mem buf;
	struct vdec_fb *pfb;
	bool res_chg = false;
	int ret;
	struct mtk_video_dec_buf *dst_buf_info, *src_buf_info;

	src_buf = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	if (!src_buf) {
		v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
		mtk_v4l2_debug(1, "[%d] src_buf empty!!", ctx->id);
		return;
	}

	dst_buf = v4l2_m2m_next_dst_buf(ctx->m2m_ctx);
	if (!dst_buf) {
		v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
		mtk_v4l2_debug(1, "[%d] dst_buf empty!!", ctx->id);
		return;
	}

	dst_buf_info =
		container_of(dst_buf, struct mtk_video_dec_buf, m2m_buf.vb);

	pfb = &dst_buf_info->frame_buffer;
	pfb->base_y.va = vb2_plane_vaddr(&dst_buf->vb2_buf, 0);
	pfb->base_y.dma_addr =
		vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	pfb->base_y.size = ctx->picinfo.fb_sz[0];

	pfb->base_c.va = vb2_plane_vaddr(&dst_buf->vb2_buf, 1);
	pfb->base_c.dma_addr =
		vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 1);
	pfb->base_c.size = ctx->picinfo.fb_sz[1];
	pfb->status = 0;
	mtk_v4l2_debug(3, "===>[%d] vdec_if_decode() ===>", ctx->id);

	mtk_v4l2_debug(3,
		       "id=%d Framebuf  pfb=%p VA=%p Y_DMA=%pad C_DMA=%pad Size=%zx",
		       dst_buf->vb2_buf.index, pfb, pfb->base_y.va,
		       &pfb->base_y.dma_addr, &pfb->base_c.dma_addr, pfb->base_y.size);

	if (src_buf == &ctx->empty_flush_buf.vb) {
		mtk_v4l2_debug(1, "Got empty flush input buffer.");
		src_buf = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);

		/* update dst buf status */
		dst_buf = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
		mutex_lock(&ctx->lock);
		dst_buf_info->used = false;
		mutex_unlock(&ctx->lock);

		vdec_if_decode(ctx, NULL, NULL, &res_chg);
		clean_display_buffer(ctx);
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, 0);
		if (ctx->q_data[MTK_Q_DATA_DST].fmt->num_planes == 2)
			vb2_set_plane_payload(&dst_buf->vb2_buf, 1, 0);
		dst_buf->flags |= V4L2_BUF_FLAG_LAST;
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);
		clean_free_buffer(ctx);
		v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
		return;
	}

	src_buf_info =
		container_of(src_buf, struct mtk_video_dec_buf, m2m_buf.vb);

	buf.va = vb2_plane_vaddr(&src_buf->vb2_buf, 0);
	buf.dma_addr = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	buf.size = (size_t)src_buf->vb2_buf.planes[0].bytesused;
	if (!buf.va) {
		v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
		mtk_v4l2_err("[%d] id=%d src_addr is NULL!!", ctx->id,
			     src_buf->vb2_buf.index);
		return;
	}
	mtk_v4l2_debug(3, "[%d] Bitstream VA=%p DMA=%pad Size=%zx vb=%p",
		       ctx->id, buf.va, &buf.dma_addr, buf.size, src_buf);
	dst_buf->vb2_buf.timestamp = src_buf->vb2_buf.timestamp;
	dst_buf->timecode = src_buf->timecode;
	mutex_lock(&ctx->lock);
	dst_buf_info->used = true;
	mutex_unlock(&ctx->lock);
	src_buf_info->used = true;

	ret = vdec_if_decode(ctx, &buf, pfb, &res_chg);

	if (ret) {
		mtk_v4l2_err(" <===[%d], src_buf[%d] sz=0x%zx pts=%llu dst_buf[%d] vdec_if_decode() ret=%d res_chg=%d===>",
			     ctx->id, src_buf->vb2_buf.index, buf.size,
			     src_buf->vb2_buf.timestamp, dst_buf->vb2_buf.index, ret, res_chg);
		src_buf = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		if (ret == -EIO) {
			mutex_lock(&ctx->lock);
			src_buf_info->error = true;
			mutex_unlock(&ctx->lock);
		}
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
	} else if (!res_chg) {
		/*
		 * we only return src buffer with VB2_BUF_STATE_DONE
		 * when decode success without resolution change
		 */
		src_buf = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
	}

	dst_buf = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
	clean_display_buffer(ctx);
	clean_free_buffer(ctx);

	if (!ret && res_chg) {
		mtk_vdec_pic_info_update(ctx);
		/*
		 * On encountering a resolution change in the stream.
		 * The driver must first process and decode all
		 * remaining buffers from before the resolution change
		 * point, so call flush decode here
		 */
		mtk_vdec_flush_decoder(ctx);
		/*
		 * After all buffers containing decoded frames from
		 * before the resolution change point ready to be
		 * dequeued on the CAPTURE queue, the driver sends a
		 * V4L2_EVENT_SOURCE_CHANGE event for source change
		 * type V4L2_EVENT_SRC_CH_RESOLUTION
		 */
		mtk_vdec_queue_res_chg_event(ctx);
	}
	v4l2_m2m_job_finish(dev->m2m_dev_dec, ctx->m2m_ctx);
}

static void vb2ops_vdec_stateful_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *src_buf;
	struct mtk_vcodec_mem src_mem;
	bool res_chg = false;
	int ret;
	unsigned int dpbsize = 1, i;
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vb2_v4l2;
	struct mtk_q_data *dst_q_data;

	mtk_v4l2_debug(3, "[%d] (%d) id=%d, vb=%p", ctx->id,
		       vb->vb2_queue->type, vb->index, vb);
	/*
	 * check if this buffer is ready to be used after decode
	 */
	if (vb->vb2_queue->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		struct mtk_video_dec_buf *buf;

		vb2_v4l2 = to_vb2_v4l2_buffer(vb);
		buf = container_of(vb2_v4l2, struct mtk_video_dec_buf,
				   m2m_buf.vb);
		mutex_lock(&ctx->lock);
		if (!buf->used) {
			v4l2_m2m_buf_queue(ctx->m2m_ctx, vb2_v4l2);
			buf->queued_in_vb2 = true;
			buf->queued_in_v4l2 = true;
		} else {
			buf->queued_in_vb2 = false;
			buf->queued_in_v4l2 = true;
		}
		mutex_unlock(&ctx->lock);
		return;
	}

	v4l2_m2m_buf_queue(ctx->m2m_ctx, to_vb2_v4l2_buffer(vb));

	if (ctx->state != MTK_STATE_INIT) {
		mtk_v4l2_debug(3, "[%d] already init driver %d", ctx->id,
			       ctx->state);
		return;
	}

	src_buf = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	if (!src_buf) {
		mtk_v4l2_err("No src buffer");
		return;
	}

	if (src_buf == &ctx->empty_flush_buf.vb) {
		/* This shouldn't happen. Just in case. */
		mtk_v4l2_err("Invalid flush buffer.");
		v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		return;
	}

	src_mem.va = vb2_plane_vaddr(&src_buf->vb2_buf, 0);
	src_mem.dma_addr = vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
	src_mem.size = (size_t)src_buf->vb2_buf.planes[0].bytesused;
	mtk_v4l2_debug(2, "[%d] buf id=%d va=%p dma=%pad size=%zx", ctx->id,
		       src_buf->vb2_buf.index, src_mem.va, &src_mem.dma_addr,
		       src_mem.size);

	ret = vdec_if_decode(ctx, &src_mem, NULL, &res_chg);
	if (ret || !res_chg) {
		/*
		 * fb == NULL means to parse SPS/PPS header or
		 * resolution info in src_mem. Decode can fail
		 * if there is no SPS header or picture info
		 * in bs
		 */

		src_buf = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);
		if (ret == -EIO) {
			mtk_v4l2_err("[%d] Unrecoverable error in vdec_if_decode.", ctx->id);
			ctx->state = MTK_STATE_ABORT;
			v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
		} else {
			v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
		}
		mtk_v4l2_debug(ret ? 0 : 1,
			       "[%d] vdec_if_decode() src_buf=%d, size=%zu, fail=%d, res_chg=%d",
			       ctx->id, src_buf->vb2_buf.index, src_mem.size, ret, res_chg);
		return;
	}

	if (vdec_if_get_param(ctx, GET_PARAM_PIC_INFO, &ctx->picinfo)) {
		mtk_v4l2_err("[%d]Error!! Cannot get param : GET_PARAM_PICTURE_INFO ERR", ctx->id);
		return;
	}

	ctx->last_decoded_picinfo = ctx->picinfo;
	dst_q_data = &ctx->q_data[MTK_Q_DATA_DST];
	for (i = 0; i < dst_q_data->fmt->num_planes; i++) {
		dst_q_data->sizeimage[i] = ctx->picinfo.fb_sz[i];
		dst_q_data->bytesperline[i] = ctx->picinfo.buf_w;
	}

	mtk_v4l2_debug(2, "[%d] vdec_if_init() OK wxh=%dx%d pic wxh=%dx%d sz[0]=0x%x sz[1]=0x%x",
		       ctx->id, ctx->picinfo.buf_w, ctx->picinfo.buf_h, ctx->picinfo.pic_w,
		       ctx->picinfo.pic_h, dst_q_data->sizeimage[0], dst_q_data->sizeimage[1]);

	ret = vdec_if_get_param(ctx, GET_PARAM_DPB_SIZE, &dpbsize);
	if (dpbsize == 0)
		mtk_v4l2_err("[%d] GET_PARAM_DPB_SIZE fail=%d", ctx->id, ret);

	ctx->dpb_size = dpbsize;
	ctx->state = MTK_STATE_HEADER;
	mtk_v4l2_debug(1, "[%d] dpbsize=%d", ctx->id, ctx->dpb_size);

	mtk_vdec_queue_res_chg_event(ctx);
}

static int mtk_vdec_g_v_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mtk_vcodec_ctx *ctx = ctrl_to_ctx(ctrl);
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_MIN_BUFFERS_FOR_CAPTURE:
		if (ctx->state >= MTK_STATE_HEADER) {
			ctrl->val = ctx->dpb_size;
		} else {
			mtk_v4l2_debug(0, "Seqinfo not ready");
			ctrl->val = 0;
		}
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static const struct v4l2_ctrl_ops mtk_vcodec_dec_ctrl_ops = {
	.g_volatile_ctrl = mtk_vdec_g_v_ctrl,
};

static int mtk_vcodec_dec_ctrls_setup(struct mtk_vcodec_ctx *ctx)
{
	struct v4l2_ctrl *ctrl;

	v4l2_ctrl_handler_init(&ctx->ctrl_hdl, 1);

	ctrl = v4l2_ctrl_new_std(&ctx->ctrl_hdl, &mtk_vcodec_dec_ctrl_ops,
				 V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 0, 32, 1, 1);
	ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;
	v4l2_ctrl_new_std_menu(&ctx->ctrl_hdl, &mtk_vcodec_dec_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_VP9_PROFILE,
			       V4L2_MPEG_VIDEO_VP9_PROFILE_0, 0,
			       V4L2_MPEG_VIDEO_VP9_PROFILE_0);
	/*
	 * H264. Baseline / Extended decoding is not supported.
	 */
	v4l2_ctrl_new_std_menu(&ctx->ctrl_hdl, &mtk_vcodec_dec_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_H264_PROFILE, V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
			       BIT(V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE) |
			       BIT(V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED),
			       V4L2_MPEG_VIDEO_H264_PROFILE_MAIN);

	if (ctx->ctrl_hdl.error) {
		mtk_v4l2_err("Adding control failed %d", ctx->ctrl_hdl.error);
		return ctx->ctrl_hdl.error;
	}

	v4l2_ctrl_handler_setup(&ctx->ctrl_hdl);
	return 0;
}

static void mtk_init_vdec_params(struct mtk_vcodec_ctx *ctx)
{
}

static struct vb2_ops mtk_vdec_frame_vb2_ops = {
	.queue_setup = vb2ops_vdec_queue_setup,
	.buf_prepare = vb2ops_vdec_buf_prepare,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.start_streaming = vb2ops_vdec_start_streaming,

	.buf_queue = vb2ops_vdec_stateful_buf_queue,
	.buf_init = vb2ops_vdec_buf_init,
	.buf_finish = vb2ops_vdec_buf_finish,
	.stop_streaming = vb2ops_vdec_stop_streaming,
};

const struct mtk_vcodec_dec_pdata mtk_vdec_8173_pdata = {
	.chip = MTK_MT8173,
	.init_vdec_params = mtk_init_vdec_params,
	.ctrls_setup = mtk_vcodec_dec_ctrls_setup,
	.vdec_vb2_ops = &mtk_vdec_frame_vb2_ops,
	.vdec_formats = mtk_video_formats,
	.num_formats = NUM_FORMATS,
	.default_out_fmt = &mtk_video_formats[DEFAULT_OUT_FMT_IDX],
	.default_cap_fmt = &mtk_video_formats[DEFAULT_CAP_FMT_IDX],
	.vdec_framesizes = mtk_vdec_framesizes,
	.num_framesizes = NUM_SUPPORTED_FRAMESIZE,
	.worker = mtk_vdec_worker,
	.flush_decoder = mtk_vdec_flush_decoder,
};
