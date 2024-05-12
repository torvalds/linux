// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2020-2021 NXP
 */

#include <linux/init.h>
#include <linux/interconnect.h>
#include <linux/ioctl.h>
#include <linux/list.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/videodev2.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-mem2mem.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-vmalloc.h>
#include "vpu.h"
#include "vpu_defs.h"
#include "vpu_core.h"
#include "vpu_helpers.h"
#include "vpu_v4l2.h"
#include "vpu_cmds.h"
#include "vpu_rpc.h"

#define VDEC_MIN_BUFFER_CAP		8
#define VDEC_MIN_BUFFER_OUT		8

struct vdec_fs_info {
	char name[8];
	u32 type;
	u32 max_count;
	u32 req_count;
	u32 count;
	u32 index;
	u32 size;
	struct vpu_buffer buffer[32];
	u32 tag;
};

struct vdec_t {
	u32 seq_hdr_found;
	struct vpu_buffer udata;
	struct vpu_decode_params params;
	struct vpu_dec_codec_info codec_info;
	enum vpu_codec_state state;

	struct vpu_vb2_buffer *slots[VB2_MAX_FRAME];
	u32 req_frame_count;
	struct vdec_fs_info mbi;
	struct vdec_fs_info dcp;
	u32 seq_tag;

	bool reset_codec;
	bool fixed_fmt;
	u32 decoded_frame_count;
	u32 display_frame_count;
	u32 sequence;
	u32 eos_received;
	bool is_source_changed;
	u32 source_change;
	u32 drain;
	bool aborting;
};

static const struct vpu_format vdec_formats[] = {
	{
		.pixfmt = V4L2_PIX_FMT_NV12M_8L128,
		.mem_planes = 2,
		.comp_planes = 2,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.sibling = V4L2_PIX_FMT_NV12_8L128,
	},
	{
		.pixfmt = V4L2_PIX_FMT_NV12_8L128,
		.mem_planes = 1,
		.comp_planes = 2,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.sibling = V4L2_PIX_FMT_NV12M_8L128,
	},
	{
		.pixfmt = V4L2_PIX_FMT_NV12M_10BE_8L128,
		.mem_planes = 2,
		.comp_planes = 2,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.sibling = V4L2_PIX_FMT_NV12_10BE_8L128,
	},
	{
		.pixfmt = V4L2_PIX_FMT_NV12_10BE_8L128,
		.mem_planes = 1,
		.comp_planes = 2,
		.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE,
		.sibling = V4L2_PIX_FMT_NV12M_10BE_8L128
	},
	{
		.pixfmt = V4L2_PIX_FMT_H264,
		.mem_planes = 1,
		.comp_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION | V4L2_FMT_FLAG_COMPRESSED
	},
	{
		.pixfmt = V4L2_PIX_FMT_H264_MVC,
		.mem_planes = 1,
		.comp_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION | V4L2_FMT_FLAG_COMPRESSED
	},
	{
		.pixfmt = V4L2_PIX_FMT_HEVC,
		.mem_planes = 1,
		.comp_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION | V4L2_FMT_FLAG_COMPRESSED
	},
	{
		.pixfmt = V4L2_PIX_FMT_VC1_ANNEX_G,
		.mem_planes = 1,
		.comp_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION | V4L2_FMT_FLAG_COMPRESSED
	},
	{
		.pixfmt = V4L2_PIX_FMT_VC1_ANNEX_L,
		.mem_planes = 1,
		.comp_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_COMPRESSED
	},
	{
		.pixfmt = V4L2_PIX_FMT_MPEG2,
		.mem_planes = 1,
		.comp_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION | V4L2_FMT_FLAG_COMPRESSED
	},
	{
		.pixfmt = V4L2_PIX_FMT_MPEG4,
		.mem_planes = 1,
		.comp_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION | V4L2_FMT_FLAG_COMPRESSED
	},
	{
		.pixfmt = V4L2_PIX_FMT_XVID,
		.mem_planes = 1,
		.comp_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION | V4L2_FMT_FLAG_COMPRESSED
	},
	{
		.pixfmt = V4L2_PIX_FMT_VP8,
		.mem_planes = 1,
		.comp_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION | V4L2_FMT_FLAG_COMPRESSED
	},
	{
		.pixfmt = V4L2_PIX_FMT_H263,
		.mem_planes = 1,
		.comp_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION | V4L2_FMT_FLAG_COMPRESSED
	},
	{
		.pixfmt = V4L2_PIX_FMT_SPK,
		.mem_planes = 1,
		.comp_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION | V4L2_FMT_FLAG_COMPRESSED
	},
	{
		.pixfmt = V4L2_PIX_FMT_RV30,
		.mem_planes = 1,
		.comp_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION | V4L2_FMT_FLAG_COMPRESSED
	},
	{
		.pixfmt = V4L2_PIX_FMT_RV40,
		.mem_planes = 1,
		.comp_planes = 1,
		.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE,
		.flags = V4L2_FMT_FLAG_DYN_RESOLUTION | V4L2_FMT_FLAG_COMPRESSED
	},
	{0, 0, 0, 0},
};

static int vdec_op_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vpu_inst *inst = ctrl_to_inst(ctrl);
	struct vdec_t *vdec = inst->priv;
	int ret = 0;

	vpu_inst_lock(inst);
	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY_ENABLE:
		vdec->params.display_delay_enable = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY:
		vdec->params.display_delay = ctrl->val;
		break;
	default:
		ret = -EINVAL;
		break;
	}
	vpu_inst_unlock(inst);

	return ret;
}

static const struct v4l2_ctrl_ops vdec_ctrl_ops = {
	.s_ctrl = vdec_op_s_ctrl,
	.g_volatile_ctrl = vpu_helper_g_volatile_ctrl,
};

static int vdec_ctrl_init(struct vpu_inst *inst)
{
	struct v4l2_ctrl *ctrl;
	int ret;

	ret = v4l2_ctrl_handler_init(&inst->ctrl_handler, 20);
	if (ret)
		return ret;

	v4l2_ctrl_new_std(&inst->ctrl_handler, &vdec_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY,
			  0, 0, 1, 0);

	v4l2_ctrl_new_std(&inst->ctrl_handler, &vdec_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY_ENABLE,
			  0, 1, 1, 0);

	ctrl = v4l2_ctrl_new_std(&inst->ctrl_handler, &vdec_ctrl_ops,
				 V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 1, 32, 1, 2);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	ctrl = v4l2_ctrl_new_std(&inst->ctrl_handler, &vdec_ctrl_ops,
				 V4L2_CID_MIN_BUFFERS_FOR_OUTPUT, 1, 32, 1, 2);
	if (ctrl)
		ctrl->flags |= V4L2_CTRL_FLAG_VOLATILE;

	if (inst->ctrl_handler.error) {
		ret = inst->ctrl_handler.error;
		v4l2_ctrl_handler_free(&inst->ctrl_handler);
		return ret;
	}

	ret = v4l2_ctrl_handler_setup(&inst->ctrl_handler);
	if (ret) {
		dev_err(inst->dev, "[%d] setup ctrls fail, ret = %d\n", inst->id, ret);
		v4l2_ctrl_handler_free(&inst->ctrl_handler);
		return ret;
	}

	return 0;
}

static void vdec_handle_resolution_change(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	struct vb2_queue *q;

	if (!inst->fh.m2m_ctx)
		return;

	if (inst->state != VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE)
		return;
	if (!vdec->source_change)
		return;

	q = v4l2_m2m_get_dst_vq(inst->fh.m2m_ctx);
	if (!list_empty(&q->done_list))
		return;

	vdec->source_change--;
	vpu_notify_source_change(inst);
}

static int vdec_update_state(struct vpu_inst *inst, enum vpu_codec_state state, u32 force)
{
	struct vdec_t *vdec = inst->priv;
	enum vpu_codec_state pre_state = inst->state;

	if (state == VPU_CODEC_STATE_SEEK) {
		if (inst->state == VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE)
			vdec->state = inst->state;
		else
			vdec->state = VPU_CODEC_STATE_ACTIVE;
	}
	if (inst->state != VPU_CODEC_STATE_SEEK || force)
		inst->state = state;
	else if (state == VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE)
		vdec->state = VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE;

	if (inst->state != pre_state)
		vpu_trace(inst->dev, "[%d] %d -> %d\n", inst->id, pre_state, inst->state);

	if (inst->state == VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE)
		vdec_handle_resolution_change(inst);

	return 0;
}

static void vdec_set_last_buffer_dequeued(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	if (inst->state == VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE)
		return;

	if (vdec->eos_received) {
		if (!vpu_set_last_buffer_dequeued(inst)) {
			vdec->eos_received--;
			vdec_update_state(inst, VPU_CODEC_STATE_DRAIN, 0);
		}
	}
}

static int vdec_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	strscpy(cap->driver, "amphion-vpu", sizeof(cap->driver));
	strscpy(cap->card, "amphion vpu decoder", sizeof(cap->card));
	strscpy(cap->bus_info, "platform: amphion-vpu", sizeof(cap->bus_info));

	return 0;
}

static int vdec_enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct vpu_inst *inst = to_inst(file);
	struct vdec_t *vdec = inst->priv;
	const struct vpu_format *fmt;
	int ret = -EINVAL;

	vpu_inst_lock(inst);
	if (V4L2_TYPE_IS_CAPTURE(f->type) && vdec->fixed_fmt) {
		fmt = vpu_get_format(inst, f->type);
		if (f->index == 1)
			fmt = vpu_helper_find_sibling(inst, f->type, fmt->pixfmt);
		if (f->index > 1)
			fmt = NULL;
	} else {
		fmt = vpu_helper_enum_format(inst, f->type, f->index);
	}
	if (!fmt)
		goto exit;

	memset(f->reserved, 0, sizeof(f->reserved));
	f->pixelformat = fmt->pixfmt;
	f->flags = fmt->flags;
	ret = 0;
exit:
	vpu_inst_unlock(inst);
	return ret;
}

static int vdec_g_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_inst *inst = to_inst(file);
	struct vdec_t *vdec = inst->priv;
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	struct vpu_format *cur_fmt;
	int i;

	vpu_inst_lock(inst);
	cur_fmt = vpu_get_format(inst, f->type);

	pixmp->pixelformat = cur_fmt->pixfmt;
	pixmp->num_planes = cur_fmt->mem_planes;
	pixmp->width = cur_fmt->width;
	pixmp->height = cur_fmt->height;
	pixmp->field = cur_fmt->field;
	pixmp->flags = cur_fmt->flags;
	for (i = 0; i < pixmp->num_planes; i++) {
		pixmp->plane_fmt[i].bytesperline = cur_fmt->bytesperline[i];
		pixmp->plane_fmt[i].sizeimage = vpu_get_fmt_plane_size(cur_fmt, i);
	}

	f->fmt.pix_mp.colorspace = vdec->codec_info.color_primaries;
	f->fmt.pix_mp.xfer_func = vdec->codec_info.transfer_chars;
	f->fmt.pix_mp.ycbcr_enc = vdec->codec_info.matrix_coeffs;
	f->fmt.pix_mp.quantization = vdec->codec_info.full_range;
	vpu_inst_unlock(inst);

	return 0;
}

static int vdec_try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_inst *inst = to_inst(file);
	struct vdec_t *vdec = inst->priv;
	struct vpu_format fmt;

	vpu_inst_lock(inst);
	if (V4L2_TYPE_IS_CAPTURE(f->type) && vdec->fixed_fmt) {
		struct vpu_format *cap_fmt = vpu_get_format(inst, f->type);

		if (!vpu_helper_match_format(inst, cap_fmt->type, cap_fmt->pixfmt,
					     f->fmt.pix_mp.pixelformat))
			f->fmt.pix_mp.pixelformat = cap_fmt->pixfmt;
	}

	vpu_try_fmt_common(inst, f, &fmt);

	if (vdec->fixed_fmt) {
		f->fmt.pix_mp.colorspace = vdec->codec_info.color_primaries;
		f->fmt.pix_mp.xfer_func = vdec->codec_info.transfer_chars;
		f->fmt.pix_mp.ycbcr_enc = vdec->codec_info.matrix_coeffs;
		f->fmt.pix_mp.quantization = vdec->codec_info.full_range;
	} else {
		f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_DEFAULT;
		f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
		f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
		f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	}
	vpu_inst_unlock(inst);

	return 0;
}

static int vdec_s_fmt_common(struct vpu_inst *inst, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	struct vpu_format fmt;
	struct vpu_format *cur_fmt;
	struct vb2_queue *q;
	struct vdec_t *vdec = inst->priv;
	int i;

	if (!inst->fh.m2m_ctx)
		return -EINVAL;

	q = v4l2_m2m_get_vq(inst->fh.m2m_ctx, f->type);
	if (!q)
		return -EINVAL;
	if (vb2_is_busy(q))
		return -EBUSY;

	if (vpu_try_fmt_common(inst, f, &fmt))
		return -EINVAL;

	cur_fmt = vpu_get_format(inst, f->type);
	if (V4L2_TYPE_IS_OUTPUT(f->type) && inst->state != VPU_CODEC_STATE_DEINIT) {
		if (cur_fmt->pixfmt != fmt.pixfmt) {
			vdec->reset_codec = true;
			vdec->fixed_fmt = false;
		}
	}
	if (V4L2_TYPE_IS_OUTPUT(f->type) || !vdec->fixed_fmt) {
		memcpy(cur_fmt, &fmt, sizeof(*cur_fmt));
	} else {
		if (vpu_helper_match_format(inst, f->type, cur_fmt->pixfmt, pixmp->pixelformat)) {
			cur_fmt->pixfmt = fmt.pixfmt;
			cur_fmt->mem_planes = fmt.mem_planes;
		}
		pixmp->pixelformat = cur_fmt->pixfmt;
		pixmp->num_planes = cur_fmt->mem_planes;
		pixmp->width = cur_fmt->width;
		pixmp->height = cur_fmt->height;
		for (i = 0; i < pixmp->num_planes; i++) {
			pixmp->plane_fmt[i].bytesperline = cur_fmt->bytesperline[i];
			pixmp->plane_fmt[i].sizeimage = vpu_get_fmt_plane_size(cur_fmt, i);
		}
		pixmp->field = cur_fmt->field;
	}

	if (!vdec->fixed_fmt) {
		if (V4L2_TYPE_IS_OUTPUT(f->type)) {
			vdec->params.codec_format = cur_fmt->pixfmt;
			vdec->codec_info.color_primaries = f->fmt.pix_mp.colorspace;
			vdec->codec_info.transfer_chars = f->fmt.pix_mp.xfer_func;
			vdec->codec_info.matrix_coeffs = f->fmt.pix_mp.ycbcr_enc;
			vdec->codec_info.full_range = f->fmt.pix_mp.quantization;
		} else {
			vdec->params.output_format = cur_fmt->pixfmt;
			inst->crop.left = 0;
			inst->crop.top = 0;
			inst->crop.width = cur_fmt->width;
			inst->crop.height = cur_fmt->height;
		}
	}

	vpu_trace(inst->dev, "[%d] %c%c%c%c %dx%d\n", inst->id,
		  f->fmt.pix_mp.pixelformat,
		  f->fmt.pix_mp.pixelformat >> 8,
		  f->fmt.pix_mp.pixelformat >> 16,
		  f->fmt.pix_mp.pixelformat >> 24,
		  f->fmt.pix_mp.width,
		  f->fmt.pix_mp.height);

	return 0;
}

static int vdec_s_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_inst *inst = to_inst(file);
	struct v4l2_pix_format_mplane *pixmp = &f->fmt.pix_mp;
	struct vdec_t *vdec = inst->priv;
	int ret = 0;

	vpu_inst_lock(inst);
	ret = vdec_s_fmt_common(inst, f);
	if (ret)
		goto exit;

	if (V4L2_TYPE_IS_OUTPUT(f->type) && !vdec->fixed_fmt) {
		struct v4l2_format fc;

		memset(&fc, 0, sizeof(fc));
		fc.type = inst->cap_format.type;
		fc.fmt.pix_mp.pixelformat = inst->cap_format.pixfmt;
		fc.fmt.pix_mp.width = pixmp->width;
		fc.fmt.pix_mp.height = pixmp->height;
		vdec_s_fmt_common(inst, &fc);
	}

	f->fmt.pix_mp.colorspace = vdec->codec_info.color_primaries;
	f->fmt.pix_mp.xfer_func = vdec->codec_info.transfer_chars;
	f->fmt.pix_mp.ycbcr_enc = vdec->codec_info.matrix_coeffs;
	f->fmt.pix_mp.quantization = vdec->codec_info.full_range;

exit:
	vpu_inst_unlock(inst);
	return ret;
}

static int vdec_g_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct vpu_inst *inst = to_inst(file);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE && s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		return -EINVAL;

	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		s->r = inst->crop;
		break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = inst->cap_format.width;
		s->r.height = inst->cap_format.height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int vdec_drain(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	if (!inst->fh.m2m_ctx)
		return 0;

	if (!vdec->drain)
		return 0;

	if (!vpu_is_source_empty(inst))
		return 0;

	if (!vdec->params.frame_count) {
		vpu_set_last_buffer_dequeued(inst);
		return 0;
	}

	vpu_iface_add_scode(inst, SCODE_PADDING_EOS);
	vdec->params.end_flag = 1;
	vpu_iface_set_decode_params(inst, &vdec->params, 1);
	vdec->drain = 0;
	vpu_trace(inst->dev, "[%d] frame_count = %d\n", inst->id, vdec->params.frame_count);

	return 0;
}

static int vdec_cmd_start(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	switch (inst->state) {
	case VPU_CODEC_STATE_STARTED:
	case VPU_CODEC_STATE_DRAIN:
	case VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE:
		vdec_update_state(inst, VPU_CODEC_STATE_ACTIVE, 0);
		break;
	default:
		break;
	}
	vpu_process_capture_buffer(inst);
	if (vdec->eos_received)
		vdec_set_last_buffer_dequeued(inst);
	return 0;
}

static int vdec_cmd_stop(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	vpu_trace(inst->dev, "[%d]\n", inst->id);

	if (inst->state == VPU_CODEC_STATE_DEINIT) {
		vpu_set_last_buffer_dequeued(inst);
	} else {
		vdec->drain = 1;
		vdec_drain(inst);
	}

	return 0;
}

static int vdec_decoder_cmd(struct file *file, void *fh, struct v4l2_decoder_cmd *cmd)
{
	struct vpu_inst *inst = to_inst(file);
	int ret;

	ret = v4l2_m2m_ioctl_try_decoder_cmd(file, fh, cmd);
	if (ret)
		return ret;

	vpu_inst_lock(inst);
	switch (cmd->cmd) {
	case V4L2_DEC_CMD_START:
		vdec_cmd_start(inst);
		break;
	case V4L2_DEC_CMD_STOP:
		vdec_cmd_stop(inst);
		break;
	default:
		break;
	}
	vpu_inst_unlock(inst);

	return 0;
}

static int vdec_subscribe_event(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub)
{
	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	case V4L2_EVENT_SOURCE_CHANGE:
		return v4l2_src_change_event_subscribe(fh, sub);
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ioctl_ops vdec_ioctl_ops = {
	.vidioc_querycap               = vdec_querycap,
	.vidioc_enum_fmt_vid_cap       = vdec_enum_fmt,
	.vidioc_enum_fmt_vid_out       = vdec_enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane   = vdec_g_fmt,
	.vidioc_g_fmt_vid_out_mplane   = vdec_g_fmt,
	.vidioc_try_fmt_vid_cap_mplane = vdec_try_fmt,
	.vidioc_try_fmt_vid_out_mplane = vdec_try_fmt,
	.vidioc_s_fmt_vid_cap_mplane   = vdec_s_fmt,
	.vidioc_s_fmt_vid_out_mplane   = vdec_s_fmt,
	.vidioc_g_selection            = vdec_g_selection,
	.vidioc_try_decoder_cmd        = v4l2_m2m_ioctl_try_decoder_cmd,
	.vidioc_decoder_cmd            = vdec_decoder_cmd,
	.vidioc_subscribe_event        = vdec_subscribe_event,
	.vidioc_unsubscribe_event      = v4l2_event_unsubscribe,
	.vidioc_reqbufs                = v4l2_m2m_ioctl_reqbufs,
	.vidioc_create_bufs	       = v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf	       = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_querybuf               = v4l2_m2m_ioctl_querybuf,
	.vidioc_qbuf                   = v4l2_m2m_ioctl_qbuf,
	.vidioc_expbuf                 = v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf                  = v4l2_m2m_ioctl_dqbuf,
	.vidioc_streamon               = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff              = v4l2_m2m_ioctl_streamoff,
};

static bool vdec_check_ready(struct vpu_inst *inst, unsigned int type)
{
	struct vdec_t *vdec = inst->priv;

	if (V4L2_TYPE_IS_OUTPUT(type))
		return true;

	if (vdec->req_frame_count)
		return true;

	return false;
}

static struct vb2_v4l2_buffer *vdec_get_src_buffer(struct vpu_inst *inst, u32 count)
{
	if (count > 1)
		vpu_skip_frame(inst, count - 1);

	return vpu_next_src_buf(inst);
}

static int vdec_frame_decoded(struct vpu_inst *inst, void *arg)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_dec_pic_info *info = arg;
	struct vpu_vb2_buffer *vpu_buf;
	struct vb2_v4l2_buffer *vbuf;
	struct vb2_v4l2_buffer *src_buf;
	int ret = 0;

	if (!info || info->id >= ARRAY_SIZE(vdec->slots))
		return -EINVAL;

	vpu_inst_lock(inst);
	vpu_buf = vdec->slots[info->id];
	if (!vpu_buf) {
		dev_err(inst->dev, "[%d] decoded invalid frame[%d]\n", inst->id, info->id);
		ret = -EINVAL;
		goto exit;
	}
	vbuf = &vpu_buf->m2m_buf.vb;
	src_buf = vdec_get_src_buffer(inst, info->consumed_count);
	if (src_buf) {
		v4l2_m2m_buf_copy_metadata(src_buf, vbuf, true);
		if (info->consumed_count) {
			v4l2_m2m_src_buf_remove(inst->fh.m2m_ctx);
			vpu_set_buffer_state(src_buf, VPU_BUF_STATE_IDLE);
			v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
		} else {
			vpu_set_buffer_state(src_buf, VPU_BUF_STATE_DECODED);
		}
	}
	if (vpu_get_buffer_state(vbuf) == VPU_BUF_STATE_DECODED)
		dev_info(inst->dev, "[%d] buf[%d] has been decoded\n", inst->id, info->id);
	vpu_set_buffer_state(vbuf, VPU_BUF_STATE_DECODED);
	vdec->decoded_frame_count++;
exit:
	vpu_inst_unlock(inst);

	return ret;
}

static struct vpu_vb2_buffer *vdec_find_buffer(struct vpu_inst *inst, u32 luma)
{
	struct vdec_t *vdec = inst->priv;
	int i;

	for (i = 0; i < ARRAY_SIZE(vdec->slots); i++) {
		if (!vdec->slots[i])
			continue;
		if (luma == vdec->slots[i]->luma)
			return vdec->slots[i];
	}

	return NULL;
}

static void vdec_buf_done(struct vpu_inst *inst, struct vpu_frame_info *frame)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_format *cur_fmt;
	struct vpu_vb2_buffer *vpu_buf;
	struct vb2_v4l2_buffer *vbuf;
	u32 sequence;
	int i;

	if (!frame)
		return;

	vpu_inst_lock(inst);
	sequence = vdec->sequence++;
	vpu_buf = vdec_find_buffer(inst, frame->luma);
	vpu_inst_unlock(inst);
	if (!vpu_buf) {
		dev_err(inst->dev, "[%d] can't find buffer, id = %d, addr = 0x%x\n",
			inst->id, frame->id, frame->luma);
		return;
	}
	if (frame->skipped) {
		dev_dbg(inst->dev, "[%d] frame skip\n", inst->id);
		return;
	}

	cur_fmt = vpu_get_format(inst, inst->cap_format.type);
	vbuf = &vpu_buf->m2m_buf.vb;
	if (vbuf->vb2_buf.index != frame->id)
		dev_err(inst->dev, "[%d] buffer id(%d, %d) dismatch\n",
			inst->id, vbuf->vb2_buf.index, frame->id);

	if (vpu_get_buffer_state(vbuf) != VPU_BUF_STATE_DECODED)
		dev_err(inst->dev, "[%d] buffer(%d) ready without decoded\n", inst->id, frame->id);
	vpu_set_buffer_state(vbuf, VPU_BUF_STATE_READY);
	for (i = 0; i < vbuf->vb2_buf.num_planes; i++)
		vb2_set_plane_payload(&vbuf->vb2_buf, i, vpu_get_fmt_plane_size(cur_fmt, i));
	vbuf->field = cur_fmt->field;
	vbuf->sequence = sequence;
	dev_dbg(inst->dev, "[%d][OUTPUT TS]%32lld\n", inst->id, vbuf->vb2_buf.timestamp);

	v4l2_m2m_buf_done(vbuf, VB2_BUF_STATE_DONE);
	vpu_inst_lock(inst);
	vdec->display_frame_count++;
	vpu_inst_unlock(inst);
	dev_dbg(inst->dev, "[%d] decoded : %d, display : %d, sequence : %d\n",
		inst->id, vdec->decoded_frame_count, vdec->display_frame_count, vdec->sequence);
}

static void vdec_stop_done(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	vpu_inst_lock(inst);
	vdec_update_state(inst, VPU_CODEC_STATE_DEINIT, 0);
	vdec->seq_hdr_found = 0;
	vdec->req_frame_count = 0;
	vdec->reset_codec = false;
	vdec->fixed_fmt = false;
	vdec->params.end_flag = 0;
	vdec->drain = 0;
	vdec->params.frame_count = 0;
	vdec->decoded_frame_count = 0;
	vdec->display_frame_count = 0;
	vdec->sequence = 0;
	vdec->eos_received = 0;
	vdec->is_source_changed = false;
	vdec->source_change = 0;
	inst->total_input_count = 0;
	vpu_inst_unlock(inst);
}

static bool vdec_check_source_change(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	const struct vpu_format *sibling;

	if (!inst->fh.m2m_ctx)
		return false;

	if (vdec->reset_codec)
		return false;

	sibling = vpu_helper_find_sibling(inst, inst->cap_format.type, inst->cap_format.pixfmt);
	if (sibling && vdec->codec_info.pixfmt == sibling->pixfmt)
		vdec->codec_info.pixfmt = inst->cap_format.pixfmt;

	if (!vb2_is_streaming(v4l2_m2m_get_dst_vq(inst->fh.m2m_ctx)))
		return true;
	if (inst->cap_format.pixfmt != vdec->codec_info.pixfmt)
		return true;
	if (inst->cap_format.width != vdec->codec_info.decoded_width)
		return true;
	if (inst->cap_format.height != vdec->codec_info.decoded_height)
		return true;
	if (vpu_get_num_buffers(inst, inst->cap_format.type) < inst->min_buffer_cap)
		return true;
	if (inst->crop.left != vdec->codec_info.offset_x)
		return true;
	if (inst->crop.top != vdec->codec_info.offset_y)
		return true;
	if (inst->crop.width != vdec->codec_info.width)
		return true;
	if (inst->crop.height != vdec->codec_info.height)
		return true;

	return false;
}

static void vdec_init_fmt(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	struct v4l2_format f;

	memset(&f, 0, sizeof(f));
	f.type = inst->cap_format.type;
	f.fmt.pix_mp.pixelformat = vdec->codec_info.pixfmt;
	f.fmt.pix_mp.width = vdec->codec_info.decoded_width;
	f.fmt.pix_mp.height = vdec->codec_info.decoded_height;
	if (vdec->codec_info.progressive)
		f.fmt.pix_mp.field = V4L2_FIELD_NONE;
	else
		f.fmt.pix_mp.field = V4L2_FIELD_SEQ_TB;
	vpu_try_fmt_common(inst, &f, &inst->cap_format);

	inst->out_format.width = vdec->codec_info.width;
	inst->out_format.height = vdec->codec_info.height;
}

static void vdec_init_crop(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	inst->crop.left = vdec->codec_info.offset_x;
	inst->crop.top = vdec->codec_info.offset_y;
	inst->crop.width = vdec->codec_info.width;
	inst->crop.height = vdec->codec_info.height;
}

static void vdec_init_mbi(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	vdec->mbi.size = vdec->codec_info.mbi_size;
	vdec->mbi.max_count = ARRAY_SIZE(vdec->mbi.buffer);
	scnprintf(vdec->mbi.name, sizeof(vdec->mbi.name), "mbi");
	vdec->mbi.type = MEM_RES_MBI;
	vdec->mbi.tag = vdec->seq_tag;
}

static void vdec_init_dcp(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	vdec->dcp.size = vdec->codec_info.dcp_size;
	vdec->dcp.max_count = ARRAY_SIZE(vdec->dcp.buffer);
	scnprintf(vdec->dcp.name, sizeof(vdec->dcp.name), "dcp");
	vdec->dcp.type = MEM_RES_DCP;
	vdec->dcp.tag = vdec->seq_tag;
}

static void vdec_request_one_fs(struct vdec_fs_info *fs)
{
	fs->req_count++;
	if (fs->req_count > fs->max_count)
		fs->req_count = fs->max_count;
}

static int vdec_alloc_fs_buffer(struct vpu_inst *inst, struct vdec_fs_info *fs)
{
	struct vpu_buffer *buffer;

	if (!fs->size)
		return -EINVAL;

	if (fs->count >= fs->req_count)
		return -EINVAL;

	buffer = &fs->buffer[fs->count];
	if (buffer->virt && buffer->length >= fs->size)
		return 0;

	vpu_free_dma(buffer);
	buffer->length = fs->size;
	return vpu_alloc_dma(inst->core, buffer);
}

static void vdec_alloc_fs(struct vpu_inst *inst, struct vdec_fs_info *fs)
{
	int ret;

	while (fs->count < fs->req_count) {
		ret = vdec_alloc_fs_buffer(inst, fs);
		if (ret)
			break;
		fs->count++;
	}
}

static void vdec_clear_fs(struct vdec_fs_info *fs)
{
	u32 i;

	if (!fs)
		return;

	for (i = 0; i < ARRAY_SIZE(fs->buffer); i++)
		vpu_free_dma(&fs->buffer[i]);
	memset(fs, 0, sizeof(*fs));
}

static int vdec_response_fs(struct vpu_inst *inst, struct vdec_fs_info *fs)
{
	struct vpu_fs_info info;
	int ret;

	if (fs->index >= fs->count)
		return 0;

	memset(&info, 0, sizeof(info));
	info.id = fs->index;
	info.type = fs->type;
	info.tag = fs->tag;
	info.luma_addr = fs->buffer[fs->index].phys;
	info.luma_size = fs->buffer[fs->index].length;
	ret = vpu_session_alloc_fs(inst, &info);
	if (ret)
		return ret;

	fs->index++;
	return 0;
}

static int vdec_response_frame_abnormal(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_fs_info info;

	if (!vdec->req_frame_count)
		return 0;

	memset(&info, 0, sizeof(info));
	info.type = MEM_RES_FRAME;
	info.tag = vdec->seq_tag + 0xf0;
	vpu_session_alloc_fs(inst, &info);
	vdec->req_frame_count--;

	return 0;
}

static int vdec_response_frame(struct vpu_inst *inst, struct vb2_v4l2_buffer *vbuf)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_vb2_buffer *vpu_buf;
	struct vpu_fs_info info;
	int ret;

	if (inst->state != VPU_CODEC_STATE_ACTIVE)
		return -EINVAL;

	if (vdec->aborting)
		return -EINVAL;

	if (!vdec->req_frame_count)
		return -EINVAL;

	if (!vbuf)
		return -EINVAL;

	if (vdec->slots[vbuf->vb2_buf.index]) {
		dev_err(inst->dev, "[%d] repeat alloc fs %d\n",
			inst->id, vbuf->vb2_buf.index);
		return -EINVAL;
	}

	dev_dbg(inst->dev, "[%d] state = %d, alloc fs %d, tag = 0x%x\n",
		inst->id, inst->state, vbuf->vb2_buf.index, vdec->seq_tag);
	vpu_buf = to_vpu_vb2_buffer(vbuf);

	memset(&info, 0, sizeof(info));
	info.id = vbuf->vb2_buf.index;
	info.type = MEM_RES_FRAME;
	info.tag = vdec->seq_tag;
	info.luma_addr = vpu_get_vb_phy_addr(&vbuf->vb2_buf, 0);
	info.luma_size = inst->cap_format.sizeimage[0];
	if (vbuf->vb2_buf.num_planes > 1)
		info.chroma_addr = vpu_get_vb_phy_addr(&vbuf->vb2_buf, 1);
	else
		info.chroma_addr = info.luma_addr + info.luma_size;
	info.chromau_size = inst->cap_format.sizeimage[1];
	info.bytesperline = inst->cap_format.bytesperline[0];
	ret = vpu_session_alloc_fs(inst, &info);
	if (ret)
		return ret;

	vpu_buf->tag = info.tag;
	vpu_buf->luma = info.luma_addr;
	vpu_buf->chroma_u = info.chroma_addr;
	vpu_buf->chroma_v = 0;
	vpu_set_buffer_state(vbuf, VPU_BUF_STATE_INUSE);
	vdec->slots[info.id] = vpu_buf;
	vdec->req_frame_count--;

	return 0;
}

static void vdec_response_fs_request(struct vpu_inst *inst, bool force)
{
	struct vdec_t *vdec = inst->priv;
	int i;
	int ret;

	if (force) {
		for (i = vdec->req_frame_count; i > 0; i--)
			vdec_response_frame_abnormal(inst);
		return;
	}

	for (i = vdec->req_frame_count; i > 0; i--) {
		ret = vpu_process_capture_buffer(inst);
		if (ret)
			break;
		if (vdec->eos_received)
			break;
	}

	for (i = vdec->mbi.index; i < vdec->mbi.count; i++) {
		if (vdec_response_fs(inst, &vdec->mbi))
			break;
		if (vdec->eos_received)
			break;
	}
	for (i = vdec->dcp.index; i < vdec->dcp.count; i++) {
		if (vdec_response_fs(inst, &vdec->dcp))
			break;
		if (vdec->eos_received)
			break;
	}
}

