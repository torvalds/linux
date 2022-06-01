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

static void *
nvkm_vfn_dtor(struct nvkm_subdev *subdev)
{
	return nvkm_vfn(subdev);
}

static const struct nvkm_subdev_func
nvkm_vfn = {
	.dtor = nvkm_vfn_dtor,
};

int
nvkm_vfn_new_(const struct nvkm_vfn_func *func, struct nvkm_device *device,
	      enum nvkm_subdev_type type, int inst, u32 addr, struct nvkm_vfn **pvfn)
{
	struct nvkm_vfn *vfn;
	int ret;

	if (!(vfn = *pvfn = kzalloc(sizeof(*vfn), GFP_KERNEL)))
		return -ENOMEM;

	nvkm_subdev_ctor(&nvkm_vfn, device, type, inst, &vfn->subdev);
	vfn->func = func;
	vfn->addr.priv = addr;
	vfn->addr.user = vfn->addr.priv + func->user.addr;

	if (vfn->func->intr) {
		ret = nvkm_intr_add(vfn->func->intr, vfn->func->intrs,
				    &vfn->subdev, 8, &vfn->intr);
		if (ret)
			return ret;
	}

	vfn->user.ctor = nvkm_uvfn_new;
	vfn->user.base = func->user.base;
	return 0;
}
