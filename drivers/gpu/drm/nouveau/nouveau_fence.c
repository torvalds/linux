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

#include "nouveau_drm.h"
#include "nouveau_dma.h"
#include "nouveau_fence.h"

#include <engine/fifo.h>

struct fence_work {
	struct work_struct base;
	struct list_head head;
	void (*func)(void *);
	void *data;
};

static void
nouveau_fence_signal(struct nouveau_fence *fence)
{
	struct fence_work *work, *temp;

	list_for_each_entry_safe(work, temp, &fence->work, head) {
		schedule_work(&work->base);
		list_del(&work->head);
	}

	fence->channel = NULL;
	list_del(&fence->head);
}

void
nouveau_fence_context_del(struct nouveau_fence_chan *fctx)
{
	struct nouveau_fence *fence, *fnext;
	spin_lock(&fctx->lock);
	list_for_each_entry_safe(fence, fnext, &fctx->pending, head) {
		nouveau_fence_signal(fence);
	}
	spin_unlock(&fctx->lock);
}

void
nouveau_fence_context_new(struct nouveau_fence_chan *fctx)
{
	INIT_LIST_HEAD(&fctx->flip);
	INIT_LIST_HEAD(&fctx->pending);
	spin_lock_init(&fctx->lock);
}

static void
nouveau_fence_work_handler(struct work_struct *kwork)
{
	struct fence_work *work = container_of(kwork, typeof(*work), base);
	work->func(work->data);
	kfree(work);
}

void
nouveau_fence_work(struct nouveau_fence *fence,
		   void (*func)(void *), void *data)
{
	struct nouveau_channel *chan = fence->channel;
	struct nouveau_fence_chan *fctx;
	struct fence_work *work = NULL;

	if (nouveau_fence_done(fence)) {
		func(data);
		return;
	}

	fctx = chan->fence;
	work = kmalloc(sizeof(*work), GFP_KERNEL);
	if (!work) {
		WARN_ON(nouveau_fence_wait(fence, false, false));
		func(data);
		return;
	}

	spin_lock(&fctx->lock);
	if (!fence->channel) {
		spin_unlock(&fctx->lock);
		kfree(work);
		func(data);
		return;
	}

	INIT_WORK(&work->base, nouveau_fence_work_handler);
	work->func = func;
	work->data = data;
	list_add(&work->head, &fence->work);
	spin_unlock(&fctx->lock);
}

static void
nouveau_fence_update(struct nouveau_channel *chan)
{
	struct nouveau_fence_chan *fctx = chan->fence;
	struct nouveau_fence *fence, *fnext;

	spin_lock(&fctx->lock);
	list_for_each_entry_safe(fence, fnext, &fctx->pending, head) {
		if (fctx->read(chan) < fence->sequence)
			break;

		nouveau_fence_signal(fence);
		nouveau_fence_unref(&fence);
	}
	spin_unlock(&fctx->lock);
}

int
nouveau_fence_emit(struct nouveau_fence *fence, struct nouveau_channel *chan)
{
	struct nouveau_fence_chan *fctx = chan->fence;
	int ret;

	fence->channel  = chan;
	fence->timeout  = jiffies + (15 * HZ);
	fence->sequence = ++fctx->sequence;

	ret = fctx->emit(fence);
	if (!ret) {
		kref_get(&fence->kref);
		spin_lock(&fctx->lock);
		list_add_tail(&fence->head, &fctx->pending);
		spin_unlock(&fctx->lock);
	}

	return ret;
}

bool
nouveau_fence_done(struct nouveau_fence *fence)
{
	if (fence->channel)
		nouveau_fence_update(fence->channel);
	return !fence->channel;
}

static int
nouveau_fence_wait_uevent_handler(void *data, u32 type, int index)
{
	struct nouveau_fence_priv *priv = data;
	wake_up_all(&priv->waiting);
	return NVKM_EVENT_KEEP;
}

static int
nouveau_fence_wait_uevent(struct nouveau_fence *fence, bool intr)

