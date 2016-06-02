/*
 * Rockchip VPU codec driver
 *
 * Copyright (C) 2014 Rockchip Electronics Co., Ltd.
 *	Hertz Wong <hertz.wong@rock-chips.com>
 *
 * Copyright (C) 2014 Google, Inc.
 *	Tomasz Figa <tfiga@chromium.org>
 *
 * Based on s5p-mfc driver by Samsung Electronics Co., Ltd.
 *
 * Copyright (C) 2010-2011 Samsung Electronics Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "rockchip_vpu_common.h"

#include <linux/module.h>
#include <linux/version.h>
#include <linux/videodev2.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-sg.h>

#include "rockchip_vpu_dec.h"
#include "rockchip_vpu_hw.h"

#define DEF_SRC_FMT_DEC				V4L2_PIX_FMT_H264_SLICE
#define DEF_DST_FMT_DEC				V4L2_PIX_FMT_NV12

#define ROCKCHIP_DEC_MIN_WIDTH			48U
#define ROCKCHIP_DEC_MAX_WIDTH			3840U
#define ROCKCHIP_DEC_MIN_HEIGHT			48U
#define ROCKCHIP_DEC_MAX_HEIGHT			2160U

#define ROCKCHIP_H264_MAX_SLICES_PER_FRAME	16

static struct rockchip_vpu_fmt formats[] = {
	{
		.name = "4:2:0 1 plane Y/CbCr",
		.fourcc = V4L2_PIX_FMT_NV12,
		.codec_mode = RK_VPU_CODEC_NONE,
		.num_planes = 1,
		.depth = { 12 },
	},
	{
		.name = "Slices of H264 Encoded Stream(RK3288)",
		.fourcc = V4L2_PIX_FMT_H264_SLICE,
		.codec_mode = RK3288_VPU_CODEC_H264D,
		.num_planes = 1,
	},
	{
		.name = "Frames of VP8 Encoded Stream(RK3288)",
		.fourcc = V4L2_PIX_FMT_VP8_FRAME,
		.codec_mode = RK3288_VPU_CODEC_VP8D,
		.num_planes = 1,
	},
};

static struct rockchip_vpu_fmt *find_format(struct rockchip_vpu_dev *dev,
					    u32 fourcc, bool bitstream)
{
	unsigned int i;

	vpu_debug_enter();

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (!rockchip_vpu_dev_codec_support(dev, formats[i].codec_mode))
			continue;
		if (formats[i].fourcc == fourcc &&
		    !!bitstream == (formats[i].codec_mode != RK_VPU_CODEC_NONE))
			return &formats[i];
	}

	return NULL;
}

/* Indices of controls that need to be accessed directly. */
enum {
	ROCKCHIP_VPU_DEC_CTRL_H264_SPS,
	ROCKCHIP_VPU_DEC_CTRL_H264_PPS,
	ROCKCHIP_VPU_DEC_CTRL_H264_SCALING_MATRIX,
	ROCKCHIP_VPU_DEC_CTRL_H264_SLICE_PARAM,
	ROCKCHIP_VPU_DEC_CTRL_H264_DECODE_PARAM,
	ROCKCHIP_VPU_DEC_CTRL_VP8_FRAME_HDR,
};

