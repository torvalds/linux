/* SPDX-License-Identifier: GPL-2.0 */
/* copy_user.S: Sparc optimized copy_from_user and copy_to_user code.
 *
 *  Copyright(C) 1995 Linus Torvalds
 *  Copyright(C) 1996 David S. Miller
 *  Copyright(C) 1996 Eddie C. Dost
 *  Copyright(C) 1996,1998 Jakub Jelinek
 *
 * derived from:
 *	e-mail between David and Eddie.
 *
 * Returns 0 if successful, otherwise count of bytes not copied yet
 */

#include <asm/ptrace.h>
#include <asm/asmmacro.h>
#include <asm/page.h>
#include <asm/thread_info.h>
#include <asm/export.h>

/* Work around cpp -rob */
#define ALLOC #alloc
#define EXECINSTR #execinstr

#define EX_ENTRY(l1, l2)			\
	.section __ex_table,ALLOC;		\
	.align	4;				\
	.word	l1, l2;				\
	.text;

#define EX(x,y,a,b) 				\
98: 	x,y;					\
	.section .fixup,ALLOC,EXECINSTR;	\
	.align	4;				\
99:	retl;					\
	 a, b, %o0;				\
	EX_ENTRY(98b, 99b)

#define EX2(x,y,c,d,e,a,b) 			\
98: 	x,y;					\
	.section .fixup,ALLOC,EXECINSTR;	\
	.align	4;				\
99:	c, d, e;				\
	retl;					\
	 a, b, %o0;				\
	EX_ENTRY(98b, 99b)

#define EXO2(x,y) 				\
98: 	x, y;					\
	EX_ENTRY(98b, 97f)

#define LD(insn, src, offset, reg, label)	\
98:	insn [%src + (offset)], %reg;		\
	.section .fixup,ALLOC,EXECINSTR;	\
99:	ba	label;				\
	 mov	offset, %g5;			\
	EX_ENTRY(98b, 99b)

#define ST(insn, dst, offset, reg, label)	\
98:	insn %reg, [%dst + (offset)];		\
	.section .fixup,ALLOC,EXECINSTR;	\
99:	ba	label;				\
	 mov	offset, %g5;			\
	EX_ENTRY(98b, 99b)

/* Both these macros have to start with exactly the same insn */
/* left: g7 + (g1 % 128) - offset */
#define MOVE_BIGCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, t7) \
	LD(ldd, src, offset + 0x00, t0, bigchunk_fault)	\
	LD(ldd, src, offset + 0x08, t2, bigchunk_fault)	\
	LD(ldd, src, offset + 0x10, t4, bigchunk_fault)	\
	LD(ldd, src, offset + 0x18, t6, bigchunk_fault)	\
	ST(st, dst, offset + 0x00, t0, bigchunk_fault)	\
	ST(st, dst, offset + 0x04, t1, bigchunk_fault)	\
	ST(st, dst, offset + 0x08, t2, bigchunk_fault)	\
	ST(st, dst, offset + 0x0c, t3, bigchunk_fault)	\
	ST(st, dst, offset + 0x10, t4, bigchunk_fault)	\
	ST(st, dst, offset + 0x14, t5, bigchunk_fault)	\
	ST(st, dst, offset + 0x18, t6, bigchunk_fault)	\
	ST(st, dst, offset + 0x1c, t7, bigchunk_fault)

/* left: g7 + (g1 % 128) - offset */
#define MOVE_BIGALIGNCHUNK(src, dst, offset, t0, t1, t2, t3, t4, t5, t6, t7) \
	LD(ldd, src, offset + 0x00, t0, bigchunk_fault)	\
	LD(ldd, src, offset + 0x08, t2, bigchunk_fault)	\
	LD(ldd, src, offset + 0x10, t4, bigchunk_fault)	\
	LD(ldd, src, offset + 0x18, t6, bigchunk_fault)	\
	ST(std, dst, offset + 0x00, t0, bigchunk_fault)	\
	ST(std, dst, offset + 0x08, t2, bigchunk_fault)	\
	ST(std, dst, offset + 0x10, t4, bigchunk_fault)	\
	ST(std, dst, offset + 0x18, t6, bigchunk_fault)

	.section .fixup,#alloc,#execinstr
