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
 * EVO overlay channel objects
 ******************************************************************************/

static const struct nv50_disp_mthd_list
nva0_disp_ovly_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x6109a0 },
		{ 0x0088, 0x6109c0 },
		{ 0x008c, 0x6109c8 },
		{ 0x0090, 0x6109b4 },
		{ 0x0094, 0x610970 },
		{ 0x00a0, 0x610998 },
		{ 0x00a4, 0x610964 },
		{ 0x00b0, 0x610c98 },
		{ 0x00b4, 0x610ca4 },
		{ 0x00b8, 0x610cac },
		{ 0x00c0, 0x610958 },
		{ 0x00e0, 0x6109a8 },
		{ 0x00e4, 0x6109d0 },
		{ 0x00e8, 0x6109d8 },
		{ 0x0100, 0x61094c },
		{ 0x0104, 0x610984 },
		{ 0x0108, 0x61098c },
		{ 0x0800, 0x6109f8 },
		{ 0x0808, 0x610a08 },
		{ 0x080c, 0x610a10 },
		{ 0x0810, 0x610a00 },
		{}
	}
};

static const struct nv50_disp_mthd_chan
nva0_disp_ovly_mthd_chan = {
	.name = "Overlay",
	.addr = 0x000540,
	.data = {
		{ "Global", 1, &nva0_disp_ovly_mthd_base },
		{}
	}
};

/*******************************************************************************
 * Base display object
 ******************************************************************************/

static struct nouveau_oclass
nva0_disp_sclass[] = {
	{ GT200_DISP_CORE_CHANNEL_DMA, &nv50_disp_core_ofuncs.base },
	{ GT200_DISP_BASE_CHANNEL_DMA, &nv50_disp_base_ofuncs.base },
	{ GT200_DISP_OVERLAY_CHANNEL_DMA, &nv50_disp_ovly_ofuncs.base },
	{ G82_DISP_OVERLAY, &nv50_disp_oimm_ofuncs.base },
	{ G82_DISP_CURSOR, &nv50_disp_curs_ofuncs.base },
	{}
};

static struct nouveau_oclass
nva0_disp_main_oclass[] = {
	{ GT200_DISP, &nv50_disp_main_ofuncs },
	{}
};

/*******************************************************************************
 * Display engine implementation
 ******************************************************************************/

static int
nva0_disp_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
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

	nv_engine(priv)->sclass = nva0_disp_main_oclass;
	nv_engine(priv)->cclass = &nv50_disp_cclass;
	nv_subdev(priv)->intr = nv50_disp_intr;
	INIT_WORK(&priv->supervisor, nv50_disp_intr_supervisor);
	priv->sclass = nva0_disp_sclass;
	priv->head.nr = 2;
	priv->dac.nr = 3;
	priv->sor.nr = 2;
	priv->pior.nr = 3;
	priv->dac.power = nv50_dac_power;
	priv->dac.sense = nv50_dac_sense;
	priv->sor.power = nv50_sor_power;
	priv->sor.hdmi = nv84_hdmi_ctrl;
	priv->pior.power = nv50_pior_power;
	return 0;
}

struct nouveau_oclass *
nva0_disp_oclass = &(struct nv50_disp_impl) {
	.base.base.handle = NV_ENGINE(DISP, 0x83),
	.base.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nva0_disp_ctor,
		.dtor = _nouveau_disp_dtor,
		.init = _nouveau_disp_init,
		.fini = _nouveau_disp_fini,
	},
	.base.vblank = &nv50_disp_vblank_func,
	.base.outp =  nv50_disp_outp_sclass,
	.mthd.core = &nv84_disp_core_mthd_chan,
	.mthd.base = &nv84_disp_base_mthd_chan,
	.mthd.ovly = &nva0_disp_ovly_mthd_chan,
	.mthd.prev = 0x000004,
	.head.scanoutpos = nv50_disp_main_scanoutpos,
}.base.base;
