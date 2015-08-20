#include "nv20.h"
#include "regs.h"

#include <core/client.h>
#include <engine/fifo.h>
#include <subdev/fb.h>
#include <subdev/timer.h>

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

static struct nvkm_oclass
nv20_gr_sclass[] = {
	{ 0x0012, &nv04_gr_ofuncs, NULL }, /* beta1 */
	{ 0x0019, &nv04_gr_ofuncs, NULL }, /* clip */
	{ 0x0030, &nv04_gr_ofuncs, NULL }, /* null */
	{ 0x0039, &nv04_gr_ofuncs, NULL }, /* m2mf */
	{ 0x0043, &nv04_gr_ofuncs, NULL }, /* rop */
	{ 0x0044, &nv04_gr_ofuncs, NULL }, /* patt */
	{ 0x004a, &nv04_gr_ofuncs, NULL }, /* gdi */
	{ 0x0062, &nv04_gr_ofuncs, NULL }, /* surf2d */
	{ 0x0072, &nv04_gr_ofuncs, NULL }, /* beta4 */
	{ 0x0089, &nv04_gr_ofuncs, NULL }, /* sifm */
	{ 0x008a, &nv04_gr_ofuncs, NULL }, /* ifc */
	{ 0x0096, &nv04_gr_ofuncs, NULL }, /* celcius */
	{ 0x0097, &nv04_gr_ofuncs, NULL }, /* kelvin */
	{ 0x009e, &nv04_gr_ofuncs, NULL }, /* swzsurf */
	{ 0x009f, &nv04_gr_ofuncs, NULL }, /* imageblit */
	{},
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv20_gr_context_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		     struct nvkm_oclass *oclass, void *data, u32 size,
		     struct nvkm_object **pobject)
{
	struct nv20_gr_chan *chan;
	struct nvkm_gpuobj *image;
	int ret, i;

