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

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_mm.h"

#include "nvc0_graph.h"
#include "nvc0_grhub.fuc.h"
#include "nvc0_grgpc.fuc.h"

static void
nvc0_graph_ctxctl_debug_unit(struct drm_device *dev, u32 base)
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
nvc0_graph_ctxctl_debug(struct drm_device *dev)
{
	u32 gpcnr = nv_rd32(dev, 0x409604) & 0xffff;
	u32 gpc;

	nvc0_graph_ctxctl_debug_unit(dev, 0x409000);
	for (gpc = 0; gpc < gpcnr; gpc++)
		nvc0_graph_ctxctl_debug_unit(dev, 0x502000 + (gpc * 0x8000));
}

static int
nvc0_graph_load_context(struct nouveau_channel *chan)
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
nvc0_graph_unload_context_to(struct drm_device *dev, u64 chan)
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
nvc0_graph_construct_context(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct nvc0_graph_priv *priv = nv_engine(chan->dev, NVOBJ_ENGINE_GR);
	struct nvc0_graph_chan *grch = chan->engctx[NVOBJ_ENGINE_GR];
	struct drm_device *dev = chan->dev;
	int ret, i;
	u32 *ctx;

	ctx = kmalloc(priv->grctx_size, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (!nouveau_ctxfw) {
		nv_wr32(dev, 0x409840, 0x80000000);
		nv_wr32(dev, 0x409500, 0x80000000 | chan->ramin->vinst >> 12);
		nv_wr32(dev, 0x409504, 0x00000001);
		if (!nv_wait(dev, 0x409800, 0x80000000, 0x80000000)) {
			NV_ERROR(dev, "PGRAPH: HUB_SET_CHAN timeout\n");
			nvc0_graph_ctxctl_debug(dev);
			ret = -EBUSY;
			goto err;
		}
	} else {
		nvc0_graph_load_context(chan);

		nv_wo32(grch->grctx, 0x1c, 1);
		nv_wo32(grch->grctx, 0x20, 0);
		nv_wo32(grch->grctx, 0x28, 0);
		nv_wo32(grch->grctx, 0x2c, 0);
		dev_priv->engine.instmem.flush(dev);
	}

	ret = nvc0_grctx_generate(chan);
	if (ret)
		goto err;

	if (!nouveau_ctxfw) {
		nv_wr32(dev, 0x409840, 0x80000000);
		nv_wr32(dev, 0x409500, 0x80000000 | chan->ramin->vinst >> 12);
		nv_wr32(dev, 0x409504, 0x00000002);
		if (!nv_wait(dev, 0x409800, 0x80000000, 0x80000000)) {
			NV_ERROR(dev, "PGRAPH: HUB_CTX_SAVE timeout\n");
			nvc0_graph_ctxctl_debug(dev);
			ret = -EBUSY;
			goto err;
		}
	} else {
		ret = nvc0_graph_unload_context_to(dev, chan->ramin->vinst);
		if (ret)
			goto err;
	}

	for (i = 0; i < priv->grctx_size; i += 4)
		ctx[i / 4] = nv_ro32(grch->grctx, i);

	priv->grctx_vals = ctx;
	return 0;

err:
	kfree(ctx);
	return ret;
}

static int
nvc0_graph_create_context_mmio_list(struct nouveau_channel *chan)
{
	struct nvc0_graph_priv *priv = nv_engine(chan->dev, NVOBJ_ENGINE_GR);
	struct nvc0_graph_chan *grch = chan->engctx[NVOBJ_ENGINE_GR];
	struct drm_device *dev = chan->dev;
	int i = 0, gpc, tp, ret;
	u32 magic;

	ret = nouveau_gpuobj_new(dev, chan, 0x2000, 256, NVOBJ_FLAG_VM,
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


	nv_wo32(grch->mmio, i++ * 4, 0x00408004);
	nv_wo32(grch->mmio, i++ * 4, grch->unk408004->linst >> 8);
	nv_wo32(grch->mmio, i++ * 4, 0x00408008);
	nv_wo32(grch->mmio, i++ * 4, 0x80000018);

	nv_wo32(grch->mmio, i++ * 4, 0x0040800c);
	nv_wo32(grch->mmio, i++ * 4, grch->unk40800c->linst >> 8);
	nv_wo32(grch->mmio, i++ * 4, 0x00408010);
	nv_wo32(grch->mmio, i++ * 4, 0x80000000);

	nv_wo32(grch->mmio, i++ * 4, 0x00418810);
	nv_wo32(grch->mmio, i++ * 4, 0x80000000 | grch->unk418810->linst >> 12);
	nv_wo32(grch->mmio, i++ * 4, 0x00419848);
	nv_wo32(grch->mmio, i++ * 4, 0x10000000 | grch->unk418810->linst >> 12);

	nv_wo32(grch->mmio, i++ * 4, 0x00419004);
	nv_wo32(grch->mmio, i++ * 4, grch->unk40800c->linst >> 8);
	nv_wo32(grch->mmio, i++ * 4, 0x00419008);
	nv_wo32(grch->mmio, i++ * 4, 0x00000000);

	nv_wo32(grch->mmio, i++ * 4, 0x00418808);
	nv_wo32(grch->mmio, i++ * 4, grch->unk408004->linst >> 8);
	nv_wo32(grch->mmio, i++ * 4, 0x0041880c);
	nv_wo32(grch->mmio, i++ * 4, 0x80000018);

	magic = 0x02180000;
	nv_wo32(grch->mmio, i++ * 4, 0x00405830);
	nv_wo32(grch->mmio, i++ * 4, magic);
	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		for (tp = 0; tp < priv->tp_nr[gpc]; tp++, magic += 0x0324) {
			u32 reg = 0x504520 + (gpc * 0x8000) + (tp * 0x0800);
			nv_wo32(grch->mmio, i++ * 4, reg);
			nv_wo32(grch->mmio, i++ * 4, magic);
		}
	}

	grch->mmio_nr = i / 2;
	return 0;
}

static int
nvc0_graph_context_new(struct nouveau_channel *chan, int engine)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct nvc0_graph_priv *priv = nv_engine(dev, engine);
	struct nvc0_graph_chan *grch;
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

	ret = nvc0_graph_create_context_mmio_list(chan);
	if (ret)
		goto error;

	nv_wo32(chan->ramin, 0x0210, lower_32_bits(grctx->linst) | 4);
	nv_wo32(chan->ramin, 0x0214, upper_32_bits(grctx->linst));
	pinstmem->flush(dev);

	if (!priv->grctx_vals) {
		ret = nvc0_graph_construct_context(chan);
		if (ret)
			goto error;
	}

	for (i = 0; i < priv->grctx_size; i += 4)
		nv_wo32(grctx, i, priv->grctx_vals[i / 4]);

	if (!nouveau_ctxfw) {
		nv_wo32(grctx, 0x00, grch->mmio_nr);
		nv_wo32(grctx, 0x04, grch->mmio->linst >> 8);
	} else {
		nv_wo32(grctx, 0xf4, 0);
		nv_wo32(grctx, 0xf8, 0);
		nv_wo32(grctx, 0x10, grch->mmio_nr);
		nv_wo32(grctx, 0x14, lower_32_bits(grch->mmio->linst));
		nv_wo32(grctx, 0x18, upper_32_bits(grch->mmio->linst));
		nv_wo32(grctx, 0x1c, 1);
		nv_wo32(grctx, 0x20, 0);
		nv_wo32(grctx, 0x28, 0);
		nv_wo32(grctx, 0x2c, 0);
	}
	pinstmem->flush(dev);
	return 0;

error:
	priv->base.context_del(chan, engine);
	return ret;
}

static void
nvc0_graph_context_del(struct nouveau_channel *chan, int engine)
{
	struct nvc0_graph_chan *grch = chan->engctx[engine];

	nouveau_gpuobj_ref(NULL, &grch->mmio);
	nouveau_gpuobj_ref(NULL, &grch->unk418810);
	nouveau_gpuobj_ref(NULL, &grch->unk40800c);
	nouveau_gpuobj_ref(NULL, &grch->unk408004);
	nouveau_gpuobj_ref(NULL, &grch->grctx);
	chan->engctx[engine] = NULL;
}

static int
nvc0_graph_object_new(struct nouveau_channel *chan, int engine,
		      u32 handle, u16 class)
{
	return 0;
}

static int
nvc0_graph_fini(struct drm_device *dev, int engine, bool suspend)
{
	return 0;
}

static int
nvc0_graph_mthd_page_flip(struct nouveau_channel *chan,
			  u32 class, u32 mthd, u32 data)
{
	nouveau_finish_page_flip(chan, NULL);
	return 0;
}

static void
nvc0_graph_init_obj418880(struct drm_device *dev)
{
	struct nvc0_graph_priv *priv = nv_engine(dev, NVOBJ_ENGINE_GR);
	int i;

	nv_wr32(dev, GPC_BCAST(0x0880), 0x00000000);
	nv_wr32(dev, GPC_BCAST(0x08a4), 0x00000000);
	for (i = 0; i < 4; i++)
		nv_wr32(dev, GPC_BCAST(0x0888) + (i * 4), 0x00000000);
	nv_wr32(dev, GPC_BCAST(0x08b4), priv->unk4188b4->vinst >> 8);
	nv_wr32(dev, GPC_BCAST(0x08b8), priv->unk4188b8->vinst >> 8);
}

static void
nvc0_graph_init_regs(struct drm_device *dev)
{
	nv_wr32(dev, 0x400080, 0x003083c2);
	nv_wr32(dev, 0x400088, 0x00006fe7);
	nv_wr32(dev, 0x40008c, 0x00000000);
	nv_wr32(dev, 0x400090, 0x00000030);
	nv_wr32(dev, 0x40013c, 0x013901f7);
	nv_wr32(dev, 0x400140, 0x00000100);
	nv_wr32(dev, 0x400144, 0x00000000);
	nv_wr32(dev, 0x400148, 0x00000110);
	nv_wr32(dev, 0x400138, 0x00000000);
	nv_wr32(dev, 0x400130, 0x00000000);
	nv_wr32(dev, 0x400134, 0x00000000);
	nv_wr32(dev, 0x400124, 0x00000002);
}

static void
nvc0_graph_init_gpc_0(struct drm_device *dev)
{
	struct nvc0_graph_priv *priv = nv_engine(dev, NVOBJ_ENGINE_GR);
	const u32 magicgpc918 = DIV_ROUND_UP(0x00800000, priv->tp_total);
	u32 data[TP_MAX / 8];
	u8  tpnr[GPC_MAX];
	int i, gpc, tpc;

	/*
	 *      TP      ROP UNKVAL(magic_not_rop_nr)
	 * 450: 4/0/0/0 2        3
	 * 460: 3/4/0/0 4        1
	 * 465: 3/4/4/0 4        7
	 * 470: 3/3/4/4 5        5
	 * 480: 3/4/4/4 6        6
	 */

	memset(data, 0x00, sizeof(data));
	memcpy(tpnr, priv->tp_nr, sizeof(priv->tp_nr));
	for (i = 0, gpc = -1; i < priv->tp_total; i++) {
		do {
			gpc = (gpc + 1) % priv->gpc_nr;
		} while (!tpnr[gpc]);
		tpc = priv->tp_nr[gpc] - tpnr[gpc]--;

		data[i / 8] |= tpc << ((i % 8) * 4);
	}

	nv_wr32(dev, GPC_BCAST(0x0980), data[0]);
	nv_wr32(dev, GPC_BCAST(0x0984), data[1]);
	nv_wr32(dev, GPC_BCAST(0x0988), data[2]);
	nv_wr32(dev, GPC_BCAST(0x098c), data[3]);

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		nv_wr32(dev, GPC_UNIT(gpc, 0x0914), priv->magic_not_rop_nr << 8 |
						  priv->tp_nr[gpc]);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0910), 0x00040000 | priv->tp_total);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0918), magicgpc918);
	}

	nv_wr32(dev, GPC_BCAST(0x1bd4), magicgpc918);
	nv_wr32(dev, GPC_BCAST(0x08ac), priv->rop_nr);
}

static void
nvc0_graph_init_units(struct drm_device *dev)
{
	nv_wr32(dev, 0x409c24, 0x000f0000);
	nv_wr32(dev, 0x404000, 0xc0000000); /* DISPATCH */
	nv_wr32(dev, 0x404600, 0xc0000000); /* M2MF */
	nv_wr32(dev, 0x408030, 0xc0000000);
	nv_wr32(dev, 0x40601c, 0xc0000000);
	nv_wr32(dev, 0x404490, 0xc0000000); /* MACRO */
	nv_wr32(dev, 0x406018, 0xc0000000);
	nv_wr32(dev, 0x405840, 0xc0000000);
	nv_wr32(dev, 0x405844, 0x00ffffff);
	nv_mask(dev, 0x419cc0, 0x00000008, 0x00000008);
	nv_mask(dev, 0x419eb4, 0x00001000, 0x00001000);
}

static void
nvc0_graph_init_gpc_1(struct drm_device *dev)
{
	struct nvc0_graph_priv *priv = nv_engine(dev, NVOBJ_ENGINE_GR);
	int gpc, tp;

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		nv_wr32(dev, GPC_UNIT(gpc, 0x0420), 0xc0000000);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0900), 0xc0000000);
		nv_wr32(dev, GPC_UNIT(gpc, 0x1028), 0xc0000000);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0824), 0xc0000000);
		for (tp = 0; tp < priv->tp_nr[gpc]; tp++) {
			nv_wr32(dev, TP_UNIT(gpc, tp, 0x508), 0xffffffff);
			nv_wr32(dev, TP_UNIT(gpc, tp, 0x50c), 0xffffffff);
			nv_wr32(dev, TP_UNIT(gpc, tp, 0x224), 0xc0000000);
			nv_wr32(dev, TP_UNIT(gpc, tp, 0x48c), 0xc0000000);
			nv_wr32(dev, TP_UNIT(gpc, tp, 0x084), 0xc0000000);
			nv_wr32(dev, TP_UNIT(gpc, tp, 0x644), 0x001ffffe);
			nv_wr32(dev, TP_UNIT(gpc, tp, 0x64c), 0x0000000f);
		}
		nv_wr32(dev, GPC_UNIT(gpc, 0x2c90), 0xffffffff);
		nv_wr32(dev, GPC_UNIT(gpc, 0x2c94), 0xffffffff);
	}
}

static void
nvc0_graph_init_rop(struct drm_device *dev)
{
	struct nvc0_graph_priv *priv = nv_engine(dev, NVOBJ_ENGINE_GR);
	int rop;

	for (rop = 0; rop < priv->rop_nr; rop++) {
		nv_wr32(dev, ROP_UNIT(rop, 0x144), 0xc0000000);
		nv_wr32(dev, ROP_UNIT(rop, 0x070), 0xc0000000);
		nv_wr32(dev, ROP_UNIT(rop, 0x204), 0xffffffff);
		nv_wr32(dev, ROP_UNIT(rop, 0x208), 0xffffffff);
	}
}

