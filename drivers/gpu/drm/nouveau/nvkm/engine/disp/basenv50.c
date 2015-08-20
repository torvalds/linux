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

#include <core/client.h>

#include <nvif/class.h>
#include <nvif/unpack.h>

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

const struct nv50_disp_mthd_chan
nv50_disp_base_mthd_chan = {
	.name = "Base",
	.addr = 0x000540,
	.data = {
		{ "Global", 1, &nv50_disp_base_mthd_base },
		{  "Image", 2, &nv50_disp_base_mthd_image },
		{}
	}
};

int
nv50_disp_base_ctor(struct nvkm_object *parent,
		    struct nvkm_object *engine,
		    struct nvkm_oclass *oclass, void *data, u32 size,
		    struct nvkm_object **pobject)
{
	union {
		struct nv50_disp_base_channel_dma_v0 v0;
	} *args = data;
	struct nv50_disp *disp = (void *)engine;
	struct nv50_disp_dmac *dmac;
	int ret;

	nvif_ioctl(parent, "create disp base channel dma size %d\n", size);
	if (nvif_unpack(args->v0, 0, 0, false)) {
		nvif_ioctl(parent, "create disp base channel dma vers %d "
				   "pushbuf %016llx head %d\n",
			   args->v0.version, args->v0.pushbuf, args->v0.head);
		if (args->v0.head > disp->head.nr)
			return -EINVAL;
	} else
		return ret;

	ret = nv50_disp_dmac_create_(parent, engine, oclass, args->v0.pushbuf,
				     args->v0.head, sizeof(*dmac),
				     (void **)&dmac);
	*pobject = nv_object(dmac);
	if (ret)
		return ret;

	return 0;
}

struct nv50_disp_chan_impl
nv50_disp_base_ofuncs = {
	.base.ctor = nv50_disp_base_ctor,
	.base.dtor = nv50_disp_dmac_dtor,
	.base.init = nv50_disp_dmac_init,
	.base.fini = nv50_disp_dmac_fini,
	.base.ntfy = nv50_disp_chan_ntfy,
	.base.map  = nv50_disp_chan_map,
	.base.rd32 = nv50_disp_chan_rd32,
	.base.wr32 = nv50_disp_chan_wr32,
	.chid = 1,
	.attach = nv50_disp_dmac_object_attach,
	.detach = nv50_disp_dmac_object_detach,
};
