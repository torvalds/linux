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

u64
gm107_devinit_disable(struct nvkm_devinit *init)
{
	struct nvkm_device *device = init->subdev.device;
	u32 r021c00 = nvkm_rd32(device, 0x021c00);
	u32 r021c04 = nvkm_rd32(device, 0x021c04);
	u64 disable = 0ULL;

	if (r021c00 & 0x00000001)
		disable |= (1ULL << NVKM_ENGINE_CE0);
	if (r021c00 & 0x00000004)
		disable |= (1ULL << NVKM_ENGINE_CE2);
	if (r021c04 & 0x00000001)
		disable |= (1ULL << NVKM_ENGINE_DISP);

	return disable;
}

static const struct nvkm_devinit_func
gm107_devinit = {
	.preinit = gf100_devinit_preinit,
	.init = nv50_devinit_init,
	.post = nv04_devinit_post,
	.pll_set = gf100_devinit_pll_set,
	.disable = gm107_devinit_disable,
};

int
gm107_devinit_new(struct nvkm_device *device, int index,
		struct nvkm_devinit **pinit)
{
	return nv50_devinit_new_(&gm107_devinit, device, index, pinit);
}
