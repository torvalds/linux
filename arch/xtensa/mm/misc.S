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
#include <asm/page.h>
#include <asm/pgtable.h>
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
	entry	a1, 16

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

	retw

/*
 * copy_page and copy_user_page are the same for non-cache-aliased configs.
 *
 * copy_page (void *to, void *from)
 *               a2          a3
 */

ENTRY(copy_page)
	entry	a1, 16

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

	retw

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
 * clear_user_page (void *addr, unsigned long vaddr, struct page *page)
 *                     a2              a3                 a4
 */

ENTRY(clear_user_page)
	entry	a1, 32

	/* Mark page dirty and determine alias. */

	movi	a7, (1 << PG_ARCH_1)
	l32i	a5, a4, PAGE_FLAGS
	xor	a6, a2, a3
	extui	a3, a3, PAGE_SHIFT, DCACHE_ALIAS_ORDER
	extui	a6, a6, PAGE_SHIFT, DCACHE_ALIAS_ORDER
	or	a5, a5, a7
	slli	a3, a3, PAGE_SHIFT
	s32i	a5, a4, PAGE_FLAGS

	/* Skip setting up a temporary DTLB if not aliased. */

	beqz	a6, 1f

	/* Invalidate kernel page. */

	mov	a10, a2
	call8	__invalidate_dcache_page

	/* Setup a temporary DTLB with the color of the VPN */

	movi	a4, -PAGE_OFFSET + (PAGE_KERNEL | _PAGE_HW_WRITE)
	movi	a5, TLBTEMP_BASE_1			# virt
	add	a6, a2, a4				# ppn
	add	a2, a5, a3				# add 'color'

	wdtlb	a6, a2
	dsync

1:	movi	a3, 0
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

	bnez	a6, 1f
	retw

	/* We need to invalidate the temporary idtlb entry, if any. */

1:	addi	a2, a2, -PAGE_SIZE
	idtlb	a2
	dsync

	retw

/*
 * copy_page_user (void *to, void *from, unsigned long vaddr, struct page *page)
 *                    a2          a3	        a4		    a5
 */

ENTRY(copy_user_page)

	entry	a1, 32 

	/* Mark page dirty and determine alias for destination. */

	movi	a8, (1 << PG_ARCH_1)
	l32i	a9, a5, PAGE_FLAGS
	xor	a6, a2, a4
	xor	a7, a3, a4
	extui	a4, a4, PAGE_SHIFT, DCACHE_ALIAS_ORDER
	extui	a6, a6, PAGE_SHIFT, DCACHE_ALIAS_ORDER
	extui	a7, a7, PAGE_SHIFT, DCACHE_ALIAS_ORDER
	or	a9, a9, a8
	slli	a4, a4, PAGE_SHIFT
	s32i	a9, a5, PAGE_FLAGS
	movi	a5, -PAGE_OFFSET + (PAGE_KERNEL | _PAGE_HW_WRITE)

	beqz	a6, 1f

	/* Invalidate dcache */

	mov	a10, a2
	call8	__invalidate_dcache_page

	/* Setup a temporary DTLB with a matching color. */

	movi	a8, TLBTEMP_BASE_1			# base
	add	a6, a2, a5				# ppn
	add	a2, a8, a4				# add 'color'

	wdtlb	a6, a2
	dsync

	/* Skip setting up a temporary DTLB for destination if not aliased. */

1:	beqz	a7, 1f

	/* Setup a temporary DTLB with a matching color. */

	movi	a8, TLBTEMP_BASE_2			# base
	add	a7, a3, a5				# ppn
	add	a3, a8, a4
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

	bnez	a6, 1f
	bnez	a7, 2f
	retw

1:	addi	a2, a2, -PAGE_SIZE
	idtlb	a2
	dsync
	bnez	a7, 2f
	retw

2:	addi	a3, a3, -PAGE_SIZE+1
	idtlb	a3
	dsync

	retw

#endif

#if (DCACHE_WAY_SIZE > PAGE_SIZE)

/*
 * void __flush_invalidate_dcache_page_alias (addr, phys)
 *                                             a2    a3
 */

ENTRY(__flush_invalidate_dcache_page_alias)
	entry	sp, 16

	movi	a7, 0			# required for exception handler
	addi	a6, a3, (PAGE_KERNEL | _PAGE_HW_WRITE)
	mov	a4, a2
	wdtlb	a6, a2
	dsync

	___flush_invalidate_dcache_page a2 a3

	idtlb	a4
	dsync

	retw

#endif

ENTRY(__tlbtemp_mapping_itlb)

#if (ICACHE_WAY_SIZE > PAGE_SIZE)
	
ENTRY(__invalidate_icache_page_alias)
	entry	sp, 16

	addi	a6, a3, (PAGE_KERNEL_EXEC | _PAGE_HW_WRITE)
	mov	a4, a2
	witlb	a6, a2
	isync

	___invalidate_icache_page a2 a3

	iitlb	a4
	isync
	retw

#endif

/* End of special treatment in tlb miss exception */

ENTRY(__tlbtemp_mapping_end)

/*
 * void __invalidate_icache_page(ulong start)
 */

ENTRY(__invalidate_icache_page)
	entry	sp, 16

	___invalidate_icache_page a2 a3
	isync

	retw

/*
 * void __invalidate_dcache_page(ulong start)
 */

ENTRY(__invalidate_dcache_page)
	entry	sp, 16

	___invalidate_dcache_page a2 a3
	dsync

	retw

/*
 * void __flush_invalidate_dcache_page(ulong start)
 */

ENTRY(__flush_invalidate_dcache_page)
	entry	sp, 16

	___flush_invalidate_dcache_page a2 a3

	dsync
	retw

/*
 * void __flush_dcache_page(ulong start)
 */

ENTRY(__flush_dcache_page)
	entry	sp, 16

	___flush_dcache_page a2 a3

	dsync
	retw

/*
 * void __invalidate_icache_range(ulong start, ulong size)
 */

ENTRY(__invalidate_icache_range)
	entry	sp, 16

	___invalidate_icache_range a2 a3 a4
	isync

	retw

/*
 * void __flush_invalidate_dcache_range(ulong start, ulong size)
 */

ENTRY(__flush_invalidate_dcache_range)
	entry	sp, 16

	___flush_invalidate_dcache_range a2 a3 a4
	dsync

	retw

/*
 * void _flush_dcache_range(ulong start, ulong size)
 */

ENTRY(__flush_dcache_range)
	entry	sp, 16

	___flush_dcache_range a2 a3 a4
	dsync

	retw

/*
 * void _invalidate_dcache_range(ulong start, ulong size)
 */

ENTRY(__invalidate_dcache_range)
	entry	sp, 16

	___invalidate_dcache_range a2 a3 a4

	retw

/*
 * void _invalidate_icache_all(void)
 */

ENTRY(__invalidate_icache_all)
	entry	sp, 16

	___invalidate_icache_all a2 a3
	isync

	retw

/*
 * void _flush_invalidate_dcache_all(void)
 */

ENTRY(__flush_invalidate_dcache_all)
	entry	sp, 16

	___flush_invalidate_dcache_all a2 a3
	dsync

	retw

/*
 * void _invalidate_dcache_all(void)
 */

ENTRY(__invalidate_dcache_all)
	entry	sp, 16

	___invalidate_dcache_all a2 a3
	dsync

	retw

