/*
 * Copyright 2012 Red Hat Inc.
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

#include <core/os.h>
#include <core/class.h>
#include <core/handle.h>
#include <core/engctx.h>

#include <subdev/fb.h>
#include <subdev/timer.h>

#include <engine/graph.h>
#include <engine/fifo.h>

#include "nv40.h"
#include "regs.h"

struct nv40_graph_priv {
	struct nouveau_graph base;
	u32 size;
};

struct nv40_graph_chan {
	struct nouveau_graph_chan base;
};

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

static int
nv40_graph_object_ctor(struct nouveau_object *parent,
		       struct nouveau_object *engine,
		       struct nouveau_oclass *oclass, void *data, u32 size,
		       struct nouveau_object **pobject)
{
	struct nouveau_gpuobj *obj;
	int ret;

	ret = nouveau_gpuobj_create(parent, engine, oclass, 0, parent,
				    20, 16, 0, &obj);
	*pobject = nv_object(obj);
	if (ret)
		return ret;

	nv_wo32(obj, 0x00, nv_mclass(obj));
	nv_wo32(obj, 0x04, 0x00000000);
	nv_wo32(obj, 0x08, 0x00000000);
#ifdef __BIG_ENDIAN
	nv_mo32(obj, 0x08, 0x01000000, 0x01000000);
#endif
	nv_wo32(obj, 0x0c, 0x00000000);
	nv_wo32(obj, 0x10, 0x00000000);
	return 0;
}

static struct nouveau_ofuncs
nv40_graph_ofuncs = {
	.ctor = nv40_graph_object_ctor,
	.dtor = _nouveau_gpuobj_dtor,
	.init = _nouveau_gpuobj_init,
	.fini = _nouveau_gpuobj_fini,
	.rd32 = _nouveau_gpuobj_rd32,
	.wr32 = _nouveau_gpuobj_wr32,
};

static struct nouveau_oclass
nv40_graph_sclass[] = {
	{ 0x0012, &nv40_graph_ofuncs, NULL }, /* beta1 */
	{ 0x0019, &nv40_graph_ofuncs, NULL }, /* clip */
	{ 0x0030, &nv40_graph_ofuncs, NULL }, /* null */
	{ 0x0039, &nv40_graph_ofuncs, NULL }, /* m2mf */
	{ 0x0043, &nv40_graph_ofuncs, NULL }, /* rop */
	{ 0x0044, &nv40_graph_ofuncs, NULL }, /* patt */
	{ 0x004a, &nv40_graph_ofuncs, NULL }, /* gdi */
	{ 0x0062, &nv40_graph_ofuncs, NULL }, /* surf2d */
	{ 0x0072, &nv40_graph_ofuncs, NULL }, /* beta4 */
	{ 0x0089, &nv40_graph_ofuncs, NULL }, /* sifm */
	{ 0x008a, &nv40_graph_ofuncs, NULL }, /* ifc */
	{ 0x009f, &nv40_graph_ofuncs, NULL }, /* imageblit */
	{ 0x3062, &nv40_graph_ofuncs, NULL }, /* surf2d (nv40) */
	{ 0x3089, &nv40_graph_ofuncs, NULL }, /* sifm (nv40) */
	{ 0x309e, &nv40_graph_ofuncs, NULL }, /* swzsurf (nv40) */
	{ 0x4097, &nv40_graph_ofuncs, NULL }, /* curie */
	{},
};

