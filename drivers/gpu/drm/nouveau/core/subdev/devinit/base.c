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

#include <subdev/devinit.h>
#include <subdev/bios.h>
#include <subdev/bios/init.h>

int
nouveau_devinit_init(struct nouveau_devinit *devinit)
{
	int ret = nouveau_subdev_init(&devinit->base);
	if (ret)
		return ret;

	return nvbios_init(&devinit->base, devinit->post);
}

int
nouveau_devinit_fini(struct nouveau_devinit *devinit, bool suspend)
{
	/* force full reinit on resume */
	if (suspend)
		devinit->post = true;

	return nouveau_subdev_fini(&devinit->base, suspend);
}

int
nouveau_devinit_create_(struct nouveau_object *parent,
			struct nouveau_object *engine,
			struct nouveau_oclass *oclass,
			int size, void **pobject)
{
	struct nouveau_device *device = nv_device(parent);
	struct nouveau_devinit *devinit;
	int ret;

	ret = nouveau_subdev_create_(parent, engine, oclass, 0, "DEVINIT",
				     "init", size, pobject);
	devinit = *pobject;
	if (ret)
		return ret;

	devinit->post = nouveau_boolopt(device->cfgopt, "NvForcePost", false);
	return 0;
}