static struct rockchip_vpu_control controls[] = {
	/* H264 slice-based interface. */
	[ROCKCHIP_VPU_DEC_CTRL_H264_SPS] = {
		.id = V4L2_CID_MPEG_VIDEO_H264_SPS,
		.type = V4L2_CTRL_TYPE_PRIVATE,
		.name = "H264 SPS Parameters",
		.elem_size = sizeof(struct v4l2_ctrl_h264_sps),
		.max_stores = VIDEO_MAX_FRAME,
		.can_store = true,
	},
	[ROCKCHIP_VPU_DEC_CTRL_H264_PPS] = {
		.id = V4L2_CID_MPEG_VIDEO_H264_PPS,
		.type = V4L2_CTRL_TYPE_PRIVATE,
		.name = "H264 PPS Parameters",
		.elem_size = sizeof(struct v4l2_ctrl_h264_pps),
		.max_stores = VIDEO_MAX_FRAME,
		.can_store = true,
	},
	[ROCKCHIP_VPU_DEC_CTRL_H264_SCALING_MATRIX] = {
		.id = V4L2_CID_MPEG_VIDEO_H264_SCALING_MATRIX,
		.type = V4L2_CTRL_TYPE_PRIVATE,
		.name = "H264 Scaling Matrix",
		.elem_size = sizeof(struct v4l2_ctrl_h264_scaling_matrix),
		.max_stores = VIDEO_MAX_FRAME,
		.can_store = true,
	},
	[ROCKCHIP_VPU_DEC_CTRL_H264_SLICE_PARAM] = {
		.id = V4L2_CID_MPEG_VIDEO_H264_SLICE_PARAM,
		.type = V4L2_CTRL_TYPE_PRIVATE,
		.name = "H264 Slice Parameters",
		.max_stores = VIDEO_MAX_FRAME,
		.elem_size = sizeof(struct v4l2_ctrl_h264_slice_param),
		.dims = { ROCKCHIP_H264_MAX_SLICES_PER_FRAME, },
		.can_store = true,
	},
	[ROCKCHIP_VPU_DEC_CTRL_H264_DECODE_PARAM] = {
		.id = V4L2_CID_MPEG_VIDEO_H264_DECODE_PARAM,
		.type = V4L2_CTRL_TYPE_PRIVATE,
		.name = "H264 Decode Parameters",
		.max_stores = VIDEO_MAX_FRAME,
		.elem_size = sizeof(struct v4l2_ctrl_h264_decode_param),
		.can_store = true,
	},
	[ROCKCHIP_VPU_DEC_CTRL_VP8_FRAME_HDR] = {
		.id = V4L2_CID_MPEG_VIDEO_VP8_FRAME_HDR,
		.type = V4L2_CTRL_TYPE_PRIVATE,
		.name = "VP8 Frame Header Parameters",
		.max_stores = VIDEO_MAX_FRAME,
		.elem_size = sizeof(struct v4l2_ctrl_vp8_frame_hdr),
		.can_store = true,
	},
};

static inline const void *get_ctrl_ptr(struct rockchip_vpu_ctx *ctx, unsigned id)
{
	struct v4l2_ctrl *ctrl = ctx->ctrls[id];

	return ctrl->p_cur.p;
}

/* Query capabilities of the device */
static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct rockchip_vpu_dev *dev = video_drvdata(file);

	vpu_debug_enter();

	strlcpy(cap->driver, ROCKCHIP_VPU_DEC_NAME, sizeof(cap->driver));
	strlcpy(cap->card, dev->pdev->name, sizeof(cap->card));
	strlcpy(cap->bus_info, "platform:" ROCKCHIP_VPU_NAME,
		sizeof(cap->bus_info));

	/*
	 * This is only a mem-to-mem video device. The capture and output
	 * device capability flags are left only for backward compatibility
	 * and are scheduled for removal.
	 */
	cap->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING |
	    V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE;
	cap->capabilities = cap->device_caps | V4L2_CAP_DEVICE_CAPS;

	vpu_debug_leave();

	return 0;
}

static int vidioc_enum_framesizes(struct file *file, void *prov,
				  struct v4l2_frmsizeenum *fsize)
{
	struct rockchip_vpu_dev *dev = video_drvdata(file);
	struct v4l2_frmsize_stepwise *s = &fsize->stepwise;
	struct rockchip_vpu_fmt *fmt;

	if (fsize->index != 0) {
		vpu_debug(0, "invalid frame size index (expected 0, got %d)\n",
				fsize->index);
		return -EINVAL;
	}

	fmt = find_format(dev, fsize->pixel_format, true);
	if (!fmt) {
		vpu_debug(0, "unsupported bitstream format (%08x)\n",
				fsize->pixel_format);
		return -EINVAL;
	}

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;

	s->min_width = ROCKCHIP_DEC_MIN_WIDTH;
	s->max_width = ROCKCHIP_DEC_MAX_WIDTH;
	s->step_width = MB_DIM;
	s->min_height = ROCKCHIP_DEC_MIN_HEIGHT;
	s->max_height = ROCKCHIP_DEC_MAX_HEIGHT;
	s->step_height = MB_DIM;

	return 0;
}

static int vidioc_enum_fmt(struct file *file, struct v4l2_fmtdesc *f, bool out)
{
	struct rockchip_vpu_dev *dev = video_drvdata(file);
	struct rockchip_vpu_fmt *fmt;
	int i, j = 0;

	vpu_debug_enter();

	for (i = 0; i < ARRAY_SIZE(formats); ++i) {
		if (!rockchip_vpu_dev_codec_support(dev, formats[i].codec_mode))
			continue;
		if (out && formats[i].codec_mode == RK_VPU_CODEC_NONE)
			continue;
		else if (!out && (formats[i].codec_mode != RK_VPU_CODEC_NONE))
			continue;

		if (j == f->index) {
			fmt = &formats[i];
			strlcpy(f->description, fmt->name,
				sizeof(f->description));
			f->pixelformat = fmt->fourcc;

			f->flags = 0;
			if (formats[i].codec_mode != RK_VPU_CODEC_NONE)
				f->flags |= V4L2_FMT_FLAG_COMPRESSED;

			vpu_debug_leave();

			return 0;
		}

		++j;
	}

	vpu_debug_leave();

	return -EINVAL;
}

static int vidioc_enum_fmt_vid_cap_mplane(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(file, f, false);
}

static int vidioc_enum_fmt_vid_out_mplane(struct file *file, void *priv,
					  struct v4l2_fmtdesc *f)
{
	return vidioc_enum_fmt(file, f, true);
}

static int vidioc_g_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);

	vpu_debug_enter();

	vpu_debug(4, "f->type = %d\n", f->type);

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		f->fmt.pix_mp = ctx->dst_fmt;
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		f->fmt.pix_mp = ctx->src_fmt;
		break;

	default:
		vpu_err("invalid buf type\n");
		return -EINVAL;
	}

	vpu_debug_leave();

	return 0;
}

static int vidioc_try_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct rockchip_vpu_dev *dev = video_drvdata(file);
	struct rockchip_vpu_fmt *fmt;
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;
	char str[5];

	vpu_debug_enter();

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		vpu_debug(4, "%s\n", fmt2str(f->fmt.pix_mp.pixelformat, str));

		fmt = find_format(dev, pix_fmt_mp->pixelformat, true);
		if (!fmt) {
			vpu_err("failed to try output format\n");
			return -EINVAL;
		}

		if (pix_fmt_mp->plane_fmt[0].sizeimage == 0) {
			vpu_err("sizeimage of output format must be given\n");
			return -EINVAL;
		}

		pix_fmt_mp->plane_fmt[0].bytesperline = 0;
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		vpu_debug(4, "%s\n", fmt2str(f->fmt.pix_mp.pixelformat, str));

		fmt = find_format(dev, pix_fmt_mp->pixelformat, false);
		if (!fmt) {
			vpu_err("failed to try capture format\n");
			return -EINVAL;
		}

		if (fmt->num_planes != pix_fmt_mp->num_planes) {
			vpu_err("plane number mismatches on capture format\n");
			return -EINVAL;
		}

		/* Limit to hardware min/max. */
		pix_fmt_mp->width = clamp(pix_fmt_mp->width,
				ROCKCHIP_DEC_MIN_WIDTH, ROCKCHIP_DEC_MAX_WIDTH);
		pix_fmt_mp->height = clamp(pix_fmt_mp->height,
				ROCKCHIP_DEC_MIN_HEIGHT, ROCKCHIP_DEC_MAX_HEIGHT);

		/* Round up to macroblocks. */
		pix_fmt_mp->width = round_up(pix_fmt_mp->width, MB_DIM);
		pix_fmt_mp->height = round_up(pix_fmt_mp->height, MB_DIM);
		break;

	default:
		vpu_err("invalid buf type\n");
		return -EINVAL;
	}

	vpu_debug_leave();

	return 0;
}

