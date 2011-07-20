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
#include "nvc0_copy.fuc.h"

struct nvc0_copy_engine {
	struct nouveau_exec_engine base;
	u32 irq;
	u32 pmc;
	u32 fuc;
	u32 ctx;
};

static int
nvc0_copy_context_new(struct nouveau_channel *chan, int engine)
{
	struct nvc0_copy_engine *pcopy = nv_engine(chan->dev, engine);
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ramin = chan->ramin;
	struct nouveau_gpuobj *ctx = NULL;
	int ret;

	ret = nouveau_gpuobj_new(dev, chan, 256, 256,
				 NVOBJ_FLAG_VM | NVOBJ_FLAG_VM_USER |
				 NVOBJ_FLAG_ZERO_ALLOC, &ctx);
	if (ret)
		return ret;

	nv_wo32(ramin, pcopy->ctx + 0, lower_32_bits(ctx->linst));
	nv_wo32(ramin, pcopy->ctx + 4, upper_32_bits(ctx->linst));
	dev_priv->engine.instmem.flush(dev);

	chan->engctx[engine] = ctx;
	return 0;
}

static int
nvc0_copy_object_new(struct nouveau_channel *chan, int engine,
		     u32 handle, u16 class)
{
	return 0;
}

static void
nvc0_copy_context_del(struct nouveau_channel *chan, int engine)
{
	struct nvc0_copy_engine *pcopy = nv_engine(chan->dev, engine);
	struct nouveau_gpuobj *ctx = chan->engctx[engine];
	struct drm_device *dev = chan->dev;
	u32 inst;

	inst  = (chan->ramin->vinst >> 12);
	inst |= 0x40000000;

	/* disable fifo access */
	nv_wr32(dev, pcopy->fuc + 0x048, 0x00000000);
	/* mark channel as unloaded if it's currently active */
	if (nv_rd32(dev, pcopy->fuc + 0x050) == inst)
		nv_mask(dev, pcopy->fuc + 0x050, 0x40000000, 0x00000000);
	/* mark next channel as invalid if it's about to be loaded */
	if (nv_rd32(dev, pcopy->fuc + 0x054) == inst)
		nv_mask(dev, pcopy->fuc + 0x054, 0x40000000, 0x00000000);
	/* restore fifo access */
	nv_wr32(dev, pcopy->fuc + 0x048, 0x00000003);

	nv_wo32(chan->ramin, pcopy->ctx + 0, 0x00000000);
	nv_wo32(chan->ramin, pcopy->ctx + 4, 0x00000000);
	nouveau_gpuobj_ref(NULL, &ctx);

	chan->engctx[engine] = ctx;
}

static int
nvc0_copy_init(struct drm_device *dev, int engine)
{
	struct nvc0_copy_engine *pcopy = nv_engine(dev, engine);
	int i;

	nv_mask(dev, 0x000200, pcopy->pmc, 0x00000000);
	nv_mask(dev, 0x000200, pcopy->pmc, pcopy->pmc);
	nv_wr32(dev, pcopy->fuc + 0x014, 0xffffffff);

	nv_wr32(dev, pcopy->fuc + 0x1c0, 0x01000000);
	for (i = 0; i < sizeof(nvc0_pcopy_data) / 4; i++)
		nv_wr32(dev, pcopy->fuc + 0x1c4, nvc0_pcopy_data[i]);

	nv_wr32(dev, pcopy->fuc + 0x180, 0x01000000);
	for (i = 0; i < sizeof(nvc0_pcopy_code) / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(dev, pcopy->fuc + 0x188, i >> 6);
		nv_wr32(dev, pcopy->fuc + 0x184, nvc0_pcopy_code[i]);
	}

	nv_wr32(dev, pcopy->fuc + 0x084, engine - NVOBJ_ENGINE_COPY0);
	nv_wr32(dev, pcopy->fuc + 0x10c, 0x00000000);
	nv_wr32(dev, pcopy->fuc + 0x104, 0x00000000); /* ENTRY */
	nv_wr32(dev, pcopy->fuc + 0x100, 0x00000002); /* TRIGGER */
	return 0;
}

static int
nvc0_copy_fini(struct drm_device *dev, int engine, bool suspend)
{
	struct nvc0_copy_engine *pcopy = nv_engine(dev, engine);

	nv_mask(dev, pcopy->fuc + 0x048, 0x00000003, 0x00000000);

	/* trigger fuc context unload */
	nv_wait(dev, pcopy->fuc + 0x008, 0x0000000c, 0x00000000);
	nv_mask(dev, pcopy->fuc + 0x054, 0x40000000, 0x00000000);
	nv_wr32(dev, pcopy->fuc + 0x000, 0x00000008);
	nv_wait(dev, pcopy->fuc + 0x008, 0x00000008, 0x00000000);

	nv_wr32(dev, pcopy->fuc + 0x014, 0xffffffff);
	return 0;
}