bigchunk_fault:
	sub	%g7, %g5, %o0
	and	%g1, 127, %g1
	retl
	 add	%o0, %g1, %o0

/* left: offset + 16 + (g1 % 16) */
#define MOVE_LASTCHUNK(src, dst, offset, t0, t1, t2, t3) \
	LD(ldd, src, -(offset + 0x10), t0, lastchunk_fault)	\
	LD(ldd, src, -(offset + 0x08), t2, lastchunk_fault)	\
	ST(st, dst, -(offset + 0x10), t0, lastchunk_fault)	\
	ST(st, dst, -(offset + 0x0c), t1, lastchunk_fault)	\
	ST(st, dst, -(offset + 0x08), t2, lastchunk_fault)	\
	ST(st, dst, -(offset + 0x04), t3, lastchunk_fault)

	.section .fixup,#alloc,#execinstr
lastchunk_fault:
	and	%g1, 15, %g1
	retl
	 sub	%g1, %g5, %o0

/* left: o3 + (o2 % 16) - offset */
#define MOVE_HALFCHUNK(src, dst, offset, t0, t1, t2, t3) \
	LD(lduh, src, offset + 0x00, t0, halfchunk_fault)	\
	LD(lduh, src, offset + 0x02, t1, halfchunk_fault)	\
	LD(lduh, src, offset + 0x04, t2, halfchunk_fault)	\
	LD(lduh, src, offset + 0x06, t3, halfchunk_fault)	\
	ST(sth, dst, offset + 0x00, t0, halfchunk_fault)	\
	ST(sth, dst, offset + 0x02, t1, halfchunk_fault)	\
	ST(sth, dst, offset + 0x04, t2, halfchunk_fault)	\
	ST(sth, dst, offset + 0x06, t3, halfchunk_fault)

/* left: o3 + (o2 % 16) + offset + 2 */
#define MOVE_SHORTCHUNK(src, dst, offset, t0, t1) \
	LD(ldub, src, -(offset + 0x02), t0, halfchunk_fault)	\
	LD(ldub, src, -(offset + 0x01), t1, halfchunk_fault)	\
	ST(stb, dst, -(offset + 0x02), t0, halfchunk_fault)	\
	ST(stb, dst, -(offset + 0x01), t1, halfchunk_fault)

	.section .fixup,#alloc,#execinstr
halfchunk_fault:
	and	%o2, 15, %o2
	sub	%o3, %g5, %o3
	retl
	 add	%o2, %o3, %o0

/* left: offset + 2 + (o2 % 2) */
#define MOVE_LAST_SHORTCHUNK(src, dst, offset, t0, t1) \
	LD(ldub, src, -(offset + 0x02), t0, last_shortchunk_fault)	\
	LD(ldub, src, -(offset + 0x01), t1, last_shortchunk_fault)	\
	ST(stb, dst, -(offset + 0x02), t0, last_shortchunk_fault)	\
	ST(stb, dst, -(offset + 0x01), t1, last_shortchunk_fault)

	.section .fixup,#alloc,#execinstr
last_shortchunk_fault:
	and	%o2, 1, %o2
	retl
	 sub	%o2, %g5, %o0

	.text
	.align	4

	.globl  __copy_user_begin
__copy_user_begin:

	.globl	__copy_user
	EXPORT_SYMBOL(__copy_user)
