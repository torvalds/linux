/*
 * Copyright 2012 Red Hat Inc.
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
#include "fuc/gf100.fuc3.h"

#include <nvif/class.h>

static void
gf100_ce_init(struct nvkm_falcon *ce)
{
	struct nvkm_device *device = ce->engine.subdev.device;
	const int index = ce->engine.subdev.index - NVKM_ENGINE_CE0;
	nvkm_wr32(device, ce->addr + 0x084, index);
}

static const struct nvkm_falcon_func
gf100_ce0 = {
	.code.data = gf100_ce_code,
	.code.size = sizeof(gf100_ce_code),
	.data.data = gf100_ce_data,
	.data.size = sizeof(gf100_ce_data),
	.init = gf100_ce_init,
	.intr = gt215_ce_intr,
	.sclass = {
		{ -1, -1, FERMI_DMA },
		{}
	}
};

static const struct nvkm_falcon_func
gf100_ce1 = {
	.code.data = gf100_ce_code,
	.code.size = sizeof(gf100_ce_code),
	.data.data = gf100_ce_data,
	.data.size = sizeof(gf100_ce_data),
	.init = gf100_ce_init,
	.intr = gt215_ce_intr,
	.sclass = {
		{ -1, -1, FERMI_DECOMPRESS },
		{}
	}
};

int
gf100_ce_new(struct nvkm_device *device, int index,
	     struct nvkm_engine **pengine)
{
	if (index == NVKM_ENGINE_CE0) {
		return nvkm_falcon_new_(&gf100_ce0, device, index, true,
					0x104000, pengine);
	} else
	if (index == NVKM_ENGINE_CE1) {
		return nvkm_falcon_new_(&gf100_ce1, device, index, true,
					0x105000, pengine);
	}
	return -ENODEV;
}
