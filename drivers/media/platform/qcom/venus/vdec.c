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
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>

#include "hfi_venus_io.h"
#include "hfi_parser.h"
#include "core.h"
#include "helpers.h"
#include "vdec.h"
#include "pm_helpers.h"

/*
 * Three resons to keep MPLANE formats (despite that the number of planes
 * currently is one):
 * - the MPLANE formats allow only one plane to be used
 * - the downstream driver use MPLANE formats too
 * - future firmware versions could add support for >1 planes
 */
static const struct venus_format vdec_formats[] = {
	{
		.pixfmt = V4L2_PIX_FMT_NV12,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
	}, {
		.pixfmt = V4L2_PIX_FMT_MPEG4,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,
	}, {
		.pixfmt = V4L2_PIX_FMT_MPEG2,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,
	}, {
		.pixfmt = V4L2_PIX_FMT_H263,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,
	}, {
		.pixfmt = V4L2_PIX_FMT_VC1_ANNEX_G,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,
	}, {
		.pixfmt = V4L2_PIX_FMT_VC1_ANNEX_L,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,
	}, {
		.pixfmt = V4L2_PIX_FMT_H264,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,
	}, {
		.pixfmt = V4L2_PIX_FMT_VP8,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,
	}, {
		.pixfmt = V4L2_PIX_FMT_VP9,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,
	}, {
		.pixfmt = V4L2_PIX_FMT_XVID,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,
	}, {
		.pixfmt = V4L2_PIX_FMT_HEVC,
		.num_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION,
	},
};

static const struct venus_format *
find_format(struct venus_inst *inst, u32 pixfmt, u32 type)
{
	const struct venus_format *fmt = vdec_formats;
	unsigned int size = ARRAY_SIZE(vdec_formats);
	unsigned int i;

	for (i = 0; i < size; i++) {
		if (fmt[i].pixfmt == pixfmt)
			break;
	}

	if (i == size || fmt[i].type != type)
		return NULL;

	if (type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE &&
	    !venus_helper_check_codec(inst, fmt[i].pixfmt))
		return NULL;

	return &fmt[i];
}

static const struct venus_format *
find_format_by_index(struct venus_inst *inst, unsigned int index, u32 type)
{
	const struct venus_format *fmt = vdec_formats;
	unsigned int size = ARRAY_SIZE(vdec_formats);
	unsigned int i, k = 0;

	if (index > size)
		return NULL;

	for (i = 0; i < size; i++) {
		bool valid;

		if (fmt[i].type != type)
			continue;
		valid = type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ||
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

static const struct venus_format *
vdec_try_fmt_common(struct venus_inst *inst, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	struct v4l2_plane_pix_format *pfmt = pixmp->plane_fmt;
	const struct venus_format *fmt;
	u32 szimage;

	memset(pfmt[0].reserved, 0, sizeof(pfmt[0].reserved));
	memset(pixmp->reserved, 0, sizeof(pixmp->reserved));

	fmt = find_format(inst, pixmp->pixelformat, f->type);
	if (!fmt) {
		if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
			pixmp->pixelformat = V4L2_PIX_FMT_NV12;
		else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			pixmp->pixelformat = V4L2_PIX_FMT_H264;
		else
			return NULL;
		fmt = find_format(inst, pixmp->pixelformat, f->type);
	}

	pixmp->width = clamp(pixmp->width, frame_width_min(inst),
			     frame_width_max(inst));
	pixmp->height = clamp(pixmp->height, frame_height_min(inst),
			      frame_height_max(inst));

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		pixmp->height = ALIGN(pixmp->height, 32);

	if (pixmp->field == V4L2_FIELD_ANY)
		pixmp->field = V4L2_FIELD_NONE;
	pixmp->num_planes = fmt->num_planes;
	pixmp->flags = 0;

	szimage = venus_helper_get_framesz(pixmp->pixelformat, pixmp->width,
					   pixmp->height);

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		pfmt[0].sizeimage = szimage;
		pfmt[0].bytesperline = ALIGN(pixmp->width, 128);
	} else {
		pfmt[0].sizeimage = clamp_t(u32, pfmt[0].sizeimage, 0, SZ_8M);
		pfmt[0].sizeimage = max(pfmt[0].sizeimage, szimage);
		pfmt[0].bytesperline = 0;
	}

	return fmt;
}

static int vdec_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct venus_inst *inst = to_inst(file);

	vdec_try_fmt_common(inst, f);

	return 0;
}

static int vdec_check_src_change(struct venus_inst *inst)
{
	int ret;

	if (inst->subscriptions & V4L2_EVENT_SOURCE_CHANGE &&
	    inst->codec_state == VENUS_DEC_STATE_INIT &&
	    !inst->reconfig)
		return -EINVAL;

	if (inst->subscriptions & V4L2_EVENT_SOURCE_CHANGE)
		return 0;

	/*
	 * The code snippet below is a workaround for backward compatibility
	 * with applications which doesn't support V4L2 events. It will be
	 * dropped in future once those applications are fixed.
	 */

	if (inst->codec_state != VENUS_DEC_STATE_INIT)
		goto done;

	ret = wait_event_timeout(inst->reconf_wait, inst->reconfig,
				 msecs_to_jiffies(100));
	if (!ret)
		return -EINVAL;

	if (!(inst->codec_state == VENUS_DEC_STATE_CAPTURE_SETUP) ||
	    !inst->reconfig)
		dev_dbg(inst->core->dev, VDBGH "wrong state\n");

done:
	return 0;
}

static int vdec_g_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct venus_inst *inst = to_inst(file);
	const struct venus_format *fmt = NULL;
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	int ret;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		fmt = inst->fmt_cap;
	else if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		fmt = inst->fmt_out;

	if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		ret = vdec_check_src_change(inst);
		if (ret)
			return ret;
	}

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

	vdec_try_fmt_common(inst, f);

	return 0;
}

static int vdec_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
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

	fmt = vdec_try_fmt_common(inst, f);

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
	vdec_try_fmt_common(inst, &format);

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		inst->out_width = format.fmt.pix_mp.width;
		inst->out_height = format.fmt.pix_mp.height;
		inst->colorspace = pixmp->colorspace;
		inst->ycbcr_enc = pixmp->ycbcr_enc;
		inst->quantization = pixmp->quantization;
		inst->xfer_func = pixmp->xfer_func;
		inst->input_buf_size = pixmp->plane_fmt[0].sizeimage;
	}

	memset(&format, 0, sizeof(format));

	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	format.fmt.pix_mp.pixelformat = pixfmt_cap;
	format.fmt.pix_mp.width = orig_pixmp.width;
	format.fmt.pix_mp.height = orig_pixmp.height;
	vdec_try_fmt_common(inst, &format);

	inst->width = format.fmt.pix_mp.width;
	inst->height = format.fmt.pix_mp.height;
	inst->crop.top = 0;
	inst->crop.left = 0;
	inst->crop.width = inst->width;
	inst->crop.height = inst->height;

	if (f->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		inst->fmt_out = fmt;
	else if (f->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		inst->fmt_cap = fmt;

	return 0;
}

static int
vdec_g_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct venus_inst *inst = to_inst(file);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE &&
	    s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	s->r.top = 0;
	s->r.left = 0;

	switch (s->target) {
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP:
		if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
			return -EINVAL;
		s->r.width = inst->out_width;
		s->r.height = inst->out_height;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		s->r.width = inst->width;
		s->r.height = inst->height;
		break;
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE:
		if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
			return -EINVAL;
		s->r = inst->crop;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
vdec_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	strscpy(cap->driver, "qcom-venus", sizeof(cap->driver));
	strscpy(cap->card, "Qualcomm Venus video decoder", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:qcom-venus", sizeof(cap->bus_info));

	return 0;
}

static int vdec_enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct venus_inst *inst = to_inst(file);
	const struct venus_format *fmt;

	memset(f->reserved, 0, sizeof(f->reserved));

	fmt = find_format_by_index(inst, f->index, f->type);
	if (!fmt)
		return -EINVAL;

	f->pixelformat = fmt->pixfmt;
	f->flags = fmt->flags;

	return 0;
}

static int vdec_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct venus_inst *inst = to_inst(file);
	struct v4l2_captureparm *cap = &a->parm.capture;
	struct v4l2_fract *timeperframe = &cap->timeperframe;
	u64 us_per_frame, fps;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE &&
	    a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	memset(cap->reserved, 0, sizeof(cap->reserved));
	if (!timeperframe->denominator)
		timeperframe->denominator = inst->timeperframe.denominator;
	if (!timeperframe->numerator)
		timeperframe->numerator = inst->timeperframe.numerator;
	cap->readbuffers = 0;
	cap->extendedmode = 0;
	cap->capability = V4L2_CAP_TIMEPERFRAME;
	us_per_frame = timeperframe->numerator * (u64)USEC_PER_SEC;
	do_div(us_per_frame, timeperframe->denominator);

	if (!us_per_frame)
		return -EINVAL;

	fps = (u64)USEC_PER_SEC;
	do_div(fps, us_per_frame);

	inst->fps = fps;
	inst->timeperframe = *timeperframe;

	return 0;
}

static int vdec_enum_framesizes(struct file *file, void *fh,
				struct v4l2_frmsizeenum *fsize)
{
	struct venus_inst *inst = to_inst(file);
	const struct venus_format *fmt;

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

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;

	fsize->stepwise.min_width = frame_width_min(inst);
	fsize->stepwise.max_width = frame_width_max(inst);
	fsize->stepwise.step_width = frame_width_step(inst);
	fsize->stepwise.min_height = frame_height_min(inst);
	fsize->stepwise.max_height = frame_height_max(inst);
	fsize->stepwise.step_height = frame_height_step(inst);

	return 0;
}

static int vdec_subscribe_event(struct v4l2_fh *fh,
				const struct v4l2_event_subscription *sub)
{
	struct venus_inst *inst = container_of(fh, struct venus_inst, fh);
	int ret;

	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 2, NULL);
	case V4L2_EVENT_SOURCE_CHANGE:
		ret = v4l2_src_change_event_subscribe(fh, sub);
		if (ret)
			return ret;
		inst->subscriptions |= V4L2_EVENT_SOURCE_CHANGE;
		return 0;
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	default:
		return -EINVAL;
	}
}

static int
vdec_decoder_cmd(struct file *file, void *fh, struct v4l2_decoder_cmd *cmd)
{
	struct venus_inst *inst = to_inst(file);
	struct hfi_frame_data fdata = {0};
	int ret;

	ret = v4l2_m2m_ioctl_try_decoder_cmd(file, fh, cmd);
	if (ret)
		return ret;

	mutex_lock(&inst->lock);

	if (cmd->cmd == V4L2_DEC_CMD_STOP) {
		/*
		 * Implement V4L2_DEC_CMD_STOP by enqueue an empty buffer on
		 * decoder input to signal EOS.
		 */
		if (!(inst->streamon_out && inst->streamon_cap))
			goto unlock;

		fdata.buffer_type = HFI_BUFFER_INPUT;
		fdata.flags |= HFI_BUFFERFLAG_EOS;
		fdata.device_addr = 0xdeadb000;

		ret = hfi_session_process_buf(inst, &fdata);

		if (!ret && inst->codec_state == VENUS_DEC_STATE_DECODING) {
			inst->codec_state = VENUS_DEC_STATE_DRAIN;
			inst->drain_active = true;
		}
	}

unlock:
	mutex_unlock(&inst->lock);
	return ret;
}

static const struct v4l2_ioctl_ops vdec_ioctl_ops = {
	.vidioc_querycap = vdec_querycap,
	.vidioc_enum_fmt_vid_cap = vdec_enum_fmt,
	.vidioc_enum_fmt_vid_out = vdec_enum_fmt,
	.vidioc_s_fmt_vid_cap_mplane = vdec_s_fmt,
	.vidioc_s_fmt_vid_out_mplane = vdec_s_fmt,
	.vidioc_g_fmt_vid_cap_mplane = vdec_g_fmt,
	.vidioc_g_fmt_vid_out_mplane = vdec_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane = vdec_try_fmt,
	.vidioc_try_fmt_vid_out_mplane = vdec_try_fmt,
	.vidioc_g_selection = vdec_g_selection,
	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,
	.vidioc_s_parm = vdec_s_parm,
	.vidioc_enum_framesizes = vdec_enum_framesizes,
	.vidioc_subscribe_event = vdec_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
	.vidioc_try_decoder_cmd = v4l2_m2m_ioctl_try_decoder_cmd,
	.vidioc_decoder_cmd = vdec_decoder_cmd,
};

static int vdec_pm_get(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	struct device *dev = core->dev_dec;
	int ret;

	mutex_lock(&core->pm_lock);
	ret = pm_runtime_get_sync(dev);
	mutex_unlock(&core->pm_lock);

	return ret < 0 ? ret : 0;
}

static int vdec_pm_put(struct venus_inst *inst, bool autosuspend)
{
	struct venus_core *core = inst->core;
	struct device *dev = core->dev_dec;
	int ret;

	mutex_lock(&core->pm_lock);

	if (autosuspend)
		ret = pm_runtime_put_autosuspend(dev);
	else
		ret = pm_runtime_put_sync(dev);

	mutex_unlock(&core->pm_lock);

	return ret < 0 ? ret : 0;
}

static int vdec_pm_get_put(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	struct device *dev = core->dev_dec;
	int ret = 0;

	mutex_lock(&core->pm_lock);

	if (pm_runtime_suspended(dev)) {
		ret = pm_runtime_get_sync(dev);
		if (ret < 0)
			goto error;

		ret = pm_runtime_put_autosuspend(dev);
	}

error:
	mutex_unlock(&core->pm_lock);

	return ret < 0 ? ret : 0;
}

static void vdec_pm_touch(struct venus_inst *inst)
{
	pm_runtime_mark_last_busy(inst->core->dev_dec);
}

static int vdec_set_properties(struct venus_inst *inst)
{
	struct vdec_controls *ctr = &inst->controls.dec;
	struct hfi_enable en = { .enable = 1 };
	u32 ptype;
	int ret;

	if (ctr->post_loop_deb_mode) {
		ptype = HFI_PROPERTY_CONFIG_VDEC_POST_LOOP_DEBLOCKER;
		ret = hfi_session_set_property(inst, ptype, &en);
		if (ret)
			return ret;
	}

	return 0;
}

#define is_ubwc_fmt(fmt) (!!((fmt) & HFI_COLOR_FORMAT_UBWC_BASE))

static int vdec_output_conf(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	struct hfi_enable en = { .enable = 1 };
	struct hfi_buffer_requirements bufreq;
	u32 width = inst->out_width;
	u32 height = inst->out_height;
	u32 out_fmt, out2_fmt;
	bool ubwc = false;
	u32 ptype;
	int ret;

	ret = venus_helper_set_work_mode(inst, VIDC_WORK_MODE_2);
	if (ret)
		return ret;

	if (core->res->hfi_version == HFI_VERSION_1XX) {
		ptype = HFI_PROPERTY_PARAM_VDEC_CONTINUE_DATA_TRANSFER;
		ret = hfi_session_set_property(inst, ptype, &en);
		if (ret)
			return ret;
	}

	/* Force searching UBWC formats for bigger then HD resolutions */
	if (width > 1920 && height > ALIGN(1080, 32))
		ubwc = true;

	/* For Venus v4 UBWC format is mandatory */
	if (IS_V4(core))
		ubwc = true;

	ret = venus_helper_get_out_fmts(inst, inst->fmt_cap->pixfmt, &out_fmt,
					&out2_fmt, ubwc);
	if (ret)
		return ret;

	inst->output_buf_size =
			venus_helper_get_framesz_raw(out_fmt, width, height);
	inst->output2_buf_size =
			venus_helper_get_framesz_raw(out2_fmt, width, height);

	if (is_ubwc_fmt(out_fmt)) {
		inst->opb_buftype = HFI_BUFFER_OUTPUT2;
		inst->opb_fmt = out2_fmt;
		inst->dpb_buftype = HFI_BUFFER_OUTPUT;
		inst->dpb_fmt = out_fmt;
	} else if (is_ubwc_fmt(out2_fmt)) {
		inst->opb_buftype = HFI_BUFFER_OUTPUT;
		inst->opb_fmt = out_fmt;
		inst->dpb_buftype = HFI_BUFFER_OUTPUT2;
		inst->dpb_fmt = out2_fmt;
	} else {
		inst->opb_buftype = HFI_BUFFER_OUTPUT;
		inst->opb_fmt = out_fmt;
		inst->dpb_buftype = 0;
		inst->dpb_fmt = 0;
	}

	ret = venus_helper_set_raw_format(inst, inst->opb_fmt,
					  inst->opb_buftype);
	if (ret)
		return ret;

	if (inst->dpb_fmt) {
		ret = venus_helper_set_multistream(inst, false, true);
		if (ret)
			return ret;

		ret = venus_helper_set_raw_format(inst, inst->dpb_fmt,
						  inst->dpb_buftype);
		if (ret)
			return ret;

		ret = venus_helper_set_output_resolution(inst, width, height,
							 HFI_BUFFER_OUTPUT2);
		if (ret)
			return ret;
	}

	if (IS_V3(core) || IS_V4(core)) {
		ret = venus_helper_get_bufreq(inst, HFI_BUFFER_OUTPUT, &bufreq);
		if (ret)
			return ret;

		if (bufreq.size > inst->output_buf_size)
			return -EINVAL;

		if (inst->dpb_fmt) {
			ret = venus_helper_get_bufreq(inst, HFI_BUFFER_OUTPUT2,
						      &bufreq);
			if (ret)
				return ret;

			if (bufreq.size > inst->output2_buf_size)
				return -EINVAL;
		}

		if (inst->output2_buf_size) {
			ret = venus_helper_set_bufsize(inst,
						       inst->output2_buf_size,
						       HFI_BUFFER_OUTPUT2);
			if (ret)
				return ret;
		}

		if (inst->output_buf_size) {
			ret = venus_helper_set_bufsize(inst,
						       inst->output_buf_size,
						       HFI_BUFFER_OUTPUT);
			if (ret)
				return ret;
		}
	}

	ret = venus_helper_set_dyn_bufmode(inst);
	if (ret)
		return ret;

	return 0;
}

static int vdec_session_init(struct venus_inst *inst)
{
	int ret;

	ret = venus_helper_session_init(inst);
	if (ret == -EALREADY)
		return 0;
	else if (ret)
		return ret;

	ret = venus_helper_set_input_resolution(inst, frame_width_min(inst),
						frame_height_min(inst));
	if (ret)
		goto deinit;

	return 0;
deinit:
	hfi_session_deinit(inst);
	return ret;
}

static int vdec_num_buffers(struct venus_inst *inst, unsigned int *in_num,
			    unsigned int *out_num)
{
	enum hfi_version ver = inst->core->res->hfi_version;
	struct hfi_buffer_requirements bufreq;
	int ret;

	*in_num = *out_num = 0;

	ret = venus_helper_get_bufreq(inst, HFI_BUFFER_INPUT, &bufreq);
	if (ret)
		return ret;

	*in_num = HFI_BUFREQ_COUNT_MIN(&bufreq, ver);

	ret = venus_helper_get_bufreq(inst, HFI_BUFFER_OUTPUT, &bufreq);
	if (ret)
		return ret;

	*out_num = HFI_BUFREQ_COUNT_MIN(&bufreq, ver);

	return 0;
}

static int vdec_queue_setup(struct vb2_queue *q,
			    unsigned int *num_buffers, unsigned int *num_planes,
			    unsigned int sizes[], struct device *alloc_devs[])
{
	struct venus_inst *inst = vb2_get_drv_priv(q);
	unsigned int in_num, out_num;
	int ret = 0;

	if (*num_planes) {
		unsigned int output_buf_size = venus_helper_get_opb_size(inst);

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
		    sizes[0] < output_buf_size)
			return -EINVAL;

		return 0;
	}

	ret = vdec_pm_get(inst);
	if (ret)
		return ret;

	ret = vdec_session_init(inst);
	if (ret)
		goto put_power;

	ret = vdec_num_buffers(inst, &in_num, &out_num);
	if (ret)
		goto put_power;

	ret = vdec_pm_put(inst, false);
	if (ret)
		return ret;

	switch (q->type) {
	case V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE:
		*num_planes = inst->fmt_out->num_planes;
		sizes[0] = venus_helper_get_framesz(inst->fmt_out->pixfmt,
						    inst->out_width,
						    inst->out_height);
		sizes[0] = max(sizes[0], inst->input_buf_size);
		inst->input_buf_size = sizes[0];
		*num_buffers = max(*num_buffers, in_num);
		inst->num_input_bufs = *num_buffers;
		inst->num_output_bufs = out_num;
		break;
	case V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE:
		*num_planes = inst->fmt_cap->num_planes;
		sizes[0] = venus_helper_get_framesz(inst->fmt_cap->pixfmt,
						    inst->width,
						    inst->height);
		inst->output_buf_size = sizes[0];
		*num_buffers = max(*num_buffers, out_num);
		inst->num_output_bufs = *num_buffers;

		mutex_lock(&inst->lock);
		if (inst->codec_state == VENUS_DEC_STATE_CAPTURE_SETUP)
			inst->codec_state = VENUS_DEC_STATE_STOPPED;
		mutex_unlock(&inst->lock);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;

put_power:
	vdec_pm_put(inst, false);
	return ret;
}

static int vdec_verify_conf(struct venus_inst *inst)
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

	if (inst->num_input_bufs < HFI_BUFREQ_COUNT_MIN(&bufreq, ver))
		return -EINVAL;

	return 0;
}

static int vdec_start_capture(struct venus_inst *inst)
{
	int ret;

	if (!inst->streamon_out)
		return 0;

	if (inst->codec_state == VENUS_DEC_STATE_DECODING) {
		if (inst->reconfig)
			goto reconfigure;

		venus_helper_queue_dpb_bufs(inst);
		venus_helper_process_initial_cap_bufs(inst);
		inst->streamon_cap = 1;
		return 0;
	}

	if (inst->codec_state != VENUS_DEC_STATE_STOPPED)
		return 0;

reconfigure:
	ret = vdec_output_conf(inst);
	if (ret)
		return ret;

	ret = venus_helper_set_num_bufs(inst, inst->num_input_bufs,
					VB2_MAX_FRAME, VB2_MAX_FRAME);
	if (ret)
		return ret;

	ret = venus_helper_intbufs_realloc(inst);
	if (ret)
		goto err;

	ret = venus_helper_alloc_dpb_bufs(inst);
	if (ret)
		goto err;

	ret = venus_helper_queue_dpb_bufs(inst);
	if (ret)
		goto free_dpb_bufs;

	ret = venus_helper_process_initial_cap_bufs(inst);
	if (ret)
		goto free_dpb_bufs;

	venus_pm_load_scale(inst);

	inst->next_buf_last = false;

	ret = hfi_session_continue(inst);
	if (ret)
		goto free_dpb_bufs;

	inst->codec_state = VENUS_DEC_STATE_DECODING;

	if (inst->drain_active)
		inst->codec_state = VENUS_DEC_STATE_DRAIN;

	inst->streamon_cap = 1;
	inst->sequence_cap = 0;
	inst->reconfig = false;
	inst->drain_active = false;

	return 0;

free_dpb_bufs:
	venus_helper_free_dpb_bufs(inst);
err:
	return ret;
}

static int vdec_start_output(struct venus_inst *inst)
{
	int ret;

	if (inst->codec_state == VENUS_DEC_STATE_SEEK) {
		ret = venus_helper_process_initial_out_bufs(inst);
		if (inst->next_buf_last)
			inst->codec_state = VENUS_DEC_STATE_DRC;
		else
			inst->codec_state = VENUS_DEC_STATE_DECODING;
		goto done;
	}

	if (inst->codec_state == VENUS_DEC_STATE_INIT ||
	    inst->codec_state == VENUS_DEC_STATE_CAPTURE_SETUP) {
		ret = venus_helper_process_initial_out_bufs(inst);
		goto done;
	}

	if (inst->codec_state != VENUS_DEC_STATE_DEINIT)
		return -EINVAL;

	venus_helper_init_instance(inst);
	inst->sequence_out = 0;
	inst->reconfig = false;
	inst->next_buf_last = false;

	ret = vdec_set_properties(inst);
	if (ret)
		return ret;

	ret = vdec_output_conf(inst);
	if (ret)
		return ret;

	ret = vdec_verify_conf(inst);
	if (ret)
		return ret;

	ret = venus_helper_set_num_bufs(inst, inst->num_input_bufs,
					VB2_MAX_FRAME, VB2_MAX_FRAME);
	if (ret)
		return ret;

	ret = venus_helper_vb2_start_streaming(inst);
	if (ret)
		return ret;

	ret = venus_helper_process_initial_out_bufs(inst);
	if (ret)
		return ret;

	inst->codec_state = VENUS_DEC_STATE_INIT;

done:
	inst->streamon_out = 1;
	return ret;
}

static int vdec_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct venus_inst *inst = vb2_get_drv_priv(q);
	int ret;

	mutex_lock(&inst->lock);

	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		ret = vdec_start_capture(inst);
	} else {
		ret = vdec_pm_get(inst);
		if (ret)
			goto error;

		ret = venus_pm_acquire_core(inst);
		if (ret)
			goto put_power;

		ret = vdec_pm_put(inst, true);
		if (ret)
			goto error;

		ret = vdec_start_output(inst);
	}

	if (ret)
		goto error;

	mutex_unlock(&inst->lock);
	return 0;

put_power:
	vdec_pm_put(inst, false);
error:
	venus_helper_buffers_done(inst, q->type, VB2_BUF_STATE_QUEUED);
	mutex_unlock(&inst->lock);
	return ret;
}

static void vdec_cancel_dst_buffers(struct venus_inst *inst)
{
	struct vb2_v4l2_buffer *buf;

	while ((buf = v4l2_m2m_dst_buf_remove(inst->m2m_ctx)))
		v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
}

static int vdec_stop_capture(struct venus_inst *inst)
{
	int ret = 0;

	switch (inst->codec_state) {
	case VENUS_DEC_STATE_DECODING:
		ret = hfi_session_flush(inst, HFI_FLUSH_ALL, true);
		fallthrough;
	case VENUS_DEC_STATE_DRAIN:
		inst->codec_state = VENUS_DEC_STATE_STOPPED;
		inst->drain_active = false;
		fallthrough;
	case VENUS_DEC_STATE_SEEK:
		vdec_cancel_dst_buffers(inst);
		break;
	case VENUS_DEC_STATE_DRC:
		ret = hfi_session_flush(inst, HFI_FLUSH_OUTPUT, true);
		inst->codec_state = VENUS_DEC_STATE_CAPTURE_SETUP;
		venus_helper_free_dpb_bufs(inst);
		break;
	default:
		break;
	}

	return ret;
}

static int vdec_stop_output(struct venus_inst *inst)
{
	int ret = 0;

	switch (inst->codec_state) {
	case VENUS_DEC_STATE_DECODING:
	case VENUS_DEC_STATE_DRAIN:
	case VENUS_DEC_STATE_STOPPED:
	case VENUS_DEC_STATE_DRC:
		ret = hfi_session_flush(inst, HFI_FLUSH_ALL, true);
		inst->codec_state = VENUS_DEC_STATE_SEEK;
		break;
	case VENUS_DEC_STATE_INIT:
	case VENUS_DEC_STATE_CAPTURE_SETUP:
		ret = hfi_session_flush(inst, HFI_FLUSH_INPUT, true);
		break;
	default:
		break;
	}

	return ret;
}

