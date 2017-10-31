#ifndef __NVKM_MMU_H__
#define __NVKM_MMU_H__
#include <core/subdev.h>
#include <core/mm.h>
struct nvkm_gpuobj;
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
	struct nvkm_vm *vm;
	struct nvkm_mm_node *node;
	union {
		u64 offset;
		u64 addr;
	};
	u32 access;
};

struct nvkm_vm {
	const struct nvkm_vmm_func *func;
	struct nvkm_mmu *mmu;
	const char *name;
	struct kref kref;
	struct mutex mutex;

	u64 start;
	u64 limit;

	struct nvkm_vmm_pt *pd;
	u16 pd_offset;
	struct list_head join;

	struct nvkm_mm mm;
	struct kref refcount;
	struct list_head pgd_list;
	struct nvkm_vm_pgt *pgt;
	u32 fpde;
	u32 lpde;

	bool bootstrapped;
	atomic_t engref[NVKM_SUBDEV_NR];
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

	struct nvkm_vmm *vmm;

	struct {
		struct mutex mutex;
		struct list_head list;
	} ptc, ptp;
};

int nv04_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int nv41_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int nv44_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int nv50_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int g84_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int gf100_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int gk104_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int gk20a_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int gm200_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int gm20b_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int gp100_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
int gp10b_mmu_new(struct nvkm_device *, int, struct nvkm_mmu **);
#endif
