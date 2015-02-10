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

#include <core/option.h>

#include <subdev/vga.h>

#include "priv.h"

int
_nouveau_devinit_fini(struct nouveau_object *object, bool suspend)
{
	struct nouveau_devinit *devinit = (void *)object;

	/* force full reinit on resume */
	if (suspend)
		devinit->post = true;

	/* unlock the extended vga crtc regs */
	nv_lockvgac(devinit, false);

	return nouveau_subdev_fini(&devinit->base, suspend);
}

int
_nouveau_devinit_init(struct nouveau_object *object)
{
	struct nouveau_devinit_impl *impl = (void *)object->oclass;
	struct nouveau_devinit *devinit = (void *)object;
	int ret;

	ret = nouveau_subdev_init(&devinit->base);
	if (ret)
		return ret;

	ret = impl->post(&devinit->base, devinit->post);
	if (ret)
		return ret;

	if (impl->disable)
		nv_device(devinit)->disable_mask |= impl->disable(devinit);
	return 0;
}

void
_nouveau_devinit_dtor(struct nouveau_object *object)
{
	struct nouveau_devinit *devinit = (void *)object;

	/* lock crtc regs */
	nv_lockvgac(devinit, true);

	nouveau_subdev_destroy(&devinit->base);
}

int
nouveau_devinit_create_(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass,
			int size, void **pobject)
{
	struct nouveau_devinit_impl *impl = (void *)oclass;
	struct nouveau_device *device = nv_device(parent);
	struct nouveau_devinit *devinit;
	int ret;

	ret = nouveau_subdev_create_(parent, engine, oclass, 0, "DEVINIT",
				     "init", size, pobject);
	devinit = *pobject;
	if (ret)
		return ret;

	devinit->post = nouveau_boolopt(device->cfgopt, "NvForcePost", false);
	devinit->meminit = impl->meminit;
	devinit->pll_set = impl->pll_set;
	devinit->mmio    = impl->mmio;
	return 0;
}
