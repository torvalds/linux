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

#include <nvif/clc37b.h>
#include <nvif/unpack.h>

static void
gv100_disp_wimm_intr(struct nv50_disp_chan *chan, bool en)
{
	struct nvkm_device *device = chan->disp->base.engine.subdev.device;
	const u32 mask = 0x00000001 << chan->head;
	const u32 data = en ? mask : 0;
	nvkm_mask(device, 0x611da8, mask, data);
}

static const struct nv50_disp_chan_func
gv100_disp_wimm = {
	.init = gv100_disp_dmac_init,
	.fini = gv100_disp_dmac_fini,
	.intr = gv100_disp_wimm_intr,
	.user = gv100_disp_chan_user,
};

static int
gv100_disp_wimm_new_(const struct nv50_disp_chan_func *func,
		     const struct nv50_disp_chan_mthd *mthd,
		     struct nv50_disp *disp, int chid,
		     const struct nvkm_oclass *oclass, void *argv, u32 argc,
		     struct nvkm_object **pobject)
{
	union {
		struct nvc37b_window_imm_channel_dma_v0 v0;
	} *args = argv;
	struct nvkm_object *parent = oclass->parent;
	int wndw, ret = -ENOSYS;
	u64 push;

	nvif_ioctl(parent, "create window imm channel dma size %d\n", argc);
	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		nvif_ioctl(parent, "create window imm channel dma vers %d "
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
gv100_disp_wimm_new(const struct nvkm_oclass *oclass, void *argv, u32 argc,
		    struct nv50_disp *disp, struct nvkm_object **pobject)
{
	return gv100_disp_wimm_new_(&gv100_disp_wimm, NULL, disp, 33,
				    oclass, argv, argc, pobject);
}
