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

const struct nouveau_mc_intr
nv50_mc_intr[] = {
	{ 0x04000000, NVDEV_ENGINE_DISP },  /* DISP before FIFO, so pageflip-timestamping works! */
	{ 0x00000001, NVDEV_ENGINE_MPEG },
	{ 0x00000100, NVDEV_ENGINE_FIFO },
	{ 0x00001000, NVDEV_ENGINE_GR },
	{ 0x00004000, NVDEV_ENGINE_CRYPT },	/* NV84- */
	{ 0x00008000, NVDEV_ENGINE_BSP },	/* NV84- */
	{ 0x00020000, NVDEV_ENGINE_VP },	/* NV84- */
	{ 0x00100000, NVDEV_SUBDEV_TIMER },
	{ 0x00200000, NVDEV_SUBDEV_GPIO },	/* PMGR->GPIO */
	{ 0x00200000, NVDEV_SUBDEV_I2C }, 	/* PMGR->I2C/AUX */
	{ 0x10000000, NVDEV_SUBDEV_BUS },
	{ 0x80000000, NVDEV_ENGINE_SW },
	{ 0x0002d101, NVDEV_SUBDEV_FB },
	{},
};

static void
nv50_mc_msi_rearm(struct nouveau_mc *pmc)
{
	struct nouveau_device *device = nv_device(pmc);
	pci_write_config_byte(device->pdev, 0x68, 0xff);
}

int
nv50_mc_init(struct nouveau_object *object)
{
	struct nv04_mc_priv *priv = (void *)object;
	nv_wr32(priv, 0x000200, 0xffffffff); /* everything on */
	return nouveau_mc_init(&priv->base);
}

struct nouveau_oclass *
nv50_mc_oclass = &(struct nouveau_mc_oclass) {
	.base.handle = NV_SUBDEV(MC, 0x50),
	.base.ofuncs = &(struct nouveau_ofuncs) {
		.ctor = nv04_mc_ctor,
		.dtor = _nouveau_mc_dtor,
		.init = nv50_mc_init,
		.fini = _nouveau_mc_fini,
	},
	.intr = nv50_mc_intr,
	.msi_rearm = nv50_mc_msi_rearm,
}.base;
