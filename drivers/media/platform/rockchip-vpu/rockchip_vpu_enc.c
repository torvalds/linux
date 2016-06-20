/*
 * Rockchip VPU codec driver
 *
 * Copyright (C) 2014 Rockchip Electronics Co., Ltd.
 *	Alpha Lin <Alpha.Lin@rock-chips.com>
 *	Jeffy Chen <jeffy.chen@rock-chips.com>
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

#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/version.h>
#include <linux/videodev2.h>
#include <media/v4l2-event.h>
#include <linux/workqueue.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-core.h>
#include <media/videobuf2-dma-sg.h>

#include "rockchip_vpu_enc.h"
#include "rockchip_vpu_hw.h"

#define DEF_SRC_FMT_ENC				V4L2_PIX_FMT_NV12
#define DEF_DST_FMT_ENC				V4L2_PIX_FMT_VP8

#define ROCKCHIP_ENC_MIN_WIDTH			96U
#define ROCKCHIP_ENC_MAX_WIDTH			1920U
#define ROCKCHIP_ENC_MIN_HEIGHT			96U
#define ROCKCHIP_ENC_MAX_HEIGHT			1088U

#define V4L2_CID_PRIVATE_ROCKCHIP_HEADER	(V4L2_CID_CUSTOM_BASE + 0)
#define V4L2_CID_PRIVATE_ROCKCHIP_REG_PARAMS	(V4L2_CID_CUSTOM_BASE + 1)
#define V4L2_CID_PRIVATE_ROCKCHIP_HW_PARAMS	(V4L2_CID_CUSTOM_BASE + 2)
#define V4L2_CID_PRIVATE_ROCKCHIP_RET_PARAMS	(V4L2_CID_CUSTOM_BASE + 3)

static struct rockchip_vpu_fmt formats[] = {
	/* Source formats. */
	{
		.name = "4:2:0 3 planes Y/Cb/Cr",
		.fourcc = V4L2_PIX_FMT_YUV420M,
		.codec_mode = RK_VPU_CODEC_NONE,
		.num_planes = 3,
		.depth = { 8, 4, 4 },
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_YUV420P,
	},
	{
		.name = "4:2:0 2 plane Y/CbCr",
		.fourcc = V4L2_PIX_FMT_NV12M,
		.codec_mode = RK_VPU_CODEC_NONE,
		.num_planes = 2,
		.depth = { 8, 8 },
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_YUV420SP,
	},
	{
		.name = "4:2:2 1 plane YUYV",
		.fourcc = V4L2_PIX_FMT_YUYV,
		.codec_mode = RK_VPU_CODEC_NONE,
		.num_planes = 1,
		.depth = { 16 },
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_YUYV422,
	},
	{
		.name = "4:2:2 1 plane UYVY",
		.fourcc = V4L2_PIX_FMT_UYVY,
		.codec_mode = RK_VPU_CODEC_NONE,
		.num_planes = 1,
		.depth = { 16 },
		.enc_fmt = ROCKCHIP_VPU_ENC_FMT_UYVY422,
	},
	/* Destination formats. */
	{
		.name = "VP8 Encoded Stream",
		.fourcc = V4L2_PIX_FMT_VP8,
		.codec_mode = RK3288_VPU_CODEC_VP8E,
		.num_planes = 1,
	},
	{
		.name = "H264 Encoded Stream",
		.fourcc = V4L2_PIX_FMT_H264,
		.codec_mode = RK3288_VPU_CODEC_H264E,
		.num_planes = 1,
	},
};

static struct rockchip_vpu_fmt *find_format(struct rockchip_vpu_dev *dev, u32 fourcc, bool bitstream)
{
	unsigned int i;

	vpu_debug_enter();

	for (i = 0; i < ARRAY_SIZE(formats); i++) {
		if (!rockchip_vpu_dev_codec_support(dev, formats[i].codec_mode))
			continue;
		if (formats[i].fourcc != fourcc)
			continue;
		if (bitstream && formats[i].codec_mode != RK_VPU_CODEC_NONE)
			return &formats[i];
		if (!bitstream && formats[i].codec_mode == RK_VPU_CODEC_NONE)
			return &formats[i];
	}

	return NULL;
}

