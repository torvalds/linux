/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 1996, 1999 by Ralf Baechle
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
 * pointer which wraps into the kernel space.  We don't deal with that.  If
 * it happens at most some bytes of the exceptions handlers will be copied.
 */

LEAF(__strncpy_from_user_asm)
	LONG_L		v0, TI_ADDR_LIMIT($28)	# pointer ok?
	and		v0, a1
	bnez		v0, fault

FEXPORT(__strncpy_from_user_nocheck_asm)
	move		v0, zero
	move		v1, a1
	.set		noreorder
1:	EX(lbu, t0, (v1), fault)
	PTR_ADDIU	v1, 1
	beqz		t0, 2f
	 sb		t0, (a0)
	PTR_ADDIU	v0, 1
	bne		v0, a2, 1b
	 PTR_ADDIU	a0, 1
	.set		reorder
2:	PTR_ADDU	t0, a1, v0
	xor		t0, a1
	bltz		t0, fault
	jr		ra			# return n
	END(__strncpy_from_user_asm)

fault:	li		v0, -EFAULT
	jr		ra

	.section	__ex_table,"a"
	PTR		1b, fault
	.previous
