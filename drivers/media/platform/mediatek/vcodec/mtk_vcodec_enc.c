// SPDX-License-Identifier: GPL-2.0
/*
* Copyright (c) 2016 MediaTek Inc.
* Author: PC Chen <pc.chen@mediatek.com>
*         Tiffany Lin <tiffany.lin@mediatek.com>
*/

#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include <linux/pm_runtime.h>

#include "mtk_vcodec_drv.h"
#include "mtk_vcodec_enc.h"
#include "mtk_vcodec_intr.h"
#include "mtk_vcodec_util.h"
#include "venc_drv_if.h"

#define MTK_VENC_MIN_W	160U
#define MTK_VENC_MIN_H	128U
#define MTK_VENC_HD_MAX_W	1920U
#define MTK_VENC_HD_MAX_H	1088U
#define MTK_VENC_4K_MAX_W	3840U
#define MTK_VENC_4K_MAX_H	2176U

#define DFT_CFG_WIDTH	MTK_VENC_MIN_W
#define DFT_CFG_HEIGHT	MTK_VENC_MIN_H
#define MTK_MAX_CTRLS_HINT	20

#define MTK_DEFAULT_FRAMERATE_NUM 1001
#define MTK_DEFAULT_FRAMERATE_DENOM 30000
#define MTK_VENC_4K_CAPABILITY_ENABLE BIT(0)

static void mtk_venc_worker(struct work_struct *work);

static const struct v4l2_frmsize_stepwise mtk_venc_hd_framesizes = {
	MTK_VENC_MIN_W, MTK_VENC_HD_MAX_W, 16,
	MTK_VENC_MIN_H, MTK_VENC_HD_MAX_H, 16,
};

static const struct v4l2_frmsize_stepwise mtk_venc_4k_framesizes = {
	MTK_VENC_MIN_W, MTK_VENC_4K_MAX_W, 16,
	MTK_VENC_MIN_H, MTK_VENC_4K_MAX_H, 16,
};

static int vidioc_venc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mtk_vcodec_ctx *ctx = ctrl_to_ctx(ctrl);
	struct mtk_enc_params *p = &ctx->enc_params;
	int ret = 0;

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_BITRATE_MODE val= %d",
			       ctrl->val);
		if (ctrl->val != V4L2_MPEG_VIDEO_BITRATE_MODE_CBR) {
			mtk_v4l2_err("Unsupported bitrate mode =%d", ctrl->val);
			ret = -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_BITRATE val = %d",
			       ctrl->val);
		p->bitrate = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_BITRATE;
		break;
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_B_FRAMES val = %d",
			       ctrl->val);
		p->num_b_frame = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE val = %d",
			       ctrl->val);
		p->rc_frame = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_H264_MAX_QP val = %d",
			       ctrl->val);
		p->h264_max_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_HEADER_MODE val = %d",
			       ctrl->val);
		p->seq_hdr_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE val = %d",
			       ctrl->val);
		p->rc_mb = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_H264_PROFILE val = %d",
			       ctrl->val);
		p->h264_profile = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_H264_LEVEL val = %d",
			       ctrl->val);
		p->h264_level = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_PERIOD:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_H264_I_PERIOD val = %d",
			       ctrl->val);
		p->intra_period = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_INTRA_PERIOD;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_GOP_SIZE val = %d",
			       ctrl->val);
		p->gop_size = ctrl->val;
		ctx->param_change |= MTK_ENCODE_PARAM_GOP_SIZE;
		break;
	case V4L2_CID_MPEG_VIDEO_VP8_PROFILE:
		/*
		 * FIXME - what vp8 profiles are actually supported?
		 * The ctrl is added (with only profile 0 supported) for now.
		 */
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_VP8_PROFILE val = %d", ctrl->val);
		break;
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:
		mtk_v4l2_debug(2, "V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME");
		p->force_intra = 1;
		ctx->param_change |= MTK_ENCODE_PARAM_FORCE_INTRA;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct v4l2_ctrl_ops mtk_vcodec_enc_ctrl_ops = {
	.s_ctrl = vidioc_venc_s_ctrl,
};

static int vidioc_enum_fmt(struct v4l2_fmtdesc *f,
			   const struct mtk_video_fmt *formats,
			   size_t num_formats)
{
	if (f->index >= num_formats)
		return -EINVAL;

	f->pixelformat = formats[f->index].fourcc;

	return 0;
}

static const struct mtk_video_fmt *
mtk_venc_find_format(u32 fourcc, const struct mtk_vcodec_enc_pdata *pdata)
{
	const struct mtk_video_fmt *fmt;
	unsigned int k;

	for (k = 0; k < pdata->num_capture_formats; k++) {
		fmt = &pdata->capture_formats[k];
		if (fmt->fourcc == fourcc)
			return fmt;
	}

	for (k = 0; k < pdata->num_output_formats; k++) {
		fmt = &pdata->output_formats[k];
		if (fmt->fourcc == fourcc)
			return fmt;
	}

	return NULL;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
				  struct v4l2_frmsizeenum *fsize)
{
	const struct mtk_video_fmt *fmt;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(fh);

	if (fsize->index != 0)
		return -EINVAL;

	fmt = mtk_venc_find_format(fsize->pixel_format,
				   ctx->dev->venc_pdata);
	if (!fmt)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;

	if (ctx->dev->enc_capability & MTK_VENC_4K_CAPABILITY_ENABLE)
		fsize->stepwise = mtk_venc_4k_framesizes;
	else
		fsize->stepwise = mtk_venc_hd_framesizes;

	return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	const struct mtk_vcodec_enc_pdata *pdata =
		fh_to_ctx(priv)->dev->venc_pdata;

	return vidioc_enum_fmt(f, pdata->capture_formats,
			       pdata->num_capture_formats);
}

static int vidioc_enum_fmt_vid_out(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	const struct mtk_vcodec_enc_pdata *pdata =
		fh_to_ctx(priv)->dev->venc_pdata;

	return vidioc_enum_fmt(f, pdata->output_formats,
			       pdata->num_output_formats);
}

static int mtk_vcodec_enc_get_chip_name(void *priv)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct device *dev = &ctx->dev->plat_dev->dev;

	if (of_device_is_compatible(dev->of_node, "mediatek,mt8173-vcodec-enc"))
		return 8173;
	else if (of_device_is_compatible(dev->of_node, "mediatek,mt8183-vcodec-enc"))
		return 8183;
	else if (of_device_is_compatible(dev->of_node, "mediatek,mt8192-vcodec-enc"))
		return 8192;
	else if (of_device_is_compatible(dev->of_node, "mediatek,mt8195-vcodec-enc"))
		return 8195;
	else if (of_device_is_compatible(dev->of_node, "mediatek,mt8188-vcodec-enc"))
		return 8188;
	else
		return 8173;
}

static int vidioc_venc_querycap(struct file *file, void *priv,
				struct v4l2_capability *cap)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct device *dev = &ctx->dev->plat_dev->dev;
	int platform_name = mtk_vcodec_enc_get_chip_name(priv);

	strscpy(cap->driver, dev->driver->name, sizeof(cap->driver));
	snprintf(cap->card, sizeof(cap->card), "MT%d video encoder", platform_name);

	return 0;
}

static int vidioc_venc_s_parm(struct file *file, void *priv,
			      struct v4l2_streamparm *a)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_fract *timeperframe = &a->parm.output.timeperframe;

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	if (timeperframe->numerator == 0 || timeperframe->denominator == 0) {
		timeperframe->numerator = MTK_DEFAULT_FRAMERATE_NUM;
		timeperframe->denominator = MTK_DEFAULT_FRAMERATE_DENOM;
	}

	ctx->enc_params.framerate_num = timeperframe->denominator;
	ctx->enc_params.framerate_denom = timeperframe->numerator;
	ctx->param_change |= MTK_ENCODE_PARAM_FRAMERATE;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;

	return 0;
}

static int vidioc_venc_g_parm(struct file *file, void *priv,
			      struct v4l2_streamparm *a)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	a->parm.output.timeperframe.denominator =
			ctx->enc_params.framerate_num;
	a->parm.output.timeperframe.numerator =
			ctx->enc_params.framerate_denom;

	return 0;
}

static struct mtk_q_data *mtk_venc_get_q_data(struct mtk_vcodec_ctx *ctx,
					      enum v4l2_buf_type type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return &ctx->q_data[MTK_Q_DATA_SRC];

	return &ctx->q_data[MTK_Q_DATA_DST];
}

static void vidioc_try_fmt_cap(struct v4l2_format *f)
{
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	f->fmt.pix_mp.num_planes = 1;
	f->fmt.pix_mp.plane_fmt[0].bytesperline = 0;
	f->fmt.pix_mp.flags = 0;
}

/* V4L2 specification suggests the driver corrects the format struct if any of
 * the dimensions is unsupported
 */
static int vidioc_try_fmt_out(struct mtk_vcodec_ctx *ctx, struct v4l2_format *f,
			      const struct mtk_video_fmt *fmt)
{
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;
	int tmp_w, tmp_h;
	unsigned int max_width, max_height;

	pix_fmt_mp->field = V4L2_FIELD_NONE;

	if (ctx->dev->enc_capability & MTK_VENC_4K_CAPABILITY_ENABLE) {
		max_width = MTK_VENC_4K_MAX_W;
		max_height = MTK_VENC_4K_MAX_H;
	} else {
		max_width = MTK_VENC_HD_MAX_W;
		max_height = MTK_VENC_HD_MAX_H;
	}

	pix_fmt_mp->height = clamp(pix_fmt_mp->height, MTK_VENC_MIN_H, max_height);
	pix_fmt_mp->width = clamp(pix_fmt_mp->width, MTK_VENC_MIN_W, max_width);

	/* find next closer width align 16, heign align 32, size align
	 * 64 rectangle
	 */
	tmp_w = pix_fmt_mp->width;
	tmp_h = pix_fmt_mp->height;
	v4l_bound_align_image(&pix_fmt_mp->width,
			      MTK_VENC_MIN_W,
			      max_width, 4,
			      &pix_fmt_mp->height,
			      MTK_VENC_MIN_H,
			      max_height, 5, 6);

	if (pix_fmt_mp->width < tmp_w && (pix_fmt_mp->width + 16) <= max_width)
		pix_fmt_mp->width += 16;
	if (pix_fmt_mp->height < tmp_h && (pix_fmt_mp->height + 32) <= max_height)
		pix_fmt_mp->height += 32;

	mtk_v4l2_debug(0, "before resize w=%d, h=%d, after resize w=%d, h=%d, sizeimage=%d %d",
		       tmp_w, tmp_h, pix_fmt_mp->width,
		       pix_fmt_mp->height,
		       pix_fmt_mp->plane_fmt[0].sizeimage,
		       pix_fmt_mp->plane_fmt[1].sizeimage);

	pix_fmt_mp->num_planes = fmt->num_planes;
	pix_fmt_mp->plane_fmt[0].sizeimage =
			pix_fmt_mp->width * pix_fmt_mp->height +
			((ALIGN(pix_fmt_mp->width, 16) * 2) * 16);
	pix_fmt_mp->plane_fmt[0].bytesperline = pix_fmt_mp->width;

	if (pix_fmt_mp->num_planes == 2) {
		pix_fmt_mp->plane_fmt[1].sizeimage =
			(pix_fmt_mp->width * pix_fmt_mp->height) / 2 +
			(ALIGN(pix_fmt_mp->width, 16) * 16);
		pix_fmt_mp->plane_fmt[2].sizeimage = 0;
		pix_fmt_mp->plane_fmt[1].bytesperline =
						pix_fmt_mp->width;
		pix_fmt_mp->plane_fmt[2].bytesperline = 0;
	} else if (pix_fmt_mp->num_planes == 3) {
		pix_fmt_mp->plane_fmt[1].sizeimage =
		pix_fmt_mp->plane_fmt[2].sizeimage =
			(pix_fmt_mp->width * pix_fmt_mp->height) / 4 +
			((ALIGN(pix_fmt_mp->width, 16) / 2) * 16);
		pix_fmt_mp->plane_fmt[1].bytesperline =
			pix_fmt_mp->plane_fmt[2].bytesperline =
			pix_fmt_mp->width / 2;
	}

	pix_fmt_mp->flags = 0;

	return 0;
}

static void mtk_venc_set_param(struct mtk_vcodec_ctx *ctx,
				struct venc_enc_param *param)
{
	struct mtk_q_data *q_data_src = &ctx->q_data[MTK_Q_DATA_SRC];
	struct mtk_enc_params *enc_params = &ctx->enc_params;

	switch (q_data_src->fmt->fourcc) {
	case V4L2_PIX_FMT_YUV420M:
		param->input_yuv_fmt = VENC_YUV_FORMAT_I420;
		break;
	case V4L2_PIX_FMT_YVU420M:
		param->input_yuv_fmt = VENC_YUV_FORMAT_YV12;
		break;
	case V4L2_PIX_FMT_NV12M:
		param->input_yuv_fmt = VENC_YUV_FORMAT_NV12;
		break;
	case V4L2_PIX_FMT_NV21M:
		param->input_yuv_fmt = VENC_YUV_FORMAT_NV21;
		break;
	default:
		mtk_v4l2_err("Unsupported fourcc =%d", q_data_src->fmt->fourcc);
		break;
	}
	param->h264_profile = enc_params->h264_profile;
	param->h264_level = enc_params->h264_level;

