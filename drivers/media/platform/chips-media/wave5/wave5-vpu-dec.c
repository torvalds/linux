// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Wave5 series multi-standard codec IP - decoder interface
 *
 * Copyright (C) 2021-2023 CHIPS&MEDIA INC
 */

#include <linux/pm_runtime.h>
#include "wave5-helper.h"

#define VPU_DEC_DEV_NAME "C&M Wave5 VPU decoder"
#define VPU_DEC_DRV_NAME "wave5-dec"

static const struct v4l2_frmsize_stepwise dec_hevc_frmsize = {
	.min_width = W5_MIN_DEC_PIC_8_WIDTH,
	.max_width = W5_MAX_DEC_PIC_WIDTH,
	.step_width = W5_DEC_CODEC_STEP_WIDTH,
	.min_height = W5_MIN_DEC_PIC_8_HEIGHT,
	.max_height = W5_MAX_DEC_PIC_HEIGHT,
	.step_height = W5_DEC_CODEC_STEP_HEIGHT,
};

static const struct v4l2_frmsize_stepwise dec_h264_frmsize = {
	.min_width = W5_MIN_DEC_PIC_32_WIDTH,
	.max_width = W5_MAX_DEC_PIC_WIDTH,
	.step_width = W5_DEC_CODEC_STEP_WIDTH,
	.min_height = W5_MIN_DEC_PIC_32_HEIGHT,
	.max_height = W5_MAX_DEC_PIC_HEIGHT,
	.step_height = W5_DEC_CODEC_STEP_HEIGHT,
};

static const struct v4l2_frmsize_stepwise dec_raw_frmsize = {
	.min_width = W5_MIN_DEC_PIC_8_WIDTH,
	.max_width = W5_MAX_DEC_PIC_WIDTH,
	.step_width = W5_DEC_RAW_STEP_WIDTH,
	.min_height = W5_MIN_DEC_PIC_8_HEIGHT,
	.max_height = W5_MAX_DEC_PIC_HEIGHT,
	.step_height = W5_DEC_RAW_STEP_HEIGHT,
};

static const struct vpu_format dec_fmt_list[FMT_TYPES][MAX_FMTS] = {
	[VPU_FMT_TYPE_CODEC] = {
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_HEVC,
			.v4l2_frmsize = &dec_hevc_frmsize,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_H264,
			.v4l2_frmsize = &dec_h264_frmsize,
		},
	},
	[VPU_FMT_TYPE_RAW] = {
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_YUV420,
			.v4l2_frmsize = &dec_raw_frmsize,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV12,
			.v4l2_frmsize = &dec_raw_frmsize,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV21,
			.v4l2_frmsize = &dec_raw_frmsize,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_YUV422P,
			.v4l2_frmsize = &dec_raw_frmsize,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV16,
			.v4l2_frmsize = &dec_raw_frmsize,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV61,
			.v4l2_frmsize = &dec_raw_frmsize,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_YUV420M,
			.v4l2_frmsize = &dec_raw_frmsize,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV12M,
			.v4l2_frmsize = &dec_raw_frmsize,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV21M,
			.v4l2_frmsize = &dec_raw_frmsize,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_YUV422M,
			.v4l2_frmsize = &dec_raw_frmsize,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV16M,
			.v4l2_frmsize = &dec_raw_frmsize,
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV61M,
			.v4l2_frmsize = &dec_raw_frmsize,
		},
	}
};

/*
 * Make sure that the state switch is allowed and add logging for debugging
 * purposes
 */
static int switch_state(struct vpu_instance *inst, enum vpu_instance_state state)
{
	switch (state) {
	case VPU_INST_STATE_NONE:
		break;
	case VPU_INST_STATE_OPEN:
		if (inst->state != VPU_INST_STATE_NONE)
			goto invalid_state_switch;
		goto valid_state_switch;
	case VPU_INST_STATE_INIT_SEQ:
		if (inst->state != VPU_INST_STATE_OPEN && inst->state != VPU_INST_STATE_STOP)
			goto invalid_state_switch;
		goto valid_state_switch;
	case VPU_INST_STATE_PIC_RUN:
		if (inst->state != VPU_INST_STATE_INIT_SEQ)
			goto invalid_state_switch;
		goto valid_state_switch;
	case VPU_INST_STATE_STOP:
		goto valid_state_switch;
	}
invalid_state_switch:
	WARN(1, "Invalid state switch from %s to %s.\n",
	     state_to_str(inst->state), state_to_str(state));
	return -EINVAL;
valid_state_switch:
	dev_dbg(inst->dev->dev, "Switch state from %s to %s.\n",
		state_to_str(inst->state), state_to_str(state));
	inst->state = state;
	return 0;
}

static int wave5_vpu_dec_set_eos_on_firmware(struct vpu_instance *inst)
{
	int ret;

	ret = wave5_vpu_dec_update_bitstream_buffer(inst, 0);
	if (ret) {
		/*
		 * To set the EOS flag, a command is sent to the firmware.
		 * That command may never return (timeout) or may report an error.
		 */
		dev_err(inst->dev->dev,
			"Setting EOS for the bitstream, fail: %d\n", ret);
		return ret;
	}
	return 0;
}

static bool wave5_last_src_buffer_consumed(struct v4l2_m2m_ctx *m2m_ctx)
{
	struct vpu_src_buffer *vpu_buf;

	if (!m2m_ctx->last_src_buf)
		return false;

	vpu_buf = wave5_to_vpu_src_buf(m2m_ctx->last_src_buf);
	return vpu_buf->consumed;
}

static void wave5_handle_src_buffer(struct vpu_instance *inst, dma_addr_t rd_ptr)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	struct v4l2_m2m_buffer *buf, *n;
	size_t consumed_bytes = 0;

	if (rd_ptr >= inst->last_rd_ptr) {
		consumed_bytes = rd_ptr - inst->last_rd_ptr;
	} else {
		size_t rd_offs = rd_ptr - inst->bitstream_vbuf.daddr;
		size_t last_rd_offs = inst->last_rd_ptr - inst->bitstream_vbuf.daddr;

		consumed_bytes = rd_offs + (inst->bitstream_vbuf.size - last_rd_offs);
	}

	inst->last_rd_ptr = rd_ptr;
	consumed_bytes += inst->remaining_consumed_bytes;

	dev_dbg(inst->dev->dev, "%s: %zu bytes of bitstream was consumed", __func__,
		consumed_bytes);

	v4l2_m2m_for_each_src_buf_safe(m2m_ctx, buf, n) {
		struct vb2_v4l2_buffer *src_buf = &buf->vb;
		size_t src_size = vb2_get_plane_payload(&src_buf->vb2_buf, 0);

		if (src_size > consumed_bytes)
			break;

		dev_dbg(inst->dev->dev, "%s: removing src buffer %i",
			__func__, src_buf->vb2_buf.index);
		src_buf = v4l2_m2m_src_buf_remove(m2m_ctx);
		inst->timestamp = src_buf->vb2_buf.timestamp;
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
		consumed_bytes -= src_size;

		/* Handle the case the last bitstream buffer has been picked */
		if (src_buf == m2m_ctx->last_src_buf) {
			int ret;

			m2m_ctx->last_src_buf = NULL;
			ret = wave5_vpu_dec_set_eos_on_firmware(inst);
			if (ret)
				dev_warn(inst->dev->dev,
					 "Setting EOS for the bitstream, fail: %d\n", ret);
			break;
		}
	}

	inst->remaining_consumed_bytes = consumed_bytes;
}

static int start_decode(struct vpu_instance *inst, u32 *fail_res)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	int ret = 0;

	ret = wave5_vpu_dec_start_one_frame(inst, fail_res);
	if (ret) {
		struct vb2_v4l2_buffer *src_buf;

		src_buf = v4l2_m2m_src_buf_remove(m2m_ctx);
		if (src_buf)
			v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
		switch_state(inst, VPU_INST_STATE_STOP);

		dev_dbg(inst->dev->dev, "%s: pic run failed / finish job", __func__);
		v4l2_m2m_job_finish(inst->v4l2_m2m_dev, m2m_ctx);
	}

	return ret;
}

static void flag_last_buffer_done(struct vpu_instance *inst)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	struct vb2_v4l2_buffer *vb;
	int i;

	lockdep_assert_held(&inst->state_spinlock);

	vb = v4l2_m2m_dst_buf_remove(m2m_ctx);
	if (!vb) {
		m2m_ctx->is_draining = true;
		m2m_ctx->next_buf_last = true;
		return;
	}

	for (i = 0; i < vb->vb2_buf.num_planes; i++)
		vb2_set_plane_payload(&vb->vb2_buf, i, 0);
	vb->field = V4L2_FIELD_NONE;

	v4l2_m2m_last_buffer_done(m2m_ctx, vb);
}

static void send_eos_event(struct vpu_instance *inst)
{
	static const struct v4l2_event vpu_event_eos = {
		.type = V4L2_EVENT_EOS
	};

	lockdep_assert_held(&inst->state_spinlock);

	v4l2_event_queue_fh(&inst->v4l2_fh, &vpu_event_eos);
	inst->eos = false;
}