	ret = nvkm_gr_context_create(parent, engine, oclass, NULL, 0x37f0,
				     16, NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	chan->chid = nvkm_fifo_chan(parent)->chid;
	image = &chan->base.base.gpuobj;

	nvkm_kmap(image);
	nvkm_wo32(image, 0x0000, 0x00000001 | (chan->chid << 24));
	nvkm_wo32(image, 0x033c, 0xffff0000);
	nvkm_wo32(image, 0x03a0, 0x0fff0000);
	nvkm_wo32(image, 0x03a4, 0x0fff0000);
	nvkm_wo32(image, 0x047c, 0x00000101);
	nvkm_wo32(image, 0x0490, 0x00000111);
	nvkm_wo32(image, 0x04a8, 0x44400000);
	for (i = 0x04d4; i <= 0x04e0; i += 4)
		nvkm_wo32(image, i, 0x00030303);
	for (i = 0x04f4; i <= 0x0500; i += 4)
		nvkm_wo32(image, i, 0x00080000);
	for (i = 0x050c; i <= 0x0518; i += 4)
		nvkm_wo32(image, i, 0x01012000);
	for (i = 0x051c; i <= 0x0528; i += 4)
		nvkm_wo32(image, i, 0x000105b8);
	for (i = 0x052c; i <= 0x0538; i += 4)
		nvkm_wo32(image, i, 0x00080008);
	for (i = 0x055c; i <= 0x0598; i += 4)
		nvkm_wo32(image, i, 0x07ff0000);
	nvkm_wo32(image, 0x05a4, 0x4b7fffff);
	nvkm_wo32(image, 0x05fc, 0x00000001);
	nvkm_wo32(image, 0x0604, 0x00004000);
	nvkm_wo32(image, 0x0610, 0x00000001);
	nvkm_wo32(image, 0x0618, 0x00040000);
	nvkm_wo32(image, 0x061c, 0x00010000);
	for (i = 0x1c1c; i <= 0x248c; i += 16) {
		nvkm_wo32(image, (i + 0), 0x10700ff9);
		nvkm_wo32(image, (i + 4), 0x0436086c);
		nvkm_wo32(image, (i + 8), 0x000c001b);
	}
	nvkm_wo32(image, 0x281c, 0x3f800000);
	nvkm_wo32(image, 0x2830, 0x3f800000);
	nvkm_wo32(image, 0x285c, 0x40000000);
	nvkm_wo32(image, 0x2860, 0x3f800000);
	nvkm_wo32(image, 0x2864, 0x3f000000);
	nvkm_wo32(image, 0x286c, 0x40000000);
	nvkm_wo32(image, 0x2870, 0x3f800000);
	nvkm_wo32(image, 0x2878, 0xbf800000);
	nvkm_wo32(image, 0x2880, 0xbf800000);
	nvkm_wo32(image, 0x34a4, 0x000fe000);
	nvkm_wo32(image, 0x3530, 0x000003f8);
	nvkm_wo32(image, 0x3540, 0x002fe000);
	for (i = 0x355c; i <= 0x3578; i += 4)
		nvkm_wo32(image, i, 0x001c527c);
	nvkm_done(image);
	return 0;
}

int
nv20_gr_context_init(struct nvkm_object *object)
{
	struct nv20_gr *gr = (void *)object->engine;
	struct nv20_gr_chan *chan = (void *)object;
	int ret;

	ret = nvkm_gr_context_init(&chan->base);
	if (ret)
		return ret;

	nvkm_kmap(gr->ctxtab);
	nvkm_wo32(gr->ctxtab, chan->chid * 4, nv_gpuobj(chan)->addr >> 4);
	nvkm_done(gr->ctxtab);
	return 0;
}

int
nv20_gr_context_fini(struct nvkm_object *object, bool suspend)
{
	struct nv20_gr *gr = (void *)object->engine;
	struct nv20_gr_chan *chan = (void *)object;
	struct nvkm_device *device = gr->base.engine.subdev.device;
	int chid = -1;

	nvkm_mask(device, 0x400720, 0x00000001, 0x00000000);
	if (nvkm_rd32(device, 0x400144) & 0x00010000)
		chid = (nvkm_rd32(device, 0x400148) & 0x1f000000) >> 24;
	if (chan->chid == chid) {
		nvkm_wr32(device, 0x400784, nv_gpuobj(chan)->addr >> 4);
		nvkm_wr32(device, 0x400788, 0x00000002);
		nvkm_msec(device, 2000,
			if (!nvkm_rd32(device, 0x400700))
				break;
		);
		nvkm_wr32(device, 0x400144, 0x10000000);
		nvkm_mask(device, 0x400148, 0xff000000, 0x1f000000);
	}
	nvkm_mask(device, 0x400720, 0x00000001, 0x00000001);

	nvkm_kmap(gr->ctxtab);
	nvkm_wo32(gr->ctxtab, chan->chid * 4, 0x00000000);
	nvkm_done(gr->ctxtab);

	return nvkm_gr_context_fini(&chan->base, suspend);
}

static struct nvkm_oclass
nv20_gr_cclass = {
	.handle = NV_ENGCTX(GR, 0x20),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv20_gr_context_ctor,
		.dtor = _nvkm_gr_context_dtor,
		.init = nv20_gr_context_init,
		.fini = nv20_gr_context_fini,
		.rd32 = _nvkm_gr_context_rd32,
		.wr32 = _nvkm_gr_context_wr32,
	},
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

void
nv20_gr_tile_prog(struct nvkm_engine *engine, int i)
{
	struct nv20_gr *gr = (void *)engine;
	struct nvkm_device *device = gr->base.engine.subdev.device;
	struct nvkm_fifo *fifo = device->fifo;
	struct nvkm_fb_tile *tile = &device->fb->tile.region[i];
	unsigned long flags;

	fifo->pause(fifo, &flags);
	nv04_gr_idle(gr);

	nvkm_wr32(device, NV20_PGRAPH_TLIMIT(i), tile->limit);
	nvkm_wr32(device, NV20_PGRAPH_TSIZE(i), tile->pitch);
	nvkm_wr32(device, NV20_PGRAPH_TILE(i), tile->addr);

	nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00EA0030 + 4 * i);
	nvkm_wr32(device, NV10_PGRAPH_RDI_DATA, tile->limit);
	nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00EA0050 + 4 * i);
	nvkm_wr32(device, NV10_PGRAPH_RDI_DATA, tile->pitch);
	nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00EA0010 + 4 * i);
	nvkm_wr32(device, NV10_PGRAPH_RDI_DATA, tile->addr);

	if (nv_device(engine)->chipset != 0x34) {
		nvkm_wr32(device, NV20_PGRAPH_ZCOMP(i), tile->zcomp);
		nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00ea0090 + 4 * i);
		nvkm_wr32(device, NV10_PGRAPH_RDI_DATA, tile->zcomp);
	}

	fifo->start(fifo, &flags);
}

void
nv20_gr_intr(struct nvkm_subdev *subdev)
{
	struct nv20_gr *gr = (void *)subdev;
	struct nvkm_device *device = gr->base.engine.subdev.device;
	struct nvkm_fifo_chan *chan;
	u32 stat = nvkm_rd32(device, NV03_PGRAPH_INTR);
	u32 nsource = nvkm_rd32(device, NV03_PGRAPH_NSOURCE);
	u32 nstatus = nvkm_rd32(device, NV03_PGRAPH_NSTATUS);
	u32 addr = nvkm_rd32(device, NV04_PGRAPH_TRAPPED_ADDR);
	u32 chid = (addr & 0x01f00000) >> 20;
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00001ffc);
	u32 data = nvkm_rd32(device, NV04_PGRAPH_TRAPPED_DATA);
	u32 class = nvkm_rd32(device, 0x400160 + subc * 4) & 0xfff;
	u32 show = stat;
	char msg[128], src[128], sta[128];
	unsigned long flags;

	chan = nvkm_fifo_chan_chid(device->fifo, chid, &flags);

	nvkm_wr32(device, NV03_PGRAPH_INTR, stat);
	nvkm_wr32(device, NV04_PGRAPH_FIFO, 0x00000001);

	if (show) {
		nvkm_snprintbf(msg, sizeof(msg), nv10_gr_intr_name, show);
		nvkm_snprintbf(src, sizeof(src), nv04_gr_nsource, nsource);
		nvkm_snprintbf(sta, sizeof(sta), nv10_gr_nstatus, nstatus);
		nvkm_error(subdev, "intr %08x [%s] nsource %08x [%s] "
				   "nstatus %08x [%s] ch %d [%s] subc %d "
				   "class %04x mthd %04x data %08x\n",
			   show, msg, nsource, src, nstatus, sta, chid,
			   nvkm_client_name(chan), subc, class, mthd, data);
	}

	nvkm_fifo_chan_put(device->fifo, flags, &chan);
}

static int
nv20_gr_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	     struct nvkm_oclass *oclass, void *data, u32 size,
	     struct nvkm_object **pobject)
{
	struct nvkm_device *device = (void *)parent;
	struct nv20_gr *gr;
	int ret;

	ret = nvkm_gr_create(parent, engine, oclass, true, &gr);
	*pobject = nv_object(gr);
	if (ret)
		return ret;

	ret = nvkm_memory_new(device, NVKM_MEM_TARGET_INST, 32 * 4, 16, true,
			      &gr->ctxtab);
	if (ret)
		return ret;

	nv_subdev(gr)->unit = 0x00001000;
	nv_subdev(gr)->intr = nv20_gr_intr;
	nv_engine(gr)->cclass = &nv20_gr_cclass;
	nv_engine(gr)->sclass = nv20_gr_sclass;
	nv_engine(gr)->tile_prog = nv20_gr_tile_prog;
	return 0;
}

void
nv20_gr_dtor(struct nvkm_object *object)
{
	struct nv20_gr *gr = (void *)object;
	nvkm_memory_del(&gr->ctxtab);
	nvkm_gr_destroy(&gr->base);
}

