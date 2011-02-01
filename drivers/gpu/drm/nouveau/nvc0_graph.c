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

static void nvc0_graph_isr(struct drm_device *);
static void nvc0_runk140_isr(struct drm_device *);
static int  nvc0_graph_unload_context_to(struct drm_device *dev, u64 chan);

void
nvc0_graph_fifo_access(struct drm_device *dev, bool enabled)
{
}

struct nouveau_channel *
nvc0_graph_channel(struct drm_device *dev)
{
	return NULL;
}

static int
nvc0_graph_construct_context(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct nvc0_graph_priv *priv = dev_priv->engine.graph.priv;
	struct nvc0_graph_chan *grch = chan->pgraph_ctx;
	struct drm_device *dev = chan->dev;
	int ret, i;
	u32 *ctx;

	ctx = kmalloc(priv->grctx_size, GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	nvc0_graph_load_context(chan);

	nv_wo32(grch->grctx, 0x1c, 1);
	nv_wo32(grch->grctx, 0x20, 0);
	nv_wo32(grch->grctx, 0x28, 0);
	nv_wo32(grch->grctx, 0x2c, 0);
	dev_priv->engine.instmem.flush(dev);

	ret = nvc0_grctx_generate(chan);
	if (ret) {
		kfree(ctx);
		return ret;
	}

	ret = nvc0_graph_unload_context_to(dev, chan->ramin->vinst);
	if (ret) {
		kfree(ctx);
		return ret;
	}

	for (i = 0; i < priv->grctx_size; i += 4)
		ctx[i / 4] = nv_ro32(grch->grctx, i);

	priv->grctx_vals = ctx;
	return 0;
}

static int
nvc0_graph_create_context_mmio_list(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct nvc0_graph_priv *priv = dev_priv->engine.graph.priv;
	struct nvc0_graph_chan *grch = chan->pgraph_ctx;
	struct drm_device *dev = chan->dev;
	int i = 0, gpc, tp, ret;
	u32 magic;

	ret = nouveau_gpuobj_new(dev, NULL, 0x2000, 256, NVOBJ_FLAG_VM,
				 &grch->unk408004);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(dev, NULL, 0x8000, 256, NVOBJ_FLAG_VM,
				 &grch->unk40800c);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(dev, NULL, 384 * 1024, 4096,
				 NVOBJ_FLAG_VM | NVOBJ_FLAG_VM_USER,
				 &grch->unk418810);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(dev, NULL, 0x1000, 0, NVOBJ_FLAG_VM,
				 &grch->mmio);
	if (ret)
		return ret;


	nv_wo32(grch->mmio, i++ * 4, 0x00408004);
	nv_wo32(grch->mmio, i++ * 4, grch->unk408004->vinst >> 8);
	nv_wo32(grch->mmio, i++ * 4, 0x00408008);
	nv_wo32(grch->mmio, i++ * 4, 0x80000018);

	nv_wo32(grch->mmio, i++ * 4, 0x0040800c);
	nv_wo32(grch->mmio, i++ * 4, grch->unk40800c->vinst >> 8);
	nv_wo32(grch->mmio, i++ * 4, 0x00408010);
	nv_wo32(grch->mmio, i++ * 4, 0x80000000);

	nv_wo32(grch->mmio, i++ * 4, 0x00418810);
	nv_wo32(grch->mmio, i++ * 4, 0x80000000 | grch->unk418810->vinst >> 12);
	nv_wo32(grch->mmio, i++ * 4, 0x00419848);
	nv_wo32(grch->mmio, i++ * 4, 0x10000000 | grch->unk418810->vinst >> 12);

	nv_wo32(grch->mmio, i++ * 4, 0x00419004);
	nv_wo32(grch->mmio, i++ * 4, grch->unk40800c->vinst >> 8);
	nv_wo32(grch->mmio, i++ * 4, 0x00419008);
	nv_wo32(grch->mmio, i++ * 4, 0x00000000);

	nv_wo32(grch->mmio, i++ * 4, 0x00418808);
	nv_wo32(grch->mmio, i++ * 4, grch->unk408004->vinst >> 8);
	nv_wo32(grch->mmio, i++ * 4, 0x0041880c);
	nv_wo32(grch->mmio, i++ * 4, 0x80000018);

	magic = 0x02180000;
	nv_wo32(grch->mmio, i++ * 4, 0x00405830);
	nv_wo32(grch->mmio, i++ * 4, magic);
	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		for (tp = 0; tp < priv->tp_nr[gpc]; tp++, magic += 0x02fc) {
			u32 reg = 0x504520 + (gpc * 0x8000) + (tp * 0x0800);
			nv_wo32(grch->mmio, i++ * 4, reg);
			nv_wo32(grch->mmio, i++ * 4, magic);
		}
	}

	grch->mmio_nr = i / 2;
	return 0;
}

int
nvc0_graph_create_context(struct nouveau_channel *chan)
{
	struct drm_nouveau_private *dev_priv = chan->dev->dev_private;
	struct nouveau_instmem_engine *pinstmem = &dev_priv->engine.instmem;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nvc0_graph_priv *priv = pgraph->priv;
	struct nvc0_graph_chan *grch;
	struct drm_device *dev = chan->dev;
	struct nouveau_gpuobj *grctx;
	int ret, i;

	chan->pgraph_ctx = kzalloc(sizeof(*grch), GFP_KERNEL);
	if (!chan->pgraph_ctx)
		return -ENOMEM;
	grch = chan->pgraph_ctx;

	ret = nouveau_gpuobj_new(dev, NULL, priv->grctx_size, 256,
				 NVOBJ_FLAG_VM | NVOBJ_FLAG_ZERO_ALLOC,
				 &grch->grctx);
	if (ret)
		goto error;
	chan->ramin_grctx = grch->grctx;
	grctx = grch->grctx;

	ret = nvc0_graph_create_context_mmio_list(chan);
	if (ret)
		goto error;

	nv_wo32(chan->ramin, 0x0210, lower_32_bits(grctx->vinst) | 4);
	nv_wo32(chan->ramin, 0x0214, upper_32_bits(grctx->vinst));
	pinstmem->flush(dev);

	if (!priv->grctx_vals) {
		ret = nvc0_graph_construct_context(chan);
		if (ret)
			goto error;
	}

	for (i = 0; i < priv->grctx_size; i += 4)
		nv_wo32(grctx, i, priv->grctx_vals[i / 4]);

        nv_wo32(grctx, 0xf4, 0);
        nv_wo32(grctx, 0xf8, 0);
        nv_wo32(grctx, 0x10, grch->mmio_nr);
        nv_wo32(grctx, 0x14, lower_32_bits(grch->mmio->vinst));
        nv_wo32(grctx, 0x18, upper_32_bits(grch->mmio->vinst));
        nv_wo32(grctx, 0x1c, 1);
        nv_wo32(grctx, 0x20, 0);
        nv_wo32(grctx, 0x28, 0);
        nv_wo32(grctx, 0x2c, 0);
	pinstmem->flush(dev);
	return 0;

error:
	pgraph->destroy_context(chan);
	return ret;
}

void
nvc0_graph_destroy_context(struct nouveau_channel *chan)
{
	struct nvc0_graph_chan *grch;

	grch = chan->pgraph_ctx;
	chan->pgraph_ctx = NULL;
	if (!grch)
		return;

	nouveau_gpuobj_ref(NULL, &grch->mmio);
	nouveau_gpuobj_ref(NULL, &grch->unk418810);
	nouveau_gpuobj_ref(NULL, &grch->unk40800c);
	nouveau_gpuobj_ref(NULL, &grch->unk408004);
	nouveau_gpuobj_ref(NULL, &grch->grctx);
	chan->ramin_grctx = NULL;
}

int
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

int
nvc0_graph_unload_context(struct drm_device *dev)
{
	u64 inst = (u64)(nv_rd32(dev, 0x409b00) & 0x0fffffff) << 12;
	return nvc0_graph_unload_context_to(dev, inst);
}

static void
nvc0_graph_destroy(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nvc0_graph_priv *priv;

	priv = pgraph->priv;
	if (!priv)
		return;

	nouveau_irq_unregister(dev, 12);
	nouveau_irq_unregister(dev, 25);

	nouveau_gpuobj_ref(NULL, &priv->unk4188b8);
	nouveau_gpuobj_ref(NULL, &priv->unk4188b4);

	if (priv->grctx_vals)
		kfree(priv->grctx_vals);
	kfree(priv);
}

void
nvc0_graph_takedown(struct drm_device *dev)
{
	nvc0_graph_destroy(dev);
}

static int
nvc0_graph_create(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nvc0_graph_priv *priv;
	int ret, gpc, i;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	pgraph->priv = priv;

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
			/* filled values up to tp_total, the rest 0 */
			priv->magicgpc980[0]   = 0x22111000;
			priv->magicgpc980[1]   = 0x00000233;
			priv->magicgpc980[2]   = 0x00000000;
			priv->magicgpc980[3]   = 0x00000000;
			priv->magicgpc918      = 0x000ba2e9;
		} else
		if (priv->tp_total == 14) { /* 470, 3/3/4/4, 5 */
			priv->magic_not_rop_nr = 0x05;
			priv->magicgpc980[0]   = 0x11110000;
			priv->magicgpc980[1]   = 0x00233222;
			priv->magicgpc980[2]   = 0x00000000;
			priv->magicgpc980[3]   = 0x00000000;
			priv->magicgpc918      = 0x00092493;
		} else
		if (priv->tp_total == 15) { /* 480, 3/4/4/4, 6 */
			priv->magic_not_rop_nr = 0x06;
			priv->magicgpc980[0]   = 0x11110000;
			priv->magicgpc980[1]   = 0x03332222;
			priv->magicgpc980[2]   = 0x00000000;
			priv->magicgpc980[3]   = 0x00000000;
			priv->magicgpc918      = 0x00088889;
		}
		break;
	case 0xc3: /* 450, 4/0/0/0, 2 */
		priv->magic_not_rop_nr = 0x03;
		priv->magicgpc980[0]   = 0x00003210;
		priv->magicgpc980[1]   = 0x00000000;
		priv->magicgpc980[2]   = 0x00000000;
		priv->magicgpc980[3]   = 0x00000000;
		priv->magicgpc918      = 0x00200000;
		break;
	case 0xc4: /* 460, 3/4/0/0, 4 */
		priv->magic_not_rop_nr = 0x01;
		priv->magicgpc980[0]   = 0x02321100;
		priv->magicgpc980[1]   = 0x00000000;
		priv->magicgpc980[2]   = 0x00000000;
		priv->magicgpc980[3]   = 0x00000000;
		priv->magicgpc918      = 0x00124925;
		break;
	}

	if (!priv->magic_not_rop_nr) {
		NV_ERROR(dev, "PGRAPH: unknown config: %d/%d/%d/%d, %d\n",
			 priv->tp_nr[0], priv->tp_nr[1], priv->tp_nr[2],
			 priv->tp_nr[3], priv->rop_nr);
		/* use 0xc3's values... */
		priv->magic_not_rop_nr = 0x03;
		priv->magicgpc980[0]   = 0x00003210;
		priv->magicgpc980[1]   = 0x00000000;
		priv->magicgpc980[2]   = 0x00000000;
		priv->magicgpc980[3]   = 0x00000000;
		priv->magicgpc918      = 0x00200000;
	}

	nouveau_irq_register(dev, 12, nvc0_graph_isr);
	nouveau_irq_register(dev, 25, nvc0_runk140_isr);
	NVOBJ_CLASS(dev, 0x902d, GR); /* 2D */
	NVOBJ_CLASS(dev, 0x9039, GR); /* M2MF */
	NVOBJ_CLASS(dev, 0x9097, GR); /* 3D */
	NVOBJ_CLASS(dev, 0x90c0, GR); /* COMPUTE */
	return 0;

error:
	nvc0_graph_destroy(dev);
	return ret;
}

static void
nvc0_graph_init_obj418880(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nvc0_graph_priv *priv = pgraph->priv;
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_graph_priv *priv = dev_priv->engine.graph.priv;
	int gpc;
	
	//      TP      ROP UNKVAL(magic_not_rop_nr)
	// 450: 4/0/0/0 2        3
	// 460: 3/4/0/0 4        1
	// 465: 3/4/4/0 4        7
	// 470: 3/3/4/4 5        5
	// 480: 3/4/4/4 6        6

	// magicgpc918
	// 450: 00200000 00000000001000000000000000000000
	// 460: 00124925 00000000000100100100100100100101
	// 465: 000ba2e9 00000000000010111010001011101001
	// 470: 00092493 00000000000010010010010010010011
	// 480: 00088889 00000000000010001000100010001001

	/* filled values up to tp_total, remainder 0 */
	// 450: 00003210 00000000 00000000 00000000
	// 460: 02321100 00000000 00000000 00000000
	// 465: 22111000 00000233 00000000 00000000
	// 470: 11110000 00233222 00000000 00000000
	// 480: 11110000 03332222 00000000 00000000
	
	nv_wr32(dev, GPC_BCAST(0x0980), priv->magicgpc980[0]);
	nv_wr32(dev, GPC_BCAST(0x0984), priv->magicgpc980[1]);
	nv_wr32(dev, GPC_BCAST(0x0988), priv->magicgpc980[2]);
	nv_wr32(dev, GPC_BCAST(0x098c), priv->magicgpc980[3]);

	for (gpc = 0; gpc < priv->gpc_nr; gpc++) {
		nv_wr32(dev, GPC_UNIT(gpc, 0x0914), priv->magic_not_rop_nr << 8 |
						  priv->tp_nr[gpc]);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0910), 0x00040000 | priv->tp_total);
		nv_wr32(dev, GPC_UNIT(gpc, 0x0918), priv->magicgpc918);
	}

	nv_wr32(dev, GPC_BCAST(0x1bd4), priv->magicgpc918);
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_graph_priv *priv = dev_priv->engine.graph.priv;
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
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_graph_priv *priv = dev_priv->engine.graph.priv;
	int rop;

	for (rop = 0; rop < priv->rop_nr; rop++) {
		nv_wr32(dev, ROP_UNIT(rop, 0x144), 0xc0000000);
		nv_wr32(dev, ROP_UNIT(rop, 0x070), 0xc0000000);
		nv_wr32(dev, ROP_UNIT(rop, 0x204), 0xffffffff);
		nv_wr32(dev, ROP_UNIT(rop, 0x208), 0xffffffff);
	}
}