/*
 * Indices of controls that need to be accessed directly, i.e. through
 * p_cur.p pointer of their v4l2_ctrl structs.
 */
enum {
	ROCKCHIP_VPU_ENC_CTRL_HEADER,
	ROCKCHIP_VPU_ENC_CTRL_REG_PARAMS,
	ROCKCHIP_VPU_ENC_CTRL_HW_PARAMS,
	ROCKCHIP_VPU_ENC_CTRL_RET_PARAMS,
};

static struct rockchip_vpu_control controls[] = {
	/* Private, per-frame controls. */
	[ROCKCHIP_VPU_ENC_CTRL_HEADER] = {
		.id = V4L2_CID_PRIVATE_ROCKCHIP_HEADER,
		.type = V4L2_CTRL_TYPE_PRIVATE,
		.name = "Rockchip Private Header",
		.elem_size = ROCKCHIP_HEADER_SIZE,
		.max_stores = VIDEO_MAX_FRAME,
		.can_store = true,
	},
	[ROCKCHIP_VPU_ENC_CTRL_REG_PARAMS] = {
		.id = V4L2_CID_PRIVATE_ROCKCHIP_REG_PARAMS,
		.type = V4L2_CTRL_TYPE_PRIVATE,
		.name = "Rockchip Private Reg Params",
		.elem_size = sizeof(struct rockchip_reg_params),
		.max_stores = VIDEO_MAX_FRAME,
		.can_store = true,
	},
	[ROCKCHIP_VPU_ENC_CTRL_HW_PARAMS] = {
		.id = V4L2_CID_PRIVATE_ROCKCHIP_HW_PARAMS,
		.type = V4L2_CTRL_TYPE_PRIVATE,
		.name = "Rockchip Private Hw Params",
		.elem_size = ROCKCHIP_HW_PARAMS_SIZE,
		.max_stores = VIDEO_MAX_FRAME,
		.can_store = true,
	},
	[ROCKCHIP_VPU_ENC_CTRL_RET_PARAMS] = {
		.id = V4L2_CID_PRIVATE_ROCKCHIP_RET_PARAMS,
		.type = V4L2_CTRL_TYPE_PRIVATE,
		.name = "Rockchip Private Ret Params",
		.is_volatile = true,
		.is_read_only = true,
		.max_stores = VIDEO_MAX_FRAME,
		.elem_size = ROCKCHIP_RET_PARAMS_SIZE,
	},
	/* Generic controls. (currently ignored) */
	{
		.id = V4L2_CID_MPEG_VIDEO_GOP_SIZE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 1,
		.maximum = 150,
		.step = 1,
		.default_value = 30,
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
		.id = V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_BITRATE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 10000,
		.maximum = 288000000,
		.step = 1,
		.default_value = 2097152,
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
		.maximum = V4L2_MPEG_VIDEO_H264_LEVEL_4_1,
		.default_value = V4L2_MPEG_VIDEO_H264_LEVEL_1_0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_MAX_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 30,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_MIN_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 18,
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
		.id = V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = 288000,
		.step = 1,
		.default_value = 30000,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_H264_SEI_FRAME_PACKING,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.minimum = 0,
		.maximum = 1,
		.step = 1,
		.default_value = 0,
	},
	{
		.id = V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME,
		.type = V4L2_CTRL_TYPE_BUTTON,
	},
	/*
	 * This hardware does not support features provided by controls
	 * below, but they are required for compatibility with certain
	 * userspace software.
	 */
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
		.id = V4L2_CID_MPEG_VIDEO_HEADER_MODE,
		.type = V4L2_CTRL_TYPE_MENU,
		.minimum = V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE,
		.maximum = V4L2_MPEG_VIDEO_HEADER_MODE_JOINED_WITH_1ST_FRAME,
		.default_value = V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE,
		.menu_skip_mask = 0,
	},
	{
		.id = V4L2_CID_MPEG_MFC51_VIDEO_RC_FIXED_TARGET_BIT,
		.type = V4L2_CTRL_TYPE_BOOLEAN,
		.name = "Fixed Target Bit Enable",
		.minimum = 0,
		.maximum = 1,
		.default_value = 0,
		.step = 1,
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
		.id = V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP,
		.type = V4L2_CTRL_TYPE_INTEGER,
		.minimum = 0,
		.maximum = 51,
		.step = 1,
		.default_value = 1,
	},
};

