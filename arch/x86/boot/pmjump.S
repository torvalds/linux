/* ----------------------------------------------------------------------- *
 *
 *   Copyright (C) 1991, 1992 Linus Torvalds
 *   Copyright 2007 rPath, Inc. - All Rights Reserved
 *
 *   This file is part of the Linux kernel, and is made available under
 *   the terms of the GNU General Public License version 2.
 *
 * ----------------------------------------------------------------------- */

/*
 * arch/i386/boot/pmjump.S
 *
 * The actual transition into protected mode
 */

#include <asm/boot.h>
#include <asm/segment.h>

	.text

	.globl	protected_mode_jump
	.type	protected_mode_jump, @function

	.code16

/*
 * void protected_mode_jump(u32 entrypoint, u32 bootparams);
 */
protected_mode_jump:
	movl	%edx, %esi		# Pointer to boot_params table
	movl	%eax, 2f		# Patch ljmpl instruction

	movw	$__BOOT_DS, %cx
	xorl	%ebx, %ebx		# Per the 32-bit boot protocol
	xorl	%ebp, %ebp		# Per the 32-bit boot protocol
	xorl	%edi, %edi		# Per the 32-bit boot protocol

	movl	%cr0, %edx
	orb	$1, %dl			# Protected mode (PE) bit
	movl	%edx, %cr0
	jmp	1f			# Short jump to serialize on 386/486
1:

	movw	%cx, %ds
	movw	%cx, %es
	movw	%cx, %fs
	movw	%cx, %gs
	movw	%cx, %ss

	# Jump to the 32-bit entrypoint
	.byte	0x66, 0xea		# ljmpl opcode
2:	.long	0			# offset
	.word	__BOOT_CS		# segment

	.size	protected_mode_jump, .-protected_mode_jump
