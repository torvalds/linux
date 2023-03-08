// SPDX-License-Identifier: GPL-2.0-only
/*
 * Tegra host1x Syncpoints
 *
 * Copyright (c) 2010-2015, NVIDIA Corporation.
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/dma-fence.h>
#include <linux/slab.h>

#include <trace/events/host1x.h>

#include "syncpt.h"
#include "dev.h"
#include "intr.h"
#include "debug.h"

#define SYNCPT_CHECK_PERIOD (2 * HZ)
#define MAX_STUCK_CHECK_COUNT 15

static struct host1x_syncpt_base *
host1x_syncpt_base_request(struct host1x *host)
{
	struct host1x_syncpt_base *bases = host->bases;
	unsigned int i;

	for (i = 0; i < host->info->nb_bases; i++)
		if (!bases[i].requested)
			break;

	if (i >= host->info->nb_bases)
		return NULL;

	bases[i].requested = true;
	return &bases[i];
}

static void host1x_syncpt_base_free(struct host1x_syncpt_base *base)
{
	if (base)
		base->requested = false;
}

/**
 * host1x_syncpt_alloc() - allocate a syncpoint
 * @host: host1x device data
 * @flags: bitfield of HOST1X_SYNCPT_* flags
 * @name: name for the syncpoint for use in debug prints
 *
 * Allocates a hardware syncpoint for the caller's use. The caller then has
 * the sole authority to mutate the syncpoint's value until it is freed again.
 *
 * If no free syncpoints are available, or a NULL name was specified, returns
 * NULL.
 */
struct host1x_syncpt *host1x_syncpt_alloc(struct host1x *host,
					  unsigned long flags,
					  const char *name)
{
	struct host1x_syncpt *sp = host->syncpt;
	char *full_name;
	unsigned int i;

	if (!name)
		return NULL;

	mutex_lock(&host->syncpt_mutex);

	for (i = 0; i < host->info->nb_pts && kref_read(&sp->ref); i++, sp++)
		;

	if (i >= host->info->nb_pts)
		goto unlock;

	if (flags & HOST1X_SYNCPT_HAS_BASE) {
		sp->base = host1x_syncpt_base_request(host);
		if (!sp->base)
			goto unlock;
	}

	full_name = kasprintf(GFP_KERNEL, "%u-%s", sp->id, name);
	if (!full_name)
		goto free_base;

	sp->name = full_name;

	if (flags & HOST1X_SYNCPT_CLIENT_MANAGED)
		sp->client_managed = true;
	else
		sp->client_managed = false;

	kref_init(&sp->ref);

	mutex_unlock(&host->syncpt_mutex);
	return sp;

free_base:
	host1x_syncpt_base_free(sp->base);
	sp->base = NULL;
unlock:
	mutex_unlock(&host->syncpt_mutex);
	return NULL;
}
EXPORT_SYMBOL(host1x_syncpt_alloc);

/**
 * host1x_syncpt_id() - retrieve syncpoint ID
 * @sp: host1x syncpoint
 *
 * Given a pointer to a struct host1x_syncpt, retrieves its ID. This ID is
 * often used as a value to program into registers that control how hardware
 * blocks interact with syncpoints.
 */
u32 host1x_syncpt_id(struct host1x_syncpt *sp)
{
	return sp->id;
}
EXPORT_SYMBOL(host1x_syncpt_id);

/**
 * host1x_syncpt_incr_max() - update the value sent to hardware
 * @sp: host1x syncpoint
 * @incrs: number of increments
 */
u32 host1x_syncpt_incr_max(struct host1x_syncpt *sp, u32 incrs)
{
	return (u32)atomic_add_return(incrs, &sp->max_val);
}
EXPORT_SYMBOL(host1x_syncpt_incr_max);

 /*
 * Write cached syncpoint and waitbase values to hardware.
 */
void host1x_syncpt_restore(struct host1x *host)
{
	struct host1x_syncpt *sp_base = host->syncpt;
	unsigned int i;

	for (i = 0; i < host1x_syncpt_nb_pts(host); i++) {
		/*
		 * Unassign syncpt from channels for purposes of Tegra186
		 * syncpoint protection. This prevents any channel from
		 * accessing it until it is reassigned.
		 */
		host1x_hw_syncpt_assign_to_channel(host, sp_base + i, NULL);
		host1x_hw_syncpt_restore(host, sp_base + i);
	}

	for (i = 0; i < host1x_syncpt_nb_bases(host); i++)
		host1x_hw_syncpt_restore_wait_base(host, sp_base + i);

	host1x_hw_syncpt_enable_protection(host);

	wmb();
}

/*
 * Update the cached syncpoint and waitbase values by reading them
 * from the registers.
  */
void host1x_syncpt_save(struct host1x *host)
{
	struct host1x_syncpt *sp_base = host->syncpt;
	unsigned int i;

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
	host1x_hw_syncpt_load_wait_base(sp->host, sp);

	return sp->base_val;
}

