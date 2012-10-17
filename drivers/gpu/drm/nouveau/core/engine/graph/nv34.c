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

static struct nouveau_oclass
nv34_graph_sclass[] = {
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
	{ 0x009f, &nv04_graph_ofuncs, NULL }, /* imageblit */
	{ 0x0362, &nv04_graph_ofuncs, NULL }, /* surf2d (nv30) */
	{ 0x0389, &nv04_graph_ofuncs, NULL }, /* sifm (nv30) */
	{ 0x038a, &nv04_graph_ofuncs, NULL }, /* ifc (nv30) */
	{ 0x039e, &nv04_graph_ofuncs, NULL }, /* swzsurf (nv30) */
	{ 0x0697, &nv04_graph_ofuncs, NULL }, /* rankine */
	{},
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv34_graph_context_ctor(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *data, u32 size,
			struct nouveau_object **pobject)
{
	struct nv20_graph_chan *chan;
	int ret, i;

	ret = nouveau_graph_context_create(parent, engine, oclass, NULL, 0x46dc,
					   16, NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	chan->chid = nouveau_fifo_chan(parent)->chid;

	nv_wo32(chan, 0x0028, 0x00000001 | (chan->chid << 24));
	nv_wo32(chan, 0x040c, 0x01000101);
	nv_wo32(chan, 0x0420, 0x00000111);
	nv_wo32(chan, 0x0424, 0x00000060);
	nv_wo32(chan, 0x0440, 0x00000080);
	nv_wo32(chan, 0x0444, 0xffff0000);
	nv_wo32(chan, 0x0448, 0x00000001);
	nv_wo32(chan, 0x045c, 0x44400000);
	nv_wo32(chan, 0x0480, 0xffff0000);
	for (i = 0x04d4; i < 0x04dc; i += 4)
		nv_wo32(chan, i, 0x0fff0000);
	nv_wo32(chan, 0x04e0, 0x00011100);
	for (i = 0x04fc; i < 0x053c; i += 4)
		nv_wo32(chan, i, 0x07ff0000);
	nv_wo32(chan, 0x0544, 0x4b7fffff);
	nv_wo32(chan, 0x057c, 0x00000080);
	nv_wo32(chan, 0x0580, 0x30201000);
	nv_wo32(chan, 0x0584, 0x70605040);
	nv_wo32(chan, 0x0588, 0xb8a89888);
	nv_wo32(chan, 0x058c, 0xf8e8d8c8);
	nv_wo32(chan, 0x05a0, 0xb0000000);
	for (i = 0x05f0; i < 0x0630; i += 4)
		nv_wo32(chan, i, 0x00010588);
	for (i = 0x0630; i < 0x0670; i += 4)
		nv_wo32(chan, i, 0x00030303);
	for (i = 0x06b0; i < 0x06f0; i += 4)
		nv_wo32(chan, i, 0x0008aae4);
	for (i = 0x06f0; i < 0x0730; i += 4)
		nv_wo32(chan, i, 0x01012000);
	for (i = 0x0730; i < 0x0770; i += 4)
		nv_wo32(chan, i, 0x00080008);
	nv_wo32(chan, 0x0850, 0x00040000);
	nv_wo32(chan, 0x0854, 0x00010000);
	for (i = 0x0858; i < 0x0868; i += 4)
		nv_wo32(chan, i, 0x00040004);
	for (i = 0x15ac; i <= 0x271c ; i += 16) {
		nv_wo32(chan, i + 0, 0x10700ff9);
		nv_wo32(chan, i + 1, 0x0436086c);
		nv_wo32(chan, i + 2, 0x000c001b);
	}
	for (i = 0x274c; i < 0x275c; i += 4)
		nv_wo32(chan, i, 0x0000ffff);
	nv_wo32(chan, 0x2ae0, 0x3f800000);
	nv_wo32(chan, 0x2e9c, 0x3f800000);
	nv_wo32(chan, 0x2eb0, 0x3f800000);
	nv_wo32(chan, 0x2edc, 0x40000000);
	nv_wo32(chan, 0x2ee0, 0x3f800000);
	nv_wo32(chan, 0x2ee4, 0x3f000000);
	nv_wo32(chan, 0x2eec, 0x40000000);
	nv_wo32(chan, 0x2ef0, 0x3f800000);
	nv_wo32(chan, 0x2ef8, 0xbf800000);
	nv_wo32(chan, 0x2f00, 0xbf800000);
	return 0;
}

static struct nouveau_oclass
nv34_graph_cclass = {
	.handle = NV_ENGCTX(GR, 0x34),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv34_graph_context_ctor,
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
nv34_graph_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
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
	nv_engine(priv)->cclass = &nv34_graph_cclass;
	nv_engine(priv)->sclass = nv34_graph_sclass;
	nv_engine(priv)->tile_prog = nv20_graph_tile_prog;
	return 0;
}

struct nouveau_oclass
nv34_graph_oclass = {
	.handle = NV_ENGINE(GR, 0x34),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv34_graph_ctor,
		.dtor = nv20_graph_dtor,
		.init = nv30_graph_init,
		.fini = _nouveau_graph_fini,
	},
};
