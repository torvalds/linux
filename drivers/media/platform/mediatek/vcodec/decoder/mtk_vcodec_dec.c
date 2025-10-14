// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Author: PC Chen <pc.chen@mediatek.com>
 *         Tiffany Lin <tiffany.lin@mediatek.com>
 */

#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "mtk_vcodec_dec_drv.h"
#include "mtk_vcodec_dec.h"
#include "vdec_drv_if.h"
#include "mtk_vcodec_dec_pm.h"

#define DFT_CFG_WIDTH	MTK_VDEC_MIN_W
#define DFT_CFG_HEIGHT	MTK_VDEC_MIN_H

static const struct mtk_video_fmt *
mtk_vdec_find_format(struct v4l2_format *f,
		     const struct mtk_vcodec_dec_pdata *dec_pdata)
{
	const struct mtk_video_fmt *fmt;
	unsigned int k;

	for (k = 0; k < *dec_pdata->num_formats; k++) {
		fmt = &dec_pdata->vdec_formats[k];
		if (fmt->fourcc == f->fmt.pix_mp.pixelformat)
			return fmt;
	}

	return NULL;
}

static bool mtk_vdec_get_cap_fmt(struct mtk_vcodec_dec_ctx *ctx, int format_index)
{
	const struct mtk_vcodec_dec_pdata *dec_pdata = ctx->dev->vdec_pdata;
	const struct mtk_video_fmt *fmt;
	struct mtk_q_data *q_data;
	int num_frame_count = 0, i;
	bool ret = false;

	fmt = &dec_pdata->vdec_formats[format_index];
	for (i = 0; i < *dec_pdata->num_formats; i++) {
		if (dec_pdata->vdec_formats[i].type != MTK_FMT_FRAME)
			continue;

		num_frame_count++;
	}

	if (num_frame_count == 1 || (!ctx->is_10bit_bitstream && fmt->fourcc == V4L2_PIX_FMT_MM21))
		return true;

	q_data = &ctx->q_data[MTK_Q_DATA_SRC];
	switch (q_data->fmt->fourcc) {
	case V4L2_PIX_FMT_H264_SLICE:
		if (ctx->is_10bit_bitstream && fmt->fourcc == V4L2_PIX_FMT_MT2110R)
			ret = true;
		break;
	case V4L2_PIX_FMT_VP9_FRAME:
	case V4L2_PIX_FMT_AV1_FRAME:
	case V4L2_PIX_FMT_HEVC_SLICE:
		if (ctx->is_10bit_bitstream && fmt->fourcc == V4L2_PIX_FMT_MT2110T)
			ret = true;
		break;
	default:
		break;
	}

	return ret;
}

static struct mtk_q_data *mtk_vdec_get_q_data(struct mtk_vcodec_dec_ctx *ctx,
					      enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return &ctx->q_data[MTK_Q_DATA_SRC];

	return &ctx->q_data[MTK_Q_DATA_DST];
}

static int stateful_try_decoder_cmd(struct file *file, void *priv, struct v4l2_decoder_cmd *cmd)
{
	return v4l2_m2m_ioctl_try_decoder_cmd(file, priv, cmd);
}

static int stateful_decoder_cmd(struct file *file, void *priv, struct v4l2_decoder_cmd *cmd)
{
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);
	struct vb2_queue *src_vq, *dst_vq;
	int ret;

	ret = stateful_try_decoder_cmd(file, priv, cmd);
	if (ret)
		return ret;

	mtk_v4l2_vdec_dbg(1, ctx, "decoder cmd=%u", cmd->cmd);
	dst_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
				V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	switch (cmd->cmd) {
	case V4L2_DEC_CMD_STOP:
		src_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
				V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
		if (!vb2_is_streaming(src_vq)) {
			mtk_v4l2_vdec_dbg(1, ctx, "Output stream is off. No need to flush.");
			return 0;
		}
		if (!vb2_is_streaming(dst_vq)) {
			mtk_v4l2_vdec_dbg(1, ctx, "Capture stream is off. No need to flush.");
			return 0;
		}
		v4l2_m2m_buf_queue(ctx->m2m_ctx, &ctx->empty_flush_buf.vb);
		v4l2_m2m_try_schedule(ctx->m2m_ctx);
		break;

	case V4L2_DEC_CMD_START:
		vb2_clear_last_buffer_dequeued(dst_vq);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int stateless_try_decoder_cmd(struct file *file, void *priv, struct v4l2_decoder_cmd *cmd)
{
	return v4l2_m2m_ioctl_stateless_try_decoder_cmd(file, priv, cmd);
}

static int stateless_decoder_cmd(struct file *file, void *priv, struct v4l2_decoder_cmd *cmd)
{
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);
	int ret;

	ret = v4l2_m2m_ioctl_stateless_try_decoder_cmd(file, priv, cmd);
	if (ret)
		return ret;

	mtk_v4l2_vdec_dbg(3, ctx, "decoder cmd=%u", cmd->cmd);
	switch (cmd->cmd) {
	case V4L2_DEC_CMD_FLUSH:
		/*
		 * If the flag of the output buffer is equals V4L2_BUF_FLAG_M2M_HOLD_CAPTURE_BUF,
		 * this command will prevent dequeueing the capture buffer containing the last
		 * decoded frame. Or do nothing
		 */
		break;
	default:
		mtk_v4l2_vdec_err(ctx, "invalid stateless decoder cmd=%u", cmd->cmd);
		return -EINVAL;
	}

	return 0;
}

