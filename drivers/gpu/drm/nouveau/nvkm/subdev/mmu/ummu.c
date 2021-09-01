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
#include "ummu.h"
#include "umem.h"
#include "uvmm.h"

#include <core/client.h>

#include <nvif/if0008.h>
#include <nvif/unpack.h>

static int
nvkm_ummu_sclass(struct nvkm_object *object, int index,
		 struct nvkm_oclass *oclass)
{
	struct nvkm_mmu *mmu = nvkm_ummu(object)->mmu;

	if (mmu->func->mem.user.oclass) {
		if (index-- == 0) {
			oclass->base = mmu->func->mem.user;
			oclass->ctor = nvkm_umem_new;
			return 0;
		}
	}

	if (mmu->func->vmm.user.oclass) {
		if (index-- == 0) {
			oclass->base = mmu->func->vmm.user;
			oclass->ctor = nvkm_uvmm_new;
			return 0;
		}
	}

	return -EINVAL;
}

static int
nvkm_ummu_heap(struct nvkm_ummu *ummu, void *argv, u32 argc)
{
	struct nvkm_mmu *mmu = ummu->mmu;
	union {
		struct nvif_mmu_heap_v0 v0;
	} *args = argv;
	int ret = -ENOSYS;
	u8 index;

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		if ((index = args->v0.index) >= mmu->heap_nr)
			return -EINVAL;
		args->v0.size = mmu->heap[index].size;
	} else
		return ret;

	return 0;
}

static int
nvkm_ummu_type(struct nvkm_ummu *ummu, void *argv, u32 argc)
{
	struct nvkm_mmu *mmu = ummu->mmu;
	union {
		struct nvif_mmu_type_v0 v0;
	} *args = argv;
	int ret = -ENOSYS;
	u8 type, index;

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		if ((index = args->v0.index) >= mmu->type_nr)
			return -EINVAL;
		type = mmu->type[index].type;
		args->v0.heap = mmu->type[index].heap;
		args->v0.vram = !!(type & NVKM_MEM_VRAM);
		args->v0.host = !!(type & NVKM_MEM_HOST);
		args->v0.comp = !!(type & NVKM_MEM_COMP);
		args->v0.disp = !!(type & NVKM_MEM_DISP);
		args->v0.kind = !!(type & NVKM_MEM_KIND);
		args->v0.mappable = !!(type & NVKM_MEM_MAPPABLE);
		args->v0.coherent = !!(type & NVKM_MEM_COHERENT);
		args->v0.uncached = !!(type & NVKM_MEM_UNCACHED);
	} else
		return ret;

	return 0;
}

static int
nvkm_ummu_kind(struct nvkm_ummu *ummu, void *argv, u32 argc)
{
	struct nvkm_mmu *mmu = ummu->mmu;
	union {
		struct nvif_mmu_kind_v0 v0;
	} *args = argv;
	const u8 *kind = NULL;
	int ret = -ENOSYS, count = 0;
	u8 kind_inv = 0;

	if (mmu->func->kind)
		kind = mmu->func->kind(mmu, &count, &kind_inv);

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, true))) {
		if (argc != args->v0.count * sizeof(*args->v0.data))
			return -EINVAL;
		if (args->v0.count > count)
			return -EINVAL;
		args->v0.kind_inv = kind_inv;
		memcpy(args->v0.data, kind, args->v0.count);
	} else
		return ret;

	return 0;
}

static int
nvkm_ummu_mthd(struct nvkm_object *object, u32 mthd, void *argv, u32 argc)
{
	struct nvkm_ummu *ummu = nvkm_ummu(object);
	switch (mthd) {
	case NVIF_MMU_V0_HEAP: return nvkm_ummu_heap(ummu, argv, argc);
	case NVIF_MMU_V0_TYPE: return nvkm_ummu_type(ummu, argv, argc);
	case NVIF_MMU_V0_KIND: return nvkm_ummu_kind(ummu, argv, argc);
	default:
		break;
	}
	return -EINVAL;
}

static const struct nvkm_object_func
nvkm_ummu = {
	.mthd = nvkm_ummu_mthd,
	.sclass = nvkm_ummu_sclass,
};

int
nvkm_ummu_new(struct nvkm_device *device, const struct nvkm_oclass *oclass,
	      void *argv, u32 argc, struct nvkm_object **pobject)
{
	union {
		struct nvif_mmu_v0 v0;
	} *args = argv;
	struct nvkm_mmu *mmu = device->mmu;
	struct nvkm_ummu *ummu;
	int ret = -ENOSYS, kinds = 0;
	u8 unused = 0;

	if (mmu->func->kind)
		mmu->func->kind(mmu, &kinds, &unused);

	if (!(ret = nvif_unpack(ret, &argv, &argc, args->v0, 0, 0, false))) {
		args->v0.dmabits = mmu->dma_bits;
		args->v0.heap_nr = mmu->heap_nr;
		args->v0.type_nr = mmu->type_nr;
		args->v0.kind_nr = kinds;
	} else
		return ret;

	if (!(ummu = kzalloc(sizeof(*ummu), GFP_KERNEL)))
		return -ENOMEM;
	nvkm_object_ctor(&nvkm_ummu, oclass, &ummu->object);
	ummu->mmu = mmu;
	*pobject = &ummu->object;
	return 0;
}
