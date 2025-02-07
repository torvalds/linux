/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_VDEC_H__
#define __IRIS_VDEC_H__

struct iris_inst;

void iris_vdec_inst_init(struct iris_inst *inst);
void iris_vdec_inst_deinit(struct iris_inst *inst);
int iris_vdec_enum_fmt(struct iris_inst *inst, struct v4l2_fmtdesc *f);
int iris_vdec_try_fmt(struct iris_inst *inst, struct v4l2_format *f);
int iris_vdec_s_fmt(struct iris_inst *inst, struct v4l2_format *f);
int iris_vdec_subscribe_event(struct iris_inst *inst, const struct v4l2_event_subscription *sub);

#endif
