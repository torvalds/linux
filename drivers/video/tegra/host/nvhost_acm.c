/*
 * drivers/video/tegra/host/nvhost_acm.c
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

#include "nvhost_acm.h"
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/device.h>
#include <mach/powergate.h>
#include <mach/clk.h>

#define ACM_TIMEOUT 1*HZ

#define DISABLE_3D_POWERGATING
#define DISABLE_MPE_POWERGATING

void nvhost_module_busy(struct nvhost_module *mod)
{
	mutex_lock(&mod->lock);
	cancel_delayed_work(&mod->powerdown);
	if ((atomic_inc_return(&mod->refcount) == 1) && !mod->powered) {
		if (mod->parent)
			nvhost_module_busy(mod->parent);
		if (mod->powergate_id != -1) {
			BUG_ON(mod->num_clks != 1);
			tegra_powergate_sequence_power_up(
				mod->powergate_id, mod->clk[0]);
		} else {
			int i;
			for (i = 0; i < mod->num_clks; i++)
				clk_enable(mod->clk[i]);
		}
		if (mod->func)
			mod->func(mod, NVHOST_POWER_ACTION_ON);
		mod->powered = true;
	}
	mutex_unlock(&mod->lock);
}

static void powerdown_handler(struct work_struct *work)
{
	struct nvhost_module *mod;
	mod = container_of(to_delayed_work(work), struct nvhost_module, powerdown);
	mutex_lock(&mod->lock);
	if ((atomic_read(&mod->refcount) == 0) && mod->powered) {
		int i;
		if (mod->func)
			mod->func(mod, NVHOST_POWER_ACTION_OFF);
		for (i = 0; i < mod->num_clks; i++) {
			clk_disable(mod->clk[i]);
		}
		if (mod->powergate_id != -1) {
			tegra_periph_reset_assert(mod->clk[0]);
			tegra_powergate_power_off(mod->powergate_id);
		}
		mod->powered = false;
		if (mod->parent)
			nvhost_module_idle(mod->parent);
	}
	mutex_unlock(&mod->lock);
}

void nvhost_module_idle_mult(struct nvhost_module *mod, int refs)
{
	bool kick = false;

	mutex_lock(&mod->lock);
	if (atomic_sub_return(refs, &mod->refcount) == 0) {
		BUG_ON(!mod->powered);
		schedule_delayed_work(&mod->powerdown, ACM_TIMEOUT);
		kick = true;
	}
	mutex_unlock(&mod->lock);

	if (kick)
		wake_up(&mod->idle);
}

static const char *get_module_clk_id(const char *module, int index)
{
	if (index == 1 && strcmp(module, "gr2d") == 0)
		return "epp";
	else if (index == 0)
		return module;
	return NULL;
}

static int get_module_powergate_id(const char *module)
{
	if (strcmp(module, "gr3d") == 0)
		return TEGRA_POWERGATE_3D;
	else if (strcmp(module, "mpe") == 0)
		return TEGRA_POWERGATE_MPE;
	return -1;
}

int nvhost_module_init(struct nvhost_module *mod, const char *name,
		nvhost_modulef func, struct nvhost_module *parent,
		struct device *dev)
{
	int i = 0;
	mod->name = name;

	while (i < NVHOST_MODULE_MAX_CLOCKS) {
		long rate;
		mod->clk[i] = clk_get(dev, get_module_clk_id(name, i));
		if (IS_ERR_OR_NULL(mod->clk[i]))
			break;
		rate = clk_round_rate(mod->clk[i], UINT_MAX);
		if (rate < 0) {
			pr_err("%s: can't get maximum rate for %s\n",
				__func__, name);
			break;
		}
		if (rate != clk_get_rate(mod->clk[i])) {
			clk_set_rate(mod->clk[i], rate);
		}
		i++;
	}

	mod->num_clks = i;
	mod->func = func;
	mod->parent = parent;
	mod->powered = false;
	mod->powergate_id = get_module_powergate_id(name);

#ifdef DISABLE_3D_POWERGATING
	/*
	 * It is possible for the 3d block to generate an invalid memory
	 * request during the power up sequence in some cases.  Workaround
	 * is to disable 3d block power gating.
	 */
	if (mod->powergate_id == TEGRA_POWERGATE_3D) {
		tegra_powergate_sequence_power_up(mod->powergate_id,
			mod->clk[0]);
		clk_disable(mod->clk[0]);
		mod->powergate_id = -1;
	}
#endif

#ifdef DISABLE_MPE_POWERGATING
	/*
	 * Disable power gating for MPE as it seems to cause issues with
	 * camera record stress tests when run in loop.
	 */
	if (mod->powergate_id == TEGRA_POWERGATE_MPE) {
		tegra_powergate_sequence_power_up(mod->powergate_id,
			mod->clk[0]);
		clk_disable(mod->clk[0]);
		mod->powergate_id = -1;
	}
#endif

	mutex_init(&mod->lock);
	init_waitqueue_head(&mod->idle);
	INIT_DELAYED_WORK(&mod->powerdown, powerdown_handler);

	return 0;
}

static int is_module_idle(struct nvhost_module *mod)
{
	int count;
	mutex_lock(&mod->lock);
	count = atomic_read(&mod->refcount);
	mutex_unlock(&mod->lock);
	return (count == 0);
}

void nvhost_module_suspend(struct nvhost_module *mod)
{
	wait_event(mod->idle, is_module_idle(mod));
	flush_delayed_work(&mod->powerdown);
	BUG_ON(mod->powered);
}

void nvhost_module_deinit(struct nvhost_module *mod)
{
	int i;
	nvhost_module_suspend(mod);
	for (i = 0; i < mod->num_clks; i++)
		clk_put(mod->clk[i]);
}
