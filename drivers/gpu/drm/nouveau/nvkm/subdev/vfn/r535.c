/*
 * Copyright 2023 Red Hat Inc.
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

#include <rm/gpu.h>

static void
r535_vfn_dtor(struct nvkm_vfn *vfn)
{
	kfree(vfn->func);
}

int
r535_vfn_new(const struct nvkm_vfn_func *hw,
	     struct nvkm_device *device, enum nvkm_subdev_type type, int inst, u32 addr,
	     struct nvkm_vfn **pvfn)
{
	const struct nvkm_rm_gpu *gpu = device->gsp->rm->gpu;
	struct nvkm_vfn_func *rm;
	int ret;

	if (!(rm = kzalloc(sizeof(*rm), GFP_KERNEL)))
		return -ENOMEM;

	rm->dtor = r535_vfn_dtor;
	rm->intr = &tu102_vfn_intr;
	rm->user.addr = 0x030000;
	rm->user.size = 0x010000;
	rm->user.base.minver = -1;
	rm->user.base.maxver = -1;
	rm->user.base.oclass = gpu->usermode.class;

	ret = nvkm_vfn_new_(rm, device, type, inst, addr, pvfn);
	if (ret)
		kfree(rm);

	return ret;
}
