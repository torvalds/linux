/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Rewritten by Cort Dougan (cort@cs.nmt.edu) for PReP
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *  Adapted for Power Macintosh by Paul Mackerras.
 *  Low-level exception handlers and MMU support
 *  rewritten by Paul Mackerras.
 *    Copyright (C) 1996 Paul Mackerras.
 *
 *  Adapted for 64bit PowerPC by Dave Engebretsen, Peter Bergner, and
 *    Mike Corrigan {engebret|bergner|mikejc}@us.ibm.com
 *
 *  This file contains the low-level support and setup for the
 *  PowerPC-64 platform, including trap and interrupt dispatch.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/threads.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>
#include <asm/bug.h>
#include <asm/cputable.h>
#include <asm/setup.h>
#include <asm/hvcall.h>
#include <asm/iseries/lpar_map.h>
#include <asm/thread_info.h>
#include <asm/firmware.h>

#ifdef CONFIG_PPC_ISERIES
#define DO_SOFT_DISABLE
#endif

/*
 * We layout physical memory as follows:
 * 0x0000 - 0x00ff : Secondary processor spin code
 * 0x0100 - 0x2fff : pSeries Interrupt prologs
 * 0x3000 - 0x5fff : interrupt support, iSeries and common interrupt prologs
 * 0x6000 - 0x6fff : Initial (CPU0) segment table
 * 0x7000 - 0x7fff : FWNMI data area
 * 0x8000 -        : Early init and support code
 */

/*
 *   SPRG Usage
 *
 *   Register	Definition
 *
 *   SPRG0	reserved for hypervisor
 *   SPRG1	temp - used to save gpr
 *   SPRG2	temp - used to save gpr
 *   SPRG3	virt addr of paca
 */

/*
 * Entering into this code we make the following assumptions:
 *  For pSeries:
 *   1. The MMU is off & open firmware is running in real mode.
 *   2. The kernel is entered at __start
 *
 *  For iSeries:
 *   1. The MMU is on (as it always is for iSeries)
 *   2. The kernel is entered at system_reset_iSeries
 */

	.text
	.globl  _stext
_stext:
#ifdef CONFIG_PPC_MULTIPLATFORM
_GLOBAL(__start)
	/* NOP this out unconditionally */
BEGIN_FTR_SECTION
	b	.__start_initialization_multiplatform
END_FTR_SECTION(0, 1)
#endif /* CONFIG_PPC_MULTIPLATFORM */

	/* Catch branch to 0 in real mode */
	trap

	/* Secondary processors spin on this value until it goes to 1. */
	.globl  __secondary_hold_spinloop
__secondary_hold_spinloop:
	.llong	0x0

	/* Secondary processors write this value with their cpu # */
	/* after they enter the spin loop immediately below.	  */
	.globl	__secondary_hold_acknowledge
__secondary_hold_acknowledge:
	.llong	0x0

#ifdef CONFIG_PPC_ISERIES
	/*
	 * At offset 0x20, there is a pointer to iSeries LPAR data.
	 * This is required by the hypervisor
	 */
	. = 0x20
	.llong hvReleaseData-KERNELBASE
#endif /* CONFIG_PPC_ISERIES */

	. = 0x60
/*
 * The following code is used on pSeries to hold secondary processors
 * in a spin loop after they have been freed from OpenFirmware, but
 * before the bulk of the kernel has been relocated.  This code
 * is relocated to physical address 0x60 before prom_init is run.
 * All of it must fit below the first exception vector at 0x100.
 */
_GLOBAL(__secondary_hold)
	mfmsr	r24
	ori	r24,r24,MSR_RI
	mtmsrd	r24			/* RI on */

	/* Grab our physical cpu number */
	mr	r24,r3

	/* Tell the master cpu we're here */
	/* Relocation is off & we are located at an address less */
	/* than 0x100, so only need to grab low order offset.    */
	std	r24,__secondary_hold_acknowledge@l(0)
	sync

	/* All secondary cpus wait here until told to start. */
100:	ld	r4,__secondary_hold_spinloop@l(0)
	cmpdi	0,r4,1
	bne	100b

#if defined(CONFIG_SMP) || defined(CONFIG_KEXEC)
	LOAD_REG_IMMEDIATE(r4, .generic_secondary_smp_init)
	mtctr	r4
	mr	r3,r24
	bctr
#else
	BUG_OPCODE
#endif

/* This value is used to mark exception frames on the stack. */
	.section ".toc","aw"
exception_marker:
	.tc	ID_72656773_68657265[TC],0x7265677368657265
	.text

/*
 * The following macros define the code that appears as
 * the prologue to each of the exception handlers.  They
 * are split into two parts to allow a single kernel binary
 * to be used for pSeries and iSeries.
 * LOL.  One day... - paulus
 */

/*
 * We make as much of the exception code common between native
 * exception handlers (including pSeries LPAR) and iSeries LPAR
 * implementations as possible.
 */

/*
 * This is the start of the interrupt handlers for pSeries
 * This code runs with relocation off.
 */
#define EX_R9		0
#define EX_R10		8
#define EX_R11		16
#define EX_R12		24
#define EX_R13		32
#define EX_SRR0		40
#define EX_DAR		48
#define EX_DSISR	56
#define EX_CCR		60
#define EX_R3		64
#define EX_LR		72

/*
 * We're short on space and time in the exception prolog, so we can't
 * use the normal SET_REG_IMMEDIATE macro. Normally we just need the
 * low halfword of the address, but for Kdump we need the whole low
 * word.
 */
#ifdef CONFIG_CRASH_DUMP
#define LOAD_HANDLER(reg, label)					\
	oris	reg,reg,(label)@h;	/* virt addr of handler ... */	\
	ori	reg,reg,(label)@l;	/* .. and the rest */
#else
#define LOAD_HANDLER(reg, label)					\
	ori	reg,reg,(label)@l;	/* virt addr of handler ... */
#endif

/*
 * Equal to EXCEPTION_PROLOG_PSERIES, except that it forces 64bit mode.
 * The firmware calls the registered system_reset_fwnmi and
 * machine_check_fwnmi handlers in 32bit mode if the cpu happens to run
 * a 32bit application at the time of the event.
 * This firmware bug is present on POWER4 and JS20.
 */
#define EXCEPTION_PROLOG_PSERIES_FORCE_64BIT(area, label)		\
	mfspr	r13,SPRN_SPRG3;		/* get paca address into r13 */	\
	std	r9,area+EX_R9(r13);	/* save r9 - r12 */		\
	std	r10,area+EX_R10(r13);					\
	std	r11,area+EX_R11(r13);					\
	std	r12,area+EX_R12(r13);					\
	mfspr	r9,SPRN_SPRG1;						\
	std	r9,area+EX_R13(r13);					\
	mfcr	r9;							\
	clrrdi	r12,r13,32;		/* get high part of &label */	\
	mfmsr	r10;							\
	/* force 64bit mode */						\
	li	r11,5;			/* MSR_SF_LG|MSR_ISF_LG */	\
	rldimi	r10,r11,61,0;		/* insert into top 3 bits */	\
	/* done 64bit mode */						\
	mfspr	r11,SPRN_SRR0;		/* save SRR0 */			\
	LOAD_HANDLER(r12,label)						\
	ori	r10,r10,MSR_IR|MSR_DR|MSR_RI;				\
	mtspr	SPRN_SRR0,r12;						\
	mfspr	r12,SPRN_SRR1;		/* and SRR1 */			\
	mtspr	SPRN_SRR1,r10;						\
	rfid;								\
	b	.	/* prevent speculative execution */

#define EXCEPTION_PROLOG_PSERIES(area, label)				\
	mfspr	r13,SPRN_SPRG3;		/* get paca address into r13 */	\
	std	r9,area+EX_R9(r13);	/* save r9 - r12 */		\
	std	r10,area+EX_R10(r13);					\
	std	r11,area+EX_R11(r13);					\
	std	r12,area+EX_R12(r13);					\
	mfspr	r9,SPRN_SPRG1;						\
	std	r9,area+EX_R13(r13);					\
	mfcr	r9;							\
	clrrdi	r12,r13,32;		/* get high part of &label */	\
	mfmsr	r10;							\
	mfspr	r11,SPRN_SRR0;		/* save SRR0 */			\
	LOAD_HANDLER(r12,label)						\
	ori	r10,r10,MSR_IR|MSR_DR|MSR_RI;				\
	mtspr	SPRN_SRR0,r12;						\
	mfspr	r12,SPRN_SRR1;		/* and SRR1 */			\
	mtspr	SPRN_SRR1,r10;						\
	rfid;								\
	b	.	/* prevent speculative execution */

/*
 * This is the start of the interrupt handlers for iSeries
 * This code runs with relocation on.
 */
#define EXCEPTION_PROLOG_ISERIES_1(area)				\
	mfspr	r13,SPRN_SPRG3;		/* get paca address into r13 */	\
	std	r9,area+EX_R9(r13);	/* save r9 - r12 */		\
	std	r10,area+EX_R10(r13);					\
	std	r11,area+EX_R11(r13);					\
	std	r12,area+EX_R12(r13);					\
	mfspr	r9,SPRN_SPRG1;						\
	std	r9,area+EX_R13(r13);					\
	mfcr	r9

#define EXCEPTION_PROLOG_ISERIES_2					\
	mfmsr	r10;							\
	ld	r12,PACALPPACAPTR(r13);					\
	ld	r11,LPPACASRR0(r12);					\
	ld	r12,LPPACASRR1(r12);					\
	ori	r10,r10,MSR_RI;						\
	mtmsrd	r10,1

/*
 * The common exception prolog is used for all except a few exceptions
 * such as a segment miss on a kernel address.  We have to be prepared
 * to take another exception from the point where we first touch the
 * kernel stack onwards.
 *
 * On entry r13 points to the paca, r9-r13 are saved in the paca,
 * r9 contains the saved CR, r11 and r12 contain the saved SRR0 and
 * SRR1, and relocation is on.
 */
