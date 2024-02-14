/* SPDX-License-Identifier: GPL-2.0 */
/* linux/arch/sparc/lib/memset.S: Sparc optimized memset, bzero and clear_user code
 * Copyright (C) 1991,1996 Free Software Foundation
 * Copyright (C) 1996,1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 *
 * Calls to memset returns initial %o0. Calls to bzero returns 0, if ok, and
 * number of bytes not yet set if exception occurs and we were called as
 * clear_user.
 */

#include <linux/export.h>
#include <asm/ptrace.h>

/* Work around cpp -rob */
#define ALLOC #alloc
#define EXECINSTR #execinstr
#define EX(x,y,a,b) 				\
98: 	x,y;					\
	.section .fixup,ALLOC,EXECINSTR;	\
	.align	4;				\
99:	retl;					\
	 a, b, %o0;				\
	.section __ex_table,ALLOC;		\
	.align	4;				\
	.word	98b, 99b;			\
	.text;					\
	.align	4

#define STORE(source, base, offset, n)		\
98: 	std source, [base + offset + n];	\
	.section .fixup,ALLOC,EXECINSTR;	\
	.align	4;				\
99:	ba 30f;					\
	 sub %o3, n - offset, %o3;		\
	.section __ex_table,ALLOC;		\
	.align	4;				\
	.word	98b, 99b;			\
	.text;					\
	.align	4;

#define STORE_LAST(source, base, offset, n)	\
	EX(std source, [base - offset - n],	\
	   add %o1, offset + n);

/* Please don't change these macros, unless you change the logic
 * in the .fixup section below as well.
 * Store 64 bytes at (BASE + OFFSET) using value SOURCE. */
#define ZERO_BIG_BLOCK(base, offset, source)	\
	STORE(source, base, offset, 0x00);	\
	STORE(source, base, offset, 0x08);	\
	STORE(source, base, offset, 0x10);	\
	STORE(source, base, offset, 0x18);	\
	STORE(source, base, offset, 0x20);	\
	STORE(source, base, offset, 0x28);	\
	STORE(source, base, offset, 0x30);	\
	STORE(source, base, offset, 0x38);

#define ZERO_LAST_BLOCKS(base, offset, source)	\
	STORE_LAST(source, base, offset, 0x38);	\
	STORE_LAST(source, base, offset, 0x30); \
	STORE_LAST(source, base, offset, 0x28);	\
	STORE_LAST(source, base, offset, 0x20);	\
	STORE_LAST(source, base, offset, 0x18);	\
	STORE_LAST(source, base, offset, 0x10);	\
	STORE_LAST(source, base, offset, 0x08);	\
	STORE_LAST(source, base, offset, 0x00);

	.text
	.align 4

        .globl  __bzero_begin
__bzero_begin:

	.globl	__bzero
	.type	__bzero,#function
	.globl	memset
	EXPORT_SYMBOL(__bzero)
	EXPORT_SYMBOL(memset)
memset:
	mov	%o0, %g1
	mov	1, %g4
	and	%o1, 0xff, %g3
	sll	%g3, 8, %g2
	or	%g3, %g2, %g3
	sll	%g3, 16, %g2
	or	%g3, %g2, %g3
	b	1f
	 mov	%o2, %o1
3:
	cmp	%o2, 3
	be	2f
	 EX(stb	%g3, [%o0], sub %o1, 0)

	cmp	%o2, 2
	be	2f
	 EX(stb	%g3, [%o0 + 0x01], sub %o1, 1)

	EX(stb	%g3, [%o0 + 0x02], sub %o1, 2)
2:
	sub	%o2, 4, %o2
	add	%o1, %o2, %o1
	b	4f
	 sub	%o0, %o2, %o0

__bzero:
	clr	%g4
	mov	%g0, %g3
1:
	cmp	%o1, 7
	bleu	7f
	 andcc	%o0, 3, %o2

	bne	3b
4:
	 andcc	%o0, 4, %g0

	be	2f
	 mov	%g3, %g2

	EX(st	%g3, [%o0], sub %o1, 0)
	sub	%o1, 4, %o1
	add	%o0, 4, %o0
2:
	andcc	%o1, 0xffffff80, %o3	! Now everything is 8 aligned and o1 is len to run
	be	9f
	 andcc	%o1, 0x78, %o2
10:
	ZERO_BIG_BLOCK(%o0, 0x00, %g2)
	subcc	%o3, 128, %o3
	ZERO_BIG_BLOCK(%o0, 0x40, %g2)
	bne	10b
	 add	%o0, 128, %o0

	orcc	%o2, %g0, %g0
9:
	be	13f
	 andcc	%o1, 7, %o1

	srl	%o2, 1, %o3
	set	13f, %o4
	sub	%o4, %o3, %o4
	jmp	%o4
	 add	%o0, %o2, %o0

	ZERO_LAST_BLOCKS(%o0, 0x48, %g2)
	ZERO_LAST_BLOCKS(%o0, 0x08, %g2)
13:
	be	8f
	 andcc	%o1, 4, %g0

	be	1f
	 andcc	%o1, 2, %g0

	EX(st	%g3, [%o0], and %o1, 7)
	add	%o0, 4, %o0
1:
	be	1f
	 andcc	%o1, 1, %g0

	EX(sth	%g3, [%o0], and %o1, 3)
	add	%o0, 2, %o0
1:
	bne,a	8f
	 EX(stb	%g3, [%o0], and %o1, 1)
8:
	b	0f
	 nop
7:
	be	13b
	 orcc	%o1, 0, %g0

	be	0f
8:
	 add	%o0, 1, %o0
	subcc	%o1, 1, %o1
	bne	8b
	 EX(stb	%g3, [%o0 - 1], add %o1, 1)
0:
	andcc	%g4, 1, %g0
	be	5f
	 nop
	retl
	 mov	%g1, %o0
5:
	retl
	 clr	%o0

	.section .fixup,#alloc,#execinstr
	.align	4
30:
	and	%o1, 0x7f, %o1
	retl
	 add	%o3, %o1, %o0

	.globl __bzero_end
__bzero_end:
