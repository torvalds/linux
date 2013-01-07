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

void
nouveau_fence_context_del(struct nouveau_fence_chan *fctx)
{
	struct nouveau_fence *fence, *fnext;
	spin_lock(&fctx->lock);
	list_for_each_entry_safe(fence, fnext, &fctx->pending, head) {
		if (fence->work)
			fence->work(fence->priv, false);
		fence->channel = NULL;
		list_del(&fence->head);
		nouveau_fence_unref(&fence);
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
nouveau_fence_update(struct nouveau_channel *chan)
{
	struct nouveau_fence_priv *priv = chan->drm->fence;
	struct nouveau_fence_chan *fctx = chan->fence;
	struct nouveau_fence *fence, *fnext;

	spin_lock(&fctx->lock);
	list_for_each_entry_safe(fence, fnext, &fctx->pending, head) {
		if (priv->read(chan) < fence->sequence)
			break;

		if (fence->work)
			fence->work(fence->priv, true);
		fence->channel = NULL;
		list_del(&fence->head);
		nouveau_fence_unref(&fence);
	}
	spin_unlock(&fctx->lock);
}

int
nouveau_fence_emit(struct nouveau_fence *fence, struct nouveau_channel *chan)
{
	struct nouveau_fence_priv *priv = chan->drm->fence;
	struct nouveau_fence_chan *fctx = chan->fence;
	int ret;

	fence->channel  = chan;
	fence->timeout  = jiffies + (3 * DRM_HZ);
	fence->sequence = ++fctx->sequence;

	ret = priv->emit(fence);
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

int
nouveau_fence_wait(struct nouveau_fence *fence, bool lazy, bool intr)
{
	unsigned long sleep_time = NSEC_PER_MSEC / 1000;
	ktime_t t;
	int ret = 0;

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
	struct nouveau_fence_priv *priv = chan->drm->fence;
	struct nouveau_channel *prev;
	int ret = 0;

	prev = fence ? fence->channel : NULL;
	if (prev) {
		if (unlikely(prev != chan && !nouveau_fence_done(fence))) {
			ret = priv->sync(fence, prev, chan);
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
	kref_get(&fence->kref);
	return fence;
}

int
nouveau_fence_new(struct nouveau_channel *chan, struct nouveau_fence **pfence)
{
	struct nouveau_fence *fence;
	int ret = 0;

	if (unlikely(!chan->fence))
		return -ENODEV;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -ENOMEM;
	kref_init(&fence->kref);

	if (chan) {
		ret = nouveau_fence_emit(fence, chan);
		if (ret)
			nouveau_fence_unref(&fence);
	}

	*pfence = fence;
	return ret;
}
