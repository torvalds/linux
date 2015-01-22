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

static void
gk104_pmu_pgob(struct nvkm_pmu *pmu, bool enable)
{
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
