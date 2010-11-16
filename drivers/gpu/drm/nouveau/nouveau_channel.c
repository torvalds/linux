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
	int ret;

	if (dev_priv->card_type >= NV_50) {
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY, 0,
					     dev_priv->vm_end, NV_DMA_ACCESS_RO,
					     NV_DMA_TARGET_AGP, &pushbuf);
		chan->pushbuf_base = pb->bo.offset;
	} else
	if (pb->bo.mem.mem_type == TTM_PL_TT) {
		ret = nouveau_gpuobj_gart_dma_new(chan, 0,
						  dev_priv->gart_info.aper_size,
						  NV_DMA_ACCESS_RO, &pushbuf,
						  NULL);
		chan->pushbuf_base = pb->bo.mem.start << PAGE_SHIFT;
	} else
	if (dev_priv->card_type != NV_04) {
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY, 0,
					     dev_priv->fb_available_size,
					     NV_DMA_ACCESS_RO,
					     NV_DMA_TARGET_VIDMEM, &pushbuf);
		chan->pushbuf_base = pb->bo.mem.start << PAGE_SHIFT;
	} else {
		/* NV04 cmdbuf hack, from original ddx.. not sure of it's
		 * exact reason for existing :)  PCI access to cmdbuf in
		 * VRAM.
		 */
		ret = nouveau_gpuobj_dma_new(chan, NV_CLASS_DMA_IN_MEMORY,
					     pci_resource_start(dev->pdev,
					     1),
					     dev_priv->fb_available_size,
					     NV_DMA_ACCESS_RO,
					     NV_DMA_TARGET_PCI, &pushbuf);
		chan->pushbuf_base = pb->bo.mem.start << PAGE_SHIFT;
	}

	nouveau_gpuobj_ref(pushbuf, &chan->pushbuf);
	nouveau_gpuobj_ref(NULL, &pushbuf);
	return 0;
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

	ret = nouveau_bo_new(dev, NULL, 65536, 0, location, 0, 0x0000, false,
			     true, &pushbuf);
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

	return pushbuf;
}