static inline const void *get_ctrl_ptr(struct rockchip_vpu_ctx *ctx, unsigned id)
{
	struct v4l2_ctrl *ctrl = ctx->ctrls[id];

	return ctrl->p_cur.p;
}

static const char *const *rockchip_vpu_enc_get_menu(u32 id)
{
	static const char *const vpu_video_frame_skip[] = {
		"Disabled",
		"Level Limit",
		"VBV/CPB Limit",
		NULL,
	};

	switch (id) {
	case V4L2_CID_MPEG_MFC51_VIDEO_FRAME_SKIP_MODE:
		return vpu_video_frame_skip;
	}

	return NULL;
}

static int vidioc_querycap(struct file *file, void *priv,
			   struct v4l2_capability *cap)
{
	struct rockchip_vpu_dev *dev = video_drvdata(file);

	vpu_debug_enter();

	strlcpy(cap->driver, ROCKCHIP_VPU_ENC_NAME, sizeof(cap->driver));
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

	s->min_width = ROCKCHIP_ENC_MIN_WIDTH;
	s->max_width = ROCKCHIP_ENC_MAX_WIDTH;
	s->step_width = MB_DIM;
	s->min_height = ROCKCHIP_ENC_MIN_HEIGHT;
	s->max_height = ROCKCHIP_ENC_MAX_HEIGHT;
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
		if (out && formats[i].codec_mode != RK_VPU_CODEC_NONE)
			continue;
		else if (!out && formats[i].codec_mode == RK_VPU_CODEC_NONE)
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
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		vpu_debug(4, "%s\n", fmt2str(f->fmt.pix_mp.pixelformat, str));

		fmt = find_format(dev, pix_fmt_mp->pixelformat, true);
		if (!fmt) {
			vpu_err("failed to try capture format\n");
			return -EINVAL;
		}

		if (pix_fmt_mp->plane_fmt[0].sizeimage == 0) {
			vpu_err("must be set encoding output size\n");
			return -EINVAL;
		}

		pix_fmt_mp->plane_fmt[0].bytesperline = 0;
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		vpu_debug(4, "%s\n", fmt2str(f->fmt.pix_mp.pixelformat, str));

		fmt = find_format(dev, pix_fmt_mp->pixelformat, false);
		if (!fmt) {
			vpu_err("failed to try output format\n");
			return -EINVAL;
		}

		if (fmt->num_planes != pix_fmt_mp->num_planes) {
			vpu_err("plane number mismatches on output format\n");
			return -EINVAL;
		}

		/* Limit to hardware min/max. */
		pix_fmt_mp->width = clamp(pix_fmt_mp->width,
				ROCKCHIP_ENC_MIN_WIDTH, ROCKCHIP_ENC_MAX_WIDTH);
		pix_fmt_mp->height = clamp(pix_fmt_mp->height,
				ROCKCHIP_ENC_MIN_HEIGHT, ROCKCHIP_ENC_MAX_HEIGHT);
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

static void calculate_plane_sizes(struct rockchip_vpu_fmt *fmt,
				  unsigned int w, unsigned int h,
				  struct v4l2_pix_format_mplane *pix_fmt_mp)
{
	int i;

