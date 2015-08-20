#include "nv20.h"
#include "regs.h"

#include <engine/fifo.h>

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv2a_gr_context_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		     struct nvkm_oclass *oclass, void *data, u32 size,
		     struct nvkm_object **pobject)
{
	struct nv20_gr_chan *chan;
	struct nvkm_gpuobj *image;
	int ret, i;

	ret = nvkm_gr_context_create(parent, engine, oclass, NULL, 0x36b0,
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
	for (i = 0x1a9c; i <= 0x22fc; i += 16) { /*XXX: check!! */
		nvkm_wo32(image, (i + 0), 0x10700ff9);
		nvkm_wo32(image, (i + 4), 0x0436086c);
		nvkm_wo32(image, (i + 8), 0x000c001b);
	}
	nvkm_wo32(image, 0x269c, 0x3f800000);
	nvkm_wo32(image, 0x26b0, 0x3f800000);
	nvkm_wo32(image, 0x26dc, 0x40000000);
	nvkm_wo32(image, 0x26e0, 0x3f800000);
	nvkm_wo32(image, 0x26e4, 0x3f000000);
	nvkm_wo32(image, 0x26ec, 0x40000000);
	nvkm_wo32(image, 0x26f0, 0x3f800000);
	nvkm_wo32(image, 0x26f8, 0xbf800000);
	nvkm_wo32(image, 0x2700, 0xbf800000);
	nvkm_wo32(image, 0x3024, 0x000fe000);
	nvkm_wo32(image, 0x30a0, 0x000003f8);
	nvkm_wo32(image, 0x33fc, 0x002fe000);
	for (i = 0x341c; i <= 0x3438; i += 4)
		nvkm_wo32(image, i, 0x001c527c);
	nvkm_done(image);
	return 0;
}

static struct nvkm_oclass
nv2a_gr_cclass = {
	.handle = NV_ENGCTX(GR, 0x2a),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv2a_gr_context_ctor,
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
nv2a_gr_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
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
	nv_engine(gr)->cclass = &nv2a_gr_cclass;
	nv_engine(gr)->sclass = nv25_gr_sclass;
	nv_engine(gr)->tile_prog = nv20_gr_tile_prog;
	return 0;
}

struct nvkm_oclass
nv2a_gr_oclass = {
	.handle = NV_ENGINE(GR, 0x2a),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv2a_gr_ctor,
		.dtor = nv20_gr_dtor,
		.init = nv20_gr_init,
		.fini = _nvkm_gr_fini,
	},
};
