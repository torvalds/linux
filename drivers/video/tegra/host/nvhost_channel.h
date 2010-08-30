/*
 * drivers/video/tegra/host/nvhost_channel.h
 *
 * Tegra Graphics Host Channel
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __NVHOST_CHANNEL_H
#define __NVHOST_CHANNEL_H

#include "nvhost_cdma.h"
#include "nvhost_acm.h"
#include "nvhost_hwctx.h"

#include <linux/cdev.h>
#include <linux/io.h>

#define NVHOST_CHANNEL_BASE 0
#define NVHOST_NUMCHANNELS (NV_HOST1X_CHANNELS - 1)
#define NVHOST_MAX_GATHERS 512
#define NVHOST_MAX_HANDLES 1280

struct nvhost_master;

struct nvhost_channeldesc {
	const char *name;
	nvhost_modulef power;
	u32 syncpts;
	u32 waitbases;
	u32 modulemutexes;
	u32 class;
};

struct nvhost_channel {
	int refcount;
	struct mutex reflock;
	struct mutex submitlock;
	void __iomem *aperture;
	struct nvhost_master *dev;
	const struct nvhost_channeldesc *desc;
	struct nvhost_hwctx *cur_ctx;
	struct device *node;
	struct cdev cdev;
	struct nvhost_hwctx_handler ctxhandler;
	struct nvhost_module mod;
	struct nvhost_cdma cdma;
};

struct nvhost_op_pair {
	u32 op1;
	u32 op2;
};

struct nvhost_cpuinterrupt {
	u32 syncpt_val;
	void *intr_data;
};

int nvhost_channel_init(
	struct nvhost_channel *ch,
	struct nvhost_master *dev, int index);

void nvhost_channel_submit(
	struct nvhost_channel *ch,
	struct nvhost_op_pair *ops,
	int num_pairs,
	struct nvhost_cpuinterrupt *intrs,
	int num_intrs,
	struct nvmap_handle **unpins,
	int num_unpins,
	u32 syncpt_id,
	u32 syncpt_val);

struct nvhost_channel *nvhost_getchannel(struct nvhost_channel *ch);
void nvhost_putchannel(struct nvhost_channel *ch, struct nvhost_hwctx *ctx);
void nvhost_channel_suspend(struct nvhost_channel *ch);

#endif