static void vdec_stop_streaming(struct vb2_queue *q)
{
	struct venus_inst *inst = vb2_get_drv_priv(q);
	int ret = -EINVAL;

	mutex_lock(&inst->lock);

	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		ret = vdec_stop_capture(inst);
	else
		ret = vdec_stop_output(inst);

	venus_helper_buffers_done(inst, q->type, VB2_BUF_STATE_ERROR);

	if (ret)
		goto unlock;

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		inst->streamon_out = 0;
	else
		inst->streamon_cap = 0;

unlock:
	mutex_unlock(&inst->lock);
}

static void vdec_session_release(struct venus_inst *inst)
{
	struct venus_core *core = inst->core;
	int ret, abort = 0;

	vdec_pm_get(inst);

	mutex_lock(&inst->lock);
	inst->codec_state = VENUS_DEC_STATE_DEINIT;

	ret = hfi_session_stop(inst);
	abort = (ret && ret != -EINVAL) ? 1 : 0;
	ret = hfi_session_unload_res(inst);
	abort = (ret && ret != -EINVAL) ? 1 : 0;
	ret = venus_helper_unregister_bufs(inst);
	abort = (ret && ret != -EINVAL) ? 1 : 0;
	ret = venus_helper_intbufs_free(inst);
	abort = (ret && ret != -EINVAL) ? 1 : 0;
	ret = hfi_session_deinit(inst);
	abort = (ret && ret != -EINVAL) ? 1 : 0;

	if (inst->session_error || core->sys_error)
		abort = 1;

	if (abort)
		hfi_session_abort(inst);

	venus_helper_free_dpb_bufs(inst);
	venus_pm_load_scale(inst);
	INIT_LIST_HEAD(&inst->registeredbufs);
	mutex_unlock(&inst->lock);

	venus_pm_release_core(inst);
	vdec_pm_put(inst, false);
}

static int vdec_buf_init(struct vb2_buffer *vb)
{
	struct venus_inst *inst = vb2_get_drv_priv(vb->vb2_queue);

	inst->buf_count++;

	return venus_helper_vb2_buf_init(vb);
}

static void vdec_buf_cleanup(struct vb2_buffer *vb)
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
		vdec_session_release(inst);
}

static void vdec_vb2_buf_queue(struct vb2_buffer *vb)
{
	struct venus_inst *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	static const struct v4l2_event eos = { .type = V4L2_EVENT_EOS };

	vdec_pm_get_put(inst);

	mutex_lock(&inst->lock);

	if (inst->next_buf_last && V4L2_TYPE_IS_CAPTURE(vb->vb2_queue->type) &&
	    inst->codec_state == VENUS_DEC_STATE_DRC) {
		vbuf->flags |= V4L2_BUF_FLAG_LAST;
		vbuf->sequence = inst->sequence_cap++;
		vbuf->field = V4L2_FIELD_NONE;
		vb2_set_plane_payload(vb, 0, 0);
		v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);
		v4l2_event_queue_fh(&inst->fh, &eos);
		inst->next_buf_last = false;
		mutex_unlock(&inst->lock);
		return;
	}

	venus_helper_vb2_buf_queue(vb);
	mutex_unlock(&inst->lock);
}

static const struct vb2_ops vdec_vb2_ops = {
	.queue_setup = vdec_queue_setup,
	.buf_init = vdec_buf_init,
	.buf_cleanup = vdec_buf_cleanup,
	.buf_prepare = venus_helper_vb2_buf_prepare,
	.start_streaming = vdec_start_streaming,
	.stop_streaming = vdec_stop_streaming,
	.buf_queue = vdec_vb2_buf_queue,
};

static void vdec_buf_done(struct venus_inst *inst, unsigned int buf_type,
			  u32 tag, u32 bytesused, u32 data_offset, u32 flags,
			  u32 hfi_flags, u64 timestamp_us)
{
	enum vb2_buffer_state state = VB2_BUF_STATE_DONE;
	struct vb2_v4l2_buffer *vbuf;
	struct vb2_buffer *vb;
	unsigned int type;

	vdec_pm_touch(inst);

	if (buf_type == HFI_BUFFER_INPUT)
		type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	else
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

	vbuf = venus_helper_find_buf(inst, type, tag);
	if (!vbuf)
		return;

	vbuf->flags = flags;
	vbuf->field = V4L2_FIELD_NONE;
	vb = &vbuf->vb2_buf;

	if (type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		vb2_set_plane_payload(vb, 0, bytesused);
		vb->planes[0].data_offset = data_offset;
		vb->timestamp = timestamp_us * NSEC_PER_USEC;
		vbuf->sequence = inst->sequence_cap++;

		if (vbuf->flags & V4L2_BUF_FLAG_LAST) {
			const struct v4l2_event ev = { .type = V4L2_EVENT_EOS };

			v4l2_event_queue_fh(&inst->fh, &ev);

			if (inst->codec_state == VENUS_DEC_STATE_DRAIN) {
				inst->drain_active = false;
				inst->codec_state = VENUS_DEC_STATE_STOPPED;
			}
		}

		if (!bytesused)
			state = VB2_BUF_STATE_ERROR;
	} else {
		vbuf->sequence = inst->sequence_out++;
	}

	venus_helper_get_ts_metadata(inst, timestamp_us, vbuf);

	if (hfi_flags & HFI_BUFFERFLAG_READONLY)
		venus_helper_acquire_buf_ref(vbuf);

	if (hfi_flags & HFI_BUFFERFLAG_DATACORRUPT)
		state = VB2_BUF_STATE_ERROR;

	if (hfi_flags & HFI_BUFFERFLAG_DROP_FRAME) {
		state = VB2_BUF_STATE_ERROR;
		vb2_set_plane_payload(vb, 0, 0);
		vb->timestamp = 0;
	}

	v4l2_m2m_buf_done(vbuf, state);
}

