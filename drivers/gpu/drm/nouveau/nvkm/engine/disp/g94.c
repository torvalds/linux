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
#include "outpdp.h"

#include <nvif/class.h>

/*******************************************************************************
 * EVO master channel object
 ******************************************************************************/

const struct nv50_disp_mthd_list
g94_disp_core_mthd_sor = {
	.mthd = 0x0040,
	.addr = 0x000008,
	.data = {
		{ 0x0600, 0x610794 },
		{}
	}
};

const struct nv50_disp_mthd_chan
g94_disp_core_mthd_chan = {
	.name = "Core",
	.addr = 0x000000,
	.data = {
		{ "Global", 1, &nv50_disp_core_mthd_base },
		{    "DAC", 3, &g84_disp_core_mthd_dac  },
		{    "SOR", 4, &g94_disp_core_mthd_sor  },
		{   "PIOR", 3, &nv50_disp_core_mthd_pior },
		{   "HEAD", 2, &g84_disp_core_mthd_head },
		{}
	}
};

/*******************************************************************************
 * Base display object
 ******************************************************************************/

static struct nvkm_oclass
g94_disp_sclass[] = {
	{ GT206_DISP_CORE_CHANNEL_DMA, &nv50_disp_core_ofuncs.base },
	{ GT200_DISP_BASE_CHANNEL_DMA, &nv50_disp_base_ofuncs.base },
	{ GT200_DISP_OVERLAY_CHANNEL_DMA, &nv50_disp_ovly_ofuncs.base },
	{ G82_DISP_OVERLAY, &nv50_disp_oimm_ofuncs.base },
	{ G82_DISP_CURSOR, &nv50_disp_curs_ofuncs.base },
	{}
};

static struct nvkm_oclass
g94_disp_main_oclass[] = {
	{ GT206_DISP, &nv50_disp_main_ofuncs },
	{}
};

/*******************************************************************************
 * Display engine implementation
 ******************************************************************************/

static int
g94_disp_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	      struct nvkm_oclass *oclass, void *data, u32 size,
	      struct nvkm_object **pobject)
{
	struct nv50_disp_priv *priv;
	int ret;

	ret = nvkm_disp_create(parent, engine, oclass, 2, "PDISP",
			       "display", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nvkm_event_init(&nv50_disp_chan_uevent, 1, 9, &priv->uevent);
	if (ret)
		return ret;

	nv_engine(priv)->sclass = g94_disp_main_oclass;
	nv_engine(priv)->cclass = &nv50_disp_cclass;
	nv_subdev(priv)->intr = nv50_disp_intr;
	INIT_WORK(&priv->supervisor, nv50_disp_intr_supervisor);
	priv->sclass = g94_disp_sclass;
	priv->head.nr = 2;
	priv->dac.nr = 3;
	priv->sor.nr = 4;
	priv->pior.nr = 3;
	priv->dac.power = nv50_dac_power;
	priv->dac.sense = nv50_dac_sense;
	priv->sor.power = nv50_sor_power;
	priv->sor.hdmi = g84_hdmi_ctrl;
	priv->pior.power = nv50_pior_power;
	return 0;
}

struct nvkm_oclass *
g94_disp_outp_sclass[] = {
	&nv50_pior_dp_impl.base.base,
	&g94_sor_dp_impl.base.base,
	NULL
};

struct nvkm_oclass *
g94_disp_oclass = &(struct nv50_disp_impl) {
	.base.base.handle = NV_ENGINE(DISP, 0x88),
	.base.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = g94_disp_ctor,
		.dtor = _nvkm_disp_dtor,
		.init = _nvkm_disp_init,
		.fini = _nvkm_disp_fini,
	},
	.base.vblank = &nv50_disp_vblank_func,
	.base.outp =  g94_disp_outp_sclass,
	.mthd.core = &g94_disp_core_mthd_chan,
	.mthd.base = &g84_disp_base_mthd_chan,
	.mthd.ovly = &g84_disp_ovly_mthd_chan,
	.mthd.prev = 0x000004,
	.head.scanoutpos = nv50_disp_main_scanoutpos,
}.base.base;
