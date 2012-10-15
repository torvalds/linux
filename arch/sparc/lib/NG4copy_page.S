/* NG4copy_page.S: Niagara-4 optimized copy page.
 *
 * Copyright (C) 2012 (davem@davemloft.net)
 */

#include <asm/asi.h>
#include <asm/page.h>

	.text
	.align		32

	.register	%g2, #scratch
	.register	%g3, #scratch

	.globl		NG4copy_user_page
NG4copy_user_page:	/* %o0=dest, %o1=src, %o2=vaddr */
	prefetch	[%o1 + 0x000], #n_reads_strong
	prefetch	[%o1 + 0x040], #n_reads_strong
	prefetch	[%o1 + 0x080], #n_reads_strong
	prefetch	[%o1 + 0x0c0], #n_reads_strong
	set		PAGE_SIZE, %g7
	prefetch	[%o1 + 0x100], #n_reads_strong
	prefetch	[%o1 + 0x140], #n_reads_strong
	prefetch	[%o1 + 0x180], #n_reads_strong
	prefetch	[%o1 + 0x1c0], #n_reads_strong
1:
	ldx		[%o1 + 0x00], %o2
	subcc		%g7, 0x40, %g7
	ldx		[%o1 + 0x08], %o3
	ldx		[%o1 + 0x10], %o4
	ldx		[%o1 + 0x18], %o5
	ldx		[%o1 + 0x20], %g1
	stxa		%o2, [%o0] ASI_ST_BLKINIT_MRU_P
	add		%o0, 0x08, %o0
	ldx		[%o1 + 0x28], %g2
	stxa		%o3, [%o0] ASI_ST_BLKINIT_MRU_P
	add		%o0, 0x08, %o0
	ldx		[%o1 + 0x30], %g3
	stxa		%o4, [%o0] ASI_ST_BLKINIT_MRU_P
	add		%o0, 0x08, %o0
	ldx		[%o1 + 0x38], %o2
	add		%o1, 0x40, %o1
	stxa		%o5, [%o0] ASI_ST_BLKINIT_MRU_P
	add		%o0, 0x08, %o0
	stxa		%g1, [%o0] ASI_ST_BLKINIT_MRU_P
	add		%o0, 0x08, %o0
	stxa		%g2, [%o0] ASI_ST_BLKINIT_MRU_P
	add		%o0, 0x08, %o0
	stxa		%g3, [%o0] ASI_ST_BLKINIT_MRU_P
	add		%o0, 0x08, %o0
	stxa		%o2, [%o0] ASI_ST_BLKINIT_MRU_P
	add		%o0, 0x08, %o0
	bne,pt		%icc, 1b
	 prefetch	[%o1 + 0x200], #n_reads_strong
	retl
	 membar		#StoreLoad | #StoreStore
	.size		NG4copy_user_page,.-NG4copy_user_page
