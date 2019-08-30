/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC entry.S
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2005 Gyorgy Jeney <nog@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 */

#include <linux/linkage.h>

#include <asm/processor.h>
#include <asm/unistd.h>
#include <asm/thread_info.h>
#include <asm/errno.h>
#include <asm/spr_defs.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/asm-offsets.h>

#define DISABLE_INTERRUPTS(t1,t2)			\
	l.mfspr t2,r0,SPR_SR				;\
	l.movhi	t1,hi(~(SPR_SR_IEE|SPR_SR_TEE))		;\
	l.ori	t1,t1,lo(~(SPR_SR_IEE|SPR_SR_TEE))	;\
	l.and   t2,t2,t1				;\
	l.mtspr r0,t2,SPR_SR

#define ENABLE_INTERRUPTS(t1)				\
	l.mfspr	t1,r0,SPR_SR				;\
	l.ori	t1,t1,lo(SPR_SR_IEE|SPR_SR_TEE)		;\
	l.mtspr	r0,t1,SPR_SR

/* =========================================================[ macros ]=== */

#ifdef CONFIG_TRACE_IRQFLAGS
/*
 * Trace irq on/off creating a stack frame.
 */
#define TRACE_IRQS_OP(trace_op)					\
	l.sw 	-8(r1),r2	/* store frame pointer */		;\
	l.sw	-4(r1),r9	/* store return address */		;\
	l.addi	r2,r1,0		/* move sp to fp */			;\
	l.jal	trace_op						;\
	 l.addi	r1,r1,-8						;\
	l.ori	r1,r2,0		/* restore sp */			;\
	l.lwz	r9,-4(r1)	/* restore return address */		;\
	l.lwz	r2,-8(r1)	/* restore fp */			;\
/*
 * Trace irq on/off and save registers we need that would otherwise be
 * clobbered.
 */
#define TRACE_IRQS_SAVE(t1,trace_op)					\
	l.sw	-12(r1),t1	/* save extra reg */			;\
	l.sw 	-8(r1),r2	/* store frame pointer */		;\
	l.sw	-4(r1),r9	/* store return address */		;\
	l.addi	r2,r1,0		/* move sp to fp */			;\
	l.jal	trace_op						;\
	 l.addi	r1,r1,-12						;\
	l.ori	r1,r2,0		/* restore sp */			;\
	l.lwz	r9,-4(r1)	/* restore return address */		;\
	l.lwz	r2,-8(r1)	/* restore fp */			;\
	l.lwz	t1,-12(r1)	/* restore extra reg */

#define TRACE_IRQS_OFF	TRACE_IRQS_OP(trace_hardirqs_off)
#define TRACE_IRQS_ON	TRACE_IRQS_OP(trace_hardirqs_on)
#define TRACE_IRQS_ON_SYSCALL						\
	TRACE_IRQS_SAVE(r10,trace_hardirqs_on)				;\
	l.lwz	r3,PT_GPR3(r1)						;\
	l.lwz	r4,PT_GPR4(r1)						;\
	l.lwz	r5,PT_GPR5(r1)						;\
	l.lwz	r6,PT_GPR6(r1)						;\
	l.lwz	r7,PT_GPR7(r1)						;\
	l.lwz	r8,PT_GPR8(r1)						;\
	l.lwz	r11,PT_GPR11(r1)
#define TRACE_IRQS_OFF_ENTRY						\
	l.lwz	r5,PT_SR(r1)						;\
	l.andi	r3,r5,(SPR_SR_IEE|SPR_SR_TEE)				;\
	l.sfeq	r5,r0		/* skip trace if irqs were already off */;\
	l.bf	1f							;\
	 l.nop								;\
	TRACE_IRQS_SAVE(r4,trace_hardirqs_off)				;\
1:
#else
#define TRACE_IRQS_OFF
#define TRACE_IRQS_ON
#define TRACE_IRQS_OFF_ENTRY
#define TRACE_IRQS_ON_SYSCALL
#endif

/*
 * We need to disable interrupts at beginning of RESTORE_ALL
 * since interrupt might come in after we've loaded EPC return address
 * and overwrite EPC with address somewhere in RESTORE_ALL
 * which is of course wrong!
 */

#define RESTORE_ALL						\
	DISABLE_INTERRUPTS(r3,r4)				;\
	l.lwz   r3,PT_PC(r1)					;\
	l.mtspr r0,r3,SPR_EPCR_BASE				;\
	l.lwz   r3,PT_SR(r1)					;\
	l.mtspr r0,r3,SPR_ESR_BASE				;\
	l.lwz   r2,PT_GPR2(r1)					;\
	l.lwz   r3,PT_GPR3(r1)					;\
	l.lwz   r4,PT_GPR4(r1)					;\
	l.lwz   r5,PT_GPR5(r1)					;\
	l.lwz   r6,PT_GPR6(r1)					;\
	l.lwz   r7,PT_GPR7(r1)					;\
	l.lwz   r8,PT_GPR8(r1)					;\
	l.lwz   r9,PT_GPR9(r1)					;\
	l.lwz   r10,PT_GPR10(r1)					;\
	l.lwz   r11,PT_GPR11(r1)					;\
	l.lwz   r12,PT_GPR12(r1)					;\
	l.lwz   r13,PT_GPR13(r1)					;\
	l.lwz   r14,PT_GPR14(r1)					;\
	l.lwz   r15,PT_GPR15(r1)					;\
	l.lwz   r16,PT_GPR16(r1)					;\
	l.lwz   r17,PT_GPR17(r1)					;\
	l.lwz   r18,PT_GPR18(r1)					;\
	l.lwz   r19,PT_GPR19(r1)					;\
	l.lwz   r20,PT_GPR20(r1)					;\
	l.lwz   r21,PT_GPR21(r1)					;\
	l.lwz   r22,PT_GPR22(r1)					;\
	l.lwz   r23,PT_GPR23(r1)					;\
	l.lwz   r24,PT_GPR24(r1)					;\
	l.lwz   r25,PT_GPR25(r1)					;\
	l.lwz   r26,PT_GPR26(r1)					;\
	l.lwz   r27,PT_GPR27(r1)					;\
	l.lwz   r28,PT_GPR28(r1)					;\
	l.lwz   r29,PT_GPR29(r1)					;\
	l.lwz   r30,PT_GPR30(r1)					;\
	l.lwz   r31,PT_GPR31(r1)					;\
	l.lwz   r1,PT_SP(r1)					;\
	l.rfe


