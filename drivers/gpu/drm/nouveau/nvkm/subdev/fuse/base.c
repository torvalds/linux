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
#include <subdev/fuse.h>

int
_nvkm_fuse_init(struct nvkm_object *object)
{
	struct nvkm_fuse *fuse = (void *)object;
	return nvkm_subdev_init(&fuse->base);
}

void
_nvkm_fuse_dtor(struct nvkm_object *object)
{
	struct nvkm_fuse *fuse = (void *)object;
	nvkm_subdev_destroy(&fuse->base);
}

int
nvkm_fuse_create_(struct nvkm_object *parent, struct nvkm_object *engine,
		  struct nvkm_oclass *oclass, int length, void **pobject)
{
	struct nvkm_fuse *fuse;
	int ret;

	ret = nvkm_subdev_create_(parent, engine, oclass, 0, "FUSE",
				  "fuse", length, pobject);
	fuse = *pobject;
	return ret;
}
