/*
 * String handling functions.
 *
 * Copyright IBM Corp. 2012
 */

#include <linux/linkage.h>

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
	basr	%r5,%r0
.Lmemset_base:
	ltr	%r4,%r4
	bzr	%r14
	ltr	%r3,%r3
	jnz	.Lmemset_fill
	ahi	%r4,-1
	lr	%r3,%r4
	srl	%r3,8
	ltr	%r3,%r3
	lr	%r1,%r2
	je	.Lmemset_clear_rest
.Lmemset_clear_loop:
	xc	0(256,%r1),0(%r1)
	la	%r1,256(%r1)
	brct	%r3,.Lmemset_clear_loop
.Lmemset_clear_rest:
	ex	%r4,.Lmemset_xc-.Lmemset_base(%r5)
	br	%r14
.Lmemset_fill:
	stc	%r3,0(%r2)
	chi	%r4,1
	lr	%r1,%r2
	ber	%r14
	ahi	%r4,-2
	lr	%r3,%r4
	srl	%r3,8
	ltr	%r3,%r3
	je	.Lmemset_fill_rest
.Lmemset_fill_loop:
	mvc	1(256,%r1),0(%r1)
	la	%r1,256(%r1)
	brct	%r3,.Lmemset_fill_loop
.Lmemset_fill_rest:
	ex	%r4,.Lmemset_mvc-.Lmemset_base(%r5)
	br	%r14
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
	basr	%r5,%r0
.Lmemcpy_base:
	ltr	%r4,%r4
	bzr	%r14
	ahi	%r4,-1
	lr	%r0,%r4
	srl	%r0,8
	ltr	%r0,%r0
	lr	%r1,%r2
	jnz	.Lmemcpy_loop
.Lmemcpy_rest:
	ex	%r4,.Lmemcpy_mvc-.Lmemcpy_base(%r5)
	br	%r14
.Lmemcpy_loop:
	mvc	0(256,%r1),0(%r3)
	la	%r1,256(%r1)
	la	%r3,256(%r3)
	brct	%r0,.Lmemcpy_loop
	j	.Lmemcpy_rest
.Lmemcpy_mvc:
	mvc	0(1,%r1),0(%r3)
