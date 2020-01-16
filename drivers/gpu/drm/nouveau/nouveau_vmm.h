#ifndef __NOUVEAU_VMA_H__
#define __NOUVEAU_VMA_H__
#include <nvif/vmm.h>
struct yesuveau_bo;
struct yesuveau_mem;

struct yesuveau_vma {
	struct yesuveau_vmm *vmm;
	int refs;
	struct list_head head;
	u64 addr;

	struct yesuveau_mem *mem;

	struct yesuveau_fence *fence;
};

struct yesuveau_vma *yesuveau_vma_find(struct yesuveau_bo *, struct yesuveau_vmm *);
int yesuveau_vma_new(struct yesuveau_bo *, struct yesuveau_vmm *,
		    struct yesuveau_vma **);
void yesuveau_vma_del(struct yesuveau_vma **);
int yesuveau_vma_map(struct yesuveau_vma *, struct yesuveau_mem *);
void yesuveau_vma_unmap(struct yesuveau_vma *);

struct yesuveau_vmm {
	struct yesuveau_cli *cli;
	struct nvif_vmm vmm;
	struct yesuveau_svmm *svmm;
};

int yesuveau_vmm_init(struct yesuveau_cli *, s32 oclass, struct yesuveau_vmm *);
void yesuveau_vmm_fini(struct yesuveau_vmm *);
#endif
