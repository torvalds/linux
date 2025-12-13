// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>

#include "iris_buffer.h"
#include "iris_common.h"
#include "iris_ctrls.h"
#include "iris_instance.h"
#include "iris_power.h"
#include "iris_venc.h"
#include "iris_vpu_buffer.h"

int iris_venc_inst_init(struct iris_inst *inst)
{
	struct iris_core *core = inst->core;
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

	inst->operating_rate = DEFAULT_FPS;
	inst->frame_rate = DEFAULT_FPS;

	memcpy(&inst->fw_caps[0], &core->inst_fw_caps_enc[0],
	       INST_FW_CAP_MAX * sizeof(struct platform_inst_fw_cap));

	return iris_ctrls_init(inst);
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

int iris_venc_s_selection(struct iris_inst *inst, struct v4l2_selection *s)
{
	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		s->r.left = 0;
		s->r.top = 0;

		if (s->r.width > inst->fmt_src->fmt.pix_mp.width ||
		    s->r.height > inst->fmt_src->fmt.pix_mp.height)
			return -EINVAL;

		inst->crop.left = s->r.left;
		inst->crop.top = s->r.top;
		inst->crop.width = s->r.width;
		inst->crop.height = s->r.height;
		inst->fmt_dst->fmt.pix_mp.width = inst->crop.width;
		inst->fmt_dst->fmt.pix_mp.height = inst->crop.height;
		return iris_venc_s_fmt_output(inst, inst->fmt_dst);
	default:
		return -EINVAL;
	}
}

int iris_venc_s_param(struct iris_inst *inst, struct v4l2_streamparm *s_parm)
{
	struct platform_inst_caps *caps = inst->core->iris_platform_data->inst_caps;
	struct vb2_queue *src_q = v4l2_m2m_get_src_vq(inst->m2m_ctx);
	struct vb2_queue *dst_q = v4l2_m2m_get_dst_vq(inst->m2m_ctx);
	struct v4l2_fract *timeperframe = NULL;
	u32 default_rate = DEFAULT_FPS;
	bool is_frame_rate = false;
	u64 us_per_frame, fps;
	u32 max_rate;

	int ret = 0;

	if (s_parm->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		timeperframe = &s_parm->parm.output.timeperframe;
		max_rate = caps->max_operating_rate;
		s_parm->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	} else {
		timeperframe = &s_parm->parm.capture.timeperframe;
		is_frame_rate = true;
		max_rate = caps->max_frame_rate;
		s_parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	}

	if (!timeperframe->denominator || !timeperframe->numerator) {
		if (!timeperframe->numerator)
			timeperframe->numerator = 1;
		if (!timeperframe->denominator)
			timeperframe->denominator = default_rate;
	}

	us_per_frame = timeperframe->numerator * (u64)USEC_PER_SEC;
	do_div(us_per_frame, timeperframe->denominator);

	if (!us_per_frame)
		return -EINVAL;

	fps = (u64)USEC_PER_SEC;
	do_div(fps, us_per_frame);
	if (fps > max_rate) {
		ret = -ENOMEM;
		goto reset_rate;
	}

	if (is_frame_rate)
		inst->frame_rate = (u32)fps;
	else
		inst->operating_rate = (u32)fps;

	if ((s_parm->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE && vb2_is_streaming(src_q)) ||
	    (s_parm->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE && vb2_is_streaming(dst_q))) {
		ret = iris_check_core_mbpf(inst);
		if (ret)
			goto reset_rate;
		ret = iris_check_core_mbps(inst);
		if (ret)
			goto reset_rate;
	}

	return 0;

reset_rate:
	if (ret) {
		if (is_frame_rate)
			inst->frame_rate = default_rate;
		else
			inst->operating_rate = default_rate;
	}

	return ret;
}

int iris_venc_g_param(struct iris_inst *inst, struct v4l2_streamparm *s_parm)
{
	struct v4l2_fract *timeperframe = NULL;

	if (s_parm->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		timeperframe = &s_parm->parm.output.timeperframe;
		timeperframe->numerator = 1;
		timeperframe->denominator = inst->operating_rate;
		s_parm->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	} else {
		timeperframe = &s_parm->parm.capture.timeperframe;
		timeperframe->numerator = 1;
		timeperframe->denominator = inst->frame_rate;
		s_parm->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	}

	return 0;
}

int iris_venc_streamon_input(struct iris_inst *inst)
{
	int ret;

	ret = iris_set_properties(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_alloc_and_queue_persist_bufs(inst, BUF_ARP);
	if (ret)
		return ret;

	iris_get_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

	ret = iris_destroy_dequeued_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_create_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_queue_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	return iris_process_streamon_input(inst);
}

int iris_venc_streamon_output(struct iris_inst *inst)
{
	int ret;

	ret = iris_set_properties(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (ret)
		goto error;

	ret = iris_alloc_and_queue_persist_bufs(inst, BUF_ARP);
	if (ret)
		return ret;

	iris_get_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	ret = iris_destroy_dequeued_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (ret)
		goto error;

	ret = iris_create_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (ret)
		goto error;

	ret = iris_queue_internal_buffers(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (ret)
		goto error;

	ret = iris_process_streamon_output(inst);
	if (ret)
		goto error;

	return ret;

error:
	iris_session_streamoff(inst, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);

	return ret;
}

int iris_venc_qbuf(struct iris_inst *inst, struct vb2_v4l2_buffer *vbuf)
{
	struct iris_buffer *buf = to_iris_buffer(vbuf);
	struct vb2_buffer *vb2 = &vbuf->vb2_buf;
	struct vb2_queue *q;
	int ret;

	ret = iris_vb2_buffer_to_driver(vb2, buf);
	if (ret)
		return ret;

	if (buf->type == BUF_INPUT)
		iris_set_ts_metadata(inst, vbuf);

	q = v4l2_m2m_get_vq(inst->m2m_ctx, vb2->type);
	if (!vb2_is_streaming(q)) {
		buf->attr |= BUF_ATTR_DEFERRED;
		return 0;
	}

	iris_scale_power(inst);

	return iris_queue_buffer(inst, buf);
}

int iris_venc_start_cmd(struct iris_inst *inst)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	enum iris_inst_sub_state clear_sub_state = 0;
	struct vb2_queue *dst_vq;
	int ret;

	dst_vq = v4l2_m2m_get_dst_vq(inst->m2m_ctx);

	if (inst->sub_state & IRIS_INST_SUB_DRAIN &&
	    inst->sub_state & IRIS_INST_SUB_DRAIN_LAST) {
		vb2_clear_last_buffer_dequeued(dst_vq);
		clear_sub_state = IRIS_INST_SUB_DRAIN | IRIS_INST_SUB_DRAIN_LAST;
		if (inst->sub_state & IRIS_INST_SUB_INPUT_PAUSE) {
			if (hfi_ops->session_resume_drain) {
				ret = hfi_ops->session_resume_drain(inst,
					V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
				if (ret)
					return ret;
			}
			clear_sub_state |= IRIS_INST_SUB_INPUT_PAUSE;
		}
		if (inst->sub_state & IRIS_INST_SUB_OUTPUT_PAUSE) {
			if (hfi_ops->session_resume_drain) {
				ret = hfi_ops->session_resume_drain(inst,
					V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
				if (ret)
					return ret;
			}
			clear_sub_state |= IRIS_INST_SUB_OUTPUT_PAUSE;
		}
	} else {
		dev_err(inst->core->dev, "start called before receiving last_flag\n");
		iris_inst_change_state(inst, IRIS_INST_ERROR);
		return -EBUSY;
	}

	inst->last_buffer_dequeued = false;

	return iris_inst_change_sub_state(inst, clear_sub_state, 0);
}

int iris_venc_stop_cmd(struct iris_inst *inst)
{
	const struct iris_hfi_command_ops *hfi_ops = inst->core->hfi_ops;
	int ret;

	ret = hfi_ops->session_drain(inst, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
	if (ret)
		return ret;

	ret = iris_inst_change_sub_state(inst, 0, IRIS_INST_SUB_DRAIN);

	iris_scale_power(inst);

	return ret;
}
