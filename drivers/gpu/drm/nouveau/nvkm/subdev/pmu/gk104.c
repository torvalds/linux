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
#define gf119_pmu_code gk104_pmu_code
#define gf119_pmu_data gk104_pmu_data
#include "priv.h"
#include "fuc/gf119.fuc4.h"

#include <core/option.h>
#include <subdev/fuse.h>
#include <subdev/timer.h>

static void
magic_(struct nvkm_device *device, u32 ctrl, int size)
{
	nvkm_wr32(device, 0x00c800, 0x00000000);
	nvkm_wr32(device, 0x00c808, 0x00000000);
	nvkm_wr32(device, 0x00c800, ctrl);
	nvkm_msec(device, 2000,
		if (nvkm_rd32(device, 0x00c800) & 0x40000000) {
			while (size--)
				nvkm_wr32(device, 0x00c804, 0x00000000);
			break;
		}
	);
	nvkm_wr32(device, 0x00c800, 0x00000000);
}

static void
magic(struct nvkm_device *device, u32 ctrl)
{
	magic_(device, 0x8000a41f | ctrl, 6);
	magic_(device, 0x80000421 | ctrl, 1);
}

static void
gk104_pmu_pgob(struct nvkm_pmu *pmu, bool enable)
{
	struct nvkm_device *device = pmu->subdev.device;

	if (!(nvkm_fuse_read(device->fuse, 0x31c) & 0x00000001))
		return;

	nvkm_mask(device, 0x000200, 0x00001000, 0x00000000);
	nvkm_rd32(device, 0x000200);
	nvkm_mask(device, 0x000200, 0x08000000, 0x08000000);
	msleep(50);

	nvkm_mask(device, 0x10a78c, 0x00000002, 0x00000002);
	nvkm_mask(device, 0x10a78c, 0x00000001, 0x00000001);
	nvkm_mask(device, 0x10a78c, 0x00000001, 0x00000000);

	nvkm_mask(device, 0x020004, 0xc0000000, enable ? 0xc0000000 : 0x40000000);
	msleep(50);

	nvkm_mask(device, 0x10a78c, 0x00000002, 0x00000000);
	nvkm_mask(device, 0x10a78c, 0x00000001, 0x00000001);
	nvkm_mask(device, 0x10a78c, 0x00000001, 0x00000000);

	nvkm_mask(device, 0x000200, 0x08000000, 0x00000000);
	nvkm_mask(device, 0x000200, 0x00001000, 0x00001000);
	nvkm_rd32(device, 0x000200);

	if (nvkm_boolopt(device->cfgopt, "War00C800_0", true)) {
		switch (device->chipset) {
		case 0xe4:
			magic(device, 0x04000000);
			magic(device, 0x06000000);
			magic(device, 0x0c000000);
			magic(device, 0x0e000000);
			break;
		case 0xe6:
			magic(device, 0x02000000);
			magic(device, 0x04000000);
			magic(device, 0x0a000000);
			break;
		case 0xe7:
			magic(device, 0x02000000);
			break;
		default:
			break;
		}
	}
}

static const struct nvkm_pmu_func
gk104_pmu = {
	.flcn = &gt215_pmu_flcn,
	.code.data = gk104_pmu_code,
	.code.size = sizeof(gk104_pmu_code),
	.data.data = gk104_pmu_data,
	.data.size = sizeof(gk104_pmu_data),
	.enabled = gf100_pmu_enabled,
	.reset = gf100_pmu_reset,
	.init = gt215_pmu_init,
	.fini = gt215_pmu_fini,
	.intr = gt215_pmu_intr,
	.send = gt215_pmu_send,
	.recv = gt215_pmu_recv,
	.pgob = gk104_pmu_pgob,
};

static const struct nvkm_pmu_fwif
gk104_pmu_fwif[] = {
	{ -1, gf100_pmu_nofw, &gk104_pmu },
	{}
};

int
gk104_pmu_new(struct nvkm_device *device, int index, struct nvkm_pmu **ppmu)
{
	return nvkm_pmu_new_(gk104_pmu_fwif, device, index, ppmu);
}
