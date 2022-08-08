/*
 * arch/xtensa/mm/misc.S
 *
 * Miscellaneous assembly functions.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2007 Tensilica Inc.
 *
 * Chris Zankel	<chris@zankel.net>
 */


#include <linux/linkage.h>
#include <linux/pgtable.h>
#include <asm/page.h>
#include <asm/asmmacro.h>
#include <asm/cacheasm.h>
#include <asm/tlbflush.h>


/*
 * clear_page and clear_user_page are the same for non-cache-aliased configs.
 *
 * clear_page (unsigned long page)
 *                    a2
 */

ENTRY(clear_page)

	abi_entry_default

	movi	a3, 0
	__loopi	a2, a7, PAGE_SIZE, 32
	s32i	a3, a2, 0
	s32i	a3, a2, 4
	s32i	a3, a2, 8
	s32i	a3, a2, 12
	s32i	a3, a2, 16
	s32i	a3, a2, 20
	s32i	a3, a2, 24
	s32i	a3, a2, 28
	__endla	a2, a7, 32

	abi_ret_default

ENDPROC(clear_page)

/*
 * copy_page and copy_user_page are the same for non-cache-aliased configs.
 *
 * copy_page (void *to, void *from)
 *               a2          a3
 */

ENTRY(copy_page)

	abi_entry_default

	__loopi a2, a4, PAGE_SIZE, 32

	l32i    a8, a3, 0
	l32i    a9, a3, 4
	s32i    a8, a2, 0
	s32i    a9, a2, 4

	l32i    a8, a3, 8
	l32i    a9, a3, 12
	s32i    a8, a2, 8
	s32i    a9, a2, 12

	l32i    a8, a3, 16
	l32i    a9, a3, 20
	s32i    a8, a2, 16
	s32i    a9, a2, 20

	l32i    a8, a3, 24
	l32i    a9, a3, 28
	s32i    a8, a2, 24
	s32i    a9, a2, 28

	addi    a2, a2, 32
	addi    a3, a3, 32

	__endl  a2, a4

	abi_ret_default

ENDPROC(copy_page)

#ifdef CONFIG_MMU
/*
 * If we have to deal with cache aliasing, we use temporary memory mappings
 * to ensure that the source and destination pages have the same color as
 * the virtual address. We use way 0 and 1 for temporary mappings in such cases.
 *
 * The temporary DTLB entries shouldn't be flushed by interrupts, but are
 * flushed by preemptive task switches. Special code in the 
 * fast_second_level_miss handler re-established the temporary mapping. 
 * It requires that the PPNs for the destination and source addresses are
 * in a6, and a7, respectively.
 */

/* TLB miss exceptions are treated special in the following region */

ENTRY(__tlbtemp_mapping_start)

#if (DCACHE_WAY_SIZE > PAGE_SIZE)

/*
 * clear_page_alias(void *addr, unsigned long paddr)
 *                     a2              a3
 */

ENTRY(clear_page_alias)

	abi_entry_default

	movi	a5, PAGE_OFFSET
	addi	a6, a3, (PAGE_KERNEL | _PAGE_HW_WRITE)
	mov	a4, a2
	wdtlb	a6, a2
	dsync

	movi	a3, 0
	__loopi	a2, a7, PAGE_SIZE, 32
	s32i	a3, a2, 0
	s32i	a3, a2, 4
	s32i	a3, a2, 8
	s32i	a3, a2, 12
	s32i	a3, a2, 16
	s32i	a3, a2, 20
	s32i	a3, a2, 24
	s32i	a3, a2, 28
	__endla	a2, a7, 32

	/* We need to invalidate the temporary dtlb entry. */

	idtlb	a4
	dsync

	abi_ret_default

ENDPROC(clear_page_alias)

/*
 * copy_page_alias(void *to, void *from,
 *			a2	  a3
 *                 unsigned long to_paddr, unsigned long from_paddr)
 *	        		 a4			 a5
 */

ENTRY(copy_page_alias)

	abi_entry_default

	/* Setup a temporary DTLB for destination. */

	addi	a6, a4, (PAGE_KERNEL | _PAGE_HW_WRITE)
	wdtlb	a6, a2
	dsync

	/* Setup a temporary DTLB for source. */

	addi	a7, a5, PAGE_KERNEL
	addi	a8, a3, 1				# way1

	wdtlb	a7, a8
	dsync

1:	__loopi a2, a4, PAGE_SIZE, 32

	l32i    a8, a3, 0
	l32i    a9, a3, 4
	s32i    a8, a2, 0
	s32i    a9, a2, 4

	l32i    a8, a3, 8
	l32i    a9, a3, 12
	s32i    a8, a2, 8
	s32i    a9, a2, 12

	l32i    a8, a3, 16
	l32i    a9, a3, 20
	s32i    a8, a2, 16
	s32i    a9, a2, 20

	l32i    a8, a3, 24
	l32i    a9, a3, 28
	s32i    a8, a2, 24
	s32i    a9, a2, 28

	addi    a2, a2, 32
	addi    a3, a3, 32

	__endl  a2, a4

	/* We need to invalidate any temporary mapping! */

	addi	a2, a2, -PAGE_SIZE
	idtlb	a2
	dsync

	addi	a3, a3, -PAGE_SIZE+1
	idtlb	a3
	dsync

	abi_ret_default

