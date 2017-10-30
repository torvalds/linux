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
#include <subdev/bios/dcb.h>
#include <subdev/bios/disp.h>
#include <subdev/bios/init.h>
#include <subdev/bios/pll.h>
#include <subdev/clk/pll.h>
#include <subdev/vga.h>

int
nv50_devinit_pll_set(struct nvkm_devinit *init, u32 type, u32 freq)
{
	struct nvkm_subdev *subdev = &init->subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_bios *bios = device->bios;
	struct nvbios_pll info;
	int N1, M1, N2, M2, P;
	int ret;

	ret = nvbios_pll_parse(bios, type, &info);
	if (ret) {
		nvkm_error(subdev, "failed to retrieve pll data, %d\n", ret);
		return ret;
	}

	ret = nv04_pll_calc(subdev, &info, freq, &N1, &M1, &N2, &M2, &P);
	if (!ret) {
		nvkm_error(subdev, "failed pll calculation\n");
		return -EINVAL;
	}

	switch (info.type) {
	case PLL_VPLL0:
	case PLL_VPLL1:
		nvkm_wr32(device, info.reg + 0, 0x10000611);
		nvkm_mask(device, info.reg + 4, 0x00ff00ff, (M1 << 16) | N1);
		nvkm_mask(device, info.reg + 8, 0x7fff00ff, (P  << 28) |
							    (M2 << 16) | N2);
		break;
	case PLL_MEMORY:
		nvkm_mask(device, info.reg + 0, 0x01ff0000,
					        (P << 22) |
						(info.bias_p << 19) |
						(P << 16));
		nvkm_wr32(device, info.reg + 4, (N1 << 8) | M1);
		break;
	default:
		nvkm_mask(device, info.reg + 0, 0x00070000, (P << 16));
		nvkm_wr32(device, info.reg + 4, (N1 << 8) | M1);
		break;
	}

	return 0;
}

static u64
nv50_devinit_disable(struct nvkm_devinit *init)
{
	struct nvkm_device *device = init->subdev.device;
	u32 r001540 = nvkm_rd32(device, 0x001540);
	u64 disable = 0ULL;

	if (!(r001540 & 0x40000000))
		disable |= (1ULL << NVKM_ENGINE_MPEG);

	return disable;
}

void
nv50_devinit_preinit(struct nvkm_devinit *base)
{
	struct nvkm_subdev *subdev = &base->subdev;
	struct nvkm_device *device = subdev->device;

	/* our heuristics can't detect whether the board has had its
	 * devinit scripts executed or not if the display engine is
	 * missing, assume it's a secondary gpu which requires post
	 */
	if (!base->post) {
		u64 disable = nvkm_devinit_disable(base);
		if (disable & (1ULL << NVKM_ENGINE_DISP))
			base->post = true;
	}

	/* magic to detect whether or not x86 vbios code has executed
	 * the devinit scripts to initialise the board
	 */
	if (!base->post) {
		if (!nvkm_rdvgac(device, 0, 0x00) &&
		    !nvkm_rdvgac(device, 0, 0x1a)) {
			nvkm_debug(subdev, "adaptor not initialised\n");
			base->post = true;
		}
	}
}

void
nv50_devinit_init(struct nvkm_devinit *base)
{
	struct nv50_devinit *init = nv50_devinit(base);
	struct nvkm_subdev *subdev = &init->base.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_bios *bios = device->bios;
	struct nvbios_outp info;
	struct dcb_output outp;
	u8  ver = 0xff, hdr, cnt, len;
	int i = 0;

	/* if we ran the init tables, we have to execute the first script
	 * pointer of each dcb entry's display encoder table in order
	 * to properly initialise each encoder.
	 */
	while (init->base.post && dcb_outp_parse(bios, i, &ver, &hdr, &outp)) {
		if (nvbios_outp_match(bios, outp.hasht, outp.hashm,
				      &ver, &hdr, &cnt, &len, &info)) {
			nvbios_init(subdev, info.script[0],
				init.outp = &outp;
				init.or   = ffs(outp.or) - 1;
				init.link = outp.sorconf.link == 2;
			);
		}
		i++;
	}
}

int
nv50_devinit_new_(const struct nvkm_devinit_func *func,
		  struct nvkm_device *device, int index,
		  struct nvkm_devinit **pinit)
{
	struct nv50_devinit *init;

	if (!(init = kzalloc(sizeof(*init), GFP_KERNEL)))
		return -ENOMEM;
	*pinit = &init->base;

	nvkm_devinit_ctor(func, device, index, &init->base);
	return 0;
}

static const struct nvkm_devinit_func
nv50_devinit = {
	.preinit = nv50_devinit_preinit,
	.init = nv50_devinit_init,
	.post = nv04_devinit_post,
	.pll_set = nv50_devinit_pll_set,
	.disable = nv50_devinit_disable,
};

int
nv50_devinit_new(struct nvkm_device *device, int index,
		 struct nvkm_devinit **pinit)
{
	return nv50_devinit_new_(&nv50_devinit, device, index, pinit);
}
