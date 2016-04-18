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

#include <core/client.h>

#include <nvif/class.h>
#include <nvif/cl0046.h>
#include <nvif/unpack.h>

struct nv04_disp_root {
	struct nvkm_object object;
	struct nvkm_disp *disp;
};

static int
nv04_disp_scanoutpos(struct nv04_disp_root *root,
		     void *data, u32 size, int head)
{
	struct nvkm_device *device = root->disp->engine.subdev.device;
	struct nvkm_object *object = &root->object;
	const u32 hoff = head * 0x2000;
	union {
		struct nv04_disp_scanoutpos_v0 v0;
	} *args = data;
	u32 line;
	int ret = -ENOSYS;

	nvif_ioctl(object, "disp scanoutpos size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(object, "disp scanoutpos vers %d\n",
			   args->v0.version);
		args->v0.vblanks = nvkm_rd32(device, 0x680800 + hoff) & 0xffff;
		args->v0.vtotal  = nvkm_rd32(device, 0x680804 + hoff) & 0xffff;
		args->v0.vblanke = args->v0.vtotal - 1;

		args->v0.hblanks = nvkm_rd32(device, 0x680820 + hoff) & 0xffff;
		args->v0.htotal  = nvkm_rd32(device, 0x680824 + hoff) & 0xffff;
		args->v0.hblanke = args->v0.htotal - 1;

		/*
		 * If output is vga instead of digital then vtotal/htotal is
		 * invalid so we have to give up and trigger the timestamping
		 * fallback in the drm core.
		 */
		if (!args->v0.vtotal || !args->v0.htotal)
			return -ENOTSUPP;

		args->v0.time[0] = ktime_to_ns(ktime_get());
		line = nvkm_rd32(device, 0x600868 + hoff);
		args->v0.time[1] = ktime_to_ns(ktime_get());
		args->v0.hline = (line & 0xffff0000) >> 16;
		args->v0.vline = (line & 0x0000ffff);
	} else
		return ret;

	return 0;
}

static int
nv04_disp_mthd(struct nvkm_object *object, u32 mthd, void *data, u32 size)
{
	struct nv04_disp_root *root = nv04_disp_root(object);
	union {
		struct nv04_disp_mthd_v0 v0;
	} *args = data;
	int head, ret = -ENOSYS;

	nvif_ioctl(object, "disp mthd size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, true))) {
		nvif_ioctl(object, "disp mthd vers %d mthd %02x head %d\n",
			   args->v0.version, args->v0.method, args->v0.head);
		mthd = args->v0.method;
		head = args->v0.head;
	} else
		return ret;

	if (head < 0 || head >= 2)
		return -ENXIO;

	switch (mthd) {
	case NV04_DISP_SCANOUTPOS:
		return nv04_disp_scanoutpos(root, data, size, head);
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
