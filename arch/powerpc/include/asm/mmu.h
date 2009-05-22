#ifndef _ASM_POWERPC_MMU_H_
#define _ASM_POWERPC_MMU_H_
#ifdef __KERNEL__

#include <asm/asm-compat.h>
#include <asm/feature-fixups.h>

/*
 * MMU features bit definitions
 */

/*
 * First half is MMU families
 */
#define MMU_FTR_HPTE_TABLE		ASM_CONST(0x00000001)
#define MMU_FTR_TYPE_8xx		ASM_CONST(0x00000002)
#define MMU_FTR_TYPE_40x		ASM_CONST(0x00000004)
#define MMU_FTR_TYPE_44x		ASM_CONST(0x00000008)
#define MMU_FTR_TYPE_FSL_E		ASM_CONST(0x00000010)

/*
 * This is individual features
 */

/* Enable use of high BAT registers */
#define MMU_FTR_USE_HIGH_BATS		ASM_CONST(0x00010000)

/* Enable >32-bit physical addresses on 32-bit processor, only used
 * by CONFIG_6xx currently as BookE supports that from day 1
 */
#define MMU_FTR_BIG_PHYS		ASM_CONST(0x00020000)

/* Enable use of broadcast TLB invalidations. We don't always set it
 * on processors that support it due to other constraints with the
 * use of such invalidations
 */
#define MMU_FTR_USE_TLBIVAX_BCAST	ASM_CONST(0x00040000)

/* Enable use of tlbilx invalidate instructions.
 */
#define MMU_FTR_USE_TLBILX		ASM_CONST(0x00080000)

/* This indicates that the processor cannot handle multiple outstanding
 * broadcast tlbivax or tlbsync. This makes the code use a spinlock
 * around such invalidate forms.
 */
#define MMU_FTR_LOCK_BCAST_INVAL	ASM_CONST(0x00100000)

/* This indicates that the processor doesn't handle way selection
 * properly and needs SW to track and update the LRU state.  This
 * is specific to an errata on e300c2/c3/c4 class parts
 */
#define MMU_FTR_NEED_DTLB_SW_LRU	ASM_CONST(0x00200000)

#ifndef __ASSEMBLY__
#include <asm/cputable.h>

static inline int mmu_has_feature(unsigned long feature)
{
	return (cur_cpu_spec->mmu_features & feature);
}

extern unsigned int __start___mmu_ftr_fixup, __stop___mmu_ftr_fixup;

/* MMU initialization (64-bit only fo now) */
extern void early_init_mmu(void);
extern void early_init_mmu_secondary(void);

#endif /* !__ASSEMBLY__ */


#ifdef CONFIG_PPC64
/* 64-bit classic hash table MMU */
#  include <asm/mmu-hash64.h>
#elif defined(CONFIG_PPC_STD_MMU)
/* 32-bit classic hash table MMU */
#  include <asm/mmu-hash32.h>
#elif defined(CONFIG_40x)
/* 40x-style software loaded TLB */
#  include <asm/mmu-40x.h>
#elif defined(CONFIG_44x)
/* 44x-style software loaded TLB */
#  include <asm/mmu-44x.h>
#elif defined(CONFIG_PPC_BOOK3E_MMU)
/* Freescale Book-E software loaded TLB or Book-3e (ISA 2.06+) MMU */
#  include <asm/mmu-book3e.h>
#elif defined (CONFIG_PPC_8xx)
/* Motorola/Freescale 8xx software loaded TLB */
#  include <asm/mmu-8xx.h>
#endif

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_MMU_H_ */