dword_align:
	andcc	%o1, 1, %g0
	be	4f
	 andcc	%o1, 2, %g0

	EXO2(ldub [%o1], %g2)
	add	%o1, 1, %o1
	EXO2(stb %g2, [%o0])
	sub	%o2, 1, %o2
	bne	3f
	 add	%o0, 1, %o0

	EXO2(lduh [%o1], %g2)
	add	%o1, 2, %o1
	EXO2(sth %g2, [%o0])
	sub	%o2, 2, %o2
	b	3f
	 add	%o0, 2, %o0
4:
	EXO2(lduh [%o1], %g2)
	add	%o1, 2, %o1
	EXO2(sth %g2, [%o0])
	sub	%o2, 2, %o2
	b	3f
	 add	%o0, 2, %o0

__copy_user:	/* %o0=dst %o1=src %o2=len */
	xor	%o0, %o1, %o4
1:
	andcc	%o4, 3, %o5
2:
	bne	cannot_optimize
	 cmp	%o2, 15

	bleu	short_aligned_end
	 andcc	%o1, 3, %g0

	bne	dword_align
3:
	 andcc	%o1, 4, %g0

	be	2f
	 mov	%o2, %g1

	EXO2(ld [%o1], %o4)
	sub	%g1, 4, %g1
	EXO2(st %o4, [%o0])
	add	%o1, 4, %o1
	add	%o0, 4, %o0
2:
	andcc	%g1, 0xffffff80, %g7
	be	3f
	 andcc	%o0, 4, %g0

	be	ldd_std + 4
