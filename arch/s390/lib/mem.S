/*
 * String handling functions.
 *
 * Copyright IBM Corp. 2012
 */

#include <linux/linkage.h>
#include <asm/nospec-insn.h>

	GEN_BR_THUNK %r14

/*
 * memset implementation
 *
 * This code corresponds to the C construct below. We do distinguish
 * between clearing (c == 0) and setting a memory array (c != 0) simply
 * because nearly all memset invocations in the kernel clear memory and
 * the xc instruction is preferred in such cases.
 *
 * void *memset(void *s, int c, size_t n)
 * {
 *	if (likely(c == 0))
 *		return __builtin_memset(s, 0, n);
 *	return __builtin_memset(s, c, n);
 * }
 */
ENTRY(memset)
	ltgr	%r4,%r4
	bzr	%r14
	ltgr	%r3,%r3
	jnz	.Lmemset_fill
	aghi	%r4,-1
	srlg	%r3,%r4,8
	ltgr	%r3,%r3
	lgr	%r1,%r2
	jz	.Lmemset_clear_rest
.Lmemset_clear_loop:
	xc	0(256,%r1),0(%r1)
	la	%r1,256(%r1)
	brctg	%r3,.Lmemset_clear_loop
.Lmemset_clear_rest:
	larl	%r3,.Lmemset_xc
	ex	%r4,0(%r3)
	BR_EX	%r14
.Lmemset_fill:
	stc	%r3,0(%r2)
	cghi	%r4,1
	lgr	%r1,%r2
	ber	%r14
	aghi	%r4,-2
	srlg	%r3,%r4,8
	ltgr	%r3,%r3
	jz	.Lmemset_fill_rest
.Lmemset_fill_loop:
	mvc	1(256,%r1),0(%r1)
	la	%r1,256(%r1)
	brctg	%r3,.Lmemset_fill_loop
.Lmemset_fill_rest:
	larl	%r3,.Lmemset_mvc
	ex	%r4,0(%r3)
	BR_EX	%r14
.Lmemset_xc:
	xc	0(1,%r1),0(%r1)
.Lmemset_mvc:
	mvc	1(1,%r1),0(%r1)

/*
 * memcpy implementation
 *
 * void *memcpy(void *dest, const void *src, size_t n)
 */
ENTRY(memcpy)
	ltgr	%r4,%r4
	bzr	%r14
	aghi	%r4,-1
	srlg	%r5,%r4,8
	ltgr	%r5,%r5
	lgr	%r1,%r2
	jnz	.Lmemcpy_loop
.Lmemcpy_rest:
	larl	%r5,.Lmemcpy_mvc
	ex	%r4,0(%r5)
	BR_EX	%r14
.Lmemcpy_loop:
	mvc	0(256,%r1),0(%r3)
	la	%r1,256(%r1)
	la	%r3,256(%r3)
	brctg	%r5,.Lmemcpy_loop
	j	.Lmemcpy_rest
.Lmemcpy_mvc:
	mvc	0(1,%r1),0(%r3)
