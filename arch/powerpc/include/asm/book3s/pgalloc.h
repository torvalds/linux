#ifndef _ASM_POWERPC_BOOK3S_PGALLOC_H
#define _ASM_POWERPC_BOOK3S_PGALLOC_H

#include <linux/mm.h>

extern void tlb_remove_table(struct mmu_gather *tlb, void *table);

#ifdef CONFIG_PPC64
#include <asm/book3s/64/pgalloc.h>
#else
#include <asm/book3s/32/pgalloc.h>
#endif

#endif /* _ASM_POWERPC_BOOK3S_PGALLOC_H */
