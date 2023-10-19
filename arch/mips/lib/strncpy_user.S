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
#include <asm/export.h>
#include <asm/regdef.h>

#define EX(insn,reg,addr,handler)			\
9:	insn	reg, addr;				\
	.section __ex_table,"a";			\
	PTR_WD	9b, handler;				\
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

LEAF(__strncpy_from_user_asm)
	move		t0, zero
	move		v1, a1
#ifdef CONFIG_EVA
	.set push
	.set eva
1:	EX(lbue, v0, (v1), .Lfault)
	.set pop
#else
1:	EX(lbu, v0, (v1), .Lfault)
#endif
	PTR_ADDIU	v1, 1
	R10KCBARRIER(0(ra))
	sb		v0, (a0)
	beqz		v0, 2f
	PTR_ADDIU	t0, 1
	PTR_ADDIU	a0, 1
	bne		t0, a2, 1b
2:	PTR_ADDU	v0, a1, t0
	xor		v0, a1
	bltz		v0, .Lfault
	move		v0, t0
	jr		ra			# return n
	END(__strncpy_from_user_asm)

.Lfault:
	li		v0, -EFAULT
	jr		ra

	.section	__ex_table,"a"
	PTR_WD		1b, .Lfault
	.previous

	EXPORT_SYMBOL(__strncpy_from_user_asm)
