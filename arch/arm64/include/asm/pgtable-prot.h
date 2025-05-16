/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2016 ARM Ltd.
 */
#ifndef __ASM_PGTABLE_PROT_H
#define __ASM_PGTABLE_PROT_H

#include <asm/memory.h>
#include <asm/pgtable-hwdef.h>

#include <linux/const.h>

/*
 * Software defined PTE bits definition.
 */
#define PTE_WRITE		(PTE_DBM)		 /* same as DBM (51) */
#define PTE_SWP_EXCLUSIVE	(_AT(pteval_t, 1) << 2)	 /* only for swp ptes */
#define PTE_DIRTY		(_AT(pteval_t, 1) << 55)
#define PTE_SPECIAL		(_AT(pteval_t, 1) << 56)
#define PTE_DEVMAP		(_AT(pteval_t, 1) << 57)

/*
 * PTE_PRESENT_INVALID=1 & PTE_VALID=0 indicates that the pte's fields should be
 * interpreted according to the HW layout by SW but any attempted HW access to
 * the address will result in a fault. pte_present() returns true.
 */
#define PTE_PRESENT_INVALID	(PTE_NG)		 /* only when !PTE_VALID */

#ifdef CONFIG_HAVE_ARCH_USERFAULTFD_WP
#define PTE_UFFD_WP		(_AT(pteval_t, 1) << 58) /* uffd-wp tracking */
#define PTE_SWP_UFFD_WP		(_AT(pteval_t, 1) << 3)	 /* only for swp ptes */
#else
#define PTE_UFFD_WP		(_AT(pteval_t, 0))
#define PTE_SWP_UFFD_WP		(_AT(pteval_t, 0))
#endif /* CONFIG_HAVE_ARCH_USERFAULTFD_WP */

#define _PROT_DEFAULT		(PTE_TYPE_PAGE | PTE_AF | PTE_SHARED)

#define PROT_DEFAULT		(PTE_TYPE_PAGE | PTE_MAYBE_NG | PTE_MAYBE_SHARED | PTE_AF)
#define PROT_SECT_DEFAULT	(PMD_TYPE_SECT | PMD_MAYBE_NG | PMD_MAYBE_SHARED | PMD_SECT_AF)

#define PROT_DEVICE_nGnRnE	(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_WRITE | PTE_ATTRINDX(MT_DEVICE_nGnRnE))
#define PROT_DEVICE_nGnRE	(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_WRITE | PTE_ATTRINDX(MT_DEVICE_nGnRE))
#define PROT_NORMAL_NC		(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_WRITE | PTE_ATTRINDX(MT_NORMAL_NC))
#define PROT_NORMAL		(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_WRITE | PTE_ATTRINDX(MT_NORMAL))
#define PROT_NORMAL_TAGGED	(PROT_DEFAULT | PTE_PXN | PTE_UXN | PTE_WRITE | PTE_ATTRINDX(MT_NORMAL_TAGGED))

#define PROT_SECT_DEVICE_nGnRE	(PROT_SECT_DEFAULT | PMD_SECT_PXN | PMD_SECT_UXN | PMD_ATTRINDX(MT_DEVICE_nGnRE))
#define PROT_SECT_NORMAL	(PROT_SECT_DEFAULT | PMD_SECT_PXN | PMD_SECT_UXN | PTE_WRITE | PMD_ATTRINDX(MT_NORMAL))
#define PROT_SECT_NORMAL_EXEC	(PROT_SECT_DEFAULT | PMD_SECT_UXN | PMD_ATTRINDX(MT_NORMAL))

#define _PAGE_DEFAULT		(_PROT_DEFAULT | PTE_ATTRINDX(MT_NORMAL))

#define _PAGE_KERNEL		(PROT_NORMAL)
#define _PAGE_KERNEL_RO		((PROT_NORMAL & ~PTE_WRITE) | PTE_RDONLY)
#define _PAGE_KERNEL_ROX	((PROT_NORMAL & ~(PTE_WRITE | PTE_PXN)) | PTE_RDONLY)
#define _PAGE_KERNEL_EXEC	(PROT_NORMAL & ~PTE_PXN)
#define _PAGE_KERNEL_EXEC_CONT	((PROT_NORMAL & ~PTE_PXN) | PTE_CONT)

