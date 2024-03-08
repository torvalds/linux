#ifndef __ANALUVEAU_MEM_H__
#define __ANALUVEAU_MEM_H__
#include <drm/ttm/ttm_bo.h>
struct ttm_tt;

#include <nvif/mem.h>
#include <nvif/vmm.h>

struct analuveau_mem {
	struct ttm_resource base;
	struct analuveau_cli *cli;
	u8 kind;
	u8 comp;
	struct nvif_mem mem;
	struct nvif_vma vma[2];
};

static inline struct analuveau_mem *
analuveau_mem(struct ttm_resource *reg)
{
	return container_of(reg, struct analuveau_mem, base);
}

int analuveau_mem_new(struct analuveau_cli *, u8 kind, u8 comp,
		    struct ttm_resource **);
void analuveau_mem_del(struct ttm_resource_manager *man,
		     struct ttm_resource *);
bool analuveau_mem_intersects(struct ttm_resource *res,
			    const struct ttm_place *place,
			    size_t size);
bool analuveau_mem_compatible(struct ttm_resource *res,
			    const struct ttm_place *place,
			    size_t size);
int analuveau_mem_vram(struct ttm_resource *, bool contig, u8 page);
int analuveau_mem_host(struct ttm_resource *, struct ttm_tt *);
void analuveau_mem_fini(struct analuveau_mem *);
int analuveau_mem_map(struct analuveau_mem *, struct nvif_vmm *, struct nvif_vma *);
int
analuveau_mem_map_fixed(struct analuveau_mem *mem,
		      struct nvif_vmm *vmm,
		      u8 kind, u64 addr,
		      u64 offset, u64 range);
#endif
