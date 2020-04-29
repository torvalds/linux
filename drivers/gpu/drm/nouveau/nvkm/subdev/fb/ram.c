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
#define nvkm_vram(p) container_of((p), struct nvkm_vram, memory)
#include "ram.h"

#include <core/memory.h>
#include <subdev/mmu.h>

struct nvkm_vram {
	struct nvkm_memory memory;
	struct nvkm_ram *ram;
	u8 page;
	struct nvkm_mm_node *mn;
};

static int
nvkm_vram_map(struct nvkm_memory *memory, u64 offset, struct nvkm_vmm *vmm,
	      struct nvkm_vma *vma, void *argv, u32 argc)
{
	struct nvkm_vram *vram = nvkm_vram(memory);
	struct nvkm_vmm_map map = {
		.memory = &vram->memory,
		.offset = offset,
		.mem = vram->mn,
	};

	return nvkm_vmm_map(vmm, vma, argv, argc, &map);
}

static u64
nvkm_vram_size(struct nvkm_memory *memory)
{
	return (u64)nvkm_mm_size(nvkm_vram(memory)->mn) << NVKM_RAM_MM_SHIFT;
}

static u64
nvkm_vram_addr(struct nvkm_memory *memory)
{
	struct nvkm_vram *vram = nvkm_vram(memory);
	if (!nvkm_mm_contiguous(vram->mn))
		return ~0ULL;
	return (u64)nvkm_mm_addr(vram->mn) << NVKM_RAM_MM_SHIFT;
}

static u8
nvkm_vram_page(struct nvkm_memory *memory)
{
	return nvkm_vram(memory)->page;
}

static enum nvkm_memory_target
nvkm_vram_target(struct nvkm_memory *memory)
{
	return NVKM_MEM_TARGET_VRAM;
}

static void *
nvkm_vram_dtor(struct nvkm_memory *memory)
{
	struct nvkm_vram *vram = nvkm_vram(memory);
	struct nvkm_mm_node *next = vram->mn;
	struct nvkm_mm_node *node;
	mutex_lock(&vram->ram->fb->subdev.mutex);
	while ((node = next)) {
		next = node->next;
		nvkm_mm_free(&vram->ram->vram, &node);
	}
	mutex_unlock(&vram->ram->fb->subdev.mutex);
	return vram;
}

static const struct nvkm_memory_func
nvkm_vram = {
	.dtor = nvkm_vram_dtor,
	.target = nvkm_vram_target,
	.page = nvkm_vram_page,
	.addr = nvkm_vram_addr,
	.size = nvkm_vram_size,
	.map = nvkm_vram_map,
};

int
nvkm_ram_get(struct nvkm_device *device, u8 heap, u8 type, u8 rpage, u64 size,
	     bool contig, bool back, struct nvkm_memory **pmemory)
{
	struct nvkm_ram *ram;
	struct nvkm_mm *mm;
	struct nvkm_mm_node **node, *r;
	struct nvkm_vram *vram;
	u8   page = max(rpage, (u8)NVKM_RAM_MM_SHIFT);
	u32 align = (1 << page) >> NVKM_RAM_MM_SHIFT;
	u32   max = ALIGN(size, 1 << page) >> NVKM_RAM_MM_SHIFT;
	u32   min = contig ? max : align;
	int ret;

	if (!device->fb || !(ram = device->fb->ram))
		return -ENODEV;
	ram = device->fb->ram;
	mm = &ram->vram;

	if (!(vram = kzalloc(sizeof(*vram), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_memory_ctor(&nvkm_vram, &vram->memory);
	vram->ram = ram;
	vram->page = page;
	*pmemory = &vram->memory;

	mutex_lock(&ram->fb->subdev.mutex);
	node = &vram->mn;
	do {
		if (back)
			ret = nvkm_mm_tail(mm, heap, type, max, min, align, &r);
		else
			ret = nvkm_mm_head(mm, heap, type, max, min, align, &r);
		if (ret) {
			mutex_unlock(&ram->fb->subdev.mutex);
			nvkm_memory_unref(pmemory);
			return ret;
		}

		*node = r;
		node = &r->next;
		max -= r->length;
	} while (max);
	mutex_unlock(&ram->fb->subdev.mutex);
	return 0;
}

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
		nvkm_mm_fini(&ram->vram);
		kfree(*pram);
		*pram = NULL;
	}
}

int
nvkm_ram_ctor(const struct nvkm_ram_func *func, struct nvkm_fb *fb,
	      enum nvkm_ram_type type, u64 size, struct nvkm_ram *ram)
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
		[NVKM_RAM_TYPE_GDDR5X ] = "GDDR5X",
		[NVKM_RAM_TYPE_GDDR6  ] = "GDDR6",
		[NVKM_RAM_TYPE_HBM2   ] = "HBM2",
	};
	struct nvkm_subdev *subdev = &fb->subdev;
	int ret;

	nvkm_info(subdev, "%d MiB %s\n", (int)(size >> 20), name[type]);
	ram->func = func;
	ram->fb = fb;
	ram->type = type;
	ram->size = size;

	if (!nvkm_mm_initialised(&ram->vram)) {
		ret = nvkm_mm_init(&ram->vram, NVKM_RAM_MM_NORMAL, 0,
				   size >> NVKM_RAM_MM_SHIFT, 1);
		if (ret)
			return ret;
	}

	return 0;
}

int
nvkm_ram_new_(const struct nvkm_ram_func *func, struct nvkm_fb *fb,
	      enum nvkm_ram_type type, u64 size, struct nvkm_ram **pram)
{
	if (!(*pram = kzalloc(sizeof(**pram), GFP_KERNEL)))
		return -ENOMEM;
	return nvkm_ram_ctor(func, fb, type, size, *pram);
}