static int vidioc_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	struct rockchip_vpu_dev *dev = ctx->dev;
	unsigned int mb_width, mb_height;
	struct rockchip_vpu_fmt *fmt;
	int ret = 0;
	int i;

	vpu_debug_enter();

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		/* Change not allowed if any queue is streaming. */
		if (vb2_is_streaming(&ctx->vq_src)
		    || vb2_is_streaming(&ctx->vq_dst)) {
			ret = -EBUSY;
			goto out;
		}
		/*
		 * Pixel format change is not allowed when the other queue has
		 * buffers allocated.
		 */
		if (vb2_is_busy(&ctx->vq_dst)
		    && pix_fmt_mp->pixelformat != ctx->src_fmt.pixelformat) {
			ret = -EBUSY;
			goto out;
		}

		ret = vidioc_try_fmt(file, priv, f);
		if (ret)
			goto out;

		ctx->vpu_src_fmt = find_format(dev, pix_fmt_mp->pixelformat, true);
		ctx->src_fmt = *pix_fmt_mp;
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		/*
		 * Change not allowed if this queue is streaming.
		 *
		 * NOTE: We allow changes with source queue streaming
		 * to support resolution change in decoded stream.
		 */
		if (vb2_is_streaming(&ctx->vq_dst)) {
			ret = -EBUSY;
			goto out;
		}
		/*
		 * Pixel format change is not allowed when the other queue has
		 * buffers allocated.
		 */
		if (vb2_is_busy(&ctx->vq_src)
		    && pix_fmt_mp->pixelformat != ctx->dst_fmt.pixelformat) {
			ret = -EBUSY;
			goto out;
		}

		ret = vidioc_try_fmt(file, priv, f);
		if (ret)
			goto out;

		fmt = find_format(dev, pix_fmt_mp->pixelformat, false);
		ctx->vpu_dst_fmt = fmt;

		mb_width = MB_WIDTH(pix_fmt_mp->width);
		mb_height = MB_HEIGHT(pix_fmt_mp->height);

		vpu_debug(0, "CAPTURE codec mode: %d\n", fmt->codec_mode);
		vpu_debug(0, "fmt - w: %d, h: %d, mb - w: %d, h: %d\n",
			  pix_fmt_mp->width, pix_fmt_mp->height,
			  mb_width, mb_height);

		for (i = 0; i < fmt->num_planes; ++i) {
			pix_fmt_mp->plane_fmt[i].bytesperline =
				mb_width * MB_DIM * fmt->depth[i] / 8;
			pix_fmt_mp->plane_fmt[i].sizeimage =
				pix_fmt_mp->plane_fmt[i].bytesperline
				* mb_height * MB_DIM;
			/*
			 * All of multiplanar formats we support have chroma
			 * planes subsampled by 2.
			 */
			if (i != 0)
				pix_fmt_mp->plane_fmt[i].sizeimage /= 2;
		}

		ctx->dst_fmt = *pix_fmt_mp;
		break;

	default:
		vpu_err("invalid buf type\n");
		return -EINVAL;
	}

out:
	vpu_debug_leave();

	return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
			  struct v4l2_requestbuffers *reqbufs)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	int ret;

	vpu_debug_enter();

	switch (reqbufs->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		ret = vb2_reqbufs(&ctx->vq_src, reqbufs);
		if (ret != 0) {
			vpu_err("error in vb2_reqbufs() for E(S)\n");
			goto out;
		}
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		ret = vb2_reqbufs(&ctx->vq_dst, reqbufs);
		if (ret != 0) {
			vpu_err("error in vb2_reqbufs() for E(D)\n");
			goto out;
		}
		break;

	default:
		vpu_err("invalid buf type\n");
		ret = -EINVAL;
		goto out;
	}

out:
	vpu_debug_leave();

	return ret;
}

static int vidioc_querybuf(struct file *file, void *priv,
			   struct v4l2_buffer *buf)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	int ret;

	vpu_debug_enter();

	switch (buf->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		ret = vb2_querybuf(&ctx->vq_dst, buf);
		if (ret != 0) {
			vpu_err("error in vb2_querybuf() for E(D)\n");
			goto out;
		}

		buf->m.planes[0].m.mem_offset += DST_QUEUE_OFF_BASE;
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		ret = vb2_querybuf(&ctx->vq_src, buf);
		if (ret != 0) {
			vpu_err("error in vb2_querybuf() for E(S)\n");
			goto out;
		}
		break;

	default:
		vpu_err("invalid buf type\n");
		ret = -EINVAL;
		goto out;
	}

out:
	vpu_debug_leave();

	return ret;
}

/* Queue a buffer */
static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	int ret;
	int i;

	vpu_debug_enter();

	for (i = 0; i < buf->length; i++)
		vpu_debug(4, "plane[%d]->length %d bytesused %d\n",
				i, buf->m.planes[i].length,
				buf->m.planes[i].bytesused);

	switch (buf->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		ret = vb2_qbuf(&ctx->vq_src, buf);
		vpu_debug(4, "OUTPUT_MPLANE : vb2_qbuf return %d\n", ret);
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		ret = vb2_qbuf(&ctx->vq_dst, buf);
		vpu_debug(4, "CAPTURE_MPLANE: vb2_qbuf return %d\n", ret);
		break;

	default:
		ret = -EINVAL;
	}

	vpu_debug_leave();

	return ret;
}

/* Dequeue a buffer */
static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	int ret;

	vpu_debug_enter();

	switch (buf->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		ret = vb2_dqbuf(&ctx->vq_src, buf, file->f_flags & O_NONBLOCK);
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		ret = vb2_dqbuf(&ctx->vq_dst, buf, file->f_flags & O_NONBLOCK);
		break;

	default:
		ret = -EINVAL;
	}

	vpu_debug_leave();

	return ret;
}

