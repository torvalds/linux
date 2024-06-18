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
#include "nv40.h"
#include "regs.h"

#include <core/client.h>
#include <core/gpuobj.h>
#include <subdev/fb.h>
#include <subdev/timer.h>
#include <engine/fifo.h>

u64
nv40_gr_units(struct nvkm_gr *gr)
{
	return nvkm_rd32(gr->engine.subdev.device, 0x1540);
}

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

static int
nv40_gr_object_bind(struct nvkm_object *object, struct nvkm_gpuobj *parent,
		    int align, struct nvkm_gpuobj **pgpuobj)
{
	int ret = nvkm_gpuobj_new(object->engine->subdev.device, 20, align,
				  false, parent, pgpuobj);
	if (ret == 0) {
		nvkm_kmap(*pgpuobj);
		nvkm_wo32(*pgpuobj, 0x00, object->oclass);
		nvkm_wo32(*pgpuobj, 0x04, 0x00000000);
		nvkm_wo32(*pgpuobj, 0x08, 0x00000000);
#ifdef __BIG_ENDIAN
		nvkm_mo32(*pgpuobj, 0x08, 0x01000000, 0x01000000);
#endif
		nvkm_wo32(*pgpuobj, 0x0c, 0x00000000);
		nvkm_wo32(*pgpuobj, 0x10, 0x00000000);
		nvkm_done(*pgpuobj);
	}
	return ret;
}

