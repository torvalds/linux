/*
 * Copyright (C) 2007 Ben Skeggs.
 *
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
#include "nouveau_vm.h"

#define BAR1_VM_BASE 0x0020000000ULL
#define BAR1_VM_SIZE pci_resource_len(dev->pdev, 1)
#define BAR3_VM_BASE 0x0000000000ULL
#define BAR3_VM_SIZE pci_resource_len(dev->pdev, 3)

struct nv50_instmem_priv {
	uint32_t save1700[5]; /* 0x1700->0x1710 */

	struct nouveau_gpuobj *bar1_dmaobj;
	struct nouveau_gpuobj *bar3_dmaobj;
};

static void
nv50_channel_del(struct nouveau_channel **pchan)
{
	struct nouveau_channel *chan;

	chan = *pchan;
	*pchan = NULL;
	if (!chan)
		return;

	nouveau_gpuobj_ref(NULL, &chan->ramfc);
	nouveau_vm_ref(NULL, &chan->vm, chan->vm_pd);
	nouveau_gpuobj_ref(NULL, &chan->vm_pd);
	if (chan->ramin_heap.free_stack.next)
		drm_mm_takedown(&chan->ramin_heap);
	nouveau_gpuobj_ref(NULL, &chan->ramin);
	kfree(chan);
}

static int
nv50_channel_new(struct drm_device *dev, u32 size, struct nouveau_vm *vm,
		 struct nouveau_channel **pchan)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 pgd = (dev_priv->chipset == 0x50) ? 0x1400 : 0x0200;
	u32  fc = (dev_priv->chipset == 0x50) ? 0x0000 : 0x4200;
	struct nouveau_channel *chan;
	int ret, i;

	chan = kzalloc(sizeof(*chan), GFP_KERNEL);
	if (!chan)
		return -ENOMEM;
	chan->dev = dev;

	ret = nouveau_gpuobj_new(dev, NULL, size, 0x1000, 0, &chan->ramin);
	if (ret) {
		nv50_channel_del(&chan);
		return ret;
	}

	ret = drm_mm_init(&chan->ramin_heap, 0x6000, chan->ramin->size);
	if (ret) {
		nv50_channel_del(&chan);
		return ret;
	}

	ret = nouveau_gpuobj_new_fake(dev, chan->ramin->pinst == ~0 ? ~0 :
				      chan->ramin->pinst + pgd,
				      chan->ramin->vinst + pgd,
				      0x4000, NVOBJ_FLAG_ZERO_ALLOC,
				      &chan->vm_pd);
	if (ret) {
		nv50_channel_del(&chan);
		return ret;
	}

	for (i = 0; i < 0x4000; i += 8) {
		nv_wo32(chan->vm_pd, i + 0, 0x00000000);
		nv_wo32(chan->vm_pd, i + 4, 0xdeadcafe);
	}

	ret = nouveau_vm_ref(vm, &chan->vm, chan->vm_pd);
	if (ret) {
		nv50_channel_del(&chan);
		return ret;
	}

	ret = nouveau_gpuobj_new_fake(dev, chan->ramin->pinst == ~0 ? ~0 :
				      chan->ramin->pinst + fc,
				      chan->ramin->vinst + fc, 0x100,
				      NVOBJ_FLAG_ZERO_ALLOC, &chan->ramfc);
	if (ret) {
		nv50_channel_del(&chan);
		return ret;
	}

	*pchan = chan;
	return 0;
}