static struct nouveau_oclass
nv44_graph_sclass[] = {
	{ 0x0012, &nv40_graph_ofuncs, NULL }, /* beta1 */
	{ 0x0019, &nv40_graph_ofuncs, NULL }, /* clip */
	{ 0x0030, &nv40_graph_ofuncs, NULL }, /* null */
	{ 0x0039, &nv40_graph_ofuncs, NULL }, /* m2mf */
	{ 0x0043, &nv40_graph_ofuncs, NULL }, /* rop */
	{ 0x0044, &nv40_graph_ofuncs, NULL }, /* patt */
	{ 0x004a, &nv40_graph_ofuncs, NULL }, /* gdi */
	{ 0x0062, &nv40_graph_ofuncs, NULL }, /* surf2d */
	{ 0x0072, &nv40_graph_ofuncs, NULL }, /* beta4 */
	{ 0x0089, &nv40_graph_ofuncs, NULL }, /* sifm */
	{ 0x008a, &nv40_graph_ofuncs, NULL }, /* ifc */
	{ 0x009f, &nv40_graph_ofuncs, NULL }, /* imageblit */
	{ 0x3062, &nv40_graph_ofuncs, NULL }, /* surf2d (nv40) */
	{ 0x3089, &nv40_graph_ofuncs, NULL }, /* sifm (nv40) */
	{ 0x309e, &nv40_graph_ofuncs, NULL }, /* swzsurf (nv40) */
	{ 0x4497, &nv40_graph_ofuncs, NULL }, /* curie */
	{},
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv40_graph_context_ctor(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *data, u32 size,
			struct nouveau_object **pobject)
{
	struct nv40_graph_priv *priv = (void *)engine;
	struct nv40_graph_chan *chan;
	int ret;

	ret = nouveau_graph_context_create(parent, engine, oclass, NULL,
					   priv->size, 16,
					   NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	nv40_grctx_fill(nv_device(priv), nv_gpuobj(chan));
	nv_wo32(chan, 0x00000, nv_gpuobj(chan)->addr >> 4);
	return 0;
}

static int
nv40_graph_context_fini(struct nouveau_object *object, bool suspend)
{
	struct nv40_graph_priv *priv = (void *)object->engine;
	struct nv40_graph_chan *chan = (void *)object;
	u32 inst = 0x01000000 | nv_gpuobj(chan)->addr >> 4;
	int ret = 0;

	nv_mask(priv, 0x400720, 0x00000001, 0x00000000);

	if (nv_rd32(priv, 0x40032c) == inst) {
		if (suspend) {
			nv_wr32(priv, 0x400720, 0x00000000);
			nv_wr32(priv, 0x400784, inst);
			nv_mask(priv, 0x400310, 0x00000020, 0x00000020);
			nv_mask(priv, 0x400304, 0x00000001, 0x00000001);
			if (!nv_wait(priv, 0x400300, 0x00000001, 0x00000000)) {
				u32 insn = nv_rd32(priv, 0x400308);
				nv_warn(priv, "ctxprog timeout 0x%08x\n", insn);
				ret = -EBUSY;
			}
		}

		nv_mask(priv, 0x40032c, 0x01000000, 0x00000000);
	}

	if (nv_rd32(priv, 0x400330) == inst)
		nv_mask(priv, 0x400330, 0x01000000, 0x00000000);

	nv_mask(priv, 0x400720, 0x00000001, 0x00000001);
	return ret;
}

static struct nouveau_oclass
nv40_graph_cclass = {
	.handle = NV_ENGCTX(GR, 0x40),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv40_graph_context_ctor,
		.dtor = _nouveau_graph_context_dtor,
		.init = _nouveau_graph_context_init,
		.fini = nv40_graph_context_fini,
		.rd32 = _nouveau_graph_context_rd32,
		.wr32 = _nouveau_graph_context_wr32,
	},
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

static void
nv40_graph_tile_prog(struct nouveau_engine *engine, int i)
{
	struct nouveau_fb_tile *tile = &nouveau_fb(engine)->tile.region[i];
	struct nouveau_fifo *pfifo = nouveau_fifo(engine);
	struct nv40_graph_priv *priv = (void *)engine;
	unsigned long flags;

	pfifo->pause(pfifo, &flags);
	nv04_graph_idle(priv);

	switch (nv_device(priv)->chipset) {
	case 0x40:
	case 0x41: /* guess */
	case 0x42:
	case 0x43:
	case 0x45: /* guess */
	case 0x4e:
		nv_wr32(priv, NV20_PGRAPH_TSIZE(i), tile->pitch);
		nv_wr32(priv, NV20_PGRAPH_TLIMIT(i), tile->limit);
		nv_wr32(priv, NV20_PGRAPH_TILE(i), tile->addr);
		nv_wr32(priv, NV40_PGRAPH_TSIZE1(i), tile->pitch);
		nv_wr32(priv, NV40_PGRAPH_TLIMIT1(i), tile->limit);
		nv_wr32(priv, NV40_PGRAPH_TILE1(i), tile->addr);
		break;
	case 0x44:
	case 0x4a:
		nv_wr32(priv, NV20_PGRAPH_TSIZE(i), tile->pitch);
		nv_wr32(priv, NV20_PGRAPH_TLIMIT(i), tile->limit);
		nv_wr32(priv, NV20_PGRAPH_TILE(i), tile->addr);
		break;
	case 0x46:
	case 0x47:
	case 0x49:
	case 0x4b:
	case 0x4c:
	case 0x67:
	default:
		nv_wr32(priv, NV47_PGRAPH_TSIZE(i), tile->pitch);
		nv_wr32(priv, NV47_PGRAPH_TLIMIT(i), tile->limit);
		nv_wr32(priv, NV47_PGRAPH_TILE(i), tile->addr);
		nv_wr32(priv, NV40_PGRAPH_TSIZE1(i), tile->pitch);
		nv_wr32(priv, NV40_PGRAPH_TLIMIT1(i), tile->limit);
		nv_wr32(priv, NV40_PGRAPH_TILE1(i), tile->addr);
		break;
	}

	pfifo->start(pfifo, &flags);
}

static void
nv40_graph_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_fifo *pfifo = nouveau_fifo(subdev);
	struct nouveau_engine *engine = nv_engine(subdev);
	struct nouveau_object *engctx;
	struct nouveau_handle *handle = NULL;
	struct nv40_graph_priv *priv = (void *)subdev;
	u32 stat = nv_rd32(priv, NV03_PGRAPH_INTR);
	u32 nsource = nv_rd32(priv, NV03_PGRAPH_NSOURCE);
	u32 nstatus = nv_rd32(priv, NV03_PGRAPH_NSTATUS);
	u32 inst = nv_rd32(priv, 0x40032c) & 0x000fffff;
	u32 addr = nv_rd32(priv, NV04_PGRAPH_TRAPPED_ADDR);
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00001ffc);
	u32 data = nv_rd32(priv, NV04_PGRAPH_TRAPPED_DATA);
	u32 class = nv_rd32(priv, 0x400160 + subc * 4) & 0xffff;
	u32 show = stat;
	int chid;

	engctx = nouveau_engctx_get(engine, inst);
	chid   = pfifo->chid(pfifo, engctx);

	if (stat & NV_PGRAPH_INTR_ERROR) {
		if (nsource & NV03_PGRAPH_NSOURCE_ILLEGAL_MTHD) {
			handle = nouveau_handle_get_class(engctx, class);
			if (handle && !nv_call(handle->object, mthd, data))
				show &= ~NV_PGRAPH_INTR_ERROR;
			nouveau_handle_put(handle);
		}

		if (nsource & NV03_PGRAPH_NSOURCE_DMA_VTX_PROTECTION) {
			nv_mask(priv, 0x402000, 0, 0);
		}
	}

	nv_wr32(priv, NV03_PGRAPH_INTR, stat);
	nv_wr32(priv, NV04_PGRAPH_FIFO, 0x00000001);

	if (show) {
		nv_info(priv, "");
		nouveau_bitfield_print(nv10_graph_intr_name, show);
		printk(" nsource:");
		nouveau_bitfield_print(nv04_graph_nsource, nsource);
		printk(" nstatus:");
		nouveau_bitfield_print(nv10_graph_nstatus, nstatus);
		printk("\n");
		nv_error(priv, "ch %d [0x%08x] subc %d class 0x%04x "
			       "mthd 0x%04x data 0x%08x\n",
			 chid, inst << 4, subc, class, mthd, data);
	}

	nouveau_engctx_put(engctx);
}