{
	struct nouveau_channel *chan = fence->channel;
	struct nouveau_fifo *pfifo = nouveau_fifo(chan->drm->device);
	struct nouveau_fence_priv *priv = chan->drm->fence;
	struct nouveau_eventh *handler;
	int ret = 0;

	ret = nouveau_event_new(pfifo->uevent, 1, 0,
				nouveau_fence_wait_uevent_handler,
				priv, &handler);
	if (ret)
		return ret;

	nouveau_event_get(handler);

	if (fence->timeout) {
		unsigned long timeout = fence->timeout - jiffies;

		if (time_before(jiffies, fence->timeout)) {
			if (intr) {
				ret = wait_event_interruptible_timeout(
						priv->waiting,
						nouveau_fence_done(fence),
						timeout);
			} else {
				ret = wait_event_timeout(priv->waiting,
						nouveau_fence_done(fence),
						timeout);
			}
		}

		if (ret >= 0) {
			fence->timeout = jiffies + ret;
			if (time_after_eq(jiffies, fence->timeout))
				ret = -EBUSY;
		}
	} else {
		if (intr) {
			ret = wait_event_interruptible(priv->waiting,
					nouveau_fence_done(fence));
		} else {
			wait_event(priv->waiting, nouveau_fence_done(fence));
		}
	}

	nouveau_event_ref(NULL, &handler);
	if (unlikely(ret < 0))
		return ret;

	return 0;
}

int
nouveau_fence_wait(struct nouveau_fence *fence, bool lazy, bool intr)
{
	struct nouveau_channel *chan = fence->channel;
	struct nouveau_fence_priv *priv = chan ? chan->drm->fence : NULL;
	unsigned long sleep_time = NSEC_PER_MSEC / 1000;
	ktime_t t;
	int ret = 0;

	while (priv && priv->uevent && lazy && !nouveau_fence_done(fence)) {
		ret = nouveau_fence_wait_uevent(fence, intr);
		if (ret < 0)
			return ret;
	}

	while (!nouveau_fence_done(fence)) {
		if (fence->timeout && time_after_eq(jiffies, fence->timeout)) {
			ret = -EBUSY;
			break;
		}

		__set_current_state(intr ? TASK_INTERRUPTIBLE :
					   TASK_UNINTERRUPTIBLE);
		if (lazy) {
			t = ktime_set(0, sleep_time);
			schedule_hrtimeout(&t, HRTIMER_MODE_REL);
			sleep_time *= 2;
			if (sleep_time > NSEC_PER_MSEC)
				sleep_time = NSEC_PER_MSEC;
		}

		if (intr && signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}

	__set_current_state(TASK_RUNNING);
	return ret;
}

int
nouveau_fence_sync(struct nouveau_fence *fence, struct nouveau_channel *chan)
{
	struct nouveau_fence_chan *fctx = chan->fence;
	struct nouveau_channel *prev;
	int ret = 0;

	prev = fence ? fence->channel : NULL;
	if (prev) {
		if (unlikely(prev != chan && !nouveau_fence_done(fence))) {
			ret = fctx->sync(fence, prev, chan);
			if (unlikely(ret))
				ret = nouveau_fence_wait(fence, true, false);
		}
	}

	return ret;
}

static void
nouveau_fence_del(struct kref *kref)
{
	struct nouveau_fence *fence = container_of(kref, typeof(*fence), kref);
	kfree(fence);
}

void
nouveau_fence_unref(struct nouveau_fence **pfence)
{
	if (*pfence)
		kref_put(&(*pfence)->kref, nouveau_fence_del);
	*pfence = NULL;
}

struct nouveau_fence *
nouveau_fence_ref(struct nouveau_fence *fence)
{
	if (fence)
		kref_get(&fence->kref);
	return fence;
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

	INIT_LIST_HEAD(&fence->work);
	fence->sysmem = sysmem;
	kref_init(&fence->kref);

	ret = nouveau_fence_emit(fence, chan);
	if (ret)
		nouveau_fence_unref(&fence);

	*pfence = fence;
	return ret;
}
