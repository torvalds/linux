/*
 * Copyright 2021 Red Hat Inc.
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
 */
#include "nv50.h"

#include <subdev/bios.h>
#include <subdev/bios/pll.h>
#include <subdev/clk/pll.h>
#include <subdev/gsp.h>

static int
ga100_devinit_pll_set(struct nvkm_devinit *init, u32 type, u32 freq)
{
	struct nvkm_subdev *subdev = &init->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvbios_pll info;
	int head = type - PLL_VPLL0;
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
	case PLL_VPLL2:
	case PLL_VPLL3:
		nvkm_wr32(device, 0x00ef00 + (head * 0x40), 0x02080004);
		nvkm_wr32(device, 0x00ef18 + (head * 0x40), (N << 16) | fN);
		nvkm_wr32(device, 0x00ef04 + (head * 0x40), (P << 16) | M);
		nvkm_wr32(device, 0x00e9c0 + (head * 0x04), 0x00000001);
		break;
	default:
		nvkm_warn(subdev, "%08x/%dKhz unimplemented\n", type, freq);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static void
ga100_devinit_disable(struct nvkm_devinit *init)
{
	struct nvkm_device *device = init->subdev.device;
	u32 r820c04 = nvkm_rd32(device, 0x820c04);

	if (r820c04 & 0x00000001)
		nvkm_subdev_disable(device, NVKM_ENGINE_DISP, 0);
}

static const struct nvkm_devinit_func
ga100_devinit = {
	.disable = ga100_devinit_disable,
	.init = nv50_devinit_init,
	.post = tu102_devinit_post,
	.pll_set = ga100_devinit_pll_set,
};

int
ga100_devinit_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
		  struct nvkm_devinit **pinit)
{
	if (nvkm_gsp_rm(device->gsp))
		return r535_devinit_new(&ga100_devinit, device, type, inst, pinit);

	return nv50_devinit_new_(&ga100_devinit, device, type, inst, pinit);
}
