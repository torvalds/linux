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

#include <subdev/timer.h>

static void
gf119_disp_pioc_fini(struct nv50_disp_chan *chan)
{
	struct nv50_disp *disp = chan->disp;
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	nvkm_mask(device, 0x610490 + (ctrl * 0x10), 0x00000001, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610490 + (ctrl * 0x10)) & 0x00030000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d fini: %08x\n", user,
			   nvkm_rd32(device, 0x610490 + (ctrl * 0x10)));
	}
}

static int
gf119_disp_pioc_init(struct nv50_disp_chan *chan)
{
	struct nv50_disp *disp = chan->disp;
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	/* activate channel */
	nvkm_wr32(device, 0x610490 + (ctrl * 0x10), 0x00000001);
	if (nvkm_msec(device, 2000,
		u32 tmp = nvkm_rd32(device, 0x610490 + (ctrl * 0x10));
		if ((tmp & 0x00030000) == 0x00010000)
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d init: %08x\n", user,
			   nvkm_rd32(device, 0x610490 + (ctrl * 0x10)));
		return -EBUSY;
	}

	return 0;
}

const struct nv50_disp_chan_func
gf119_disp_pioc_func = {
	.init = gf119_disp_pioc_init,
	.fini = gf119_disp_pioc_fini,
	.intr = gf119_disp_chan_intr,
	.user = nv50_disp_chan_user,
};