/* Export DMA buffer */
static int vidioc_expbuf(struct file *file, void *priv,
			 struct v4l2_exportbuffer *eb)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	int ret;

	vpu_debug_enter();

	switch (eb->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		ret = vb2_expbuf(&ctx->vq_src, eb);
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		ret = vb2_expbuf(&ctx->vq_dst, eb);
		break;

	default:
		ret = -EINVAL;
	}

	vpu_debug_leave();

	return ret;
}

/* Stream on */
static int vidioc_streamon(struct file *file, void *priv,
			   enum v4l2_buf_type type)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	int ret;

	vpu_debug_enter();

	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		ret = vb2_streamon(&ctx->vq_src, type);
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		ret = vb2_streamon(&ctx->vq_dst, type);
		break;

	default:
		ret = -EINVAL;
	}

	vpu_debug_leave();

	return ret;
}

/* Stream off, which equals to a pause */
static int vidioc_streamoff(struct file *file, void *priv,
			    enum v4l2_buf_type type)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	int ret;

	vpu_debug_enter();

	switch (type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		ret = vb2_streamoff(&ctx->vq_src, type);
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		ret = vb2_streamoff(&ctx->vq_dst, type);
		break;

	default:
		ret = -EINVAL;
	}

	vpu_debug_leave();

	return ret;
}

static void rockchip_vpu_dec_set_dpb(struct rockchip_vpu_ctx *ctx,
					    struct v4l2_ctrl *ctrl)
{
	struct v4l2_ctrl_h264_decode_param *dec_param = ctrl->p_new.p;
	const struct v4l2_h264_dpb_entry *new_dpb_entry;
	u8 *dpb_map = ctx->run.h264d.dpb_map;
	struct v4l2_h264_dpb_entry *cur_dpb_entry;
	DECLARE_BITMAP(used, ARRAY_SIZE(ctx->run.h264d.dpb)) = { 0, };
	DECLARE_BITMAP(new, ARRAY_SIZE(dec_param->dpb)) = { 0, };
	int i, j;

	BUILD_BUG_ON(ARRAY_SIZE(ctx->run.h264d.dpb) !=
						ARRAY_SIZE(dec_param->dpb));

	/* Disable all entries by default. */
	for (j = 0; j < ARRAY_SIZE(ctx->run.h264d.dpb); ++j) {
		cur_dpb_entry = &ctx->run.h264d.dpb[j];

		cur_dpb_entry->flags &= ~V4L2_H264_DPB_ENTRY_FLAG_ACTIVE;
	}

	/* Try to match new DPB entries with existing ones by their POCs. */
	for (i = 0; i < ARRAY_SIZE(dec_param->dpb); ++i) {
		new_dpb_entry = &dec_param->dpb[i];

		if (!(new_dpb_entry->flags & V4L2_H264_DPB_ENTRY_FLAG_ACTIVE))
			continue;

		/*
		 * To cut off some comparisons, iterate only on target DPB
		 * entries which are not used yet.
		 */
		for_each_clear_bit(j, used, ARRAY_SIZE(ctx->run.h264d.dpb)) {
			cur_dpb_entry = &ctx->run.h264d.dpb[j];

			if (new_dpb_entry->top_field_order_cnt ==
					cur_dpb_entry->top_field_order_cnt &&
			    new_dpb_entry->bottom_field_order_cnt ==
					cur_dpb_entry->bottom_field_order_cnt) {
				memcpy(cur_dpb_entry, new_dpb_entry,
					sizeof(*cur_dpb_entry));
				set_bit(j, used);
				dpb_map[i] = j;
				break;
			}
		}

		if (j == ARRAY_SIZE(ctx->run.h264d.dpb))
			set_bit(i, new);
	}

	/* For entries that could not be matched, use remaining free slots. */
	for_each_set_bit(i, new, ARRAY_SIZE(dec_param->dpb)) {
		new_dpb_entry = &dec_param->dpb[i];

		j = find_first_zero_bit(used, ARRAY_SIZE(ctx->run.h264d.dpb));
		/*
		 * Both arrays are of the same sizes, so there is no way
		 * we can end up with no space in target array, unless
		 * something is buggy.
		 */
		if (WARN_ON(j >= ARRAY_SIZE(ctx->run.h264d.dpb)))
			return;

		cur_dpb_entry = &ctx->run.h264d.dpb[j];
		memcpy(cur_dpb_entry, new_dpb_entry, sizeof(*cur_dpb_entry));
		set_bit(j, used);
		dpb_map[i] = j;
	}

