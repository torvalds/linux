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

#include <linux/ktime.h>
#include <linux/hrtimer.h>

#include "nouveau_drv.h"
#include "nouveau_ramht.h"
#include "nouveau_dma.h"

#define USE_REFCNT(dev) (nouveau_private(dev)->chipset >= 0x10)
#define USE_SEMA(dev) (nouveau_private(dev)->chipset >= 0x17)

struct nouveau_fence {
	struct nouveau_channel *channel;
	struct kref refcount;
	struct list_head entry;

	uint32_t sequence;
	bool signalled;

	void (*work)(void *priv, bool signalled);
	void *priv;
};

struct nouveau_semaphore {
	struct kref ref;
	struct drm_device *dev;
	struct drm_mm_node *mem;
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

	nouveau_channel_ref(NULL, &fence->channel);
	kfree(fence);
}

void
nouveau_fence_update(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct nouveau_fence *tmp, *fence;
	uint32_t sequence;

	spin_lock(&chan->fence.lock);

	/* Fetch the last sequence if the channel is still up and running */
	if (likely(!list_empty(&chan->fence.pending))) {
		if (USE_REFCNT(dev))
			sequence = nvchan_rd32(chan, 0x48);
		else
			sequence = atomic_read(&chan->fence.last_sequence_irq);

		if (chan->fence.sequence_ack == sequence)
			goto out;
		chan->fence.sequence_ack = sequence;
	}

	list_for_each_entry_safe(fence, tmp, &chan->fence.pending, entry) {
		sequence = fence->sequence;
		fence->signalled = true;
		list_del(&fence->entry);

		if (unlikely(fence->work))
			fence->work(fence->priv, true);

		kref_put(&fence->refcount, nouveau_fence_del);

		if (sequence == chan->fence.sequence_ack)
			break;
	}
out:
	spin_unlock(&chan->fence.lock);
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
	nouveau_channel_ref(chan, &fence->channel);

	if (emit)
		ret = nouveau_fence_emit(fence);

	if (ret)
		nouveau_fence_unref(&fence);
	*pfence = fence;
	return ret;
}

struct nouveau_channel *
nouveau_fence_channel(struct nouveau_fence *fence)
{
	return fence ? nouveau_channel_get_unlocked(fence->channel) : NULL;
}

int
nouveau_fence_emit(struct nouveau_fence *fence)
{
	struct nouveau_channel *chan = fence->channel;
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	ret = RING_SPACE(chan, 2);
	if (ret)
		return ret;

	if (unlikely(chan->fence.sequence == chan->fence.sequence_ack - 1)) {
		nouveau_fence_update(chan);

		BUG_ON(chan->fence.sequence ==
		       chan->fence.sequence_ack - 1);
	}

	fence->sequence = ++chan->fence.sequence;

	kref_get(&fence->refcount);
	spin_lock(&chan->fence.lock);
	list_add_tail(&fence->entry, &chan->fence.pending);
	spin_unlock(&chan->fence.lock);

	if (USE_REFCNT(dev)) {
		if (dev_priv->card_type < NV_C0)
			BEGIN_RING(chan, NvSubSw, 0x0050, 1);
		else
			BEGIN_NVC0(chan, 2, NvSubM2MF, 0x0050, 1);
	} else {
		BEGIN_RING(chan, NvSubSw, 0x0150, 1);
	}
	OUT_RING (chan, fence->sequence);
	FIRE_RING(chan);

	return 0;
}

void
nouveau_fence_work(struct nouveau_fence *fence,
		   void (*work)(void *priv, bool signalled),
		   void *priv)
{
	BUG_ON(fence->work);

	spin_lock(&fence->channel->fence.lock);

	if (fence->signalled) {
		work(priv, true);
	} else {
		fence->work = work;
		fence->priv = priv;
	}

	spin_unlock(&fence->channel->fence.lock);
}

void
__nouveau_fence_unref(void **sync_obj)
{
	struct nouveau_fence *fence = nouveau_fence(*sync_obj);

	if (fence)
		kref_put(&fence->refcount, nouveau_fence_del);
	*sync_obj = NULL;
}

