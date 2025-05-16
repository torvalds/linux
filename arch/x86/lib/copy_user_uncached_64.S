/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2023 Linus Torvalds <torvalds@linux-foundation.org>
 */

#include <linux/export.h>
#include <linux/linkage.h>
#include <linux/objtool.h>
#include <asm/asm.h>

/*
 * copy_user_nocache - Uncached memory copy with exception handling
 *
 * This copies from user space into kernel space, but the kernel
 * space accesses can take a machine check exception, so they too
 * need exception handling.
 *
 * Note: only 32-bit and 64-bit stores have non-temporal versions,
 * and we only use aligned versions. Any unaligned parts at the
 * start or end of the copy will be done using normal cached stores.
 *
 * Input:
 * rdi destination
 * rsi source
 * edx count
 *
 * Output:
 * rax uncopied bytes or 0 if successful.
 */
SYM_FUNC_START(__copy_user_nocache)
	ANNOTATE_NOENDBR
	/* If destination is not 7-byte aligned, we'll have to align it */
	testb $7,%dil
	jne .Lalign

.Lis_aligned:
	cmp $64,%edx
	jb .Lquadwords

	.p2align 4,0x90
.Lunrolled:
10:	movq (%rsi),%r8
11:	movq 8(%rsi),%r9
12:	movq 16(%rsi),%r10
13:	movq 24(%rsi),%r11
20:	movnti %r8,(%rdi)
21:	movnti %r9,8(%rdi)
22:	movnti %r10,16(%rdi)
23:	movnti %r11,24(%rdi)
30:	movq 32(%rsi),%r8
31:	movq 40(%rsi),%r9
32:	movq 48(%rsi),%r10
33:	movq 56(%rsi),%r11
40:	movnti %r8,32(%rdi)
41:	movnti %r9,40(%rdi)
42:	movnti %r10,48(%rdi)
43:	movnti %r11,56(%rdi)

	addq $64,%rsi
	addq $64,%rdi
	sub $64,%edx
	cmp $64,%edx
	jae .Lunrolled

/*
 * First set of user mode loads have been done
 * without any stores, so if they fail, we can
 * just try the non-unrolled loop.
 */
_ASM_EXTABLE_UA(10b, .Lquadwords)
_ASM_EXTABLE_UA(11b, .Lquadwords)
_ASM_EXTABLE_UA(12b, .Lquadwords)
_ASM_EXTABLE_UA(13b, .Lquadwords)

/*
 * The second set of user mode loads have been
 * done with 32 bytes stored to the destination,
 * so we need to take that into account before
 * falling back to the unrolled loop.
 */
_ASM_EXTABLE_UA(30b, .Lfixup32)
_ASM_EXTABLE_UA(31b, .Lfixup32)
_ASM_EXTABLE_UA(32b, .Lfixup32)
_ASM_EXTABLE_UA(33b, .Lfixup32)

/*
 * An exception on a write means that we're
 * done, but we need to update the count
 * depending on where in the unrolled loop
 * we were.
 */
_ASM_EXTABLE_UA(20b, .Ldone0)
_ASM_EXTABLE_UA(21b, .Ldone8)
_ASM_EXTABLE_UA(22b, .Ldone16)
_ASM_EXTABLE_UA(23b, .Ldone24)
_ASM_EXTABLE_UA(40b, .Ldone32)
_ASM_EXTABLE_UA(41b, .Ldone40)
_ASM_EXTABLE_UA(42b, .Ldone48)
_ASM_EXTABLE_UA(43b, .Ldone56)

.Lquadwords:
	cmp $8,%edx
	jb .Llong
50:	movq (%rsi),%rax
51:	movnti %rax,(%rdi)
	addq $8,%rsi
	addq $8,%rdi
	sub $8,%edx
	jmp .Lquadwords

/*
 * If we fail on the last full quadword, we will
 * not try to do any byte-wise cached accesses.
 * We will try to do one more 4-byte uncached
 * one, though.
 */
