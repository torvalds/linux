/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _IRIS_VENC_H_
#define _IRIS_VENC_H_

struct iris_inst;

int iris_venc_inst_init(struct iris_inst *inst);
void iris_venc_inst_deinit(struct iris_inst *inst);

#endif