void *
__nouveau_fence_ref(void *sync_obj)
{
	struct nouveau_fence *fence = nouveau_fence(sync_obj);

	kref_get(&fence->refcount);
	return sync_obj;
}

bool
__nouveau_fence_signalled(void *sync_obj, void *sync_arg)
{
	struct nouveau_fence *fence = nouveau_fence(sync_obj);
	struct nouveau_channel *chan = fence->channel;

	if (fence->signalled)
		return true;

	nouveau_fence_update(chan);
	return fence->signalled;
}

int
__nouveau_fence_wait(void *sync_obj, void *sync_arg, bool lazy, bool intr)
{
	unsigned long timeout = jiffies + (3 * DRM_HZ);
	unsigned long sleep_time = NSEC_PER_MSEC / 1000;
	ktime_t t;
	int ret = 0;

	while (1) {
		if (__nouveau_fence_signalled(sync_obj, sync_arg))
			break;

		if (time_after_eq(jiffies, timeout)) {
			ret = -EBUSY;
			break;
		}

		__set_current_state(intr ? TASK_INTERRUPTIBLE
			: TASK_UNINTERRUPTIBLE);
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

static struct nouveau_semaphore *
semaphore_alloc(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_semaphore *sema;
	int size = (dev_priv->chipset < 0x84) ? 4 : 16;
	int ret, i;

	if (!USE_SEMA(dev))
		return NULL;

	sema = kmalloc(sizeof(*sema), GFP_KERNEL);
	if (!sema)
		goto fail;

	ret = drm_mm_pre_get(&dev_priv->fence.heap);
	if (ret)
		goto fail;

	spin_lock(&dev_priv->fence.lock);
	sema->mem = drm_mm_search_free(&dev_priv->fence.heap, size, 0, 0);
	if (sema->mem)
		sema->mem = drm_mm_get_block_atomic(sema->mem, size, 0);
	spin_unlock(&dev_priv->fence.lock);

	if (!sema->mem)
		goto fail;

	kref_init(&sema->ref);
	sema->dev = dev;
	for (i = sema->mem->start; i < sema->mem->start + size; i += 4)
		nouveau_bo_wr32(dev_priv->fence.bo, i / 4, 0);

	return sema;
fail:
	kfree(sema);
	return NULL;
}

static void
semaphore_free(struct kref *ref)
{
	struct nouveau_semaphore *sema =
		container_of(ref, struct nouveau_semaphore, ref);
	struct drm_nouveau_private *dev_priv = sema->dev->dev_private;

	spin_lock(&dev_priv->fence.lock);
	drm_mm_put_block(sema->mem);
	spin_unlock(&dev_priv->fence.lock);

	kfree(sema);
}

static void
semaphore_work(void *priv, bool signalled)
{
	struct nouveau_semaphore *sema = priv;
	struct drm_nouveau_private *dev_priv = sema->dev->dev_private;

	if (unlikely(!signalled))
		nouveau_bo_wr32(dev_priv->fence.bo, sema->mem->start / 4, 1);

	kref_put(&sema->ref, semaphore_free);
}

static int
semaphore_acquire(struct nouveau_channel *chan, struct nouveau_semaphore *sema)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct nouveau_fence *fence = NULL;
	u64 offset = chan->fence.vma.offset + sema->mem->start;
	int ret;

	if (dev_priv->chipset < 0x84) {
		ret = RING_SPACE(chan, 4);
		if (ret)
			return ret;

		BEGIN_RING(chan, NvSubSw, NV_SW_DMA_SEMAPHORE, 3);
		OUT_RING  (chan, NvSema);
		OUT_RING  (chan, offset);
		OUT_RING  (chan, 1);
	} else
	if (dev_priv->chipset < 0xc0) {
		ret = RING_SPACE(chan, 7);
		if (ret)
			return ret;

		BEGIN_RING(chan, NvSubSw, NV_SW_DMA_SEMAPHORE, 1);
		OUT_RING  (chan, chan->vram_handle);
		BEGIN_RING(chan, NvSubSw, 0x0010, 4);
		OUT_RING  (chan, upper_32_bits(offset));
		OUT_RING  (chan, lower_32_bits(offset));
		OUT_RING  (chan, 1);
		OUT_RING  (chan, 1); /* ACQUIRE_EQ */
	} else {
		ret = RING_SPACE(chan, 5);
		if (ret)
			return ret;

		BEGIN_NVC0(chan, 2, NvSubM2MF, 0x0010, 4);
		OUT_RING  (chan, upper_32_bits(offset));
		OUT_RING  (chan, lower_32_bits(offset));
		OUT_RING  (chan, 1);
		OUT_RING  (chan, 0x1001); /* ACQUIRE_EQ */
	}

	/* Delay semaphore destruction until its work is done */
	ret = nouveau_fence_new(chan, &fence, true);
	if (ret)
		return ret;

	kref_get(&sema->ref);
	nouveau_fence_work(fence, semaphore_work, sema);
	nouveau_fence_unref(&fence);
	return 0;
}

static int
semaphore_release(struct nouveau_channel *chan, struct nouveau_semaphore *sema)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct nouveau_fence *fence = NULL;
	u64 offset = chan->fence.vma.offset + sema->mem->start;
	int ret;

	if (dev_priv->chipset < 0x84) {
		ret = RING_SPACE(chan, 5);
		if (ret)
			return ret;

		BEGIN_RING(chan, NvSubSw, NV_SW_DMA_SEMAPHORE, 2);
		OUT_RING  (chan, NvSema);
		OUT_RING  (chan, offset);
		BEGIN_RING(chan, NvSubSw, NV_SW_SEMAPHORE_RELEASE, 1);
		OUT_RING  (chan, 1);
	} else
	if (dev_priv->chipset < 0xc0) {
		ret = RING_SPACE(chan, 7);
		if (ret)
			return ret;

		BEGIN_RING(chan, NvSubSw, NV_SW_DMA_SEMAPHORE, 1);
		OUT_RING  (chan, chan->vram_handle);
		BEGIN_RING(chan, NvSubSw, 0x0010, 4);
		OUT_RING  (chan, upper_32_bits(offset));
		OUT_RING  (chan, lower_32_bits(offset));
		OUT_RING  (chan, 1);
		OUT_RING  (chan, 2); /* RELEASE */
	} else {
		ret = RING_SPACE(chan, 5);
		if (ret)
			return ret;

		BEGIN_NVC0(chan, 2, NvSubM2MF, 0x0010, 4);
		OUT_RING  (chan, upper_32_bits(offset));
		OUT_RING  (chan, lower_32_bits(offset));
		OUT_RING  (chan, 1);
		OUT_RING  (chan, 0x1002); /* RELEASE */
	}

	/* Delay semaphore destruction until its work is done */
	ret = nouveau_fence_new(chan, &fence, true);
	if (ret)
		return ret;

	kref_get(&sema->ref);
	nouveau_fence_work(fence, semaphore_work, sema);
	nouveau_fence_unref(&fence);
	return 0;
}

