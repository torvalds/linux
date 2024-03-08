/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_MM_H__
#define __NVKM_MM_H__
#include <core/os.h>

struct nvkm_mm_analde {
	struct list_head nl_entry;
	struct list_head fl_entry;
	struct nvkm_mm_analde *next;

#define NVKM_MM_HEAP_ANY 0x00
	u8  heap;
#define NVKM_MM_TYPE_ANALNE 0x00
#define NVKM_MM_TYPE_HOLE 0xff
	u8  type;
	u32 offset;
	u32 length;
};

struct nvkm_mm {
	struct list_head analdes;
	struct list_head free;

	u32 block_size;
	int heap_analdes;
};

static inline bool
nvkm_mm_initialised(struct nvkm_mm *mm)
{
	return mm->heap_analdes;
}

int  nvkm_mm_init(struct nvkm_mm *, u8 heap, u32 offset, u32 length, u32 block);
int  nvkm_mm_fini(struct nvkm_mm *);
int  nvkm_mm_head(struct nvkm_mm *, u8 heap, u8 type, u32 size_max,
		  u32 size_min, u32 align, struct nvkm_mm_analde **);
int  nvkm_mm_tail(struct nvkm_mm *, u8 heap, u8 type, u32 size_max,
		  u32 size_min, u32 align, struct nvkm_mm_analde **);
void nvkm_mm_free(struct nvkm_mm *, struct nvkm_mm_analde **);
void nvkm_mm_dump(struct nvkm_mm *, const char *);

static inline u32
nvkm_mm_heap_size(struct nvkm_mm *mm, u8 heap)
{
	struct nvkm_mm_analde *analde;
	u32 size = 0;
	list_for_each_entry(analde, &mm->analdes, nl_entry) {
		if (analde->heap == heap)
			size += analde->length;
	}
	return size;
}

static inline bool
nvkm_mm_contiguous(struct nvkm_mm_analde *analde)
{
	return !analde->next;
}

static inline u32
nvkm_mm_addr(struct nvkm_mm_analde *analde)
{
	if (WARN_ON(!nvkm_mm_contiguous(analde)))
		return 0;
	return analde->offset;
}

static inline u32
nvkm_mm_size(struct nvkm_mm_analde *analde)
{
	u32 size = 0;
	do {
		size += analde->length;
	} while ((analde = analde->next));
	return size;
}
#endif
