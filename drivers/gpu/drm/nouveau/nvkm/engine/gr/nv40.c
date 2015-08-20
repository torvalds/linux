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
#include <core/handle.h>
#include <subdev/fb.h>
#include <subdev/timer.h>
#include <engine/fifo.h>

struct nv40_gr {
	struct nvkm_gr base;
	u32 size;
};

struct nv40_gr_chan {
	struct nvkm_gr_chan base;
};

static u64
nv40_gr_units(struct nvkm_gr *gr)
{
	return nvkm_rd32(gr->engine.subdev.device, 0x1540);
}

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

static int
nv40_gr_object_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, void *data, u32 size,
		    struct nvkm_object **pobject)
{
	struct nvkm_gpuobj *obj;
	int ret;

	ret = nvkm_gpuobj_create(parent, engine, oclass, 0, parent,
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

static struct nvkm_ofuncs
nv40_gr_ofuncs = {
	.ctor = nv40_gr_object_ctor,
	.dtor = _nvkm_gpuobj_dtor,
	.init = _nvkm_gpuobj_init,
	.fini = _nvkm_gpuobj_fini,
	.rd32 = _nvkm_gpuobj_rd32,
	.wr32 = _nvkm_gpuobj_wr32,
};

static struct nvkm_oclass
nv40_gr_sclass[] = {
	{ 0x0012, &nv40_gr_ofuncs, NULL }, /* beta1 */
	{ 0x0019, &nv40_gr_ofuncs, NULL }, /* clip */
	{ 0x0030, &nv40_gr_ofuncs, NULL }, /* null */
	{ 0x0039, &nv40_gr_ofuncs, NULL }, /* m2mf */
	{ 0x0043, &nv40_gr_ofuncs, NULL }, /* rop */
	{ 0x0044, &nv40_gr_ofuncs, NULL }, /* patt */
	{ 0x004a, &nv40_gr_ofuncs, NULL }, /* gdi */
	{ 0x0062, &nv40_gr_ofuncs, NULL }, /* surf2d */
	{ 0x0072, &nv40_gr_ofuncs, NULL }, /* beta4 */
	{ 0x0089, &nv40_gr_ofuncs, NULL }, /* sifm */
	{ 0x008a, &nv40_gr_ofuncs, NULL }, /* ifc */
	{ 0x009f, &nv40_gr_ofuncs, NULL }, /* imageblit */
	{ 0x3062, &nv40_gr_ofuncs, NULL }, /* surf2d (nv40) */
	{ 0x3089, &nv40_gr_ofuncs, NULL }, /* sifm (nv40) */
	{ 0x309e, &nv40_gr_ofuncs, NULL }, /* swzsurf (nv40) */
	{ 0x4097, &nv40_gr_ofuncs, NULL }, /* curie */
	{},
};

static struct nvkm_oclass
nv44_gr_sclass[] = {
	{ 0x0012, &nv40_gr_ofuncs, NULL }, /* beta1 */
	{ 0x0019, &nv40_gr_ofuncs, NULL }, /* clip */
	{ 0x0030, &nv40_gr_ofuncs, NULL }, /* null */
	{ 0x0039, &nv40_gr_ofuncs, NULL }, /* m2mf */
	{ 0x0043, &nv40_gr_ofuncs, NULL }, /* rop */
	{ 0x0044, &nv40_gr_ofuncs, NULL }, /* patt */
	{ 0x004a, &nv40_gr_ofuncs, NULL }, /* gdi */
	{ 0x0062, &nv40_gr_ofuncs, NULL }, /* surf2d */
	{ 0x0072, &nv40_gr_ofuncs, NULL }, /* beta4 */
	{ 0x0089, &nv40_gr_ofuncs, NULL }, /* sifm */
	{ 0x008a, &nv40_gr_ofuncs, NULL }, /* ifc */
	{ 0x009f, &nv40_gr_ofuncs, NULL }, /* imageblit */
	{ 0x3062, &nv40_gr_ofuncs, NULL }, /* surf2d (nv40) */
	{ 0x3089, &nv40_gr_ofuncs, NULL }, /* sifm (nv40) */
	{ 0x309e, &nv40_gr_ofuncs, NULL }, /* swzsurf (nv40) */
	{ 0x4497, &nv40_gr_ofuncs, NULL }, /* curie */
	{},
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv40_gr_context_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		     struct nvkm_oclass *oclass, void *data, u32 size,
		     struct nvkm_object **pobject)
{
	struct nv40_gr *gr = (void *)engine;
	struct nv40_gr_chan *chan;
	int ret;

	ret = nvkm_gr_context_create(parent, engine, oclass, NULL, gr->size,
				     16, NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	nv40_grctx_fill(nv_device(gr), nv_gpuobj(chan));
	nv_wo32(chan, 0x00000, nv_gpuobj(chan)->addr >> 4);
	return 0;
}

static int
nv40_gr_context_fini(struct nvkm_object *object, bool suspend)
{
	struct nv40_gr *gr = (void *)object->engine;
	struct nv40_gr_chan *chan = (void *)object;
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 inst = 0x01000000 | nv_gpuobj(chan)->addr >> 4;
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

static struct nvkm_oclass
nv40_gr_cclass = {
	.handle = NV_ENGCTX(GR, 0x40),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv40_gr_context_ctor,
		.dtor = _nvkm_gr_context_dtor,
		.init = _nvkm_gr_context_init,
		.fini = nv40_gr_context_fini,
		.rd32 = _nvkm_gr_context_rd32,
		.wr32 = _nvkm_gr_context_wr32,
	},
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

static void
nv40_gr_tile_prog(struct nvkm_engine *engine, int i)
{
	struct nv40_gr *gr = (void *)engine;
	struct nvkm_device *device = gr->base.engine.subdev.device;
	struct nvkm_fifo *fifo = device->fifo;
	struct nvkm_fb_tile *tile = &device->fb->tile.region[i];
	unsigned long flags;

	fifo->pause(fifo, &flags);
	nv04_gr_idle(gr);

	switch (nv_device(gr)->chipset) {
	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x45:
	case 0x4e:
		nvkm_wr32(device, NV20_PGRAPH_TSIZE(i), tile->pitch);
		nvkm_wr32(device, NV20_PGRAPH_TLIMIT(i), tile->limit);
		nvkm_wr32(device, NV20_PGRAPH_TILE(i), tile->addr);
		nvkm_wr32(device, NV40_PGRAPH_TSIZE1(i), tile->pitch);
		nvkm_wr32(device, NV40_PGRAPH_TLIMIT1(i), tile->limit);
		nvkm_wr32(device, NV40_PGRAPH_TILE1(i), tile->addr);
		switch (nv_device(gr)->chipset) {
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
	case 0x44:
	case 0x4a:
		nvkm_wr32(device, NV20_PGRAPH_TSIZE(i), tile->pitch);
		nvkm_wr32(device, NV20_PGRAPH_TLIMIT(i), tile->limit);
		nvkm_wr32(device, NV20_PGRAPH_TILE(i), tile->addr);
		break;
	case 0x46:
	case 0x4c:
	case 0x47:
	case 0x49:
	case 0x4b:
	case 0x63:
	case 0x67:
	case 0x68:
		nvkm_wr32(device, NV47_PGRAPH_TSIZE(i), tile->pitch);
		nvkm_wr32(device, NV47_PGRAPH_TLIMIT(i), tile->limit);
		nvkm_wr32(device, NV47_PGRAPH_TILE(i), tile->addr);
		nvkm_wr32(device, NV40_PGRAPH_TSIZE1(i), tile->pitch);
		nvkm_wr32(device, NV40_PGRAPH_TLIMIT1(i), tile->limit);
		nvkm_wr32(device, NV40_PGRAPH_TILE1(i), tile->addr);
		switch (nv_device(gr)->chipset) {
		case 0x47:
		case 0x49:
		case 0x4b:
			nvkm_wr32(device, NV47_PGRAPH_ZCOMP0(i), tile->zcomp);
			nvkm_wr32(device, NV47_PGRAPH_ZCOMP1(i), tile->zcomp);
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	fifo->start(fifo, &flags);
}

static void
nv40_gr_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_fifo *fifo = nvkm_fifo(subdev);
	struct nvkm_engine *engine = nv_engine(subdev);
	struct nvkm_object *engctx;
	struct nvkm_handle *handle = NULL;
	struct nv40_gr *gr = (void *)subdev;
	struct nvkm_device *device = gr->base.engine.subdev.device;
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
	int chid;

	engctx = nvkm_engctx_get(engine, inst);
	chid   = fifo->chid(fifo, engctx);

	if (stat & NV_PGRAPH_INTR_ERROR) {
		if (nsource & NV03_PGRAPH_NSOURCE_ILLEGAL_MTHD) {
			handle = nvkm_handle_get_class(engctx, class);
			if (handle && !nv_call(handle->object, mthd, data))
				show &= ~NV_PGRAPH_INTR_ERROR;
			nvkm_handle_put(handle);
		}

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
			   show, msg, nsource, src, nstatus, sta, chid,
			   inst << 4, nvkm_client_name(engctx), subc,
			   class, mthd, data);
	}

	nvkm_engctx_put(engctx);
}

static int
nv40_gr_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	     struct nvkm_oclass *oclass, void *data, u32 size,
	     struct nvkm_object **pobject)
{
	struct nv40_gr *gr;
	int ret;

	ret = nvkm_gr_create(parent, engine, oclass, true, &gr);
	*pobject = nv_object(gr);
	if (ret)
		return ret;

	nv_subdev(gr)->unit = 0x00001000;
	nv_subdev(gr)->intr = nv40_gr_intr;
	nv_engine(gr)->cclass = &nv40_gr_cclass;
	if (nv44_gr_class(gr))
		nv_engine(gr)->sclass = nv44_gr_sclass;
	else
		nv_engine(gr)->sclass = nv40_gr_sclass;
	nv_engine(gr)->tile_prog = nv40_gr_tile_prog;

	gr->base.units = nv40_gr_units;
	return 0;
}

static int
nv40_gr_init(struct nvkm_object *object)
{
	struct nvkm_engine *engine = nv_engine(object);
	struct nv40_gr *gr = (void *)engine;
	struct nvkm_device *device = gr->base.engine.subdev.device;
	struct nvkm_fb *fb = device->fb;
	int ret, i, j;
	u32 vramsz;

	ret = nvkm_gr_init(&gr->base);
	if (ret)
		return ret;

	/* generate and upload context program */
	ret = nv40_grctx_init(nv_device(gr), &gr->size);
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

	if (nv_device(gr)->chipset == 0x40) {
		nvkm_wr32(device, 0x4009b0, 0x83280fff);
		nvkm_wr32(device, 0x4009b4, 0x000000a0);
	} else {
		nvkm_wr32(device, 0x400820, 0x83280eff);
		nvkm_wr32(device, 0x400824, 0x000000a0);
	}

	switch (nv_device(gr)->chipset) {
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
	switch (nv_device(gr)->chipset) {
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

	/* Turn all the tiling regions off. */
	for (i = 0; i < fb->tile.regions; i++)
		engine->tile_prog(engine, i);

	/* begin RAM config */
	vramsz = nv_device_resource_len(nv_device(gr), 1) - 1;
	switch (nv_device(gr)->chipset) {
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
		switch (nv_device(gr)->chipset) {
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

struct nvkm_oclass
nv40_gr_oclass = {
	.handle = NV_ENGINE(GR, 0x40),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv40_gr_ctor,
		.dtor = _nvkm_gr_dtor,
		.init = nv40_gr_init,
		.fini = _nvkm_gr_fini,
	},
};
