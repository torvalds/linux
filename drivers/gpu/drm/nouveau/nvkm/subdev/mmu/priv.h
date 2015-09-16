#ifndef __NVKM_MMU_PRIV_H__
#define __NVKM_MMU_PRIV_H__
#define nvkm_mmu(p) container_of((p), struct nvkm_mmu, subdev)
#include <subdev/mmu.h>

void nvkm_mmu_ctor(const struct nvkm_mmu_func *, struct nvkm_device *,
		   int index, struct nvkm_mmu *);
int nvkm_mmu_new_(const struct nvkm_mmu_func *, struct nvkm_device *,
		  int index, struct nvkm_mmu **);

struct nvkm_mmu_func {
	void *(*dtor)(struct nvkm_mmu *);
	int (*oneinit)(struct nvkm_mmu *);
	void (*init)(struct nvkm_mmu *);

	u64 limit;
	u8  dma_bits;
	u32 pgt_bits;
	u8  spg_shift;
	u8  lpg_shift;

	int  (*create)(struct nvkm_mmu *, u64 offset, u64 length, u64 mm_offset,
		       struct lock_class_key *, struct nvkm_vm **);

	void (*map_pgt)(struct nvkm_gpuobj *pgd, u32 pde,
			struct nvkm_memory *pgt[2]);
	void (*map)(struct nvkm_vma *, struct nvkm_memory *,
		    struct nvkm_mem *, u32 pte, u32 cnt,
		    u64 phys, u64 delta);
	void (*map_sg)(struct nvkm_vma *, struct nvkm_memory *,
		       struct nvkm_mem *, u32 pte, u32 cnt, dma_addr_t *);
	void (*unmap)(struct nvkm_vma *, struct nvkm_memory *pgt,
		      u32 pte, u32 cnt);
	void (*flush)(struct nvkm_vm *);
};

int nvkm_vm_create(struct nvkm_mmu *, u64, u64, u64, u32,
		   struct lock_class_key *, struct nvkm_vm **);
#endif
