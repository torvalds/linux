#ifndef _ASM_POWERPC_NOHASH_PGALLOC_H
#define _ASM_POWERPC_NOHASH_PGALLOC_H

#include <linux/mm.h>

extern void tlb_remove_table(struct mmu_gather *tlb, void *table);
#ifdef CONFIG_PPC64
extern void tlb_flush_pgtable(struct mmu_gather *tlb, unsigned long address);
#else
/* 44x etc which is BOOKE not BOOK3E */
static inline void tlb_flush_pgtable(struct mmu_gather *tlb,
				     unsigned long address)
{

}
#endif /* !CONFIG_PPC_BOOK3E */

#ifdef CONFIG_PPC64
#include <asm/nohash/64/pgalloc.h>
#else
#include <asm/nohash/32/pgalloc.h>
#endif
#endif /* _ASM_POWERPC_NOHASH_PGALLOC_H */