#define EXCEPTION_PROLOG_COMMON(n, area)				   \
	andi.	r10,r12,MSR_PR;		/* See if coming from user	*/ \
	mr	r10,r1;			/* Save r1			*/ \
	subi	r1,r1,INT_FRAME_SIZE;	/* alloc frame on kernel stack	*/ \
	beq-	1f;							   \
	ld	r1,PACAKSAVE(r13);	/* kernel stack to use		*/ \
1:	cmpdi	cr1,r1,0;		/* check if r1 is in userspace	*/ \
	bge-	cr1,bad_stack;		/* abort if it is		*/ \
	std	r9,_CCR(r1);		/* save CR in stackframe	*/ \
	std	r11,_NIP(r1);		/* save SRR0 in stackframe	*/ \
	std	r12,_MSR(r1);		/* save SRR1 in stackframe	*/ \
	std	r10,0(r1);		/* make stack chain pointer	*/ \
	std	r0,GPR0(r1);		/* save r0 in stackframe	*/ \
	std	r10,GPR1(r1);		/* save r1 in stackframe	*/ \
	ACCOUNT_CPU_USER_ENTRY(r9, r10);				   \
	std	r2,GPR2(r1);		/* save r2 in stackframe	*/ \
	SAVE_4GPRS(3, r1);		/* save r3 - r6 in stackframe	*/ \
	SAVE_2GPRS(7, r1);		/* save r7, r8 in stackframe	*/ \
	ld	r9,area+EX_R9(r13);	/* move r9, r10 to stackframe	*/ \
	ld	r10,area+EX_R10(r13);					   \
	std	r9,GPR9(r1);						   \
	std	r10,GPR10(r1);						   \
	ld	r9,area+EX_R11(r13);	/* move r11 - r13 to stackframe	*/ \
	ld	r10,area+EX_R12(r13);					   \
	ld	r11,area+EX_R13(r13);					   \
	std	r9,GPR11(r1);						   \
	std	r10,GPR12(r1);						   \
	std	r11,GPR13(r1);						   \
	ld	r2,PACATOC(r13);	/* get kernel TOC into r2	*/ \
	mflr	r9;			/* save LR in stackframe	*/ \
	std	r9,_LINK(r1);						   \
	mfctr	r10;			/* save CTR in stackframe	*/ \
	std	r10,_CTR(r1);						   \
	mfspr	r11,SPRN_XER;		/* save XER in stackframe	*/ \
	std	r11,_XER(r1);						   \
	li	r9,(n)+1;						   \
	std	r9,_TRAP(r1);		/* set trap number		*/ \
	li	r10,0;							   \
	ld	r11,exception_marker@toc(r2);				   \
	std	r10,RESULT(r1);		/* clear regs->result		*/ \
	std	r11,STACK_FRAME_OVERHEAD-16(r1); /* mark the frame	*/

/*
 * Exception vectors.
 */
#define STD_EXCEPTION_PSERIES(n, label)			\
	. = n;						\
	.globl label##_pSeries;				\
