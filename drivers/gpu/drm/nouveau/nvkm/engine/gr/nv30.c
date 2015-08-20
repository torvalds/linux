#include "nv20.h"
#include "regs.h"

#include <engine/fifo.h>
#include <engine/fifo/chan.h>
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
	struct nvkm_gpuobj *image;
	int ret, i;

	ret = nvkm_gr_context_create(parent, engine, oclass, NULL, 0x5f48,
				     16, NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	chan->chid = nvkm_fifo_chan(parent)->chid;
	image = &chan->base.base.gpuobj;

	nvkm_kmap(image);
	nvkm_wo32(image, 0x0028, 0x00000001 | (chan->chid << 24));
	nvkm_wo32(image, 0x0410, 0x00000101);
	nvkm_wo32(image, 0x0424, 0x00000111);
	nvkm_wo32(image, 0x0428, 0x00000060);
	nvkm_wo32(image, 0x0444, 0x00000080);
	nvkm_wo32(image, 0x0448, 0xffff0000);
	nvkm_wo32(image, 0x044c, 0x00000001);
	nvkm_wo32(image, 0x0460, 0x44400000);
	nvkm_wo32(image, 0x048c, 0xffff0000);
	for (i = 0x04e0; i < 0x04e8; i += 4)
		nvkm_wo32(image, i, 0x0fff0000);
	nvkm_wo32(image, 0x04ec, 0x00011100);
	for (i = 0x0508; i < 0x0548; i += 4)
		nvkm_wo32(image, i, 0x07ff0000);
	nvkm_wo32(image, 0x0550, 0x4b7fffff);
	nvkm_wo32(image, 0x058c, 0x00000080);
	nvkm_wo32(image, 0x0590, 0x30201000);
	nvkm_wo32(image, 0x0594, 0x70605040);
	nvkm_wo32(image, 0x0598, 0xb8a89888);
	nvkm_wo32(image, 0x059c, 0xf8e8d8c8);
	nvkm_wo32(image, 0x05b0, 0xb0000000);
	for (i = 0x0600; i < 0x0640; i += 4)
		nvkm_wo32(image, i, 0x00010588);
	for (i = 0x0640; i < 0x0680; i += 4)
		nvkm_wo32(image, i, 0x00030303);
	for (i = 0x06c0; i < 0x0700; i += 4)
		nvkm_wo32(image, i, 0x0008aae4);
	for (i = 0x0700; i < 0x0740; i += 4)
		nvkm_wo32(image, i, 0x01012000);
	for (i = 0x0740; i < 0x0780; i += 4)
		nvkm_wo32(image, i, 0x00080008);
	nvkm_wo32(image, 0x085c, 0x00040000);
	nvkm_wo32(image, 0x0860, 0x00010000);
	for (i = 0x0864; i < 0x0874; i += 4)
		nvkm_wo32(image, i, 0x00040004);
	for (i = 0x1f18; i <= 0x3088 ; i += 16) {
		nvkm_wo32(image, i + 0, 0x10700ff9);
		nvkm_wo32(image, i + 1, 0x0436086c);
		nvkm_wo32(image, i + 2, 0x000c001b);
	}
	for (i = 0x30b8; i < 0x30c8; i += 4)
		nvkm_wo32(image, i, 0x0000ffff);
	nvkm_wo32(image, 0x344c, 0x3f800000);
	nvkm_wo32(image, 0x3808, 0x3f800000);
	nvkm_wo32(image, 0x381c, 0x3f800000);
	nvkm_wo32(image, 0x3848, 0x40000000);
	nvkm_wo32(image, 0x384c, 0x3f800000);
	nvkm_wo32(image, 0x3850, 0x3f000000);
	nvkm_wo32(image, 0x3858, 0x40000000);
	nvkm_wo32(image, 0x385c, 0x3f800000);
	nvkm_wo32(image, 0x3864, 0xbf800000);
	nvkm_wo32(image, 0x386c, 0xbf800000);
	nvkm_done(image);
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
	nv_engine(gr)->cclass = &nv30_gr_cclass;
	nv_engine(gr)->sclass = nv30_gr_sclass;
	nv_engine(gr)->tile_prog = nv20_gr_tile_prog;
	return 0;
}

int
nv30_gr_init(struct nvkm_object *object)
{
	struct nvkm_engine *engine = nv_engine(object);
	struct nv20_gr *gr = (void *)engine;
	struct nvkm_device *device = gr->base.engine.subdev.device;
	struct nvkm_fb *fb = device->fb;
	int ret, i;

	ret = nvkm_gr_init(&gr->base);
	if (ret)
		return ret;

	nvkm_wr32(device, NV20_PGRAPH_CHANNEL_CTX_TABLE,
			  nvkm_memory_addr(gr->ctxtab) >> 4);

	nvkm_wr32(device, NV03_PGRAPH_INTR   , 0xFFFFFFFF);
	nvkm_wr32(device, NV03_PGRAPH_INTR_EN, 0xFFFFFFFF);

	nvkm_wr32(device, NV04_PGRAPH_DEBUG_0, 0xFFFFFFFF);
	nvkm_wr32(device, NV04_PGRAPH_DEBUG_0, 0x00000000);
	nvkm_wr32(device, NV04_PGRAPH_DEBUG_1, 0x401287c0);
	nvkm_wr32(device, 0x400890, 0x01b463ff);
	nvkm_wr32(device, NV04_PGRAPH_DEBUG_3, 0xf2de0475);
	nvkm_wr32(device, NV10_PGRAPH_DEBUG_4, 0x00008000);
	nvkm_wr32(device, NV04_PGRAPH_LIMIT_VIOL_PIX, 0xf04bdff6);
	nvkm_wr32(device, 0x400B80, 0x1003d888);
	nvkm_wr32(device, 0x400B84, 0x0c000000);
	nvkm_wr32(device, 0x400098, 0x00000000);
	nvkm_wr32(device, 0x40009C, 0x0005ad00);
	nvkm_wr32(device, 0x400B88, 0x62ff00ff); /* suspiciously like PGRAPH_DEBUG_2 */
	nvkm_wr32(device, 0x4000a0, 0x00000000);
	nvkm_wr32(device, 0x4000a4, 0x00000008);
	nvkm_wr32(device, 0x4008a8, 0xb784a400);
	nvkm_wr32(device, 0x400ba0, 0x002f8685);
	nvkm_wr32(device, 0x400ba4, 0x00231f3f);
	nvkm_wr32(device, 0x4008a4, 0x40000020);

	if (nv_device(gr)->chipset == 0x34) {
		nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00EA0004);
		nvkm_wr32(device, NV10_PGRAPH_RDI_DATA , 0x00200201);
		nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00EA0008);
		nvkm_wr32(device, NV10_PGRAPH_RDI_DATA , 0x00000008);
		nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00EA0000);
		nvkm_wr32(device, NV10_PGRAPH_RDI_DATA , 0x00000032);
		nvkm_wr32(device, NV10_PGRAPH_RDI_INDEX, 0x00E00004);
		nvkm_wr32(device, NV10_PGRAPH_RDI_DATA , 0x00000002);
	}

	nvkm_wr32(device, 0x4000c0, 0x00000016);

	/* Turn all the tiling regions off. */
	for (i = 0; i < fb->tile.regions; i++)
		engine->tile_prog(engine, i);

	nvkm_wr32(device, NV10_PGRAPH_CTX_CONTROL, 0x10000100);
	nvkm_wr32(device, NV10_PGRAPH_STATE      , 0xFFFFFFFF);
	nvkm_wr32(device, 0x0040075c             , 0x00000001);

	/* begin RAM config */
	/* vramsz = pci_resource_len(gr->dev->pdev, 1) - 1; */
	nvkm_wr32(device, 0x4009A4, nvkm_rd32(device, 0x100200));
	nvkm_wr32(device, 0x4009A8, nvkm_rd32(device, 0x100204));
	if (nv_device(gr)->chipset != 0x34) {
		nvkm_wr32(device, 0x400750, 0x00EA0000);
		nvkm_wr32(device, 0x400754, nvkm_rd32(device, 0x100200));
		nvkm_wr32(device, 0x400750, 0x00EA0004);
		nvkm_wr32(device, 0x400754, nvkm_rd32(device, 0x100204));
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
