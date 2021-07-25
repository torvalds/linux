#ifndef __UM_ASM_CACHEFLUSH_H
#define __UM_ASM_CACHEFLUSH_H

#include <asm/tlbflush.h>
#define flush_cache_vmap flush_tlb_kernel_range
#define flush_cache_vunmap flush_tlb_kernel_range

#include <asm-generic/cacheflush.h>
#endif /* __UM_ASM_CACHEFLUSH_H */
