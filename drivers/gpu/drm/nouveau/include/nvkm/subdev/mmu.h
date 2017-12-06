/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NVKM_MMU_H__
#define __NVKM_MMU_H__
#include <core/subdev.h>
#include <core/mm.h>
struct nvkm_device;
struct nvkm_mem;

struct nvkm_vm_pgt {
	struct nvkm_memory *mem[2];
	u32 refcount[2];
};

struct nvkm_vm_pgd {
	struct list_head head;
	struct nvkm_gpuobj *obj;
};

struct nvkm_vma {
	struct list_head head;
	int refcount;
	struct nvkm_vm *vm;
	struct nvkm_mm_node *node;
	u64 offset;
	u32 access;
};

struct nvkm_vm {
	struct nvkm_mmu *mmu;

	struct mutex mutex;
	struct nvkm_mm mm;
	struct kref refcount;

	struct list_head pgd_list;
	atomic_t engref[NVKM_SUBDEV_NR];

	struct nvkm_vm_pgt *pgt;
	u32 fpde;
	u32 lpde;
};

int  nvkm_vm_new(struct nvkm_device *, u64 offset, u64 length, u64 mm_offset,
		 struct lock_class_key *, struct nvkm_vm **);
int  nvkm_vm_ref(struct nvkm_vm *, struct nvkm_vm **, struct nvkm_gpuobj *pgd);
int  nvkm_vm_boot(struct nvkm_vm *, u64 size);
int  nvkm_vm_get(struct nvkm_vm *, u64 size, u32 page_shift, u32 access,
		 struct nvkm_vma *);
void nvkm_vm_put(struct nvkm_vma *);
void nvkm_vm_map(struct nvkm_vma *, struct nvkm_mem *);
void nvkm_vm_map_at(struct nvkm_vma *, u64 offset, struct nvkm_mem *);
void nvkm_vm_unmap(struct nvkm_vma *);
void nvkm_vm_unmap_at(struct nvkm_vma *, u64 offset, u64 length);

struct nvkm_mmu {
	const struct nvkm_mmu_func *func;
	struct nvkm_subdev subdev;

	u64 limit;
	u8  dma_bits;
	u8  lpg_shift;
};

int nv04_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int nv41_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int nv44_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int nv50_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int gf100_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
#endif