	/* Config visible resolution */
	param->width = q_data_src->visible_width;
	param->height = q_data_src->visible_height;
	/* Config coded resolution */
	param->buf_width = q_data_src->coded_width;
	param->buf_height = q_data_src->coded_height;
	param->frm_rate = enc_params->framerate_num /
			enc_params->framerate_denom;
	param->intra_period = enc_params->intra_period;
	param->gop_size = enc_params->gop_size;
	param->bitrate = enc_params->bitrate;

	mtk_v4l2_debug(0,
		"fmt 0x%x, P/L %d/%d, w/h %d/%d, buf %d/%d, fps/bps %d/%d, gop %d, i_period %d",
		param->input_yuv_fmt, param->h264_profile,
		param->h264_level, param->width, param->height,
		param->buf_width, param->buf_height,
		param->frm_rate, param->bitrate,
		param->gop_size, param->intra_period);
}

static int vidioc_venc_s_fmt_cap(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	const struct mtk_vcodec_enc_pdata *pdata = ctx->dev->venc_pdata;
	struct vb2_queue *vq;
	struct mtk_q_data *q_data = mtk_venc_get_q_data(ctx, f->type);
	int i, ret;
	const struct mtk_video_fmt *fmt;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq) {
		mtk_v4l2_err("fail to get vq");
		return -EINVAL;
	}

	if (vb2_is_busy(vq)) {
		mtk_v4l2_err("queue busy");
		return -EBUSY;
	}

	fmt = mtk_venc_find_format(f->fmt.pix.pixelformat, pdata);
	if (!fmt) {
		fmt = &ctx->dev->venc_pdata->capture_formats[0];
		f->fmt.pix.pixelformat = fmt->fourcc;
	}

	q_data->fmt = fmt;
	vidioc_try_fmt_cap(f);

	q_data->coded_width = f->fmt.pix_mp.width;
	q_data->coded_height = f->fmt.pix_mp.height;
	q_data->field = f->fmt.pix_mp.field;

	for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
		struct v4l2_plane_pix_format	*plane_fmt;

		plane_fmt = &f->fmt.pix_mp.plane_fmt[i];
		q_data->bytesperline[i]	= plane_fmt->bytesperline;
		q_data->sizeimage[i] = plane_fmt->sizeimage;
	}

	if (ctx->state == MTK_STATE_FREE) {
		ret = venc_if_init(ctx, q_data->fmt->fourcc);
		if (ret) {
			mtk_v4l2_err("venc_if_init failed=%d, codec type=%x",
					ret, q_data->fmt->fourcc);
			return -EBUSY;
		}
		ctx->state = MTK_STATE_INIT;
	}

	return 0;
}

static int vidioc_venc_s_fmt_out(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	const struct mtk_vcodec_enc_pdata *pdata = ctx->dev->venc_pdata;
	struct vb2_queue *vq;
	struct mtk_q_data *q_data = mtk_venc_get_q_data(ctx, f->type);
	int ret, i;
	const struct mtk_video_fmt *fmt;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq) {
		mtk_v4l2_err("fail to get vq");
		return -EINVAL;
	}

	if (vb2_is_busy(vq)) {
		mtk_v4l2_err("queue busy");
		return -EBUSY;
	}

	fmt = mtk_venc_find_format(f->fmt.pix.pixelformat, pdata);
	if (!fmt) {
		fmt = &ctx->dev->venc_pdata->output_formats[0];
		f->fmt.pix.pixelformat = fmt->fourcc;
	}

	q_data->visible_width = f->fmt.pix_mp.width;
	q_data->visible_height = f->fmt.pix_mp.height;
	q_data->fmt = fmt;
	ret = vidioc_try_fmt_out(ctx, f, q_data->fmt);
	if (ret)
		return ret;

	q_data->coded_width = f->fmt.pix_mp.width;
	q_data->coded_height = f->fmt.pix_mp.height;

	q_data->field = f->fmt.pix_mp.field;
	ctx->colorspace = f->fmt.pix_mp.colorspace;
	ctx->ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	ctx->quantization = f->fmt.pix_mp.quantization;
	ctx->xfer_func = f->fmt.pix_mp.xfer_func;

	for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
		struct v4l2_plane_pix_format *plane_fmt;

		plane_fmt = &f->fmt.pix_mp.plane_fmt[i];
		q_data->bytesperline[i] = plane_fmt->bytesperline;
		q_data->sizeimage[i] = plane_fmt->sizeimage;
	}

	return 0;
}

static int vidioc_venc_g_fmt(struct file *file, void *priv,
			     struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix = &f->fmt.pix_mp;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *vq;
	struct mtk_q_data *q_data = mtk_venc_get_q_data(ctx, f->type);
	int i;

	vq = v4l2_m2m_get_vq(ctx->m2m_ctx, f->type);
	if (!vq)
		return -EINVAL;


	pix->width = q_data->coded_width;
	pix->height = q_data->coded_height;
	pix->pixelformat = q_data->fmt->fourcc;
	pix->field = q_data->field;
	pix->num_planes = q_data->fmt->num_planes;
	for (i = 0; i < pix->num_planes; i++) {
		pix->plane_fmt[i].bytesperline = q_data->bytesperline[i];
		pix->plane_fmt[i].sizeimage = q_data->sizeimage[i];
	}

	pix->flags = 0;
	pix->colorspace = ctx->colorspace;
	pix->ycbcr_enc = ctx->ycbcr_enc;
	pix->quantization = ctx->quantization;
	pix->xfer_func = ctx->xfer_func;

	return 0;
}

static int vidioc_try_fmt_vid_cap_mplane(struct file *file, void *priv,
					 struct v4l2_format *f)
{
	const struct mtk_video_fmt *fmt;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	const struct mtk_vcodec_enc_pdata *pdata = ctx->dev->venc_pdata;

	fmt = mtk_venc_find_format(f->fmt.pix.pixelformat, pdata);
	if (!fmt) {
		fmt = &ctx->dev->venc_pdata->capture_formats[0];
		f->fmt.pix.pixelformat = fmt->fourcc;
	}
	f->fmt.pix_mp.colorspace = ctx->colorspace;
	f->fmt.pix_mp.ycbcr_enc = ctx->ycbcr_enc;
	f->fmt.pix_mp.quantization = ctx->quantization;
	f->fmt.pix_mp.xfer_func = ctx->xfer_func;

