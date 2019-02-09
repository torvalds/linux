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
#include "channv50.h"
#include "head.h"

#include <core/client.h>

#include <nvif/cl507b.h>
#include <nvif/unpack.h>

int
nv50_disp_oimm_new_(const struct nv50_disp_chan_func *func,
		    struct nv50_disp *disp, int ctrl, int user,
		    const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nvkm_object **pobject)
{
	union {
		struct nv50_disp_overlay_v0 v0;
	} *args = argv;
	struct nvkm_object *parent = oclass->parent;
	int head, ret = -ENOSYS;

	nvif_ioctl(parent, "create disp overlay size %d\n", argc);
	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		nvif_ioctl(parent, "create disp overlay vers %d head %d\n",
			   args->v0.version, args->v0.head);
		if (!nvkm_head_find(&disp->base, args->v0.head))
			return -EINVAL;
		head = args->v0.head;
	} else
		return ret;

	return nv50_disp_chan_new_(func, NULL, disp, ctrl + head, user + head,
				   head, oclass, pobject);
}

int
nv50_disp_oimm_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		   struct nv50_disp *disp, struct nvkm_object **pobject)
{
	return nv50_disp_oimm_new_(&nv50_disp_pioc_func, disp, 5, 5,
				   oclass, argv, argc, pobject);
}
