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
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
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

#include "nouveau_drv.h"
#include "nouveau_dma.h"
#include "nouveau_fence.h"

static const struct dma_fence_ops nouveau_fence_ops_uevent;
static const struct dma_fence_ops nouveau_fence_ops_legacy;

static inline struct nouveau_fence_chan *
nouveau_fctx(struct nouveau_fence *fence)
{
	return container_of(fence->base.lock, struct nouveau_fence_chan, lock);
}

static bool
nouveau_fence_signal(struct nouveau_fence *fence)
{
	bool drop = false;

	dma_fence_signal_locked(&fence->base);
	list_del(&fence->head);
	rcu_assign_pointer(fence->channel, NULL);

	if (test_bit(DMA_FENCE_FLAG_USER_BITS, &fence->base.flags)) {
		struct nouveau_fence_chan *fctx = nouveau_fctx(fence);

		if (!--fctx->notify_ref)
			drop = true;
	}

	dma_fence_put(&fence->base);
	return drop;
}

static struct nouveau_fence *
nouveau_local_fence(struct dma_fence *fence, struct nouveau_drm *drm)
{
	if (fence->ops != &nouveau_fence_ops_legacy &&
	    fence->ops != &nouveau_fence_ops_uevent)
		return NULL;

	return to_nouveau_fence(fence);
}

void
nouveau_fence_context_kill(struct nouveau_fence_chan *fctx, int error)
{
	struct nouveau_fence *fence, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&fctx->lock, flags);
	list_for_each_entry_safe(fence, tmp, &fctx->pending, head) {
		if (error && !dma_fence_is_signaled_locked(&fence->base))
			dma_fence_set_error(&fence->base, error);

		if (nouveau_fence_signal(fence))
			nvif_event_block(&fctx->event);
	}
	fctx->killed = 1;
	spin_unlock_irqrestore(&fctx->lock, flags);
}

void
nouveau_fence_context_del(struct nouveau_fence_chan *fctx)
{
	cancel_work_sync(&fctx->uevent_work);
	nouveau_fence_context_kill(fctx, 0);
	nvif_event_dtor(&fctx->event);
	fctx->dead = 1;

	/*
	 * Ensure that all accesses to fence->channel complete before freeing
	 * the channel.
	 */
	synchronize_rcu();
}

static void
nouveau_fence_context_put(struct kref *fence_ref)
{
	kfree(container_of(fence_ref, struct nouveau_fence_chan, fence_ref));
}

void
nouveau_fence_context_free(struct nouveau_fence_chan *fctx)
{
	kref_put(&fctx->fence_ref, nouveau_fence_context_put);
}

static void
nouveau_fence_update(struct nouveau_channel *chan, struct nouveau_fence_chan *fctx)
{
	struct nouveau_fence *fence, *tmp;
	bool drop = false;
	u32 seq = fctx->read(chan);

	list_for_each_entry_safe(fence, tmp, &fctx->pending, head) {
		if ((int)(seq - fence->base.seqno) < 0)
			break;

		if (nouveau_fence_signal(fence))
			drop = true;
	}

	if (drop)
		nvif_event_block(&fctx->event);
}

static void
nouveau_fence_uevent_work(struct work_struct *work)
{
	struct nouveau_fence_chan *fctx = container_of(work, struct nouveau_fence_chan,
						       uevent_work);
	struct nouveau_channel *chan;
	struct nouveau_fence *fence;
	unsigned long flags;

	spin_lock_irqsave(&fctx->lock, flags);
	fence = list_first_entry_or_null(&fctx->pending, typeof(*fence), head);
	if (fence) {
		chan = rcu_dereference_protected(fence->channel, lockdep_is_held(&fctx->lock));
		nouveau_fence_update(chan, fctx);
	}
	spin_unlock_irqrestore(&fctx->lock, flags);
}

static int
nouveau_fence_wait_uevent_handler(struct nvif_event *event, void *repv, u32 repc)
{
	struct nouveau_fence_chan *fctx = container_of(event, typeof(*fctx), event);
	schedule_work(&fctx->uevent_work);
	return NVIF_EVENT_KEEP;
}

