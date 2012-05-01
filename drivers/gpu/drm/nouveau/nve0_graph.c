/*
 * Copyright 2010 Red Hat Inc.
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
#include <linux/module.h>

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_mm.h"
#include "nouveau_fifo.h"

#include "nve0_graph.h"

static void
nve0_graph_ctxctl_debug_unit(struct drm_device *dev, u32 base)
{
	NV_INFO(dev, "PGRAPH: %06x - done 0x%08x\n", base,
		nv_rd32(dev, base + 0x400));
	NV_INFO(dev, "PGRAPH: %06x - stat 0x%08x 0x%08x 0x%08x 0x%08x\n", base,
		nv_rd32(dev, base + 0x800), nv_rd32(dev, base + 0x804),
		nv_rd32(dev, base + 0x808), nv_rd32(dev, base + 0x80c));
	NV_INFO(dev, "PGRAPH: %06x - stat 0x%08x 0x%08x 0x%08x 0x%08x\n", base,
		nv_rd32(dev, base + 0x810), nv_rd32(dev, base + 0x814),
		nv_rd32(dev, base + 0x818), nv_rd32(dev, base + 0x81c));
}

static void
nve0_graph_ctxctl_debug(struct drm_device *dev)
{
	u32 gpcnr = nv_rd32(dev, 0x409604) & 0xffff;
	u32 gpc;

	nve0_graph_ctxctl_debug_unit(dev, 0x409000);
	for (gpc = 0; gpc < gpcnr; gpc++)
		nve0_graph_ctxctl_debug_unit(dev, 0x502000 + (gpc * 0x8000));
}

static int
nve0_graph_load_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;

	nv_wr32(dev, 0x409840, 0x00000030);
	nv_wr32(dev, 0x409500, 0x80000000 | chan->ramin->vinst >> 12);
	nv_wr32(dev, 0x409504, 0x00000003);
	if (!nv_wait(dev, 0x409800, 0x00000010, 0x00000010))
		NV_ERROR(dev, "PGRAPH: load_ctx timeout\n");

	return 0;
}

static int
nve0_graph_unload_context_to(struct drm_device *dev, u64 chan)
{
	nv_wr32(dev, 0x409840, 0x00000003);
	nv_wr32(dev, 0x409500, 0x80000000 | chan >> 12);
	nv_wr32(dev, 0x409504, 0x00000009);
	if (!nv_wait(dev, 0x409800, 0x00000001, 0x00000000)) {
		NV_ERROR(dev, "PGRAPH: unload_ctx timeout\n");
		return -EBUSY;
	}

	return 0;
}

static int
nve0_graph_construct_context(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct nve0_graph_priv *priv = nv_engine(chan->dev, NVOBJ_ENGINE_GR);
	struct nve0_graph_chan *grch = chan->engctx[NVOBJ_ENGINE_GR];
	struct drm_device *dev = chan->dev;
	int ret, i;
	u32 *ctx;

	ctx = kmalloc(priv->grctx_size, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	nve0_graph_load_context(chan);

	nv_wo32(grch->grctx, 0x1c, 1);
	nv_wo32(grch->grctx, 0x20, 0);
	nv_wo32(grch->grctx, 0x28, 0);
	nv_wo32(grch->grctx, 0x2c, 0);
	dev_priv->engine.instmem.flush(dev);

	ret = nve0_grctx_generate(chan);
	if (ret)
		goto err;

	ret = nve0_graph_unload_context_to(dev, chan->ramin->vinst);
	if (ret)
		goto err;

	for (i = 0; i < priv->grctx_size; i += 4)
		ctx[i / 4] = nv_ro32(grch->grctx, i);

	priv->grctx_vals = ctx;
	return 0;

err:
	kfree(ctx);
	return ret;
}

static int
nve0_graph_create_context_mmio_list(struct nouveau_channel *chan)
{
	struct nve0_graph_priv *priv = nv_engine(chan->dev, NVOBJ_ENGINE_GR);
	struct nve0_graph_chan *grch = chan->engctx[NVOBJ_ENGINE_GR];
	struct drm_device *dev = chan->dev;
	u32 magic[GPC_MAX][2];
	u16 offset = 0x0000;
	int gpc;
	int ret;

	ret = nouveau_gpuobj_new(dev, chan, 0x3000, 256, NVOBJ_FLAG_VM,
				 &grch->unk408004);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(dev, chan, 0x8000, 256, NVOBJ_FLAG_VM,
				 &grch->unk40800c);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(dev, chan, 384 * 1024, 4096,
				 NVOBJ_FLAG_VM | NVOBJ_FLAG_VM_USER,
				 &grch->unk418810);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(dev, chan, 0x1000, 0, NVOBJ_FLAG_VM,
				 &grch->mmio);
	if (ret)
		return ret;

#define mmio(r,v) do {                                                         \
	nv_wo32(grch->mmio, (grch->mmio_nr * 8) + 0, (r));                     \
	nv_wo32(grch->mmio, (grch->mmio_nr * 8) + 4, (v));                     \
	grch->mmio_nr++;                                                       \
} while (0)
	mmio(0x40800c, grch->unk40800c->linst >> 8);
	mmio(0x408010, 0x80000000);
	mmio(0x419004, grch->unk40800c->linst >> 8);
	mmio(0x419008, 0x00000000);
	mmio(0x4064cc, 0x80000000);
	mmio(0x408004, grch->unk408004->linst >> 8);
	mmio(0x408008, 0x80000030);
	mmio(0x418808, grch->unk408004->linst >> 8);
	mmio(0x41880c, 0x80000030);
	mmio(0x4064c8, 0x01800600);
	mmio(0x418810, 0x80000000 | grch->unk418810->linst >> 12);
	mmio(0x419848, 0x10000000 | grch->unk418810->linst >> 12);
	mmio(0x405830, 0x02180648);
	mmio(0x4064c4, 0x0192ffff);

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		u16 magic0 = 0x0218 * priv->tpc_nr[gpc];
		u16 magic1 = 0x0648 * priv->tpc_nr[gpc];
		magic[gpc][0]  = 0x10000000 | (magic0 << 16) | offset;
		magic[gpc][1]  = 0x00000000 | (magic1 << 16);
		offset += 0x0324 * priv->tpc_nr[gpc];
	}

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		mmio(GPC_UNIT(gpc, 0x30c0), magic[gpc][0]);
		mmio(GPC_UNIT(gpc, 0x30e4), magic[gpc][1] | offset);
		offset += 0x07ff * priv->tpc_nr[gpc];
	}

	mmio(0x17e91c, 0x06060609);
	mmio(0x17e920, 0x00090a05);
#undef mmio
	return 0;
}

static int
nve0_graph_context_new(struct nouveau_channel *chan, int engine)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct nve0_graph_priv *priv = nv_engine(dev, engine);
	struct nve0_graph_chan *grch;
	struct nouveau_gpuobj *grctx;
	int ret, i;

	grch = kzalloc(sizeof(*grch), GFP_KERNEL);
	if (!grch)
		return -ENOMEM;
	chan->engctx[NVOBJ_ENGINE_GR] = grch;

	ret = nouveau_gpuobj_new(dev, chan, priv->grctx_size, 256,
				 NVOBJ_FLAG_VM | NVOBJ_FLAG_ZERO_ALLOC,
				 &grch->grctx);
	if (ret)
		goto error;
	grctx = grch->grctx;

	ret = nve0_graph_create_context_mmio_list(chan);
	if (ret)
		goto error;

	nv_wo32(chan->ramin, 0x0210, lower_32_bits(grctx->linst) | 4);
	nv_wo32(chan->ramin, 0x0214, upper_32_bits(grctx->linst));
	pinstmem->flush(dev);

	if (!priv->grctx_vals) {
		ret = nve0_graph_construct_context(chan);
		if (ret)
			goto error;
	}

	for (i = 0; i < priv->grctx_size; i += 4)
		nv_wo32(grctx, i, priv->grctx_vals[i / 4]);
	nv_wo32(grctx, 0xf4, 0);
	nv_wo32(grctx, 0xf8, 0);
	nv_wo32(grctx, 0x10, grch->mmio_nr);
	nv_wo32(grctx, 0x14, lower_32_bits(grch->mmio->linst));
	nv_wo32(grctx, 0x18, upper_32_bits(grch->mmio->linst));
	nv_wo32(grctx, 0x1c, 1);
	nv_wo32(grctx, 0x20, 0);
	nv_wo32(grctx, 0x28, 0);
	nv_wo32(grctx, 0x2c, 0);

	pinstmem->flush(dev);
	return 0;

error:
	priv->base.context_del(chan, engine);
	return ret;
}

static void
nve0_graph_context_del(struct nouveau_channel *chan, int engine)
{
	struct nve0_graph_chan *grch = chan->engctx[engine];

	nouveau_gpuobj_ref(NULL, &grch->mmio);
	nouveau_gpuobj_ref(NULL, &grch->unk418810);
	nouveau_gpuobj_ref(NULL, &grch->unk40800c);
	nouveau_gpuobj_ref(NULL, &grch->unk408004);
	nouveau_gpuobj_ref(NULL, &grch->grctx);
	chan->engctx[engine] = NULL;
}

static int
nve0_graph_object_new(struct nouveau_channel *chan, int engine,
		      u32 handle, u16 class)
{
	return 0;
}

static int
nve0_graph_fini(struct drm_device *dev, int engine, bool suspend)
{
	return 0;
}

static void
nve0_graph_init_obj418880(struct drm_device *dev)
{
	struct nve0_graph_priv *priv = nv_engine(dev, NVOBJ_ENGINE_GR);
	int i;

	nv_wr32(dev, GPC_BCAST(0x0880), 0x00000000);
	nv_wr32(dev, GPC_BCAST(0x08a4), 0x00000000);
	for (i = 0; i < 4; i++)
		nv_wr32(dev, GPC_BCAST(0x0888) + (i * 4), 0x00000000);
	nv_wr32(dev, GPC_BCAST(0x08b4), priv->unk4188b4->vinst >> 8);
	nv_wr32(dev, GPC_BCAST(0x08b8), priv->unk4188b8->vinst >> 8);
}

static void
nve0_graph_init_regs(struct drm_device *dev)
{
	nv_wr32(dev, 0x400080, 0x003083c2);
	nv_wr32(dev, 0x400088, 0x0001ffe7);
	nv_wr32(dev, 0x40008c, 0x00000000);
	nv_wr32(dev, 0x400090, 0x00000030);
	nv_wr32(dev, 0x40013c, 0x003901f7);
	nv_wr32(dev, 0x400140, 0x00000100);
	nv_wr32(dev, 0x400144, 0x00000000);
	nv_wr32(dev, 0x400148, 0x00000110);
	nv_wr32(dev, 0x400138, 0x00000000);
	nv_wr32(dev, 0x400130, 0x00000000);
	nv_wr32(dev, 0x400134, 0x00000000);
	nv_wr32(dev, 0x400124, 0x00000002);
}

static void
nve0_graph_init_units(struct drm_device *dev)
{
	nv_wr32(dev, 0x409ffc, 0x00000000);
	nv_wr32(dev, 0x409c14, 0x00003e3e);
	nv_wr32(dev, 0x409c24, 0x000f0000);

	nv_wr32(dev, 0x404000, 0xc0000000);
	nv_wr32(dev, 0x404600, 0xc0000000);
	nv_wr32(dev, 0x408030, 0xc0000000);
	nv_wr32(dev, 0x404490, 0xc0000000);
	nv_wr32(dev, 0x406018, 0xc0000000);
	nv_wr32(dev, 0x407020, 0xc0000000);
	nv_wr32(dev, 0x405840, 0xc0000000);
	nv_wr32(dev, 0x405844, 0x00ffffff);

	nv_mask(dev, 0x419cc0, 0x00000008, 0x00000008);
	nv_mask(dev, 0x419eb4, 0x00001000, 0x00001000);

}

static void
nve0_graph_init_gpc_0(struct drm_device *dev)
{
	struct nve0_graph_priv *priv = nv_engine(dev, NVOBJ_ENGINE_GR);
	const u32 magicgpc918 = DIV_ROUND_UP(0x00800000, priv->tpc_total);
	u32 data[TPC_MAX / 8];
	u8  tpcnr[GPC_MAX];
	int i, gpc, tpc;

	nv_wr32(dev, GPC_UNIT(0, 0x3018), 0x00000001);

	memset(data, 0x00, sizeof(data));
	memcpy(tpcnr, priv->tpc_nr, sizeof(priv->tpc_nr));
	for (i = 0, gpc = -1; i < priv->tpc_total; i++) {
		do {
			gpc = (gpc + 1) % priv->gpc_nr;
		} while (!tpcnr[gpc]);
		tpc = priv->tpc_nr[gpc] - tpcnr[gpc]--;

		data[i / 8] |= tpc << ((i % 8) * 4);
	}

	nv_wr32(dev, GPC_BCAST(0x0980), data[0]);
	nv_wr32(dev, GPC_BCAST(0x0984), data[1]);
	nv_wr32(dev, GPC_BCAST(0x0988), data[2]);
	nv_wr32(dev, GPC_BCAST(0x098c), data[3]);

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		nv_wr32(dev, GPC_UNIT(gpc, 0x0914), priv->magic_not_rop_nr << 8 |
						  priv->tpc_nr[gpc]);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0910), 0x00040000 | priv->tpc_total);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0918), magicgpc918);
	}

	nv_wr32(dev, GPC_BCAST(0x1bd4), magicgpc918);
	nv_wr32(dev, GPC_BCAST(0x08ac), nv_rd32(dev, 0x100800));
}

static void
nve0_graph_init_gpc_1(struct drm_device *dev)
{
	struct nve0_graph_priv *priv = nv_engine(dev, NVOBJ_ENGINE_GR);
	int gpc, tpc;

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		nv_wr32(dev, GPC_UNIT(gpc, 0x3038), 0xc0000000);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0420), 0xc0000000);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0900), 0xc0000000);
		nv_wr32(dev, GPC_UNIT(gpc, 0x1028), 0xc0000000);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0824), 0xc0000000);
		for (tpc = 0; tpc < priv->tpc_nr[gpc]; tpc++) {
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x508), 0xffffffff);
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x50c), 0xffffffff);
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x224), 0xc0000000);
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x48c), 0xc0000000);
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x084), 0xc0000000);
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x644), 0x001ffffe);
			nv_wr32(dev, TPC_UNIT(gpc, tpc, 0x64c), 0x0000000f);
		}
		nv_wr32(dev, GPC_UNIT(gpc, 0x2c90), 0xffffffff);
		nv_wr32(dev, GPC_UNIT(gpc, 0x2c94), 0xffffffff);
	}
}

static void
nve0_graph_init_rop(struct drm_device *dev)
{
	struct nve0_graph_priv *priv = nv_engine(dev, NVOBJ_ENGINE_GR);
	int rop;

	for (rop = 0; rop < priv->rop_nr; rop++) {
		nv_wr32(dev, ROP_UNIT(rop, 0x144), 0xc0000000);
		nv_wr32(dev, ROP_UNIT(rop, 0x070), 0xc0000000);
		nv_wr32(dev, ROP_UNIT(rop, 0x204), 0xffffffff);
		nv_wr32(dev, ROP_UNIT(rop, 0x208), 0xffffffff);
	}
}

static void
nve0_graph_init_fuc(struct drm_device *dev, u32 fuc_base,
		    struct nve0_graph_fuc *code, struct nve0_graph_fuc *data)
{
	int i;

	nv_wr32(dev, fuc_base + 0x01c0, 0x01000000);
	for (i = 0; i < data->size / 4; i++)
		nv_wr32(dev, fuc_base + 0x01c4, data->data[i]);

	nv_wr32(dev, fuc_base + 0x0180, 0x01000000);
	for (i = 0; i < code->size / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(dev, fuc_base + 0x0188, i >> 6);
		nv_wr32(dev, fuc_base + 0x0184, code->data[i]);
	}
}

static int
nve0_graph_init_ctxctl(struct drm_device *dev)
{
	struct nve0_graph_priv *priv = nv_engine(dev, NVOBJ_ENGINE_GR);
	u32 r000260;

	/* load fuc microcode */
	r000260 = nv_mask(dev, 0x000260, 0x00000001, 0x00000000);
	nve0_graph_init_fuc(dev, 0x409000, &priv->fuc409c, &priv->fuc409d);
	nve0_graph_init_fuc(dev, 0x41a000, &priv->fuc41ac, &priv->fuc41ad);
	nv_wr32(dev, 0x000260, r000260);

	/* start both of them running */
	nv_wr32(dev, 0x409840, 0xffffffff);
	nv_wr32(dev, 0x41a10c, 0x00000000);
	nv_wr32(dev, 0x40910c, 0x00000000);
	nv_wr32(dev, 0x41a100, 0x00000002);
	nv_wr32(dev, 0x409100, 0x00000002);
	if (!nv_wait(dev, 0x409800, 0x00000001, 0x00000001))
		NV_INFO(dev, "0x409800 wait failed\n");

	nv_wr32(dev, 0x409840, 0xffffffff);
	nv_wr32(dev, 0x409500, 0x7fffffff);
	nv_wr32(dev, 0x409504, 0x00000021);

	nv_wr32(dev, 0x409840, 0xffffffff);
	nv_wr32(dev, 0x409500, 0x00000000);
	nv_wr32(dev, 0x409504, 0x00000010);
	if (!nv_wait_ne(dev, 0x409800, 0xffffffff, 0x00000000)) {
		NV_ERROR(dev, "fuc09 req 0x10 timeout\n");
		return -EBUSY;
	}
	priv->grctx_size = nv_rd32(dev, 0x409800);

	nv_wr32(dev, 0x409840, 0xffffffff);
	nv_wr32(dev, 0x409500, 0x00000000);
	nv_wr32(dev, 0x409504, 0x00000016);
	if (!nv_wait_ne(dev, 0x409800, 0xffffffff, 0x00000000)) {
		NV_ERROR(dev, "fuc09 req 0x16 timeout\n");
		return -EBUSY;
	}

	nv_wr32(dev, 0x409840, 0xffffffff);
	nv_wr32(dev, 0x409500, 0x00000000);
	nv_wr32(dev, 0x409504, 0x00000025);
	if (!nv_wait_ne(dev, 0x409800, 0xffffffff, 0x00000000)) {
		NV_ERROR(dev, "fuc09 req 0x25 timeout\n");
		return -EBUSY;
	}

	nv_wr32(dev, 0x409800, 0x00000000);
	nv_wr32(dev, 0x409500, 0x00000001);
	nv_wr32(dev, 0x409504, 0x00000030);
	if (!nv_wait_ne(dev, 0x409800, 0xffffffff, 0x00000000)) {
		NV_ERROR(dev, "fuc09 req 0x30 timeout\n");
		return -EBUSY;
	}

	nv_wr32(dev, 0x409810, 0xb00095c8);
	nv_wr32(dev, 0x409800, 0x00000000);
	nv_wr32(dev, 0x409500, 0x00000001);
	nv_wr32(dev, 0x409504, 0x00000031);
	if (!nv_wait_ne(dev, 0x409800, 0xffffffff, 0x00000000)) {
		NV_ERROR(dev, "fuc09 req 0x31 timeout\n");
		return -EBUSY;
	}

	nv_wr32(dev, 0x409810, 0x00080420);
	nv_wr32(dev, 0x409800, 0x00000000);
	nv_wr32(dev, 0x409500, 0x00000001);
	nv_wr32(dev, 0x409504, 0x00000032);
	if (!nv_wait_ne(dev, 0x409800, 0xffffffff, 0x00000000)) {
		NV_ERROR(dev, "fuc09 req 0x32 timeout\n");
		return -EBUSY;
	}

	nv_wr32(dev, 0x409614, 0x00000070);
	nv_wr32(dev, 0x409614, 0x00000770);
	nv_wr32(dev, 0x40802c, 0x00000001);
	return 0;
}

