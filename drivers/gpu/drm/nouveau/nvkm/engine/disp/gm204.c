/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
#include "nv50.h"
#include "rootnv50.h"

static int
gm204_disp_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	struct nvkm_device *device = (void *)parent;
	struct nv50_disp *disp;
	int heads = nvkm_rd32(device, 0x022448);
	int ret;

	ret = nvkm_disp_create(parent, engine, oclass, heads,
			       "PDISP", "display", &disp);
	*pobject = nv_object(disp);
	if (ret)
		return ret;

	ret = nvkm_event_init(&gf119_disp_chan_uevent, 1, 17, &disp->uevent);
	if (ret)
		return ret;

	nv_engine(disp)->sclass = gm204_disp_root_oclass;
	nv_engine(disp)->cclass = &nv50_disp_cclass;
	nv_subdev(disp)->intr = gf119_disp_intr;
	INIT_WORK(&disp->supervisor, gf119_disp_intr_supervisor);
	disp->sclass = gm204_disp_sclass;
	disp->head.nr = heads;
	disp->dac.nr = 3;
	disp->sor.nr = 4;
	disp->dac.power = nv50_dac_power;
	disp->dac.sense = nv50_dac_sense;
	disp->sor.power = nv50_sor_power;
	disp->sor.hda_eld = gf119_hda_eld;
	disp->sor.hdmi = gf119_hdmi_ctrl;
	disp->sor.magic = gm204_sor_magic;
	return 0;
}

struct nvkm_oclass *
gm204_disp_oclass = &(struct nv50_disp_impl) {
	.base.base.handle = NV_ENGINE(DISP, 0x07),
	.base.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gm204_disp_ctor,
		.dtor = _nvkm_disp_dtor,
		.init = _nvkm_disp_init,
		.fini = _nvkm_disp_fini,
	},
	.base.outp.internal.crt = nv50_dac_output_new,
	.base.outp.internal.tmds = nv50_sor_output_new,
	.base.outp.internal.lvds = nv50_sor_output_new,
	.base.outp.internal.dp = gm204_sor_dp_new,
	.base.vblank = &gf119_disp_vblank_func,
	.mthd.core = &gk104_disp_core_mthd_chan,
	.mthd.base = &gf119_disp_base_mthd_chan,
	.mthd.ovly = &gk104_disp_ovly_mthd_chan,
	.mthd.prev = -0x020000,
	.head.scanoutpos = gf119_disp_root_scanoutpos,
}.base.base;
