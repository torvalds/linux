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
#include "vmm.h"

#include <nvif/if500d.h>
#include <nvif/unpack.h>

static const struct nvkm_vmm_desc_func
nv50_vmm_pgt = {
};

static const struct nvkm_vmm_desc_func
nv50_vmm_pgd = {
};

static const struct nvkm_vmm_desc
nv50_vmm_desc_12[] = {
	{ PGT, 17, 8, 0x1000, &nv50_vmm_pgt },
	{ PGD, 11, 0, 0x0000, &nv50_vmm_pgd },
	{}
};

static const struct nvkm_vmm_desc
nv50_vmm_desc_16[] = {
	{ PGT, 13, 8, 0x1000, &nv50_vmm_pgt },
	{ PGD, 11, 0, 0x0000, &nv50_vmm_pgd },
	{}
};

static void
nv50_vmm_part(struct nvkm_vmm *vmm, struct nvkm_memory *inst)
{
	struct nvkm_vmm_join *join;

	list_for_each_entry(join, &vmm->join, head) {
		if (join->inst == inst) {
			list_del(&join->head);
			kfree(join);
			break;
		}
	}
}

static int
nv50_vmm_join(struct nvkm_vmm *vmm, struct nvkm_memory *inst)
{
	struct nvkm_vmm_join *join;

	if (!(join = kmalloc(sizeof(*join), GFP_KERNEL)))
		return -ENOMEM;
	join->inst = inst;
	list_add_tail(&join->head, &vmm->join);
	return 0;
}

static const struct nvkm_vmm_func
nv50_vmm = {
	.join = nv50_vmm_join,
	.part = nv50_vmm_part,
	.page_block = 1 << 29,
	.page = {
		{ 16, &nv50_vmm_desc_16[0], NVKM_VMM_PAGE_xVxC },
		{ 12, &nv50_vmm_desc_12[0], NVKM_VMM_PAGE_xVHx },
		{}
	}
};

int
nv50_vmm_new(struct nvkm_mmu *mmu, u64 addr, u64 size, void *argv, u32 argc,
	     struct lock_class_key *key, const char *name,
	     struct nvkm_vmm **pvmm)
{
	return nv04_vmm_new_(&nv50_vmm, mmu, 0, addr, size,
			     argv, argc, key, name, pvmm);
}
