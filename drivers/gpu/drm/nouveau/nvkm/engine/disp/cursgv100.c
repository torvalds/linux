/*
 * Copyright 2018 Red Hat Inc.
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
 */
#include "channv50.h"

#include <subdev/timer.h>

static int
gv100_disp_curs_idle(struct nv50_disp_chan *chan)
{
	struct nvkm_device *device = chan->disp->base.engine.subdev.device;
	const u32 soff = (chan->chid.ctrl - 1) * 0x04;
	nvkm_msec(device, 2000,
		u32 stat = nvkm_rd32(device, 0x610664 + soff);
		if ((stat & 0x00070000) == 0x00040000)
			return 0;
	);
	return -EBUSY;
}

static void
gv100_disp_curs_intr(struct nv50_disp_chan *chan, bool en)
{
	struct nvkm_device *device = chan->disp->base.engine.subdev.device;
	const u32 mask = 0x00010000 << chan->head;
	const u32 data = en ? mask : 0;
	nvkm_mask(device, 0x611dac, mask, data);
}

static void
gv100_disp_curs_fini(struct nv50_disp_chan *chan)
{
	struct nvkm_device *device = chan->disp->base.engine.subdev.device;
	const u32 hoff = chan->chid.ctrl * 4;
	nvkm_mask(device, 0x6104e0 + hoff, 0x00000010, 0x00000010);
	gv100_disp_curs_idle(chan);
	nvkm_mask(device, 0x6104e0 + hoff, 0x00000001, 0x00000000);
}

static int
gv100_disp_curs_init(struct nv50_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	nvkm_wr32(device, 0x6104e0 + chan->chid.ctrl * 4, 0x00000001);
	return gv100_disp_curs_idle(chan);
}

static const struct nv50_disp_chan_func
gv100_disp_curs = {
	.init = gv100_disp_curs_init,
	.fini = gv100_disp_curs_fini,
	.intr = gv100_disp_curs_intr,
	.user = gv100_disp_chan_user,
};

int
gv100_disp_curs_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nv50_disp *disp, struct nvkm_object **pobject)
{
	return nv50_disp_curs_new_(&gv100_disp_curs, disp, 73, 73,
				   oclass, argv, argc, pobject);
}
