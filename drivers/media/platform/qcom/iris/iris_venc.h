/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_VENC_H_
#define _IRIS_VENC_H_

struct iris_inst;

int iris_venc_inst_init(struct iris_inst *inst);
void iris_venc_inst_deinit(struct iris_inst *inst);
int iris_venc_enum_fmt(struct iris_inst *inst, struct v4l2_fmtdesc *f);
int iris_venc_try_fmt(struct iris_inst *inst, struct v4l2_format *f);
int iris_venc_s_fmt(struct iris_inst *inst, struct v4l2_format *f);
int iris_venc_validate_format(struct iris_inst *inst, u32 pixelformat);
int iris_venc_subscribe_event(struct iris_inst *inst, const struct v4l2_event_subscription *sub);
int iris_venc_s_selection(struct iris_inst *inst, struct v4l2_selection *s);
int iris_venc_g_param(struct iris_inst *inst, struct v4l2_streamparm *s_parm);
int iris_venc_s_param(struct iris_inst *inst, struct v4l2_streamparm *s_parm);
int iris_venc_streamon_input(struct iris_inst *inst);
int iris_venc_streamon_output(struct iris_inst *inst);
int iris_venc_qbuf(struct iris_inst *inst, struct vb2_v4l2_buffer *vbuf);
int iris_venc_start_cmd(struct iris_inst *inst);
int iris_venc_stop_cmd(struct iris_inst *inst);

#endif
