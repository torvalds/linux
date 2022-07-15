/*
 * Copyright 2016 Red Hat Inc.
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
#include "priv.h"

static int
gm200_pmu_flcn_reset(struct nvkm_falcon *falcon)
{
	struct nvkm_pmu *pmu = container_of(falcon, typeof(*pmu), falcon);

	nvkm_falcon_wr32(falcon, 0x014, 0x0000ffff);
	pmu->func->reset(pmu);
	return nvkm_falcon_enable(falcon);
}

const struct nvkm_falcon_func
gm200_pmu_flcn = {
	.debug = 0xc08,
	.fbif = 0xe00,
	.load_imem = nvkm_falcon_v1_load_imem,
	.load_dmem = nvkm_falcon_v1_load_dmem,
	.read_dmem = nvkm_falcon_v1_read_dmem,
	.bind_context = nvkm_falcon_v1_bind_context,
	.wait_for_halt = nvkm_falcon_v1_wait_for_halt,
	.clear_interrupt = nvkm_falcon_v1_clear_interrupt,
	.set_start_addr = nvkm_falcon_v1_set_start_addr,
	.start = nvkm_falcon_v1_start,
	.enable = nvkm_falcon_v1_enable,
	.disable = nvkm_falcon_v1_disable,
	.reset = gm200_pmu_flcn_reset,
	.cmdq = { 0x4a0, 0x4b0, 4 },
	.msgq = { 0x4c8, 0x4cc, 0 },
};

static const struct nvkm_pmu_func
gm200_pmu = {
	.flcn = &gm200_pmu_flcn,
	.enabled = gf100_pmu_enabled,
	.reset = gf100_pmu_reset,
};


int
gm200_pmu_nofw(struct nvkm_pmu *pmu, int ver, const struct nvkm_pmu_fwif *fwif)
{
	nvkm_warn(&pmu->subdev, "firmware unavailable\n");
	return 0;
}

static const struct nvkm_pmu_fwif
gm200_pmu_fwif[] = {
	{ -1, gm200_pmu_nofw, &gm200_pmu },
	{}
};

int
gm200_pmu_new(struct nvkm_device *device, int index, struct nvkm_pmu **ppmu)
{
	return nvkm_pmu_new_(gm200_pmu_fwif, device, index, ppmu);
}
