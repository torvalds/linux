/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 */
#ifndef __VENUS_HELPERS_H__
#define __VENUS_HELPERS_H__

#include <media/videobuf2-v4l2.h>

struct venus_inst;
struct venus_core;

bool venus_helper_check_codec(struct venus_inst *inst, u32 v4l2_pixfmt);
struct vb2_v4l2_buffer *venus_helper_find_buf(struct venus_inst *inst,
					      unsigned int type, u32 idx);
void venus_helper_buffers_done(struct venus_inst *inst, unsigned int type,
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
u32 venus_helper_get_framesz_raw(u32 hfi_fmt, u32 width, u32 height);
u32 venus_helper_get_framesz(u32 v4l2_fmt, u32 width, u32 height);
int venus_helper_set_input_resolution(struct venus_inst *inst,
				      unsigned int width, unsigned int height);
int venus_helper_set_output_resolution(struct venus_inst *inst,
				       unsigned int width, unsigned int height,
				       u32 buftype);
int venus_helper_set_work_mode(struct venus_inst *inst);
int venus_helper_set_format_constraints(struct venus_inst *inst);
int venus_helper_set_num_bufs(struct venus_inst *inst, unsigned int input_bufs,
			      unsigned int output_bufs,
			      unsigned int output2_bufs);
int venus_helper_set_raw_format(struct venus_inst *inst, u32 hfi_format,
				u32 buftype);
int venus_helper_set_color_format(struct venus_inst *inst, u32 fmt);
int venus_helper_set_dyn_bufmode(struct venus_inst *inst);
int venus_helper_set_bufsize(struct venus_inst *inst, u32 bufsize, u32 buftype);
int venus_helper_set_multistream(struct venus_inst *inst, bool out_en,
				 bool out2_en);
unsigned int venus_helper_get_opb_size(struct venus_inst *inst);
void venus_helper_acquire_buf_ref(struct vb2_v4l2_buffer *vbuf);
void venus_helper_release_buf_ref(struct venus_inst *inst, unsigned int idx);
void venus_helper_init_instance(struct venus_inst *inst);
int venus_helper_session_init(struct venus_inst *inst);
int venus_helper_get_out_fmts(struct venus_inst *inst, u32 fmt, u32 *out_fmt,
			      u32 *out2_fmt, bool ubwc);
int venus_helper_alloc_dpb_bufs(struct venus_inst *inst);
int venus_helper_free_dpb_bufs(struct venus_inst *inst);
int venus_helper_intbufs_alloc(struct venus_inst *inst);
int venus_helper_intbufs_free(struct venus_inst *inst);
int venus_helper_intbufs_realloc(struct venus_inst *inst);
int venus_helper_queue_dpb_bufs(struct venus_inst *inst);
int venus_helper_unregister_bufs(struct venus_inst *inst);
int venus_helper_process_initial_cap_bufs(struct venus_inst *inst);
int venus_helper_process_initial_out_bufs(struct venus_inst *inst);
void venus_helper_get_ts_metadata(struct venus_inst *inst, u64 timestamp_us,
				  struct vb2_v4l2_buffer *vbuf);
int venus_helper_get_profile_level(struct venus_inst *inst, u32 *profile, u32 *level);
int venus_helper_set_profile_level(struct venus_inst *inst, u32 profile, u32 level);
int venus_helper_set_stride(struct venus_inst *inst, unsigned int aligned_width,
			    unsigned int aligned_height);
#endif