static void vdec_event_change(struct venus_inst *inst,
			      struct hfi_event_data *ev_data, bool sufficient)
{
	static const struct v4l2_event ev = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION };
	struct device *dev = inst->core->dev_dec;
	struct v4l2_format format = {};

	mutex_lock(&inst->lock);

	format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	format.fmt.pix_mp.pixelformat = inst->fmt_cap->pixfmt;
	format.fmt.pix_mp.width = ev_data->width;
	format.fmt.pix_mp.height = ev_data->height;

	vdec_try_fmt_common(inst, &format);

	inst->width = format.fmt.pix_mp.width;
	inst->height = format.fmt.pix_mp.height;
	/*
	 * Some versions of the firmware do not report crop information for
	 * all codecs. For these cases, set the crop to the coded resolution.
	 */
	if (ev_data->input_crop.width > 0 && ev_data->input_crop.height > 0) {
		inst->crop.left = ev_data->input_crop.left;
		inst->crop.top = ev_data->input_crop.top;
		inst->crop.width = ev_data->input_crop.width;
		inst->crop.height = ev_data->input_crop.height;
	} else {
		inst->crop.left = 0;
		inst->crop.top = 0;
		inst->crop.width = ev_data->width;
		inst->crop.height = ev_data->height;
	}

	inst->out_width = ev_data->width;
	inst->out_height = ev_data->height;

	if (inst->bit_depth != ev_data->bit_depth)
		inst->bit_depth = ev_data->bit_depth;

	if (inst->pic_struct != ev_data->pic_struct)
		inst->pic_struct = ev_data->pic_struct;

	dev_dbg(dev, VDBGM "event %s sufficient resources (%ux%u)\n",
		sufficient ? "" : "not", ev_data->width, ev_data->height);

	switch (inst->codec_state) {
	case VENUS_DEC_STATE_INIT:
		inst->codec_state = VENUS_DEC_STATE_CAPTURE_SETUP;
		break;
	case VENUS_DEC_STATE_DECODING:
	case VENUS_DEC_STATE_DRAIN:
		inst->codec_state = VENUS_DEC_STATE_DRC;
		break;
	default:
		break;
	}

	/*
	 * The assumption is that the firmware have to return the last buffer
	 * before this event is received in the v4l2 driver. Also the firmware
	 * itself doesn't mark the last decoder output buffer with HFI EOS flag.
	 */

	if (inst->codec_state == VENUS_DEC_STATE_DRC) {
		int ret;

		inst->next_buf_last = true;

		ret = hfi_session_flush(inst, HFI_FLUSH_OUTPUT, false);
		if (ret)
			dev_dbg(dev, VDBGH "flush output error %d\n", ret);
	}

	inst->next_buf_last = true;
	inst->reconfig = true;
	v4l2_event_queue_fh(&inst->fh, &ev);
	wake_up(&inst->reconf_wait);

	mutex_unlock(&inst->lock);
}

static void vdec_event_notify(struct venus_inst *inst, u32 event,
			      struct hfi_event_data *data)
{
	struct venus_core *core = inst->core;
	struct device *dev = core->dev_dec;

	vdec_pm_touch(inst);

	switch (event) {
	case EVT_SESSION_ERROR:
		inst->session_error = true;
		dev_err(dev, "dec: event session error %x\n", inst->error);
		break;
	case EVT_SYS_EVENT_CHANGE:
		switch (data->event_type) {
		case HFI_EVENT_DATA_SEQUENCE_CHANGED_SUFFICIENT_BUF_RESOURCES:
			vdec_event_change(inst, data, true);
			break;
		case HFI_EVENT_DATA_SEQUENCE_CHANGED_INSUFFICIENT_BUF_RESOURCES:
			vdec_event_change(inst, data, false);
			break;
		case HFI_EVENT_RELEASE_BUFFER_REFERENCE:
			venus_helper_release_buf_ref(inst, data->tag);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}
}

static void vdec_flush_done(struct venus_inst *inst)
{
	dev_dbg(inst->core->dev_dec, VDBGH "flush done\n");
}

static const struct hfi_inst_ops vdec_hfi_ops = {
	.buf_done = vdec_buf_done,
	.event_notify = vdec_event_notify,
	.flush_done = vdec_flush_done,
};

static void vdec_inst_init(struct venus_inst *inst)
{
	inst->hfi_codec = HFI_VIDEO_CODEC_H264;
	inst->fmt_out = &vdec_formats[6];
	inst->fmt_cap = &vdec_formats[0];
	inst->width = frame_width_min(inst);
	inst->height = ALIGN(frame_height_min(inst), 32);
	inst->crop.left = 0;
	inst->crop.top = 0;
	inst->crop.width = inst->width;
	inst->crop.height = inst->height;
	inst->out_width = frame_width_min(inst);
	inst->out_height = frame_height_min(inst);
	inst->fps = 30;
	inst->timeperframe.numerator = 1;
	inst->timeperframe.denominator = 30;
	inst->opb_buftype = HFI_BUFFER_OUTPUT;
}

static void vdec_m2m_device_run(void *priv)
{
}

static const struct v4l2_m2m_ops vdec_m2m_ops = {
	.device_run = vdec_m2m_device_run,
	.job_abort = venus_helper_m2m_job_abort,
};

static int m2m_queue_init(void *priv, struct vb2_queue *src_vq,
			  struct vb2_queue *dst_vq)
{
	struct venus_inst *inst = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->ops = &vdec_vb2_ops;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->drv_priv = inst;
	src_vq->buf_struct_size = sizeof(struct venus_buffer);
	src_vq->allow_zero_bytesused = 1;
	src_vq->min_buffers_needed = 0;
	src_vq->dev = inst->core->dev;
	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->ops = &vdec_vb2_ops;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->drv_priv = inst;
	dst_vq->buf_struct_size = sizeof(struct venus_buffer);
	dst_vq->allow_zero_bytesused = 1;
	dst_vq->min_buffers_needed = 0;
	dst_vq->dev = inst->core->dev;
	return vb2_queue_init(dst_vq);
}

static int vdec_open(struct file *file)
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
	inst->session_type = VIDC_SESSION_TYPE_DEC;
	inst->num_output_bufs = 1;
	inst->codec_state = VENUS_DEC_STATE_DEINIT;
	inst->buf_count = 0;
	inst->clk_data.core_id = VIDC_CORE_ID_DEFAULT;
	inst->core_acquired = false;
	inst->bit_depth = VIDC_BITDEPTH_8;
	inst->pic_struct = HFI_INTERLACE_FRAME_PROGRESSIVE;
	init_waitqueue_head(&inst->reconf_wait);
	venus_helper_init_instance(inst);

