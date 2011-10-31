/*
 * Copyright 2011 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_ramht.h"

struct nv31_mpeg_engine {
	struct nouveau_exec_engine base;
	atomic_t refcount;
};


static int
nv31_mpeg_context_new(struct nouveau_channel *chan, int engine)
{
	struct nv31_mpeg_engine *pmpeg = nv_engine(chan->dev, engine);

	if (!atomic_add_unless(&pmpeg->refcount, 1, 1))
		return -EBUSY;

	chan->engctx[engine] = (void *)0xdeadcafe;
	return 0;
}

static void
nv31_mpeg_context_del(struct nouveau_channel *chan, int engine)
{
	struct nv31_mpeg_engine *pmpeg = nv_engine(chan->dev, engine);
	atomic_dec(&pmpeg->refcount);
	chan->engctx[engine] = NULL;
}

static int
nv40_mpeg_context_new(struct nouveau_channel *chan, int engine)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ctx = NULL;
	unsigned long flags;
	int ret;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	ret = nouveau_gpuobj_new(dev, NULL, 264 * 4, 16, NVOBJ_FLAG_ZERO_ALLOC |
				 NVOBJ_FLAG_ZERO_FREE, &ctx);
	if (ret)
		return ret;

	nv_wo32(ctx, 0x78, 0x02001ec1);

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv_mask(dev, 0x002500, 0x00000001, 0x00000000);
	if ((nv_rd32(dev, 0x003204) & 0x1f) == chan->id)
		nv_wr32(dev, 0x00330c, ctx->pinst >> 4);
	nv_wo32(chan->ramfc, 0x54, ctx->pinst >> 4);
	nv_mask(dev, 0x002500, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	chan->engctx[engine] = ctx;
	return 0;
}

static void
nv40_mpeg_context_del(struct nouveau_channel *chan, int engine)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct nouveau_gpuobj *ctx = chan->engctx[engine];
	struct drm_device *dev = chan->dev;
	unsigned long flags;
	u32 inst = 0x80000000 | (ctx->pinst >> 4);

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv_mask(dev, 0x00b32c, 0x00000001, 0x00000000);
	if (nv_rd32(dev, 0x00b318) == inst)
		nv_mask(dev, 0x00b318, 0x80000000, 0x00000000);
	nv_mask(dev, 0x00b32c, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	nouveau_gpuobj_ref(NULL, &ctx);
	chan->engctx[engine] = NULL;
}

static int
nv31_mpeg_object_new(struct nouveau_channel *chan, int engine,
		      u32 handle, u16 class)
{
	struct drm_device *dev = chan->dev;
	struct nouveau_gpuobj *obj = NULL;
	int ret;

	ret = nouveau_gpuobj_new(dev, chan, 20, 16, NVOBJ_FLAG_ZERO_ALLOC |
				 NVOBJ_FLAG_ZERO_FREE, &obj);
	if (ret)
		return ret;
	obj->engine = 2;
	obj->class  = class;

	nv_wo32(obj, 0x00, class);

	ret = nouveau_ramht_insert(chan, handle, obj);
	nouveau_gpuobj_ref(NULL, &obj);
	return ret;
}

static int
nv31_mpeg_init(struct drm_device *dev, int engine)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv31_mpeg_engine *pmpeg = nv_engine(dev, engine);
	int i;

	/* VPE init */
	nv_mask(dev, 0x000200, 0x00000002, 0x00000000);
	nv_mask(dev, 0x000200, 0x00000002, 0x00000002);
	nv_wr32(dev, 0x00b0e0, 0x00000020); /* nvidia: rd 0x01, wr 0x20 */
	nv_wr32(dev, 0x00b0e8, 0x00000020); /* nvidia: rd 0x01, wr 0x20 */

	for (i = 0; i < dev_priv->engine.fb.num_tiles; i++)
		pmpeg->base.set_tile_region(dev, i);

	/* PMPEG init */
	nv_wr32(dev, 0x00b32c, 0x00000000);
	nv_wr32(dev, 0x00b314, 0x00000100);
	nv_wr32(dev, 0x00b220, nv44_graph_class(dev) ? 0x00000044 : 0x00000031);
	nv_wr32(dev, 0x00b300, 0x02001ec1);
	nv_mask(dev, 0x00b32c, 0x00000001, 0x00000001);

	nv_wr32(dev, 0x00b100, 0xffffffff);
	nv_wr32(dev, 0x00b140, 0xffffffff);

	if (!nv_wait(dev, 0x00b200, 0x00000001, 0x00000000)) {
		NV_ERROR(dev, "PMPEG init: 0x%08x\n", nv_rd32(dev, 0x00b200));
		return -EBUSY;
	}

	return 0;
}

