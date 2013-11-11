#include <core/os.h>
#include <core/class.h>
#include <core/engctx.h>
#include <core/enum.h>

#include <subdev/timer.h>
#include <subdev/fb.h>

#include <engine/graph.h>

#include "nv20.h"
#include "regs.h"

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv2a_graph_context_ctor(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *data, u32 size,
			struct nouveau_object **pobject)
{
	struct nv20_graph_chan *chan;
	int ret, i;

	ret = nouveau_graph_context_create(parent, engine, oclass, NULL, 0x36b0,
					   16, NVOBJ_FLAG_ZERO_ALLOC, &chan);
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
	for (i = 0x1a9c; i <= 0x22fc; i += 16) { /*XXX: check!! */
		nv_wo32(chan, (i + 0), 0x10700ff9);
		nv_wo32(chan, (i + 4), 0x0436086c);
		nv_wo32(chan, (i + 8), 0x000c001b);
	}
	nv_wo32(chan, 0x269c, 0x3f800000);
	nv_wo32(chan, 0x26b0, 0x3f800000);
	nv_wo32(chan, 0x26dc, 0x40000000);
	nv_wo32(chan, 0x26e0, 0x3f800000);
	nv_wo32(chan, 0x26e4, 0x3f000000);
	nv_wo32(chan, 0x26ec, 0x40000000);
	nv_wo32(chan, 0x26f0, 0x3f800000);
	nv_wo32(chan, 0x26f8, 0xbf800000);
	nv_wo32(chan, 0x2700, 0xbf800000);
	nv_wo32(chan, 0x3024, 0x000fe000);
	nv_wo32(chan, 0x30a0, 0x000003f8);
	nv_wo32(chan, 0x33fc, 0x002fe000);
	for (i = 0x341c; i <= 0x3438; i += 4)
		nv_wo32(chan, i, 0x001c527c);
	return 0;
}

static struct nouveau_oclass
nv2a_graph_cclass = {
	.handle = NV_ENGCTX(GR, 0x2a),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv2a_graph_context_ctor,
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

static int
nv2a_graph_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
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
	nv_engine(priv)->cclass = &nv2a_graph_cclass;
	nv_engine(priv)->sclass = nv25_graph_sclass;
	nv_engine(priv)->tile_prog = nv20_graph_tile_prog;
	return 0;
}

struct nouveau_oclass
nv2a_graph_oclass = {
	.handle = NV_ENGINE(GR, 0x2a),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv2a_graph_ctor,
		.dtor = nv20_graph_dtor,
		.init = nv20_graph_init,
		.fini = _nouveau_graph_fini,
	},
};