static int vidioc_try_decoder_cmd(struct file *file, void *priv, struct v4l2_decoder_cmd *cmd)
{
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);

	if (ctx->dev->vdec_pdata->uses_stateless_api)
		return stateless_try_decoder_cmd(file, priv, cmd);

	return stateful_try_decoder_cmd(file, priv, cmd);
}

static int vidioc_decoder_cmd(struct file *file, void *priv, struct v4l2_decoder_cmd *cmd)
{
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);

	if (ctx->dev->vdec_pdata->uses_stateless_api)
		return stateless_decoder_cmd(file, priv, cmd);

	return stateful_decoder_cmd(file, priv, cmd);
}

void mtk_vdec_unlock(struct mtk_vcodec_dec_ctx *ctx)
{
	mutex_unlock(&ctx->dev->dec_mutex[ctx->hw_id]);
}

void mtk_vdec_lock(struct mtk_vcodec_dec_ctx *ctx)
{
	mutex_lock(&ctx->dev->dec_mutex[ctx->hw_id]);
}

void mtk_vcodec_dec_release(struct mtk_vcodec_dec_ctx *ctx)
{
	vdec_if_deinit(ctx);
	ctx->state = MTK_STATE_FREE;
}

void mtk_vcodec_dec_set_default_params(struct mtk_vcodec_dec_ctx *ctx)
{
	struct mtk_q_data *q_data;

	ctx->m2m_ctx->q_lock = &ctx->dev->dev_mutex;
	ctx->fh.m2m_ctx = ctx->m2m_ctx;
	ctx->fh.ctrl_handler = &ctx->ctrl_hdl;
	INIT_WORK(&ctx->decode_work, ctx->dev->vdec_pdata->worker);
	ctx->colorspace = V4L2_COLORSPACE_REC709;
	ctx->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	ctx->quantization = V4L2_QUANTIZATION_DEFAULT;
	ctx->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	q_data = &ctx->q_data[MTK_Q_DATA_SRC];
	memset(q_data, 0, sizeof(struct mtk_q_data));
	q_data->visible_width = DFT_CFG_WIDTH;
	q_data->visible_height = DFT_CFG_HEIGHT;
	q_data->fmt = ctx->dev->vdec_pdata->default_out_fmt;
	q_data->field = V4L2_FIELD_NONE;

	q_data->sizeimage[0] = DFT_CFG_WIDTH * DFT_CFG_HEIGHT;
	q_data->bytesperline[0] = 0;

	q_data = &ctx->q_data[MTK_Q_DATA_DST];
	memset(q_data, 0, sizeof(struct mtk_q_data));
	q_data->visible_width = DFT_CFG_WIDTH;
	q_data->visible_height = DFT_CFG_HEIGHT;
	q_data->coded_width = DFT_CFG_WIDTH;
	q_data->coded_height = DFT_CFG_HEIGHT;
	q_data->fmt = ctx->dev->vdec_pdata->default_cap_fmt;
	q_data->field = V4L2_FIELD_NONE;

	q_data->sizeimage[0] = q_data->coded_width * q_data->coded_height;
	q_data->bytesperline[0] = q_data->coded_width;
	q_data->sizeimage[1] = q_data->sizeimage[0] / 2;
	q_data->bytesperline[1] = q_data->coded_width;
}

static int vidioc_vdec_qbuf(struct file *file, void *priv,
			    struct v4l2_buffer *buf)
{
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);

	if (ctx->state == MTK_STATE_ABORT) {
		mtk_v4l2_vdec_err(ctx, "[%d] Call on QBUF after unrecoverable error", ctx->id);
		return -EIO;
	}

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_vdec_dqbuf(struct file *file, void *priv,
			     struct v4l2_buffer *buf)
{
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);

	if (ctx->state == MTK_STATE_ABORT) {
		mtk_v4l2_vdec_err(ctx, "[%d] Call on DQBUF after unrecoverable error", ctx->id);
		return -EIO;
	}

	return v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_vdec_querycap(struct file *file, void *priv,
				struct v4l2_capability *cap)
{
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);
	struct device *dev = &ctx->dev->plat_dev->dev;

	strscpy(cap->driver, dev->driver->name, sizeof(cap->driver));
	snprintf(cap->card, sizeof(cap->card), "MT%d video decoder", ctx->dev->chip_name);

	return 0;
}

