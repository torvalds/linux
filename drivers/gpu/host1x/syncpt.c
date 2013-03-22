/*
 * Tegra host1x Syncpoints
 *
 * Copyright (c) 2010-2013, NVIDIA Corporation.
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

#include <linux/module.h>
#include <linux/device.h>
#include <linux/slab.h>

#include <trace/events/host1x.h>

#include "syncpt.h"
#include "dev.h"

static struct host1x_syncpt *_host1x_syncpt_alloc(struct host1x *host,
						  struct device *dev,
						  int client_managed)
{
	int i;
	struct host1x_syncpt *sp = host->syncpt;
	char *name;

	for (i = 0; i < host->info->nb_pts && sp->name; i++, sp++)
		;
	if (sp->dev)
		return NULL;

	name = kasprintf(GFP_KERNEL, "%02d-%s", sp->id,
			dev ? dev_name(dev) : NULL);
	if (!name)
		return NULL;

	sp->dev = dev;
	sp->name = name;
	sp->client_managed = client_managed;

	return sp;
}

u32 host1x_syncpt_id(struct host1x_syncpt *sp)
{
	return sp->id;
}

/*
 * Updates the value sent to hardware.
 */
u32 host1x_syncpt_incr_max(struct host1x_syncpt *sp, u32 incrs)
{
	return (u32)atomic_add_return(incrs, &sp->max_val);
}

 /*
 * Write cached syncpoint and waitbase values to hardware.
 */
void host1x_syncpt_restore(struct host1x *host)
{
	struct host1x_syncpt *sp_base = host->syncpt;
	u32 i;

	for (i = 0; i < host1x_syncpt_nb_pts(host); i++)
		host1x_hw_syncpt_restore(host, sp_base + i);
	for (i = 0; i < host1x_syncpt_nb_bases(host); i++)
		host1x_hw_syncpt_restore_wait_base(host, sp_base + i);
	wmb();
}

/*
 * Update the cached syncpoint and waitbase values by reading them
 * from the registers.
  */
void host1x_syncpt_save(struct host1x *host)
{
	struct host1x_syncpt *sp_base = host->syncpt;
	u32 i;

	for (i = 0; i < host1x_syncpt_nb_pts(host); i++) {
		if (host1x_syncpt_client_managed(sp_base + i))
			host1x_hw_syncpt_load(host, sp_base + i);
		else
			WARN_ON(!host1x_syncpt_idle(sp_base + i));
	}

	for (i = 0; i < host1x_syncpt_nb_bases(host); i++)
		host1x_hw_syncpt_load_wait_base(host, sp_base + i);
}

/*
 * Updates the cached syncpoint value by reading a new value from the hardware
 * register
 */
u32 host1x_syncpt_load(struct host1x_syncpt *sp)
{
	u32 val;
	val = host1x_hw_syncpt_load(sp->host, sp);
	trace_host1x_syncpt_load_min(sp->id, val);

	return val;
}

/*
 * Get the current syncpoint base
 */
u32 host1x_syncpt_load_wait_base(struct host1x_syncpt *sp)
{
	u32 val;
	host1x_hw_syncpt_load_wait_base(sp->host, sp);
	val = sp->base_val;
	return val;
}

/*
 * Write a cpu syncpoint increment to the hardware, without touching
 * the cache. Caller is responsible for host being powered.
 */
void host1x_syncpt_cpu_incr(struct host1x_syncpt *sp)
{
	host1x_hw_syncpt_cpu_incr(sp->host, sp);
}

/*
 * Increment syncpoint value from cpu, updating cache
 */
void host1x_syncpt_incr(struct host1x_syncpt *sp)
{
	if (host1x_syncpt_client_managed(sp))
		host1x_syncpt_incr_max(sp, 1);
	host1x_syncpt_cpu_incr(sp);
}

int host1x_syncpt_init(struct host1x *host)
{
	struct host1x_syncpt *syncpt;
	int i;

	syncpt = devm_kzalloc(host->dev, sizeof(*syncpt) * host->info->nb_pts,
		GFP_KERNEL);
	if (!syncpt)
		return -ENOMEM;

	for (i = 0; i < host->info->nb_pts; ++i) {
		syncpt[i].id = i;
		syncpt[i].host = host;
	}

	host->syncpt = syncpt;

	host1x_syncpt_restore(host);

	return 0;
}

struct host1x_syncpt *host1x_syncpt_request(struct device *dev,
					    int client_managed)
{
	struct host1x *host = dev_get_drvdata(dev->parent);
	return _host1x_syncpt_alloc(host, dev, client_managed);
}

void host1x_syncpt_free(struct host1x_syncpt *sp)
{
	if (!sp)
		return;

	kfree(sp->name);
	sp->dev = NULL;
	sp->name = NULL;
	sp->client_managed = 0;
}

void host1x_syncpt_deinit(struct host1x *host)
{
	int i;
	struct host1x_syncpt *sp = host->syncpt;
	for (i = 0; i < host->info->nb_pts; i++, sp++)
		kfree(sp->name);
}

int host1x_syncpt_nb_pts(struct host1x *host)
{
	return host->info->nb_pts;
}

int host1x_syncpt_nb_bases(struct host1x *host)
{
	return host->info->nb_bases;
}

int host1x_syncpt_nb_mlocks(struct host1x *host)
{
	return host->info->nb_mlocks;
}

struct host1x_syncpt *host1x_syncpt_get(struct host1x *host, u32 id)
{
	if (host->info->nb_pts < id)
		return NULL;
	return host->syncpt + id;
}
