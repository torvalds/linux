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

#include <core/class.h>

#include "nv50.h"

/*******************************************************************************
 * EVO master channel object
 ******************************************************************************/

const struct nv50_disp_mthd_list
nv94_disp_mast_mthd_sor = {
	.mthd = 0x0040,
	.addr = 0x000008,
	.data = {
		{ 0x0600, 0x610794 },
		{}
	}
};

const struct nv50_disp_mthd_chan
nv94_disp_mast_mthd_chan = {
	.name = "Core",
	.addr = 0x000000,
	.data = {
		{ "Global", 1, &nv50_disp_mast_mthd_base },
		{    "DAC", 3, &nv84_disp_mast_mthd_dac  },
		{    "SOR", 4, &nv94_disp_mast_mthd_sor  },
		{   "PIOR", 3, &nv50_disp_mast_mthd_pior },
		{   "HEAD", 2, &nv84_disp_mast_mthd_head },
		{}
	}
};

/*******************************************************************************
 * Base display object
 ******************************************************************************/

static struct nouveau_oclass
nv94_disp_sclass[] = {
	{ NV94_DISP_MAST_CLASS, &nv50_disp_mast_ofuncs },
	{ NV94_DISP_SYNC_CLASS, &nv50_disp_sync_ofuncs },
	{ NV94_DISP_OVLY_CLASS, &nv50_disp_ovly_ofuncs },
	{ NV94_DISP_OIMM_CLASS, &nv50_disp_oimm_ofuncs },
	{ NV94_DISP_CURS_CLASS, &nv50_disp_curs_ofuncs },
	{}
};

static struct nouveau_omthds
nv94_disp_base_omthds[] = {
	{ HEAD_MTHD(NV50_DISP_SCANOUTPOS)     , nv50_disp_base_scanoutpos },
	{ SOR_MTHD(NV50_DISP_SOR_PWR)         , nv50_sor_mthd },
	{ SOR_MTHD(NV84_DISP_SOR_HDMI_PWR)    , nv50_sor_mthd },
	{ SOR_MTHD(NV50_DISP_SOR_LVDS_SCRIPT) , nv50_sor_mthd },
	{ DAC_MTHD(NV50_DISP_DAC_PWR)         , nv50_dac_mthd },
	{ DAC_MTHD(NV50_DISP_DAC_LOAD)        , nv50_dac_mthd },
	{ PIOR_MTHD(NV50_DISP_PIOR_PWR)       , nv50_pior_mthd },
	{ PIOR_MTHD(NV50_DISP_PIOR_TMDS_PWR)  , nv50_pior_mthd },
	{ PIOR_MTHD(NV50_DISP_PIOR_DP_PWR)    , nv50_pior_mthd },
	{},
};

static struct nouveau_oclass
nv94_disp_base_oclass[] = {
	{ NV94_DISP_CLASS, &nv50_disp_base_ofuncs, nv94_disp_base_omthds },
	{}
};

/*******************************************************************************
 * Display engine implementation
 ******************************************************************************/

static int
nv94_disp_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
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

	nv_engine(priv)->sclass = nv94_disp_base_oclass;
	nv_engine(priv)->cclass = &nv50_disp_cclass;
	nv_subdev(priv)->intr = nv50_disp_intr;
	INIT_WORK(&priv->supervisor, nv50_disp_intr_supervisor);
	priv->sclass = nv94_disp_sclass;
	priv->head.nr = 2;
	priv->dac.nr = 3;
	priv->sor.nr = 4;
	priv->pior.nr = 3;
	priv->dac.power = nv50_dac_power;
	priv->dac.sense = nv50_dac_sense;
	priv->sor.power = nv50_sor_power;
	priv->sor.hdmi = nv84_hdmi_ctrl;
	priv->sor.dp = &nv94_sor_dp_func;
	priv->pior.power = nv50_pior_power;
	priv->pior.dp = &nv50_pior_dp_func;
	return 0;
}

struct nouveau_oclass *
nv94_disp_oclass = &(struct nv50_disp_impl) {
	.base.base.handle = NV_ENGINE(DISP, 0x88),
	.base.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv94_disp_ctor,
		.dtor = _nouveau_disp_dtor,
		.init = _nouveau_disp_init,
		.fini = _nouveau_disp_fini,
	},
	.mthd.core = &nv94_disp_mast_mthd_chan,
	.mthd.base = &nv84_disp_sync_mthd_chan,
	.mthd.ovly = &nv84_disp_ovly_mthd_chan,
	.mthd.prev = 0x000004,
}.base.base;
