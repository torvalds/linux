#include "nv20.h"
#include "regs.h"

#include <engine/fifo.h>
#include <engine/fifo/chan.h>

/*******************************************************************************
 * Graphics object classes
 ******************************************************************************/

struct nvkm_oclass
nv25_gr_sclass[] = {
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
	{ 0x009e, &nv04_gr_ofuncs, NULL }, /* swzsurf */
	{ 0x009f, &nv04_gr_ofuncs, NULL }, /* imageblit */
	{ 0x0597, &nv04_gr_ofuncs, NULL }, /* kelvin */
	{},
};

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static int
nv25_gr_context_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		     struct nvkm_oclass *oclass, void *data, u32 size,
		     struct nvkm_object **pobject)
{
	struct nv20_gr_chan *chan;
	struct nvkm_gpuobj *image;
	int ret, i;

	ret = nvkm_gr_context_create(parent, engine, oclass, NULL, 0x3724,
				     16, NVOBJ_FLAG_ZERO_ALLOC, &chan);
	*pobject = nv_object(chan);
	if (ret)
		return ret;

	chan->chid = nvkm_fifo_chan(parent)->chid;
	image = &chan->base.base.gpuobj;

	nvkm_kmap(image);
	nvkm_wo32(image, 0x0028, 0x00000001 | (chan->chid << 24));
	nvkm_wo32(image, 0x035c, 0xffff0000);
	nvkm_wo32(image, 0x03c0, 0x0fff0000);
	nvkm_wo32(image, 0x03c4, 0x0fff0000);
	nvkm_wo32(image, 0x049c, 0x00000101);
	nvkm_wo32(image, 0x04b0, 0x00000111);
	nvkm_wo32(image, 0x04c8, 0x00000080);
	nvkm_wo32(image, 0x04cc, 0xffff0000);
	nvkm_wo32(image, 0x04d0, 0x00000001);
	nvkm_wo32(image, 0x04e4, 0x44400000);
	nvkm_wo32(image, 0x04fc, 0x4b800000);
	for (i = 0x0510; i <= 0x051c; i += 4)
		nvkm_wo32(image, i, 0x00030303);
	for (i = 0x0530; i <= 0x053c; i += 4)
		nvkm_wo32(image, i, 0x00080000);
	for (i = 0x0548; i <= 0x0554; i += 4)
		nvkm_wo32(image, i, 0x01012000);
	for (i = 0x0558; i <= 0x0564; i += 4)
		nvkm_wo32(image, i, 0x000105b8);
	for (i = 0x0568; i <= 0x0574; i += 4)
		nvkm_wo32(image, i, 0x00080008);
	for (i = 0x0598; i <= 0x05d4; i += 4)
		nvkm_wo32(image, i, 0x07ff0000);
	nvkm_wo32(image, 0x05e0, 0x4b7fffff);
	nvkm_wo32(image, 0x0620, 0x00000080);
	nvkm_wo32(image, 0x0624, 0x30201000);
	nvkm_wo32(image, 0x0628, 0x70605040);
	nvkm_wo32(image, 0x062c, 0xb0a09080);
	nvkm_wo32(image, 0x0630, 0xf0e0d0c0);
	nvkm_wo32(image, 0x0664, 0x00000001);
	nvkm_wo32(image, 0x066c, 0x00004000);
	nvkm_wo32(image, 0x0678, 0x00000001);
	nvkm_wo32(image, 0x0680, 0x00040000);
	nvkm_wo32(image, 0x0684, 0x00010000);
	for (i = 0x1b04; i <= 0x2374; i += 16) {
		nvkm_wo32(image, (i + 0), 0x10700ff9);
		nvkm_wo32(image, (i + 4), 0x0436086c);
		nvkm_wo32(image, (i + 8), 0x000c001b);
	}
	nvkm_wo32(image, 0x2704, 0x3f800000);
	nvkm_wo32(image, 0x2718, 0x3f800000);
	nvkm_wo32(image, 0x2744, 0x40000000);
	nvkm_wo32(image, 0x2748, 0x3f800000);
	nvkm_wo32(image, 0x274c, 0x3f000000);
	nvkm_wo32(image, 0x2754, 0x40000000);
	nvkm_wo32(image, 0x2758, 0x3f800000);
	nvkm_wo32(image, 0x2760, 0xbf800000);
	nvkm_wo32(image, 0x2768, 0xbf800000);
	nvkm_wo32(image, 0x308c, 0x000fe000);
	nvkm_wo32(image, 0x3108, 0x000003f8);
	nvkm_wo32(image, 0x3468, 0x002fe000);
	for (i = 0x3484; i <= 0x34a0; i += 4)
		nvkm_wo32(image, i, 0x001c527c);
	nvkm_done(image);
	return 0;
}

static struct nvkm_oclass
nv25_gr_cclass = {
	.handle = NV_ENGCTX(GR, 0x25),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv25_gr_context_ctor,
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
nv25_gr_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
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
	nv_engine(gr)->cclass = &nv25_gr_cclass;
	nv_engine(gr)->sclass = nv25_gr_sclass;
	nv_engine(gr)->tile_prog = nv20_gr_tile_prog;
	return 0;
}

struct nvkm_oclass
nv25_gr_oclass = {
	.handle = NV_ENGINE(GR, 0x25),
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv25_gr_ctor,
		.dtor = nv20_gr_dtor,
		.init = nv20_gr_init,
		.fini = _nvkm_gr_fini,
	},
};
