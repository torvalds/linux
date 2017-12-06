/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#ifndef __VENUS_HELPERS_H__
#define __VENUS_HELPERS_H__

#include <media/videobuf2-v4l2.h>

struct venus_inst;

bool venus_helper_check_codec(struct venus_inst *inst, u32 v4l2_pixfmt);
struct vb2_v4l2_buffer *venus_helper_find_buf(struct venus_inst *inst,
					      unsigned int type, u32 idx);
void venus_helper_buffers_done(struct venus_inst *inst,
			       enum vb2_buffer_state state);
int venus_helper_vb2_buf_init(struct vb2_buffer *vb);
int venus_helper_vb2_buf_prepare(struct vb2_buffer *vb);
void venus_helper_vb2_buf_queue(struct vb2_buffer *vb);
void venus_helper_vb2_stop_streaming(struct vb2_queue *q);
int venus_helper_vb2_start_streaming(struct venus_inst *inst);
void venus_helper_m2m_device_run(void *priv);
void venus_helper_m2m_job_abort(void *priv);
int venus_helper_get_bufreq(struct venus_inst *inst, u32 type,
			    struct hfi_buffer_requirements *req);
int venus_helper_set_input_resolution(struct venus_inst *inst,
				      unsigned int width, unsigned int height);
int venus_helper_set_output_resolution(struct venus_inst *inst,
				       unsigned int width, unsigned int height);
int venus_helper_set_num_bufs(struct venus_inst *inst, unsigned int input_bufs,
			      unsigned int output_bufs);
int venus_helper_set_color_format(struct venus_inst *inst, u32 fmt);
void venus_helper_acquire_buf_ref(struct vb2_v4l2_buffer *vbuf);
void venus_helper_release_buf_ref(struct venus_inst *inst, unsigned int idx);
void venus_helper_init_instance(struct venus_inst *inst);
#endif
