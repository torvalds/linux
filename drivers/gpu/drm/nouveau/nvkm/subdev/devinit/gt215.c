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
#include <subdev/bios/pll.h>
#include <subdev/clk/pll.h>

int
gt215_devinit_pll_set(struct nvkm_devinit *init, u32 type, u32 freq)
{
	struct nvkm_subdev *subdev = &init->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvbios_pll info;
	int N, fN, M, P;
	int ret;

	ret = nvbios_pll_parse(device->bios, type, &info);
	if (ret)
		return ret;

	ret = gt215_pll_calc(subdev, &info, freq, &N, &fN, &M, &P);
	if (ret < 0)
		return ret;

	switch (info.type) {
	case PLL_VPLL0:
	case PLL_VPLL1:
		nvkm_wr32(device, info.reg + 0, 0x50000610);
		nvkm_mask(device, info.reg + 4, 0x003fffff,
						(P << 16) | (M << 8) | N);
		nvkm_wr32(device, info.reg + 8, fN);
		break;
	default:
		nvkm_warn(subdev, "%08x/%dKhz unimplemented\n", type, freq);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static u64
gt215_devinit_disable(struct nvkm_devinit *init)
{
	struct nvkm_device *device = init->subdev.device;
	u32 r001540 = nvkm_rd32(device, 0x001540);
	u32 r00154c = nvkm_rd32(device, 0x00154c);
	u64 disable = 0ULL;

	if (!(r001540 & 0x40000000)) {
		disable |= (1ULL << NVKM_ENGINE_MSPDEC);
		disable |= (1ULL << NVKM_ENGINE_MSPPP);
	}

	if (!(r00154c & 0x00000004))
		disable |= (1ULL << NVKM_ENGINE_DISP);
	if (!(r00154c & 0x00000020))
		disable |= (1ULL << NVKM_ENGINE_MSVLD);
	if (!(r00154c & 0x00000200))
		disable |= (1ULL << NVKM_ENGINE_CE0);

	return disable;
}

static u32
gt215_devinit_mmio_part[] = {
	0x100720, 0x1008bc, 4,
	0x100a20, 0x100adc, 4,
	0x100d80, 0x100ddc, 4,
	0x110000, 0x110f9c, 4,
	0x111000, 0x11103c, 8,
	0x111080, 0x1110fc, 4,
	0x111120, 0x1111fc, 4,
	0x111300, 0x1114bc, 4,
	0,
};

static u32
gt215_devinit_mmio(struct nvkm_devinit *base, u32 addr)
{
	struct nv50_devinit *init = nv50_devinit(base);
	struct nvkm_device *device = init->base.subdev.device;
	u32 *mmio = gt215_devinit_mmio_part;

	/* the init tables on some boards have INIT_RAM_RESTRICT_ZM_REG_GROUP
	 * instructions which touch registers that may not even exist on
	 * some configurations (Quadro 400), which causes the register
	 * interface to screw up for some amount of time after attempting to
	 * write to one of these, and results in all sorts of things going
	 * horribly wrong.
	 *
	 * the binary driver avoids touching these registers at all, however,
	 * the video bios doesn't care and does what the scripts say.  it's
	 * presumed that the io-port access to init registers isn't effected
	 * by the screw-up bug mentioned above.
	 *
	 * really, a new opcode should've been invented to handle these
	 * requirements, but whatever, it's too late for that now.
	 */
	while (mmio[0]) {
		if (addr >= mmio[0] && addr <= mmio[1]) {
			u32 part = (addr / mmio[2]) & 7;
			if (!init->r001540)
				init->r001540 = nvkm_rd32(device, 0x001540);
			if (part >= hweight8((init->r001540 >> 16) & 0xff))
				return ~0;
			return addr;
		}
		mmio += 3;
	}

	return addr;
}

static const struct nvkm_devinit_func
gt215_devinit = {
	.preinit = nv50_devinit_preinit,
	.init = nv50_devinit_init,
	.post = nv04_devinit_post,
	.mmio = gt215_devinit_mmio,
	.pll_set = gt215_devinit_pll_set,
	.disable = gt215_devinit_disable,
};

int
gt215_devinit_new(struct nvkm_device *device, int index,
		struct nvkm_devinit **pinit)
{
	return nv50_devinit_new_(&gt215_devinit, device, index, pinit);
}
