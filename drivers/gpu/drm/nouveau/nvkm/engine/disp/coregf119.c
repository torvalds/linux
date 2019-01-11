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

#include <subdev/timer.h>

const struct nv50_disp_mthd_list
gf119_disp_core_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0080, 0x660080 },
		{ 0x0084, 0x660084 },
		{ 0x0088, 0x660088 },
		{ 0x008c, 0x000000 },
		{}
	}
};

const struct nv50_disp_mthd_list
gf119_disp_core_mthd_dac = {
	.mthd = 0x0020,
	.addr = 0x000020,
	.data = {
		{ 0x0180, 0x660180 },
		{ 0x0184, 0x660184 },
		{ 0x0188, 0x660188 },
		{ 0x0190, 0x660190 },
		{}
	}
};

const struct nv50_disp_mthd_list
gf119_disp_core_mthd_sor = {
	.mthd = 0x0020,
	.addr = 0x000020,
	.data = {
		{ 0x0200, 0x660200 },
		{ 0x0204, 0x660204 },
		{ 0x0208, 0x660208 },
		{ 0x0210, 0x660210 },
		{}
	}
};

const struct nv50_disp_mthd_list
gf119_disp_core_mthd_pior = {
	.mthd = 0x0020,
	.addr = 0x000020,
	.data = {
		{ 0x0300, 0x660300 },
		{ 0x0304, 0x660304 },
		{ 0x0308, 0x660308 },
		{ 0x0310, 0x660310 },
		{}
	}
};

static const struct nv50_disp_mthd_list
gf119_disp_core_mthd_head = {
	.mthd = 0x0300,
	.addr = 0x000300,
	.data = {
		{ 0x0400, 0x660400 },
		{ 0x0404, 0x660404 },
		{ 0x0408, 0x660408 },
		{ 0x040c, 0x66040c },
		{ 0x0410, 0x660410 },
		{ 0x0414, 0x660414 },
		{ 0x0418, 0x660418 },
		{ 0x041c, 0x66041c },
		{ 0x0420, 0x660420 },
		{ 0x0424, 0x660424 },
		{ 0x0428, 0x660428 },
		{ 0x042c, 0x66042c },
		{ 0x0430, 0x660430 },
		{ 0x0434, 0x660434 },
		{ 0x0438, 0x660438 },
		{ 0x0440, 0x660440 },
		{ 0x0444, 0x660444 },
		{ 0x0448, 0x660448 },
		{ 0x044c, 0x66044c },
		{ 0x0450, 0x660450 },
		{ 0x0454, 0x660454 },
		{ 0x0458, 0x660458 },
		{ 0x045c, 0x66045c },
		{ 0x0460, 0x660460 },
		{ 0x0468, 0x660468 },
		{ 0x046c, 0x66046c },
		{ 0x0470, 0x660470 },
		{ 0x0474, 0x660474 },
		{ 0x0480, 0x660480 },
		{ 0x0484, 0x660484 },
		{ 0x048c, 0x66048c },
		{ 0x0490, 0x660490 },
		{ 0x0494, 0x660494 },
		{ 0x0498, 0x660498 },
		{ 0x04b0, 0x6604b0 },
		{ 0x04b8, 0x6604b8 },
		{ 0x04bc, 0x6604bc },
		{ 0x04c0, 0x6604c0 },
		{ 0x04c4, 0x6604c4 },
		{ 0x04c8, 0x6604c8 },
		{ 0x04d0, 0x6604d0 },
		{ 0x04d4, 0x6604d4 },
		{ 0x04e0, 0x6604e0 },
		{ 0x04e4, 0x6604e4 },
		{ 0x04e8, 0x6604e8 },
		{ 0x04ec, 0x6604ec },
		{ 0x04f0, 0x6604f0 },
		{ 0x04f4, 0x6604f4 },
		{ 0x04f8, 0x6604f8 },
		{ 0x04fc, 0x6604fc },
		{ 0x0500, 0x660500 },
		{ 0x0504, 0x660504 },
		{ 0x0508, 0x660508 },
		{ 0x050c, 0x66050c },
		{ 0x0510, 0x660510 },
		{ 0x0514, 0x660514 },
		{ 0x0518, 0x660518 },
		{ 0x051c, 0x66051c },
		{ 0x052c, 0x66052c },
		{ 0x0530, 0x660530 },
		{ 0x054c, 0x66054c },
		{ 0x0550, 0x660550 },
		{ 0x0554, 0x660554 },
		{ 0x0558, 0x660558 },
		{ 0x055c, 0x66055c },
		{}
	}
};

static const struct nv50_disp_chan_mthd
gf119_disp_core_mthd = {
	.name = "Core",
	.addr = 0x000000,
	.prev = -0x020000,
	.data = {
		{ "Global", 1, &gf119_disp_core_mthd_base },
		{    "DAC", 3, &gf119_disp_core_mthd_dac  },
		{    "SOR", 8, &gf119_disp_core_mthd_sor  },
		{   "PIOR", 4, &gf119_disp_core_mthd_pior },
		{   "HEAD", 4, &gf119_disp_core_mthd_head },
		{}
	}
};

void
gf119_disp_core_fini(struct nv50_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;

	/* deactivate channel */
	nvkm_mask(device, 0x610490, 0x00000010, 0x00000000);
	nvkm_mask(device, 0x610490, 0x00000003, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610490) & 0x001e0000))
			break;
	) < 0) {
		nvkm_error(subdev, "core fini: %08x\n",
			   nvkm_rd32(device, 0x610490));
	}
}

static int
gf119_disp_core_init(struct nv50_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;

	/* initialise channel for dma command submission */
	nvkm_wr32(device, 0x610494, chan->push);
	nvkm_wr32(device, 0x610498, 0x00010000);
	nvkm_wr32(device, 0x61049c, 0x00000001);
	nvkm_mask(device, 0x610490, 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x640000, 0x00000000);
	nvkm_wr32(device, 0x610490, 0x01000013);

	/* wait for it to go inactive */
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610490) & 0x80000000))
			break;
	) < 0) {
		nvkm_error(subdev, "core init: %08x\n",
			   nvkm_rd32(device, 0x610490));
		return -EBUSY;
	}

	return 0;
}

const struct nv50_disp_chan_func
gf119_disp_core_func = {
	.init = gf119_disp_core_init,
	.fini = gf119_disp_core_fini,
	.intr = gf119_disp_chan_intr,
	.user = nv50_disp_chan_user,
	.bind = gf119_disp_dmac_bind,
};

int
gf119_disp_core_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nv50_disp *disp, struct nvkm_object **pobject)
{
	return nv50_disp_core_new_(&gf119_disp_core_func, &gf119_disp_core_mthd,
				   disp, 0, oclass, argv, argc, pobject);
}
