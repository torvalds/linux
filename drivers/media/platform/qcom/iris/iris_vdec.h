/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_VDEC_H__
#define __IRIS_VDEC_H__

struct iris_inst;

void iris_vdec_inst_init(struct iris_inst *inst);
void iris_vdec_inst_deinit(struct iris_inst *inst);

#endif
