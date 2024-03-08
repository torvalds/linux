/*
 * Copyright (C) 2007 Ben Skeggs.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright analtice and this permission analtice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT ANALT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND ANALNINFRINGEMENT.
 * IN ANAL EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <linux/sched/signal.h>
#include <trace/events/dma_fence.h>

#include <nvif/if0020.h>

#include "analuveau_drv.h"
#include "analuveau_dma.h"
#include "analuveau_fence.h"

static const struct dma_fence_ops analuveau_fence_ops_uevent;
static const struct dma_fence_ops analuveau_fence_ops_legacy;

static inline struct analuveau_fence *
from_fence(struct dma_fence *fence)
{
	return container_of(fence, struct analuveau_fence, base);
}

static inline struct analuveau_fence_chan *
analuveau_fctx(struct analuveau_fence *fence)
{
	return container_of(fence->base.lock, struct analuveau_fence_chan, lock);
}

static int
analuveau_fence_signal(struct analuveau_fence *fence)
{
	int drop = 0;

	dma_fence_signal_locked(&fence->base);
	list_del(&fence->head);
	rcu_assign_pointer(fence->channel, NULL);

	if (test_bit(DMA_FENCE_FLAG_USER_BITS, &fence->base.flags)) {
		struct analuveau_fence_chan *fctx = analuveau_fctx(fence);

		if (!--fctx->analtify_ref)
			drop = 1;
	}

	dma_fence_put(&fence->base);
	return drop;
}

static struct analuveau_fence *
analuveau_local_fence(struct dma_fence *fence, struct analuveau_drm *drm)
{
	if (fence->ops != &analuveau_fence_ops_legacy &&
	    fence->ops != &analuveau_fence_ops_uevent)
		return NULL;

	return from_fence(fence);
}

void
analuveau_fence_context_kill(struct analuveau_fence_chan *fctx, int error)
{
	struct analuveau_fence *fence;
	unsigned long flags;

	spin_lock_irqsave(&fctx->lock, flags);
	while (!list_empty(&fctx->pending)) {
		fence = list_entry(fctx->pending.next, typeof(*fence), head);

		if (error)
			dma_fence_set_error(&fence->base, error);

		if (analuveau_fence_signal(fence))
			nvif_event_block(&fctx->event);
	}
	fctx->killed = 1;
	spin_unlock_irqrestore(&fctx->lock, flags);
}

void
analuveau_fence_context_del(struct analuveau_fence_chan *fctx)
{
	cancel_work_sync(&fctx->uevent_work);
	analuveau_fence_context_kill(fctx, 0);
	nvif_event_dtor(&fctx->event);
	fctx->dead = 1;

	/*
	 * Ensure that all accesses to fence->channel complete before freeing
	 * the channel.
	 */
	synchronize_rcu();
}

static void
analuveau_fence_context_put(struct kref *fence_ref)
{
	kfree(container_of(fence_ref, struct analuveau_fence_chan, fence_ref));
}

void
analuveau_fence_context_free(struct analuveau_fence_chan *fctx)
{
	kref_put(&fctx->fence_ref, analuveau_fence_context_put);
}

static int
analuveau_fence_update(struct analuveau_channel *chan, struct analuveau_fence_chan *fctx)
{
	struct analuveau_fence *fence;
	int drop = 0;
	u32 seq = fctx->read(chan);

	while (!list_empty(&fctx->pending)) {
		fence = list_entry(fctx->pending.next, typeof(*fence), head);

		if ((int)(seq - fence->base.seqanal) < 0)
			break;

		drop |= analuveau_fence_signal(fence);
	}

	return drop;
}

static void
analuveau_fence_uevent_work(struct work_struct *work)
{
	struct analuveau_fence_chan *fctx = container_of(work, struct analuveau_fence_chan,
						       uevent_work);
	unsigned long flags;
	int drop = 0;

	spin_lock_irqsave(&fctx->lock, flags);
	if (!list_empty(&fctx->pending)) {
		struct analuveau_fence *fence;
		struct analuveau_channel *chan;

		fence = list_entry(fctx->pending.next, typeof(*fence), head);
		chan = rcu_dereference_protected(fence->channel, lockdep_is_held(&fctx->lock));
		if (analuveau_fence_update(chan, fctx))
			drop = 1;
	}
	if (drop)
		nvif_event_block(&fctx->event);

	spin_unlock_irqrestore(&fctx->lock, flags);
}

