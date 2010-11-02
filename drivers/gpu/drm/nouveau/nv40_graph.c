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

struct nouveau_channel *
nv40_graph_channel(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t inst;
	int i;

	inst = nv_rd32(dev, NV40_PGRAPH_CTXCTL_CUR);
	if (!(inst & NV40_PGRAPH_CTXCTL_CUR_LOADED))
		return NULL;
	inst = (inst & NV40_PGRAPH_CTXCTL_CUR_INSTANCE) << 4;

	for (i = 0; i < dev_priv->engine.fifo.channels; i++) {
		struct nouveau_channel *chan = dev_priv->fifos[i];

		if (chan && chan->ramin_grctx &&
		    chan->ramin_grctx->pinst == inst)
			return chan;
	}

	return NULL;
}

int
nv40_graph_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_grctx ctx = {};
	int ret;

	ret = nouveau_gpuobj_new(dev, chan, pgraph->grctx_size, 16,
				 NVOBJ_FLAG_ZERO_ALLOC, &chan->ramin_grctx);
	if (ret)
		return ret;

	/* Initialise default context values */
	ctx.dev = chan->dev;
	ctx.mode = NOUVEAU_GRCTX_VALS;
	ctx.data = chan->ramin_grctx;
	nv40_grctx_init(&ctx);

	nv_wo32(chan->ramin_grctx, 0, chan->ramin_grctx->pinst);
	return 0;
}

void
nv40_graph_destroy_context(struct nouveau_channel *chan)
{
	nouveau_gpuobj_ref(NULL, &chan->ramin_grctx);
}

static int
nv40_graph_transfer_context(struct drm_device *dev, uint32_t inst, int save)
{
	uint32_t old_cp, tv = 1000, tmp;
	int i;

	old_cp = nv_rd32(dev, NV20_PGRAPH_CHANNEL_CTX_POINTER);
	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_POINTER, inst);

	tmp  = nv_rd32(dev, NV40_PGRAPH_CTXCTL_0310);
	tmp |= save ? NV40_PGRAPH_CTXCTL_0310_XFER_SAVE :
		      NV40_PGRAPH_CTXCTL_0310_XFER_LOAD;
	nv_wr32(dev, NV40_PGRAPH_CTXCTL_0310, tmp);

	tmp  = nv_rd32(dev, NV40_PGRAPH_CTXCTL_0304);
	tmp |= NV40_PGRAPH_CTXCTL_0304_XFER_CTX;
	nv_wr32(dev, NV40_PGRAPH_CTXCTL_0304, tmp);

	nouveau_wait_for_idle(dev);

	for (i = 0; i < tv; i++) {
		if (nv_rd32(dev, NV40_PGRAPH_CTXCTL_030C) == 0)
			break;
	}

	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_POINTER, old_cp);

	if (i == tv) {
		uint32_t ucstat = nv_rd32(dev, NV40_PGRAPH_CTXCTL_UCODE_STAT);
		NV_ERROR(dev, "Failed: Instance=0x%08x Save=%d\n", inst, save);
		NV_ERROR(dev, "IP: 0x%02x, Opcode: 0x%08x\n",
			 ucstat >> NV40_PGRAPH_CTXCTL_UCODE_STAT_IP_SHIFT,
			 ucstat  & NV40_PGRAPH_CTXCTL_UCODE_STAT_OP_MASK);
		NV_ERROR(dev, "0x40030C = 0x%08x\n",
			 nv_rd32(dev, NV40_PGRAPH_CTXCTL_030C));
		return -EBUSY;
	}

	return 0;
}

/* Restore the context for a specific channel into PGRAPH */
int
nv40_graph_load_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	uint32_t inst;
	int ret;

	if (!chan->ramin_grctx)
		return -EINVAL;
	inst = chan->ramin_grctx->pinst >> 4;

	ret = nv40_graph_transfer_context(dev, inst, 0);
	if (ret)
		return ret;

	/* 0x40032C, no idea of it's exact function.  Could simply be a
	 * record of the currently active PGRAPH context.  It's currently
	 * unknown as to what bit 24 does.  The nv ddx has it set, so we will
	 * set it here too.
	 */
	nv_wr32(dev, NV20_PGRAPH_CHANNEL_CTX_POINTER, inst);
	nv_wr32(dev, NV40_PGRAPH_CTXCTL_CUR,
		 (inst & NV40_PGRAPH_CTXCTL_CUR_INSTANCE) |
		  NV40_PGRAPH_CTXCTL_CUR_LOADED);
	/* 0x32E0 records the instance address of the active FIFO's PGRAPH
	 * context.  If at any time this doesn't match 0x40032C, you will
	 * recieve PGRAPH_INTR_CONTEXT_SWITCH
	 */
	nv_wr32(dev, NV40_PFIFO_GRCTX_INSTANCE, inst);
	return 0;
}

