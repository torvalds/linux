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

#include <nvif/cl507c.h>
#include <nvif/unpack.h>

int
nv50_disp_base_new_(const struct nv50_disp_chan_func *func,
		    const struct nv50_disp_chan_mthd *mthd,
		    struct nv50_disp *disp, int chid,
		    const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nvkm_object **pobject)
{
	union {
		struct nv50_disp_base_channel_dma_v0 v0;
	} *args = argv;
	struct nvkm_object *parent = oclass->parent;
	int head, ret = -ENOSYS;
	u64 push;

	nvif_ioctl(parent, "create disp base channel dma size %d\n", argc);
	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		nvif_ioctl(parent, "create disp base channel dma vers %d "
				   "pushbuf %016llx head %d\n",
			   args->v0.version, args->v0.pushbuf, args->v0.head);
		if (!nvkm_head_find(&disp->base, args->v0.head))
			return -EINVAL;
		push = args->v0.pushbuf;
		head = args->v0.head;
	} else
		return ret;

	return nv50_disp_dmac_new_(func, mthd, disp, chid + head,
				   head, push, oclass, pobject);
}

static const struct nv50_disp_mthd_list
nv50_disp_base_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x0008c4 },
		{ 0x0088, 0x0008d0 },
		{ 0x008c, 0x0008dc },
		{ 0x0090, 0x0008e4 },
		{ 0x0094, 0x610884 },
		{ 0x00a0, 0x6108a0 },
		{ 0x00a4, 0x610878 },
		{ 0x00c0, 0x61086c },
		{ 0x00e0, 0x610858 },
		{ 0x00e4, 0x610860 },
		{ 0x00e8, 0x6108ac },
		{ 0x00ec, 0x6108b4 },
		{ 0x0100, 0x610894 },
		{ 0x0110, 0x6108bc },
		{ 0x0114, 0x61088c },
		{}
	}
};

const struct nv50_disp_mthd_list
nv50_disp_base_mthd_image = {
	.mthd = 0x0400,
	.addr = 0x000000,
	.data = {
		{ 0x0800, 0x6108f0 },
		{ 0x0804, 0x6108fc },
		{ 0x0808, 0x61090c },
		{ 0x080c, 0x610914 },
		{ 0x0810, 0x610904 },
		{}
	}
};

static const struct nv50_disp_chan_mthd
nv50_disp_base_mthd = {
	.name = "Base",
	.addr = 0x000540,
	.prev = 0x000004,
	.data = {
		{ "Global", 1, &nv50_disp_base_mthd_base },
		{  "Image", 2, &nv50_disp_base_mthd_image },
		{}
	}
};

int
nv50_disp_base_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		   struct nv50_disp *disp, struct nvkm_object **pobject)
{
	return nv50_disp_base_new_(&nv50_disp_dmac_func, &nv50_disp_base_mthd,
				   disp, 1, oclass, argv, argc, pobject);
}
