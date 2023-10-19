/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020-2021 NXP
 */

#ifndef _AMPHION_VPU_V4L2_H
#define _AMPHION_VPU_V4L2_H

#include <linux/videodev2.h>

void vpu_inst_lock(struct vpu_inst *inst);
void vpu_inst_unlock(struct vpu_inst *inst);
void vpu_set_buffer_state(struct vb2_v4l2_buffer *vbuf, unsigned int state);
unsigned int vpu_get_buffer_state(struct vb2_v4l2_buffer *vbuf);

int vpu_v4l2_open(struct file *file, struct vpu_inst *inst);
int vpu_v4l2_close(struct file *file);

const struct vpu_format *vpu_try_fmt_common(struct vpu_inst *inst, struct v4l2_format *f);
int vpu_process_output_buffer(struct vpu_inst *inst);
int vpu_process_capture_buffer(struct vpu_inst *inst);
struct vb2_v4l2_buffer *vpu_next_src_buf(struct vpu_inst *inst);
void vpu_skip_frame(struct vpu_inst *inst, int count);
struct vb2_v4l2_buffer *vpu_find_buf_by_sequence(struct vpu_inst *inst, u32 type, u32 sequence);
struct vb2_v4l2_buffer *vpu_find_buf_by_idx(struct vpu_inst *inst, u32 type, u32 idx);
void vpu_v4l2_set_error(struct vpu_inst *inst);
int vpu_notify_eos(struct vpu_inst *inst);
int vpu_notify_source_change(struct vpu_inst *inst);
int vpu_set_last_buffer_dequeued(struct vpu_inst *inst);
void vpu_vb2_buffers_return(struct vpu_inst *inst, unsigned int type, enum vb2_buffer_state state);
int vpu_get_num_buffers(struct vpu_inst *inst, u32 type);
bool vpu_is_source_empty(struct vpu_inst *inst);

dma_addr_t vpu_get_vb_phy_addr(struct vb2_buffer *vb, u32 plane_no);
unsigned int vpu_get_vb_length(struct vb2_buffer *vb, u32 plane_no);
static inline struct vpu_format *vpu_get_format(struct vpu_inst *inst, u32 type)
{
	if (V4L2_TYPE_IS_OUTPUT(type))
		return &inst->out_format;
	else
		return &inst->cap_format;
}

static inline char *vpu_type_name(u32 type)
{
	return V4L2_TYPE_IS_OUTPUT(type) ? "output" : "capture";
}

static inline int vpu_vb_is_codecconfig(struct vb2_v4l2_buffer *vbuf)
{
#ifdef V4L2_BUF_FLAG_CODECCONFIG
	return (vbuf->flags & V4L2_BUF_FLAG_CODECCONFIG) ? 1 : 0;
#else
	return 0;
#endif
}

#endif
