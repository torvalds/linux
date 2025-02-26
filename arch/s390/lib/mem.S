/* SPDX-License-Identifier: GPL-2.0 */
/*
 * String handling functions.
 *
 * Copyright IBM Corp. 2012
 */

#include <linux/export.h>
#include <linux/linkage.h>
#include <asm/nospec-insn.h>

	GEN_BR_THUNK %r14

/*
 * void *memmove(void *dest, const void *src, size_t n)
 */
SYM_FUNC_START(__memmove)
	ltgr	%r4,%r4
	lgr	%r1,%r2
	jz	.Lmemmove_exit
	aghi	%r4,-1
	clgr	%r2,%r3
	jnh	.Lmemmove_forward
	la	%r5,1(%r4,%r3)
	clgr	%r2,%r5
	jl	.Lmemmove_reverse
.Lmemmove_forward:
	srlg	%r0,%r4,8
	ltgr	%r0,%r0
	jz	.Lmemmove_forward_remainder
.Lmemmove_forward_loop:
	mvc	0(256,%r1),0(%r3)
	la	%r1,256(%r1)
	la	%r3,256(%r3)
	brctg	%r0,.Lmemmove_forward_loop
.Lmemmove_forward_remainder:
	exrl	%r4,.Lmemmove_mvc
.Lmemmove_exit:
	BR_EX	%r14
.Lmemmove_reverse:
	ic	%r0,0(%r4,%r3)
	stc	%r0,0(%r4,%r1)
	brctg	%r4,.Lmemmove_reverse
	ic	%r0,0(%r4,%r3)
	stc	%r0,0(%r4,%r1)
	BR_EX	%r14
.Lmemmove_mvc:
	mvc	0(1,%r1),0(%r3)
SYM_FUNC_END(__memmove)
EXPORT_SYMBOL(__memmove)

SYM_FUNC_ALIAS(memmove, __memmove)
EXPORT_SYMBOL(memmove)

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
SYM_FUNC_START(__memset)
	ltgr	%r4,%r4
	jz	.Lmemset_exit
	ltgr	%r3,%r3
	jnz	.Lmemset_fill
	aghi	%r4,-1
	srlg	%r3,%r4,8
	ltgr	%r3,%r3
	lgr	%r1,%r2
	jz	.Lmemset_clear_remainder
.Lmemset_clear_loop:
	xc	0(256,%r1),0(%r1)
	la	%r1,256(%r1)
	brctg	%r3,.Lmemset_clear_loop
.Lmemset_clear_remainder:
	exrl	%r4,.Lmemset_xc
.Lmemset_exit:
	BR_EX	%r14
.Lmemset_fill:
	cghi	%r4,1
	lgr	%r1,%r2
	je	.Lmemset_fill_exit
	aghi	%r4,-2
	srlg	%r5,%r4,8
	ltgr	%r5,%r5
	jz	.Lmemset_fill_remainder
.Lmemset_fill_loop:
	stc	%r3,0(%r1)
	mvc	1(255,%r1),0(%r1)
	la	%r1,256(%r1)
	brctg	%r5,.Lmemset_fill_loop
.Lmemset_fill_remainder:
	stc	%r3,0(%r1)
	exrl	%r4,.Lmemset_mvc
	BR_EX	%r14
.Lmemset_fill_exit:
	stc	%r3,0(%r1)
	BR_EX	%r14
.Lmemset_xc:
	xc	0(1,%r1),0(%r1)
.Lmemset_mvc:
	mvc	1(1,%r1),0(%r1)
SYM_FUNC_END(__memset)
EXPORT_SYMBOL(__memset)

SYM_FUNC_ALIAS(memset, __memset)
EXPORT_SYMBOL(memset)

/*
 * memcpy implementation
 *
 * void *memcpy(void *dest, const void *src, size_t n)
 */
SYM_FUNC_START(__memcpy)
	ltgr	%r4,%r4
	jz	.Lmemcpy_exit
	aghi	%r4,-1
	srlg	%r5,%r4,8
	ltgr	%r5,%r5
	lgr	%r1,%r2
	jnz	.Lmemcpy_loop
.Lmemcpy_remainder:
	exrl	%r4,.Lmemcpy_mvc
.Lmemcpy_exit:
	BR_EX	%r14
.Lmemcpy_loop:
	mvc	0(256,%r1),0(%r3)
	la	%r1,256(%r1)
	la	%r3,256(%r3)
	brctg	%r5,.Lmemcpy_loop
	j	.Lmemcpy_remainder
.Lmemcpy_mvc:
	mvc	0(1,%r1),0(%r3)
SYM_FUNC_END(__memcpy)
EXPORT_SYMBOL(__memcpy)

SYM_FUNC_ALIAS(memcpy, __memcpy)
EXPORT_SYMBOL(memcpy)

/*
 * __memset16/32/64
 *
 * void *__memset16(uint16_t *s, uint16_t v, size_t count)
 * void *__memset32(uint32_t *s, uint32_t v, size_t count)
 * void *__memset64(uint64_t *s, uint64_t v, size_t count)
 */
.macro __MEMSET bits,bytes,insn
SYM_FUNC_START(__memset\bits)
	ltgr	%r4,%r4
	jz	.L__memset_exit\bits
	cghi	%r4,\bytes
	je	.L__memset_store\bits
	aghi	%r4,-(\bytes+1)
	srlg	%r5,%r4,8
	ltgr	%r5,%r5
	lgr	%r1,%r2
	jz	.L__memset_remainder\bits
.L__memset_loop\bits:
	\insn	%r3,0(%r1)
	mvc	\bytes(256-\bytes,%r1),0(%r1)
	la	%r1,256(%r1)
	brctg	%r5,.L__memset_loop\bits
.L__memset_remainder\bits:
	\insn	%r3,0(%r1)
	exrl	%r4,.L__memset_mvc\bits
	BR_EX	%r14
.L__memset_store\bits:
	\insn	%r3,0(%r2)
.L__memset_exit\bits:
	BR_EX	%r14
.L__memset_mvc\bits:
	mvc	\bytes(1,%r1),0(%r1)
SYM_FUNC_END(__memset\bits)
.endm

__MEMSET 16,2,sth
EXPORT_SYMBOL(__memset16)

__MEMSET 32,4,st
EXPORT_SYMBOL(__memset32)

__MEMSET 64,8,stg
EXPORT_SYMBOL(__memset64)