const struct nvkm_object_func
nv40_gr_object = {
	.bind = nv40_gr_object_bind,
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv40_gr_chan_bind(struct nvkm_object *object, struct nvkm_gpuobj *parent,
		  int align, struct nvkm_gpuobj **pgpuobj)
{
	struct nv40_gr_chan *chan = nv40_gr_chan(object);
	struct nv40_gr *gr = chan->gr;
	int ret = nvkm_gpuobj_new(gr->base.engine.subdev.device, gr->size,
				  align, true, parent, pgpuobj);
	if (ret == 0) {
		chan->inst = (*pgpuobj)->addr;
		nvkm_kmap(*pgpuobj);
		nv40_grctx_fill(gr->base.engine.subdev.device, *pgpuobj);
		nvkm_wo32(*pgpuobj, 0x00000, chan->inst >> 4);
		nvkm_done(*pgpuobj);
	}
	return ret;
}

static int
nv40_gr_chan_fini(struct nvkm_object *object, bool suspend)
{
	struct nv40_gr_chan *chan = nv40_gr_chan(object);
	struct nv40_gr *gr = chan->gr;
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 inst = 0x01000000 | chan->inst >> 4;
	int ret = 0;

	nvkm_mask(device, 0x400720, 0x00000001, 0x00000000);

	if (nvkm_rd32(device, 0x40032c) == inst) {
		if (suspend) {
			nvkm_wr32(device, 0x400720, 0x00000000);
			nvkm_wr32(device, 0x400784, inst);
			nvkm_mask(device, 0x400310, 0x00000020, 0x00000020);
			nvkm_mask(device, 0x400304, 0x00000001, 0x00000001);
			if (nvkm_msec(device, 2000,
				if (!(nvkm_rd32(device, 0x400300) & 0x00000001))
					break;
			) < 0) {
				u32 insn = nvkm_rd32(device, 0x400308);
				nvkm_warn(subdev, "ctxprog timeout %08x\n", insn);
				ret = -EBUSY;
			}
		}

		nvkm_mask(device, 0x40032c, 0x01000000, 0x00000000);
	}

	if (nvkm_rd32(device, 0x400330) == inst)
		nvkm_mask(device, 0x400330, 0x01000000, 0x00000000);

	nvkm_mask(device, 0x400720, 0x00000001, 0x00000001);
	return ret;
}

static void *
nv40_gr_chan_dtor(struct nvkm_object *object)
{
	struct nv40_gr_chan *chan = nv40_gr_chan(object);
	unsigned long flags;
	spin_lock_irqsave(&chan->gr->base.engine.lock, flags);
	list_del(&chan->head);
	spin_unlock_irqrestore(&chan->gr->base.engine.lock, flags);
	return chan;
}

static const struct nvkm_object_func
nv40_gr_chan = {
	.dtor = nv40_gr_chan_dtor,
	.fini = nv40_gr_chan_fini,
	.bind = nv40_gr_chan_bind,
};

int
nv40_gr_chan_new(struct nvkm_gr *base, struct nvkm_chan *fifoch,
		 const struct nvkm_oclass *oclass, struct nvkm_object **pobject)
{
	struct nv40_gr *gr = nv40_gr(base);
	struct nv40_gr_chan *chan;
	unsigned long flags;

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_object_ctor(&nv40_gr_chan, oclass, &chan->object);
	chan->gr = gr;
	chan->fifo = fifoch;
	*pobject = &chan->object;

	spin_lock_irqsave(&chan->gr->base.engine.lock, flags);
	list_add(&chan->head, &gr->chan);
	spin_unlock_irqrestore(&chan->gr->base.engine.lock, flags);
	return 0;
}

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

static void
nv40_gr_tile(struct nvkm_gr *base, int i, struct nvkm_fb_tile *tile)
{
	struct nv40_gr *gr = nv40_gr(base);
	struct nvkm_device *device = gr->base.engine.subdev.device;
	struct nvkm_fifo *fifo = device->fifo;
	unsigned long flags;

	nvkm_fifo_pause(fifo, &flags);
	nv04_gr_idle(&gr->base);

	switch (device->chipset) {
	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x45:
		nvkm_wr32(device, NV20_PGRAPH_TSIZE(i), tile->pitch);
		nvkm_wr32(device, NV20_PGRAPH_TLIMIT(i), tile->limit);
		nvkm_wr32(device, NV20_PGRAPH_TILE(i), tile->addr);
		nvkm_wr32(device, NV40_PGRAPH_TSIZE1(i), tile->pitch);
		nvkm_wr32(device, NV40_PGRAPH_TLIMIT1(i), tile->limit);
		nvkm_wr32(device, NV40_PGRAPH_TILE1(i), tile->addr);
		switch (device->chipset) {
		case 0x40:
		case 0x45:
			nvkm_wr32(device, NV20_PGRAPH_ZCOMP(i), tile->zcomp);
			nvkm_wr32(device, NV40_PGRAPH_ZCOMP1(i), tile->zcomp);
			break;
		case 0x41:
		case 0x42:
		case 0x43:
			nvkm_wr32(device, NV41_PGRAPH_ZCOMP0(i), tile->zcomp);
			nvkm_wr32(device, NV41_PGRAPH_ZCOMP1(i), tile->zcomp);
			break;
		default:
			break;
		}
		break;
	case 0x47:
	case 0x49:
	case 0x4b:
		nvkm_wr32(device, NV47_PGRAPH_TSIZE(i), tile->pitch);
		nvkm_wr32(device, NV47_PGRAPH_TLIMIT(i), tile->limit);
		nvkm_wr32(device, NV47_PGRAPH_TILE(i), tile->addr);
		nvkm_wr32(device, NV40_PGRAPH_TSIZE1(i), tile->pitch);
		nvkm_wr32(device, NV40_PGRAPH_TLIMIT1(i), tile->limit);
		nvkm_wr32(device, NV40_PGRAPH_TILE1(i), tile->addr);
		nvkm_wr32(device, NV47_PGRAPH_ZCOMP0(i), tile->zcomp);
		nvkm_wr32(device, NV47_PGRAPH_ZCOMP1(i), tile->zcomp);
		break;
	default:
		WARN_ON(1);
		break;
	}

	nvkm_fifo_start(fifo, &flags);
}

void
nv40_gr_intr(struct nvkm_gr *base)
{
	struct nv40_gr *gr = nv40_gr(base);
	struct nv40_gr_chan *temp, *chan = NULL;
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 stat = nvkm_rd32(device, NV03_PGRAPH_INTR);
	u32 nsource = nvkm_rd32(device, NV03_PGRAPH_NSOURCE);
	u32 nstatus = nvkm_rd32(device, NV03_PGRAPH_NSTATUS);
	u32 inst = nvkm_rd32(device, 0x40032c) & 0x000fffff;
	u32 addr = nvkm_rd32(device, NV04_PGRAPH_TRAPPED_ADDR);
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00001ffc);
	u32 data = nvkm_rd32(device, NV04_PGRAPH_TRAPPED_DATA);
	u32 class = nvkm_rd32(device, 0x400160 + subc * 4) & 0xffff;
	u32 show = stat;
	char msg[128], src[128], sta[128];
	unsigned long flags;

	spin_lock_irqsave(&gr->base.engine.lock, flags);
	list_for_each_entry(temp, &gr->chan, head) {
		if (temp->inst >> 4 == inst) {
			chan = temp;
			list_del(&chan->head);
			list_add(&chan->head, &gr->chan);
			break;
		}
	}

	if (stat & NV_PGRAPH_INTR_ERROR) {
		if (nsource & NV03_PGRAPH_NSOURCE_DMA_VTX_PROTECTION) {
			nvkm_mask(device, 0x402000, 0, 0);
		}
	}

	nvkm_wr32(device, NV03_PGRAPH_INTR, stat);
	nvkm_wr32(device, NV04_PGRAPH_FIFO, 0x00000001);

	if (show) {
		nvkm_snprintbf(msg, sizeof(msg), nv10_gr_intr_name, show);
		nvkm_snprintbf(src, sizeof(src), nv04_gr_nsource, nsource);
		nvkm_snprintbf(sta, sizeof(sta), nv10_gr_nstatus, nstatus);
		nvkm_error(subdev, "intr %08x [%s] nsource %08x [%s] "
				   "nstatus %08x [%s] ch %d [%08x %s] subc %d "
				   "class %04x mthd %04x data %08x\n",
			   show, msg, nsource, src, nstatus, sta,
			   chan ? chan->fifo->id : -1, inst << 4,
			   chan ? chan->fifo->name : "unknown",
			   subc, class, mthd, data);
	}

	spin_unlock_irqrestore(&gr->base.engine.lock, flags);
}

int
nv40_gr_init(struct nvkm_gr *base)
{
	struct nv40_gr *gr = nv40_gr(base);
	struct nvkm_device *device = gr->base.engine.subdev.device;
	int ret, i, j;
	u32 vramsz;

	/* generate and upload context program */
	ret = nv40_grctx_init(device, &gr->size);
	if (ret)
		return ret;

	/* No context present currently */
	nvkm_wr32(device, NV40_PGRAPH_CTXCTL_CUR, 0x00000000);

	nvkm_wr32(device, NV03_PGRAPH_INTR   , 0xFFFFFFFF);
	nvkm_wr32(device, NV40_PGRAPH_INTR_EN, 0xFFFFFFFF);

	nvkm_wr32(device, NV04_PGRAPH_DEBUG_0, 0xFFFFFFFF);
	nvkm_wr32(device, NV04_PGRAPH_DEBUG_0, 0x00000000);
	nvkm_wr32(device, NV04_PGRAPH_DEBUG_1, 0x401287c0);
	nvkm_wr32(device, NV04_PGRAPH_DEBUG_3, 0xe0de8055);
	nvkm_wr32(device, NV10_PGRAPH_DEBUG_4, 0x00008000);
	nvkm_wr32(device, NV04_PGRAPH_LIMIT_VIOL_PIX, 0x00be3c5f);

	nvkm_wr32(device, NV10_PGRAPH_CTX_CONTROL, 0x10010100);
	nvkm_wr32(device, NV10_PGRAPH_STATE      , 0xFFFFFFFF);

	j = nvkm_rd32(device, 0x1540) & 0xff;
	if (j) {
		for (i = 0; !(j & 1); j >>= 1, i++)
			;
		nvkm_wr32(device, 0x405000, i);
	}

	if (device->chipset == 0x40) {
		nvkm_wr32(device, 0x4009b0, 0x83280fff);
		nvkm_wr32(device, 0x4009b4, 0x000000a0);
	} else {
		nvkm_wr32(device, 0x400820, 0x83280eff);
		nvkm_wr32(device, 0x400824, 0x000000a0);
	}

	switch (device->chipset) {
	case 0x40:
	case 0x45:
		nvkm_wr32(device, 0x4009b8, 0x0078e366);
		nvkm_wr32(device, 0x4009bc, 0x0000014c);
		break;
	case 0x41:
	case 0x42: /* pciid also 0x00Cx */
	/* case 0x0120: XXX (pciid) */
		nvkm_wr32(device, 0x400828, 0x007596ff);
		nvkm_wr32(device, 0x40082c, 0x00000108);
		break;
	case 0x43:
		nvkm_wr32(device, 0x400828, 0x0072cb77);
		nvkm_wr32(device, 0x40082c, 0x00000108);
		break;
	case 0x44:
	case 0x46: /* G72 */
	case 0x4a:
	case 0x4c: /* G7x-based C51 */
	case 0x4e:
		nvkm_wr32(device, 0x400860, 0);
		nvkm_wr32(device, 0x400864, 0);
		break;
	case 0x47: /* G70 */
	case 0x49: /* G71 */
	case 0x4b: /* G73 */
		nvkm_wr32(device, 0x400828, 0x07830610);
		nvkm_wr32(device, 0x40082c, 0x0000016A);
		break;
	default:
		break;
	}

	nvkm_wr32(device, 0x400b38, 0x2ffff800);
	nvkm_wr32(device, 0x400b3c, 0x00006000);

	/* Tiling related stuff. */
	switch (device->chipset) {
	case 0x44:
	case 0x4a:
		nvkm_wr32(device, 0x400bc4, 0x1003d888);
		nvkm_wr32(device, 0x400bbc, 0xb7a7b500);
		break;
	case 0x46:
		nvkm_wr32(device, 0x400bc4, 0x0000e024);
		nvkm_wr32(device, 0x400bbc, 0xb7a7b520);
		break;
	case 0x4c:
	case 0x4e:
	case 0x67:
		nvkm_wr32(device, 0x400bc4, 0x1003d888);
		nvkm_wr32(device, 0x400bbc, 0xb7a7b540);
		break;
	default:
		break;
	}

	/* begin RAM config */
	vramsz = device->func->resource_size(device, 1) - 1;
	switch (device->chipset) {
	case 0x40:
		nvkm_wr32(device, 0x4009A4, nvkm_rd32(device, 0x100200));
		nvkm_wr32(device, 0x4009A8, nvkm_rd32(device, 0x100204));
		nvkm_wr32(device, 0x4069A4, nvkm_rd32(device, 0x100200));
		nvkm_wr32(device, 0x4069A8, nvkm_rd32(device, 0x100204));
		nvkm_wr32(device, 0x400820, 0);
		nvkm_wr32(device, 0x400824, 0);
		nvkm_wr32(device, 0x400864, vramsz);
		nvkm_wr32(device, 0x400868, vramsz);
		break;
	default:
		switch (device->chipset) {
		case 0x41:
		case 0x42:
		case 0x43:
		case 0x45:
		case 0x4e:
		case 0x44:
		case 0x4a:
			nvkm_wr32(device, 0x4009F0, nvkm_rd32(device, 0x100200));
			nvkm_wr32(device, 0x4009F4, nvkm_rd32(device, 0x100204));
			break;
		default:
			nvkm_wr32(device, 0x400DF0, nvkm_rd32(device, 0x100200));
			nvkm_wr32(device, 0x400DF4, nvkm_rd32(device, 0x100204));
			break;
		}
		nvkm_wr32(device, 0x4069F0, nvkm_rd32(device, 0x100200));
		nvkm_wr32(device, 0x4069F4, nvkm_rd32(device, 0x100204));
		nvkm_wr32(device, 0x400840, 0);
		nvkm_wr32(device, 0x400844, 0);
		nvkm_wr32(device, 0x4008A0, vramsz);
		nvkm_wr32(device, 0x4008A4, vramsz);
		break;
	}

	return 0;
}

