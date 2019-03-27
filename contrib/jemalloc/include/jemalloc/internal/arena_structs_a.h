#ifndef JEMALLOC_INTERNAL_ARENA_STRUCTS_A_H
#define JEMALLOC_INTERNAL_ARENA_STRUCTS_A_H

#include "jemalloc/internal/bitmap.h"

struct arena_slab_data_s {
	/* Per region allocated/deallocated bitmap. */
	bitmap_t	bitmap[BITMAP_GROUPS_MAX];
};

#endif /* JEMALLOC_INTERNAL_ARENA_STRUCTS_A_H */
