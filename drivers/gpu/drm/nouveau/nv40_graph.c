/*
 * Copyright (C) 2007 Ben Skeggs.
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
#include "nouveau_grctx.h"
#include "nouveau_ramht.h"

struct nv40_graph_engine {
	struct nouveau_exec_engine base;
	u32 grctx_size;
};

static int
nv40_graph_context_new(struct nouveau_channel *chan, int engine)
{
	struct nv40_graph_engine *pgraph = nv_engine(chan->dev, engine);
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *grctx = NULL;
	struct nouveau_grctx ctx = {};
	unsigned long flags;
	int ret;

	ret = nouveau_gpuobj_new(dev, NULL, pgraph->grctx_size, 16,
				 NVOBJ_FLAG_ZERO_ALLOC, &grctx);
	if (ret)
		return ret;

	/* Initialise default context values */
	ctx.dev = chan->dev;
	ctx.mode = NOUVEAU_GRCTX_VALS;
	ctx.data = grctx;
	nv40_grctx_init(&ctx);

	nv_wo32(grctx, 0, grctx->vinst);

	/* init grctx pointer in ramfc, and on PFIFO if channel is
	 * already active there
	 */
	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv_wo32(chan->ramfc, 0x38, grctx->vinst >> 4);
	nv_mask(dev, 0x002500, 0x00000001, 0x00000000);
	if ((nv_rd32(dev, 0x003204) & 0x0000001f) == chan->id)
		nv_wr32(dev, 0x0032e0, grctx->vinst >> 4);
	nv_mask(dev, 0x002500, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	chan->engctx[engine] = grctx;
	return 0;
}

static void
nv40_graph_context_del(struct nouveau_channel *chan, int engine)
{
	struct nouveau_gpuobj *grctx = chan->engctx[engine];
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	u32 inst = 0x01000000 | (grctx->pinst >> 4);
	unsigned long flags;

	spin_lock_irqsave(&dev_priv->context_switch_lock, flags);
	nv_mask(dev, 0x400720, 0x00000000, 0x00000001);
	if (nv_rd32(dev, 0x40032c) == inst)
		nv_mask(dev, 0x40032c, 0x01000000, 0x00000000);
	if (nv_rd32(dev, 0x400330) == inst)
		nv_mask(dev, 0x400330, 0x01000000, 0x00000000);
	nv_mask(dev, 0x400720, 0x00000001, 0x00000001);
	spin_unlock_irqrestore(&dev_priv->context_switch_lock, flags);

	/* Free the context resources */
	nouveau_gpuobj_ref(NULL, &grctx);
	chan->engctx[engine] = NULL;
}

int
nv40_graph_object_new(struct nouveau_channel *chan, int engine,
		      u32 handle, u16 class)
{
	struct drm_device *dev = chan->dev;
	struct nouveau_gpuobj *obj = NULL;
	int ret;

	ret = nouveau_gpuobj_new(dev, chan, 20, 16, NVOBJ_FLAG_ZERO_FREE, &obj);
	if (ret)
		return ret;
	obj->engine = 1;
	obj->class  = class;

	nv_wo32(obj, 0x00, class);
	nv_wo32(obj, 0x04, 0x00000000);
#ifndef __BIG_ENDIAN
	nv_wo32(obj, 0x08, 0x00000000);
#else
	nv_wo32(obj, 0x08, 0x01000000);
#endif
	nv_wo32(obj, 0x0c, 0x00000000);
	nv_wo32(obj, 0x10, 0x00000000);

	ret = nouveau_ramht_insert(chan, handle, obj);
	nouveau_gpuobj_ref(NULL, &obj);
	return ret;
}

static void
nv40_graph_set_tile_region(struct drm_device *dev, int i)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_tile_reg *tile = &dev_priv->tile.reg[i];

	switch (dev_priv->chipset) {
	case 0x40:
	case 0x41: /* guess */
	case 0x42:
	case 0x43:
	case 0x45: /* guess */
	case 0x4e:
		nv_wr32(dev, NV20_PGRAPH_TSIZE(i), tile->pitch);
		nv_wr32(dev, NV20_PGRAPH_TLIMIT(i), tile->limit);
		nv_wr32(dev, NV20_PGRAPH_TILE(i), tile->addr);
		nv_wr32(dev, NV40_PGRAPH_TSIZE1(i), tile->pitch);
		nv_wr32(dev, NV40_PGRAPH_TLIMIT1(i), tile->limit);
		nv_wr32(dev, NV40_PGRAPH_TILE1(i), tile->addr);
		break;
	case 0x44:
	case 0x4a:
		nv_wr32(dev, NV20_PGRAPH_TSIZE(i), tile->pitch);
		nv_wr32(dev, NV20_PGRAPH_TLIMIT(i), tile->limit);
		nv_wr32(dev, NV20_PGRAPH_TILE(i), tile->addr);
		break;
	case 0x46:
	case 0x47:
	case 0x49:
	case 0x4b:
	case 0x4c:
	case 0x67:
	default:
		nv_wr32(dev, NV47_PGRAPH_TSIZE(i), tile->pitch);
		nv_wr32(dev, NV47_PGRAPH_TLIMIT(i), tile->limit);
		nv_wr32(dev, NV47_PGRAPH_TILE(i), tile->addr);
		nv_wr32(dev, NV40_PGRAPH_TSIZE1(i), tile->pitch);
		nv_wr32(dev, NV40_PGRAPH_TLIMIT1(i), tile->limit);
		nv_wr32(dev, NV40_PGRAPH_TILE1(i), tile->addr);
		break;
	}
}

