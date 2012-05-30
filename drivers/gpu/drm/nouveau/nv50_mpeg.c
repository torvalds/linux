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

struct nv50_mpeg_engine {
	struct nouveau_exec_engine base;
};

static inline u32
CTX_PTR(struct drm_device *dev, u32 offset)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;

	if (dev_priv->chipset == 0x50)
		offset += 0x0260;
	else
		offset += 0x0060;

	return offset;
}

static int
nv50_mpeg_context_new(struct nouveau_channel *chan, int engine)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramin = chan->ramin;
	struct nouveau_gpuobj *ctx = NULL;
	int ret;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	ret = nouveau_gpuobj_new(dev, chan, 128 * 4, 0, NVOBJ_FLAG_ZERO_ALLOC |
				 NVOBJ_FLAG_ZERO_FREE, &ctx);
	if (ret)
		return ret;

	nv_wo32(ramin, CTX_PTR(dev, 0x00), 0x80190002);
	nv_wo32(ramin, CTX_PTR(dev, 0x04), ctx->vinst + ctx->size - 1);
	nv_wo32(ramin, CTX_PTR(dev, 0x08), ctx->vinst);
	nv_wo32(ramin, CTX_PTR(dev, 0x0c), 0);
	nv_wo32(ramin, CTX_PTR(dev, 0x10), 0);
	nv_wo32(ramin, CTX_PTR(dev, 0x14), 0x00010000);

	nv_wo32(ctx, 0x70, 0x00801ec1);
	nv_wo32(ctx, 0x7c, 0x0000037c);
	dev_priv->engine.instmem.flush(dev);

	chan->engctx[engine] = ctx;
	return 0;
}

static void
nv50_mpeg_context_del(struct nouveau_channel *chan, int engine)
{
	struct nouveau_gpuobj *ctx = chan->engctx[engine];
	struct drm_device *dev = chan->dev;
	int i;

	for (i = 0x00; i <= 0x14; i += 4)
		nv_wo32(chan->ramin, CTX_PTR(dev, i), 0x00000000);

	nouveau_gpuobj_ref(NULL, &ctx);
	chan->engctx[engine] = NULL;
}

static int
nv50_mpeg_object_new(struct nouveau_channel *chan, int engine,
		     u32 handle, u16 class)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *obj = NULL;
	int ret;

	ret = nouveau_gpuobj_new(dev, chan, 16, 16, NVOBJ_FLAG_ZERO_FREE, &obj);
	if (ret)
		return ret;
	obj->engine = 2;
	obj->class  = class;

	nv_wo32(obj, 0x00, class);
	nv_wo32(obj, 0x04, 0x00000000);
	nv_wo32(obj, 0x08, 0x00000000);
	nv_wo32(obj, 0x0c, 0x00000000);
	dev_priv->engine.instmem.flush(dev);

	ret = nouveau_ramht_insert(chan, handle, obj);
	nouveau_gpuobj_ref(NULL, &obj);
	return ret;
}

static void
nv50_mpeg_tlb_flush(struct drm_device *dev, int engine)
{
	nv50_vm_flush_engine(dev, 0x08);
}

static int
nv50_mpeg_init(struct drm_device *dev, int engine)
{
	nv_wr32(dev, 0x00b32c, 0x00000000);
	nv_wr32(dev, 0x00b314, 0x00000100);
	nv_wr32(dev, 0x00b0e0, 0x0000001a);

	nv_wr32(dev, 0x00b220, 0x00000044);
	nv_wr32(dev, 0x00b300, 0x00801ec1);
	nv_wr32(dev, 0x00b390, 0x00000000);
	nv_wr32(dev, 0x00b394, 0x00000000);
	nv_wr32(dev, 0x00b398, 0x00000000);
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
nv50_mpeg_fini(struct drm_device *dev, int engine, bool suspend)
{
	nv_mask(dev, 0x00b32c, 0x00000001, 0x00000000);
	nv_wr32(dev, 0x00b140, 0x00000000);
	return 0;
}

static void
nv50_mpeg_isr(struct drm_device *dev)
{
	u32 stat = nv_rd32(dev, 0x00b100);
	u32 type = nv_rd32(dev, 0x00b230);
	u32 mthd = nv_rd32(dev, 0x00b234);
	u32 data = nv_rd32(dev, 0x00b238);
	u32 show = stat;

	if (stat & 0x01000000) {
		/* happens on initial binding of the object */
		if (type == 0x00000020 && mthd == 0x0000) {
			nv_wr32(dev, 0x00b308, 0x00000100);
			show &= ~0x01000000;
		}
	}

	if (show && nouveau_ratelimit()) {
		NV_INFO(dev, "PMPEG - 0x%08x 0x%08x 0x%08x 0x%08x\n",
			stat, type, mthd, data);
	}

	nv_wr32(dev, 0x00b100, stat);
	nv_wr32(dev, 0x00b230, 0x00000001);
	nv50_fb_vm_trap(dev, 1);
}

static void
nv50_vpe_isr(struct drm_device *dev)
{
	if (nv_rd32(dev, 0x00b100))
		nv50_mpeg_isr(dev);

	if (nv_rd32(dev, 0x00b800)) {
		u32 stat = nv_rd32(dev, 0x00b800);
		NV_INFO(dev, "PMSRCH: 0x%08x\n", stat);
		nv_wr32(dev, 0xb800, stat);
	}
}

static void
nv50_mpeg_destroy(struct drm_device *dev, int engine)
{
	struct nv50_mpeg_engine *pmpeg = nv_engine(dev, engine);

	nouveau_irq_unregister(dev, 0);

	NVOBJ_ENGINE_DEL(dev, MPEG);
	kfree(pmpeg);
}

int
nv50_mpeg_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nv50_mpeg_engine *pmpeg;

	pmpeg = kzalloc(sizeof(*pmpeg), GFP_KERNEL);
	if (!pmpeg)
		return -ENOMEM;

	pmpeg->base.destroy = nv50_mpeg_destroy;
	pmpeg->base.init = nv50_mpeg_init;
	pmpeg->base.fini = nv50_mpeg_fini;
	pmpeg->base.context_new = nv50_mpeg_context_new;
	pmpeg->base.context_del = nv50_mpeg_context_del;
	pmpeg->base.object_new = nv50_mpeg_object_new;
	pmpeg->base.tlb_flush = nv50_mpeg_tlb_flush;

	if (dev_priv->chipset == 0x50) {
		nouveau_irq_register(dev, 0, nv50_vpe_isr);
		NVOBJ_ENGINE_ADD(dev, MPEG, &pmpeg->base);
		NVOBJ_CLASS(dev, 0x3174, MPEG);
#if 0
		NVOBJ_ENGINE_ADD(dev, ME, &pme->base);
		NVOBJ_CLASS(dev, 0x4075, ME);
#endif
	} else {
		nouveau_irq_register(dev, 0, nv50_mpeg_isr);
		NVOBJ_ENGINE_ADD(dev, MPEG, &pmpeg->base);
		NVOBJ_CLASS(dev, 0x8274, MPEG);
	}

	return 0;

}