#define _PAGE_SHARED		(_PAGE_DEFAULT | PTE_USER | PTE_RDONLY | PTE_NG | PTE_PXN | PTE_UXN | PTE_WRITE)
#define _PAGE_SHARED_EXEC	(_PAGE_DEFAULT | PTE_USER | PTE_RDONLY | PTE_NG | PTE_PXN | PTE_WRITE)
#define _PAGE_READONLY		(_PAGE_DEFAULT | PTE_USER | PTE_RDONLY | PTE_NG | PTE_PXN | PTE_UXN)
#define _PAGE_READONLY_EXEC	(_PAGE_DEFAULT | PTE_USER | PTE_RDONLY | PTE_NG | PTE_PXN)
#define _PAGE_EXECONLY		(_PAGE_DEFAULT | PTE_RDONLY | PTE_NG | PTE_PXN)

#ifndef __ASSEMBLY__

#include <asm/cpufeature.h>
#include <asm/pgtable-types.h>
#include <asm/rsi.h>

extern bool arm64_use_ng_mappings;
extern unsigned long prot_ns_shared;

#define PROT_NS_SHARED		(is_realm_world() ? prot_ns_shared : 0)

#define PTE_MAYBE_NG		(arm64_use_ng_mappings ? PTE_NG : 0)
#define PMD_MAYBE_NG		(arm64_use_ng_mappings ? PMD_SECT_NG : 0)

#ifndef CONFIG_ARM64_LPA2
#define lpa2_is_enabled()	false
#define PTE_MAYBE_SHARED	PTE_SHARED
#define PMD_MAYBE_SHARED	PMD_SECT_S
#define PHYS_MASK_SHIFT		(CONFIG_ARM64_PA_BITS)
#else
static inline bool __pure lpa2_is_enabled(void)
{
	return read_tcr() & TCR_DS;
}

#define PTE_MAYBE_SHARED	(lpa2_is_enabled() ? 0 : PTE_SHARED)
#define PMD_MAYBE_SHARED	(lpa2_is_enabled() ? 0 : PMD_SECT_S)
#define PHYS_MASK_SHIFT		(lpa2_is_enabled() ? CONFIG_ARM64_PA_BITS : 48)
#endif

/*
 * Highest possible physical address supported.
 */
#define PHYS_MASK		((UL(1) << PHYS_MASK_SHIFT) - 1)

/*
 * If we have userspace only BTI we don't want to mark kernel pages
 * guarded even if the system does support BTI.
 */
#define PTE_MAYBE_GP		(system_supports_bti_kernel() ? PTE_GP : 0)

#define PAGE_KERNEL		__pgprot(_PAGE_KERNEL)
#define PAGE_KERNEL_RO		__pgprot(_PAGE_KERNEL_RO)
#define PAGE_KERNEL_ROX		__pgprot(_PAGE_KERNEL_ROX)
#define PAGE_KERNEL_EXEC	__pgprot(_PAGE_KERNEL_EXEC)
#define PAGE_KERNEL_EXEC_CONT	__pgprot(_PAGE_KERNEL_EXEC_CONT)

