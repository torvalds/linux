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

#include <core/client.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

int
nv50_disp_curs_ctor(struct nvkm_object *parent,
		    struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, void *data, u32 size,
		    struct nvkm_object **pobject)
{
	union {
		struct nv50_disp_cursor_v0 v0;
	} *args = data;
	struct nv50_disp *disp = (void *)engine;
	struct nv50_disp_pioc *pioc;
	int ret;

	nvif_ioctl(parent, "create disp cursor size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nvif_ioctl(parent, "create disp cursor vers %d head %d\n",
			   args->v0.version, args->v0.head);
		if (args->v0.head > disp->head.nr)
			return -EINVAL;
	} else
		return ret;

	ret = nv50_disp_pioc_create_(parent, engine, oclass, args->v0.head,
				     sizeof(*pioc), (void **)&pioc);
	*pobject = nv_object(pioc);
	if (ret)
		return ret;

	return 0;
}

struct nv50_disp_chan_impl
nv50_disp_curs_ofuncs = {
	.base.ctor = nv50_disp_curs_ctor,
	.base.dtor = nv50_disp_pioc_dtor,
	.base.init = nv50_disp_pioc_init,
	.base.fini = nv50_disp_pioc_fini,
	.base.ntfy = nv50_disp_chan_ntfy,
	.base.map  = nv50_disp_chan_map,
	.base.rd32 = nv50_disp_chan_rd32,
	.base.wr32 = nv50_disp_chan_wr32,
	.chid = 7,
};
