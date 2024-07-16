/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/arch/arm/lib/backtrace-clang.S
 *
 *  Copyright (C) 2019 Nathan Huckleberry
 *
 */
#include <linux/kern_levels.h>
#include <linux/linkage.h>
#include <asm/assembler.h>
		.text

/* fp is 0 or stack frame */

#define frame	r4
#define sv_fp	r5
#define sv_pc	r6
#define mask	r7
#define sv_lr	r8
#define loglvl	r9

ENTRY(c_backtrace)

#if !defined(CONFIG_FRAME_POINTER) || !defined(CONFIG_PRINTK)
		ret	lr
ENDPROC(c_backtrace)
#else


/*
 * Clang does not store pc or sp in function prologues so we don't know exactly
 * where the function starts.
 *
 * We can treat the current frame's lr as the saved pc and the preceding
 * frame's lr as the current frame's lr, but we can't trace the most recent
 * call.  Inserting a false stack frame allows us to reference the function
 * called last in the stacktrace.
 *
 * If the call instruction was a bl we can look at the callers branch
 * instruction to calculate the saved pc.  We can recover the pc in most cases,
 * but in cases such as calling function pointers we cannot. In this case,
 * default to using the lr. This will be some address in the function, but will
 * not be the function start.
 *
 * Unfortunately due to the stack frame layout we can't dump r0 - r3, but these
 * are less frequently saved.
 *
 * Stack frame layout:
 * 		<larger addresses>
 * 		saved lr
 * 	frame=> saved fp
 * 		optionally saved caller registers (r4 - r10)
 * 		optionally saved arguments (r0 - r3)
 * 		<top of stack frame>
 * 		<smaller addresses>
 *
 * Functions start with the following code sequence:
 * corrected pc =>  stmfd sp!, {..., fp, lr}
 *		add fp, sp, #x
 *		stmfd sp!, {r0 - r3} (optional)
 *
 *
 *
 *
 *
 *
 * The diagram below shows an example stack setup for dump_stack.
 *
 * The frame for c_backtrace has pointers to the code of dump_stack. This is
 * why the frame of c_backtrace is used to for the pc calculation of
 * dump_stack. This is why we must move back a frame to print dump_stack.
 *
 * The stored locals for dump_stack are in dump_stack's frame. This means that
 * to fully print dump_stack's frame we need both the frame for dump_stack (for
 * locals) and the frame that was called by dump_stack (for pc).
 *
 * To print locals we must know where the function start is. If we read the
 * function prologue opcodes we can determine which variables are stored in the
 * stack frame.
 *
 * To find the function start of dump_stack we can look at the stored LR of
 * show_stack. It points at the instruction directly after the bl dump_stack.
 * We can then read the offset from the bl opcode to determine where the branch
 * takes us.  The address calculated must be the start of dump_stack.
 *
 * c_backtrace frame           dump_stack:
 * {[LR]    }  ============|   ...
 * {[FP]    }  =======|    |   bl c_backtrace
 *                    |    |=> ...
 * {[R4-R10]}         |
 * {[R0-R3] }         |        show_stack:
 * dump_stack frame   |        ...
 * {[LR]    } =============|   bl dump_stack
 * {[FP]    } <=======|    |=> ...
 * {[R4-R10]}
 * {[R0-R3] }
 */

		stmfd	sp!, {r4 - r9, fp, lr}	@ Save an extra register
						@ to ensure 8 byte alignment
		movs	frame, r0		@ if frame pointer is zero
		beq	no_frame		@ we have no stack frames
		mov	loglvl, r2
		tst	r1, #0x10		@ 26 or 32-bit mode?
		moveq	mask, #0xfc000003
		movne	mask, #0		@ mask for 32-bit

/*
 * Switches the current frame to be the frame for dump_stack.
 */
		add	frame, sp, #24		@ switch to false frame
for_each_frame:	tst	frame, mask		@ Check for address exceptions
		bne	no_frame

/*
 * sv_fp is the stack frame with the locals for the current considered
 * function.
 *
 * sv_pc is the saved lr frame the frame above. This is a pointer to a code
 * address within the current considered function, but it is not the function
 * start. This value gets updated to be the function start later if it is
 * possible.
 */