void
nouveau_fence_context_new(struct nouveau_channel *chan, struct nouveau_fence_chan *fctx)
{
	struct nouveau_cli *cli = chan->cli;
	struct nouveau_drm *drm = cli->drm;
	struct nouveau_fence_priv *priv = (void*)drm->fence;
	DEFINE_RAW_FLEX(struct nvif_event_v0, args, data,
			sizeof(struct nvif_chan_event_v0));
	struct nvif_chan_event_v0 *host =
				(struct nvif_chan_event_v0 *)args->data;
	int ret;

	INIT_WORK(&fctx->uevent_work, nouveau_fence_uevent_work);
	INIT_LIST_HEAD(&fctx->flip);
	INIT_LIST_HEAD(&fctx->pending);
	spin_lock_init(&fctx->lock);
	fctx->context = drm->runl[chan->runlist].context_base + chan->chid;

	if (chan == drm->cechan)
		strcpy(fctx->name, "copy engine channel");
	else if (chan == drm->channel)
		strcpy(fctx->name, "generic kernel channel");
	else
		strcpy(fctx->name, cli->name);

	kref_init(&fctx->fence_ref);
	if (!priv->uevent)
		return;

	host->version = 0;
	host->type = NVIF_CHAN_EVENT_V0_NON_STALL_INTR;

	ret = nvif_event_ctor(&chan->user, "fenceNonStallIntr", (chan->runlist << 16) | chan->chid,
			      nouveau_fence_wait_uevent_handler, false,
			      args, __struct_size(args), &fctx->event);

	WARN_ON(ret);
}

int
nouveau_fence_emit(struct nouveau_fence *fence)
{
	struct nouveau_channel *chan = unrcu_pointer(fence->channel);
	struct nouveau_fence_chan *fctx = chan->fence;
	struct nouveau_fence_priv *priv = (void*)chan->cli->drm->fence;
	int ret;

	fence->timeout  = jiffies + (15 * HZ);

	if (priv->uevent)
		dma_fence_init(&fence->base, &nouveau_fence_ops_uevent,
			       &fctx->lock, fctx->context, ++fctx->sequence);
	else
		dma_fence_init(&fence->base, &nouveau_fence_ops_legacy,
			       &fctx->lock, fctx->context, ++fctx->sequence);
	kref_get(&fctx->fence_ref);

	ret = fctx->emit(fence);
	if (!ret) {
		dma_fence_get(&fence->base);
		spin_lock_irq(&fctx->lock);

		if (unlikely(fctx->killed)) {
			spin_unlock_irq(&fctx->lock);
			dma_fence_put(&fence->base);
			return -ENODEV;
		}

		nouveau_fence_update(chan, fctx);
		list_add_tail(&fence->head, &fctx->pending);
		spin_unlock_irq(&fctx->lock);
	}

	return ret;
}

bool
nouveau_fence_done(struct nouveau_fence *fence)
{
	struct nouveau_fence_chan *fctx = nouveau_fctx(fence);
	struct nouveau_channel *chan;
	unsigned long flags;

	if (dma_fence_is_signaled(&fence->base))
		return true;

	spin_lock_irqsave(&fctx->lock, flags);
	chan = rcu_dereference_protected(fence->channel, lockdep_is_held(&fctx->lock));
	if (chan)
		nouveau_fence_update(chan, fctx);
	spin_unlock_irqrestore(&fctx->lock, flags);

	return dma_fence_is_signaled(&fence->base);
}

