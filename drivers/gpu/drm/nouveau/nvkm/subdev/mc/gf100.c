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
#include "nv04.h"

const struct nvkm_mc_intr
gf100_mc_intr[] = {
	{ 0x04000000, NVDEV_ENGINE_DISP },  /* DISP first, so pageflip timestamps work. */
	{ 0x00000001, NVDEV_ENGINE_MSPPP },
	{ 0x00000020, NVDEV_ENGINE_CE0 },
	{ 0x00000040, NVDEV_ENGINE_CE1 },
	{ 0x00000080, NVDEV_ENGINE_CE2 },
	{ 0x00000100, NVDEV_ENGINE_FIFO },
	{ 0x00001000, NVDEV_ENGINE_GR },
	{ 0x00002000, NVDEV_SUBDEV_FB },
	{ 0x00008000, NVDEV_ENGINE_MSVLD },
	{ 0x00040000, NVDEV_SUBDEV_THERM },
	{ 0x00020000, NVDEV_ENGINE_MSPDEC },
	{ 0x00100000, NVDEV_SUBDEV_TIMER },
	{ 0x00200000, NVDEV_SUBDEV_GPIO },	/* PMGR->GPIO */
	{ 0x00200000, NVDEV_SUBDEV_I2C },	/* PMGR->I2C/AUX */
	{ 0x01000000, NVDEV_SUBDEV_PMU },
	{ 0x02000000, NVDEV_SUBDEV_LTC },
	{ 0x08000000, NVDEV_SUBDEV_FB },
	{ 0x10000000, NVDEV_SUBDEV_BUS },
	{ 0x40000000, NVDEV_SUBDEV_IBUS },
	{ 0x80000000, NVDEV_ENGINE_SW },
	{},
};

static void
gf100_mc_msi_rearm(struct nvkm_mc *pmc)
{
	struct nv04_mc_priv *priv = (void *)pmc;
	nv_wr32(priv, 0x088704, 0x00000000);
}

void
gf100_mc_unk260(struct nvkm_mc *pmc, u32 data)
{
	nv_wr32(pmc, 0x000260, data);
}

struct nvkm_oclass *
gf100_mc_oclass = &(struct nvkm_mc_oclass) {
	.base.handle = NV_SUBDEV(MC, 0xc0),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_mc_ctor,
		.dtor = _nvkm_mc_dtor,
		.init = nv50_mc_init,
		.fini = _nvkm_mc_fini,
	},
	.intr = gf100_mc_intr,
	.msi_rearm = gf100_mc_msi_rearm,
	.unk260 = gf100_mc_unk260,
}.base;