#define EXCEPTION_ENTRY(handler)				\
	.global	handler						;\
handler:							;\
	/* r1, EPCR, ESR a already saved */			;\
	l.sw	PT_GPR2(r1),r2					;\
	l.sw    PT_GPR3(r1),r3					;\
	/* r4 already save */					;\
	l.sw    PT_GPR5(r1),r5					;\
	l.sw    PT_GPR6(r1),r6					;\
	l.sw    PT_GPR7(r1),r7					;\
	l.sw    PT_GPR8(r1),r8					;\
	l.sw    PT_GPR9(r1),r9					;\
	/* r10 already saved */					;\
	l.sw    PT_GPR11(r1),r11					;\
	/* r12 already saved */					;\
	l.sw    PT_GPR13(r1),r13					;\
	l.sw    PT_GPR14(r1),r14					;\
	l.sw    PT_GPR15(r1),r15					;\
	l.sw    PT_GPR16(r1),r16					;\
	l.sw    PT_GPR17(r1),r17					;\
	l.sw    PT_GPR18(r1),r18					;\
	l.sw    PT_GPR19(r1),r19					;\
	l.sw    PT_GPR20(r1),r20					;\
	l.sw    PT_GPR21(r1),r21					;\
	l.sw    PT_GPR22(r1),r22					;\
	l.sw    PT_GPR23(r1),r23					;\
	l.sw    PT_GPR24(r1),r24					;\
	l.sw    PT_GPR25(r1),r25					;\
	l.sw    PT_GPR26(r1),r26					;\
	l.sw    PT_GPR27(r1),r27					;\
	l.sw    PT_GPR28(r1),r28					;\
	l.sw    PT_GPR29(r1),r29					;\
	/* r30 already save */					;\
/*        l.sw    PT_GPR30(r1),r30*/					;\
	l.sw    PT_GPR31(r1),r31					;\
	TRACE_IRQS_OFF_ENTRY						;\
	/* Store -1 in orig_gpr11 for non-syscall exceptions */	;\
	l.addi	r30,r0,-1					;\
	l.sw	PT_ORIG_GPR11(r1),r30

#define UNHANDLED_EXCEPTION(handler,vector)			\
	.global	handler						;\
handler:							;\
	/* r1, EPCR, ESR already saved */			;\
	l.sw    PT_GPR2(r1),r2					;\
	l.sw    PT_GPR3(r1),r3					;\
	l.sw    PT_GPR5(r1),r5					;\
	l.sw    PT_GPR6(r1),r6					;\
	l.sw    PT_GPR7(r1),r7					;\
	l.sw    PT_GPR8(r1),r8					;\
	l.sw    PT_GPR9(r1),r9					;\
	/* r10 already saved */					;\
	l.sw    PT_GPR11(r1),r11					;\
	/* r12 already saved */					;\
	l.sw    PT_GPR13(r1),r13					;\
	l.sw    PT_GPR14(r1),r14					;\
	l.sw    PT_GPR15(r1),r15					;\
	l.sw    PT_GPR16(r1),r16					;\
	l.sw    PT_GPR17(r1),r17					;\
	l.sw    PT_GPR18(r1),r18					;\
	l.sw    PT_GPR19(r1),r19					;\
	l.sw    PT_GPR20(r1),r20					;\
	l.sw    PT_GPR21(r1),r21					;\
	l.sw    PT_GPR22(r1),r22					;\
	l.sw    PT_GPR23(r1),r23					;\
	l.sw    PT_GPR24(r1),r24					;\
	l.sw    PT_GPR25(r1),r25					;\
	l.sw    PT_GPR26(r1),r26					;\
	l.sw    PT_GPR27(r1),r27					;\
	l.sw    PT_GPR28(r1),r28					;\
	l.sw    PT_GPR29(r1),r29					;\
	/* r31 already saved */					;\
	l.sw    PT_GPR30(r1),r30					;\
/*        l.sw    PT_GPR31(r1),r31	*/				;\
	/* Store -1 in orig_gpr11 for non-syscall exceptions */	;\
	l.addi	r30,r0,-1					;\
	l.sw	PT_ORIG_GPR11(r1),r30				;\
	l.addi	r3,r1,0						;\
	/* r4 is exception EA */				;\
	l.addi	r5,r0,vector					;\
	l.jal	unhandled_exception				;\
	 l.nop							;\
	l.j	_ret_from_exception				;\
	 l.nop

/* clobbers 'reg' */
#define CLEAR_LWA_FLAG(reg)		\
	l.movhi	reg,hi(lwa_flag)	;\
	l.ori	reg,reg,lo(lwa_flag)	;\
	l.sw	0(reg),r0