/**
 * host1x_syncpt_incr() - increment syncpoint value from CPU, updating cache
 * @sp: host1x syncpoint
 */
int host1x_syncpt_incr(struct host1x_syncpt *sp)
{
	return host1x_hw_syncpt_cpu_incr(sp->host, sp);
}
EXPORT_SYMBOL(host1x_syncpt_incr);

/**
 * host1x_syncpt_wait() - wait for a syncpoint to reach a given value
 * @sp: host1x syncpoint
 * @thresh: threshold
 * @timeout: maximum time to wait for the syncpoint to reach the given value
 * @value: return location for the syncpoint value
 */
int host1x_syncpt_wait(struct host1x_syncpt *sp, u32 thresh, long timeout,
		       u32 *value)
{
	struct dma_fence *fence;
	long wait_err;

	host1x_hw_syncpt_load(sp->host, sp);

	if (value)
		*value = host1x_syncpt_load(sp);

	if (host1x_syncpt_is_expired(sp, thresh))
		return 0;

	if (timeout < 0)
		timeout = LONG_MAX;
	else if (timeout == 0)
		return -EAGAIN;

	fence = host1x_fence_create(sp, thresh, false);
	if (IS_ERR(fence))
		return PTR_ERR(fence);

	wait_err = dma_fence_wait_timeout(fence, true, timeout);
	if (wait_err == 0)
		host1x_fence_cancel(fence);
	dma_fence_put(fence);

	if (value)
		*value = host1x_syncpt_load(sp);

	if (wait_err == 0)
		return -EAGAIN;
	else if (wait_err < 0)
		return wait_err;
	else
		return 0;
}
EXPORT_SYMBOL(host1x_syncpt_wait);

/*
 * Returns true if syncpoint is expired, false if we may need to wait
 */
bool host1x_syncpt_is_expired(struct host1x_syncpt *sp, u32 thresh)
{
	u32 current_val;

	smp_rmb();

	current_val = (u32)atomic_read(&sp->min_val);

	return ((current_val - thresh) & 0x80000000U) == 0U;
}

int host1x_syncpt_init(struct host1x *host)
{
	struct host1x_syncpt_base *bases;
	struct host1x_syncpt *syncpt;
	unsigned int i;

	syncpt = devm_kcalloc(host->dev, host->info->nb_pts, sizeof(*syncpt),
			      GFP_KERNEL);
	if (!syncpt)
		return -ENOMEM;

	bases = devm_kcalloc(host->dev, host->info->nb_bases, sizeof(*bases),
			     GFP_KERNEL);
	if (!bases)
		return -ENOMEM;

	for (i = 0; i < host->info->nb_pts; i++) {
		syncpt[i].id = i;
		syncpt[i].host = host;
	}

	for (i = 0; i < host->info->nb_bases; i++)
		bases[i].id = i;

	mutex_init(&host->syncpt_mutex);
	host->syncpt = syncpt;
	host->bases = bases;

	/* Allocate sync point to use for clearing waits for expired fences */
	host->nop_sp = host1x_syncpt_alloc(host, 0, "reserved-nop");
	if (!host->nop_sp)
		return -ENOMEM;

	if (host->info->reserve_vblank_syncpts) {
		kref_init(&host->syncpt[26].ref);
		kref_init(&host->syncpt[27].ref);
	}

	return 0;
}

/**
 * host1x_syncpt_request() - request a syncpoint
 * @client: client requesting the syncpoint
 * @flags: flags
 *
 * host1x client drivers can use this function to allocate a syncpoint for
 * subsequent use. A syncpoint returned by this function will be reserved for
 * use by the client exclusively. When no longer using a syncpoint, a host1x
 * client driver needs to release it using host1x_syncpt_put().
 */
struct host1x_syncpt *host1x_syncpt_request(struct host1x_client *client,
					    unsigned long flags)
{
	struct host1x *host = dev_get_drvdata(client->host->parent);

	return host1x_syncpt_alloc(host, flags, dev_name(client->dev));
}
EXPORT_SYMBOL(host1x_syncpt_request);

static void syncpt_release(struct kref *ref)
{
	struct host1x_syncpt *sp = container_of(ref, struct host1x_syncpt, ref);

	atomic_set(&sp->max_val, host1x_syncpt_read(sp));

	sp->locked = false;

	mutex_lock(&sp->host->syncpt_mutex);

	host1x_syncpt_base_free(sp->base);
	kfree(sp->name);
	sp->base = NULL;
	sp->name = NULL;
	sp->client_managed = false;

	mutex_unlock(&sp->host->syncpt_mutex);
}

/**
 * host1x_syncpt_put() - free a requested syncpoint
 * @sp: host1x syncpoint
 *
 * Release a syncpoint previously allocated using host1x_syncpt_request(). A
 * host1x client driver should call this when the syncpoint is no longer in
 * use.
 */
void host1x_syncpt_put(struct host1x_syncpt *sp)
{
	if (!sp)
		return;

	kref_put(&sp->ref, syncpt_release);
}
EXPORT_SYMBOL(host1x_syncpt_put);

void host1x_syncpt_deinit(struct host1x *host)
{
	struct host1x_syncpt *sp = host->syncpt;
	unsigned int i;

	for (i = 0; i < host->info->nb_pts; i++, sp++)
		kfree(sp->name);
}

/**
 * host1x_syncpt_read_max() - read maximum syncpoint value
 * @sp: host1x syncpoint
 *
 * The maximum syncpoint value indicates how many operations there are in
 * queue, either in channel or in a software thread.
 */
u32 host1x_syncpt_read_max(struct host1x_syncpt *sp)
{
	smp_rmb();

	return (u32)atomic_read(&sp->max_val);
}
EXPORT_SYMBOL(host1x_syncpt_read_max);

/**
 * host1x_syncpt_read_min() - read minimum syncpoint value
 * @sp: host1x syncpoint
 *
 * The minimum syncpoint value is a shadow of the current sync point value in
 * hardware.
 */
u32 host1x_syncpt_read_min(struct host1x_syncpt *sp)
{
	smp_rmb();

	return (u32)atomic_read(&sp->min_val);
}
EXPORT_SYMBOL(host1x_syncpt_read_min);

/**
 * host1x_syncpt_read() - read the current syncpoint value
 * @sp: host1x syncpoint
 */
u32 host1x_syncpt_read(struct host1x_syncpt *sp)
{
	return host1x_syncpt_load(sp);
}
EXPORT_SYMBOL(host1x_syncpt_read);

unsigned int host1x_syncpt_nb_pts(struct host1x *host)
{
	return host->info->nb_pts;
}

unsigned int host1x_syncpt_nb_bases(struct host1x *host)
{
	return host->info->nb_bases;
}

unsigned int host1x_syncpt_nb_mlocks(struct host1x *host)
{
	return host->info->nb_mlocks;
}

/**
 * host1x_syncpt_get_by_id() - obtain a syncpoint by ID
 * @host: host1x controller
 * @id: syncpoint ID
 */
struct host1x_syncpt *host1x_syncpt_get_by_id(struct host1x *host,
					      unsigned int id)
{
	if (id >= host->info->nb_pts)
		return NULL;

	if (kref_get_unless_zero(&host->syncpt[id].ref))
		return &host->syncpt[id];
	else
		return NULL;
}
EXPORT_SYMBOL(host1x_syncpt_get_by_id);

/**
 * host1x_syncpt_get_by_id_noref() - obtain a syncpoint by ID but don't
 * 	increase the refcount.
 * @host: host1x controller
 * @id: syncpoint ID
 */
struct host1x_syncpt *host1x_syncpt_get_by_id_noref(struct host1x *host,
						    unsigned int id)
{
	if (id >= host->info->nb_pts)
		return NULL;

	return &host->syncpt[id];
}
EXPORT_SYMBOL(host1x_syncpt_get_by_id_noref);

/**
 * host1x_syncpt_get() - increment syncpoint refcount
 * @sp: syncpoint
 */
struct host1x_syncpt *host1x_syncpt_get(struct host1x_syncpt *sp)
{
	kref_get(&sp->ref);

	return sp;
}
EXPORT_SYMBOL(host1x_syncpt_get);

/**
 * host1x_syncpt_get_base() - obtain the wait base associated with a syncpoint
 * @sp: host1x syncpoint
 */
struct host1x_syncpt_base *host1x_syncpt_get_base(struct host1x_syncpt *sp)
{
	return sp ? sp->base : NULL;
}
EXPORT_SYMBOL(host1x_syncpt_get_base);

/**
 * host1x_syncpt_base_id() - retrieve the ID of a syncpoint wait base
 * @base: host1x syncpoint wait base
 */
u32 host1x_syncpt_base_id(struct host1x_syncpt_base *base)
{
	return base->id;
}
EXPORT_SYMBOL(host1x_syncpt_base_id);

static void do_nothing(struct kref *ref)
{
}

/**
 * host1x_syncpt_release_vblank_reservation() - Make VBLANK syncpoint
 *   available for allocation
 *
 * @client: host1x bus client
 * @syncpt_id: syncpoint ID to make available
 *
 * Makes VBLANK<i> syncpoint available for allocatation if it was
 * reserved at initialization time. This should be called by the display
 * driver after it has ensured that any VBLANK increment programming configured
 * by the boot chain has been disabled.
 */
void host1x_syncpt_release_vblank_reservation(struct host1x_client *client,
					      u32 syncpt_id)
{
	struct host1x *host = dev_get_drvdata(client->host->parent);

	if (!host->info->reserve_vblank_syncpts)
		return;

	kref_put(&host->syncpt[syncpt_id].ref, do_nothing);
}
EXPORT_SYMBOL(host1x_syncpt_release_vblank_reservation);