int
nv50_instmem_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv;
	struct nouveau_channel *chan;
	struct nouveau_vm *vm;
	int ret, i;
	u32 tmp;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	dev_priv->engine.instmem.priv = priv;

	/* Save state, will restore at takedown. */
	for (i = 0x1700; i <= 0x1710; i += 4)
		priv->save1700[(i-0x1700)/4] = nv_rd32(dev, i);

	/* Global PRAMIN heap */
	ret = drm_mm_init(&dev_priv->ramin_heap, 0, dev_priv->ramin_size);
	if (ret) {
		NV_ERROR(dev, "Failed to init RAMIN heap\n");
		goto error;
	}

	/* BAR3 */
	ret = nouveau_vm_new(dev, BAR3_VM_BASE, BAR3_VM_SIZE, BAR3_VM_BASE,
			     29, 12, 16, &dev_priv->bar3_vm);
	if (ret)
		goto error;

	ret = nouveau_gpuobj_new(dev, NULL, (BAR3_VM_SIZE >> 12) * 8,
				 0x1000, NVOBJ_FLAG_DONT_MAP |
				 NVOBJ_FLAG_ZERO_ALLOC,
				 &dev_priv->bar3_vm->pgt[0].obj);
	if (ret)
		goto error;
	dev_priv->bar3_vm->pgt[0].page_shift = 12;
	dev_priv->bar3_vm->pgt[0].refcount = 1;

	nv50_instmem_map(dev_priv->bar3_vm->pgt[0].obj);

	ret = nv50_channel_new(dev, 128 * 1024, dev_priv->bar3_vm, &chan);
	if (ret)
		goto error;
	dev_priv->channels.ptr[0] = dev_priv->channels.ptr[127] = chan;

	ret = nv50_gpuobj_dma_new(chan, 0x0000, BAR3_VM_BASE, BAR3_VM_SIZE,
				  NV_MEM_TARGET_VM, NV_MEM_ACCESS_VM,
				  NV_MEM_TYPE_VM, NV_MEM_COMP_VM,
				  &priv->bar3_dmaobj);
	if (ret)
		goto error;

	nv_wr32(dev, 0x001704, 0x00000000 | (chan->ramin->vinst >> 12));
	nv_wr32(dev, 0x001704, 0x40000000 | (chan->ramin->vinst >> 12));
	nv_wr32(dev, 0x00170c, 0x80000000 | (priv->bar3_dmaobj->cinst >> 4));

	tmp = nv_ri32(dev, 0);
	nv_wi32(dev, 0, ~tmp);
	if (nv_ri32(dev, 0) != ~tmp) {
		NV_ERROR(dev, "PRAMIN readback failed\n");
		ret = -EIO;
		goto error;
	}
	nv_wi32(dev, 0, tmp);

	dev_priv->ramin_available = true;

	/* BAR1 */
	ret = nouveau_vm_new(dev, BAR1_VM_BASE, BAR1_VM_SIZE, BAR1_VM_BASE,
			     29, 12, 16, &vm);
	if (ret)
		goto error;

	ret = nouveau_vm_ref(vm, &dev_priv->bar1_vm, chan->vm_pd);
	if (ret)
		goto error;
	nouveau_vm_ref(NULL, &vm, NULL);

	ret = nv50_gpuobj_dma_new(chan, 0x0000, BAR1_VM_BASE, BAR1_VM_SIZE,
				  NV_MEM_TARGET_VM, NV_MEM_ACCESS_VM,
				  NV_MEM_TYPE_VM, NV_MEM_COMP_VM,
				  &priv->bar1_dmaobj);
	if (ret)
		goto error;

	nv_wr32(dev, 0x001708, 0x80000000 | (priv->bar1_dmaobj->cinst >> 4));
	for (i = 0; i < 8; i++)
		nv_wr32(dev, 0x1900 + (i*4), 0);

	/* Create shared channel VM, space is reserved at the beginning
	 * to catch "NULL pointer" references
	 */
	ret = nouveau_vm_new(dev, 0, (1ULL << 40), 0x0020000000ULL,
			     29, 12, 16, &dev_priv->chan_vm);
	if (ret)
		return ret;

	return 0;

error:
	nv50_instmem_takedown(dev);
	return ret;
}

void
nv50_instmem_takedown(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv = dev_priv->engine.instmem.priv;
	struct nouveau_channel *chan = dev_priv->channels.ptr[0];
	int i;

	NV_DEBUG(dev, "\n");

	if (!priv)
		return;

	dev_priv->ramin_available = false;

	nouveau_vm_ref(NULL, &dev_priv->chan_vm, NULL);

	for (i = 0x1700; i <= 0x1710; i += 4)
		nv_wr32(dev, i, priv->save1700[(i - 0x1700) / 4]);

	nouveau_gpuobj_ref(NULL, &priv->bar3_dmaobj);
	nouveau_gpuobj_ref(NULL, &priv->bar1_dmaobj);

	nouveau_vm_ref(NULL, &dev_priv->bar1_vm, chan->vm_pd);
	dev_priv->channels.ptr[127] = 0;
	nv50_channel_del(&dev_priv->channels.ptr[0]);

	nouveau_gpuobj_ref(NULL, &dev_priv->bar3_vm->pgt[0].obj);
	nouveau_vm_ref(NULL, &dev_priv->bar3_vm, NULL);

	if (dev_priv->ramin_heap.free_stack.next)
		drm_mm_takedown(&dev_priv->ramin_heap);

	dev_priv->engine.instmem.priv = NULL;
	kfree(priv);
}

int
nv50_instmem_suspend(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	dev_priv->ramin_available = false;
	return 0;
}