int
nv40_gr_new_(const struct nvkm_gr_func *func, struct nvkm_device *device,
	     enum nvkm_subdev_type type, int inst, struct nvkm_gr **pgr)
{
	struct nv40_gr *gr;

	if (!(gr = kzalloc(sizeof(*gr), GFP_KERNEL)))
		return -ENOMEM;
	*pgr = &gr->base;
	INIT_LIST_HEAD(&gr->chan);

	return nvkm_gr_ctor(func, device, type, inst, true, &gr->base);
}

static const struct nvkm_gr_func
nv40_gr = {
	.init = nv40_gr_init,
	.intr = nv40_gr_intr,
	.tile = nv40_gr_tile,
	.units = nv40_gr_units,
	.chan_new = nv40_gr_chan_new,
	.sclass = {
		{ -1, -1, 0x0012, &nv40_gr_object }, /* beta1 */
		{ -1, -1, 0x0019, &nv40_gr_object }, /* clip */
		{ -1, -1, 0x0030, &nv40_gr_object }, /* null */
		{ -1, -1, 0x0039, &nv40_gr_object }, /* m2mf */
		{ -1, -1, 0x0043, &nv40_gr_object }, /* rop */
		{ -1, -1, 0x0044, &nv40_gr_object }, /* patt */
		{ -1, -1, 0x004a, &nv40_gr_object }, /* gdi */
		{ -1, -1, 0x0062, &nv40_gr_object }, /* surf2d */
		{ -1, -1, 0x0072, &nv40_gr_object }, /* beta4 */
		{ -1, -1, 0x0089, &nv40_gr_object }, /* sifm */
		{ -1, -1, 0x008a, &nv40_gr_object }, /* ifc */
		{ -1, -1, 0x009f, &nv40_gr_object }, /* imageblit */
		{ -1, -1, 0x3062, &nv40_gr_object }, /* surf2d (nv40) */
		{ -1, -1, 0x3089, &nv40_gr_object }, /* sifm (nv40) */
		{ -1, -1, 0x309e, &nv40_gr_object }, /* swzsurf (nv40) */
		{ -1, -1, 0x4097, &nv40_gr_object }, /* curie */
		{}
	}
};

int
nv40_gr_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_gr **pgr)
{
	return nv40_gr_new_(&nv40_gr, device, type, inst, pgr);
}
