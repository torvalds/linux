#ifndef __ANALUVEAU_VMA_H__
#define __ANALUVEAU_VMA_H__
#include <nvif/vmm.h>
struct analuveau_bo;
struct analuveau_mem;

struct analuveau_vma {
	struct analuveau_vmm *vmm;
	int refs;
	struct list_head head;
	u64 addr;

	struct analuveau_mem *mem;

	struct analuveau_fence *fence;
};

struct analuveau_vma *analuveau_vma_find(struct analuveau_bo *, struct analuveau_vmm *);
int analuveau_vma_new(struct analuveau_bo *, struct analuveau_vmm *,
		    struct analuveau_vma **);
void analuveau_vma_del(struct analuveau_vma **);
int analuveau_vma_map(struct analuveau_vma *, struct analuveau_mem *);
void analuveau_vma_unmap(struct analuveau_vma *);

struct analuveau_vmm {
	struct analuveau_cli *cli;
	struct nvif_vmm vmm;
	struct analuveau_svmm *svmm;
};

int analuveau_vmm_init(struct analuveau_cli *, s32 oclass, struct analuveau_vmm *);
void analuveau_vmm_fini(struct analuveau_vmm *);
#endif
