/*
 * Copyright (c) 2012-2013, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HOST1X_DEV_H
#define HOST1X_DEV_H

#include <linux/platform_device.h>
#include <linux/device.h>

#include "syncpt.h"

struct host1x_syncpt;

struct host1x_syncpt_ops {
	void (*restore)(struct host1x_syncpt *syncpt);
	void (*restore_wait_base)(struct host1x_syncpt *syncpt);
	void (*load_wait_base)(struct host1x_syncpt *syncpt);
	u32 (*load)(struct host1x_syncpt *syncpt);
	void (*cpu_incr)(struct host1x_syncpt *syncpt);
	int (*patch_wait)(struct host1x_syncpt *syncpt, void *patch_addr);
};

struct host1x_info {
	int	nb_channels;		/* host1x: num channels supported */
	int	nb_pts;			/* host1x: num syncpoints supported */
	int	nb_bases;		/* host1x: num syncpoints supported */
	int	nb_mlocks;		/* host1x: number of mlocks */
	int	(*init)(struct host1x *); /* initialize per SoC ops */
	int	sync_offset;
};

struct host1x {
	const struct host1x_info *info;

	void __iomem *regs;
	struct host1x_syncpt *syncpt;
	struct device *dev;
	struct clk *clk;

	const struct host1x_syncpt_ops *syncpt_op;
};

void host1x_sync_writel(struct host1x *host1x, u32 r, u32 v);
u32 host1x_sync_readl(struct host1x *host1x, u32 r);

static inline void host1x_hw_syncpt_restore(struct host1x *host,
					    struct host1x_syncpt *sp)
{
	host->syncpt_op->restore(sp);
}

static inline void host1x_hw_syncpt_restore_wait_base(struct host1x *host,
						      struct host1x_syncpt *sp)
{
	host->syncpt_op->restore_wait_base(sp);
}

static inline void host1x_hw_syncpt_load_wait_base(struct host1x *host,
						   struct host1x_syncpt *sp)
{
	host->syncpt_op->load_wait_base(sp);
}

static inline u32 host1x_hw_syncpt_load(struct host1x *host,
					struct host1x_syncpt *sp)
{
	return host->syncpt_op->load(sp);
}

static inline void host1x_hw_syncpt_cpu_incr(struct host1x *host,
					     struct host1x_syncpt *sp)
{
	host->syncpt_op->cpu_incr(sp);
}

static inline int host1x_hw_syncpt_patch_wait(struct host1x *host,
					      struct host1x_syncpt *sp,
					      void *patch_addr)
{
	return host->syncpt_op->patch_wait(sp, patch_addr);
}

#endif