static int handle_dynamic_resolution_change(struct vpu_instance *inst)
{
	struct v4l2_fh *fh = &inst->v4l2_fh;
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;

	static const struct v4l2_event vpu_event_src_ch = {
		.type = V4L2_EVENT_SOURCE_CHANGE,
		.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION,
	};
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	struct dec_initial_info *initial_info = &inst->codec_info->dec_info.initial_info;

	lockdep_assert_held(&inst->state_spinlock);

	dev_dbg(inst->dev->dev, "%s: rd_ptr %pad", __func__, &initial_info->rd_ptr);

	dev_dbg(inst->dev->dev, "%s: width: %u height: %u profile: %u | minbuffer: %u\n",
		__func__, initial_info->pic_width, initial_info->pic_height,
		initial_info->profile, initial_info->min_frame_buffer_count);

	inst->needs_reallocation = true;
	inst->fbc_buf_count = initial_info->min_frame_buffer_count + 1;
	if (inst->fbc_buf_count != v4l2_m2m_num_dst_bufs_ready(m2m_ctx)) {
		struct v4l2_ctrl *ctrl;

		ctrl = v4l2_ctrl_find(&inst->v4l2_ctrl_hdl,
				      V4L2_CID_MIN_BUFFERS_FOR_CAPTURE);
		if (ctrl)
			v4l2_ctrl_s_ctrl(ctrl, inst->fbc_buf_count);
	}

	if (p_dec_info->initial_info_obtained) {
		const struct vpu_format *vpu_fmt;

		inst->conf_win.left = initial_info->pic_crop_rect.left;
		inst->conf_win.top = initial_info->pic_crop_rect.top;
		inst->conf_win.width = initial_info->pic_width -
			initial_info->pic_crop_rect.left - initial_info->pic_crop_rect.right;
		inst->conf_win.height = initial_info->pic_height -
			initial_info->pic_crop_rect.top - initial_info->pic_crop_rect.bottom;

		vpu_fmt = wave5_find_vpu_fmt(inst->src_fmt.pixelformat,
					     dec_fmt_list[VPU_FMT_TYPE_CODEC]);
		if (!vpu_fmt)
			return -EINVAL;

		wave5_update_pix_fmt(&inst->src_fmt,
				     VPU_FMT_TYPE_CODEC,
				     initial_info->pic_width,
				     initial_info->pic_height,
				     vpu_fmt->v4l2_frmsize);

		vpu_fmt = wave5_find_vpu_fmt(inst->dst_fmt.pixelformat,
					     dec_fmt_list[VPU_FMT_TYPE_RAW]);
		if (!vpu_fmt)
			return -EINVAL;

		wave5_update_pix_fmt(&inst->dst_fmt,
				     VPU_FMT_TYPE_RAW,
				     initial_info->pic_width,
				     initial_info->pic_height,
				     vpu_fmt->v4l2_frmsize);
	}

	v4l2_event_queue_fh(fh, &vpu_event_src_ch);

	return 0;
}

static void wave5_vpu_dec_finish_decode(struct vpu_instance *inst)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	struct dec_output_info dec_info;
	int ret;
	struct vb2_v4l2_buffer *dec_buf = NULL;
	struct vb2_v4l2_buffer *disp_buf = NULL;
	struct vb2_queue *dst_vq = v4l2_m2m_get_dst_vq(m2m_ctx);
	struct queue_status_info q_status;

	dev_dbg(inst->dev->dev, "%s: Fetch output info from firmware.", __func__);

	ret = wave5_vpu_dec_get_output_info(inst, &dec_info);
	if (ret) {
		dev_warn(inst->dev->dev, "%s: could not get output info.", __func__);
		v4l2_m2m_job_finish(inst->v4l2_m2m_dev, m2m_ctx);
		return;
	}

	dev_dbg(inst->dev->dev, "%s: rd_ptr %pad wr_ptr %pad", __func__, &dec_info.rd_ptr,
		&dec_info.wr_ptr);
	wave5_handle_src_buffer(inst, dec_info.rd_ptr);

	dev_dbg(inst->dev->dev, "%s: dec_info dec_idx %i disp_idx %i", __func__,
		dec_info.index_frame_decoded, dec_info.index_frame_display);

	if (!vb2_is_streaming(dst_vq)) {
		dev_dbg(inst->dev->dev, "%s: capture is not streaming..", __func__);
		v4l2_m2m_job_finish(inst->v4l2_m2m_dev, m2m_ctx);
		return;
	}

	/* Remove decoded buffer from the ready queue now that it has been
	 * decoded.
	 */
	if (dec_info.index_frame_decoded >= 0) {
		struct vb2_buffer *vb = vb2_get_buffer(dst_vq,
						       dec_info.index_frame_decoded);
		if (vb) {
			dec_buf = to_vb2_v4l2_buffer(vb);
			dec_buf->vb2_buf.timestamp = inst->timestamp;
		} else {
			dev_warn(inst->dev->dev, "%s: invalid decoded frame index %i",
				 __func__, dec_info.index_frame_decoded);
		}
	}

	if (dec_info.index_frame_display >= 0) {
		disp_buf = v4l2_m2m_dst_buf_remove_by_idx(m2m_ctx, dec_info.index_frame_display);
		if (!disp_buf)
			dev_warn(inst->dev->dev, "%s: invalid display frame index %i",
				 __func__, dec_info.index_frame_display);
	}

	/* If there is anything to display, do that now */
	if (disp_buf) {
		struct vpu_dst_buffer *dst_vpu_buf = wave5_to_vpu_dst_buf(disp_buf);

		if (inst->dst_fmt.num_planes == 1) {
			vb2_set_plane_payload(&disp_buf->vb2_buf, 0,
					      inst->dst_fmt.plane_fmt[0].sizeimage);
		} else if (inst->dst_fmt.num_planes == 2) {
			vb2_set_plane_payload(&disp_buf->vb2_buf, 0,
					      inst->dst_fmt.plane_fmt[0].sizeimage);
			vb2_set_plane_payload(&disp_buf->vb2_buf, 1,
					      inst->dst_fmt.plane_fmt[1].sizeimage);
		} else if (inst->dst_fmt.num_planes == 3) {
			vb2_set_plane_payload(&disp_buf->vb2_buf, 0,
					      inst->dst_fmt.plane_fmt[0].sizeimage);
			vb2_set_plane_payload(&disp_buf->vb2_buf, 1,
					      inst->dst_fmt.plane_fmt[1].sizeimage);
			vb2_set_plane_payload(&disp_buf->vb2_buf, 2,
					      inst->dst_fmt.plane_fmt[2].sizeimage);
		}

		/* TODO implement interlace support */
		disp_buf->field = V4L2_FIELD_NONE;
		dst_vpu_buf->display = true;
		v4l2_m2m_buf_done(disp_buf, VB2_BUF_STATE_DONE);

		dev_dbg(inst->dev->dev, "%s: frame_cycle %8u (payload %lu)\n",
			__func__, dec_info.frame_cycle,
			vb2_get_plane_payload(&disp_buf->vb2_buf, 0));
	}

	if ((dec_info.index_frame_display == DISPLAY_IDX_FLAG_SEQ_END ||
	     dec_info.sequence_changed)) {
		unsigned long flags;

		spin_lock_irqsave(&inst->state_spinlock, flags);
		if (!v4l2_m2m_has_stopped(m2m_ctx)) {
			switch_state(inst, VPU_INST_STATE_STOP);

			if (dec_info.sequence_changed)
				handle_dynamic_resolution_change(inst);
			else
				send_eos_event(inst);

			flag_last_buffer_done(inst);
		}
		spin_unlock_irqrestore(&inst->state_spinlock, flags);
	}

	/*
	 * During a resolution change and while draining, the firmware may flush
	 * the reorder queue regardless of having a matching decoding operation
	 * pending. Only terminate the job if there are no more IRQ coming.
	 */
	wave5_vpu_dec_give_command(inst, DEC_GET_QUEUE_STATUS, &q_status);
	if (q_status.report_queue_count == 0 &&
	    (q_status.instance_queue_count == 0 || dec_info.sequence_changed)) {
		dev_dbg(inst->dev->dev, "%s: finishing job.\n", __func__);
		pm_runtime_mark_last_busy(inst->dev->dev);
		pm_runtime_put_autosuspend(inst->dev->dev);
		v4l2_m2m_job_finish(inst->v4l2_m2m_dev, m2m_ctx);
	}
}

static int wave5_vpu_dec_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	strscpy(cap->driver, VPU_DEC_DRV_NAME, sizeof(cap->driver));
	strscpy(cap->card, VPU_DEC_DRV_NAME, sizeof(cap->card));

	return 0;
}

static int wave5_vpu_dec_enum_framesizes(struct file *f, void *fh, struct v4l2_frmsizeenum *fsize)
{
	const struct vpu_format *vpu_fmt;

	if (fsize->index)
		return -EINVAL;

	vpu_fmt = wave5_find_vpu_fmt(fsize->pixel_format, dec_fmt_list[VPU_FMT_TYPE_CODEC]);
	if (!vpu_fmt) {
		vpu_fmt = wave5_find_vpu_fmt(fsize->pixel_format, dec_fmt_list[VPU_FMT_TYPE_RAW]);
		if (!vpu_fmt)
			return -EINVAL;
	}

	fsize->type = V4L2_FRMSIZE_TYPE_CONTINUOUS;
	fsize->stepwise.min_width = vpu_fmt->v4l2_frmsize->min_width;
	fsize->stepwise.max_width = vpu_fmt->v4l2_frmsize->max_width;
	fsize->stepwise.step_width = W5_DEC_CODEC_STEP_WIDTH;
	fsize->stepwise.min_height = vpu_fmt->v4l2_frmsize->min_height;
	fsize->stepwise.max_height = vpu_fmt->v4l2_frmsize->max_height;
	fsize->stepwise.step_height = W5_DEC_CODEC_STEP_HEIGHT;

	return 0;
}

static int wave5_vpu_dec_enum_fmt_cap(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	const struct vpu_format *vpu_fmt;

	vpu_fmt = wave5_find_vpu_fmt_by_idx(f->index, dec_fmt_list[VPU_FMT_TYPE_RAW]);
	if (!vpu_fmt)
		return -EINVAL;

	f->pixelformat = vpu_fmt->v4l2_pix_fmt;
	f->flags = 0;

	return 0;
}

static int wave5_vpu_dec_try_fmt_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	struct dec_info *p_dec_info = &inst->codec_info->dec_info;
	const struct v4l2_frmsize_stepwise *frmsize;
	const struct vpu_format *vpu_fmt;
	int width, height;

	dev_dbg(inst->dev->dev,
		"%s: fourcc: %u width: %u height: %u nm planes: %u colorspace: %u field: %u\n",
		__func__, f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width, f->fmt.pix_mp.height,
		f->fmt.pix_mp.num_planes, f->fmt.pix_mp.colorspace, f->fmt.pix_mp.field);

	vpu_fmt = wave5_find_vpu_fmt(f->fmt.pix_mp.pixelformat, dec_fmt_list[VPU_FMT_TYPE_RAW]);
	if (!vpu_fmt) {
		width = inst->dst_fmt.width;
		height = inst->dst_fmt.height;
		f->fmt.pix_mp.pixelformat = inst->dst_fmt.pixelformat;
		frmsize = &dec_raw_frmsize;
	} else {
		width = f->fmt.pix_mp.width;
		height = f->fmt.pix_mp.height;
		f->fmt.pix_mp.pixelformat = vpu_fmt->v4l2_pix_fmt;
		frmsize = vpu_fmt->v4l2_frmsize;
	}

	if (p_dec_info->initial_info_obtained) {
		width = inst->dst_fmt.width;
		height = inst->dst_fmt.height;
	}

	wave5_update_pix_fmt(&f->fmt.pix_mp, VPU_FMT_TYPE_RAW,
			     width, height, frmsize);
	f->fmt.pix_mp.colorspace = inst->colorspace;
	f->fmt.pix_mp.ycbcr_enc = inst->ycbcr_enc;
	f->fmt.pix_mp.quantization = inst->quantization;
	f->fmt.pix_mp.xfer_func = inst->xfer_func;

	return 0;
}

static int wave5_vpu_dec_s_fmt_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	int i, ret;

	dev_dbg(inst->dev->dev,
		"%s: fourcc: %u width: %u height: %u num_planes: %u colorspace: %u field: %u\n",
		__func__, f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width, f->fmt.pix_mp.height,
		f->fmt.pix_mp.num_planes, f->fmt.pix_mp.colorspace, f->fmt.pix_mp.field);

	ret = wave5_vpu_dec_try_fmt_cap(file, fh, f);
	if (ret)
		return ret;

	inst->dst_fmt.width = f->fmt.pix_mp.width;
	inst->dst_fmt.height = f->fmt.pix_mp.height;
	inst->dst_fmt.pixelformat = f->fmt.pix_mp.pixelformat;
	inst->dst_fmt.field = f->fmt.pix_mp.field;
	inst->dst_fmt.flags = f->fmt.pix_mp.flags;
	inst->dst_fmt.num_planes = f->fmt.pix_mp.num_planes;
	for (i = 0; i < inst->dst_fmt.num_planes; i++) {
		inst->dst_fmt.plane_fmt[i].bytesperline = f->fmt.pix_mp.plane_fmt[i].bytesperline;
		inst->dst_fmt.plane_fmt[i].sizeimage = f->fmt.pix_mp.plane_fmt[i].sizeimage;
	}

	if (inst->dst_fmt.pixelformat == V4L2_PIX_FMT_NV12 ||
	    inst->dst_fmt.pixelformat == V4L2_PIX_FMT_NV12M) {
		inst->cbcr_interleave = true;
		inst->nv21 = false;
		inst->output_format = FORMAT_420;
	} else if (inst->dst_fmt.pixelformat == V4L2_PIX_FMT_NV21 ||
		   inst->dst_fmt.pixelformat == V4L2_PIX_FMT_NV21M) {
		inst->cbcr_interleave = true;
		inst->nv21 = true;
		inst->output_format = FORMAT_420;
	} else if (inst->dst_fmt.pixelformat == V4L2_PIX_FMT_NV16 ||
		   inst->dst_fmt.pixelformat == V4L2_PIX_FMT_NV16M) {
		inst->cbcr_interleave = true;
		inst->nv21 = false;
		inst->output_format = FORMAT_422;
	} else if (inst->dst_fmt.pixelformat == V4L2_PIX_FMT_NV61 ||
		   inst->dst_fmt.pixelformat == V4L2_PIX_FMT_NV61M) {
		inst->cbcr_interleave = true;
		inst->nv21 = true;
		inst->output_format = FORMAT_422;
	} else if (inst->dst_fmt.pixelformat == V4L2_PIX_FMT_YUV422P ||
		   inst->dst_fmt.pixelformat == V4L2_PIX_FMT_YUV422M) {
		inst->cbcr_interleave = false;
		inst->nv21 = false;
		inst->output_format = FORMAT_422;
	} else {
		inst->cbcr_interleave = false;
		inst->nv21 = false;
		inst->output_format = FORMAT_420;
	}

	return 0;
}

static int wave5_vpu_dec_g_fmt_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	int i;

	f->fmt.pix_mp.width = inst->dst_fmt.width;
	f->fmt.pix_mp.height = inst->dst_fmt.height;
	f->fmt.pix_mp.pixelformat = inst->dst_fmt.pixelformat;
	f->fmt.pix_mp.field = inst->dst_fmt.field;
	f->fmt.pix_mp.flags = inst->dst_fmt.flags;
	f->fmt.pix_mp.num_planes = inst->dst_fmt.num_planes;
	for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
		f->fmt.pix_mp.plane_fmt[i].bytesperline = inst->dst_fmt.plane_fmt[i].bytesperline;
		f->fmt.pix_mp.plane_fmt[i].sizeimage = inst->dst_fmt.plane_fmt[i].sizeimage;
	}

	f->fmt.pix_mp.colorspace = inst->colorspace;
	f->fmt.pix_mp.ycbcr_enc = inst->ycbcr_enc;
	f->fmt.pix_mp.quantization = inst->quantization;
	f->fmt.pix_mp.xfer_func = inst->xfer_func;

	return 0;
}

static int wave5_vpu_dec_enum_fmt_out(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	const struct vpu_format *vpu_fmt;

	dev_dbg(inst->dev->dev, "%s: index: %u\n", __func__, f->index);

	vpu_fmt = wave5_find_vpu_fmt_by_idx(f->index, dec_fmt_list[VPU_FMT_TYPE_CODEC]);
	if (!vpu_fmt)
		return -EINVAL;

	f->pixelformat = vpu_fmt->v4l2_pix_fmt;
	f->flags = V4L2_FMT_FLAG_DYN_RESOLUTION | V4L2_FMT_FLAG_COMPRESSED;

	return 0;
}

static int wave5_vpu_dec_try_fmt_out(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	const struct v4l2_frmsize_stepwise *frmsize;
	const struct vpu_format *vpu_fmt;
	int width, height;

	dev_dbg(inst->dev->dev,
		"%s: fourcc: %u width: %u height: %u num_planes: %u colorspace: %u field: %u\n",
		__func__, f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width, f->fmt.pix_mp.height,
		f->fmt.pix_mp.num_planes, f->fmt.pix_mp.colorspace, f->fmt.pix_mp.field);

	vpu_fmt = wave5_find_vpu_fmt(f->fmt.pix_mp.pixelformat, dec_fmt_list[VPU_FMT_TYPE_CODEC]);
	if (!vpu_fmt) {
		width = inst->src_fmt.width;
		height = inst->src_fmt.height;
		f->fmt.pix_mp.pixelformat = inst->src_fmt.pixelformat;
		frmsize = &dec_hevc_frmsize;
	} else {
		width = f->fmt.pix_mp.width;
		height = f->fmt.pix_mp.height;
		f->fmt.pix_mp.pixelformat = vpu_fmt->v4l2_pix_fmt;
		frmsize = vpu_fmt->v4l2_frmsize;
	}

	wave5_update_pix_fmt(&f->fmt.pix_mp, VPU_FMT_TYPE_CODEC,
			     width, height, frmsize);

	return 0;
}

static int wave5_vpu_dec_s_fmt_out(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	const struct vpu_format *vpu_fmt;
	int i, ret;

	dev_dbg(inst->dev->dev,
		"%s: fourcc: %u width: %u height: %u num_planes: %u field: %u\n",
		__func__, f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width, f->fmt.pix_mp.height,
		f->fmt.pix_mp.num_planes, f->fmt.pix_mp.field);

	ret = wave5_vpu_dec_try_fmt_out(file, fh, f);
	if (ret)
		return ret;

	inst->std = wave5_to_vpu_std(f->fmt.pix_mp.pixelformat, inst->type);
	if (inst->std == STD_UNKNOWN) {
		dev_warn(inst->dev->dev, "unsupported pixelformat: %.4s\n",
			 (char *)&f->fmt.pix_mp.pixelformat);
		return -EINVAL;
	}

	inst->src_fmt.width = f->fmt.pix_mp.width;
	inst->src_fmt.height = f->fmt.pix_mp.height;
	inst->src_fmt.pixelformat = f->fmt.pix_mp.pixelformat;
	inst->src_fmt.field = f->fmt.pix_mp.field;
	inst->src_fmt.flags = f->fmt.pix_mp.flags;
	inst->src_fmt.num_planes = f->fmt.pix_mp.num_planes;
	for (i = 0; i < inst->src_fmt.num_planes; i++) {
		inst->src_fmt.plane_fmt[i].bytesperline = f->fmt.pix_mp.plane_fmt[i].bytesperline;
		inst->src_fmt.plane_fmt[i].sizeimage = f->fmt.pix_mp.plane_fmt[i].sizeimage;
	}

	inst->colorspace = f->fmt.pix_mp.colorspace;
	inst->ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	inst->quantization = f->fmt.pix_mp.quantization;
	inst->xfer_func = f->fmt.pix_mp.xfer_func;

	vpu_fmt = wave5_find_vpu_fmt(inst->dst_fmt.pixelformat, dec_fmt_list[VPU_FMT_TYPE_RAW]);
	if (!vpu_fmt)
		return -EINVAL;

	wave5_update_pix_fmt(&inst->dst_fmt, VPU_FMT_TYPE_RAW,
			     f->fmt.pix_mp.width, f->fmt.pix_mp.height,
			     vpu_fmt->v4l2_frmsize);

	return 0;
}

static int wave5_vpu_dec_g_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);

	dev_dbg(inst->dev->dev, "%s: type: %u | target: %u\n", __func__, s->type, s->target);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;
	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
	case V4L2_SEL_TGT_COMPOSE_PADDED:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = inst->dst_fmt.width;
		s->r.height = inst->dst_fmt.height;
		break;
	case V4L2_SEL_TGT_COMPOSE:
	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
		s->r.left = 0;
		s->r.top = 0;
		if (inst->state > VPU_INST_STATE_OPEN) {
			s->r = inst->conf_win;
		} else {
			s->r.width = inst->src_fmt.width;
			s->r.height = inst->src_fmt.height;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wave5_vpu_dec_s_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);

	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (s->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	dev_dbg(inst->dev->dev, "V4L2_SEL_TGT_COMPOSE w: %u h: %u\n",
		s->r.width, s->r.height);

	s->r.left = 0;
	s->r.top = 0;
	s->r.width = inst->dst_fmt.width;
	s->r.height = inst->dst_fmt.height;

	return 0;
}

static int wave5_vpu_dec_stop(struct vpu_instance *inst)
{
	int ret = 0;
	unsigned long flags;
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;

	spin_lock_irqsave(&inst->state_spinlock, flags);

	if (m2m_ctx->is_draining) {
		ret = -EBUSY;
		goto unlock_and_return;
	}

	if (inst->state != VPU_INST_STATE_NONE) {
		/*
		 * Temporarily release the state_spinlock so that subsequent
		 * calls do not block on a mutex while inside this spinlock.
		 */
		spin_unlock_irqrestore(&inst->state_spinlock, flags);
		ret = wave5_vpu_dec_set_eos_on_firmware(inst);
		if (ret)
			return ret;

		spin_lock_irqsave(&inst->state_spinlock, flags);
		/*
		 * TODO eliminate this check by using a separate check for
		 * draining triggered by a resolution change.
		 */
		if (m2m_ctx->is_draining) {
			ret = -EBUSY;
			goto unlock_and_return;
		}
	}

	/*
	 * Used to remember the EOS state after the streamoff/on transition on
	 * the capture queue.
	 */
	inst->eos = true;

	if (m2m_ctx->has_stopped)
		goto unlock_and_return;

	m2m_ctx->last_src_buf = v4l2_m2m_last_src_buf(m2m_ctx);
	m2m_ctx->is_draining = true;

	/*
	 * Deferred to device run in case it wasn't in the ring buffer
	 * yet. In other case, we have to send the EOS signal to the
	 * firmware so that any pending PIC_RUN ends without new
	 * bitstream buffer.
	 */
	if (m2m_ctx->last_src_buf)
		goto unlock_and_return;

	if (inst->state == VPU_INST_STATE_NONE) {
		send_eos_event(inst);
		flag_last_buffer_done(inst);
	}

unlock_and_return:
	spin_unlock_irqrestore(&inst->state_spinlock, flags);
	return ret;
}

static int wave5_vpu_dec_start(struct vpu_instance *inst)
{
	int ret = 0;
	unsigned long flags;
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	struct vb2_queue *dst_vq = v4l2_m2m_get_dst_vq(m2m_ctx);

	spin_lock_irqsave(&inst->state_spinlock, flags);

	if (m2m_ctx->is_draining) {
		ret = -EBUSY;
		goto unlock_and_return;
	}

	if (m2m_ctx->has_stopped)
		m2m_ctx->has_stopped = false;

	vb2_clear_last_buffer_dequeued(dst_vq);
	inst->eos = false;

unlock_and_return:
	spin_unlock_irqrestore(&inst->state_spinlock, flags);
	return ret;
}

static int wave5_vpu_dec_decoder_cmd(struct file *file, void *fh, struct v4l2_decoder_cmd *dc)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	int ret;

	dev_dbg(inst->dev->dev, "decoder command: %u\n", dc->cmd);

	ret = v4l2_m2m_ioctl_try_decoder_cmd(file, fh, dc);
	if (ret)
		return ret;

	switch (dc->cmd) {
	case V4L2_DEC_CMD_STOP:
		ret = wave5_vpu_dec_stop(inst);
		/* Just in case we don't have anything to decode anymore */
		v4l2_m2m_try_schedule(m2m_ctx);
		break;
	case V4L2_DEC_CMD_START:
		ret = wave5_vpu_dec_start(inst);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static const struct v4l2_ioctl_ops wave5_vpu_dec_ioctl_ops = {
	.vidioc_querycap = wave5_vpu_dec_querycap,
	.vidioc_enum_framesizes = wave5_vpu_dec_enum_framesizes,

	.vidioc_enum_fmt_vid_cap	= wave5_vpu_dec_enum_fmt_cap,
	.vidioc_s_fmt_vid_cap_mplane = wave5_vpu_dec_s_fmt_cap,
	.vidioc_g_fmt_vid_cap_mplane = wave5_vpu_dec_g_fmt_cap,
	.vidioc_try_fmt_vid_cap_mplane = wave5_vpu_dec_try_fmt_cap,

	.vidioc_enum_fmt_vid_out	= wave5_vpu_dec_enum_fmt_out,
	.vidioc_s_fmt_vid_out_mplane = wave5_vpu_dec_s_fmt_out,
	.vidioc_g_fmt_vid_out_mplane = wave5_vpu_g_fmt_out,
	.vidioc_try_fmt_vid_out_mplane = wave5_vpu_dec_try_fmt_out,

	.vidioc_g_selection = wave5_vpu_dec_g_selection,
	.vidioc_s_selection = wave5_vpu_dec_s_selection,

	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	/*
	 * Firmware does not support CREATE_BUFS for CAPTURE queue. Since
	 * there is no immediate use-case for supporting CREATE_BUFS on
	 * just the OUTPUT queue, disable CREATE_BUFS altogether.
	 */
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,

	.vidioc_try_decoder_cmd = v4l2_m2m_ioctl_try_decoder_cmd,
	.vidioc_decoder_cmd = wave5_vpu_dec_decoder_cmd,

	.vidioc_subscribe_event = wave5_vpu_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int wave5_vpu_dec_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
				     unsigned int *num_planes, unsigned int sizes[],
				     struct device *alloc_devs[])
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	struct v4l2_pix_format_mplane inst_format =
		(q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ? inst->src_fmt : inst->dst_fmt;
	unsigned int i;

	dev_dbg(inst->dev->dev, "%s: num_buffers: %u | num_planes: %u | type: %u\n", __func__,
		*num_buffers, *num_planes, q->type);

	*num_planes = inst_format.num_planes;

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		sizes[0] = inst_format.plane_fmt[0].sizeimage;
		dev_dbg(inst->dev->dev, "%s: size[0]: %u\n", __func__, sizes[0]);
	} else if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		if (*num_buffers < inst->fbc_buf_count)
			*num_buffers = inst->fbc_buf_count;

		for (i = 0; i < *num_planes; i++) {
			sizes[i] = inst_format.plane_fmt[i].sizeimage;
			dev_dbg(inst->dev->dev, "%s: size[%u]: %u\n", __func__, i, sizes[i]);
		}
	}

	return 0;
}

static int wave5_prepare_fb(struct vpu_instance *inst)
{
	int linear_num;
	int non_linear_num;
	int fb_stride = 0, fb_height = 0;
	int luma_size, chroma_size;
	int ret, i;
	struct v4l2_m2m_buffer *buf, *n;
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	u32 bitdepth = inst->codec_info->dec_info.initial_info.luma_bitdepth;

	switch (bitdepth) {
	case 8:
		break;
	case 10:
		if (inst->std == W_HEVC_DEC &&
		    inst->dev->attr.support_hevc10bit_dec)
			break;

		fallthrough;
	default:
		dev_err(inst->dev->dev, "no support for %d bit depth\n", bitdepth);

		return -EINVAL;
	}

	linear_num = v4l2_m2m_num_dst_bufs_ready(m2m_ctx);
	non_linear_num = inst->fbc_buf_count;

	for (i = 0; i < non_linear_num; i++) {
		struct frame_buffer *frame = &inst->frame_buf[i];
		struct vpu_buf *vframe = &inst->frame_vbuf[i];

		fb_stride = ALIGN(inst->dst_fmt.width * bitdepth / 8, 32);
		fb_height = ALIGN(inst->dst_fmt.height, 32);
		luma_size = fb_stride * fb_height;

		chroma_size = ALIGN(fb_stride / 2, 16) * fb_height;

		if (vframe->size == (luma_size + chroma_size))
			continue;

		if (vframe->size)
			wave5_vpu_dec_reset_framebuffer(inst, i);

		vframe->size = luma_size + chroma_size;
		ret = wave5_vdi_allocate_dma_memory(inst->dev, vframe);
		if (ret) {
			dev_dbg(inst->dev->dev,
				"%s: Allocating FBC buf of size %zu, fail: %d\n",
				__func__, vframe->size, ret);
			return ret;
		}

		frame->buf_y = vframe->daddr;
		frame->buf_cb = vframe->daddr + luma_size;
		frame->buf_cr = (dma_addr_t)-1;
		frame->size = vframe->size;
		frame->width = inst->src_fmt.width;
		frame->stride = fb_stride;
		frame->map_type = COMPRESSED_FRAME_MAP;
		frame->update_fb_info = true;
	}
	/* In case the count has reduced, clean up leftover framebuffer memory */
	for (i = non_linear_num; i < MAX_REG_FRAME; i++) {
		ret = wave5_vpu_dec_reset_framebuffer(inst, i);
		if (ret)
			break;
	}

	for (i = 0; i < linear_num; i++) {
		struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
		struct vb2_queue *dst_vq = v4l2_m2m_get_dst_vq(m2m_ctx);
		struct vb2_buffer *vb = vb2_get_buffer(dst_vq, i);
		struct frame_buffer *frame = &inst->frame_buf[non_linear_num + i];
		dma_addr_t buf_addr_y = 0, buf_addr_cb = 0, buf_addr_cr = 0;
		u32 buf_size = 0;
		u32 fb_stride = inst->dst_fmt.width;
		u32 luma_size = fb_stride * inst->dst_fmt.height;
		u32 chroma_size;

		if (inst->output_format == FORMAT_422)
			chroma_size = fb_stride * inst->dst_fmt.height / 2;
		else
			chroma_size = fb_stride * inst->dst_fmt.height / 4;

		if (inst->dst_fmt.num_planes == 1) {
			buf_size = vb2_plane_size(vb, 0);
			buf_addr_y = vb2_dma_contig_plane_dma_addr(vb, 0);
			buf_addr_cb = buf_addr_y + luma_size;
			buf_addr_cr = buf_addr_cb + chroma_size;
		} else if (inst->dst_fmt.num_planes == 2) {
			buf_size = vb2_plane_size(vb, 0) +
				vb2_plane_size(vb, 1);
			buf_addr_y = vb2_dma_contig_plane_dma_addr(vb, 0);
			buf_addr_cb = vb2_dma_contig_plane_dma_addr(vb, 1);
			buf_addr_cr = buf_addr_cb + chroma_size;
		} else if (inst->dst_fmt.num_planes == 3) {
			buf_size = vb2_plane_size(vb, 0) +
				vb2_plane_size(vb, 1) +
				vb2_plane_size(vb, 2);
			buf_addr_y = vb2_dma_contig_plane_dma_addr(vb, 0);
			buf_addr_cb = vb2_dma_contig_plane_dma_addr(vb, 1);
			buf_addr_cr = vb2_dma_contig_plane_dma_addr(vb, 2);
		}

		frame->buf_y = buf_addr_y;
		frame->buf_cb = buf_addr_cb;
		frame->buf_cr = buf_addr_cr;
		frame->size = buf_size;
		frame->width = inst->src_fmt.width;
		frame->stride = fb_stride;
		frame->map_type = LINEAR_FRAME_MAP;
		frame->update_fb_info = true;
	}

	ret = wave5_vpu_dec_register_frame_buffer_ex(inst, non_linear_num, linear_num,
						     fb_stride, inst->dst_fmt.height);
	if (ret) {
		dev_dbg(inst->dev->dev, "%s: vpu_dec_register_frame_buffer_ex fail: %d",
			__func__, ret);
		return ret;
	}

	/*
	 * Mark all frame buffers as out of display, to avoid using them before
	 * the application have them queued.
	 */
	for (i = 0; i < v4l2_m2m_num_dst_bufs_ready(m2m_ctx); i++) {
		ret = wave5_vpu_dec_set_disp_flag(inst, i);
		if (ret) {
			dev_dbg(inst->dev->dev,
				"%s: Setting display flag of buf index: %u, fail: %d\n",
				__func__, i, ret);
		}
	}

	v4l2_m2m_for_each_dst_buf_safe(m2m_ctx, buf, n) {
		struct vb2_v4l2_buffer *vbuf = &buf->vb;

		ret = wave5_vpu_dec_clr_disp_flag(inst, vbuf->vb2_buf.index);
		if (ret)
			dev_dbg(inst->dev->dev,
				"%s: Clearing display flag of buf index: %u, fail: %d\n",
				__func__, i, ret);
	}

	return 0;
}

static int write_to_ringbuffer(struct vpu_instance *inst, void *buffer, size_t buffer_size,
			       struct vpu_buf *ring_buffer, dma_addr_t wr_ptr)
{
	size_t size;
	size_t offset = wr_ptr - ring_buffer->daddr;
	int ret;

	if (wr_ptr + buffer_size > ring_buffer->daddr + ring_buffer->size) {
		size = ring_buffer->daddr + ring_buffer->size - wr_ptr;
		ret = wave5_vdi_write_memory(inst->dev, ring_buffer, offset, (u8 *)buffer, size);
		if (ret < 0)
			return ret;

		ret = wave5_vdi_write_memory(inst->dev, ring_buffer, 0, (u8 *)buffer + size,
					     buffer_size - size);
		if (ret < 0)
			return ret;
	} else {
		ret = wave5_vdi_write_memory(inst->dev, ring_buffer, offset, (u8 *)buffer,
					     buffer_size);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int fill_ringbuffer(struct vpu_instance *inst)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	struct v4l2_m2m_buffer *buf, *n;
	int ret;

	if (m2m_ctx->last_src_buf)  {
		struct vpu_src_buffer *vpu_buf = wave5_to_vpu_src_buf(m2m_ctx->last_src_buf);

		if (vpu_buf->consumed) {
			dev_dbg(inst->dev->dev, "last src buffer already written\n");
			return 0;
		}
	}

	v4l2_m2m_for_each_src_buf_safe(m2m_ctx, buf, n) {
		struct vb2_v4l2_buffer *vbuf = &buf->vb;
		struct vpu_src_buffer *vpu_buf = wave5_to_vpu_src_buf(vbuf);
		struct vpu_buf *ring_buffer = &inst->bitstream_vbuf;
		size_t src_size = vb2_get_plane_payload(&vbuf->vb2_buf, 0);
		void *src_buf = vb2_plane_vaddr(&vbuf->vb2_buf, 0);
		dma_addr_t rd_ptr = 0;
		dma_addr_t wr_ptr = 0;
		size_t remain_size = 0;

		if (vpu_buf->consumed) {
			dev_dbg(inst->dev->dev, "already copied src buf (%u) to the ring buffer\n",
				vbuf->vb2_buf.index);
			continue;
		}

		if (!src_buf) {
			dev_dbg(inst->dev->dev,
				"%s: Acquiring kernel pointer to src buf (%u), fail\n",
				__func__, vbuf->vb2_buf.index);
			break;
		}

		ret = wave5_vpu_dec_get_bitstream_buffer(inst, &rd_ptr, &wr_ptr, &remain_size);
		if (ret) {
			/* Unable to acquire the mutex */
			dev_err(inst->dev->dev, "Getting the bitstream buffer, fail: %d\n",
				ret);
			return ret;
		}

		dev_dbg(inst->dev->dev, "%s: rd_ptr %pad wr_ptr %pad", __func__, &rd_ptr, &wr_ptr);

		if (remain_size < src_size) {
			dev_dbg(inst->dev->dev,
				"%s: remaining size: %zu < source size: %zu for src buf (%u)\n",
				__func__, remain_size, src_size, vbuf->vb2_buf.index);
			break;
		}

		ret = write_to_ringbuffer(inst, src_buf, src_size, ring_buffer, wr_ptr);
		if (ret) {
			dev_err(inst->dev->dev, "Write src buf (%u) to ring buffer, fail: %d\n",
				vbuf->vb2_buf.index, ret);
			return ret;
		}

		ret = wave5_vpu_dec_update_bitstream_buffer(inst, src_size);
		if (ret) {
			dev_dbg(inst->dev->dev,
				"update_bitstream_buffer fail: %d for src buf (%u)\n",
				ret, vbuf->vb2_buf.index);
			break;
		}

		vpu_buf->consumed = true;

		/* Don't write buffers passed the last one while draining. */
		if (v4l2_m2m_is_last_draining_src_buf(m2m_ctx, vbuf)) {
			dev_dbg(inst->dev->dev, "last src buffer written to the ring buffer\n");
			break;
		}
	}

	return 0;
}

static void wave5_vpu_dec_buf_queue_src(struct vb2_buffer *vb)
{
	struct vpu_instance *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpu_src_buffer *vpu_buf = wave5_to_vpu_src_buf(vbuf);

	vpu_buf->consumed = false;
	vbuf->sequence = inst->queued_src_buf_num++;

	v4l2_m2m_buf_queue(m2m_ctx, vbuf);
}

static void wave5_vpu_dec_buf_queue_dst(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpu_instance *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;

	vbuf->sequence = inst->queued_dst_buf_num++;

	if (inst->state == VPU_INST_STATE_PIC_RUN) {
		struct vpu_dst_buffer *vpu_buf = wave5_to_vpu_dst_buf(vbuf);
		int ret;

		/*
		 * The buffer is already registered just clear the display flag
		 * to let the firmware know it can be used.
		 */
		vpu_buf->display = false;
		ret = wave5_vpu_dec_clr_disp_flag(inst, vb->index);
		if (ret) {
			dev_dbg(inst->dev->dev,
				"%s: Clearing the display flag of buffer index: %u, fail: %d\n",
				__func__, vb->index, ret);
		}
	}

	if (vb2_is_streaming(vb->vb2_queue) && v4l2_m2m_dst_buf_is_last(m2m_ctx)) {
		unsigned int i;

		for (i = 0; i < vb->num_planes; i++)
			vb2_set_plane_payload(vb, i, 0);

		vbuf->field = V4L2_FIELD_NONE;

		send_eos_event(inst);
		v4l2_m2m_last_buffer_done(m2m_ctx, vbuf);
	} else {
		v4l2_m2m_buf_queue(m2m_ctx, vbuf);
	}
}

static void wave5_vpu_dec_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpu_instance *inst = vb2_get_drv_priv(vb->vb2_queue);

	dev_dbg(inst->dev->dev, "%s: type: %4u index: %4u size: ([0]=%4lu, [1]=%4lu, [2]=%4lu)\n",
		__func__, vb->type, vb->index, vb2_plane_size(&vbuf->vb2_buf, 0),
		vb2_plane_size(&vbuf->vb2_buf, 1), vb2_plane_size(&vbuf->vb2_buf, 2));

	if (vb->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		wave5_vpu_dec_buf_queue_src(vb);
	else if (vb->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE)
		wave5_vpu_dec_buf_queue_dst(vb);
}

static int wave5_vpu_dec_allocate_ring_buffer(struct vpu_instance *inst)
{
	int ret;
	struct vpu_buf *ring_buffer = &inst->bitstream_vbuf;

	ring_buffer->size = ALIGN(inst->src_fmt.plane_fmt[0].sizeimage, 1024) * 4;
	ret = wave5_vdi_allocate_dma_memory(inst->dev, ring_buffer);
	if (ret) {
		dev_dbg(inst->dev->dev, "%s: allocate ring buffer of size %zu fail: %d\n",
			__func__, ring_buffer->size, ret);
		return ret;
	}

	inst->last_rd_ptr = ring_buffer->daddr;

	return 0;
}

static int wave5_vpu_dec_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	int ret = 0;

	dev_dbg(inst->dev->dev, "%s: type: %u\n", __func__, q->type);
	pm_runtime_resume_and_get(inst->dev->dev);

	v4l2_m2m_update_start_streaming_state(m2m_ctx, q);

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE && inst->state == VPU_INST_STATE_NONE) {
		struct dec_open_param open_param;

		memset(&open_param, 0, sizeof(struct dec_open_param));

		ret = wave5_vpu_dec_allocate_ring_buffer(inst);
		if (ret)
			goto return_buffers;

		open_param.bitstream_buffer = inst->bitstream_vbuf.daddr;
		open_param.bitstream_buffer_size = inst->bitstream_vbuf.size;

		ret = wave5_vpu_dec_open(inst, &open_param);
		if (ret) {
			dev_dbg(inst->dev->dev, "%s: decoder opening, fail: %d\n",
				__func__, ret);
			goto free_bitstream_vbuf;
		}

		ret = switch_state(inst, VPU_INST_STATE_OPEN);
		if (ret)
			goto free_bitstream_vbuf;
	} else if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
		struct dec_initial_info *initial_info =
			&inst->codec_info->dec_info.initial_info;

		if (inst->state == VPU_INST_STATE_STOP)
			ret = switch_state(inst, VPU_INST_STATE_INIT_SEQ);
		if (ret)
			goto return_buffers;

		if (inst->state == VPU_INST_STATE_INIT_SEQ &&
		    inst->dev->product_code == WAVE521C_CODE) {
			if (initial_info->luma_bitdepth != 8) {
				dev_info(inst->dev->dev, "%s: no support for %d bit depth",
					 __func__, initial_info->luma_bitdepth);
				ret = -EINVAL;
				goto return_buffers;
			}
		}

	}
	pm_runtime_mark_last_busy(inst->dev->dev);
	pm_runtime_put_autosuspend(inst->dev->dev);
	return ret;

free_bitstream_vbuf:
	wave5_vdi_free_dma_memory(inst->dev, &inst->bitstream_vbuf);
return_buffers:
	wave5_return_bufs(q, VB2_BUF_STATE_QUEUED);
	pm_runtime_put_autosuspend(inst->dev->dev);
	return ret;
}

static int streamoff_output(struct vb2_queue *q)
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	struct vb2_v4l2_buffer *buf;
	int ret;
	dma_addr_t new_rd_ptr;
	struct dec_output_info dec_info;
	unsigned int i;

	for (i = 0; i < v4l2_m2m_num_dst_bufs_ready(m2m_ctx); i++) {
		ret = wave5_vpu_dec_set_disp_flag(inst, i);
		if (ret)
			dev_dbg(inst->dev->dev,
				"%s: Setting display flag of buf index: %u, fail: %d\n",
				__func__, i, ret);
	}

	while ((buf = v4l2_m2m_src_buf_remove(m2m_ctx))) {
		dev_dbg(inst->dev->dev, "%s: (Multiplanar) buf type %4u | index %4u\n",
			__func__, buf->vb2_buf.type, buf->vb2_buf.index);
		v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	}

	while (wave5_vpu_dec_get_output_info(inst, &dec_info) == 0) {
		if (dec_info.index_frame_display >= 0)
			wave5_vpu_dec_set_disp_flag(inst, dec_info.index_frame_display);
	}

	ret = wave5_vpu_flush_instance(inst);
	if (ret)
		return ret;

	/* Reset the ring buffer information */
	new_rd_ptr = wave5_vpu_dec_get_rd_ptr(inst);
	inst->last_rd_ptr = new_rd_ptr;
	inst->codec_info->dec_info.stream_rd_ptr = new_rd_ptr;
	inst->codec_info->dec_info.stream_wr_ptr = new_rd_ptr;

	if (v4l2_m2m_has_stopped(m2m_ctx))
		send_eos_event(inst);

	/* streamoff on output cancels any draining operation */
	inst->eos = false;

	return 0;
}

static int streamoff_capture(struct vb2_queue *q)
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	struct vb2_v4l2_buffer *buf;
	unsigned int i;
	int ret = 0;

	for (i = 0; i < v4l2_m2m_num_dst_bufs_ready(m2m_ctx); i++) {
		ret = wave5_vpu_dec_set_disp_flag(inst, i);
		if (ret)
			dev_dbg(inst->dev->dev,
				"%s: Setting display flag of buf index: %u, fail: %d\n",
				__func__, i, ret);
	}

	while ((buf = v4l2_m2m_dst_buf_remove(m2m_ctx))) {
		u32 plane;

		dev_dbg(inst->dev->dev, "%s: buf type %4u | index %4u\n",
			__func__, buf->vb2_buf.type, buf->vb2_buf.index);

		for (plane = 0; plane < inst->dst_fmt.num_planes; plane++)
			vb2_set_plane_payload(&buf->vb2_buf, plane, 0);

		v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	}

	if (inst->needs_reallocation) {
		wave5_vpu_dec_give_command(inst, DEC_RESET_FRAMEBUF_INFO, NULL);
		inst->needs_reallocation = false;
	}

	if (v4l2_m2m_has_stopped(m2m_ctx)) {
		ret = switch_state(inst, VPU_INST_STATE_INIT_SEQ);
		if (ret)
			return ret;
	}

	return 0;
}

static void wave5_vpu_dec_stop_streaming(struct vb2_queue *q)
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	bool check_cmd = TRUE;

	dev_dbg(inst->dev->dev, "%s: type: %u\n", __func__, q->type);
	pm_runtime_resume_and_get(inst->dev->dev);

	while (check_cmd) {
		struct queue_status_info q_status;
		struct dec_output_info dec_output_info;

		wave5_vpu_dec_give_command(inst, DEC_GET_QUEUE_STATUS, &q_status);

		if (q_status.report_queue_count == 0)
			break;

		if (wave5_vpu_wait_interrupt(inst, VPU_DEC_TIMEOUT) < 0)
			break;

		if (wave5_vpu_dec_get_output_info(inst, &dec_output_info))
			dev_dbg(inst->dev->dev, "there is no output info\n");
	}

	v4l2_m2m_update_stop_streaming_state(m2m_ctx, q);

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		streamoff_output(q);
	else
		streamoff_capture(q);

	pm_runtime_mark_last_busy(inst->dev->dev);
	pm_runtime_put_autosuspend(inst->dev->dev);
}

static const struct vb2_ops wave5_vpu_dec_vb2_ops = {
	.queue_setup = wave5_vpu_dec_queue_setup,
	.buf_queue = wave5_vpu_dec_buf_queue,
	.start_streaming = wave5_vpu_dec_start_streaming,
	.stop_streaming = wave5_vpu_dec_stop_streaming,
};

static void wave5_set_default_format(struct v4l2_pix_format_mplane *src_fmt,
				     struct v4l2_pix_format_mplane *dst_fmt)
{
	src_fmt->pixelformat = dec_fmt_list[VPU_FMT_TYPE_CODEC][0].v4l2_pix_fmt;
	wave5_update_pix_fmt(src_fmt, VPU_FMT_TYPE_CODEC,
			     W5_DEF_DEC_PIC_WIDTH, W5_DEF_DEC_PIC_HEIGHT,
			     &dec_hevc_frmsize);

	dst_fmt->pixelformat = dec_fmt_list[VPU_FMT_TYPE_RAW][0].v4l2_pix_fmt;
	wave5_update_pix_fmt(dst_fmt, VPU_FMT_TYPE_RAW,
			     W5_DEF_DEC_PIC_WIDTH, W5_DEF_DEC_PIC_HEIGHT,
			     &dec_raw_frmsize);
}

static int wave5_vpu_dec_queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	return wave5_vpu_queue_init(priv, src_vq, dst_vq, &wave5_vpu_dec_vb2_ops);
}

static const struct vpu_instance_ops wave5_vpu_dec_inst_ops = {
	.finish_process = wave5_vpu_dec_finish_decode,
};

static int initialize_sequence(struct vpu_instance *inst)
{
	struct dec_initial_info initial_info;
	int ret = 0;

	memset(&initial_info, 0, sizeof(struct dec_initial_info));

	ret = wave5_vpu_dec_issue_seq_init(inst);
	if (ret) {
		dev_dbg(inst->dev->dev, "%s: wave5_vpu_dec_issue_seq_init, fail: %d\n",
			__func__, ret);
		return ret;
	}

	if (wave5_vpu_wait_interrupt(inst, VPU_DEC_TIMEOUT) < 0)
		dev_dbg(inst->dev->dev, "%s: failed to call vpu_wait_interrupt()\n", __func__);

	ret = wave5_vpu_dec_complete_seq_init(inst, &initial_info);
	if (ret) {
		dev_dbg(inst->dev->dev, "%s: vpu_dec_complete_seq_init, fail: %d, reason: %u\n",
			__func__, ret, initial_info.seq_init_err_reason);
		wave5_handle_src_buffer(inst, initial_info.rd_ptr);
		return ret;
	}

	handle_dynamic_resolution_change(inst);

	return 0;
}

static bool wave5_is_draining_or_eos(struct vpu_instance *inst)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;

	lockdep_assert_held(&inst->state_spinlock);
	return m2m_ctx->is_draining || inst->eos;
}

static void wave5_vpu_dec_device_run(void *priv)
{
	struct vpu_instance *inst = priv;
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	struct queue_status_info q_status;
	u32 fail_res = 0;
	int ret = 0;

	dev_dbg(inst->dev->dev, "%s: Fill the ring buffer with new bitstream data", __func__);
	pm_runtime_resume_and_get(inst->dev->dev);
	ret = fill_ringbuffer(inst);
	if (ret) {
		dev_warn(inst->dev->dev, "Filling ring buffer failed\n");
		goto finish_job_and_return;
	}

	switch (inst->state) {
	case VPU_INST_STATE_OPEN:
		ret = initialize_sequence(inst);
		if (ret) {
			unsigned long flags;

			spin_lock_irqsave(&inst->state_spinlock, flags);
			if (wave5_is_draining_or_eos(inst) &&
			    wave5_last_src_buffer_consumed(m2m_ctx)) {
				struct vb2_queue *dst_vq = v4l2_m2m_get_dst_vq(m2m_ctx);

				switch_state(inst, VPU_INST_STATE_STOP);

				if (vb2_is_streaming(dst_vq))
					send_eos_event(inst);
				else
					handle_dynamic_resolution_change(inst);

				flag_last_buffer_done(inst);
			}
			spin_unlock_irqrestore(&inst->state_spinlock, flags);
		} else {
			switch_state(inst, VPU_INST_STATE_INIT_SEQ);
		}

		break;

	case VPU_INST_STATE_INIT_SEQ:
		/*
		 * Do this early, preparing the fb can trigger an IRQ before
		 * we had a chance to switch, which leads to an invalid state
		 * change.
		 */
		switch_state(inst, VPU_INST_STATE_PIC_RUN);

		/*
		 * During DRC, the picture decoding remains pending, so just leave the job
		 * active until this decode operation completes.
		 */
		wave5_vpu_dec_give_command(inst, DEC_GET_QUEUE_STATUS, &q_status);

		/*
		 * The sequence must be analyzed first to calculate the proper
		 * size of the auxiliary buffers.
		 */
		ret = wave5_prepare_fb(inst);
		if (ret) {
			dev_warn(inst->dev->dev, "Framebuffer preparation, fail: %d\n", ret);
			switch_state(inst, VPU_INST_STATE_STOP);
			break;
		}

		if (q_status.instance_queue_count) {
			dev_dbg(inst->dev->dev, "%s: leave with active job", __func__);
			return;
		}

		fallthrough;
	case VPU_INST_STATE_PIC_RUN:
		ret = start_decode(inst, &fail_res);
		if (ret) {
			dev_err(inst->dev->dev,
				"Frame decoding on m2m context (%p), fail: %d (result: %d)\n",
				m2m_ctx, ret, fail_res);
			break;
		}
		/* Return so that we leave this job active */
		dev_dbg(inst->dev->dev, "%s: leave with active job", __func__);
		return;
	default:
		WARN(1, "Execution of a job in state %s illegal.\n", state_to_str(inst->state));
		break;
	}

finish_job_and_return:
	dev_dbg(inst->dev->dev, "%s: leave and finish job", __func__);
	pm_runtime_mark_last_busy(inst->dev->dev);
	pm_runtime_put_autosuspend(inst->dev->dev);
	v4l2_m2m_job_finish(inst->v4l2_m2m_dev, m2m_ctx);
}

static void wave5_vpu_dec_job_abort(void *priv)
{
	struct vpu_instance *inst = priv;
	int ret;

	ret = switch_state(inst, VPU_INST_STATE_STOP);
	if (ret)
		return;

	ret = wave5_vpu_dec_set_eos_on_firmware(inst);
	if (ret)
		dev_warn(inst->dev->dev,
			 "Setting EOS for the bitstream, fail: %d\n", ret);
}

static int wave5_vpu_dec_job_ready(void *priv)
{
	struct vpu_instance *inst = priv;
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&inst->state_spinlock, flags);

	switch (inst->state) {
	case VPU_INST_STATE_NONE:
		dev_dbg(inst->dev->dev, "Decoder must be open to start queueing M2M jobs!\n");
		break;
	case VPU_INST_STATE_OPEN:
		if (wave5_is_draining_or_eos(inst) || !v4l2_m2m_has_stopped(m2m_ctx) ||
		    v4l2_m2m_num_src_bufs_ready(m2m_ctx) > 0) {
			ret = 1;
			break;
		}

		dev_dbg(inst->dev->dev,
			"Decoder must be draining or >= 1 OUTPUT queue buffer must be queued!\n");
		break;
	case VPU_INST_STATE_INIT_SEQ:
	case VPU_INST_STATE_PIC_RUN:
		if (!m2m_ctx->cap_q_ctx.q.streaming) {
			dev_dbg(inst->dev->dev, "CAPTURE queue must be streaming to queue jobs!\n");
			break;
		} else if (v4l2_m2m_num_dst_bufs_ready(m2m_ctx) < (inst->fbc_buf_count - 1)) {
			dev_dbg(inst->dev->dev,
				"No capture buffer ready to decode!\n");
			break;
		} else if (!wave5_is_draining_or_eos(inst) &&
			   !v4l2_m2m_num_src_bufs_ready(m2m_ctx)) {
			dev_dbg(inst->dev->dev,
				"No bitstream data to decode!\n");
			break;
		}
		ret = 1;
		break;
	case VPU_INST_STATE_STOP:
		dev_dbg(inst->dev->dev, "Decoder is stopped, not running.\n");
		break;
	}

	spin_unlock_irqrestore(&inst->state_spinlock, flags);

	return ret;
}

static const struct v4l2_m2m_ops wave5_vpu_dec_m2m_ops = {
	.device_run = wave5_vpu_dec_device_run,
	.job_abort = wave5_vpu_dec_job_abort,
	.job_ready = wave5_vpu_dec_job_ready,
};

static int wave5_vpu_open_dec(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct vpu_device *dev = video_drvdata(filp);
	struct vpu_instance *inst = NULL;
	struct v4l2_m2m_ctx *m2m_ctx;
	int ret = 0;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;

	inst->dev = dev;
	inst->type = VPU_INST_TYPE_DEC;
	inst->ops = &wave5_vpu_dec_inst_ops;

	spin_lock_init(&inst->state_spinlock);

	inst->codec_info = kzalloc(sizeof(*inst->codec_info), GFP_KERNEL);
	if (!inst->codec_info)
		return -ENOMEM;

	v4l2_fh_init(&inst->v4l2_fh, vdev);
	filp->private_data = &inst->v4l2_fh;
	v4l2_fh_add(&inst->v4l2_fh);

	INIT_LIST_HEAD(&inst->list);

	inst->v4l2_m2m_dev = inst->dev->v4l2_m2m_dec_dev;
	inst->v4l2_fh.m2m_ctx =
		v4l2_m2m_ctx_init(inst->v4l2_m2m_dev, inst, wave5_vpu_dec_queue_init);
	if (IS_ERR(inst->v4l2_fh.m2m_ctx)) {
		ret = PTR_ERR(inst->v4l2_fh.m2m_ctx);
		goto cleanup_inst;
	}
	m2m_ctx = inst->v4l2_fh.m2m_ctx;

	v4l2_m2m_set_src_buffered(m2m_ctx, true);
	v4l2_m2m_set_dst_buffered(m2m_ctx, true);
	/*
	 * We use the M2M job queue to ensure synchronization of steps where
	 * needed, as IOCTLs can occur at anytime and we need to run commands on
	 * the firmware in a specified order.
	 * In order to initialize the sequence on the firmware within an M2M
	 * job, the M2M framework needs to be able to queue jobs before
	 * the CAPTURE queue has been started, because we need the results of the
	 * initialization to properly prepare the CAPTURE queue with the correct
	 * amount of buffers.
	 * By setting ignore_cap_streaming to true the m2m framework will call
	 * job_ready as soon as the OUTPUT queue is streaming, instead of
	 * waiting until both the CAPTURE and OUTPUT queues are streaming.
	 */
	m2m_ctx->ignore_cap_streaming = true;

	v4l2_ctrl_handler_init(&inst->v4l2_ctrl_hdl, 10);
	v4l2_ctrl_new_std(&inst->v4l2_ctrl_hdl, NULL,
			  V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 1, 32, 1, 1);

	if (inst->v4l2_ctrl_hdl.error) {
		ret = -ENODEV;
		goto cleanup_inst;
	}

	inst->v4l2_fh.ctrl_handler = &inst->v4l2_ctrl_hdl;
	v4l2_ctrl_handler_setup(&inst->v4l2_ctrl_hdl);

	wave5_set_default_format(&inst->src_fmt, &inst->dst_fmt);
	inst->colorspace = V4L2_COLORSPACE_REC709;
	inst->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	inst->quantization = V4L2_QUANTIZATION_DEFAULT;
	inst->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	init_completion(&inst->irq_done);

	inst->id = ida_alloc(&inst->dev->inst_ida, GFP_KERNEL);
	if (inst->id < 0) {
		dev_warn(inst->dev->dev, "Allocating instance ID, fail: %d\n", inst->id);
		ret = inst->id;
		goto cleanup_inst;
	}

	/*
	 * For Wave515 SRAM memory was already allocated
	 * at wave5_vpu_dec_register_device()
	 */
	if (inst->dev->product_code != WAVE515_CODE)
		wave5_vdi_allocate_sram(inst->dev);

	ret = mutex_lock_interruptible(&dev->dev_lock);
	if (ret)
		goto cleanup_inst;

	if (list_empty(&dev->instances))
		pm_runtime_use_autosuspend(inst->dev->dev);

	list_add_tail(&inst->list, &dev->instances);

	mutex_unlock(&dev->dev_lock);

	return 0;

cleanup_inst:
	wave5_cleanup_instance(inst);
	return ret;
}

static int wave5_vpu_dec_release(struct file *filp)
{
	return wave5_vpu_release_device(filp, wave5_vpu_dec_close, "decoder");
}

static const struct v4l2_file_operations wave5_vpu_dec_fops = {
	.owner = THIS_MODULE,
	.open = wave5_vpu_open_dec,
	.release = wave5_vpu_dec_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = v4l2_m2m_fop_poll,
	.mmap = v4l2_m2m_fop_mmap,
};

int wave5_vpu_dec_register_device(struct vpu_device *dev)
{
	struct video_device *vdev_dec;
	int ret;

	/*
	 * Secondary AXI setup for Wave515 is done by INIT_VPU command,
	 * i.e. wave5_vpu_init(), that's why we allocate SRAM memory early.
	 */
	if (dev->product_code == WAVE515_CODE)
		wave5_vdi_allocate_sram(dev);

	vdev_dec = devm_kzalloc(dev->v4l2_dev.dev, sizeof(*vdev_dec), GFP_KERNEL);
	if (!vdev_dec)
		return -ENOMEM;

	dev->v4l2_m2m_dec_dev = v4l2_m2m_init(&wave5_vpu_dec_m2m_ops);
	if (IS_ERR(dev->v4l2_m2m_dec_dev)) {
		ret = PTR_ERR(dev->v4l2_m2m_dec_dev);
		dev_err(dev->dev, "v4l2_m2m_init, fail: %d\n", ret);
		return -EINVAL;
	}

	dev->video_dev_dec = vdev_dec;

	strscpy(vdev_dec->name, VPU_DEC_DEV_NAME, sizeof(vdev_dec->name));
	vdev_dec->fops = &wave5_vpu_dec_fops;
	vdev_dec->ioctl_ops = &wave5_vpu_dec_ioctl_ops;
	vdev_dec->release = video_device_release_empty;
	vdev_dec->v4l2_dev = &dev->v4l2_dev;
	vdev_dec->vfl_dir = VFL_DIR_M2M;
	vdev_dec->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	vdev_dec->lock = &dev->dev_lock;

	ret = video_register_device(vdev_dec, VFL_TYPE_VIDEO, -1);
	if (ret)
		return ret;

	video_set_drvdata(vdev_dec, dev);

	return 0;
}

void wave5_vpu_dec_unregister_device(struct vpu_device *dev)
{
	/*
	 * Here is a freeing pair for Wave515 SRAM memory allocation
	 * happened at wave5_vpu_dec_register_device().
	 */
	if (dev->product_code == WAVE515_CODE)
		wave5_vdi_free_sram(dev);

	video_unregister_device(dev->video_dev_dec);
	if (dev->v4l2_m2m_dec_dev)
		v4l2_m2m_release(dev->v4l2_m2m_dec_dev);
}
