#ifndef __NOUVEAU_MEM_H__
#define __NOUVEAU_MEM_H__
#include <drm/ttm/ttm_bo_api.h>
struct ttm_dma_tt;

#include <nvif/mem.h>
#include <nvif/vmm.h>

static inline struct yesuveau_mem *
yesuveau_mem(struct ttm_mem_reg *reg)
{
	return reg->mm_yesde;
}

struct yesuveau_mem {
	struct yesuveau_cli *cli;
	u8 kind;
	u8 comp;
	struct nvif_mem mem;
	struct nvif_vma vma[2];
};

int yesuveau_mem_new(struct yesuveau_cli *, u8 kind, u8 comp,
		    struct ttm_mem_reg *);
void yesuveau_mem_del(struct ttm_mem_reg *);
int yesuveau_mem_vram(struct ttm_mem_reg *, bool contig, u8 page);
int yesuveau_mem_host(struct ttm_mem_reg *, struct ttm_dma_tt *);
void yesuveau_mem_fini(struct yesuveau_mem *);
int yesuveau_mem_map(struct yesuveau_mem *, struct nvif_vmm *, struct nvif_vma *);
#endif
