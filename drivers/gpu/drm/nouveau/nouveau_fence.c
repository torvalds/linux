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

#include <drm/drmP.h>

#include <linux/ktime.h>
#include <linux/hrtimer.h>
#include <trace/events/fence.h>

#include <nvif/notify.h>
#include <nvif/event.h>

#include "nouveau_drm.h"
#include "nouveau_dma.h"
#include "nouveau_fence.h"

static const struct fence_ops nouveau_fence_ops_uevent;
static const struct fence_ops nouveau_fence_ops_legacy;

static inline struct nouveau_fence *
from_fence(struct fence *fence)
{
	return container_of(fence, struct nouveau_fence, base);
}

static inline struct nouveau_fence_chan *
nouveau_fctx(struct nouveau_fence *fence)
{
	return container_of(fence->base.lock, struct nouveau_fence_chan, lock);
}

static int
nouveau_fence_signal(struct nouveau_fence *fence)
{
	int drop = 0;

	fence_signal_locked(&fence->base);
	list_del(&fence->head);
	rcu_assign_pointer(fence->channel, NULL);

	if (test_bit(FENCE_FLAG_USER_BITS, &fence->base.flags)) {
		struct nouveau_fence_chan *fctx = nouveau_fctx(fence);

		if (!--fctx->notify_ref)
			drop = 1;
	}

	fence_put(&fence->base);
	return drop;
}

static struct nouveau_fence *
nouveau_local_fence(struct fence *fence, struct nouveau_drm *drm) {
	struct nouveau_fence_priv *priv = (void*)drm->fence;

	if (fence->ops != &nouveau_fence_ops_legacy &&
	    fence->ops != &nouveau_fence_ops_uevent)
		return NULL;

	if (fence->context < priv->context_base ||
	    fence->context >= priv->context_base + priv->contexts)
		return NULL;

	return from_fence(fence);
}

