#ifndef __NV04_MMU_PRIV__
#define __NV04_MMU_PRIV__

#include <subdev/mmu.h>

struct nv04_mmu_priv {
	struct nouveau_mmu base;
	struct nouveau_vm *vm;
	dma_addr_t null;
	void *nullp;
};

static inline struct nv04_mmu_priv *
nv04_mmu(void *obj)
{
	return (void *)nouveau_mmu(obj);
}

#endif
