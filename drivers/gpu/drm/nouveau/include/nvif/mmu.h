#ifndef __NVIF_MMU_H__
#define __NVIF_MMU_H__
#include <nvif/object.h>

struct nvif_mmu {
	struct nvif_object object;
	u8  dmabits;
	u8  heap_nr;
	u8  type_nr;
	u8  kind_inv;
	u16 kind_nr;
	s32 mem;

	struct {
		u64 size;
	} *heap;

	struct {
#define NVIF_MEM_VRAM                                                      0x01
#define NVIF_MEM_HOST                                                      0x02
#define NVIF_MEM_COMP                                                      0x04
#define NVIF_MEM_DISP                                                      0x08
#define NVIF_MEM_KIND                                                      0x10
#define NVIF_MEM_MAPPABLE                                                  0x20
#define NVIF_MEM_COHERENT                                                  0x40
#define NVIF_MEM_UNCACHED                                                  0x80
		u8 type;
		u8 heap;
	} *type;

	u8 *kind;
};

int nvif_mmu_ctor(struct nvif_object *, const char *name, s32 oclass,
		  struct nvif_mmu *);
void nvif_mmu_dtor(struct nvif_mmu *);

static inline bool
nvif_mmu_kind_valid(struct nvif_mmu *mmu, u8 kind)
{
	if (kind) {
		if (kind >= mmu->kind_nr || mmu->kind[kind] == mmu->kind_inv)
			return false;
	}
	return true;
}

static inline int
nvif_mmu_type(struct nvif_mmu *mmu, u8 mask)
{
	int i;
	for (i = 0; i < mmu->type_nr; i++) {
		if ((mmu->type[i].type & mask) == mask)
			return i;
	}
	return -EINVAL;
}
#endif