int
nouveau_fence_sync(struct nouveau_fence *fence,
		   struct nouveau_channel *wchan)
{
	struct nouveau_channel *chan = nouveau_fence_channel(fence);
	struct drm_device *dev = wchan->dev;
	struct nouveau_semaphore *sema;
	int ret = 0;

	if (likely(!chan || chan == wchan ||
		   nouveau_fence_signalled(fence)))
		goto out;

	sema = semaphore_alloc(dev);
	if (!sema) {
		/* Early card or broken userspace, fall back to
		 * software sync. */
		ret = nouveau_fence_wait(fence, true, false);
		goto out;
	}

	/* try to take chan's mutex, if we can't take it right away
	 * we have to fallback to software sync to prevent locking
	 * order issues
	 */
	if (!mutex_trylock(&chan->mutex)) {
		ret = nouveau_fence_wait(fence, true, false);
		goto out_unref;
	}

	/* Make wchan wait until it gets signalled */
	ret = semaphore_acquire(wchan, sema);
	if (ret)
		goto out_unlock;

	/* Signal the semaphore from chan */
	ret = semaphore_release(chan, sema);

out_unlock:
	mutex_unlock(&chan->mutex);
out_unref:
	kref_put(&sema->ref, semaphore_free);
out:
	if (chan)
		nouveau_channel_put_unlocked(&chan);
	return ret;
}

int
__nouveau_fence_flush(void *sync_obj, void *sync_arg)
{
	return 0;
}

int
nouveau_fence_channel_init(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *obj = NULL;
	int ret;

	if (dev_priv->card_type < NV_C0) {
		/* Create an NV_SW object for various sync purposes */
		ret = nouveau_gpuobj_gr_new(chan, NvSw, NV_SW);
		if (ret)
			return ret;

		ret = RING_SPACE(chan, 2);
		if (ret)
			return ret;

		BEGIN_RING(chan, NvSubSw, 0, 1);
		OUT_RING  (chan, NvSw);
		FIRE_RING (chan);
	}

	/* Setup area of memory shared between all channels for x-chan sync */
	if (USE_SEMA(dev) && dev_priv->chipset < 0x84) {
		struct ttm_mem_reg *mem = &dev_priv->fence.bo->bo.mem;

		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
					     mem->start << PAGE_SHIFT,
					     mem->size, NV_MEM_ACCESS_RW,
					     NV_MEM_TARGET_VRAM, &obj);
		if (ret)
			return ret;

		ret = nouveau_ramht_insert(chan, NvSema, obj);
		nouveau_gpuobj_ref(NULL, &obj);
		if (ret)
			return ret;
	} else {
		/* map fence bo into channel's vm */
		ret = nouveau_bo_vma_add(dev_priv->fence.bo, chan->vm,
					 &chan->fence.vma);
		if (ret)
			return ret;
	}

	INIT_LIST_HEAD(&chan->fence.pending);
	spin_lock_init(&chan->fence.lock);
	atomic_set(&chan->fence.last_sequence_irq, 0);
	return 0;
}

void
nouveau_fence_channel_fini(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct nouveau_fence *tmp, *fence;

	spin_lock(&chan->fence.lock);
	list_for_each_entry_safe(fence, tmp, &chan->fence.pending, entry) {
		fence->signalled = true;
		list_del(&fence->entry);

		if (unlikely(fence->work))
			fence->work(fence->priv, false);

		kref_put(&fence->refcount, nouveau_fence_del);
	}
	spin_unlock(&chan->fence.lock);

	nouveau_bo_vma_del(dev_priv->fence.bo, &chan->fence.vma);
}

int
nouveau_fence_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int size = (dev_priv->chipset < 0x84) ? 4096 : 16384;
	int ret;

	/* Create a shared VRAM heap for cross-channel sync. */
	if (USE_SEMA(dev)) {
		ret = nouveau_bo_new(dev, size, 0, TTM_PL_FLAG_VRAM,
				     0, 0, &dev_priv->fence.bo);
		if (ret)
			return ret;

		ret = nouveau_bo_pin(dev_priv->fence.bo, TTM_PL_FLAG_VRAM);
		if (ret)
			goto fail;

		ret = nouveau_bo_map(dev_priv->fence.bo);
		if (ret)
			goto fail;

		ret = drm_mm_init(&dev_priv->fence.heap, 0,
				  dev_priv->fence.bo->bo.mem.size);
		if (ret)
			goto fail;

		spin_lock_init(&dev_priv->fence.lock);
	}

	return 0;
fail:
	nouveau_bo_unmap(dev_priv->fence.bo);
	nouveau_bo_ref(NULL, &dev_priv->fence.bo);
	return ret;
}

void
nouveau_fence_fini(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (USE_SEMA(dev)) {
		drm_mm_takedown(&dev_priv->fence.heap);
		nouveau_bo_unmap(dev_priv->fence.bo);
		nouveau_bo_unpin(dev_priv->fence.bo);
		nouveau_bo_ref(NULL, &dev_priv->fence.bo);
	}
}
