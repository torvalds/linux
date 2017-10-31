#ifndef __NOUVEAU_MEM_H__
#define __NOUVEAU_MEM_H__
#include <subdev/fb.h>

#include <drm/ttm/ttm_bo_api.h>
struct ttm_dma_tt;

static inline struct nouveau_mem *
nouveau_mem(struct ttm_mem_reg *reg)
{
	return reg->mm_node;
}

struct nouveau_mem {
	struct nouveau_cli *cli;
	u8 kind;
	u8 comp;
	struct {
		u8 page;
	} mem;
	struct nvkm_vma vma[2];

	struct nvkm_mem __mem;
	struct nvkm_mem *_mem;
	struct nvkm_vma bar_vma;

	struct nvkm_memory memory;
	struct nvkm_tags *tags;
};

enum nvif_vmm_get {
	PTES,
	LAZY,
};

int nouveau_mem_new(struct nouveau_cli *, u8 kind, u8 comp,
		    struct ttm_mem_reg *);
void nouveau_mem_del(struct ttm_mem_reg *);
int nouveau_mem_vram(struct ttm_mem_reg *, bool contig, u8 page);
int nouveau_mem_host(struct ttm_mem_reg *, struct ttm_dma_tt *);
void nouveau_mem_fini(struct nouveau_mem *);
int nouveau_mem_map(struct nouveau_mem *, struct nvkm_vmm *, struct nvkm_vma *);
#endif
