/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1999 by Ralf Baechle
 * Copyright (C) 2011 MIPS Technologies, Inc.
 */
#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/regdef.h>

#define EX(insn,reg,addr,handler)			\
9:	insn	reg, addr;				\
	.section __ex_table,"a";			\
	PTR	9b, handler;				\
	.previous

/*
 * Returns: -EFAULT if exception before terminator, N if the entire
 * buffer filled, else strlen.
 */

/*
 * Ugly special case have to check: we might get passed a user space
 * pointer which wraps into the kernel space.  We don't deal with that.	 If
 * it happens at most some bytes of the exceptions handlers will be copied.
 */

	.macro __BUILD_STRNCPY_ASM func
LEAF(__strncpy_from_\func\()_asm)
	LONG_L		v0, TI_ADDR_LIMIT($28)	# pointer ok?
	and		v0, a1
	bnez		v0, .Lfault\@

FEXPORT(__strncpy_from_\func\()_nocheck_asm)
	.set		noreorder
	move		t0, zero
	move		v1, a1
.ifeqs "\func","kernel"
1:	EX(lbu, v0, (v1), .Lfault\@)
.else
1:	EX(lbue, v0, (v1), .Lfault\@)
.endif
	PTR_ADDIU	v1, 1
	R10KCBARRIER(0(ra))
	beqz		v0, 2f
	 sb		v0, (a0)
	PTR_ADDIU	t0, 1
	bne		t0, a2, 1b
	 PTR_ADDIU	a0, 1
2:	PTR_ADDU	v0, a1, t0
	xor		v0, a1
	bltz		v0, .Lfault\@
	 nop
	jr		ra			# return n
	 move		v0, t0
	END(__strncpy_from_\func\()_asm)

.Lfault\@: jr		ra
	  li		v0, -EFAULT

	.section	__ex_table,"a"
	PTR		1b, .Lfault\@
	.previous

	.endm

#ifndef CONFIG_EVA
	/* Set aliases */
	.global __strncpy_from_user_asm
	.global __strncpy_from_user_nocheck_asm
	.set __strncpy_from_user_asm, __strncpy_from_kernel_asm
	.set __strncpy_from_user_nocheck_asm, __strncpy_from_kernel_nocheck_asm
#endif

__BUILD_STRNCPY_ASM kernel

#ifdef CONFIG_EVA
	.set push
	.set eva
__BUILD_STRNCPY_ASM user
	.set pop
#endif
