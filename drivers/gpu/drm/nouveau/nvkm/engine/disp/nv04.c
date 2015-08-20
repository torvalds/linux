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
#include "priv.h"

static void
nv04_disp_vblank_init(struct nvkm_event *event, int type, int head)
{
	struct nvkm_disp *disp = container_of(event, typeof(*disp), vblank);
	struct nvkm_device *device = disp->engine.subdev.device;
	nvkm_wr32(device, 0x600140 + (head * 0x2000) , 0x00000001);
}

static void
nv04_disp_vblank_fini(struct nvkm_event *event, int type, int head)
{
	struct nvkm_disp *disp = container_of(event, typeof(*disp), vblank);
	struct nvkm_device *device = disp->engine.subdev.device;
	nvkm_wr32(device, 0x600140 + (head * 0x2000) , 0x00000000);
}

static const struct nvkm_event_func
nv04_disp_vblank_func = {
	.ctor = nvkm_disp_vblank_ctor,
	.init = nv04_disp_vblank_init,
	.fini = nv04_disp_vblank_fini,
};

static void
nv04_disp_intr(struct nvkm_subdev *subdev)
{
	struct nvkm_disp *disp = (void *)subdev;
	struct nvkm_device *device = disp->engine.subdev.device;
	u32 crtc0 = nvkm_rd32(device, 0x600100);
	u32 crtc1 = nvkm_rd32(device, 0x602100);
	u32 pvideo;

	if (crtc0 & 0x00000001) {
		nvkm_disp_vblank(disp, 0);
		nvkm_wr32(device, 0x600100, 0x00000001);
	}

	if (crtc1 & 0x00000001) {
		nvkm_disp_vblank(disp, 1);
		nvkm_wr32(device, 0x602100, 0x00000001);
	}

	if (nv_device(disp)->chipset >= 0x10 &&
	    nv_device(disp)->chipset <= 0x40) {
		pvideo = nvkm_rd32(device, 0x8100);
		if (pvideo & ~0x11)
			nvkm_info(subdev, "PVIDEO intr: %08x\n", pvideo);
		nvkm_wr32(device, 0x8100, pvideo);
	}
}

static int
nv04_disp_ctor(struct nvkm_object *parent, struct nvkm_object *engine,
	       struct nvkm_oclass *oclass, void *data, u32 size,
	       struct nvkm_object **pobject)
{
	struct nvkm_disp *disp;
	int ret;

	ret = nvkm_disp_create(parent, engine, oclass, 2, "DISPLAY",
			       "display", &disp);
	*pobject = nv_object(disp);
	if (ret)
		return ret;

	nv_engine(disp)->sclass = nv04_disp_sclass;
	nv_subdev(disp)->intr = nv04_disp_intr;
	return 0;
}

struct nvkm_oclass *
nv04_disp_oclass = &(struct nvkm_disp_impl) {
	.base.handle = NV_ENGINE(DISP, 0x04),
	.base.ofuncs = &(struct nvkm_ofuncs) {
		.ctor = nv04_disp_ctor,
		.dtor = _nvkm_disp_dtor,
		.init = _nvkm_disp_init,
		.fini = _nvkm_disp_fini,
	},
	.vblank = &nv04_disp_vblank_func,
}.base;