ENDPROC(copy_page_alias)

#endif

#if (DCACHE_WAY_SIZE > PAGE_SIZE)

/*
 * void __flush_invalidate_dcache_page_alias (addr, phys)
 *                                             a2    a3
 */

ENTRY(__flush_invalidate_dcache_page_alias)

	abi_entry_default

	movi	a7, 0			# required for exception handler
	addi	a6, a3, (PAGE_KERNEL | _PAGE_HW_WRITE)
	mov	a4, a2
	wdtlb	a6, a2
	dsync

	___flush_invalidate_dcache_page a2 a3

	idtlb	a4
	dsync

	abi_ret_default

ENDPROC(__flush_invalidate_dcache_page_alias)

/*
 * void __invalidate_dcache_page_alias (addr, phys)
 *                                       a2    a3
 */

ENTRY(__invalidate_dcache_page_alias)

	abi_entry_default

	movi	a7, 0			# required for exception handler
	addi	a6, a3, (PAGE_KERNEL | _PAGE_HW_WRITE)
	mov	a4, a2
	wdtlb	a6, a2
	dsync

	___invalidate_dcache_page a2 a3

	idtlb	a4
	dsync

	abi_ret_default

ENDPROC(__invalidate_dcache_page_alias)
#endif

ENTRY(__tlbtemp_mapping_itlb)

#if (ICACHE_WAY_SIZE > PAGE_SIZE)
	
ENTRY(__invalidate_icache_page_alias)

	abi_entry_default

	addi	a6, a3, (PAGE_KERNEL_EXEC | _PAGE_HW_WRITE)
	mov	a4, a2
	witlb	a6, a2
	isync

	___invalidate_icache_page a2 a3

	iitlb	a4
	isync
	abi_ret_default

ENDPROC(__invalidate_icache_page_alias)

#endif

/* End of special treatment in tlb miss exception */

ENTRY(__tlbtemp_mapping_end)

#endif /* CONFIG_MMU

/*
 * void __invalidate_icache_page(ulong start)
 */

ENTRY(__invalidate_icache_page)

	abi_entry_default

	___invalidate_icache_page a2 a3
	isync

	abi_ret_default

ENDPROC(__invalidate_icache_page)

/*
 * void __invalidate_dcache_page(ulong start)
 */

ENTRY(__invalidate_dcache_page)

	abi_entry_default

	___invalidate_dcache_page a2 a3
	dsync

	abi_ret_default

ENDPROC(__invalidate_dcache_page)

/*
 * void __flush_invalidate_dcache_page(ulong start)
 */

ENTRY(__flush_invalidate_dcache_page)

	abi_entry_default

	___flush_invalidate_dcache_page a2 a3

	dsync
	abi_ret_default

ENDPROC(__flush_invalidate_dcache_page)

/*
 * void __flush_dcache_page(ulong start)
 */

ENTRY(__flush_dcache_page)

	abi_entry_default

	___flush_dcache_page a2 a3

	dsync
	abi_ret_default

ENDPROC(__flush_dcache_page)

/*
 * void __invalidate_icache_range(ulong start, ulong size)
 */

ENTRY(__invalidate_icache_range)

	abi_entry_default

	___invalidate_icache_range a2 a3 a4
	isync

	abi_ret_default

ENDPROC(__invalidate_icache_range)

/*
 * void __flush_invalidate_dcache_range(ulong start, ulong size)
 */

ENTRY(__flush_invalidate_dcache_range)

	abi_entry_default

	___flush_invalidate_dcache_range a2 a3 a4
	dsync

	abi_ret_default

ENDPROC(__flush_invalidate_dcache_range)

/*
 * void _flush_dcache_range(ulong start, ulong size)
 */

ENTRY(__flush_dcache_range)

	abi_entry_default

	___flush_dcache_range a2 a3 a4
	dsync

	abi_ret_default

ENDPROC(__flush_dcache_range)

/*
 * void _invalidate_dcache_range(ulong start, ulong size)
 */

ENTRY(__invalidate_dcache_range)

	abi_entry_default

	___invalidate_dcache_range a2 a3 a4

	abi_ret_default

ENDPROC(__invalidate_dcache_range)

/*
 * void _invalidate_icache_all(void)
 */

ENTRY(__invalidate_icache_all)

	abi_entry_default

	___invalidate_icache_all a2 a3
	isync

	abi_ret_default

ENDPROC(__invalidate_icache_all)

/*
 * void _flush_invalidate_dcache_all(void)
 */

ENTRY(__flush_invalidate_dcache_all)

	abi_entry_default

	___flush_invalidate_dcache_all a2 a3
	dsync

	abi_ret_default

ENDPROC(__flush_invalidate_dcache_all)

/*
 * void _invalidate_dcache_all(void)
 */

ENTRY(__invalidate_dcache_all)

	abi_entry_default

	___invalidate_dcache_all a2 a3
	dsync

	abi_ret_default

ENDPROC(__invalidate_dcache_all)
