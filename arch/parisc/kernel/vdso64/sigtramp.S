/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Signal trampolines for 64 bit processes.
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
	.word SIGFRAME_CONTEXT_REGS

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
	.callinfo FRAME=ASM_SIGFRAME_SIZE,CALLS,SAVE_RP
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
#define PTREGS SIGFRAME_CONTEXT_REGS	/* 64-bit process offset is -720 */

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
	.sleb128 8		/* Data alignment factor */
	.byte 61		/* Return address register column, iaoq[0] */
	.uleb128 1		/* Augmentation value length */
	.byte 0x1b		/* DW_EH_PE_pcrel | DW_EH_PE_sdata4. */
	.byte 0x0f		/* DW_CFA_def_cfa_expresion */
	.uleb128 9f - 1f	/*   length */
1:
	.byte 0x8e		/*   DW_OP_breg30 */
	.sleb128 PTREGS
9:
	.balign 8
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
	rsave 32, 36
	rsave 33, 37
	rsave 34, 38
	rsave 35, 39
	rsave 36, 40
	rsave 37, 41
	rsave 38, 42
	rsave 39, 43
	rsave 40, 44
	rsave 41, 45
	rsave 42, 46
	rsave 43, 47
	rsave 44, 48
	rsave 45, 49
	rsave 46, 50
	rsave 47, 51
	rsave 48, 52
	rsave 49, 53
	rsave 50, 54
	rsave 51, 55
	rsave 52, 56
	rsave 53, 57
	rsave 54, 58
	rsave 55, 59
	rsave 56, 60
	rsave 57, 61
	rsave 58, 62
	rsave 59, 63

	/* SAR register */
	rsave 60, 67

	/* iaoq[0] return address register */
	rsave 61, 65
	.balign 8
.Lfde0_end:
