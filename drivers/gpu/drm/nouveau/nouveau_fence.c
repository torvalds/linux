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

#include "drmP.h"
#include "drm.h"

#include "nouveau_drv.h"
#include "nouveau_dma.h"

#define USE_REFCNT (dev_priv->card_type >= NV_10)

struct nouveau_fence {
	struct nouveau_channel *channel;
	struct kref refcount;
	struct list_head entry;

	uint32_t sequence;
	bool signalled;
};

static inline struct nouveau_fence *
nouveau_fence(void *sync_obj)
{
	return (struct nouveau_fence *)sync_obj;
}

static void
nouveau_fence_del(struct kref *ref)
{
	struct nouveau_fence *fence =
		container_of(ref, struct nouveau_fence, refcount);

	kfree(fence);
}

void
nouveau_fence_update(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct list_head *entry, *tmp;
	struct nouveau_fence *fence;
	uint32_t sequence;

	if (USE_REFCNT)
		sequence = nvchan_rd32(chan, 0x48);
	else
		sequence = chan->fence.last_sequence_irq;

	if (chan->fence.sequence_ack == sequence)
		return;
	chan->fence.sequence_ack = sequence;

	list_for_each_safe(entry, tmp, &chan->fence.pending) {
		fence = list_entry(entry, struct nouveau_fence, entry);

		sequence = fence->sequence;
		fence->signalled = true;
		list_del(&fence->entry);
		kref_put(&fence->refcount, nouveau_fence_del);

		if (sequence == chan->fence.sequence_ack)
			break;
	}
}

int
nouveau_fence_new(struct nouveau_channel *chan, struct nouveau_fence **pfence,
		  bool emit)
{
	struct nouveau_fence *fence;
	int ret = 0;

	fence = kzalloc(sizeof(*fence), GFP_KERNEL);
	if (!fence)
		return -ENOMEM;
	kref_init(&fence->refcount);
	fence->channel = chan;

	if (emit)
		ret = nouveau_fence_emit(fence);

	if (ret)
		nouveau_fence_unref((void *)&fence);
	*pfence = fence;
	return ret;
}

struct nouveau_channel *
nouveau_fence_channel(struct nouveau_fence *fence)
{
	return fence ? fence->channel : NULL;
}

int
nouveau_fence_emit(struct nouveau_fence *fence)
{
	struct drm_nouveau_private *dev_priv = fence->channel->dev->dev_private;
	struct nouveau_channel *chan = fence->channel;
	unsigned long flags;
	int ret;

	ret = RING_SPACE(chan, 2);
	if (ret)
		return ret;

	if (unlikely(chan->fence.sequence == chan->fence.sequence_ack - 1)) {
		spin_lock_irqsave(&chan->fence.lock, flags);
		nouveau_fence_update(chan);
		spin_unlock_irqrestore(&chan->fence.lock, flags);

		BUG_ON(chan->fence.sequence ==
		       chan->fence.sequence_ack - 1);
	}

	fence->sequence = ++chan->fence.sequence;

	kref_get(&fence->refcount);
	spin_lock_irqsave(&chan->fence.lock, flags);
	list_add_tail(&fence->entry, &chan->fence.pending);
	spin_unlock_irqrestore(&chan->fence.lock, flags);

	BEGIN_RING(chan, NvSubSw, USE_REFCNT ? 0x0050 : 0x0150, 1);
	OUT_RING(chan, fence->sequence);
	FIRE_RING(chan);

	return 0;
}

void
nouveau_fence_unref(void **sync_obj)
{
	struct nouveau_fence *fence = nouveau_fence(*sync_obj);

	if (fence)
		kref_put(&fence->refcount, nouveau_fence_del);
	*sync_obj = NULL;
}

void *
nouveau_fence_ref(void *sync_obj)
{
	struct nouveau_fence *fence = nouveau_fence(sync_obj);

	kref_get(&fence->refcount);
	return sync_obj;
}

bool
nouveau_fence_signalled(void *sync_obj, void *sync_arg)
{
	struct nouveau_fence *fence = nouveau_fence(sync_obj);
	struct nouveau_channel *chan = fence->channel;
	unsigned long flags;

	if (fence->signalled)
		return true;

	spin_lock_irqsave(&chan->fence.lock, flags);
	nouveau_fence_update(chan);
	spin_unlock_irqrestore(&chan->fence.lock, flags);
	return fence->signalled;
}

int
nouveau_fence_wait(void *sync_obj, void *sync_arg, bool lazy, bool intr)
{
	unsigned long timeout = jiffies + (3 * DRM_HZ);
	int ret = 0;

	__set_current_state(intr ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);

	while (1) {
		if (nouveau_fence_signalled(sync_obj, sync_arg))
			break;

		if (time_after_eq(jiffies, timeout)) {
			ret = -EBUSY;
			break;
		}

		if (lazy)
			schedule_timeout(1);

		if (intr && signal_pending(current)) {
			ret = -ERESTARTSYS;
			break;
		}
	}

	__set_current_state(TASK_RUNNING);

	return ret;
}

int
nouveau_fence_flush(void *sync_obj, void *sync_arg)
{
	return 0;
}

void
nouveau_fence_handler(struct drm_device *dev, int channel)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan = NULL;

	if (channel >= 0 && channel < dev_priv->engine.fifo.channels)
		chan = dev_priv->fifos[channel];

	if (chan) {
		spin_lock_irq(&chan->fence.lock);
		nouveau_fence_update(chan);
		spin_unlock_irq(&chan->fence.lock);
	}
}

int
nouveau_fence_init(struct nouveau_channel *chan)
{
	INIT_LIST_HEAD(&chan->fence.pending);
	spin_lock_init(&chan->fence.lock);
	return 0;
}

void
nouveau_fence_fini(struct nouveau_channel *chan)
{
	struct list_head *entry, *tmp;
	struct nouveau_fence *fence;

	list_for_each_safe(entry, tmp, &chan->fence.pending) {
		fence = list_entry(entry, struct nouveau_fence, entry);

		fence->signalled = true;
		list_del(&fence->entry);
		kref_put(&fence->refcount, nouveau_fence_del);
	}
}

