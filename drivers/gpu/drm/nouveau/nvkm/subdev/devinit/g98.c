/*
 * Copyright 2013 Red Hat Inc.
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

#include <subdev/bios.h>
#include <subdev/bios/init.h>

static u64
g98_devinit_disable(struct nvkm_devinit *init)
{
	struct nvkm_device *device = init->subdev.device;
	u32 r001540 = nvkm_rd32(device, 0x001540);
	u32 r00154c = nvkm_rd32(device, 0x00154c);

	if (!(r001540 & 0x40000000)) {
		nvkm_subdev_disable(device, NVKM_ENGINE_MSPDEC, 0);
		nvkm_subdev_disable(device, NVKM_ENGINE_MSVLD, 0);
		nvkm_subdev_disable(device, NVKM_ENGINE_MSPPP, 0);
	}

	if (!(r00154c & 0x00000004))
		nvkm_subdev_disable(device, NVKM_ENGINE_DISP, 0);
	if (!(r00154c & 0x00000020))
		nvkm_subdev_disable(device, NVKM_ENGINE_MSVLD, 0);
	if (!(r00154c & 0x00000040))
		nvkm_subdev_disable(device, NVKM_ENGINE_SEC, 0);

	return 0ULL;
}

static const struct nvkm_devinit_func
g98_devinit = {
	.preinit = nv50_devinit_preinit,
	.init = nv50_devinit_init,
	.post = nv04_devinit_post,
	.pll_set = nv50_devinit_pll_set,
	.disable = g98_devinit_disable,
};

int
g98_devinit_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
		struct nvkm_devinit **pinit)
{
	return nv50_devinit_new_(&g98_devinit, device, type, inst, pinit);
}