	/*
	 * Verify that reference picture lists are in range, since they
	 * will be indexing dpb_map[] when programming the hardware.
	 *
	 * Fallback to 0 should be safe, as we will get at most corrupt
	 * decoding result, without any serious side effects. Moreover,
	 * even if entry 0 is unused, the hardware programming code will
	 * handle this properly.
	 */
	for (i = 0; i < ARRAY_SIZE(dec_param->ref_pic_list_b0); ++i)
		if (dec_param->ref_pic_list_b0[i]
		    >= ARRAY_SIZE(ctx->run.h264d.dpb_map))
			dec_param->ref_pic_list_b0[i] = 0;
	for (i = 0; i < ARRAY_SIZE(dec_param->ref_pic_list_b1); ++i)
		if (dec_param->ref_pic_list_b1[i]
		    >= ARRAY_SIZE(ctx->run.h264d.dpb_map))
			dec_param->ref_pic_list_b1[i] = 0;
	for (i = 0; i < ARRAY_SIZE(dec_param->ref_pic_list_p0); ++i)
		if (dec_param->ref_pic_list_p0[i]
		    >= ARRAY_SIZE(ctx->run.h264d.dpb_map))
			dec_param->ref_pic_list_p0[i] = 0;
}

static int rockchip_vpu_dec_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct rockchip_vpu_ctx *ctx = ctrl_to_ctx(ctrl);
	struct rockchip_vpu_dev *dev = ctx->dev;
	int ret = 0;

	vpu_debug_enter();

	vpu_debug(4, "ctrl id %d\n", ctrl->id);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_H264_SPS:
	case V4L2_CID_MPEG_VIDEO_H264_PPS:
	case V4L2_CID_MPEG_VIDEO_H264_SCALING_MATRIX:
	case V4L2_CID_MPEG_VIDEO_H264_SLICE_PARAM:
	case V4L2_CID_MPEG_VIDEO_VP8_FRAME_HDR:
		/* These controls are used directly. */
		break;

	case V4L2_CID_MPEG_VIDEO_H264_DECODE_PARAM:
		if (ctrl->store)
			break;
		rockchip_vpu_dec_set_dpb(ctx, ctrl);
		break;

	default:
		v4l2_err(&dev->v4l2_dev, "Invalid control, id=%d, val=%d\n",
			 ctrl->id, ctrl->val);
		ret = -EINVAL;
	}

	vpu_debug_leave();

	return ret;
}

static const struct v4l2_ctrl_ops rockchip_vpu_dec_ctrl_ops = {
	.s_ctrl = rockchip_vpu_dec_s_ctrl,
};

static const struct v4l2_ioctl_ops rockchip_vpu_dec_ioctl_ops = {
	.vidioc_querycap = vidioc_querycap,
	.vidioc_enum_framesizes = vidioc_enum_framesizes,
	.vidioc_enum_fmt_vid_cap_mplane = vidioc_enum_fmt_vid_cap_mplane,
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
	.vidioc_expbuf = vidioc_expbuf,
	.vidioc_streamon = vidioc_streamon,
	.vidioc_streamoff = vidioc_streamoff,
};

static int rockchip_vpu_queue_setup(struct vb2_queue *vq,
				  const void *parg,
				  unsigned int *buf_count,
				  unsigned int *plane_count,
				  unsigned int psize[], void *allocators[])
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(vq->drv_priv);
	int ret = 0;

	vpu_debug_enter();

	switch (vq->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		*plane_count = ctx->vpu_src_fmt->num_planes;

		if (*buf_count < 1)
			*buf_count = 1;

		if (*buf_count > VIDEO_MAX_FRAME)
			*buf_count = VIDEO_MAX_FRAME;

		psize[0] = ctx->src_fmt.plane_fmt[0].sizeimage;
		allocators[0] = ctx->dev->alloc_ctx;
		vpu_debug(0, "output psize[%d]: %d\n", 0, psize[0]);
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		*plane_count = ctx->vpu_dst_fmt->num_planes;

		if (*buf_count < 1)
			*buf_count = 1;

		if (*buf_count > VIDEO_MAX_FRAME)
			*buf_count = VIDEO_MAX_FRAME;

		psize[0] = round_up(ctx->dst_fmt.plane_fmt[0].sizeimage, 8);
		allocators[0] = ctx->dev->alloc_ctx;

		if (ctx->vpu_src_fmt->fourcc == V4L2_PIX_FMT_H264_SLICE)
			/* Add space for appended motion vectors. */
			psize[0] += 64 * MB_WIDTH(ctx->dst_fmt.width)
					* MB_HEIGHT(ctx->dst_fmt.height);

		vpu_debug(0, "capture psize[%d]: %d\n", 0, psize[0]);
		break;

	default:
		vpu_err("invalid queue type: %d\n", vq->type);
		ret = -EINVAL;
	}

	vpu_debug_leave();

	return ret;
}

