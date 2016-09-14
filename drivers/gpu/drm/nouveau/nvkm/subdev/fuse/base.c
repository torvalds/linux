/*
 * Copyright 2014 Martin Peres
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
 * Authors: Martin Peres
 */
#include "priv.h"

u32
nvkm_fuse_read(struct nvkm_fuse *fuse, u32 addr)
{
	return fuse->func->read(fuse, addr);
}

static void *
nvkm_fuse_dtor(struct nvkm_subdev *subdev)
{
	return nvkm_fuse(subdev);
}

static const struct nvkm_subdev_func
nvkm_fuse = {
	.dtor = nvkm_fuse_dtor,
};

int
nvkm_fuse_new_(const struct nvkm_fuse_func *func, struct nvkm_device *device,
	       int index, struct nvkm_fuse **pfuse)
{
	struct nvkm_fuse *fuse;
	if (!(fuse = *pfuse = kzalloc(sizeof(*fuse), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_subdev_ctor(&nvkm_fuse, device, index, &fuse->subdev);
	fuse->func = func;
	spin_lock_init(&fuse->lock);
	return 0;
}