5:
	MOVE_BIGCHUNK(o1, o0, 0x00, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x20, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x40, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGCHUNK(o1, o0, 0x60, o2, o3, o4, o5, g2, g3, g4, g5)
	subcc	%g7, 128, %g7
	add	%o1, 128, %o1
	bne	5b
	 add	%o0, 128, %o0
3:
	andcc	%g1, 0x70, %g7
	be	copy_user_table_end
	 andcc	%g1, 8, %g0

	sethi	%hi(copy_user_table_end), %o5
	srl	%g7, 1, %o4
	add	%g7, %o4, %o4
	add	%o1, %g7, %o1
	sub	%o5, %o4, %o5
	jmpl	%o5 + %lo(copy_user_table_end), %g0
	 add	%o0, %g7, %o0

	MOVE_LASTCHUNK(o1, o0, 0x60, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x50, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x40, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x30, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x20, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x10, g2, g3, g4, g5)
	MOVE_LASTCHUNK(o1, o0, 0x00, g2, g3, g4, g5)
copy_user_table_end:
	be	copy_user_last7
	 andcc	%g1, 4, %g0

	EX(ldd	[%o1], %g2, and %g1, 0xf)
	add	%o0, 8, %o0
	add	%o1, 8, %o1
	EX(st	%g2, [%o0 - 0x08], and %g1, 0xf)
	EX2(st	%g3, [%o0 - 0x04], and %g1, 0xf, %g1, sub %g1, 4)
copy_user_last7:
	be	1f
	 andcc	%g1, 2, %g0

	EX(ld	[%o1], %g2, and %g1, 7)
	add	%o1, 4, %o1
	EX(st	%g2, [%o0], and %g1, 7)
	add	%o0, 4, %o0
1:
	be	1f
	 andcc	%g1, 1, %g0

	EX(lduh	[%o1], %g2, and %g1, 3)
	add	%o1, 2, %o1
	EX(sth	%g2, [%o0], and %g1, 3)
	add	%o0, 2, %o0
1:
	be	1f
	 nop

	EX(ldub	[%o1], %g2, add %g0, 1)
	EX(stb	%g2, [%o0], add %g0, 1)
1:
	retl
 	 clr	%o0

ldd_std:
	MOVE_BIGALIGNCHUNK(o1, o0, 0x00, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGALIGNCHUNK(o1, o0, 0x20, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGALIGNCHUNK(o1, o0, 0x40, o2, o3, o4, o5, g2, g3, g4, g5)
	MOVE_BIGALIGNCHUNK(o1, o0, 0x60, o2, o3, o4, o5, g2, g3, g4, g5)
	subcc	%g7, 128, %g7
	add	%o1, 128, %o1
	bne	ldd_std
	 add	%o0, 128, %o0

	andcc	%g1, 0x70, %g7
	be	copy_user_table_end
	 andcc	%g1, 8, %g0

	sethi	%hi(copy_user_table_end), %o5
	srl	%g7, 1, %o4
	add	%g7, %o4, %o4
	add	%o1, %g7, %o1
	sub	%o5, %o4, %o5
	jmpl	%o5 + %lo(copy_user_table_end), %g0
	 add	%o0, %g7, %o0

cannot_optimize:
	bleu	short_end
	 cmp	%o5, 2

	bne	byte_chunk
	 and	%o2, 0xfffffff0, %o3
	 
	andcc	%o1, 1, %g0
	be	10f
	 nop

	EXO2(ldub [%o1], %g2)
	add	%o1, 1, %o1
	EXO2(stb %g2, [%o0])
	sub	%o2, 1, %o2
	andcc	%o2, 0xfffffff0, %o3
	be	short_end
	 add	%o0, 1, %o0
10:
	MOVE_HALFCHUNK(o1, o0, 0x00, g2, g3, g4, g5)
	MOVE_HALFCHUNK(o1, o0, 0x08, g2, g3, g4, g5)
	subcc	%o3, 0x10, %o3
	add	%o1, 0x10, %o1
	bne	10b
	 add	%o0, 0x10, %o0
	b	2f
	 and	%o2, 0xe, %o3
	
byte_chunk:
	MOVE_SHORTCHUNK(o1, o0, -0x02, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x04, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x06, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x08, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x0a, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x0c, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x0e, g2, g3)
	MOVE_SHORTCHUNK(o1, o0, -0x10, g2, g3)
	subcc	%o3, 0x10, %o3
	add	%o1, 0x10, %o1
	bne	byte_chunk
	 add	%o0, 0x10, %o0

short_end:
	and	%o2, 0xe, %o3
2:
	sethi	%hi(short_table_end), %o5
	sll	%o3, 3, %o4
	add	%o0, %o3, %o0
	sub	%o5, %o4, %o5
	add	%o1, %o3, %o1
	jmpl	%o5 + %lo(short_table_end), %g0
	 andcc	%o2, 1, %g0
	MOVE_LAST_SHORTCHUNK(o1, o0, 0x0c, g2, g3)
	MOVE_LAST_SHORTCHUNK(o1, o0, 0x0a, g2, g3)
	MOVE_LAST_SHORTCHUNK(o1, o0, 0x08, g2, g3)
	MOVE_LAST_SHORTCHUNK(o1, o0, 0x06, g2, g3)
	MOVE_LAST_SHORTCHUNK(o1, o0, 0x04, g2, g3)
	MOVE_LAST_SHORTCHUNK(o1, o0, 0x02, g2, g3)
	MOVE_LAST_SHORTCHUNK(o1, o0, 0x00, g2, g3)
short_table_end:
	be	1f
	 nop
	EX(ldub	[%o1], %g2, add %g0, 1)
	EX(stb	%g2, [%o0], add %g0, 1)
1:
	retl
 	 clr	%o0

short_aligned_end:
	bne	short_end
	 andcc	%o2, 8, %g0

	be	1f
	 andcc	%o2, 4, %g0

	EXO2(ld	[%o1 + 0x00], %g2)
	EXO2(ld	[%o1 + 0x04], %g3)
	add	%o1, 8, %o1
	EXO2(st	%g2, [%o0 + 0x00])
	EX(st	%g3, [%o0 + 0x04], sub %o2, 4)
	add	%o0, 8, %o0
1:
	b	copy_user_last7
	 mov	%o2, %g1

	.section .fixup,#alloc,#execinstr
	.align	4
97:
	retl
	 mov	%o2, %o0

	.globl  __copy_user_end
__copy_user_end:
