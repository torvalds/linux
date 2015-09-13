#ifndef __NV04_MMU_PRIV__
#define __NV04_MMU_PRIV__
#define nv04_mmu(p) container_of((p), struct nv04_mmu, base)
#include "priv.h"

struct nv04_mmu {
	struct nvkm_mmu base;
	struct nvkm_vm *vm;
	dma_addr_t null;
	void *nullp;
};

int nv04_mmu_new_(const struct nvkm_mmu_func *, struct nvkm_device *,
		  int index, struct nvkm_mmu **);
void *nv04_mmu_dtor(struct nvkm_mmu *);

extern const struct nvkm_mmu_func nv04_mmu;
#endif
