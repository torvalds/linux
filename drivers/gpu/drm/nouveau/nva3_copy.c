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

#include <linux/firmware.h>
#include "drmP.h"
#include "nouveau_drv.h"
#include "nouveau_util.h"
#include "nouveau_vm.h"
#include "nouveau_ramht.h"
#include "nva3_copy.fuc.h"

struct nva3_copy_engine {
	struct nouveau_exec_engine base;
};

static int
nva3_copy_context_new(struct nouveau_channel *chan, int engine)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramin = chan->ramin;
	struct nouveau_gpuobj *ctx = NULL;
	int ret;

	NV_DEBUG(dev, "ch%d\n", chan->id);

	ret = nouveau_gpuobj_new(dev, chan, 256, 0, NVOBJ_FLAG_ZERO_ALLOC |
				 NVOBJ_FLAG_ZERO_FREE, &ctx);
	if (ret)
		return ret;

	nv_wo32(ramin, 0xc0, 0x00190000);
	nv_wo32(ramin, 0xc4, ctx->vinst + ctx->size - 1);
	nv_wo32(ramin, 0xc8, ctx->vinst);
	nv_wo32(ramin, 0xcc, 0x00000000);
	nv_wo32(ramin, 0xd0, 0x00000000);
	nv_wo32(ramin, 0xd4, 0x00000000);
	dev_priv->engine.instmem.flush(dev);

	atomic_inc(&chan->vm->engref[engine]);
	chan->engctx[engine] = ctx;
	return 0;
}

static int
nva3_copy_object_new(struct nouveau_channel *chan, int engine,
		     u32 handle, u16 class)
{
	struct nouveau_gpuobj *ctx = chan->engctx[engine];

	/* fuc engine doesn't need an object, our ramht code does.. */
	ctx->engine = 3;
	ctx->class  = class;
	return nouveau_ramht_insert(chan, handle, ctx);
}

static void
nva3_copy_context_del(struct nouveau_channel *chan, int engine)
{
	struct nouveau_gpuobj *ctx = chan->engctx[engine];
	struct drm_device *dev = chan->dev;
	u32 inst;

	inst  = (chan->ramin->vinst >> 12);
	inst |= 0x40000000;

	/* disable fifo access */
	nv_wr32(dev, 0x104048, 0x00000000);
	/* mark channel as unloaded if it's currently active */
	if (nv_rd32(dev, 0x104050) == inst)
		nv_mask(dev, 0x104050, 0x40000000, 0x00000000);
	/* mark next channel as invalid if it's about to be loaded */
	if (nv_rd32(dev, 0x104054) == inst)
		nv_mask(dev, 0x104054, 0x40000000, 0x00000000);
	/* restore fifo access */
	nv_wr32(dev, 0x104048, 0x00000003);

	for (inst = 0xc0; inst <= 0xd4; inst += 4)
		nv_wo32(chan->ramin, inst, 0x00000000);

	nouveau_gpuobj_ref(NULL, &ctx);

	atomic_dec(&chan->vm->engref[engine]);
	chan->engctx[engine] = ctx;
}

static void
nva3_copy_tlb_flush(struct drm_device *dev, int engine)
{
	nv50_vm_flush_engine(dev, 0x0d);
}

static int
nva3_copy_init(struct drm_device *dev, int engine)
{
	int i;

	nv_mask(dev, 0x000200, 0x00002000, 0x00000000);
	nv_mask(dev, 0x000200, 0x00002000, 0x00002000);
	nv_wr32(dev, 0x104014, 0xffffffff); /* disable all interrupts */

	/* upload ucode */
	nv_wr32(dev, 0x1041c0, 0x01000000);
	for (i = 0; i < sizeof(nva3_pcopy_data) / 4; i++)
		nv_wr32(dev, 0x1041c4, nva3_pcopy_data[i]);

	nv_wr32(dev, 0x104180, 0x01000000);
	for (i = 0; i < sizeof(nva3_pcopy_code) / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(dev, 0x104188, i >> 6);
		nv_wr32(dev, 0x104184, nva3_pcopy_code[i]);
	}

	/* start it running */
	nv_wr32(dev, 0x10410c, 0x00000000);
	nv_wr32(dev, 0x104104, 0x00000000); /* ENTRY */
	nv_wr32(dev, 0x104100, 0x00000002); /* TRIGGER */
	return 0;
}

