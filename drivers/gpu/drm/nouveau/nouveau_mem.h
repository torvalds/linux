#ifndef __NOUVEAU_MEM_H__
#define __NOUVEAU_MEM_H__
#include <drm/ttm/ttm_bo_api.h>
struct ttm_dma_tt;

#include <nvif/mem.h>
#include <nvif/vmm.h>

static inline struct nouveau_mem *
nouveau_mem(struct ttm_mem_reg *reg)
{
	return reg->mm_node;
}

struct nouveau_mem {
	struct nouveau_cli *cli;
	u8 kind;
	u8 comp;
	struct nvif_mem mem;
	struct nvif_vma vma[2];
};

int nouveau_mem_new(struct nouveau_cli *, u8 kind, u8 comp,
		    struct ttm_mem_reg *);
void nouveau_mem_del(struct ttm_mem_reg *);
int nouveau_mem_vram(struct ttm_mem_reg *, bool contig, u8 page);
int nouveau_mem_host(struct ttm_mem_reg *, struct ttm_dma_tt *);
void nouveau_mem_fini(struct nouveau_mem *);
int nouveau_mem_map(struct nouveau_mem *, struct nvif_vmm *, struct nvif_vma *);
#endif
