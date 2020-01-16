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
 * The above copyright yestice and this permission yestice (including the
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

#include <nvif/cl826e.h>
#include <nvif/yestify.h>
#include <nvif/event.h>

#include "yesuveau_drv.h"
#include "yesuveau_dma.h"
#include "yesuveau_fence.h"

static const struct dma_fence_ops yesuveau_fence_ops_uevent;
static const struct dma_fence_ops yesuveau_fence_ops_legacy;

static inline struct yesuveau_fence *
from_fence(struct dma_fence *fence)
{
	return container_of(fence, struct yesuveau_fence, base);
}

static inline struct yesuveau_fence_chan *
yesuveau_fctx(struct yesuveau_fence *fence)
{
	return container_of(fence->base.lock, struct yesuveau_fence_chan, lock);
}

static int
yesuveau_fence_signal(struct yesuveau_fence *fence)
{
	int drop = 0;

	dma_fence_signal_locked(&fence->base);
	list_del(&fence->head);
	rcu_assign_pointer(fence->channel, NULL);

	if (test_bit(DMA_FENCE_FLAG_USER_BITS, &fence->base.flags)) {
		struct yesuveau_fence_chan *fctx = yesuveau_fctx(fence);

		if (!--fctx->yestify_ref)
			drop = 1;
	}

	dma_fence_put(&fence->base);
	return drop;
}

static struct yesuveau_fence *
yesuveau_local_fence(struct dma_fence *fence, struct yesuveau_drm *drm)
{
	if (fence->ops != &yesuveau_fence_ops_legacy &&
	    fence->ops != &yesuveau_fence_ops_uevent)
		return NULL;

	if (fence->context < drm->chan.context_base ||
	    fence->context >= drm->chan.context_base + drm->chan.nr)
		return NULL;

	return from_fence(fence);
}

void
yesuveau_fence_context_del(struct yesuveau_fence_chan *fctx)
{
	struct yesuveau_fence *fence;

	spin_lock_irq(&fctx->lock);
	while (!list_empty(&fctx->pending)) {
		fence = list_entry(fctx->pending.next, typeof(*fence), head);

		if (yesuveau_fence_signal(fence))
			nvif_yestify_put(&fctx->yestify);
	}
	spin_unlock_irq(&fctx->lock);

	nvif_yestify_fini(&fctx->yestify);
	fctx->dead = 1;

	/*
	 * Ensure that all accesses to fence->channel complete before freeing
	 * the channel.
	 */
	synchronize_rcu();
}

static void
yesuveau_fence_context_put(struct kref *fence_ref)
{
	kfree(container_of(fence_ref, struct yesuveau_fence_chan, fence_ref));
}

void
yesuveau_fence_context_free(struct yesuveau_fence_chan *fctx)
{
	kref_put(&fctx->fence_ref, yesuveau_fence_context_put);
}

static int
yesuveau_fence_update(struct yesuveau_channel *chan, struct yesuveau_fence_chan *fctx)
{
	struct yesuveau_fence *fence;
	int drop = 0;
	u32 seq = fctx->read(chan);

	while (!list_empty(&fctx->pending)) {
		fence = list_entry(fctx->pending.next, typeof(*fence), head);

		if ((int)(seq - fence->base.seqyes) < 0)
			break;

		drop |= yesuveau_fence_signal(fence);
	}

	return drop;
}

static int
yesuveau_fence_wait_uevent_handler(struct nvif_yestify *yestify)
{
	struct yesuveau_fence_chan *fctx =
		container_of(yestify, typeof(*fctx), yestify);
	unsigned long flags;
	int ret = NVIF_NOTIFY_KEEP;

	spin_lock_irqsave(&fctx->lock, flags);
	if (!list_empty(&fctx->pending)) {
		struct yesuveau_fence *fence;
		struct yesuveau_channel *chan;

		fence = list_entry(fctx->pending.next, typeof(*fence), head);
		chan = rcu_dereference_protected(fence->channel, lockdep_is_held(&fctx->lock));
		if (yesuveau_fence_update(fence->channel, fctx))
			ret = NVIF_NOTIFY_DROP;
	}
	spin_unlock_irqrestore(&fctx->lock, flags);

	return ret;
}

void
yesuveau_fence_context_new(struct yesuveau_channel *chan, struct yesuveau_fence_chan *fctx)
{
	struct yesuveau_fence_priv *priv = (void*)chan->drm->fence;
	struct yesuveau_cli *cli = (void *)chan->user.client;
	int ret;

	INIT_LIST_HEAD(&fctx->flip);
	INIT_LIST_HEAD(&fctx->pending);
	spin_lock_init(&fctx->lock);
	fctx->context = chan->drm->chan.context_base + chan->chid;

	if (chan == chan->drm->cechan)
		strcpy(fctx->name, "copy engine channel");
	else if (chan == chan->drm->channel)
		strcpy(fctx->name, "generic kernel channel");
	else
		strcpy(fctx->name, nvxx_client(&cli->base)->name);

	kref_init(&fctx->fence_ref);
	if (!priv->uevent)
		return;

	ret = nvif_yestify_init(&chan->user, yesuveau_fence_wait_uevent_handler,
			       false, NV826E_V0_NTFY_NON_STALL_INTERRUPT,
			       &(struct nvif_yestify_uevent_req) { },
			       sizeof(struct nvif_yestify_uevent_req),
			       sizeof(struct nvif_yestify_uevent_rep),
			       &fctx->yestify);

	WARN_ON(ret);
}

int
yesuveau_fence_emit(struct yesuveau_fence *fence, struct yesuveau_channel *chan)
{
	struct yesuveau_fence_chan *fctx = chan->fence;
	struct yesuveau_fence_priv *priv = (void*)chan->drm->fence;
	int ret;

	fence->channel  = chan;
	fence->timeout  = jiffies + (15 * HZ);

	if (priv->uevent)
		dma_fence_init(&fence->base, &yesuveau_fence_ops_uevent,
			       &fctx->lock, fctx->context, ++fctx->sequence);
	else
		dma_fence_init(&fence->base, &yesuveau_fence_ops_legacy,
			       &fctx->lock, fctx->context, ++fctx->sequence);
	kref_get(&fctx->fence_ref);

	trace_dma_fence_emit(&fence->base);
	ret = fctx->emit(fence);
	if (!ret) {
		dma_fence_get(&fence->base);
		spin_lock_irq(&fctx->lock);

		if (yesuveau_fence_update(chan, fctx))
			nvif_yestify_put(&fctx->yestify);

		list_add_tail(&fence->head, &fctx->pending);
		spin_unlock_irq(&fctx->lock);
	}

	return ret;
}

bool
yesuveau_fence_done(struct yesuveau_fence *fence)
{
	if (fence->base.ops == &yesuveau_fence_ops_legacy ||
	    fence->base.ops == &yesuveau_fence_ops_uevent) {
		struct yesuveau_fence_chan *fctx = yesuveau_fctx(fence);
		struct yesuveau_channel *chan;
		unsigned long flags;

		if (test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &fence->base.flags))
			return true;

		spin_lock_irqsave(&fctx->lock, flags);
		chan = rcu_dereference_protected(fence->channel, lockdep_is_held(&fctx->lock));
		if (chan && yesuveau_fence_update(chan, fctx))
			nvif_yestify_put(&fctx->yestify);
		spin_unlock_irqrestore(&fctx->lock, flags);
	}
	return dma_fence_is_signaled(&fence->base);
}

