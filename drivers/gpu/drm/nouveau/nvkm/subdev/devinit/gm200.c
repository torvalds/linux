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
#include <subdev/bios/bit.h>
#include <subdev/bios/pmu.h>
#include <subdev/pmu.h>
#include <subdev/timer.h>

static void
pmu_code(struct nv50_devinit *init, u32 pmu, u32 img, u32 len, bool sec)
{
	struct nvkm_device *device = init->base.subdev.device;
	struct nvkm_bios *bios = device->bios;
	int i;

	nvkm_wr32(device, 0x10a180, 0x01000000 | (sec ? 0x10000000 : 0) | pmu);
	for (i = 0; i < len; i += 4) {
		if ((i & 0xff) == 0)
			nvkm_wr32(device, 0x10a188, (pmu + i) >> 8);
		nvkm_wr32(device, 0x10a184, nvbios_rd32(bios, img + i));
	}

	while (i & 0xff) {
		nvkm_wr32(device, 0x10a184, 0x00000000);
		i += 4;
	}
}

static void
pmu_data(struct nv50_devinit *init, u32 pmu, u32 img, u32 len)
{
	struct nvkm_device *device = init->base.subdev.device;
	struct nvkm_bios *bios = device->bios;
	int i;

	nvkm_wr32(device, 0x10a1c0, 0x01000000 | pmu);
	for (i = 0; i < len; i += 4)
		nvkm_wr32(device, 0x10a1c4, nvbios_rd32(bios, img + i));
}

static u32
pmu_args(struct nv50_devinit *init, u32 argp, u32 argi)
{
	struct nvkm_device *device = init->base.subdev.device;
	nvkm_wr32(device, 0x10a1c0, argp);
	nvkm_wr32(device, 0x10a1c0, nvkm_rd32(device, 0x10a1c4) + argi);
	return nvkm_rd32(device, 0x10a1c4);
}

static void
pmu_exec(struct nv50_devinit *init, u32 init_addr)
{
	struct nvkm_device *device = init->base.subdev.device;
	nvkm_wr32(device, 0x10a104, init_addr);
	nvkm_wr32(device, 0x10a10c, 0x00000000);
	nvkm_wr32(device, 0x10a100, 0x00000002);
}

static int
pmu_load(struct nv50_devinit *init, u8 type, bool post,
	 u32 *init_addr_pmu, u32 *args_addr_pmu)
{
	struct nvkm_subdev *subdev = &init->base.subdev;
	struct nvkm_bios *bios = subdev->device->bios;
	struct nvbios_pmuR pmu;
	int ret;

	if (!nvbios_pmuRm(bios, type, &pmu))
		return -EINVAL;

	if (!post || !subdev->device->pmu)
		return 0;

	ret = nvkm_falcon_reset(&subdev->device->pmu->falcon);
	if (ret)
		return ret;

	pmu_code(init, pmu.boot_addr_pmu, pmu.boot_addr, pmu.boot_size, false);
	pmu_code(init, pmu.code_addr_pmu, pmu.code_addr, pmu.code_size, true);
	pmu_data(init, pmu.data_addr_pmu, pmu.data_addr, pmu.data_size);

	if (init_addr_pmu) {
		*init_addr_pmu = pmu.init_addr_pmu;
		*args_addr_pmu = pmu.args_addr_pmu;
		return 0;
	}

	return pmu_exec(init, pmu.init_addr_pmu), 0;
}

void
gm200_devinit_preos(struct nv50_devinit *init, bool post)
{
	/* Optional: Execute PRE_OS application on PMU, which should at
	 * least take care of fans until a full PMU has been loaded.
	 */
	pmu_load(init, 0x01, post, NULL, NULL);
}

int
gm200_devinit_post(struct nvkm_devinit *base, bool post)
{
	struct nv50_devinit *init = nv50_devinit(base);
	struct nvkm_subdev *subdev = &init->base.subdev;
	struct nvkm_device *device = subdev->device;
	struct nvkm_bios *bios = device->bios;
	struct bit_entry bit_I;
	u32 exec, args;
	int ret;

	if (bit_entry(bios, 'I', &bit_I) || bit_I.version != 1 ||
					    bit_I.length < 0x1c) {
		nvkm_error(subdev, "VBIOS PMU init data not found\n");
		return -EINVAL;
	}

	/* Upload DEVINIT application from VBIOS onto PMU. */
	ret = pmu_load(init, 0x04, post, &exec, &args);
	if (ret) {
		nvkm_error(subdev, "VBIOS PMU/DEVINIT not found\n");
		return ret;
	}

	/* Upload tables required by opcodes in boot scripts. */
	if (post) {
		u32 pmu = pmu_args(init, args + 0x08, 0x08);
		u32 img = nvbios_rd16(bios, bit_I.offset + 0x14);
		u32 len = nvbios_rd16(bios, bit_I.offset + 0x16);
		pmu_data(init, pmu, img, len);
	}

	/* Upload boot scripts. */
	if (post) {
		u32 pmu = pmu_args(init, args + 0x08, 0x10);
		u32 img = nvbios_rd16(bios, bit_I.offset + 0x18);
		u32 len = nvbios_rd16(bios, bit_I.offset + 0x1a);
		pmu_data(init, pmu, img, len);
	}

	/* Execute DEVINIT. */
	if (post) {
		nvkm_wr32(device, 0x10a040, 0x00005000);
		pmu_exec(init, exec);
		if (nvkm_msec(device, 2000,
			if (nvkm_rd32(device, 0x10a040) & 0x00002000)
				break;
		) < 0)
			return -ETIMEDOUT;
	}

	gm200_devinit_preos(init, post);
	return 0;
}

static const struct nvkm_devinit_func
gm200_devinit = {
	.preinit = gf100_devinit_preinit,
	.init = nv50_devinit_init,
	.post = gm200_devinit_post,
	.pll_set = gf100_devinit_pll_set,
	.disable = gm107_devinit_disable,
};

int
gm200_devinit_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
		  struct nvkm_devinit **pinit)
{
	return nv50_devinit_new_(&gm200_devinit, device, type, inst, pinit);
}
