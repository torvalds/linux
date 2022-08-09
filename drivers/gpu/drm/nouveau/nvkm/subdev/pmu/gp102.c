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

static void
gp102_pmu_reset(struct nvkm_pmu *pmu)
{
	struct nvkm_device *device = pmu->subdev.device;
	nvkm_mask(device, 0x10a3c0, 0x00000001, 0x00000001);
	nvkm_mask(device, 0x10a3c0, 0x00000001, 0x00000000);
}

static bool
gp102_pmu_enabled(struct nvkm_pmu *pmu)
{
	return !(nvkm_rd32(pmu->subdev.device, 0x10a3c0) & 0x00000001);
}

static const struct nvkm_pmu_func
gp102_pmu = {
	.flcn = &gm200_pmu_flcn,
	.enabled = gp102_pmu_enabled,
	.reset = gp102_pmu_reset,
};

static const struct nvkm_pmu_fwif
gp102_pmu_fwif[] = {
	{ -1, gm200_pmu_nofw, &gp102_pmu },
	{}
};

int
gp102_pmu_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	      struct nvkm_pmu **ppmu)
{
	return nvkm_pmu_new_(gp102_pmu_fwif, device, type, inst, ppmu);
}