	for (i = 0; i < fmt->num_planes; ++i) {
		pix_fmt_mp->plane_fmt[i].bytesperline = w * fmt->depth[i] / 8;
		pix_fmt_mp->plane_fmt[i].sizeimage = h *
					pix_fmt_mp->plane_fmt[i].bytesperline;
		/*
		 * All of multiplanar formats we support have chroma
		 * planes subsampled by 2 vertically.
		 */
		if (i != 0)
			pix_fmt_mp->plane_fmt[i].sizeimage /= 2;
	}
}

static int vidioc_s_fmt(struct file *file, void *priv, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pix_fmt_mp = &f->fmt.pix_mp;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	struct rockchip_vpu_dev *dev = ctx->dev;
	unsigned int mb_width, mb_height;
	struct rockchip_vpu_fmt *fmt;
	int ret = 0;

	vpu_debug_enter();

	/* Change not allowed if any queue is streaming. */
	if (vb2_is_streaming(&ctx->vq_src) || vb2_is_streaming(&ctx->vq_dst)) {
		ret = -EBUSY;
		goto out;
	}

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
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

		ctx->vpu_dst_fmt = find_format(dev, pix_fmt_mp->pixelformat, true);
		ctx->dst_fmt = *pix_fmt_mp;
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
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

		fmt = find_format(dev, pix_fmt_mp->pixelformat, false);
		ctx->vpu_src_fmt = fmt;

		mb_width = MB_WIDTH(pix_fmt_mp->width);
		mb_height = MB_HEIGHT(pix_fmt_mp->height);

		vpu_debug(0, "OUTPUT codec mode: %d\n", fmt->codec_mode);
		vpu_debug(0, "fmt - w: %d, h: %d, mb - w: %d, h: %d\n",
			  pix_fmt_mp->width, pix_fmt_mp->height,
			  mb_width, mb_height);

		calculate_plane_sizes(fmt, mb_width * MB_DIM,
					mb_height * MB_DIM, pix_fmt_mp);

		/* Reset crop rectangle. */
		ctx->src_crop.width = pix_fmt_mp->width;
		ctx->src_crop.height = pix_fmt_mp->height;

		ctx->src_fmt = *pix_fmt_mp;
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
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		vpu_debug(4, "\n");

		ret = vb2_reqbufs(&ctx->vq_dst, reqbufs);
		if (ret != 0) {
			vpu_err("error in vb2_reqbufs() for E(D)\n");
			goto out;
		}
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		vpu_debug(4, "memory type %d\n", reqbufs->memory);

		ret = vb2_reqbufs(&ctx->vq_src, reqbufs);
		if (ret != 0) {
			vpu_err("error in vb2_reqbufs() for E(S)\n");
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

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *buf)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	int ret;
	int i;

	vpu_debug_enter();

	for (i = 0; i < buf->length; i++) {
		vpu_debug(4, "plane[%d]->length %d bytesused %d\n",
			  i, buf->m.planes[i].length,
			  buf->m.planes[i].bytesused);
	}

	switch (buf->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		ret = vb2_qbuf(&ctx->vq_src, buf);
		vpu_debug(4, "vb2_qbuf return %d\n", ret);
		break;

	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		ret = vb2_qbuf(&ctx->vq_dst, buf);
		vpu_debug(4, "vb2_qbuf return %d\n", ret);
		break;

	default:
		ret = -EINVAL;
	}

	vpu_debug_leave();

	return ret;
}

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

static int rockchip_vpu_enc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct rockchip_vpu_ctx *ctx = ctrl_to_ctx(ctrl);
	struct rockchip_vpu_dev *dev = ctx->dev;
	int ret = 0;

	vpu_debug_enter();

	vpu_debug(4, "ctrl id %d\n", ctrl->id);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
	case V4L2_CID_MPEG_VIDEO_BITRATE:
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
	case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
	case V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM:
	case V4L2_CID_MPEG_VIDEO_H264_CPB_SIZE:
	case V4L2_CID_MPEG_VIDEO_H264_SEI_FRAME_PACKING:
	case V4L2_CID_MPEG_MFC51_VIDEO_RC_REACTION_COEFF:
	case V4L2_CID_MPEG_VIDEO_HEADER_MODE:
	case V4L2_CID_MPEG_MFC51_VIDEO_RC_FIXED_TARGET_BIT:
	case V4L2_CID_MPEG_VIDEO_B_FRAMES:
	case V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP:
	case V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME:
		/* Ignore these controls for now. (FIXME?) */
		break;

	case V4L2_CID_PRIVATE_ROCKCHIP_HEADER:
	case V4L2_CID_PRIVATE_ROCKCHIP_REG_PARAMS:
	case V4L2_CID_PRIVATE_ROCKCHIP_HW_PARAMS:
		/* Nothing to do here. The control is used directly. */
		break;

	default:
		v4l2_err(&dev->v4l2_dev, "Invalid control, id=%d, val=%d\n",
			 ctrl->id, ctrl->val);
		ret = -EINVAL;
	}

	vpu_debug_leave();

	return ret;
}

