/*
 * Copyright 2023 Red Hat Inc.
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
#include "priv.h"
#include "ram.h"

#include <subdev/gsp.h>

static const struct nvkm_ram_func
r535_fb_ram = {
};

static int
r535_fb_ram_new(struct nvkm_fb *fb, struct nvkm_ram **pram)
{
	struct nvkm_gsp *gsp = fb->subdev.device->gsp;
	struct nvkm_ram *ram;
	int ret;

	if (!(ram = *pram = kzalloc(sizeof(*ram), GFP_KERNEL)))
		return -ENOMEM;

	ram->func = &r535_fb_ram;
	ram->fb = fb;
	ram->type = NVKM_RAM_TYPE_UNKNOWN; /*TODO: pull this from GSP. */
	ram->size = gsp->fb.size;
	ram->stolen = false;
	mutex_init(&ram->mutex);

	for (int i = 0; i < gsp->fb.region_nr; i++) {
		ret = nvkm_mm_init(&ram->vram, NVKM_RAM_MM_NORMAL,
				   gsp->fb.region[i].addr >> NVKM_RAM_MM_SHIFT,
				   gsp->fb.region[i].size >> NVKM_RAM_MM_SHIFT,
				   1);
		if (ret)
			return ret;
	}

	return 0;
}

static void *
r535_fb_dtor(struct nvkm_fb *fb)
{
	kfree(fb->func);
	return fb;
}

int
r535_fb_new(const struct nvkm_fb_func *hw,
	    struct nvkm_device *device, enum nvkm_subdev_type type, int inst, struct nvkm_fb **pfb)
{
	struct nvkm_fb_func *rm;
	int ret;

	if (!(rm = kzalloc(sizeof(*rm), GFP_KERNEL)))
		return -ENOMEM;

	rm->dtor = r535_fb_dtor;
	rm->sysmem.flush_page_init = hw->sysmem.flush_page_init;
	rm->vidmem.size = hw->vidmem.size;
	rm->ram_new = r535_fb_ram_new;

	ret = nvkm_fb_new_(rm, device, type, inst, pfb);
	if (ret)
		kfree(rm);

	return ret;
}
