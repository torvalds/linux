#include "nv20.h"
#include "regs.h"

#include <core/device.h>
#include <engine/fifo.h>
#include <subdev/fb.h>

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

static struct nvkm_oclass
nv30_gr_sclass[] = {
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
	{ 0x009f, &nv04_gr_ofuncs, NULL }, /* imageblit */
	{ 0x0362, &nv04_gr_ofuncs, NULL }, /* surf2d (nv30) */
	{ 0x0389, &nv04_gr_ofuncs, NULL }, /* sifm (nv30) */
	{ 0x038a, &nv04_gr_ofuncs, NULL }, /* ifc (nv30) */
	{ 0x039e, &nv04_gr_ofuncs, NULL }, /* swzsurf (nv30) */
	{ 0x0397, &nv04_gr_ofuncs, NULL }, /* rankine */
	{},
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv30_gr_context_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		     struct nvkm_oclass *oclass, void *data, u32 size,
		     struct nvkm_object **pobject)
{
	struct nv20_gr_chan *chan;
	int ret, i;

	ret = nvkm_gr_context_create(parent, engine, oclass, NULL, 0x5f48,
				     16, NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	chan->chid = nvkm_fifo_chan(parent)->chid;

	nv_wo32(chan, 0x0028, 0x00000001 | (chan->chid << 24));
	nv_wo32(chan, 0x0410, 0x00000101);
	nv_wo32(chan, 0x0424, 0x00000111);
	nv_wo32(chan, 0x0428, 0x00000060);
	nv_wo32(chan, 0x0444, 0x00000080);
	nv_wo32(chan, 0x0448, 0xffff0000);
	nv_wo32(chan, 0x044c, 0x00000001);
	nv_wo32(chan, 0x0460, 0x44400000);
	nv_wo32(chan, 0x048c, 0xffff0000);
	for (i = 0x04e0; i < 0x04e8; i += 4)
		nv_wo32(chan, i, 0x0fff0000);
	nv_wo32(chan, 0x04ec, 0x00011100);
	for (i = 0x0508; i < 0x0548; i += 4)
		nv_wo32(chan, i, 0x07ff0000);
	nv_wo32(chan, 0x0550, 0x4b7fffff);
	nv_wo32(chan, 0x058c, 0x00000080);
	nv_wo32(chan, 0x0590, 0x30201000);
	nv_wo32(chan, 0x0594, 0x70605040);
	nv_wo32(chan, 0x0598, 0xb8a89888);
	nv_wo32(chan, 0x059c, 0xf8e8d8c8);
	nv_wo32(chan, 0x05b0, 0xb0000000);
	for (i = 0x0600; i < 0x0640; i += 4)
		nv_wo32(chan, i, 0x00010588);
	for (i = 0x0640; i < 0x0680; i += 4)
		nv_wo32(chan, i, 0x00030303);
	for (i = 0x06c0; i < 0x0700; i += 4)
		nv_wo32(chan, i, 0x0008aae4);
	for (i = 0x0700; i < 0x0740; i += 4)
		nv_wo32(chan, i, 0x01012000);
	for (i = 0x0740; i < 0x0780; i += 4)
		nv_wo32(chan, i, 0x00080008);
	nv_wo32(chan, 0x085c, 0x00040000);
	nv_wo32(chan, 0x0860, 0x00010000);
	for (i = 0x0864; i < 0x0874; i += 4)
		nv_wo32(chan, i, 0x00040004);
	for (i = 0x1f18; i <= 0x3088 ; i += 16) {
		nv_wo32(chan, i + 0, 0x10700ff9);
		nv_wo32(chan, i + 1, 0x0436086c);
		nv_wo32(chan, i + 2, 0x000c001b);
	}
	for (i = 0x30b8; i < 0x30c8; i += 4)
		nv_wo32(chan, i, 0x0000ffff);
	nv_wo32(chan, 0x344c, 0x3f800000);
	nv_wo32(chan, 0x3808, 0x3f800000);
	nv_wo32(chan, 0x381c, 0x3f800000);
	nv_wo32(chan, 0x3848, 0x40000000);
	nv_wo32(chan, 0x384c, 0x3f800000);
	nv_wo32(chan, 0x3850, 0x3f000000);
	nv_wo32(chan, 0x3858, 0x40000000);
	nv_wo32(chan, 0x385c, 0x3f800000);
	nv_wo32(chan, 0x3864, 0xbf800000);
	nv_wo32(chan, 0x386c, 0xbf800000);
	return 0;
}

static struct nvkm_oclass
nv30_gr_cclass = {
	.handle = NV_ENGCTX(GR, 0x30),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv30_gr_context_ctor,
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

static int
nv30_gr_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	     struct nvkm_oclass *oclass, void *data, u32 size,
	     struct nvkm_object **pobject)
{
	struct nv20_gr_priv *priv;
	int ret;

	ret = nvkm_gr_create(parent, engine, oclass, true, &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nvkm_gpuobj_new(nv_object(priv), NULL, 32 * 4, 16,
			      NVOBJ_FLAG_ZERO_ALLOC, &priv->ctxtab);
	if (ret)
		return ret;

	nv_subdev(priv)->unit = 0x00001000;
	nv_subdev(priv)->intr = nv20_gr_intr;
	nv_engine(priv)->cclass = &nv30_gr_cclass;
	nv_engine(priv)->sclass = nv30_gr_sclass;
	nv_engine(priv)->tile_prog = nv20_gr_tile_prog;
	return 0;
}

int
nv30_gr_init(struct nvkm_object *object)
{
	struct nvkm_engine *engine = nv_engine(object);
	struct nv20_gr_priv *priv = (void *)engine;
	struct nvkm_fb *pfb = nvkm_fb(object);
	int ret, i;

	ret = nvkm_gr_init(&priv->base);
	if (ret)
		return ret;

	nv_wr32(priv, NV20_PGRAPH_CHANNEL_CTX_TABLE, priv->ctxtab->addr >> 4);

	nv_wr32(priv, NV03_PGRAPH_INTR   , 0xFFFFFFFF);
	nv_wr32(priv, NV03_PGRAPH_INTR_EN, 0xFFFFFFFF);

	nv_wr32(priv, NV04_PGRAPH_DEBUG_0, 0xFFFFFFFF);
	nv_wr32(priv, NV04_PGRAPH_DEBUG_0, 0x00000000);
	nv_wr32(priv, NV04_PGRAPH_DEBUG_1, 0x401287c0);
	nv_wr32(priv, 0x400890, 0x01b463ff);
	nv_wr32(priv, NV04_PGRAPH_DEBUG_3, 0xf2de0475);
	nv_wr32(priv, NV10_PGRAPH_DEBUG_4, 0x00008000);
	nv_wr32(priv, NV04_PGRAPH_LIMIT_VIOL_PIX, 0xf04bdff6);
	nv_wr32(priv, 0x400B80, 0x1003d888);
	nv_wr32(priv, 0x400B84, 0x0c000000);
	nv_wr32(priv, 0x400098, 0x00000000);
	nv_wr32(priv, 0x40009C, 0x0005ad00);
	nv_wr32(priv, 0x400B88, 0x62ff00ff); /* suspiciously like PGRAPH_DEBUG_2 */
	nv_wr32(priv, 0x4000a0, 0x00000000);
	nv_wr32(priv, 0x4000a4, 0x00000008);
	nv_wr32(priv, 0x4008a8, 0xb784a400);
	nv_wr32(priv, 0x400ba0, 0x002f8685);
	nv_wr32(priv, 0x400ba4, 0x00231f3f);
	nv_wr32(priv, 0x4008a4, 0x40000020);

	if (nv_device(priv)->chipset == 0x34) {
		nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x00EA0004);
		nv_wr32(priv, NV10_PGRAPH_RDI_DATA , 0x00200201);
		nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x00EA0008);
		nv_wr32(priv, NV10_PGRAPH_RDI_DATA , 0x00000008);
		nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x00EA0000);
		nv_wr32(priv, NV10_PGRAPH_RDI_DATA , 0x00000032);
		nv_wr32(priv, NV10_PGRAPH_RDI_INDEX, 0x00E00004);
		nv_wr32(priv, NV10_PGRAPH_RDI_DATA , 0x00000002);
	}

	nv_wr32(priv, 0x4000c0, 0x00000016);

	/* Turn all the tiling regions off. */
	for (i = 0; i < pfb->tile.regions; i++)
		engine->tile_prog(engine, i);

	nv_wr32(priv, NV10_PGRAPH_CTX_CONTROL, 0x10000100);
	nv_wr32(priv, NV10_PGRAPH_STATE      , 0xFFFFFFFF);
	nv_wr32(priv, 0x0040075c             , 0x00000001);

	/* begin RAM config */
	/* vramsz = pci_resource_len(priv->dev->pdev, 0) - 1; */
	nv_wr32(priv, 0x4009A4, nv_rd32(priv, 0x100200));
	nv_wr32(priv, 0x4009A8, nv_rd32(priv, 0x100204));
	if (nv_device(priv)->chipset != 0x34) {
		nv_wr32(priv, 0x400750, 0x00EA0000);
		nv_wr32(priv, 0x400754, nv_rd32(priv, 0x100200));
		nv_wr32(priv, 0x400750, 0x00EA0004);
		nv_wr32(priv, 0x400754, nv_rd32(priv, 0x100204));
	}
	return 0;
}

struct nvkm_oclass
nv30_gr_oclass = {
	.handle = NV_ENGINE(GR, 0x30),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv30_gr_ctor,
		.dtor = nv20_gr_dtor,
		.init = nv30_gr_init,
		.fini = _nvkm_gr_fini,
	},
};