static int vidioc_vdec_subscribe_evt(struct v4l2_fh *fh,
				     const struct v4l2_event_subscription *sub)
{
	struct mtk_vcodec_dec_ctx *ctx = fh_to_dec_ctx(fh);

	if (ctx->dev->vdec_pdata->uses_stateless_api)
		return v4l2_ctrl_subscribe_event(fh, sub);

	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 2, NULL);
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	default:
		return v4l2_ctrl_subscribe_event(fh, sub);
	}
}

static int vidioc_try_fmt(struct mtk_vcodec_dec_ctx *ctx, struct v4l2_format *f,
			  const struct mtk_video_fmt *fmt)
{
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;
	const struct v4l2_frmsize_stepwise *frmsize;

	pix_fmt_mp->field = V4L2_FIELD_NONE;

	/* Always apply frame size constraints from the coded side */
	if (V4L2_TYPE_IS_OUTPUT(f->type))
		frmsize = &fmt->frmsize;
	else
		frmsize = &ctx->q_data[MTK_Q_DATA_SRC].fmt->frmsize;

	pix_fmt_mp->width = clamp(pix_fmt_mp->width, MTK_VDEC_MIN_W, frmsize->max_width);
	pix_fmt_mp->height = clamp(pix_fmt_mp->height, MTK_VDEC_MIN_H, frmsize->max_height);

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		pix_fmt_mp->num_planes = 1;
		pix_fmt_mp->plane_fmt[0].bytesperline = 0;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		int tmp_w, tmp_h;

		/*
		 * Find next closer width align 64, height align 64, size align
		 * 64 rectangle
		 * Note: This only get default value, the real HW needed value
		 *       only available when ctx in MTK_STATE_HEADER state
		 */
		tmp_w = pix_fmt_mp->width;
		tmp_h = pix_fmt_mp->height;
		v4l_bound_align_image(&pix_fmt_mp->width, MTK_VDEC_MIN_W, frmsize->max_width, 6,
				      &pix_fmt_mp->height, MTK_VDEC_MIN_H, frmsize->max_height, 6,
				      9);

		if (pix_fmt_mp->width < tmp_w &&
		    (pix_fmt_mp->width + 64) <= frmsize->max_width)
			pix_fmt_mp->width += 64;
		if (pix_fmt_mp->height < tmp_h &&
		    (pix_fmt_mp->height + 64) <= frmsize->max_height)
			pix_fmt_mp->height += 64;

		mtk_v4l2_vdec_dbg(0, ctx,
				  "before resize wxh=%dx%d, after resize wxh=%dx%d, sizeimage=%d",
				  tmp_w, tmp_h, pix_fmt_mp->width, pix_fmt_mp->height,
				  pix_fmt_mp->width * pix_fmt_mp->height);

		pix_fmt_mp->num_planes = fmt->num_planes;
		pix_fmt_mp->plane_fmt[0].sizeimage =
				pix_fmt_mp->width * pix_fmt_mp->height;
		pix_fmt_mp->plane_fmt[0].bytesperline = pix_fmt_mp->width;

		if (pix_fmt_mp->num_planes == 2) {
			pix_fmt_mp->plane_fmt[1].sizeimage =
				(pix_fmt_mp->width * pix_fmt_mp->height) / 2;
			pix_fmt_mp->plane_fmt[1].bytesperline =
				pix_fmt_mp->width;
		}
	}

	pix_fmt_mp->flags = 0;
	return 0;
}

static int vidioc_try_fmt_vid_cap_mplane(struct file *file, void *priv,
				struct v4l2_format *f)
{
	const struct mtk_video_fmt *fmt;
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);
	const struct mtk_vcodec_dec_pdata *dec_pdata = ctx->dev->vdec_pdata;

	fmt = mtk_vdec_find_format(f, dec_pdata);
	if (!fmt) {
		f->fmt.pix.pixelformat =
			ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc;
		fmt = mtk_vdec_find_format(f, dec_pdata);
	}

	return vidioc_try_fmt(ctx, f, fmt);
}

static int vidioc_try_fmt_vid_out_mplane(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;
	const struct mtk_video_fmt *fmt;
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);
	const struct mtk_vcodec_dec_pdata *dec_pdata = ctx->dev->vdec_pdata;

	fmt = mtk_vdec_find_format(f, dec_pdata);
	if (!fmt) {
		f->fmt.pix.pixelformat =
			ctx->q_data[MTK_Q_DATA_SRC].fmt->fourcc;
		fmt = mtk_vdec_find_format(f, dec_pdata);
	}

	if (pix_fmt_mp->plane_fmt[0].sizeimage == 0) {
		mtk_v4l2_vdec_err(ctx, "sizeimage of output format must be given");
		return -EINVAL;
	}

	return vidioc_try_fmt(ctx, f, fmt);
}

static int vidioc_vdec_g_selection(struct file *file, void *priv,
			struct v4l2_selection *s)
{
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);
	struct mtk_q_data *q_data;

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	q_data = &ctx->q_data[MTK_Q_DATA_DST];

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = ctx->picinfo.pic_w;
		s->r.height = ctx->picinfo.pic_h;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = ctx->picinfo.buf_w;
		s->r.height = ctx->picinfo.buf_h;
		break;
	case V4L2_SEL_TGT_COMPOSE:
		if (vdec_if_get_param(ctx, GET_PARAM_CROP_INFO, &(s->r))) {
			/* set to default value if header info not ready yet*/
			s->r.left = 0;
			s->r.top = 0;
			s->r.width = q_data->visible_width;
			s->r.height = q_data->visible_height;
		}
		break;
	default:
		return -EINVAL;
	}

	if (ctx->state < MTK_STATE_HEADER) {
		/* set to default value if header info not ready yet*/
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = q_data->visible_width;
		s->r.height = q_data->visible_height;
		return 0;
	}

	return 0;
}

static int vidioc_vdec_s_selection(struct file *file, void *priv,
				struct v4l2_selection *s)
{
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = ctx->picinfo.pic_w;
		s->r.height = ctx->picinfo.pic_h;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vidioc_vdec_s_fmt(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);
	struct v4l2_pix_format_mplane *pix_mp;
	struct mtk_q_data *q_data;
	int ret = 0;
	const struct mtk_video_fmt *fmt;
	const struct mtk_vcodec_dec_pdata *dec_pdata = ctx->dev->vdec_pdata;

	mtk_v4l2_vdec_dbg(3, ctx, "[%d]", ctx->id);

	q_data = mtk_vdec_get_q_data(ctx, f->type);
	if (!q_data)
		return -EINVAL;

	pix_mp = &f->fmt.pix_mp;
	/*
	 * Setting OUTPUT format after OUTPUT buffers are allocated is invalid
	 * if using the stateful API.
	 */
	if (!dec_pdata->uses_stateless_api &&
	    f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
	    vb2_is_busy(&ctx->m2m_ctx->out_q_ctx.q)) {
		mtk_v4l2_vdec_err(ctx, "out_q_ctx buffers already requested");
		ret = -EBUSY;
	}

	/*
	 * Setting CAPTURE format after CAPTURE buffers are allocated is
	 * invalid.
	 */
	if ((f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) &&
	    vb2_is_busy(&ctx->m2m_ctx->cap_q_ctx.q)) {
		mtk_v4l2_vdec_err(ctx, "cap_q_ctx buffers already requested");
		ret = -EBUSY;
	}

	fmt = mtk_vdec_find_format(f, dec_pdata);
	if (fmt == NULL) {
		if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
			f->fmt.pix.pixelformat =
				dec_pdata->default_out_fmt->fourcc;
			fmt = mtk_vdec_find_format(f, dec_pdata);
		} else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			f->fmt.pix.pixelformat =
				dec_pdata->default_cap_fmt->fourcc;
			fmt = mtk_vdec_find_format(f, dec_pdata);
		}
	}
	if (fmt == NULL)
		return -EINVAL;

	q_data->fmt = fmt;
	vidioc_try_fmt(ctx, f, q_data->fmt);
	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		q_data->sizeimage[0] = pix_mp->plane_fmt[0].sizeimage;
		q_data->coded_width = pix_mp->width;
		q_data->coded_height = pix_mp->height;

		ctx->colorspace = pix_mp->colorspace;
		ctx->ycbcr_enc = pix_mp->ycbcr_enc;
		ctx->quantization = pix_mp->quantization;
		ctx->xfer_func = pix_mp->xfer_func;

		ctx->current_codec = fmt->fourcc;
		if (ctx->state == MTK_STATE_FREE) {
			ret = vdec_if_init(ctx, q_data->fmt->fourcc);
			if (ret) {
				mtk_v4l2_vdec_err(ctx, "[%d]: vdec_if_init() fail ret=%d",
						  ctx->id, ret);
				return -EINVAL;
			}
			ctx->state = MTK_STATE_INIT;
		}
	} else {
		ctx->capture_fourcc = fmt->fourcc;
	}

	/*
	 * If using the stateless API, S_FMT should have the effect of setting
	 * the CAPTURE queue resolution no matter which queue it was called on.
	 */
	if (dec_pdata->uses_stateless_api) {
		ctx->picinfo.pic_w = pix_mp->width;
		ctx->picinfo.pic_h = pix_mp->height;

		/*
		 * If get pic info fail, need to use the default pic info params, or
		 * v4l2-compliance will fail
		 */
		ret = vdec_if_get_param(ctx, GET_PARAM_PIC_INFO, &ctx->picinfo);
		if (ret) {
			mtk_v4l2_vdec_err(ctx, "[%d]Error!! Get GET_PARAM_PICTURE_INFO Fail",
					  ctx->id);
		}

		ctx->last_decoded_picinfo = ctx->picinfo;

		if (ctx->q_data[MTK_Q_DATA_DST].fmt->num_planes == 1) {
			ctx->q_data[MTK_Q_DATA_DST].sizeimage[0] =
				ctx->picinfo.fb_sz[0] +
				ctx->picinfo.fb_sz[1];
			ctx->q_data[MTK_Q_DATA_DST].bytesperline[0] =
				ctx->picinfo.buf_w;
		} else {
			ctx->q_data[MTK_Q_DATA_DST].sizeimage[0] =
				ctx->picinfo.fb_sz[0];
			ctx->q_data[MTK_Q_DATA_DST].bytesperline[0] =
				ctx->picinfo.buf_w;
			ctx->q_data[MTK_Q_DATA_DST].sizeimage[1] =
				ctx->picinfo.fb_sz[1];
			ctx->q_data[MTK_Q_DATA_DST].bytesperline[1] =
				ctx->picinfo.buf_w;
		}

		ctx->q_data[MTK_Q_DATA_DST].coded_width = ctx->picinfo.buf_w;
		ctx->q_data[MTK_Q_DATA_DST].coded_height = ctx->picinfo.buf_h;
		mtk_v4l2_vdec_dbg(2, ctx,
				  "[%d] init() plane:%d wxh=%dx%d pic wxh=%dx%d sz=0x%x_0x%x",
				  ctx->id, pix_mp->num_planes,
				  ctx->picinfo.buf_w, ctx->picinfo.buf_h,
				  ctx->picinfo.pic_w, ctx->picinfo.pic_h,
				  ctx->q_data[MTK_Q_DATA_DST].sizeimage[0],
				  ctx->q_data[MTK_Q_DATA_DST].sizeimage[1]);
	}
	return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *priv,
				struct v4l2_frmsizeenum *fsize)
{
	int i = 0;
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);
	const struct mtk_vcodec_dec_pdata *dec_pdata = ctx->dev->vdec_pdata;

	if (fsize->index != 0)
		return -EINVAL;

	for (i = 0; i < *dec_pdata->num_formats; i++) {
		if (fsize->pixel_format != dec_pdata->vdec_formats[i].fourcc)
			continue;

		/* Only coded formats have frame sizes set */
		if (!dec_pdata->vdec_formats[i].frmsize.max_width)
			return -ENOTTY;

		fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
		fsize->stepwise = dec_pdata->vdec_formats[i].frmsize;

		mtk_v4l2_vdec_dbg(1, ctx, "%x, %d %d %d %d %d %d",
				  ctx->dev->dec_capability, fsize->stepwise.min_width,
				  fsize->stepwise.max_width, fsize->stepwise.step_width,
				  fsize->stepwise.min_height, fsize->stepwise.max_height,
				  fsize->stepwise.step_height);

		return 0;
	}

	return -EINVAL;
}

static int vidioc_enum_fmt(struct file *file, struct v4l2_fmtdesc *f,
			   bool output_queue)
{
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);
	const struct mtk_vcodec_dec_pdata *dec_pdata = ctx->dev->vdec_pdata;
	const struct mtk_video_fmt *fmt;
	int i, j = 0;

	for (i = 0; i < *dec_pdata->num_formats; i++) {
		if (output_queue &&
		    dec_pdata->vdec_formats[i].type != MTK_FMT_DEC)
			continue;
		if (!output_queue &&
		    dec_pdata->vdec_formats[i].type != MTK_FMT_FRAME)
			continue;

		if (!output_queue && !mtk_vdec_get_cap_fmt(ctx, i))
			continue;

		if (j == f->index)
			break;
		++j;
	}

	if (i == *dec_pdata->num_formats)
		return -EINVAL;

	fmt = &dec_pdata->vdec_formats[i];
	f->pixelformat = fmt->fourcc;
	f->flags = fmt->flags;

	return 0;
}

static int vidioc_vdec_enum_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(file, f, false);
}

static int vidioc_vdec_enum_fmt_vid_out(struct file *file, void *priv,
					struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(file, f, true);
}

static int vidioc_vdec_g_fmt(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct mtk_vcodec_dec_ctx *ctx = file_to_dec_ctx(file);
	struct v4l2_pix_format_mplane *pix_mp = &f->fmt.pix_mp;
	struct vb2_queue *vq;
	struct mtk_q_data *q_data;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq) {
		mtk_v4l2_vdec_err(ctx, "no vb2 queue for type=%d", f->type);
		return -EINVAL;
	}

	q_data = mtk_vdec_get_q_data(ctx, f->type);

	pix_mp->field = V4L2_FIELD_NONE;
	pix_mp->colorspace = ctx->colorspace;
	pix_mp->ycbcr_enc = ctx->ycbcr_enc;
	pix_mp->quantization = ctx->quantization;
	pix_mp->xfer_func = ctx->xfer_func;

	if ((f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) &&
	    (ctx->state >= MTK_STATE_HEADER)) {
		/* Until STREAMOFF is called on the CAPTURE queue
		 * (acknowledging the event), the driver operates as if
		 * the resolution hasn't changed yet.
		 * So we just return picinfo yet, and update picinfo in
		 * stop_streaming hook function
		 */
		q_data->sizeimage[0] = ctx->picinfo.fb_sz[0];
		q_data->sizeimage[1] = ctx->picinfo.fb_sz[1];
		q_data->bytesperline[0] = ctx->last_decoded_picinfo.buf_w;
		q_data->bytesperline[1] = ctx->last_decoded_picinfo.buf_w;
		q_data->coded_width = ctx->picinfo.buf_w;
		q_data->coded_height = ctx->picinfo.buf_h;
		ctx->last_decoded_picinfo.cap_fourcc = q_data->fmt->fourcc;

		/*
		 * Width and height are set to the dimensions
		 * of the movie, the buffer is bigger and
		 * further processing stages should crop to this
		 * rectangle.
		 */
		pix_mp->width = q_data->coded_width;
		pix_mp->height = q_data->coded_height;

		/*
		 * Set pixelformat to the format in which mt vcodec
		 * outputs the decoded frame
		 */
		pix_mp->num_planes = q_data->fmt->num_planes;
		pix_mp->pixelformat = q_data->fmt->fourcc;
		pix_mp->plane_fmt[0].bytesperline = q_data->bytesperline[0];
		pix_mp->plane_fmt[0].sizeimage = q_data->sizeimage[0];
		pix_mp->plane_fmt[1].bytesperline = q_data->bytesperline[1];
		pix_mp->plane_fmt[1].sizeimage = q_data->sizeimage[1];

	} else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		/*
		 * This is run on OUTPUT
		 * The buffer contains compressed image
		 * so width and height have no meaning.
		 * Assign value here to pass v4l2-compliance test
		 */
		pix_mp->width = q_data->visible_width;
		pix_mp->height = q_data->visible_height;
		pix_mp->plane_fmt[0].bytesperline = q_data->bytesperline[0];
		pix_mp->plane_fmt[0].sizeimage = q_data->sizeimage[0];
		pix_mp->pixelformat = q_data->fmt->fourcc;
		pix_mp->num_planes = q_data->fmt->num_planes;
	} else {
		pix_mp->width = q_data->coded_width;
		pix_mp->height = q_data->coded_height;
		pix_mp->num_planes = q_data->fmt->num_planes;
		pix_mp->pixelformat = q_data->fmt->fourcc;
		pix_mp->plane_fmt[0].bytesperline = q_data->bytesperline[0];
		pix_mp->plane_fmt[0].sizeimage = q_data->sizeimage[0];
		pix_mp->plane_fmt[1].bytesperline = q_data->bytesperline[1];
		pix_mp->plane_fmt[1].sizeimage = q_data->sizeimage[1];

		mtk_v4l2_vdec_dbg(1, ctx, "[%d] type=%d state=%d Format information not ready!",
				  ctx->id, f->type, ctx->state);
	}

	return 0;
}

int vb2ops_vdec_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			    unsigned int *nplanes, unsigned int sizes[],
			    struct device *alloc_devs[])
{
	struct mtk_vcodec_dec_ctx *ctx = vb2_get_drv_priv(vq);
	struct mtk_q_data *q_data;
	unsigned int i;

	q_data = mtk_vdec_get_q_data(ctx, vq->type);

	if (q_data == NULL) {
		mtk_v4l2_vdec_err(ctx, "vq->type=%d err\n", vq->type);
		return -EINVAL;
	}

	if (*nplanes) {
		if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
			if (*nplanes != q_data->fmt->num_planes)
				return -EINVAL;
		} else {
			if (*nplanes != 1)
				return -EINVAL;
		}
		for (i = 0; i < *nplanes; i++) {
			if (sizes[i] < q_data->sizeimage[i])
				return -EINVAL;
		}
	} else {
		if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
			*nplanes = q_data->fmt->num_planes;
		else
			*nplanes = 1;

		for (i = 0; i < *nplanes; i++)
			sizes[i] = q_data->sizeimage[i];
	}

	mtk_v4l2_vdec_dbg(1, ctx,
			  "[%d]\t type = %d, get %d plane(s), %d buffer(s) of size 0x%x 0x%x ",
			  ctx->id, vq->type, *nplanes, *nbuffers, sizes[0], sizes[1]);

	return 0;
}

int vb2ops_vdec_buf_prepare(struct vb2_buffer *vb)
{
	struct mtk_vcodec_dec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct mtk_q_data *q_data;
	int i;

	mtk_v4l2_vdec_dbg(3, ctx, "[%d] (%d) id=%d",
			  ctx->id, vb->vb2_queue->type, vb->index);

	q_data = mtk_vdec_get_q_data(ctx, vb->vb2_queue->type);

	for (i = 0; i < q_data->fmt->num_planes; i++) {
		if (vb2_plane_size(vb, i) < q_data->sizeimage[i]) {
			mtk_v4l2_vdec_err(ctx, "data will not fit into plane %d (%lu < %d)",
					  i, vb2_plane_size(vb, i), q_data->sizeimage[i]);
			return -EINVAL;
		}
		if (!V4L2_TYPE_IS_OUTPUT(vb->type))
			vb2_set_plane_payload(vb, i, q_data->sizeimage[i]);
	}

	return 0;
}

void vb2ops_vdec_buf_finish(struct vb2_buffer *vb)
{
	struct mtk_vcodec_dec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vb2_v4l2;
	struct mtk_video_dec_buf *buf;
	bool buf_error;

	vb2_v4l2 = container_of(vb, struct vb2_v4l2_buffer, vb2_buf);
	buf = container_of(vb2_v4l2, struct mtk_video_dec_buf, m2m_buf.vb);
	mutex_lock(&ctx->lock);
	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		buf->queued_in_v4l2 = false;
		buf->queued_in_vb2 = false;
	}
	buf_error = buf->error;
	mutex_unlock(&ctx->lock);

	if (buf_error) {
		mtk_v4l2_vdec_err(ctx, "Unrecoverable error on buffer.");
		ctx->state = MTK_STATE_ABORT;
	}
}

int vb2ops_vdec_buf_init(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vb2_v4l2 = container_of(vb,
					struct vb2_v4l2_buffer, vb2_buf);
	struct mtk_video_dec_buf *buf = container_of(vb2_v4l2,
					struct mtk_video_dec_buf, m2m_buf.vb);

	if (vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		buf->used = false;
		buf->queued_in_v4l2 = false;
	}

	return 0;
}

int vb2ops_vdec_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct mtk_vcodec_dec_ctx *ctx = vb2_get_drv_priv(q);

	if (ctx->state == MTK_STATE_FLUSH)
		ctx->state = MTK_STATE_HEADER;

	return 0;
}

void vb2ops_vdec_stop_streaming(struct vb2_queue *q)
{
	struct vb2_v4l2_buffer *src_buf = NULL, *dst_buf = NULL;
	struct mtk_vcodec_dec_ctx *ctx = vb2_get_drv_priv(q);
	int ret;

	mtk_v4l2_vdec_dbg(3, ctx, "[%d] (%d) state=(%x) ctx->decoded_frame_cnt=%d",
			  ctx->id, q->type, ctx->state, ctx->decoded_frame_cnt);

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		while ((src_buf = v4l2_m2m_src_buf_remove(ctx->m2m_ctx))) {
			if (src_buf != &ctx->empty_flush_buf.vb) {
				struct media_request *req =
					src_buf->vb2_buf.req_obj.req;
				v4l2_m2m_buf_done(src_buf,
						VB2_BUF_STATE_ERROR);
				if (req)
					v4l2_ctrl_request_complete(req, &ctx->ctrl_hdl);
			}
		}
		return;
	}

	if (ctx->state >= MTK_STATE_HEADER) {

		/* Until STREAMOFF is called on the CAPTURE queue
		 * (acknowledging the event), the driver operates
		 * as if the resolution hasn't changed yet, i.e.
		 * VIDIOC_G_FMT< etc. return previous resolution.
		 * So we update picinfo here
		 */
		ctx->picinfo = ctx->last_decoded_picinfo;

		mtk_v4l2_vdec_dbg(2, ctx,
				  "[%d]-> new(%d,%d), old(%d,%d), real(%d,%d)",
				  ctx->id, ctx->last_decoded_picinfo.pic_w,
				  ctx->last_decoded_picinfo.pic_h,
				  ctx->picinfo.pic_w, ctx->picinfo.pic_h,
				  ctx->last_decoded_picinfo.buf_w,
				  ctx->last_decoded_picinfo.buf_h);

		ret = ctx->dev->vdec_pdata->flush_decoder(ctx);
		if (ret)
			mtk_v4l2_vdec_err(ctx, "DecodeFinal failed, ret=%d", ret);
	}
	ctx->state = MTK_STATE_FLUSH;

	while ((dst_buf = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx))) {
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, 0);
		if (ctx->q_data[MTK_Q_DATA_DST].fmt->num_planes == 2)
			vb2_set_plane_payload(&dst_buf->vb2_buf, 1, 0);
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
	}

}