static void
nvc0_graph_init_fuc(struct drm_device *dev, u32 fuc_base,
		    struct nvc0_graph_fuc *code, struct nvc0_graph_fuc *data)
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
nvc0_graph_init_ctxctl(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_graph_priv *priv = nv_engine(dev, NVOBJ_ENGINE_GR);
	u32 r000260;
	int i;

	if (!nouveau_ctxfw) {
		/* load HUB microcode */
		r000260 = nv_mask(dev, 0x000260, 0x00000001, 0x00000000);
		nv_wr32(dev, 0x4091c0, 0x01000000);
		for (i = 0; i < sizeof(nvc0_grhub_data) / 4; i++)
			nv_wr32(dev, 0x4091c4, nvc0_grhub_data[i]);

		nv_wr32(dev, 0x409180, 0x01000000);
		for (i = 0; i < sizeof(nvc0_grhub_code) / 4; i++) {
			if ((i & 0x3f) == 0)
				nv_wr32(dev, 0x409188, i >> 6);
			nv_wr32(dev, 0x409184, nvc0_grhub_code[i]);
		}

		/* load GPC microcode */
		nv_wr32(dev, 0x41a1c0, 0x01000000);
		for (i = 0; i < sizeof(nvc0_grgpc_data) / 4; i++)
			nv_wr32(dev, 0x41a1c4, nvc0_grgpc_data[i]);

		nv_wr32(dev, 0x41a180, 0x01000000);
		for (i = 0; i < sizeof(nvc0_grgpc_code) / 4; i++) {
			if ((i & 0x3f) == 0)
				nv_wr32(dev, 0x41a188, i >> 6);
			nv_wr32(dev, 0x41a184, nvc0_grgpc_code[i]);
		}
		nv_wr32(dev, 0x000260, r000260);

		/* start HUB ucode running, it'll init the GPCs */
		nv_wr32(dev, 0x409800, dev_priv->chipset);
		nv_wr32(dev, 0x40910c, 0x00000000);
		nv_wr32(dev, 0x409100, 0x00000002);
		if (!nv_wait(dev, 0x409800, 0x80000000, 0x80000000)) {
			NV_ERROR(dev, "PGRAPH: HUB_INIT timed out\n");
			nvc0_graph_ctxctl_debug(dev);
			return -EBUSY;
		}

		priv->grctx_size = nv_rd32(dev, 0x409804);
		return 0;
	}

	/* load fuc microcode */
	r000260 = nv_mask(dev, 0x000260, 0x00000001, 0x00000000);
	nvc0_graph_init_fuc(dev, 0x409000, &priv->fuc409c, &priv->fuc409d);
	nvc0_graph_init_fuc(dev, 0x41a000, &priv->fuc41ac, &priv->fuc41ad);
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

	return 0;
}

static int
nvc0_graph_init(struct drm_device *dev, int engine)
{
	int ret;

	nv_mask(dev, 0x000200, 0x18001000, 0x00000000);
	nv_mask(dev, 0x000200, 0x18001000, 0x18001000);

	nvc0_graph_init_obj418880(dev);
	nvc0_graph_init_regs(dev);
	/*nvc0_graph_init_unitplemented_magics(dev);*/
	nvc0_graph_init_gpc_0(dev);
	/*nvc0_graph_init_unitplemented_c242(dev);*/

	nv_wr32(dev, 0x400500, 0x00010001);
	nv_wr32(dev, 0x400100, 0xffffffff);
	nv_wr32(dev, 0x40013c, 0xffffffff);

	nvc0_graph_init_units(dev);
	nvc0_graph_init_gpc_1(dev);
	nvc0_graph_init_rop(dev);

	nv_wr32(dev, 0x400108, 0xffffffff);
	nv_wr32(dev, 0x400138, 0xffffffff);
	nv_wr32(dev, 0x400118, 0xffffffff);
	nv_wr32(dev, 0x400130, 0xffffffff);
	nv_wr32(dev, 0x40011c, 0xffffffff);
	nv_wr32(dev, 0x400134, 0xffffffff);
	nv_wr32(dev, 0x400054, 0x34ce3464);

	ret = nvc0_graph_init_ctxctl(dev);
	if (ret)
		return ret;

	return 0;
}

int
nvc0_graph_isr_chid(struct drm_device *dev, u64 inst)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_channel *chan;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&dev_priv->channels.lock, flags);
	for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
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
nvc0_graph_ctxctl_isr(struct drm_device *dev)
{
	u32 ustat = nv_rd32(dev, 0x409c18);

	if (ustat & 0x00000001)
		NV_INFO(dev, "PGRAPH: CTXCTRL ucode error\n");
	if (ustat & 0x00080000)
		NV_INFO(dev, "PGRAPH: CTXCTRL watchdog timeout\n");
	if (ustat & ~0x00080001)
		NV_INFO(dev, "PGRAPH: CTXCTRL 0x%08x\n", ustat);

	nvc0_graph_ctxctl_debug(dev);
	nv_wr32(dev, 0x409c20, ustat);
}

