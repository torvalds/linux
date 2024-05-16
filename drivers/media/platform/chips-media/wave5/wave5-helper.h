/* SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause) */
/*
 * Wave5 series multi-standard codec IP - basic types
 *
 * Copyright (C) 2021-2023 CHIPS&MEDIA INC
 */

#ifndef __WAVE_HELPER_H__
#define __WAVE_HELPER_H__

#include "wave5-vpu.h"

#define FMT_TYPES	2
#define MAX_FMTS	12

const char *state_to_str(enum vpu_instance_state state);
void wave5_cleanup_instance(struct vpu_instance *inst);
int wave5_vpu_release_device(struct file *filp,
			     int (*close_func)(struct vpu_instance *inst, u32 *fail_res),
			     char *name);
int wave5_vpu_queue_init(void *priv, struct vb2_queue *src_vq, struct vb2_queue *dst_vq,
			 const struct vb2_ops *ops);
int wave5_vpu_subscribe_event(struct v4l2_fh *fh, const struct v4l2_event_subscription *sub);
int wave5_vpu_g_fmt_out(struct file *file, void *fh, struct v4l2_format *f);
const struct vpu_format *wave5_find_vpu_fmt(unsigned int v4l2_pix_fmt,
					    const struct vpu_format fmt_list[MAX_FMTS]);
const struct vpu_format *wave5_find_vpu_fmt_by_idx(unsigned int idx,
						   const struct vpu_format fmt_list[MAX_FMTS]);
enum wave_std wave5_to_vpu_std(unsigned int v4l2_pix_fmt, enum vpu_instance_type type);
void wave5_return_bufs(struct vb2_queue *q, u32 state);
#endif
