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
#include "dmacnv50.h"
#include "rootnv50.h"

#include <core/client.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

int
nv50_disp_ovly_new(const struct nv50_disp_dmac_func *func,
		   const struct nv50_disp_chan_mthd *mthd,
		   struct nv50_disp_root *root, int chid,
		   const struct nvkm_oclass *oclass, void *data, u32 size,
		   struct nvkm_object **pobject)
{
	union {
		struct nv50_disp_overlay_channel_dma_v0 v0;
	} *args = data;
	struct nvkm_object *parent = oclass->parent;
	struct nv50_disp *disp = root->disp;
	int head, ret;
	u64 push;

	nvif_ioctl(parent, "create disp overlay channel dma size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nvif_ioctl(parent, "create disp overlay channel dma vers %d "
				   "pushbuf %016llx head %d\n",
			   args->v0.version, args->v0.pushbuf, args->v0.head);
		if (args->v0.head > disp->base.head.nr)
			return -EINVAL;
		push = args->v0.pushbuf;
		head = args->v0.head;
	} else
		return ret;

	return nv50_disp_dmac_new_(func, mthd, root, chid + head,
				   head, push, oclass, pobject);
}

static const struct nv50_disp_mthd_list
nv50_disp_ovly_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x0009a0 },
		{ 0x0088, 0x0009c0 },
		{ 0x008c, 0x0009c8 },
		{ 0x0090, 0x6109b4 },
		{ 0x0094, 0x610970 },
		{ 0x00a0, 0x610998 },
		{ 0x00a4, 0x610964 },
		{ 0x00c0, 0x610958 },
		{ 0x00e0, 0x6109a8 },
		{ 0x00e4, 0x6109d0 },
		{ 0x00e8, 0x6109d8 },
		{ 0x0100, 0x61094c },
		{ 0x0104, 0x610984 },
		{ 0x0108, 0x61098c },
		{ 0x0800, 0x6109f8 },
		{ 0x0808, 0x610a08 },
		{ 0x080c, 0x610a10 },
		{ 0x0810, 0x610a00 },
		{}
	}
};

static const struct nv50_disp_chan_mthd
nv50_disp_ovly_chan_mthd = {
	.name = "Overlay",
	.addr = 0x000540,
	.prev = 0x000004,
	.data = {
		{ "Global", 1, &nv50_disp_ovly_mthd_base },
		{}
	}
};

const struct nv50_disp_dmac_oclass
nv50_disp_ovly_oclass = {
	.base.oclass = NV50_DISP_OVERLAY_CHANNEL_DMA,
	.base.minver = 0,
	.base.maxver = 0,
	.ctor = nv50_disp_ovly_new,
	.func = &nv50_disp_dmac_func,
	.mthd = &nv50_disp_ovly_chan_mthd,
	.chid = 3,
};