void
nouveau_fence_context_del(struct nouveau_fence_chan *fctx)
{
	struct nouveau_fence *fence;

	spin_lock_irq(&fctx->lock);
	while (!list_empty(&fctx->pending)) {
		fence = list_entry(fctx->pending.next, typeof(*fence), head);

		if (nouveau_fence_signal(fence))
			nvif_notify_put(&fctx->notify);
	}
	spin_unlock_irq(&fctx->lock);

	nvif_notify_fini(&fctx->notify);
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

static int
nouveau_fence_update(struct nouveau_channel *chan, struct nouveau_fence_chan *fctx)
{
	struct nouveau_fence *fence;
	int drop = 0;
	u32 seq = fctx->read(chan);

	while (!list_empty(&fctx->pending)) {
		fence = list_entry(fctx->pending.next, typeof(*fence), head);

		if ((int)(seq - fence->base.seqno) < 0)
			break;

		drop |= nouveau_fence_signal(fence);
	}

	return drop;
}

static int
nouveau_fence_wait_uevent_handler(struct nvif_notify *notify)
{
	struct nouveau_fence_chan *fctx =
		container_of(notify, typeof(*fctx), notify);
	unsigned long flags;
	int ret = NVIF_NOTIFY_KEEP;

	spin_lock_irqsave(&fctx->lock, flags);
	if (!list_empty(&fctx->pending)) {
		struct nouveau_fence *fence;
		struct nouveau_channel *chan;

		fence = list_entry(fctx->pending.next, typeof(*fence), head);
		chan = rcu_dereference_protected(fence->channel, lockdep_is_held(&fctx->lock));
		if (nouveau_fence_update(fence->channel, fctx))
			ret = NVIF_NOTIFY_DROP;
	}
	spin_unlock_irqrestore(&fctx->lock, flags);

	return ret;
}

void
nouveau_fence_context_new(struct nouveau_channel *chan, struct nouveau_fence_chan *fctx)
{
	struct nouveau_fence_priv *priv = (void*)chan->drm->fence;
	struct nouveau_cli *cli = (void *)nvif_client(chan->object);
	int ret;

	INIT_LIST_HEAD(&fctx->flip);
	INIT_LIST_HEAD(&fctx->pending);
	spin_lock_init(&fctx->lock);
	fctx->context = priv->context_base + chan->chid;

	if (chan == chan->drm->cechan)
		strcpy(fctx->name, "copy engine channel");
	else if (chan == chan->drm->channel)
		strcpy(fctx->name, "generic kernel channel");
	else
		strcpy(fctx->name, nvxx_client(&cli->base)->name);

	kref_init(&fctx->fence_ref);
	if (!priv->uevent)
		return;

	ret = nvif_notify_init(chan->object, NULL,
			 nouveau_fence_wait_uevent_handler, false,
			 G82_CHANNEL_DMA_V0_NTFY_UEVENT,
			 &(struct nvif_notify_uevent_req) { },
			 sizeof(struct nvif_notify_uevent_req),
			 sizeof(struct nvif_notify_uevent_rep),
			 &fctx->notify);

	WARN_ON(ret);
}

struct nouveau_fence_work {
	struct work_struct work;
	struct fence_cb cb;
	void (*func)(void *);
	void *data;
};

static void
nouveau_fence_work_handler(struct work_struct *kwork)
{
	struct nouveau_fence_work *work = container_of(kwork, typeof(*work), work);
	work->func(work->data);
	kfree(work);
}

static void nouveau_fence_work_cb(struct fence *fence, struct fence_cb *cb)
{
	struct nouveau_fence_work *work = container_of(cb, typeof(*work), cb);

	schedule_work(&work->work);
}

void
nouveau_fence_work(struct fence *fence,
		   void (*func)(void *), void *data)
{
	struct nouveau_fence_work *work;

	if (fence_is_signaled(fence))
		goto err;

	work = kmalloc(sizeof(*work), GFP_KERNEL);
	if (!work) {
		/*
		 * this might not be a nouveau fence any more,
		 * so force a lazy wait here
		 */
		WARN_ON(nouveau_fence_wait((struct nouveau_fence *)fence,
					   true, false));
		goto err;
	}

	INIT_WORK(&work->work, nouveau_fence_work_handler);
	work->func = func;
	work->data = data;

	if (fence_add_callback(fence, &work->cb, nouveau_fence_work_cb) < 0)
		goto err_free;
	return;

err_free:
	kfree(work);
err:
	func(data);
}

int
nouveau_fence_emit(struct nouveau_fence *fence, struct nouveau_channel *chan)
{
	struct nouveau_fence_chan *fctx = chan->fence;
	struct nouveau_fence_priv *priv = (void*)chan->drm->fence;
	int ret;

	fence->channel  = chan;
	fence->timeout  = jiffies + (15 * HZ);

	if (priv->uevent)
		fence_init(&fence->base, &nouveau_fence_ops_uevent,
			   &fctx->lock, fctx->context, ++fctx->sequence);
	else
		fence_init(&fence->base, &nouveau_fence_ops_legacy,
			   &fctx->lock, fctx->context, ++fctx->sequence);
	kref_get(&fctx->fence_ref);

	trace_fence_emit(&fence->base);
	ret = fctx->emit(fence);
	if (!ret) {
		fence_get(&fence->base);
		spin_lock_irq(&fctx->lock);

		if (nouveau_fence_update(chan, fctx))
			nvif_notify_put(&fctx->notify);

		list_add_tail(&fence->head, &fctx->pending);
		spin_unlock_irq(&fctx->lock);
	}

	return ret;
}

bool
nouveau_fence_done(struct nouveau_fence *fence)
{
	if (fence->base.ops == &nouveau_fence_ops_legacy ||
	    fence->base.ops == &nouveau_fence_ops_uevent) {
		struct nouveau_fence_chan *fctx = nouveau_fctx(fence);
		struct nouveau_channel *chan;
		unsigned long flags;

		if (test_bit(FENCE_FLAG_SIGNALED_BIT, &fence->base.flags))
			return true;

		spin_lock_irqsave(&fctx->lock, flags);
		chan = rcu_dereference_protected(fence->channel, lockdep_is_held(&fctx->lock));
		if (chan && nouveau_fence_update(chan, fctx))
			nvif_notify_put(&fctx->notify);
		spin_unlock_irqrestore(&fctx->lock, flags);
	}
	return fence_is_signaled(&fence->base);
}

static long
nouveau_fence_wait_legacy(struct fence *f, bool intr, long wait)
{
	struct nouveau_fence *fence = from_fence(f);
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

		kt = ktime_set(0, sleep_time);
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

	ret = fence_wait_timeout(&fence->base, intr, 15 * HZ);
	if (ret < 0)
		return ret;
	else if (!ret)
		return -EBUSY;
	else
		return 0;
}

int
nouveau_fence_sync(struct nouveau_bo *nvbo, struct nouveau_channel *chan, bool exclusive, bool intr)
{
	struct nouveau_fence_chan *fctx = chan->fence;
	struct fence *fence;
	struct reservation_object *resv = nvbo->bo.resv;
	struct reservation_object_list *fobj;
	struct nouveau_fence *f;
	int ret = 0, i;

	if (!exclusive) {
		ret = reservation_object_reserve_shared(resv);

		if (ret)
			return ret;
	}

	fobj = reservation_object_get_list(resv);
	fence = reservation_object_get_excl(resv);

	if (fence && (!exclusive || !fobj || !fobj->shared_count)) {
		struct nouveau_channel *prev = NULL;
		bool must_wait = true;

		f = nouveau_local_fence(fence, chan->drm);
		if (f) {
			rcu_read_lock();
			prev = rcu_dereference(f->channel);
			if (prev && (prev == chan || fctx->sync(f, prev, chan) == 0))
				must_wait = false;
			rcu_read_unlock();
		}

		if (must_wait)
			ret = fence_wait(fence, intr);

		return ret;
	}

	if (!exclusive || !fobj)
		return ret;

	for (i = 0; i < fobj->shared_count && !ret; ++i) {
		struct nouveau_channel *prev = NULL;
		bool must_wait = true;

		fence = rcu_dereference_protected(fobj->shared[i],
						reservation_object_held(resv));

		f = nouveau_local_fence(fence, chan->drm);
		if (f) {
			rcu_read_lock();
			prev = rcu_dereference(f->channel);
			if (prev && (prev == chan || fctx->sync(f, prev, chan) == 0))
				must_wait = false;
			rcu_read_unlock();
		}

		if (must_wait)
			ret = fence_wait(fence, intr);
	}

	return ret;
}

void
nouveau_fence_unref(struct nouveau_fence **pfence)
{
	if (*pfence)
		fence_put(&(*pfence)->base);
	*pfence = NULL;
}

int
nouveau_fence_new(struct nouveau_channel *chan, bool sysmem,
		  struct nouveau_fence **pfence)
{
	struct nouveau_fence *fence;
	int ret = 0;

	if (unlikely(!chan->fence))
		return -ENODEV;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -ENOMEM;

	fence->sysmem = sysmem;

	ret = nouveau_fence_emit(fence, chan);
	if (ret)
		nouveau_fence_unref(&fence);

	*pfence = fence;
	return ret;
}

static const char *nouveau_fence_get_get_driver_name(struct fence *fence)
{
	return "nouveau";
}

static const char *nouveau_fence_get_timeline_name(struct fence *f)
{
	struct nouveau_fence *fence = from_fence(f);
	struct nouveau_fence_chan *fctx = nouveau_fctx(fence);

	return !fctx->dead ? fctx->name : "dead channel";
}

/*
 * In an ideal world, read would not assume the channel context is still alive.
 * This function may be called from another device, running into free memory as a
 * result. The drm node should still be there, so we can derive the index from
 * the fence context.
 */
static bool nouveau_fence_is_signaled(struct fence *f)
{
	struct nouveau_fence *fence = from_fence(f);
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

static bool nouveau_fence_no_signaling(struct fence *f)
{
	struct nouveau_fence *fence = from_fence(f);

	/*
	 * caller should have a reference on the fence,
	 * else fence could get freed here
	 */
	WARN_ON(atomic_read(&fence->base.refcount.refcount) <= 1);

	/*
	 * This needs uevents to work correctly, but fence_add_callback relies on
	 * being able to enable signaling. It will still get signaled eventually,
	 * just not right away.
	 */
	if (nouveau_fence_is_signaled(f)) {
		list_del(&fence->head);

		fence_put(&fence->base);
		return false;
	}

	return true;
}

static void nouveau_fence_release(struct fence *f)
{
	struct nouveau_fence *fence = from_fence(f);
	struct nouveau_fence_chan *fctx = nouveau_fctx(fence);

	kref_put(&fctx->fence_ref, nouveau_fence_context_put);
	fence_free(&fence->base);
}

static const struct fence_ops nouveau_fence_ops_legacy = {
	.get_driver_name = nouveau_fence_get_get_driver_name,
	.get_timeline_name = nouveau_fence_get_timeline_name,
	.enable_signaling = nouveau_fence_no_signaling,
	.signaled = nouveau_fence_is_signaled,
	.wait = nouveau_fence_wait_legacy,
	.release = nouveau_fence_release
};

static bool nouveau_fence_enable_signaling(struct fence *f)
{
	struct nouveau_fence *fence = from_fence(f);
	struct nouveau_fence_chan *fctx = nouveau_fctx(fence);
	bool ret;

	if (!fctx->notify_ref++)
		nvif_notify_get(&fctx->notify);

	ret = nouveau_fence_no_signaling(f);
	if (ret)
		set_bit(FENCE_FLAG_USER_BITS, &fence->base.flags);
	else if (!--fctx->notify_ref)
		nvif_notify_put(&fctx->notify);

	return ret;
}

static const struct fence_ops nouveau_fence_ops_uevent = {
	.get_driver_name = nouveau_fence_get_get_driver_name,
	.get_timeline_name = nouveau_fence_get_timeline_name,
	.enable_signaling = nouveau_fence_enable_signaling,
	.signaled = nouveau_fence_is_signaled,
	.wait = fence_default_wait,
	.release = NULL
};
