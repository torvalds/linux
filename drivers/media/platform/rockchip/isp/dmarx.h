/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2019 Fuzhou Rockchip Electronics Co., Ltd. */

#ifndef _RKISP_DMARX_H
#define _RKISP_DMARX_H

#include "capture.h"
#include "common.h"

#define RKISP_STREAM_DMARX		0
#define RKISP_MAX_DMARX_STREAM		1

struct rkisp_dmarx_device;

enum rkisp_dmarx_pad {
	RKISP_DMARX_PAD_SINK,
	RKISP_DMARX_PAD_SOURCE,
	RKISP_DMARX_PAD_MAX
};

struct rkisp_dmarx_device {
	struct rkisp_device *ispdev;
	struct rkisp_stream stream[RKISP_MAX_DMARX_STREAM];
};

void rkisp_dmarx_isr(u32 mis_val, struct rkisp_device *dev);
void rkisp_unregister_dmarx_vdev(struct rkisp_device *dev);
int rkisp_register_dmarx_vdev(struct rkisp_device *dev);
#endif /* _RKISP_DMARX_H */
