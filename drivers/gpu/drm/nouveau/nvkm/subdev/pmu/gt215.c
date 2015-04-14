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
#include "priv.h"
#include "fuc/gt215.fuc3.h"

static int
gt215_pmu_init(struct nvkm_object *object)
{
	struct nvkm_pmu *pmu = (void *)object;
	nv_mask(pmu, 0x022210, 0x00000001, 0x00000000);
	nv_mask(pmu, 0x022210, 0x00000001, 0x00000001);
	return nvkm_pmu_init(pmu);
}

struct nvkm_oclass *
gt215_pmu_oclass = &(struct nvkm_pmu_impl) {
	.base.handle = NV_SUBDEV(PMU, 0xa3),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = _nvkm_pmu_ctor,
		.dtor = _nvkm_pmu_dtor,
		.init = gt215_pmu_init,
		.fini = _nvkm_pmu_fini,
	},
	.code.data = gt215_pmu_code,
	.code.size = sizeof(gt215_pmu_code),
	.data.data = gt215_pmu_data,
	.data.size = sizeof(gt215_pmu_data),
}.base;
