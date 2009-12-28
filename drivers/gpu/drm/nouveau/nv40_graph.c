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

#include <linux/firmware.h>

#include "drmP.h"
#include "drm.h"
#include "nouveau_drv.h"

MODULE_FIRMWARE("nouveau/nv40.ctxprog");
MODULE_FIRMWARE("nouveau/nv40.ctxvals");
MODULE_FIRMWARE("nouveau/nv41.ctxprog");
MODULE_FIRMWARE("nouveau/nv41.ctxvals");
MODULE_FIRMWARE("nouveau/nv42.ctxprog");
MODULE_FIRMWARE("nouveau/nv42.ctxvals");
MODULE_FIRMWARE("nouveau/nv43.ctxprog");
MODULE_FIRMWARE("nouveau/nv43.ctxvals");
MODULE_FIRMWARE("nouveau/nv44.ctxprog");
MODULE_FIRMWARE("nouveau/nv44.ctxvals");
MODULE_FIRMWARE("nouveau/nv46.ctxprog");
MODULE_FIRMWARE("nouveau/nv46.ctxvals");
MODULE_FIRMWARE("nouveau/nv47.ctxprog");
MODULE_FIRMWARE("nouveau/nv47.ctxvals");
MODULE_FIRMWARE("nouveau/nv49.ctxprog");
MODULE_FIRMWARE("nouveau/nv49.ctxvals");
MODULE_FIRMWARE("nouveau/nv4a.ctxprog");
MODULE_FIRMWARE("nouveau/nv4a.ctxvals");
MODULE_FIRMWARE("nouveau/nv4b.ctxprog");
MODULE_FIRMWARE("nouveau/nv4b.ctxvals");
MODULE_FIRMWARE("nouveau/nv4c.ctxprog");
MODULE_FIRMWARE("nouveau/nv4c.ctxvals");
MODULE_FIRMWARE("nouveau/nv4e.ctxprog");
MODULE_FIRMWARE("nouveau/nv4e.ctxvals");

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
		    chan->ramin_grctx->instance == inst)
			return chan;
	}

	return NULL;
}

int
nv40_graph_create_context(struct nouveau_channel *chan)
{
	struct drm_device *dev = chan->dev;
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_gpuobj *ctx;
	int ret;

	/* Allocate a 175KiB block of PRAMIN to store the context.  This
	 * is massive overkill for a lot of chipsets, but it should be safe
	 * until we're able to implement this properly (will happen at more
	 * or less the same time we're able to write our own context programs.
	 */
	ret = nouveau_gpuobj_new_ref(dev, chan, NULL, 0, 175*1024, 16,
					  NVOBJ_FLAG_ZERO_ALLOC,
					  &chan->ramin_grctx);
	if (ret)
		return ret;
	ctx = chan->ramin_grctx->gpuobj;

	/* Initialise default context values */
	dev_priv->engine.instmem.prepare_access(dev, true);
	nv40_grctx_vals_load(dev, ctx);
	nv_wo32(dev, ctx, 0, ctx->im_pramin->start);
	dev_priv->engine.instmem.finish_access(dev);

	return 0;
}

