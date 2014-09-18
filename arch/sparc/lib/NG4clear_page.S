/* NG4copy_page.S: Niagara-4 optimized clear page.
 *
 * Copyright (C) 2012 (davem@davemloft.net)
 */

#include <asm/asi.h>
#include <asm/page.h>

	.text

	.register	%g3, #scratch

	.align		32
	.globl		NG4clear_page
	.globl		NG4clear_user_page
NG4clear_page:		/* %o0=dest */
NG4clear_user_page:	/* %o0=dest, %o1=vaddr */
	set		PAGE_SIZE, %g7
	mov		0x20, %g3
1:	stxa		%g0, [%o0 + %g0] ASI_ST_BLKINIT_MRU_P
	subcc		%g7, 0x40, %g7
	stxa		%g0, [%o0 + %g3] ASI_ST_BLKINIT_MRU_P
	bne,pt		%xcc, 1b
	 add		%o0, 0x40, %o0
	membar		#StoreLoad|#StoreStore
	retl
	 nop
	.size		NG4clear_page,.-NG4clear_page
	.size		NG4clear_user_page,.-NG4clear_user_page