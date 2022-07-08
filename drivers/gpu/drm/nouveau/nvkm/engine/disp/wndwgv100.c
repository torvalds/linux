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

#include <core/client.h>

#include <nvif/clc37e.h>
#include <nvif/unpack.h>

static const struct nv50_disp_mthd_list
gv100_disp_wndw_mthd_base = {
	.mthd = 0x0000,
	.addr = 0x000000,
	.data = {
		{ 0x0200, 0x690200 },
		{ 0x020c, 0x69020c },
		{ 0x0210, 0x690210 },
		{ 0x0214, 0x690214 },
		{ 0x0218, 0x690218 },
		{ 0x021c, 0x69021c },
		{ 0x0220, 0x690220 },
		{ 0x0224, 0x690224 },
		{ 0x0228, 0x690228 },
		{ 0x022c, 0x69022c },
		{ 0x0230, 0x690230 },
		{ 0x0234, 0x690234 },
		{ 0x0238, 0x690238 },
		{ 0x0240, 0x690240 },
		{ 0x0244, 0x690244 },
		{ 0x0248, 0x690248 },
		{ 0x024c, 0x69024c },
		{ 0x0250, 0x690250 },
		{ 0x0254, 0x690254 },
		{ 0x0260, 0x690260 },
		{ 0x0264, 0x690264 },
		{ 0x0268, 0x690268 },
		{ 0x026c, 0x69026c },
		{ 0x0270, 0x690270 },
		{ 0x0274, 0x690274 },
		{ 0x0280, 0x690280 },
		{ 0x0284, 0x690284 },
		{ 0x0288, 0x690288 },
		{ 0x028c, 0x69028c },
		{ 0x0290, 0x690290 },
		{ 0x0298, 0x690298 },
		{ 0x029c, 0x69029c },
		{ 0x02a0, 0x6902a0 },
		{ 0x02a4, 0x6902a4 },
		{ 0x02a8, 0x6902a8 },
		{ 0x02ac, 0x6902ac },
		{ 0x02b0, 0x6902b0 },
		{ 0x02b4, 0x6902b4 },
		{ 0x02b8, 0x6902b8 },
		{ 0x02bc, 0x6902bc },
		{ 0x02c0, 0x6902c0 },
		{ 0x02c4, 0x6902c4 },
		{ 0x02c8, 0x6902c8 },
		{ 0x02cc, 0x6902cc },
		{ 0x02d0, 0x6902d0 },
		{ 0x02d4, 0x6902d4 },
		{ 0x02d8, 0x6902d8 },
		{ 0x02dc, 0x6902dc },
		{ 0x02e0, 0x6902e0 },
		{ 0x02e4, 0x6902e4 },
		{ 0x02e8, 0x6902e8 },
		{ 0x02ec, 0x6902ec },
		{ 0x02f0, 0x6902f0 },
		{ 0x02f4, 0x6902f4 },
		{ 0x02f8, 0x6902f8 },
		{ 0x02fc, 0x6902fc },
		{ 0x0300, 0x690300 },
		{ 0x0304, 0x690304 },
		{ 0x0308, 0x690308 },
		{ 0x0310, 0x690310 },
		{ 0x0314, 0x690314 },
		{ 0x0318, 0x690318 },
		{ 0x031c, 0x69031c },
		{ 0x0320, 0x690320 },
		{ 0x0324, 0x690324 },
		{ 0x0328, 0x690328 },
		{ 0x032c, 0x69032c },
		{ 0x033c, 0x69033c },
		{ 0x0340, 0x690340 },
		{ 0x0344, 0x690344 },
		{ 0x0348, 0x690348 },
		{ 0x034c, 0x69034c },
		{ 0x0350, 0x690350 },
		{ 0x0354, 0x690354 },
		{ 0x0358, 0x690358 },
		{ 0x0364, 0x690364 },
		{ 0x0368, 0x690368 },
		{ 0x036c, 0x69036c },
		{ 0x0370, 0x690370 },
		{ 0x0374, 0x690374 },
		{ 0x0380, 0x690380 },
		{}
	}
};

static const struct nv50_disp_chan_mthd
gv100_disp_wndw_mthd = {
	.name = "Window",
	.addr = 0x001000,
	.prev = 0x000800,
	.data = {
		{ "Global", 1, &gv100_disp_wndw_mthd_base },
		{}
	}
};

static void
gv100_disp_wndw_intr(struct nv50_disp_chan *chan, bool en)
{
	struct nvkm_device *device = chan->disp->base.engine.subdev.device;
	const u32 mask = 0x00000001 << chan->head;
	const u32 data = en ? mask : 0;
	nvkm_mask(device, 0x611da4, mask, data);
}

static const struct nv50_disp_chan_func
gv100_disp_wndw = {
	.init = gv100_disp_dmac_init,
	.fini = gv100_disp_dmac_fini,
	.intr = gv100_disp_wndw_intr,
	.user = gv100_disp_chan_user,
	.bind = gv100_disp_dmac_bind,
};

static int
gv100_disp_wndw_new_(const struct nv50_disp_chan_func *func,
		     const struct nv50_disp_chan_mthd *mthd,
		     struct nv50_disp *disp, int chid,
		     const struct nvkm_oclass *oclass, void *argv, u32 argc,
		     struct nvkm_object **pobject)
{
	union {
		struct nvc37e_window_channel_dma_v0 v0;
	} *args = argv;
	struct nvkm_object *parent = oclass->parent;
	int wndw, ret = -ENOSYS;
	u64 push;

	nvif_ioctl(parent, "create window channel dma size %d\n", argc);
	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		nvif_ioctl(parent, "create window channel dma vers %d "
				   "pushbuf %016llx index %d\n",
			   args->v0.version, args->v0.pushbuf, args->v0.index);
		if (!(disp->wndw.mask & BIT(args->v0.index)))
			return -EINVAL;
		push = args->v0.pushbuf;
		wndw = args->v0.index;
	} else
		return ret;

	return nv50_disp_dmac_new_(func, mthd, disp, chid + wndw,
				   wndw, push, oclass, pobject);
}

int
gv100_disp_wndw_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nv50_disp *disp, struct nvkm_object **pobject)
{
	return gv100_disp_wndw_new_(&gv100_disp_wndw, &gv100_disp_wndw_mthd,
				    disp, 1, oclass, argv, argc, pobject);
}