/*
 * NOTE: one should never assume that SPR_EPC, SPR_ESR, SPR_EEAR
 *       contain the same values as when exception we're handling
 *	 occured. in fact they never do. if you need them use
 *	 values saved on stack (for SPR_EPC, SPR_ESR) or content
 *       of r4 (for SPR_EEAR). for details look at EXCEPTION_HANDLE()
 *       in 'arch/openrisc/kernel/head.S'
 */

/* =====================================================[ exceptions] === */

/* ---[ 0x100: RESET exception ]----------------------------------------- */

EXCEPTION_ENTRY(_tng_kernel_start)
	l.jal	_start
	 l.andi r0,r0,0

/* ---[ 0x200: BUS exception ]------------------------------------------- */

EXCEPTION_ENTRY(_bus_fault_handler)
	CLEAR_LWA_FLAG(r3)
	/* r4: EA of fault (set by EXCEPTION_HANDLE) */
	l.jal   do_bus_fault
	 l.addi  r3,r1,0 /* pt_regs */

	l.j     _ret_from_exception
	 l.nop

/* ---[ 0x300: Data Page Fault exception ]------------------------------- */
EXCEPTION_ENTRY(_dtlb_miss_page_fault_handler)
	CLEAR_LWA_FLAG(r3)
	l.and	r5,r5,r0
	l.j	1f
	 l.nop

EXCEPTION_ENTRY(_data_page_fault_handler)
	CLEAR_LWA_FLAG(r3)
	/* set up parameters for do_page_fault */
	l.ori	r5,r0,0x300		   // exception vector
1:
	l.addi  r3,r1,0                    // pt_regs
	/* r4 set be EXCEPTION_HANDLE */   // effective address of fault

#ifdef CONFIG_OPENRISC_NO_SPR_SR_DSX
	l.lwz   r6,PT_PC(r3)               // address of an offending insn
	l.lwz   r6,0(r6)                   // instruction that caused pf

	l.srli  r6,r6,26                   // check opcode for jump insn
	l.sfeqi r6,0                       // l.j
	l.bf    8f
	l.sfeqi r6,1                       // l.jal
	l.bf    8f
	l.sfeqi r6,3                       // l.bnf
	l.bf    8f
	l.sfeqi r6,4                       // l.bf
	l.bf    8f
	l.sfeqi r6,0x11                    // l.jr
	l.bf    8f
	l.sfeqi r6,0x12                    // l.jalr
	l.bf    8f
	 l.nop

	l.j     9f
	 l.nop

8: // offending insn is in delay slot
	l.lwz   r6,PT_PC(r3)               // address of an offending insn
	l.addi  r6,r6,4
	l.lwz   r6,0(r6)                   // instruction that caused pf
	l.srli  r6,r6,26                   // get opcode
9: // offending instruction opcode loaded in r6

#else

	l.mfspr r6,r0,SPR_SR               // SR
	l.andi  r6,r6,SPR_SR_DSX           // check for delay slot exception
	l.sfne  r6,r0                      // exception happened in delay slot
	l.bnf   7f
	 l.lwz  r6,PT_PC(r3)               // address of an offending insn

	l.addi	r6,r6,4                    // offending insn is in delay slot
7:
	l.lwz   r6,0(r6)                   // instruction that caused pf
	l.srli  r6,r6,26                   // check opcode for write access
#endif

	l.sfgeui r6,0x33                   // check opcode for write access
	l.bnf   1f
	l.sfleui r6,0x37
	l.bnf   1f
	l.ori   r6,r0,0x1                  // write access
	l.j     2f
	 l.nop
1:	l.ori   r6,r0,0x0                  // !write access
2:

	/* call fault.c handler in or32/mm/fault.c */
	l.jal   do_page_fault
	 l.nop
	l.j     _ret_from_exception
	 l.nop

/* ---[ 0x400: Insn Page Fault exception ]------------------------------- */
EXCEPTION_ENTRY(_itlb_miss_page_fault_handler)
	CLEAR_LWA_FLAG(r3)
	l.and	r5,r5,r0
	l.j	1f
	 l.nop

EXCEPTION_ENTRY(_insn_page_fault_handler)
	CLEAR_LWA_FLAG(r3)
	/* set up parameters for do_page_fault */
	l.ori	r5,r0,0x400		   // exception vector
1:
	l.addi  r3,r1,0                    // pt_regs
	/* r4 set be EXCEPTION_HANDLE */   // effective address of fault
	l.ori	r6,r0,0x0		   // !write access

	/* call fault.c handler in or32/mm/fault.c */
	l.jal   do_page_fault
	 l.nop
	l.j     _ret_from_exception
	 l.nop


/* ---[ 0x500: Timer exception ]----------------------------------------- */

EXCEPTION_ENTRY(_timer_handler)
	CLEAR_LWA_FLAG(r3)
	l.jal	timer_interrupt
	 l.addi r3,r1,0 /* pt_regs */

	l.j    _ret_from_intr
	 l.nop

/* ---[ 0x600: Alignment exception ]-------------------------------------- */

EXCEPTION_ENTRY(_alignment_handler)
	CLEAR_LWA_FLAG(r3)
	/* r4: EA of fault (set by EXCEPTION_HANDLE) */
	l.jal   do_unaligned_access
	 l.addi  r3,r1,0 /* pt_regs */

	l.j     _ret_from_exception
	 l.nop

#if 0
EXCEPTION_ENTRY(_alignment_handler)
//        l.mfspr r2,r0,SPR_EEAR_BASE     /* Load the effective address */
	l.addi	r2,r4,0
//        l.mfspr r5,r0,SPR_EPCR_BASE     /* Load the insn address */
	l.lwz   r5,PT_PC(r1)

	l.lwz   r3,0(r5)                /* Load insn */
	l.srli  r4,r3,26                /* Shift left to get the insn opcode */

	l.sfeqi r4,0x00                 /* Check if the load/store insn is in delay slot */
	l.bf    jmp
	l.sfeqi r4,0x01
	l.bf    jmp
	l.sfeqi r4,0x03
	l.bf    jmp
	l.sfeqi r4,0x04
	l.bf    jmp
	l.sfeqi r4,0x11
	l.bf    jr
	l.sfeqi r4,0x12
	l.bf    jr
	l.nop
	l.j     1f
	l.addi  r5,r5,4                 /* Increment PC to get return insn address */

jmp:
	l.slli  r4,r3,6                 /* Get the signed extended jump length */
	l.srai  r4,r4,4

	l.lwz   r3,4(r5)                /* Load the real load/store insn */

	l.add   r5,r5,r4                /* Calculate jump target address */

	l.j     1f
	l.srli  r4,r3,26                /* Shift left to get the insn opcode */

jr:
	l.slli  r4,r3,9                 /* Shift to get the reg nb */
	l.andi  r4,r4,0x7c

	l.lwz   r3,4(r5)                /* Load the real load/store insn */

	l.add   r4,r4,r1                /* Load the jump register value from the stack */
	l.lwz   r5,0(r4)

	l.srli  r4,r3,26                /* Shift left to get the insn opcode */


1:
//	  l.mtspr r0,r5,SPR_EPCR_BASE
	l.sw	PT_PC(r1),r5

	l.sfeqi r4,0x26
	l.bf    lhs
	l.sfeqi r4,0x25
	l.bf    lhz
	l.sfeqi r4,0x22
	l.bf    lws
	l.sfeqi r4,0x21
	l.bf    lwz
	l.sfeqi r4,0x37
	l.bf    sh
	l.sfeqi r4,0x35
	l.bf    sw
	l.nop

1:      l.j     1b                      /* I don't know what to do */
	l.nop

lhs:    l.lbs   r5,0(r2)
	l.slli  r5,r5,8
	l.lbz   r6,1(r2)
	l.or    r5,r5,r6
	l.srli  r4,r3,19
	l.andi  r4,r4,0x7c
	l.add   r4,r4,r1
	l.j     align_end
	l.sw    0(r4),r5

lhz:    l.lbz   r5,0(r2)
	l.slli  r5,r5,8
	l.lbz   r6,1(r2)
	l.or    r5,r5,r6
	l.srli  r4,r3,19
	l.andi  r4,r4,0x7c
	l.add   r4,r4,r1
	l.j     align_end
	l.sw    0(r4),r5

lws:    l.lbs   r5,0(r2)
	l.slli  r5,r5,24
	l.lbz   r6,1(r2)
	l.slli  r6,r6,16
	l.or    r5,r5,r6
	l.lbz   r6,2(r2)
	l.slli  r6,r6,8
	l.or    r5,r5,r6
	l.lbz   r6,3(r2)
	l.or    r5,r5,r6
	l.srli  r4,r3,19
	l.andi  r4,r4,0x7c
	l.add   r4,r4,r1
	l.j     align_end
	l.sw    0(r4),r5

lwz:    l.lbz   r5,0(r2)
	l.slli  r5,r5,24
	l.lbz   r6,1(r2)
	l.slli  r6,r6,16
	l.or    r5,r5,r6
	l.lbz   r6,2(r2)
	l.slli  r6,r6,8
	l.or    r5,r5,r6
	l.lbz   r6,3(r2)
	l.or    r5,r5,r6
	l.srli  r4,r3,19
	l.andi  r4,r4,0x7c
	l.add   r4,r4,r1
	l.j     align_end
	l.sw    0(r4),r5

sh:
	l.srli  r4,r3,9
	l.andi  r4,r4,0x7c
	l.add   r4,r4,r1
	l.lwz   r5,0(r4)
	l.sb    1(r2),r5
	l.srli  r5,r5,8
	l.j     align_end
	l.sb    0(r2),r5

sw:
	l.srli  r4,r3,9
	l.andi  r4,r4,0x7c
	l.add   r4,r4,r1
	l.lwz   r5,0(r4)
	l.sb    3(r2),r5
	l.srli  r5,r5,8
	l.sb    2(r2),r5
	l.srli  r5,r5,8
	l.sb    1(r2),r5
	l.srli  r5,r5,8
	l.j     align_end
	l.sb    0(r2),r5

align_end:
	l.j    _ret_from_intr
	l.nop
#endif

/* ---[ 0x700: Illegal insn exception ]---------------------------------- */

EXCEPTION_ENTRY(_illegal_instruction_handler)
	/* r4: EA of fault (set by EXCEPTION_HANDLE) */
	l.jal   do_illegal_instruction
	 l.addi  r3,r1,0 /* pt_regs */

	l.j     _ret_from_exception
	 l.nop

/* ---[ 0x800: External interrupt exception ]---------------------------- */

EXCEPTION_ENTRY(_external_irq_handler)
#ifdef CONFIG_OPENRISC_ESR_EXCEPTION_BUG_CHECK
	l.lwz	r4,PT_SR(r1)		// were interrupts enabled ?
	l.andi	r4,r4,SPR_SR_IEE
	l.sfeqi	r4,0
	l.bnf	1f			// ext irq enabled, all ok.
	l.nop

	l.addi  r1,r1,-0x8
	l.movhi r3,hi(42f)
	l.ori	r3,r3,lo(42f)
	l.sw    0x0(r1),r3
	l.jal   printk
	l.sw    0x4(r1),r4
	l.addi  r1,r1,0x8

	.section .rodata, "a"
42:
		.string "\n\rESR interrupt bug: in _external_irq_handler (ESR %x)\n\r"
		.align 4
	.previous

	l.ori	r4,r4,SPR_SR_IEE	// fix the bug
