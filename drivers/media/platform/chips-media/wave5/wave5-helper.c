// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
/*
 * Wave5 series multi-standard codec IP - decoder interface
 *
 * Copyright (C) 2021-2023 CHIPS&MEDIA INC
 */

#include "wave5-helper.h"

const char *state_to_str(enum vpu_instance_state state)
{
	switch (state) {
	case VPU_INST_STATE_NONE:
		return "NONE";
	case VPU_INST_STATE_OPEN:
		return "OPEN";
	case VPU_INST_STATE_INIT_SEQ:
		return "INIT_SEQ";
	case VPU_INST_STATE_PIC_RUN:
		return "PIC_RUN";
	case VPU_INST_STATE_STOP:
		return "STOP";
	default:
		return "UNKNOWN";
	}
}

void wave5_cleanup_instance(struct vpu_instance *inst)
{
	int i;

	if (list_is_singular(&inst->list))
		wave5_vdi_free_sram(inst->dev);

	for (i = 0; i < inst->fbc_buf_count; i++)
		wave5_vpu_dec_reset_framebuffer(inst, i);

	wave5_vdi_free_dma_memory(inst->dev, &inst->bitstream_vbuf);
	v4l2_ctrl_handler_free(&inst->v4l2_ctrl_hdl);
	if (inst->v4l2_fh.vdev) {
		v4l2_fh_del(&inst->v4l2_fh);
		v4l2_fh_exit(&inst->v4l2_fh);
	}
	list_del_init(&inst->list);
	ida_free(&inst->dev->inst_ida, inst->id);
	kfree(inst->codec_info);
	kfree(inst);
}

int wave5_vpu_release_device(struct file *filp,
			     int (*close_func)(struct vpu_instance *inst, u32 *fail_res),
			     char *name)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(filp->private_data);

	v4l2_m2m_ctx_release(inst->v4l2_fh.m2m_ctx);
	if (inst->state != VPU_INST_STATE_NONE) {
		u32 fail_res;
		int ret;

		ret = close_func(inst, &fail_res);
		if (fail_res == WAVE5_SYSERR_VPU_STILL_RUNNING) {
			dev_err(inst->dev->dev, "%s close failed, device is still running\n",
				name);
			return -EBUSY;
		}
		if (ret && ret != -EIO) {
			dev_err(inst->dev->dev, "%s close, fail: %d\n", name, ret);
			return ret;
		}
	}

	wave5_cleanup_instance(inst);

	return 0;
}

int wave5_vpu_queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq,
			 const struct vb2_ops *ops)
{
	struct vpu_instance *inst = priv;
	int ret;

	src_vq->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	src_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	src_vq->mem_ops = &vb2_dma_contig_memops;
	src_vq->ops = ops;
	src_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	src_vq->buf_struct_size = sizeof(struct vpu_src_buffer);
	src_vq->drv_priv = inst;
	src_vq->lock = &inst->dev->dev_lock;
	src_vq->dev = inst->dev->v4l2_dev.dev;
	ret = vb2_queue_init(src_vq);
	if (ret)
		return ret;

	dst_vq->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	dst_vq->io_modes = VB2_MMAP | VB2_DMABUF;
	dst_vq->mem_ops = &vb2_dma_contig_memops;
	dst_vq->ops = ops;
	dst_vq->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
	dst_vq->buf_struct_size = sizeof(struct vpu_src_buffer);
	dst_vq->drv_priv = inst;
	dst_vq->lock = &inst->dev->dev_lock;
	dst_vq->dev = inst->dev->v4l2_dev.dev;
	ret = vb2_queue_init(dst_vq);
	if (ret)
		return ret;

	return 0;
}

int wave5_vpu_subscribe_event(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	bool is_decoder = inst->type == VPU_INST_TYPE_DEC;

	dev_dbg(inst->dev->dev, "%s: [%s] type: %u id: %u | flags: %u\n", __func__,
		is_decoder ? "decoder" : "encoder", sub->type, sub->id, sub->flags);

	switch (sub->type) {
	case V4L2_EVENT_EOS:
		return v4l2_event_subscribe(fh, sub, 0, NULL);
	case V4L2_EVENT_SOURCE_CHANGE:
		if (is_decoder)
			return v4l2_src_change_event_subscribe(fh, sub);
		return -EINVAL;
	case V4L2_EVENT_CTRL:
		return v4l2_ctrl_subscribe_event(fh, sub);
	default:
		return -EINVAL;
	}
}

int wave5_vpu_g_fmt_out(struct file *file, void *fh, struct v4l2_format *f)
{
	struct vpu_instance *inst = wave5_to_vpu_inst(fh);
	int i;

	f->fmt.pix_mp.width = inst->src_fmt.width;
	f->fmt.pix_mp.height = inst->src_fmt.height;
	f->fmt.pix_mp.pixelformat = inst->src_fmt.pixelformat;
	f->fmt.pix_mp.field = inst->src_fmt.field;
	f->fmt.pix_mp.flags = inst->src_fmt.flags;
	f->fmt.pix_mp.num_planes = inst->src_fmt.num_planes;
	for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
		f->fmt.pix_mp.plane_fmt[i].bytesperline = inst->src_fmt.plane_fmt[i].bytesperline;
		f->fmt.pix_mp.plane_fmt[i].sizeimage = inst->src_fmt.plane_fmt[i].sizeimage;
	}

	f->fmt.pix_mp.colorspace = inst->colorspace;
	f->fmt.pix_mp.ycbcr_enc = inst->ycbcr_enc;
	f->fmt.pix_mp.quantization = inst->quantization;
	f->fmt.pix_mp.xfer_func = inst->xfer_func;

	return 0;
}

const struct vpu_format *wave5_find_vpu_fmt(unsigned int v4l2_pix_fmt,
					    const struct vpu_format fmt_list[MAX_FMTS])
{
	unsigned int index;

	for (index = 0; index < MAX_FMTS; index++) {
		if (fmt_list[index].v4l2_pix_fmt == v4l2_pix_fmt)
			return &fmt_list[index];
	}

	return NULL;
}

const struct vpu_format *wave5_find_vpu_fmt_by_idx(unsigned int idx,
						   const struct vpu_format fmt_list[MAX_FMTS])
{
	if (idx >= MAX_FMTS)
		return NULL;

	if (!fmt_list[idx].v4l2_pix_fmt)
		return NULL;

	return &fmt_list[idx];
}

enum wave_std wave5_to_vpu_std(unsigned int v4l2_pix_fmt, enum vpu_instance_type type)
{
	switch (v4l2_pix_fmt) {
	case V4L2_PIX_FMT_H264:
		return type == VPU_INST_TYPE_DEC ? W_AVC_DEC : W_AVC_ENC;
	case V4L2_PIX_FMT_HEVC:
		return type == VPU_INST_TYPE_DEC ? W_HEVC_DEC : W_HEVC_ENC;
	default:
		return STD_UNKNOWN;
	}
}

void wave5_return_bufs(struct vb2_queue *q, u32 state)
{
	struct vpu_instance *inst = vb2_get_drv_priv(q);
	struct v4l2_m2m_ctx *m2m_ctx = inst->v4l2_fh.m2m_ctx;
	struct v4l2_ctrl_handler v4l2_ctrl_hdl = inst->v4l2_ctrl_hdl;
	struct vb2_v4l2_buffer *vbuf;

	for (;;) {
		if (q->type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE)
			vbuf = v4l2_m2m_src_buf_remove(m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(m2m_ctx);
		if (!vbuf)
			return;
		v4l2_ctrl_request_complete(vbuf->vb2_buf.req_obj.req, &v4l2_ctrl_hdl);
		v4l2_m2m_buf_done(vbuf, state);
	}
}
