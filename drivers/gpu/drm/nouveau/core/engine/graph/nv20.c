#include <core/client.h>
#include <core/os.h>
#include <core/class.h>
#include <core/engctx.h>
#include <core/handle.h>
#include <core/enum.h>

#include <subdev/timer.h>
#include <subdev/fb.h>

#include <engine/graph.h>
#include <engine/fifo.h>

#include "nv20.h"
#include "regs.h"

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

static struct nouveau_oclass
nv20_graph_sclass[] = {
	{ 0x0012, &nv04_graph_ofuncs, NULL }, /* beta1 */
	{ 0x0019, &nv04_graph_ofuncs, NULL }, /* clip */
	{ 0x0030, &nv04_graph_ofuncs, NULL }, /* null */
	{ 0x0039, &nv04_graph_ofuncs, NULL }, /* m2mf */
	{ 0x0043, &nv04_graph_ofuncs, NULL }, /* rop */
	{ 0x0044, &nv04_graph_ofuncs, NULL }, /* patt */
	{ 0x004a, &nv04_graph_ofuncs, NULL }, /* gdi */
	{ 0x0062, &nv04_graph_ofuncs, NULL }, /* surf2d */
	{ 0x0072, &nv04_graph_ofuncs, NULL }, /* beta4 */
	{ 0x0089, &nv04_graph_ofuncs, NULL }, /* sifm */
	{ 0x008a, &nv04_graph_ofuncs, NULL }, /* ifc */
	{ 0x0096, &nv04_graph_ofuncs, NULL }, /* celcius */
	{ 0x0097, &nv04_graph_ofuncs, NULL }, /* kelvin */
	{ 0x009e, &nv04_graph_ofuncs, NULL }, /* swzsurf */
	{ 0x009f, &nv04_graph_ofuncs, NULL }, /* imageblit */
	{},
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv20_graph_context_ctor(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *data, u32 size,
			struct nouveau_object **pobject)
{
	struct nv20_graph_chan *chan;
	int ret, i;

	ret = nouveau_graph_context_create(parent, engine, oclass, NULL,
					   0x37f0, 16, NVOBJ_FLAG_ZERO_ALLOC,
					   &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	chan->chid = nouveau_fifo_chan(parent)->chid;

	nv_wo32(chan, 0x0000, 0x00000001 | (chan->chid << 24));
	nv_wo32(chan, 0x033c, 0xffff0000);
	nv_wo32(chan, 0x03a0, 0x0fff0000);
	nv_wo32(chan, 0x03a4, 0x0fff0000);
	nv_wo32(chan, 0x047c, 0x00000101);
	nv_wo32(chan, 0x0490, 0x00000111);
	nv_wo32(chan, 0x04a8, 0x44400000);
	for (i = 0x04d4; i <= 0x04e0; i += 4)
		nv_wo32(chan, i, 0x00030303);
	for (i = 0x04f4; i <= 0x0500; i += 4)
		nv_wo32(chan, i, 0x00080000);
	for (i = 0x050c; i <= 0x0518; i += 4)
		nv_wo32(chan, i, 0x01012000);
	for (i = 0x051c; i <= 0x0528; i += 4)
		nv_wo32(chan, i, 0x000105b8);
	for (i = 0x052c; i <= 0x0538; i += 4)
		nv_wo32(chan, i, 0x00080008);
	for (i = 0x055c; i <= 0x0598; i += 4)
		nv_wo32(chan, i, 0x07ff0000);
	nv_wo32(chan, 0x05a4, 0x4b7fffff);
	nv_wo32(chan, 0x05fc, 0x00000001);
	nv_wo32(chan, 0x0604, 0x00004000);
	nv_wo32(chan, 0x0610, 0x00000001);
	nv_wo32(chan, 0x0618, 0x00040000);
	nv_wo32(chan, 0x061c, 0x00010000);
	for (i = 0x1c1c; i <= 0x248c; i += 16) {
		nv_wo32(chan, (i + 0), 0x10700ff9);
		nv_wo32(chan, (i + 4), 0x0436086c);
		nv_wo32(chan, (i + 8), 0x000c001b);
	}
	nv_wo32(chan, 0x281c, 0x3f800000);
	nv_wo32(chan, 0x2830, 0x3f800000);
	nv_wo32(chan, 0x285c, 0x40000000);
	nv_wo32(chan, 0x2860, 0x3f800000);
	nv_wo32(chan, 0x2864, 0x3f000000);
	nv_wo32(chan, 0x286c, 0x40000000);
	nv_wo32(chan, 0x2870, 0x3f800000);
	nv_wo32(chan, 0x2878, 0xbf800000);
	nv_wo32(chan, 0x2880, 0xbf800000);
	nv_wo32(chan, 0x34a4, 0x000fe000);
	nv_wo32(chan, 0x3530, 0x000003f8);
	nv_wo32(chan, 0x3540, 0x002fe000);
	for (i = 0x355c; i <= 0x3578; i += 4)
		nv_wo32(chan, i, 0x001c527c);
	return 0;
}

int
nv20_graph_context_init(struct nouveau_object *object)
{
	struct nv20_graph_priv *priv = (void *)object->engine;
	struct nv20_graph_chan *chan = (void *)object;
	int ret;

	ret = nouveau_graph_context_init(&chan->base);
	if (ret)
		return ret;

	nv_wo32(priv->ctxtab, chan->chid * 4, nv_gpuobj(chan)->addr >> 4);
	return 0;
}

int
nv20_graph_context_fini(struct nouveau_object *object, bool suspend)
{
	struct nv20_graph_priv *priv = (void *)object->engine;
	struct nv20_graph_chan *chan = (void *)object;
	int chid = -1;

	nv_mask(priv, 0x400720, 0x00000001, 0x00000000);
	if (nv_rd32(priv, 0x400144) & 0x00010000)
		chid = (nv_rd32(priv, 0x400148) & 0x1f000000) >> 24;
	if (chan->chid == chid) {
		nv_wr32(priv, 0x400784, nv_gpuobj(chan)->addr >> 4);
		nv_wr32(priv, 0x400788, 0x00000002);
		nv_wait(priv, 0x400700, 0xffffffff, 0x00000000);
		nv_wr32(priv, 0x400144, 0x10000000);
		nv_mask(priv, 0x400148, 0xff000000, 0x1f000000);
	}
	nv_mask(priv, 0x400720, 0x00000001, 0x00000001);

	nv_wo32(priv->ctxtab, chan->chid * 4, 0x00000000);
	return nouveau_graph_context_fini(&chan->base, suspend);
}

static struct nouveau_oclass
nv20_graph_cclass = {
	.handle = NV_ENGCTX(GR, 0x20),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv20_graph_context_ctor,
		.dtor = _nouveau_graph_context_dtor,
		.init = nv20_graph_context_init,
		.fini = nv20_graph_context_fini,
		.rd32 = _nouveau_graph_context_rd32,
		.wr32 = _nouveau_graph_context_wr32,
	},
};

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

void
nv20_graph_tile_prog(struct nouveau_engine *engine, int i)
{
	struct nouveau_fb_tile *tile = &nouveau_fb(engine)->tile.region[i];
	struct nouveau_fifo *pfifo = nouveau_fifo(engine);
	struct nv20_graph_priv *priv = (void *)engine;
	unsigned long flags;

	pfifo->pause(pfifo, &flags);
	nv04_graph_idle(priv);

	nv_wr32(priv, NV20_PGRAPH_TLIMIT(i), tile->limit);
	nv_wr32(priv, NV20_PGRAPH_TSIZE(i), tile->pitch);
	nv_wr32(priv, NV20_PGRAPH_TILE(i), tile->addr);

	nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x00EA0030 + 4 * i);
	nv_wr32(priv, NV10_PGRAPH_RDI_DATA, tile->limit);
	nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x00EA0050 + 4 * i);
	nv_wr32(priv, NV10_PGRAPH_RDI_DATA, tile->pitch);
	nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x00EA0010 + 4 * i);
	nv_wr32(priv, NV10_PGRAPH_RDI_DATA, tile->addr);

	if (nv_device(engine)->chipset != 0x34) {
		nv_wr32(priv, NV20_PGRAPH_ZCOMP(i), tile->zcomp);
		nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x00ea0090 + 4 * i);
		nv_wr32(priv, NV10_PGRAPH_RDI_DATA, tile->zcomp);
	}

	pfifo->start(pfifo, &flags);
}

void
nv20_graph_intr(struct nouveau_subdev *subdev)
{
	struct nouveau_engine *engine = nv_engine(subdev);
	struct nouveau_object *engctx;
	struct nouveau_handle *handle;
	struct nv20_graph_priv *priv = (void *)subdev;
	u32 stat = nv_rd32(priv, NV03_PGRAPH_INTR);
	u32 nsource = nv_rd32(priv, NV03_PGRAPH_NSOURCE);
	u32 nstatus = nv_rd32(priv, NV03_PGRAPH_NSTATUS);
	u32 addr = nv_rd32(priv, NV04_PGRAPH_TRAPPED_ADDR);
	u32 chid = (addr & 0x01f00000) >> 20;
	u32 subc = (addr & 0x00070000) >> 16;
	u32 mthd = (addr & 0x00001ffc);
	u32 data = nv_rd32(priv, NV04_PGRAPH_TRAPPED_DATA);
	u32 class = nv_rd32(priv, 0x400160 + subc * 4) & 0xfff;
	u32 show = stat;

	engctx = nouveau_engctx_get(engine, chid);
	if (stat & NV_PGRAPH_INTR_ERROR) {
		if (nsource & NV03_PGRAPH_NSOURCE_ILLEGAL_MTHD) {
			handle = nouveau_handle_get_class(engctx, class);
			if (handle && !nv_call(handle->object, mthd, data))
				show &= ~NV_PGRAPH_INTR_ERROR;
			nouveau_handle_put(handle);
		}
	}

	nv_wr32(priv, NV03_PGRAPH_INTR, stat);
	nv_wr32(priv, NV04_PGRAPH_FIFO, 0x00000001);

	if (show) {
		nv_error(priv, "%s", "");
		nouveau_bitfield_print(nv10_graph_intr_name, show);
		pr_cont(" nsource:");
		nouveau_bitfield_print(nv04_graph_nsource, nsource);
		pr_cont(" nstatus:");
		nouveau_bitfield_print(nv10_graph_nstatus, nstatus);
		pr_cont("\n");
		nv_error(priv,
			 "ch %d [%s] subc %d class 0x%04x mthd 0x%04x data 0x%08x\n",
			 chid, nouveau_client_name(engctx), subc, class, mthd,
			 data);
	}

	nouveau_engctx_put(engctx);
}

static int
nv20_graph_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv20_graph_priv *priv;
	int ret;

	ret = nouveau_graph_create(parent, engine, oclass, true, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(nv_object(priv), NULL, 32 * 4, 16,
				 NVOBJ_FLAG_ZERO_ALLOC, &priv->ctxtab);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00001000;
	nv_subdev(priv)->intr = nv20_graph_intr;
	nv_engine(priv)->cclass = &nv20_graph_cclass;
	nv_engine(priv)->sclass = nv20_graph_sclass;
	nv_engine(priv)->tile_prog = nv20_graph_tile_prog;
	return 0;
}

void
nv20_graph_dtor(struct nouveau_object *object)
{
	struct nv20_graph_priv *priv = (void *)object;
	nouveau_gpuobj_ref(NULL, &priv->ctxtab);
	nouveau_graph_destroy(&priv->base);
}

int
nv20_graph_init(struct nouveau_object *object)
{
	struct nouveau_engine *engine = nv_engine(object);
	struct nv20_graph_priv *priv = (void *)engine;
	struct nouveau_fb *pfb = nouveau_fb(object);
	u32 tmp, vramsz;
	int ret, i;

	ret = nouveau_graph_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, NV20_PGRAPH_CHANNEL_CTX_TABLE, priv->ctxtab->addr >> 4);

	if (nv_device(priv)->chipset == 0x20) {
		nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x003d0000);
		for (i = 0; i < 15; i++)
			nv_wr32(priv, NV10_PGRAPH_RDI_DATA, 0x00000000);
		nv_wait(priv, 0x400700, 0xffffffff, 0x00000000);
	} else {
		nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x02c80000);
		for (i = 0; i < 32; i++)
			nv_wr32(priv, NV10_PGRAPH_RDI_DATA, 0x00000000);
		nv_wait(priv, 0x400700, 0xffffffff, 0x00000000);
	}

	nv_wr32(priv, NV03_PGRAPH_INTR   , 0xFFFFFFFF);
	nv_wr32(priv, NV03_PGRAPH_INTR_EN, 0xFFFFFFFF);

	nv_wr32(priv, NV04_PGRAPH_DEBUG_0, 0xFFFFFFFF);
	nv_wr32(priv, NV04_PGRAPH_DEBUG_0, 0x00000000);
	nv_wr32(priv, NV04_PGRAPH_DEBUG_1, 0x00118700);
	nv_wr32(priv, NV04_PGRAPH_DEBUG_3, 0xF3CE0475); /* 0x4 = auto ctx switch */
	nv_wr32(priv, NV10_PGRAPH_DEBUG_4, 0x00000000);
	nv_wr32(priv, 0x40009C           , 0x00000040);

	if (nv_device(priv)->chipset >= 0x25) {
		nv_wr32(priv, 0x400890, 0x00a8cfff);
		nv_wr32(priv, 0x400610, 0x304B1FB6);
		nv_wr32(priv, 0x400B80, 0x1cbd3883);
		nv_wr32(priv, 0x400B84, 0x44000000);
		nv_wr32(priv, 0x400098, 0x40000080);
		nv_wr32(priv, 0x400B88, 0x000000ff);

	} else {
		nv_wr32(priv, 0x400880, 0x0008c7df);
		nv_wr32(priv, 0x400094, 0x00000005);
		nv_wr32(priv, 0x400B80, 0x45eae20e);
		nv_wr32(priv, 0x400B84, 0x24000000);
		nv_wr32(priv, 0x400098, 0x00000040);
		nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x00E00038);
		nv_wr32(priv, NV10_PGRAPH_RDI_DATA , 0x00000030);
		nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x00E10038);
		nv_wr32(priv, NV10_PGRAPH_RDI_DATA , 0x00000030);
	}

	/* Turn all the tiling regions off. */
	for (i = 0; i < pfb->tile.regions; i++)
		engine->tile_prog(engine, i);

	nv_wr32(priv, 0x4009a0, nv_rd32(priv, 0x100324));
	nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x00EA000C);
	nv_wr32(priv, NV10_PGRAPH_RDI_DATA, nv_rd32(priv, 0x100324));

	nv_wr32(priv, NV10_PGRAPH_CTX_CONTROL, 0x10000100);
	nv_wr32(priv, NV10_PGRAPH_STATE      , 0xFFFFFFFF);

	tmp = nv_rd32(priv, NV10_PGRAPH_SURFACE) & 0x0007ff00;
	nv_wr32(priv, NV10_PGRAPH_SURFACE, tmp);
	tmp = nv_rd32(priv, NV10_PGRAPH_SURFACE) | 0x00020100;
	nv_wr32(priv, NV10_PGRAPH_SURFACE, tmp);

	/* begin RAM config */
	vramsz = nv_device_resource_len(nv_device(priv), 0) - 1;
	nv_wr32(priv, 0x4009A4, nv_rd32(priv, 0x100200));
	nv_wr32(priv, 0x4009A8, nv_rd32(priv, 0x100204));
	nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x00EA0000);
	nv_wr32(priv, NV10_PGRAPH_RDI_DATA , nv_rd32(priv, 0x100200));
	nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x00EA0004);
	nv_wr32(priv, NV10_PGRAPH_RDI_DATA , nv_rd32(priv, 0x100204));
	nv_wr32(priv, 0x400820, 0);
	nv_wr32(priv, 0x400824, 0);
	nv_wr32(priv, 0x400864, vramsz - 1);
	nv_wr32(priv, 0x400868, vramsz - 1);

	/* interesting.. the below overwrites some of the tile setup above.. */
	nv_wr32(priv, 0x400B20, 0x00000000);
	nv_wr32(priv, 0x400B04, 0xFFFFFFFF);

	nv_wr32(priv, NV03_PGRAPH_ABS_UCLIP_XMIN, 0);
	nv_wr32(priv, NV03_PGRAPH_ABS_UCLIP_YMIN, 0);
	nv_wr32(priv, NV03_PGRAPH_ABS_UCLIP_XMAX, 0x7fff);
	nv_wr32(priv, NV03_PGRAPH_ABS_UCLIP_YMAX, 0x7fff);
	return 0;
}

struct nouveau_oclass
nv20_graph_oclass = {
	.handle = NV_ENGINE(GR, 0x20),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv20_graph_ctor,
		.dtor = nv20_graph_dtor,
		.init = nv20_graph_init,
		.fini = _nouveau_graph_fini,
	},
};