void
nv40_graph_destroy_context(struct nouveau_channel *chan)
{
	nouveau_gpuobj_ref_del(chan->dev, &chan->ramin_grctx);
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
	inst = chan->ramin_grctx->instance >> 4;

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

struct nouveau_ctxprog {
	uint32_t signature;
	uint8_t  version;
	uint16_t length;
	uint32_t data[];
} __attribute__ ((packed));

struct nouveau_ctxvals {
	uint32_t signature;
	uint8_t  version;
	uint32_t length;
	struct {
		uint32_t offset;
		uint32_t value;
	} data[];
} __attribute__ ((packed));

int
nv40_grctx_init(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	const int chipset = dev_priv->chipset;
	const struct firmware *fw;
	const struct nouveau_ctxprog *cp;
	const struct nouveau_ctxvals *cv;
	char name[32];
	int ret, i;

	pgraph->accel_blocked = true;

	if (!pgraph->ctxprog) {
		sprintf(name, "nouveau/nv%02x.ctxprog", chipset);
		ret = request_firmware(&fw, name, &dev->pdev->dev);
		if (ret) {
			NV_ERROR(dev, "No ctxprog for NV%02x\n", chipset);
			return ret;
		}

		pgraph->ctxprog = kmalloc(fw->size, GFP_KERNEL);
		if (!pgraph->ctxprog) {
			NV_ERROR(dev, "OOM copying ctxprog\n");
			release_firmware(fw);
			return -ENOMEM;
		}
		memcpy(pgraph->ctxprog, fw->data, fw->size);

		cp = pgraph->ctxprog;
		if (le32_to_cpu(cp->signature) != 0x5043564e ||
		    cp->version != 0 ||
		    le16_to_cpu(cp->length) != ((fw->size - 7) / 4)) {
			NV_ERROR(dev, "ctxprog invalid\n");
			release_firmware(fw);
			nv40_grctx_fini(dev);
			return -EINVAL;
		}
		release_firmware(fw);
	}

	if (!pgraph->ctxvals) {
		sprintf(name, "nouveau/nv%02x.ctxvals", chipset);
		ret = request_firmware(&fw, name, &dev->pdev->dev);
		if (ret) {
			NV_ERROR(dev, "No ctxvals for NV%02x\n", chipset);
			nv40_grctx_fini(dev);
			return ret;
		}

		pgraph->ctxvals = kmalloc(fw->size, GFP_KERNEL);
		if (!pgraph->ctxprog) {
			NV_ERROR(dev, "OOM copying ctxprog\n");
			release_firmware(fw);
			nv40_grctx_fini(dev);
			return -ENOMEM;
		}
		memcpy(pgraph->ctxvals, fw->data, fw->size);

		cv = (void *)pgraph->ctxvals;
		if (le32_to_cpu(cv->signature) != 0x5643564e ||
		    cv->version != 0 ||
		    le32_to_cpu(cv->length) != ((fw->size - 9) / 8)) {
			NV_ERROR(dev, "ctxvals invalid\n");
			release_firmware(fw);
			nv40_grctx_fini(dev);
			return -EINVAL;
		}
		release_firmware(fw);
	}

	cp = pgraph->ctxprog;

	nv_wr32(dev, NV40_PGRAPH_CTXCTL_UCODE_INDEX, 0);
	for (i = 0; i < le16_to_cpu(cp->length); i++)
		nv_wr32(dev, NV40_PGRAPH_CTXCTL_UCODE_DATA,
			le32_to_cpu(cp->data[i]));

	pgraph->accel_blocked = false;
	return 0;
}

void
nv40_grctx_fini(struct drm_device *dev)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;

	if (pgraph->ctxprog) {
		kfree(pgraph->ctxprog);
		pgraph->ctxprog = NULL;
	}

	if (pgraph->ctxvals) {
		kfree(pgraph->ctxprog);
		pgraph->ctxvals = NULL;
	}
}