_ASM_EXTABLE_UA(50b, .Llast4)
_ASM_EXTABLE_UA(51b, .Ldone0)

.Llong:
	test $4,%dl
	je .Lword
60:	movl (%rsi),%eax
61:	movnti %eax,(%rdi)
	addq $4,%rsi
	addq $4,%rdi
	sub $4,%edx
.Lword:
	sfence
	test $2,%dl
	je .Lbyte
70:	movw (%rsi),%ax
71:	movw %ax,(%rdi)
	addq $2,%rsi
	addq $2,%rdi
	sub $2,%edx
.Lbyte:
	test $1,%dl
	je .Ldone
80:	movb (%rsi),%al
81:	movb %al,(%rdi)
	dec %edx
.Ldone:
	mov %edx,%eax
	RET

/*
 * If we fail on the last four bytes, we won't
 * bother with any fixups. It's dead, Jim. Note
 * that there's no need for 'sfence' for any
 * of this, since the exception will have been
 * serializing.
 */
_ASM_EXTABLE_UA(60b, .Ldone)
_ASM_EXTABLE_UA(61b, .Ldone)
_ASM_EXTABLE_UA(70b, .Ldone)
_ASM_EXTABLE_UA(71b, .Ldone)
_ASM_EXTABLE_UA(80b, .Ldone)
_ASM_EXTABLE_UA(81b, .Ldone)

/*
 * This is the "head needs aliging" case when
 * the destination isn't 8-byte aligned. The
 * 4-byte case can be done uncached, but any
 * smaller alignment is done with regular stores.
 */
.Lalign:
	test $1,%dil
	je .Lalign_word
	test %edx,%edx
	je .Ldone
90:	movb (%rsi),%al
91:	movb %al,(%rdi)
	inc %rsi
	inc %rdi
	dec %edx
.Lalign_word:
	test $2,%dil
	je .Lalign_long
	cmp $2,%edx
	jb .Lbyte
92:	movw (%rsi),%ax
93:	movw %ax,(%rdi)
	addq $2,%rsi
	addq $2,%rdi
	sub $2,%edx
.Lalign_long:
	test $4,%dil
	je .Lis_aligned
	cmp $4,%edx
	jb .Lword
94:	movl (%rsi),%eax
95:	movnti %eax,(%rdi)
	addq $4,%rsi
	addq $4,%rdi
	sub $4,%edx
	jmp .Lis_aligned

/*
 * If we fail on the initial alignment accesses,
 * we're all done. Again, no point in trying to
 * do byte-by-byte probing if the 4-byte load
 * fails - we're not doing any uncached accesses
 * any more.
 */
_ASM_EXTABLE_UA(90b, .Ldone)
_ASM_EXTABLE_UA(91b, .Ldone)
_ASM_EXTABLE_UA(92b, .Ldone)
_ASM_EXTABLE_UA(93b, .Ldone)
_ASM_EXTABLE_UA(94b, .Ldone)
_ASM_EXTABLE_UA(95b, .Ldone)

/*
 * Exception table fixups for faults in the middle
 */
.Ldone56: sub $8,%edx
.Ldone48: sub $8,%edx
.Ldone40: sub $8,%edx
.Ldone32: sub $8,%edx
.Ldone24: sub $8,%edx
.Ldone16: sub $8,%edx
.Ldone8: sub $8,%edx
.Ldone0:
	mov %edx,%eax
	RET

.Lfixup32:
	addq $32,%rsi
	addq $32,%rdi
	sub $32,%edx
	jmp .Lquadwords

.Llast4:
52:	movl (%rsi),%eax
53:	movnti %eax,(%rdi)
	sfence
	sub $4,%edx
	mov %edx,%eax
	RET
_ASM_EXTABLE_UA(52b, .Ldone0)
_ASM_EXTABLE_UA(53b, .Ldone0)

SYM_FUNC_END(__copy_user_nocache)
EXPORT_SYMBOL(__copy_user_nocache)
