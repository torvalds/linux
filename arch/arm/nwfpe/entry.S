/*
    NetWinder Floating Point Emulator
    (c) Rebel.COM, 1998
    (c) 1998, 1999 Philip Blundell

    Direct questions, comments to Scott Bambrough <scottb@netwinder.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/
#include <asm/assembler.h>
#include <asm/opcodes.h>

/* This is the kernel's entry point into the floating point emulator.
It is called from the kernel with code similar to this:

	sub	r4, r5, #4
	ldrt	r0, [r4]			@ r0  = instruction
	adrsvc	al, r9, ret_from_exception	@ r9  = normal FP return
	adrsvc	al, lr, fpundefinstr		@ lr  = undefined instr return

	get_current_task r10
	mov	r8, #1
	strb	r8, [r10, #TSK_USED_MATH]	@ set current->used_math
	add	r10, r10, #TSS_FPESAVE		@ r10 = workspace
	ldr	r4, .LC2
	ldr	pc, [r4]			@ Call FP emulator entry point

The kernel expects the emulator to return via one of two possible
points of return it passes to the emulator.  The emulator, if
successful in its emulation, jumps to ret_from_exception (passed in
r9) and the kernel takes care of returning control from the trap to
the user code.  If the emulator is unable to emulate the instruction,
it returns via _fpundefinstr (passed via lr) and the kernel halts the
user program with a core dump.

On entry to the emulator r10 points to an area of private FP workspace
reserved in the thread structure for this process.  This is where the
emulator saves its registers across calls.  The first word of this area
is used as a flag to detect the first time a process uses floating point,
so that the emulator startup cost can be avoided for tasks that don't
want it.

This routine does three things:

1) The kernel has created a struct pt_regs on the stack and saved the
user registers into it.  See /usr/include/asm/proc/ptrace.h for details.

2) It calls EmulateAll to emulate a floating point instruction.
EmulateAll returns 1 if the emulation was successful, or 0 if not.

3) If an instruction has been emulated successfully, it looks ahead at
the next instruction.  If it is a floating point instruction, it
executes the instruction, without returning to user space.  In this
way it repeatedly looks ahead and executes floating point instructions
until it encounters a non floating point instruction, at which time it
returns via _fpreturn.

This is done to reduce the effect of the trap overhead on each
floating point instructions.  GCC attempts to group floating point
instructions to allow the emulator to spread the cost of the trap over
several floating point instructions.  */

#include <asm/asm-offsets.h>

	.globl	nwfpe_enter
nwfpe_enter:
	mov	r4, lr			@ save the failure-return addresses
	mov	sl, sp			@ we access the registers via 'sl'

	ldr	r5, [sp, #S_PC]		@ get contents of PC;
	mov	r6, r0			@ save the opcode
emulate:
	ldr	r1, [sp, #S_PSR]	@ fetch the PSR
	bl	arm_check_condition	@ check the condition
	cmp	r0, #ARM_OPCODE_CONDTEST_PASS	@ condition passed?

	@ if condition code failed to match, next insn
	bne	next			@ get the next instruction;

	mov	r0, r6			@ prepare for EmulateAll()
	bl	EmulateAll		@ emulate the instruction
	cmp	r0, #0			@ was emulation successful
	reteq	r4			@ no, return failure

next:
.Lx1:	ldrt	r6, [r5], #4		@ get the next instruction and
					@ increment PC

	and	r2, r6, #0x0F000000	@ test for FP insns
	teq	r2, #0x0C000000
	teqne	r2, #0x0D000000
	teqne	r2, #0x0E000000
	retne	r9			@ return ok if not a fp insn

	str	r5, [sp, #S_PC]		@ update PC copy in regs

	mov	r0, r6			@ save a copy
	b	emulate			@ check condition and emulate

	@ We need to be prepared for the instructions at .Lx1 and .Lx2 
	@ to fault.  Emit the appropriate exception gunk to fix things up.
	@ ??? For some reason, faults can happen at .Lx2 even with a
	@ plain LDR instruction.  Weird, but it seems harmless.
	.pushsection .fixup,"ax"
	.align	2
.Lfix:	ret	r9			@ let the user eat segfaults
	.popsection

	.pushsection __ex_table,"a"
	.align	3
	.long	.Lx1, .Lfix
	.popsection