static int
nve0_graph_init(struct drm_device *dev, int engine)
{
	int ret;

	nv_mask(dev, 0x000200, 0x18001000, 0x00000000);
	nv_mask(dev, 0x000200, 0x18001000, 0x18001000);

	nve0_graph_init_obj418880(dev);
	nve0_graph_init_regs(dev);
	nve0_graph_init_gpc_0(dev);

	nv_wr32(dev, 0x400500, 0x00010001);
	nv_wr32(dev, 0x400100, 0xffffffff);
	nv_wr32(dev, 0x40013c, 0xffffffff);

	nve0_graph_init_units(dev);
	nve0_graph_init_gpc_1(dev);
	nve0_graph_init_rop(dev);

	nv_wr32(dev, 0x400108, 0xffffffff);
	nv_wr32(dev, 0x400138, 0xffffffff);
	nv_wr32(dev, 0x400118, 0xffffffff);
	nv_wr32(dev, 0x400130, 0xffffffff);
	nv_wr32(dev, 0x40011c, 0xffffffff);
	nv_wr32(dev, 0x400134, 0xffffffff);
	nv_wr32(dev, 0x400054, 0x34ce3464);

	ret = nve0_graph_init_ctxctl(dev);
	if (ret)
		return ret;

	return 0;
}

int
nve0_graph_isr_chid(struct drm_device *dev, u64 inst)
{
	struct nouveau_fifo_priv *pfifo = nv_engine(dev, NVOBJ_ENGINE_FIFO);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&dev_priv->channels.lock, flags);
	for (i = 0; i < pfifo->channels; i++) {
		chan = dev_priv->channels.ptr[i];
		if (!chan || !chan->ramin)
			continue;

		if (inst == chan->ramin->vinst)
			break;
	}
	spin_unlock_irqrestore(&dev_priv->channels.lock, flags);
	return i;
}

static void
nve0_graph_ctxctl_isr(struct drm_device *dev)
{
	u32 ustat = nv_rd32(dev, 0x409c18);

	if (ustat & 0x00000001)
		NV_INFO(dev, "PGRAPH: CTXCTRL ucode error\n");
	if (ustat & 0x00080000)
		NV_INFO(dev, "PGRAPH: CTXCTRL watchdog timeout\n");
	if (ustat & ~0x00080001)
		NV_INFO(dev, "PGRAPH: CTXCTRL 0x%08x\n", ustat);

	nve0_graph_ctxctl_debug(dev);
	nv_wr32(dev, 0x409c20, ustat);
}

static void
nve0_graph_trap_isr(struct drm_device *dev, int chid)
{
	struct nve0_graph_priv *priv = nv_engine(dev, NVOBJ_ENGINE_GR);
	u32 trap = nv_rd32(dev, 0x400108);
	int rop;

	if (trap & 0x00000001) {
		u32 stat = nv_rd32(dev, 0x404000);
		NV_INFO(dev, "PGRAPH: DISPATCH ch %d 0x%08x\n", chid, stat);
		nv_wr32(dev, 0x404000, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x00000001);
		trap &= ~0x00000001;
	}

	if (trap & 0x00000010) {
		u32 stat = nv_rd32(dev, 0x405840);
		NV_INFO(dev, "PGRAPH: SHADER ch %d 0x%08x\n", chid, stat);
		nv_wr32(dev, 0x405840, 0xc0000000);
		nv_wr32(dev, 0x400108, 0x00000010);
		trap &= ~0x00000010;
	}

	if (trap & 0x02000000) {
		for (rop = 0; rop < priv->rop_nr; rop++) {
			u32 statz = nv_rd32(dev, ROP_UNIT(rop, 0x070));
			u32 statc = nv_rd32(dev, ROP_UNIT(rop, 0x144));
			NV_INFO(dev, "PGRAPH: ROP%d ch %d 0x%08x 0x%08x\n",
				     rop, chid, statz, statc);
			nv_wr32(dev, ROP_UNIT(rop, 0x070), 0xc0000000);
			nv_wr32(dev, ROP_UNIT(rop, 0x144), 0xc0000000);
		}
		nv_wr32(dev, 0x400108, 0x02000000);
		trap &= ~0x02000000;
	}

	if (trap) {
		NV_INFO(dev, "PGRAPH: TRAP ch %d 0x%08x\n", chid, trap);
		nv_wr32(dev, 0x400108, trap);
	}
}

