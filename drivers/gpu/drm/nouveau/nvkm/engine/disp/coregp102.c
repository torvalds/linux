/*
 * Copyright 2016 Red Hat Inc.
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
 * Authors: Ben Skeggs <bskeggs@redhat.com>
 */
#include "dmacnv50.h"
#include "rootnv50.h"

#include <subdev/timer.h>

#include <nvif/class.h>

static int
gp102_disp_core_init(struct nv50_disp_dmac *chan)
{
	struct nv50_disp *disp = chan->base.root->disp;
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;

	/* enable error reporting */
	nvkm_mask(device, 0x6100a0, 0x00000001, 0x00000001);

	/* initialise channel for dma command submission */
	nvkm_wr32(device, 0x611494, chan->push);
	nvkm_wr32(device, 0x611498, 0x00010000);
	nvkm_wr32(device, 0x61149c, 0x00000001);
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

static const struct nv50_disp_dmac_func
gp102_disp_core_func = {
	.init = gp102_disp_core_init,
	.fini = gf119_disp_core_fini,
	.bind = gf119_disp_dmac_bind,
};

const struct nv50_disp_dmac_oclass
gp102_disp_core_oclass = {
	.base.oclass = GP102_DISP_CORE_CHANNEL_DMA,
	.base.minver = 0,
	.base.maxver = 0,
	.ctor = nv50_disp_core_new,
	.func = &gp102_disp_core_func,
	.mthd = &gk104_disp_core_chan_mthd,
	.chid = 0,
};