static int
nv40_graph_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv40_graph_priv *priv;
	int ret;

	ret = nouveau_graph_create(parent, engine, oclass, true, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00001000;
	nv_subdev(priv)->intr = nv40_graph_intr;
	nv_engine(priv)->cclass = &nv40_graph_cclass;
	if (nv44_graph_class(priv))
		nv_engine(priv)->sclass = nv44_graph_sclass;
	else
		nv_engine(priv)->sclass = nv40_graph_sclass;
	nv_engine(priv)->tile_prog = nv40_graph_tile_prog;
	return 0;
}

static int
nv40_graph_init(struct nouveau_object *object)
{
	struct nouveau_engine *engine = nv_engine(object);
	struct nouveau_fb *pfb = nouveau_fb(object);
	struct nv40_graph_priv *priv = (void *)engine;
	int ret, i, j;
	u32 vramsz;

	ret = nouveau_graph_init(&priv->base);
	if (ret)
		return ret;

	/* generate and upload context program */
	nv40_grctx_init(nv_device(priv), &priv->size);

	/* No context present currently */
	nv_wr32(priv, NV40_PGRAPH_CTXCTL_CUR, 0x00000000);

	nv_wr32(priv, NV03_PGRAPH_INTR   , 0xFFFFFFFF);
	nv_wr32(priv, NV40_PGRAPH_INTR_EN, 0xFFFFFFFF);

	nv_wr32(priv, NV04_PGRAPH_DEBUG_0, 0xFFFFFFFF);
	nv_wr32(priv, NV04_PGRAPH_DEBUG_0, 0x00000000);
	nv_wr32(priv, NV04_PGRAPH_DEBUG_1, 0x401287c0);
	nv_wr32(priv, NV04_PGRAPH_DEBUG_3, 0xe0de8055);
	nv_wr32(priv, NV10_PGRAPH_DEBUG_4, 0x00008000);
	nv_wr32(priv, NV04_PGRAPH_LIMIT_VIOL_PIX, 0x00be3c5f);

	nv_wr32(priv, NV10_PGRAPH_CTX_CONTROL, 0x10010100);
	nv_wr32(priv, NV10_PGRAPH_STATE      , 0xFFFFFFFF);

	j = nv_rd32(priv, 0x1540) & 0xff;
	if (j) {
		for (i = 0; !(j & 1); j >>= 1, i++)
			;
		nv_wr32(priv, 0x405000, i);
	}

	if (nv_device(priv)->chipset == 0x40) {
		nv_wr32(priv, 0x4009b0, 0x83280fff);
		nv_wr32(priv, 0x4009b4, 0x000000a0);
	} else {
		nv_wr32(priv, 0x400820, 0x83280eff);
		nv_wr32(priv, 0x400824, 0x000000a0);
	}

	switch (nv_device(priv)->chipset) {
	case 0x40:
	case 0x45:
		nv_wr32(priv, 0x4009b8, 0x0078e366);
		nv_wr32(priv, 0x4009bc, 0x0000014c);
		break;
	case 0x41:
	case 0x42: /* pciid also 0x00Cx */
	/* case 0x0120: XXX (pciid) */
		nv_wr32(priv, 0x400828, 0x007596ff);
		nv_wr32(priv, 0x40082c, 0x00000108);
		break;
	case 0x43:
		nv_wr32(priv, 0x400828, 0x0072cb77);
		nv_wr32(priv, 0x40082c, 0x00000108);
		break;
	case 0x44:
	case 0x46: /* G72 */
	case 0x4a:
	case 0x4c: /* G7x-based C51 */
	case 0x4e:
		nv_wr32(priv, 0x400860, 0);
		nv_wr32(priv, 0x400864, 0);
		break;
	case 0x47: /* G70 */
	case 0x49: /* G71 */
	case 0x4b: /* G73 */
		nv_wr32(priv, 0x400828, 0x07830610);
		nv_wr32(priv, 0x40082c, 0x0000016A);
		break;
	default:
		break;
	}

	nv_wr32(priv, 0x400b38, 0x2ffff800);
	nv_wr32(priv, 0x400b3c, 0x00006000);

	/* Tiling related stuff. */
	switch (nv_device(priv)->chipset) {
	case 0x44:
	case 0x4a:
		nv_wr32(priv, 0x400bc4, 0x1003d888);
		nv_wr32(priv, 0x400bbc, 0xb7a7b500);
		break;
	case 0x46:
		nv_wr32(priv, 0x400bc4, 0x0000e024);
		nv_wr32(priv, 0x400bbc, 0xb7a7b520);
		break;
	case 0x4c:
	case 0x4e:
	case 0x67:
		nv_wr32(priv, 0x400bc4, 0x1003d888);
		nv_wr32(priv, 0x400bbc, 0xb7a7b540);
		break;
	default:
		break;
	}

	/* Turn all the tiling regions off. */
	for (i = 0; i < pfb->tile.regions; i++)
		engine->tile_prog(engine, i);

	/* begin RAM config */
	vramsz = pci_resource_len(nv_device(priv)->pdev, 0) - 1;
	switch (nv_device(priv)->chipset) {
	case 0x40:
		nv_wr32(priv, 0x4009A4, nv_rd32(priv, 0x100200));
		nv_wr32(priv, 0x4009A8, nv_rd32(priv, 0x100204));
		nv_wr32(priv, 0x4069A4, nv_rd32(priv, 0x100200));
		nv_wr32(priv, 0x4069A8, nv_rd32(priv, 0x100204));
		nv_wr32(priv, 0x400820, 0);
		nv_wr32(priv, 0x400824, 0);
		nv_wr32(priv, 0x400864, vramsz);
		nv_wr32(priv, 0x400868, vramsz);
		break;
	default:
		switch (nv_device(priv)->chipset) {
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x45:
		case 0x4e:
		case 0x44:
		case 0x4a:
			nv_wr32(priv, 0x4009F0, nv_rd32(priv, 0x100200));
			nv_wr32(priv, 0x4009F4, nv_rd32(priv, 0x100204));
			break;
		default:
			nv_wr32(priv, 0x400DF0, nv_rd32(priv, 0x100200));
			nv_wr32(priv, 0x400DF4, nv_rd32(priv, 0x100204));
			break;
		}
		nv_wr32(priv, 0x4069F0, nv_rd32(priv, 0x100200));
		nv_wr32(priv, 0x4069F4, nv_rd32(priv, 0x100204));
		nv_wr32(priv, 0x400840, 0);
		nv_wr32(priv, 0x400844, 0);
		nv_wr32(priv, 0x4008A0, vramsz);
		nv_wr32(priv, 0x4008A4, vramsz);
		break;
	}

	return 0;
}

struct nouveau_oclass
nv40_graph_oclass = {
	.handle = NV_ENGINE(GR, 0x40),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv40_graph_ctor,
		.dtor = _nouveau_graph_dtor,
		.init = nv40_graph_init,
		.fini = _nouveau_graph_fini,
	},
};