static void
nve0_graph_isr(struct drm_device *dev)
{
	u64 inst = (u64)(nv_rd32(dev, 0x409b00) & 0x0fffffff) << 12;
	u32 chid = nve0_graph_isr_chid(dev, inst);
	u32 stat = nv_rd32(dev, 0x400100);
	u32 addr = nv_rd32(dev, 0x400704);
	u32 mthd = (addr & 0x00003ffc);
	u32 subc = (addr & 0x00070000) >> 16;
	u32 data = nv_rd32(dev, 0x400708);
	u32 code = nv_rd32(dev, 0x400110);
	u32 class = nv_rd32(dev, 0x404200 + (subc * 4));

	if (stat & 0x00000010) {
		if (nouveau_gpuobj_mthd_call2(dev, chid, class, mthd, data)) {
			NV_INFO(dev, "PGRAPH: ILLEGAL_MTHD ch %d [0x%010llx] "
				     "subc %d class 0x%04x mthd 0x%04x "
				     "data 0x%08x\n",
				chid, inst, subc, class, mthd, data);
		}
		nv_wr32(dev, 0x400100, 0x00000010);
		stat &= ~0x00000010;
	}

	if (stat & 0x00000020) {
		NV_INFO(dev, "PGRAPH: ILLEGAL_CLASS ch %d [0x%010llx] subc %d "
			     "class 0x%04x mthd 0x%04x data 0x%08x\n",
			chid, inst, subc, class, mthd, data);
		nv_wr32(dev, 0x400100, 0x00000020);
		stat &= ~0x00000020;
	}

	if (stat & 0x00100000) {
		NV_INFO(dev, "PGRAPH: DATA_ERROR [");
		nouveau_enum_print(nv50_data_error_names, code);
		printk("] ch %d [0x%010llx] subc %d class 0x%04x "
		       "mthd 0x%04x data 0x%08x\n",
		       chid, inst, subc, class, mthd, data);
		nv_wr32(dev, 0x400100, 0x00100000);
		stat &= ~0x00100000;
	}

	if (stat & 0x00200000) {
		nve0_graph_trap_isr(dev, chid);
		nv_wr32(dev, 0x400100, 0x00200000);
		stat &= ~0x00200000;
	}

	if (stat & 0x00080000) {
		nve0_graph_ctxctl_isr(dev);
		nv_wr32(dev, 0x400100, 0x00080000);
		stat &= ~0x00080000;
	}

	if (stat) {
		NV_INFO(dev, "PGRAPH: unknown stat 0x%08x\n", stat);
		nv_wr32(dev, 0x400100, stat);
	}

	nv_wr32(dev, 0x400500, 0x00010001);
}

static int
nve0_graph_create_fw(struct drm_device *dev, const char *fwname,
		     struct nve0_graph_fuc *fuc)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	const struct firmware *fw;
	char f[32];
	int ret;

	snprintf(f, sizeof(f), "nouveau/nv%02x_%s", dev_priv->chipset, fwname);
	ret = request_firmware(&fw, f, &dev->pdev->dev);
	if (ret)
		return ret;

	fuc->size = fw->size;
	fuc->data = kmemdup(fw->data, fuc->size, GFP_KERNEL);
	release_firmware(fw);
	return (fuc->data != NULL) ? 0 : -ENOMEM;
}

static void
nve0_graph_destroy_fw(struct nve0_graph_fuc *fuc)
{
	if (fuc->data) {
		kfree(fuc->data);
		fuc->data = NULL;
	}
}

static void
nve0_graph_destroy(struct drm_device *dev, int engine)
{
	struct nve0_graph_priv *priv = nv_engine(dev, engine);

	nve0_graph_destroy_fw(&priv->fuc409c);
	nve0_graph_destroy_fw(&priv->fuc409d);
	nve0_graph_destroy_fw(&priv->fuc41ac);
	nve0_graph_destroy_fw(&priv->fuc41ad);

	nouveau_irq_unregister(dev, 12);

	nouveau_gpuobj_ref(NULL, &priv->unk4188b8);
	nouveau_gpuobj_ref(NULL, &priv->unk4188b4);

	if (priv->grctx_vals)
		kfree(priv->grctx_vals);

	NVOBJ_ENGINE_DEL(dev, GR);
	kfree(priv);
}

