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
#include <nvif/vmm.h>
#include <nvif/mem.h>

#include <nvif/if000c.h>

int
nvif_vmm_unmap(struct nvif_vmm *vmm, u64 addr)
{
	return nvif_object_mthd(&vmm->object, NVIF_VMM_V0_UNMAP,
				&(struct nvif_vmm_unmap_v0) { .addr = addr },
				sizeof(struct nvif_vmm_unmap_v0));
}

int
nvif_vmm_map(struct nvif_vmm *vmm, u64 addr, u64 size, void *argv, u32 argc,
	     struct nvif_mem *mem, u64 offset)
{
	struct nvif_vmm_map_v0 *args;
	u8 stack[48];
	int ret;

	if (sizeof(*args) + argc > sizeof(stack)) {
		if (!(args = kmalloc(sizeof(*args) + argc, GFP_KERNEL)))
			return -ENOMEM;
	} else {
		args = (void *)stack;
	}

	args->version = 0;
	args->addr = addr;
	args->size = size;
	args->memory = nvif_handle(&mem->object);
	args->offset = offset;
	memcpy(args->data, argv, argc);

	ret = nvif_object_mthd(&vmm->object, NVIF_VMM_V0_MAP,
			       args, sizeof(*args) + argc);
	if (args != (void *)stack)
		kfree(args);
	return ret;
}

void
nvif_vmm_put(struct nvif_vmm *vmm, struct nvif_vma *vma)
{
	if (vma->size) {
		WARN_ON(nvif_object_mthd(&vmm->object, NVIF_VMM_V0_PUT,
					 &(struct nvif_vmm_put_v0) {
						.addr = vma->addr,
					 }, sizeof(struct nvif_vmm_put_v0)));
		vma->size = 0;
	}
}

int
nvif_vmm_get(struct nvif_vmm *vmm, enum nvif_vmm_get type, bool sparse,
	     u8 page, u8 align, u64 size, struct nvif_vma *vma)
{
	struct nvif_vmm_get_v0 args;
	int ret;

	args.version = vma->size = 0;
	args.sparse = sparse;
	args.page = page;
	args.align = align;
	args.size = size;

	switch (type) {
	case ADDR: args.type = NVIF_VMM_GET_V0_ADDR; break;
	case PTES: args.type = NVIF_VMM_GET_V0_PTES; break;
	case LAZY: args.type = NVIF_VMM_GET_V0_LAZY; break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	ret = nvif_object_mthd(&vmm->object, NVIF_VMM_V0_GET,
			       &args, sizeof(args));
	if (ret == 0) {
		vma->addr = args.addr;
		vma->size = args.size;
	}
	return ret;
}

void
nvif_vmm_dtor(struct nvif_vmm *vmm)
{
	kfree(vmm->page);
	nvif_object_dtor(&vmm->object);
}

int
nvif_vmm_ctor(struct nvif_mmu *mmu, const char *name, s32 oclass, bool managed,
	      u64 addr, u64 size, void *argv, u32 argc, struct nvif_vmm *vmm)
{
	struct nvif_vmm_v0 *args;
	u32 argn = sizeof(*args) + argc;
	int ret = -ENOSYS, i;

	vmm->object.client = NULL;
	vmm->page = NULL;

	if (!(args = kmalloc(argn, GFP_KERNEL)))
		return -ENOMEM;
	args->version = 0;
	args->managed = managed;
	args->addr = addr;
	args->size = size;
	memcpy(args->data, argv, argc);

	ret = nvif_object_ctor(&mmu->object, name ? name : "nvifVmm", 0,
			       oclass, args, argn, &vmm->object);
	if (ret)
		goto done;

	vmm->start = args->addr;
	vmm->limit = args->size;

	vmm->page_nr = args->page_nr;
	vmm->page = kmalloc_array(vmm->page_nr, sizeof(*vmm->page),
				  GFP_KERNEL);
	if (!vmm->page) {
		ret = -ENOMEM;
		goto done;
	}

	for (i = 0; i < vmm->page_nr; i++) {
		struct nvif_vmm_page_v0 args = { .index = i };

		ret = nvif_object_mthd(&vmm->object, NVIF_VMM_V0_PAGE,
				       &args, sizeof(args));
		if (ret)
			break;

		vmm->page[i].shift = args.shift;
		vmm->page[i].sparse = args.sparse;
		vmm->page[i].vram = args.vram;
		vmm->page[i].host = args.host;
		vmm->page[i].comp = args.comp;
	}

done:
	if (ret)
		nvif_vmm_dtor(vmm);
	kfree(args);
	return ret;
}
