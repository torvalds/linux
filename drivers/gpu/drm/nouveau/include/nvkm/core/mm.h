#ifndef __NVKM_MM_H__
#define __NVKM_MM_H__
#include <core/os.h>

struct nvkm_mm_node {
	struct list_head nl_entry;
	struct list_head fl_entry;
	struct list_head rl_entry;

#define NVKM_MM_HEAP_ANY 0x00
	u8  heap;
#define NVKM_MM_TYPE_NONE 0x00
#define NVKM_MM_TYPE_HOLE 0xff
	u8  type;
	u32 offset;
	u32 length;
};

struct nvkm_mm {
	struct list_head nodes;
	struct list_head free;

	u32 block_size;
	int heap_nodes;
};

static inline bool
nvkm_mm_initialised(struct nvkm_mm *mm)
{
	return mm->heap_nodes;
}

int  nvkm_mm_init(struct nvkm_mm *, u32 offset, u32 length, u32 block);
int  nvkm_mm_fini(struct nvkm_mm *);
int  nvkm_mm_head(struct nvkm_mm *, u8 heap, u8 type, u32 size_max,
		  u32 size_min, u32 align, struct nvkm_mm_node **);
int  nvkm_mm_tail(struct nvkm_mm *, u8 heap, u8 type, u32 size_max,
		  u32 size_min, u32 align, struct nvkm_mm_node **);
void nvkm_mm_free(struct nvkm_mm *, struct nvkm_mm_node **);
void nvkm_mm_dump(struct nvkm_mm *, const char *);
#endif
