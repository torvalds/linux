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
#include <subdev/timer.h>

#include <nvif/cl507d.h>
#include <nvif/unpack.h>

int
nv50_disp_core_new_(const struct nv50_disp_chan_func *func,
		    const struct nv50_disp_chan_mthd *mthd,
		    struct nv50_disp *disp, int chid,
		    const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nvkm_object **pobject)
{
	union {
		struct nv50_disp_core_channel_dma_v0 v0;
	} *args = argv;
	struct nvkm_object *parent = oclass->parent;
	u64 push;
	int ret = -ENOSYS;

	nvif_ioctl(parent, "create disp core channel dma size %d\n", argc);
	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		nvif_ioctl(parent, "create disp core channel dma vers %d "
				   "pushbuf %016llx\n",
			   args->v0.version, args->v0.pushbuf);
		push = args->v0.pushbuf;
	} else
		return ret;

	return nv50_disp_dmac_new_(func, mthd, disp, chid, 0,
				   push, oclass, pobject);
}

const struct nv50_disp_mthd_list
nv50_disp_core_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x000000 },
		{ 0x0084, 0x610bb8 },
		{ 0x0088, 0x610b9c },
		{ 0x008c, 0x000000 },
		{}
	}
};

static const struct nv50_disp_mthd_list
nv50_disp_core_mthd_dac = {
	.mthd = 0x0080,
	.addr = 0x000008,
	.data = {
		{ 0x0400, 0x610b58 },
		{ 0x0404, 0x610bdc },
		{ 0x0420, 0x610828 },
		{}
	}
};

const struct nv50_disp_mthd_list
nv50_disp_core_mthd_sor = {
	.mthd = 0x0040,
	.addr = 0x000008,
	.data = {
		{ 0x0600, 0x610b70 },
		{}
	}
};

const struct nv50_disp_mthd_list
nv50_disp_core_mthd_pior = {
	.mthd = 0x0040,
	.addr = 0x000008,
	.data = {
		{ 0x0700, 0x610b80 },
		{}
	}
};

static const struct nv50_disp_mthd_list
nv50_disp_core_mthd_head = {
	.mthd = 0x0400,
	.addr = 0x000540,
	.data = {
		{ 0x0800, 0x610ad8 },
		{ 0x0804, 0x610ad0 },
		{ 0x0808, 0x610a48 },
		{ 0x080c, 0x610a78 },
		{ 0x0810, 0x610ac0 },
		{ 0x0814, 0x610af8 },
		{ 0x0818, 0x610b00 },
		{ 0x081c, 0x610ae8 },
		{ 0x0820, 0x610af0 },
		{ 0x0824, 0x610b08 },
		{ 0x0828, 0x610b10 },
		{ 0x082c, 0x610a68 },
		{ 0x0830, 0x610a60 },
		{ 0x0834, 0x000000 },
		{ 0x0838, 0x610a40 },
		{ 0x0840, 0x610a24 },
		{ 0x0844, 0x610a2c },
		{ 0x0848, 0x610aa8 },
		{ 0x084c, 0x610ab0 },
		{ 0x0860, 0x610a84 },
		{ 0x0864, 0x610a90 },
		{ 0x0868, 0x610b18 },
		{ 0x086c, 0x610b20 },
		{ 0x0870, 0x610ac8 },
		{ 0x0874, 0x610a38 },
		{ 0x0880, 0x610a58 },
		{ 0x0884, 0x610a9c },
		{ 0x08a0, 0x610a70 },
		{ 0x08a4, 0x610a50 },
		{ 0x08a8, 0x610ae0 },
		{ 0x08c0, 0x610b28 },
		{ 0x08c4, 0x610b30 },
		{ 0x08c8, 0x610b40 },
		{ 0x08d4, 0x610b38 },
		{ 0x08d8, 0x610b48 },
		{ 0x08dc, 0x610b50 },
		{ 0x0900, 0x610a18 },
		{ 0x0904, 0x610ab8 },
		{}
	}
};

static const struct nv50_disp_chan_mthd
nv50_disp_core_mthd = {
	.name = "Core",
	.addr = 0x000000,
	.prev = 0x000004,
	.data = {
		{ "Global", 1, &nv50_disp_core_mthd_base },
		{    "DAC", 3, &nv50_disp_core_mthd_dac  },
		{    "SOR", 2, &nv50_disp_core_mthd_sor  },
		{   "PIOR", 3, &nv50_disp_core_mthd_pior },
		{   "HEAD", 2, &nv50_disp_core_mthd_head },
		{}
	}
};

static void
nv50_disp_core_fini(struct nv50_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;

	/* deactivate channel */
	nvkm_mask(device, 0x610200, 0x00000010, 0x00000000);
	nvkm_mask(device, 0x610200, 0x00000003, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610200) & 0x001e0000))
			break;
	) < 0) {
		nvkm_error(subdev, "core fini: %08x\n",
			   nvkm_rd32(device, 0x610200));
	}
}

static int
nv50_disp_core_init(struct nv50_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;

	/* attempt to unstick channel from some unknown state */
	if ((nvkm_rd32(device, 0x610200) & 0x009f0000) == 0x00020000)
		nvkm_mask(device, 0x610200, 0x00800000, 0x00800000);
	if ((nvkm_rd32(device, 0x610200) & 0x003f0000) == 0x00030000)
		nvkm_mask(device, 0x610200, 0x00600000, 0x00600000);

	/* initialise channel for dma command submission */
	nvkm_wr32(device, 0x610204, chan->push);
	nvkm_wr32(device, 0x610208, 0x00010000);
	nvkm_wr32(device, 0x61020c, 0x00000000);
	nvkm_mask(device, 0x610200, 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x640000, 0x00000000);
	nvkm_wr32(device, 0x610200, 0x01000013);

	/* wait for it to go inactive */
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610200) & 0x80000000))
			break;
	) < 0) {
		nvkm_error(subdev, "core init: %08x\n",
			   nvkm_rd32(device, 0x610200));
		return -EBUSY;
	}

	return 0;
}

const struct nv50_disp_chan_func
nv50_disp_core_func = {
	.init = nv50_disp_core_init,
	.fini = nv50_disp_core_fini,
	.intr = nv50_disp_chan_intr,
	.user = nv50_disp_chan_user,
	.bind = nv50_disp_dmac_bind,
};

int
nv50_disp_core_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		   struct nv50_disp *disp, struct nvkm_object **pobject)
{
	return nv50_disp_core_new_(&nv50_disp_core_func, &nv50_disp_core_mthd,
				   disp, 0, oclass, argv, argc, pobject);
}
