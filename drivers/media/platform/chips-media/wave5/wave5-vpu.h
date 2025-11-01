/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Wave5 series multi-standard codec IP - basic types
 *
 * Copyright (C) 2021-2023 CHIPS&MEDIA INC
 */
#ifndef __VPU_DRV_H__
#define __VPU_DRV_H__

#include <media/v4l2-ctrls.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-event.h>
#include <media/v4l2-fh.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-vmalloc.h>
#include "wave5-vpuconfig.h"
#include "wave5-vpuapi.h"

#define VPU_BUF_SYNC_TO_DEVICE 0
#define VPU_BUF_SYNC_FROM_DEVICE 1

struct vpu_src_buffer {
	struct v4l2_m2m_buffer	v4l2_m2m_buf;
	struct list_head	list;
	bool			consumed;
};

struct vpu_dst_buffer {
	struct v4l2_m2m_buffer v4l2_m2m_buf;
	bool                   display;
};

enum vpu_fmt_type {
	VPU_FMT_TYPE_CODEC = 0,
	VPU_FMT_TYPE_RAW   = 1
};

struct vpu_format {
	unsigned int v4l2_pix_fmt;
	const struct v4l2_frmsize_stepwise *v4l2_frmsize;
};

static inline struct vpu_instance *wave5_to_vpu_inst(struct v4l2_fh *vfh)
{
	return container_of(vfh, struct vpu_instance, v4l2_fh);
}

static inline struct vpu_instance *file_to_vpu_inst(struct file *filp)
{
	return wave5_to_vpu_inst(file_to_v4l2_fh(filp));
}

static inline struct vpu_instance *wave5_ctrl_to_vpu_inst(struct v4l2_ctrl *vctrl)
{
	return container_of(vctrl->handler, struct vpu_instance, v4l2_ctrl_hdl);
}

static inline struct vpu_src_buffer *wave5_to_vpu_src_buf(struct vb2_v4l2_buffer *vbuf)
{
	return container_of(vbuf, struct vpu_src_buffer, v4l2_m2m_buf.vb);
}

static inline struct vpu_dst_buffer *wave5_to_vpu_dst_buf(struct vb2_v4l2_buffer *vbuf)
{
	return container_of(vbuf, struct vpu_dst_buffer, v4l2_m2m_buf.vb);
}

int wave5_vpu_wait_interrupt(struct vpu_instance *inst, unsigned int timeout);

int  wave5_vpu_dec_register_device(struct vpu_device *dev);
void wave5_vpu_dec_unregister_device(struct vpu_device *dev);
int  wave5_vpu_enc_register_device(struct vpu_device *dev);
void wave5_vpu_enc_unregister_device(struct vpu_device *dev);
static inline bool wave5_vpu_both_queues_are_streaming(struct vpu_instance *inst)
{
	struct vb2_queue *vq_cap =
		v4l2_m2m_get_vq(inst->v4l2_fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE);
	struct vb2_queue *vq_out =
		v4l2_m2m_get_vq(inst->v4l2_fh.m2m_ctx, V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE);

	return vb2_is_streaming(vq_cap) && vb2_is_streaming(vq_out);
}

#endif
