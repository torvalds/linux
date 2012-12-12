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
 * Graphics object classes
 ******************************************************************************/

struct nouveau_oclass
nv25_graph_sclass[] = {
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
	{ 0x009e, &nv04_graph_ofuncs, NULL }, /* swzsurf */
	{ 0x009f, &nv04_graph_ofuncs, NULL }, /* imageblit */
	{ 0x0597, &nv04_graph_ofuncs, NULL }, /* kelvin */
	{},
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv25_graph_context_ctor(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *data, u32 size,
			struct nouveau_object **pobject)
{
	struct nv20_graph_chan *chan;
	int ret, i;

	ret = nouveau_graph_context_create(parent, engine, oclass, NULL, 0x3724,
					   16, NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	chan->chid = nouveau_fifo_chan(parent)->chid;

	nv_wo32(chan, 0x0028, 0x00000001 | (chan->chid << 24));
	nv_wo32(chan, 0x035c, 0xffff0000);
	nv_wo32(chan, 0x03c0, 0x0fff0000);
	nv_wo32(chan, 0x03c4, 0x0fff0000);
	nv_wo32(chan, 0x049c, 0x00000101);
	nv_wo32(chan, 0x04b0, 0x00000111);
	nv_wo32(chan, 0x04c8, 0x00000080);
	nv_wo32(chan, 0x04cc, 0xffff0000);
	nv_wo32(chan, 0x04d0, 0x00000001);
	nv_wo32(chan, 0x04e4, 0x44400000);
	nv_wo32(chan, 0x04fc, 0x4b800000);
	for (i = 0x0510; i <= 0x051c; i += 4)
		nv_wo32(chan, i, 0x00030303);
	for (i = 0x0530; i <= 0x053c; i += 4)
		nv_wo32(chan, i, 0x00080000);
	for (i = 0x0548; i <= 0x0554; i += 4)
		nv_wo32(chan, i, 0x01012000);
	for (i = 0x0558; i <= 0x0564; i += 4)
		nv_wo32(chan, i, 0x000105b8);
	for (i = 0x0568; i <= 0x0574; i += 4)
		nv_wo32(chan, i, 0x00080008);
	for (i = 0x0598; i <= 0x05d4; i += 4)
		nv_wo32(chan, i, 0x07ff0000);
	nv_wo32(chan, 0x05e0, 0x4b7fffff);
	nv_wo32(chan, 0x0620, 0x00000080);
	nv_wo32(chan, 0x0624, 0x30201000);
	nv_wo32(chan, 0x0628, 0x70605040);
	nv_wo32(chan, 0x062c, 0xb0a09080);
	nv_wo32(chan, 0x0630, 0xf0e0d0c0);
	nv_wo32(chan, 0x0664, 0x00000001);
	nv_wo32(chan, 0x066c, 0x00004000);
	nv_wo32(chan, 0x0678, 0x00000001);
	nv_wo32(chan, 0x0680, 0x00040000);
	nv_wo32(chan, 0x0684, 0x00010000);
	for (i = 0x1b04; i <= 0x2374; i += 16) {
		nv_wo32(chan, (i + 0), 0x10700ff9);
		nv_wo32(chan, (i + 4), 0x0436086c);
		nv_wo32(chan, (i + 8), 0x000c001b);
	}
	nv_wo32(chan, 0x2704, 0x3f800000);
	nv_wo32(chan, 0x2718, 0x3f800000);
	nv_wo32(chan, 0x2744, 0x40000000);
	nv_wo32(chan, 0x2748, 0x3f800000);
	nv_wo32(chan, 0x274c, 0x3f000000);
	nv_wo32(chan, 0x2754, 0x40000000);
	nv_wo32(chan, 0x2758, 0x3f800000);
	nv_wo32(chan, 0x2760, 0xbf800000);
	nv_wo32(chan, 0x2768, 0xbf800000);
	nv_wo32(chan, 0x308c, 0x000fe000);
	nv_wo32(chan, 0x3108, 0x000003f8);
	nv_wo32(chan, 0x3468, 0x002fe000);
	for (i = 0x3484; i <= 0x34a0; i += 4)
		nv_wo32(chan, i, 0x001c527c);
	return 0;
}

static struct nouveau_oclass
nv25_graph_cclass = {
	.handle = NV_ENGCTX(GR, 0x25),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv25_graph_context_ctor,
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
nv25_graph_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv20_graph_priv *priv;
	int ret;

	ret = nouveau_graph_create(parent, engine, oclass, true, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nouveau_gpuobj_new(parent, NULL, 32 * 4, 16,
				 NVOBJ_FLAG_ZERO_ALLOC, &priv->ctxtab);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00001000;
	nv_subdev(priv)->intr = nv20_graph_intr;
	nv_engine(priv)->cclass = &nv25_graph_cclass;
	nv_engine(priv)->sclass = nv25_graph_sclass;
	nv_engine(priv)->tile_prog = nv20_graph_tile_prog;
	return 0;
}

struct nouveau_oclass
nv25_graph_oclass = {
	.handle = NV_ENGINE(GR, 0x25),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv25_graph_ctor,
		.dtor = nv20_graph_dtor,
		.init = nv20_graph_init,
		.fini = _nouveau_graph_fini,
	},
};
