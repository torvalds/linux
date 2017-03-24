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
#include "rootnv50.h"

#include <core/client.h>

#include <nvif/class.h>
#include <nvif/cl507a.h>
#include <nvif/unpack.h>

int
nv50_disp_curs_new(const struct nv50_disp_chan_func *func,
		   const struct nv50_disp_chan_mthd *mthd,
		   struct nv50_disp_root *root, int ctrl, int user,
		   const struct nvkm_oclass *oclass, void *data, u32 size,
		   struct nvkm_object **pobject)
{
	union {
		struct nv50_disp_cursor_v0 v0;
	} *args = data;
	struct nvkm_object *parent = oclass->parent;
	struct nv50_disp *disp = root->disp;
	int head, ret = -ENOSYS;

	nvif_ioctl(parent, "create disp cursor size %d\n", size);
	if (!(ret = nvif_unpack(ret, &data, &size, args->v0, 0, 0, false))) {
		nvif_ioctl(parent, "create disp cursor vers %d head %d\n",
			   args->v0.version, args->v0.head);
		if (args->v0.head > disp->base.head.nr)
			return -EINVAL;
		head = args->v0.head;
	} else
		return ret;

	return nv50_disp_chan_new_(func, mthd, root, ctrl + head, user + head,
				   head, oclass, pobject);
}

const struct nv50_disp_pioc_oclass
nv50_disp_curs_oclass = {
	.base.oclass = NV50_DISP_CURSOR,
	.base.minver = 0,
	.base.maxver = 0,
	.ctor = nv50_disp_curs_new,
	.func = &nv50_disp_pioc_func,
	.chid = { 7, 7 },
};