static struct nouveau_enum nvc0_copy_isr_error_name[] = {
	{ 0x0001, "ILLEGAL_MTHD" },
	{ 0x0002, "INVALID_ENUM" },
	{ 0x0003, "INVALID_BITFIELD" },
	{}
};

static void
nvc0_copy_isr(struct drm_device *dev, int engine)
{
	struct nvc0_copy_engine *pcopy = nv_engine(dev, engine);
	u32 disp = nv_rd32(dev, pcopy->fuc + 0x01c);
	u32 stat = nv_rd32(dev, pcopy->fuc + 0x008) & disp & ~(disp >> 16);
	u64 inst = (u64)(nv_rd32(dev, pcopy->fuc + 0x050) & 0x0fffffff) << 12;
	u32 chid = nvc0_graph_isr_chid(dev, inst);
	u32 ssta = nv_rd32(dev, pcopy->fuc + 0x040) & 0x0000ffff;
	u32 addr = nv_rd32(dev, pcopy->fuc + 0x040) >> 16;
	u32 mthd = (addr & 0x07ff) << 2;
	u32 subc = (addr & 0x3800) >> 11;
	u32 data = nv_rd32(dev, pcopy->fuc + 0x044);

	if (stat & 0x00000040) {
		NV_INFO(dev, "PCOPY: DISPATCH_ERROR [");
		nouveau_enum_print(nvc0_copy_isr_error_name, ssta);
		printk("] ch %d [0x%010llx] subc %d mthd 0x%04x data 0x%08x\n",
			chid, inst, subc, mthd, data);
		nv_wr32(dev, pcopy->fuc + 0x004, 0x00000040);
		stat &= ~0x00000040;
	}

	if (stat) {
		NV_INFO(dev, "PCOPY: unhandled intr 0x%08x\n", stat);
		nv_wr32(dev, pcopy->fuc + 0x004, stat);
	}
}

static void
nvc0_copy_isr_0(struct drm_device *dev)
{
	nvc0_copy_isr(dev, NVOBJ_ENGINE_COPY0);
}

static void
nvc0_copy_isr_1(struct drm_device *dev)
{
	nvc0_copy_isr(dev, NVOBJ_ENGINE_COPY1);
}

static void
nvc0_copy_destroy(struct drm_device *dev, int engine)
{
	struct nvc0_copy_engine *pcopy = nv_engine(dev, engine);

	nouveau_irq_unregister(dev, pcopy->irq);

	if (engine == NVOBJ_ENGINE_COPY0)
		NVOBJ_ENGINE_DEL(dev, COPY0);
	else
		NVOBJ_ENGINE_DEL(dev, COPY1);
	kfree(pcopy);
}

int
nvc0_copy_create(struct drm_device *dev, int engine)
{
	struct nvc0_copy_engine *pcopy;

	pcopy = kzalloc(sizeof(*pcopy), GFP_KERNEL);
	if (!pcopy)
		return -ENOMEM;

	pcopy->base.destroy = nvc0_copy_destroy;
	pcopy->base.init = nvc0_copy_init;
	pcopy->base.fini = nvc0_copy_fini;
	pcopy->base.context_new = nvc0_copy_context_new;
	pcopy->base.context_del = nvc0_copy_context_del;
	pcopy->base.object_new = nvc0_copy_object_new;

	if (engine == 0) {
		pcopy->irq = 5;
		pcopy->pmc = 0x00000040;
		pcopy->fuc = 0x104000;
		pcopy->ctx = 0x0230;
		nouveau_irq_register(dev, pcopy->irq, nvc0_copy_isr_0);
		NVOBJ_ENGINE_ADD(dev, COPY0, &pcopy->base);
		NVOBJ_CLASS(dev, 0x90b5, COPY0);
	} else {
		pcopy->irq = 6;
		pcopy->pmc = 0x00000080;
		pcopy->fuc = 0x105000;
		pcopy->ctx = 0x0240;
		nouveau_irq_register(dev, pcopy->irq, nvc0_copy_isr_1);
		NVOBJ_ENGINE_ADD(dev, COPY1, &pcopy->base);
		NVOBJ_CLASS(dev, 0x90b8, COPY1);
	}

	return 0;
}