static void
nvc0_graph_isr(struct drm_device *dev)
{
	u64 inst = (u64)(nv_rd32(dev, 0x409b00) & 0x0fffffff) << 12;
	u32 chid = nvc0_graph_isr_chid(dev, inst);
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
		u32 trap = nv_rd32(dev, 0x400108);
		NV_INFO(dev, "PGRAPH: TRAP ch %d status 0x%08x\n", chid, trap);
		nv_wr32(dev, 0x400108, trap);
		nv_wr32(dev, 0x400100, 0x00200000);
		stat &= ~0x00200000;
	}

	if (stat & 0x00080000) {
		nvc0_graph_ctxctl_isr(dev);
		nv_wr32(dev, 0x400100, 0x00080000);
		stat &= ~0x00080000;
	}

	if (stat) {
		NV_INFO(dev, "PGRAPH: unknown stat 0x%08x\n", stat);
		nv_wr32(dev, 0x400100, stat);
	}

	nv_wr32(dev, 0x400500, 0x00010001);
}

static void
nvc0_runk140_isr(struct drm_device *dev)
{
	u32 units = nv_rd32(dev, 0x00017c) & 0x1f;

	while (units) {
		u32 unit = ffs(units) - 1;
		u32 reg = 0x140000 + unit * 0x2000;
		u32 st0 = nv_mask(dev, reg + 0x1020, 0, 0);
		u32 st1 = nv_mask(dev, reg + 0x1420, 0, 0);

		NV_DEBUG(dev, "PRUNK140: %d 0x%08x 0x%08x\n", unit, st0, st1);
		units &= ~(1 << unit);
	}
}

static int
nvc0_graph_create_fw(struct drm_device *dev, const char *fwname,
		     struct nvc0_graph_fuc *fuc)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	const struct firmware *fw;
	char f[32];
	int ret;

	snprintf(f, sizeof(f), "nouveau/nv%02x_%s", dev_priv->chipset, fwname);
	ret = request_firmware(&fw, f, &dev->pdev->dev);
	if (ret) {
		snprintf(f, sizeof(f), "nouveau/%s", fwname);
		ret = request_firmware(&fw, f, &dev->pdev->dev);
		if (ret) {
			NV_ERROR(dev, "failed to load %s\n", fwname);
			return ret;
		}
	}

	fuc->size = fw->size;
	fuc->data = kmemdup(fw->data, fuc->size, GFP_KERNEL);
	release_firmware(fw);
	return (fuc->data != NULL) ? 0 : -ENOMEM;
}

static void
nvc0_graph_destroy_fw(struct nvc0_graph_fuc *fuc)
{
	if (fuc->data) {
		kfree(fuc->data);
		fuc->data = NULL;
	}
}

static void
nvc0_graph_destroy(struct drm_device *dev, int engine)
{
	struct nvc0_graph_priv *priv = nv_engine(dev, engine);

	if (nouveau_ctxfw) {
		nvc0_graph_destroy_fw(&priv->fuc409c);
		nvc0_graph_destroy_fw(&priv->fuc409d);
		nvc0_graph_destroy_fw(&priv->fuc41ac);
		nvc0_graph_destroy_fw(&priv->fuc41ad);
	}

	nouveau_irq_unregister(dev, 12);
	nouveau_irq_unregister(dev, 25);

	nouveau_gpuobj_ref(NULL, &priv->unk4188b8);
	nouveau_gpuobj_ref(NULL, &priv->unk4188b4);

	if (priv->grctx_vals)
		kfree(priv->grctx_vals);

	NVOBJ_ENGINE_DEL(dev, GR);
	kfree(priv);
}

