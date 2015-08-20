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
#include "nv40.h"

#include <subdev/bios.h>
#include <subdev/bios/bit.h>
#include <subdev/bios/init.h>
#include <subdev/bios/pll.h>
#include <subdev/clk/pll.h>
#include <subdev/timer.h>

int
nv40_ram_calc(struct nvkm_fb *fb, u32 freq)
{
	struct nvkm_subdev *subdev = &fb->subdev;
	struct nvkm_bios *bios = subdev->device->bios;
	struct nv40_ram *ram = (void *)fb->ram;
	struct nvbios_pll pll;
	int N1, M1, N2, M2;
	int log2P, ret;

	ret = nvbios_pll_parse(bios, 0x04, &pll);
	if (ret) {
		nvkm_error(subdev, "mclk pll data not found\n");
		return ret;
	}

	ret = nv04_pll_calc(nv_subdev(fb), &pll, freq,
			    &N1, &M1, &N2, &M2, &log2P);
	if (ret < 0)
		return ret;

	ram->ctrl  = 0x80000000 | (log2P << 16);
	ram->ctrl |= min(pll.bias_p + log2P, (int)pll.max_p) << 20;
	if (N2 == M2) {
		ram->ctrl |= 0x00000100;
		ram->coef  = (N1 << 8) | M1;
	} else {
		ram->ctrl |= 0x40000000;
		ram->coef  = (N2 << 24) | (M2 << 16) | (N1 << 8) | M1;
	}

	return 0;
}

int
nv40_ram_prog(struct nvkm_fb *fb)
{
	struct nvkm_device *device = fb->subdev.device;
	struct nvkm_bios *bios = device->bios;
	struct nv40_ram *ram = (void *)fb->ram;
	struct bit_entry M;
	u32 crtc_mask = 0;
	u8  sr1[2];
	int i;

	/* determine which CRTCs are active, fetch VGA_SR1 for each */
	for (i = 0; i < 2; i++) {
		u32 vbl = nvkm_rd32(device, 0x600808 + (i * 0x2000));
		u32 cnt = 0;
		do {
			if (vbl != nvkm_rd32(device, 0x600808 + (i * 0x2000))) {
				nvkm_wr08(device, 0x0c03c4 + (i * 0x2000), 0x01);
				sr1[i] = nvkm_rd08(device, 0x0c03c5 + (i * 0x2000));
				if (!(sr1[i] & 0x20))
					crtc_mask |= (1 << i);
				break;
			}
			udelay(1);
		} while (cnt++ < 32);
	}

	/* wait for vblank start on active crtcs, disable memory access */
	for (i = 0; i < 2; i++) {
		if (!(crtc_mask & (1 << i)))
			continue;

		nvkm_msec(device, 2000,
			u32 tmp = nvkm_rd32(device, 0x600808 + (i * 0x2000));
			if (!(tmp & 0x00010000))
				break;
		);

		nvkm_msec(device, 2000,
			u32 tmp = nvkm_rd32(device, 0x600808 + (i * 0x2000));
			if ( (tmp & 0x00010000))
				break;
		);

		nvkm_wr08(device, 0x0c03c4 + (i * 0x2000), 0x01);
		nvkm_wr08(device, 0x0c03c5 + (i * 0x2000), sr1[i] | 0x20);
	}

	/* prepare ram for reclocking */
	nvkm_wr32(device, 0x1002d4, 0x00000001); /* precharge */
	nvkm_wr32(device, 0x1002d0, 0x00000001); /* refresh */
	nvkm_wr32(device, 0x1002d0, 0x00000001); /* refresh */
	nvkm_mask(device, 0x100210, 0x80000000, 0x00000000); /* no auto refresh */
	nvkm_wr32(device, 0x1002dc, 0x00000001); /* enable self-refresh */

	/* change the PLL of each memory partition */
	nvkm_mask(device, 0x00c040, 0x0000c000, 0x00000000);
	switch (device->chipset) {
	case 0x40:
	case 0x45:
	case 0x41:
	case 0x42:
	case 0x47:
		nvkm_mask(device, 0x004044, 0xc0771100, ram->ctrl);
		nvkm_mask(device, 0x00402c, 0xc0771100, ram->ctrl);
		nvkm_wr32(device, 0x004048, ram->coef);
		nvkm_wr32(device, 0x004030, ram->coef);
	case 0x43:
	case 0x49:
	case 0x4b:
		nvkm_mask(device, 0x004038, 0xc0771100, ram->ctrl);
		nvkm_wr32(device, 0x00403c, ram->coef);
	default:
		nvkm_mask(device, 0x004020, 0xc0771100, ram->ctrl);
		nvkm_wr32(device, 0x004024, ram->coef);
		break;
	}
	udelay(100);
	nvkm_mask(device, 0x00c040, 0x0000c000, 0x0000c000);

	/* re-enable normal operation of memory controller */
	nvkm_wr32(device, 0x1002dc, 0x00000000);
	nvkm_mask(device, 0x100210, 0x80000000, 0x80000000);
	udelay(100);

	/* execute memory reset script from vbios */
	if (!bit_entry(bios, 'M', &M)) {
		struct nvbios_init init = {
			.subdev = nv_subdev(fb),
			.bios = bios,
			.offset = nvbios_rd16(bios, M.offset + 0x00),
			.execute = 1,
		};

		nvbios_exec(&init);
	}

	/* make sure we're in vblank (hopefully the same one as before), and
	 * then re-enable crtc memory access
	 */
	for (i = 0; i < 2; i++) {
		if (!(crtc_mask & (1 << i)))
			continue;

		nvkm_msec(device, 2000,
			u32 tmp = nvkm_rd32(device, 0x600808 + (i * 0x2000));
			if ( (tmp & 0x00010000))
				break;
		);

		nvkm_wr08(device, 0x0c03c4 + (i * 0x2000), 0x01);
		nvkm_wr08(device, 0x0c03c5 + (i * 0x2000), sr1[i]);
	}

	return 0;
}

