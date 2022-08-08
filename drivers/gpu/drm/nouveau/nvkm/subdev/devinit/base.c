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

#include <core/option.h>
#include <subdev/vga.h>

u32
nvkm_devinit_mmio(struct nvkm_devinit *init, u32 addr)
{
	if (init->func->mmio)
		addr = init->func->mmio(init, addr);
	return addr;
}

int
nvkm_devinit_pll_set(struct nvkm_devinit *init, u32 type, u32 khz)
{
	return init->func->pll_set(init, type, khz);
}

void
nvkm_devinit_meminit(struct nvkm_devinit *init)
{
	if (init->func->meminit)
		init->func->meminit(init);
}

u64
nvkm_devinit_disable(struct nvkm_devinit *init)
{
	if (init && init->func->disable)
		return init->func->disable(init);
	return 0;
}

int
nvkm_devinit_post(struct nvkm_devinit *init)
{
	int ret = 0;
	if (init && init->func->post)
		ret = init->func->post(init, init->post);
	nvkm_devinit_disable(init);
	return ret;
}

static int
nvkm_devinit_fini(struct nvkm_subdev *subdev, bool suspend)
{
	struct nvkm_devinit *init = nvkm_devinit(subdev);
	/* force full reinit on resume */
	if (suspend)
		init->post = true;
	return 0;
}

static int
nvkm_devinit_preinit(struct nvkm_subdev *subdev)
{
	struct nvkm_devinit *init = nvkm_devinit(subdev);

	if (init->func->preinit)
		init->func->preinit(init);

	/* Override the post flag during the first call if NvForcePost is set */
	if (init->force_post) {
		init->post = init->force_post;
		init->force_post = false;
	}

	/* unlock the extended vga crtc regs */
	nvkm_lockvgac(subdev->device, false);
	return 0;
}

static int
nvkm_devinit_init(struct nvkm_subdev *subdev)
{
	struct nvkm_devinit *init = nvkm_devinit(subdev);
	if (init->func->init)
		init->func->init(init);
	return 0;
}

static void *
nvkm_devinit_dtor(struct nvkm_subdev *subdev)
{
	struct nvkm_devinit *init = nvkm_devinit(subdev);
	void *data = init;

	if (init->func->dtor)
		data = init->func->dtor(init);

	/* lock crtc regs */
	nvkm_lockvgac(subdev->device, true);
	return data;
}

static const struct nvkm_subdev_func
nvkm_devinit = {
	.dtor = nvkm_devinit_dtor,
	.preinit = nvkm_devinit_preinit,
	.init = nvkm_devinit_init,
	.fini = nvkm_devinit_fini,
};

void
nvkm_devinit_ctor(const struct nvkm_devinit_func *func, struct nvkm_device *device,
		  enum nvkm_subdev_type type, int inst, struct nvkm_devinit *init)
{
	nvkm_subdev_ctor(&nvkm_devinit, device, type, inst, &init->subdev);
	init->func = func;
	init->force_post = nvkm_boolopt(device->cfgopt, "NvForcePost", false);
}
