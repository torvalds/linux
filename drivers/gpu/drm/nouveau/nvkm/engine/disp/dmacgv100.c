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

#include <core/ramht.h>
#include <subdev/timer.h>

static int
gv100_disp_dmac_idle(struct nv50_disp_chan *chan)
{
	struct nvkm_device *device = chan->disp->base.engine.subdev.device;
	const u32 soff = (chan->chid.ctrl - 1) * 0x04;
	nvkm_msec(device, 2000,
		u32 stat = nvkm_rd32(device, 0x610664 + soff);
		if ((stat & 0x000f0000) == 0x00040000)
			return 0;
	);
	return -EBUSY;
}

int
gv100_disp_dmac_bind(struct nv50_disp_chan *chan,
		     struct nvkm_object *object, u32 handle)
{
	return nvkm_ramht_insert(chan->disp->ramht, object,
				 chan->chid.user, -9, handle,
				 chan->chid.user << 25 | 0x00000040);
}

void
gv100_disp_dmac_fini(struct nv50_disp_chan *chan)
{
	struct nvkm_device *device = chan->disp->base.engine.subdev.device;
	const u32 coff = chan->chid.ctrl * 0x04;
	nvkm_mask(device, 0x6104e0 + coff, 0x00000010, 0x00000000);
	gv100_disp_dmac_idle(chan);
	nvkm_mask(device, 0x6104e0 + coff, 0x00000002, 0x00000000);
}

int
gv100_disp_dmac_init(struct nv50_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	const u32 uoff = (chan->chid.ctrl - 1) * 0x1000;
	const u32 poff = chan->chid.ctrl * 0x10;
	const u32 coff = chan->chid.ctrl * 0x04;

	nvkm_wr32(device, 0x610b24 + poff, lower_32_bits(chan->push));
	nvkm_wr32(device, 0x610b20 + poff, upper_32_bits(chan->push));
	nvkm_wr32(device, 0x610b28 + poff, 0x00000001);
	nvkm_wr32(device, 0x610b2c + poff, 0x00000040);

	nvkm_mask(device, 0x6104e0 + coff, 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x690000 + uoff, 0x00000000);
	nvkm_wr32(device, 0x6104e0 + coff, 0x00000013);
	return gv100_disp_dmac_idle(chan);
}
