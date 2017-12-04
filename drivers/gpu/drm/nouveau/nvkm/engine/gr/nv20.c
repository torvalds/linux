// SPDX-License-Identifier: GPL-2.0
#include "nv20.h"
#include "regs.h"

#include <core/client.h>
#include <core/gpuobj.h>
#include <engine/fifo.h>
#include <engine/fifo/chan.h>
#include <subdev/fb.h>
#include <subdev/timer.h>

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

int
nv20_gr_chan_init(struct nvkm_object *object)
{
	struct nv20_gr_chan *chan = nv20_gr_chan(object);
	struct nv20_gr *gr = chan->gr;
	u32 inst = nvkm_memory_addr(chan->inst);

	nvkm_kmap(gr->ctxtab);
	nvkm_wo32(gr->ctxtab, chan->chid * 4, inst >> 4);
	nvkm_done(gr->ctxtab);
	return 0;
}

int
nv20_gr_chan_fini(struct nvkm_object *object, bool suspend)
{
	struct nv20_gr_chan *chan = nv20_gr_chan(object);
	struct nv20_gr *gr = chan->gr;
	struct nvkm_device *device = gr->base.engine.subdev.device;
	u32 inst = nvkm_memory_addr(chan->inst);
	int chid = -1;

	nvkm_mask(device, 0x400720, 0x00000001, 0x00000000);
	if (nvkm_rd32(device, 0x400144) & 0x00010000)
		chid = (nvkm_rd32(device, 0x400148) & 0x1f000000) >> 24;
	if (chan->chid == chid) {
		nvkm_wr32(device, 0x400784, inst >> 4);
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
	return 0;
}

void *
nv20_gr_chan_dtor(struct nvkm_object *object)
{
	struct nv20_gr_chan *chan = nv20_gr_chan(object);
	nvkm_memory_unref(&chan->inst);
	return chan;
}

static const struct nvkm_object_func
nv20_gr_chan = {
	.dtor = nv20_gr_chan_dtor,
	.init = nv20_gr_chan_init,
	.fini = nv20_gr_chan_fini,
};

static int
nv20_gr_chan_new(struct nvkm_gr *base, struct nvkm_fifo_chan *fifoch,
		 const struct nvkm_oclass *oclass, struct nvkm_object **pobject)
{
	struct nv20_gr *gr = nv20_gr(base);
	struct nv20_gr_chan *chan;
	int ret, i;

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_object_ctor(&nv20_gr_chan, oclass, &chan->object);
	chan->gr = gr;
	chan->chid = fifoch->chid;
	*pobject = &chan->object;

	ret = nvkm_memory_new(gr->base.engine.subdev.device,
			      NVKM_MEM_TARGET_INST, 0x37f0, 16, true,
			      &chan->inst);
	if (ret)
		return ret;

	nvkm_kmap(chan->inst);
	nvkm_wo32(chan->inst, 0x0000, 0x00000001 | (chan->chid << 24));
	nvkm_wo32(chan->inst, 0x033c, 0xffff0000);
	nvkm_wo32(chan->inst, 0x03a0, 0x0fff0000);
	nvkm_wo32(chan->inst, 0x03a4, 0x0fff0000);
	nvkm_wo32(chan->inst, 0x047c, 0x00000101);
	nvkm_wo32(chan->inst, 0x0490, 0x00000111);
	nvkm_wo32(chan->inst, 0x04a8, 0x44400000);
	for (i = 0x04d4; i <= 0x04e0; i += 4)
		nvkm_wo32(chan->inst, i, 0x00030303);
	for (i = 0x04f4; i <= 0x0500; i += 4)
		nvkm_wo32(chan->inst, i, 0x00080000);
	for (i = 0x050c; i <= 0x0518; i += 4)
		nvkm_wo32(chan->inst, i, 0x01012000);
	for (i = 0x051c; i <= 0x0528; i += 4)
		nvkm_wo32(chan->inst, i, 0x000105b8);
	for (i = 0x052c; i <= 0x0538; i += 4)
		nvkm_wo32(chan->inst, i, 0x00080008);
	for (i = 0x055c; i <= 0x0598; i += 4)
		nvkm_wo32(chan->inst, i, 0x07ff0000);
	nvkm_wo32(chan->inst, 0x05a4, 0x4b7fffff);
	nvkm_wo32(chan->inst, 0x05fc, 0x00000001);
	nvkm_wo32(chan->inst, 0x0604, 0x00004000);
	nvkm_wo32(chan->inst, 0x0610, 0x00000001);
	nvkm_wo32(chan->inst, 0x0618, 0x00040000);
	nvkm_wo32(chan->inst, 0x061c, 0x00010000);
	for (i = 0x1c1c; i <= 0x248c; i += 16) {
		nvkm_wo32(chan->inst, (i + 0), 0x10700ff9);
		nvkm_wo32(chan->inst, (i + 4), 0x0436086c);
		nvkm_wo32(chan->inst, (i + 8), 0x000c001b);
	}
	nvkm_wo32(chan->inst, 0x281c, 0x3f800000);
	nvkm_wo32(chan->inst, 0x2830, 0x3f800000);
	nvkm_wo32(chan->inst, 0x285c, 0x40000000);
	nvkm_wo32(chan->inst, 0x2860, 0x3f800000);
	nvkm_wo32(chan->inst, 0x2864, 0x3f000000);
	nvkm_wo32(chan->inst, 0x286c, 0x40000000);
	nvkm_wo32(chan->inst, 0x2870, 0x3f800000);
	nvkm_wo32(chan->inst, 0x2878, 0xbf800000);
	nvkm_wo32(chan->inst, 0x2880, 0xbf800000);
	nvkm_wo32(chan->inst, 0x34a4, 0x000fe000);
	nvkm_wo32(chan->inst, 0x3530, 0x000003f8);
	nvkm_wo32(chan->inst, 0x3540, 0x002fe000);
	for (i = 0x355c; i <= 0x3578; i += 4)
		nvkm_wo32(chan->inst, i, 0x001c527c);
	nvkm_done(chan->inst);
	return 0;
}

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

void
nv20_gr_tile(struct nvkm_gr *base, int i, struct nvkm_fb_tile *tile)
{
	struct nv20_gr *gr = nv20_gr(base);
	struct nvkm_device *device = gr->base.engine.subdev.device;
	struct nvkm_fifo *fifo = device->fifo;
	unsigned long flags;

	nvkm_fifo_pause(fifo, &flags);
	nv04_gr_idle(&gr->base);

	nvkm_wr32(device, NV20_PGRAPH_TLIMIT(i), tile->limit);
	nvkm_wr32(device, NV20_PGRAPH_TSIZE(i), tile->pitch);
	nvkm_wr32(device, NV20_PGRAPH_TILE(i), tile->addr);

	nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00EA0030 + 4 * i);
	nvkm_wr32(device, NV10_PGRAPH_RDI_DATA, tile->limit);
	nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00EA0050 + 4 * i);
	nvkm_wr32(device, NV10_PGRAPH_RDI_DATA, tile->pitch);
	nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00EA0010 + 4 * i);
	nvkm_wr32(device, NV10_PGRAPH_RDI_DATA, tile->addr);

	if (device->chipset != 0x34) {
		nvkm_wr32(device, NV20_PGRAPH_ZCOMP(i), tile->zcomp);
		nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00ea0090 + 4 * i);
		nvkm_wr32(device, NV10_PGRAPH_RDI_DATA, tile->zcomp);
	}

	nvkm_fifo_start(fifo, &flags);
}

void
nv20_gr_intr(struct nvkm_gr *base)
{
	struct nv20_gr *gr = nv20_gr(base);
	struct nvkm_subdev *subdev = &gr->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
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
			   chan ? chan->object.client->name : "unknown",
			   subc, class, mthd, data);
	}

	nvkm_fifo_chan_put(device->fifo, flags, &chan);
}

int
nv20_gr_oneinit(struct nvkm_gr *base)
{
	struct nv20_gr *gr = nv20_gr(base);
	return nvkm_memory_new(gr->base.engine.subdev.device,
			       NVKM_MEM_TARGET_INST, 32 * 4, 16,
			       true, &gr->ctxtab);
}

int
nv20_gr_init(struct nvkm_gr *base)
{
	struct nv20_gr *gr = nv20_gr(base);
	struct nvkm_device *device = gr->base.engine.subdev.device;
	u32 tmp, vramsz;
	int i;

	nvkm_wr32(device, NV20_PGRAPH_CHANNEL_CTX_TABLE,
			  nvkm_memory_addr(gr->ctxtab) >> 4);

	if (device->chipset == 0x20) {
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

	if (device->chipset >= 0x25) {
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
	vramsz = device->func->resource_size(device, 1) - 1;
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

void *
nv20_gr_dtor(struct nvkm_gr *base)
{
	struct nv20_gr *gr = nv20_gr(base);
	nvkm_memory_unref(&gr->ctxtab);
	return gr;
}

int
nv20_gr_new_(const struct nvkm_gr_func *func, struct nvkm_device *device,
	     int index, struct nvkm_gr **pgr)
{
	struct nv20_gr *gr;

	if (!(gr = kzalloc(sizeof(*gr), GFP_KERNEL)))
		return -ENOMEM;
	*pgr = &gr->base;

	return nvkm_gr_ctor(func, device, index, true, &gr->base);
}

static const struct nvkm_gr_func
nv20_gr = {
	.dtor = nv20_gr_dtor,
	.oneinit = nv20_gr_oneinit,
	.init = nv20_gr_init,
	.intr = nv20_gr_intr,
	.tile = nv20_gr_tile,
	.chan_new = nv20_gr_chan_new,
	.sclass = {
		{ -1, -1, 0x0012, &nv04_gr_object }, /* beta1 */
		{ -1, -1, 0x0019, &nv04_gr_object }, /* clip */
		{ -1, -1, 0x0030, &nv04_gr_object }, /* null */
		{ -1, -1, 0x0039, &nv04_gr_object }, /* m2mf */
		{ -1, -1, 0x0043, &nv04_gr_object }, /* rop */
		{ -1, -1, 0x0044, &nv04_gr_object }, /* patt */
		{ -1, -1, 0x004a, &nv04_gr_object }, /* gdi */
		{ -1, -1, 0x0062, &nv04_gr_object }, /* surf2d */
		{ -1, -1, 0x0072, &nv04_gr_object }, /* beta4 */
		{ -1, -1, 0x0089, &nv04_gr_object }, /* sifm */
		{ -1, -1, 0x008a, &nv04_gr_object }, /* ifc */
		{ -1, -1, 0x0096, &nv04_gr_object }, /* celcius */
		{ -1, -1, 0x0097, &nv04_gr_object }, /* kelvin */
		{ -1, -1, 0x009e, &nv04_gr_object }, /* swzsurf */
		{ -1, -1, 0x009f, &nv04_gr_object }, /* imageblit */
		{}
	}
};

int
nv20_gr_new(struct nvkm_device *device, int index, struct nvkm_gr **pgr)
{
	return nv20_gr_new_(&nv20_gr, device, index, pgr);
}