void
nv40_grctx_vals_load(struct drm_device *dev, struct nouveau_gpuobj *ctx)
{
	struct drm_nouveau_private *dev_priv = dev->dev_private;
	struct nouveau_pgraph_engine *pgraph = &dev_priv->engine.graph;
	struct nouveau_ctxvals *cv = pgraph->ctxvals;
	int i;

	if (!cv)
		return;

	for (i = 0; i < le32_to_cpu(cv->length); i++)
		nv_wo32(dev, ctx, le32_to_cpu(cv->data[i].offset),
			le32_to_cpu(cv->data[i].value));
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
	uint32_t vramsz, tmp;
	int i, j;

	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) &
			~NV_PMC_ENABLE_PGRAPH);
	nv_wr32(dev, NV03_PMC_ENABLE, nv_rd32(dev, NV03_PMC_ENABLE) |
			 NV_PMC_ENABLE_PGRAPH);

	nv40_grctx_init(dev);

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

	/* copy tile info from PFB */
	switch (dev_priv->chipset) {
	case 0x40: /* vanilla NV40 */
		for (i = 0; i < NV10_PFB_TILE__SIZE; i++) {
			tmp = nv_rd32(dev, NV10_PFB_TILE(i));
			nv_wr32(dev, NV40_PGRAPH_TILE0(i), tmp);
			nv_wr32(dev, NV40_PGRAPH_TILE1(i), tmp);
			tmp = nv_rd32(dev, NV10_PFB_TLIMIT(i));
			nv_wr32(dev, NV40_PGRAPH_TLIMIT0(i), tmp);
			nv_wr32(dev, NV40_PGRAPH_TLIMIT1(i), tmp);
			tmp = nv_rd32(dev, NV10_PFB_TSIZE(i));
			nv_wr32(dev, NV40_PGRAPH_TSIZE0(i), tmp);
			nv_wr32(dev, NV40_PGRAPH_TSIZE1(i), tmp);
			tmp = nv_rd32(dev, NV10_PFB_TSTATUS(i));
			nv_wr32(dev, NV40_PGRAPH_TSTATUS0(i), tmp);
			nv_wr32(dev, NV40_PGRAPH_TSTATUS1(i), tmp);
		}
		break;
	case 0x44:
	case 0x4a:
	case 0x4e: /* NV44-based cores don't have 0x406900? */
		for (i = 0; i < NV40_PFB_TILE__SIZE_0; i++) {
			tmp = nv_rd32(dev, NV40_PFB_TILE(i));
			nv_wr32(dev, NV40_PGRAPH_TILE0(i), tmp);
			tmp = nv_rd32(dev, NV40_PFB_TLIMIT(i));
			nv_wr32(dev, NV40_PGRAPH_TLIMIT0(i), tmp);
			tmp = nv_rd32(dev, NV40_PFB_TSIZE(i));
			nv_wr32(dev, NV40_PGRAPH_TSIZE0(i), tmp);
			tmp = nv_rd32(dev, NV40_PFB_TSTATUS(i));
			nv_wr32(dev, NV40_PGRAPH_TSTATUS0(i), tmp);
		}
		break;
	case 0x46:
	case 0x47:
	case 0x49:
	case 0x4b: /* G7X-based cores */
		for (i = 0; i < NV40_PFB_TILE__SIZE_1; i++) {
			tmp = nv_rd32(dev, NV40_PFB_TILE(i));
			nv_wr32(dev, NV47_PGRAPH_TILE0(i), tmp);
			nv_wr32(dev, NV40_PGRAPH_TILE1(i), tmp);
			tmp = nv_rd32(dev, NV40_PFB_TLIMIT(i));
			nv_wr32(dev, NV47_PGRAPH_TLIMIT0(i), tmp);
			nv_wr32(dev, NV40_PGRAPH_TLIMIT1(i), tmp);
			tmp = nv_rd32(dev, NV40_PFB_TSIZE(i));
			nv_wr32(dev, NV47_PGRAPH_TSIZE0(i), tmp);
			nv_wr32(dev, NV40_PGRAPH_TSIZE1(i), tmp);
			tmp = nv_rd32(dev, NV40_PFB_TSTATUS(i));
			nv_wr32(dev, NV47_PGRAPH_TSTATUS0(i), tmp);
			nv_wr32(dev, NV40_PGRAPH_TSTATUS1(i), tmp);
		}
		break;
	default: /* everything else */
		for (i = 0; i < NV40_PFB_TILE__SIZE_0; i++) {
			tmp = nv_rd32(dev, NV40_PFB_TILE(i));
			nv_wr32(dev, NV40_PGRAPH_TILE0(i), tmp);
			nv_wr32(dev, NV40_PGRAPH_TILE1(i), tmp);
			tmp = nv_rd32(dev, NV40_PFB_TLIMIT(i));
			nv_wr32(dev, NV40_PGRAPH_TLIMIT0(i), tmp);
			nv_wr32(dev, NV40_PGRAPH_TLIMIT1(i), tmp);
			tmp = nv_rd32(dev, NV40_PFB_TSIZE(i));
			nv_wr32(dev, NV40_PGRAPH_TSIZE0(i), tmp);
			nv_wr32(dev, NV40_PGRAPH_TSIZE1(i), tmp);
			tmp = nv_rd32(dev, NV40_PFB_TSTATUS(i));
			nv_wr32(dev, NV40_PGRAPH_TSTATUS0(i), tmp);
			nv_wr32(dev, NV40_PGRAPH_TSTATUS1(i), tmp);
		}
		break;
	}

	/* begin RAM config */
	vramsz = drm_get_resource_len(dev, 0) - 1;
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

