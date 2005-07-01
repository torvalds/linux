/*
 * arch/xtensa/mm/misc.S
 *
 * Miscellaneous assembly functions.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 *
 * Chris Zankel	<chris@zankel.net>
 */

/* Note: we might want to implement some of the loops as zero-overhead-loops,
 *	 where applicable and if supported by the processor.
 */

#include <linux/linkage.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <xtensa/cacheasm.h>
#include <xtensa/cacheattrasm.h>

/* clear_page (page) */

ENTRY(clear_page)
	entry	a1, 16
	addi	a4, a2, PAGE_SIZE
	movi	a3, 0

1:	s32i	a3, a2, 0
	s32i	a3, a2, 4
	s32i	a3, a2, 8
	s32i	a3, a2, 12
	s32i	a3, a2, 16
	s32i	a3, a2, 20
	s32i	a3, a2, 24
	s32i	a3, a2, 28
	addi	a2, a2, 32
	blt	a2, a4, 1b

	retw

/*
 * copy_page (void *to, void *from)
 *                  a2        a3
 */

ENTRY(copy_page)
	entry	a1, 16
	addi	a4, a2, PAGE_SIZE

1:	l32i	a5, a3, 0
	l32i	a6, a3, 4
	l32i	a7, a3, 8
	s32i	a5, a2, 0
	s32i	a6, a2, 4
	s32i	a7, a2, 8
	l32i	a5, a3, 12
	l32i	a6, a3, 16
	l32i	a7, a3, 20
	s32i	a5, a2, 12
	s32i	a6, a2, 16
	s32i	a7, a2, 20
	l32i	a5, a3, 24
	l32i	a6, a3, 28
	s32i	a5, a2, 24
	s32i	a6, a2, 28
	addi	a2, a2, 32
	addi	a3, a3, 32
	blt	a2, a4, 1b

	retw


/*
 * void __flush_invalidate_cache_all(void)
 */

ENTRY(__flush_invalidate_cache_all)
	entry	sp, 16
	dcache_writeback_inv_all a2, a3
	icache_invalidate_all a2, a3
	retw

/*
 * void __invalidate_icache_all(void)
 */

ENTRY(__invalidate_icache_all)
	entry	sp, 16
	icache_invalidate_all a2, a3
	retw

/*
 * void __flush_invalidate_dcache_all(void)
 */

ENTRY(__flush_invalidate_dcache_all)
	entry	sp, 16
	dcache_writeback_inv_all a2, a3
	retw


/*
 * void __flush_invalidate_cache_range(ulong start, ulong size)
 */

ENTRY(__flush_invalidate_cache_range)
	entry	sp, 16
	mov	a4, a2
	mov	a5, a3
	dcache_writeback_inv_region a4, a5, a6
	icache_invalidate_region a2, a3, a4
	retw

/*
 * void __invalidate_icache_page(ulong start)
 */

ENTRY(__invalidate_icache_page)
	entry	sp, 16
	movi	a3, PAGE_SIZE
	icache_invalidate_region a2, a3, a4
	retw

/*
 * void __invalidate_dcache_page(ulong start)
 */

ENTRY(__invalidate_dcache_page)
	entry	sp, 16
	movi	a3, PAGE_SIZE
	dcache_invalidate_region a2, a3, a4
	retw

/*
 * void __invalidate_icache_range(ulong start, ulong size)
 */

ENTRY(__invalidate_icache_range)
	entry	sp, 16
	icache_invalidate_region a2, a3, a4
	retw

/*
 * void __invalidate_dcache_range(ulong start, ulong size)
 */

ENTRY(__invalidate_dcache_range)
	entry	sp, 16
	dcache_invalidate_region a2, a3, a4
	retw

/*
 * void __flush_dcache_page(ulong start)
 */

ENTRY(__flush_dcache_page)
	entry	sp, 16
	movi	a3, PAGE_SIZE
	dcache_writeback_region a2, a3, a4
	retw

/*
 * void __flush_invalidate_dcache_page(ulong start)
 */

ENTRY(__flush_invalidate_dcache_page)
	entry	sp, 16
	movi	a3, PAGE_SIZE
	dcache_writeback_inv_region a2, a3, a4
	retw

/*
 * void __flush_invalidate_dcache_range(ulong start, ulong size)
 */

ENTRY(__flush_invalidate_dcache_range)
	entry	sp, 16
	dcache_writeback_inv_region a2, a3, a4
	retw

/*
 * void __invalidate_dcache_all(void)
 */

ENTRY(__invalidate_dcache_all)
	entry	sp, 16
	dcache_invalidate_all a2, a3
	retw

/*
 * void __flush_invalidate_dcache_page_phys(ulong start)
 */

ENTRY(__flush_invalidate_dcache_page_phys)
	entry	sp, 16

	movi	a3, XCHAL_DCACHE_SIZE
	movi	a4, PAGE_MASK | 1
	addi	a2, a2, 1

