/*
 * Copyright 2005-2006 Stephane Marchesin
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"
#include "nouveau_drm.h"
#include "nouveau_dma.h"

static int
nouveau_channel_pushbuf_ctxdma_init(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_bo *pb = chan->pushbuf_bo;
	struct nouveau_gpuobj *pushbuf = NULL;
	int ret = 0;

	if (dev_priv->card_type >= NV_50) {
		if (dev_priv->card_type < NV_C0) {
			ret = nouveau_gpuobj_dma_new(chan,
						     NV_CLASS_DMA_IN_MEMORY, 0,
						     (1ULL << 40),
						     NV_MEM_ACCESS_RO,
						     NV_MEM_TARGET_VM,
						     &pushbuf);
		}
		chan->pushbuf_base = pb->bo.offset;
	} else
	if (pb->bo.mem.mem_type == TTM_PL_TT) {
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY, 0,
					     dev_priv->gart_info.aper_size,
					     NV_MEM_ACCESS_RO,
					     NV_MEM_TARGET_GART, &pushbuf);
		chan->pushbuf_base = pb->bo.mem.start << PAGE_SHIFT;
	} else
	if (dev_priv->card_type != NV_04) {
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY, 0,
					     dev_priv->fb_available_size,
					     NV_MEM_ACCESS_RO,
					     NV_MEM_TARGET_VRAM, &pushbuf);
		chan->pushbuf_base = pb->bo.mem.start << PAGE_SHIFT;
	} else {
		/* NV04 cmdbuf hack, from original ddx.. not sure of it's
		 * exact reason for existing :)  PCI access to cmdbuf in
		 * VRAM.
		 */
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
					     pci_resource_start(dev->pdev, 1),
					     dev_priv->fb_available_size,
					     NV_MEM_ACCESS_RO,
					     NV_MEM_TARGET_PCI, &pushbuf);
		chan->pushbuf_base = pb->bo.mem.start << PAGE_SHIFT;
	}

	nouveau_gpuobj_ref(pushbuf, &chan->pushbuf);
	nouveau_gpuobj_ref(NULL, &pushbuf);
	return ret;
}

static struct nouveau_bo *
nouveau_channel_user_pushbuf_alloc(struct drm_device *dev)
{
	struct nouveau_bo *pushbuf = NULL;
	int location, ret;

	if (nouveau_vram_pushbuf)
		location = TTM_PL_FLAG_VRAM;
	else
		location = TTM_PL_FLAG_TT;

	ret = nouveau_bo_new(dev, NULL, 65536, 0, location, 0, 0x0000, &pushbuf);
	if (ret) {
		NV_ERROR(dev, "error allocating DMA push buffer: %d\n", ret);
		return NULL;
	}

	ret = nouveau_bo_pin(pushbuf, location);
	if (ret) {
		NV_ERROR(dev, "error pinning DMA push buffer: %d\n", ret);
		nouveau_bo_ref(NULL, &pushbuf);
		return NULL;
	}

	ret = nouveau_bo_map(pushbuf);
	if (ret) {
		nouveau_bo_unpin(pushbuf);
		nouveau_bo_ref(NULL, &pushbuf);
		return NULL;
	}

	return pushbuf;
}

/* allocates and initializes a fifo for user space consumption */
int
nouveau_channel_alloc(struct drm_device *dev, struct nouveau_channel **chan_ret,
		      struct drm_file *file_priv,
		      uint32_t vram_handle, uint32_t gart_handle)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_channel *chan;
	unsigned long flags;
	int ret;

	/* allocate and lock channel structure */
	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;
	chan->dev = dev;
	chan->file_priv = file_priv;
	chan->vram_handle = vram_handle;
	chan->gart_handle = gart_handle;

	kref_init(&chan->ref);
	atomic_set(&chan->users, 1);
	mutex_init(&chan->mutex);
	mutex_lock(&chan->mutex);

	/* allocate hw channel id */
	spin_lock_irqsave(&dev_priv->channels.lock, flags);
	for (chan->id = 0; chan->id < pfifo->channels; chan->id++) {
		if (!dev_priv->channels.ptr[chan->id]) {
			nouveau_channel_ref(chan, &dev_priv->channels.ptr[chan->id]);
			break;
		}
	}
	spin_unlock_irqrestore(&dev_priv->channels.lock, flags);

	if (chan->id == pfifo->channels) {
		mutex_unlock(&chan->mutex);
		kfree(chan);
		return -ENODEV;
	}

	NV_DEBUG(dev, "initialising channel %d\n", chan->id);
	INIT_LIST_HEAD(&chan->nvsw.vbl_wait);
	INIT_LIST_HEAD(&chan->nvsw.flip);
	INIT_LIST_HEAD(&chan->fence.pending);

	/* Allocate DMA push buffer */
	chan->pushbuf_bo = nouveau_channel_user_pushbuf_alloc(dev);
	if (!chan->pushbuf_bo) {
		ret = -ENOMEM;
		NV_ERROR(dev, "pushbuf %d\n", ret);
		nouveau_channel_put(&chan);
		return ret;
	}

	nouveau_dma_pre_init(chan);
	chan->user_put = 0x40;
	chan->user_get = 0x44;

	/* Allocate space for per-channel fixed notifier memory */
	ret = nouveau_notifier_init_channel(chan);
	if (ret) {
		NV_ERROR(dev, "ntfy %d\n", ret);
		nouveau_channel_put(&chan);
		return ret;
	}

	/* Setup channel's default objects */
	ret = nouveau_gpuobj_channel_init(chan, vram_handle, gart_handle);
	if (ret) {
		NV_ERROR(dev, "gpuobj %d\n", ret);
		nouveau_channel_put(&chan);
		return ret;
	}

	/* Create a dma object for the push buffer */
	ret = nouveau_channel_pushbuf_ctxdma_init(chan);
	if (ret) {
		NV_ERROR(dev, "pbctxdma %d\n", ret);
		nouveau_channel_put(&chan);
		return ret;
	}

	/* disable the fifo caches */
	pfifo->reassign(dev, false);

	/* Construct initial RAMFC for new channel */
	ret = pfifo->create_context(chan);
	if (ret) {
		nouveau_channel_put(&chan);
		return ret;
	}

	pfifo->reassign(dev, true);

	ret = nouveau_dma_init(chan);
	if (!ret)
		ret = nouveau_fence_channel_init(chan);
	if (ret) {
		nouveau_channel_put(&chan);
		return ret;
	}

	nouveau_debugfs_channel_init(chan);

	NV_DEBUG(dev, "channel %d initialised\n", chan->id);
	*chan_ret = chan;
	return 0;
}