	ret = vdec_ctrl_init(inst);
	if (ret)
		goto err_free;

	ret = hfi_session_create(inst, &vdec_hfi_ops);
	if (ret)
		goto err_ctrl_deinit;

	vdec_inst_init(inst);

	/*
	 * create m2m device for every instance, the m2m context scheduling
	 * is made by firmware side so we do not need to care about.
	 */
	inst->m2m_dev = v4l2_m2m_init(&vdec_m2m_ops);
	if (IS_ERR(inst->m2m_dev)) {
		ret = PTR_ERR(inst->m2m_dev);
		goto err_session_destroy;
	}

	inst->m2m_ctx = v4l2_m2m_ctx_init(inst->m2m_dev, inst, m2m_queue_init);
	if (IS_ERR(inst->m2m_ctx)) {
		ret = PTR_ERR(inst->m2m_ctx);
		goto err_m2m_release;
	}

	v4l2_fh_init(&inst->fh, core->vdev_dec);

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
	vdec_ctrl_deinit(inst);
err_free:
	kfree(inst);
	return ret;
}

static int vdec_close(struct file *file)
{
	struct venus_inst *inst = to_inst(file);

	vdec_pm_get(inst);

	v4l2_m2m_ctx_release(inst->m2m_ctx);
	v4l2_m2m_release(inst->m2m_dev);
	vdec_ctrl_deinit(inst);
	hfi_session_destroy(inst);
	mutex_destroy(&inst->lock);
	v4l2_fh_del(&inst->fh);
	v4l2_fh_exit(&inst->fh);

	vdec_pm_put(inst, false);

	kfree(inst);
	return 0;
}

static const struct v4l2_file_operations vdec_fops = {
	.owner = THIS_MODULE,
	.open = vdec_open,
	.release = vdec_close,
	.unlocked_ioctl = video_ioctl2,
	.poll = v4l2_m2m_fop_poll,
	.mmap = v4l2_m2m_fop_mmap,
};

static int vdec_probe(struct platform_device *pdev)
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

	if (core->pm_ops->vdec_get) {
		ret = core->pm_ops->vdec_get(dev);
		if (ret)
			return ret;
	}

	vdev = video_device_alloc();
	if (!vdev)
		return -ENOMEM;

	strscpy(vdev->name, "qcom-venus-decoder", sizeof(vdev->name));
	vdev->release = video_device_release;
	vdev->fops = &vdec_fops;
	vdev->ioctl_ops = &vdec_ioctl_ops;
	vdev->vfl_dir = VFL_DIR_M2M;
	vdev->v4l2_dev = &core->v4l2_dev;
	vdev->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;

	ret = video_register_device(vdev, VFL_TYPE_VIDEO, -1);
	if (ret)
		goto err_vdev_release;

	core->vdev_dec = vdev;
	core->dev_dec = dev;

	video_set_drvdata(vdev, core);
	pm_runtime_set_autosuspend_delay(dev, 2000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_enable(dev);

	return 0;

err_vdev_release:
	video_device_release(vdev);
	return ret;
}

static int vdec_remove(struct platform_device *pdev)
{
	struct venus_core *core = dev_get_drvdata(pdev->dev.parent);

	video_unregister_device(core->vdev_dec);
	pm_runtime_disable(core->dev_dec);

	if (core->pm_ops->vdec_put)
		core->pm_ops->vdec_put(core->dev_dec);

	return 0;
}

static __maybe_unused int vdec_runtime_suspend(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);
	const struct venus_pm_ops *pm_ops = core->pm_ops;
	int ret = 0;

	if (pm_ops->vdec_power)
		ret = pm_ops->vdec_power(dev, POWER_OFF);

	return ret;
}

static __maybe_unused int vdec_runtime_resume(struct device *dev)
{
	struct venus_core *core = dev_get_drvdata(dev);
	const struct venus_pm_ops *pm_ops = core->pm_ops;
	int ret = 0;

	if (pm_ops->vdec_power)
		ret = pm_ops->vdec_power(dev, POWER_ON);

	return ret;
}

static const struct dev_pm_ops vdec_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(vdec_runtime_suspend, vdec_runtime_resume, NULL)
};

static const struct of_device_id vdec_dt_match[] = {
	{ .compatible = "venus-decoder" },
	{ }
};
MODULE_DEVICE_TABLE(of, vdec_dt_match);

static struct platform_driver qcom_venus_dec_driver = {
	.probe = vdec_probe,
	.remove = vdec_remove,
	.driver = {
		.name = "qcom-venus-decoder",
		.of_match_table = vdec_dt_match,
		.pm = &vdec_pm_ops,
	},
};
module_platform_driver(qcom_venus_dec_driver);

MODULE_ALIAS("platform:qcom-venus-decoder");
MODULE_DESCRIPTION("Qualcomm Venus video decoder driver");
MODULE_LICENSE("GPL v2");