1001:		ldr	sv_pc, [frame, #4]	@ get saved 'pc'
1002:		ldr	sv_fp, [frame, #0]	@ get saved fp

		teq	sv_fp, mask		@ make sure next frame exists
		beq	no_frame

/*
 * sv_lr is the lr from the function that called the current function. This is
 * a pointer to a code address in the current function's caller.  sv_lr-4 is
 * the instruction used to call the current function.
 *
 * This sv_lr can be used to calculate the function start if the function was
 * called using a bl instruction. If the function start can be recovered sv_pc
 * is overwritten with the function start.
 *
 * If the current function was called using a function pointer we cannot
 * recover the function start and instead continue with sv_pc as an arbitrary
 * value within the current function. If this is the case we cannot print
 * registers for the current function, but the stacktrace is still printed
 * properly.
 */
1003:		ldr	sv_lr, [sv_fp, #4]	@ get saved lr from next frame

1004:		ldr	r0, [sv_lr, #-4]	@ get call instruction
		ldr	r3, .Lopcode+4
		and	r2, r3, r0		@ is this a bl call
		teq	r2, r3
		bne	finished_setup		@ give up if it's not
		and	r0, #0xffffff		@ get call offset 24-bit int
		lsl	r0, r0, #8		@ sign extend offset
		asr	r0, r0, #8
		ldr	sv_pc, [sv_fp, #4]	@ get lr address
		add	sv_pc, sv_pc, #-4	@ get call instruction address
		add	sv_pc, sv_pc, #8	@ take care of prefetch
		add	sv_pc, sv_pc, r0, lsl #2@ find function start

finished_setup:

		bic	sv_pc, sv_pc, mask	@ mask PC/LR for the mode

/*
 * Print the function (sv_pc) and where it was called from (sv_lr).
 */
		mov	r0, sv_pc

		mov	r1, sv_lr
		mov	r2, frame
		bic	r1, r1, mask		@ mask PC/LR for the mode
		mov	r3, loglvl
		bl	dump_backtrace_entry

/*
 * Test if the function start is a stmfd instruction to determine which
 * registers were stored in the function prologue.
 *
 * If we could not recover the sv_pc because we were called through a function
 * pointer the comparison will fail and no registers will print. Unwinding will
 * continue as if there had been no registers stored in this frame.
 */
1005:		ldr	r1, [sv_pc, #0]		@ if stmfd sp!, {..., fp, lr}
		ldr	r3, .Lopcode		@ instruction exists,
		teq	r3, r1, lsr #11
		ldr	r0, [frame]		@ locals are stored in
						@ the preceding frame
		subeq	r0, r0, #4
		mov	r2, loglvl
		bleq	dump_backtrace_stm	@ dump saved registers

/*
 * If we are out of frames or if the next frame is invalid.
 */
		teq	sv_fp, #0		@ zero saved fp means
		beq	no_frame		@ no further frames

		cmp	sv_fp, frame		@ next frame must be
		mov	frame, sv_fp		@ above the current frame
#ifdef CONFIG_IRQSTACKS
		@
		@ Kernel stacks may be discontiguous in memory. If the next
		@ frame is below the previous frame, accept it as long as it
		@ lives in kernel memory.
		@
		cmpls	sv_fp, #PAGE_OFFSET
#endif
		bhi	for_each_frame

1006:		adr	r0, .Lbad
		mov	r1, loglvl
		mov	r2, frame
		bl	_printk
no_frame:	ldmfd	sp!, {r4 - r9, fp, pc}
ENDPROC(c_backtrace)
		.pushsection __ex_table,"a"
		.align	3
		.long	1001b, 1006b
		.long	1002b, 1006b
		.long	1003b, 1006b
		.long	1004b, finished_setup
		.long   1005b, 1006b
		.popsection

.Lbad:		.asciz	"%sBacktrace aborted due to bad frame pointer <%p>\n"
		.align
.Lopcode:	.word	0xe92d4800 >> 11	@ stmfd sp!, {... fp, lr}
		.word	0x0b000000		@ bl if these bits are set

#endif
