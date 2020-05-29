/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP1_DMARX_H
#define _RKISP1_DMARX_H

#include "capture.h"
#include "common.h"

#define RKISP1_STREAM_DMARX		0
#define RKISP1_MAX_DMARX_STREAM		1

struct rkisp1_dmarx_device;

enum rkisp1_dmarx_pad {
	RKISP1_DMARX_PAD_SINK,
	RKISP1_DMARX_PAD_SOURCE,
	RKISP1_DMARX_PAD_MAX
};

struct rkisp1_dmarx_device {
	struct rkisp1_device *ispdev;
	struct rkisp1_stream stream[RKISP1_MAX_DMARX_STREAM];
};

void rkisp1_dmarx_isr(u32 mis_val, struct rkisp1_device *dev);
void rkisp1_unregister_dmarx_vdev(struct rkisp1_device *dev);
int rkisp1_register_dmarx_vdev(struct rkisp1_device *dev);
#endif /* _RKISP1_DMARX_H */
