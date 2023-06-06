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
#define PTE_PROT_NONE		(_AT(pteval_t, 1) << 58) /* only when !PTE_VALID */

/*
 * This bit indicates that the entry is present i.e. pmd_page()
 * still points to a valid huge page in memory even if the pmd
 * has been invalidated.
 */
#define PMD_PRESENT_INVALID	(_AT(pteval_t, 1) << 59) /* only when !PMD_SECT_VALID */

#define _PROT_DEFAULT		(PTE_TYPE_PAGE | PTE_AF | PTE_SHARED)
#define _PROT_SECT_DEFAULT	(PMD_TYPE_SECT | PMD_SECT_AF | PMD_SECT_S)

#define PROT_DEFAULT		(_PROT_DEFAULT | PTE_MAYBE_NG)
#define PROT_SECT_DEFAULT	(_PROT_SECT_DEFAULT | PMD_MAYBE_NG)

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

#ifdef __ASSEMBLY__
#define PTE_MAYBE_NG	0
#endif

#ifndef __ASSEMBLY__

#include <asm/cpufeature.h>
#include <asm/pgtable-types.h>

extern bool arm64_use_ng_mappings;

#define PTE_MAYBE_NG		(arm64_use_ng_mappings ? PTE_NG : 0)
#define PMD_MAYBE_NG		(arm64_use_ng_mappings ? PMD_SECT_NG : 0)

/*
 * If we have userspace only BTI we don't want to mark kernel pages
 * guarded even if the system does support BTI.
 */
#ifdef CONFIG_ARM64_BTI_KERNEL
#define PTE_MAYBE_GP		(system_supports_bti() ? PTE_GP : 0)
#else
#define PTE_MAYBE_GP		0
#endif

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

#define PAGE_NONE		__pgprot(((_PAGE_DEFAULT) & ~PTE_VALID) | PTE_PROT_NONE | PTE_RDONLY | PTE_NG | PTE_PXN | PTE_UXN)
/* shared+writable pages are clean by default, hence PTE_RDONLY|PTE_WRITE */
#define PAGE_SHARED		__pgprot(_PAGE_SHARED)
#define PAGE_SHARED_EXEC	__pgprot(_PAGE_SHARED_EXEC)
#define PAGE_READONLY		__pgprot(_PAGE_READONLY)
#define PAGE_READONLY_EXEC	__pgprot(_PAGE_READONLY_EXEC)
#define PAGE_EXECONLY		__pgprot(_PAGE_EXECONLY)

#endif /* __ASSEMBLY__ */

#endif /* __ASM_PGTABLE_PROT_H */