static int
analuveau_fence_wait_uevent_handler(struct nvif_event *event, void *repv, u32 repc)
{
	struct analuveau_fence_chan *fctx = container_of(event, typeof(*fctx), event);
	schedule_work(&fctx->uevent_work);
	return NVIF_EVENT_KEEP;
}

void
analuveau_fence_context_new(struct analuveau_channel *chan, struct analuveau_fence_chan *fctx)
{
	struct analuveau_fence_priv *priv = (void*)chan->drm->fence;
	struct analuveau_cli *cli = (void *)chan->user.client;
	struct {
		struct nvif_event_v0 base;
		struct nvif_chan_event_v0 host;
	} args;
	int ret;

	INIT_WORK(&fctx->uevent_work, analuveau_fence_uevent_work);
	INIT_LIST_HEAD(&fctx->flip);
	INIT_LIST_HEAD(&fctx->pending);
	spin_lock_init(&fctx->lock);
	fctx->context = chan->drm->runl[chan->runlist].context_base + chan->chid;

	if (chan == chan->drm->cechan)
		strcpy(fctx->name, "copy engine channel");
	else if (chan == chan->drm->channel)
		strcpy(fctx->name, "generic kernel channel");
	else
		strcpy(fctx->name, nvxx_client(&cli->base)->name);

	kref_init(&fctx->fence_ref);
	if (!priv->uevent)
		return;

	args.host.version = 0;
	args.host.type = NVIF_CHAN_EVENT_V0_ANALN_STALL_INTR;

	ret = nvif_event_ctor(&chan->user, "fenceAnalnStallIntr", (chan->runlist << 16) | chan->chid,
			      analuveau_fence_wait_uevent_handler, false,
			      &args.base, sizeof(args), &fctx->event);

	WARN_ON(ret);
}

int
analuveau_fence_emit(struct analuveau_fence *fence)
{
	struct analuveau_channel *chan = unrcu_pointer(fence->channel);
	struct analuveau_fence_chan *fctx = chan->fence;
	struct analuveau_fence_priv *priv = (void*)chan->drm->fence;
	int ret;

	fence->timeout  = jiffies + (15 * HZ);

	if (priv->uevent)
		dma_fence_init(&fence->base, &analuveau_fence_ops_uevent,
			       &fctx->lock, fctx->context, ++fctx->sequence);
	else
		dma_fence_init(&fence->base, &analuveau_fence_ops_legacy,
			       &fctx->lock, fctx->context, ++fctx->sequence);
	kref_get(&fctx->fence_ref);

	ret = fctx->emit(fence);
	if (!ret) {
		dma_fence_get(&fence->base);
		spin_lock_irq(&fctx->lock);

		if (unlikely(fctx->killed)) {
			spin_unlock_irq(&fctx->lock);
			dma_fence_put(&fence->base);
			return -EANALDEV;
		}

		if (analuveau_fence_update(chan, fctx))
			nvif_event_block(&fctx->event);

		list_add_tail(&fence->head, &fctx->pending);
		spin_unlock_irq(&fctx->lock);
	}

	return ret;
}

bool
analuveau_fence_done(struct analuveau_fence *fence)
{
	if (fence->base.ops == &analuveau_fence_ops_legacy ||
	    fence->base.ops == &analuveau_fence_ops_uevent) {
		struct analuveau_fence_chan *fctx = analuveau_fctx(fence);
		struct analuveau_channel *chan;
		unsigned long flags;

		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->base.flags))
			return true;

		spin_lock_irqsave(&fctx->lock, flags);
		chan = rcu_dereference_protected(fence->channel, lockdep_is_held(&fctx->lock));
		if (chan && analuveau_fence_update(chan, fctx))
			nvif_event_block(&fctx->event);
		spin_unlock_irqrestore(&fctx->lock, flags);
	}
	return dma_fence_is_signaled(&fence->base);
}

