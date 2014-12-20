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
nvf0_disp_sclass[] = {
	{ GK110_DISP_CORE_CHANNEL_DMA, &nvd0_disp_core_ofuncs.base },
	{ GK110_DISP_BASE_CHANNEL_DMA, &nvd0_disp_base_ofuncs.base },
	{ GK104_DISP_OVERLAY_CONTROL_DMA, &nvd0_disp_ovly_ofuncs.base },
	{ GK104_DISP_OVERLAY, &nvd0_disp_oimm_ofuncs.base },
	{ GK104_DISP_CURSOR, &nvd0_disp_curs_ofuncs.base },
	{}
};

static struct nouveau_oclass
nvf0_disp_main_oclass[] = {
	{ GK110_DISP, &nvd0_disp_main_ofuncs },
	{}
};

/*******************************************************************************
 * Display engine implementation
 ******************************************************************************/

static int
nvf0_disp_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
	       struct nouveau_oclass *oclass, void *data, u32 size,
	       struct nouveau_object **pobject)
{
	struct nv50_disp_priv *priv;
	int heads = nv_rd32(parent, 0x022448);
	int ret;

	ret = nouveau_disp_create(parent, engine, oclass, heads,
				  "PDISP", "display", &priv);
	*pobject = nv_object(priv);
	if (ret)
		return ret;

	ret = nvkm_event_init(&nvd0_disp_chan_uevent, 1, 17, &priv->uevent);
	if (ret)
		return ret;

	nv_engine(priv)->sclass = nvf0_disp_main_oclass;
	nv_engine(priv)->cclass = &nv50_disp_cclass;
	nv_subdev(priv)->intr = nvd0_disp_intr;
	INIT_WORK(&priv->supervisor, nvd0_disp_intr_supervisor);
	priv->sclass = nvf0_disp_sclass;
	priv->head.nr = heads;
	priv->dac.nr = 3;
	priv->sor.nr = 4;
	priv->dac.power = nv50_dac_power;
	priv->dac.sense = nv50_dac_sense;
	priv->sor.power = nv50_sor_power;
	priv->sor.hda_eld = nvd0_hda_eld;
	priv->sor.hdmi = nve0_hdmi_ctrl;
	return 0;
}

struct nouveau_oclass *
nvf0_disp_oclass = &(struct nv50_disp_impl) {
	.base.base.handle = NV_ENGINE(DISP, 0x92),
	.base.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nvf0_disp_ctor,
		.dtor = _nouveau_disp_dtor,
		.init = _nouveau_disp_init,
		.fini = _nouveau_disp_fini,
	},
	.base.vblank = &nvd0_disp_vblank_func,
	.base.outp =  nvd0_disp_outp_sclass,
	.mthd.core = &nve0_disp_core_mthd_chan,
	.mthd.base = &nvd0_disp_base_mthd_chan,
	.mthd.ovly = &nve0_disp_ovly_mthd_chan,
	.mthd.prev = -0x020000,
	.head.scanoutpos = nvd0_disp_main_scanoutpos,
}.base.base;
