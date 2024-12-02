/*
 * Copyright 2017 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "ram.h"

u32
gm200_ram_probe_fbp_amount(const struct nvkm_ram_func *func, u32 fbpao,
			   struct nvkm_device *device, int fbp, int *pltcs)
{
	u32 ltcs  = nvkm_rd32(device, 0x022450);
	u32 fbpas = nvkm_rd32(device, 0x022458);
	u32 fbpa  = fbp * fbpas;
	u32 size  = 0;
	if (!(nvkm_rd32(device, 0x021d38) & BIT(fbp))) {
		u32 ltco = nvkm_rd32(device, 0x021d70 + (fbp * 4));
		u32 ltcm = ~ltco & ((1 << ltcs) - 1);

		while (fbpas--) {
			if (!(fbpao & (1 << fbpa)))
				size += func->probe_fbpa_amount(device, fbpa);
			fbpa++;
		}

		*pltcs = hweight32(ltcm);
	}
	return size;
}

static const struct nvkm_ram_func
gm200_ram = {
	.upper = 0x1000000000ULL,
	.probe_fbp = gm107_ram_probe_fbp,
	.probe_fbp_amount = gm200_ram_probe_fbp_amount,
	.probe_fbpa_amount = gf100_ram_probe_fbpa_amount,
	.dtor = gk104_ram_dtor,
	.init = gk104_ram_init,
	.calc = gk104_ram_calc,
	.prog = gk104_ram_prog,
	.tidy = gk104_ram_tidy,
};

int
gm200_ram_new(struct nvkm_fb *fb, struct nvkm_ram **pram)
{
	return gk104_ram_new_(&gm200_ram, fb, pram);
}