static int
nva3_copy_fini(struct drm_device *dev, int engine)
{
	nv_mask(dev, 0x104048, 0x00000003, 0x00000000);

	/* trigger fuc context unload */
	nv_wait(dev, 0x104008, 0x0000000c, 0x00000000);
	nv_mask(dev, 0x104054, 0x40000000, 0x00000000);
	nv_wr32(dev, 0x104000, 0x00000008);
	nv_wait(dev, 0x104008, 0x00000008, 0x00000000);

	nv_wr32(dev, 0x104014, 0xffffffff);
	return 0;
}

static struct nouveau_enum nva3_copy_isr_error_name[] = {
	{ 0x0001, "ILLEGAL_MTHD" },
	{ 0x0002, "INVALID_ENUM" },
	{ 0x0003, "INVALID_BITFIELD" },
	{}
};

static void
nva3_copy_isr(struct drm_device *dev)
{
	u32 dispatch = nv_rd32(dev, 0x10401c);
	u32 stat = nv_rd32(dev, 0x104008) & dispatch & ~(dispatch >> 16);
	u32 inst = nv_rd32(dev, 0x104050) & 0x3fffffff;
	u32 ssta = nv_rd32(dev, 0x104040) & 0x0000ffff;
	u32 addr = nv_rd32(dev, 0x104040) >> 16;
	u32 mthd = (addr & 0x07ff) << 2;
	u32 subc = (addr & 0x3800) >> 11;
	u32 data = nv_rd32(dev, 0x104044);
	int chid = nv50_graph_isr_chid(dev, inst);

	if (stat & 0x00000040) {
		NV_INFO(dev, "PCOPY: DISPATCH_ERROR [");
		nouveau_enum_print(nva3_copy_isr_error_name, ssta);
		printk("] ch %d [0x%08x] subc %d mthd 0x%04x data 0x%08x\n",
			chid, inst, subc, mthd, data);
		nv_wr32(dev, 0x104004, 0x00000040);
		stat &= ~0x00000040;
	}

	if (stat) {
		NV_INFO(dev, "PCOPY: unhandled intr 0x%08x\n", stat);
		nv_wr32(dev, 0x104004, stat);
	}
	nv50_fb_vm_trap(dev, 1);
}

static void
nva3_copy_destroy(struct drm_device *dev, int engine)
{
	struct nva3_copy_engine *pcopy = nv_engine(dev, engine);

	nouveau_irq_unregister(dev, 22);

	NVOBJ_ENGINE_DEL(dev, COPY0);
	kfree(pcopy);
}

int
nva3_copy_create(struct drm_device *dev)
{
	struct nva3_copy_engine *pcopy;

	pcopy = kzalloc(sizeof(*pcopy), GFP_KERNEL);
	if (!pcopy)
		return -ENOMEM;

	pcopy->base.destroy = nva3_copy_destroy;
	pcopy->base.init = nva3_copy_init;
	pcopy->base.fini = nva3_copy_fini;
	pcopy->base.context_new = nva3_copy_context_new;
	pcopy->base.context_del = nva3_copy_context_del;
	pcopy->base.object_new = nva3_copy_object_new;
	pcopy->base.tlb_flush = nva3_copy_tlb_flush;

	nouveau_irq_register(dev, 22, nva3_copy_isr);

	NVOBJ_ENGINE_ADD(dev, COPY0, &pcopy->base);
	NVOBJ_CLASS(dev, 0x85b5, COPY0);
	return 0;
}