static long
nouveau_fence_wait_legacy(struct dma_fence *f, bool intr, long wait)
{
	struct nouveau_fence *fence = to_nouveau_fence(f);
	unsigned long sleep_time = NSEC_PER_MSEC / 1000;
	unsigned long t = jiffies, timeout = t + wait;

	while (!nouveau_fence_done(fence)) {
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
nouveau_fence_wait_busy(struct nouveau_fence *fence, bool intr)
{
	int ret = 0;

	while (!nouveau_fence_done(fence)) {
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
nouveau_fence_wait(struct nouveau_fence *fence, bool lazy, bool intr)
{
	long ret;

	if (!lazy)
		return nouveau_fence_wait_busy(fence, intr);

	ret = dma_fence_wait_timeout(&fence->base, intr, 15 * HZ);
	if (ret < 0)
		return ret;
	else if (!ret)
		return -EBUSY;
	else
		return 0;
}

int
nouveau_fence_sync(struct nouveau_bo *nvbo, struct nouveau_channel *chan,
		   bool exclusive, bool intr)
{
	struct nouveau_fence_chan *fctx = chan->fence;
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
			struct nouveau_fence *f;

			usage = dma_resv_iter_usage(&cursor);
			if (i == 0 && usage == DMA_RESV_USAGE_WRITE)
				continue;

			f = nouveau_local_fence(fence, chan->cli->drm);
			if (f) {
				struct nouveau_channel *prev;
				bool must_wait = true;
				bool local;

				rcu_read_lock();
				prev = rcu_dereference(f->channel);
				local = prev && prev->cli->drm == chan->cli->drm;
				if (local && (prev == chan ||
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
nouveau_fence_unref(struct nouveau_fence **pfence)
{
	if (*pfence)
		dma_fence_put(&(*pfence)->base);
	*pfence = NULL;
}

int
nouveau_fence_create(struct nouveau_fence **pfence,
		     struct nouveau_channel *chan)
{
	struct nouveau_fence *fence;

	if (unlikely(!chan->fence))
		return -ENODEV;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -ENOMEM;

	fence->channel = chan;

	*pfence = fence;
	return 0;
}

int
nouveau_fence_new(struct nouveau_fence **pfence,
		  struct nouveau_channel *chan)
{
	int ret = 0;

	ret = nouveau_fence_create(pfence, chan);
	if (ret)
		return ret;

	ret = nouveau_fence_emit(*pfence);
	if (ret)
		nouveau_fence_unref(pfence);

	return ret;
}

static const char *nouveau_fence_get_get_driver_name(struct dma_fence *fence)
{
	return "nouveau";
}

static const char *nouveau_fence_get_timeline_name(struct dma_fence *f)
{
	struct nouveau_fence *fence = to_nouveau_fence(f);
	struct nouveau_fence_chan *fctx = nouveau_fctx(fence);

	return !fctx->dead ? fctx->name : "dead channel";
}

/*
 * In an ideal world, read would not assume the channel context is still alive.
 * This function may be called from another device, running into free memory as a
 * result. The drm node should still be there, so we can derive the index from
 * the fence context.
 */
static bool nouveau_fence_is_signaled(struct dma_fence *f)
{
	struct nouveau_fence *fence = to_nouveau_fence(f);
	struct nouveau_fence_chan *fctx = nouveau_fctx(fence);
	struct nouveau_channel *chan;
	bool ret = false;

	rcu_read_lock();
	chan = rcu_dereference(fence->channel);
	if (chan)
		ret = (int)(fctx->read(chan) - fence->base.seqno) >= 0;
	rcu_read_unlock();

	return ret;
}

static bool nouveau_fence_no_signaling(struct dma_fence *f)
{
	struct nouveau_fence *fence = to_nouveau_fence(f);

	/*
	 * caller should have a reference on the fence,
	 * else fence could get freed here
	 */
	WARN_ON(kref_read(&fence->base.refcount) <= 1);

	/*
	 * This needs uevents to work correctly, but dma_fence_add_callback relies on
	 * being able to enable signaling. It will still get signaled eventually,
	 * just not right away.
	 */
	if (nouveau_fence_is_signaled(f)) {
		list_del(&fence->head);

		dma_fence_put(&fence->base);
		return false;
	}

	return true;
}

static void nouveau_fence_release(struct dma_fence *f)
{
	struct nouveau_fence *fence = to_nouveau_fence(f);
	struct nouveau_fence_chan *fctx = nouveau_fctx(fence);

	kref_put(&fctx->fence_ref, nouveau_fence_context_put);
	dma_fence_free(&fence->base);
}

static const struct dma_fence_ops nouveau_fence_ops_legacy = {
	.get_driver_name = nouveau_fence_get_get_driver_name,
	.get_timeline_name = nouveau_fence_get_timeline_name,
	.enable_signaling = nouveau_fence_no_signaling,
	.signaled = nouveau_fence_is_signaled,
	.wait = nouveau_fence_wait_legacy,
	.release = nouveau_fence_release
};

static bool nouveau_fence_enable_signaling(struct dma_fence *f)
{
	struct nouveau_fence *fence = to_nouveau_fence(f);
	struct nouveau_fence_chan *fctx = nouveau_fctx(fence);
	bool ret;

	if (!fctx->notify_ref++)
		nvif_event_allow(&fctx->event);

	ret = nouveau_fence_no_signaling(f);
	if (ret)
		set_bit(DMA_FENCE_FLAG_USER_BITS, &fence->base.flags);
	else if (!--fctx->notify_ref)
		nvif_event_block(&fctx->event);

	return ret;
}

static const struct dma_fence_ops nouveau_fence_ops_uevent = {
	.get_driver_name = nouveau_fence_get_get_driver_name,
	.get_timeline_name = nouveau_fence_get_timeline_name,
	.enable_signaling = nouveau_fence_enable_signaling,
	.signaled = nouveau_fence_is_signaled,
	.release = nouveau_fence_release
};