struct nouveau_channel *
nouveau_channel_get_unlocked(struct nouveau_channel *ref)
{
	struct nouveau_channel *chan = NULL;

	if (likely(ref && atomic_inc_not_zero(&ref->users)))
		nouveau_channel_ref(ref, &chan);

	return chan;
}

struct nouveau_channel *
nouveau_channel_get(struct drm_device *dev, struct drm_file *file_priv, int id)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan;
	unsigned long flags;

	if (unlikely(id < 0 || id >= NOUVEAU_MAX_CHANNEL_NR))
		return ERR_PTR(-EINVAL);

	spin_lock_irqsave(&dev_priv->channels.lock, flags);
	chan = nouveau_channel_get_unlocked(dev_priv->channels.ptr[id]);
	spin_unlock_irqrestore(&dev_priv->channels.lock, flags);

	if (unlikely(!chan))
		return ERR_PTR(-EINVAL);

	if (unlikely(file_priv && chan->file_priv != file_priv)) {
		nouveau_channel_put_unlocked(&chan);
		return ERR_PTR(-EINVAL);
	}

	mutex_lock(&chan->mutex);
	return chan;
}

void
nouveau_channel_put_unlocked(struct nouveau_channel **pchan)
{
	struct nouveau_channel *chan = *pchan;
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	unsigned long flags;
	int i;

	/* decrement the refcount, and we're done if there's still refs */
	if (likely(!atomic_dec_and_test(&chan->users))) {
		nouveau_channel_ref(NULL, pchan);
		return;
	}

	/* no one wants the channel anymore */
	NV_DEBUG(dev, "freeing channel %d\n", chan->id);
	nouveau_debugfs_channel_fini(chan);

	/* give it chance to idle */
	nouveau_channel_idle(chan);

	/* ensure all outstanding fences are signaled.  they should be if the
	 * above attempts at idling were OK, but if we failed this'll tell TTM
	 * we're done with the buffers.
	 */
	nouveau_fence_channel_fini(chan);

	/* boot it off the hardware */
	pfifo->reassign(dev, false);

	/* destroy the engine specific contexts */
	pfifo->destroy_context(chan);
	for (i = 0; i < NVOBJ_ENGINE_NR; i++) {
		if (chan->engctx[i])
			dev_priv->eng[i]->context_del(chan, i);
	}

	pfifo->reassign(dev, true);

	/* aside from its resources, the channel should now be dead,
	 * remove it from the channel list
	 */
	spin_lock_irqsave(&dev_priv->channels.lock, flags);
	nouveau_channel_ref(NULL, &dev_priv->channels.ptr[chan->id]);
	spin_unlock_irqrestore(&dev_priv->channels.lock, flags);

	/* destroy any resources the channel owned */
	nouveau_gpuobj_ref(NULL, &chan->pushbuf);
	if (chan->pushbuf_bo) {
		nouveau_bo_unmap(chan->pushbuf_bo);
		nouveau_bo_unpin(chan->pushbuf_bo);
		nouveau_bo_ref(NULL, &chan->pushbuf_bo);
	}
	nouveau_gpuobj_channel_takedown(chan);
	nouveau_notifier_takedown_channel(chan);

	nouveau_channel_ref(NULL, pchan);
}

void
nouveau_channel_put(struct nouveau_channel **pchan)
{
	mutex_unlock(&(*pchan)->mutex);
	nouveau_channel_put_unlocked(pchan);
}

static void
nouveau_channel_del(struct kref *ref)
{
	struct nouveau_channel *chan =
		container_of(ref, struct nouveau_channel, ref);

	kfree(chan);
}

void
nouveau_channel_ref(struct nouveau_channel *chan,
		    struct nouveau_channel **pchan)
{
	if (chan)
		kref_get(&chan->ref);

	if (*pchan)
		kref_put(&(*pchan)->ref, nouveau_channel_del);

	*pchan = chan;
}

void
nouveau_channel_idle(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct nouveau_fence *fence = NULL;
	int ret;

	nouveau_fence_update(chan);

	if (chan->fence.sequence != chan->fence.sequence_ack) {
		ret = nouveau_fence_new(chan, &fence, true);
		if (!ret) {
			ret = nouveau_fence_wait(fence, false, false);
			nouveau_fence_unref(&fence);
		}

		if (ret)
			NV_ERROR(dev, "Failed to idle channel %d.\n", chan->id);
	}
}

/* cleans up all the fifos from file_priv */
void
nouveau_channel_cleanup(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	struct nouveau_channel *chan;
	int i;

	NV_DEBUG(dev, "clearing FIFO enables from file_priv\n");
	for (i = 0; i < engine->fifo.channels; i++) {
		chan = nouveau_channel_get(dev, file_priv, i);
		if (IS_ERR(chan))
			continue;

		atomic_dec(&chan->users);
		nouveau_channel_put(&chan);
	}
}


/***********************************
 * ioctls wrapping the functions
 ***********************************/

static int
nouveau_ioctl_fifo_alloc(struct drm_device *dev, void *data,
			 struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct drm_nouveau_channel_alloc *init = data;
	struct nouveau_channel *chan;
	int ret;

	if (!dev_priv->eng[NVOBJ_ENGINE_GR])
		return -ENODEV;

	if (init->fb_ctxdma_handle == ~0 || init->tt_ctxdma_handle == ~0)
		return -EINVAL;

	ret = nouveau_channel_alloc(dev, &chan, file_priv,
				    init->fb_ctxdma_handle,
				    init->tt_ctxdma_handle);
	if (ret)
		return ret;
	init->channel  = chan->id;

	if (chan->dma.ib_max)
		init->pushbuf_domains = NOUVEAU_GEM_DOMAIN_VRAM |
					NOUVEAU_GEM_DOMAIN_GART;
	else if (chan->pushbuf_bo->bo.mem.mem_type == TTM_PL_VRAM)
		init->pushbuf_domains = NOUVEAU_GEM_DOMAIN_VRAM;
	else
		init->pushbuf_domains = NOUVEAU_GEM_DOMAIN_GART;

	if (dev_priv->card_type < NV_C0) {
		init->subchan[0].handle = NvM2MF;
		if (dev_priv->card_type < NV_50)
			init->subchan[0].grclass = 0x0039;
		else
			init->subchan[0].grclass = 0x5039;
		init->subchan[1].handle = NvSw;
		init->subchan[1].grclass = NV_SW;
		init->nr_subchan = 2;
	} else {
		init->subchan[0].handle  = 0x9039;
		init->subchan[0].grclass = 0x9039;
		init->nr_subchan = 1;
	}

	/* Named memory object area */
	ret = drm_gem_handle_create(file_priv, chan->notifier_bo->gem,
				    &init->notifier_handle);

	if (ret == 0)
		atomic_inc(&chan->users); /* userspace reference */
	nouveau_channel_put(&chan);
	return ret;
}

static int
nouveau_ioctl_fifo_free(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_nouveau_channel_free *req = data;
	struct nouveau_channel *chan;

	chan = nouveau_channel_get(dev, file_priv, req->channel);
	if (IS_ERR(chan))
		return PTR_ERR(chan);

	atomic_dec(&chan->users);
	nouveau_channel_put(&chan);
	return 0;
}

/***********************************
 * finally, the ioctl table
 ***********************************/

struct drm_ioctl_desc nouveau_ioctls[] = {
	DRM_IOCTL_DEF_DRV(NOUVEAU_GETPARAM, nouveau_ioctl_getparam, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_SETPARAM, nouveau_ioctl_setparam, DRM_UNLOCKED|DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_ALLOC, nouveau_ioctl_fifo_alloc, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_FREE, nouveau_ioctl_fifo_free, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GROBJ_ALLOC, nouveau_ioctl_grobj_alloc, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_NOTIFIEROBJ_ALLOC, nouveau_ioctl_notifier_alloc, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GPUOBJ_FREE, nouveau_ioctl_gpuobj_free, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_NEW, nouveau_gem_ioctl_new, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_PUSHBUF, nouveau_gem_ioctl_pushbuf, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_PREP, nouveau_gem_ioctl_cpu_prep, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_FINI, nouveau_gem_ioctl_cpu_fini, DRM_UNLOCKED|DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_INFO, nouveau_gem_ioctl_info, DRM_UNLOCKED|DRM_AUTH),
};

int nouveau_max_ioctl = DRM_ARRAY_SIZE(nouveau_ioctls);