int
nv40_graph_unload_context(struct drm_device *dev)
{
	uint32_t inst;
	int ret;

	inst = nv_rd32(dev, NV40_PGRAPH_CTXCTL_CUR);
	if (!(inst & NV40_PGRAPH_CTXCTL_CUR_LOADED))
		return 0;
	inst &= NV40_PGRAPH_CTXCTL_CUR_INSTANCE;

	ret = nv40_graph_transfer_context(dev, inst, 1);

	nv_wr32(dev, NV40_PGRAPH_CTXCTL_CUR, inst);
	return ret;
}

void
nv40_graph_set_region_tiling(struct drm_device *dev, int i, uint32_t addr,
			     uint32_t size, uint32_t pitch)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	uint32_t limit = max(1u, addr + size) - 1;

	if (pitch)
		addr |= 1;

	switch (dev_priv->chipset) {
	case 0x44:
	case 0x4a:
	case 0x4e:
		nv_wr32(dev, NV20_PGRAPH_TSIZE(i), pitch);
		nv_wr32(dev, NV20_PGRAPH_TLIMIT(i), limit);
		nv_wr32(dev, NV20_PGRAPH_TILE(i), addr);
		break;

	case 0x46:
	case 0x47:
	case 0x49:
	case 0x4b:
		nv_wr32(dev, NV47_PGRAPH_TSIZE(i), pitch);
		nv_wr32(dev, NV47_PGRAPH_TLIMIT(i), limit);
		nv_wr32(dev, NV47_PGRAPH_TILE(i), addr);
		nv_wr32(dev, NV40_PGRAPH_TSIZE1(i), pitch);
		nv_wr32(dev, NV40_PGRAPH_TLIMIT1(i), limit);
		nv_wr32(dev, NV40_PGRAPH_TILE1(i), addr);
		break;

	default:
		nv_wr32(dev, NV20_PGRAPH_TSIZE(i), pitch);
		nv_wr32(dev, NV20_PGRAPH_TLIMIT(i), limit);
		nv_wr32(dev, NV20_PGRAPH_TILE(i), addr);
		nv_wr32(dev, NV40_PGRAPH_TSIZE1(i), pitch);
		nv_wr32(dev, NV40_PGRAPH_TLIMIT1(i), limit);
		nv_wr32(dev, NV40_PGRAPH_TILE1(i), addr);
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
nv40_graph_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv =
		(struct drm_nouveau_private *)dev->dev_private;
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
	dev_priv->engine.graph.grctx_size = ctx.ctxvals_pos * 4;

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
		nv40_graph_set_region_tiling(dev, i, 0, 0, 0);

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
		case 0x46:
		case 0x47:
		case 0x49:
		case 0x4b:
			nv_wr32(dev, 0x400DF0, nv_rd32(dev, NV04_PFB_CFG0));
			nv_wr32(dev, 0x400DF4, nv_rd32(dev, NV04_PFB_CFG1));
			break;
		default:
			nv_wr32(dev, 0x4009F0, nv_rd32(dev, NV04_PFB_CFG0));
			nv_wr32(dev, 0x4009F4, nv_rd32(dev, NV04_PFB_CFG1));
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

void nv40_graph_takedown(struct drm_device *dev)
{
}

struct nouveau_pgraph_object_class nv40_graph_grclass[] = {
	{ 0x0030, false, NULL }, /* null */
	{ 0x0039, false, NULL }, /* m2mf */
	{ 0x004a, false, NULL }, /* gdirect */
	{ 0x009f, false, NULL }, /* imageblit (nv12) */
	{ 0x008a, false, NULL }, /* ifc */
	{ 0x0089, false, NULL }, /* sifm */
	{ 0x3089, false, NULL }, /* sifm (nv40) */
	{ 0x0062, false, NULL }, /* surf2d */
	{ 0x3062, false, NULL }, /* surf2d (nv40) */
	{ 0x0043, false, NULL }, /* rop */
	{ 0x0012, false, NULL }, /* beta1 */
	{ 0x0072, false, NULL }, /* beta4 */
	{ 0x0019, false, NULL }, /* cliprect */
	{ 0x0044, false, NULL }, /* pattern */
	{ 0x309e, false, NULL }, /* swzsurf */
	{ 0x4097, false, NULL }, /* curie (nv40) */
	{ 0x4497, false, NULL }, /* curie (nv44) */
	{}
};

