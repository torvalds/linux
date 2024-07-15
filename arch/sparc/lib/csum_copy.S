/* SPDX-License-Identifier: GPL-2.0 */
/* csum_copy.S: Checksum+copy code for sparc64
 *
 * Copyright (C) 2005 David S. Miller <davem@davemloft.net>
 */

#include <linux/export.h>

#ifdef __KERNEL__
#define GLOBAL_SPARE	%g7
#else
#define GLOBAL_SPARE	%g5
#endif

#ifndef EX_LD
#define EX_LD(x)	x
#endif

#ifndef EX_ST
#define EX_ST(x)	x
#endif

#ifndef EX_RETVAL
#define EX_RETVAL(x)	x
#endif

#ifndef LOAD
#define LOAD(type,addr,dest)	type [addr], dest
#endif

#ifndef STORE
#define STORE(type,src,addr)	type src, [addr]
#endif

#ifndef FUNC_NAME
#define FUNC_NAME	csum_partial_copy_nocheck
#endif

	.register	%g2, #scratch
	.register	%g3, #scratch

	.text

90:
	/* We checked for zero length already, so there must be
	 * at least one byte.
	 */
	be,pt		%icc, 1f
	 nop
	EX_LD(LOAD(ldub, %o0 + 0x00, %o4))
	add		%o0, 1, %o0
	sub		%o2, 1, %o2
	EX_ST(STORE(stb, %o4, %o1 + 0x00))
	add		%o1, 1, %o1
1:	andcc		%o0, 0x2, %g0
	be,pn		%icc, 80f
	 cmp		%o2, 2
	blu,pn		%icc, 60f
	 nop
	EX_LD(LOAD(lduh, %o0 + 0x00, %o5))
	add		%o0, 2, %o0
	sub		%o2, 2, %o2
	EX_ST(STORE(sth, %o5, %o1 + 0x00))
	add		%o1, 2, %o1
	ba,pt		%xcc, 80f
	 add		%o5, %o4, %o4

	.globl		FUNC_NAME
	.type		FUNC_NAME,#function
	EXPORT_SYMBOL(FUNC_NAME)