int
nvc0_graph_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_graph_priv *priv;
	int ret, gpc, i;
	u32 fermi;

	fermi = nvc0_graph_class(dev);
	if (!fermi) {
		NV_ERROR(dev, "PGRAPH: unsupported chipset, please report!\n");
		return 0;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base.destroy = nvc0_graph_destroy;
	priv->base.init = nvc0_graph_init;
	priv->base.fini = nvc0_graph_fini;
	priv->base.context_new = nvc0_graph_context_new;
	priv->base.context_del = nvc0_graph_context_del;
	priv->base.object_new = nvc0_graph_object_new;

	NVOBJ_ENGINE_ADD(dev, GR, &priv->base);
	nouveau_irq_register(dev, 12, nvc0_graph_isr);
	nouveau_irq_register(dev, 25, nvc0_runk140_isr);

	if (nouveau_ctxfw) {
		NV_INFO(dev, "PGRAPH: using external firmware\n");
		if (nvc0_graph_create_fw(dev, "fuc409c", &priv->fuc409c) ||
		    nvc0_graph_create_fw(dev, "fuc409d", &priv->fuc409d) ||
		    nvc0_graph_create_fw(dev, "fuc41ac", &priv->fuc41ac) ||
		    nvc0_graph_create_fw(dev, "fuc41ad", &priv->fuc41ad)) {
			ret = 0;
			goto error;
		}
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
		priv->tp_nr[gpc] = nv_rd32(dev, GPC_UNIT(gpc, 0x2608));
		priv->tp_total += priv->tp_nr[gpc];
	}

	/*XXX: these need figuring out... */
	switch (dev_priv->chipset) {
	case 0xc0:
		if (priv->tp_total == 11) { /* 465, 3/4/4/0, 4 */
			priv->magic_not_rop_nr = 0x07;
		} else
		if (priv->tp_total == 14) { /* 470, 3/3/4/4, 5 */
			priv->magic_not_rop_nr = 0x05;
		} else
		if (priv->tp_total == 15) { /* 480, 3/4/4/4, 6 */
			priv->magic_not_rop_nr = 0x06;
		}
		break;
	case 0xc3: /* 450, 4/0/0/0, 2 */
		priv->magic_not_rop_nr = 0x03;
		break;
	case 0xc4: /* 460, 3/4/0/0, 4 */
		priv->magic_not_rop_nr = 0x01;
		break;
	case 0xc1: /* 2/0/0/0, 1 */
		priv->magic_not_rop_nr = 0x01;
		break;
	case 0xc8: /* 4/4/3/4, 5 */
		priv->magic_not_rop_nr = 0x06;
		break;
	case 0xce: /* 4/4/0/0, 4 */
		priv->magic_not_rop_nr = 0x03;
		break;
	}

	if (!priv->magic_not_rop_nr) {
		NV_ERROR(dev, "PGRAPH: unknown config: %d/%d/%d/%d, %d\n",
			 priv->tp_nr[0], priv->tp_nr[1], priv->tp_nr[2],
			 priv->tp_nr[3], priv->rop_nr);
		/* use 0xc3's values... */
		priv->magic_not_rop_nr = 0x03;
	}

	NVOBJ_CLASS(dev, 0x902d, GR); /* 2D */
	NVOBJ_CLASS(dev, 0x9039, GR); /* M2MF */
	NVOBJ_MTHD (dev, 0x9039, 0x0500, nvc0_graph_mthd_page_flip);
	NVOBJ_CLASS(dev, 0x9097, GR); /* 3D */
	if (fermi >= 0x9197)
		NVOBJ_CLASS(dev, 0x9197, GR); /* 3D (NVC1-) */
	if (fermi >= 0x9297)
		NVOBJ_CLASS(dev, 0x9297, GR); /* 3D (NVC8-) */
	NVOBJ_CLASS(dev, 0x90c0, GR); /* COMPUTE */
	return 0;

error:
	nvc0_graph_destroy(dev, NVOBJ_ENGINE_GR);
	return ret;
}

MODULE_FIRMWARE("nouveau/nvc0_fuc409c");
MODULE_FIRMWARE("nouveau/nvc0_fuc409d");
MODULE_FIRMWARE("nouveau/nvc0_fuc41ac");
MODULE_FIRMWARE("nouveau/nvc0_fuc41ad");
MODULE_FIRMWARE("nouveau/nvc3_fuc409c");
MODULE_FIRMWARE("nouveau/nvc3_fuc409d");
MODULE_FIRMWARE("nouveau/nvc3_fuc41ac");
MODULE_FIRMWARE("nouveau/nvc3_fuc41ad");
MODULE_FIRMWARE("nouveau/nvc4_fuc409c");
MODULE_FIRMWARE("nouveau/nvc4_fuc409d");
MODULE_FIRMWARE("nouveau/nvc4_fuc41ac");
MODULE_FIRMWARE("nouveau/nvc4_fuc41ad");
MODULE_FIRMWARE("nouveau/fuc409c");
MODULE_FIRMWARE("nouveau/fuc409d");
MODULE_FIRMWARE("nouveau/fuc41ac");
MODULE_FIRMWARE("nouveau/fuc41ad");
