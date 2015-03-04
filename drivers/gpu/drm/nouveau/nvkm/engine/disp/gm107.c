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

#include <nvif/class.h>

/*******************************************************************************
 * Base display object
 ******************************************************************************/

static struct nvkm_oclass
gm107_disp_sclass[] = {
	{ GM107_DISP_CORE_CHANNEL_DMA, &gf110_disp_core_ofuncs.base },
	{ GK110_DISP_BASE_CHANNEL_DMA, &gf110_disp_base_ofuncs.base },
	{ GK104_DISP_OVERLAY_CONTROL_DMA, &gf110_disp_ovly_ofuncs.base },
	{ GK104_DISP_OVERLAY, &gf110_disp_oimm_ofuncs.base },
	{ GK104_DISP_CURSOR, &gf110_disp_curs_ofuncs.base },
	{}
};

static struct nvkm_oclass
gm107_disp_main_oclass[] = {
	{ GM107_DISP, &gf110_disp_main_ofuncs },
	{}
};

/*******************************************************************************
 * Display engine implementation
 ******************************************************************************/

static int
gm107_disp_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	struct nv50_disp_priv *priv;
	int heads = nv_rd32(parent, 0x022448);
	int ret;

	ret = nvkm_disp_create(parent, engine, oclass, heads,
			       "PDISP", "display", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nvkm_event_init(&gf110_disp_chan_uevent, 1, 17, &priv->uevent);
	if (ret)
		return ret;

	nv_engine(priv)->sclass = gm107_disp_main_oclass;
	nv_engine(priv)->cclass = &nv50_disp_cclass;
	nv_subdev(priv)->intr = gf110_disp_intr;
	INIT_WORK(&priv->supervisor, gf110_disp_intr_supervisor);
	priv->sclass = gm107_disp_sclass;
	priv->head.nr = heads;
	priv->dac.nr = 3;
	priv->sor.nr = 4;
	priv->dac.power = nv50_dac_power;
	priv->dac.sense = nv50_dac_sense;
	priv->sor.power = nv50_sor_power;
	priv->sor.hda_eld = gf110_hda_eld;
	priv->sor.hdmi = gk104_hdmi_ctrl;
	return 0;
}

struct nvkm_oclass *
gm107_disp_oclass = &(struct nv50_disp_impl) {
	.base.base.handle = NV_ENGINE(DISP, 0x07),
	.base.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = gm107_disp_ctor,
		.dtor = _nvkm_disp_dtor,
		.init = _nvkm_disp_init,
		.fini = _nvkm_disp_fini,
	},
	.base.vblank = &gf110_disp_vblank_func,
	.base.outp =  gf110_disp_outp_sclass,
	.mthd.core = &gk104_disp_core_mthd_chan,
	.mthd.base = &gf110_disp_base_mthd_chan,
	.mthd.ovly = &gk104_disp_ovly_mthd_chan,
	.mthd.prev = -0x020000,
	.head.scanoutpos = gf110_disp_main_scanoutpos,
}.base.base;
