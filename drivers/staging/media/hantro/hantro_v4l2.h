/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Hantro VPU codec driver
 *
 * Copyright (C) 2018 Rockchip Electronics Co., Ltd.
 *	Alpha Lin <Alpha.Lin@rock-chips.com>
 *	Jeffy Chen <jeffy.chen@rock-chips.com>
 *
 * Copyright 2018 Google LLC.
 *	Tomasz Figa <tfiga@chromium.org>
 *
 * Based on s5p-mfc driver by Samsung Electronics Co., Ltd.
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 */

#ifndef HANTRO_V4L2_H_
#define HANTRO_V4L2_H_

#include "hantro.h"

extern const struct v4l2_ioctl_ops hantro_ioctl_ops;
extern const struct vb2_ops hantro_queue_ops;

void hantro_reset_fmts(struct hantro_ctx *ctx);

#endif /* HANTRO_V4L2_H_ */
