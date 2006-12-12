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
#include <asm/asmmacro.h>
#include <asm/cacheasm.h>

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

