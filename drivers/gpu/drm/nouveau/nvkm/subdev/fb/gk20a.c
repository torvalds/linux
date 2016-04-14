/*
 * Copyright (c) 2014, NVIDIA CORPORATION. All rights reserved.
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
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include "priv.h"

static void
gk20a_fb_init(struct nvkm_fb *fb)
{
	struct nvkm_device *device = fb->subdev.device;
	nvkm_mask(device, 0x100c80, 0x00000001, 0x00000000); /* 128KiB lpg */
}

static const struct nvkm_fb_func
gk20a_fb = {
	.oneinit = gf100_fb_oneinit,
	.init = gk20a_fb_init,
	.memtype_valid = gf100_fb_memtype_valid,
};

int
gk20a_fb_new(struct nvkm_device *device, int index, struct nvkm_fb **pfb)
{
	return nvkm_fb_new_(&gk20a_fb, device, index, pfb);
}
