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
#define nv04_disp_root(p) container_of((p), struct nv04_disp_root, object)
#include "priv.h"
#include "head.h"

#include <core/client.h>

#include <nvif/class.h>
#include <nvif/cl0046.h>
#include <nvif/unpack.h>

struct nv04_disp_root {
	struct nvkm_object object;
	struct nvkm_disp *disp;
};

static int
nv04_disp_mthd(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	struct nv04_disp_root *root = nv04_disp_root(object);
	union {
		struct nv04_disp_mthd_v0 v0;
	} *args = data;
	struct nvkm_head *head;
	int id, ret = -ENOSYS;

	nvif_ioctl(object, "disp mthd size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
		nvif_ioctl(object, "disp mthd vers %d mthd %02x head %d\n",
			   args->v0.version, args->v0.method, args->v0.head);
		mthd = args->v0.method;
		id   = args->v0.head;
	} else
		return ret;

	if (!(head = nvkm_head_find(root->disp, id)))
		return -ENXIO;

	switch (mthd) {
	case NV04_DISP_SCANOUTPOS:
		return nvkm_head_mthd_scanoutpos(object, head, data, size);
	default:
		break;
	}

	return -EINVAL;
}

static const struct nvkm_object_func
nv04_disp_root = {
	.mthd = nv04_disp_mthd,
	.ntfy = nvkm_disp_ntfy,
};

static int
nv04_disp_root_new(struct nvkm_disp *disp, const struct nvkm_oclass *oclass,
		   void *data, u32 size, struct nvkm_object **pobject)
{
	struct nv04_disp_root *root;

	if (!(root = kzalloc(sizeof(*root), GFP_KERNEL)))
		return -ENOMEM;
	root->disp = disp;
	*pobject = &root->object;

	nvkm_object_ctor(&nv04_disp_root, oclass, &root->object);
	return 0;
}

const struct nvkm_disp_oclass
nv04_disp_root_oclass = {
	.base.oclass = NV04_DISP,
	.base.minver = -1,
	.base.maxver = -1,
	.ctor = nv04_disp_root_new,
};
