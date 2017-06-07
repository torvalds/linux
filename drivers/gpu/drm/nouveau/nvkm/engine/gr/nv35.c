#include "nv20.h"
#include "regs.h"

#include <core/gpuobj.h>
#include <engine/fifo.h>
#include <engine/fifo/chan.h>

/*******************************************************************************
 * PGRAPH context
 ******************************************************************************/

static const struct nvkm_object_func
nv35_gr_chan = {
	.dtor = nv20_gr_chan_dtor,
	.init = nv20_gr_chan_init,
	.fini = nv20_gr_chan_fini,
};

static int
nv35_gr_chan_new(struct nvkm_gr *base, struct nvkm_fifo_chan *fifoch,
		 const struct nvkm_oclass *oclass, struct nvkm_object **pobject)
{
	struct nv20_gr *gr = nv20_gr(base);
	struct nv20_gr_chan *chan;
	int ret, i;

	if (!(chan = kzalloc(sizeof(*chan), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_object_ctor(&nv35_gr_chan, oclass, &chan->object);
	chan->gr = gr;
	chan->chid = fifoch->chid;
	*pobject = &chan->object;

	ret = nvkm_memory_new(gr->base.engine.subdev.device,
			      NVKM_MEM_TARGET_INST, 0x577c, 16, true,
			      &chan->inst);
	if (ret)
		return ret;

	nvkm_kmap(chan->inst);
	nvkm_wo32(chan->inst, 0x0028, 0x00000001 | (chan->chid << 24));
	nvkm_wo32(chan->inst, 0x040c, 0x00000101);
	nvkm_wo32(chan->inst, 0x0420, 0x00000111);
	nvkm_wo32(chan->inst, 0x0424, 0x00000060);
	nvkm_wo32(chan->inst, 0x0440, 0x00000080);
	nvkm_wo32(chan->inst, 0x0444, 0xffff0000);
	nvkm_wo32(chan->inst, 0x0448, 0x00000001);
	nvkm_wo32(chan->inst, 0x045c, 0x44400000);
	nvkm_wo32(chan->inst, 0x0488, 0xffff0000);
	for (i = 0x04dc; i < 0x04e4; i += 4)
		nvkm_wo32(chan->inst, i, 0x0fff0000);
	nvkm_wo32(chan->inst, 0x04e8, 0x00011100);
	for (i = 0x0504; i < 0x0544; i += 4)
		nvkm_wo32(chan->inst, i, 0x07ff0000);
	nvkm_wo32(chan->inst, 0x054c, 0x4b7fffff);
	nvkm_wo32(chan->inst, 0x0588, 0x00000080);
	nvkm_wo32(chan->inst, 0x058c, 0x30201000);
	nvkm_wo32(chan->inst, 0x0590, 0x70605040);
	nvkm_wo32(chan->inst, 0x0594, 0xb8a89888);
	nvkm_wo32(chan->inst, 0x0598, 0xf8e8d8c8);
	nvkm_wo32(chan->inst, 0x05ac, 0xb0000000);
	for (i = 0x0604; i < 0x0644; i += 4)
		nvkm_wo32(chan->inst, i, 0x00010588);
	for (i = 0x0644; i < 0x0684; i += 4)
		nvkm_wo32(chan->inst, i, 0x00030303);
	for (i = 0x06c4; i < 0x0704; i += 4)
		nvkm_wo32(chan->inst, i, 0x0008aae4);
	for (i = 0x0704; i < 0x0744; i += 4)
		nvkm_wo32(chan->inst, i, 0x01012000);
	for (i = 0x0744; i < 0x0784; i += 4)
		nvkm_wo32(chan->inst, i, 0x00080008);
	nvkm_wo32(chan->inst, 0x0860, 0x00040000);
	nvkm_wo32(chan->inst, 0x0864, 0x00010000);
	for (i = 0x0868; i < 0x0878; i += 4)
		nvkm_wo32(chan->inst, i, 0x00040004);
	for (i = 0x1f1c; i <= 0x308c ; i += 16) {
		nvkm_wo32(chan->inst, i + 0, 0x10700ff9);
		nvkm_wo32(chan->inst, i + 4, 0x0436086c);
		nvkm_wo32(chan->inst, i + 8, 0x000c001b);
	}
	for (i = 0x30bc; i < 0x30cc; i += 4)
		nvkm_wo32(chan->inst, i, 0x0000ffff);
	nvkm_wo32(chan->inst, 0x3450, 0x3f800000);
	nvkm_wo32(chan->inst, 0x380c, 0x3f800000);
	nvkm_wo32(chan->inst, 0x3820, 0x3f800000);
	nvkm_wo32(chan->inst, 0x384c, 0x40000000);
	nvkm_wo32(chan->inst, 0x3850, 0x3f800000);
	nvkm_wo32(chan->inst, 0x3854, 0x3f000000);
	nvkm_wo32(chan->inst, 0x385c, 0x40000000);
	nvkm_wo32(chan->inst, 0x3860, 0x3f800000);
	nvkm_wo32(chan->inst, 0x3868, 0xbf800000);
	nvkm_wo32(chan->inst, 0x3870, 0xbf800000);
	nvkm_done(chan->inst);
	return 0;
}

/*******************************************************************************
 * PGRAPH engine/subdev functions
 ******************************************************************************/

static const struct nvkm_gr_func
nv35_gr = {
	.dtor = nv20_gr_dtor,
	.oneinit = nv20_gr_oneinit,
	.init = nv30_gr_init,
	.intr = nv20_gr_intr,
	.tile = nv20_gr_tile,
	.chan_new = nv35_gr_chan_new,
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
		{ -1, -1, 0x009f, &nv04_gr_object }, /* imageblit */
		{ -1, -1, 0x0362, &nv04_gr_object }, /* surf2d (nv30) */
		{ -1, -1, 0x0389, &nv04_gr_object }, /* sifm (nv30) */
		{ -1, -1, 0x038a, &nv04_gr_object }, /* ifc (nv30) */
		{ -1, -1, 0x039e, &nv04_gr_object }, /* swzsurf (nv30) */
		{ -1, -1, 0x0497, &nv04_gr_object }, /* rankine */
		{ -1, -1, 0x0597, &nv04_gr_object }, /* kelvin */
		{}
	}
};

int
nv35_gr_new(struct nvkm_device *device, int index, struct nvkm_gr **pgr)
{
	return nv20_gr_new_(&nv35_gr, device, index, pgr);
}
