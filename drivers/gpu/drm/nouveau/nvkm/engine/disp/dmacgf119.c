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

#include <core/ramht.h>
#include <subdev/timer.h>

int
gf119_disp_dmac_bind(struct nv50_disp_chan *chan,
		     struct nvkm_object *object, u32 handle)
{
	return nvkm_ramht_insert(chan->disp->ramht, object,
				 chan->chid.user, -9, handle,
				 chan->chid.user << 27 | 0x00000001);
}

void
gf119_disp_dmac_fini(struct nv50_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	/* deactivate channel */
	nvkm_mask(device, 0x610490 + (ctrl * 0x0010), 0x00001010, 0x00001000);
	nvkm_mask(device, 0x610490 + (ctrl * 0x0010), 0x00000003, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610490 + (ctrl * 0x10)) & 0x001e0000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d fini: %08x\n", user,
			   nvkm_rd32(device, 0x610490 + (ctrl * 0x10)));
	}

	chan->suspend_put = nvkm_rd32(device, 0x640000 + (ctrl * 0x1000));
}

static int
gf119_disp_dmac_init(struct nv50_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	/* initialise channel for dma command submission */
	nvkm_wr32(device, 0x610494 + (ctrl * 0x0010), chan->push);
	nvkm_wr32(device, 0x610498 + (ctrl * 0x0010), 0x00010000);
	nvkm_wr32(device, 0x61049c + (ctrl * 0x0010), 0x00000001);
	nvkm_mask(device, 0x610490 + (ctrl * 0x0010), 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x640000 + (ctrl * 0x1000), chan->suspend_put);
	nvkm_wr32(device, 0x610490 + (ctrl * 0x0010), 0x00000013);

	/* wait for it to go inactive */
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610490 + (ctrl * 0x10)) & 0x80000000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d init: %08x\n", user,
			   nvkm_rd32(device, 0x610490 + (ctrl * 0x10)));
		return -EBUSY;
	}

	return 0;
}

const struct nv50_disp_chan_func
gf119_disp_dmac_func = {
	.init = gf119_disp_dmac_init,
	.fini = gf119_disp_dmac_fini,
	.intr = gf119_disp_chan_intr,
	.user = nv50_disp_chan_user,
	.bind = gf119_disp_dmac_bind,
};
