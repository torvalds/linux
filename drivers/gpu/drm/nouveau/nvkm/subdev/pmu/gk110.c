/*
 * Copyright 2015 Red Hat Inc.
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
#define gf110_pmu_code gk110_pmu_code
#define gf110_pmu_data gk110_pmu_data
#include "priv.h"
#include "fuc/gf110.fuc4.h"

#include <subdev/timer.h>

void
gk110_pmu_pgob(struct nvkm_pmu *pmu, bool enable)
{
	static const struct {
		u32 addr;
		u32 data;
	} magic[] = {
		{ 0x020520, 0xfffffffc },
		{ 0x020524, 0xfffffffe },
		{ 0x020524, 0xfffffffc },
		{ 0x020524, 0xfffffff8 },
		{ 0x020524, 0xffffffe0 },
		{ 0x020530, 0xfffffffe },
		{ 0x02052c, 0xfffffffa },
		{ 0x02052c, 0xfffffff0 },
		{ 0x02052c, 0xffffffc0 },
		{ 0x02052c, 0xffffff00 },
		{ 0x02052c, 0xfffffc00 },
		{ 0x02052c, 0xfffcfc00 },
		{ 0x02052c, 0xfff0fc00 },
		{ 0x02052c, 0xff80fc00 },
		{ 0x020528, 0xfffffffe },
		{ 0x020528, 0xfffffffc },
	};
	int i;

	nv_mask(pmu, 0x000200, 0x00001000, 0x00000000);
	nv_rd32(pmu, 0x000200);
	nv_mask(pmu, 0x000200, 0x08000000, 0x08000000);
	msleep(50);

	nv_mask(pmu, 0x10a78c, 0x00000002, 0x00000002);
	nv_mask(pmu, 0x10a78c, 0x00000001, 0x00000001);
	nv_mask(pmu, 0x10a78c, 0x00000001, 0x00000000);

	nv_mask(pmu, 0x0206b4, 0x00000000, 0x00000000);
	for (i = 0; i < ARRAY_SIZE(magic); i++) {
		nv_wr32(pmu, magic[i].addr, magic[i].data);
		nv_wait(pmu, magic[i].addr, 0x80000000, 0x00000000);
	}

	nv_mask(pmu, 0x10a78c, 0x00000002, 0x00000000);
	nv_mask(pmu, 0x10a78c, 0x00000001, 0x00000001);
	nv_mask(pmu, 0x10a78c, 0x00000001, 0x00000000);

	nv_mask(pmu, 0x000200, 0x08000000, 0x00000000);
	nv_mask(pmu, 0x000200, 0x00001000, 0x00001000);
	nv_rd32(pmu, 0x000200);
}

struct nvkm_oclass *
gk110_pmu_oclass = &(struct nvkm_pmu_impl) {
	.base.handle = NV_SUBDEV(PMU, 0xf0),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = _nvkm_pmu_ctor,
		.dtor = _nvkm_pmu_dtor,
		.init = _nvkm_pmu_init,
		.fini = _nvkm_pmu_fini,
	},
	.code.data = gk110_pmu_code,
	.code.size = sizeof(gk110_pmu_code),
	.data.data = gk110_pmu_data,
	.data.size = sizeof(gk110_pmu_data),
	.pgob = gk110_pmu_pgob,
}.base;
