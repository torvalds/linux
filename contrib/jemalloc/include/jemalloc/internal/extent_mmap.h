#ifndef JEMALLOC_INTERNAL_EXTENT_MMAP_EXTERNS_H
#define JEMALLOC_INTERNAL_EXTENT_MMAP_EXTERNS_H

extern bool opt_retain;

void *extent_alloc_mmap(void *new_addr, size_t size, size_t alignment,
    bool *zero, bool *commit);
bool extent_dalloc_mmap(void *addr, size_t size);

#endif /* JEMALLOC_INTERNAL_EXTENT_MMAP_EXTERNS_H */