//	l.sw	PT_SR(r1),r4
1:
#endif
	CLEAR_LWA_FLAG(r3)
	l.addi	r3,r1,0
	l.movhi	r8,hi(do_IRQ)
	l.ori	r8,r8,lo(do_IRQ)
	l.jalr r8
	l.nop
	l.j    _ret_from_intr
	l.nop

/* ---[ 0x900: DTLB miss exception ]------------------------------------- */


/* ---[ 0xa00: ITLB miss exception ]------------------------------------- */


/* ---[ 0xb00: Range exception ]----------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0xb00,0xb00)

/* ---[ 0xc00: Syscall exception ]--------------------------------------- */

/*
 * Syscalls are a special type of exception in that they are
 * _explicitly_ invoked by userspace and can therefore be
 * held to conform to the same ABI as normal functions with
 * respect to whether registers are preserved across the call
 * or not.
 */

/* Upon syscall entry we just save the callee-saved registers
 * and not the call-clobbered ones.
 */

_string_syscall_return:
	.string "syscall return %ld \n\r\0"
	.align 4

ENTRY(_sys_call_handler)
	/* r1, EPCR, ESR a already saved */
	l.sw	PT_GPR2(r1),r2
	/* r3-r8 must be saved because syscall restart relies
	 * on us being able to restart the syscall args... technically
	 * they should be clobbered, otherwise
	 */
	l.sw    PT_GPR3(r1),r3
	/*
	 * r4 already saved
	 * r4 holds the EEAR address of the fault, use it as screatch reg and
	 * then load the original r4
	 */
	CLEAR_LWA_FLAG(r4)
	l.lwz	r4,PT_GPR4(r1)
	l.sw    PT_GPR5(r1),r5
	l.sw    PT_GPR6(r1),r6
	l.sw    PT_GPR7(r1),r7
	l.sw    PT_GPR8(r1),r8
	l.sw    PT_GPR9(r1),r9
	/* r10 already saved */
	l.sw    PT_GPR11(r1),r11
	/* orig_gpr11 must be set for syscalls */
	l.sw    PT_ORIG_GPR11(r1),r11
	/* r12,r13 already saved */

	/* r14-r28 (even) aren't touched by the syscall fast path below
	 * so we don't need to save them.  However, the functions that return
	 * to userspace via a call to switch() DO need to save these because
	 * switch() effectively clobbers them... saving these registers for
	 * such functions is handled in their syscall wrappers (see fork, vfork,
	 * and clone, below).

	/* r30 is the only register we clobber in the fast path */
	/* r30 already saved */
/*	l.sw    PT_GPR30(r1),r30 */

_syscall_check_trace_enter:
	/* syscalls run with interrupts enabled */
	TRACE_IRQS_ON_SYSCALL
	ENABLE_INTERRUPTS(r29)		// enable interrupts, r29 is temp

	/* If TIF_SYSCALL_TRACE is set, then we want to do syscall tracing */
	l.lwz	r30,TI_FLAGS(r10)
	l.andi	r30,r30,_TIF_SYSCALL_TRACE
	l.sfne	r30,r0
	l.bf	_syscall_trace_enter
	 l.nop

_syscall_check:
	/* Ensure that the syscall number is reasonable */
	l.sfgeui r11,__NR_syscalls
	l.bf	_syscall_badsys
	 l.nop

_syscall_call:
	l.movhi r29,hi(sys_call_table)
	l.ori   r29,r29,lo(sys_call_table)
	l.slli  r11,r11,2
	l.add   r29,r29,r11
	l.lwz   r29,0(r29)

	l.jalr  r29
	 l.nop

_syscall_return:
	/* All syscalls return here... just pay attention to ret_from_fork
	 * which does it in a round-about way.
	 */
	l.sw    PT_GPR11(r1),r11           // save return value

#if 0
_syscall_debug:
	l.movhi r3,hi(_string_syscall_return)
	l.ori   r3,r3,lo(_string_syscall_return)
	l.ori   r27,r0,1
	l.sw    -4(r1),r27
	l.sw    -8(r1),r11
	l.addi  r1,r1,-8
	l.movhi r27,hi(printk)
	l.ori   r27,r27,lo(printk)
	l.jalr  r27
	 l.nop
	l.addi  r1,r1,8
#endif

_syscall_check_trace_leave:
	/* r30 is a callee-saved register so this should still hold the
	 * _TIF_SYSCALL_TRACE flag from _syscall_check_trace_enter above...
	 * _syscall_trace_leave expects syscall result to be in pt_regs->r11.
	 */
	l.sfne	r30,r0
	l.bf	_syscall_trace_leave
	 l.nop

/* This is where the exception-return code begins... interrupts need to be
 * disabled the rest of the way here because we can't afford to miss any
 * interrupts that set NEED_RESCHED or SIGNALPENDING... really true? */

_syscall_check_work:
	/* Here we need to disable interrupts */
	DISABLE_INTERRUPTS(r27,r29)
	TRACE_IRQS_OFF
	l.lwz	r30,TI_FLAGS(r10)
	l.andi	r30,r30,_TIF_WORK_MASK
	l.sfne	r30,r0

	l.bnf	_syscall_resume_userspace
	 l.nop

	/* Work pending follows a different return path, so we need to
	 * make sure that all the call-saved registers get into pt_regs
	 * before branching...
	 */
	l.sw    PT_GPR14(r1),r14
	l.sw    PT_GPR16(r1),r16
	l.sw    PT_GPR18(r1),r18
	l.sw    PT_GPR20(r1),r20
	l.sw    PT_GPR22(r1),r22
	l.sw    PT_GPR24(r1),r24
	l.sw    PT_GPR26(r1),r26
	l.sw    PT_GPR28(r1),r28

	/* _work_pending needs to be called with interrupts disabled */
	l.j	_work_pending
	 l.nop

