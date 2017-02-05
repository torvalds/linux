/*
 * OpenRISC head.S
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * Modifications for the OpenRISC architecture:
 * Copyright (C) 2003 Matjaz Breskvar <phoenix@bsemi.com>
 * Copyright (C) 2010-2011 Jonas Bonn <jonas@southpole.se>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/linkage.h>
#include <linux/threads.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/serial_reg.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/thread_info.h>
#include <asm/cache.h>
#include <asm/spr_defs.h>
#include <asm/asm-offsets.h>
#include <linux/of_fdt.h>

#define tophys(rd,rs)				\
	l.movhi	rd,hi(-KERNELBASE)		;\
	l.add	rd,rd,rs

#define CLEAR_GPR(gpr)				\
	l.movhi	gpr,0x0

#define LOAD_SYMBOL_2_GPR(gpr,symbol)		\
	l.movhi gpr,hi(symbol)			;\
	l.ori   gpr,gpr,lo(symbol)


#define UART_BASE_ADD      0x90000000

#define EXCEPTION_SR  (SPR_SR_DME | SPR_SR_IME | SPR_SR_DCE | SPR_SR_ICE | SPR_SR_SM)
#define SYSCALL_SR  (SPR_SR_DME | SPR_SR_IME | SPR_SR_DCE | SPR_SR_ICE | SPR_SR_IEE | SPR_SR_TEE | SPR_SR_SM)

/* ============================================[ tmp store locations ]=== */

/*
 * emergency_print temporary stores
 */
#define EMERGENCY_PRINT_STORE_GPR4	l.sw    0x20(r0),r4
#define EMERGENCY_PRINT_LOAD_GPR4	l.lwz   r4,0x20(r0)

#define EMERGENCY_PRINT_STORE_GPR5	l.sw    0x24(r0),r5
#define EMERGENCY_PRINT_LOAD_GPR5	l.lwz   r5,0x24(r0)

#define EMERGENCY_PRINT_STORE_GPR6	l.sw    0x28(r0),r6
#define EMERGENCY_PRINT_LOAD_GPR6	l.lwz   r6,0x28(r0)

#define EMERGENCY_PRINT_STORE_GPR7	l.sw    0x2c(r0),r7
#define EMERGENCY_PRINT_LOAD_GPR7	l.lwz   r7,0x2c(r0)

#define EMERGENCY_PRINT_STORE_GPR8	l.sw    0x30(r0),r8
#define EMERGENCY_PRINT_LOAD_GPR8	l.lwz   r8,0x30(r0)

#define EMERGENCY_PRINT_STORE_GPR9	l.sw    0x34(r0),r9
#define EMERGENCY_PRINT_LOAD_GPR9	l.lwz   r9,0x34(r0)


/*
 * TLB miss handlers temorary stores
 */
#define EXCEPTION_STORE_GPR9		l.sw    0x10(r0),r9
#define EXCEPTION_LOAD_GPR9		l.lwz   r9,0x10(r0)

#define EXCEPTION_STORE_GPR2		l.sw    0x64(r0),r2
#define EXCEPTION_LOAD_GPR2		l.lwz   r2,0x64(r0)

#define EXCEPTION_STORE_GPR3		l.sw    0x68(r0),r3
#define EXCEPTION_LOAD_GPR3		l.lwz   r3,0x68(r0)

#define EXCEPTION_STORE_GPR4		l.sw    0x6c(r0),r4
#define EXCEPTION_LOAD_GPR4		l.lwz   r4,0x6c(r0)

#define EXCEPTION_STORE_GPR5		l.sw    0x70(r0),r5
#define EXCEPTION_LOAD_GPR5		l.lwz   r5,0x70(r0)

#define EXCEPTION_STORE_GPR6		l.sw    0x74(r0),r6
#define EXCEPTION_LOAD_GPR6		l.lwz   r6,0x74(r0)


/*
 * EXCEPTION_HANDLE temporary stores
 */

#define EXCEPTION_T_STORE_GPR30		l.sw    0x78(r0),r30
#define EXCEPTION_T_LOAD_GPR30(reg)	l.lwz   reg,0x78(r0)

#define EXCEPTION_T_STORE_GPR10		l.sw    0x7c(r0),r10
#define EXCEPTION_T_LOAD_GPR10(reg)	l.lwz   reg,0x7c(r0)

#define EXCEPTION_T_STORE_SP		l.sw	0x80(r0),r1
#define EXCEPTION_T_LOAD_SP(reg)	l.lwz   reg,0x80(r0)

/*
 * For UNHANLDED_EXCEPTION
 */

#define EXCEPTION_T_STORE_GPR31		l.sw    0x84(r0),r31
#define EXCEPTION_T_LOAD_GPR31(reg)	l.lwz   reg,0x84(r0)

/* =========================================================[ macros ]=== */


#define GET_CURRENT_PGD(reg,t1)					\
	LOAD_SYMBOL_2_GPR(reg,current_pgd)			;\
	tophys  (t1,reg)					;\
	l.lwz   reg,0(t1)


/*
 * DSCR: this is a common hook for handling exceptions. it will save
 *       the needed registers, set up stack and pointer to current
 *	 then jump to the handler while enabling MMU
 *
 * PRMS: handler	- a function to jump to. it has to save the
 *			remaining registers to kernel stack, call
 *			appropriate arch-independant exception handler
 *			and finaly jump to ret_from_except
 *
 * PREQ: unchanged state from the time exception happened
 *
 * POST: SAVED the following registers original value
 *	       to the new created exception frame pointed to by r1
 *
 *	 r1  - ksp	pointing to the new (exception) frame
 *	 r4  - EEAR     exception EA
 *	 r10 - current	pointing to current_thread_info struct
 *	 r12 - syscall  0, since we didn't come from syscall
 *	 r13 - temp	it actually contains new SR, not needed anymore
 *	 r31 - handler	address of the handler we'll jump to
 *
 *	 handler has to save remaining registers to the exception
 *	 ksp frame *before* tainting them!
 *
 * NOTE: this function is not reentrant per se. reentrancy is guaranteed
 *       by processor disabling all exceptions/interrupts when exception
 *	 accours.
 *
 * OPTM: no need to make it so wasteful to extract ksp when in user mode
 */

#define EXCEPTION_HANDLE(handler)				\
	EXCEPTION_T_STORE_GPR30					;\
	l.mfspr r30,r0,SPR_ESR_BASE				;\
	l.andi  r30,r30,SPR_SR_SM				;\
	l.sfeqi r30,0						;\
	EXCEPTION_T_STORE_GPR10					;\
	l.bnf   2f                            /* kernel_mode */	;\
	 EXCEPTION_T_STORE_SP                 /* delay slot */	;\
1: /* user_mode:   */						;\
	LOAD_SYMBOL_2_GPR(r1,current_thread_info_set)		;\
	tophys  (r30,r1)					;\
	/* r10: current_thread_info  */				;\
	l.lwz   r10,0(r30)					;\
	tophys  (r30,r10)					;\
	l.lwz   r1,(TI_KSP)(r30)				;\
	/* fall through */					;\
2: /* kernel_mode: */						;\
	/* create new stack frame, save only needed gprs */	;\
	/* r1: KSP, r10: current, r4: EEAR, r31: __pa(KSP) */	;\
	/* r12:	temp, syscall indicator */			;\
	l.addi  r1,r1,-(INT_FRAME_SIZE)				;\
	/* r1 is KSP, r30 is __pa(KSP) */			;\
	tophys  (r30,r1)					;\
	l.sw    PT_GPR12(r30),r12				;\
	l.mfspr r12,r0,SPR_EPCR_BASE				;\
	l.sw    PT_PC(r30),r12					;\
	l.mfspr r12,r0,SPR_ESR_BASE				;\
	l.sw    PT_SR(r30),r12					;\
	/* save r30 */						;\
	EXCEPTION_T_LOAD_GPR30(r12)				;\
	l.sw	PT_GPR30(r30),r12				;\
	/* save r10 as was prior to exception */		;\
	EXCEPTION_T_LOAD_GPR10(r12)				;\
	l.sw	PT_GPR10(r30),r12				;\
	/* save PT_SP as was prior to exception */		;\
	EXCEPTION_T_LOAD_SP(r12)				;\
	l.sw	PT_SP(r30),r12					;\
	/* save exception r4, set r4 = EA */			;\
	l.sw	PT_GPR4(r30),r4					;\
	l.mfspr r4,r0,SPR_EEAR_BASE				;\
	/* r12 == 1 if we come from syscall */			;\
	CLEAR_GPR(r12)						;\
	/* ----- turn on MMU ----- */				;\
	l.ori	r30,r0,(EXCEPTION_SR)				;\
	l.mtspr	r0,r30,SPR_ESR_BASE				;\
	/* r30:	EA address of handler */			;\
	LOAD_SYMBOL_2_GPR(r30,handler)				;\
	l.mtspr r0,r30,SPR_EPCR_BASE				;\
	l.rfe

/*
 * this doesn't work
 *
 *
 * #ifdef CONFIG_JUMP_UPON_UNHANDLED_EXCEPTION
 * #define UNHANDLED_EXCEPTION(handler)				\
 *	l.ori   r3,r0,0x1					;\
 *	l.mtspr r0,r3,SPR_SR					;\
 *      l.movhi r3,hi(0xf0000100)				;\
 *      l.ori   r3,r3,lo(0xf0000100)				;\
 *	l.jr	r3						;\
 *	l.nop	1
 *
 * #endif
 */

/* DSCR: this is the same as EXCEPTION_HANDLE(), we are just
 *       a bit more carefull (if we have a PT_SP or current pointer
 *       corruption) and set them up from 'current_set'
 *
 */
#define UNHANDLED_EXCEPTION(handler)				\
	EXCEPTION_T_STORE_GPR31					;\
	EXCEPTION_T_STORE_GPR10					;\
	EXCEPTION_T_STORE_SP					;\
	/* temporary store r3, r9 into r1, r10 */		;\
	l.addi	r1,r3,0x0					;\
	l.addi	r10,r9,0x0					;\
	/* the string referenced by r3 must be low enough */	;\
	l.jal	_emergency_print				;\
	l.ori	r3,r0,lo(_string_unhandled_exception)		;\
	l.mfspr	r3,r0,SPR_NPC					;\
	l.jal	_emergency_print_nr				;\
	l.andi	r3,r3,0x1f00					;\
	/* the string referenced by r3 must be low enough */	;\
	l.jal	_emergency_print				;\
	l.ori	r3,r0,lo(_string_epc_prefix)			;\
	l.jal	_emergency_print_nr				;\
	l.mfspr	r3,r0,SPR_EPCR_BASE				;\
	l.jal	_emergency_print				;\
	l.ori	r3,r0,lo(_string_nl)				;\
	/* end of printing */					;\
	l.addi	r3,r1,0x0					;\
	l.addi	r9,r10,0x0					;\
	/* extract current, ksp from current_set */		;\
	LOAD_SYMBOL_2_GPR(r1,_unhandled_stack_top)		;\
	LOAD_SYMBOL_2_GPR(r10,init_thread_union)		;\
	/* create new stack frame, save only needed gprs */	;\
	/* r1: KSP, r10: current, r31: __pa(KSP) */		;\
	/* r12:	temp, syscall indicator, r13 temp */		;\
	l.addi  r1,r1,-(INT_FRAME_SIZE)				;\
	/* r1 is KSP, r31 is __pa(KSP) */			;\
	tophys  (r31,r1)					;\
	l.sw    PT_GPR12(r31),r12					;\
	l.mfspr r12,r0,SPR_EPCR_BASE				;\
	l.sw    PT_PC(r31),r12					;\
	l.mfspr r12,r0,SPR_ESR_BASE				;\
	l.sw    PT_SR(r31),r12					;\
	/* save r31 */						;\
	EXCEPTION_T_LOAD_GPR31(r12)				;\
	l.sw	PT_GPR31(r31),r12					;\
	/* save r10 as was prior to exception */		;\
	EXCEPTION_T_LOAD_GPR10(r12)				;\
	l.sw	PT_GPR10(r31),r12					;\
	/* save PT_SP as was prior to exception */			;\
	EXCEPTION_T_LOAD_SP(r12)				;\
	l.sw	PT_SP(r31),r12					;\
	l.sw    PT_GPR13(r31),r13					;\
	/* --> */						;\
	/* save exception r4, set r4 = EA */			;\
	l.sw	PT_GPR4(r31),r4					;\
	l.mfspr r4,r0,SPR_EEAR_BASE				;\
	/* r12 == 1 if we come from syscall */			;\
	CLEAR_GPR(r12)						;\
	/* ----- play a MMU trick ----- */			;\
	l.ori	r31,r0,(EXCEPTION_SR)				;\
	l.mtspr	r0,r31,SPR_ESR_BASE				;\
	/* r31:	EA address of handler */			;\
	LOAD_SYMBOL_2_GPR(r31,handler)				;\
	l.mtspr r0,r31,SPR_EPCR_BASE				;\
	l.rfe

/* =====================================================[ exceptions] === */

/* ---[ 0x100: RESET exception ]----------------------------------------- */
    .org 0x100
	/* Jump to .init code at _start which lives in the .head section
	 * and will be discarded after boot.
	 */
	LOAD_SYMBOL_2_GPR(r15, _start)
	tophys	(r13,r15)			/* MMU disabled */
	l.jr	r13
	 l.nop

/* ---[ 0x200: BUS exception ]------------------------------------------- */
    .org 0x200
_dispatch_bus_fault:
	EXCEPTION_HANDLE(_bus_fault_handler)

/* ---[ 0x300: Data Page Fault exception ]------------------------------- */
    .org 0x300
_dispatch_do_dpage_fault:
//      totaly disable timer interrupt
// 	l.mtspr	r0,r0,SPR_TTMR
//	DEBUG_TLB_PROBE(0x300)
//	EXCEPTION_DEBUG_VALUE_ER_ENABLED(0x300)
	EXCEPTION_HANDLE(_data_page_fault_handler)

/* ---[ 0x400: Insn Page Fault exception ]------------------------------- */
    .org 0x400
_dispatch_do_ipage_fault:
//      totaly disable timer interrupt
//	l.mtspr	r0,r0,SPR_TTMR
//	DEBUG_TLB_PROBE(0x400)
//	EXCEPTION_DEBUG_VALUE_ER_ENABLED(0x400)
	EXCEPTION_HANDLE(_insn_page_fault_handler)

/* ---[ 0x500: Timer exception ]----------------------------------------- */
    .org 0x500
	EXCEPTION_HANDLE(_timer_handler)

/* ---[ 0x600: Aligment exception ]-------------------------------------- */
    .org 0x600
	EXCEPTION_HANDLE(_alignment_handler)

/* ---[ 0x700: Illegal insn exception ]---------------------------------- */
    .org 0x700
	EXCEPTION_HANDLE(_illegal_instruction_handler)

/* ---[ 0x800: External interrupt exception ]---------------------------- */
    .org 0x800
	EXCEPTION_HANDLE(_external_irq_handler)

/* ---[ 0x900: DTLB miss exception ]------------------------------------- */
    .org 0x900
	l.j	boot_dtlb_miss_handler
	l.nop

/* ---[ 0xa00: ITLB miss exception ]------------------------------------- */
    .org 0xa00
	l.j	boot_itlb_miss_handler
	l.nop

/* ---[ 0xb00: Range exception ]----------------------------------------- */
    .org 0xb00
	UNHANDLED_EXCEPTION(_vector_0xb00)

/* ---[ 0xc00: Syscall exception ]--------------------------------------- */
    .org 0xc00
	EXCEPTION_HANDLE(_sys_call_handler)

/* ---[ 0xd00: Trap exception ]------------------------------------------ */
    .org 0xd00
	UNHANDLED_EXCEPTION(_vector_0xd00)

/* ---[ 0xe00: Trap exception ]------------------------------------------ */
    .org 0xe00
//	UNHANDLED_EXCEPTION(_vector_0xe00)
	EXCEPTION_HANDLE(_trap_handler)

/* ---[ 0xf00: Reserved exception ]-------------------------------------- */
    .org 0xf00
	UNHANDLED_EXCEPTION(_vector_0xf00)

/* ---[ 0x1000: Reserved exception ]------------------------------------- */
    .org 0x1000
	UNHANDLED_EXCEPTION(_vector_0x1000)

/* ---[ 0x1100: Reserved exception ]------------------------------------- */
    .org 0x1100
	UNHANDLED_EXCEPTION(_vector_0x1100)

/* ---[ 0x1200: Reserved exception ]------------------------------------- */
    .org 0x1200
	UNHANDLED_EXCEPTION(_vector_0x1200)

/* ---[ 0x1300: Reserved exception ]------------------------------------- */
    .org 0x1300
	UNHANDLED_EXCEPTION(_vector_0x1300)

/* ---[ 0x1400: Reserved exception ]------------------------------------- */
    .org 0x1400
	UNHANDLED_EXCEPTION(_vector_0x1400)

/* ---[ 0x1500: Reserved exception ]------------------------------------- */
    .org 0x1500
	UNHANDLED_EXCEPTION(_vector_0x1500)

/* ---[ 0x1600: Reserved exception ]------------------------------------- */
    .org 0x1600
	UNHANDLED_EXCEPTION(_vector_0x1600)

/* ---[ 0x1700: Reserved exception ]------------------------------------- */
    .org 0x1700
	UNHANDLED_EXCEPTION(_vector_0x1700)

/* ---[ 0x1800: Reserved exception ]------------------------------------- */
    .org 0x1800
	UNHANDLED_EXCEPTION(_vector_0x1800)

/* ---[ 0x1900: Reserved exception ]------------------------------------- */
    .org 0x1900
	UNHANDLED_EXCEPTION(_vector_0x1900)

/* ---[ 0x1a00: Reserved exception ]------------------------------------- */
    .org 0x1a00
	UNHANDLED_EXCEPTION(_vector_0x1a00)

/* ---[ 0x1b00: Reserved exception ]------------------------------------- */
    .org 0x1b00
	UNHANDLED_EXCEPTION(_vector_0x1b00)

/* ---[ 0x1c00: Reserved exception ]------------------------------------- */
    .org 0x1c00
	UNHANDLED_EXCEPTION(_vector_0x1c00)

/* ---[ 0x1d00: Reserved exception ]------------------------------------- */
    .org 0x1d00
	UNHANDLED_EXCEPTION(_vector_0x1d00)

/* ---[ 0x1e00: Reserved exception ]------------------------------------- */
    .org 0x1e00
	UNHANDLED_EXCEPTION(_vector_0x1e00)

/* ---[ 0x1f00: Reserved exception ]------------------------------------- */
    .org 0x1f00
	UNHANDLED_EXCEPTION(_vector_0x1f00)

    .org 0x2000
/* ===================================================[ kernel start ]=== */

/*    .text*/

/* This early stuff belongs in HEAD, but some of the functions below definitely
 * don't... */

	__HEAD
	.global _start
_start:
	/* Init r0 to zero as per spec */
	CLEAR_GPR(r0)

	/* save kernel parameters */
	l.or	r25,r0,r3	/* pointer to fdt */

	/*
	 * ensure a deterministic start
	 */

	l.ori	r3,r0,0x1
	l.mtspr	r0,r3,SPR_SR

	CLEAR_GPR(r1)
	CLEAR_GPR(r2)
	CLEAR_GPR(r3)
	CLEAR_GPR(r4)
	CLEAR_GPR(r5)
	CLEAR_GPR(r6)
	CLEAR_GPR(r7)
	CLEAR_GPR(r8)
	CLEAR_GPR(r9)
	CLEAR_GPR(r10)
	CLEAR_GPR(r11)
	CLEAR_GPR(r12)
	CLEAR_GPR(r13)
	CLEAR_GPR(r14)
	CLEAR_GPR(r15)
	CLEAR_GPR(r16)
	CLEAR_GPR(r17)
	CLEAR_GPR(r18)
	CLEAR_GPR(r19)
	CLEAR_GPR(r20)
	CLEAR_GPR(r21)
	CLEAR_GPR(r22)
	CLEAR_GPR(r23)
	CLEAR_GPR(r24)
	CLEAR_GPR(r26)
	CLEAR_GPR(r27)
	CLEAR_GPR(r28)
	CLEAR_GPR(r29)
	CLEAR_GPR(r30)
	CLEAR_GPR(r31)

	/*
	 * set up initial ksp and current
	 */
	/* setup kernel stack */
	LOAD_SYMBOL_2_GPR(r1,init_thread_union + THREAD_SIZE)
	LOAD_SYMBOL_2_GPR(r10,init_thread_union)	// setup current
	tophys	(r31,r10)
	l.sw	TI_KSP(r31), r1

	l.ori	r4,r0,0x0


	/*
	 * .data contains initialized data,
	 * .bss contains uninitialized data - clear it up
	 */
clear_bss:
	LOAD_SYMBOL_2_GPR(r24, __bss_start)
	LOAD_SYMBOL_2_GPR(r26, _end)
	tophys(r28,r24)
	tophys(r30,r26)
	CLEAR_GPR(r24)
	CLEAR_GPR(r26)
1:
	l.sw    (0)(r28),r0
	l.sfltu r28,r30
	l.bf    1b
	l.addi  r28,r28,4

enable_ic:
	l.jal	_ic_enable
	 l.nop

enable_dc:
	l.jal	_dc_enable
	 l.nop

flush_tlb:
	l.jal	_flush_tlb
	 l.nop

/* The MMU needs to be enabled before or32_early_setup is called */

enable_mmu:
	/*
	 * enable dmmu & immu
	 * SR[5] = 0, SR[6] = 0, 6th and 7th bit of SR set to 0
	 */
	l.mfspr	r30,r0,SPR_SR
	l.movhi	r28,hi(SPR_SR_DME | SPR_SR_IME)
	l.ori	r28,r28,lo(SPR_SR_DME | SPR_SR_IME)
	l.or	r30,r30,r28
	l.mtspr	r0,r30,SPR_SR
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop

	// reset the simulation counters
	l.nop 5

	/* check fdt header magic word */
	l.lwz	r3,0(r25)	/* load magic from fdt into r3 */
	l.movhi	r4,hi(OF_DT_HEADER)
	l.ori	r4,r4,lo(OF_DT_HEADER)
	l.sfeq	r3,r4
	l.bf	_fdt_found
	 l.nop
	/* magic number mismatch, set fdt pointer to null */
	l.or	r25,r0,r0
_fdt_found:
	/* pass fdt pointer to or32_early_setup in r3 */
	l.or	r3,r0,r25
	LOAD_SYMBOL_2_GPR(r24, or32_early_setup)
	l.jalr r24
	 l.nop

clear_regs:
	/*
	 * clear all GPRS to increase determinism
	 */
	CLEAR_GPR(r2)
	CLEAR_GPR(r3)
	CLEAR_GPR(r4)
	CLEAR_GPR(r5)
	CLEAR_GPR(r6)
	CLEAR_GPR(r7)
	CLEAR_GPR(r8)
	CLEAR_GPR(r9)
	CLEAR_GPR(r11)
	CLEAR_GPR(r12)
	CLEAR_GPR(r13)
	CLEAR_GPR(r14)
	CLEAR_GPR(r15)
	CLEAR_GPR(r16)
	CLEAR_GPR(r17)
	CLEAR_GPR(r18)
	CLEAR_GPR(r19)
	CLEAR_GPR(r20)
	CLEAR_GPR(r21)
	CLEAR_GPR(r22)
	CLEAR_GPR(r23)
	CLEAR_GPR(r24)
	CLEAR_GPR(r25)
	CLEAR_GPR(r26)
	CLEAR_GPR(r27)
	CLEAR_GPR(r28)
	CLEAR_GPR(r29)
	CLEAR_GPR(r30)
	CLEAR_GPR(r31)

jump_start_kernel:
	/*
	 * jump to kernel entry (start_kernel)
	 */
	LOAD_SYMBOL_2_GPR(r30, start_kernel)
	l.jr    r30
	 l.nop

_flush_tlb:
	/*
	 *  I N V A L I D A T E   T L B   e n t r i e s
	 */
	LOAD_SYMBOL_2_GPR(r5,SPR_DTLBMR_BASE(0))
	LOAD_SYMBOL_2_GPR(r6,SPR_ITLBMR_BASE(0))
	l.addi	r7,r0,128 /* Maximum number of sets */
1:
	l.mtspr	r5,r0,0x0
	l.mtspr	r6,r0,0x0

	l.addi	r5,r5,1
	l.addi	r6,r6,1
	l.sfeq	r7,r0
	l.bnf	1b
	 l.addi	r7,r7,-1

	l.jr	r9
	 l.nop

/* ========================================[ cache ]=== */

	/* aligment here so we don't change memory offsets with
	 * memory controler defined
	 */
	.align 0x2000

_ic_enable:
	/* Check if IC present and skip enabling otherwise */
	l.mfspr r24,r0,SPR_UPR
	l.andi  r26,r24,SPR_UPR_ICP
	l.sfeq  r26,r0
	l.bf	9f
	l.nop

	/* Disable IC */
	l.mfspr r6,r0,SPR_SR
	l.addi  r5,r0,-1
	l.xori  r5,r5,SPR_SR_ICE
	l.and   r5,r6,r5
	l.mtspr r0,r5,SPR_SR

	/* Establish cache block size
	   If BS=0, 16;
	   If BS=1, 32;
	   r14 contain block size
	*/
	l.mfspr r24,r0,SPR_ICCFGR
	l.andi	r26,r24,SPR_ICCFGR_CBS
	l.srli	r28,r26,7
	l.ori	r30,r0,16
	l.sll	r14,r30,r28

	/* Establish number of cache sets
	   r16 contains number of cache sets
	   r28 contains log(# of cache sets)
	*/
	l.andi  r26,r24,SPR_ICCFGR_NCS
	l.srli 	r28,r26,3
	l.ori   r30,r0,1
	l.sll   r16,r30,r28

	/* Invalidate IC */
	l.addi  r6,r0,0
	l.sll   r5,r14,r28
//        l.mul   r5,r14,r16
//	l.trap  1
//	l.addi  r5,r0,IC_SIZE
1:
	l.mtspr r0,r6,SPR_ICBIR
	l.sfne  r6,r5
	l.bf    1b
	l.add   r6,r6,r14
 //       l.addi   r6,r6,IC_LINE

	/* Enable IC */
	l.mfspr r6,r0,SPR_SR
	l.ori   r6,r6,SPR_SR_ICE
	l.mtspr r0,r6,SPR_SR
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
	l.nop
9:
	l.jr    r9
	l.nop

_dc_enable:
	/* Check if DC present and skip enabling otherwise */
	l.mfspr r24,r0,SPR_UPR
	l.andi  r26,r24,SPR_UPR_DCP
	l.sfeq  r26,r0
	l.bf	9f
	l.nop

	/* Disable DC */
	l.mfspr r6,r0,SPR_SR
	l.addi  r5,r0,-1
	l.xori  r5,r5,SPR_SR_DCE
	l.and   r5,r6,r5
	l.mtspr r0,r5,SPR_SR

	/* Establish cache block size
	   If BS=0, 16;
	   If BS=1, 32;
	   r14 contain block size
	*/
	l.mfspr r24,r0,SPR_DCCFGR
	l.andi	r26,r24,SPR_DCCFGR_CBS
	l.srli	r28,r26,7
	l.ori	r30,r0,16
	l.sll	r14,r30,r28

	/* Establish number of cache sets
	   r16 contains number of cache sets
	   r28 contains log(# of cache sets)
	*/
	l.andi  r26,r24,SPR_DCCFGR_NCS
	l.srli 	r28,r26,3
	l.ori   r30,r0,1
	l.sll   r16,r30,r28

	/* Invalidate DC */
	l.addi  r6,r0,0
	l.sll   r5,r14,r28
1:
	l.mtspr r0,r6,SPR_DCBIR
	l.sfne  r6,r5
	l.bf    1b
	l.add   r6,r6,r14

	/* Enable DC */
	l.mfspr r6,r0,SPR_SR
	l.ori   r6,r6,SPR_SR_DCE
	l.mtspr r0,r6,SPR_SR
9:
	l.jr    r9
	l.nop

/* ===============================================[ page table masks ]=== */

#define DTLB_UP_CONVERT_MASK  0x3fa
#define ITLB_UP_CONVERT_MASK  0x3a

/* for SMP we'd have (this is a bit subtle, CC must be always set
 * for SMP, but since we have _PAGE_PRESENT bit always defined
 * we can just modify the mask)
 */
#define DTLB_SMP_CONVERT_MASK  0x3fb
#define ITLB_SMP_CONVERT_MASK  0x3b

/* ---[ boot dtlb miss handler ]----------------------------------------- */

boot_dtlb_miss_handler:

/* mask for DTLB_MR register: - (0) sets V (valid) bit,
 *                            - (31-12) sets bits belonging to VPN (31-12)
 */
#define DTLB_MR_MASK 0xfffff001

/* mask for DTLB_TR register: - (2) sets CI (cache inhibit) bit,
 *			      - (4) sets A (access) bit,
 *                            - (5) sets D (dirty) bit,
 *                            - (8) sets SRE (superuser read) bit
 *                            - (9) sets SWE (superuser write) bit
 *                            - (31-12) sets bits belonging to VPN (31-12)
 */
#define DTLB_TR_MASK 0xfffff332

/* These are for masking out the VPN/PPN value from the MR/TR registers...
 * it's not the same as the PFN */
#define VPN_MASK 0xfffff000
#define PPN_MASK 0xfffff000


	EXCEPTION_STORE_GPR6

#if 0
	l.mfspr r6,r0,SPR_ESR_BASE	   //
	l.andi  r6,r6,SPR_SR_SM            // are we in kernel mode ?
	l.sfeqi r6,0                       // r6 == 0x1 --> SM
	l.bf    exit_with_no_dtranslation  //
	l.nop
#endif

	/* this could be optimized by moving storing of
	 * non r6 registers here, and jumping r6 restore
	 * if not in supervisor mode
	 */

	EXCEPTION_STORE_GPR2
	EXCEPTION_STORE_GPR3
	EXCEPTION_STORE_GPR4
	EXCEPTION_STORE_GPR5

	l.mfspr r4,r0,SPR_EEAR_BASE        // get the offending EA

immediate_translation:
	CLEAR_GPR(r6)

	l.srli	r3,r4,0xd                  // r3 <- r4 / 8192 (sets are relative to page size (8Kb) NOT VPN size (4Kb)

	l.mfspr r6, r0, SPR_DMMUCFGR
	l.andi	r6, r6, SPR_DMMUCFGR_NTS
	l.srli	r6, r6, SPR_DMMUCFGR_NTS_OFF
	l.ori	r5, r0, 0x1
	l.sll	r5, r5, r6 	// r5 = number DMMU sets
	l.addi	r6, r5, -1  	// r6 = nsets mask
	l.and	r2, r3, r6	// r2 <- r3 % NSETS_MASK

	l.or    r6,r6,r4                   // r6 <- r4
	l.ori   r6,r6,~(VPN_MASK)          // r6 <- VPN :VPN .xfff - clear up lo(r6) to 0x**** *fff
	l.movhi r5,hi(DTLB_MR_MASK)        // r5 <- ffff:0000.x000
	l.ori   r5,r5,lo(DTLB_MR_MASK)     // r5 <- ffff:1111.x001 - apply DTLB_MR_MASK
	l.and   r5,r5,r6                   // r5 <- VPN :VPN .x001 - we have DTLBMR entry
	l.mtspr r2,r5,SPR_DTLBMR_BASE(0)   // set DTLBMR

	/* set up DTLB with no translation for EA <= 0xbfffffff */
	LOAD_SYMBOL_2_GPR(r6,0xbfffffff)
	l.sfgeu  r6,r4                     // flag if r6 >= r4 (if 0xbfffffff >= EA)
	l.bf     1f                        // goto out
	l.and    r3,r4,r4                  // delay slot :: 24 <- r4 (if flag==1)

	tophys(r3,r4)                      // r3 <- PA
1:
	l.ori   r3,r3,~(PPN_MASK)          // r3 <- PPN :PPN .xfff - clear up lo(r6) to 0x**** *fff
	l.movhi r5,hi(DTLB_TR_MASK)        // r5 <- ffff:0000.x000
	l.ori   r5,r5,lo(DTLB_TR_MASK)     // r5 <- ffff:1111.x330 - apply DTLB_MR_MASK
	l.and   r5,r5,r3                   // r5 <- PPN :PPN .x330 - we have DTLBTR entry
	l.mtspr r2,r5,SPR_DTLBTR_BASE(0)   // set DTLBTR

	EXCEPTION_LOAD_GPR6
	EXCEPTION_LOAD_GPR5
	EXCEPTION_LOAD_GPR4
	EXCEPTION_LOAD_GPR3
	EXCEPTION_LOAD_GPR2

	l.rfe                              // SR <- ESR, PC <- EPC

exit_with_no_dtranslation:
	/* EA out of memory or not in supervisor mode */
	EXCEPTION_LOAD_GPR6
	EXCEPTION_LOAD_GPR4
	l.j	_dispatch_bus_fault

/* ---[ boot itlb miss handler ]----------------------------------------- */

boot_itlb_miss_handler:

/* mask for ITLB_MR register: - sets V (valid) bit,
 *                            - sets bits belonging to VPN (15-12)
 */
#define ITLB_MR_MASK 0xfffff001

/* mask for ITLB_TR register: - sets A (access) bit,
 *                            - sets SXE (superuser execute) bit
 *                            - sets bits belonging to VPN (15-12)
 */
#define ITLB_TR_MASK 0xfffff050

/*
#define VPN_MASK 0xffffe000
#define PPN_MASK 0xffffe000
*/



	EXCEPTION_STORE_GPR2
	EXCEPTION_STORE_GPR3
	EXCEPTION_STORE_GPR4
	EXCEPTION_STORE_GPR5
	EXCEPTION_STORE_GPR6

#if 0
	l.mfspr r6,r0,SPR_ESR_BASE         //
	l.andi  r6,r6,SPR_SR_SM            // are we in kernel mode ?
	l.sfeqi r6,0                       // r6 == 0x1 --> SM
	l.bf    exit_with_no_itranslation
	l.nop
#endif


	l.mfspr r4,r0,SPR_EEAR_BASE        // get the offending EA

earlyearly:
	CLEAR_GPR(r6)

	l.srli  r3,r4,0xd                  // r3 <- r4 / 8192 (sets are relative to page size (8Kb) NOT VPN size (4Kb)

	l.mfspr r6, r0, SPR_IMMUCFGR
	l.andi	r6, r6, SPR_IMMUCFGR_NTS
	l.srli	r6, r6, SPR_IMMUCFGR_NTS_OFF
	l.ori	r5, r0, 0x1
	l.sll	r5, r5, r6 	// r5 = number IMMU sets from IMMUCFGR
	l.addi	r6, r5, -1  	// r6 = nsets mask
	l.and	r2, r3, r6	// r2 <- r3 % NSETS_MASK

	l.or    r6,r6,r4                   // r6 <- r4
	l.ori   r6,r6,~(VPN_MASK)          // r6 <- VPN :VPN .xfff - clear up lo(r6) to 0x**** *fff
	l.movhi r5,hi(ITLB_MR_MASK)        // r5 <- ffff:0000.x000
	l.ori   r5,r5,lo(ITLB_MR_MASK)     // r5 <- ffff:1111.x001 - apply ITLB_MR_MASK
	l.and   r5,r5,r6                   // r5 <- VPN :VPN .x001 - we have ITLBMR entry
	l.mtspr r2,r5,SPR_ITLBMR_BASE(0)   // set ITLBMR

	/*
	 * set up ITLB with no translation for EA <= 0x0fffffff
	 *
	 * we need this for head.S mapping (EA = PA). if we move all functions
	 * which run with mmu enabled into entry.S, we might be able to eliminate this.
	 *
	 */
	LOAD_SYMBOL_2_GPR(r6,0x0fffffff)
	l.sfgeu  r6,r4                     // flag if r6 >= r4 (if 0xb0ffffff >= EA)
	l.bf     1f                        // goto out
	l.and    r3,r4,r4                  // delay slot :: 24 <- r4 (if flag==1)

	tophys(r3,r4)                      // r3 <- PA
1:
	l.ori   r3,r3,~(PPN_MASK)          // r3 <- PPN :PPN .xfff - clear up lo(r6) to 0x**** *fff
	l.movhi r5,hi(ITLB_TR_MASK)        // r5 <- ffff:0000.x000
	l.ori   r5,r5,lo(ITLB_TR_MASK)     // r5 <- ffff:1111.x050 - apply ITLB_MR_MASK
	l.and   r5,r5,r3                   // r5 <- PPN :PPN .x050 - we have ITLBTR entry
	l.mtspr r2,r5,SPR_ITLBTR_BASE(0)   // set ITLBTR

	EXCEPTION_LOAD_GPR6
	EXCEPTION_LOAD_GPR5
	EXCEPTION_LOAD_GPR4
	EXCEPTION_LOAD_GPR3
	EXCEPTION_LOAD_GPR2

	l.rfe                              // SR <- ESR, PC <- EPC

exit_with_no_itranslation:
	EXCEPTION_LOAD_GPR4
	EXCEPTION_LOAD_GPR6
	l.j    _dispatch_bus_fault
	l.nop

/* ====================================================================== */
/*
 * Stuff below here shouldn't go into .head section... maybe this stuff
 * can be moved to entry.S ???
 */

/* ==============================================[ DTLB miss handler ]=== */

/*
 * Comments:
 *   Exception handlers are entered with MMU off so the following handler
 *   needs to use physical addressing
 *
 */

	.text
ENTRY(dtlb_miss_handler)
	EXCEPTION_STORE_GPR2
	EXCEPTION_STORE_GPR3
	EXCEPTION_STORE_GPR4
	/*
	 * get EA of the miss
	 */
	l.mfspr	r2,r0,SPR_EEAR_BASE
	/*
	 * pmd = (pmd_t *)(current_pgd + pgd_index(daddr));
	 */
	GET_CURRENT_PGD(r3,r4)		// r3 is current_pgd, r4 is temp
	l.srli	r4,r2,0x18		// >> PAGE_SHIFT + (PAGE_SHIFT - 2)
	l.slli	r4,r4,0x2		// to get address << 2
	l.add	r3,r4,r3		// r4 is pgd_index(daddr)
	/*
	 * if (pmd_none(*pmd))
	 *   goto pmd_none:
	 */
	tophys	(r4,r3)
	l.lwz	r3,0x0(r4)		// get *pmd value
	l.sfne	r3,r0
	l.bnf	d_pmd_none
	 l.addi	r3,r0,0xffffe000	// PAGE_MASK

d_pmd_good:
	/*
	 * pte = *pte_offset(pmd, daddr);
	 */
	l.lwz	r4,0x0(r4)		// get **pmd value
	l.and	r4,r4,r3		// & PAGE_MASK
	l.srli	r2,r2,0xd		// >> PAGE_SHIFT, r2 == EEAR
	l.andi	r3,r2,0x7ff		// (1UL << PAGE_SHIFT - 2) - 1
	l.slli	r3,r3,0x2		// to get address << 2
	l.add	r3,r3,r4
	l.lwz	r3,0x0(r3)		// this is pte at last
	/*
	 * if (!pte_present(pte))
	 */
	l.andi	r4,r3,0x1
	l.sfne	r4,r0			// is pte present
	l.bnf	d_pte_not_present
	l.addi	r4,r0,0xffffe3fa	// PAGE_MASK | DTLB_UP_CONVERT_MASK
	/*
	 * fill DTLB TR register
	 */
	l.and	r4,r3,r4		// apply the mask
	// Determine number of DMMU sets
	l.mfspr r2, r0, SPR_DMMUCFGR
	l.andi	r2, r2, SPR_DMMUCFGR_NTS
	l.srli	r2, r2, SPR_DMMUCFGR_NTS_OFF
	l.ori	r3, r0, 0x1
	l.sll	r3, r3, r2 	// r3 = number DMMU sets DMMUCFGR
	l.addi	r2, r3, -1  	// r2 = nsets mask
	l.mfspr	r3, r0, SPR_EEAR_BASE
	l.srli	r3, r3, 0xd	// >> PAGE_SHIFT
	l.and	r2, r3, r2	// calc offset:	 & (NUM_TLB_ENTRIES-1)
	                                                   //NUM_TLB_ENTRIES
	l.mtspr	r2,r4,SPR_DTLBTR_BASE(0)
	/*
	 * fill DTLB MR register
	 */
	l.slli	r3, r3, 0xd		/* << PAGE_SHIFT => EA & PAGE_MASK */
	l.ori	r4,r3,0x1		// set hardware valid bit: DTBL_MR entry
	l.mtspr	r2,r4,SPR_DTLBMR_BASE(0)

	EXCEPTION_LOAD_GPR2
	EXCEPTION_LOAD_GPR3
	EXCEPTION_LOAD_GPR4
	l.rfe
d_pmd_none:
d_pte_not_present:
	EXCEPTION_LOAD_GPR2
	EXCEPTION_LOAD_GPR3
	EXCEPTION_LOAD_GPR4
	EXCEPTION_HANDLE(_dtlb_miss_page_fault_handler)

/* ==============================================[ ITLB miss handler ]=== */
ENTRY(itlb_miss_handler)
	EXCEPTION_STORE_GPR2
	EXCEPTION_STORE_GPR3
	EXCEPTION_STORE_GPR4
	/*
	 * get EA of the miss
	 */
	l.mfspr	r2,r0,SPR_EEAR_BASE

	/*
	 * pmd = (pmd_t *)(current_pgd + pgd_index(daddr));
	 *
	 */
	GET_CURRENT_PGD(r3,r4)		// r3 is current_pgd, r5 is temp
	l.srli	r4,r2,0x18		// >> PAGE_SHIFT + (PAGE_SHIFT - 2)
	l.slli	r4,r4,0x2		// to get address << 2
	l.add	r3,r4,r3		// r4 is pgd_index(daddr)
	/*
	 * if (pmd_none(*pmd))
	 *   goto pmd_none:
	 */
	tophys	(r4,r3)
	l.lwz	r3,0x0(r4)		// get *pmd value
	l.sfne	r3,r0
	l.bnf	i_pmd_none
	 l.addi	r3,r0,0xffffe000	// PAGE_MASK

i_pmd_good:
	/*
	 * pte = *pte_offset(pmd, iaddr);
	 *
	 */
	l.lwz	r4,0x0(r4)		// get **pmd value
	l.and	r4,r4,r3		// & PAGE_MASK
	l.srli	r2,r2,0xd		// >> PAGE_SHIFT, r2 == EEAR
	l.andi	r3,r2,0x7ff		// (1UL << PAGE_SHIFT - 2) - 1
	l.slli	r3,r3,0x2		// to get address << 2
	l.add	r3,r3,r4
	l.lwz	r3,0x0(r3)		// this is pte at last
	/*
	 * if (!pte_present(pte))
	 *
	 */
	l.andi	r4,r3,0x1
	l.sfne	r4,r0			// is pte present
	l.bnf	i_pte_not_present
	 l.addi	r4,r0,0xffffe03a	// PAGE_MASK | ITLB_UP_CONVERT_MASK
	/*
	 * fill ITLB TR register
	 */
	l.and	r4,r3,r4		// apply the mask
	l.andi	r3,r3,0x7c0		// _PAGE_EXEC | _PAGE_SRE | _PAGE_SWE |  _PAGE_URE | _PAGE_UWE
	l.sfeq	r3,r0
	l.bf	itlb_tr_fill //_workaround
	// Determine number of IMMU sets
	l.mfspr r2, r0, SPR_IMMUCFGR
	l.andi	r2, r2, SPR_IMMUCFGR_NTS
	l.srli	r2, r2, SPR_IMMUCFGR_NTS_OFF
	l.ori	r3, r0, 0x1
	l.sll	r3, r3, r2 	// r3 = number IMMU sets IMMUCFGR
	l.addi	r2, r3, -1  	// r2 = nsets mask
	l.mfspr	r3, r0, SPR_EEAR_BASE
	l.srli	r3, r3, 0xd	// >> PAGE_SHIFT
	l.and	r2, r3, r2	// calc offset:	 & (NUM_TLB_ENTRIES-1)

/*
 * __PHX__ :: fixme
 * we should not just blindly set executable flags,
 * but it does help with ping. the clean way would be to find out
 * (and fix it) why stack doesn't have execution permissions
 */

itlb_tr_fill_workaround:
	l.ori	r4,r4,0xc0		// | (SPR_ITLBTR_UXE | ITLBTR_SXE)
itlb_tr_fill:
	l.mtspr	r2,r4,SPR_ITLBTR_BASE(0)
	/*
	 * fill DTLB MR register
	 */
	l.slli	r3, r3, 0xd		/* << PAGE_SHIFT => EA & PAGE_MASK */
	l.ori	r4,r3,0x1		// set hardware valid bit: ITBL_MR entry
	l.mtspr	r2,r4,SPR_ITLBMR_BASE(0)

	EXCEPTION_LOAD_GPR2
	EXCEPTION_LOAD_GPR3
	EXCEPTION_LOAD_GPR4
	l.rfe

i_pmd_none:
i_pte_not_present:
	EXCEPTION_LOAD_GPR2
	EXCEPTION_LOAD_GPR3
	EXCEPTION_LOAD_GPR4
	EXCEPTION_HANDLE(_itlb_miss_page_fault_handler)

/* ==============================================[ boot tlb handlers ]=== */


/* =================================================[ debugging aids ]=== */

	.align 64
_immu_trampoline:
	.space 64
_immu_trampoline_top:

#define TRAMP_SLOT_0		(0x0)
#define TRAMP_SLOT_1		(0x4)
#define TRAMP_SLOT_2		(0x8)
#define TRAMP_SLOT_3		(0xc)
#define TRAMP_SLOT_4		(0x10)
#define TRAMP_SLOT_5		(0x14)
#define TRAMP_FRAME_SIZE	(0x18)

ENTRY(_immu_trampoline_workaround)
	// r2 EEA
	// r6 is physical EEA
	tophys(r6,r2)

	LOAD_SYMBOL_2_GPR(r5,_immu_trampoline)
	tophys	(r3,r5)			// r3 is trampoline (physical)

	LOAD_SYMBOL_2_GPR(r4,0x15000000)
	l.sw	TRAMP_SLOT_0(r3),r4
	l.sw	TRAMP_SLOT_1(r3),r4
	l.sw	TRAMP_SLOT_4(r3),r4
	l.sw	TRAMP_SLOT_5(r3),r4

					// EPC = EEA - 0x4
	l.lwz	r4,0x0(r6)		// load op @ EEA + 0x0 (fc address)
	l.sw	TRAMP_SLOT_3(r3),r4	// store it to _immu_trampoline_data
	l.lwz	r4,-0x4(r6)		// load op @ EEA - 0x4 (f8 address)
	l.sw	TRAMP_SLOT_2(r3),r4	// store it to _immu_trampoline_data

	l.srli  r5,r4,26                // check opcode for write access
	l.sfeqi r5,0                    // l.j
	l.bf    0f
	l.sfeqi r5,0x11                 // l.jr
	l.bf    1f
	l.sfeqi r5,1                    // l.jal
	l.bf    2f
	l.sfeqi r5,0x12                 // l.jalr
	l.bf    3f
	l.sfeqi r5,3                    // l.bnf
	l.bf    4f
	l.sfeqi r5,4                    // l.bf
	l.bf    5f
99:
	l.nop
	l.j	99b			// should never happen
	l.nop	1

	// r2 is EEA
	// r3 is trampoline address (physical)
	// r4 is instruction
	// r6 is physical(EEA)
	//
	// r5

2:	// l.jal

	/* 19 20 aa aa	l.movhi r9,0xaaaa
	 * a9 29 bb bb  l.ori	r9,0xbbbb
	 *
	 * where 0xaaaabbbb is EEA + 0x4 shifted right 2
	 */

	l.addi	r6,r2,0x4		// this is 0xaaaabbbb

					// l.movhi r9,0xaaaa
	l.ori	r5,r0,0x1920		// 0x1920 == l.movhi r9
	l.sh	(TRAMP_SLOT_0+0x0)(r3),r5
	l.srli	r5,r6,16
	l.sh	(TRAMP_SLOT_0+0x2)(r3),r5

					// l.ori   r9,0xbbbb
	l.ori	r5,r0,0xa929		// 0xa929 == l.ori r9
	l.sh	(TRAMP_SLOT_1+0x0)(r3),r5
	l.andi	r5,r6,0xffff
	l.sh	(TRAMP_SLOT_1+0x2)(r3),r5

	/* falthrough, need to set up new jump offset */


0:	// l.j
	l.slli	r6,r4,6			// original offset shifted left 6 - 2
//	l.srli	r6,r6,6			// original offset shifted right 2

	l.slli	r4,r2,4			// old jump position: EEA shifted left 4
//	l.srli	r4,r4,6			// old jump position: shifted right 2

	l.addi	r5,r3,0xc		// new jump position (physical)
	l.slli	r5,r5,4			// new jump position: shifted left 4

	// calculate new jump offset
	// new_off = old_off + (old_jump - new_jump)

	l.sub	r5,r4,r5		// old_jump - new_jump
	l.add	r5,r6,r5		// orig_off + (old_jump - new_jump)
	l.srli	r5,r5,6			// new offset shifted right 2

	// r5 is new jump offset
					// l.j has opcode 0x0...
	l.sw	TRAMP_SLOT_2(r3),r5	// write it back

	l.j	trampoline_out
	l.nop

/* ----------------------------- */

3:	// l.jalr

	/* 19 20 aa aa	l.movhi r9,0xaaaa
	 * a9 29 bb bb  l.ori	r9,0xbbbb
	 *
	 * where 0xaaaabbbb is EEA + 0x4 shifted right 2
	 */

	l.addi	r6,r2,0x4		// this is 0xaaaabbbb

					// l.movhi r9,0xaaaa
	l.ori	r5,r0,0x1920		// 0x1920 == l.movhi r9
	l.sh	(TRAMP_SLOT_0+0x0)(r3),r5
	l.srli	r5,r6,16
	l.sh	(TRAMP_SLOT_0+0x2)(r3),r5

					// l.ori   r9,0xbbbb
	l.ori	r5,r0,0xa929		// 0xa929 == l.ori r9
	l.sh	(TRAMP_SLOT_1+0x0)(r3),r5
	l.andi	r5,r6,0xffff
	l.sh	(TRAMP_SLOT_1+0x2)(r3),r5

	l.lhz	r5,(TRAMP_SLOT_2+0x0)(r3)	// load hi part of jump instruction
	l.andi	r5,r5,0x3ff		// clear out opcode part
	l.ori	r5,r5,0x4400		// opcode changed from l.jalr -> l.jr
	l.sh	(TRAMP_SLOT_2+0x0)(r3),r5 // write it back

	/* falthrough */

1:	// l.jr
	l.j	trampoline_out
	l.nop

/* ----------------------------- */

4:	// l.bnf
5:	// l.bf
	l.slli	r6,r4,6			// original offset shifted left 6 - 2
//	l.srli	r6,r6,6			// original offset shifted right 2

	l.slli	r4,r2,4			// old jump position: EEA shifted left 4
//	l.srli	r4,r4,6			// old jump position: shifted right 2

	l.addi	r5,r3,0xc		// new jump position (physical)
	l.slli	r5,r5,4			// new jump position: shifted left 4

	// calculate new jump offset
	// new_off = old_off + (old_jump - new_jump)

	l.add	r6,r6,r4		// (orig_off + old_jump)
	l.sub	r6,r6,r5		// (orig_off + old_jump) - new_jump
	l.srli	r6,r6,6			// new offset shifted right 2

	// r6 is new jump offset
	l.lwz	r4,(TRAMP_SLOT_2+0x0)(r3)	// load jump instruction
	l.srli	r4,r4,16
	l.andi	r4,r4,0xfc00		// get opcode part
	l.slli	r4,r4,16
	l.or	r6,r4,r6		// l.b(n)f new offset
	l.sw	TRAMP_SLOT_2(r3),r6	// write it back

	/* we need to add l.j to EEA + 0x8 */
	tophys	(r4,r2)			// may not be needed (due to shifts down_
	l.addi	r4,r4,(0x8 - 0x8)	// jump target = r2 + 0x8 (compensate for 0x8)
					// jump position = r5 + 0x8 (0x8 compensated)
	l.sub	r4,r4,r5		// jump offset = target - new_position + 0x8

	l.slli	r4,r4,4			// the amount of info in imediate of jump
	l.srli	r4,r4,6			// jump instruction with offset
	l.sw	TRAMP_SLOT_4(r3),r4	// write it to 4th slot

	/* fallthrough */

trampoline_out:
	// set up new EPC to point to our trampoline code
	LOAD_SYMBOL_2_GPR(r5,_immu_trampoline)
	l.mtspr	r0,r5,SPR_EPCR_BASE

	// immu_trampoline is (4x) CACHE_LINE aligned
	// and only 6 instructions long,
	// so we need to invalidate only 2 lines

	/* Establish cache block size
	   If BS=0, 16;
	   If BS=1, 32;
	   r14 contain block size
	*/
	l.mfspr r21,r0,SPR_ICCFGR
	l.andi	r21,r21,SPR_ICCFGR_CBS
	l.srli	r21,r21,7
	l.ori	r23,r0,16
	l.sll	r14,r23,r21

	l.mtspr	r0,r5,SPR_ICBIR
	l.add	r5,r5,r14
	l.mtspr	r0,r5,SPR_ICBIR

	l.jr	r9
	l.nop


/*
 * DSCR: prints a string referenced by r3.
 *
 * PRMS: r3     	- address of the first character of null
 *			terminated string to be printed
 *
 * PREQ: UART at UART_BASE_ADD has to be initialized
 *
 * POST: caller should be aware that r3, r9 are changed
 */
ENTRY(_emergency_print)
	EMERGENCY_PRINT_STORE_GPR4
	EMERGENCY_PRINT_STORE_GPR5
	EMERGENCY_PRINT_STORE_GPR6
	EMERGENCY_PRINT_STORE_GPR7
2:
	l.lbz	r7,0(r3)
	l.sfeq	r7,r0
	l.bf	9f
	l.nop

// putc:
	l.movhi r4,hi(UART_BASE_ADD)

	l.addi  r6,r0,0x20
1:      l.lbz   r5,5(r4)
	l.andi  r5,r5,0x20
	l.sfeq  r5,r6
	l.bnf   1b
	l.nop

	l.sb    0(r4),r7

	l.addi  r6,r0,0x60
1:      l.lbz   r5,5(r4)
	l.andi  r5,r5,0x60
	l.sfeq  r5,r6
	l.bnf   1b
	l.nop

	/* next character */
	l.j	2b
	l.addi	r3,r3,0x1

9:
	EMERGENCY_PRINT_LOAD_GPR7
	EMERGENCY_PRINT_LOAD_GPR6
	EMERGENCY_PRINT_LOAD_GPR5
	EMERGENCY_PRINT_LOAD_GPR4
	l.jr	r9
	l.nop

ENTRY(_emergency_print_nr)
	EMERGENCY_PRINT_STORE_GPR4
	EMERGENCY_PRINT_STORE_GPR5
	EMERGENCY_PRINT_STORE_GPR6
	EMERGENCY_PRINT_STORE_GPR7
	EMERGENCY_PRINT_STORE_GPR8

	l.addi	r8,r0,32		// shift register

1:	/* remove leading zeros */
	l.addi	r8,r8,-0x4
	l.srl	r7,r3,r8
	l.andi	r7,r7,0xf

	/* don't skip the last zero if number == 0x0 */
	l.sfeqi	r8,0x4
	l.bf	2f
	l.nop

	l.sfeq	r7,r0
	l.bf	1b
	l.nop

2:
	l.srl	r7,r3,r8

	l.andi	r7,r7,0xf
	l.sflts	r8,r0
	l.bf	9f

	l.sfgtui r7,0x9
	l.bnf	8f
	l.nop
	l.addi	r7,r7,0x27

8:
	l.addi	r7,r7,0x30
// putc:
	l.movhi r4,hi(UART_BASE_ADD)

	l.addi  r6,r0,0x20
1:      l.lbz   r5,5(r4)
	l.andi  r5,r5,0x20
	l.sfeq  r5,r6
	l.bnf   1b
	l.nop

	l.sb    0(r4),r7

	l.addi  r6,r0,0x60
1:      l.lbz   r5,5(r4)
	l.andi  r5,r5,0x60
	l.sfeq  r5,r6
	l.bnf   1b
	l.nop

	/* next character */
	l.j	2b
	l.addi	r8,r8,-0x4

9:
	EMERGENCY_PRINT_LOAD_GPR8
	EMERGENCY_PRINT_LOAD_GPR7
	EMERGENCY_PRINT_LOAD_GPR6
	EMERGENCY_PRINT_LOAD_GPR5
	EMERGENCY_PRINT_LOAD_GPR4
	l.jr	r9
	l.nop


/*
 * This should be used for debugging only.
 * It messes up the Linux early serial output
 * somehow, so use it sparingly and essentially
 * only if you need to debug something that goes wrong
 * before Linux gets the early serial going.
 *
 * Furthermore, you'll have to make sure you set the
 * UART_DEVISOR correctly according to the system
 * clock rate.
 *
 *
 */



#define SYS_CLK            20000000
//#define SYS_CLK            1843200
#define OR32_CONSOLE_BAUD  115200
#define UART_DIVISOR       SYS_CLK/(16*OR32_CONSOLE_BAUD)

ENTRY(_early_uart_init)
	l.movhi	r3,hi(UART_BASE_ADD)

	l.addi	r4,r0,0x7
	l.sb	0x2(r3),r4

	l.addi	r4,r0,0x0
	l.sb	0x1(r3),r4

	l.addi	r4,r0,0x3
	l.sb	0x3(r3),r4

	l.lbz	r5,3(r3)
	l.ori	r4,r5,0x80
	l.sb	0x3(r3),r4
	l.addi	r4,r0,((UART_DIVISOR>>8) & 0x000000ff)
	l.sb	UART_DLM(r3),r4
	l.addi  r4,r0,((UART_DIVISOR) & 0x000000ff)
	l.sb	UART_DLL(r3),r4
	l.sb	0x3(r3),r5

	l.jr	r9
	l.nop

	.section .rodata
_string_unhandled_exception:
	.string "\n\rRunarunaround: Unhandled exception 0x\0"

_string_epc_prefix:
	.string ": EPC=0x\0"

_string_nl:
	.string "\n\r\0"


/* ========================================[ page aligned structures ]=== */

/*
 * .data section should be page aligned
 *	(look into arch/or32/kernel/vmlinux.lds)
 */
	.section .data,"aw"
	.align	8192
	.global  empty_zero_page
empty_zero_page:
	.space  8192

	.global  swapper_pg_dir
swapper_pg_dir:
	.space  8192

	.global	_unhandled_stack
_unhandled_stack:
	.space	8192
_unhandled_stack_top:

/* ============================================================[ EOF ]=== */
