/*
 * Copyright 2015 Red Hat Inc.
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
#include "ram.h"

int
nvkm_ram_init(struct nvkm_ram *ram)
{
	if (ram->func->init)
		return ram->func->init(ram);
	return 0;
}

void
nvkm_ram_del(struct nvkm_ram **pram)
{
	struct nvkm_ram *ram = *pram;
	if (ram && !WARN_ON(!ram->func)) {
		if (ram->func->dtor)
			*pram = ram->func->dtor(ram);
		nvkm_mm_fini(&ram->tags);
		nvkm_mm_fini(&ram->vram);
		kfree(*pram);
		*pram = NULL;
	}
}

int
nvkm_ram_ctor(const struct nvkm_ram_func *func, struct nvkm_fb *fb,
	      enum nvkm_ram_type type, u64 size, u32 tags,
	      struct nvkm_ram *ram)
{
	static const char *name[] = {
		[NVKM_RAM_TYPE_UNKNOWN] = "of unknown memory type",
		[NVKM_RAM_TYPE_STOLEN ] = "stolen system memory",
		[NVKM_RAM_TYPE_SGRAM  ] = "SGRAM",
		[NVKM_RAM_TYPE_SDRAM  ] = "SDRAM",
		[NVKM_RAM_TYPE_DDR1   ] = "DDR1",
		[NVKM_RAM_TYPE_DDR2   ] = "DDR2",
		[NVKM_RAM_TYPE_DDR3   ] = "DDR3",
		[NVKM_RAM_TYPE_GDDR2  ] = "GDDR2",
		[NVKM_RAM_TYPE_GDDR3  ] = "GDDR3",
		[NVKM_RAM_TYPE_GDDR4  ] = "GDDR4",
		[NVKM_RAM_TYPE_GDDR5  ] = "GDDR5",
	};
	struct nvkm_subdev *subdev = &fb->subdev;
	int ret;

	nvkm_info(subdev, "%d MiB %s\n", (int)(size >> 20), name[type]);
	ram->func = func;
	ram->fb = fb;
	ram->type = type;
	ram->size = size;

	if (!nvkm_mm_initialised(&ram->vram)) {
		ret = nvkm_mm_init(&ram->vram, 0, size >> NVKM_RAM_MM_SHIFT, 1);
		if (ret)
			return ret;
	}

	if (!nvkm_mm_initialised(&ram->tags)) {
		ret = nvkm_mm_init(&ram->tags, 0, tags ? ++tags : 0, 1);
		if (ret)
			return ret;

		nvkm_debug(subdev, "%d compression tags\n", tags);
	}

	return 0;
}

int
nvkm_ram_new_(const struct nvkm_ram_func *func, struct nvkm_fb *fb,
	      enum nvkm_ram_type type, u64 size, u32 tags,
	      struct nvkm_ram **pram)
{
	if (!(*pram = kzalloc(sizeof(**pram), GFP_KERNEL)))
		return -ENOMEM;
	return nvkm_ram_ctor(func, fb, type, size, tags, *pram);
}
