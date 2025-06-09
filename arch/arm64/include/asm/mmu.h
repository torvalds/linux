/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_MMU_H
#define __ASM_MMU_H

#include <asm/cputype.h>

#define MMCF_AARCH32	0x1	/* mm context flag for AArch32 executables */
#define USER_ASID_BIT	48
#define USER_ASID_FLAG	(UL(1) << USER_ASID_BIT)
#define TTBR_ASID_MASK	(UL(0xffff) << 48)

#ifndef __ASSEMBLY__

#include <linux/refcount.h>
#include <asm/cpufeature.h>

typedef struct {
	atomic64_t	id;
#ifdef CONFIG_COMPAT
	void		*sigpage;
#endif
	refcount_t	pinned;
	void		*vdso;
	unsigned long	flags;
	u8		pkey_allocation_map;
} mm_context_t;

/*
 * We use atomic64_read() here because the ASID for an 'mm_struct' can
 * be reallocated when scheduling one of its threads following a
 * rollover event (see new_context() and flush_context()). In this case,
 * a concurrent TLBI (e.g. via try_to_unmap_one() and ptep_clear_flush())
 * may use a stale ASID. This is fine in principle as the new ASID is
 * guaranteed to be clean in the TLB, but the TLBI routines have to take
 * care to handle the following race:
 *
 *    CPU 0                    CPU 1                          CPU 2
 *
 *    // ptep_clear_flush(mm)
 *    xchg_relaxed(pte, 0)
 *    DSB ISHST
 *    old = ASID(mm)
 *         |                                                  <rollover>
 *         |                   new = new_context(mm)
 *         \-----------------> atomic_set(mm->context.id, new)
 *                             cpu_switch_mm(mm)
 *                             // Hardware walk of pte using new ASID
 *    TLBI(old)
 *
 * In this scenario, the barrier on CPU 0 and the dependency on CPU 1
 * ensure that the page-table walker on CPU 1 *must* see the invalid PTE
 * written by CPU 0.
 */
#define ASID(mm)	(atomic64_read(&(mm)->context.id) & 0xffff)

static inline bool arm64_kernel_unmapped_at_el0(void)
{
	return alternative_has_cap_unlikely(ARM64_UNMAP_KERNEL_AT_EL0);
}

extern void arm64_memblock_init(void);
extern void paging_init(void);
extern void bootmem_init(void);
extern void create_mapping_noalloc(phys_addr_t phys, unsigned long virt,
				   phys_addr_t size, pgprot_t prot);
extern void create_pgd_mapping(struct mm_struct *mm, phys_addr_t phys,
			       unsigned long virt, phys_addr_t size,
			       pgprot_t prot, bool page_mappings_only);
extern void *fixmap_remap_fdt(phys_addr_t dt_phys, int *size, pgprot_t prot);
extern void mark_linear_text_alias_ro(void);

/*
 * This check is triggered during the early boot before the cpufeature
 * is initialised. Checking the status on the local CPU allows the boot
 * CPU to detect the need for non-global mappings and thus avoiding a
 * pagetable re-write after all the CPUs are booted. This check will be
 * anyway run on individual CPUs, allowing us to get the consistent
 * state once the SMP CPUs are up and thus make the switch to non-global
 * mappings if required.
 */
static inline bool kaslr_requires_kpti(void)
{
	/*
	 * E0PD does a similar job to KPTI so can be used instead
	 * where available.
	 */
	if (IS_ENABLED(CONFIG_ARM64_E0PD)) {
		u64 mmfr2 = read_sysreg_s(SYS_ID_AA64MMFR2_EL1);
		if (cpuid_feature_extract_unsigned_field(mmfr2,
						ID_AA64MMFR2_EL1_E0PD_SHIFT))
			return false;
	}

	return true;
}

#endif	/* !__ASSEMBLY__ */
#endif