static int
nv31_mpeg_fini(struct drm_device *dev, int engine, bool suspend)
{
	/*XXX: context save? */
	nv_mask(dev, 0x00b32c, 0x00000001, 0x00000000);
	nv_wr32(dev, 0x00b140, 0x00000000);
	return 0;
}

static int
nv31_mpeg_mthd_dma(struct nouveau_channel *chan, u32 class, u32 mthd, u32 data)
{
	struct drm_device *dev = chan->dev;
	u32 inst = data << 4;
	u32 dma0 = nv_ri32(dev, inst + 0);
	u32 dma1 = nv_ri32(dev, inst + 4);
	u32 dma2 = nv_ri32(dev, inst + 8);
	u32 base = (dma2 & 0xfffff000) | (dma0 >> 20);
	u32 size = dma1 + 1;

	/* only allow linear DMA objects */
	if (!(dma0 & 0x00002000))
		return -EINVAL;

	if (mthd == 0x0190) {
		/* DMA_CMD */
		nv_mask(dev, 0x00b300, 0x00030000, (dma0 & 0x00030000));
		nv_wr32(dev, 0x00b334, base);
		nv_wr32(dev, 0x00b324, size);
	} else
	if (mthd == 0x01a0) {
		/* DMA_DATA */
		nv_mask(dev, 0x00b300, 0x000c0000, (dma0 & 0x00030000) << 2);
		nv_wr32(dev, 0x00b360, base);
		nv_wr32(dev, 0x00b364, size);
	} else {
		/* DMA_IMAGE, VRAM only */
		if (dma0 & 0x000c0000)
			return -EINVAL;

		nv_wr32(dev, 0x00b370, base);
		nv_wr32(dev, 0x00b374, size);
	}

	return 0;
}

static int
nv31_mpeg_isr_chid(struct drm_device *dev, u32 inst)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ctx;
	unsigned long flags;
	int i;

	/* hardcode drm channel id on nv3x, so swmthd lookup works */
	if (dev_priv->card_type < NV_40)
		return 0;

	spin_lock_irqsave(&dev_priv->channels.lock, flags);
	for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
		if (!dev_priv->channels.ptr[i])
			continue;

		ctx = dev_priv->channels.ptr[i]->engctx[NVOBJ_ENGINE_MPEG];
		if (ctx && ctx->pinst == inst)
			break;
	}
	spin_unlock_irqrestore(&dev_priv->channels.lock, flags);
	return i;
}

static void
nv31_vpe_set_tile_region(struct drm_device *dev, int i)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_tile_reg *tile = &dev_priv->tile.reg[i];

	nv_wr32(dev, 0x00b008 + (i * 0x10), tile->pitch);
	nv_wr32(dev, 0x00b004 + (i * 0x10), tile->limit);
	nv_wr32(dev, 0x00b000 + (i * 0x10), tile->addr);
}

