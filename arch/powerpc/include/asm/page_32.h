/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_PAGE_32_H
#define _ASM_POWERPC_PAGE_32_H

#include <asm/cache.h>

#if defined(CONFIG_PHYSICAL_ALIGN) && (CONFIG_PHYSICAL_START != 0)
#if (CONFIG_PHYSICAL_START % CONFIG_PHYSICAL_ALIGN) != 0
#error "CONFIG_PHYSICAL_START must be a multiple of CONFIG_PHYSICAL_ALIGN"
#endif
#endif

#define VM_DATA_DEFAULT_FLAGS	VM_DATA_DEFAULT_FLAGS32

#if defined(CONFIG_PPC_256K_PAGES) || \
    (defined(CONFIG_PPC_8xx) && defined(CONFIG_PPC_16K_PAGES))
#define PTE_SHIFT	(PAGE_SHIFT - PTE_T_LOG2 - 2)	/* 1/4 of a page */
#else
#define PTE_SHIFT	(PAGE_SHIFT - PTE_T_LOG2)	/* full page */
#endif

#ifndef __ASSEMBLY__
/*
 * The basic type of a PTE - 64 bits for those CPUs with > 32 bit
 * physical addressing.
 */
#ifdef CONFIG_PTE_64BIT
typedef unsigned long long pte_basic_t;
#else
typedef unsigned long pte_basic_t;
#endif

#include <asm/bug.h>

/*
 * Clear page using the dcbz instruction, which doesn't cause any
 * memory traffic (except to write out any cache lines which get
 * displaced).  This only works on cacheable memory.
 */
static inline void clear_page(void *addr)
{
	unsigned int i;

	WARN_ON((unsigned long)addr & (L1_CACHE_BYTES - 1));

	for (i = 0; i < PAGE_SIZE / L1_CACHE_BYTES; i++, addr += L1_CACHE_BYTES)
		dcbz(addr);
}
extern void copy_page(void *to, void *from);

#include <asm-generic/getorder.h>

#define PGD_T_LOG2	(__builtin_ffs(sizeof(pgd_t)) - 1)
#define PTE_T_LOG2	(__builtin_ffs(sizeof(pte_t)) - 1)

#endif /* __ASSEMBLY__ */

#endif /* _ASM_POWERPC_PAGE_32_H */
