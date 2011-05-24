/*
 * linux/arch/unicore32/lib/findbit.S
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/linkage.h>
#include <asm/assembler.h>
                .text

/*
 * Purpose  : Find a 'zero' bit
 * Prototype: int find_first_zero_bit(void *addr, unsigned int maxbit);
 */
__uc32_find_first_zero_bit:
		cxor.a	r1, #0
		beq	3f
		mov	r2, #0
1:		ldb	r3, [r0+], r2 >> #3
		xor.a	r3, r3, #0xff		@ invert bits
		bne	.L_found		@ any now set - found zero bit
		add	r2, r2, #8		@ next bit pointer
2:		csub.a	r2, r1			@ any more?
		bub	1b
3:		mov	r0, r1			@ no free bits
		mov	pc, lr

/*
 * Purpose  : Find next 'zero' bit
 * Prototype: int find_next_zero_bit
 *		(void *addr, unsigned int maxbit, int offset)
 */
ENTRY(__uc32_find_next_zero_bit)
		cxor.a	r1, #0
		beq	3b
		and.a	ip, r2, #7
		beq	1b			@ If new byte, goto old routine
		ldb	r3, [r0+], r2 >> #3
		xor	r3, r3, #0xff		@ now looking for a 1 bit
		mov.a	r3, r3 >> ip		@ shift off unused bits
		bne	.L_found
		or	r2, r2, #7		@ if zero, then no bits here
		add	r2, r2, #1		@ align bit pointer
		b	2b			@ loop for next bit
ENDPROC(__uc32_find_next_zero_bit)

/*
 * Purpose  : Find a 'one' bit
 * Prototype: int find_first_bit
 *		(const unsigned long *addr, unsigned int maxbit);
 */
__uc32_find_first_bit:
		cxor.a	r1, #0
		beq	3f
		mov	r2, #0
1:		ldb	r3, [r0+], r2 >> #3
		mov.a	r3, r3
		bne	.L_found		@ any now set - found zero bit
		add	r2, r2, #8		@ next bit pointer
2:		csub.a	r2, r1			@ any more?
		bub	1b
3:		mov	r0, r1			@ no free bits
		mov	pc, lr

/*
 * Purpose  : Find next 'one' bit
 * Prototype: int find_next_zero_bit
 *		(void *addr, unsigned int maxbit, int offset)
 */
ENTRY(__uc32_find_next_bit)
		cxor.a	r1, #0
		beq	3b
		and.a	ip, r2, #7
		beq	1b			@ If new byte, goto old routine
		ldb	r3, [r0+], r2 >> #3
		mov.a	r3, r3 >> ip		@ shift off unused bits
		bne	.L_found
		or	r2, r2, #7		@ if zero, then no bits here
		add	r2, r2, #1		@ align bit pointer
		b	2b			@ loop for next bit
ENDPROC(__uc32_find_next_bit)

/*
 * One or more bits in the LSB of r3 are assumed to be set.
 */
.L_found:
		rsub	r1, r3, #0
		and	r3, r3, r1
		cntlz	r3, r3
		rsub	r3, r3, #31
		add	r0, r2, r3
		mov	pc, lr