/*
 * G70		0x47
 * G71		0x49
 * NV45		0x48
 * G72[M]	0x46
 * G73		0x4b
 * C51_G7X	0x4c
 * C51		0x4e
 */
int
nv40_graph_init(struct drm_device *dev, int engine)
{
	struct nv40_graph_engine *pgraph = nv_engine(dev, engine);
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_fb_engine *pfb = &dev_priv->engine.fb;
	struct nouveau_grctx ctx = {};
	uint32_t vramsz, *cp;
	int i, j;

	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) &
			~NV_PMC_ENABLE_PGRAPH);
	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) |
			 NV_PMC_ENABLE_PGRAPH);

	cp = kmalloc(sizeof(*cp) * 256, GFP_KERNEL);
	if (!cp)
		return -ENOMEM;

	ctx.dev = dev;
	ctx.mode = NOUVEAU_GRCTX_PROG;
	ctx.data = cp;
	ctx.ctxprog_max = 256;
	nv40_grctx_init(&ctx);
	pgraph->grctx_size = ctx.ctxvals_pos * 4;

	nv_wr32(dev, NV40_PGRAPH_CTXCTL_UCODE_INDEX, 0);
	for (i = 0; i < ctx.ctxprog_len; i++)
		nv_wr32(dev, NV40_PGRAPH_CTXCTL_UCODE_DATA, cp[i]);

	kfree(cp);

	/* No context present currently */
	nv_wr32(dev, NV40_PGRAPH_CTXCTL_CUR, 0x00000000);

	nv_wr32(dev, NV03_PGRAPH_INTR   , 0xFFFFFFFF);
	nv_wr32(dev, NV40_PGRAPH_INTR_EN, 0xFFFFFFFF);

	nv_wr32(dev, NV04_PGRAPH_DEBUG_0, 0xFFFFFFFF);
	nv_wr32(dev, NV04_PGRAPH_DEBUG_0, 0x00000000);
	nv_wr32(dev, NV04_PGRAPH_DEBUG_1, 0x401287c0);
	nv_wr32(dev, NV04_PGRAPH_DEBUG_3, 0xe0de8055);
	nv_wr32(dev, NV10_PGRAPH_DEBUG_4, 0x00008000);
	nv_wr32(dev, NV04_PGRAPH_LIMIT_VIOL_PIX, 0x00be3c5f);

	nv_wr32(dev, NV10_PGRAPH_CTX_CONTROL, 0x10010100);
	nv_wr32(dev, NV10_PGRAPH_STATE      , 0xFFFFFFFF);

	j = nv_rd32(dev, 0x1540) & 0xff;
	if (j) {
		for (i = 0; !(j & 1); j >>= 1, i++)
			;
		nv_wr32(dev, 0x405000, i);
	}

	if (dev_priv->chipset == 0x40) {
		nv_wr32(dev, 0x4009b0, 0x83280fff);
		nv_wr32(dev, 0x4009b4, 0x000000a0);
	} else {
		nv_wr32(dev, 0x400820, 0x83280eff);
		nv_wr32(dev, 0x400824, 0x000000a0);
	}

	switch (dev_priv->chipset) {
	case 0x40:
	case 0x45:
		nv_wr32(dev, 0x4009b8, 0x0078e366);
		nv_wr32(dev, 0x4009bc, 0x0000014c);
		break;
	case 0x41:
	case 0x42: /* pciid also 0x00Cx */
	/* case 0x0120: XXX (pciid) */
		nv_wr32(dev, 0x400828, 0x007596ff);
		nv_wr32(dev, 0x40082c, 0x00000108);
		break;
	case 0x43:
		nv_wr32(dev, 0x400828, 0x0072cb77);
		nv_wr32(dev, 0x40082c, 0x00000108);
		break;
	case 0x44:
	case 0x46: /* G72 */
	case 0x4a:
	case 0x4c: /* G7x-based C51 */
	case 0x4e:
		nv_wr32(dev, 0x400860, 0);
		nv_wr32(dev, 0x400864, 0);
		break;
	case 0x47: /* G70 */
	case 0x49: /* G71 */
	case 0x4b: /* G73 */
		nv_wr32(dev, 0x400828, 0x07830610);
		nv_wr32(dev, 0x40082c, 0x0000016A);
		break;
	default:
		break;
	}

	nv_wr32(dev, 0x400b38, 0x2ffff800);
	nv_wr32(dev, 0x400b3c, 0x00006000);

	/* Tiling related stuff. */
	switch (dev_priv->chipset) {
	case 0x44:
	case 0x4a:
		nv_wr32(dev, 0x400bc4, 0x1003d888);
		nv_wr32(dev, 0x400bbc, 0xb7a7b500);
		break;
	case 0x46:
		nv_wr32(dev, 0x400bc4, 0x0000e024);
		nv_wr32(dev, 0x400bbc, 0xb7a7b520);
		break;
	case 0x4c:
	case 0x4e:
	case 0x67:
		nv_wr32(dev, 0x400bc4, 0x1003d888);
		nv_wr32(dev, 0x400bbc, 0xb7a7b540);
		break;
	default:
		break;
	}

	/* Turn all the tiling regions off. */
	for (i = 0; i < pfb->num_tiles; i++)
		nv40_graph_set_tile_region(dev, i);

	/* begin RAM config */
	vramsz = pci_resource_len(dev->pdev, 0) - 1;
	switch (dev_priv->chipset) {
	case 0x40:
		nv_wr32(dev, 0x4009A4, nv_rd32(dev, NV04_PFB_CFG0));
		nv_wr32(dev, 0x4009A8, nv_rd32(dev, NV04_PFB_CFG1));
		nv_wr32(dev, 0x4069A4, nv_rd32(dev, NV04_PFB_CFG0));
		nv_wr32(dev, 0x4069A8, nv_rd32(dev, NV04_PFB_CFG1));
		nv_wr32(dev, 0x400820, 0);
		nv_wr32(dev, 0x400824, 0);
		nv_wr32(dev, 0x400864, vramsz);
		nv_wr32(dev, 0x400868, vramsz);
		break;
	default:
		switch (dev_priv->chipset) {
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x45:
		case 0x4e:
		case 0x44:
		case 0x4a:
			nv_wr32(dev, 0x4009F0, nv_rd32(dev, NV04_PFB_CFG0));
			nv_wr32(dev, 0x4009F4, nv_rd32(dev, NV04_PFB_CFG1));
			break;
		default:
			nv_wr32(dev, 0x400DF0, nv_rd32(dev, NV04_PFB_CFG0));
			nv_wr32(dev, 0x400DF4, nv_rd32(dev, NV04_PFB_CFG1));
			break;
		}
		nv_wr32(dev, 0x4069F0, nv_rd32(dev, NV04_PFB_CFG0));
		nv_wr32(dev, 0x4069F4, nv_rd32(dev, NV04_PFB_CFG1));
		nv_wr32(dev, 0x400840, 0);
		nv_wr32(dev, 0x400844, 0);
		nv_wr32(dev, 0x4008A0, vramsz);
		nv_wr32(dev, 0x4008A4, vramsz);
		break;
	}

	return 0;
}

