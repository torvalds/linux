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

static const struct nvkm_disp_func
g94_disp = {
	.root = &g94_disp_root_oclass,
};

static int
g94_disp_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	struct nv50_disp *disp;
	int ret;

	ret = nvkm_disp_create(parent, engine, oclass, 2, "PDISP",
			       "display", &disp);
	*pobject = nv_object(disp);
	if (ret)
		return ret;

	disp->base.func = &g94_disp;

	ret = nvkm_event_init(&nv50_disp_chan_uevent, 1, 9, &disp->uevent);
	if (ret)
		return ret;

	nv_subdev(disp)->intr = nv50_disp_intr;
	INIT_WORK(&disp->supervisor, nv50_disp_intr_supervisor);
	disp->head.nr = 2;
	disp->dac.nr = 3;
	disp->sor.nr = 4;
	disp->pior.nr = 3;
	disp->dac.power = nv50_dac_power;
	disp->dac.sense = nv50_dac_sense;
	disp->sor.power = nv50_sor_power;
	disp->sor.hdmi = g84_hdmi_ctrl;
	disp->pior.power = nv50_pior_power;
	return 0;
}

struct nvkm_oclass *
g94_disp_oclass = &(struct nv50_disp_impl) {
	.base.base.handle = NV_ENGINE(DISP, 0x88),
	.base.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = g94_disp_ctor,
		.dtor = _nvkm_disp_dtor,
		.init = _nvkm_disp_init,
		.fini = _nvkm_disp_fini,
	},
	.base.outp.internal.crt = nv50_dac_output_new,
	.base.outp.internal.tmds = nv50_sor_output_new,
	.base.outp.internal.lvds = nv50_sor_output_new,
	.base.outp.internal.dp = g94_sor_dp_new,
	.base.outp.external.lvds = nv50_pior_output_new,
	.base.outp.external.dp = nv50_pior_dp_new,
	.base.vblank = &nv50_disp_vblank_func,
	.head.scanoutpos = nv50_disp_root_scanoutpos,
}.base.base;