_syscall_resume_userspace:
//	ENABLE_INTERRUPTS(r29)


/* This is the hot path for returning to userspace from a syscall.  If there's
 * work to be done and the branch to _work_pending was taken above, then the
 * return to userspace will be done via the normal exception return path...
 * that path restores _all_ registers and will overwrite the "clobbered"
 * registers with whatever garbage is in pt_regs -- that's OK because those
 * registers are clobbered anyway and because the extra work is insignificant
 * in the context of the extra work that _work_pending is doing.

/* Once again, syscalls are special and only guarantee to preserve the
 * same registers as a normal function call */

/* The assumption here is that the registers r14-r28 (even) are untouched and
 * don't need to be restored... be sure that that's really the case!
 */

/* This is still too much... we should only be restoring what we actually
 * clobbered... we should even be using 'scratch' (odd) regs above so that
 * we don't need to restore anything, hardly...
 */

	l.lwz	r2,PT_GPR2(r1)

	/* Restore args */
	/* r3-r8 are technically clobbered, but syscall restart needs these
	 * to be restored...
	 */
	l.lwz	r3,PT_GPR3(r1)
	l.lwz	r4,PT_GPR4(r1)
	l.lwz	r5,PT_GPR5(r1)
	l.lwz	r6,PT_GPR6(r1)
	l.lwz	r7,PT_GPR7(r1)
	l.lwz	r8,PT_GPR8(r1)

	l.lwz	r9,PT_GPR9(r1)
	l.lwz	r10,PT_GPR10(r1)
	l.lwz	r11,PT_GPR11(r1)

	/* r30 is the only register we clobber in the fast path */
	l.lwz	r30,PT_GPR30(r1)

	/* Here we use r13-r19 (odd) as scratch regs */
	l.lwz   r13,PT_PC(r1)
	l.lwz   r15,PT_SR(r1)
	l.lwz	r1,PT_SP(r1)
	/* Interrupts need to be disabled for setting EPCR and ESR
	 * so that another interrupt doesn't come in here and clobber
	 * them before we can use them for our l.rfe */
	DISABLE_INTERRUPTS(r17,r19)
	l.mtspr r0,r13,SPR_EPCR_BASE
	l.mtspr r0,r15,SPR_ESR_BASE
	l.rfe

/* End of hot path!
 * Keep the below tracing and error handling out of the hot path...
*/

_syscall_trace_enter:
	/* Here we pass pt_regs to do_syscall_trace_enter.  Make sure
	 * that function is really getting all the info it needs as
	 * pt_regs isn't a complete set of userspace regs, just the
	 * ones relevant to the syscall...
	 *
	 * Note use of delay slot for setting argument.
	 */
	l.jal	do_syscall_trace_enter
	 l.addi	r3,r1,0

	/* Restore arguments (not preserved across do_syscall_trace_enter)
	 * so that we can do the syscall for real and return to the syscall
	 * hot path.
	 */
	l.lwz	r11,PT_GPR11(r1)
	l.lwz	r3,PT_GPR3(r1)
	l.lwz	r4,PT_GPR4(r1)
	l.lwz	r5,PT_GPR5(r1)
	l.lwz	r6,PT_GPR6(r1)
	l.lwz	r7,PT_GPR7(r1)

	l.j	_syscall_check
	 l.lwz	r8,PT_GPR8(r1)

_syscall_trace_leave:
	l.jal	do_syscall_trace_leave
	 l.addi	r3,r1,0

	l.j	_syscall_check_work
	 l.nop

_syscall_badsys:
	/* Here we effectively pretend to have executed an imaginary
	 * syscall that returns -ENOSYS and then return to the regular
	 * syscall hot path.
	 * Note that "return value" is set in the delay slot...
	 */
	l.j	_syscall_return
	 l.addi	r11,r0,-ENOSYS

/******* END SYSCALL HANDLING *******/

/* ---[ 0xd00: Trap exception ]------------------------------------------ */

UNHANDLED_EXCEPTION(_vector_0xd00,0xd00)

/* ---[ 0xe00: Trap exception ]------------------------------------------ */

EXCEPTION_ENTRY(_trap_handler)
	CLEAR_LWA_FLAG(r3)
	/* r4: EA of fault (set by EXCEPTION_HANDLE) */
	l.jal   do_trap
	 l.addi  r3,r1,0 /* pt_regs */

	l.j     _ret_from_exception
	 l.nop

/* ---[ 0xf00: Reserved exception ]-------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0xf00,0xf00)

/* ---[ 0x1000: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1000,0x1000)

/* ---[ 0x1100: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1100,0x1100)

/* ---[ 0x1200: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1200,0x1200)

/* ---[ 0x1300: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1300,0x1300)

/* ---[ 0x1400: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1400,0x1400)

/* ---[ 0x1500: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1500,0x1500)

/* ---[ 0x1600: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1600,0x1600)

/* ---[ 0x1700: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1700,0x1700)

/* ---[ 0x1800: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1800,0x1800)

/* ---[ 0x1900: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1900,0x1900)

/* ---[ 0x1a00: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1a00,0x1a00)

/* ---[ 0x1b00: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1b00,0x1b00)

/* ---[ 0x1c00: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1c00,0x1c00)

/* ---[ 0x1d00: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1d00,0x1d00)

/* ---[ 0x1e00: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1e00,0x1e00)

/* ---[ 0x1f00: Reserved exception ]------------------------------------- */

UNHANDLED_EXCEPTION(_vector_0x1f00,0x1f00)

/* ========================================================[ return ] === */

_resume_userspace:
	DISABLE_INTERRUPTS(r3,r4)
	TRACE_IRQS_OFF
	l.lwz	r4,TI_FLAGS(r10)
	l.andi	r13,r4,_TIF_WORK_MASK
	l.sfeqi	r13,0
	l.bf	_restore_all
	 l.nop

_work_pending:
	l.lwz	r5,PT_ORIG_GPR11(r1)
	l.sfltsi r5,0
	l.bnf	1f
	 l.nop
	l.andi	r5,r5,0
1:
	l.jal	do_work_pending
	 l.ori	r3,r1,0			/* pt_regs */

	l.sfeqi	r11,0
	l.bf	_restore_all
	 l.nop
	l.sfltsi r11,0
	l.bnf	1f
	 l.nop
	l.and	r11,r11,r0
	l.ori	r11,r11,__NR_restart_syscall
	l.j	_syscall_check_trace_enter
	 l.nop
1:
	l.lwz	r11,PT_ORIG_GPR11(r1)
	/* Restore arg registers */
	l.lwz	r3,PT_GPR3(r1)
	l.lwz	r4,PT_GPR4(r1)
	l.lwz	r5,PT_GPR5(r1)
	l.lwz	r6,PT_GPR6(r1)
	l.lwz	r7,PT_GPR7(r1)
	l.j	_syscall_check_trace_enter
	 l.lwz	r8,PT_GPR8(r1)

_restore_all:
#ifdef CONFIG_TRACE_IRQFLAGS
	l.lwz	r4,PT_SR(r1)
	l.andi	r3,r4,(SPR_SR_IEE|SPR_SR_TEE)
	l.sfeq	r3,r0		/* skip trace if irqs were off */
	l.bf	skip_hardirqs_on
	 l.nop
	TRACE_IRQS_ON
skip_hardirqs_on:
#endif
	RESTORE_ALL
	/* This returns to userspace code */


ENTRY(_ret_from_intr)
ENTRY(_ret_from_exception)
	l.lwz	r4,PT_SR(r1)
	l.andi	r3,r4,SPR_SR_SM
	l.sfeqi	r3,0
	l.bnf	_restore_all
	 l.nop
	l.j	_resume_userspace
	 l.nop

ENTRY(ret_from_fork)
	l.jal	schedule_tail
	 l.nop

	/* Check if we are a kernel thread */
	l.sfeqi	r20,0
	l.bf	1f
	 l.nop

	/* ...we are a kernel thread so invoke the requested callback */
	l.jalr	r20
	 l.or	r3,r22,r0

1:
	/* _syscall_returns expect r11 to contain return value */
	l.lwz	r11,PT_GPR11(r1)

	/* The syscall fast path return expects call-saved registers
	 * r12-r28 to be untouched, so we restore them here as they
	 * will have been effectively clobbered when arriving here
	 * via the call to switch()
	 */
	l.lwz	r12,PT_GPR12(r1)
	l.lwz	r14,PT_GPR14(r1)
	l.lwz	r16,PT_GPR16(r1)
	l.lwz	r18,PT_GPR18(r1)
	l.lwz	r20,PT_GPR20(r1)
	l.lwz	r22,PT_GPR22(r1)
	l.lwz	r24,PT_GPR24(r1)
	l.lwz	r26,PT_GPR26(r1)
	l.lwz	r28,PT_GPR28(r1)

	l.j	_syscall_return
	 l.nop

/* ========================================================[ switch ] === */

/*
 * This routine switches between two different tasks.  The process
 * state of one is saved on its kernel stack.  Then the state
 * of the other is restored from its kernel stack.  The memory
 * management hardware is updated to the second process's state.
 * Finally, we can return to the second process, via the 'return'.
 *
 * Note: there are two ways to get to the "going out" portion
 * of this code; either by coming in via the entry (_switch)
 * or via "fork" which must set up an environment equivalent
 * to the "_switch" path.  If you change this (or in particular, the
 * SAVE_REGS macro), you'll have to change the fork code also.
 */


/* _switch MUST never lay on page boundry, cause it runs from
 * effective addresses and beeing interrupted by iTLB miss would kill it.
 * dTLB miss seams to never accour in the bad place since data accesses
 * are from task structures which are always page aligned.
 *
 * The problem happens in RESTORE_ALL_NO_R11 where we first set the EPCR
 * register, then load the previous register values and only at the end call
 * the l.rfe instruction. If get TLB miss in beetwen the EPCR register gets
 * garbled and we end up calling l.rfe with the wrong EPCR. (same probably
 * holds for ESR)
 *
 * To avoid this problems it is sufficient to align _switch to
 * some nice round number smaller than it's size...
 */

/* ABI rules apply here... we either enter _switch via schedule() or via
 * an imaginary call to which we shall return at return_from_fork.  Either
 * way, we are a function call and only need to preserve the callee-saved
 * registers when we return.  As such, we don't need to save the registers
 * on the stack that we won't be returning as they were...
 */

	.align 0x400
ENTRY(_switch)
	/* We don't store SR as _switch only gets called in a context where
	 * the SR will be the same going in and coming out... */

	/* Set up new pt_regs struct for saving task state */
	l.addi  r1,r1,-(INT_FRAME_SIZE)

	/* No need to store r1/PT_SP as it goes into KSP below */
	l.sw    PT_GPR2(r1),r2
	l.sw    PT_GPR9(r1),r9
	/* This is wrong, r12 shouldn't be here... but GCC is broken for the time being
	 * and expects r12 to be callee-saved... */
	l.sw    PT_GPR12(r1),r12
	l.sw    PT_GPR14(r1),r14
	l.sw    PT_GPR16(r1),r16
	l.sw    PT_GPR18(r1),r18
	l.sw    PT_GPR20(r1),r20
	l.sw    PT_GPR22(r1),r22
	l.sw    PT_GPR24(r1),r24
	l.sw    PT_GPR26(r1),r26
	l.sw    PT_GPR28(r1),r28
	l.sw    PT_GPR30(r1),r30

	l.addi	r11,r10,0			/* Save old 'current' to 'last' return value*/

	/* We use thread_info->ksp for storing the address of the above
	 * structure so that we can get back to it later... we don't want
	 * to lose the value of thread_info->ksp, though, so store it as
	 * pt_regs->sp so that we can easily restore it when we are made
	 * live again...
	 */

	/* Save the old value of thread_info->ksp as pt_regs->sp */
	l.lwz	r29,TI_KSP(r10)
	l.sw	PT_SP(r1),r29

	/* Swap kernel stack pointers */
	l.sw    TI_KSP(r10),r1			/* Save old stack pointer */
	l.or	r10,r4,r0			/* Set up new current_thread_info */
	l.lwz   r1,TI_KSP(r10)			/* Load new stack pointer */

	/* Restore the old value of thread_info->ksp */
	l.lwz	r29,PT_SP(r1)
	l.sw	TI_KSP(r10),r29

	/* ...and restore the registers, except r11 because the return value
	 * has already been set above.
	 */
	l.lwz   r2,PT_GPR2(r1)
	l.lwz   r9,PT_GPR9(r1)
	/* No need to restore r10 */
	/* ...and do not restore r11 */

	/* This is wrong, r12 shouldn't be here... but GCC is broken for the time being
	 * and expects r12 to be callee-saved... */
	l.lwz   r12,PT_GPR12(r1)
	l.lwz   r14,PT_GPR14(r1)
	l.lwz   r16,PT_GPR16(r1)
	l.lwz   r18,PT_GPR18(r1)
	l.lwz   r20,PT_GPR20(r1)
	l.lwz   r22,PT_GPR22(r1)
	l.lwz   r24,PT_GPR24(r1)
	l.lwz   r26,PT_GPR26(r1)
	l.lwz   r28,PT_GPR28(r1)
	l.lwz   r30,PT_GPR30(r1)

	/* Unwind stack to pre-switch state */
	l.addi  r1,r1,(INT_FRAME_SIZE)

	/* Return via the link-register back to where we 'came from', where
	 * that may be either schedule(), ret_from_fork(), or
	 * ret_from_kernel_thread().  If we are returning to a new thread,
	 * we are expected to have set up the arg to schedule_tail already,
	 * hence we do so here unconditionally:
	 */
	l.lwz   r3,TI_TASK(r3)		/* Load 'prev' as schedule_tail arg */
	l.jr	r9
	 l.nop

/* ==================================================================== */

/* These all use the delay slot for setting the argument register, so the
 * jump is always happening after the l.addi instruction.
 *
 * These are all just wrappers that don't touch the link-register r9, so the
 * return from the "real" syscall function will return back to the syscall
 * code that did the l.jal that brought us here.
 */

/* fork requires that we save all the callee-saved registers because they
 * are all effectively clobbered by the call to _switch.  Here we store
 * all the registers that aren't touched by the syscall fast path and thus
 * weren't saved there.
 */

_fork_save_extra_regs_and_call:
	l.sw    PT_GPR14(r1),r14
	l.sw    PT_GPR16(r1),r16
	l.sw    PT_GPR18(r1),r18
	l.sw    PT_GPR20(r1),r20
	l.sw    PT_GPR22(r1),r22
	l.sw    PT_GPR24(r1),r24
	l.sw    PT_GPR26(r1),r26
	l.jr	r29
	 l.sw    PT_GPR28(r1),r28

ENTRY(__sys_clone)
	l.movhi	r29,hi(sys_clone)
	l.ori	r29,r29,lo(sys_clone)
	l.j	_fork_save_extra_regs_and_call
	 l.addi	r7,r1,0

ENTRY(__sys_fork)
	l.movhi	r29,hi(sys_fork)
	l.ori	r29,r29,lo(sys_fork)
	l.j	_fork_save_extra_regs_and_call
	 l.addi	r3,r1,0

ENTRY(sys_rt_sigreturn)
	l.jal	_sys_rt_sigreturn
	 l.addi	r3,r1,0
	l.sfne	r30,r0
	l.bnf	_no_syscall_trace
	 l.nop
	l.jal	do_syscall_trace_leave
	 l.addi	r3,r1,0
_no_syscall_trace:
	l.j	_resume_userspace
	 l.nop

/* This is a catch-all syscall for atomic instructions for the OpenRISC 1000.
 * The functions takes a variable number of parameters depending on which
 * particular flavour of atomic you want... parameter 1 is a flag identifying
 * the atomic in question.  Currently, this function implements the
 * following variants:
 *
 * XCHG:
 *  @flag: 1
 *  @ptr1:
 *  @ptr2:
 * Atomically exchange the values in pointers 1 and 2.
 *
 */

ENTRY(sys_or1k_atomic)
	/* FIXME: This ignores r3 and always does an XCHG */
	DISABLE_INTERRUPTS(r17,r19)
	l.lwz	r29,0(r4)
	l.lwz	r27,0(r5)
	l.sw	0(r4),r27
	l.sw	0(r5),r29
	ENABLE_INTERRUPTS(r17)
	l.jr	r9
	 l.or	r11,r0,r0

/* ============================================================[ EOF ]=== */
