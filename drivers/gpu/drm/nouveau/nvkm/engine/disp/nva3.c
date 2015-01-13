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

#include <engine/software.h>
#include <engine/disp.h>

#include <nvif/class.h>

#include "nv50.h"

/*******************************************************************************
 * Base display object
 ******************************************************************************/

static struct nouveau_oclass
nva3_disp_sclass[] = {
	{ GT214_DISP_CORE_CHANNEL_DMA, &nv50_disp_core_ofuncs.base },
	{ GT214_DISP_BASE_CHANNEL_DMA, &nv50_disp_base_ofuncs.base },
	{ GT214_DISP_OVERLAY_CHANNEL_DMA, &nv50_disp_ovly_ofuncs.base },
	{ GT214_DISP_OVERLAY, &nv50_disp_oimm_ofuncs.base },
	{ GT214_DISP_CURSOR, &nv50_disp_curs_ofuncs.base },
	{}
};

static struct nouveau_oclass
nva3_disp_main_oclass[] = {
	{ GT214_DISP, &nv50_disp_main_ofuncs },
	{}
};

/*******************************************************************************
 * Display engine implementation
 ******************************************************************************/

static int
nva3_disp_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv50_disp_priv *priv;
	int ret;

	ret = nouveau_disp_create(parent, engine, oclass, 2, "PDISP",
				  "display", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nvkm_event_init(&nv50_disp_chan_uevent, 1, 9, &priv->uevent);
	if (ret)
		return ret;

	nv_engine(priv)->sclass = nva3_disp_main_oclass;
	nv_engine(priv)->cclass = &nv50_disp_cclass;
	nv_subdev(priv)->intr = nv50_disp_intr;
	INIT_WORK(&priv->supervisor, nv50_disp_intr_supervisor);
	priv->sclass = nva3_disp_sclass;
	priv->head.nr = 2;
	priv->dac.nr = 3;
	priv->sor.nr = 4;
	priv->pior.nr = 3;
	priv->dac.power = nv50_dac_power;
	priv->dac.sense = nv50_dac_sense;
	priv->sor.power = nv50_sor_power;
	priv->sor.hda_eld = nva3_hda_eld;
	priv->sor.hdmi = nva3_hdmi_ctrl;
	priv->pior.power = nv50_pior_power;
	return 0;
}

struct nouveau_oclass *
nva3_disp_oclass = &(struct nv50_disp_impl) {
	.base.base.handle = NV_ENGINE(DISP, 0x85),
	.base.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nva3_disp_ctor,
		.dtor = _nouveau_disp_dtor,
		.init = _nouveau_disp_init,
		.fini = _nouveau_disp_fini,
	},
	.base.vblank = &nv50_disp_vblank_func,
	.base.outp =  nv94_disp_outp_sclass,
	.mthd.core = &nv94_disp_core_mthd_chan,
	.mthd.base = &nv84_disp_base_mthd_chan,
	.mthd.ovly = &nv84_disp_ovly_mthd_chan,
	.mthd.prev = 0x000004,
	.head.scanoutpos = nv50_disp_main_scanoutpos,
}.base.base;
