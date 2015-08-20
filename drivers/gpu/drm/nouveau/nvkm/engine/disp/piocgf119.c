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

#include <subdev/timer.h>

int
gf119_disp_pioc_fini(struct nvkm_object *object, bool suspend)
{
	struct nv50_disp *disp = (void *)object->engine;
	struct nv50_disp_pioc *pioc = (void *)object;
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int chid = pioc->base.chid;

	nvkm_mask(device, 0x610490 + (chid * 0x10), 0x00000001, 0x00000000);
	if (nvkm_msec(device, 2000,
		if (!(nvkm_rd32(device, 0x610490 + (chid * 0x10)) & 0x00030000))
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d fini: %08x\n", chid,
			   nvkm_rd32(device, 0x610490 + (chid * 0x10)));
		if (suspend)
			return -EBUSY;
	}

	/* disable error reporting and completion notification */
	nvkm_mask(device, 0x610090, 0x00000001 << chid, 0x00000000);
	nvkm_mask(device, 0x6100a0, 0x00000001 << chid, 0x00000000);

	return nv50_disp_chan_fini(&pioc->base, suspend);
}

int
gf119_disp_pioc_init(struct nvkm_object *object)
{
	struct nv50_disp *disp = (void *)object->engine;
	struct nv50_disp_pioc *pioc = (void *)object;
	struct nvkm_subdev *subdev = &disp->base.engine.subdev;
	struct nvkm_device *device = subdev->device;
	int chid = pioc->base.chid;
	int ret;

	ret = nv50_disp_chan_init(&pioc->base);
	if (ret)
		return ret;

	/* enable error reporting */
	nvkm_mask(device, 0x6100a0, 0x00000001 << chid, 0x00000001 << chid);

	/* activate channel */
	nvkm_wr32(device, 0x610490 + (chid * 0x10), 0x00000001);
	if (nvkm_msec(device, 2000,
		u32 tmp = nvkm_rd32(device, 0x610490 + (chid * 0x10));
		if ((tmp & 0x00030000) == 0x00010000)
			break;
	) < 0) {
		nvkm_error(subdev, "ch %d init: %08x\n", chid,
			   nvkm_rd32(device, 0x610490 + (chid * 0x10)));
		return -EBUSY;
	}

	return 0;
}