static void m2mops_vdec_device_run(void *priv)
{
	struct mtk_vcodec_dec_ctx *ctx = priv;
	struct mtk_vcodec_dec_dev *dev = ctx->dev;

	queue_work(dev->decode_workqueue, &ctx->decode_work);
}

static int m2mops_vdec_job_ready(void *m2m_priv)
{
	struct mtk_vcodec_dec_ctx *ctx = m2m_priv;

	mtk_v4l2_vdec_dbg(3, ctx, "[%d]", ctx->id);

	if (ctx->state == MTK_STATE_ABORT)
		return 0;

	if ((ctx->last_decoded_picinfo.pic_w != ctx->picinfo.pic_w) ||
	    (ctx->last_decoded_picinfo.pic_h != ctx->picinfo.pic_h))
		return 0;

	if (ctx->state != MTK_STATE_HEADER)
		return 0;

	return 1;
}

static void m2mops_vdec_job_abort(void *priv)
{
	struct mtk_vcodec_dec_ctx *ctx = priv;

	ctx->state = MTK_STATE_ABORT;
}

const struct v4l2_m2m_ops mtk_vdec_m2m_ops = {
	.device_run	= m2mops_vdec_device_run,
	.job_ready	= m2mops_vdec_job_ready,
	.job_abort	= m2mops_vdec_job_abort,
};

const struct v4l2_ioctl_ops mtk_vdec_ioctl_ops = {
	.vidioc_streamon	= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff	= v4l2_m2m_ioctl_streamoff,
	.vidioc_reqbufs		= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf	= v4l2_m2m_ioctl_querybuf,
	.vidioc_expbuf		= v4l2_m2m_ioctl_expbuf,

	.vidioc_qbuf		= vidioc_vdec_qbuf,
	.vidioc_dqbuf		= vidioc_vdec_dqbuf,

	.vidioc_try_fmt_vid_cap_mplane	= vidioc_try_fmt_vid_cap_mplane,
	.vidioc_try_fmt_vid_out_mplane	= vidioc_try_fmt_vid_out_mplane,

	.vidioc_s_fmt_vid_cap_mplane	= vidioc_vdec_s_fmt,
	.vidioc_s_fmt_vid_out_mplane	= vidioc_vdec_s_fmt,
	.vidioc_g_fmt_vid_cap_mplane	= vidioc_vdec_g_fmt,
	.vidioc_g_fmt_vid_out_mplane	= vidioc_vdec_g_fmt,

	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,

	.vidioc_enum_fmt_vid_cap	= vidioc_vdec_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out	= vidioc_vdec_enum_fmt_vid_out,
	.vidioc_enum_framesizes	= vidioc_enum_framesizes,

	.vidioc_querycap		= vidioc_vdec_querycap,
	.vidioc_subscribe_event		= vidioc_vdec_subscribe_evt,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,
	.vidioc_g_selection             = vidioc_vdec_g_selection,
	.vidioc_s_selection             = vidioc_vdec_s_selection,

	.vidioc_decoder_cmd = vidioc_decoder_cmd,
	.vidioc_try_decoder_cmd = vidioc_try_decoder_cmd,
};

int mtk_vcodec_dec_queue_init(void *priv, struct vb2_queue *src_vq,
			   struct vb2_queue *dst_vq)
{
	struct mtk_vcodec_dec_ctx *ctx = priv;
	int ret = 0;

	mtk_v4l2_vdec_dbg(3, ctx, "[%d]", ctx->id);

	src_vq->type		= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes	= VB2_DMABUF | VB2_MMAP;
	src_vq->drv_priv	= ctx;
	src_vq->buf_struct_size = sizeof(struct mtk_video_dec_buf);
	src_vq->ops		= ctx->dev->vdec_pdata->vdec_vb2_ops;
	src_vq->mem_ops		= &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock		= &ctx->dev->dev_mutex;
	src_vq->dev             = &ctx->dev->plat_dev->dev;
	src_vq->allow_cache_hints = 1;

	ret = vb2_queue_init(src_vq);
	if (ret) {
		mtk_v4l2_vdec_err(ctx, "Failed to initialize videobuf2 queue(output)");
		return ret;
	}
	dst_vq->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes	= VB2_DMABUF | VB2_MMAP;
	dst_vq->drv_priv	= ctx;
	dst_vq->buf_struct_size = sizeof(struct mtk_video_dec_buf);
	dst_vq->ops		= ctx->dev->vdec_pdata->vdec_vb2_ops;
	dst_vq->mem_ops		= &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock		= &ctx->dev->dev_mutex;
	dst_vq->dev             = &ctx->dev->plat_dev->dev;
	dst_vq->allow_cache_hints = 1;

	ret = vb2_queue_init(dst_vq);
	if (ret)
		mtk_v4l2_vdec_err(ctx, "Failed to initialize videobuf2 queue(capture)");

	return ret;
}