static int rockchip_vpu_enc_g_volatile_ctrl(struct v4l2_ctrl *ctrl)
{
	struct rockchip_vpu_ctx *ctx = ctrl_to_ctx(ctrl);
	struct rockchip_vpu_dev *dev = ctx->dev;
	int ret = 0;

	vpu_debug_enter();

	vpu_debug(4, "ctrl id %d\n", ctrl->id);

	switch (ctrl->id) {
	case V4L2_CID_PRIVATE_ROCKCHIP_RET_PARAMS:
		memcpy(ctrl->p_new.p, ctx->run.priv_dst.cpu,
			ROCKCHIP_RET_PARAMS_SIZE);
		break;

	default:
		v4l2_err(&dev->v4l2_dev, "Invalid control, id=%d, val=%d\n",
			 ctrl->id, ctrl->val);
		ret = -EINVAL;
	}

	vpu_debug_leave();

	return ret;
}

static const struct v4l2_ctrl_ops rockchip_vpu_enc_ctrl_ops = {
	.s_ctrl = rockchip_vpu_enc_s_ctrl,
	.g_volatile_ctrl = rockchip_vpu_enc_g_volatile_ctrl,
};

static int vidioc_cropcap(struct file *file, void *priv,
			  struct v4l2_cropcap *cap)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_pix_format_mplane *fmt = &ctx->src_fmt;
	int ret = 0;

	vpu_debug_enter();

	/* Crop only supported on source. */
	if (cap->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = -EINVAL;
		goto out;
	}

	cap->bounds.left = 0;
	cap->bounds.top = 0;
	cap->bounds.width = fmt->width;
	cap->bounds.height = fmt->height;
	cap->defrect = cap->bounds;
	cap->pixelaspect.numerator = 1;
	cap->pixelaspect.denominator = 1;

out:
	vpu_debug_leave();

	return ret;
}

static int vidioc_g_crop(struct file *file, void *priv, struct v4l2_crop *crop)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	int ret = 0;

	vpu_debug_enter();

	/* Crop only supported on source. */
	if (crop->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = -EINVAL;
		goto out;
	}

	crop->c = ctx->src_crop;

out:
	vpu_debug_leave();

	return ret;
}

static int vidioc_s_crop(struct file *file, void *priv,
			 const struct v4l2_crop *crop)
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(priv);
	struct v4l2_pix_format_mplane *fmt = &ctx->src_fmt;
	const struct v4l2_rect *rect = &crop->c;
	int ret = 0;

	vpu_debug_enter();

	/* Crop only supported on source. */
	if (crop->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		ret = -EINVAL;
		goto out;
	}

	/* Change not allowed if the queue is streaming. */
	if (vb2_is_streaming(&ctx->vq_src)) {
		ret = -EBUSY;
		goto out;
	}

	/* We do not support offsets. */
	if (rect->left != 0 || rect->top != 0)
		goto fallback;

	/* We can crop only inside right- or bottom-most macroblocks. */
	if (round_up(rect->width, MB_DIM) != fmt->width
	    || round_up(rect->height, MB_DIM) != fmt->height)
		goto fallback;

	/* We support widths aligned to 4 pixels and arbitrary heights. */
	ctx->src_crop.width = round_up(rect->width, 4);
	ctx->src_crop.height = rect->height;

	vpu_debug_leave();

	return 0;

fallback:
	/* Default to full frame for incorrect settings. */
	ctx->src_crop.width = fmt->width;
	ctx->src_crop.height = fmt->height;

out:
	vpu_debug_leave();

	return ret;
}

static const struct v4l2_ioctl_ops rockchip_vpu_enc_ioctl_ops = {
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
	.vidioc_cropcap = vidioc_cropcap,
	.vidioc_g_crop = vidioc_g_crop,
	.vidioc_s_crop = vidioc_s_crop,
};