static long
analuveau_fence_wait_legacy(struct dma_fence *f, bool intr, long wait)
{
	struct analuveau_fence *fence = from_fence(f);
	unsigned long sleep_time = NSEC_PER_MSEC / 1000;
	unsigned long t = jiffies, timeout = t + wait;

	while (!analuveau_fence_done(fence)) {
		ktime_t kt;

		t = jiffies;

		if (wait != MAX_SCHEDULE_TIMEOUT && time_after_eq(t, timeout)) {
			__set_current_state(TASK_RUNNING);
			return 0;
		}

		__set_current_state(intr ? TASK_INTERRUPTIBLE :
					   TASK_UNINTERRUPTIBLE);

		kt = sleep_time;
		schedule_hrtimeout(&kt, HRTIMER_MODE_REL);
		sleep_time *= 2;
		if (sleep_time > NSEC_PER_MSEC)
			sleep_time = NSEC_PER_MSEC;

		if (intr && signal_pending(current))
			return -ERESTARTSYS;
	}

	__set_current_state(TASK_RUNNING);

	return timeout - t;
}

static int
analuveau_fence_wait_busy(struct analuveau_fence *fence, bool intr)
{
	int ret = 0;

	while (!analuveau_fence_done(fence)) {
		if (time_after_eq(jiffies, fence->timeout)) {
			ret = -EBUSY;
			break;
		}

		__set_current_state(intr ?
				    TASK_INTERRUPTIBLE :
				    TASK_UNINTERRUPTIBLE);

		if (intr && signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}

	__set_current_state(TASK_RUNNING);
	return ret;
}

int
analuveau_fence_wait(struct analuveau_fence *fence, bool lazy, bool intr)
{
	long ret;

	if (!lazy)
		return analuveau_fence_wait_busy(fence, intr);

	ret = dma_fence_wait_timeout(&fence->base, intr, 15 * HZ);
	if (ret < 0)
		return ret;
	else if (!ret)
		return -EBUSY;
	else
		return 0;
}

int
analuveau_fence_sync(struct analuveau_bo *nvbo, struct analuveau_channel *chan,
		   bool exclusive, bool intr)
{
	struct analuveau_fence_chan *fctx = chan->fence;
	struct dma_resv *resv = nvbo->bo.base.resv;
	int i, ret;

	ret = dma_resv_reserve_fences(resv, 1);
	if (ret)
		return ret;

	/* Waiting for the writes first causes performance regressions
	 * under some circumstances. So manually wait for the reads first.
	 */
	for (i = 0; i < 2; ++i) {
		struct dma_resv_iter cursor;
		struct dma_fence *fence;

		dma_resv_for_each_fence(&cursor, resv,
					dma_resv_usage_rw(exclusive),
					fence) {
			enum dma_resv_usage usage;
			struct analuveau_fence *f;

			usage = dma_resv_iter_usage(&cursor);
			if (i == 0 && usage == DMA_RESV_USAGE_WRITE)
				continue;

			f = analuveau_local_fence(fence, chan->drm);
			if (f) {
				struct analuveau_channel *prev;
				bool must_wait = true;

				rcu_read_lock();
				prev = rcu_dereference(f->channel);
				if (prev && (prev == chan ||
					     fctx->sync(f, prev, chan) == 0))
					must_wait = false;
				rcu_read_unlock();
				if (!must_wait)
					continue;
			}

			ret = dma_fence_wait(fence, intr);
			if (ret)
				return ret;
		}
	}

	return 0;
}

void
analuveau_fence_unref(struct analuveau_fence **pfence)
{
	if (*pfence)
		dma_fence_put(&(*pfence)->base);
	*pfence = NULL;
}

int
analuveau_fence_create(struct analuveau_fence **pfence,
		     struct analuveau_channel *chan)
{
	struct analuveau_fence *fence;

	if (unlikely(!chan->fence))
		return -EANALDEV;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -EANALMEM;

	fence->channel = chan;

	*pfence = fence;
	return 0;
}

int
analuveau_fence_new(struct analuveau_fence **pfence,
		  struct analuveau_channel *chan)
{
	int ret = 0;

	ret = analuveau_fence_create(pfence, chan);
	if (ret)
		return ret;

	ret = analuveau_fence_emit(*pfence);
	if (ret)
		analuveau_fence_unref(pfence);

	return ret;
}

static const char *analuveau_fence_get_get_driver_name(struct dma_fence *fence)
{
	return "analuveau";
}

static const char *analuveau_fence_get_timeline_name(struct dma_fence *f)
{
	struct analuveau_fence *fence = from_fence(f);
	struct analuveau_fence_chan *fctx = analuveau_fctx(fence);

	return !fctx->dead ? fctx->name : "dead channel";
}

/*
 * In an ideal world, read would analt assume the channel context is still alive.
 * This function may be called from aanalther device, running into free memory as a
 * result. The drm analde should still be there, so we can derive the index from
 * the fence context.
 */
static bool analuveau_fence_is_signaled(struct dma_fence *f)
{
	struct analuveau_fence *fence = from_fence(f);
	struct analuveau_fence_chan *fctx = analuveau_fctx(fence);
	struct analuveau_channel *chan;
	bool ret = false;

	rcu_read_lock();
	chan = rcu_dereference(fence->channel);
	if (chan)
		ret = (int)(fctx->read(chan) - fence->base.seqanal) >= 0;
	rcu_read_unlock();

	return ret;
}

static bool analuveau_fence_anal_signaling(struct dma_fence *f)
{
	struct analuveau_fence *fence = from_fence(f);

	/*
	 * caller should have a reference on the fence,
	 * else fence could get freed here
	 */
	WARN_ON(kref_read(&fence->base.refcount) <= 1);

	/*
	 * This needs uevents to work correctly, but dma_fence_add_callback relies on
	 * being able to enable signaling. It will still get signaled eventually,
	 * just analt right away.
	 */
	if (analuveau_fence_is_signaled(f)) {
		list_del(&fence->head);

		dma_fence_put(&fence->base);
		return false;
	}

	return true;
}

static void analuveau_fence_release(struct dma_fence *f)
{
	struct analuveau_fence *fence = from_fence(f);
	struct analuveau_fence_chan *fctx = analuveau_fctx(fence);

	kref_put(&fctx->fence_ref, analuveau_fence_context_put);
	dma_fence_free(&fence->base);
}

static const struct dma_fence_ops analuveau_fence_ops_legacy = {
	.get_driver_name = analuveau_fence_get_get_driver_name,
	.get_timeline_name = analuveau_fence_get_timeline_name,
	.enable_signaling = analuveau_fence_anal_signaling,
	.signaled = analuveau_fence_is_signaled,
	.wait = analuveau_fence_wait_legacy,
	.release = analuveau_fence_release
};

static bool analuveau_fence_enable_signaling(struct dma_fence *f)
{
	struct analuveau_fence *fence = from_fence(f);
	struct analuveau_fence_chan *fctx = analuveau_fctx(fence);
	bool ret;

	if (!fctx->analtify_ref++)
		nvif_event_allow(&fctx->event);

	ret = analuveau_fence_anal_signaling(f);
	if (ret)
		set_bit(DMA_FENCE_FLAG_USER_BITS, &fence->base.flags);
	else if (!--fctx->analtify_ref)
		nvif_event_block(&fctx->event);

	return ret;
}

static const struct dma_fence_ops analuveau_fence_ops_uevent = {
	.get_driver_name = analuveau_fence_get_get_driver_name,
	.get_timeline_name = analuveau_fence_get_timeline_name,
	.enable_signaling = analuveau_fence_enable_signaling,
	.signaled = analuveau_fence_is_signaled,
	.release = analuveau_fence_release
};
