// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>

#include "iris_buffer.h"
#include "iris_instance.h"
#include "iris_venc.h"
#include "iris_vpu_buffer.h"

int iris_venc_inst_init(struct iris_inst *inst)
{
	struct v4l2_format *f;

	inst->fmt_src = kzalloc(sizeof(*inst->fmt_src), GFP_KERNEL);
	inst->fmt_dst  = kzalloc(sizeof(*inst->fmt_dst), GFP_KERNEL);
	if (!inst->fmt_src || !inst->fmt_dst) {
		kfree(inst->fmt_src);
		kfree(inst->fmt_dst);
		return -ENOMEM;
	}

	f = inst->fmt_dst;
	f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f->fmt.pix_mp.width = DEFAULT_WIDTH;
	f->fmt.pix_mp.height = DEFAULT_HEIGHT;
	f->fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
	inst->codec = f->fmt.pix_mp.pixelformat;
	f->fmt.pix_mp.num_planes = 1;
	f->fmt.pix_mp.plane_fmt[0].bytesperline = 0;
	f->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_OUTPUT);
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
	f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	inst->buffers[BUF_OUTPUT].min_count = iris_vpu_buf_count(inst, BUF_OUTPUT);
	inst->buffers[BUF_OUTPUT].size = f->fmt.pix_mp.plane_fmt[0].sizeimage;

	f = inst->fmt_src;
	f->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	f->fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
	f->fmt.pix_mp.width = ALIGN(DEFAULT_WIDTH, 128);
	f->fmt.pix_mp.height = ALIGN(DEFAULT_HEIGHT, 32);
	f->fmt.pix_mp.num_planes = 1;
	f->fmt.pix_mp.plane_fmt[0].bytesperline = ALIGN(DEFAULT_WIDTH, 128);
	f->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_INPUT);
	f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
	f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	inst->buffers[BUF_INPUT].min_count = iris_vpu_buf_count(inst, BUF_INPUT);
	inst->buffers[BUF_INPUT].size = f->fmt.pix_mp.plane_fmt[0].sizeimage;

	inst->crop.left = 0;
	inst->crop.top = 0;
	inst->crop.width = f->fmt.pix_mp.width;
	inst->crop.height = f->fmt.pix_mp.height;

	return 0;
}

void iris_venc_inst_deinit(struct iris_inst *inst)
{
	kfree(inst->fmt_dst);
	kfree(inst->fmt_src);
}

static const struct iris_fmt iris_venc_formats[] = {
	[IRIS_FMT_H264] = {
		.pixfmt = V4L2_PIX_FMT_H264,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	},
	[IRIS_FMT_HEVC] = {
		.pixfmt = V4L2_PIX_FMT_HEVC,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	},
};

static const struct iris_fmt *
find_format(struct iris_inst *inst, u32 pixfmt, u32 type)
{
	const struct iris_fmt *fmt = iris_venc_formats;
	unsigned int size = ARRAY_SIZE(iris_venc_formats);
	unsigned int i;

	for (i = 0; i < size; i++) {
		if (fmt[i].pixfmt == pixfmt)
			break;
	}

	if (i == size || fmt[i].type != type)
		return NULL;

	return &fmt[i];
}

static const struct iris_fmt *
find_format_by_index(struct iris_inst *inst, u32 index, u32 type)
{
	const struct iris_fmt *fmt = iris_venc_formats;
	unsigned int size = ARRAY_SIZE(iris_venc_formats);

	if (index >= size || fmt[index].type != type)
		return NULL;

	return &fmt[index];
}

int iris_venc_enum_fmt(struct iris_inst *inst, struct v4l2_fmtdesc *f)
{
	const struct iris_fmt *fmt;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (f->index)
			return -EINVAL;
		f->pixelformat = V4L2_PIX_FMT_NV12;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		fmt = find_format_by_index(inst, f->index, f->type);
		if (!fmt)
			return -EINVAL;

		f->pixelformat = fmt->pixfmt;
		f->flags = V4L2_FMT_FLAG_COMPRESSED | V4L2_FMT_FLAG_ENC_CAP_FRAME_INTERVAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

int iris_venc_try_fmt(struct iris_inst *inst, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	const struct iris_fmt *fmt;
	struct v4l2_format *f_inst;

	memset(pixmp->reserved, 0, sizeof(pixmp->reserved));
	fmt = find_format(inst, pixmp->pixelformat, f->type);
	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		if (f->fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12) {
			f_inst = inst->fmt_src;
			f->fmt.pix_mp.width = f_inst->fmt.pix_mp.width;
			f->fmt.pix_mp.height = f_inst->fmt.pix_mp.height;
			f->fmt.pix_mp.pixelformat = f_inst->fmt.pix_mp.pixelformat;
		}
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		if (!fmt) {
			f_inst = inst->fmt_dst;
			f->fmt.pix_mp.width = f_inst->fmt.pix_mp.width;
			f->fmt.pix_mp.height = f_inst->fmt.pix_mp.height;
			f->fmt.pix_mp.pixelformat = f_inst->fmt.pix_mp.pixelformat;
		}
		break;
	default:
		return -EINVAL;
	}

	if (pixmp->field == V4L2_FIELD_ANY)
		pixmp->field = V4L2_FIELD_NONE;

	pixmp->num_planes = 1;

	return 0;
}

static int iris_venc_s_fmt_output(struct iris_inst *inst, struct v4l2_format *f)
{
	struct v4l2_format *fmt;

	iris_venc_try_fmt(inst, f);

	if (!(find_format(inst, f->fmt.pix_mp.pixelformat, f->type)))
		return -EINVAL;

	fmt = inst->fmt_dst;
	fmt->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt->fmt.pix_mp.num_planes = 1;
	fmt->fmt.pix_mp.plane_fmt[0].bytesperline = 0;
	fmt->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_OUTPUT);

	if (f->fmt.pix_mp.colorspace != V4L2_COLORSPACE_DEFAULT &&
	    f->fmt.pix_mp.colorspace != V4L2_COLORSPACE_REC709)
		f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
	fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
	fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
	fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

	inst->buffers[BUF_OUTPUT].min_count = iris_vpu_buf_count(inst, BUF_OUTPUT);
	inst->buffers[BUF_OUTPUT].size = fmt->fmt.pix_mp.plane_fmt[0].sizeimage;
	fmt->fmt.pix_mp.pixelformat = f->fmt.pix_mp.pixelformat;
	inst->codec = f->fmt.pix_mp.pixelformat;
	memcpy(f, fmt, sizeof(struct v4l2_format));

	return 0;
}

static int iris_venc_s_fmt_input(struct iris_inst *inst, struct v4l2_format *f)
{
	struct v4l2_format *fmt, *output_fmt;

	iris_venc_try_fmt(inst, f);

	if (f->fmt.pix_mp.pixelformat != V4L2_PIX_FMT_NV12)
		return -EINVAL;

	fmt = inst->fmt_src;
	fmt->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	fmt->fmt.pix_mp.width = ALIGN(f->fmt.pix_mp.width, 128);
	fmt->fmt.pix_mp.height = ALIGN(f->fmt.pix_mp.height, 32);
	fmt->fmt.pix_mp.num_planes = 1;
	fmt->fmt.pix_mp.pixelformat = f->fmt.pix_mp.pixelformat;
	fmt->fmt.pix_mp.plane_fmt[0].bytesperline = ALIGN(f->fmt.pix_mp.width, 128);
	fmt->fmt.pix_mp.plane_fmt[0].sizeimage = iris_get_buffer_size(inst, BUF_INPUT);

	fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
	fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
	fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;

	output_fmt = inst->fmt_dst;
	output_fmt->fmt.pix_mp.width = fmt->fmt.pix_mp.width;
	output_fmt->fmt.pix_mp.height = fmt->fmt.pix_mp.height;
	output_fmt->fmt.pix_mp.colorspace = fmt->fmt.pix_mp.colorspace;
	output_fmt->fmt.pix_mp.xfer_func = fmt->fmt.pix_mp.xfer_func;
	output_fmt->fmt.pix_mp.ycbcr_enc = fmt->fmt.pix_mp.ycbcr_enc;
	output_fmt->fmt.pix_mp.quantization = fmt->fmt.pix_mp.quantization;

	inst->buffers[BUF_INPUT].min_count = iris_vpu_buf_count(inst, BUF_INPUT);
	inst->buffers[BUF_INPUT].size = fmt->fmt.pix_mp.plane_fmt[0].sizeimage;

	if (f->fmt.pix_mp.width != inst->crop.width ||
	    f->fmt.pix_mp.height != inst->crop.height) {
		inst->crop.top = 0;
		inst->crop.left = 0;
		inst->crop.width = fmt->fmt.pix_mp.width;
		inst->crop.height = fmt->fmt.pix_mp.height;

		iris_venc_s_fmt_output(inst, output_fmt);
	}

	memcpy(f, fmt, sizeof(struct v4l2_format));

	return 0;
}

int iris_venc_s_fmt(struct iris_inst *inst, struct v4l2_format *f)
{
	struct vb2_queue *q;

	q = v4l2_m2m_get_vq(inst->m2m_ctx, f->type);
	if (!q)
		return -EINVAL;

	if (vb2_is_busy(q))
		return -EBUSY;

	switch (f->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		return iris_venc_s_fmt_input(inst, f);
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		return iris_venc_s_fmt_output(inst, f);
	default:
		return -EINVAL;
	}
}

int iris_venc_validate_format(struct iris_inst *inst, u32 pixelformat)
{
	const struct iris_fmt *fmt = NULL;

	if (pixelformat != V4L2_PIX_FMT_NV12) {
		fmt = find_format(inst, pixelformat, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
		if (!fmt)
			return -EINVAL;
	}

	return 0;
}

int iris_venc_subscribe_event(struct iris_inst *inst,
			      const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(&inst->fh, sub, 0, NULL);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(&inst->fh, sub);
	default:
		return -EINVAL;
	}
}
