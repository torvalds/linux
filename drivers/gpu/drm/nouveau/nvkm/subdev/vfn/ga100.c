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
#include "priv.h"

#include <subdev/gsp.h>

#include <nvif/class.h>

static const struct nvkm_intr_data
ga100_vfn_intrs[] = {
	{ NVKM_ENGINE_DISP    , 0, 4, 0x04000000, true },
	{ NVKM_SUBDEV_GPIO    , 0, 4, 0x00200000, true },
	{ NVKM_SUBDEV_I2C     , 0, 4, 0x00200000, true },
	{ NVKM_SUBDEV_PRIVRING, 0, 4, 0x40000000, true },
	{}
};

static const struct nvkm_vfn_func
ga100_vfn = {
	.intr = &tu102_vfn_intr,
	.intrs = ga100_vfn_intrs,
	.user = { 0x030000, 0x010000, { -1, -1, AMPERE_USERMODE_A } },
};

int
ga100_vfn_new(struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, struct nvkm_vfn **pvfn)
{
	if (nvkm_gsp_rm(device->gsp))
		return r535_vfn_new(&ga100_vfn, device, type, inst, 0xb80000, pvfn);

	return nvkm_vfn_new_(&ga100_vfn, device, type, inst, 0xb80000, pvfn);
}