#define PAGE_S2_MEMATTR(attr, has_fwb)					\
	({								\
		u64 __val;						\
		if (has_fwb)						\
			__val = PTE_S2_MEMATTR(MT_S2_FWB_ ## attr);	\
		else							\
			__val = PTE_S2_MEMATTR(MT_S2_ ## attr);		\
		__val;							\
	 })

#define PAGE_NONE		__pgprot(((_PAGE_DEFAULT) & ~PTE_VALID) | PTE_PRESENT_INVALID | PTE_RDONLY | PTE_NG | PTE_PXN | PTE_UXN)
/* shared+writable pages are clean by default, hence PTE_RDONLY|PTE_WRITE */
#define PAGE_SHARED		__pgprot(_PAGE_SHARED)
#define PAGE_SHARED_EXEC	__pgprot(_PAGE_SHARED_EXEC)
#define PAGE_READONLY		__pgprot(_PAGE_READONLY)
#define PAGE_READONLY_EXEC	__pgprot(_PAGE_READONLY_EXEC)
#define PAGE_EXECONLY		__pgprot(_PAGE_EXECONLY)

#endif /* __ASSEMBLY__ */

#define pte_pi_index(pte) ( \
	((pte & BIT(PTE_PI_IDX_3)) >> (PTE_PI_IDX_3 - 3)) | \
	((pte & BIT(PTE_PI_IDX_2)) >> (PTE_PI_IDX_2 - 2)) | \
	((pte & BIT(PTE_PI_IDX_1)) >> (PTE_PI_IDX_1 - 1)) | \
	((pte & BIT(PTE_PI_IDX_0)) >> (PTE_PI_IDX_0 - 0)))

/*
 * Page types used via Permission Indirection Extension (PIE). PIE uses
 * the USER, DBM, PXN and UXN bits to to generate an index which is used
 * to look up the actual permission in PIR_ELx and PIRE0_EL1. We define
 * combinations we use on non-PIE systems with the same encoding, for
 * convenience these are listed here as comments as are the unallocated
 * encodings.
 */

/* 0: PAGE_DEFAULT                                                  */
/* 1:                                                      PTE_USER */
/* 2:                                          PTE_WRITE            */
/* 3:                                          PTE_WRITE | PTE_USER */
/* 4: PAGE_EXECONLY                  PTE_PXN                        */
/* 5: PAGE_READONLY_EXEC             PTE_PXN |             PTE_USER */
/* 6:                                PTE_PXN | PTE_WRITE            */
/* 7: PAGE_SHARED_EXEC               PTE_PXN | PTE_WRITE | PTE_USER */
/* 8: PAGE_KERNEL_ROX      PTE_UXN                                  */
/* 9: PAGE_GCS_RO          PTE_UXN |                       PTE_USER */
/* a: PAGE_KERNEL_EXEC     PTE_UXN |           PTE_WRITE            */
/* b: PAGE_GCS             PTE_UXN |           PTE_WRITE | PTE_USER */
/* c: PAGE_KERNEL_RO       PTE_UXN | PTE_PXN                        */
/* d: PAGE_READONLY        PTE_UXN | PTE_PXN |             PTE_USER */
/* e: PAGE_KERNEL          PTE_UXN | PTE_PXN | PTE_WRITE            */
/* f: PAGE_SHARED          PTE_UXN | PTE_PXN | PTE_WRITE | PTE_USER */

#define _PAGE_GCS	(_PAGE_DEFAULT | PTE_NG | PTE_UXN | PTE_WRITE | PTE_USER)
#define _PAGE_GCS_RO	(_PAGE_DEFAULT | PTE_NG | PTE_UXN | PTE_USER)

#define PAGE_GCS	__pgprot(_PAGE_GCS)
#define PAGE_GCS_RO	__pgprot(_PAGE_GCS_RO)

#define PIE_E0	( \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_GCS),           PIE_GCS)  | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_GCS_RO),        PIE_R)   | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_EXECONLY),      PIE_X_O) | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_READONLY_EXEC), PIE_RX_O)  | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_SHARED_EXEC),   PIE_RWX_O) | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_READONLY),      PIE_R_O)   | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_SHARED),        PIE_RW_O))

#define PIE_E1	( \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_GCS),           PIE_NONE_O) | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_GCS_RO),        PIE_NONE_O) | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_EXECONLY),      PIE_NONE_O) | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_READONLY_EXEC), PIE_R)      | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_SHARED_EXEC),   PIE_RW)     | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_READONLY),      PIE_R)      | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_SHARED),        PIE_RW)     | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_KERNEL_ROX),    PIE_RX)     | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_KERNEL_EXEC),   PIE_RWX)    | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_KERNEL_RO),     PIE_R)      | \
	PIRx_ELx_PERM_PREP(pte_pi_index(_PAGE_KERNEL),        PIE_RW))

#endif /* __ASM_PGTABLE_PROT_H */