static int rockchip_vpu_queue_setup(struct vb2_queue *vq,
				  const void *parg,
				  unsigned int *buf_count,
				  unsigned int *plane_count,
				  unsigned int psize[], void *allocators[])
{
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(vq->drv_priv);
	int ret = 0;
	int i;

	vpu_debug_enter();

	switch (vq->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		*plane_count = ctx->vpu_dst_fmt->num_planes;

		if (*buf_count < 1)
			*buf_count = 1;

		if (*buf_count > VIDEO_MAX_FRAME)
			*buf_count = VIDEO_MAX_FRAME;

		psize[0] = ctx->dst_fmt.plane_fmt[0].sizeimage;
		/* Kernel mapping necessary for bitstream post processing. */
		allocators[0] = ctx->dev->alloc_ctx_vm;
		vpu_debug(0, "capture psize[%d]: %d\n", 0, psize[0]);
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		*plane_count = ctx->vpu_src_fmt->num_planes;

		if (*buf_count < 1)
			*buf_count = 1;

		if (*buf_count > VIDEO_MAX_FRAME)
			*buf_count = VIDEO_MAX_FRAME;

		for (i = 0; i < ctx->vpu_src_fmt->num_planes; ++i) {
			psize[i] = ctx->src_fmt.plane_fmt[i].sizeimage;
			vpu_debug(0, "output psize[%d]: %d\n", i, psize[i]);
			allocators[i] = ctx->dev->alloc_ctx;
		}
		break;

	default:
		vpu_err("invalid queue type: %d\n", vq->type);
		ret = -EINVAL;
	}

	vpu_debug_leave();

	return ret;
}

static int rockchip_vpu_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(vq->drv_priv);
	int ret = 0;
	int i;

	vpu_debug_enter();

	switch (vq->type) {
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		vpu_debug(4, "plane size: %ld, dst size: %d\n",
				vb2_plane_size(vb, 0),
				ctx->dst_fmt.plane_fmt[0].sizeimage);

		if (vb2_plane_size(vb, 0)
		    < ctx->dst_fmt.plane_fmt[0].sizeimage) {
			vpu_err("plane size is too small for capture\n");
			ret = -EINVAL;
		}
		break;

	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		for (i = 0; i < ctx->vpu_src_fmt->num_planes; ++i) {
			vpu_debug(4, "plane %d size: %ld, sizeimage: %u\n", i,
					vb2_plane_size(vb, i),
					ctx->src_fmt.plane_fmt[i].sizeimage);

			if (vb2_plane_size(vb, i)
			    < ctx->src_fmt.plane_fmt[i].sizeimage) {
				vpu_err("size of plane %d is too small for output\n",
					i);
				break;
			}
		}

		if (i != ctx->vpu_src_fmt->num_planes)
			ret = -EINVAL;
		break;

	default:
		vpu_err("invalid queue type: %d\n", vq->type);
		ret = -EINVAL;
	}

	vpu_debug_leave();

	return ret;
}

static void rockchip_vpu_buf_finish(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct rockchip_vpu_ctx *ctx = fh_to_ctx(vq->drv_priv);

	vpu_debug_enter();

	if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
	    && vb->state == VB2_BUF_STATE_DONE
	    && ctx->vpu_dst_fmt->fourcc == V4L2_PIX_FMT_VP8) {
		struct rockchip_vpu_buf *buf;

		buf = vb_to_buf(vb);
		rockchip_vpu_vp8e_assemble_bitstream(ctx, buf);
	} else if (vq->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE
	    && vb->state == VB2_BUF_STATE_DONE
	    && ctx->vpu_dst_fmt->fourcc == V4L2_PIX_FMT_H264) {
		struct rockchip_vpu_buf *buf;

		buf = vb_to_buf(vb);
		rockchip_vpu_h264e_assemble_bitstream(ctx, buf);
	}

	vpu_debug_leave();
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

static struct vb2_ops rockchip_vpu_enc_qops = {
	.queue_setup = rockchip_vpu_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
	.buf_prepare = rockchip_vpu_buf_prepare,
	.buf_finish = rockchip_vpu_buf_finish,
	.start_streaming = rockchip_vpu_start_streaming,
	.stop_streaming = rockchip_vpu_stop_streaming,
	.buf_queue = rockchip_vpu_buf_queue,
};

struct vb2_ops *get_enc_queue_ops(void)
{
	return &rockchip_vpu_enc_qops;
}

const struct v4l2_ioctl_ops *get_enc_v4l2_ioctl_ops(void)
{
	return &rockchip_vpu_enc_ioctl_ops;
}

static void rockchip_vpu_enc_prepare_run(struct rockchip_vpu_ctx *ctx)
{
	struct vb2_v4l2_buffer *vb2_src = to_vb2_v4l2_buffer(&ctx->run.src->vb.vb2_buf);
	unsigned config_store = vb2_src->config_store;

	v4l2_ctrl_apply_store(&ctx->ctrl_handler, config_store);

	if (ctx->vpu_dst_fmt->fourcc == V4L2_PIX_FMT_VP8) {
		memcpy(ctx->run.dst->vp8e.header,
			get_ctrl_ptr(ctx, ROCKCHIP_VPU_ENC_CTRL_HEADER),
			ROCKCHIP_HEADER_SIZE);
		ctx->run.vp8e.reg_params = get_ctrl_ptr(ctx,
			ROCKCHIP_VPU_ENC_CTRL_REG_PARAMS);
		memcpy(ctx->run.priv_src.cpu,
			get_ctrl_ptr(ctx, ROCKCHIP_VPU_ENC_CTRL_HW_PARAMS),
			ROCKCHIP_HW_PARAMS_SIZE);
	} else if (ctx->vpu_dst_fmt->fourcc == V4L2_PIX_FMT_H264) {
		ctx->run.h264e.reg_params = get_ctrl_ptr(ctx,
			ROCKCHIP_VPU_ENC_CTRL_REG_PARAMS);
	}
}

static const struct rockchip_vpu_run_ops rockchip_vpu_enc_run_ops = {
	.prepare_run = rockchip_vpu_enc_prepare_run,
};

int rockchip_vpu_enc_init(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_dev *vpu = ctx->dev;
	int ret;

	ctx->vpu_src_fmt = find_format(vpu, DEF_SRC_FMT_ENC, false);
	ctx->vpu_dst_fmt = find_format(vpu, DEF_DST_FMT_ENC, true);

	ret = rockchip_vpu_aux_buf_alloc(vpu, &ctx->run.priv_src,
					ROCKCHIP_HW_PARAMS_SIZE);
	if (ret) {
		vpu_err("Failed to allocate private source buffer.\n");
		return ret;
	}


	ret = rockchip_vpu_aux_buf_alloc(vpu, &ctx->run.priv_dst,
					ROCKCHIP_RET_PARAMS_SIZE);
	if (ret) {
		vpu_err("Failed to allocate private destination buffer.\n");
		goto err_priv_src;
	}

	ret = rockchip_vpu_ctrls_setup(ctx, &rockchip_vpu_enc_ctrl_ops,
					controls, ARRAY_SIZE(controls),
					rockchip_vpu_enc_get_menu);
	if (ret) {
		vpu_err("Failed to set up controls\n");
		goto err_priv_dst;
	}

	ctx->run_ops = &rockchip_vpu_enc_run_ops;

	return 0;

err_priv_dst:
	rockchip_vpu_aux_buf_free(vpu, &ctx->run.priv_dst);
err_priv_src:
	rockchip_vpu_aux_buf_free(vpu, &ctx->run.priv_src);

	return ret;
}

void rockchip_vpu_enc_exit(struct rockchip_vpu_ctx *ctx)
{
	struct rockchip_vpu_dev *vpu = ctx->dev;

	rockchip_vpu_ctrls_delete(ctx);

	rockchip_vpu_aux_buf_free(vpu, &ctx->run.priv_dst);
	rockchip_vpu_aux_buf_free(vpu, &ctx->run.priv_src);
};

/*
 * WAR for encoder state corruption after decoding
 */

static const struct rockchip_vpu_run_ops dummy_encode_run_ops = {
	/* No ops needed for dummy encoding. */
};

#define DUMMY_W		64
#define DUMMY_H		64
#define DUMMY_SRC_FMT	V4L2_PIX_FMT_YUYV
#define DUMMY_DST_FMT	V4L2_PIX_FMT_VP8
#define DUMMY_DST_SIZE	(32 * 1024)

int rockchip_vpu_enc_init_dummy_ctx(struct rockchip_vpu_dev *dev)
{
	struct rockchip_vpu_ctx *ctx;
	int ret;
	int i;

	ctx = devm_kzalloc(dev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dev = dev;

	ctx->vpu_src_fmt = find_format(dev, DUMMY_SRC_FMT, false);
	ctx->src_fmt.width = DUMMY_W;
	ctx->src_fmt.height = DUMMY_H;
	ctx->src_fmt.pixelformat = ctx->vpu_src_fmt->fourcc;
	ctx->src_fmt.num_planes = ctx->vpu_src_fmt->num_planes;

	calculate_plane_sizes(ctx->vpu_src_fmt, ctx->src_fmt.width,
				ctx->src_fmt.height, &ctx->src_fmt);

	ctx->vpu_dst_fmt = find_format(dev, DUMMY_DST_FMT, true);
	ctx->dst_fmt.width = ctx->src_fmt.width;
	ctx->dst_fmt.height = ctx->src_fmt.height;
	ctx->dst_fmt.pixelformat = ctx->vpu_dst_fmt->fourcc;
	ctx->dst_fmt.plane_fmt[0].sizeimage = DUMMY_DST_SIZE;
	ctx->dst_fmt.plane_fmt[0].bytesperline = 0;
	ctx->dst_fmt.num_planes = 1;

	INIT_LIST_HEAD(&ctx->src_queue);

	ctx->src_crop.left = 0;
	ctx->src_crop.top = 0;
	ctx->src_crop.width = ctx->src_fmt.width;
	ctx->src_crop.left = ctx->src_fmt.height;

	INIT_LIST_HEAD(&ctx->dst_queue);
	INIT_LIST_HEAD(&ctx->list);

	ctx->run.vp8e.reg_params =
		rk3288_vpu_vp8e_get_dummy_params();
	ctx->run_ops = &dummy_encode_run_ops;

	ctx->run.dst = devm_kzalloc(dev->dev, sizeof(*ctx->run.dst),
					GFP_KERNEL);
	if (!ctx->run.dst)
		return -ENOMEM;

	ret = rockchip_vpu_aux_buf_alloc(dev, &ctx->run.priv_src,
					ROCKCHIP_HW_PARAMS_SIZE);
	if (ret)
		return ret;

	ret = rockchip_vpu_aux_buf_alloc(dev, &ctx->run.priv_dst,
					ROCKCHIP_RET_PARAMS_SIZE);
	if (ret)
		goto err_free_priv_src;

	for (i = 0; i < ctx->src_fmt.num_planes; ++i) {
		ret = rockchip_vpu_aux_buf_alloc(dev, &dev->dummy_encode_src[i],
					ctx->src_fmt.plane_fmt[i].sizeimage);
		if (ret)
			goto err_free_src;

		memset(dev->dummy_encode_src[i].cpu, 0,
			dev->dummy_encode_src[i].size);
	}

	ret = rockchip_vpu_aux_buf_alloc(dev, &dev->dummy_encode_dst,
					ctx->dst_fmt.plane_fmt[0].sizeimage);
	if (ret)
		goto err_free_src;

	memset(dev->dummy_encode_dst.cpu, 0, dev->dummy_encode_dst.size);

	ret = rockchip_vpu_init(ctx);
	if (ret)
		goto err_free_dst;

	dev->dummy_encode_ctx = ctx;

	return 0;

err_free_dst:
	rockchip_vpu_aux_buf_free(dev, &dev->dummy_encode_dst);
err_free_src:
	for (i = 0; i < ctx->src_fmt.num_planes; ++i)
		if (dev->dummy_encode_src[i].cpu)
			rockchip_vpu_aux_buf_free(dev, &dev->dummy_encode_src[i]);
	rockchip_vpu_aux_buf_free(dev, &ctx->run.priv_dst);
err_free_priv_src:
	rockchip_vpu_aux_buf_free(dev, &ctx->run.priv_src);

	return ret;
}

void rockchip_vpu_enc_free_dummy_ctx(struct rockchip_vpu_dev *dev)
{
	struct rockchip_vpu_ctx *ctx = dev->dummy_encode_ctx;
	int i;

	if (!ctx)
		return;

	rockchip_vpu_deinit(ctx);

	for (i = 0; i < ctx->src_fmt.num_planes; ++i)
		rockchip_vpu_aux_buf_free(dev, &dev->dummy_encode_src[i]);
	rockchip_vpu_aux_buf_free(dev, &dev->dummy_encode_dst);
	rockchip_vpu_aux_buf_free(dev, &ctx->run.priv_src);
	rockchip_vpu_aux_buf_free(dev, &ctx->run.priv_dst);
}