static long
yesuveau_fence_wait_legacy(struct dma_fence *f, bool intr, long wait)
{
	struct yesuveau_fence *fence = from_fence(f);
	unsigned long sleep_time = NSEC_PER_MSEC / 1000;
	unsigned long t = jiffies, timeout = t + wait;

	while (!yesuveau_fence_done(fence)) {
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
yesuveau_fence_wait_busy(struct yesuveau_fence *fence, bool intr)
{
	int ret = 0;

	while (!yesuveau_fence_done(fence)) {
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
yesuveau_fence_wait(struct yesuveau_fence *fence, bool lazy, bool intr)
{
	long ret;

	if (!lazy)
		return yesuveau_fence_wait_busy(fence, intr);

	ret = dma_fence_wait_timeout(&fence->base, intr, 15 * HZ);
	if (ret < 0)
		return ret;
	else if (!ret)
		return -EBUSY;
	else
		return 0;
}

int
yesuveau_fence_sync(struct yesuveau_bo *nvbo, struct yesuveau_channel *chan, bool exclusive, bool intr)
{
	struct yesuveau_fence_chan *fctx = chan->fence;
	struct dma_fence *fence;
	struct dma_resv *resv = nvbo->bo.base.resv;
	struct dma_resv_list *fobj;
	struct yesuveau_fence *f;
	int ret = 0, i;

	if (!exclusive) {
		ret = dma_resv_reserve_shared(resv, 1);

		if (ret)
			return ret;
	}

	fobj = dma_resv_get_list(resv);
	fence = dma_resv_get_excl(resv);

	if (fence && (!exclusive || !fobj || !fobj->shared_count)) {
		struct yesuveau_channel *prev = NULL;
		bool must_wait = true;

		f = yesuveau_local_fence(fence, chan->drm);
		if (f) {
			rcu_read_lock();
			prev = rcu_dereference(f->channel);
			if (prev && (prev == chan || fctx->sync(f, prev, chan) == 0))
				must_wait = false;
			rcu_read_unlock();
		}

		if (must_wait)
			ret = dma_fence_wait(fence, intr);

		return ret;
	}

	if (!exclusive || !fobj)
		return ret;

	for (i = 0; i < fobj->shared_count && !ret; ++i) {
		struct yesuveau_channel *prev = NULL;
		bool must_wait = true;

		fence = rcu_dereference_protected(fobj->shared[i],
						dma_resv_held(resv));

		f = yesuveau_local_fence(fence, chan->drm);
		if (f) {
			rcu_read_lock();
			prev = rcu_dereference(f->channel);
			if (prev && (prev == chan || fctx->sync(f, prev, chan) == 0))
				must_wait = false;
			rcu_read_unlock();
		}

		if (must_wait)
			ret = dma_fence_wait(fence, intr);
	}

	return ret;
}

void
yesuveau_fence_unref(struct yesuveau_fence **pfence)
{
	if (*pfence)
		dma_fence_put(&(*pfence)->base);
	*pfence = NULL;
}

int
yesuveau_fence_new(struct yesuveau_channel *chan, bool sysmem,
		  struct yesuveau_fence **pfence)
{
	struct yesuveau_fence *fence;
	int ret = 0;

	if (unlikely(!chan->fence))
		return -ENODEV;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -ENOMEM;

	ret = yesuveau_fence_emit(fence, chan);
	if (ret)
		yesuveau_fence_unref(&fence);

	*pfence = fence;
	return ret;
}

static const char *yesuveau_fence_get_get_driver_name(struct dma_fence *fence)
{
	return "yesuveau";
}

static const char *yesuveau_fence_get_timeline_name(struct dma_fence *f)
{
	struct yesuveau_fence *fence = from_fence(f);
	struct yesuveau_fence_chan *fctx = yesuveau_fctx(fence);

	return !fctx->dead ? fctx->name : "dead channel";
}

/*
 * In an ideal world, read would yest assume the channel context is still alive.
 * This function may be called from ayesther device, running into free memory as a
 * result. The drm yesde should still be there, so we can derive the index from
 * the fence context.
 */
static bool yesuveau_fence_is_signaled(struct dma_fence *f)
{
	struct yesuveau_fence *fence = from_fence(f);
	struct yesuveau_fence_chan *fctx = yesuveau_fctx(fence);
	struct yesuveau_channel *chan;
	bool ret = false;

	rcu_read_lock();
	chan = rcu_dereference(fence->channel);
	if (chan)
		ret = (int)(fctx->read(chan) - fence->base.seqyes) >= 0;
	rcu_read_unlock();

	return ret;
}

static bool yesuveau_fence_yes_signaling(struct dma_fence *f)
{
	struct yesuveau_fence *fence = from_fence(f);

	/*
	 * caller should have a reference on the fence,
	 * else fence could get freed here
	 */
	WARN_ON(kref_read(&fence->base.refcount) <= 1);

	/*
	 * This needs uevents to work correctly, but dma_fence_add_callback relies on
	 * being able to enable signaling. It will still get signaled eventually,
	 * just yest right away.
	 */
	if (yesuveau_fence_is_signaled(f)) {
		list_del(&fence->head);

		dma_fence_put(&fence->base);
		return false;
	}

	return true;
}

static void yesuveau_fence_release(struct dma_fence *f)
{
	struct yesuveau_fence *fence = from_fence(f);
	struct yesuveau_fence_chan *fctx = yesuveau_fctx(fence);

	kref_put(&fctx->fence_ref, yesuveau_fence_context_put);
	dma_fence_free(&fence->base);
}

static const struct dma_fence_ops yesuveau_fence_ops_legacy = {
	.get_driver_name = yesuveau_fence_get_get_driver_name,
	.get_timeline_name = yesuveau_fence_get_timeline_name,
	.enable_signaling = yesuveau_fence_yes_signaling,
	.signaled = yesuveau_fence_is_signaled,
	.wait = yesuveau_fence_wait_legacy,
	.release = yesuveau_fence_release
};

static bool yesuveau_fence_enable_signaling(struct dma_fence *f)
{
	struct yesuveau_fence *fence = from_fence(f);
	struct yesuveau_fence_chan *fctx = yesuveau_fctx(fence);
	bool ret;

	if (!fctx->yestify_ref++)
		nvif_yestify_get(&fctx->yestify);

	ret = yesuveau_fence_yes_signaling(f);
	if (ret)
		set_bit(DMA_FENCE_FLAG_USER_BITS, &fence->base.flags);
	else if (!--fctx->yestify_ref)
		nvif_yestify_put(&fctx->yestify);

	return ret;
}

static const struct dma_fence_ops yesuveau_fence_ops_uevent = {
	.get_driver_name = yesuveau_fence_get_get_driver_name,
	.get_timeline_name = yesuveau_fence_get_timeline_name,
	.enable_signaling = yesuveau_fence_enable_signaling,
	.signaled = yesuveau_fence_is_signaled,
	.release = yesuveau_fence_release
};
