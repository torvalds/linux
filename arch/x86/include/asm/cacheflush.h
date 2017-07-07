#ifndef _ASM_X86_CACHEFLUSH_H
#define _ASM_X86_CACHEFLUSH_H

/* Caches aren't brain-dead on the intel. */
#include <asm-generic/cacheflush.h>
#include <asm/special_insns.h>

void clflush_cache_range(void *addr, unsigned int size);

#define mmio_flush_range(addr, size) clflush_cache_range(addr, size)

#endif /* _ASM_X86_CACHEFLUSH_H */
