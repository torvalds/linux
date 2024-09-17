/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Signal trampolines for 32 bit processes.
 *
 * Copyright (C) 2006 Randolph Chung <tausq@debian.org>
 * Copyright (C) 2018-2022 Helge Deller <deller@gmx.de>
 * Copyright (C) 2022 John David Anglin <dave.anglin@bell.net>
 */
#include <asm/unistd.h>
#include <linux/linkage.h>
#include <generated/asm-offsets.h>

	.text

/* Gdb expects the trampoline is on the stack and the pc is offset from
   a 64-byte boundary by 0, 4 or 5 instructions. Since the vdso trampoline
   is not on the stack, we need a new variant with different offsets and
   data to tell gdb where to find the signal context on the stack.

   Here we put the offset to the context data at the start of the trampoline
   region and offset the first trampoline by 2 instructions. Please do
   not change the trampoline as the code in gdb depends on the following
   instruction sequence exactly.
 */
	.align 64
	.word SIGFRAME_CONTEXT_REGS32

/* The nop here is a hack. The dwarf2 unwind routines subtract 1 from
   the return address to get an address in the middle of the presumed
   call instruction. Since we don't have a call here, we artifically
   extend the range covered by the unwind info by adding a nop before
   the real start.
 */
	nop

	.globl __kernel_sigtramp_rt
	.type __kernel_sigtramp_rt, @function
__kernel_sigtramp_rt:
	.proc
	.callinfo FRAME=ASM_SIGFRAME_SIZE32,CALLS,SAVE_RP
	.entry

.Lsigrt_start = . - 4
0:	ldi	0, %r25			/* (in_syscall=0) */
	ldi  __NR_rt_sigreturn, %r20
	ble  0x100(%sr2, %r0)
	nop

1:	ldi	1, %r25			/* (in_syscall=1) */
	ldi  __NR_rt_sigreturn, %r20
	ble  0x100(%sr2, %r0)
	nop
.Lsigrt_end:
	.exit
	.procend
	.size __kernel_sigtramp_rt,.-__kernel_sigtramp_rt


	.section .eh_frame,"a",@progbits

/* This is where the mcontext_t struct can be found on the stack.  */
#define PTREGS SIGFRAME_CONTEXT_REGS32	/* 32-bit process offset is -672 */

/* Register REGNO can be found at offset OFS of the mcontext_t structure. */
	.macro rsave regno,ofs
	.byte 0x05		/* DW_CFA_offset_extended */
	.uleb128 \regno;	/*   regno */
	.uleb128 \ofs		/*   factored offset */
	.endm

.Lcie:
	.long .Lcie_end - .Lcie_start
.Lcie_start:
	.long 0			/* CIE ID */
	.byte 1			/* Version number */
	.stringz "zRS"		/* NUL-terminated augmentation string */
	.uleb128 4		/* Code alignment factor */
	.sleb128 4		/* Data alignment factor */
	.byte 89		/* Return address register column, iaoq[0] */
	.uleb128 1		/* Augmentation value length */
	.byte 0x1b		/* DW_EH_PE_pcrel | DW_EH_PE_sdata4. */
	.byte 0x0f		/* DW_CFA_def_cfa_expresion */
	.uleb128 9f - 1f	/*   length */
1:
	.byte 0x8e		/*   DW_OP_breg30 */
	.sleb128 PTREGS
9:
	.balign 4
.Lcie_end:

	.long .Lfde0_end - .Lfde0_start
.Lfde0_start:
	.long .Lfde0_start - .Lcie	/* CIE pointer. */
	.long .Lsigrt_start - .		/* PC start, length */
	.long .Lsigrt_end - .Lsigrt_start
	.uleb128 0			/* Augmentation */

	/* General registers */
	rsave  1,  2
	rsave  2,  3
	rsave  3,  4
	rsave  4,  5
	rsave  5,  6
	rsave  6,  7
	rsave  7,  8
	rsave  8,  9
	rsave  9, 10
	rsave 10, 11
	rsave 11, 12
	rsave 12, 13
	rsave 13, 14
	rsave 14, 15
	rsave 15, 16
	rsave 16, 17
	rsave 17, 18
	rsave 18, 19
	rsave 19, 20
	rsave 20, 21
	rsave 21, 22
	rsave 22, 23
	rsave 23, 24
	rsave 24, 25
	rsave 25, 26
	rsave 26, 27
	rsave 27, 28
	rsave 28, 29
	rsave 29, 30
	rsave 30, 31
	rsave 31, 32

	/* Floating-point registers */
	rsave 32, 42
	rsave 33, 43
	rsave 34, 44
	rsave 35, 45
	rsave 36, 46
	rsave 37, 47
	rsave 38, 48
	rsave 39, 49
	rsave 40, 50
	rsave 41, 51
	rsave 42, 52
	rsave 43, 53
	rsave 44, 54
	rsave 45, 55
	rsave 46, 56
	rsave 47, 57
	rsave 48, 58
	rsave 49, 59
	rsave 50, 60
	rsave 51, 61
	rsave 52, 62
	rsave 53, 63
	rsave 54, 64
	rsave 55, 65
	rsave 56, 66
	rsave 57, 67
	rsave 58, 68
	rsave 59, 69
	rsave 60, 70
	rsave 61, 71
	rsave 62, 72
	rsave 63, 73
	rsave 64, 74
	rsave 65, 75
	rsave 66, 76
	rsave 67, 77
	rsave 68, 78
	rsave 69, 79
	rsave 70, 80
	rsave 71, 81
	rsave 72, 82
	rsave 73, 83
	rsave 74, 84
	rsave 75, 85
	rsave 76, 86
	rsave 77, 87
	rsave 78, 88
	rsave 79, 89
	rsave 80, 90
	rsave 81, 91
	rsave 82, 92
	rsave 83, 93
	rsave 84, 94
	rsave 85, 95
	rsave 86, 96
	rsave 87, 97

	/* SAR register */
	rsave 88, 102

	/* iaoq[0] return address register */
	rsave 89, 100
	.balign 4
.Lfde0_end:
