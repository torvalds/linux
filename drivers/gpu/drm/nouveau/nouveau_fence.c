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

	kfree(fence);
}

void
nouveau_fence_update(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct nouveau_fence *tmp, *fence;
	uint32_t sequence;

	spin_lock(&chan->fence.lock);

	if (USE_REFCNT(dev))
		sequence = nvchan_rd32(chan, 0x48);
	else
		sequence = atomic_read(&chan->fence.last_sequence_irq);

	if (chan->fence.sequence_ack == sequence)
		goto out;
	chan->fence.sequence_ack = sequence;

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
	struct nouveau_channel *chan = fence->channel;
	struct drm_device *dev = chan->dev;
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

	BEGIN_RING(chan, NvSubSw, USE_REFCNT(dev) ? 0x0050 : 0x0150, 1);
	OUT_RING(chan, fence->sequence);
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

	if (fence->signalled)
		return true;

	nouveau_fence_update(chan);
	return fence->signalled;
}

int
nouveau_fence_wait(void *sync_obj, void *sync_arg, bool lazy, bool intr)
{
	unsigned long timeout = jiffies + (3 * DRM_HZ);
	int ret = 0;

	while (1) {
		if (nouveau_fence_signalled(sync_obj, sync_arg))
			break;

		if (time_after_eq(jiffies, timeout)) {
			ret = -EBUSY;
			break;
		}

		__set_current_state(intr ? TASK_INTERRUPTIBLE
			: TASK_UNINTERRUPTIBLE);
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

static struct nouveau_semaphore *
alloc_semaphore(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_semaphore *sema;
	int ret;

	if (!USE_SEMA(dev))
		return NULL;

	sema = kmalloc(sizeof(*sema), GFP_KERNEL);
	if (!sema)
		goto fail;

	ret = drm_mm_pre_get(&dev_priv->fence.heap);
	if (ret)
		goto fail;

	spin_lock(&dev_priv->fence.lock);
	sema->mem = drm_mm_search_free(&dev_priv->fence.heap, 4, 0, 0);
	if (sema->mem)
		sema->mem = drm_mm_get_block_atomic(sema->mem, 4, 0);
	spin_unlock(&dev_priv->fence.lock);

	if (!sema->mem)
		goto fail;

	kref_init(&sema->ref);
	sema->dev = dev;
	nouveau_bo_wr32(dev_priv->fence.bo, sema->mem->start / 4, 0);

	return sema;
fail:
	kfree(sema);
	return NULL;
}

static void
free_semaphore(struct kref *ref)
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

	kref_put(&sema->ref, free_semaphore);
}

static int
emit_semaphore(struct nouveau_channel *chan, int method,
	       struct nouveau_semaphore *sema)
{
	struct drm_nouveau_private *dev_priv = sema->dev->dev_private;
	struct nouveau_fence *fence;
	bool smart = (dev_priv->card_type >= NV_50);
	int ret;

	ret = RING_SPACE(chan, smart ? 8 : 4);
	if (ret)
		return ret;

	if (smart) {
		BEGIN_RING(chan, NvSubSw, NV_SW_DMA_SEMAPHORE, 1);
		OUT_RING(chan, NvSema);
	}
	BEGIN_RING(chan, NvSubSw, NV_SW_SEMAPHORE_OFFSET, 1);
	OUT_RING(chan, sema->mem->start);

	if (smart && method == NV_SW_SEMAPHORE_ACQUIRE) {
		/*
		 * NV50 tries to be too smart and context-switch
		 * between semaphores instead of doing a "first come,
		 * first served" strategy like previous cards
		 * do.
		 *
		 * That's bad because the ACQUIRE latency can get as
		 * large as the PFIFO context time slice in the
		 * typical DRI2 case where you have several
		 * outstanding semaphores at the same moment.
		 *
		 * If we're going to ACQUIRE, force the card to
		 * context switch before, just in case the matching
		 * RELEASE is already scheduled to be executed in
		 * another channel.
		 */
		BEGIN_RING(chan, NvSubSw, NV_SW_YIELD, 1);
		OUT_RING(chan, 0);
	}

	BEGIN_RING(chan, NvSubSw, method, 1);
	OUT_RING(chan, 1);

	if (smart && method == NV_SW_SEMAPHORE_RELEASE) {
		/*
		 * Force the card to context switch, there may be
		 * another channel waiting for the semaphore we just
		 * released.
		 */
		BEGIN_RING(chan, NvSubSw, NV_SW_YIELD, 1);
		OUT_RING(chan, 0);
	}

	/* Delay semaphore destruction until its work is done */
	ret = nouveau_fence_new(chan, &fence, true);
	if (ret)
		return ret;

	kref_get(&sema->ref);
	nouveau_fence_work(fence, semaphore_work, sema);
	nouveau_fence_unref((void *)&fence);

	return 0;
}

int
nouveau_fence_sync(struct nouveau_fence *fence,
		   struct nouveau_channel *wchan)
{
	struct nouveau_channel *chan = nouveau_fence_channel(fence);
	struct drm_device *dev = wchan->dev;
	struct nouveau_semaphore *sema;
	int ret;

	if (likely(!fence || chan == wchan ||
		   nouveau_fence_signalled(fence, NULL)))
		return 0;

	sema = alloc_semaphore(dev);
	if (!sema) {
		/* Early card or broken userspace, fall back to
		 * software sync. */
		return nouveau_fence_wait(fence, NULL, false, false);
	}

	/* try to take wchan's mutex, if we can't take it right away
	 * we have to fallback to software sync to prevent locking
	 * order issues
	 */
	if (!mutex_trylock(&wchan->mutex)) {
		free_semaphore(&sema->ref);
		return nouveau_fence_wait(fence, NULL, false, false);
	}

	/* Make wchan wait until it gets signalled */
	ret = emit_semaphore(wchan, NV_SW_SEMAPHORE_ACQUIRE, sema);
	mutex_unlock(&wchan->mutex);
	if (ret)
		goto out;

	/* Signal the semaphore from chan */
	ret = emit_semaphore(chan, NV_SW_SEMAPHORE_RELEASE, sema);
out:
	kref_put(&sema->ref, free_semaphore);
	return ret;
}

int
nouveau_fence_flush(void *sync_obj, void *sync_arg)
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

	/* Create an NV_SW object for various sync purposes */
	ret = nouveau_gpuobj_sw_new(chan, NV_SW, &obj);
	if (ret)
		return ret;

	ret = nouveau_ramht_insert(chan, NvSw, obj);
	nouveau_gpuobj_ref(NULL, &obj);
	if (ret)
		return ret;

	ret = RING_SPACE(chan, 2);
	if (ret)
		return ret;
	BEGIN_RING(chan, NvSubSw, 0, 1);
	OUT_RING(chan, NvSw);

	/* Create a DMA object for the shared cross-channel sync area. */
	if (USE_SEMA(dev)) {
		struct drm_mm_node *mem = dev_priv->fence.bo->bo.mem.mm_node;

		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
					     mem->start << PAGE_SHIFT,
					     mem->size << PAGE_SHIFT,
					     NV_DMA_ACCESS_RW,
					     NV_DMA_TARGET_VIDMEM, &obj);
		if (ret)
			return ret;

		ret = nouveau_ramht_insert(chan, NvSema, obj);
		nouveau_gpuobj_ref(NULL, &obj);
		if (ret)
			return ret;

		ret = RING_SPACE(chan, 2);
		if (ret)
			return ret;
		BEGIN_RING(chan, NvSubSw, NV_SW_DMA_SEMAPHORE, 1);
		OUT_RING(chan, NvSema);
	}

	FIRE_RING(chan);

	INIT_LIST_HEAD(&chan->fence.pending);
	spin_lock_init(&chan->fence.lock);
	atomic_set(&chan->fence.last_sequence_irq, 0);

	return 0;
}

void
nouveau_fence_channel_fini(struct nouveau_channel *chan)
{
	struct nouveau_fence *tmp, *fence;

	list_for_each_entry_safe(fence, tmp, &chan->fence.pending, entry) {
		fence->signalled = true;
		list_del(&fence->entry);

		if (unlikely(fence->work))
			fence->work(fence->priv, false);

		kref_put(&fence->refcount, nouveau_fence_del);
	}
}

int
nouveau_fence_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	int ret;

	/* Create a shared VRAM heap for cross-channel sync. */
	if (USE_SEMA(dev)) {
		ret = nouveau_bo_new(dev, NULL, 4096, 0, TTM_PL_FLAG_VRAM,
				     0, 0, false, true, &dev_priv->fence.bo);
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