static int
nv40_graph_fini(struct drm_device *dev, int engine, bool suspend)
{
	u32 inst = nv_rd32(dev, 0x40032c);
	if (inst & 0x01000000) {
		nv_wr32(dev, 0x400720, 0x00000000);
		nv_wr32(dev, 0x400784, inst);
		nv_mask(dev, 0x400310, 0x00000020, 0x00000020);
		nv_mask(dev, 0x400304, 0x00000001, 0x00000001);
		if (!nv_wait(dev, 0x400300, 0x00000001, 0x00000000)) {
			u32 insn = nv_rd32(dev, 0x400308);
			NV_ERROR(dev, "PGRAPH: ctxprog timeout 0x%08x\n", insn);
		}
		nv_mask(dev, 0x40032c, 0x01000000, 0x00000000);
	}
	return 0;
}

static int
nv40_graph_isr_chid(struct drm_device *dev, u32 inst)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *grctx;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&dev_priv->channels.lock, flags);
	for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
		if (!dev_priv->channels.ptr[i])
			continue;
		grctx = dev_priv->channels.ptr[i]->engctx[NVOBJ_ENGINE_GR];

		if (grctx && grctx->pinst == inst)
			break;
	}
	spin_unlock_irqrestore(&dev_priv->channels.lock, flags);
	return i;
}

static void
nv40_graph_isr(struct drm_device *dev)
{
	u32 stat;

	while ((stat = nv_rd32(dev, NV03_PGRAPH_INTR))) {
		u32 nsource = nv_rd32(dev, NV03_PGRAPH_NSOURCE);
		u32 nstatus = nv_rd32(dev, NV03_PGRAPH_NSTATUS);
		u32 inst = (nv_rd32(dev, 0x40032c) & 0x000fffff) << 4;
		u32 chid = nv40_graph_isr_chid(dev, inst);
		u32 addr = nv_rd32(dev, NV04_PGRAPH_TRAPPED_ADDR);
		u32 subc = (addr & 0x00070000) >> 16;
		u32 mthd = (addr & 0x00001ffc);
		u32 data = nv_rd32(dev, NV04_PGRAPH_TRAPPED_DATA);
		u32 class = nv_rd32(dev, 0x400160 + subc * 4) & 0xffff;
		u32 show = stat;

		if (stat & NV_PGRAPH_INTR_ERROR) {
			if (nsource & NV03_PGRAPH_NSOURCE_ILLEGAL_MTHD) {
				if (!nouveau_gpuobj_mthd_call2(dev, chid, class, mthd, data))
					show &= ~NV_PGRAPH_INTR_ERROR;
			} else
			if (nsource & NV03_PGRAPH_NSOURCE_DMA_VTX_PROTECTION) {
				nv_mask(dev, 0x402000, 0, 0);
			}
		}

		nv_wr32(dev, NV03_PGRAPH_INTR, stat);
		nv_wr32(dev, NV04_PGRAPH_FIFO, 0x00000001);

		if (show && nouveau_ratelimit()) {
			NV_INFO(dev, "PGRAPH -");
			nouveau_bitfield_print(nv10_graph_intr, show);
			printk(" nsource:");
			nouveau_bitfield_print(nv04_graph_nsource, nsource);
			printk(" nstatus:");
			nouveau_bitfield_print(nv10_graph_nstatus, nstatus);
			printk("\n");
			NV_INFO(dev, "PGRAPH - ch %d (0x%08x) subc %d "
				     "class 0x%04x mthd 0x%04x data 0x%08x\n",
				chid, inst, subc, class, mthd, data);
		}
	}
}

static void
nv40_graph_destroy(struct drm_device *dev, int engine)
{
	struct nv40_graph_engine *pgraph = nv_engine(dev, engine);

	nouveau_irq_unregister(dev, 12);

	NVOBJ_ENGINE_DEL(dev, GR);
	kfree(pgraph);
}

int
nv40_graph_create(struct drm_device *dev)
{
	struct nv40_graph_engine *pgraph;

	pgraph = kzalloc(sizeof(*pgraph), GFP_KERNEL);
	if (!pgraph)
		return -ENOMEM;

	pgraph->base.destroy = nv40_graph_destroy;
	pgraph->base.init = nv40_graph_init;
	pgraph->base.fini = nv40_graph_fini;
	pgraph->base.context_new = nv40_graph_context_new;
	pgraph->base.context_del = nv40_graph_context_del;
	pgraph->base.object_new = nv40_graph_object_new;
	pgraph->base.set_tile_region = nv40_graph_set_tile_region;

	NVOBJ_ENGINE_ADD(dev, GR, &pgraph->base);
	nouveau_irq_register(dev, 12, nv40_graph_isr);

	NVOBJ_CLASS(dev, 0x506e, SW); /* nvsw */
	NVOBJ_CLASS(dev, 0x0030, GR); /* null */
	NVOBJ_CLASS(dev, 0x0039, GR); /* m2mf */
	NVOBJ_CLASS(dev, 0x004a, GR); /* gdirect */
	NVOBJ_CLASS(dev, 0x009f, GR); /* imageblit (nv12) */
	NVOBJ_CLASS(dev, 0x008a, GR); /* ifc */
	NVOBJ_CLASS(dev, 0x0089, GR); /* sifm */
	NVOBJ_CLASS(dev, 0x3089, GR); /* sifm (nv40) */
	NVOBJ_CLASS(dev, 0x0062, GR); /* surf2d */
	NVOBJ_CLASS(dev, 0x3062, GR); /* surf2d (nv40) */
	NVOBJ_CLASS(dev, 0x0043, GR); /* rop */
	NVOBJ_CLASS(dev, 0x0012, GR); /* beta1 */
	NVOBJ_CLASS(dev, 0x0072, GR); /* beta4 */
	NVOBJ_CLASS(dev, 0x0019, GR); /* cliprect */
	NVOBJ_CLASS(dev, 0x0044, GR); /* pattern */
	NVOBJ_CLASS(dev, 0x309e, GR); /* swzsurf */

	/* curie */
	if (nv44_graph_class(dev))
		NVOBJ_CLASS(dev, 0x4497, GR);
	else
		NVOBJ_CLASS(dev, 0x4097, GR);

	/* nvsw */
	NVOBJ_CLASS(dev, 0x506e, SW);
	NVOBJ_MTHD (dev, 0x506e, 0x0500, nv04_graph_mthd_page_flip);
	return 0;
}
