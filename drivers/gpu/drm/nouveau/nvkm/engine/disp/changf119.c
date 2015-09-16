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

static void
gf119_disp_chan_uevent_fini(struct nvkm_event *event, int type, int index)
{
	struct nv50_disp *disp = container_of(event, typeof(*disp), uevent);
	struct nvkm_device *device = disp->base.engine.subdev.device;
	nvkm_mask(device, 0x610090, 0x00000001 << index, 0x00000000 << index);
	nvkm_wr32(device, 0x61008c, 0x00000001 << index);
}

static void
gf119_disp_chan_uevent_init(struct nvkm_event *event, int types, int index)
{
	struct nv50_disp *disp = container_of(event, typeof(*disp), uevent);
	struct nvkm_device *device = disp->base.engine.subdev.device;
	nvkm_wr32(device, 0x61008c, 0x00000001 << index);
	nvkm_mask(device, 0x610090, 0x00000001 << index, 0x00000001 << index);
}

const struct nvkm_event_func
gf119_disp_chan_uevent = {
	.ctor = nv50_disp_chan_uevent_ctor,
	.init = gf119_disp_chan_uevent_init,
	.fini = gf119_disp_chan_uevent_fini,
};
