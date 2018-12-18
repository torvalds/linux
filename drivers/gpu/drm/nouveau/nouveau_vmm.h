#ifndef __NOUVEAU_VMA_H__
#define __NOUVEAU_VMA_H__
#include <nvif/vmm.h>
struct nouveau_bo;
struct nouveau_mem;

struct nouveau_vma {
	struct nouveau_vmm *vmm;
	int refs;
	struct list_head head;
	u64 addr;

	struct nouveau_mem *mem;

	struct nouveau_fence *fence;
};

struct nouveau_vma *nouveau_vma_find(struct nouveau_bo *, struct nouveau_vmm *);
int nouveau_vma_new(struct nouveau_bo *, struct nouveau_vmm *,
		    struct nouveau_vma **);
void nouveau_vma_del(struct nouveau_vma **);
int nouveau_vma_map(struct nouveau_vma *, struct nouveau_mem *);
void nouveau_vma_unmap(struct nouveau_vma *);

struct nouveau_vmm {
	struct nouveau_cli *cli;
	struct nvif_vmm vmm;
	struct nvkm_vm *vm;
};

int nouveau_vmm_init(struct nouveau_cli *, s32 oclass, struct nouveau_vmm *);
void nouveau_vmm_fini(struct nouveau_vmm *);
#endif
