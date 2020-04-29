/*
 * Copyright 2017 Red Hat Inc.
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
#include <nvif/mem.h>
#include <nvif/client.h>

#include <nvif/if000a.h>

int
nvif_mem_init_map(struct nvif_mmu *mmu, u8 type, u64 size, struct nvif_mem *mem)
{
	int ret = nvif_mem_init(mmu, mmu->mem, NVIF_MEM_MAPPABLE | type, 0,
				size, NULL, 0, mem);
	if (ret == 0) {
		ret = nvif_object_map(&mem->object, NULL, 0);
		if (ret)
			nvif_mem_fini(mem);
	}
	return ret;
}

void
nvif_mem_fini(struct nvif_mem *mem)
{
	nvif_object_fini(&mem->object);
}

int
nvif_mem_init_type(struct nvif_mmu *mmu, s32 oclass, int type, u8 page,
		   u64 size, void *argv, u32 argc, struct nvif_mem *mem)
{
	struct nvif_mem_v0 *args;
	u8 stack[128];
	int ret;

	mem->object.client = NULL;
	if (type < 0)
		return -EINVAL;

	if (sizeof(*args) + argc > sizeof(stack)) {
		if (!(args = kmalloc(sizeof(*args) + argc, GFP_KERNEL)))
			return -ENOMEM;
	} else {
		args = (void *)stack;
	}
	args->version = 0;
	args->type = type;
	args->page = page;
	args->size = size;
	memcpy(args->data, argv, argc);

	ret = nvif_object_init(&mmu->object, 0, oclass, args,
			       sizeof(*args) + argc, &mem->object);
	if (ret == 0) {
		mem->type = mmu->type[type].type;
		mem->page = args->page;
		mem->addr = args->addr;
		mem->size = args->size;
	}

	if (args != (void *)stack)
		kfree(args);
	return ret;

}

int
nvif_mem_init(struct nvif_mmu *mmu, s32 oclass, u8 type, u8 page,
	      u64 size, void *argv, u32 argc, struct nvif_mem *mem)
{
	int ret = -EINVAL, i;

	mem->object.client = NULL;

	for (i = 0; ret && i < mmu->type_nr; i++) {
		if ((mmu->type[i].type & type) == type) {
			ret = nvif_mem_init_type(mmu, oclass, i, page, size,
						 argv, argc, mem);
		}
	}

	return ret;
}