static int
nvc0_fuc_load_fw(struct drm_device *dev, u32 fuc_base,
		 const char *code_fw, const char *data_fw)
{
	const struct firmware *fw;
	char name[32];
	int ret, i;

	snprintf(name, sizeof(name), "nouveau/%s", data_fw);
	ret = request_firmware(&fw, name, &dev->pdev->dev);
	if (ret) {
		NV_ERROR(dev, "failed to load %s\n", data_fw);
		return ret;
	}

	nv_wr32(dev, fuc_base + 0x01c0, 0x01000000);
	for (i = 0; i < fw->size / 4; i++)
		nv_wr32(dev, fuc_base + 0x01c4, ((u32 *)fw->data)[i]);
	release_firmware(fw);

	snprintf(name, sizeof(name), "nouveau/%s", code_fw);
	ret = request_firmware(&fw, name, &dev->pdev->dev);
	if (ret) {
		NV_ERROR(dev, "failed to load %s\n", code_fw);
		return ret;
	}

	nv_wr32(dev, fuc_base + 0x0180, 0x01000000);
	for (i = 0; i < fw->size / 4; i++) {
		if ((i & 0x3f) == 0)
			nv_wr32(dev, fuc_base + 0x0188, i >> 6);
		nv_wr32(dev, fuc_base + 0x0184, ((u32 *)fw->data)[i]);
	}
	release_firmware(fw);

	return 0;
}

static int
nvc0_graph_init_ctxctl(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nvc0_graph_priv *priv = dev_priv->engine.graph.priv;
	u32 r000260;
	int ret;

	/* load fuc microcode */
	r000260 = nv_mask(dev, 0x000260, 0x00000001, 0x00000000);
	ret = nvc0_fuc_load_fw(dev, 0x409000, "fuc409c", "fuc409d");
	if (ret == 0)
		ret = nvc0_fuc_load_fw(dev, 0x41a000, "fuc41ac", "fuc41ad");
	nv_wr32(dev, 0x000260, r000260);

	if (ret)
		return ret;

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

int
nvc0_graph_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nvc0_graph_priv *priv;
	int ret;

	dev_priv->engine.graph.accel_blocked = true;

	switch (dev_priv->chipset) {
	case 0xc0:
	case 0xc3:
	case 0xc4:
		break;
	default:
		NV_ERROR(dev, "PGRAPH: unsupported chipset, please report!\n");
		if (nouveau_noaccel != 0)
			return 0;
		break;
	}

	nv_mask(dev, 0x000200, 0x18001000, 0x00000000);
	nv_mask(dev, 0x000200, 0x18001000, 0x18001000);

	if (!pgraph->priv) {
		ret = nvc0_graph_create(dev);
		if (ret)
			return ret;
	}
	priv = pgraph->priv;

	nvc0_graph_init_obj418880(dev);
	nvc0_graph_init_regs(dev);
	//nvc0_graph_init_unitplemented_magics(dev);
	nvc0_graph_init_gpc_0(dev);
	//nvc0_graph_init_unitplemented_c242(dev);

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
	if (ret == 0)
		dev_priv->engine.graph.accel_blocked = false;
	return 0;
}

static int
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
		NV_INFO(dev, "PGRAPH: ILLEGAL_MTHD ch %d [0x%010llx] subc %d "
			     "class 0x%04x mthd 0x%04x data 0x%08x\n",
			chid, inst, subc, class, mthd, data);
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
		u32 ustat = nv_rd32(dev, 0x409c18);

		NV_INFO(dev, "PGRAPH: CTXCTRL ustat 0x%08x\n", ustat);

		nv_wr32(dev, 0x409c20, ustat);
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

		NV_INFO(dev, "PRUNK140: %d 0x%08x 0x%08x\n", unit, st0, st1);
		units &= ~(1 << unit);
	}
}