FUNC_NAME:		/* %o0=src, %o1=dst, %o2=len */
	LOAD(prefetch, %o0 + 0x000, #n_reads)
	xor		%o0, %o1, %g1
	mov		-1, %o3
	clr		%o4
	andcc		%g1, 0x3, %g0
	bne,pn		%icc, 95f
	 LOAD(prefetch, %o0 + 0x040, #n_reads)
	
	brz,pn		%o2, 70f
	 andcc		%o0, 0x3, %g0

	/* We "remember" whether the lowest bit in the address
	 * was set in GLOBAL_SPARE.  Because if it is, we have to swap
	 * upper and lower 8 bit fields of the sum we calculate.
	*/
	bne,pn		%icc, 90b
	 andcc		%o0, 0x1, GLOBAL_SPARE

80:
	LOAD(prefetch, %o0 + 0x080, #n_reads)
	andncc		%o2, 0x3f, %g3

	LOAD(prefetch, %o0 + 0x0c0, #n_reads)
	sub		%o2, %g3, %o2
	brz,pn		%g3, 2f
	 LOAD(prefetch, %o0 + 0x100, #n_reads)

	/* So that we don't need to use the non-pairing
	 * add-with-carry instructions we accumulate 32-bit
	 * values into a 64-bit register.  At the end of the
	 * loop we fold it down to 32-bits and so on.
	 */
	ba,pt		%xcc, 1f
	LOAD(prefetch, %o0 + 0x140, #n_reads)

	.align		32
1:	EX_LD(LOAD(lduw, %o0 + 0x00, %o5))
	EX_LD(LOAD(lduw, %o0 + 0x04, %g1))
	EX_LD(LOAD(lduw, %o0 + 0x08, %g2))
	add		%o4, %o5, %o4
	EX_ST(STORE(stw, %o5, %o1 + 0x00))
	EX_LD(LOAD(lduw, %o0 + 0x0c, %o5))
	add		%o4, %g1, %o4
	EX_ST(STORE(stw, %g1, %o1 + 0x04))
	EX_LD(LOAD(lduw, %o0 + 0x10, %g1))
	add		%o4, %g2, %o4
	EX_ST(STORE(stw, %g2, %o1 + 0x08))
	EX_LD(LOAD(lduw, %o0 + 0x14, %g2))
	add		%o4, %o5, %o4
	EX_ST(STORE(stw, %o5, %o1 + 0x0c))
	EX_LD(LOAD(lduw, %o0 + 0x18, %o5))
	add		%o4, %g1, %o4
	EX_ST(STORE(stw, %g1, %o1 + 0x10))
	EX_LD(LOAD(lduw, %o0 + 0x1c, %g1))
	add		%o4, %g2, %o4
	EX_ST(STORE(stw, %g2, %o1 + 0x14))
	EX_LD(LOAD(lduw, %o0 + 0x20, %g2))
	add		%o4, %o5, %o4
	EX_ST(STORE(stw, %o5, %o1 + 0x18))
	EX_LD(LOAD(lduw, %o0 + 0x24, %o5))
	add		%o4, %g1, %o4
	EX_ST(STORE(stw, %g1, %o1 + 0x1c))
	EX_LD(LOAD(lduw, %o0 + 0x28, %g1))
	add		%o4, %g2, %o4
	EX_ST(STORE(stw, %g2, %o1 + 0x20))
	EX_LD(LOAD(lduw, %o0 + 0x2c, %g2))
	add		%o4, %o5, %o4
	EX_ST(STORE(stw, %o5, %o1 + 0x24))
	EX_LD(LOAD(lduw, %o0 + 0x30, %o5))
	add		%o4, %g1, %o4
	EX_ST(STORE(stw, %g1, %o1 + 0x28))
	EX_LD(LOAD(lduw, %o0 + 0x34, %g1))
	add		%o4, %g2, %o4
	EX_ST(STORE(stw, %g2, %o1 + 0x2c))
	EX_LD(LOAD(lduw, %o0 + 0x38, %g2))
	add		%o4, %o5, %o4
	EX_ST(STORE(stw, %o5, %o1 + 0x30))
	EX_LD(LOAD(lduw, %o0 + 0x3c, %o5))
	add		%o4, %g1, %o4
	EX_ST(STORE(stw, %g1, %o1 + 0x34))
	LOAD(prefetch, %o0 + 0x180, #n_reads)
	add		%o4, %g2, %o4
	EX_ST(STORE(stw, %g2, %o1 + 0x38))
	subcc		%g3, 0x40, %g3
	add		%o0, 0x40, %o0
	add		%o4, %o5, %o4
	EX_ST(STORE(stw, %o5, %o1 + 0x3c))
	bne,pt		%icc, 1b
	 add		%o1, 0x40, %o1

2:	and		%o2, 0x3c, %g3
	brz,pn		%g3, 2f
	 sub		%o2, %g3, %o2
1:	EX_LD(LOAD(lduw, %o0 + 0x00, %o5))
	subcc		%g3, 0x4, %g3
	add		%o0, 0x4, %o0
	add		%o4, %o5, %o4
	EX_ST(STORE(stw, %o5, %o1 + 0x00))
	bne,pt		%icc, 1b
	 add		%o1, 0x4, %o1

2:
	/* fold 64-->32 */
	srlx		%o4, 32, %o5
	srl		%o4, 0, %o4
	add		%o4, %o5, %o4
	srlx		%o4, 32, %o5
	srl		%o4, 0, %o4
	add		%o4, %o5, %o4

	/* fold 32-->16 */
	sethi		%hi(0xffff0000), %g1
	srl		%o4, 16, %o5
	andn		%o4, %g1, %g2
	add		%o5, %g2, %o4
	srl		%o4, 16, %o5
	andn		%o4, %g1, %g2
	add		%o5, %g2, %o4

60:
	/* %o4 has the 16-bit sum we have calculated so-far.  */
	cmp		%o2, 2
	blu,pt		%icc, 1f
	 nop
	EX_LD(LOAD(lduh, %o0 + 0x00, %o5))
	sub		%o2, 2, %o2
	add		%o0, 2, %o0
	add		%o4, %o5, %o4
	EX_ST(STORE(sth, %o5, %o1 + 0x00))
	add		%o1, 0x2, %o1
1:	brz,pt		%o2, 1f
	 nop
	EX_LD(LOAD(ldub, %o0 + 0x00, %o5))
	sub		%o2, 1, %o2
	add		%o0, 1, %o0
	EX_ST(STORE(stb, %o5, %o1 + 0x00))
	sllx		%o5, 8, %o5
	add		%o1, 1, %o1
	add		%o4, %o5, %o4
1:
	/* fold 32-->16 */
	sethi		%hi(0xffff0000), %g1
	srl		%o4, 16, %o5
	andn		%o4, %g1, %g2
	add		%o5, %g2, %o4
	srl		%o4, 16, %o5
	andn		%o4, %g1, %g2
	add		%o5, %g2, %o4

1:	brz,pt		GLOBAL_SPARE, 1f
	 nop

	/* We started with an odd byte, byte-swap the result.  */
	srl		%o4, 8, %o5
	and		%o4, 0xff, %g1
	sll		%g1, 8, %g1
	or		%o5, %g1, %o4

1:	addcc		%o3, %o4, %o3
	addc		%g0, %o3, %o3

70:
	retl
	 srl		%o3, 0, %o0

95:	mov		0, GLOBAL_SPARE
	brlez,pn	%o2, 4f
	 andcc		%o0, 1, %o5		
	be,a,pt		%icc, 1f
	 srl		%o2, 1, %g1		
	sub		%o2, 1, %o2	
	EX_LD(LOAD(ldub, %o0, GLOBAL_SPARE))
	add		%o0, 1, %o0	
	EX_ST(STORE(stb, GLOBAL_SPARE, %o1))
	srl		%o2, 1, %g1
	add		%o1, 1, %o1
1:	brz,a,pn	%g1, 3f
	 andcc		%o2, 1, %g0
	andcc		%o0, 2, %g0	
	be,a,pt		%icc, 1f
	 srl		%g1, 1, %g1
	EX_LD(LOAD(lduh, %o0, %o4))
	sub		%o2, 2, %o2	
	srl		%o4, 8, %g2
	sub		%g1, 1, %g1	
	EX_ST(STORE(stb, %g2, %o1))
	add		%o4, GLOBAL_SPARE, GLOBAL_SPARE
	EX_ST(STORE(stb, %o4, %o1 + 1))
	add		%o0, 2, %o0	
	srl		%g1, 1, %g1
	add		%o1, 2, %o1
1:	brz,a,pn	%g1, 2f		
	 andcc		%o2, 2, %g0
	EX_LD(LOAD(lduw, %o0, %o4))
5:	srl		%o4, 24, %g2
	srl		%o4, 16, %g3
	EX_ST(STORE(stb, %g2, %o1))
	srl		%o4, 8, %g2
	EX_ST(STORE(stb, %g3, %o1 + 1))
	add		%o0, 4, %o0
	EX_ST(STORE(stb, %g2, %o1 + 2))
	addcc		%o4, GLOBAL_SPARE, GLOBAL_SPARE
	EX_ST(STORE(stb, %o4, %o1 + 3))
	addc		GLOBAL_SPARE, %g0, GLOBAL_SPARE
	add		%o1, 4, %o1
	subcc		%g1, 1, %g1
	bne,a,pt	%icc, 5b
	 EX_LD(LOAD(lduw, %o0, %o4))
	sll		GLOBAL_SPARE, 16, %g2
	srl		GLOBAL_SPARE, 16, GLOBAL_SPARE
	srl		%g2, 16, %g2
	andcc		%o2, 2, %g0
	add		%g2, GLOBAL_SPARE, GLOBAL_SPARE 
2:	be,a,pt		%icc, 3f		
	 andcc		%o2, 1, %g0
	EX_LD(LOAD(lduh, %o0, %o4))
	andcc		%o2, 1, %g0
	srl		%o4, 8, %g2
	add		%o0, 2, %o0	
	EX_ST(STORE(stb, %g2, %o1))
	add		GLOBAL_SPARE, %o4, GLOBAL_SPARE
	EX_ST(STORE(stb, %o4, %o1 + 1))
	add		%o1, 2, %o1
3:	be,a,pt		%icc, 1f		
	 sll		GLOBAL_SPARE, 16, %o4
	EX_LD(LOAD(ldub, %o0, %g2))
	sll		%g2, 8, %o4	
	EX_ST(STORE(stb, %g2, %o1))
	add		GLOBAL_SPARE, %o4, GLOBAL_SPARE
	sll		GLOBAL_SPARE, 16, %o4
1:	addcc		%o4, GLOBAL_SPARE, GLOBAL_SPARE
	srl		GLOBAL_SPARE, 16, %o4
	addc		%g0, %o4, GLOBAL_SPARE
	brz,pt		%o5, 4f
	 srl		GLOBAL_SPARE, 8, %o4
	and		GLOBAL_SPARE, 0xff, %g2
	and		%o4, 0xff, %o4
	sll		%g2, 8, %g2
	or		%g2, %o4, GLOBAL_SPARE
4:	addcc		%o3, GLOBAL_SPARE, %o3
	addc		%g0, %o3, %o0
	retl
	 srl		%o0, 0, %o0
	.size		FUNC_NAME, .-FUNC_NAME