void
nv40_ram_tidy(struct nvkm_fb *fb)
{
}

static int
nv40_ram_create(struct nvkm_object *parent, struct nvkm_object *engine,
		struct nvkm_oclass *oclass, void *data, u32 size,
		struct nvkm_object **pobject)
{
	struct nvkm_fb *fb = nvkm_fb(parent);
	struct nv40_ram *ram;
	struct nvkm_device *device = fb->subdev.device;
	u32 pbus1218 = nvkm_rd32(device, 0x001218);
	int ret;

	ret = nvkm_ram_create(parent, engine, oclass, &ram);
	*pobject = nv_object(ram);
	if (ret)
		return ret;

	switch (pbus1218 & 0x00000300) {
	case 0x00000000: ram->base.type = NV_MEM_TYPE_SDRAM; break;
	case 0x00000100: ram->base.type = NV_MEM_TYPE_DDR1; break;
	case 0x00000200: ram->base.type = NV_MEM_TYPE_GDDR3; break;
	case 0x00000300: ram->base.type = NV_MEM_TYPE_DDR2; break;
	}

	ram->base.size  =  nvkm_rd32(device, 0x10020c) & 0xff000000;
	ram->base.parts = (nvkm_rd32(device, 0x100200) & 0x00000003) + 1;
	ram->base.tags  =  nvkm_rd32(device, 0x100320);
	ram->base.calc = nv40_ram_calc;
	ram->base.prog = nv40_ram_prog;
	ram->base.tidy = nv40_ram_tidy;
	return 0;
}


struct nvkm_oclass
nv40_ram_oclass = {
	.handle = 0,
	.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv40_ram_create,
		.dtor = _nvkm_ram_dtor,
		.init = _nvkm_ram_init,
		.fini = _nvkm_ram_fini,
	}
};
