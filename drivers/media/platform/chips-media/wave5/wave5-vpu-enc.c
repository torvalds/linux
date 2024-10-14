// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Wave5 series multi-standard codec IP - encoder interface
 *
 * Copyright (C) 2021-2023 CHIPS&MEDIA INC
 */

#include <linux/pm_runtime.h>
#include "wave5-helper.h"

#define VPU_ENC_DEV_NAME "C&M Wave5 VPU encoder"
#define VPU_ENC_DRV_NAME "wave5-enc"

static const struct v4l2_frmsize_stepwise enc_frmsize[FMT_TYPES] = {
	[VPU_FMT_TYPE_CODEC] = {
		.min_width = W5_MIN_ENC_PIC_WIDTH,
		.max_width = W5_MAX_ENC_PIC_WIDTH,
		.step_width = W5_ENC_CODEC_STEP_WIDTH,
		.min_height = W5_MIN_ENC_PIC_HEIGHT,
		.max_height = W5_MAX_ENC_PIC_HEIGHT,
		.step_height = W5_ENC_CODEC_STEP_HEIGHT,
	},
	[VPU_FMT_TYPE_RAW] = {
		.min_width = W5_MIN_ENC_PIC_WIDTH,
		.max_width = W5_MAX_ENC_PIC_WIDTH,
		.step_width = W5_ENC_RAW_STEP_WIDTH,
		.min_height = W5_MIN_ENC_PIC_HEIGHT,
		.max_height = W5_MAX_ENC_PIC_HEIGHT,
		.step_height = W5_ENC_RAW_STEP_HEIGHT,
	},
};

static const struct vpu_format enc_fmt_list[FMT_TYPES][MAX_FMTS] = {
	[VPU_FMT_TYPE_CODEC] = {
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_HEVC,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_CODEC],
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_H264,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_CODEC],
		},
	},
	[VPU_FMT_TYPE_RAW] = {
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_YUV420,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_RAW],
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV12,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_RAW],
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV21,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_RAW],
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_YUV420M,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_RAW],
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV12M,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_RAW],
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV21M,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_RAW],
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_YUV422P,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_RAW],
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV16,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_RAW],
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV61,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_RAW],
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_YUV422M,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_RAW],
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV16M,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_RAW],
		},
		{
			.v4l2_pix_fmt = V4L2_PIX_FMT_NV61M,
			.v4l2_frmsize = &enc_frmsize[VPU_FMT_TYPE_RAW],
		},
	}
};

static int switch_state(struct vpu_instance *inst, enum vpu_instance_state state)
{
	switch (state) {
	case VPU_INST_STATE_NONE:
		goto invalid_state_switch;
	case VPU_INST_STATE_OPEN:
		if (inst->state != VPU_INST_STATE_NONE)
			goto invalid_state_switch;
		break;
	case VPU_INST_STATE_INIT_SEQ:
		if (inst->state != VPU_INST_STATE_OPEN && inst->state != VPU_INST_STATE_STOP)
			goto invalid_state_switch;
		break;
	case VPU_INST_STATE_PIC_RUN:
		if (inst->state != VPU_INST_STATE_INIT_SEQ)
			goto invalid_state_switch;
		break;
	case VPU_INST_STATE_STOP:
		break;
	}

	dev_dbg(inst->dev->dev, "Switch state from %s to %s.\n",
		state_to_str(inst->state), state_to_str(state));
	inst->state = state;
	return 0;

invalid_state_switch:
	WARN(1, "Invalid state switch from %s to %s.\n",
	     state_to_str(inst->state), state_to_str(state));
	return -EINVAL;
}

static int start_encode(struct vpu_instance *inst, u32 *fail_res)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	int ret;
	struct vb2_v4l2_buffer *src_buf;
	struct vb2_v4l2_buffer *dst_buf;
	struct frame_buffer frame_buf;
	struct enc_param pic_param;
	const struct v4l2_format_info *info;
	u32 stride = inst->src_fmt.plane_fmt[0].bytesperline;
	u32 luma_size = 0;
	u32 chroma_size = 0;

	memset(&pic_param, 0, sizeof(struct enc_param));
	memset(&frame_buf, 0, sizeof(struct frame_buffer));

	info = v4l2_format_info(inst->src_fmt.pixelformat);
	if (!info)
		return -EINVAL;

	if (info->mem_planes == 1) {
		luma_size = stride * inst->dst_fmt.height;
		chroma_size = luma_size / (info->hdiv * info->vdiv);
	} else {
		luma_size = inst->src_fmt.plane_fmt[0].sizeimage;
		chroma_size = inst->src_fmt.plane_fmt[1].sizeimage;
	}

	dst_buf = v4l2_m2m_next_dst_buf(m2m_ctx);
	if (!dst_buf) {
		dev_dbg(inst->dev->dev, "%s: No destination buffer found\n", __func__);
		return -EAGAIN;
	}

	pic_param.pic_stream_buffer_addr =
		vb2_dma_contig_plane_dma_addr(&dst_buf->vb2_buf, 0);
	pic_param.pic_stream_buffer_size =
		vb2_plane_size(&dst_buf->vb2_buf, 0);

	src_buf = v4l2_m2m_next_src_buf(m2m_ctx);
	if (!src_buf) {
		dev_dbg(inst->dev->dev, "%s: No source buffer found\n", __func__);
		if (m2m_ctx->is_draining)
			pic_param.src_end_flag = 1;
		else
			return -EAGAIN;
	} else {
		if (inst->src_fmt.num_planes == 1) {
			frame_buf.buf_y =
				vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
			frame_buf.buf_cb = frame_buf.buf_y + luma_size;
			frame_buf.buf_cr = frame_buf.buf_cb + chroma_size;
		} else if (inst->src_fmt.num_planes == 2) {
			frame_buf.buf_y =
				vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
			frame_buf.buf_cb =
				vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 1);
			frame_buf.buf_cr = frame_buf.buf_cb + chroma_size;
		} else if (inst->src_fmt.num_planes == 3) {
			frame_buf.buf_y =
				vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 0);
			frame_buf.buf_cb =
				vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 1);
			frame_buf.buf_cr =
				vb2_dma_contig_plane_dma_addr(&src_buf->vb2_buf, 2);
		}
		frame_buf.stride = stride;
		pic_param.src_idx = src_buf->vb2_buf.index;
	}

	pic_param.source_frame = &frame_buf;
	pic_param.code_option.implicit_header_encode = 1;
	pic_param.code_option.encode_aud = inst->encode_aud;
	ret = wave5_vpu_enc_start_one_frame(inst, &pic_param, fail_res);
	if (ret) {
		if (*fail_res == WAVE5_SYSERR_QUEUEING_FAIL)
			return -EINVAL;

		dev_dbg(inst->dev->dev, "%s: wave5_vpu_enc_start_one_frame fail: %d\n",
			__func__, ret);
		src_buf = v4l2_m2m_src_buf_remove(m2m_ctx);
		if (!src_buf) {
			dev_dbg(inst->dev->dev,
				"%s: Removing src buf failed, the queue is empty\n",
				__func__);
			return -EINVAL;
		}
		dst_buf = v4l2_m2m_dst_buf_remove(m2m_ctx);
		if (!dst_buf) {
			dev_dbg(inst->dev->dev,
				"%s: Removing dst buf failed, the queue is empty\n",
				__func__);
			return -EINVAL;
		}
		switch_state(inst, VPU_INST_STATE_STOP);
		dst_buf->vb2_buf.timestamp = src_buf->vb2_buf.timestamp;
		v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_ERROR);
		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_ERROR);
	} else {
		dev_dbg(inst->dev->dev, "%s: wave5_vpu_enc_start_one_frame success\n",
			__func__);
		/*
		 * Remove the source buffer from the ready-queue now and finish
		 * it in the videobuf2 framework once the index is returned by the
		 * firmware in finish_encode
		 */
		if (src_buf)
			v4l2_m2m_src_buf_remove_by_idx(m2m_ctx, src_buf->vb2_buf.index);
	}

	return 0;
}

static void wave5_vpu_enc_finish_encode(struct vpu_instance *inst)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	int ret;
	struct enc_output_info enc_output_info;
	struct vb2_v4l2_buffer *src_buf = NULL;
	struct vb2_v4l2_buffer *dst_buf = NULL;

	ret = wave5_vpu_enc_get_output_info(inst, &enc_output_info);
	if (ret) {
		dev_dbg(inst->dev->dev,
			"%s: vpu_enc_get_output_info fail: %d  reason: %u | info: %u\n",
			__func__, ret, enc_output_info.error_reason, enc_output_info.warn_info);
		return;
	}

	dev_dbg(inst->dev->dev,
		"%s: pic_type %i recon_idx %i src_idx %i pic_byte %u pts %llu\n",
		__func__,  enc_output_info.pic_type, enc_output_info.recon_frame_index,
		enc_output_info.enc_src_idx, enc_output_info.enc_pic_byte, enc_output_info.pts);

	/*
	 * The source buffer will not be found in the ready-queue as it has been
	 * dropped after sending of the encode firmware command, locate it in
	 * the videobuf2 queue directly
	 */
	if (enc_output_info.enc_src_idx >= 0) {
		struct vb2_buffer *vb = vb2_get_buffer(v4l2_m2m_get_src_vq(m2m_ctx),
						       enc_output_info.enc_src_idx);
		if (vb->state != VB2_BUF_STATE_ACTIVE)
			dev_warn(inst->dev->dev,
				 "%s: encoded buffer (%d) was not in ready queue %i.",
				 __func__, enc_output_info.enc_src_idx, vb->state);
		else
			src_buf = to_vb2_v4l2_buffer(vb);

		if (src_buf) {
			inst->timestamp = src_buf->vb2_buf.timestamp;
			v4l2_m2m_buf_done(src_buf, VB2_BUF_STATE_DONE);
		} else {
			dev_warn(inst->dev->dev, "%s: no source buffer with index: %d found\n",
				 __func__, enc_output_info.enc_src_idx);
		}
	}

	dst_buf = v4l2_m2m_dst_buf_remove(m2m_ctx);
	if (enc_output_info.recon_frame_index == RECON_IDX_FLAG_ENC_END) {
		static const struct v4l2_event vpu_event_eos = {
			.type = V4L2_EVENT_EOS
		};

		if (!WARN_ON(!dst_buf)) {
			vb2_set_plane_payload(&dst_buf->vb2_buf, 0, 0);
			dst_buf->field = V4L2_FIELD_NONE;
			v4l2_m2m_last_buffer_done(m2m_ctx, dst_buf);
		}

		v4l2_event_queue_fh(&inst->v4l2_fh, &vpu_event_eos);

		v4l2_m2m_job_finish(inst->v4l2_m2m_dev, m2m_ctx);
	} else {
		if (!dst_buf) {
			dev_warn(inst->dev->dev, "No bitstream buffer.");
			v4l2_m2m_job_finish(inst->v4l2_m2m_dev, m2m_ctx);
			return;
		}

		vb2_set_plane_payload(&dst_buf->vb2_buf, 0, enc_output_info.bitstream_size);

		dst_buf->vb2_buf.timestamp = inst->timestamp;
		dst_buf->field = V4L2_FIELD_NONE;
		if (enc_output_info.pic_type == PIC_TYPE_I) {
			if (enc_output_info.enc_vcl_nut == 19 ||
			    enc_output_info.enc_vcl_nut == 20)
				dst_buf->flags |= V4L2_BUF_FLAG_KEYFRAME;
			else
				dst_buf->flags |= V4L2_BUF_FLAG_PFRAME;
		} else if (enc_output_info.pic_type == PIC_TYPE_P) {
			dst_buf->flags |= V4L2_BUF_FLAG_PFRAME;
		} else if (enc_output_info.pic_type == PIC_TYPE_B) {
			dst_buf->flags |= V4L2_BUF_FLAG_BFRAME;
		}

		v4l2_m2m_buf_done(dst_buf, VB2_BUF_STATE_DONE);

		dev_dbg(inst->dev->dev, "%s: frame_cycle %8u\n",
			__func__, enc_output_info.frame_cycle);

		v4l2_m2m_job_finish(inst->v4l2_m2m_dev, m2m_ctx);
	}
}

static int wave5_vpu_enc_querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	strscpy(cap->driver, VPU_ENC_DRV_NAME, sizeof(cap->driver));
	strscpy(cap->card, VPU_ENC_DRV_NAME, sizeof(cap->card));

	return 0;
}

static int wave5_vpu_enc_enum_framesizes(struct file *f, void *fh, struct v4l2_frmsizeenum *fsize)
{
	const struct vpu_format *vpu_fmt;

	if (fsize->index)
		return -EINVAL;

	vpu_fmt = wave5_find_vpu_fmt(fsize->pixel_format, enc_fmt_list[VPU_FMT_TYPE_CODEC]);
	if (!vpu_fmt) {
		vpu_fmt = wave5_find_vpu_fmt(fsize->pixel_format, enc_fmt_list[VPU_FMT_TYPE_RAW]);
		if (!vpu_fmt)
			return -EINVAL;
	}

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise = enc_frmsize[VPU_FMT_TYPE_CODEC];

	return 0;
}

static int wave5_vpu_enc_enum_fmt_cap(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	const struct vpu_format *vpu_fmt;

	dev_dbg(inst->dev->dev, "%s: index: %u\n", __func__, f->index);

	vpu_fmt = wave5_find_vpu_fmt_by_idx(f->index, enc_fmt_list[VPU_FMT_TYPE_CODEC]);
	if (!vpu_fmt)
		return -EINVAL;

	f->pixelformat = vpu_fmt->v4l2_pix_fmt;
	f->flags = 0;

	return 0;
}

static int wave5_vpu_enc_try_fmt_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	const struct v4l2_frmsize_stepwise *frmsize;
	const struct vpu_format *vpu_fmt;
	int width, height;

	dev_dbg(inst->dev->dev, "%s: fourcc: %u width: %u height: %u num_planes: %u field: %u\n",
		__func__, f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width, f->fmt.pix_mp.height,
		f->fmt.pix_mp.num_planes, f->fmt.pix_mp.field);

	vpu_fmt = wave5_find_vpu_fmt(f->fmt.pix_mp.pixelformat, enc_fmt_list[VPU_FMT_TYPE_CODEC]);
	if (!vpu_fmt) {
		width = inst->dst_fmt.width;
		height = inst->dst_fmt.height;
		f->fmt.pix_mp.pixelformat = inst->dst_fmt.pixelformat;
		frmsize = &enc_frmsize[VPU_FMT_TYPE_CODEC];
	} else {
		width = f->fmt.pix_mp.width;
		height = f->fmt.pix_mp.height;
		f->fmt.pix_mp.pixelformat = vpu_fmt->v4l2_pix_fmt;
		frmsize = vpu_fmt->v4l2_frmsize;
	}

	wave5_update_pix_fmt(&f->fmt.pix_mp, VPU_FMT_TYPE_CODEC,
			     width, height, frmsize);
	f->fmt.pix_mp.colorspace = inst->colorspace;
	f->fmt.pix_mp.ycbcr_enc = inst->ycbcr_enc;
	f->fmt.pix_mp.quantization = inst->quantization;
	f->fmt.pix_mp.xfer_func = inst->xfer_func;

	return 0;
}

static int wave5_vpu_enc_s_fmt_cap(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	int i, ret;

	dev_dbg(inst->dev->dev, "%s: fourcc: %u width: %u height: %u num_planes: %u field: %u\n",
		__func__, f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width, f->fmt.pix_mp.height,
		f->fmt.pix_mp.num_planes, f->fmt.pix_mp.field);

	ret = wave5_vpu_enc_try_fmt_cap(file, fh, f);
	if (ret)
		return ret;

	inst->std = wave5_to_vpu_std(f->fmt.pix_mp.pixelformat, inst->type);
	if (inst->std == STD_UNKNOWN) {
		dev_warn(inst->dev->dev, "unsupported pixelformat: %.4s\n",
			 (char *)&f->fmt.pix_mp.pixelformat);
		return -EINVAL;
	}

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

	return 0;
}

static int wave5_vpu_enc_g_fmt_cap(struct file *file, void *fh, struct v4l2_format *f)
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

static int wave5_vpu_enc_enum_fmt_out(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	const struct vpu_format *vpu_fmt;

	dev_dbg(inst->dev->dev, "%s: index: %u\n", __func__, f->index);

	vpu_fmt = wave5_find_vpu_fmt_by_idx(f->index, enc_fmt_list[VPU_FMT_TYPE_RAW]);
	if (!vpu_fmt)
		return -EINVAL;

	f->pixelformat = vpu_fmt->v4l2_pix_fmt;
	f->flags = 0;

	return 0;
}

static int wave5_vpu_enc_try_fmt_out(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	const struct v4l2_frmsize_stepwise *frmsize;
	const struct vpu_format *vpu_fmt;
	int width, height;

	dev_dbg(inst->dev->dev, "%s: fourcc: %u width: %u height: %u num_planes: %u field: %u\n",
		__func__, f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width, f->fmt.pix_mp.height,
		f->fmt.pix_mp.num_planes, f->fmt.pix_mp.field);

	vpu_fmt = wave5_find_vpu_fmt(f->fmt.pix_mp.pixelformat, enc_fmt_list[VPU_FMT_TYPE_RAW]);
	if (!vpu_fmt) {
		width = inst->src_fmt.width;
		height = inst->src_fmt.height;
		f->fmt.pix_mp.pixelformat = inst->src_fmt.pixelformat;
		frmsize = &enc_frmsize[VPU_FMT_TYPE_RAW];
	} else {
		width = f->fmt.pix_mp.width;
		height = f->fmt.pix_mp.height;
		f->fmt.pix_mp.pixelformat = vpu_fmt->v4l2_pix_fmt;
		frmsize = vpu_fmt->v4l2_frmsize;
	}

	wave5_update_pix_fmt(&f->fmt.pix_mp, VPU_FMT_TYPE_RAW,
			     width, height, frmsize);
	return 0;
}

static int wave5_vpu_enc_s_fmt_out(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	const struct vpu_format *vpu_fmt;
	const struct v4l2_format_info *info;
	int i, ret;

	dev_dbg(inst->dev->dev, "%s: fourcc: %u width: %u height: %u num_planes: %u field: %u\n",
		__func__, f->fmt.pix_mp.pixelformat, f->fmt.pix_mp.width, f->fmt.pix_mp.height,
		f->fmt.pix_mp.num_planes, f->fmt.pix_mp.field);

	ret = wave5_vpu_enc_try_fmt_out(file, fh, f);
	if (ret)
		return ret;

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

	info = v4l2_format_info(inst->src_fmt.pixelformat);
	if (!info)
		return -EINVAL;

	inst->cbcr_interleave = (info->comp_planes == 2) ? true : false;

	switch (inst->src_fmt.pixelformat) {
	case V4L2_PIX_FMT_NV21:
	case V4L2_PIX_FMT_NV21M:
	case V4L2_PIX_FMT_NV61:
	case V4L2_PIX_FMT_NV61M:
		inst->nv21 = true;
		break;
	default:
		inst->nv21 = false;
	}

	inst->colorspace = f->fmt.pix_mp.colorspace;
	inst->ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
	inst->quantization = f->fmt.pix_mp.quantization;
	inst->xfer_func = f->fmt.pix_mp.xfer_func;

	vpu_fmt = wave5_find_vpu_fmt(inst->dst_fmt.pixelformat, enc_fmt_list[VPU_FMT_TYPE_CODEC]);
	if (!vpu_fmt)
		return -EINVAL;

	wave5_update_pix_fmt(&inst->dst_fmt, VPU_FMT_TYPE_CODEC,
			     f->fmt.pix_mp.width, f->fmt.pix_mp.height,
			     vpu_fmt->v4l2_frmsize);
	inst->conf_win.width = inst->dst_fmt.width;
	inst->conf_win.height = inst->dst_fmt.height;

	return 0;
}

static int wave5_vpu_enc_g_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);

	dev_dbg(inst->dev->dev, "%s: type: %u | target: %u\n", __func__, s->type, s->target);

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;
	switch (s->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = inst->dst_fmt.width;
		s->r.height = inst->dst_fmt.height;
		break;
	case V4L2_SEL_TGT_CROP:
		s->r.left = 0;
		s->r.top = 0;
		s->r.width = inst->conf_win.width;
		s->r.height = inst->conf_win.height;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wave5_vpu_enc_s_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);

	if (s->type != V4L2_BUF_TYPE_VIDEO_OUTPUT)
		return -EINVAL;

	if (s->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	dev_dbg(inst->dev->dev, "%s: V4L2_SEL_TGT_CROP width: %u | height: %u\n",
		__func__, s->r.width, s->r.height);

	s->r.left = 0;
	s->r.top = 0;
	s->r.width = min(s->r.width, inst->dst_fmt.width);
	s->r.height = min(s->r.height, inst->dst_fmt.height);

	inst->conf_win = s->r;

	return 0;
}

static int wave5_vpu_enc_encoder_cmd(struct file *file, void *fh, struct v4l2_encoder_cmd *ec)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	int ret;

	ret = v4l2_m2m_ioctl_try_encoder_cmd(file, fh, ec);
	if (ret)
		return ret;

	if (!wave5_vpu_both_queues_are_streaming(inst))
		return 0;

	switch (ec->cmd) {
	case V4L2_ENC_CMD_STOP:
		if (m2m_ctx->is_draining)
			return -EBUSY;

		if (m2m_ctx->has_stopped)
			return 0;

		m2m_ctx->last_src_buf = v4l2_m2m_last_src_buf(m2m_ctx);
		m2m_ctx->is_draining = true;
		break;
	case V4L2_ENC_CMD_START:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wave5_vpu_enc_g_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);

	dev_dbg(inst->dev->dev, "%s: type: %u\n", __func__, a->type);

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT && a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	a->parm.output.timeperframe.numerator = 1;
	a->parm.output.timeperframe.denominator = inst->frame_rate;

	dev_dbg(inst->dev->dev, "%s: numerator: %u | denominator: %u\n",
		__func__, a->parm.output.timeperframe.numerator,
		a->parm.output.timeperframe.denominator);

	return 0;
}

static int wave5_vpu_enc_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);

	dev_dbg(inst->dev->dev, "%s: type: %u\n", __func__, a->type);

	if (a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT && a->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		return -EINVAL;

	a->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	if (a->parm.output.timeperframe.denominator && a->parm.output.timeperframe.numerator) {
		inst->frame_rate = a->parm.output.timeperframe.denominator /
				   a->parm.output.timeperframe.numerator;
	} else {
		a->parm.output.timeperframe.numerator = 1;
		a->parm.output.timeperframe.denominator = inst->frame_rate;
	}

	dev_dbg(inst->dev->dev, "%s: numerator: %u | denominator: %u\n",
		__func__, a->parm.output.timeperframe.numerator,
		a->parm.output.timeperframe.denominator);

	return 0;
}

static const struct v4l2_ioctl_ops wave5_vpu_enc_ioctl_ops = {
	.vidioc_querycap = wave5_vpu_enc_querycap,
	.vidioc_enum_framesizes = wave5_vpu_enc_enum_framesizes,

	.vidioc_enum_fmt_vid_cap	= wave5_vpu_enc_enum_fmt_cap,
	.vidioc_s_fmt_vid_cap_mplane = wave5_vpu_enc_s_fmt_cap,
	.vidioc_g_fmt_vid_cap_mplane = wave5_vpu_enc_g_fmt_cap,
	.vidioc_try_fmt_vid_cap_mplane = wave5_vpu_enc_try_fmt_cap,

	.vidioc_enum_fmt_vid_out	= wave5_vpu_enc_enum_fmt_out,
	.vidioc_s_fmt_vid_out_mplane = wave5_vpu_enc_s_fmt_out,
	.vidioc_g_fmt_vid_out_mplane = wave5_vpu_g_fmt_out,
	.vidioc_try_fmt_vid_out_mplane = wave5_vpu_enc_try_fmt_out,

	.vidioc_g_selection = wave5_vpu_enc_g_selection,
	.vidioc_s_selection = wave5_vpu_enc_s_selection,

	.vidioc_g_parm = wave5_vpu_enc_g_parm,
	.vidioc_s_parm = wave5_vpu_enc_s_parm,

	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf,
	.vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_prepare_buf = v4l2_m2m_ioctl_prepare_buf,
	.vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf,
	.vidioc_streamon = v4l2_m2m_ioctl_streamon,
	.vidioc_streamoff = v4l2_m2m_ioctl_streamoff,

	.vidioc_try_encoder_cmd = v4l2_m2m_ioctl_try_encoder_cmd,
	.vidioc_encoder_cmd = wave5_vpu_enc_encoder_cmd,

	.vidioc_subscribe_event = wave5_vpu_subscribe_event,
	.vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};

static int wave5_vpu_enc_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct vpu_instance *inst = wave5_ctrl_to_vpu_inst(ctrl);

	dev_dbg(inst->dev->dev, "%s: name: %s | value: %d\n", __func__, ctrl->name, ctrl->val);

	switch (ctrl->id) {
	case V4L2_CID_MPEG_VIDEO_AU_DELIMITER:
		inst->encode_aud = ctrl->val;
		break;
	case V4L2_CID_HFLIP:
		inst->mirror_direction |= (ctrl->val << 1);
		break;
	case V4L2_CID_VFLIP:
		inst->mirror_direction |= ctrl->val;
		break;
	case V4L2_CID_ROTATE:
		inst->rot_angle = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_VBV_SIZE:
		inst->vbv_buf_size = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE_MODE:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_BITRATE_MODE_VBR:
			inst->rc_mode = 0;
			break;
		case V4L2_MPEG_VIDEO_BITRATE_MODE_CBR:
			inst->rc_mode = 1;
			break;
		default:
			return -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_BITRATE:
		inst->bit_rate = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_GOP_SIZE:
		inst->enc_param.avc_idr_period = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE:
		inst->enc_param.independ_slice_mode = ctrl->val;
		inst->enc_param.avc_slice_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB:
		inst->enc_param.independ_slice_mode_arg = ctrl->val;
		inst->enc_param.avc_slice_arg = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE:
		inst->rc_enable = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE:
		inst->enc_param.mb_level_rc_enable = ctrl->val;
		inst->enc_param.cu_level_rc_enable = ctrl->val;
		inst->enc_param.hvs_qp_enable = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_PROFILE:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN:
			inst->enc_param.profile = HEVC_PROFILE_MAIN;
			inst->bit_depth = 8;
			break;
		case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_STILL_PICTURE:
			inst->enc_param.profile = HEVC_PROFILE_STILLPICTURE;
			inst->enc_param.en_still_picture = 1;
			inst->bit_depth = 8;
			break;
		case V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10:
			inst->enc_param.profile = HEVC_PROFILE_MAIN10;
			inst->bit_depth = 10;
			break;
		default:
			return -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_LEVEL:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_HEVC_LEVEL_1:
			inst->enc_param.level = 10 * 3;
			break;
		case V4L2_MPEG_VIDEO_HEVC_LEVEL_2:
			inst->enc_param.level = 20 * 3;
			break;
		case V4L2_MPEG_VIDEO_HEVC_LEVEL_2_1:
			inst->enc_param.level = 21 * 3;
			break;
		case V4L2_MPEG_VIDEO_HEVC_LEVEL_3:
			inst->enc_param.level = 30 * 3;
			break;
		case V4L2_MPEG_VIDEO_HEVC_LEVEL_3_1:
			inst->enc_param.level = 31 * 3;
			break;
		case V4L2_MPEG_VIDEO_HEVC_LEVEL_4:
			inst->enc_param.level = 40 * 3;
			break;
		case V4L2_MPEG_VIDEO_HEVC_LEVEL_4_1:
			inst->enc_param.level = 41 * 3;
			break;
		case V4L2_MPEG_VIDEO_HEVC_LEVEL_5:
			inst->enc_param.level = 50 * 3;
			break;
		case V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1:
			inst->enc_param.level = 51 * 3;
			break;
		case V4L2_MPEG_VIDEO_HEVC_LEVEL_5_2:
			inst->enc_param.level = 52 * 3;
			break;
		default:
			return -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP:
		inst->enc_param.min_qp_i = ctrl->val;
		inst->enc_param.min_qp_p = ctrl->val;
		inst->enc_param.min_qp_b = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP:
		inst->enc_param.max_qp_i = ctrl->val;
		inst->enc_param.max_qp_p = ctrl->val;
		inst->enc_param.max_qp_b = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_QP:
		inst->enc_param.intra_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE_DISABLED:
			inst->enc_param.disable_deblk = 1;
			inst->enc_param.sao_enable = 0;
			inst->enc_param.lf_cross_slice_boundary_enable = 0;
			break;
		case V4L2_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE_ENABLED:
			inst->enc_param.disable_deblk = 0;
			inst->enc_param.sao_enable = 1;
			inst->enc_param.lf_cross_slice_boundary_enable = 1;
			break;
		case V4L2_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE_DISABLED_AT_SLICE_BOUNDARY:
			inst->enc_param.disable_deblk = 0;
			inst->enc_param.sao_enable = 1;
			inst->enc_param.lf_cross_slice_boundary_enable = 0;
			break;
		default:
			return -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_LF_BETA_OFFSET_DIV2:
		inst->enc_param.beta_offset_div2 = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_LF_TC_OFFSET_DIV2:
		inst->enc_param.tc_offset_div2 = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_REFRESH_TYPE:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_HEVC_REFRESH_NONE:
			inst->enc_param.decoding_refresh_type = 0;
			break;
		case V4L2_MPEG_VIDEO_HEVC_REFRESH_CRA:
			inst->enc_param.decoding_refresh_type = 1;
			break;
		case V4L2_MPEG_VIDEO_HEVC_REFRESH_IDR:
			inst->enc_param.decoding_refresh_type = 2;
			break;
		default:
			return -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_REFRESH_PERIOD:
		inst->enc_param.intra_period = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_LOSSLESS_CU:
		inst->enc_param.lossless_enable = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_CONST_INTRA_PRED:
		inst->enc_param.const_intra_pred_flag = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_WAVEFRONT:
		inst->enc_param.wpp_enable = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_STRONG_SMOOTHING:
		inst->enc_param.strong_intra_smooth_enable = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_MAX_NUM_MERGE_MV_MINUS1:
		inst->enc_param.max_num_merge = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_HEVC_TMV_PREDICTION:
		inst->enc_param.tmvp_enable = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_PROFILE:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE:
		case V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE:
			inst->enc_param.profile = H264_PROFILE_BP;
			inst->bit_depth = 8;
			break;
		case V4L2_MPEG_VIDEO_H264_PROFILE_MAIN:
			inst->enc_param.profile = H264_PROFILE_MP;
			inst->bit_depth = 8;
			break;
		case V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED:
			inst->enc_param.profile = H264_PROFILE_EXTENDED;
			inst->bit_depth = 8;
			break;
		case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH:
			inst->enc_param.profile = H264_PROFILE_HP;
			inst->bit_depth = 8;
			break;
		case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_10:
			inst->enc_param.profile = H264_PROFILE_HIGH10;
			inst->bit_depth = 10;
			break;
		case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_422:
			inst->enc_param.profile = H264_PROFILE_HIGH422;
			inst->bit_depth = 10;
			break;
		case V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE:
			inst->enc_param.profile = H264_PROFILE_HIGH444;
			inst->bit_depth = 10;
			break;
		default:
			return -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LEVEL:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_H264_LEVEL_1_0:
			inst->enc_param.level = 10;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_1B:
			inst->enc_param.level = 9;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_1_1:
			inst->enc_param.level = 11;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_1_2:
			inst->enc_param.level = 12;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_1_3:
			inst->enc_param.level = 13;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_2_0:
			inst->enc_param.level = 20;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_2_1:
			inst->enc_param.level = 21;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_2_2:
			inst->enc_param.level = 22;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_3_0:
			inst->enc_param.level = 30;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_3_1:
			inst->enc_param.level = 31;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_3_2:
			inst->enc_param.level = 32;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_4_0:
			inst->enc_param.level = 40;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_4_1:
			inst->enc_param.level = 41;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_4_2:
			inst->enc_param.level = 42;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_5_0:
			inst->enc_param.level = 50;
			break;
		case V4L2_MPEG_VIDEO_H264_LEVEL_5_1:
			inst->enc_param.level = 51;
			break;
		default:
			return -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MIN_QP:
		inst->enc_param.min_qp_i = ctrl->val;
		inst->enc_param.min_qp_p = ctrl->val;
		inst->enc_param.min_qp_b = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_MAX_QP:
		inst->enc_param.max_qp_i = ctrl->val;
		inst->enc_param.max_qp_p = ctrl->val;
		inst->enc_param.max_qp_b = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP:
		inst->enc_param.intra_qp = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE:
		switch (ctrl->val) {
		case V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED:
			inst->enc_param.disable_deblk = 1;
			inst->enc_param.lf_cross_slice_boundary_enable = 1;
			break;
		case V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_ENABLED:
			inst->enc_param.disable_deblk = 0;
			inst->enc_param.lf_cross_slice_boundary_enable = 1;
			break;
		case V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED_AT_SLICE_BOUNDARY:
			inst->enc_param.disable_deblk = 0;
			inst->enc_param.lf_cross_slice_boundary_enable = 0;
			break;
		default:
			return -EINVAL;
		}
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA:
		inst->enc_param.beta_offset_div2 = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA:
		inst->enc_param.tc_offset_div2 = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM:
		inst->enc_param.transform8x8_enable = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_CONSTRAINED_INTRA_PREDICTION:
		inst->enc_param.const_intra_pred_flag = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_CHROMA_QP_INDEX_OFFSET:
		inst->enc_param.chroma_cb_qp_offset = ctrl->val;
		inst->enc_param.chroma_cr_qp_offset = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_I_PERIOD:
		inst->enc_param.intra_period = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE:
		inst->enc_param.entropy_coding_mode = ctrl->val;
		break;
	case V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR:
		inst->enc_param.forced_idr_header_enable = ctrl->val;
		break;
	case V4L2_CID_MIN_BUFFERS_FOR_OUTPUT:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct v4l2_ctrl_ops wave5_vpu_enc_ctrl_ops = {
	.s_ctrl = wave5_vpu_enc_s_ctrl,
};

static int wave5_vpu_enc_queue_setup(struct vb2_queue *q, unsigned int *num_buffers,
				     unsigned int *num_planes, unsigned int sizes[],
				     struct device *alloc_devs[])
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	struct v4l2_pix_format_mplane inst_format =
		(q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) ? inst->src_fmt : inst->dst_fmt;
	unsigned int i;

	dev_dbg(inst->dev->dev, "%s: num_buffers: %u | num_planes: %u | type: %u\n", __func__,
		*num_buffers, *num_planes, q->type);

	if (*num_planes) {
		if (inst_format.num_planes != *num_planes)
			return -EINVAL;

		for (i = 0; i < *num_planes; i++) {
			if (sizes[i] < inst_format.plane_fmt[i].sizeimage)
				return -EINVAL;
		}
	} else {
		*num_planes = inst_format.num_planes;
		for (i = 0; i < *num_planes; i++) {
			sizes[i] = inst_format.plane_fmt[i].sizeimage;
			dev_dbg(inst->dev->dev, "%s: size[%u]: %u\n", __func__, i, sizes[i]);
		}
	}

	dev_dbg(inst->dev->dev, "%s: size: %u\n", __func__, sizes[0]);

	return 0;
}

static void wave5_vpu_enc_buf_queue(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);
	struct vpu_instance *inst = vb2_get_drv_priv(vb->vb2_queue);
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;

	dev_dbg(inst->dev->dev, "%s: type: %4u index: %4u size: ([0]=%4lu, [1]=%4lu, [2]=%4lu)\n",
		__func__, vb->type, vb->index, vb2_plane_size(&vbuf->vb2_buf, 0),
		vb2_plane_size(&vbuf->vb2_buf, 1), vb2_plane_size(&vbuf->vb2_buf, 2));

	if (vb->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		vbuf->sequence = inst->queued_src_buf_num++;
	else
		vbuf->sequence = inst->queued_dst_buf_num++;

	v4l2_m2m_buf_queue(m2m_ctx, vbuf);
}

static int wave5_set_enc_openparam(struct enc_open_param *open_param,
				   struct vpu_instance *inst)
{
	struct enc_wave_param input = inst->enc_param;
	const struct v4l2_format_info *info;
	u32 num_ctu_row = ALIGN(inst->dst_fmt.height, 64) / 64;
	u32 num_mb_row = ALIGN(inst->dst_fmt.height, 16) / 16;

	info = v4l2_format_info(inst->src_fmt.pixelformat);
	if (!info)
		return -EINVAL;

	if (info->hdiv == 2 && info->vdiv == 1)
		open_param->src_format = FORMAT_422;
	else
		open_param->src_format = FORMAT_420;

	open_param->wave_param.gop_preset_idx = PRESET_IDX_IPP_SINGLE;
	open_param->wave_param.hvs_qp_scale = 2;
	open_param->wave_param.hvs_max_delta_qp = 10;
	open_param->wave_param.skip_intra_trans = 1;
	open_param->wave_param.intra_nx_n_enable = 1;
	open_param->wave_param.nr_intra_weight_y = 7;
	open_param->wave_param.nr_intra_weight_cb = 7;
	open_param->wave_param.nr_intra_weight_cr = 7;
	open_param->wave_param.nr_inter_weight_y = 4;
	open_param->wave_param.nr_inter_weight_cb = 4;
	open_param->wave_param.nr_inter_weight_cr = 4;
	open_param->wave_param.rdo_skip = 1;
	open_param->wave_param.lambda_scaling_enable = 1;

	open_param->line_buf_int_en = true;
	open_param->pic_width = inst->conf_win.width;
	open_param->pic_height = inst->conf_win.height;
	open_param->frame_rate_info = inst->frame_rate;
	open_param->rc_enable = inst->rc_enable;
	if (inst->rc_enable) {
		open_param->wave_param.initial_rc_qp = -1;
		open_param->wave_param.rc_weight_param = 16;
		open_param->wave_param.rc_weight_buf = 128;
	}
	open_param->wave_param.mb_level_rc_enable = input.mb_level_rc_enable;
	open_param->wave_param.cu_level_rc_enable = input.cu_level_rc_enable;
	open_param->wave_param.hvs_qp_enable = input.hvs_qp_enable;
	open_param->bit_rate = inst->bit_rate;
	open_param->vbv_buffer_size = inst->vbv_buf_size;
	if (inst->rc_mode == 0)
		open_param->vbv_buffer_size = 3000;
	open_param->wave_param.profile = input.profile;
	open_param->wave_param.en_still_picture = input.en_still_picture;
	open_param->wave_param.level = input.level;
	open_param->wave_param.internal_bit_depth = inst->bit_depth;
	open_param->wave_param.intra_qp = input.intra_qp;
	open_param->wave_param.min_qp_i = input.min_qp_i;
	open_param->wave_param.max_qp_i = input.max_qp_i;
	open_param->wave_param.min_qp_p = input.min_qp_p;
	open_param->wave_param.max_qp_p = input.max_qp_p;
	open_param->wave_param.min_qp_b = input.min_qp_b;
	open_param->wave_param.max_qp_b = input.max_qp_b;
	open_param->wave_param.disable_deblk = input.disable_deblk;
	open_param->wave_param.lf_cross_slice_boundary_enable =
		input.lf_cross_slice_boundary_enable;
	open_param->wave_param.tc_offset_div2 = input.tc_offset_div2;
	open_param->wave_param.beta_offset_div2 = input.beta_offset_div2;
	open_param->wave_param.decoding_refresh_type = input.decoding_refresh_type;
	open_param->wave_param.intra_period = input.intra_period;
	if (inst->std == W_HEVC_ENC) {
		if (input.intra_period == 0) {
			open_param->wave_param.decoding_refresh_type = DEC_REFRESH_TYPE_IDR;
			open_param->wave_param.intra_period = input.avc_idr_period;
		}
	} else {
		open_param->wave_param.avc_idr_period = input.avc_idr_period;
	}
	open_param->wave_param.entropy_coding_mode = input.entropy_coding_mode;
	open_param->wave_param.lossless_enable = input.lossless_enable;
	open_param->wave_param.const_intra_pred_flag = input.const_intra_pred_flag;
	open_param->wave_param.wpp_enable = input.wpp_enable;
	open_param->wave_param.strong_intra_smooth_enable = input.strong_intra_smooth_enable;
	open_param->wave_param.max_num_merge = input.max_num_merge;
	open_param->wave_param.tmvp_enable = input.tmvp_enable;
	open_param->wave_param.transform8x8_enable = input.transform8x8_enable;
	open_param->wave_param.chroma_cb_qp_offset = input.chroma_cb_qp_offset;
	open_param->wave_param.chroma_cr_qp_offset = input.chroma_cr_qp_offset;
	open_param->wave_param.independ_slice_mode = input.independ_slice_mode;
	open_param->wave_param.independ_slice_mode_arg = input.independ_slice_mode_arg;
	open_param->wave_param.avc_slice_mode = input.avc_slice_mode;
	open_param->wave_param.avc_slice_arg = input.avc_slice_arg;
	open_param->wave_param.intra_mb_refresh_mode = input.intra_mb_refresh_mode;
	if (input.intra_mb_refresh_mode != REFRESH_MB_MODE_NONE) {
		if (num_mb_row >= input.intra_mb_refresh_arg)
			open_param->wave_param.intra_mb_refresh_arg =
				num_mb_row / input.intra_mb_refresh_arg;
		else
			open_param->wave_param.intra_mb_refresh_arg = num_mb_row;
	}
	open_param->wave_param.intra_refresh_mode = input.intra_refresh_mode;
	if (input.intra_refresh_mode != 0) {
		if (num_ctu_row >= input.intra_refresh_arg)
			open_param->wave_param.intra_refresh_arg =
				num_ctu_row / input.intra_refresh_arg;
		else
			open_param->wave_param.intra_refresh_arg = num_ctu_row;
	}
	open_param->wave_param.forced_idr_header_enable = input.forced_idr_header_enable;

	return 0;
}

static int initialize_sequence(struct vpu_instance *inst)
{
	struct enc_initial_info initial_info;
	struct v4l2_ctrl *ctrl;
	int ret;

	ret = wave5_vpu_enc_issue_seq_init(inst);
	if (ret) {
		dev_err(inst->dev->dev, "%s: wave5_vpu_enc_issue_seq_init, fail: %d\n",
			__func__, ret);
		return ret;
	}

	if (wave5_vpu_wait_interrupt(inst, VPU_ENC_TIMEOUT) < 0) {
		dev_err(inst->dev->dev, "%s: wave5_vpu_wait_interrupt failed\n", __func__);
		return -EINVAL;
	}

	ret = wave5_vpu_enc_complete_seq_init(inst, &initial_info);
	if (ret)
		return ret;

	dev_dbg(inst->dev->dev, "%s: min_frame_buffer: %u | min_source_buffer: %u\n",
		__func__, initial_info.min_frame_buffer_count,
		initial_info.min_src_frame_count);
	inst->min_src_buf_count = initial_info.min_src_frame_count +
				  WAVE521_COMMAND_QUEUE_DEPTH;

	ctrl = v4l2_ctrl_find(&inst->v4l2_ctrl_hdl,
			      V4L2_CID_MIN_BUFFERS_FOR_OUTPUT);
	if (ctrl)
		v4l2_ctrl_s_ctrl(ctrl, inst->min_src_buf_count);

	inst->fbc_buf_count = initial_info.min_frame_buffer_count;

	return 0;
}

static int prepare_fb(struct vpu_instance *inst)
{
	u32 fb_stride = ALIGN(inst->dst_fmt.width, 32);
	u32 fb_height = ALIGN(inst->dst_fmt.height, 32);
	int i, ret = 0;

	for (i = 0; i < inst->fbc_buf_count; i++) {
		u32 luma_size = fb_stride * fb_height;
		u32 chroma_size = ALIGN(fb_stride / 2, 16) * fb_height;

		inst->frame_vbuf[i].size = luma_size + chroma_size;
		ret = wave5_vdi_allocate_dma_memory(inst->dev, &inst->frame_vbuf[i]);
		if (ret < 0) {
			dev_err(inst->dev->dev, "%s: failed to allocate FBC buffer %zu\n",
				__func__, inst->frame_vbuf[i].size);
			goto free_buffers;
		}

		inst->frame_buf[i].buf_y = inst->frame_vbuf[i].daddr;
		inst->frame_buf[i].buf_cb = (dma_addr_t)-1;
		inst->frame_buf[i].buf_cr = (dma_addr_t)-1;
		inst->frame_buf[i].update_fb_info = true;
		inst->frame_buf[i].size = inst->frame_vbuf[i].size;
	}

	ret = wave5_vpu_enc_register_frame_buffer(inst, inst->fbc_buf_count, fb_stride,
						  fb_height, COMPRESSED_FRAME_MAP);
	if (ret) {
		dev_err(inst->dev->dev,
			"%s: wave5_vpu_enc_register_frame_buffer, fail: %d\n",
			__func__, ret);
		goto free_buffers;
	}

	return 0;
free_buffers:
	for (i = 0; i < inst->fbc_buf_count; i++)
		wave5_vpu_dec_reset_framebuffer(inst, i);
	return ret;
}

static int wave5_vpu_enc_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	int ret = 0;

	pm_runtime_resume_and_get(inst->dev->dev);
	v4l2_m2m_update_start_streaming_state(m2m_ctx, q);

	if (inst->state == VPU_INST_STATE_NONE && q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) {
		struct enc_open_param open_param;

		memset(&open_param, 0, sizeof(struct enc_open_param));

		ret = wave5_set_enc_openparam(&open_param, inst);
		if (ret) {
			dev_dbg(inst->dev->dev, "%s: wave5_set_enc_openparam, fail: %d\n",
				__func__, ret);
			goto return_buffers;
		}

		ret = wave5_vpu_enc_open(inst, &open_param);
		if (ret) {
			dev_dbg(inst->dev->dev, "%s: wave5_vpu_enc_open, fail: %d\n",
				__func__, ret);
			goto return_buffers;
		}

		if (inst->mirror_direction) {
			wave5_vpu_enc_give_command(inst, ENABLE_MIRRORING, NULL);
			wave5_vpu_enc_give_command(inst, SET_MIRROR_DIRECTION,
						   &inst->mirror_direction);
		}
		if (inst->rot_angle) {
			wave5_vpu_enc_give_command(inst, ENABLE_ROTATION, NULL);
			wave5_vpu_enc_give_command(inst, SET_ROTATION_ANGLE, &inst->rot_angle);
		}

		ret = switch_state(inst, VPU_INST_STATE_OPEN);
		if (ret)
			goto return_buffers;
	}
	if (inst->state == VPU_INST_STATE_OPEN && m2m_ctx->cap_q_ctx.q.streaming) {
		ret = initialize_sequence(inst);
		if (ret) {
			dev_warn(inst->dev->dev, "Sequence not found: %d\n", ret);
			goto return_buffers;
		}
		ret = switch_state(inst, VPU_INST_STATE_INIT_SEQ);
		if (ret)
			goto return_buffers;
		/*
		 * The sequence must be analyzed first to calculate the proper
		 * size of the auxiliary buffers.
		 */
		ret = prepare_fb(inst);
		if (ret) {
			dev_warn(inst->dev->dev, "Framebuffer preparation, fail: %d\n", ret);
			goto return_buffers;
		}

		ret = switch_state(inst, VPU_INST_STATE_PIC_RUN);
	}
	if (ret)
		goto return_buffers;

	pm_runtime_mark_last_busy(inst->dev->dev);
	pm_runtime_put_autosuspend(inst->dev->dev);
	return 0;
return_buffers:
	wave5_return_bufs(q, VB2_BUF_STATE_QUEUED);
	pm_runtime_mark_last_busy(inst->dev->dev);
	pm_runtime_put_autosuspend(inst->dev->dev);
	return ret;
}

static void streamoff_output(struct vpu_instance *inst, struct vb2_queue *q)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	struct vb2_v4l2_buffer *buf;

	while ((buf = v4l2_m2m_src_buf_remove(m2m_ctx))) {
		dev_dbg(inst->dev->dev, "%s: buf type %4u | index %4u\n",
			__func__, buf->vb2_buf.type, buf->vb2_buf.index);
		v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	}
}

static void streamoff_capture(struct vpu_instance *inst, struct vb2_queue *q)
{
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	struct vb2_v4l2_buffer *buf;

	while ((buf = v4l2_m2m_dst_buf_remove(m2m_ctx))) {
		dev_dbg(inst->dev->dev, "%s: buf type %4u | index %4u\n",
			__func__, buf->vb2_buf.type, buf->vb2_buf.index);
		vb2_set_plane_payload(&buf->vb2_buf, 0, 0);
		v4l2_m2m_buf_done(buf, VB2_BUF_STATE_ERROR);
	}

	v4l2_m2m_clear_state(m2m_ctx);
}

static void wave5_vpu_enc_stop_streaming(struct vb2_queue *q)
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	bool check_cmd = true;

	/*
	 * Note that we don't need m2m_ctx->next_buf_last for this driver, so we
	 * don't call v4l2_m2m_update_stop_streaming_state().
	 */

	dev_dbg(inst->dev->dev, "%s: type: %u\n", __func__, q->type);
	pm_runtime_resume_and_get(inst->dev->dev);

	if (wave5_vpu_both_queues_are_streaming(inst))
		switch_state(inst, VPU_INST_STATE_STOP);

	while (check_cmd) {
		struct queue_status_info q_status;
		struct enc_output_info enc_output_info;

		wave5_vpu_enc_give_command(inst, ENC_GET_QUEUE_STATUS, &q_status);

		if (q_status.report_queue_count == 0)
			break;

		if (wave5_vpu_wait_interrupt(inst, VPU_ENC_TIMEOUT) < 0)
			break;

		if (wave5_vpu_enc_get_output_info(inst, &enc_output_info))
			dev_dbg(inst->dev->dev, "Getting encoding results from fw, fail\n");
	}

	if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
		streamoff_output(inst, q);
	else
		streamoff_capture(inst, q);

	pm_runtime_mark_last_busy(inst->dev->dev);
	pm_runtime_put_autosuspend(inst->dev->dev);
}

static const struct vb2_ops wave5_vpu_enc_vb2_ops = {
	.queue_setup = wave5_vpu_enc_queue_setup,
	.buf_queue = wave5_vpu_enc_buf_queue,
	.start_streaming = wave5_vpu_enc_start_streaming,
	.stop_streaming = wave5_vpu_enc_stop_streaming,
};

static void wave5_set_default_format(struct v4l2_pix_format_mplane *src_fmt,
				     struct v4l2_pix_format_mplane *dst_fmt)
{
	src_fmt->pixelformat = enc_fmt_list[VPU_FMT_TYPE_RAW][0].v4l2_pix_fmt;
	wave5_update_pix_fmt(src_fmt, VPU_FMT_TYPE_RAW,
			     W5_DEF_ENC_PIC_WIDTH, W5_DEF_ENC_PIC_HEIGHT,
			     &enc_frmsize[VPU_FMT_TYPE_RAW]);

	dst_fmt->pixelformat = enc_fmt_list[VPU_FMT_TYPE_CODEC][0].v4l2_pix_fmt;
	wave5_update_pix_fmt(dst_fmt, VPU_FMT_TYPE_CODEC,
			     W5_DEF_ENC_PIC_WIDTH, W5_DEF_ENC_PIC_HEIGHT,
			     &enc_frmsize[VPU_FMT_TYPE_CODEC]);
}

static int wave5_vpu_enc_queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq)
{
	return wave5_vpu_queue_init(priv, src_vq, dst_vq, &wave5_vpu_enc_vb2_ops);
}

static const struct vpu_instance_ops wave5_vpu_enc_inst_ops = {
	.finish_process = wave5_vpu_enc_finish_encode,
};

static void wave5_vpu_enc_device_run(void *priv)
{
	struct vpu_instance *inst = priv;
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	u32 fail_res = 0;
	int ret = 0;

	pm_runtime_resume_and_get(inst->dev->dev);
	switch (inst->state) {
	case VPU_INST_STATE_PIC_RUN:
		ret = start_encode(inst, &fail_res);
		if (ret) {
			if (ret == -EINVAL)
				dev_err(inst->dev->dev,
					"Frame encoding on m2m context (%p), fail: %d (res: %d)\n",
					m2m_ctx, ret, fail_res);
			else if (ret == -EAGAIN)
				dev_dbg(inst->dev->dev, "Missing buffers for encode, try again\n");
			break;
		}
		dev_dbg(inst->dev->dev, "%s: leave with active job", __func__);
		pm_runtime_mark_last_busy(inst->dev->dev);
		pm_runtime_put_autosuspend(inst->dev->dev);
		return;
	default:
		WARN(1, "Execution of a job in state %s is invalid.\n",
		     state_to_str(inst->state));
		break;
	}
	dev_dbg(inst->dev->dev, "%s: leave and finish job", __func__);
	pm_runtime_mark_last_busy(inst->dev->dev);
	pm_runtime_put_autosuspend(inst->dev->dev);
	v4l2_m2m_job_finish(inst->v4l2_m2m_dev, m2m_ctx);
}

static int wave5_vpu_enc_job_ready(void *priv)
{
	struct vpu_instance *inst = priv;
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;

	switch (inst->state) {
	case VPU_INST_STATE_NONE:
		dev_dbg(inst->dev->dev, "Encoder must be open to start queueing M2M jobs!\n");
		return false;
	case VPU_INST_STATE_PIC_RUN:
		if (m2m_ctx->is_draining || v4l2_m2m_num_src_bufs_ready(m2m_ctx)) {
			dev_dbg(inst->dev->dev, "Encoder ready for a job, state: %s\n",
				state_to_str(inst->state));
			return true;
		}
		fallthrough;
	default:
		dev_dbg(inst->dev->dev,
			"Encoder not ready for a job, state: %s, %s draining, %d src bufs ready\n",
			state_to_str(inst->state), m2m_ctx->is_draining ? "is" : "is not",
			v4l2_m2m_num_src_bufs_ready(m2m_ctx));
		break;
	}
	return false;
}

static const struct v4l2_m2m_ops wave5_vpu_enc_m2m_ops = {
	.device_run = wave5_vpu_enc_device_run,
	.job_ready = wave5_vpu_enc_job_ready,
};

static int wave5_vpu_open_enc(struct file *filp)
{
	struct video_device *vdev = video_devdata(filp);
	struct vpu_device *dev = video_drvdata(filp);
	struct vpu_instance *inst = NULL;
	struct v4l2_ctrl_handler *v4l2_ctrl_hdl;
	int ret = 0;

	inst = kzalloc(sizeof(*inst), GFP_KERNEL);
	if (!inst)
		return -ENOMEM;
	v4l2_ctrl_hdl = &inst->v4l2_ctrl_hdl;

	inst->dev = dev;
	inst->type = VPU_INST_TYPE_ENC;
	inst->ops = &wave5_vpu_enc_inst_ops;

	inst->codec_info = kzalloc(sizeof(*inst->codec_info), GFP_KERNEL);
	if (!inst->codec_info)
		return -ENOMEM;

	v4l2_fh_init(&inst->v4l2_fh, vdev);
	filp->private_data = &inst->v4l2_fh;
	v4l2_fh_add(&inst->v4l2_fh);

	INIT_LIST_HEAD(&inst->list);

	inst->v4l2_m2m_dev = inst->dev->v4l2_m2m_enc_dev;
	inst->v4l2_fh.m2m_ctx =
		v4l2_m2m_ctx_init(inst->v4l2_m2m_dev, inst, wave5_vpu_enc_queue_init);
	if (IS_ERR(inst->v4l2_fh.m2m_ctx)) {
		ret = PTR_ERR(inst->v4l2_fh.m2m_ctx);
		goto cleanup_inst;
	}
	v4l2_m2m_set_src_buffered(inst->v4l2_fh.m2m_ctx, true);

	v4l2_ctrl_handler_init(v4l2_ctrl_hdl, 50);
	v4l2_ctrl_new_std_menu(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_HEVC_PROFILE,
			       V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN_10, 0,
			       V4L2_MPEG_VIDEO_HEVC_PROFILE_MAIN);
	v4l2_ctrl_new_std_menu(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_HEVC_LEVEL,
			       V4L2_MPEG_VIDEO_HEVC_LEVEL_5_1, 0,
			       V4L2_MPEG_VIDEO_HEVC_LEVEL_1);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_MIN_QP,
			  0, 63, 1, 8);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_MAX_QP,
			  0, 63, 1, 51);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_I_FRAME_QP,
			  0, 63, 1, 30);
	v4l2_ctrl_new_std_menu(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE,
			       V4L2_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE_DISABLED_AT_SLICE_BOUNDARY, 0,
			       V4L2_MPEG_VIDEO_HEVC_LOOP_FILTER_MODE_ENABLED);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_LF_BETA_OFFSET_DIV2,
			  -6, 6, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_LF_TC_OFFSET_DIV2,
			  -6, 6, 1, 0);
	v4l2_ctrl_new_std_menu(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_HEVC_REFRESH_TYPE,
			       V4L2_MPEG_VIDEO_HEVC_REFRESH_IDR, 0,
			       V4L2_MPEG_VIDEO_HEVC_REFRESH_IDR);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_REFRESH_PERIOD,
			  0, 2047, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_LOSSLESS_CU,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_CONST_INTRA_PRED,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_WAVEFRONT,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_STRONG_SMOOTHING,
			  0, 1, 1, 1);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_MAX_NUM_MERGE_MV_MINUS1,
			  1, 2, 1, 2);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_HEVC_TMV_PREDICTION,
			  0, 1, 1, 1);

	v4l2_ctrl_new_std_menu(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_H264_PROFILE,
			       V4L2_MPEG_VIDEO_H264_PROFILE_HIGH_444_PREDICTIVE, 0,
			       V4L2_MPEG_VIDEO_H264_PROFILE_BASELINE);
	v4l2_ctrl_new_std_menu(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_H264_LEVEL,
			       V4L2_MPEG_VIDEO_H264_LEVEL_5_1, 0,
			       V4L2_MPEG_VIDEO_H264_LEVEL_1_0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_MIN_QP,
			  0, 63, 1, 8);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_MAX_QP,
			  0, 63, 1, 51);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP,
			  0, 63, 1, 30);
	v4l2_ctrl_new_std_menu(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_MODE,
			       V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_DISABLED_AT_SLICE_BOUNDARY, 0,
			       V4L2_MPEG_VIDEO_H264_LOOP_FILTER_MODE_ENABLED);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_ALPHA,
			  -6, 6, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_LOOP_FILTER_BETA,
			  -6, 6, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_8X8_TRANSFORM,
			  0, 1, 1, 1);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_CONSTRAINED_INTRA_PREDICTION,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_CHROMA_QP_INDEX_OFFSET,
			  -12, 12, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_H264_I_PERIOD,
			  0, 2047, 1, 0);
	v4l2_ctrl_new_std_menu(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_H264_ENTROPY_MODE,
			       V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CABAC, 0,
			       V4L2_MPEG_VIDEO_H264_ENTROPY_MODE_CAVLC);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_AU_DELIMITER,
			  0, 1, 1, 1);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_HFLIP,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_VFLIP,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_ROTATE,
			  0, 270, 90, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_VBV_SIZE,
			  10, 3000, 1, 1000);
	v4l2_ctrl_new_std_menu(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
			       V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, 0,
			       V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_BITRATE,
			  0, 700000000, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_GOP_SIZE,
			  0, 2047, 1, 0);
	v4l2_ctrl_new_std_menu(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			       V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MODE,
			       V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_MAX_MB, 0,
			       V4L2_MPEG_VIDEO_MULTI_SLICE_MODE_SINGLE);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_MULTI_SLICE_MAX_MB,
			  0, 0xFFFF, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_FRAME_RC_ENABLE,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_MB_RC_ENABLE,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MIN_BUFFERS_FOR_OUTPUT, 1, 32, 1, 1);
	v4l2_ctrl_new_std(v4l2_ctrl_hdl, &wave5_vpu_enc_ctrl_ops,
			  V4L2_CID_MPEG_VIDEO_PREPEND_SPSPPS_TO_IDR,
			  0, 1, 1, 0);

	if (v4l2_ctrl_hdl->error) {
		ret = -ENODEV;
		goto cleanup_inst;
	}

	inst->v4l2_fh.ctrl_handler = v4l2_ctrl_hdl;
	v4l2_ctrl_handler_setup(v4l2_ctrl_hdl);

	wave5_set_default_format(&inst->src_fmt, &inst->dst_fmt);
	inst->conf_win.width = inst->dst_fmt.width;
	inst->conf_win.height = inst->dst_fmt.height;
	inst->colorspace = V4L2_COLORSPACE_REC709;
	inst->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	inst->quantization = V4L2_QUANTIZATION_DEFAULT;
	inst->xfer_func = V4L2_XFER_FUNC_DEFAULT;
	inst->frame_rate = 30;

	init_completion(&inst->irq_done);

	inst->id = ida_alloc(&inst->dev->inst_ida, GFP_KERNEL);
	if (inst->id < 0) {
		dev_warn(inst->dev->dev, "Allocating instance ID, fail: %d\n", inst->id);
		ret = inst->id;
		goto cleanup_inst;
	}

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

static int wave5_vpu_enc_release(struct file *filp)
{
	return wave5_vpu_release_device(filp, wave5_vpu_enc_close, "encoder");
}

static const struct v4l2_file_operations wave5_vpu_enc_fops = {
	.owner = THIS_MODULE,
	.open = wave5_vpu_open_enc,
	.release = wave5_vpu_enc_release,
	.unlocked_ioctl = video_ioctl2,
	.poll = v4l2_m2m_fop_poll,
	.mmap = v4l2_m2m_fop_mmap,
};

int wave5_vpu_enc_register_device(struct vpu_device *dev)
{
	struct video_device *vdev_enc;
	int ret;

	vdev_enc = devm_kzalloc(dev->v4l2_dev.dev, sizeof(*vdev_enc), GFP_KERNEL);
	if (!vdev_enc)
		return -ENOMEM;

	dev->v4l2_m2m_enc_dev = v4l2_m2m_init(&wave5_vpu_enc_m2m_ops);
	if (IS_ERR(dev->v4l2_m2m_enc_dev)) {
		ret = PTR_ERR(dev->v4l2_m2m_enc_dev);
		dev_err(dev->dev, "v4l2_m2m_init, fail: %d\n", ret);
		return -EINVAL;
	}

	dev->video_dev_enc = vdev_enc;

	strscpy(vdev_enc->name, VPU_ENC_DEV_NAME, sizeof(vdev_enc->name));
	vdev_enc->fops = &wave5_vpu_enc_fops;
	vdev_enc->ioctl_ops = &wave5_vpu_enc_ioctl_ops;
	vdev_enc->release = video_device_release_empty;
	vdev_enc->v4l2_dev = &dev->v4l2_dev;
	vdev_enc->vfl_dir = VFL_DIR_M2M;
	vdev_enc->device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	vdev_enc->lock = &dev->dev_lock;

	ret = video_register_device(vdev_enc, VFL_TYPE_VIDEO, -1);
	if (ret)
		return ret;

	video_set_drvdata(vdev_enc, dev);

	return 0;
}

void wave5_vpu_enc_unregister_device(struct vpu_device *dev)
{
	video_unregister_device(dev->video_dev_enc);
	if (dev->v4l2_m2m_enc_dev)
		v4l2_m2m_release(dev->v4l2_m2m_enc_dev);
}