/* allocates and initializes a fifo for user space consumption */
int
nouveau_channel_alloc(struct drm_device *dev, struct nouveau_channel **chan_ret,
		      struct drm_file *file_priv,
		      uint32_t vram_handle, uint32_t tt_handle)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	struct nouveau_channel *chan;
	int channel, user;
	int ret;

	/*
	 * Alright, here is the full story
	 * Nvidia cards have multiple hw fifo contexts (praise them for that,
	 * no complicated crash-prone context switches)
	 * We allocate a new context for each app and let it write to it
	 * directly (woo, full userspace command submission !)
	 * When there are no more contexts, you lost
	 */
	for (channel = 0; channel < pfifo->channels; channel++) {
		if (dev_priv->fifos[channel] == NULL)
			break;
	}

	/* no more fifos. you lost. */
	if (channel == pfifo->channels)
		return -EINVAL;

	dev_priv->fifos[channel] = kzalloc(sizeof(struct nouveau_channel),
					   GFP_KERNEL);
	if (!dev_priv->fifos[channel])
		return -ENOMEM;
	chan = dev_priv->fifos[channel];
	INIT_LIST_HEAD(&chan->nvsw.vbl_wait);
	INIT_LIST_HEAD(&chan->fence.pending);
	chan->dev = dev;
	chan->id = channel;
	chan->file_priv = file_priv;
	chan->vram_handle = vram_handle;
	chan->gart_handle = tt_handle;

	NV_INFO(dev, "Allocating FIFO number %d\n", channel);

	/* Allocate DMA push buffer */
	chan->pushbuf_bo = nouveau_channel_user_pushbuf_alloc(dev);
	if (!chan->pushbuf_bo) {
		ret = -ENOMEM;
		NV_ERROR(dev, "pushbuf %d\n", ret);
		nouveau_channel_free(chan);
		return ret;
	}

	nouveau_dma_pre_init(chan);

	/* Locate channel's user control regs */
	if (dev_priv->card_type < NV_40)
		user = NV03_USER(channel);
	else
	if (dev_priv->card_type < NV_50)
		user = NV40_USER(channel);
	else
		user = NV50_USER(channel);

	chan->user = ioremap(pci_resource_start(dev->pdev, 0) + user,
								PAGE_SIZE);
	if (!chan->user) {
		NV_ERROR(dev, "ioremap of regs failed.\n");
		nouveau_channel_free(chan);
		return -ENOMEM;
	}
	chan->user_put = 0x40;
	chan->user_get = 0x44;

	/* Allocate space for per-channel fixed notifier memory */
	ret = nouveau_notifier_init_channel(chan);
	if (ret) {
		NV_ERROR(dev, "ntfy %d\n", ret);
		nouveau_channel_free(chan);
		return ret;
	}

	/* Setup channel's default objects */
	ret = nouveau_gpuobj_channel_init(chan, vram_handle, tt_handle);
	if (ret) {
		NV_ERROR(dev, "gpuobj %d\n", ret);
		nouveau_channel_free(chan);
		return ret;
	}

	/* Create a dma object for the push buffer */
	ret = nouveau_channel_pushbuf_ctxdma_init(chan);
	if (ret) {
		NV_ERROR(dev, "pbctxdma %d\n", ret);
		nouveau_channel_free(chan);
		return ret;
	}

	/* disable the fifo caches */
	pfifo->reassign(dev, false);

	/* Create a graphics context for new channel */
	ret = pgraph->create_context(chan);
	if (ret) {
		nouveau_channel_free(chan);
		return ret;
	}

	/* Construct inital RAMFC for new channel */
	ret = pfifo->create_context(chan);
	if (ret) {
		nouveau_channel_free(chan);
		return ret;
	}

	pfifo->reassign(dev, true);

	ret = nouveau_dma_init(chan);
	if (!ret)
		ret = nouveau_fence_channel_init(chan);
	if (ret) {
		nouveau_channel_free(chan);
		return ret;
	}

	nouveau_debugfs_channel_init(chan);

	NV_INFO(dev, "%s: initialised FIFO %d\n", __func__, channel);
	*chan_ret = chan;
	return 0;
}

/* stops a fifo */
void
nouveau_channel_free(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_fifo_engine *pfifo = &dev_priv->engine.fifo;
	unsigned long flags;
	int ret;

	NV_INFO(dev, "%s: freeing fifo %d\n", __func__, chan->id);

	nouveau_debugfs_channel_fini(chan);

	/* Give outstanding push buffers a chance to complete */
	nouveau_fence_update(chan);
	if (chan->fence.sequence != chan->fence.sequence_ack) {
		struct nouveau_fence *fence = NULL;

		ret = nouveau_fence_new(chan, &fence, true);
		if (ret == 0) {
			ret = nouveau_fence_wait(fence, NULL, false, false);
			nouveau_fence_unref((void *)&fence);
		}

		if (ret)
			NV_ERROR(dev, "Failed to idle channel %d.\n", chan->id);
	}

	/* Ensure all outstanding fences are signaled.  They should be if the
	 * above attempts at idling were OK, but if we failed this'll tell TTM
	 * we're done with the buffers.
	 */
	nouveau_fence_channel_fini(chan);

	/* This will prevent pfifo from switching channels. */
	pfifo->reassign(dev, false);

	/* We want to give pgraph a chance to idle and get rid of all potential
	 * errors. We need to do this before the lock, otherwise the irq handler
	 * is unable to process them.
	 */
	if (pgraph->channel(dev) == chan)
		nouveau_wait_for_idle(dev);

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);

	pgraph->fifo_access(dev, false);
	if (pgraph->channel(dev) == chan)
		pgraph->unload_context(dev);
	pgraph->destroy_context(chan);
	pgraph->fifo_access(dev, true);

	if (pfifo->channel_id(dev) == chan->id) {
		pfifo->disable(dev);
		pfifo->unload_context(dev);
		pfifo->enable(dev);
	}
	pfifo->destroy_context(chan);

	pfifo->reassign(dev, true);

	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	/* Release the channel's resources */
	nouveau_gpuobj_ref(NULL, &chan->pushbuf);
	if (chan->pushbuf_bo) {
		nouveau_bo_unmap(chan->pushbuf_bo);
		nouveau_bo_unpin(chan->pushbuf_bo);
		nouveau_bo_ref(NULL, &chan->pushbuf_bo);
	}
	nouveau_gpuobj_channel_takedown(chan);
	nouveau_notifier_takedown_channel(chan);
	if (chan->user)
		iounmap(chan->user);

	dev_priv->fifos[chan->id] = NULL;
	kfree(chan);
}

