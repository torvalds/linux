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
#include <media/videobuf2-dma-sg.h>
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
	{
		.pixfmt = V4L2_PIX_FMT_NV12,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
	}, {
		.pixfmt = V4L2_PIX_FMT_MPEG4,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	}, {
		.pixfmt = V4L2_PIX_FMT_H263,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	}, {
		.pixfmt = V4L2_PIX_FMT_H264,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	}, {
		.pixfmt = V4L2_PIX_FMT_VP8,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	}, {
		.pixfmt = V4L2_PIX_FMT_HEVC,
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

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
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
		s->r.width = inst->width;
		s->r.height = inst->height;
		break;
	case V4L2_SEL_TGT_CROP:
		s->r.width = inst->out_width;
		s->r.height = inst->out_height;
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

	switch (s->target) {
	case V4L2_SEL_TGT_CROP:
		if (s->r.width != inst->out_width ||
		    s->r.height != inst->out_height ||
		    s->r.top != 0 || s->r.left != 0)
			return -EINVAL;
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

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
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

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
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
	.vidioc_subscribe_event = v4l2_ctrl_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int venc_set_properties(struct venus_inst *inst)
{
	struct venc_controls *ctr = &inst->controls.enc;
	struct hfi_intra_period intra_period;
	struct hfi_framerate frate;
	struct hfi_bitrate brate;
	struct hfi_idr_period idrp;
	struct hfi_quantization quant;
	struct hfi_quantization_range quant_range;
	u32 ptype, rate_control, bitrate;
	u32 profile, level;
	int ret;

	ret = venus_helper_set_work_mode(inst, VIDC_WORK_MODE_2);
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
	}

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

	ptype = HFI_PROPERTY_PARAM_VENC_SESSION_QP;
	quant.qp_i = ctr->h264_i_qp;
	quant.qp_p = ctr->h264_p_qp;
	quant.qp_b = ctr->h264_b_qp;
	quant.layer_id = 0;
	ret = hfi_session_set_property(inst, ptype, &quant);
	if (ret)
		return ret;

	ptype = HFI_PROPERTY_PARAM_VENC_SESSION_QP_RANGE;
	quant_range.min_qp = ctr->h264_min_qp;
	quant_range.max_qp = ctr->h264_max_qp;
	quant_range.layer_id = 0;
	ret = hfi_session_set_property(inst, ptype, &quant_range);
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

	return 0;
}

static int venc_init_session(struct venus_inst *inst)
{
	int ret;

	ret = hfi_session_init(inst, inst->fmt_cap->pixfmt);
	if (ret)
		return ret;

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

	ret = venus_helper_init_codec_freq_data(inst);
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

	ret = venc_init_session(inst);
	if (ret)
		return ret;

	ret = venus_helper_get_bufreq(inst, HFI_BUFFER_INPUT, &bufreq);

	*num = bufreq.count_actual;

	hfi_session_deinit(inst);

	return ret;
}

static int venc_queue_setup(struct vb2_queue *q,
			    unsigned int *num_buffers, unsigned int *num_planes,
			    unsigned int sizes[], struct device *alloc_devs[])
{
	struct venus_inst *inst = vb2_get_drv_priv(q);
	unsigned int num, min = 4;
	int ret = 0;

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
						    inst->width,
						    inst->height);
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
	    inst->num_output_bufs < HFI_BUFREQ_COUNT_MIN(&bufreq, ver))
		return -EINVAL;

	ret = venus_helper_get_bufreq(inst, HFI_BUFFER_INPUT, &bufreq);
	if (ret)
		return ret;

	if (inst->num_input_bufs < bufreq.count_actual ||
	    inst->num_input_bufs < HFI_BUFREQ_COUNT_MIN(&bufreq, ver))
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

	ret = venc_init_session(inst);
	if (ret)
		goto bufs_done;

	ret = venus_pm_acquire_core(inst);
	if (ret)
		goto deinit_sess;

	ret = venc_set_properties(inst);
	if (ret)
		goto deinit_sess;

	ret = venc_verify_conf(inst);
	if (ret)
		goto deinit_sess;

	ret = venus_helper_set_num_bufs(inst, inst->num_input_bufs,
					inst->num_output_bufs, 0);
	if (ret)
		goto deinit_sess;

	ret = venus_helper_vb2_start_streaming(inst);
	if (ret)
		goto deinit_sess;

	mutex_unlock(&inst->lock);

	return 0;

deinit_sess:
	hfi_session_deinit(inst);
bufs_done:
	venus_helper_buffers_done(inst, q->type, VB2_BUF_STATE_QUEUED);
	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		inst->streamon_out = 0;
	else
		inst->streamon_cap = 0;
	mutex_unlock(&inst->lock);
	return ret;
}