void
nv50_instmem_resume(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_instmem_priv *priv = dev_priv->engine.instmem.priv;
	struct nouveau_channel *chan = dev_priv->channels.ptr[0];
	int i;

	/* Poke the relevant regs, and pray it works :) */
	nv_wr32(dev, NV50_PUNK_BAR_CFG_BASE, (chan->ramin->vinst >> 12));
	nv_wr32(dev, NV50_PUNK_UNK1710, 0);
	nv_wr32(dev, NV50_PUNK_BAR_CFG_BASE, (chan->ramin->vinst >> 12) |
					 NV50_PUNK_BAR_CFG_BASE_VALID);
	nv_wr32(dev, NV50_PUNK_BAR1_CTXDMA, (priv->bar1_dmaobj->cinst >> 4) |
					NV50_PUNK_BAR1_CTXDMA_VALID);
	nv_wr32(dev, NV50_PUNK_BAR3_CTXDMA, (priv->bar3_dmaobj->cinst >> 4) |
					NV50_PUNK_BAR3_CTXDMA_VALID);

	for (i = 0; i < 8; i++)
		nv_wr32(dev, 0x1900 + (i*4), 0);

	dev_priv->ramin_available = true;
}

struct nv50_gpuobj_node {
	struct nouveau_vram *vram;
	struct nouveau_vma chan_vma;
	u32 align;
};


int
nv50_instmem_get(struct nouveau_gpuobj *gpuobj, u32 size, u32 align)
{
	struct drm_device *dev = gpuobj->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_gpuobj_node *node = NULL;
	int ret;

	node = kzalloc(sizeof(*node), GFP_KERNEL);
	if (!node)
		return -ENOMEM;
	node->align = align;

	size  = (size + 4095) & ~4095;
	align = max(align, (u32)4096);

	ret = nv50_vram_new(dev, size, align, 0, 0, &node->vram);
	if (ret) {
		kfree(node);
		return ret;
	}

	gpuobj->vinst = node->vram->offset;

	if (gpuobj->flags & NVOBJ_FLAG_VM) {
		ret = nouveau_vm_get(dev_priv->chan_vm, size, 12,
				     NV_MEM_ACCESS_RW | NV_MEM_ACCESS_SYS,
				     &node->chan_vma);
		if (ret) {
			nv50_vram_del(dev, &node->vram);
			kfree(node);
			return ret;
		}

		nouveau_vm_map(&node->chan_vma, node->vram);
		gpuobj->vinst = node->chan_vma.offset;
	}

	gpuobj->size = size;
	gpuobj->node = node;
	return 0;
}

void
nv50_instmem_put(struct nouveau_gpuobj *gpuobj)
{
	struct drm_device *dev = gpuobj->dev;
	struct nv50_gpuobj_node *node;

	node = gpuobj->node;
	gpuobj->node = NULL;

	if (node->chan_vma.node) {
		nouveau_vm_unmap(&node->chan_vma);
		nouveau_vm_put(&node->chan_vma);
	}
	nv50_vram_del(dev, &node->vram);
	kfree(node);
}

int
nv50_instmem_map(struct nouveau_gpuobj *gpuobj)
{
	struct drm_nouveau_private *dev_priv = gpuobj->dev->dev_private;
	struct nv50_gpuobj_node *node = gpuobj->node;
	int ret;

	ret = nouveau_vm_get(dev_priv->bar3_vm, gpuobj->size, 12,
			     NV_MEM_ACCESS_RW, &node->vram->bar_vma);
	if (ret)
		return ret;

	nouveau_vm_map(&node->vram->bar_vma, node->vram);
	gpuobj->pinst = node->vram->bar_vma.offset;
	return 0;
}

void
nv50_instmem_unmap(struct nouveau_gpuobj *gpuobj)
{
	struct nv50_gpuobj_node *node = gpuobj->node;

	if (node->vram->bar_vma.node) {
		nouveau_vm_unmap(&node->vram->bar_vma);
		nouveau_vm_put(&node->vram->bar_vma);
	}
}

void
nv50_instmem_flush(struct drm_device *dev)
{
	nv_wr32(dev, 0x00330c, 0x00000001);
	if (!nv_wait(dev, 0x00330c, 0x00000002, 0x00000000))
		NV_ERROR(dev, "PRAMIN flush timeout\n");
}

void
nv84_instmem_flush(struct drm_device *dev)
{
	nv_wr32(dev, 0x070000, 0x00000001);
	if (!nv_wait(dev, 0x070000, 0x00000002, 0x00000000))
		NV_ERROR(dev, "PRAMIN flush timeout\n");
}

