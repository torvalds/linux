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
#include <core/ramht.h>
#include <subdev/fb.h>
#include <subdev/mmu.h>
#include <subdev/timer.h>
#include <engine/dma.h>

int
nv50_disp_dmac_new_(const struct nv50_disp_chan_func *func,
		    const struct nv50_disp_chan_mthd *mthd,
		    struct nv50_disp *disp, int chid, int head, u64 push,
		    const struct nvkm_oclass *oclass,
		    struct nvkm_object **pobject)
{
	struct nvkm_client *client = oclass->client;
	struct nv50_disp_chan *chan;
	int ret;

	ret = nv50_disp_chan_new_(func, mthd, disp, chid, chid, head, oclass,
				  pobject);
	chan = nv50_disp_chan(*pobject);
	if (ret)
		return ret;

	chan->memory = nvkm_umem_search(client, push);
	if (IS_ERR(chan->memory))
		return PTR_ERR(chan->memory);

	if (nvkm_memory_size(chan->memory) < 0x1000)
		return -EINVAL;

	switch (nvkm_memory_target(chan->memory)) {
	case NVKM_MEM_TARGET_VRAM: chan->push = 0x00000001; break;
	case NVKM_MEM_TARGET_NCOH: chan->push = 0x00000002; break;
	case NVKM_MEM_TARGET_HOST: chan->push = 0x00000003; break;
	default:
		return -EINVAL;
	}

	chan->push |= nvkm_memory_addr(chan->memory) >> 8;
	return 0;
}

int
nv50_disp_dmac_bind(struct nv50_disp_chan *chan,
		    struct nvkm_object *object, u32 handle)
{
	return nvkm_ramht_insert(chan->disp->ramht, object,
				 chan->chid.user, -10, handle,
				 chan->chid.user << 28 |
				 chan->chid.user);
}

static void
nv50_disp_dmac_fini(struct nv50_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	/* deactivate channel */
	nvkm_mask(device, 0x610200 + (ctrl * 0x0010), 0x00001010, 0x00001000);
	nvkm_mask(device, 0x610200 + (ctrl * 0x0010), 0x00000003, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610200 + (ctrl * 0x10)) & 0x001e0000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d fini timeout, %08x\n", user,
			   nvkm_rd32(device, 0x610200 + (ctrl * 0x10)));
	}
}

static int
nv50_disp_dmac_init(struct nv50_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	/* initialise channel for dma command submission */
	nvkm_wr32(device, 0x610204 + (ctrl * 0x0010), chan->push);
	nvkm_wr32(device, 0x610208 + (ctrl * 0x0010), 0x00010000);
	nvkm_wr32(device, 0x61020c + (ctrl * 0x0010), ctrl);
	nvkm_mask(device, 0x610200 + (ctrl * 0x0010), 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x640000 + (ctrl * 0x1000), 0x00000000);
	nvkm_wr32(device, 0x610200 + (ctrl * 0x0010), 0x00000013);

	/* wait for it to go inactive */
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610200 + (ctrl * 0x10)) & 0x80000000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d init timeout, %08x\n", user,
			   nvkm_rd32(device, 0x610200 + (ctrl * 0x10)));
		return -EBUSY;
	}

	return 0;
}

const struct nv50_disp_chan_func
nv50_disp_dmac_func = {
	.init = nv50_disp_dmac_init,
	.fini = nv50_disp_dmac_fini,
	.intr = nv50_disp_chan_intr,
	.user = nv50_disp_chan_user,
	.bind = nv50_disp_dmac_bind,
};