int
nve0_graph_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nve0_graph_priv *priv;
	int ret, gpc, i;
	u32 kepler;

	kepler = nve0_graph_class(dev);
	if (!kepler) {
		NV_ERROR(dev, "PGRAPH: unsupported chipset, please report!\n");
		return 0;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.destroy = nve0_graph_destroy;
	priv->base.init = nve0_graph_init;
	priv->base.fini = nve0_graph_fini;
	priv->base.context_new = nve0_graph_context_new;
	priv->base.context_del = nve0_graph_context_del;
	priv->base.object_new = nve0_graph_object_new;

	NVOBJ_ENGINE_ADD(dev, GR, &priv->base);
	nouveau_irq_register(dev, 12, nve0_graph_isr);

	NV_INFO(dev, "PGRAPH: using external firmware\n");
	if (nve0_graph_create_fw(dev, "fuc409c", &priv->fuc409c) ||
	    nve0_graph_create_fw(dev, "fuc409d", &priv->fuc409d) ||
	    nve0_graph_create_fw(dev, "fuc41ac", &priv->fuc41ac) ||
	    nve0_graph_create_fw(dev, "fuc41ad", &priv->fuc41ad)) {
		ret = 0;
		goto error;
	}

	ret = nouveau_gpuobj_new(dev, NULL, 0x1000, 256, 0, &priv->unk4188b4);
	if (ret)
		goto error;

	ret = nouveau_gpuobj_new(dev, NULL, 0x1000, 256, 0, &priv->unk4188b8);
	if (ret)
		goto error;

	for (i = 0; i < 0x1000; i += 4) {
		nv_wo32(priv->unk4188b4, i, 0x00000010);
		nv_wo32(priv->unk4188b8, i, 0x00000010);
	}

	priv->gpc_nr  =  nv_rd32(dev, 0x409604) & 0x0000001f;
	priv->rop_nr = (nv_rd32(dev, 0x409604) & 0x001f0000) >> 16;
	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		priv->tpc_nr[gpc] = nv_rd32(dev, GPC_UNIT(gpc, 0x2608));
		priv->tpc_total += priv->tpc_nr[gpc];
	}

	switch (dev_priv->chipset) {
	case 0xe4:
		if (priv->tpc_total == 8)
			priv->magic_not_rop_nr = 3;
		else
		if (priv->tpc_total == 7)
			priv->magic_not_rop_nr = 1;
		break;
	case 0xe7:
		priv->magic_not_rop_nr = 1;
		break;
	default:
		break;
	}

	if (!priv->magic_not_rop_nr) {
		NV_ERROR(dev, "PGRAPH: unknown config: %d/%d/%d/%d, %d\n",
			 priv->tpc_nr[0], priv->tpc_nr[1], priv->tpc_nr[2],
			 priv->tpc_nr[3], priv->rop_nr);
		priv->magic_not_rop_nr = 0x00;
	}

	NVOBJ_CLASS(dev, 0xa097, GR); /* subc 0: 3D */
	NVOBJ_CLASS(dev, 0xa0c0, GR); /* subc 1: COMPUTE */
	NVOBJ_CLASS(dev, 0xa040, GR); /* subc 2: P2MF */
	NVOBJ_CLASS(dev, 0x902d, GR); /* subc 3: 2D */
	//NVOBJ_CLASS(dev, 0xa0b5, GR); /* subc 4: COPY */
	return 0;

error:
	nve0_graph_destroy(dev, NVOBJ_ENGINE_GR);
	return ret;
}
