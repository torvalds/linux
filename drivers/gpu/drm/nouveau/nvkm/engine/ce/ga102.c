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

static const struct nvkm_engine_func
ga102_ce = {
	.oneinit = ga100_ce_oneinit,
	.init = ga100_ce_init,
	.fini = ga100_ce_fini,
	.nonstall = ga100_ce_nonstall,
	.cclass = &gv100_ce_cclass,
	.sclass = {
		{ -1, -1, AMPERE_DMA_COPY_A },
		{ -1, -1, AMPERE_DMA_COPY_B },
		{}
	}
};

int
ga102_ce_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	     struct nvkm_engine **pengine)
{
	if (nvkm_gsp_rm(device->gsp))
		return -ENODEV;

	return nvkm_engine_new_(&ga102_ce, device, type, inst, true, pengine);
}