static void
nv31_mpeg_isr(struct drm_device *dev)
{
	u32 inst = (nv_rd32(dev, 0x00b318) & 0x000fffff) << 4;
	u32 chid = nv31_mpeg_isr_chid(dev, inst);
	u32 stat = nv_rd32(dev, 0x00b100);
	u32 type = nv_rd32(dev, 0x00b230);
	u32 mthd = nv_rd32(dev, 0x00b234);
	u32 data = nv_rd32(dev, 0x00b238);
	u32 show = stat;

	if (stat & 0x01000000) {
		/* happens on initial binding of the object */
		if (type == 0x00000020 && mthd == 0x0000) {
			nv_mask(dev, 0x00b308, 0x00000000, 0x00000000);
			show &= ~0x01000000;
		}

		if (type == 0x00000010) {
			if (!nouveau_gpuobj_mthd_call2(dev, chid, 0x3174, mthd, data))
				show &= ~0x01000000;
		}
	}

	nv_wr32(dev, 0x00b100, stat);
	nv_wr32(dev, 0x00b230, 0x00000001);

	if (show && nouveau_ratelimit()) {
		NV_INFO(dev, "PMPEG: Ch %d [0x%08x] 0x%08x 0x%08x 0x%08x 0x%08x\n",
			chid, inst, stat, type, mthd, data);
	}
}

static void
nv31_vpe_isr(struct drm_device *dev)
{
	if (nv_rd32(dev, 0x00b100))
		nv31_mpeg_isr(dev);

	if (nv_rd32(dev, 0x00b800)) {
		u32 stat = nv_rd32(dev, 0x00b800);
		NV_INFO(dev, "PMSRCH: 0x%08x\n", stat);
		nv_wr32(dev, 0xb800, stat);
	}
}

static void
nv31_mpeg_destroy(struct drm_device *dev, int engine)
{
	struct nv31_mpeg_engine *pmpeg = nv_engine(dev, engine);

	nouveau_irq_unregister(dev, 0);

	NVOBJ_ENGINE_DEL(dev, MPEG);
	kfree(pmpeg);
}

int
nv31_mpeg_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv31_mpeg_engine *pmpeg;

	pmpeg = kzalloc(sizeof(*pmpeg), GFP_KERNEL);
	if (!pmpeg)
		return -ENOMEM;
	atomic_set(&pmpeg->refcount, 0);

	pmpeg->base.destroy = nv31_mpeg_destroy;
	pmpeg->base.init = nv31_mpeg_init;
	pmpeg->base.fini = nv31_mpeg_fini;
	if (dev_priv->card_type < NV_40) {
		pmpeg->base.context_new = nv31_mpeg_context_new;
		pmpeg->base.context_del = nv31_mpeg_context_del;
	} else {
		pmpeg->base.context_new = nv40_mpeg_context_new;
		pmpeg->base.context_del = nv40_mpeg_context_del;
	}
	pmpeg->base.object_new = nv31_mpeg_object_new;

	/* ISR vector, PMC_ENABLE bit,  and TILE regs are shared between
	 * all VPE engines, for this driver's purposes the PMPEG engine
	 * will be treated as the "master" and handle the global VPE
	 * bits too
	 */
	pmpeg->base.set_tile_region = nv31_vpe_set_tile_region;
	nouveau_irq_register(dev, 0, nv31_vpe_isr);

	NVOBJ_ENGINE_ADD(dev, MPEG, &pmpeg->base);
	NVOBJ_CLASS(dev, 0x3174, MPEG);
	NVOBJ_MTHD (dev, 0x3174, 0x0190, nv31_mpeg_mthd_dma);
	NVOBJ_MTHD (dev, 0x3174, 0x01a0, nv31_mpeg_mthd_dma);
	NVOBJ_MTHD (dev, 0x3174, 0x01b0, nv31_mpeg_mthd_dma);

#if 0
	NVOBJ_ENGINE_ADD(dev, ME, &pme->base);
	NVOBJ_CLASS(dev, 0x4075, ME);
#endif
	return 0;

}
