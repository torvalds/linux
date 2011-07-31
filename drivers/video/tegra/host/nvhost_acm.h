/*
 * drivers/video/tegra/host/nvhost_acm.h
 *
 * Tegra Graphics Host Automatic Clock Management
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

#ifndef __NVHOST_ACM_H
#define __NVHOST_ACM_H

#include <linux/workqueue.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/clk.h>

#define NVHOST_MODULE_MAX_CLOCKS 3

struct nvhost_module;

enum nvhost_power_action {
	NVHOST_POWER_ACTION_OFF,
	NVHOST_POWER_ACTION_ON,
};

typedef void (*nvhost_modulef)(struct nvhost_module *mod, enum nvhost_power_action action);

struct nvhost_module {
	const char *name;
	nvhost_modulef func;
	struct delayed_work powerdown;
	struct clk *clk[NVHOST_MODULE_MAX_CLOCKS];
	int num_clks;
	struct mutex lock;
	bool powered;
	atomic_t refcount;
	wait_queue_head_t idle;
	struct nvhost_module *parent;
	int powergate_id;
};

int nvhost_module_init(struct nvhost_module *mod, const char *name,
		nvhost_modulef func, struct nvhost_module *parent,
		struct device *dev);
void nvhost_module_deinit(struct nvhost_module *mod);
void nvhost_module_suspend(struct nvhost_module *mod);

void nvhost_module_busy(struct nvhost_module *mod);
void nvhost_module_idle_mult(struct nvhost_module *mod, int refs);

static inline bool nvhost_module_powered(struct nvhost_module *mod)
{
	return mod->powered;
}

static inline void nvhost_module_idle(struct nvhost_module *mod)
{
	nvhost_module_idle_mult(mod, 1);

}

#endif
