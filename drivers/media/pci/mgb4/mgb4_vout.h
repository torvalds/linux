/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021-2023 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 */

#ifndef __MGB4_VOUT_H__
#define __MGB4_VOUT_H__

#include <media/v4l2-device.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ctrls.h>
#include <media/videobuf2-core.h>
#include <linux/debugfs.h>
#include "mgb4_i2c.h"

struct mgb4_vout_regs {
	u32 address;
	u32 config;
	u32 status;
	u32 resolution;
	u32 frame_limit;
	u32 hsync;
	u32 vsync;
	u32 padding;
	u32 timer;
};

struct mgb4_vout_config {
	int id;
	int dma_channel;
	int irq;
	struct mgb4_vout_regs regs;
};

struct mgb4_vout_dev {
	struct mgb4_dev *mgbdev;
	struct v4l2_device v4l2dev;
	struct video_device vdev;
	struct vb2_queue queue;
	struct mutex lock; /* vdev lock */

	spinlock_t qlock; /* buffer queue lock */
	struct list_head buf_list;
	struct work_struct dma_work;

	u32 width;
	u32 height;
	u32 freq;
	u32 padding;

	struct mgb4_i2c_client ser;

	const struct mgb4_vout_config *config;

#ifdef CONFIG_DEBUG_FS
	struct dentry *debugfs;
	struct debugfs_regset32 regset;
	struct debugfs_reg32 regs[sizeof(struct mgb4_vout_regs) / 4];
#endif
};

struct mgb4_vout_dev *mgb4_vout_create(struct mgb4_dev *mgbdev, int id);
void mgb4_vout_free(struct mgb4_vout_dev *voutdev);

#endif
