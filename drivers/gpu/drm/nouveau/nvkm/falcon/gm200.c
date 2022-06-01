/*
 * Copyright 2022 Red Hat Inc.
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

#include <subdev/mc.h>
#include <subdev/timer.h>

int
gm200_flcn_reset_wait_mem_scrubbing(struct nvkm_falcon *falcon)
{
	nvkm_falcon_mask(falcon, 0x040, 0x00000000, 0x00000000);

	if (nvkm_msec(falcon->owner->device, 10,
		if (!(nvkm_falcon_rd32(falcon, 0x10c) & 0x00000006))
			break;
	) < 0)
		return -ETIMEDOUT;

	return 0;
}

int
gm200_flcn_enable(struct nvkm_falcon *falcon)
{
	struct nvkm_device *device = falcon->owner->device;
	int ret;

	if (falcon->func->reset_eng) {
		ret = falcon->func->reset_eng(falcon);
		if (ret)
			return ret;
	}

	if (falcon->func->reset_pmc)
		nvkm_mc_enable(device, falcon->owner->type, falcon->owner->inst);

	ret = falcon->func->reset_wait_mem_scrubbing(falcon);
	if (ret)
		return ret;

	nvkm_falcon_wr32(falcon, 0x084, nvkm_rd32(device, 0x000000));
	return 0;
}

int
gm200_flcn_disable(struct nvkm_falcon *falcon)
{
	struct nvkm_device *device = falcon->owner->device;
	int ret;

	nvkm_falcon_mask(falcon, 0x048, 0x00000003, 0x00000000);
	nvkm_falcon_wr32(falcon, 0x014, 0xffffffff);

	if (falcon->func->reset_pmc)
		nvkm_mc_disable(device, falcon->owner->type, falcon->owner->inst);

	if (falcon->func->reset_eng) {
		ret = falcon->func->reset_eng(falcon);
		if (ret)
			return ret;
	}

	return 0;
}