label##_pSeries:					\
	HMT_MEDIUM;					\
	mtspr	SPRN_SPRG1,r13;		/* save r13 */	\
	EXCEPTION_PROLOG_PSERIES(PACA_EXGEN, label##_common)

#define HSTD_EXCEPTION_PSERIES(n, label)		\
	. = n;						\
	.globl label##_pSeries;				\
label##_pSeries:					\
	HMT_MEDIUM;					\
	mtspr	SPRN_SPRG1,r20;		/* save r20 */	\
	mfspr	r20,SPRN_HSRR0;		/* copy HSRR0 to SRR0 */ \
	mtspr	SPRN_SRR0,r20;				\
	mfspr	r20,SPRN_HSRR1;		/* copy HSRR0 to SRR0 */ \
	mtspr	SPRN_SRR1,r20;				\
	mfspr	r20,SPRN_SPRG1;		/* restore r20 */ \
	mtspr	SPRN_SPRG1,r13;		/* save r13 */	\
	EXCEPTION_PROLOG_PSERIES(PACA_EXGEN, label##_common)


#define STD_EXCEPTION_ISERIES(n, label, area)		\
	.globl label##_iSeries;				\
label##_iSeries:					\
	HMT_MEDIUM;					\
	mtspr	SPRN_SPRG1,r13;		/* save r13 */	\
	EXCEPTION_PROLOG_ISERIES_1(area);		\
	EXCEPTION_PROLOG_ISERIES_2;			\
	b	label##_common

#define MASKABLE_EXCEPTION_ISERIES(n, label)				\
	.globl label##_iSeries;						\
label##_iSeries:							\
	HMT_MEDIUM;							\
	mtspr	SPRN_SPRG1,r13;		/* save r13 */			\
	EXCEPTION_PROLOG_ISERIES_1(PACA_EXGEN);				\
	lbz	r10,PACAPROCENABLED(r13);				\
	cmpwi	0,r10,0;						\
	beq-	label##_iSeries_masked;					\
	EXCEPTION_PROLOG_ISERIES_2;					\
	b	label##_common;						\

#ifdef DO_SOFT_DISABLE
#define DISABLE_INTS				\
BEGIN_FW_FTR_SECTION;				\
	lbz	r10,PACAPROCENABLED(r13);	\
	li	r11,0;				\
	std	r10,SOFTE(r1);			\
	mfmsr	r10;				\
	stb	r11,PACAPROCENABLED(r13);	\
	ori	r10,r10,MSR_EE;			\
	mtmsrd	r10,1;				\
END_FW_FTR_SECTION_IFSET(FW_FEATURE_ISERIES)

#define ENABLE_INTS				\
BEGIN_FW_FTR_SECTION;				\
	lbz	r10,PACAPROCENABLED(r13);	\
	mfmsr	r11;				\
	std	r10,SOFTE(r1);			\
	ori	r11,r11,MSR_EE;			\
END_FW_FTR_SECTION_IFSET(FW_FEATURE_ISERIES);	\
BEGIN_FW_FTR_SECTION;				\
	ld	r12,_MSR(r1);			\
	mfmsr	r11;				\
	rlwimi	r11,r12,0,MSR_EE;		\
END_FW_FTR_SECTION_IFCLR(FW_FEATURE_ISERIES);	\
	mtmsrd	r11,1

#else	/* hard enable/disable interrupts */
#define DISABLE_INTS

#define ENABLE_INTS				\
	ld	r12,_MSR(r1);			\
	mfmsr	r11;				\
	rlwimi	r11,r12,0,MSR_EE;		\
	mtmsrd	r11,1

#endif

#define STD_EXCEPTION_COMMON(trap, label, hdlr)		\
	.align	7;					\
	.globl label##_common;				\
label##_common:						\
	EXCEPTION_PROLOG_COMMON(trap, PACA_EXGEN);	\
	DISABLE_INTS;					\
	bl	.save_nvgprs;				\
	addi	r3,r1,STACK_FRAME_OVERHEAD;		\
	bl	hdlr;					\
	b	.ret_from_except

/*
 * Like STD_EXCEPTION_COMMON, but for exceptions that can occur
 * in the idle task and therefore need the special idle handling.
 */
#define STD_EXCEPTION_COMMON_IDLE(trap, label, hdlr)	\
	.align	7;					\
	.globl label##_common;				\
label##_common:						\
	EXCEPTION_PROLOG_COMMON(trap, PACA_EXGEN);	\
	FINISH_NAP;					\
	DISABLE_INTS;					\
	bl	.save_nvgprs;				\
	addi	r3,r1,STACK_FRAME_OVERHEAD;		\
	bl	hdlr;					\
	b	.ret_from_except

#define STD_EXCEPTION_COMMON_LITE(trap, label, hdlr)	\
	.align	7;					\
	.globl label##_common;				\
label##_common:						\
	EXCEPTION_PROLOG_COMMON(trap, PACA_EXGEN);	\
	FINISH_NAP;					\
	DISABLE_INTS;					\
	bl	.ppc64_runlatch_on;			\
	addi	r3,r1,STACK_FRAME_OVERHEAD;		\
	bl	hdlr;					\
	b	.ret_from_except_lite

/*
 * When the idle code in power4_idle puts the CPU into NAP mode,
 * it has to do so in a loop, and relies on the external interrupt
 * and decrementer interrupt entry code to get it out of the loop.
 * It sets the _TLF_NAPPING bit in current_thread_info()->local_flags
 * to signal that it is in the loop and needs help to get out.
 */
#ifdef CONFIG_PPC_970_NAP
#define FINISH_NAP				\
BEGIN_FTR_SECTION				\
	clrrdi	r11,r1,THREAD_SHIFT;		\
	ld	r9,TI_LOCAL_FLAGS(r11);		\
	andi.	r10,r9,_TLF_NAPPING;		\
	bnel	power4_fixup_nap;		\
END_FTR_SECTION_IFSET(CPU_FTR_CAN_NAP)
#else
#define FINISH_NAP
#endif

/*
 * Start of pSeries system interrupt routines
 */
	. = 0x100
	.globl __start_interrupts
__start_interrupts:

	STD_EXCEPTION_PSERIES(0x100, system_reset)

	. = 0x200
_machine_check_pSeries:
	HMT_MEDIUM
	mtspr	SPRN_SPRG1,r13		/* save r13 */
	EXCEPTION_PROLOG_PSERIES(PACA_EXMC, machine_check_common)

	. = 0x300
	.globl data_access_pSeries
data_access_pSeries:
	HMT_MEDIUM
	mtspr	SPRN_SPRG1,r13
BEGIN_FTR_SECTION
	mtspr	SPRN_SPRG2,r12
	mfspr	r13,SPRN_DAR
	mfspr	r12,SPRN_DSISR
	srdi	r13,r13,60
	rlwimi	r13,r12,16,0x20
	mfcr	r12
	cmpwi	r13,0x2c
	beq	.do_stab_bolted_pSeries
	mtcrf	0x80,r12
	mfspr	r12,SPRN_SPRG2
END_FTR_SECTION_IFCLR(CPU_FTR_SLB)
	EXCEPTION_PROLOG_PSERIES(PACA_EXGEN, data_access_common)

	. = 0x380
	.globl data_access_slb_pSeries
data_access_slb_pSeries:
	HMT_MEDIUM
	mtspr	SPRN_SPRG1,r13
	mfspr	r13,SPRN_SPRG3		/* get paca address into r13 */
	std	r3,PACA_EXSLB+EX_R3(r13)
	mfspr	r3,SPRN_DAR
	std	r9,PACA_EXSLB+EX_R9(r13)	/* save r9 - r12 */
	mfcr	r9
#ifdef __DISABLED__
	/* Keep that around for when we re-implement dynamic VSIDs */
	cmpdi	r3,0
	bge	slb_miss_user_pseries
#endif /* __DISABLED__ */
	std	r10,PACA_EXSLB+EX_R10(r13)
	std	r11,PACA_EXSLB+EX_R11(r13)
	std	r12,PACA_EXSLB+EX_R12(r13)
	mfspr	r10,SPRN_SPRG1
	std	r10,PACA_EXSLB+EX_R13(r13)
	mfspr	r12,SPRN_SRR1		/* and SRR1 */
	b	.slb_miss_realmode	/* Rel. branch works in real mode */

	STD_EXCEPTION_PSERIES(0x400, instruction_access)

	. = 0x480
	.globl instruction_access_slb_pSeries
instruction_access_slb_pSeries:
	HMT_MEDIUM
	mtspr	SPRN_SPRG1,r13
	mfspr	r13,SPRN_SPRG3		/* get paca address into r13 */
	std	r3,PACA_EXSLB+EX_R3(r13)
	mfspr	r3,SPRN_SRR0		/* SRR0 is faulting address */
	std	r9,PACA_EXSLB+EX_R9(r13)	/* save r9 - r12 */
	mfcr	r9
#ifdef __DISABLED__
	/* Keep that around for when we re-implement dynamic VSIDs */
	cmpdi	r3,0
	bge	slb_miss_user_pseries
#endif /* __DISABLED__ */
	std	r10,PACA_EXSLB+EX_R10(r13)
	std	r11,PACA_EXSLB+EX_R11(r13)
	std	r12,PACA_EXSLB+EX_R12(r13)
	mfspr	r10,SPRN_SPRG1
	std	r10,PACA_EXSLB+EX_R13(r13)
	mfspr	r12,SPRN_SRR1		/* and SRR1 */
	b	.slb_miss_realmode	/* Rel. branch works in real mode */

	STD_EXCEPTION_PSERIES(0x500, hardware_interrupt)
	STD_EXCEPTION_PSERIES(0x600, alignment)
	STD_EXCEPTION_PSERIES(0x700, program_check)
	STD_EXCEPTION_PSERIES(0x800, fp_unavailable)
	STD_EXCEPTION_PSERIES(0x900, decrementer)
	STD_EXCEPTION_PSERIES(0xa00, trap_0a)
	STD_EXCEPTION_PSERIES(0xb00, trap_0b)

	. = 0xc00
	.globl	system_call_pSeries
system_call_pSeries:
	HMT_MEDIUM
	mr	r9,r13
	mfmsr	r10
	mfspr	r13,SPRN_SPRG3
	mfspr	r11,SPRN_SRR0
	clrrdi	r12,r13,32
	oris	r12,r12,system_call_common@h
	ori	r12,r12,system_call_common@l
	mtspr	SPRN_SRR0,r12
	ori	r10,r10,MSR_IR|MSR_DR|MSR_RI
	mfspr	r12,SPRN_SRR1
	mtspr	SPRN_SRR1,r10
	rfid
	b	.	/* prevent speculative execution */

	STD_EXCEPTION_PSERIES(0xd00, single_step)
	STD_EXCEPTION_PSERIES(0xe00, trap_0e)

	/* We need to deal with the Altivec unavailable exception
	 * here which is at 0xf20, thus in the middle of the
	 * prolog code of the PerformanceMonitor one. A little
	 * trickery is thus necessary
	 */
	. = 0xf00
	b	performance_monitor_pSeries

	STD_EXCEPTION_PSERIES(0xf20, altivec_unavailable)

#ifdef CONFIG_CBE_RAS
	HSTD_EXCEPTION_PSERIES(0x1200, cbe_system_error)
#endif /* CONFIG_CBE_RAS */
	STD_EXCEPTION_PSERIES(0x1300, instruction_breakpoint)
#ifdef CONFIG_CBE_RAS
	HSTD_EXCEPTION_PSERIES(0x1600, cbe_maintenance)
#endif /* CONFIG_CBE_RAS */
	STD_EXCEPTION_PSERIES(0x1700, altivec_assist)
#ifdef CONFIG_CBE_RAS
	HSTD_EXCEPTION_PSERIES(0x1800, cbe_thermal)
#endif /* CONFIG_CBE_RAS */

	. = 0x3000

/*** pSeries interrupt support ***/

	/* moved from 0xf00 */
	STD_EXCEPTION_PSERIES(., performance_monitor)

	.align	7
_GLOBAL(do_stab_bolted_pSeries)
	mtcrf	0x80,r12
	mfspr	r12,SPRN_SPRG2
	EXCEPTION_PROLOG_PSERIES(PACA_EXSLB, .do_stab_bolted)

/*
 * We have some room here  we use that to put
 * the peries slb miss user trampoline code so it's reasonably
 * away from slb_miss_user_common to avoid problems with rfid
 *
 * This is used for when the SLB miss handler has to go virtual,
 * which doesn't happen for now anymore but will once we re-implement
 * dynamic VSIDs for shared page tables
 */
#ifdef __DISABLED__
slb_miss_user_pseries:
	std	r10,PACA_EXGEN+EX_R10(r13)
	std	r11,PACA_EXGEN+EX_R11(r13)
	std	r12,PACA_EXGEN+EX_R12(r13)
	mfspr	r10,SPRG1
	ld	r11,PACA_EXSLB+EX_R9(r13)
	ld	r12,PACA_EXSLB+EX_R3(r13)
	std	r10,PACA_EXGEN+EX_R13(r13)
	std	r11,PACA_EXGEN+EX_R9(r13)
	std	r12,PACA_EXGEN+EX_R3(r13)
	clrrdi	r12,r13,32
	mfmsr	r10
	mfspr	r11,SRR0			/* save SRR0 */
	ori	r12,r12,slb_miss_user_common@l	/* virt addr of handler */
	ori	r10,r10,MSR_IR|MSR_DR|MSR_RI
	mtspr	SRR0,r12
	mfspr	r12,SRR1			/* and SRR1 */
	mtspr	SRR1,r10
	rfid
	b	.				/* prevent spec. execution */
#endif /* __DISABLED__ */

/*
 * Vectors for the FWNMI option.  Share common code.
 */
	.globl system_reset_fwnmi
      .align 7
system_reset_fwnmi:
	HMT_MEDIUM
	mtspr	SPRN_SPRG1,r13		/* save r13 */
	EXCEPTION_PROLOG_PSERIES_FORCE_64BIT(PACA_EXGEN, system_reset_common)

	.globl machine_check_fwnmi
      .align 7
machine_check_fwnmi:
	HMT_MEDIUM
	mtspr	SPRN_SPRG1,r13		/* save r13 */
	EXCEPTION_PROLOG_PSERIES_FORCE_64BIT(PACA_EXMC, machine_check_common)

#ifdef CONFIG_PPC_ISERIES
/***  ISeries-LPAR interrupt handlers ***/

	STD_EXCEPTION_ISERIES(0x200, machine_check, PACA_EXMC)

	.globl data_access_iSeries
data_access_iSeries:
	mtspr	SPRN_SPRG1,r13
BEGIN_FTR_SECTION
	mtspr	SPRN_SPRG2,r12
	mfspr	r13,SPRN_DAR
	mfspr	r12,SPRN_DSISR
	srdi	r13,r13,60
	rlwimi	r13,r12,16,0x20
	mfcr	r12
	cmpwi	r13,0x2c
	beq	.do_stab_bolted_iSeries
	mtcrf	0x80,r12
	mfspr	r12,SPRN_SPRG2
END_FTR_SECTION_IFCLR(CPU_FTR_SLB)
	EXCEPTION_PROLOG_ISERIES_1(PACA_EXGEN)
	EXCEPTION_PROLOG_ISERIES_2
	b	data_access_common

.do_stab_bolted_iSeries:
	mtcrf	0x80,r12
	mfspr	r12,SPRN_SPRG2
	EXCEPTION_PROLOG_ISERIES_1(PACA_EXSLB)
	EXCEPTION_PROLOG_ISERIES_2
	b	.do_stab_bolted

	.globl	data_access_slb_iSeries
data_access_slb_iSeries:
	mtspr	SPRN_SPRG1,r13		/* save r13 */
	mfspr	r13,SPRN_SPRG3		/* get paca address into r13 */
	std	r3,PACA_EXSLB+EX_R3(r13)
	mfspr	r3,SPRN_DAR
	std	r9,PACA_EXSLB+EX_R9(r13)
	mfcr	r9
#ifdef __DISABLED__
	cmpdi	r3,0
	bge	slb_miss_user_iseries
#endif
	std	r10,PACA_EXSLB+EX_R10(r13)
	std	r11,PACA_EXSLB+EX_R11(r13)
	std	r12,PACA_EXSLB+EX_R12(r13)
	mfspr	r10,SPRN_SPRG1
	std	r10,PACA_EXSLB+EX_R13(r13)
	ld	r12,PACALPPACAPTR(r13)
	ld	r12,LPPACASRR1(r12)
	b	.slb_miss_realmode

	STD_EXCEPTION_ISERIES(0x400, instruction_access, PACA_EXGEN)

	.globl	instruction_access_slb_iSeries
instruction_access_slb_iSeries:
	mtspr	SPRN_SPRG1,r13		/* save r13 */
	mfspr	r13,SPRN_SPRG3		/* get paca address into r13 */
	std	r3,PACA_EXSLB+EX_R3(r13)
	ld	r3,PACALPPACAPTR(r13)
	ld	r3,LPPACASRR0(r3)	/* get SRR0 value */
	std	r9,PACA_EXSLB+EX_R9(r13)
	mfcr	r9
#ifdef __DISABLED__
	cmpdi	r3,0
	bge	.slb_miss_user_iseries
#endif
	std	r10,PACA_EXSLB+EX_R10(r13)
	std	r11,PACA_EXSLB+EX_R11(r13)
	std	r12,PACA_EXSLB+EX_R12(r13)
	mfspr	r10,SPRN_SPRG1
	std	r10,PACA_EXSLB+EX_R13(r13)
	ld	r12,PACALPPACAPTR(r13)
	ld	r12,LPPACASRR1(r12)
	b	.slb_miss_realmode

#ifdef __DISABLED__
slb_miss_user_iseries:
	std	r10,PACA_EXGEN+EX_R10(r13)
	std	r11,PACA_EXGEN+EX_R11(r13)
	std	r12,PACA_EXGEN+EX_R12(r13)
	mfspr	r10,SPRG1
	ld	r11,PACA_EXSLB+EX_R9(r13)
	ld	r12,PACA_EXSLB+EX_R3(r13)
	std	r10,PACA_EXGEN+EX_R13(r13)
	std	r11,PACA_EXGEN+EX_R9(r13)
	std	r12,PACA_EXGEN+EX_R3(r13)
	EXCEPTION_PROLOG_ISERIES_2
	b	slb_miss_user_common
#endif

	MASKABLE_EXCEPTION_ISERIES(0x500, hardware_interrupt)
	STD_EXCEPTION_ISERIES(0x600, alignment, PACA_EXGEN)
	STD_EXCEPTION_ISERIES(0x700, program_check, PACA_EXGEN)
	STD_EXCEPTION_ISERIES(0x800, fp_unavailable, PACA_EXGEN)
	MASKABLE_EXCEPTION_ISERIES(0x900, decrementer)
	STD_EXCEPTION_ISERIES(0xa00, trap_0a, PACA_EXGEN)
	STD_EXCEPTION_ISERIES(0xb00, trap_0b, PACA_EXGEN)

	.globl	system_call_iSeries
system_call_iSeries:
	mr	r9,r13
	mfspr	r13,SPRN_SPRG3
	EXCEPTION_PROLOG_ISERIES_2
	b	system_call_common

	STD_EXCEPTION_ISERIES( 0xd00, single_step, PACA_EXGEN)
	STD_EXCEPTION_ISERIES( 0xe00, trap_0e, PACA_EXGEN)
	STD_EXCEPTION_ISERIES( 0xf00, performance_monitor, PACA_EXGEN)

	.globl system_reset_iSeries
system_reset_iSeries:
	mfspr	r13,SPRN_SPRG3		/* Get paca address */
	mfmsr	r24
	ori	r24,r24,MSR_RI
	mtmsrd	r24			/* RI on */
	lhz	r24,PACAPACAINDEX(r13)	/* Get processor # */
	cmpwi	0,r24,0			/* Are we processor 0? */
	beq	.__start_initialization_iSeries	/* Start up the first processor */
	mfspr	r4,SPRN_CTRLF
	li	r5,CTRL_RUNLATCH	/* Turn off the run light */
	andc	r4,r4,r5
	mtspr	SPRN_CTRLT,r4

1:
	HMT_LOW
#ifdef CONFIG_SMP
	lbz	r23,PACAPROCSTART(r13)	/* Test if this processor
					 * should start */
	sync
	LOAD_REG_IMMEDIATE(r3,current_set)
	sldi	r28,r24,3		/* get current_set[cpu#] */
	ldx	r3,r3,r28
	addi	r1,r3,THREAD_SIZE
	subi	r1,r1,STACK_FRAME_OVERHEAD

	cmpwi	0,r23,0
	beq	iSeries_secondary_smp_loop	/* Loop until told to go */
	bne	.__secondary_start		/* Loop until told to go */
iSeries_secondary_smp_loop:
	/* Let the Hypervisor know we are alive */
	/* 8002 is a call to HvCallCfg::getLps, a harmless Hypervisor function */
	lis	r3,0x8002
	rldicr	r3,r3,32,15		/* r0 = (r3 << 32) & 0xffff000000000000 */
#else /* CONFIG_SMP */
	/* Yield the processor.  This is required for non-SMP kernels
		which are running on multi-threaded machines. */
	lis	r3,0x8000
	rldicr	r3,r3,32,15		/* r3 = (r3 << 32) & 0xffff000000000000 */
	addi	r3,r3,18		/* r3 = 0x8000000000000012 which is "yield" */
	li	r4,0			/* "yield timed" */
	li	r5,-1			/* "yield forever" */
#endif /* CONFIG_SMP */
	li	r0,-1			/* r0=-1 indicates a Hypervisor call */
	sc				/* Invoke the hypervisor via a system call */
	mfspr	r13,SPRN_SPRG3		/* Put r13 back ???? */
	b	1b			/* If SMP not configured, secondaries
					 * loop forever */

	.globl decrementer_iSeries_masked
decrementer_iSeries_masked:
	/* We may not have a valid TOC pointer in here. */
	li	r11,1
	ld	r12,PACALPPACAPTR(r13)
	stb	r11,LPPACADECRINT(r12)
	LOAD_REG_IMMEDIATE(r12, tb_ticks_per_jiffy)
	lwz	r12,0(r12)
	mtspr	SPRN_DEC,r12
	/* fall through */

	.globl hardware_interrupt_iSeries_masked
hardware_interrupt_iSeries_masked:
	mtcrf	0x80,r9		/* Restore regs */
	ld	r12,PACALPPACAPTR(r13)
	ld	r11,LPPACASRR0(r12)
	ld	r12,LPPACASRR1(r12)
	mtspr	SPRN_SRR0,r11
	mtspr	SPRN_SRR1,r12
	ld	r9,PACA_EXGEN+EX_R9(r13)
	ld	r10,PACA_EXGEN+EX_R10(r13)
	ld	r11,PACA_EXGEN+EX_R11(r13)
	ld	r12,PACA_EXGEN+EX_R12(r13)
	ld	r13,PACA_EXGEN+EX_R13(r13)
	rfid
	b	.	/* prevent speculative execution */
#endif /* CONFIG_PPC_ISERIES */

/*** Common interrupt handlers ***/

	STD_EXCEPTION_COMMON(0x100, system_reset, .system_reset_exception)

	/*
	 * Machine check is different because we use a different
	 * save area: PACA_EXMC instead of PACA_EXGEN.
	 */
	.align	7
	.globl machine_check_common
machine_check_common:
	EXCEPTION_PROLOG_COMMON(0x200, PACA_EXMC)
	FINISH_NAP
	DISABLE_INTS
	bl	.save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.machine_check_exception
	b	.ret_from_except

	STD_EXCEPTION_COMMON_LITE(0x900, decrementer, .timer_interrupt)
	STD_EXCEPTION_COMMON(0xa00, trap_0a, .unknown_exception)
	STD_EXCEPTION_COMMON(0xb00, trap_0b, .unknown_exception)
	STD_EXCEPTION_COMMON(0xd00, single_step, .single_step_exception)
	STD_EXCEPTION_COMMON(0xe00, trap_0e, .unknown_exception)
	STD_EXCEPTION_COMMON_IDLE(0xf00, performance_monitor, .performance_monitor_exception)
	STD_EXCEPTION_COMMON(0x1300, instruction_breakpoint, .instruction_breakpoint_exception)
#ifdef CONFIG_ALTIVEC
	STD_EXCEPTION_COMMON(0x1700, altivec_assist, .altivec_assist_exception)
#else
	STD_EXCEPTION_COMMON(0x1700, altivec_assist, .unknown_exception)
#endif
#ifdef CONFIG_CBE_RAS
	STD_EXCEPTION_COMMON(0x1200, cbe_system_error, .cbe_system_error_exception)
	STD_EXCEPTION_COMMON(0x1600, cbe_maintenance, .cbe_maintenance_exception)
	STD_EXCEPTION_COMMON(0x1800, cbe_thermal, .cbe_thermal_exception)
#endif /* CONFIG_CBE_RAS */

/*
 * Here we have detected that the kernel stack pointer is bad.
 * R9 contains the saved CR, r13 points to the paca,
 * r10 contains the (bad) kernel stack pointer,
 * r11 and r12 contain the saved SRR0 and SRR1.
 * We switch to using an emergency stack, save the registers there,
 * and call kernel_bad_stack(), which panics.
 */
bad_stack:
	ld	r1,PACAEMERGSP(r13)
	subi	r1,r1,64+INT_FRAME_SIZE
	std	r9,_CCR(r1)
	std	r10,GPR1(r1)
	std	r11,_NIP(r1)
	std	r12,_MSR(r1)
	mfspr	r11,SPRN_DAR
	mfspr	r12,SPRN_DSISR
	std	r11,_DAR(r1)
	std	r12,_DSISR(r1)
	mflr	r10
	mfctr	r11
	mfxer	r12
	std	r10,_LINK(r1)
	std	r11,_CTR(r1)
	std	r12,_XER(r1)
	SAVE_GPR(0,r1)
	SAVE_GPR(2,r1)
	SAVE_4GPRS(3,r1)
	SAVE_2GPRS(7,r1)
	SAVE_10GPRS(12,r1)
	SAVE_10GPRS(22,r1)
	addi	r11,r1,INT_FRAME_SIZE
	std	r11,0(r1)
	li	r12,0
	std	r12,0(r11)
	ld	r2,PACATOC(r13)
1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.kernel_bad_stack
	b	1b

/*
 * Return from an exception with minimal checks.
 * The caller is assumed to have done EXCEPTION_PROLOG_COMMON.
 * If interrupts have been enabled, or anything has been
 * done that might have changed the scheduling status of
 * any task or sent any task a signal, you should use
 * ret_from_except or ret_from_except_lite instead of this.
 */
	.globl	fast_exception_return
fast_exception_return:
	ld	r12,_MSR(r1)
	ld	r11,_NIP(r1)
	andi.	r3,r12,MSR_RI		/* check if RI is set */
	beq-	unrecov_fer

#ifdef CONFIG_VIRT_CPU_ACCOUNTING
	andi.	r3,r12,MSR_PR
	beq	2f
	ACCOUNT_CPU_USER_EXIT(r3, r4)
2:
#endif

	ld	r3,_CCR(r1)
	ld	r4,_LINK(r1)
	ld	r5,_CTR(r1)
	ld	r6,_XER(r1)
	mtcr	r3
	mtlr	r4
	mtctr	r5
	mtxer	r6
	REST_GPR(0, r1)
	REST_8GPRS(2, r1)

	mfmsr	r10
	clrrdi	r10,r10,2		/* clear RI (LE is 0 already) */
	mtmsrd	r10,1

	mtspr	SPRN_SRR1,r12
	mtspr	SPRN_SRR0,r11
	REST_4GPRS(10, r1)
	ld	r1,GPR1(r1)
	rfid
	b	.	/* prevent speculative execution */

unrecov_fer:
	bl	.save_nvgprs
1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.unrecoverable_exception
	b	1b

/*
 * Here r13 points to the paca, r9 contains the saved CR,
 * SRR0 and SRR1 are saved in r11 and r12,
 * r9 - r13 are saved in paca->exgen.
 */
	.align	7
	.globl data_access_common
data_access_common:
	mfspr	r10,SPRN_DAR
	std	r10,PACA_EXGEN+EX_DAR(r13)
	mfspr	r10,SPRN_DSISR
	stw	r10,PACA_EXGEN+EX_DSISR(r13)
	EXCEPTION_PROLOG_COMMON(0x300, PACA_EXGEN)
	ld	r3,PACA_EXGEN+EX_DAR(r13)
	lwz	r4,PACA_EXGEN+EX_DSISR(r13)
	li	r5,0x300
	b	.do_hash_page	 	/* Try to handle as hpte fault */

	.align	7
	.globl instruction_access_common
instruction_access_common:
	EXCEPTION_PROLOG_COMMON(0x400, PACA_EXGEN)
	ld	r3,_NIP(r1)
	andis.	r4,r12,0x5820
	li	r5,0x400
	b	.do_hash_page		/* Try to handle as hpte fault */

/*
 * Here is the common SLB miss user that is used when going to virtual
 * mode for SLB misses, that is currently not used
 */
#ifdef __DISABLED__
	.align	7
	.globl	slb_miss_user_common
slb_miss_user_common:
	mflr	r10
	std	r3,PACA_EXGEN+EX_DAR(r13)
	stw	r9,PACA_EXGEN+EX_CCR(r13)
	std	r10,PACA_EXGEN+EX_LR(r13)
	std	r11,PACA_EXGEN+EX_SRR0(r13)
	bl	.slb_allocate_user

	ld	r10,PACA_EXGEN+EX_LR(r13)
	ld	r3,PACA_EXGEN+EX_R3(r13)
	lwz	r9,PACA_EXGEN+EX_CCR(r13)
	ld	r11,PACA_EXGEN+EX_SRR0(r13)
	mtlr	r10
	beq-	slb_miss_fault

	andi.	r10,r12,MSR_RI		/* check for unrecoverable exception */
	beq-	unrecov_user_slb
	mfmsr	r10

.machine push
.machine "power4"
	mtcrf	0x80,r9
.machine pop

	clrrdi	r10,r10,2		/* clear RI before setting SRR0/1 */
	mtmsrd	r10,1

	mtspr	SRR0,r11
	mtspr	SRR1,r12

	ld	r9,PACA_EXGEN+EX_R9(r13)
	ld	r10,PACA_EXGEN+EX_R10(r13)
	ld	r11,PACA_EXGEN+EX_R11(r13)
	ld	r12,PACA_EXGEN+EX_R12(r13)
	ld	r13,PACA_EXGEN+EX_R13(r13)
	rfid
	b	.

slb_miss_fault:
	EXCEPTION_PROLOG_COMMON(0x380, PACA_EXGEN)
	ld	r4,PACA_EXGEN+EX_DAR(r13)
	li	r5,0
	std	r4,_DAR(r1)
	std	r5,_DSISR(r1)
	b	.handle_page_fault

unrecov_user_slb:
	EXCEPTION_PROLOG_COMMON(0x4200, PACA_EXGEN)
	DISABLE_INTS
	bl	.save_nvgprs
1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.unrecoverable_exception
	b	1b

#endif /* __DISABLED__ */


/*
 * r13 points to the PACA, r9 contains the saved CR,
 * r12 contain the saved SRR1, SRR0 is still ready for return
 * r3 has the faulting address
 * r9 - r13 are saved in paca->exslb.
 * r3 is saved in paca->slb_r3
 * We assume we aren't going to take any exceptions during this procedure.
 */
_GLOBAL(slb_miss_realmode)
	mflr	r10

	stw	r9,PACA_EXSLB+EX_CCR(r13)	/* save CR in exc. frame */
	std	r10,PACA_EXSLB+EX_LR(r13)	/* save LR */

	bl	.slb_allocate_realmode

	/* All done -- return from exception. */

	ld	r10,PACA_EXSLB+EX_LR(r13)
	ld	r3,PACA_EXSLB+EX_R3(r13)
	lwz	r9,PACA_EXSLB+EX_CCR(r13)	/* get saved CR */
#ifdef CONFIG_PPC_ISERIES
BEGIN_FW_FTR_SECTION
	ld	r11,PACALPPACAPTR(r13)
	ld	r11,LPPACASRR0(r11)		/* get SRR0 value */
END_FW_FTR_SECTION_IFSET(FW_FEATURE_ISERIES)
#endif /* CONFIG_PPC_ISERIES */

	mtlr	r10

	andi.	r10,r12,MSR_RI	/* check for unrecoverable exception */
	beq-	unrecov_slb

.machine	push
.machine	"power4"
	mtcrf	0x80,r9
	mtcrf	0x01,r9		/* slb_allocate uses cr0 and cr7 */
.machine	pop

#ifdef CONFIG_PPC_ISERIES
BEGIN_FW_FTR_SECTION
	mtspr	SPRN_SRR0,r11
	mtspr	SPRN_SRR1,r12
END_FW_FTR_SECTION_IFSET(FW_FEATURE_ISERIES)
#endif /* CONFIG_PPC_ISERIES */
	ld	r9,PACA_EXSLB+EX_R9(r13)
	ld	r10,PACA_EXSLB+EX_R10(r13)
	ld	r11,PACA_EXSLB+EX_R11(r13)
	ld	r12,PACA_EXSLB+EX_R12(r13)
	ld	r13,PACA_EXSLB+EX_R13(r13)
	rfid
	b	.	/* prevent speculative execution */

unrecov_slb:
	EXCEPTION_PROLOG_COMMON(0x4100, PACA_EXSLB)
	DISABLE_INTS
	bl	.save_nvgprs
1:	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.unrecoverable_exception
	b	1b

	.align	7
	.globl hardware_interrupt_common
	.globl hardware_interrupt_entry
hardware_interrupt_common:
	EXCEPTION_PROLOG_COMMON(0x500, PACA_EXGEN)
	FINISH_NAP
hardware_interrupt_entry:
	DISABLE_INTS
	bl	.ppc64_runlatch_on
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.do_IRQ
	b	.ret_from_except_lite

#ifdef CONFIG_PPC_970_NAP
power4_fixup_nap:
	andc	r9,r9,r10
	std	r9,TI_LOCAL_FLAGS(r11)
	ld	r10,_LINK(r1)		/* make idle task do the */
	std	r10,_NIP(r1)		/* equivalent of a blr */
	blr
#endif

	.align	7
	.globl alignment_common
alignment_common:
	mfspr	r10,SPRN_DAR
	std	r10,PACA_EXGEN+EX_DAR(r13)
	mfspr	r10,SPRN_DSISR
	stw	r10,PACA_EXGEN+EX_DSISR(r13)
	EXCEPTION_PROLOG_COMMON(0x600, PACA_EXGEN)
	ld	r3,PACA_EXGEN+EX_DAR(r13)
	lwz	r4,PACA_EXGEN+EX_DSISR(r13)
	std	r3,_DAR(r1)
	std	r4,_DSISR(r1)
	bl	.save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	ENABLE_INTS
	bl	.alignment_exception
	b	.ret_from_except

	.align	7
	.globl program_check_common
program_check_common:
	EXCEPTION_PROLOG_COMMON(0x700, PACA_EXGEN)
	bl	.save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	ENABLE_INTS
	bl	.program_check_exception
	b	.ret_from_except

	.align	7
	.globl fp_unavailable_common
fp_unavailable_common:
	EXCEPTION_PROLOG_COMMON(0x800, PACA_EXGEN)
	bne	.load_up_fpu		/* if from user, just load it up */
	bl	.save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	ENABLE_INTS
	bl	.kernel_fp_unavailable_exception
	BUG_OPCODE

	.align	7
	.globl altivec_unavailable_common
altivec_unavailable_common:
	EXCEPTION_PROLOG_COMMON(0xf20, PACA_EXGEN)
#ifdef CONFIG_ALTIVEC
BEGIN_FTR_SECTION
	bne	.load_up_altivec	/* if from user, just load it up */
END_FTR_SECTION_IFSET(CPU_FTR_ALTIVEC)
#endif
	bl	.save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	ENABLE_INTS
	bl	.altivec_unavailable_exception
	b	.ret_from_except

#ifdef CONFIG_ALTIVEC
/*
 * load_up_altivec(unused, unused, tsk)
 * Disable VMX for the task which had it previously,
 * and save its vector registers in its thread_struct.
 * Enables the VMX for use in the kernel on return.
 * On SMP we know the VMX is free, since we give it up every
 * switch (ie, no lazy save of the vector registers).
 * On entry: r13 == 'current' && last_task_used_altivec != 'current'
 */
_STATIC(load_up_altivec)
	mfmsr	r5			/* grab the current MSR */
	oris	r5,r5,MSR_VEC@h
	mtmsrd	r5			/* enable use of VMX now */
	isync

/*
 * For SMP, we don't do lazy VMX switching because it just gets too
 * horrendously complex, especially when a task switches from one CPU
 * to another.  Instead we call giveup_altvec in switch_to.
 * VRSAVE isn't dealt with here, that is done in the normal context
 * switch code. Note that we could rely on vrsave value to eventually
 * avoid saving all of the VREGs here...
 */
#ifndef CONFIG_SMP
	ld	r3,last_task_used_altivec@got(r2)
	ld	r4,0(r3)
	cmpdi	0,r4,0
	beq	1f
	/* Save VMX state to last_task_used_altivec's THREAD struct */
	addi	r4,r4,THREAD
	SAVE_32VRS(0,r5,r4)
	mfvscr	vr0
	li	r10,THREAD_VSCR
	stvx	vr0,r10,r4
	/* Disable VMX for last_task_used_altivec */
	ld	r5,PT_REGS(r4)
	ld	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	lis	r6,MSR_VEC@h
	andc	r4,r4,r6
	std	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#endif /* CONFIG_SMP */
	/* Hack: if we get an altivec unavailable trap with VRSAVE
	 * set to all zeros, we assume this is a broken application
	 * that fails to set it properly, and thus we switch it to
	 * all 1's
	 */
	mfspr	r4,SPRN_VRSAVE
	cmpdi	0,r4,0
	bne+	1f
	li	r4,-1
	mtspr	SPRN_VRSAVE,r4
1:
	/* enable use of VMX after return */
	ld	r4,PACACURRENT(r13)
	addi	r5,r4,THREAD		/* Get THREAD */
	oris	r12,r12,MSR_VEC@h
	std	r12,_MSR(r1)
	li	r4,1
	li	r10,THREAD_VSCR
	stw	r4,THREAD_USED_VR(r5)
	lvx	vr0,r10,r5
	mtvscr	vr0
	REST_32VRS(0,r4,r5)
#ifndef CONFIG_SMP
	/* Update last_task_used_math to 'current' */
	subi	r4,r5,THREAD		/* Back to 'current' */
	std	r4,0(r3)
#endif /* CONFIG_SMP */
	/* restore registers and return */
	b	fast_exception_return
#endif /* CONFIG_ALTIVEC */

/*
 * Hash table stuff
 */
	.align	7
_GLOBAL(do_hash_page)
	std	r3,_DAR(r1)
	std	r4,_DSISR(r1)

	andis.	r0,r4,0xa450		/* weird error? */
	bne-	.handle_page_fault	/* if not, try to insert a HPTE */
BEGIN_FTR_SECTION
	andis.	r0,r4,0x0020		/* Is it a segment table fault? */
	bne-	.do_ste_alloc		/* If so handle it */
END_FTR_SECTION_IFCLR(CPU_FTR_SLB)

	/*
	 * We need to set the _PAGE_USER bit if MSR_PR is set or if we are
	 * accessing a userspace segment (even from the kernel). We assume
	 * kernel addresses always have the high bit set.
	 */
	rlwinm	r4,r4,32-25+9,31-9,31-9	/* DSISR_STORE -> _PAGE_RW */
	rotldi	r0,r3,15		/* Move high bit into MSR_PR posn */
	orc	r0,r12,r0		/* MSR_PR | ~high_bit */
	rlwimi	r4,r0,32-13,30,30	/* becomes _PAGE_USER access bit */
	ori	r4,r4,1			/* add _PAGE_PRESENT */
	rlwimi	r4,r5,22+2,31-2,31-2	/* Set _PAGE_EXEC if trap is 0x400 */

	/*
	 * On iSeries, we soft-disable interrupts here, then
	 * hard-enable interrupts so that the hash_page code can spin on
	 * the hash_table_lock without problems on a shared processor.
	 */
	DISABLE_INTS

	/*
	 * r3 contains the faulting address
	 * r4 contains the required access permissions
	 * r5 contains the trap number
	 *
	 * at return r3 = 0 for success
	 */
	bl	.hash_page		/* build HPTE if possible */
	cmpdi	r3,0			/* see if hash_page succeeded */

#ifdef DO_SOFT_DISABLE
BEGIN_FW_FTR_SECTION
	/*
	 * If we had interrupts soft-enabled at the point where the
	 * DSI/ISI occurred, and an interrupt came in during hash_page,
	 * handle it now.
	 * We jump to ret_from_except_lite rather than fast_exception_return
	 * because ret_from_except_lite will check for and handle pending
	 * interrupts if necessary.
	 */
	beq	.ret_from_except_lite
	/* For a hash failure, we don't bother re-enabling interrupts */
	ble-	12f

	/*
	 * hash_page couldn't handle it, set soft interrupt enable back
	 * to what it was before the trap.  Note that .local_irq_restore
	 * handles any interrupts pending at this point.
	 */
	ld	r3,SOFTE(r1)
	bl	.local_irq_restore
	b	11f
END_FW_FTR_SECTION_IFSET(FW_FEATURE_ISERIES)
#endif
BEGIN_FW_FTR_SECTION
	beq	fast_exception_return   /* Return from exception on success */
	ble-	12f			/* Failure return from hash_page */

	/* fall through */
END_FW_FTR_SECTION_IFCLR(FW_FEATURE_ISERIES)

/* Here we have a page fault that hash_page can't handle. */
_GLOBAL(handle_page_fault)
	ENABLE_INTS
11:	ld	r4,_DAR(r1)
	ld	r5,_DSISR(r1)
	addi	r3,r1,STACK_FRAME_OVERHEAD
	bl	.do_page_fault
	cmpdi	r3,0
	beq+	.ret_from_except_lite
	bl	.save_nvgprs
	mr	r5,r3
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lwz	r4,_DAR(r1)
	bl	.bad_page_fault
	b	.ret_from_except

/* We have a page fault that hash_page could handle but HV refused
 * the PTE insertion
 */
12:	bl	.save_nvgprs
	addi	r3,r1,STACK_FRAME_OVERHEAD
	lwz	r4,_DAR(r1)
	bl	.low_hash_fault
	b	.ret_from_except

	/* here we have a segment miss */
_GLOBAL(do_ste_alloc)
	bl	.ste_allocate		/* try to insert stab entry */
	cmpdi	r3,0
	beq+	fast_exception_return
	b	.handle_page_fault

/*
 * r13 points to the PACA, r9 contains the saved CR,
 * r11 and r12 contain the saved SRR0 and SRR1.
 * r9 - r13 are saved in paca->exslb.
 * We assume we aren't going to take any exceptions during this procedure.
 * We assume (DAR >> 60) == 0xc.
 */
	.align	7
_GLOBAL(do_stab_bolted)
	stw	r9,PACA_EXSLB+EX_CCR(r13)	/* save CR in exc. frame */
	std	r11,PACA_EXSLB+EX_SRR0(r13)	/* save SRR0 in exc. frame */

	/* Hash to the primary group */
	ld	r10,PACASTABVIRT(r13)
	mfspr	r11,SPRN_DAR
	srdi	r11,r11,28
	rldimi	r10,r11,7,52	/* r10 = first ste of the group */

	/* Calculate VSID */
	/* This is a kernel address, so protovsid = ESID */
	ASM_VSID_SCRAMBLE(r11, r9)
	rldic	r9,r11,12,16	/* r9 = vsid << 12 */

	/* Search the primary group for a free entry */
1:	ld	r11,0(r10)	/* Test valid bit of the current ste	*/
	andi.	r11,r11,0x80
	beq	2f
	addi	r10,r10,16
	andi.	r11,r10,0x70
	bne	1b

	/* Stick for only searching the primary group for now.		*/
	/* At least for now, we use a very simple random castout scheme */
	/* Use the TB as a random number ;  OR in 1 to avoid entry 0	*/
	mftb	r11
	rldic	r11,r11,4,57	/* r11 = (r11 << 4) & 0x70 */
	ori	r11,r11,0x10

	/* r10 currently points to an ste one past the group of interest */
	/* make it point to the randomly selected entry			*/
	subi	r10,r10,128
	or 	r10,r10,r11	/* r10 is the entry to invalidate	*/

	isync			/* mark the entry invalid		*/
	ld	r11,0(r10)
	rldicl	r11,r11,56,1	/* clear the valid bit */
	rotldi	r11,r11,8
	std	r11,0(r10)
	sync

	clrrdi	r11,r11,28	/* Get the esid part of the ste		*/
	slbie	r11

2:	std	r9,8(r10)	/* Store the vsid part of the ste	*/
	eieio

	mfspr	r11,SPRN_DAR		/* Get the new esid			*/
	clrrdi	r11,r11,28	/* Permits a full 32b of ESID		*/
	ori	r11,r11,0x90	/* Turn on valid and kp			*/
	std	r11,0(r10)	/* Put new entry back into the stab	*/

	sync

	/* All done -- return from exception. */
	lwz	r9,PACA_EXSLB+EX_CCR(r13)	/* get saved CR */
	ld	r11,PACA_EXSLB+EX_SRR0(r13)	/* get saved SRR0 */

	andi.	r10,r12,MSR_RI
	beq-	unrecov_slb

	mtcrf	0x80,r9			/* restore CR */

	mfmsr	r10
	clrrdi	r10,r10,2
	mtmsrd	r10,1

	mtspr	SPRN_SRR0,r11
	mtspr	SPRN_SRR1,r12
	ld	r9,PACA_EXSLB+EX_R9(r13)
	ld	r10,PACA_EXSLB+EX_R10(r13)
	ld	r11,PACA_EXSLB+EX_R11(r13)
	ld	r12,PACA_EXSLB+EX_R12(r13)
	ld	r13,PACA_EXSLB+EX_R13(r13)
	rfid
	b	.	/* prevent speculative execution */

/*
 * Space for CPU0's segment table.
 *
 * On iSeries, the hypervisor must fill in at least one entry before
 * we get control (with relocate on).  The address is give to the hv
 * as a page number (see xLparMap in lpardata.c), so this must be at a
 * fixed address (the linker can't compute (u64)&initial_stab >>
 * PAGE_SHIFT).
 */
	. = STAB0_OFFSET	/* 0x6000 */
	.globl initial_stab
initial_stab:
	.space	4096

/*
 * Data area reserved for FWNMI option.
 * This address (0x7000) is fixed by the RPA.
 */
	.= 0x7000
	.globl fwnmi_data_area
fwnmi_data_area:

	/* iSeries does not use the FWNMI stuff, so it is safe to put
	 * this here, even if we later allow kernels that will boot on
	 * both pSeries and iSeries */
#ifdef CONFIG_PPC_ISERIES
        . = LPARMAP_PHYS
#include "lparmap.s"
/*
 * This ".text" is here for old compilers that generate a trailing
 * .note section when compiling .c files to .s
 */
	.text
#endif /* CONFIG_PPC_ISERIES */

        . = 0x8000

/*
 * On pSeries and most other platforms, secondary processors spin
 * in the following code.
 * At entry, r3 = this processor's number (physical cpu id)
 */
_GLOBAL(generic_secondary_smp_init)
	mr	r24,r3
	
	/* turn on 64-bit mode */
	bl	.enable_64b_mode
	isync

	/* Set up a paca value for this processor. Since we have the
	 * physical cpu id in r24, we need to search the pacas to find
	 * which logical id maps to our physical one.
	 */
	LOAD_REG_IMMEDIATE(r13, paca)	/* Get base vaddr of paca array	 */
	li	r5,0			/* logical cpu id                */
1:	lhz	r6,PACAHWCPUID(r13)	/* Load HW procid from paca      */
	cmpw	r6,r24			/* Compare to our id             */
	beq	2f
	addi	r13,r13,PACA_SIZE	/* Loop to next PACA on miss     */
	addi	r5,r5,1
	cmpwi	r5,NR_CPUS
	blt	1b

	mr	r3,r24			/* not found, copy phys to r3	 */
	b	.kexec_wait		/* next kernel might do better	 */

2:	mtspr	SPRN_SPRG3,r13		/* Save vaddr of paca in SPRG3	 */
	/* From now on, r24 is expected to be logical cpuid */
	mr	r24,r5
3:	HMT_LOW
	lbz	r23,PACAPROCSTART(r13)	/* Test if this processor should */
					/* start.			 */
	sync

#ifndef CONFIG_SMP
	b	3b			/* Never go on non-SMP		 */
#else
	cmpwi	0,r23,0
	beq	3b			/* Loop until told to go	 */

	/* See if we need to call a cpu state restore handler */
	LOAD_REG_IMMEDIATE(r23, cur_cpu_spec)
	ld	r23,0(r23)
	ld	r23,CPU_SPEC_RESTORE(r23)
	cmpdi	0,r23,0
	beq	4f
	ld	r23,0(r23)
	mtctr	r23
	bctrl

4:	/* Create a temp kernel stack for use before relocation is on.	*/
	ld	r1,PACAEMERGSP(r13)
	subi	r1,r1,STACK_FRAME_OVERHEAD

	b	.__secondary_start
#endif

#ifdef CONFIG_PPC_ISERIES
_STATIC(__start_initialization_iSeries)
	/* Clear out the BSS */
	LOAD_REG_IMMEDIATE(r11,__bss_stop)
	LOAD_REG_IMMEDIATE(r8,__bss_start)
	sub	r11,r11,r8		/* bss size			*/
	addi	r11,r11,7		/* round up to an even double word */
	rldicl. r11,r11,61,3		/* shift right by 3		*/
	beq	4f
	addi	r8,r8,-8
	li	r0,0
	mtctr	r11			/* zero this many doublewords	*/
3:	stdu	r0,8(r8)
	bdnz	3b
4:
	LOAD_REG_IMMEDIATE(r1,init_thread_union)
	addi	r1,r1,THREAD_SIZE
	li	r0,0
	stdu	r0,-STACK_FRAME_OVERHEAD(r1)

	LOAD_REG_IMMEDIATE(r2,__toc_start)
	addi	r2,r2,0x4000
	addi	r2,r2,0x4000

	bl	.iSeries_early_setup
	bl	.early_setup

	/* relocation is on at this point */

	b	.start_here_common
#endif /* CONFIG_PPC_ISERIES */

#ifdef CONFIG_PPC_MULTIPLATFORM

_STATIC(__mmu_off)
	mfmsr	r3
	andi.	r0,r3,MSR_IR|MSR_DR
	beqlr
	andc	r3,r3,r0
	mtspr	SPRN_SRR0,r4
	mtspr	SPRN_SRR1,r3
	sync
	rfid
	b	.	/* prevent speculative execution */


/*
 * Here is our main kernel entry point. We support currently 2 kind of entries
 * depending on the value of r5.
 *
 *   r5 != NULL -> OF entry, we go to prom_init, "legacy" parameter content
 *                 in r3...r7
 *   
 *   r5 == NULL -> kexec style entry. r3 is a physical pointer to the
 *                 DT block, r4 is a physical pointer to the kernel itself
 *
 */
_GLOBAL(__start_initialization_multiplatform)
#ifdef CONFIG_PPC_MULTIPLATFORM
	/*
	 * Are we booted from a PROM Of-type client-interface ?
	 */
	cmpldi	cr0,r5,0
	bne	.__boot_from_prom		/* yes -> prom */
#endif

	/* Save parameters */
	mr	r31,r3
	mr	r30,r4

	/* Make sure we are running in 64 bits mode */
	bl	.enable_64b_mode

	/* Setup some critical 970 SPRs before switching MMU off */
	mfspr	r0,SPRN_PVR
	srwi	r0,r0,16
	cmpwi	r0,0x39		/* 970 */
	beq	1f
	cmpwi	r0,0x3c		/* 970FX */
	beq	1f
	cmpwi	r0,0x44		/* 970MP */
	bne	2f
1:	bl	.__cpu_preinit_ppc970
2:

	/* Switch off MMU if not already */
	LOAD_REG_IMMEDIATE(r4, .__after_prom_start - KERNELBASE)
	add	r4,r4,r30
	bl	.__mmu_off
	b	.__after_prom_start

#ifdef CONFIG_PPC_MULTIPLATFORM
_STATIC(__boot_from_prom)
	/* Save parameters */
	mr	r31,r3
	mr	r30,r4
	mr	r29,r5
	mr	r28,r6
	mr	r27,r7

	/*
	 * Align the stack to 16-byte boundary
	 * Depending on the size and layout of the ELF sections in the initial
	 * boot binary, the stack pointer will be unalignet on PowerMac
	 */
	rldicr	r1,r1,0,59

	/* Make sure we are running in 64 bits mode */
	bl	.enable_64b_mode

	/* put a relocation offset into r3 */
	bl	.reloc_offset

	LOAD_REG_IMMEDIATE(r2,__toc_start)
	addi	r2,r2,0x4000
	addi	r2,r2,0x4000

	/* Relocate the TOC from a virt addr to a real addr */
	add	r2,r2,r3

	/* Restore parameters */
	mr	r3,r31
	mr	r4,r30
	mr	r5,r29
	mr	r6,r28
	mr	r7,r27

	/* Do all of the interaction with OF client interface */
	bl	.prom_init
	/* We never return */
	trap
#endif

/*
 * At this point, r3 contains the physical address we are running at,
 * returned by prom_init()
 */
_STATIC(__after_prom_start)

/*
 * We need to run with __start at physical address PHYSICAL_START.
 * This will leave some code in the first 256B of
 * real memory, which are reserved for software use.
 * The remainder of the first page is loaded with the fixed
 * interrupt vectors.  The next two pages are filled with
 * unknown exception placeholders.
 *
 * Note: This process overwrites the OF exception vectors.
 *	r26 == relocation offset
 *	r27 == KERNELBASE
 */
	bl	.reloc_offset
	mr	r26,r3
	LOAD_REG_IMMEDIATE(r27, KERNELBASE)

	LOAD_REG_IMMEDIATE(r3, PHYSICAL_START)	/* target addr */

	// XXX FIXME: Use phys returned by OF (r30)
	add	r4,r27,r26 		/* source addr			 */
					/* current address of _start	 */
					/*   i.e. where we are running	 */
					/*	the source addr		 */

	cmpdi	r4,0			/* In some cases the loader may  */
	beq	.start_here_multiplatform /* have already put us at zero */
					/* so we can skip the copy.      */
	LOAD_REG_IMMEDIATE(r5,copy_to_here) /* # bytes of memory to copy */
	sub	r5,r5,r27

	li	r6,0x100		/* Start offset, the first 0x100 */
					/* bytes were copied earlier.	 */

	bl	.copy_and_flush		/* copy the first n bytes	 */
					/* this includes the code being	 */
					/* executed here.		 */

	LOAD_REG_IMMEDIATE(r0, 4f)	/* Jump to the copy of this code */
	mtctr	r0			/* that we just made/relocated	 */
	bctr

4:	LOAD_REG_IMMEDIATE(r5,klimit)
	add	r5,r5,r26
	ld	r5,0(r5)		/* get the value of klimit */
	sub	r5,r5,r27
	bl	.copy_and_flush		/* copy the rest */
	b	.start_here_multiplatform

#endif /* CONFIG_PPC_MULTIPLATFORM */

/*
 * Copy routine used to copy the kernel to start at physical address 0
 * and flush and invalidate the caches as needed.
 * r3 = dest addr, r4 = source addr, r5 = copy limit, r6 = start offset
 * on exit, r3, r4, r5 are unchanged, r6 is updated to be >= r5.
 *
 * Note: this routine *only* clobbers r0, r6 and lr
 */
_GLOBAL(copy_and_flush)
	addi	r5,r5,-8
	addi	r6,r6,-8
4:	li	r0,8			/* Use the smallest common	*/
					/* denominator cache line	*/
					/* size.  This results in	*/
					/* extra cache line flushes	*/
					/* but operation is correct.	*/
					/* Can't get cache line size	*/
					/* from NACA as it is being	*/
					/* moved too.			*/

	mtctr	r0			/* put # words/line in ctr	*/
3:	addi	r6,r6,8			/* copy a cache line		*/
	ldx	r0,r6,r4
	stdx	r0,r6,r3
	bdnz	3b
	dcbst	r6,r3			/* write it to memory		*/
	sync
	icbi	r6,r3			/* flush the icache line	*/
	cmpld	0,r6,r5
	blt	4b
	sync
	addi	r5,r5,8
	addi	r6,r6,8
	blr

.align 8
copy_to_here:

#ifdef CONFIG_SMP
#ifdef CONFIG_PPC_PMAC
/*
 * On PowerMac, secondary processors starts from the reset vector, which
 * is temporarily turned into a call to one of the functions below.
 */
	.section ".text";
	.align 2 ;

	.globl	__secondary_start_pmac_0
__secondary_start_pmac_0:
	/* NB the entries for cpus 0, 1, 2 must each occupy 8 bytes. */
	li	r24,0
	b	1f
	li	r24,1
	b	1f
	li	r24,2
	b	1f
	li	r24,3
1:
	
_GLOBAL(pmac_secondary_start)
	/* turn on 64-bit mode */
	bl	.enable_64b_mode
	isync

	/* Copy some CPU settings from CPU 0 */
	bl	.__restore_cpu_ppc970

	/* pSeries do that early though I don't think we really need it */
	mfmsr	r3
	ori	r3,r3,MSR_RI
	mtmsrd	r3			/* RI on */

	/* Set up a paca value for this processor. */
	LOAD_REG_IMMEDIATE(r4, paca)	/* Get base vaddr of paca array	*/
	mulli	r13,r24,PACA_SIZE	 /* Calculate vaddr of right paca */
	add	r13,r13,r4		/* for this processor.		*/
	mtspr	SPRN_SPRG3,r13		 /* Save vaddr of paca in SPRG3	*/

	/* Create a temp kernel stack for use before relocation is on.	*/
	ld	r1,PACAEMERGSP(r13)
	subi	r1,r1,STACK_FRAME_OVERHEAD

	b	.__secondary_start

#endif /* CONFIG_PPC_PMAC */

/*
 * This function is called after the master CPU has released the
 * secondary processors.  The execution environment is relocation off.
 * The paca for this processor has the following fields initialized at
 * this point:
 *   1. Processor number
 *   2. Segment table pointer (virtual address)
 * On entry the following are set:
 *   r1	= stack pointer.  vaddr for iSeries, raddr (temp stack) for pSeries
 *   r24   = cpu# (in Linux terms)
 *   r13   = paca virtual address
 *   SPRG3 = paca virtual address
 */
_GLOBAL(__secondary_start)
	/* Set thread priority to MEDIUM */
	HMT_MEDIUM

	/* Load TOC */
	ld	r2,PACATOC(r13)

	/* Do early setup for that CPU (stab, slb, hash table pointer) */
	bl	.early_setup_secondary

	/* Initialize the kernel stack.  Just a repeat for iSeries.	 */
	LOAD_REG_ADDR(r3, current_set)
	sldi	r28,r24,3		/* get current_set[cpu#]	 */
	ldx	r1,r3,r28
	addi	r1,r1,THREAD_SIZE-STACK_FRAME_OVERHEAD
	std	r1,PACAKSAVE(r13)

	/* Clear backchain so we get nice backtraces */
	li	r7,0
	mtlr	r7

	/* enable MMU and jump to start_secondary */
	LOAD_REG_ADDR(r3, .start_secondary_prolog)
	LOAD_REG_IMMEDIATE(r4, MSR_KERNEL)
#ifdef DO_SOFT_DISABLE
BEGIN_FW_FTR_SECTION
	ori	r4,r4,MSR_EE
END_FW_FTR_SECTION_IFSET(FW_FEATURE_ISERIES)
#endif
	mtspr	SPRN_SRR0,r3
	mtspr	SPRN_SRR1,r4
	rfid
	b	.	/* prevent speculative execution */

/* 
 * Running with relocation on at this point.  All we want to do is
 * zero the stack back-chain pointer before going into C code.
 */
_GLOBAL(start_secondary_prolog)
	li	r3,0
	std	r3,0(r1)		/* Zero the stack frame pointer	*/
	bl	.start_secondary
	b	.
#endif

/*
 * This subroutine clobbers r11 and r12
 */
_GLOBAL(enable_64b_mode)
	mfmsr	r11			/* grab the current MSR */
	li	r12,1
	rldicr	r12,r12,MSR_SF_LG,(63-MSR_SF_LG)
	or	r11,r11,r12
	li	r12,1
	rldicr	r12,r12,MSR_ISF_LG,(63-MSR_ISF_LG)
	or	r11,r11,r12
	mtmsrd	r11
	isync
	blr

#ifdef CONFIG_PPC_MULTIPLATFORM
/*
 * This is where the main kernel code starts.
 */
_STATIC(start_here_multiplatform)
	/* get a new offset, now that the kernel has moved. */
	bl	.reloc_offset
	mr	r26,r3

	/* Clear out the BSS. It may have been done in prom_init,
	 * already but that's irrelevant since prom_init will soon
	 * be detached from the kernel completely. Besides, we need
	 * to clear it now for kexec-style entry.
	 */
	LOAD_REG_IMMEDIATE(r11,__bss_stop)
	LOAD_REG_IMMEDIATE(r8,__bss_start)
	sub	r11,r11,r8		/* bss size			*/
	addi	r11,r11,7		/* round up to an even double word */
	rldicl. r11,r11,61,3		/* shift right by 3		*/
	beq	4f
	addi	r8,r8,-8
	li	r0,0
	mtctr	r11			/* zero this many doublewords	*/
3:	stdu	r0,8(r8)
	bdnz	3b
4:

	mfmsr	r6
	ori	r6,r6,MSR_RI
	mtmsrd	r6			/* RI on */

	/* The following gets the stack and TOC set up with the regs */
	/* pointing to the real addr of the kernel stack.  This is   */
	/* all done to support the C function call below which sets  */
	/* up the htab.  This is done because we have relocated the  */
	/* kernel but are still running in real mode. */

	LOAD_REG_IMMEDIATE(r3,init_thread_union)
	add	r3,r3,r26

	/* set up a stack pointer (physical address) */
	addi	r1,r3,THREAD_SIZE
	li	r0,0
	stdu	r0,-STACK_FRAME_OVERHEAD(r1)

	/* set up the TOC (physical address) */
	LOAD_REG_IMMEDIATE(r2,__toc_start)
	addi	r2,r2,0x4000
	addi	r2,r2,0x4000
	add	r2,r2,r26

	/* Do very early kernel initializations, including initial hash table,
	 * stab and slb setup before we turn on relocation.	*/

	/* Restore parameters passed from prom_init/kexec */
	mr	r3,r31
 	bl	.early_setup

	LOAD_REG_IMMEDIATE(r3, .start_here_common)
	LOAD_REG_IMMEDIATE(r4, MSR_KERNEL)
	mtspr	SPRN_SRR0,r3
	mtspr	SPRN_SRR1,r4
	rfid
	b	.	/* prevent speculative execution */
#endif /* CONFIG_PPC_MULTIPLATFORM */
	
	/* This is where all platforms converge execution */
_STATIC(start_here_common)
	/* relocation is on at this point */

	/* The following code sets up the SP and TOC now that we are */
	/* running with translation enabled. */

	LOAD_REG_IMMEDIATE(r3,init_thread_union)

	/* set up the stack */
	addi	r1,r3,THREAD_SIZE
	li	r0,0
	stdu	r0,-STACK_FRAME_OVERHEAD(r1)

	/* ptr to current */
	LOAD_REG_IMMEDIATE(r4, init_task)
	std	r4,PACACURRENT(r13)

	/* Load the TOC */
	ld	r2,PACATOC(r13)
	std	r1,PACAKSAVE(r13)

	bl	.setup_system

	/* Load up the kernel context */
5:
#ifdef DO_SOFT_DISABLE
BEGIN_FW_FTR_SECTION
	li	r5,0
	stb	r5,PACAPROCENABLED(r13)	/* Soft Disabled */
	mfmsr	r5
	ori	r5,r5,MSR_EE		/* Hard Enabled */
	mtmsrd	r5
END_FW_FTR_SECTION_IFSET(FW_FEATURE_ISERIES)
#endif

	bl .start_kernel

	/* Not reached */
	BUG_OPCODE

/*
 * We put a few things here that have to be page-aligned.
 * This stuff goes at the beginning of the bss, which is page-aligned.
 */
	.section ".bss"

	.align	PAGE_SHIFT

	.globl	empty_zero_page
empty_zero_page:
	.space	PAGE_SIZE

	.globl	swapper_pg_dir
swapper_pg_dir:
	.space	PAGE_SIZE

/*
 * This space gets a copy of optional info passed to us by the bootstrap
 * Used to pass parameters into the kernel like root=/dev/sda1, etc.
 */
	.globl	cmd_line
cmd_line:
	.space	COMMAND_LINE_SIZE