1:	addi	a3, a3, -XCHAL_DCACHE_LINESIZE

	ldct	a6, a3
	dsync
	and	a6, a6, a4
	beq	a6, a2, 2f
	bgeui	a3, 2, 1b
	retw

2:	diwbi	a3, 0
	bgeui	a3, 2, 1b
	retw

ENTRY(check_dcache_low0)
	entry	sp, 16

	movi	a3, XCHAL_DCACHE_SIZE / 4
	movi	a4, PAGE_MASK | 1
	addi	a2, a2, 1

1:	addi	a3, a3, -XCHAL_DCACHE_LINESIZE

	ldct	a6, a3
	dsync
	and	a6, a6, a4
	beq	a6, a2, 2f
	bgeui	a3, 2, 1b
	retw

2:	j 2b

ENTRY(check_dcache_high0)
	entry	sp, 16

	movi	a5, XCHAL_DCACHE_SIZE / 4
	movi	a3, XCHAL_DCACHE_SIZE / 2
	movi	a4, PAGE_MASK | 1
	addi	a2, a2, 1

1:	addi	a3, a3, -XCHAL_DCACHE_LINESIZE
	addi	a5, a5, -XCHAL_DCACHE_LINESIZE

	ldct	a6, a3
	dsync
	and	a6, a6, a4
	beq	a6, a2, 2f
	bgeui	a5, 2, 1b
	retw

2:	j 2b

ENTRY(check_dcache_low1)
	entry	sp, 16

	movi	a5, XCHAL_DCACHE_SIZE / 4
	movi	a3, XCHAL_DCACHE_SIZE * 3 / 4
	movi	a4, PAGE_MASK | 1
	addi	a2, a2, 1

1:	addi	a3, a3, -XCHAL_DCACHE_LINESIZE
	addi	a5, a5, -XCHAL_DCACHE_LINESIZE

	ldct	a6, a3
	dsync
	and	a6, a6, a4
	beq	a6, a2, 2f
	bgeui	a5, 2, 1b
	retw

2:	j 2b

ENTRY(check_dcache_high1)
	entry	sp, 16

	movi	a5, XCHAL_DCACHE_SIZE / 4
	movi	a3, XCHAL_DCACHE_SIZE
	movi	a4, PAGE_MASK | 1
	addi	a2, a2, 1

1:	addi	a3, a3, -XCHAL_DCACHE_LINESIZE
	addi	a5, a5, -XCHAL_DCACHE_LINESIZE

	ldct	a6, a3
	dsync
	and	a6, a6, a4
	beq	a6, a2, 2f
	bgeui	a5, 2, 1b
	retw

2:	j 2b


/*
 * void __invalidate_icache_page_phys(ulong start)
 */

ENTRY(__invalidate_icache_page_phys)
	entry	sp, 16

	movi	a3, XCHAL_ICACHE_SIZE
	movi	a4, PAGE_MASK | 1
	addi	a2, a2, 1

1:	addi	a3, a3, -XCHAL_ICACHE_LINESIZE

	lict	a6, a3
	isync
	and	a6, a6, a4
	beq	a6, a2, 2f
	bgeui	a3, 2, 1b
	retw

2:	iii	a3, 0
	bgeui	a3, 2, 1b
	retw


#if 0

	movi	a3, XCHAL_DCACHE_WAYS - 1
	movi	a4, PAGE_SIZE

1:	mov	a5, a2
	add	a6, a2, a4

2:	diwbi	a5, 0
	diwbi	a5, XCHAL_DCACHE_LINESIZE
	diwbi	a5, XCHAL_DCACHE_LINESIZE * 2
	diwbi	a5, XCHAL_DCACHE_LINESIZE * 3

	addi	a5, a5, XCHAL_DCACHE_LINESIZE * 4
	blt	a5, a6, 2b

	addi	a3, a3, -1
	addi	a2, a2, XCHAL_DCACHE_SIZE / XCHAL_DCACHE_WAYS
	bgez	a3, 1b

	retw

ENTRY(__invalidate_icache_page_index)
	entry	sp, 16

	movi	a3, XCHAL_ICACHE_WAYS - 1
	movi	a4, PAGE_SIZE

1:	mov	a5, a2
	add	a6, a2, a4

2:	iii	a5, 0
	iii	a5, XCHAL_ICACHE_LINESIZE
	iii	a5, XCHAL_ICACHE_LINESIZE * 2
	iii	a5, XCHAL_ICACHE_LINESIZE * 3

	addi	a5, a5, XCHAL_ICACHE_LINESIZE * 4
	blt	a5, a6, 2b

	addi	a3, a3, -1
	addi	a2, a2, XCHAL_ICACHE_SIZE / XCHAL_ICACHE_WAYS
	bgez	a3, 2b

	retw

#endif






