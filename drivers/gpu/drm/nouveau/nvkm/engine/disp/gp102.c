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
#include "priv.h"
#include "chan.h"
#include "head.h"
#include "ior.h"

#include <subdev/timer.h>

#include <nvif/class.h>

static int
gp102_disp_dmac_init(struct nvkm_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	int ctrl = chan->chid.ctrl;
	int user = chan->chid.user;

	/* initialise channel for dma command submission */
	nvkm_wr32(device, 0x611494 + (ctrl * 0x0010), chan->push);
	nvkm_wr32(device, 0x611498 + (ctrl * 0x0010), 0x00010000);
	nvkm_wr32(device, 0x61149c + (ctrl * 0x0010), 0x00000001);
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

const struct nvkm_disp_chan_func
gp102_disp_dmac_func = {
	.push = nv50_disp_dmac_push,
	.init = gp102_disp_dmac_init,
	.fini = gf119_disp_dmac_fini,
	.intr = gf119_disp_chan_intr,
	.user = nv50_disp_chan_user,
	.bind = gf119_disp_dmac_bind,
};

static const struct nvkm_disp_chan_user
gp102_disp_curs = {
	.func = &gf119_disp_pioc_func,
	.ctrl = 13,
	.user = 17,
};

static const struct nvkm_disp_chan_user
gp102_disp_oimm = {
	.func = &gf119_disp_pioc_func,
	.ctrl = 9,
	.user = 13,
};

static const struct nvkm_disp_chan_user
gp102_disp_ovly = {
	.func = &gp102_disp_dmac_func,
	.ctrl = 5,
	.user = 5,
	.mthd = &gk104_disp_ovly_mthd,
};

static const struct nvkm_disp_chan_user
gp102_disp_base = {
	.func = &gp102_disp_dmac_func,
	.ctrl = 1,
	.user = 1,
	.mthd = &gf119_disp_base_mthd,
};

static int
gp102_disp_core_init(struct nvkm_disp_chan *chan)
{
	struct nvkm_subdev *subdev = &chan->disp->engine.subdev;
	struct nvkm_device *device = subdev->device;

	/* initialise channel for dma command submission */
	nvkm_wr32(device, 0x611494, chan->push);
	nvkm_wr32(device, 0x611498, 0x00010000);
	nvkm_wr32(device, 0x61149c, 0x00000001);
	nvkm_mask(device, 0x610490, 0x00000010, 0x00000010);
	nvkm_wr32(device, 0x640000, chan->suspend_put);
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

static const struct nvkm_disp_chan_func
gp102_disp_core_func = {
	.push = nv50_disp_dmac_push,
	.init = gp102_disp_core_init,
	.fini = gf119_disp_core_fini,
	.intr = gf119_disp_chan_intr,
	.user = nv50_disp_chan_user,
	.bind = gf119_disp_dmac_bind,
};

static const struct nvkm_disp_chan_user
gp102_disp_core = {
	.func = &gp102_disp_core_func,
	.ctrl = 0,
	.user = 0,
	.mthd = &gk104_disp_core_mthd,
};

static void
gp102_disp_intr_error(struct nvkm_disp *disp, int chid)
{
	struct nvkm_subdev *subdev = &disp->engine.subdev;
	struct nvkm_device *device = subdev->device;
	u32 mthd = nvkm_rd32(device, 0x6111f0 + (chid * 12));
	u32 data = nvkm_rd32(device, 0x6111f4 + (chid * 12));
	u32 unkn = nvkm_rd32(device, 0x6111f8 + (chid * 12));

	nvkm_error(subdev, "chid %d mthd %04x data %08x %08x %08x\n",
		   chid, (mthd & 0x0000ffc), data, mthd, unkn);

	if (chid < ARRAY_SIZE(disp->chan)) {
		switch (mthd & 0xffc) {
		case 0x0080:
			nv50_disp_chan_mthd(disp->chan[chid], NV_DBG_ERROR);
			break;
		default:
			break;
		}
	}

	nvkm_wr32(device, 0x61009c, (1 << chid));
	nvkm_wr32(device, 0x6111f0 + (chid * 12), 0x90000000);
}

static const struct nvkm_disp_func
gp102_disp = {
	.oneinit = nv50_disp_oneinit,
	.init = gf119_disp_init,
	.fini = gf119_disp_fini,
	.intr = gf119_disp_intr,
	.intr_error = gp102_disp_intr_error,
	.super = gf119_disp_super,
	.uevent = &gf119_disp_chan_uevent,
	.head = { .cnt = gf119_head_cnt, .new = gf119_head_new },
	.sor = { .cnt = gf119_sor_cnt, .new = gp100_sor_new },
	.root = { 0,0,GP102_DISP },
	.user = {
		{{0,0,GK104_DISP_CURSOR             }, nvkm_disp_chan_new, &gp102_disp_curs },
		{{0,0,GK104_DISP_OVERLAY            }, nvkm_disp_chan_new, &gp102_disp_oimm },
		{{0,0,GK110_DISP_BASE_CHANNEL_DMA   }, nvkm_disp_chan_new, &gp102_disp_base },
		{{0,0,GP102_DISP_CORE_CHANNEL_DMA   }, nvkm_disp_core_new, &gp102_disp_core },
		{{0,0,GK104_DISP_OVERLAY_CONTROL_DMA}, nvkm_disp_chan_new, &gp102_disp_ovly },
		{}
	},
};

int
gp102_disp_new(struct nvkm_device *device, enum nvkm_subdev_type type, int inst,
	       struct nvkm_disp **pdisp)
{
	return nvkm_disp_new_(&gp102_disp, device, type, inst, pdisp);
}
