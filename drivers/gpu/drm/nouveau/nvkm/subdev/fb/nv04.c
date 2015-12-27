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
#include "ram.h"
#include "regsnv04.h"

bool
nv04_fb_memtype_valid(struct nvkm_fb *fb, u32 tile_flags)
{
	if (!(tile_flags & 0xff00))
		return true;
	return false;
}

static void
nv04_fb_init(struct nvkm_fb *fb)
{
	struct nvkm_device *device = fb->subdev.device;

	/* This is what the DDX did for NV_ARCH_04, but a mmio-trace shows
	 * nvidia reading PFB_CFG_0, then writing back its original value.
	 * (which was 0x701114 in this case)
	 */
	nvkm_wr32(device, NV04_PFB_CFG0, 0x1114);
}

static const struct nvkm_fb_func
nv04_fb = {
	.init = nv04_fb_init,
	.ram_new = nv04_ram_new,
	.memtype_valid = nv04_fb_memtype_valid,
};

int
nv04_fb_new(struct nvkm_device *device, int index, struct nvkm_fb **pfb)
{
	return nvkm_fb_new_(&nv04_fb, device, index, pfb);
}