	vidioc_try_fmt_cap(f);

	return 0;
}

static int vidioc_try_fmt_vid_out_mplane(struct file *file, void *priv,
					 struct v4l2_format *f)
{
	const struct mtk_video_fmt *fmt;
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	const struct mtk_vcodec_enc_pdata *pdata = ctx->dev->venc_pdata;

	fmt = mtk_venc_find_format(f->fmt.pix.pixelformat, pdata);
	if (!fmt) {
		fmt = &ctx->dev->venc_pdata->output_formats[0];
		f->fmt.pix.pixelformat = fmt->fourcc;
	}
	if (!f->fmt.pix_mp.colorspace) {
		f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_REC709;
		f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
		f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
		f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	}

	return vidioc_try_fmt_out(ctx, f, fmt);
}

static int vidioc_venc_g_selection(struct file *file, void *priv,
				     struct v4l2_selection *s)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct mtk_q_data *q_data = mtk_venc_get_q_data(ctx, s->type);

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		s->r.top = 0;
		s->r.left = 0;
		s->r.width = q_data->coded_width;
		s->r.height = q_data->coded_height;
		break;
	case V4L2_SEL_TGT_CROP:
		s->r.top = 0;
		s->r.left = 0;
		s->r.width = q_data->visible_width;
		s->r.height = q_data->visible_height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vidioc_venc_s_selection(struct file *file, void *priv,
				     struct v4l2_selection *s)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct mtk_q_data *q_data = mtk_venc_get_q_data(ctx, s->type);

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		/* Only support crop from (0,0) */
		s->r.top = 0;
		s->r.left = 0;
		s->r.width = min(s->r.width, q_data->coded_width);
		s->r.height = min(s->r.height, q_data->coded_height);
		q_data->visible_width = s->r.width;
		q_data->visible_height = s->r.height;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int vidioc_venc_qbuf(struct file *file, void *priv,
			    struct v4l2_buffer *buf)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);

	if (ctx->state == MTK_STATE_ABORT) {
		mtk_v4l2_err("[%d] Call on QBUF after unrecoverable error",
				ctx->id);
		return -EIO;
	}

	return v4l2_m2m_qbuf(file, ctx->m2m_ctx, buf);
}

static int vidioc_venc_dqbuf(struct file *file, void *priv,
			     struct v4l2_buffer *buf)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	int ret;

	if (ctx->state == MTK_STATE_ABORT) {
		mtk_v4l2_err("[%d] Call on QBUF after unrecoverable error",
				ctx->id);
		return -EIO;
	}

	ret = v4l2_m2m_dqbuf(file, ctx->m2m_ctx, buf);
	if (ret)
		return ret;

	/*
	 * Complete flush if the user dequeued the 0-payload LAST buffer.
	 * We check the payload because a buffer with the LAST flag can also
	 * be seen during resolution changes. If we happen to be flushing at
	 * that time, the last buffer before the resolution changes could be
	 * misinterpreted for the buffer generated by the flush and terminate
	 * it earlier than we want.
	 */
	if (!V4L2_TYPE_IS_OUTPUT(buf->type) &&
	    buf->flags & V4L2_BUF_FLAG_LAST &&
	    buf->m.planes[0].bytesused == 0 &&
	    ctx->is_flushing) {
		/*
		 * Last CAPTURE buffer is dequeued, we can allow another flush
		 * to take place.
		 */
		ctx->is_flushing = false;
	}

	return 0;
}

static int vidioc_encoder_cmd(struct file *file, void *priv,
			      struct v4l2_encoder_cmd *cmd)
{
	struct mtk_vcodec_ctx *ctx = fh_to_ctx(priv);
	struct vb2_queue *src_vq, *dst_vq;
	int ret;

	if (ctx->state == MTK_STATE_ABORT) {
		mtk_v4l2_err("[%d] Call to CMD after unrecoverable error",
			     ctx->id);
		return -EIO;
	}

	ret = v4l2_m2m_ioctl_try_encoder_cmd(file, priv, cmd);
	if (ret)
		return ret;

	/* Calling START or STOP is invalid if a flush is in progress */
	if (ctx->is_flushing)
		return -EBUSY;

	mtk_v4l2_debug(1, "encoder cmd=%u", cmd->cmd);

	dst_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
				 V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	switch (cmd->cmd) {
	case V4L2_ENC_CMD_STOP:
		src_vq = v4l2_m2m_get_vq(ctx->m2m_ctx,
					 V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
		if (!vb2_is_streaming(src_vq)) {
			mtk_v4l2_debug(1, "Output stream is off. No need to flush.");
			return 0;
		}
		if (!vb2_is_streaming(dst_vq)) {
			mtk_v4l2_debug(1, "Capture stream is off. No need to flush.");
			return 0;
		}
		ctx->is_flushing = true;
		v4l2_m2m_buf_queue(ctx->m2m_ctx, &ctx->empty_flush_buf.vb);
		v4l2_m2m_try_schedule(ctx->m2m_ctx);
		break;

	case V4L2_ENC_CMD_START:
		vb2_clear_last_buffer_dequeued(dst_vq);
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

const struct v4l2_ioctl_ops mtk_venc_ioctl_ops = {
	.vidioc_streamon		= v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff		= v4l2_m2m_ioctl_streamoff,

	.vidioc_reqbufs			= v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf		= v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf			= vidioc_venc_qbuf,
	.vidioc_dqbuf			= vidioc_venc_dqbuf,

	.vidioc_querycap		= vidioc_venc_querycap,
	.vidioc_enum_fmt_vid_cap	= vidioc_enum_fmt_vid_cap,
	.vidioc_enum_fmt_vid_out	= vidioc_enum_fmt_vid_out,
	.vidioc_enum_framesizes		= vidioc_enum_framesizes,

	.vidioc_try_fmt_vid_cap_mplane	= vidioc_try_fmt_vid_cap_mplane,
	.vidioc_try_fmt_vid_out_mplane	= vidioc_try_fmt_vid_out_mplane,
	.vidioc_expbuf			= v4l2_m2m_ioctl_expbuf,
	.vidioc_subscribe_event		= v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event	= v4l2_event_unsubscribe,

	.vidioc_s_parm			= vidioc_venc_s_parm,
	.vidioc_g_parm			= vidioc_venc_g_parm,
	.vidioc_s_fmt_vid_cap_mplane	= vidioc_venc_s_fmt_cap,
	.vidioc_s_fmt_vid_out_mplane	= vidioc_venc_s_fmt_out,

	.vidioc_g_fmt_vid_cap_mplane	= vidioc_venc_g_fmt,
	.vidioc_g_fmt_vid_out_mplane	= vidioc_venc_g_fmt,

	.vidioc_create_bufs		= v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf		= v4l2_m2m_ioctl_prepare_buf,

	.vidioc_g_selection		= vidioc_venc_g_selection,
	.vidioc_s_selection		= vidioc_venc_s_selection,

	.vidioc_encoder_cmd		= vidioc_encoder_cmd,
	.vidioc_try_encoder_cmd		= v4l2_m2m_ioctl_try_encoder_cmd,
};

static int vb2ops_venc_queue_setup(struct vb2_queue *vq,
				   unsigned int *nbuffers,
				   unsigned int *nplanes,
				   unsigned int sizes[],
				   struct device *alloc_devs[])
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vq);
	struct mtk_q_data *q_data = mtk_venc_get_q_data(ctx, vq->type);
	unsigned int i;

	if (q_data == NULL)
		return -EINVAL;

	if (*nplanes) {
		if (*nplanes != q_data->fmt->num_planes)
			return -EINVAL;
		for (i = 0; i < *nplanes; i++)
			if (sizes[i] < q_data->sizeimage[i])
				return -EINVAL;
	} else {
		*nplanes = q_data->fmt->num_planes;
		for (i = 0; i < *nplanes; i++)
			sizes[i] = q_data->sizeimage[i];
	}

	return 0;
}

static int vb2ops_venc_buf_prepare(struct vb2_buffer *vb)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct mtk_q_data *q_data = mtk_venc_get_q_data(ctx, vb->vb2_queue->type);
	int i;

	for (i = 0; i < q_data->fmt->num_planes; i++) {
		if (vb2_plane_size(vb, i) < q_data->sizeimage[i]) {
			mtk_v4l2_err("data will not fit into plane %d (%lu < %d)",
				i, vb2_plane_size(vb, i),
				q_data->sizeimage[i]);
			return -EINVAL;
		}
	}

	return 0;
}

static void vb2ops_venc_buf_queue(struct vb2_buffer *vb)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vb2_v4l2 =
			container_of(vb, struct vb2_v4l2_buffer, vb2_buf);

	struct mtk_video_enc_buf *mtk_buf =
			container_of(vb2_v4l2, struct mtk_video_enc_buf,
				     m2m_buf.vb);

	if ((vb->vb2_queue->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) &&
	    (ctx->param_change != MTK_ENCODE_PARAM_NONE)) {
		mtk_v4l2_debug(1, "[%d] Before id=%d encode parameter change %x",
			       ctx->id,
			       vb2_v4l2->vb2_buf.index,
			       ctx->param_change);
		mtk_buf->param_change = ctx->param_change;
		mtk_buf->enc_params = ctx->enc_params;
		ctx->param_change = MTK_ENCODE_PARAM_NONE;
	}

	v4l2_m2m_buf_queue(ctx->m2m_ctx, to_vb2_v4l2_buffer(vb));
}

static int vb2ops_venc_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(q);
	struct venc_enc_param param;
	int ret, pm_ret;
	int i;

	/* Once state turn into MTK_STATE_ABORT, we need stop_streaming
	  * to clear it
	  */
	if ((ctx->state == MTK_STATE_ABORT) || (ctx->state == MTK_STATE_FREE)) {
		ret = -EIO;
		goto err_start_stream;
	}

	/* Do the initialization when both start_streaming have been called */
	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		if (!vb2_start_streaming_called(&ctx->m2m_ctx->cap_q_ctx.q))
			return 0;
	} else {
		if (!vb2_start_streaming_called(&ctx->m2m_ctx->out_q_ctx.q))
			return 0;
	}

	ret = pm_runtime_resume_and_get(&ctx->dev->plat_dev->dev);
	if (ret < 0) {
		mtk_v4l2_err("pm_runtime_resume_and_get fail %d", ret);
		goto err_start_stream;
	}

	mtk_venc_set_param(ctx, &param);
	ret = venc_if_set_param(ctx, VENC_SET_PARAM_ENC, &param);
	if (ret) {
		mtk_v4l2_err("venc_if_set_param failed=%d", ret);
		ctx->state = MTK_STATE_ABORT;
		goto err_set_param;
	}
	ctx->param_change = MTK_ENCODE_PARAM_NONE;

	if ((ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc == V4L2_PIX_FMT_H264) &&
	    (ctx->enc_params.seq_hdr_mode !=
				V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE)) {
		ret = venc_if_set_param(ctx,
					VENC_SET_PARAM_PREPEND_HEADER,
					NULL);
		if (ret) {
			mtk_v4l2_err("venc_if_set_param failed=%d", ret);
			ctx->state = MTK_STATE_ABORT;
			goto err_set_param;
		}
		ctx->state = MTK_STATE_HEADER;
	}

	return 0;

err_set_param:
	pm_ret = pm_runtime_put(&ctx->dev->plat_dev->dev);
	if (pm_ret < 0)
		mtk_v4l2_err("pm_runtime_put fail %d", pm_ret);

err_start_stream:
	for (i = 0; i < q->num_buffers; ++i) {
		struct vb2_buffer *buf = vb2_get_buffer(q, i);

		/*
		 * FIXME: This check is not needed as only active buffers
		 * can be marked as done.
		 */
		if (buf->state == VB2_BUF_STATE_ACTIVE) {
			mtk_v4l2_debug(0, "[%d] id=%d, type=%d, %d -> VB2_BUF_STATE_QUEUED",
					ctx->id, i, q->type,
					(int)buf->state);
			v4l2_m2m_buf_done(to_vb2_v4l2_buffer(buf),
					  VB2_BUF_STATE_QUEUED);
		}
	}

	return ret;
}

static void vb2ops_venc_stop_streaming(struct vb2_queue *q)
{
	struct mtk_vcodec_ctx *ctx = vb2_get_drv_priv(q);
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	int ret;

	mtk_v4l2_debug(2, "[%d]-> type=%d", ctx->id, q->type);

	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		while ((dst_buf = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx))) {
			vb2_set_plane_payload(&dst_buf->vb2_buf, 0, 0);
			v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
		}
		/* STREAMOFF on the CAPTURE queue completes any ongoing flush */
		if (ctx->is_flushing) {
			struct v4l2_m2m_buffer *b, *n;

			mtk_v4l2_debug(1, "STREAMOFF called while flushing");
			/*
			 * STREAMOFF could be called before the flush buffer is
			 * dequeued. Check whether empty flush buf is still in
			 * queue before removing it.
			 */
			v4l2_m2m_for_each_src_buf_safe(ctx->m2m_ctx, b, n) {
				if (b == &ctx->empty_flush_buf) {
					v4l2_m2m_src_buf_remove_by_buf(ctx->m2m_ctx, &b->vb);
					break;
				}
			}
			ctx->is_flushing = false;
		}
	} else {
		while ((src_buf = v4l2_m2m_src_buf_remove(ctx->m2m_ctx))) {
			if (src_buf != &ctx->empty_flush_buf.vb)
				v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
		}
		if (ctx->is_flushing) {
			/*
			 * If we are in the middle of a flush, put the flush
			 * buffer back into the queue so the next CAPTURE
			 * buffer gets returned with the LAST flag set.
			 */
			v4l2_m2m_buf_queue(ctx->m2m_ctx,
					   &ctx->empty_flush_buf.vb);
		}
	}

	if ((q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
	     vb2_is_streaming(&ctx->m2m_ctx->out_q_ctx.q)) ||
	    (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
	     vb2_is_streaming(&ctx->m2m_ctx->cap_q_ctx.q))) {
		mtk_v4l2_debug(1, "[%d]-> q type %d out=%d cap=%d",
			       ctx->id, q->type,
			       vb2_is_streaming(&ctx->m2m_ctx->out_q_ctx.q),
			       vb2_is_streaming(&ctx->m2m_ctx->cap_q_ctx.q));
		return;
	}

	/* Release the encoder if both streams are stopped. */
	ret = venc_if_deinit(ctx);
	if (ret)
		mtk_v4l2_err("venc_if_deinit failed=%d", ret);

	ret = pm_runtime_put(&ctx->dev->plat_dev->dev);
	if (ret < 0)
		mtk_v4l2_err("pm_runtime_put fail %d", ret);

	ctx->state = MTK_STATE_FREE;
}

static int vb2ops_venc_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;
	return 0;
}

static const struct vb2_ops mtk_venc_vb2_ops = {
	.queue_setup		= vb2ops_venc_queue_setup,
	.buf_out_validate	= vb2ops_venc_buf_out_validate,
	.buf_prepare		= vb2ops_venc_buf_prepare,
	.buf_queue		= vb2ops_venc_buf_queue,
	.wait_prepare		= vb2_ops_wait_prepare,
	.wait_finish		= vb2_ops_wait_finish,
	.start_streaming	= vb2ops_venc_start_streaming,
	.stop_streaming		= vb2ops_venc_stop_streaming,
};

static int mtk_venc_encode_header(void *priv)
{
	struct mtk_vcodec_ctx *ctx = priv;
	int ret;
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct mtk_vcodec_mem bs_buf;
	struct venc_done_result enc_result;

	dst_buf = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
	if (!dst_buf) {
		mtk_v4l2_debug(1, "No dst buffer");
		return -EINVAL;
	}

	bs_buf.va = vb2_plane_vaddr(&dst_buf->vb2_buf, 0);
	bs_buf.dma_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	bs_buf.size = (size_t)dst_buf->vb2_buf.planes[0].length;

	mtk_v4l2_debug(1,
			"[%d] buf id=%d va=0x%p dma_addr=0x%llx size=%zu",
			ctx->id,
			dst_buf->vb2_buf.index, bs_buf.va,
			(u64)bs_buf.dma_addr,
			bs_buf.size);

	ret = venc_if_encode(ctx,
			VENC_START_OPT_ENCODE_SEQUENCE_HEADER,
			NULL, &bs_buf, &enc_result);

	if (ret) {
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, 0);
		ctx->state = MTK_STATE_ABORT;
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
		mtk_v4l2_err("venc_if_encode failed=%d", ret);
		return -EINVAL;
	}
	src_buf = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	if (src_buf) {
		dst_buf->vb2_buf.timestamp = src_buf->vb2_buf.timestamp;
		dst_buf->timecode = src_buf->timecode;
	} else {
		mtk_v4l2_err("No timestamp for the header buffer.");
	}

	ctx->state = MTK_STATE_HEADER;
	vb2_set_plane_payload(&dst_buf->vb2_buf, 0, enc_result.bs_size);
	v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);

	return 0;
}

static int mtk_venc_param_change(struct mtk_vcodec_ctx *ctx)
{
	struct venc_enc_param enc_prm;
	struct vb2_v4l2_buffer *vb2_v4l2 = v4l2_m2m_next_src_buf(ctx->m2m_ctx);
	struct mtk_video_enc_buf *mtk_buf;
	int ret = 0;

	/* Don't upcast the empty flush buffer */
	if (vb2_v4l2 == &ctx->empty_flush_buf.vb)
		return 0;

	mtk_buf = container_of(vb2_v4l2, struct mtk_video_enc_buf, m2m_buf.vb);

	memset(&enc_prm, 0, sizeof(enc_prm));
	if (mtk_buf->param_change == MTK_ENCODE_PARAM_NONE)
		return 0;

	if (mtk_buf->param_change & MTK_ENCODE_PARAM_BITRATE) {
		enc_prm.bitrate = mtk_buf->enc_params.bitrate;
		mtk_v4l2_debug(1, "[%d] id=%d, change param br=%d",
				ctx->id,
				vb2_v4l2->vb2_buf.index,
				enc_prm.bitrate);
		ret |= venc_if_set_param(ctx,
					 VENC_SET_PARAM_ADJUST_BITRATE,
					 &enc_prm);
	}
	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_FRAMERATE) {
		enc_prm.frm_rate = mtk_buf->enc_params.framerate_num /
				   mtk_buf->enc_params.framerate_denom;
		mtk_v4l2_debug(1, "[%d] id=%d, change param fr=%d",
			       ctx->id,
			       vb2_v4l2->vb2_buf.index,
			       enc_prm.frm_rate);
		ret |= venc_if_set_param(ctx,
					 VENC_SET_PARAM_ADJUST_FRAMERATE,
					 &enc_prm);
	}
	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_GOP_SIZE) {
		enc_prm.gop_size = mtk_buf->enc_params.gop_size;
		mtk_v4l2_debug(1, "change param intra period=%d",
			       enc_prm.gop_size);
		ret |= venc_if_set_param(ctx,
					 VENC_SET_PARAM_GOP_SIZE,
					 &enc_prm);
	}
	if (!ret && mtk_buf->param_change & MTK_ENCODE_PARAM_FORCE_INTRA) {
		mtk_v4l2_debug(1, "[%d] id=%d, change param force I=%d",
				ctx->id,
				vb2_v4l2->vb2_buf.index,
				mtk_buf->enc_params.force_intra);
		if (mtk_buf->enc_params.force_intra)
			ret |= venc_if_set_param(ctx,
						 VENC_SET_PARAM_FORCE_INTRA,
						 NULL);
	}

	mtk_buf->param_change = MTK_ENCODE_PARAM_NONE;

	if (ret) {
		ctx->state = MTK_STATE_ABORT;
		mtk_v4l2_err("venc_if_set_param %d failed=%d",
				mtk_buf->param_change, ret);
		return -1;
	}

	return 0;
}

/*
 * v4l2_m2m_streamoff() holds dev_mutex and waits mtk_venc_worker()
 * to call v4l2_m2m_job_finish().
 * If mtk_venc_worker() tries to acquire dev_mutex, it will deadlock.
 * So this function must not try to acquire dev->dev_mutex.
 * This means v4l2 ioctls and mtk_venc_worker() can run at the same time.
 * mtk_venc_worker() should be carefully implemented to avoid bugs.
 */
static void mtk_venc_worker(struct work_struct *work)
{
	struct mtk_vcodec_ctx *ctx = container_of(work, struct mtk_vcodec_ctx,
				    encode_work);
	struct vb2_v4l2_buffer *src_buf, *dst_buf;
	struct venc_frm_buf frm_buf;
	struct mtk_vcodec_mem bs_buf;
	struct venc_done_result enc_result;
	int ret, i;

	/* check dst_buf, dst_buf may be removed in device_run
	 * to stored encdoe header so we need check dst_buf and
	 * call job_finish here to prevent recursion
	 */
	dst_buf = v4l2_m2m_dst_buf_remove(ctx->m2m_ctx);
	if (!dst_buf) {
		v4l2_m2m_job_finish(ctx->dev->m2m_dev_enc, ctx->m2m_ctx);
		return;
	}

	src_buf = v4l2_m2m_src_buf_remove(ctx->m2m_ctx);

	/*
	 * If we see the flush buffer, send an empty buffer with the LAST flag
	 * to the client. is_flushing will be reset at the time the buffer
	 * is dequeued.
	 */
	if (src_buf == &ctx->empty_flush_buf.vb) {
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, 0);
		dst_buf->flags |= V4L2_BUF_FLAG_LAST;
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);
		v4l2_m2m_job_finish(ctx->dev->m2m_dev_enc, ctx->m2m_ctx);
		return;
	}

	memset(&frm_buf, 0, sizeof(frm_buf));
	for (i = 0; i < src_buf->vb2_buf.num_planes ; i++) {
		frm_buf.fb_addr[i].dma_addr =
				vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, i);
		frm_buf.fb_addr[i].size =
				(size_t)src_buf->vb2_buf.planes[i].length;
	}
	bs_buf.va = vb2_plane_vaddr(&dst_buf->vb2_buf, 0);
	bs_buf.dma_addr = vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	bs_buf.size = (size_t)dst_buf->vb2_buf.planes[0].length;

	mtk_v4l2_debug(2,
			"Framebuf PA=%llx Size=0x%zx;PA=0x%llx Size=0x%zx;PA=0x%llx Size=%zu",
			(u64)frm_buf.fb_addr[0].dma_addr,
			frm_buf.fb_addr[0].size,
			(u64)frm_buf.fb_addr[1].dma_addr,
			frm_buf.fb_addr[1].size,
			(u64)frm_buf.fb_addr[2].dma_addr,
			frm_buf.fb_addr[2].size);

	ret = venc_if_encode(ctx, VENC_START_OPT_ENCODE_FRAME,
			     &frm_buf, &bs_buf, &enc_result);

	dst_buf->vb2_buf.timestamp = src_buf->vb2_buf.timestamp;
	dst_buf->timecode = src_buf->timecode;

	if (enc_result.is_key_frm)
		dst_buf->flags |= V4L2_BUF_FLAG_KEYFRAME;

	if (ret) {
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, 0);
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
		mtk_v4l2_err("venc_if_encode failed=%d", ret);
	} else {
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, enc_result.bs_size);
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);
		mtk_v4l2_debug(2, "venc_if_encode bs size=%d",
				 enc_result.bs_size);
	}

	v4l2_m2m_job_finish(ctx->dev->m2m_dev_enc, ctx->m2m_ctx);

	mtk_v4l2_debug(1, "<=== src_buf[%d] dst_buf[%d] venc_if_encode ret=%d Size=%u===>",
			src_buf->vb2_buf.index, dst_buf->vb2_buf.index, ret,
			enc_result.bs_size);
}

static void m2mops_venc_device_run(void *priv)
{
	struct mtk_vcodec_ctx *ctx = priv;

	if ((ctx->q_data[MTK_Q_DATA_DST].fmt->fourcc == V4L2_PIX_FMT_H264) &&
	    (ctx->state != MTK_STATE_HEADER)) {
		/* encode h264 sps/pps header */
		mtk_venc_encode_header(ctx);
		queue_work(ctx->dev->encode_workqueue, &ctx->encode_work);
		return;
	}

	mtk_venc_param_change(ctx);
	queue_work(ctx->dev->encode_workqueue, &ctx->encode_work);
}

static int m2mops_venc_job_ready(void *m2m_priv)
{
	struct mtk_vcodec_ctx *ctx = m2m_priv;

	if (ctx->state == MTK_STATE_ABORT || ctx->state == MTK_STATE_FREE) {
		mtk_v4l2_debug(3, "[%d]Not ready: state=0x%x.",
			       ctx->id, ctx->state);
		return 0;
	}

	return 1;
}

static void m2mops_venc_job_abort(void *priv)
{
	struct mtk_vcodec_ctx *ctx = priv;

	ctx->state = MTK_STATE_ABORT;
}

const struct v4l2_m2m_ops mtk_venc_m2m_ops = {
	.device_run	= m2mops_venc_device_run,
	.job_ready	= m2mops_venc_job_ready,
	.job_abort	= m2mops_venc_job_abort,
};

void mtk_vcodec_enc_set_default_params(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_q_data *q_data;

	ctx->m2m_ctx->q_lock = &ctx->q_mutex;
	ctx->fh.m2m_ctx = ctx->m2m_ctx;
	ctx->fh.ctrl_handler = &ctx->ctrl_hdl;
	INIT_WORK(&ctx->encode_work, mtk_venc_worker);

	ctx->colorspace = V4L2_COLORSPACE_REC709;
	ctx->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	ctx->quantization = V4L2_QUANTIZATION_DEFAULT;
	ctx->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	q_data = &ctx->q_data[MTK_Q_DATA_SRC];
	memset(q_data, 0, sizeof(struct mtk_q_data));
	q_data->visible_width = DFT_CFG_WIDTH;
	q_data->visible_height = DFT_CFG_HEIGHT;
	q_data->coded_width = DFT_CFG_WIDTH;
	q_data->coded_height = DFT_CFG_HEIGHT;
	q_data->field = V4L2_FIELD_NONE;

	q_data->fmt = &ctx->dev->venc_pdata->output_formats[0];

	v4l_bound_align_image(&q_data->coded_width,
				MTK_VENC_MIN_W,
				MTK_VENC_HD_MAX_W, 4,
				&q_data->coded_height,
				MTK_VENC_MIN_H,
				MTK_VENC_HD_MAX_H, 5, 6);

	if (q_data->coded_width < DFT_CFG_WIDTH &&
		(q_data->coded_width + 16) <= MTK_VENC_HD_MAX_W)
		q_data->coded_width += 16;
	if (q_data->coded_height < DFT_CFG_HEIGHT &&
		(q_data->coded_height + 32) <= MTK_VENC_HD_MAX_H)
		q_data->coded_height += 32;

	q_data->sizeimage[0] =
		q_data->coded_width * q_data->coded_height+
		((ALIGN(q_data->coded_width, 16) * 2) * 16);
	q_data->bytesperline[0] = q_data->coded_width;
	q_data->sizeimage[1] =
		(q_data->coded_width * q_data->coded_height) / 2 +
		(ALIGN(q_data->coded_width, 16) * 16);
	q_data->bytesperline[1] = q_data->coded_width;

	q_data = &ctx->q_data[MTK_Q_DATA_DST];
	memset(q_data, 0, sizeof(struct mtk_q_data));
	q_data->coded_width = DFT_CFG_WIDTH;
	q_data->coded_height = DFT_CFG_HEIGHT;
	q_data->fmt = &ctx->dev->venc_pdata->capture_formats[0];
	q_data->field = V4L2_FIELD_NONE;
	ctx->q_data[MTK_Q_DATA_DST].sizeimage[0] =
		DFT_CFG_WIDTH * DFT_CFG_HEIGHT;
	ctx->q_data[MTK_Q_DATA_DST].bytesperline[0] = 0;

	ctx->enc_params.framerate_num = MTK_DEFAULT_FRAMERATE_NUM;
	ctx->enc_params.framerate_denom = MTK_DEFAULT_FRAMERATE_DENOM;
}

int mtk_vcodec_enc_ctrls_setup(struct mtk_vcodec_ctx *ctx)
{
	const struct v4l2_ctrl_ops *ops = &mtk_vcodec_enc_ctrl_ops;
	struct v4l2_ctrl_handler *handler = &ctx->ctrl_hdl;
	u8 h264_max_level;

	if (ctx->dev->enc_capability & MTK_VENC_4K_CAPABILITY_ENABLE)
		h264_max_level = V4L2_MPEG_VIDEO_H264_LEVEL_5_1;
	else
		h264_max_level = V4L2_MPEG_VIDEO_H264_LEVEL_4_2;

	v4l2_ctrl_handler_init(handler, MTK_MAX_CTRLS_HINT);

	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MIN_BUFFERS_FOR_OUTPUT,
			  1, 1, 1, 1);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_BITRATE,
			  ctx->dev->venc_pdata->min_bitrate,
			  ctx->dev->venc_pdata->max_bitrate, 1, 4000000);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_B_FRAMES,
			0, 2, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE,
			0, 1, 1, 1);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_H264_MAX_QP,
			0, 51, 1, 51);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_H264_I_PERIOD,
			0, 65535, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_GOP_SIZE,
			0, 65535, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE,
			0, 1, 1, 0);
	v4l2_ctrl_new_std(handler, ops, V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME,
			0, 0, 0, 0);
	v4l2_ctrl_new_std_menu(handler, ops,
			V4L2_CID_MPEG_VIDEO_HEADER_MODE,
			V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
			0, V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_H264_PROFILE,
			V4L2_MPEG_VIDEO_H264_PROFILE_HIGH,
			0, V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_H264_LEVEL,
			       h264_max_level,
			       0, V4L2_MPEG_VIDEO_H264_LEVEL_4_0);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_VP8_PROFILE,
			       V4L2_MPEG_VIDEO_VP8_PROFILE_0, 0, V4L2_MPEG_VIDEO_VP8_PROFILE_0);
	v4l2_ctrl_new_std_menu(handler, ops, V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
			       V4L2_MPEG_VIDEO_BITRATE_MODE_CBR,
			       ~(1 << V4L2_MPEG_VIDEO_BITRATE_MODE_CBR),
			       V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);


	if (handler->error) {
		mtk_v4l2_err("Init control handler fail %d",
				handler->error);
		return handler->error;
	}

	v4l2_ctrl_handler_setup(&ctx->ctrl_hdl);

	return 0;
}

int mtk_vcodec_enc_queue_init(void *priv, struct vb2_queue *src_vq,
			      struct vb2_queue *dst_vq)
{
	struct mtk_vcodec_ctx *ctx = priv;
	int ret;

	/* Note: VB2_USERPTR works with dma-contig because mt8173
	 * support iommu
	 * https://patchwork.kernel.org/patch/8335461/
	 * https://patchwork.kernel.org/patch/7596181/
	 */
	src_vq->type		= V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes	= VB2_DMABUF | VB2_MMAP | VB2_USERPTR;
	src_vq->drv_priv	= ctx;
	src_vq->buf_struct_size = sizeof(struct mtk_video_enc_buf);
	src_vq->ops		= &mtk_venc_vb2_ops;
	src_vq->mem_ops		= &vb2_dma_contig_memops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->lock		= &ctx->q_mutex;
	src_vq->dev		= &ctx->dev->plat_dev->dev;

	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type		= V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes	= VB2_DMABUF | VB2_MMAP | VB2_USERPTR;
	dst_vq->drv_priv	= ctx;
	dst_vq->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
	dst_vq->ops		= &mtk_venc_vb2_ops;
	dst_vq->mem_ops		= &vb2_dma_contig_memops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->lock		= &ctx->q_mutex;
	dst_vq->dev		= &ctx->dev->plat_dev->dev;

	return vb2_queue_init(dst_vq);
}

int mtk_venc_unlock(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_vcodec_dev *dev = ctx->dev;

	mutex_unlock(&dev->enc_mutex);
	return 0;
}

int mtk_venc_lock(struct mtk_vcodec_ctx *ctx)
{
	struct mtk_vcodec_dev *dev = ctx->dev;

	mutex_lock(&dev->enc_mutex);
	return 0;
}

void mtk_vcodec_enc_release(struct mtk_vcodec_ctx *ctx)
{
	int ret = venc_if_deinit(ctx);

	if (ret)
		mtk_v4l2_err("venc_if_deinit failed=%d", ret);

	ctx->state = MTK_STATE_FREE;
}
