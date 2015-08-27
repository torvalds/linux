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
#define gf110_pmu_code gk104_pmu_code
#define gf110_pmu_data gk104_pmu_data
#include "priv.h"
#include "fuc/gf110.fuc4.h"

#include <core/device.h>
#include <core/option.h>
#include <subdev/timer.h>

static void
magic_(struct nvkm_pmu *pmu, u32 ctrl, int size)
{
	nv_wr32(pmu, 0x00c800, 0x00000000);
	nv_wr32(pmu, 0x00c808, 0x00000000);
	nv_wr32(pmu, 0x00c800, ctrl);
	if (nv_wait(pmu, 0x00c800, 0x40000000, 0x40000000)) {
		while (size--)
			nv_wr32(pmu, 0x00c804, 0x00000000);
	}
	nv_wr32(pmu, 0x00c800, 0x00000000);
}

static void
magic(struct nvkm_pmu *pmu, u32 ctrl)
{
	magic_(pmu, 0x8000a41f | ctrl, 6);
	magic_(pmu, 0x80000421 | ctrl, 1);
}

static void
gk104_pmu_pgob(struct nvkm_pmu *pmu, bool enable)
{
	struct nvkm_device *device = nv_device(pmu);
	struct nvkm_object *dev = nv_object(device);

	nv_mask(pmu, 0x000200, 0x00001000, 0x00000000);
	nv_rd32(pmu, 0x000200);
	nv_mask(pmu, 0x000200, 0x08000000, 0x08000000);
	msleep(50);

	nv_mask(pmu, 0x10a78c, 0x00000002, 0x00000002);
	nv_mask(pmu, 0x10a78c, 0x00000001, 0x00000001);
	nv_mask(pmu, 0x10a78c, 0x00000001, 0x00000000);

	nv_mask(pmu, 0x020004, 0xc0000000, enable ? 0xc0000000 : 0x40000000);
	msleep(50);

	nv_mask(pmu, 0x10a78c, 0x00000002, 0x00000000);
	nv_mask(pmu, 0x10a78c, 0x00000001, 0x00000001);
	nv_mask(pmu, 0x10a78c, 0x00000001, 0x00000000);

	nv_mask(pmu, 0x000200, 0x08000000, 0x00000000);
	nv_mask(pmu, 0x000200, 0x00001000, 0x00001000);
	nv_rd32(pmu, 0x000200);

	if (nv_device_match(dev, 0x11fc, 0x17aa, 0x2211) /* Lenovo W541 */
	 || nv_device_match(dev, 0x11fc, 0x17aa, 0x221e) /* Lenovo W541 */
	 || nvkm_boolopt(device->cfgopt, "War00C800_0", false)) {
		nv_info(pmu, "hw bug workaround enabled\n");
		switch (device->chipset) {
		case 0xe4:
			magic(pmu, 0x04000000);
			magic(pmu, 0x06000000);
			magic(pmu, 0x0c000000);
			magic(pmu, 0x0e000000);
			break;
		case 0xe6:
			magic(pmu, 0x02000000);
			magic(pmu, 0x04000000);
			magic(pmu, 0x0a000000);
			break;
		case 0xe7:
			magic(pmu, 0x02000000);
			break;
		default:
			break;
		}
	}
}

struct nvkm_oclass *
gk104_pmu_oclass = &(struct nvkm_pmu_impl) {
	.base.handle = NV_SUBDEV(PMU, 0xe4),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = _nvkm_pmu_ctor,
		.dtor = _nvkm_pmu_dtor,
		.init = _nvkm_pmu_init,
		.fini = _nvkm_pmu_fini,
	},
	.code.data = gk104_pmu_code,
	.code.size = sizeof(gk104_pmu_code),
	.data.data = gk104_pmu_data,
	.data.size = sizeof(gk104_pmu_data),
	.pgob = gk104_pmu_pgob,
}.base;