/* cleans up all the fifos from file_priv */
void
nouveau_channel_cleanup(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;
	int i;

	NV_DEBUG(dev, "clearing FIFO enables from file_priv\n");
	for (i = 0; i < engine->fifo.channels; i++) {
		struct nouveau_channel *chan = dev_priv->fifos[i];

		if (chan && chan->file_priv == file_priv)
			nouveau_channel_free(chan);
	}
}

int
nouveau_channel_owner(struct drm_device *dev, struct drm_file *file_priv,
		      int channel)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_engine *engine = &dev_priv->engine;

	if (channel >= engine->fifo.channels)
		return 0;
	if (dev_priv->fifos[channel] == NULL)
		return 0;

	return (dev_priv->fifos[channel]->file_priv == file_priv);
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

	if (dev_priv->engine.graph.accel_blocked)
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

	init->subchan[0].handle = NvM2MF;
	if (dev_priv->card_type < NV_50)
		init->subchan[0].grclass = 0x0039;
	else
		init->subchan[0].grclass = 0x5039;
	init->subchan[1].handle = NvSw;
	init->subchan[1].grclass = NV_SW;
	init->nr_subchan = 2;

	/* Named memory object area */
	ret = drm_gem_handle_create(file_priv, chan->notifier_bo->gem,
				    &init->notifier_handle);
	if (ret) {
		nouveau_channel_free(chan);
		return ret;
	}

	return 0;
}

static int
nouveau_ioctl_fifo_free(struct drm_device *dev, void *data,
			struct drm_file *file_priv)
{
	struct drm_nouveau_channel_free *cfree = data;
	struct nouveau_channel *chan;

	NOUVEAU_GET_USER_CHANNEL_WITH_RETURN(cfree->channel, file_priv, chan);

	nouveau_channel_free(chan);
	return 0;
}

/***********************************
 * finally, the ioctl table
 ***********************************/

struct drm_ioctl_desc nouveau_ioctls[] = {
	DRM_IOCTL_DEF_DRV(NOUVEAU_GETPARAM, nouveau_ioctl_getparam, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_SETPARAM, nouveau_ioctl_setparam, DRM_AUTH|DRM_MASTER|DRM_ROOT_ONLY),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_ALLOC, nouveau_ioctl_fifo_alloc, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_CHANNEL_FREE, nouveau_ioctl_fifo_free, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GROBJ_ALLOC, nouveau_ioctl_grobj_alloc, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_NOTIFIEROBJ_ALLOC, nouveau_ioctl_notifier_alloc, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GPUOBJ_FREE, nouveau_ioctl_gpuobj_free, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_NEW, nouveau_gem_ioctl_new, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_PUSHBUF, nouveau_gem_ioctl_pushbuf, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_PREP, nouveau_gem_ioctl_cpu_prep, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_CPU_FINI, nouveau_gem_ioctl_cpu_fini, DRM_AUTH),
	DRM_IOCTL_DEF_DRV(NOUVEAU_GEM_INFO, nouveau_gem_ioctl_info, DRM_AUTH),
};

int nouveau_max_ioctl = DRM_ARRAY_SIZE(nouveau_ioctls);
