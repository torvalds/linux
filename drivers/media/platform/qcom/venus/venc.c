// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ctrls.h>

#include "hfi_venus_io.h"
#include "hfi_parser.h"
#include "core.h"
#include "helpers.h"
#include "venc.h"
#include "pm_helpers.h"

#define NUM_B_FRAMES_MAX	4

/*
 * Three resons to keep MPLANE formats (despite that the number of planes
 * currently is one):
 * - the MPLANE formats allow only one plane to be used
 * - the downstream driver use MPLANE formats too
 * - future firmware versions could add support for >1 planes
 */
static const struct venus_format venc_formats[] = {
	[VENUS_FMT_NV12] = {
		.pixfmt = V4L2_PIX_FMT_NV12,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
	},
	[VENUS_FMT_H264] = {
		.pixfmt = V4L2_PIX_FMT_H264,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	},
	[VENUS_FMT_VP8] = {
		.pixfmt = V4L2_PIX_FMT_VP8,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	},
	[VENUS_FMT_HEVC] = {
		.pixfmt = V4L2_PIX_FMT_HEVC,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	},
	[VENUS_FMT_MPEG4] = {
		.pixfmt = V4L2_PIX_FMT_MPEG4,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	},
	[VENUS_FMT_H263] = {
		.pixfmt = V4L2_PIX_FMT_H263,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	},
};

static const struct venus_format *
find_format(struct venus_inst *inst, u32 pixfmt, u32 type)
{
	const struct venus_format *fmt = venc_formats;
	unsigned int size = ARRAY_SIZE(venc_formats);
	unsigned int i;

	for (i = 0; i < size; i++) {
		if (fmt[i].pixfmt == pixfmt)
			break;
	}

	if (i == size || fmt[i].type != type)
		return NULL;

	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
	    !venus_helper_check_codec(inst, fmt[i].pixfmt))
		return NULL;

	return &fmt[i];
}

static const struct venus_format *
find_format_by_index(struct venus_inst *inst, unsigned int index, u32 type)
{
	const struct venus_format *fmt = venc_formats;
	unsigned int size = ARRAY_SIZE(venc_formats);
	unsigned int i, k = 0;

	if (index > size)
		return NULL;

	for (i = 0; i < size; i++) {
		bool valid;

		if (fmt[i].type != type)
			continue;
		valid = type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ||
			venus_helper_check_codec(inst, fmt[i].pixfmt);
		if (k == index && valid)
			break;
		if (valid)
			k++;
	}

	if (i == size)
		return NULL;

	return &fmt[i];
}

static int venc_v4l2_to_hfi(int id, int value)
{
	switch (id) {
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
		switch (value) {
		case V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC:
		default:
			return HFI_H264_ENTROPY_CAVLC;
		case V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC:
			return HFI_H264_ENTROPY_CABAC;
		}
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:
		switch (value) {
		case V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_ENABLED:
		default:
			return HFI_H264_DB_MODE_ALL_BOUNDARY;
		case V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED:
			return HFI_H264_DB_MODE_DISABLE;
		case V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED_AT_SLICE_BOUNDARY:
			return HFI_H264_DB_MODE_SKIP_SLICE_BOUNDARY;
		}
	}

	return 0;
}

static int
venc_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	strscpy(cap->driver, "qcom-venus", sizeof(cap->driver));
	strscpy(cap->card, "Qualcomm Venus video encoder", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:qcom-venus", sizeof(cap->bus_info));

	return 0;
}

static int venc_enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct venus_inst *inst = to_inst(file);
	const struct venus_format *fmt;

	fmt = find_format_by_index(inst, f->index, f->type);

	memset(f->reserved, 0, sizeof(f->reserved));

	if (!fmt)
		return -EINVAL;

	f->pixelformat = fmt->pixfmt;

	return 0;
}

static const struct venus_format *
venc_try_fmt_common(struct venus_inst *inst, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	struct v4l2_plane_pix_format *pfmt = pixmp->plane_fmt;
	const struct venus_format *fmt;
	u32 sizeimage;

	memset(pfmt[0].reserved, 0, sizeof(pfmt[0].reserved));
	memset(pixmp->reserved, 0, sizeof(pixmp->reserved));

	fmt = find_format(inst, pixmp->pixelformat, f->type);
	if (!fmt) {
		if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
			pixmp->pixelformat = V4L2_PIX_FMT_H264;
		else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			pixmp->pixelformat = V4L2_PIX_FMT_NV12;
		else
			return NULL;
		fmt = find_format(inst, pixmp->pixelformat, f->type);
		if (!fmt)
			return NULL;
	}

	pixmp->width = clamp(pixmp->width, frame_width_min(inst),
			     frame_width_max(inst));
	pixmp->height = clamp(pixmp->height, frame_height_min(inst),
			      frame_height_max(inst));

	pixmp->width = ALIGN(pixmp->width, 128);
	pixmp->height = ALIGN(pixmp->height, 32);

	pixmp->width = ALIGN(pixmp->width, 2);
	pixmp->height = ALIGN(pixmp->height, 2);

	if (pixmp->field == V4L2_FIELD_ANY)
		pixmp->field = V4L2_FIELD_NONE;
	pixmp->num_planes = fmt->num_planes;
	pixmp->flags = 0;

	sizeimage = venus_helper_get_framesz(pixmp->pixelformat,
					     pixmp->width,
					     pixmp->height);
	pfmt[0].sizeimage = max(ALIGN(pfmt[0].sizeimage, SZ_4K), sizeimage);

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		pfmt[0].bytesperline = ALIGN(pixmp->width, 128);
	else
		pfmt[0].bytesperline = 0;

	return fmt;
}

static int venc_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct venus_inst *inst = to_inst(file);

	venc_try_fmt_common(inst, f);

	return 0;
}

static int venc_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct venus_inst *inst = to_inst(file);
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	struct v4l2_pix_format_mplane orig_pixmp;
	const struct venus_format *fmt;
	struct v4l2_format format;
	u32 pixfmt_out = 0, pixfmt_cap = 0;
	struct vb2_queue *q;

	q = v4l2_m2m_get_vq(inst->m2m_ctx, f->type);
	if (!q)
		return -EINVAL;

	if (vb2_is_busy(q))
		return -EBUSY;

	orig_pixmp = *pixmp;

	fmt = venc_try_fmt_common(inst, f);
	if (!fmt)
		return -EINVAL;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		pixfmt_out = pixmp->pixelformat;
		pixfmt_cap = inst->fmt_cap->pixfmt;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		pixfmt_cap = pixmp->pixelformat;
		pixfmt_out = inst->fmt_out->pixfmt;
	}

	memset(&format, 0, sizeof(format));

	format.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	format.fmt.pix_mp.pixelformat = pixfmt_out;
	format.fmt.pix_mp.width = orig_pixmp.width;
	format.fmt.pix_mp.height = orig_pixmp.height;
	venc_try_fmt_common(inst, &format);

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		inst->out_width = format.fmt.pix_mp.width;
		inst->out_height = format.fmt.pix_mp.height;
		inst->colorspace = pixmp->colorspace;
		inst->ycbcr_enc = pixmp->ycbcr_enc;
		inst->quantization = pixmp->quantization;
		inst->xfer_func = pixmp->xfer_func;
	}

	memset(&format, 0, sizeof(format));

	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	format.fmt.pix_mp.pixelformat = pixfmt_cap;
	format.fmt.pix_mp.width = orig_pixmp.width;
	format.fmt.pix_mp.height = orig_pixmp.height;
	venc_try_fmt_common(inst, &format);

	inst->width = format.fmt.pix_mp.width;
	inst->height = format.fmt.pix_mp.height;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		inst->fmt_out = fmt;
	else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		inst->fmt_cap = fmt;
		inst->output_buf_size = pixmp->plane_fmt[0].sizeimage;
	}

	return 0;
}

static int venc_g_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	struct venus_inst *inst = to_inst(file);
	const struct venus_format *fmt;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		fmt = inst->fmt_cap;
	else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		fmt = inst->fmt_out;
	else
		return -EINVAL;

	pixmp->pixelformat = fmt->pixfmt;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		pixmp->width = inst->width;
		pixmp->height = inst->height;
		pixmp->colorspace = inst->colorspace;
		pixmp->ycbcr_enc = inst->ycbcr_enc;
		pixmp->quantization = inst->quantization;
		pixmp->xfer_func = inst->xfer_func;
	} else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		pixmp->width = inst->out_width;
		pixmp->height = inst->out_height;
	}

	venc_try_fmt_common(inst, f);

	return 0;
}

static int
venc_g_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct venus_inst *inst = to_inst(file);

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		s->r.width = inst->out_width;
		s->r.height = inst->out_height;
		break;
	case V4L2_SEL_TGT_CROP:
		s->r.width = inst->width;
		s->r.height = inst->height;
		break;
	default:
		return -EINVAL;
	}

	s->r.top = 0;
	s->r.left = 0;

	return 0;
}

static int
venc_s_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct venus_inst *inst = to_inst(file);

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	if (s->r.width > inst->out_width ||
	    s->r.height > inst->out_height)
		return -EINVAL;

	s->r.width = ALIGN(s->r.width, 2);
	s->r.height = ALIGN(s->r.height, 2);

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		s->r.top = 0;
		s->r.left = 0;
		inst->width = s->r.width;
		inst->height = s->r.height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int venc_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct venus_inst *inst = to_inst(file);
	struct v4l2_outputparm *out = &a->parm.output;
	struct v4l2_fract *timeperframe = &out->timeperframe;
	u64 us_per_frame, fps;

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	memset(out->reserved, 0, sizeof(out->reserved));

	if (!timeperframe->denominator)
		timeperframe->denominator = inst->timeperframe.denominator;
	if (!timeperframe->numerator)
		timeperframe->numerator = inst->timeperframe.numerator;

	out->capability = V4L2_CAP_TIMEPERFRAME;

	us_per_frame = timeperframe->numerator * (u64)USEC_PER_SEC;
	do_div(us_per_frame, timeperframe->denominator);

	if (!us_per_frame)
		return -EINVAL;

	fps = (u64)USEC_PER_SEC;
	do_div(fps, us_per_frame);

	inst->timeperframe = *timeperframe;
	inst->fps = fps;

	return 0;
}

static int venc_g_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct venus_inst *inst = to_inst(file);

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT &&
	    a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	a->parm.output.capability |= V4L2_CAP_TIMEPERFRAME;
	a->parm.output.timeperframe = inst->timeperframe;

	return 0;
}

static int venc_enum_framesizes(struct file *file, void *fh,
				struct v4l2_frmsizeenum *fsize)
{
	struct venus_inst *inst = to_inst(file);
	const struct venus_format *fmt;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;

	fmt = find_format(inst, fsize->pixel_format,
			  V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (!fmt) {
		fmt = find_format(inst, fsize->pixel_format,
				  V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
		if (!fmt)
			return -EINVAL;
	}

	if (fsize->index)
		return -EINVAL;

	fsize->stepwise.min_width = frame_width_min(inst);
	fsize->stepwise.max_width = frame_width_max(inst);
	fsize->stepwise.step_width = frame_width_step(inst);
	fsize->stepwise.min_height = frame_height_min(inst);
	fsize->stepwise.max_height = frame_height_max(inst);
	fsize->stepwise.step_height = frame_height_step(inst);

	return 0;
}

static int venc_enum_frameintervals(struct file *file, void *fh,
				    struct v4l2_frmivalenum *fival)
{
	struct venus_inst *inst = to_inst(file);
	const struct venus_format *fmt;
	unsigned int framerate_factor = 1;

	fival->type = V4L2_FRMIVAL_TYPE_STEPWISE;

	fmt = find_format(inst, fival->pixel_format,
			  V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	if (!fmt) {
		fmt = find_format(inst, fival->pixel_format,
				  V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);
		if (!fmt)
			return -EINVAL;
	}

	if (fival->index)
		return -EINVAL;

	if (!fival->width || !fival->height)
		return -EINVAL;

	if (fival->width > frame_width_max(inst) ||
	    fival->width < frame_width_min(inst) ||
	    fival->height > frame_height_max(inst) ||
	    fival->height < frame_height_min(inst))
		return -EINVAL;

	if (IS_V1(inst->core)) {
		/* framerate is reported in 1/65535 fps unit */
		framerate_factor = (1 << 16);
	}

	fival->stepwise.min.numerator = 1;
	fival->stepwise.min.denominator = frate_max(inst) / framerate_factor;
	fival->stepwise.max.numerator = 1;
	fival->stepwise.max.denominator = frate_min(inst) / framerate_factor;
	fival->stepwise.step.numerator = 1;
	fival->stepwise.step.denominator = frate_max(inst) / framerate_factor;

	return 0;
}

static int venc_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 2, NULL);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	default:
		return -EINVAL;
	}
}

static int
venc_encoder_cmd(struct file *file, void *fh, struct v4l2_encoder_cmd *cmd)
{
	struct venus_inst *inst = to_inst(file);
	struct hfi_frame_data fdata = {0};
	int ret = 0;

	ret = v4l2_m2m_ioctl_try_encoder_cmd(file, fh, cmd);
	if (ret)
		return ret;

	mutex_lock(&inst->lock);

	if (cmd->cmd == V4L2_ENC_CMD_STOP &&
	    inst->enc_state == VENUS_ENC_STATE_ENCODING) {
		/*
		 * Implement V4L2_ENC_CMD_STOP by enqueue an empty buffer on
		 * encoder input to signal EOS.
		 */
		if (!(inst->streamon_out && inst->streamon_cap))
			goto unlock;

		fdata.buffer_type = HFI_BUFFER_INPUT;
		fdata.flags |= HFI_BUFFERFLAG_EOS;
		fdata.device_addr = 0xdeadb000;

		ret = hfi_session_process_buf(inst, &fdata);

		inst->enc_state = VENUS_ENC_STATE_DRAIN;
	} else if (cmd->cmd == V4L2_ENC_CMD_START) {
		if (inst->enc_state == VENUS_ENC_STATE_DRAIN) {
			ret = -EBUSY;
			goto unlock;
		}
		if (inst->enc_state == VENUS_ENC_STATE_STOPPED) {
			vb2_clear_last_buffer_dequeued(&inst->fh.m2m_ctx->cap_q_ctx.q);
			inst->enc_state = VENUS_ENC_STATE_ENCODING;
		}
	}

unlock:
	mutex_unlock(&inst->lock);
	return ret;
}

static const struct v4l2_ioctl_ops venc_ioctl_ops = {
	.vidioc_querycap = venc_querycap,
	.vidioc_enum_fmt_vid_cap = venc_enum_fmt,
	.vidioc_enum_fmt_vid_out = venc_enum_fmt,
	.vidioc_s_fmt_vid_cap_mplane = venc_s_fmt,
	.vidioc_s_fmt_vid_out_mplane = venc_s_fmt,
	.vidioc_g_fmt_vid_cap_mplane = venc_g_fmt,
	.vidioc_g_fmt_vid_out_mplane = venc_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane = venc_try_fmt,
	.vidioc_try_fmt_vid_out_mplane = venc_try_fmt,
	.vidioc_g_selection = venc_g_selection,
	.vidioc_s_selection = venc_s_selection,
	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,
	.vidioc_s_parm = venc_s_parm,
	.vidioc_g_parm = venc_g_parm,
	.vidioc_enum_framesizes = venc_enum_framesizes,
	.vidioc_enum_frameintervals = venc_enum_frameintervals,
	.vidioc_subscribe_event = venc_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_try_encoder_cmd = v4l2_m2m_ioctl_try_encoder_cmd,
	.vidioc_encoder_cmd = venc_encoder_cmd,
};

static int venc_pm_get(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	struct device *dev = core->dev_enc;
	int ret;

	mutex_lock(&core->pm_lock);
	ret = pm_runtime_resume_and_get(dev);
	mutex_unlock(&core->pm_lock);

	return ret < 0 ? ret : 0;
}

static int venc_pm_put(struct venus_inst *inst, bool autosuspend)
{
	struct venus_core *core = inst->core;
	struct device *dev = core->dev_enc;
	int ret;

	mutex_lock(&core->pm_lock);

	if (autosuspend)
		ret = pm_runtime_put_autosuspend(dev);
	else
		ret = pm_runtime_put_sync(dev);

	mutex_unlock(&core->pm_lock);

	return ret < 0 ? ret : 0;
}

static int venc_pm_get_put(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	struct device *dev = core->dev_enc;
	int ret = 0;

	mutex_lock(&core->pm_lock);

	if (pm_runtime_suspended(dev)) {
		ret = pm_runtime_resume_and_get(dev);
		if (ret < 0)
			goto error;

		ret = pm_runtime_put_autosuspend(dev);
	}

error:
	mutex_unlock(&core->pm_lock);

	return ret < 0 ? ret : 0;
}

static void venc_pm_touch(struct venus_inst *inst)
{
	pm_runtime_mark_last_busy(inst->core->dev_enc);
}

static int venc_set_properties(struct venus_inst *inst)
{
	struct venc_controls *ctr = &inst->controls.enc;
	struct hfi_intra_period intra_period;
	struct hfi_framerate frate;
	struct hfi_bitrate brate;
	struct hfi_idr_period idrp;
	struct hfi_quantization quant;
	struct hfi_quantization_range quant_range;
	struct hfi_quantization_range_v2 quant_range_v2;
	struct hfi_enable en;
	struct hfi_ltr_mode ltr_mode;
	struct hfi_intra_refresh intra_refresh = {};
	u32 ptype, rate_control, bitrate;
	u32 profile, level;
	int ret;

	ret = venus_helper_set_work_mode(inst);
	if (ret)
		return ret;

	ptype = HFI_PROPERTY_CONFIG_FRAME_RATE;
	frate.buffer_type = HFI_BUFFER_OUTPUT;
	frate.framerate = inst->fps * (1 << 16);

	ret = hfi_session_set_property(inst, ptype, &frate);
	if (ret)
		return ret;

	if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_H264) {
		struct hfi_h264_vui_timing_info info;
		struct hfi_h264_entropy_control entropy;
		struct hfi_h264_db_control deblock;
		struct hfi_h264_8x8_transform h264_transform;

		ptype = HFI_PROPERTY_PARAM_VENC_H264_VUI_TIMING_INFO;
		info.enable = 1;
		info.fixed_framerate = 1;
		info.time_scale = NSEC_PER_SEC;

		ret = hfi_session_set_property(inst, ptype, &info);
		if (ret)
			return ret;

		ptype = HFI_PROPERTY_PARAM_VENC_H264_ENTROPY_CONTROL;
		entropy.entropy_mode = venc_v4l2_to_hfi(
					  V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE,
					  ctr->h264_entropy_mode);
		entropy.cabac_model = HFI_H264_CABAC_MODEL_0;

		ret = hfi_session_set_property(inst, ptype, &entropy);
		if (ret)
			return ret;

		ptype = HFI_PROPERTY_PARAM_VENC_H264_DEBLOCK_CONTROL;
		deblock.mode = venc_v4l2_to_hfi(
				      V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE,
				      ctr->h264_loop_filter_mode);
		deblock.slice_alpha_offset = ctr->h264_loop_filter_alpha;
		deblock.slice_beta_offset = ctr->h264_loop_filter_beta;

		ret = hfi_session_set_property(inst, ptype, &deblock);
		if (ret)
			return ret;

		ptype = HFI_PROPERTY_PARAM_VENC_H264_TRANSFORM_8X8;
		h264_transform.enable_type = 0;
		if (ctr->profile.h264 == V4L2_MPEG_VIDEO_H264_PROFILE_HIGH ||
		    ctr->profile.h264 == V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_HIGH)
			h264_transform.enable_type = ctr->h264_8x8_transform;

		ret = hfi_session_set_property(inst, ptype, &h264_transform);
		if (ret)
			return ret;

		if (ctr->layer_bitrate) {
			unsigned int i;

			ptype = HFI_PROPERTY_PARAM_VENC_HIER_P_MAX_NUM_ENH_LAYER;
			ret = hfi_session_set_property(inst, ptype, &ctr->h264_hier_layers);
			if (ret)
				return ret;

			ptype = HFI_PROPERTY_CONFIG_VENC_HIER_P_ENH_LAYER;
			ret = hfi_session_set_property(inst, ptype, &ctr->layer_bitrate);
			if (ret)
				return ret;

			for (i = 0; i < ctr->h264_hier_layers; ++i) {
				ptype = HFI_PROPERTY_CONFIG_VENC_TARGET_BITRATE;
				brate.bitrate = ctr->h264_hier_layer_bitrate[i];
				brate.layer_id = i;

				ret = hfi_session_set_property(inst, ptype, &brate);
				if (ret)
					return ret;
			}
		}
	}

	if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_H264 ||
	    inst->fmt_cap->pixfmt == V4L2_PIX_FMT_HEVC) {
		/* IDR periodicity, n:
		 * n = 0 - only the first I-frame is IDR frame
		 * n = 1 - all I-frames will be IDR frames
		 * n > 1 - every n-th I-frame will be IDR frame
		 */
		ptype = HFI_PROPERTY_CONFIG_VENC_IDR_PERIOD;
		idrp.idr_period = 0;
		ret = hfi_session_set_property(inst, ptype, &idrp);
		if (ret)
			return ret;
	}

	if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_HEVC &&
	    ctr->profile.hevc == V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10) {
		struct hfi_hdr10_pq_sei hdr10;
		unsigned int c;

		ptype = HFI_PROPERTY_PARAM_VENC_HDR10_PQ_SEI;

		for (c = 0; c < 3; c++) {
			hdr10.mastering.display_primaries_x[c] =
				ctr->mastering.display_primaries_x[c];
			hdr10.mastering.display_primaries_y[c] =
				ctr->mastering.display_primaries_y[c];
		}

		hdr10.mastering.white_point_x = ctr->mastering.white_point_x;
		hdr10.mastering.white_point_y = ctr->mastering.white_point_y;
		hdr10.mastering.max_display_mastering_luminance =
			ctr->mastering.max_display_mastering_luminance;
		hdr10.mastering.min_display_mastering_luminance =
			ctr->mastering.min_display_mastering_luminance;

		hdr10.cll.max_content_light = ctr->cll.max_content_light_level;
		hdr10.cll.max_pic_average_light =
			ctr->cll.max_pic_average_light_level;

		ret = hfi_session_set_property(inst, ptype, &hdr10);
		if (ret)
			return ret;
	}

	if (ctr->num_b_frames) {
		u32 max_num_b_frames = NUM_B_FRAMES_MAX;

		ptype = HFI_PROPERTY_PARAM_VENC_MAX_NUM_B_FRAMES;
		ret = hfi_session_set_property(inst, ptype, &max_num_b_frames);
		if (ret)
			return ret;
	}

	ptype = HFI_PROPERTY_CONFIG_VENC_INTRA_PERIOD;
	intra_period.pframes = ctr->num_p_frames;
	intra_period.bframes = ctr->num_b_frames;

	ret = hfi_session_set_property(inst, ptype, &intra_period);
	if (ret)
		return ret;

	if (!ctr->rc_enable)
		rate_control = HFI_RATE_CONTROL_OFF;
	else if (ctr->bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_VBR)
		rate_control = ctr->frame_skip_mode ? HFI_RATE_CONTROL_VBR_VFR :
						      HFI_RATE_CONTROL_VBR_CFR;
	else if (ctr->bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR)
		rate_control = ctr->frame_skip_mode ? HFI_RATE_CONTROL_CBR_VFR :
						      HFI_RATE_CONTROL_CBR_CFR;
	else if (ctr->bitrate_mode == V4L2_MPEG_VIDEO_BITRATE_MODE_CQ)
		rate_control = HFI_RATE_CONTROL_CQ;

	ptype = HFI_PROPERTY_PARAM_VENC_RATE_CONTROL;
	ret = hfi_session_set_property(inst, ptype, &rate_control);
	if (ret)
		return ret;

	if (rate_control == HFI_RATE_CONTROL_CQ && ctr->const_quality) {
		struct hfi_heic_frame_quality quality = {};

		ptype = HFI_PROPERTY_CONFIG_HEIC_FRAME_QUALITY;
		quality.frame_quality = ctr->const_quality;
		ret = hfi_session_set_property(inst, ptype, &quality);
		if (ret)
			return ret;
	}

	if (!ctr->layer_bitrate) {
		if (!ctr->bitrate)
			bitrate = 64000;
		else
			bitrate = ctr->bitrate;

		ptype = HFI_PROPERTY_CONFIG_VENC_TARGET_BITRATE;
		brate.bitrate = bitrate;
		brate.layer_id = 0;

		ret = hfi_session_set_property(inst, ptype, &brate);
		if (ret)
			return ret;

		if (!ctr->bitrate_peak)
			bitrate *= 2;
		else
			bitrate = ctr->bitrate_peak;

		ptype = HFI_PROPERTY_CONFIG_VENC_MAX_BITRATE;
		brate.bitrate = bitrate;
		brate.layer_id = 0;

		ret = hfi_session_set_property(inst, ptype, &brate);
		if (ret)
			return ret;
	}

	if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_H264 ||
	    inst->fmt_cap->pixfmt == V4L2_PIX_FMT_HEVC) {
		ptype = HFI_PROPERTY_CONFIG_VENC_SYNC_FRAME_SEQUENCE_HEADER;
		if (ctr->header_mode == V4L2_MPEG_VIDEO_HEADER_MODE_SEPARATE)
			en.enable = 0;
		else
			en.enable = 1;

		ret = hfi_session_set_property(inst, ptype, &en);
		if (ret)
			return ret;
	}

	ptype = HFI_PROPERTY_PARAM_VENC_SESSION_QP;
	if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_HEVC) {
		quant.qp_i = ctr->hevc_i_qp;
		quant.qp_p = ctr->hevc_p_qp;
		quant.qp_b = ctr->hevc_b_qp;
	} else {
		quant.qp_i = ctr->h264_i_qp;
		quant.qp_p = ctr->h264_p_qp;
		quant.qp_b = ctr->h264_b_qp;
	}
	quant.layer_id = 0;
	ret = hfi_session_set_property(inst, ptype, &quant);
	if (ret)
		return ret;

	if (inst->core->res->hfi_version == HFI_VERSION_4XX ||
	    inst->core->res->hfi_version == HFI_VERSION_6XX) {
		ptype = HFI_PROPERTY_PARAM_VENC_SESSION_QP_RANGE_V2;

		if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_HEVC) {
			quant_range_v2.min_qp.qp_packed = ctr->hevc_min_qp;
			quant_range_v2.max_qp.qp_packed = ctr->hevc_max_qp;
		} else if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_VP8) {
			quant_range_v2.min_qp.qp_packed = ctr->vp8_min_qp;
			quant_range_v2.max_qp.qp_packed = ctr->vp8_max_qp;
		} else {
			quant_range_v2.min_qp.qp_packed = ctr->h264_min_qp;
			quant_range_v2.max_qp.qp_packed = ctr->h264_max_qp;
		}

		ret = hfi_session_set_property(inst, ptype, &quant_range_v2);
	} else {
		ptype = HFI_PROPERTY_PARAM_VENC_SESSION_QP_RANGE;

		if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_HEVC) {
			quant_range.min_qp = ctr->hevc_min_qp;
			quant_range.max_qp = ctr->hevc_max_qp;
		} else if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_VP8) {
			quant_range.min_qp = ctr->vp8_min_qp;
			quant_range.max_qp = ctr->vp8_max_qp;
		} else {
			quant_range.min_qp = ctr->h264_min_qp;
			quant_range.max_qp = ctr->h264_max_qp;
		}

		quant_range.layer_id = 0;
		ret = hfi_session_set_property(inst, ptype, &quant_range);
	}

	if (ret)
		return ret;

	ptype = HFI_PROPERTY_PARAM_VENC_LTRMODE;
	ltr_mode.ltr_count = ctr->ltr_count;
	ltr_mode.ltr_mode = HFI_LTR_MODE_MANUAL;
	ltr_mode.trust_mode = 1;
	ret = hfi_session_set_property(inst, ptype, &ltr_mode);
	if (ret)
		return ret;

	switch (inst->hfi_codec) {
	case HFI_VIDEO_CODEC_H264:
		profile = ctr->profile.h264;
		level = ctr->level.h264;
		break;
	case HFI_VIDEO_CODEC_MPEG4:
		profile = ctr->profile.mpeg4;
		level = ctr->level.mpeg4;
		break;
	case HFI_VIDEO_CODEC_VP8:
		profile = ctr->profile.vp8;
		level = 0;
		break;
	case HFI_VIDEO_CODEC_VP9:
		profile = ctr->profile.vp9;
		level = ctr->level.vp9;
		break;
	case HFI_VIDEO_CODEC_HEVC:
		profile = ctr->profile.hevc;
		level = ctr->level.hevc;
		break;
	case HFI_VIDEO_CODEC_MPEG2:
	default:
		profile = 0;
		level = 0;
		break;
	}

	ret = venus_helper_set_profile_level(inst, profile, level);
	if (ret)
		return ret;

	if (inst->fmt_cap->pixfmt == V4L2_PIX_FMT_H264 ||
	    inst->fmt_cap->pixfmt == V4L2_PIX_FMT_HEVC) {
		struct hfi_enable en = {};

		ptype = HFI_PROPERTY_PARAM_VENC_H264_GENERATE_AUDNAL;

		if (ctr->aud_enable)
			en.enable = 1;

		ret = hfi_session_set_property(inst, ptype, &en);
	}

	if ((inst->fmt_cap->pixfmt == V4L2_PIX_FMT_H264 ||
	     inst->fmt_cap->pixfmt == V4L2_PIX_FMT_HEVC) &&
	    (rate_control == HFI_RATE_CONTROL_CBR_VFR ||
	     rate_control == HFI_RATE_CONTROL_CBR_CFR)) {
		intra_refresh.mode = HFI_INTRA_REFRESH_NONE;
		intra_refresh.cir_mbs = 0;

		if (ctr->intra_refresh_period) {
			u32 mbs;

			mbs = ALIGN(inst->width, 16) * ALIGN(inst->height, 16);
			mbs /= 16 * 16;
			if (mbs % ctr->intra_refresh_period)
				mbs++;
			mbs /= ctr->intra_refresh_period;

			intra_refresh.cir_mbs = mbs;
			if (ctr->intra_refresh_type ==
			    V4L2_CID_MPEG_VIDEO_INTRA_REFRESH_PERIOD_TYPE_CYCLIC)
				intra_refresh.mode = HFI_INTRA_REFRESH_CYCLIC;
			else
				intra_refresh.mode = HFI_INTRA_REFRESH_RANDOM;
		}

		ptype = HFI_PROPERTY_PARAM_VENC_INTRA_REFRESH;

		ret = hfi_session_set_property(inst, ptype, &intra_refresh);
		if (ret)
			return ret;
	}

	return 0;
}

static int venc_init_session(struct venus_inst *inst)
{
	int ret;

	ret = venus_helper_session_init(inst);
	if (ret == -EALREADY)
		return 0;
	else if (ret)
		return ret;

	ret = venus_helper_set_stride(inst, inst->out_width,
				      inst->out_height);
	if (ret)
		goto deinit;

	ret = venus_helper_set_input_resolution(inst, inst->width,
						inst->height);
	if (ret)
		goto deinit;

	ret = venus_helper_set_output_resolution(inst, inst->width,
						 inst->height,
						 HFI_BUFFER_OUTPUT);
	if (ret)
		goto deinit;

	ret = venus_helper_set_color_format(inst, inst->fmt_out->pixfmt);
	if (ret)
		goto deinit;

	ret = venc_set_properties(inst);
	if (ret)
		goto deinit;

	return 0;
deinit:
	hfi_session_deinit(inst);
	return ret;
}

static int venc_out_num_buffers(struct venus_inst *inst, unsigned int *num)
{
	struct hfi_buffer_requirements bufreq;
	int ret;

	ret = venus_helper_get_bufreq(inst, HFI_BUFFER_INPUT, &bufreq);
	if (ret)
		return ret;

	*num = bufreq.count_actual;

	return 0;
}

static int venc_queue_setup(struct vb2_queue *q,
			    unsigned int *num_buffers, unsigned int *num_planes,
			    unsigned int sizes[], struct device *alloc_devs[])
{
	struct venus_inst *inst = vb2_get_drv_priv(q);
	struct venus_core *core = inst->core;
	unsigned int num, min = 4;
	int ret;

	if (*num_planes) {
		if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
		    *num_planes != inst->fmt_out->num_planes)
			return -EINVAL;

		if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
		    *num_planes != inst->fmt_cap->num_planes)
			return -EINVAL;

		if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
		    sizes[0] < inst->input_buf_size)
			return -EINVAL;

		if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
		    sizes[0] < inst->output_buf_size)
			return -EINVAL;

		return 0;
	}

	if (test_bit(0, &core->sys_error)) {
		if (inst->nonblock)
			return -EAGAIN;

		ret = wait_event_interruptible(core->sys_err_done,
					       !test_bit(0, &core->sys_error));
		if (ret)
			return ret;
	}

	ret = venc_pm_get(inst);
	if (ret)
		return ret;

	mutex_lock(&inst->lock);
	ret = venc_init_session(inst);
	mutex_unlock(&inst->lock);

	if (ret)
		goto put_power;

	ret = venc_pm_put(inst, false);
	if (ret)
		return ret;

	switch (q->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		*num_planes = inst->fmt_out->num_planes;

		ret = venc_out_num_buffers(inst, &num);
		if (ret)
			break;

		num = max(num, min);
		*num_buffers = max(*num_buffers, num);
		inst->num_input_bufs = *num_buffers;

		sizes[0] = venus_helper_get_framesz(inst->fmt_out->pixfmt,
						    inst->out_width,
						    inst->out_height);
		inst->input_buf_size = sizes[0];
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		*num_planes = inst->fmt_cap->num_planes;
		*num_buffers = max(*num_buffers, min);
		inst->num_output_bufs = *num_buffers;
		sizes[0] = venus_helper_get_framesz(inst->fmt_cap->pixfmt,
						    inst->width,
						    inst->height);
		sizes[0] = max(sizes[0], inst->output_buf_size);
		inst->output_buf_size = sizes[0];
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
put_power:
	venc_pm_put(inst, false);
	return ret;
}

static int venc_buf_init(struct vb2_buffer *vb)
{
	struct venus_inst *inst = vb2_get_drv_priv(vb->vb2_queue);

	inst->buf_count++;

	return venus_helper_vb2_buf_init(vb);
}

static void venc_release_session(struct venus_inst *inst)
{
	int ret;

	venc_pm_get(inst);

	mutex_lock(&inst->lock);

	ret = hfi_session_deinit(inst);
	if (ret || inst->session_error)
		hfi_session_abort(inst);

	mutex_unlock(&inst->lock);

	venus_pm_load_scale(inst);
	INIT_LIST_HEAD(&inst->registeredbufs);
	venus_pm_release_core(inst);

	venc_pm_put(inst, false);
}

static void venc_buf_cleanup(struct vb2_buffer *vb)
{
	struct venus_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct venus_buffer *buf = to_venus_buffer(vbuf);

	mutex_lock(&inst->lock);
	if (vb->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		if (!list_empty(&inst->registeredbufs))
			list_del_init(&buf->reg_list);
	mutex_unlock(&inst->lock);

	inst->buf_count--;
	if (!inst->buf_count)
		venc_release_session(inst);
}

static int venc_verify_conf(struct venus_inst *inst)
{
	enum hfi_version ver = inst->core->res->hfi_version;
	struct hfi_buffer_requirements bufreq;
	int ret;

	if (!inst->num_input_bufs || !inst->num_output_bufs)
		return -EINVAL;

	ret = venus_helper_get_bufreq(inst, HFI_BUFFER_OUTPUT, &bufreq);
	if (ret)
		return ret;

	if (inst->num_output_bufs < bufreq.count_actual ||
	    inst->num_output_bufs < hfi_bufreq_get_count_min(&bufreq, ver))
		return -EINVAL;

	ret = venus_helper_get_bufreq(inst, HFI_BUFFER_INPUT, &bufreq);
	if (ret)
		return ret;

	if (inst->num_input_bufs < bufreq.count_actual ||
	    inst->num_input_bufs < hfi_bufreq_get_count_min(&bufreq, ver))
		return -EINVAL;

	return 0;
}

static int venc_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct venus_inst *inst = vb2_get_drv_priv(q);
	int ret;

	mutex_lock(&inst->lock);

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		inst->streamon_out = 1;
	else
		inst->streamon_cap = 1;

	if (!(inst->streamon_out & inst->streamon_cap)) {
		mutex_unlock(&inst->lock);
		return 0;
	}

	venus_helper_init_instance(inst);

	inst->sequence_cap = 0;
	inst->sequence_out = 0;

	ret = venc_pm_get(inst);
	if (ret)
		goto error;

	ret = venus_pm_acquire_core(inst);
	if (ret)
		goto put_power;

	ret = venc_pm_put(inst, true);
	if (ret)
		goto error;

	ret = venc_set_properties(inst);
	if (ret)
		goto error;

	ret = venc_verify_conf(inst);
	if (ret)
		goto error;

	ret = venus_helper_set_num_bufs(inst, inst->num_input_bufs,
					inst->num_output_bufs, 0);
	if (ret)
		goto error;

	ret = venus_helper_vb2_start_streaming(inst);
	if (ret)
		goto error;

	inst->enc_state = VENUS_ENC_STATE_ENCODING;

	mutex_unlock(&inst->lock);

	return 0;

put_power:
	venc_pm_put(inst, false);
error:
	venus_helper_buffers_done(inst, q->type, VB2_BUF_STATE_QUEUED);
	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		inst->streamon_out = 0;
	else
		inst->streamon_cap = 0;
	mutex_unlock(&inst->lock);
	return ret;
}

static void venc_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct venus_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	venc_pm_get_put(inst);

	mutex_lock(&inst->lock);

	if (inst->enc_state == VENUS_ENC_STATE_STOPPED) {
		vbuf->sequence = inst->sequence_cap++;
		vbuf->field = V4L2_FIELD_NONE;
		vb2_set_plane_payload(vb, 0, 0);
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);
		mutex_unlock(&inst->lock);
		return;
	}

	venus_helper_vb2_buf_queue(vb);
	mutex_unlock(&inst->lock);
}

static const struct vb2_ops venc_vb2_ops = {
	.queue_setup = venc_queue_setup,
	.buf_init = venc_buf_init,
	.buf_cleanup = venc_buf_cleanup,
	.buf_prepare = venus_helper_vb2_buf_prepare,
	.start_streaming = venc_start_streaming,
	.stop_streaming = venus_helper_vb2_stop_streaming,
	.buf_queue = venc_vb2_buf_queue,
};

static void venc_buf_done(struct venus_inst *inst, unsigned int buf_type,
			  u32 tag, u32 bytesused, u32 data_offset, u32 flags,
			  u32 hfi_flags, u64 timestamp_us)
{
	struct vb2_v4l2_buffer *vbuf;
	struct vb2_buffer *vb;
	unsigned int type;

	venc_pm_touch(inst);

	if (buf_type == HFI_BUFFER_INPUT)
		type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	else
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	vbuf = venus_helper_find_buf(inst, type, tag);
	if (!vbuf)
		return;

	vbuf->flags = flags;

	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		vb = &vbuf->vb2_buf;
		vb2_set_plane_payload(vb, 0, bytesused + data_offset);
		vb->planes[0].data_offset = data_offset;
		vb->timestamp = timestamp_us * NSEC_PER_USEC;
		vbuf->sequence = inst->sequence_cap++;
		if ((vbuf->flags & V4L2_BUF_FLAG_LAST) &&
		    inst->enc_state == VENUS_ENC_STATE_DRAIN) {
			inst->enc_state = VENUS_ENC_STATE_STOPPED;
		}
	} else {
		vbuf->sequence = inst->sequence_out++;
	}

	v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);
}

static void venc_event_notify(struct venus_inst *inst, u32 event,
			      struct hfi_event_data *data)
{
	struct device *dev = inst->core->dev_enc;

	venc_pm_touch(inst);

	if (event == EVT_SESSION_ERROR) {
		inst->session_error = true;
		venus_helper_vb2_queue_error(inst);
		dev_err(dev, "enc: event session error %x\n", inst->error);
	}
}

static const struct hfi_inst_ops venc_hfi_ops = {
	.buf_done = venc_buf_done,
	.event_notify = venc_event_notify,
};

static const struct v4l2_m2m_ops venc_m2m_ops = {
	.device_run = venus_helper_m2m_device_run,
	.job_abort = venus_helper_m2m_job_abort,
};

static int m2m_queue_init(void *priv, struct vb2_queue *src_vq,
			  struct vb2_queue *dst_vq)
{
	struct venus_inst *inst = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->ops = &venc_vb2_ops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->drv_priv = inst;
	src_vq->buf_struct_size = sizeof(struct venus_buffer);
	src_vq->allow_zero_bytesused = 1;
	src_vq->min_queued_buffers = 1;
	src_vq->dev = inst->core->dev;
	src_vq->lock = &inst->ctx_q_lock;
	if (inst->core->res->hfi_version == HFI_VERSION_1XX)
		src_vq->bidirectional = 1;
	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->ops = &venc_vb2_ops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->drv_priv = inst;
	dst_vq->buf_struct_size = sizeof(struct venus_buffer);
	dst_vq->allow_zero_bytesused = 1;
	dst_vq->min_queued_buffers = 1;
	dst_vq->dev = inst->core->dev;
	dst_vq->lock = &inst->ctx_q_lock;
	return vb2_queue_init(dst_vq);
}

static void venc_inst_init(struct venus_inst *inst)
{
	inst->fmt_cap = &venc_formats[VENUS_FMT_H264];
	inst->fmt_out = &venc_formats[VENUS_FMT_NV12];
	inst->width = 1280;
	inst->height = ALIGN(720, 32);
	inst->out_width = 1280;
	inst->out_height = 720;
	inst->fps = 15;
	inst->timeperframe.numerator = 1;
	inst->timeperframe.denominator = 15;
	inst->hfi_codec = HFI_VIDEO_CODEC_H264;
}

static int venc_open(struct file *file)
{
	struct venus_core *core = video_drvdata(file);
	struct venus_inst *inst;
	int ret;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	INIT_LIST_HEAD(&inst->dpbbufs);
	INIT_LIST_HEAD(&inst->registeredbufs);
	INIT_LIST_HEAD(&inst->internalbufs);
	INIT_LIST_HEAD(&inst->list);
	mutex_init(&inst->lock);
	mutex_init(&inst->ctx_q_lock);

	inst->core = core;
	inst->session_type = VIDC_SESSION_TYPE_ENC;
	inst->clk_data.core_id = VIDC_CORE_ID_DEFAULT;
	inst->core_acquired = false;
	inst->nonblock = file->f_flags & O_NONBLOCK;

	if (inst->enc_state == VENUS_ENC_STATE_DEINIT)
		inst->enc_state = VENUS_ENC_STATE_INIT;

	venus_helper_init_instance(inst);

	ret = venc_ctrl_init(inst);
	if (ret)
		goto err_free;

	venc_inst_init(inst);

	/*
	 * create m2m device for every instance, the m2m context scheduling
	 * is made by firmware side so we do not need to care about.
	 */
	inst->m2m_dev = v4l2_m2m_init(&venc_m2m_ops);
	if (IS_ERR(inst->m2m_dev)) {
		ret = PTR_ERR(inst->m2m_dev);
		goto err_ctrl_deinit;
	}

	inst->m2m_ctx = v4l2_m2m_ctx_init(inst->m2m_dev, inst, m2m_queue_init);
	if (IS_ERR(inst->m2m_ctx)) {
		ret = PTR_ERR(inst->m2m_ctx);
		goto err_m2m_dev_release;
	}

	ret = hfi_session_create(inst, &venc_hfi_ops);
	if (ret)
		goto err_m2m_ctx_release;

	v4l2_fh_init(&inst->fh, core->vdev_enc);

	inst->fh.ctrl_handler = &inst->ctrl_handler;
	v4l2_fh_add(&inst->fh);
	inst->fh.m2m_ctx = inst->m2m_ctx;
	file->private_data = &inst->fh;

	return 0;

err_m2m_ctx_release:
	v4l2_m2m_ctx_release(inst->m2m_ctx);
err_m2m_dev_release:
	v4l2_m2m_release(inst->m2m_dev);
err_ctrl_deinit:
	v4l2_ctrl_handler_free(&inst->ctrl_handler);
err_free:
	kfree(inst);
	return ret;
}

static int venc_close(struct file *file)
{
	struct venus_inst *inst = to_inst(file);

	venc_pm_get(inst);
	venus_close_common(inst);
	inst->enc_state = VENUS_ENC_STATE_DEINIT;
	venc_pm_put(inst, false);

	kfree(inst);
	return 0;
}

static const struct v4l2_file_operations venc_fops = {
	.owner = THIS_MODULE,
	.open = venc_open,
	.release = venc_close,
	.unlocked_ioctl = video_ioctl2,
	.poll = v4l2_m2m_fop_poll,
	.mmap = v4l2_m2m_fop_mmap,
};

static int venc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct video_device *vdev;
	struct venus_core *core;
	int ret;

	if (!dev->parent)
		return -EPROBE_DEFER;

	core = dev_get_drvdata(dev->parent);
	if (!core)
		return -EPROBE_DEFER;

	platform_set_drvdata(pdev, core);

	if (core->pm_ops->venc_get) {
		ret = core->pm_ops->venc_get(dev);
		if (ret)
			return ret;
	}

	vdev = video_device_alloc();
	if (!vdev)
		return -ENOMEM;

	strscpy(vdev->name, "qcom-venus-encoder", sizeof(vdev->name));
	vdev->release = video_device_release;
	vdev->fops = &venc_fops;
	vdev->ioctl_ops = &venc_ioctl_ops;
	vdev->vfl_dir = VFL_DIR_M2M;
	vdev->v4l2_dev = &core->v4l2_dev;
	vdev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_vdev_release;

	core->vdev_enc = vdev;
	core->dev_enc = dev;

	video_set_drvdata(vdev, core);
	pm_runtime_set_autosuspend_delay(dev, 2000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	return 0;

err_vdev_release:
	video_device_release(vdev);
	return ret;
}

static void venc_remove(struct platform_device *pdev)
{
	struct venus_core *core = dev_get_drvdata(pdev->dev.parent);

	video_unregister_device(core->vdev_enc);
	pm_runtime_disable(core->dev_enc);

	if (core->pm_ops->venc_put)
		core->pm_ops->venc_put(core->dev_enc);
}

static __maybe_unused int venc_runtime_suspend(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);
	const struct venus_pm_ops *pm_ops = core->pm_ops;
	int ret = 0;

	if (pm_ops->venc_power)
		ret = pm_ops->venc_power(dev, POWER_OFF);

	return ret;
}

static __maybe_unused int venc_runtime_resume(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);
	const struct venus_pm_ops *pm_ops = core->pm_ops;
	int ret = 0;

	if (pm_ops->venc_power)
		ret = pm_ops->venc_power(dev, POWER_ON);

	return ret;
}

static const struct dev_pm_ops venc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(venc_runtime_suspend, venc_runtime_resume, NULL)
};

static const struct of_device_id venc_dt_match[] = {
	{ .compatible = "venus-encoder" },
	{ }
};
MODULE_DEVICE_TABLE(of, venc_dt_match);

static struct platform_driver qcom_venus_enc_driver = {
	.probe = venc_probe,
	.remove = venc_remove,
	.driver = {
		.name = "qcom-venus-encoder",
		.of_match_table = venc_dt_match,
		.pm = &venc_pm_ops,
	},
};
module_platform_driver(qcom_venus_enc_driver);

MODULE_ALIAS("platform:qcom-venus-encoder");
MODULE_DESCRIPTION("Qualcomm Venus video encoder driver");
MODULE_LICENSE("GPL v2");
