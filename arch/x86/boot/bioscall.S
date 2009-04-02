/* -----------------------------------------------------------------------
 *
 *   Copyright 2009 Intel Corporation; author H. Peter Anvin
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2 or (at your
 *   option) any later version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */

/*
 * "Glove box" for BIOS calls.  Avoids the constant problems with BIOSes
 * touching memory they shouldn't be.
 */

	.code16
	.text
	.globl	intcall
	.type	intcall, @function
intcall:
	/* Self-modify the INT instruction.  Ugly, but works. */
	cmpb	%al, 3f
	je	1f
	movb	%al, 3f
	jmp	1f		/* Synchronize pipeline */
1:
	/* Save state */
	pushfl
	pushw	%fs
	pushw	%gs
	pushal

	/* Copy input state to stack frame */
	subw	$44, %sp
	movw	%dx, %si
	movw	%sp, %di
	movw	$11, %cx
	rep; movsd

	/* Pop full state from the stack */
	popal
	popw	%gs
	popw	%fs
	popw	%es
	popw	%ds
	popfl

	/* Actual INT */
	.byte	0xcd		/* INT opcode */
3:	.byte	0

	/* Push full state to the stack */
	pushfl
	pushw	%ds
	pushw	%es
	pushw	%fs
	pushw	%gs
	pushal

	/* Re-establish C environment invariants */
	cld
	movzwl	%sp, %esp
	movw	%cs, %ax
	movw	%ax, %ds
	movw	%ax, %es

	/* Copy output state from stack frame */
	movw	68(%esp), %di	/* Original %cx == 3rd argument */
	andw	%di, %di
	jz	4f
	movw	%sp, %si
	movw	$11, %cx
	rep; movsd
4:	addw	$44, %sp

	/* Restore state and return */
	popal
	popw	%gs
	popw	%fs
	popfl
	retl
	.size	intcall, .-intcall