static const struct vb2_ops venc_vb2_ops = {
	.queue_setup = venc_queue_setup,
	.buf_init = venus_helper_vb2_buf_init,
	.buf_prepare = venus_helper_vb2_buf_prepare,
	.start_streaming = venc_start_streaming,
	.stop_streaming = venus_helper_vb2_stop_streaming,
	.buf_queue = venus_helper_vb2_buf_queue,
};

static void venc_buf_done(struct venus_inst *inst, unsigned int buf_type,
			  u32 tag, u32 bytesused, u32 data_offset, u32 flags,
			  u32 hfi_flags, u64 timestamp_us)
{
	struct vb2_v4l2_buffer *vbuf;
	struct vb2_buffer *vb;
	unsigned int type;

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
	} else {
		vbuf->sequence = inst->sequence_out++;
	}

	v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);
}

static void venc_event_notify(struct venus_inst *inst, u32 event,
			      struct hfi_event_data *data)
{
	struct device *dev = inst->core->dev_enc;

	if (event == EVT_SESSION_ERROR) {
		inst->session_error = true;
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
	src_vq->mem_ops = &vb2_dma_sg_memops;
	src_vq->drv_priv = inst;
	src_vq->buf_struct_size = sizeof(struct venus_buffer);
	src_vq->allow_zero_bytesused = 1;
	src_vq->min_buffers_needed = 1;
	src_vq->dev = inst->core->dev;
	if (inst->core->res->hfi_version == HFI_VERSION_1XX)
		src_vq->bidirectional = 1;
	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_USERPTR | VB2_DMABUF;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->ops = &venc_vb2_ops;
	dst_vq->mem_ops = &vb2_dma_sg_memops;
	dst_vq->drv_priv = inst;
	dst_vq->buf_struct_size = sizeof(struct venus_buffer);
	dst_vq->allow_zero_bytesused = 1;
	dst_vq->min_buffers_needed = 1;
	dst_vq->dev = inst->core->dev;
	return vb2_queue_init(dst_vq);
}

static void venc_inst_init(struct venus_inst *inst)
{
	inst->fmt_cap = &venc_formats[2];
	inst->fmt_out = &venc_formats[0];
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

	inst->core = core;
	inst->session_type = VIDC_SESSION_TYPE_ENC;
	inst->clk_data.core_id = VIDC_CORE_ID_DEFAULT;
	inst->core_acquired = false;

	venus_helper_init_instance(inst);

	ret = pm_runtime_get_sync(core->dev_enc);
	if (ret < 0)
		goto err_put_sync;

	ret = venc_ctrl_init(inst);
	if (ret)
		goto err_put_sync;

	ret = hfi_session_create(inst, &venc_hfi_ops);
	if (ret)
		goto err_ctrl_deinit;

	venc_inst_init(inst);

	/*
	 * create m2m device for every instance, the m2m context scheduling
	 * is made by firmware side so we do not need to care about.
	 */
	inst->m2m_dev = v4l2_m2m_init(&venc_m2m_ops);
	if (IS_ERR(inst->m2m_dev)) {
		ret = PTR_ERR(inst->m2m_dev);
		goto err_session_destroy;
	}

	inst->m2m_ctx = v4l2_m2m_ctx_init(inst->m2m_dev, inst, m2m_queue_init);
	if (IS_ERR(inst->m2m_ctx)) {
		ret = PTR_ERR(inst->m2m_ctx);
		goto err_m2m_release;
	}

	v4l2_fh_init(&inst->fh, core->vdev_enc);

	inst->fh.ctrl_handler = &inst->ctrl_handler;
	v4l2_fh_add(&inst->fh);
	inst->fh.m2m_ctx = inst->m2m_ctx;
	file->private_data = &inst->fh;

	return 0;

err_m2m_release:
	v4l2_m2m_release(inst->m2m_dev);
err_session_destroy:
	hfi_session_destroy(inst);
err_ctrl_deinit:
	venc_ctrl_deinit(inst);
err_put_sync:
	pm_runtime_put_sync(core->dev_enc);
	kfree(inst);
	return ret;
}

static int venc_close(struct file *file)
{
	struct venus_inst *inst = to_inst(file);

	v4l2_m2m_ctx_release(inst->m2m_ctx);
	v4l2_m2m_release(inst->m2m_dev);
	venc_ctrl_deinit(inst);
	hfi_session_destroy(inst);
	mutex_destroy(&inst->lock);
	v4l2_fh_del(&inst->fh);
	v4l2_fh_exit(&inst->fh);

	pm_runtime_put_sync(inst->core->dev_enc);

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
	pm_runtime_enable(dev);

	return 0;

err_vdev_release:
	video_device_release(vdev);
	return ret;
}

static int venc_remove(struct platform_device *pdev)
{
	struct venus_core *core = dev_get_drvdata(pdev->dev.parent);

	video_unregister_device(core->vdev_enc);
	pm_runtime_disable(core->dev_enc);

	if (core->pm_ops->venc_put)
		core->pm_ops->venc_put(core->dev_enc);

	return 0;
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
