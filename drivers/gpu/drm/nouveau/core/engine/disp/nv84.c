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
 * EVO master channel object
 ******************************************************************************/

const struct nv50_disp_mthd_list
nv84_disp_mast_mthd_dac = {
	.mthd = 0x0080,
	.addr = 0x000008,
	.data = {
		{ 0x0400, 0x610b58 },
		{ 0x0404, 0x610bdc },
		{ 0x0420, 0x610bc4 },
		{}
	}
};

const struct nv50_disp_mthd_list
nv84_disp_mast_mthd_head = {
	.mthd = 0x0400,
	.addr = 0x000540,
	.data = {
		{ 0x0800, 0x610ad8 },
		{ 0x0804, 0x610ad0 },
		{ 0x0808, 0x610a48 },
		{ 0x080c, 0x610a78 },
		{ 0x0810, 0x610ac0 },
		{ 0x0814, 0x610af8 },
		{ 0x0818, 0x610b00 },
		{ 0x081c, 0x610ae8 },
		{ 0x0820, 0x610af0 },
		{ 0x0824, 0x610b08 },
		{ 0x0828, 0x610b10 },
		{ 0x082c, 0x610a68 },
		{ 0x0830, 0x610a60 },
		{ 0x0834, 0x000000 },
		{ 0x0838, 0x610a40 },
		{ 0x0840, 0x610a24 },
		{ 0x0844, 0x610a2c },
		{ 0x0848, 0x610aa8 },
		{ 0x084c, 0x610ab0 },
		{ 0x085c, 0x610c5c },
		{ 0x0860, 0x610a84 },
		{ 0x0864, 0x610a90 },
		{ 0x0868, 0x610b18 },
		{ 0x086c, 0x610b20 },
		{ 0x0870, 0x610ac8 },
		{ 0x0874, 0x610a38 },
		{ 0x0878, 0x610c50 },
		{ 0x0880, 0x610a58 },
		{ 0x0884, 0x610a9c },
		{ 0x089c, 0x610c68 },
		{ 0x08a0, 0x610a70 },
		{ 0x08a4, 0x610a50 },
		{ 0x08a8, 0x610ae0 },
		{ 0x08c0, 0x610b28 },
		{ 0x08c4, 0x610b30 },
		{ 0x08c8, 0x610b40 },
		{ 0x08d4, 0x610b38 },
		{ 0x08d8, 0x610b48 },
		{ 0x08dc, 0x610b50 },
		{ 0x0900, 0x610a18 },
		{ 0x0904, 0x610ab8 },
		{ 0x0910, 0x610c70 },
		{ 0x0914, 0x610c78 },
		{}
	}
};

const struct nv50_disp_mthd_chan
nv84_disp_mast_mthd_chan = {
	.name = "Core",
	.addr = 0x000000,
	.data = {
		{ "Global", 1, &nv50_disp_mast_mthd_base },
		{    "DAC", 3, &nv84_disp_mast_mthd_dac  },
		{    "SOR", 2, &nv50_disp_mast_mthd_sor  },
		{   "PIOR", 3, &nv50_disp_mast_mthd_pior },
		{   "HEAD", 2, &nv84_disp_mast_mthd_head },
		{}
	}
};

/*******************************************************************************
 * EVO sync channel objects
 ******************************************************************************/

static const struct nv50_disp_mthd_list
nv84_disp_sync_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x0008c4 },
		{ 0x0088, 0x0008d0 },
		{ 0x008c, 0x0008dc },
		{ 0x0090, 0x0008e4 },
		{ 0x0094, 0x610884 },
		{ 0x00a0, 0x6108a0 },
		{ 0x00a4, 0x610878 },
		{ 0x00c0, 0x61086c },
		{ 0x00c4, 0x610800 },
		{ 0x00c8, 0x61080c },
		{ 0x00cc, 0x610818 },
		{ 0x00e0, 0x610858 },
		{ 0x00e4, 0x610860 },
		{ 0x00e8, 0x6108ac },
		{ 0x00ec, 0x6108b4 },
		{ 0x00fc, 0x610824 },
		{ 0x0100, 0x610894 },
		{ 0x0104, 0x61082c },
		{ 0x0110, 0x6108bc },
		{ 0x0114, 0x61088c },
		{}
	}
};

const struct nv50_disp_mthd_chan
nv84_disp_sync_mthd_chan = {
	.name = "Base",
	.addr = 0x000540,
	.data = {
		{ "Global", 1, &nv84_disp_sync_mthd_base },
		{  "Image", 2, &nv50_disp_sync_mthd_image },
		{}
	}
};

/*******************************************************************************
 * EVO overlay channel objects
 ******************************************************************************/

static const struct nv50_disp_mthd_list
nv84_disp_ovly_mthd_base = {
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

const struct nv50_disp_mthd_chan
nv84_disp_ovly_mthd_chan = {
	.name = "Overlay",
	.addr = 0x000540,
	.data = {
		{ "Global", 1, &nv84_disp_ovly_mthd_base },
		{}
	}
};

/*******************************************************************************
 * Base display object
 ******************************************************************************/

static struct nouveau_oclass
nv84_disp_sclass[] = {
	{ G82_DISP_CORE_CHANNEL_DMA, &nv50_disp_mast_ofuncs.base },
	{ G82_DISP_BASE_CHANNEL_DMA, &nv50_disp_sync_ofuncs.base },
	{ G82_DISP_OVERLAY_CHANNEL_DMA, &nv50_disp_ovly_ofuncs.base },
	{ G82_DISP_OVERLAY, &nv50_disp_oimm_ofuncs.base },
	{ G82_DISP_CURSOR, &nv50_disp_curs_ofuncs.base },
	{}
};

static struct nouveau_oclass
nv84_disp_base_oclass[] = {
	{ G82_DISP, &nv50_disp_base_ofuncs },
	{}
};

/*******************************************************************************
 * Display engine implementation
 ******************************************************************************/

static int
nv84_disp_ctor(struct nouveau_object *parent, struct nouveau_object *engine,
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

	nv_engine(priv)->sclass = nv84_disp_base_oclass;
	nv_engine(priv)->cclass = &nv50_disp_cclass;
	nv_subdev(priv)->intr = nv50_disp_intr;
	INIT_WORK(&priv->supervisor, nv50_disp_intr_supervisor);
	priv->sclass = nv84_disp_sclass;
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
nv84_disp_oclass = &(struct nv50_disp_impl) {
	.base.base.handle = NV_ENGINE(DISP, 0x82),
	.base.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv84_disp_ctor,
		.dtor = _nouveau_disp_dtor,
		.init = _nouveau_disp_init,
		.fini = _nouveau_disp_fini,
	},
	.base.vblank = &nv50_disp_vblank_func,
	.base.outp =  nv50_disp_outp_sclass,
	.mthd.core = &nv84_disp_mast_mthd_chan,
	.mthd.base = &nv84_disp_sync_mthd_chan,
	.mthd.ovly = &nv84_disp_ovly_mthd_chan,
	.mthd.prev = 0x000004,
	.head.scanoutpos = nv50_disp_base_scanoutpos,
}.base.base;