static void vdec_response_fs_release(struct vpu_inst *inst, u32 id, u32 tag)
{
	struct vpu_fs_info info;

	memset(&info, 0, sizeof(info));
	info.id = id;
	info.tag = tag;
	vpu_session_release_fs(inst, &info);
}

static void vdec_recycle_buffer(struct vpu_inst *inst, struct vb2_v4l2_buffer *vbuf)
{
	if (!inst->fh.m2m_ctx)
		return;
	if (vbuf->vb2_buf.state != VB2_BUF_STATE_ACTIVE)
		return;
	if (vpu_find_buf_by_idx(inst, vbuf->vb2_buf.type, vbuf->vb2_buf.index))
		return;
	v4l2_m2m_buf_queue(inst->fh.m2m_ctx, vbuf);
}

static void vdec_clear_slots(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_vb2_buffer *vpu_buf;
	struct vb2_v4l2_buffer *vbuf;
	int i;

	for (i = 0; i < ARRAY_SIZE(vdec->slots); i++) {
		if (!vdec->slots[i])
			continue;

		vpu_buf = vdec->slots[i];
		vbuf = &vpu_buf->m2m_buf.vb;

		vpu_trace(inst->dev, "clear slot %d\n", i);
		vdec_response_fs_release(inst, i, vpu_buf->tag);
		vdec_recycle_buffer(inst, vbuf);
		vdec->slots[i]->state = VPU_BUF_STATE_IDLE;
		vdec->slots[i] = NULL;
	}
}

static void vdec_event_seq_hdr(struct vpu_inst *inst, struct vpu_dec_codec_info *hdr)
{
	struct vdec_t *vdec = inst->priv;

	vpu_inst_lock(inst);
	memcpy(&vdec->codec_info, hdr, sizeof(vdec->codec_info));

	vpu_trace(inst->dev, "[%d] %d x %d, crop : (%d, %d) %d x %d, %d, %d\n",
		  inst->id,
		  vdec->codec_info.decoded_width,
		  vdec->codec_info.decoded_height,
		  vdec->codec_info.offset_x,
		  vdec->codec_info.offset_y,
		  vdec->codec_info.width,
		  vdec->codec_info.height,
		  hdr->num_ref_frms,
		  hdr->num_dpb_frms);
	inst->min_buffer_cap = hdr->num_ref_frms + hdr->num_dpb_frms;
	vdec->is_source_changed = vdec_check_source_change(inst);
	vdec_init_fmt(inst);
	vdec_init_crop(inst);
	vdec_init_mbi(inst);
	vdec_init_dcp(inst);
	if (!vdec->seq_hdr_found) {
		vdec->seq_tag = vdec->codec_info.tag;
		if (vdec->is_source_changed) {
			vdec_update_state(inst, VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE, 0);
			vdec->source_change++;
			vdec_handle_resolution_change(inst);
			vdec->is_source_changed = false;
		}
	}
	if (vdec->seq_tag != vdec->codec_info.tag) {
		vdec_response_fs_request(inst, true);
		vpu_trace(inst->dev, "[%d] seq tag change: %d -> %d\n",
			  inst->id, vdec->seq_tag, vdec->codec_info.tag);
	}
	vdec->seq_hdr_found++;
	vdec->fixed_fmt = true;
	vpu_inst_unlock(inst);
}

static void vdec_event_resolution_change(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	vpu_trace(inst->dev, "[%d]\n", inst->id);
	vpu_inst_lock(inst);
	vdec->seq_tag = vdec->codec_info.tag;
	vdec_clear_fs(&vdec->mbi);
	vdec_clear_fs(&vdec->dcp);
	vdec_clear_slots(inst);
	vdec_init_mbi(inst);
	vdec_init_dcp(inst);
	if (vdec->is_source_changed) {
		vdec_update_state(inst, VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE, 0);
		vdec->source_change++;
		vdec_handle_resolution_change(inst);
		vdec->is_source_changed = false;
	}
	vpu_inst_unlock(inst);
}

static void vdec_event_req_fs(struct vpu_inst *inst, struct vpu_fs_info *fs)
{
	struct vdec_t *vdec = inst->priv;

	if (!fs)
		return;

	vpu_inst_lock(inst);

	switch (fs->type) {
	case MEM_RES_FRAME:
		vdec->req_frame_count++;
		break;
	case MEM_RES_MBI:
		vdec_request_one_fs(&vdec->mbi);
		break;
	case MEM_RES_DCP:
		vdec_request_one_fs(&vdec->dcp);
		break;
	default:
		break;
	}

	vdec_alloc_fs(inst, &vdec->mbi);
	vdec_alloc_fs(inst, &vdec->dcp);

	vdec_response_fs_request(inst, false);

	vpu_inst_unlock(inst);
}

static void vdec_evnet_rel_fs(struct vpu_inst *inst, struct vpu_fs_info *fs)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_vb2_buffer *vpu_buf;
	struct vb2_v4l2_buffer *vbuf;

	if (!fs || fs->id >= ARRAY_SIZE(vdec->slots))
		return;
	if (fs->type != MEM_RES_FRAME)
		return;

	if (fs->id >= vpu_get_num_buffers(inst, inst->cap_format.type)) {
		dev_err(inst->dev, "[%d] invalid fs(%d) to release\n", inst->id, fs->id);
		return;
	}

	vpu_inst_lock(inst);
	vpu_buf = vdec->slots[fs->id];
	vdec->slots[fs->id] = NULL;

	if (!vpu_buf) {
		dev_dbg(inst->dev, "[%d] fs[%d] has bee released\n", inst->id, fs->id);
		goto exit;
	}

	vbuf = &vpu_buf->m2m_buf.vb;
	if (vpu_get_buffer_state(vbuf) == VPU_BUF_STATE_DECODED) {
		dev_dbg(inst->dev, "[%d] frame skip\n", inst->id);
		vdec->sequence++;
	}

	vdec_response_fs_release(inst, fs->id, vpu_buf->tag);
	if (vpu_get_buffer_state(vbuf) != VPU_BUF_STATE_READY)
		vdec_recycle_buffer(inst, vbuf);

	vpu_set_buffer_state(vbuf, VPU_BUF_STATE_IDLE);
	vpu_process_capture_buffer(inst);

exit:
	vpu_inst_unlock(inst);
}

static void vdec_event_eos(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;

	vpu_trace(inst->dev, "[%d] input : %d, decoded : %d, display : %d, sequence : %d\n",
		  inst->id,
		  vdec->params.frame_count,
		  vdec->decoded_frame_count,
		  vdec->display_frame_count,
		  vdec->sequence);
	vpu_inst_lock(inst);
	vdec->eos_received++;
	vdec->fixed_fmt = false;
	inst->min_buffer_cap = VDEC_MIN_BUFFER_CAP;
	vdec_set_last_buffer_dequeued(inst);
	vpu_inst_unlock(inst);
}

static void vdec_event_notify(struct vpu_inst *inst, u32 event, void *data)
{
	switch (event) {
	case VPU_MSG_ID_SEQ_HDR_FOUND:
		vdec_event_seq_hdr(inst, data);
		break;
	case VPU_MSG_ID_RES_CHANGE:
		vdec_event_resolution_change(inst);
		break;
	case VPU_MSG_ID_FRAME_REQ:
		vdec_event_req_fs(inst, data);
		break;
	case VPU_MSG_ID_FRAME_RELEASE:
		vdec_evnet_rel_fs(inst, data);
		break;
	case VPU_MSG_ID_PIC_EOS:
		vdec_event_eos(inst);
		break;
	default:
		break;
	}
}

static int vdec_process_output(struct vpu_inst *inst, struct vb2_buffer *vb)
{
	struct vdec_t *vdec = inst->priv;
	struct vb2_v4l2_buffer *vbuf;
	struct vpu_rpc_buffer_desc desc;
	u32 free_space;
	int ret;

	vbuf = to_vb2_v4l2_buffer(vb);
	dev_dbg(inst->dev, "[%d] dec output [%d] %d : %ld\n",
		inst->id, vbuf->sequence, vb->index, vb2_get_plane_payload(vb, 0));

	if (inst->state == VPU_CODEC_STATE_DEINIT)
		return -EINVAL;
	if (vdec->reset_codec)
		return -EINVAL;

	if (inst->state == VPU_CODEC_STATE_STARTED)
		vdec_update_state(inst, VPU_CODEC_STATE_ACTIVE, 0);

	ret = vpu_iface_get_stream_buffer_desc(inst, &desc);
	if (ret)
		return ret;

	free_space = vpu_helper_get_free_space(inst);
	if (free_space < vb2_get_plane_payload(vb, 0) + 0x40000)
		return -ENOMEM;

	vpu_set_buffer_state(vbuf, VPU_BUF_STATE_INUSE);
	ret = vpu_iface_input_frame(inst, vb);
	if (ret < 0)
		return -ENOMEM;

	dev_dbg(inst->dev, "[%d][INPUT  TS]%32lld\n", inst->id, vb->timestamp);
	vdec->params.frame_count++;

	if (vdec->drain)
		vdec_drain(inst);

	return 0;
}

static int vdec_process_capture(struct vpu_inst *inst, struct vb2_buffer *vb)
{
	struct vdec_t *vdec = inst->priv;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	int ret;

	if (inst->state == VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE)
		return -EINVAL;
	if (vdec->reset_codec)
		return -EINVAL;

	ret = vdec_response_frame(inst, vbuf);
	if (ret)
		return ret;
	v4l2_m2m_dst_buf_remove_by_buf(inst->fh.m2m_ctx, vbuf);
	return 0;
}

static void vdec_on_queue_empty(struct vpu_inst *inst, u32 type)
{
	struct vdec_t *vdec = inst->priv;

	if (V4L2_TYPE_IS_OUTPUT(type))
		return;

	vdec_handle_resolution_change(inst);
	if (vdec->eos_received)
		vdec_set_last_buffer_dequeued(inst);
}

static void vdec_abort(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	struct vpu_rpc_buffer_desc desc;
	int ret;

	vpu_trace(inst->dev, "[%d] state = %d\n", inst->id, inst->state);

	vdec->aborting = true;
	vpu_iface_add_scode(inst, SCODE_PADDING_ABORT);
	vdec->params.end_flag = 1;
	vpu_iface_set_decode_params(inst, &vdec->params, 1);

	vpu_session_abort(inst);

	ret = vpu_iface_get_stream_buffer_desc(inst, &desc);
	if (!ret)
		vpu_iface_update_stream_buffer(inst, desc.rptr, 1);

	vpu_session_rst_buf(inst);
	vpu_trace(inst->dev, "[%d] input : %d, decoded : %d, display : %d, sequence : %d\n",
		  inst->id,
		  vdec->params.frame_count,
		  vdec->decoded_frame_count,
		  vdec->display_frame_count,
		  vdec->sequence);
	if (!vdec->seq_hdr_found)
		vdec->reset_codec = true;
	vdec->params.end_flag = 0;
	vdec->drain = 0;
	vdec->params.frame_count = 0;
	vdec->decoded_frame_count = 0;
	vdec->display_frame_count = 0;
	vdec->sequence = 0;
	vdec->aborting = false;
	inst->extra_size = 0;
}

static void vdec_stop(struct vpu_inst *inst, bool free)
{
	struct vdec_t *vdec = inst->priv;

	vdec_clear_slots(inst);
	if (inst->state != VPU_CODEC_STATE_DEINIT)
		vpu_session_stop(inst);
	vdec_clear_fs(&vdec->mbi);
	vdec_clear_fs(&vdec->dcp);
	if (free) {
		vpu_free_dma(&vdec->udata);
		vpu_free_dma(&inst->stream_buffer);
	}
	vdec_update_state(inst, VPU_CODEC_STATE_DEINIT, 1);
	vdec->reset_codec = false;
}

static void vdec_release(struct vpu_inst *inst)
{
	if (inst->id != VPU_INST_NULL_ID)
		vpu_trace(inst->dev, "[%d]\n", inst->id);
	vpu_inst_lock(inst);
	vdec_stop(inst, true);
	vpu_inst_unlock(inst);
}

static void vdec_cleanup(struct vpu_inst *inst)
{
	struct vdec_t *vdec;

	if (!inst)
		return;

	vdec = inst->priv;
	vfree(vdec);
	inst->priv = NULL;
	vfree(inst);
}

static void vdec_init_params(struct vdec_t *vdec)
{
	vdec->params.frame_count = 0;
	vdec->params.end_flag = 0;
}

static int vdec_start(struct vpu_inst *inst)
{
	struct vdec_t *vdec = inst->priv;
	int stream_buffer_size;
	int ret;

	if (inst->state != VPU_CODEC_STATE_DEINIT)
		return 0;

	vpu_trace(inst->dev, "[%d]\n", inst->id);
	if (!vdec->udata.virt) {
		vdec->udata.length = 0x1000;
		ret = vpu_alloc_dma(inst->core, &vdec->udata);
		if (ret) {
			dev_err(inst->dev, "[%d] alloc udata fail\n", inst->id);
			goto error;
		}
	}

	if (!inst->stream_buffer.virt) {
		stream_buffer_size = vpu_iface_get_stream_buffer_size(inst->core);
		if (stream_buffer_size > 0) {
			inst->stream_buffer.length = stream_buffer_size;
			ret = vpu_alloc_dma(inst->core, &inst->stream_buffer);
			if (ret) {
				dev_err(inst->dev, "[%d] alloc stream buffer fail\n", inst->id);
				goto error;
			}
			inst->use_stream_buffer = true;
		}
	}

	if (inst->use_stream_buffer)
		vpu_iface_config_stream_buffer(inst, &inst->stream_buffer);
	vpu_iface_init_instance(inst);
	vdec->params.udata.base = vdec->udata.phys;
	vdec->params.udata.size = vdec->udata.length;
	ret = vpu_iface_set_decode_params(inst, &vdec->params, 0);
	if (ret) {
		dev_err(inst->dev, "[%d] set decode params fail\n", inst->id);
		goto error;
	}

	vdec_init_params(vdec);
	ret = vpu_session_start(inst);
	if (ret) {
		dev_err(inst->dev, "[%d] start fail\n", inst->id);
		goto error;
	}

	vdec_update_state(inst, VPU_CODEC_STATE_STARTED, 0);

	return 0;
error:
	vpu_free_dma(&vdec->udata);
	vpu_free_dma(&inst->stream_buffer);
	return ret;
}

static int vdec_start_session(struct vpu_inst *inst, u32 type)
{
	struct vdec_t *vdec = inst->priv;
	int ret = 0;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		if (vdec->reset_codec)
			vdec_stop(inst, false);
		if (inst->state == VPU_CODEC_STATE_DEINIT) {
			ret = vdec_start(inst);
			if (ret)
				return ret;
		}
	}

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		vdec_update_state(inst, vdec->state, 1);
		vdec->eos_received = 0;
		vpu_process_output_buffer(inst);
	} else {
		vdec_cmd_start(inst);
	}
	if (inst->state == VPU_CODEC_STATE_ACTIVE)
		vdec_response_fs_request(inst, false);

	return ret;
}

static int vdec_stop_session(struct vpu_inst *inst, u32 type)
{
	struct vdec_t *vdec = inst->priv;

	if (inst->state == VPU_CODEC_STATE_DEINIT)
		return 0;

	if (V4L2_TYPE_IS_OUTPUT(type)) {
		vdec_update_state(inst, VPU_CODEC_STATE_SEEK, 0);
		vdec->drain = 0;
	} else {
		if (inst->state != VPU_CODEC_STATE_DYAMIC_RESOLUTION_CHANGE) {
			vdec_abort(inst);
			vdec->eos_received = 0;
		}
		vdec_clear_slots(inst);
	}

	return 0;
}

static int vdec_get_debug_info(struct vpu_inst *inst, char *str, u32 size, u32 i)
{
	struct vdec_t *vdec = inst->priv;
	int num = -1;

	switch (i) {
	case 0:
		num = scnprintf(str, size,
				"req_frame_count = %d\ninterlaced = %d\n",
				vdec->req_frame_count,
				vdec->codec_info.progressive ? 0 : 1);
		break;
	case 1:
		num = scnprintf(str, size,
				"mbi: size = 0x%x request = %d, alloc = %d, response = %d\n",
				vdec->mbi.size,
				vdec->mbi.req_count,
				vdec->mbi.count,
				vdec->mbi.index);
		break;
	case 2:
		num = scnprintf(str, size,
				"dcp: size = 0x%x request = %d, alloc = %d, response = %d\n",
				vdec->dcp.size,
				vdec->dcp.req_count,
				vdec->dcp.count,
				vdec->dcp.index);
		break;
	case 3:
		num = scnprintf(str, size, "input_frame_count = %d\n", vdec->params.frame_count);
		break;
	case 4:
		num = scnprintf(str, size, "decoded_frame_count = %d\n", vdec->decoded_frame_count);
		break;
	case 5:
		num = scnprintf(str, size, "display_frame_count = %d\n", vdec->display_frame_count);
		break;
	case 6:
		num = scnprintf(str, size, "sequence = %d\n", vdec->sequence);
		break;
	case 7:
		num = scnprintf(str, size, "drain = %d, eos = %d, source_change = %d\n",
				vdec->drain, vdec->eos_received, vdec->source_change);
		break;
	case 8:
		num = scnprintf(str, size, "fps = %d/%d\n",
				vdec->codec_info.frame_rate.numerator,
				vdec->codec_info.frame_rate.denominator);
		break;
	case 9:
		num = scnprintf(str, size, "colorspace: %d, %d, %d, %d (%d)\n",
				vdec->codec_info.color_primaries,
				vdec->codec_info.transfer_chars,
				vdec->codec_info.matrix_coeffs,
				vdec->codec_info.full_range,
				vdec->codec_info.vui_present);
		break;
	default:
		break;
	}

	return num;
}

static struct vpu_inst_ops vdec_inst_ops = {
	.ctrl_init = vdec_ctrl_init,
	.check_ready = vdec_check_ready,
	.buf_done = vdec_buf_done,
	.get_one_frame = vdec_frame_decoded,
	.stop_done = vdec_stop_done,
	.event_notify = vdec_event_notify,
	.release = vdec_release,
	.cleanup = vdec_cleanup,
	.start = vdec_start_session,
	.stop = vdec_stop_session,
	.process_output = vdec_process_output,
	.process_capture = vdec_process_capture,
	.on_queue_empty = vdec_on_queue_empty,
	.get_debug_info = vdec_get_debug_info,
	.wait_prepare = vpu_inst_unlock,
	.wait_finish = vpu_inst_lock,
};

static void vdec_init(struct file *file)
{
	struct vpu_inst *inst = to_inst(file);
	struct v4l2_format f;

	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_H264;
	f.fmt.pix_mp.width = 1280;
	f.fmt.pix_mp.height = 720;
	f.fmt.pix_mp.field = V4L2_FIELD_NONE;
	vdec_s_fmt(file, &inst->fh, &f);

	memset(&f, 0, sizeof(f));
	f.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	f.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M_8L128;
	f.fmt.pix_mp.width = 1280;
	f.fmt.pix_mp.height = 720;
	f.fmt.pix_mp.field = V4L2_FIELD_NONE;
	vdec_s_fmt(file, &inst->fh, &f);
}

static int vdec_open(struct file *file)
{
	struct vpu_inst *inst;
	struct vdec_t *vdec;
	int ret;

	inst = vzalloc(sizeof(*inst));
	if (!inst)
		return -ENOMEM;

	vdec = vzalloc(sizeof(*vdec));
	if (!vdec) {
		vfree(inst);
		return -ENOMEM;
	}

	inst->ops = &vdec_inst_ops;
	inst->formats = vdec_formats;
	inst->type = VPU_CORE_TYPE_DEC;
	inst->priv = vdec;

	ret = vpu_v4l2_open(file, inst);
	if (ret)
		return ret;

	vdec->fixed_fmt = false;
	vdec->state = VPU_CODEC_STATE_ACTIVE;
	inst->min_buffer_cap = VDEC_MIN_BUFFER_CAP;
	inst->min_buffer_out = VDEC_MIN_BUFFER_OUT;
	vdec_init(file);

	return 0;
}

static const struct v4l2_file_operations vdec_fops = {
	.owner = THIS_MODULE,
	.open = vdec_open,
	.release = vpu_v4l2_close,
	.unlocked_ioctl = video_ioctl2,
	.poll = v4l2_m2m_fop_poll,
	.mmap = v4l2_m2m_fop_mmap,
};

const struct v4l2_ioctl_ops *vdec_get_ioctl_ops(void)
{
	return &vdec_ioctl_ops;
}

const struct v4l2_file_operations *vdec_get_fops(void)
{
	return &vdec_fops;
}
