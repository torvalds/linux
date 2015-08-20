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

int
_nvkm_devinit_fini(struct nvkm_object *object, bool suspend)
{
	struct nvkm_devinit *init = (void *)object;

	/* force full reinit on resume */
	if (suspend)
		init->post = true;

	/* unlock the extended vga crtc regs */
	nvkm_lockvgac(init->subdev.device, false);

	return nvkm_subdev_fini(&init->subdev, suspend);
}

int
_nvkm_devinit_init(struct nvkm_object *object)
{
	struct nvkm_devinit_impl *impl = (void *)object->oclass;
	struct nvkm_devinit *init = (void *)object;
	int ret;

	ret = nvkm_subdev_init(&init->subdev);
	if (ret)
		return ret;

	ret = impl->post(&init->subdev, init->post);
	if (ret)
		return ret;

	if (impl->disable)
		nv_device(init)->disable_mask |= impl->disable(init);
	return 0;
}

void
_nvkm_devinit_dtor(struct nvkm_object *object)
{
	struct nvkm_devinit *init = (void *)object;

	/* lock crtc regs */
	nvkm_lockvgac(init->subdev.device, true);

	nvkm_subdev_destroy(&init->subdev);
}

int
nvkm_devinit_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		     struct nvkm_oclass *oclass, int size, void **pobject)
{
	struct nvkm_devinit_impl *impl = (void *)oclass;
	struct nvkm_device *device = nv_device(parent);
	struct nvkm_devinit *init;
	int ret;

	ret = nvkm_subdev_create_(parent, engine, oclass, 0, "DEVINIT",
				  "init", size, pobject);
	init = *pobject;
	if (ret)
		return ret;

	init->post = nvkm_boolopt(device->cfgopt, "NvForcePost", false);
	init->meminit = impl->meminit;
	init->pll_set = impl->pll_set;
	init->mmio    = impl->mmio;
	return 0;
}
