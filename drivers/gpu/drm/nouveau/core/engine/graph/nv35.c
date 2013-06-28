#include <core/os.h>
#include <core/class.h>
#include <core/engctx.h>
#include <core/enum.h>

#include <subdev/timer.h>
#include <subdev/fb.h>

#include "nv20.h"
#include "regs.h"

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

static struct nouveau_oclass
nv35_graph_sclass[] = {
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
	{ 0x0497, &nv04_graph_ofuncs, NULL }, /* rankine */
	{},
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv35_graph_context_ctor(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass, void *data, u32 size,
			struct nouveau_object **pobject)
{
	struct nv20_graph_chan *chan;
	int ret, i;

	ret = nouveau_graph_context_create(parent, engine, oclass, NULL, 0x577c,
					   16, NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	chan->chid = nouveau_fifo_chan(parent)->chid;

	nv_wo32(chan, 0x0028, 0x00000001 | (chan->chid << 24));
	nv_wo32(chan, 0x040c, 0x00000101);
	nv_wo32(chan, 0x0420, 0x00000111);
	nv_wo32(chan, 0x0424, 0x00000060);
	nv_wo32(chan, 0x0440, 0x00000080);
	nv_wo32(chan, 0x0444, 0xffff0000);
	nv_wo32(chan, 0x0448, 0x00000001);
	nv_wo32(chan, 0x045c, 0x44400000);
	nv_wo32(chan, 0x0488, 0xffff0000);
	for (i = 0x04dc; i < 0x04e4; i += 4)
		nv_wo32(chan, i, 0x0fff0000);
	nv_wo32(chan, 0x04e8, 0x00011100);
	for (i = 0x0504; i < 0x0544; i += 4)
		nv_wo32(chan, i, 0x07ff0000);
	nv_wo32(chan, 0x054c, 0x4b7fffff);
	nv_wo32(chan, 0x0588, 0x00000080);
	nv_wo32(chan, 0x058c, 0x30201000);
	nv_wo32(chan, 0x0590, 0x70605040);
	nv_wo32(chan, 0x0594, 0xb8a89888);
	nv_wo32(chan, 0x0598, 0xf8e8d8c8);
	nv_wo32(chan, 0x05ac, 0xb0000000);
	for (i = 0x0604; i < 0x0644; i += 4)
		nv_wo32(chan, i, 0x00010588);
	for (i = 0x0644; i < 0x0684; i += 4)
		nv_wo32(chan, i, 0x00030303);
	for (i = 0x06c4; i < 0x0704; i += 4)
		nv_wo32(chan, i, 0x0008aae4);
	for (i = 0x0704; i < 0x0744; i += 4)
		nv_wo32(chan, i, 0x01012000);
	for (i = 0x0744; i < 0x0784; i += 4)
		nv_wo32(chan, i, 0x00080008);
	nv_wo32(chan, 0x0860, 0x00040000);
	nv_wo32(chan, 0x0864, 0x00010000);
	for (i = 0x0868; i < 0x0878; i += 4)
		nv_wo32(chan, i, 0x00040004);
	for (i = 0x1f1c; i <= 0x308c ; i += 16) {
		nv_wo32(chan, i + 0, 0x10700ff9);
		nv_wo32(chan, i + 4, 0x0436086c);
		nv_wo32(chan, i + 8, 0x000c001b);
	}
	for (i = 0x30bc; i < 0x30cc; i += 4)
		nv_wo32(chan, i, 0x0000ffff);
	nv_wo32(chan, 0x3450, 0x3f800000);
	nv_wo32(chan, 0x380c, 0x3f800000);
	nv_wo32(chan, 0x3820, 0x3f800000);
	nv_wo32(chan, 0x384c, 0x40000000);
	nv_wo32(chan, 0x3850, 0x3f800000);
	nv_wo32(chan, 0x3854, 0x3f000000);
	nv_wo32(chan, 0x385c, 0x40000000);
	nv_wo32(chan, 0x3860, 0x3f800000);
	nv_wo32(chan, 0x3868, 0xbf800000);
	nv_wo32(chan, 0x3870, 0xbf800000);
	return 0;
}

static struct nouveau_oclass
nv35_graph_cclass = {
	.handle = NV_ENGCTX(GR, 0x35),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv35_graph_context_ctor,
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
nv35_graph_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
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
	nv_engine(priv)->cclass = &nv35_graph_cclass;
	nv_engine(priv)->sclass = nv35_graph_sclass;
	nv_engine(priv)->tile_prog = nv20_graph_tile_prog;
	return 0;
}

struct nouveau_oclass
nv35_graph_oclass = {
	.handle = NV_ENGINE(GR, 0x35),
	.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv35_graph_ctor,
		.dtor = nv20_graph_dtor,
		.init = nv30_graph_init,
		.fini = _nouveau_graph_fini,
	},
};