static int rockchip_vpu_buf_init(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(vq->drv_priv);

	vpu_debug_enter();

	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		ctx->dst_bufs[vb->index] = vb;

	vpu_debug_leave();

	return 0;
}

static void rockchip_vpu_buf_cleanup(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(vq->drv_priv);

	vpu_debug_enter();

	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		ctx->dst_bufs[vb->index] = NULL;

	vpu_debug_leave();
}

static int rockchip_vpu_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(vq->drv_priv);
	int ret = 0;
	int i;

	vpu_debug_enter();

	switch (vq->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		vpu_debug(4, "plane size: %ld, dst size: %d\n",
				vb2_plane_size(vb, 0),
				ctx->src_fmt.plane_fmt[0].sizeimage);

		if (vb2_plane_size(vb, 0)
		    < ctx->src_fmt.plane_fmt[0].sizeimage) {
			vpu_err("plane size is too small for output\n");
			ret = -EINVAL;
		}
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		for (i = 0; i < ctx->vpu_dst_fmt->num_planes; ++i) {
			vpu_debug(4, "plane %d size: %ld, sizeimage: %u\n", i,
					vb2_plane_size(vb, i),
					ctx->dst_fmt.plane_fmt[i].sizeimage);

			if (vb2_plane_size(vb, i)
			    < ctx->dst_fmt.plane_fmt[i].sizeimage) {
				vpu_err("size of plane %d is too small for capture\n",
					i);
				break;
			}
		}

		if (i != ctx->vpu_dst_fmt->num_planes)
			ret = -EINVAL;
		break;

	default:
		vpu_err("invalid queue type: %d\n", vq->type);
		ret = -EINVAL;
	}

	vpu_debug_leave();

	return ret;
}

static int rockchip_vpu_start_streaming(struct vb2_queue *q, unsigned int count)
{
	int ret = 0;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(q->drv_priv);
	struct rockchip_vpu_dev *dev = ctx->dev;
	bool ready = false;

	vpu_debug_enter();

	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		ret = rockchip_vpu_init(ctx);
		if (ret < 0) {
			vpu_err("rockchip_vpu_init failed\n");
			return ret;
		}

		ready = vb2_is_streaming(&ctx->vq_src);
	} else if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ready = vb2_is_streaming(&ctx->vq_dst);
	}

	if (ready)
		rockchip_vpu_try_context(dev, ctx);

	vpu_debug_leave();

	return 0;
}

static void rockchip_vpu_stop_streaming(struct vb2_queue *q)
{
	unsigned long flags;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(q->drv_priv);
	struct rockchip_vpu_dev *dev = ctx->dev;
	struct rockchip_vpu_buf *b;
	LIST_HEAD(queue);
	int i;

	vpu_debug_enter();

	spin_lock_irqsave(&dev->irqlock, flags);

	list_del_init(&ctx->list);

	switch (q->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		list_splice_init(&ctx->dst_queue, &queue);
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		list_splice_init(&ctx->src_queue, &queue);
		break;

	default:
		break;
	}

	spin_unlock_irqrestore(&dev->irqlock, flags);

	wait_event(dev->run_wq, dev->current_ctx != ctx);

	while (!list_empty(&queue)) {
		b = list_first_entry(&queue, struct rockchip_vpu_buf, list);
		for (i = 0; i < b->vb.vb2_buf.num_planes; i++)
			vb2_set_plane_payload(&b->vb.vb2_buf, i, 0);
		vb2_buffer_done(&b->vb.vb2_buf, VB2_BUF_STATE_ERROR);
		list_del(&b->list);
	}

	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		rockchip_vpu_deinit(ctx);

	vpu_debug_leave();
}

static void rockchip_vpu_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(vq->drv_priv);
	struct rockchip_vpu_dev *dev = ctx->dev;
	struct rockchip_vpu_buf *vpu_buf;
	unsigned long flags;

	vpu_debug_enter();

	switch (vq->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		vpu_buf = vb_to_buf(vb);

		/* Mark destination as available for use by VPU */
		spin_lock_irqsave(&dev->irqlock, flags);

		list_add_tail(&vpu_buf->list, &ctx->dst_queue);

		spin_unlock_irqrestore(&dev->irqlock, flags);
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		vpu_buf = vb_to_buf(vb);

		spin_lock_irqsave(&dev->irqlock, flags);

		list_add_tail(&vpu_buf->list, &ctx->src_queue);

		spin_unlock_irqrestore(&dev->irqlock, flags);
		break;

	default:
		vpu_err("unsupported buffer type (%d)\n", vq->type);
	}

	if (vb2_is_streaming(&ctx->vq_src) && vb2_is_streaming(&ctx->vq_dst))
		rockchip_vpu_try_context(dev, ctx);

	vpu_debug_leave();
}

static struct vb2_ops rockchip_vpu_dec_qops = {
	.queue_setup = rockchip_vpu_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.buf_init = rockchip_vpu_buf_init,
	.buf_prepare = rockchip_vpu_buf_prepare,
	.buf_cleanup = rockchip_vpu_buf_cleanup,
	.start_streaming = rockchip_vpu_start_streaming,
	.stop_streaming = rockchip_vpu_stop_streaming,
	.buf_queue = rockchip_vpu_buf_queue,
};

struct vb2_ops *get_dec_queue_ops(void)
{
	return &rockchip_vpu_dec_qops;
}

const struct v4l2_ioctl_ops *get_dec_v4l2_ioctl_ops(void)
{
	return &rockchip_vpu_dec_ioctl_ops;
}

static void rockchip_vpu_dec_prepare_run(struct rockchip_vpu_ctx *ctx)
{
	struct vb2_v4l2_buffer *src = to_vb2_v4l2_buffer(&ctx->run.src->vb.vb2_buf);

	v4l2_ctrl_apply_store(&ctx->ctrl_handler, src->config_store);

	if (ctx->vpu_src_fmt->fourcc == V4L2_PIX_FMT_H264_SLICE) {
		ctx->run.h264d.sps = get_ctrl_ptr(ctx,
										  ROCKCHIP_VPU_DEC_CTRL_H264_SPS);
		ctx->run.h264d.pps = get_ctrl_ptr(ctx,
										  ROCKCHIP_VPU_DEC_CTRL_H264_PPS);
		ctx->run.h264d.scaling_matrix = get_ctrl_ptr(ctx,
										ROCKCHIP_VPU_DEC_CTRL_H264_SCALING_MATRIX);
		ctx->run.h264d.slice_param = get_ctrl_ptr(ctx,
									 ROCKCHIP_VPU_DEC_CTRL_H264_SLICE_PARAM);
		ctx->run.h264d.decode_param = get_ctrl_ptr(ctx,
									  ROCKCHIP_VPU_DEC_CTRL_H264_DECODE_PARAM);
	} else if (ctx->vpu_src_fmt->fourcc == V4L2_PIX_FMT_VP8_FRAME) {
		ctx->run.vp8d.frame_hdr = get_ctrl_ptr(ctx,
											   ROCKCHIP_VPU_DEC_CTRL_VP8_FRAME_HDR);
	}
}

static void rockchip_vpu_dec_run_done(struct rockchip_vpu_ctx *ctx,
				    enum vb2_buffer_state result)
{
	struct v4l2_plane_pix_format *plane_fmts = ctx->dst_fmt.plane_fmt;
	struct vb2_buffer *dst = &ctx->run.dst->vb.vb2_buf;
	int i;

	if (result != VB2_BUF_STATE_DONE) {
		/* Assume no payload after failed run. */
		for (i = 0; i < dst->num_planes; ++i)
			vb2_set_plane_payload(dst, i, 0);
		return;
	}

	for (i = 0; i < dst->num_planes; ++i)
		vb2_set_plane_payload(dst, i, plane_fmts[i].sizeimage);
}

static const struct rockchip_vpu_run_ops rockchip_vpu_dec_run_ops = {
	.prepare_run = rockchip_vpu_dec_prepare_run,
	.run_done = rockchip_vpu_dec_run_done,
};

int rockchip_vpu_dec_init(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_dev *vpu = ctx->dev;

	ctx->vpu_src_fmt = find_format(vpu, DEF_SRC_FMT_DEC, false);
	ctx->vpu_dst_fmt = find_format(vpu, DEF_DST_FMT_DEC, true);

	ctx->run_ops = &rockchip_vpu_dec_run_ops;

	return rockchip_vpu_ctrls_setup(ctx, &rockchip_vpu_dec_ctrl_ops,
					controls, ARRAY_SIZE(controls), NULL);
}

void rockchip_vpu_dec_exit(struct rockchip_vpu_ctx *ctx)
{
	rockchip_vpu_ctrls_delete(ctx);
}
