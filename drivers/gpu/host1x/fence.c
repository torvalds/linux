// SPDX-License-Identifier: GPL-2.0-only
/*
 * Syncpoint dma_fence implementation
 *
 * Copyright (c) 2020, NVIDIA Corporation.
 */

#include <linux/dma-fence.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/sync_file.h>

#include "fence.h"
#include "intr.h"
#include "syncpt.h"

static const char *host1x_syncpt_fence_get_driver_name(struct dma_fence *f)
{
	return "host1x";
}

static const char *host1x_syncpt_fence_get_timeline_name(struct dma_fence *f)
{
	return "syncpoint";
}

static struct host1x_syncpt_fence *to_host1x_fence(struct dma_fence *f)
{
	return container_of(f, struct host1x_syncpt_fence, base);
}

static bool host1x_syncpt_fence_enable_signaling(struct dma_fence *f)
{
	struct host1x_syncpt_fence *sf = to_host1x_fence(f);

	if (host1x_syncpt_is_expired(sf->sp, sf->threshold))
		return false;

	/* Reference for interrupt path. */
	dma_fence_get(f);

	/*
	 * The dma_fence framework requires the fence driver to keep a
	 * reference to any fences for which 'enable_signaling' has been
	 * called (and that have not been signalled).
	 *
	 * We cannot currently always guarantee that all fences get signalled
	 * or cancelled. As such, for such situations, set up a timeout, so
	 * that long-lasting fences will get reaped eventually.
	 */
	if (sf->timeout) {
		/* Reference for timeout path. */
		dma_fence_get(f);
		schedule_delayed_work(&sf->timeout_work, msecs_to_jiffies(30000));
	}

	host1x_intr_add_fence_locked(sf->sp->host, sf);

	/*
	 * The fence may get signalled at any time after the above call,
	 * so we need to initialize all state used by signalling
	 * before it.
	 */

	return true;
}

static const struct dma_fence_ops host1x_syncpt_fence_ops = {
	.get_driver_name = host1x_syncpt_fence_get_driver_name,
	.get_timeline_name = host1x_syncpt_fence_get_timeline_name,
	.enable_signaling = host1x_syncpt_fence_enable_signaling,
};

void host1x_fence_signal(struct host1x_syncpt_fence *f)
{
	if (atomic_xchg(&f->signaling, 1)) {
		/*
		 * Already on timeout path, but we removed the fence before
		 * timeout path could, so drop interrupt path reference.
		 */
		dma_fence_put(&f->base);
		return;
	}

	if (f->timeout && cancel_delayed_work(&f->timeout_work)) {
		/*
		 * We know that the timeout path will not be entered.
		 * Safe to drop the timeout path's reference now.
		 */
		dma_fence_put(&f->base);
	}

	dma_fence_signal_locked(&f->base);
	dma_fence_put(&f->base);
}

static void do_fence_timeout(struct work_struct *work)
{
	struct delayed_work *dwork = (struct delayed_work *)work;
	struct host1x_syncpt_fence *f =
		container_of(dwork, struct host1x_syncpt_fence, timeout_work);

	if (atomic_xchg(&f->signaling, 1)) {
		/* Already on interrupt path, drop timeout path reference if any. */
		if (f->timeout)
			dma_fence_put(&f->base);
		return;
	}

	if (host1x_intr_remove_fence(f->sp->host, f)) {
		/*
		 * Managed to remove fence from queue, so it's safe to drop
		 * the interrupt path's reference.
		 */
		dma_fence_put(&f->base);
	}

	dma_fence_set_error(&f->base, -ETIMEDOUT);
	dma_fence_signal(&f->base);
	if (f->timeout)
		dma_fence_put(&f->base);
}

struct dma_fence *host1x_fence_create(struct host1x_syncpt *sp, u32 threshold,
				      bool timeout)
{
	struct host1x_syncpt_fence *fence;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return ERR_PTR(-ENOMEM);

	fence->sp = sp;
	fence->threshold = threshold;
	fence->timeout = timeout;

	dma_fence_init(&fence->base, &host1x_syncpt_fence_ops, &sp->fences.lock,
		       dma_fence_context_alloc(1), 0);

	INIT_DELAYED_WORK(&fence->timeout_work, do_fence_timeout);

	return &fence->base;
}
EXPORT_SYMBOL(host1x_fence_create);

void host1x_fence_cancel(struct dma_fence *f)
{
	struct host1x_syncpt_fence *sf = to_host1x_fence(f);

	schedule_delayed_work(&sf->timeout_work, 0);
	flush_delayed_work(&sf->timeout_work);
}
EXPORT_SYMBOL(host1x_fence_cancel);