int
nv20_gr_init(struct nvkm_object *object)
{
	struct nvkm_engine *engine = nv_engine(object);
	struct nv20_gr *gr = (void *)engine;
	struct nvkm_device *device = gr->base.engine.subdev.device;
	struct nvkm_fb *fb = device->fb;
	u32 tmp, vramsz;
	int ret, i;

	ret = nvkm_gr_init(&gr->base);
	if (ret)
		return ret;

	nvkm_wr32(device, NV20_PGRAPH_CHANNEL_CTX_TABLE,
			  nvkm_memory_addr(gr->ctxtab) >> 4);

	if (nv_device(gr)->chipset == 0x20) {
		nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x003d0000);
		for (i = 0; i < 15; i++)
			nvkm_wr32(device, NV10_PGRAPH_RDI_DATA, 0x00000000);
		nvkm_msec(device, 2000,
			if (!nvkm_rd32(device, 0x400700))
				break;
		);
	} else {
		nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x02c80000);
		for (i = 0; i < 32; i++)
			nvkm_wr32(device, NV10_PGRAPH_RDI_DATA, 0x00000000);
		nvkm_msec(device, 2000,
			if (!nvkm_rd32(device, 0x400700))
				break;
		);
	}

	nvkm_wr32(device, NV03_PGRAPH_INTR   , 0xFFFFFFFF);
	nvkm_wr32(device, NV03_PGRAPH_INTR_EN, 0xFFFFFFFF);

	nvkm_wr32(device, NV04_PGRAPH_DEBUG_0, 0xFFFFFFFF);
	nvkm_wr32(device, NV04_PGRAPH_DEBUG_0, 0x00000000);
	nvkm_wr32(device, NV04_PGRAPH_DEBUG_1, 0x00118700);
	nvkm_wr32(device, NV04_PGRAPH_DEBUG_3, 0xF3CE0475); /* 0x4 = auto ctx switch */
	nvkm_wr32(device, NV10_PGRAPH_DEBUG_4, 0x00000000);
	nvkm_wr32(device, 0x40009C           , 0x00000040);

	if (nv_device(gr)->chipset >= 0x25) {
		nvkm_wr32(device, 0x400890, 0x00a8cfff);
		nvkm_wr32(device, 0x400610, 0x304B1FB6);
		nvkm_wr32(device, 0x400B80, 0x1cbd3883);
		nvkm_wr32(device, 0x400B84, 0x44000000);
		nvkm_wr32(device, 0x400098, 0x40000080);
		nvkm_wr32(device, 0x400B88, 0x000000ff);

	} else {
		nvkm_wr32(device, 0x400880, 0x0008c7df);
		nvkm_wr32(device, 0x400094, 0x00000005);
		nvkm_wr32(device, 0x400B80, 0x45eae20e);
		nvkm_wr32(device, 0x400B84, 0x24000000);
		nvkm_wr32(device, 0x400098, 0x00000040);
		nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00E00038);
		nvkm_wr32(device, NV10_PGRAPH_RDI_DATA , 0x00000030);
		nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00E10038);
		nvkm_wr32(device, NV10_PGRAPH_RDI_DATA , 0x00000030);
	}

	/* Turn all the tiling regions off. */
	for (i = 0; i < fb->tile.regions; i++)
		engine->tile_prog(engine, i);

	nvkm_wr32(device, 0x4009a0, nvkm_rd32(device, 0x100324));
	nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00EA000C);
	nvkm_wr32(device, NV10_PGRAPH_RDI_DATA, nvkm_rd32(device, 0x100324));

	nvkm_wr32(device, NV10_PGRAPH_CTX_CONTROL, 0x10000100);
	nvkm_wr32(device, NV10_PGRAPH_STATE      , 0xFFFFFFFF);

	tmp = nvkm_rd32(device, NV10_PGRAPH_SURFACE) & 0x0007ff00;
	nvkm_wr32(device, NV10_PGRAPH_SURFACE, tmp);
	tmp = nvkm_rd32(device, NV10_PGRAPH_SURFACE) | 0x00020100;
	nvkm_wr32(device, NV10_PGRAPH_SURFACE, tmp);

	/* begin RAM config */
	vramsz = nv_device_resource_len(nv_device(gr), 1) - 1;
	nvkm_wr32(device, 0x4009A4, nvkm_rd32(device, 0x100200));
	nvkm_wr32(device, 0x4009A8, nvkm_rd32(device, 0x100204));
	nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00EA0000);
	nvkm_wr32(device, NV10_PGRAPH_RDI_DATA , nvkm_rd32(device, 0x100200));
	nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00EA0004);
	nvkm_wr32(device, NV10_PGRAPH_RDI_DATA , nvkm_rd32(device, 0x100204));
	nvkm_wr32(device, 0x400820, 0);
	nvkm_wr32(device, 0x400824, 0);
	nvkm_wr32(device, 0x400864, vramsz - 1);
	nvkm_wr32(device, 0x400868, vramsz - 1);

	/* interesting.. the below overwrites some of the tile setup above.. */
	nvkm_wr32(device, 0x400B20, 0x00000000);
	nvkm_wr32(device, 0x400B04, 0xFFFFFFFF);

	nvkm_wr32(device, NV03_PGRAPH_ABS_UCLIP_XMIN, 0);
	nvkm_wr32(device, NV03_PGRAPH_ABS_UCLIP_YMIN, 0);
	nvkm_wr32(device, NV03_PGRAPH_ABS_UCLIP_XMAX, 0x7fff);
	nvkm_wr32(device, NV03_PGRAPH_ABS_UCLIP_YMAX, 0x7fff);
	return 0;
}

struct nvkm_oclass
nv20_gr_oclass = {
	.handle = NV_ENGINE(GR, 0x20),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv20_gr_ctor,
		.dtor = nv20_gr_dtor,
		.init = nv20_gr_init,
		.fini = _nvkm_gr_fini,
	},
};
