/* SPDX-License-Identifier: GPL-2.0
 *
 * arch/sh/kernel/cpu/sh5/entry.S
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2004 - 2008  Paul Mundt
 * Copyright (C) 2003, 2004  Richard Curnow
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/sys.h>
#include <cpu/registers.h>
#include <asm/processor.h>
#include <asm/unistd.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>

/*
 * SR fields.
 */
#define SR_ASID_MASK	0x00ff0000
#define SR_FD_MASK	0x00008000
#define SR_SS		0x08000000
#define SR_BL		0x10000000
#define SR_MD		0x40000000

/*
 * Event code.
 */
#define	EVENT_INTERRUPT		0
#define	EVENT_FAULT_TLB		1
#define	EVENT_FAULT_NOT_TLB	2
#define	EVENT_DEBUG		3

/* EXPEVT values */
#define	RESET_CAUSE		0x20
#define DEBUGSS_CAUSE		0x980

/*
 * Frame layout. Quad index.
 */
#define	FRAME_T(x)	FRAME_TBASE+(x*8)
#define	FRAME_R(x)	FRAME_RBASE+(x*8)
#define	FRAME_S(x)	FRAME_SBASE+(x*8)
#define FSPC		0
#define FSSR		1
#define FSYSCALL_ID	2

/* Arrange the save frame to be a multiple of 32 bytes long */
#define FRAME_SBASE	0
#define FRAME_RBASE	(FRAME_SBASE+(3*8))	/* SYSCALL_ID - SSR - SPC */
#define FRAME_TBASE	(FRAME_RBASE+(63*8))	/* r0 - r62 */
#define FRAME_PBASE	(FRAME_TBASE+(8*8))	/* tr0 -tr7 */
#define	FRAME_SIZE	(FRAME_PBASE+(2*8))	/* pad0-pad1 */

#define FP_FRAME_SIZE	FP_FRAME_BASE+(33*8)	/* dr0 - dr31 + fpscr */
#define FP_FRAME_BASE	0

#define	SAVED_R2	0*8
#define	SAVED_R3	1*8
#define	SAVED_R4	2*8
#define	SAVED_R5	3*8
#define	SAVED_R18	4*8
#define	SAVED_R6	5*8
#define	SAVED_TR0	6*8

/* These are the registers saved in the TLB path that aren't saved in the first
   level of the normal one. */
#define	TLB_SAVED_R25	7*8
#define	TLB_SAVED_TR1	8*8
#define	TLB_SAVED_TR2	9*8
#define	TLB_SAVED_TR3	10*8
#define	TLB_SAVED_TR4	11*8
/* Save R0/R1 : PT-migrating compiler currently dishounours -ffixed-r0 and -ffixed-r1 causing
   breakage otherwise. */
#define	TLB_SAVED_R0	12*8
#define	TLB_SAVED_R1	13*8

#define CLI()				\
	getcon	SR, r6;			\
	ori	r6, 0xf0, r6;		\
	putcon	r6, SR;

#define STI()				\
	getcon	SR, r6;			\
	andi	r6, ~0xf0, r6;		\
	putcon	r6, SR;

#ifdef CONFIG_PREEMPTION
#  define preempt_stop()	CLI()
#else
#  define preempt_stop()
#  define resume_kernel		restore_all
#endif

	.section	.data, "aw"

#define FAST_TLBMISS_STACK_CACHELINES 4
#define FAST_TLBMISS_STACK_QUADWORDS (4*FAST_TLBMISS_STACK_CACHELINES)

/* Register back-up area for all exceptions */
	.balign	32
	/* Allow for 16 quadwords to be pushed by fast tlbmiss handling
	 * register saves etc. */
	.fill FAST_TLBMISS_STACK_QUADWORDS, 8, 0x0
/* This is 32 byte aligned by construction */
/* Register back-up area for all exceptions */
reg_save_area:
	.quad	0
	.quad	0
	.quad	0
	.quad	0

	.quad	0
	.quad	0
	.quad	0
	.quad	0

	.quad	0
	.quad	0
	.quad	0
	.quad	0

	.quad	0
	.quad   0

/* Save area for RESVEC exceptions. We cannot use reg_save_area because of
 * reentrancy. Note this area may be accessed via physical address.
 * Align so this fits a whole single cache line, for ease of purging.
 */
	.balign 32,0,32
resvec_save_area:
	.quad	0
	.quad	0
	.quad	0
	.quad	0
	.quad	0
	.balign 32,0,32

/* Jump table of 3rd level handlers  */
trap_jtable:
	.long	do_exception_error		/* 0x000 */
	.long	do_exception_error		/* 0x020 */
#ifdef CONFIG_MMU
	.long	tlb_miss_load				/* 0x040 */
	.long	tlb_miss_store				/* 0x060 */
#else
	.long	do_exception_error
	.long	do_exception_error
#endif
	! ARTIFICIAL pseudo-EXPEVT setting
	.long	do_debug_interrupt		/* 0x080 */
#ifdef CONFIG_MMU
	.long	tlb_miss_load				/* 0x0A0 */
	.long	tlb_miss_store				/* 0x0C0 */
#else
	.long	do_exception_error
	.long	do_exception_error
#endif
	.long	do_address_error_load	/* 0x0E0 */
	.long	do_address_error_store	/* 0x100 */
#ifdef CONFIG_SH_FPU
	.long	do_fpu_error		/* 0x120 */
#else
	.long	do_exception_error		/* 0x120 */
#endif
	.long	do_exception_error		/* 0x140 */
	.long	system_call				/* 0x160 */
	.long	do_reserved_inst		/* 0x180 */
	.long	do_illegal_slot_inst	/* 0x1A0 */
	.long	do_exception_error		/* 0x1C0 - NMI */
	.long	do_exception_error		/* 0x1E0 */
	.rept 15
		.long do_IRQ		/* 0x200 - 0x3C0 */
	.endr
	.long	do_exception_error		/* 0x3E0 */
	.rept 32
		.long do_IRQ		/* 0x400 - 0x7E0 */
	.endr
	.long	fpu_error_or_IRQA			/* 0x800 */
	.long	fpu_error_or_IRQB			/* 0x820 */
	.long	do_IRQ			/* 0x840 */
	.long	do_IRQ			/* 0x860 */
	.rept 6
		.long do_exception_error	/* 0x880 - 0x920 */
	.endr
	.long	breakpoint_trap_handler	/* 0x940 */
	.long	do_exception_error		/* 0x960 */
	.long	do_single_step		/* 0x980 */

	.rept 3
		.long do_exception_error	/* 0x9A0 - 0x9E0 */
	.endr
	.long	do_IRQ			/* 0xA00 */
	.long	do_IRQ			/* 0xA20 */
#ifdef CONFIG_MMU
	.long	itlb_miss_or_IRQ			/* 0xA40 */
#else
	.long	do_IRQ
#endif
	.long	do_IRQ			/* 0xA60 */
	.long	do_IRQ			/* 0xA80 */
#ifdef CONFIG_MMU
	.long	itlb_miss_or_IRQ			/* 0xAA0 */
#else
	.long	do_IRQ
#endif
	.long	do_exception_error		/* 0xAC0 */
	.long	do_address_error_exec	/* 0xAE0 */
	.rept 8
		.long do_exception_error	/* 0xB00 - 0xBE0 */
	.endr
	.rept 18
		.long do_IRQ		/* 0xC00 - 0xE20 */
	.endr

	.section	.text64, "ax"

/*
 * --- Exception/Interrupt/Event Handling Section
 */

/*
 * VBR and RESVEC blocks.
 *
 * First level handler for VBR-based exceptions.
 *
 * To avoid waste of space, align to the maximum text block size.
 * This is assumed to be at most 128 bytes or 32 instructions.
 * DO NOT EXCEED 32 instructions on the first level handlers !
 *
 * Also note that RESVEC is contained within the VBR block
 * where the room left (1KB - TEXT_SIZE) allows placing
 * the RESVEC block (at most 512B + TEXT_SIZE).
 *
 * So first (and only) level handler for RESVEC-based exceptions.
 *
 * Where the fault/interrupt is handled (not_a_tlb_miss, tlb_miss
 * and interrupt) we are a lot tight with register space until
 * saving onto the stack frame, which is done in handle_exception().
 *
 */

#define	TEXT_SIZE 	128
#define	BLOCK_SIZE 	1664 		/* Dynamic check, 13*128 */

	.balign TEXT_SIZE
LVBR_block:
	.space	256, 0			/* Power-on class handler, */
					/* not required here       */
not_a_tlb_miss:
	synco	/* TAKum03020 (but probably a good idea anyway.) */
	/* Save original stack pointer into KCR1 */
	putcon	SP, KCR1

	/* Save other original registers into reg_save_area */
        movi  reg_save_area, SP
	st.q	SP, SAVED_R2, r2
	st.q	SP, SAVED_R3, r3
	st.q	SP, SAVED_R4, r4
	st.q	SP, SAVED_R5, r5
	st.q	SP, SAVED_R6, r6
	st.q	SP, SAVED_R18, r18
	gettr	tr0, r3
	st.q	SP, SAVED_TR0, r3

	/* Set args for Non-debug, Not a TLB miss class handler */
	getcon	EXPEVT, r2
	movi	ret_from_exception, r3
	ori	r3, 1, r3
	movi	EVENT_FAULT_NOT_TLB, r4
	or	SP, ZERO, r5
	getcon	KCR1, SP
	pta	handle_exception, tr0
	blink	tr0, ZERO

	.balign 256
	! VBR+0x200
	nop
	.balign 256
	! VBR+0x300
	nop
	.balign 256
	/*
	 * Instead of the natural .balign 1024 place RESVEC here
	 * respecting the final 1KB alignment.
	 */
	.balign TEXT_SIZE
	/*
	 * Instead of '.space 1024-TEXT_SIZE' place the RESVEC
	 * block making sure the final alignment is correct.
	 */
#ifdef CONFIG_MMU
tlb_miss:
	synco	/* TAKum03020 (but probably a good idea anyway.) */
	putcon	SP, KCR1
	movi	reg_save_area, SP
	/* SP is guaranteed 32-byte aligned. */
	st.q	SP, TLB_SAVED_R0 , r0
	st.q	SP, TLB_SAVED_R1 , r1
	st.q	SP, SAVED_R2 , r2
	st.q	SP, SAVED_R3 , r3
	st.q	SP, SAVED_R4 , r4
	st.q	SP, SAVED_R5 , r5
	st.q	SP, SAVED_R6 , r6
	st.q	SP, SAVED_R18, r18

	/* Save R25 for safety; as/ld may want to use it to achieve the call to
	 * the code in mm/tlbmiss.c */
	st.q	SP, TLB_SAVED_R25, r25
	gettr	tr0, r2
	gettr	tr1, r3
	gettr	tr2, r4
	gettr	tr3, r5
	gettr	tr4, r18
	st.q	SP, SAVED_TR0 , r2
	st.q	SP, TLB_SAVED_TR1 , r3
	st.q	SP, TLB_SAVED_TR2 , r4
	st.q	SP, TLB_SAVED_TR3 , r5
	st.q	SP, TLB_SAVED_TR4 , r18

	pt	do_fast_page_fault, tr0
	getcon	SSR, r2
	getcon	EXPEVT, r3
	getcon	TEA, r4
	shlri	r2, 30, r2
	andi	r2, 1, r2	/* r2 = SSR.MD */
	blink 	tr0, LINK

	pt	fixup_to_invoke_general_handler, tr1

	/* If the fast path handler fixed the fault, just drop through quickly
	   to the restore code right away to return to the excepting context.
	   */
	bnei/u	r2, 0, tr1

fast_tlb_miss_restore:
	ld.q	SP, SAVED_TR0, r2
	ld.q	SP, TLB_SAVED_TR1, r3
	ld.q	SP, TLB_SAVED_TR2, r4

	ld.q	SP, TLB_SAVED_TR3, r5
	ld.q	SP, TLB_SAVED_TR4, r18

	ptabs	r2, tr0
	ptabs	r3, tr1
	ptabs	r4, tr2
	ptabs	r5, tr3
	ptabs	r18, tr4

	ld.q	SP, TLB_SAVED_R0, r0
	ld.q	SP, TLB_SAVED_R1, r1
	ld.q	SP, SAVED_R2, r2
	ld.q	SP, SAVED_R3, r3
	ld.q	SP, SAVED_R4, r4
	ld.q	SP, SAVED_R5, r5
	ld.q	SP, SAVED_R6, r6
	ld.q	SP, SAVED_R18, r18
	ld.q	SP, TLB_SAVED_R25, r25

	getcon	KCR1, SP
	rte
	nop /* for safety, in case the code is run on sh5-101 cut1.x */

fixup_to_invoke_general_handler:

	/* OK, new method.  Restore stuff that's not expected to get saved into
	   the 'first-level' reg save area, then just fall through to setting
	   up the registers and calling the second-level handler. */

	/* 2nd level expects r2,3,4,5,6,18,tr0 to be saved.  So we must restore
	   r25,tr1-4 and save r6 to get into the right state.  */

	ld.q	SP, TLB_SAVED_TR1, r3
	ld.q	SP, TLB_SAVED_TR2, r4
	ld.q	SP, TLB_SAVED_TR3, r5
	ld.q	SP, TLB_SAVED_TR4, r18
	ld.q	SP, TLB_SAVED_R25, r25

	ld.q	SP, TLB_SAVED_R0, r0
	ld.q	SP, TLB_SAVED_R1, r1

	ptabs/u	r3, tr1
	ptabs/u	r4, tr2
	ptabs/u	r5, tr3
	ptabs/u	r18, tr4

	/* Set args for Non-debug, TLB miss class handler */
	getcon	EXPEVT, r2
	movi	ret_from_exception, r3
	ori	r3, 1, r3
	movi	EVENT_FAULT_TLB, r4
	or	SP, ZERO, r5
	getcon	KCR1, SP
	pta	handle_exception, tr0
	blink	tr0, ZERO
#else /* CONFIG_MMU */
	.balign 256
#endif

/* NB TAKE GREAT CARE HERE TO ENSURE THAT THE INTERRUPT CODE
   DOES END UP AT VBR+0x600 */
	nop
	nop
	nop
	nop
	nop
	nop

	.balign 256
	/* VBR + 0x600 */

interrupt:
	synco	/* TAKum03020 (but probably a good idea anyway.) */
	/* Save original stack pointer into KCR1 */
	putcon	SP, KCR1

	/* Save other original registers into reg_save_area */
        movi  reg_save_area, SP
	st.q	SP, SAVED_R2, r2
	st.q	SP, SAVED_R3, r3
	st.q	SP, SAVED_R4, r4
	st.q	SP, SAVED_R5, r5
	st.q	SP, SAVED_R6, r6
	st.q	SP, SAVED_R18, r18
	gettr	tr0, r3
	st.q	SP, SAVED_TR0, r3

	/* Set args for interrupt class handler */
	getcon	INTEVT, r2
	movi	ret_from_irq, r3
	ori	r3, 1, r3
	movi	EVENT_INTERRUPT, r4
	or	SP, ZERO, r5
	getcon	KCR1, SP
	pta	handle_exception, tr0
	blink	tr0, ZERO
	.balign	TEXT_SIZE		/* let's waste the bare minimum */

LVBR_block_end:				/* Marker. Used for total checking */

	.balign 256
LRESVEC_block:
	/* Panic handler. Called with MMU off. Possible causes/actions:
	 * - Reset:		Jump to program start.
	 * - Single Step:	Turn off Single Step & return.
	 * - Others:		Call panic handler, passing PC as arg.
	 *			(this may need to be extended...)
	 */
reset_or_panic:
	synco	/* TAKum03020 (but probably a good idea anyway.) */
	putcon	SP, DCR
	/* First save r0-1 and tr0, as we need to use these */
	movi	resvec_save_area-CONFIG_PAGE_OFFSET, SP
	st.q	SP, 0, r0
	st.q	SP, 8, r1
	gettr	tr0, r0
	st.q	SP, 32, r0

	/* Check cause */
	getcon	EXPEVT, r0
	movi	RESET_CAUSE, r1
	sub	r1, r0, r1		/* r1=0 if reset */
	movi	_stext-CONFIG_PAGE_OFFSET, r0
	ori	r0, 1, r0
	ptabs	r0, tr0
	beqi	r1, 0, tr0		/* Jump to start address if reset */

	getcon	EXPEVT, r0
	movi	DEBUGSS_CAUSE, r1
	sub	r1, r0, r1		/* r1=0 if single step */
	pta	single_step_panic, tr0
	beqi	r1, 0, tr0		/* jump if single step */

	/* Now jump to where we save the registers. */
	movi	panic_stash_regs-CONFIG_PAGE_OFFSET, r1
	ptabs	r1, tr0
	blink	tr0, r63

single_step_panic:
	/* We are in a handler with Single Step set. We need to resume the
	 * handler, by turning on MMU & turning off Single Step. */
	getcon	SSR, r0
	movi	SR_MMU, r1
	or	r0, r1, r0
	movi	~SR_SS, r1
	and	r0, r1, r0
	putcon	r0, SSR
	/* Restore EXPEVT, as the rte won't do this */
	getcon	PEXPEVT, r0
	putcon	r0, EXPEVT
	/* Restore regs */
	ld.q	SP, 32, r0
	ptabs	r0, tr0
	ld.q	SP, 0, r0
	ld.q	SP, 8, r1
	getcon	DCR, SP
	synco
	rte


	.balign	256
debug_exception:
	synco	/* TAKum03020 (but probably a good idea anyway.) */
	/*
	 * Single step/software_break_point first level handler.
	 * Called with MMU off, so the first thing we do is enable it
	 * by doing an rte with appropriate SSR.
	 */
	putcon	SP, DCR
	/* Save SSR & SPC, together with R0 & R1, as we need to use 2 regs. */
	movi	resvec_save_area-CONFIG_PAGE_OFFSET, SP

	/* With the MMU off, we are bypassing the cache, so purge any
         * data that will be made stale by the following stores.
         */
	ocbp	SP, 0
	synco

	st.q	SP, 0, r0
	st.q	SP, 8, r1
	getcon	SPC, r0
	st.q	SP, 16, r0
	getcon	SSR, r0
	st.q	SP, 24, r0

	/* Enable MMU, block exceptions, set priv mode, disable single step */
	movi	SR_MMU | SR_BL | SR_MD, r1
	or	r0, r1, r0
	movi	~SR_SS, r1
	and	r0, r1, r0
	putcon	r0, SSR
	/* Force control to debug_exception_2 when rte is executed */
	movi	debug_exeception_2, r0
	ori	r0, 1, r0      /* force SHmedia, just in case */
	putcon	r0, SPC
	getcon	DCR, SP
	synco
	rte
debug_exeception_2:
	/* Restore saved regs */
	putcon	SP, KCR1
	movi	resvec_save_area, SP
	ld.q	SP, 24, r0
	putcon	r0, SSR
	ld.q	SP, 16, r0
	putcon	r0, SPC
	ld.q	SP, 0, r0
	ld.q	SP, 8, r1

	/* Save other original registers into reg_save_area */
        movi  reg_save_area, SP
	st.q	SP, SAVED_R2, r2
	st.q	SP, SAVED_R3, r3
	st.q	SP, SAVED_R4, r4
	st.q	SP, SAVED_R5, r5
	st.q	SP, SAVED_R6, r6
	st.q	SP, SAVED_R18, r18
	gettr	tr0, r3
	st.q	SP, SAVED_TR0, r3

	/* Set args for debug class handler */
	getcon	EXPEVT, r2
	movi	ret_from_exception, r3
	ori	r3, 1, r3
	movi	EVENT_DEBUG, r4
	or	SP, ZERO, r5
	getcon	KCR1, SP
	pta	handle_exception, tr0
	blink	tr0, ZERO

	.balign	256
debug_interrupt:
	/* !!! WE COME HERE IN REAL MODE !!! */
	/* Hook-up debug interrupt to allow various debugging options to be
	 * hooked into its handler. */
	/* Save original stack pointer into KCR1 */
	synco
	putcon	SP, KCR1
	movi	resvec_save_area-CONFIG_PAGE_OFFSET, SP
	ocbp	SP, 0
	ocbp	SP, 32
	synco

	/* Save other original registers into reg_save_area thru real addresses */
	st.q	SP, SAVED_R2, r2
	st.q	SP, SAVED_R3, r3
	st.q	SP, SAVED_R4, r4
	st.q	SP, SAVED_R5, r5
	st.q	SP, SAVED_R6, r6
	st.q	SP, SAVED_R18, r18
	gettr	tr0, r3
	st.q	SP, SAVED_TR0, r3

	/* move (spc,ssr)->(pspc,pssr).  The rte will shift
	   them back again, so that they look like the originals
	   as far as the real handler code is concerned. */
	getcon	spc, r6
	putcon	r6, pspc
	getcon	ssr, r6
	putcon	r6, pssr

	! construct useful SR for handle_exception
	movi	3, r6
	shlli	r6, 30, r6
	getcon	sr, r18
	or	r18, r6, r6
	putcon	r6, ssr

	! SSR is now the current SR with the MD and MMU bits set
	! i.e. the rte will switch back to priv mode and put
	! the mmu back on

	! construct spc
	movi	handle_exception, r18
	ori	r18, 1, r18		! for safety (do we need this?)
	putcon	r18, spc

	/* Set args for Non-debug, Not a TLB miss class handler */

	! EXPEVT==0x80 is unused, so 'steal' this value to put the
	! debug interrupt handler in the vectoring table
	movi	0x80, r2
	movi	ret_from_exception, r3
	ori	r3, 1, r3
	movi	EVENT_FAULT_NOT_TLB, r4

	or	SP, ZERO, r5
	movi	CONFIG_PAGE_OFFSET, r6
	add	r6, r5, r5
	getcon	KCR1, SP

	synco	! for safety
	rte	! -> handle_exception, switch back to priv mode again

LRESVEC_block_end:			/* Marker. Unused. */

	.balign	TEXT_SIZE

/*
 * Second level handler for VBR-based exceptions. Pre-handler.
 * In common to all stack-frame sensitive handlers.
 *
 * Inputs:
 * (KCR0) Current [current task union]
 * (KCR1) Original SP
 * (r2)   INTEVT/EXPEVT
 * (r3)   appropriate return address
 * (r4)   Event (0 = interrupt, 1 = TLB miss fault, 2 = Not TLB miss fault, 3=debug)
 * (r5)   Pointer to reg_save_area
 * (SP)   Original SP
 *
 * Available registers:
 * (r6)
 * (r18)
 * (tr0)
 *
 */
handle_exception:
	/* Common 2nd level handler. */

	/* First thing we need an appropriate stack pointer */
	getcon	SSR, r6
	shlri	r6, 30, r6
	andi	r6, 1, r6
	pta	stack_ok, tr0
	bne	r6, ZERO, tr0		/* Original stack pointer is fine */

	/* Set stack pointer for user fault */
	getcon	KCR0, SP
	movi	THREAD_SIZE, r6		/* Point to the end */
	add	SP, r6, SP

stack_ok:

/* DEBUG : check for underflow/overflow of the kernel stack */
	pta	no_underflow, tr0
	getcon  KCR0, r6
	movi	1024, r18
	add	r6, r18, r6
	bge	SP, r6, tr0 	! ? below 1k from bottom of stack : danger zone

/* Just panic to cause a crash. */
bad_sp:
	ld.b	r63, 0, r6
	nop

no_underflow:
	pta	bad_sp, tr0
	getcon	kcr0, r6
	movi	THREAD_SIZE, r18
	add	r18, r6, r6
	bgt	SP, r6, tr0	! sp above the stack

	/* Make some room for the BASIC frame. */
	movi	-(FRAME_SIZE), r6
	add	SP, r6, SP

/* Could do this with no stalling if we had another spare register, but the
   code below will be OK. */
	ld.q	r5, SAVED_R2, r6
	ld.q	r5, SAVED_R3, r18
	st.q	SP, FRAME_R(2), r6
	ld.q	r5, SAVED_R4, r6
	st.q	SP, FRAME_R(3), r18
	ld.q	r5, SAVED_R5, r18
	st.q	SP, FRAME_R(4), r6
	ld.q	r5, SAVED_R6, r6
	st.q	SP, FRAME_R(5), r18
	ld.q	r5, SAVED_R18, r18
	st.q	SP, FRAME_R(6), r6
	ld.q	r5, SAVED_TR0, r6
	st.q	SP, FRAME_R(18), r18
	st.q	SP, FRAME_T(0), r6

	/* Keep old SP around */
	getcon	KCR1, r6

	/* Save the rest of the general purpose registers */
	st.q	SP, FRAME_R(0), r0
	st.q	SP, FRAME_R(1), r1
	st.q	SP, FRAME_R(7), r7
	st.q	SP, FRAME_R(8), r8
	st.q	SP, FRAME_R(9), r9
	st.q	SP, FRAME_R(10), r10
	st.q	SP, FRAME_R(11), r11
	st.q	SP, FRAME_R(12), r12
	st.q	SP, FRAME_R(13), r13
	st.q	SP, FRAME_R(14), r14

	/* SP is somewhere else */
	st.q	SP, FRAME_R(15), r6

	st.q	SP, FRAME_R(16), r16
	st.q	SP, FRAME_R(17), r17
	/* r18 is saved earlier. */
	st.q	SP, FRAME_R(19), r19
	st.q	SP, FRAME_R(20), r20
	st.q	SP, FRAME_R(21), r21
	st.q	SP, FRAME_R(22), r22
	st.q	SP, FRAME_R(23), r23
	st.q	SP, FRAME_R(24), r24
	st.q	SP, FRAME_R(25), r25
	st.q	SP, FRAME_R(26), r26
	st.q	SP, FRAME_R(27), r27
	st.q	SP, FRAME_R(28), r28
	st.q	SP, FRAME_R(29), r29
	st.q	SP, FRAME_R(30), r30
	st.q	SP, FRAME_R(31), r31
	st.q	SP, FRAME_R(32), r32
	st.q	SP, FRAME_R(33), r33
	st.q	SP, FRAME_R(34), r34
	st.q	SP, FRAME_R(35), r35
	st.q	SP, FRAME_R(36), r36
	st.q	SP, FRAME_R(37), r37
	st.q	SP, FRAME_R(38), r38
	st.q	SP, FRAME_R(39), r39
	st.q	SP, FRAME_R(40), r40
	st.q	SP, FRAME_R(41), r41
	st.q	SP, FRAME_R(42), r42
	st.q	SP, FRAME_R(43), r43
	st.q	SP, FRAME_R(44), r44
	st.q	SP, FRAME_R(45), r45
	st.q	SP, FRAME_R(46), r46
	st.q	SP, FRAME_R(47), r47
	st.q	SP, FRAME_R(48), r48
	st.q	SP, FRAME_R(49), r49
	st.q	SP, FRAME_R(50), r50
	st.q	SP, FRAME_R(51), r51
	st.q	SP, FRAME_R(52), r52
	st.q	SP, FRAME_R(53), r53
	st.q	SP, FRAME_R(54), r54
	st.q	SP, FRAME_R(55), r55
	st.q	SP, FRAME_R(56), r56
	st.q	SP, FRAME_R(57), r57
	st.q	SP, FRAME_R(58), r58
	st.q	SP, FRAME_R(59), r59
	st.q	SP, FRAME_R(60), r60
	st.q	SP, FRAME_R(61), r61
	st.q	SP, FRAME_R(62), r62

	/*
	 * Save the S* registers.
	 */
	getcon	SSR, r61
	st.q	SP, FRAME_S(FSSR), r61
	getcon	SPC, r62
	st.q	SP, FRAME_S(FSPC), r62
	movi	-1, r62			/* Reset syscall_nr */
	st.q	SP, FRAME_S(FSYSCALL_ID), r62

	/* Save the rest of the target registers */
	gettr	tr1, r6
	st.q	SP, FRAME_T(1), r6
	gettr	tr2, r6
	st.q	SP, FRAME_T(2), r6
	gettr	tr3, r6
	st.q	SP, FRAME_T(3), r6
	gettr	tr4, r6
	st.q	SP, FRAME_T(4), r6
	gettr	tr5, r6
	st.q	SP, FRAME_T(5), r6
	gettr	tr6, r6
	st.q	SP, FRAME_T(6), r6
	gettr	tr7, r6
	st.q	SP, FRAME_T(7), r6

	! setup FP so that unwinder can wind back through nested kernel mode
	! exceptions
	add	SP, ZERO, r14

	/* For syscall and debug race condition, get TRA now */
	getcon	TRA, r5

	/* We are in a safe position to turn SR.BL off, but set IMASK=0xf
	 * Also set FD, to catch FPU usage in the kernel.
	 *
	 * benedict.gaster@superh.com 29/07/2002
	 *
	 * On all SH5-101 revisions it is unsafe to raise the IMASK and at the
	 * same time change BL from 1->0, as any pending interrupt of a level
	 * higher than he previous value of IMASK will leak through and be
	 * taken unexpectedly.
	 *
	 * To avoid this we raise the IMASK and then issue another PUTCON to
	 * enable interrupts.
         */
	getcon	SR, r6
	movi	SR_IMASK | SR_FD, r7
	or	r6, r7, r6
	putcon	r6, SR
	movi	SR_UNBLOCK_EXC, r7
	and	r6, r7, r6
	putcon	r6, SR


	/* Now call the appropriate 3rd level handler */
	or	r3, ZERO, LINK
	movi	trap_jtable, r3
	shlri	r2, 3, r2
	ldx.l	r2, r3, r3
	shlri	r2, 2, r2
	ptabs	r3, tr0
	or	SP, ZERO, r3
	blink	tr0, ZERO

/*
 * Second level handler for VBR-based exceptions. Post-handlers.
 *
 * Post-handlers for interrupts (ret_from_irq), exceptions
 * (ret_from_exception) and common reentrance doors (restore_all
 * to get back to the original context, ret_from_syscall loop to
 * check kernel exiting).
 *
 * ret_with_reschedule and work_notifysig are an inner lables of
 * the ret_from_syscall loop.
 *
 * In common to all stack-frame sensitive handlers.
 *
 * Inputs:
 * (SP)   struct pt_regs *, original register's frame pointer (basic)
 *
 */
	.global ret_from_irq
ret_from_irq:
	ld.q	SP, FRAME_S(FSSR), r6
	shlri	r6, 30, r6
	andi	r6, 1, r6
	pta	resume_kernel, tr0
	bne	r6, ZERO, tr0		/* no further checks */
	STI()
	pta	ret_with_reschedule, tr0
	blink	tr0, ZERO		/* Do not check softirqs */

	.global ret_from_exception
ret_from_exception:
	preempt_stop()

	ld.q	SP, FRAME_S(FSSR), r6
	shlri	r6, 30, r6
	andi	r6, 1, r6
	pta	resume_kernel, tr0
	bne	r6, ZERO, tr0		/* no further checks */

	/* Check softirqs */

#ifdef CONFIG_PREEMPTION
	pta   ret_from_syscall, tr0
	blink   tr0, ZERO

resume_kernel:
	CLI()

	pta	restore_all, tr0

	getcon	KCR0, r6
	ld.l	r6, TI_PRE_COUNT, r7
	beq/u	r7, ZERO, tr0

need_resched:
	ld.l	r6, TI_FLAGS, r7
	movi	(1 << TIF_NEED_RESCHED), r8
	and	r8, r7, r8
	bne	r8, ZERO, tr0

	getcon	SR, r7
	andi	r7, 0xf0, r7
	bne	r7, ZERO, tr0

	movi	preempt_schedule_irq, r7
	ori	r7, 1, r7
	ptabs	r7, tr1
	blink	tr1, LINK

	pta	need_resched, tr1
	blink	tr1, ZERO
#endif

	.global ret_from_syscall
ret_from_syscall:

ret_with_reschedule:
	getcon	KCR0, r6		! r6 contains current_thread_info
	ld.l	r6, TI_FLAGS, r7	! r7 contains current_thread_info->flags

	movi	_TIF_NEED_RESCHED, r8
	and	r8, r7, r8
	pta	work_resched, tr0
	bne	r8, ZERO, tr0

	pta	restore_all, tr1

	movi	(_TIF_SIGPENDING|_TIF_NOTIFY_RESUME), r8
	and	r8, r7, r8
	pta	work_notifysig, tr0
	bne	r8, ZERO, tr0

	blink	tr1, ZERO

work_resched:
	pta	ret_from_syscall, tr0
	gettr	tr0, LINK
	movi	schedule, r6
	ptabs	r6, tr0
	blink	tr0, ZERO		/* Call schedule(), return on top */

work_notifysig:
	gettr	tr1, LINK

	movi	do_notify_resume, r6
	ptabs	r6, tr0
	or	SP, ZERO, r2
	or	r7, ZERO, r3
	blink	tr0, LINK	    /* Call do_notify_resume(regs, current_thread_info->flags), return here */

restore_all:
	/* Do prefetches */

	ld.q	SP, FRAME_T(0), r6
	ld.q	SP, FRAME_T(1), r7
	ld.q	SP, FRAME_T(2), r8
	ld.q	SP, FRAME_T(3), r9
	ptabs	r6, tr0
	ptabs	r7, tr1
	ptabs	r8, tr2
	ptabs	r9, tr3
	ld.q	SP, FRAME_T(4), r6
	ld.q	SP, FRAME_T(5), r7
	ld.q	SP, FRAME_T(6), r8
	ld.q	SP, FRAME_T(7), r9
	ptabs	r6, tr4
	ptabs	r7, tr5
	ptabs	r8, tr6
	ptabs	r9, tr7

	ld.q	SP, FRAME_R(0), r0
	ld.q	SP, FRAME_R(1), r1
	ld.q	SP, FRAME_R(2), r2
	ld.q	SP, FRAME_R(3), r3
	ld.q	SP, FRAME_R(4), r4
	ld.q	SP, FRAME_R(5), r5
	ld.q	SP, FRAME_R(6), r6
	ld.q	SP, FRAME_R(7), r7
	ld.q	SP, FRAME_R(8), r8
	ld.q	SP, FRAME_R(9), r9
	ld.q	SP, FRAME_R(10), r10
	ld.q	SP, FRAME_R(11), r11
	ld.q	SP, FRAME_R(12), r12
	ld.q	SP, FRAME_R(13), r13
	ld.q	SP, FRAME_R(14), r14

	ld.q	SP, FRAME_R(16), r16
	ld.q	SP, FRAME_R(17), r17
	ld.q	SP, FRAME_R(18), r18
	ld.q	SP, FRAME_R(19), r19
	ld.q	SP, FRAME_R(20), r20
	ld.q	SP, FRAME_R(21), r21
	ld.q	SP, FRAME_R(22), r22
	ld.q	SP, FRAME_R(23), r23
	ld.q	SP, FRAME_R(24), r24
	ld.q	SP, FRAME_R(25), r25
	ld.q	SP, FRAME_R(26), r26
	ld.q	SP, FRAME_R(27), r27
	ld.q	SP, FRAME_R(28), r28
	ld.q	SP, FRAME_R(29), r29
	ld.q	SP, FRAME_R(30), r30
	ld.q	SP, FRAME_R(31), r31
	ld.q	SP, FRAME_R(32), r32
	ld.q	SP, FRAME_R(33), r33
	ld.q	SP, FRAME_R(34), r34
	ld.q	SP, FRAME_R(35), r35
	ld.q	SP, FRAME_R(36), r36
	ld.q	SP, FRAME_R(37), r37
	ld.q	SP, FRAME_R(38), r38
	ld.q	SP, FRAME_R(39), r39
	ld.q	SP, FRAME_R(40), r40
	ld.q	SP, FRAME_R(41), r41
	ld.q	SP, FRAME_R(42), r42
	ld.q	SP, FRAME_R(43), r43
	ld.q	SP, FRAME_R(44), r44
	ld.q	SP, FRAME_R(45), r45
	ld.q	SP, FRAME_R(46), r46
	ld.q	SP, FRAME_R(47), r47
	ld.q	SP, FRAME_R(48), r48
	ld.q	SP, FRAME_R(49), r49
	ld.q	SP, FRAME_R(50), r50
	ld.q	SP, FRAME_R(51), r51
	ld.q	SP, FRAME_R(52), r52
	ld.q	SP, FRAME_R(53), r53
	ld.q	SP, FRAME_R(54), r54
	ld.q	SP, FRAME_R(55), r55
	ld.q	SP, FRAME_R(56), r56
	ld.q	SP, FRAME_R(57), r57
	ld.q	SP, FRAME_R(58), r58

	getcon	SR, r59
	movi	SR_BLOCK_EXC, r60
	or	r59, r60, r59
	putcon	r59, SR			/* SR.BL = 1, keep nesting out */
	ld.q	SP, FRAME_S(FSSR), r61
	ld.q	SP, FRAME_S(FSPC), r62
	movi	SR_ASID_MASK, r60
	and	r59, r60, r59
	andc	r61, r60, r61		/* Clear out older ASID */
	or	r59, r61, r61		/* Retain current ASID */
	putcon	r61, SSR
	putcon	r62, SPC

	/* Ignore FSYSCALL_ID */

	ld.q	SP, FRAME_R(59), r59
	ld.q	SP, FRAME_R(60), r60
	ld.q	SP, FRAME_R(61), r61
	ld.q	SP, FRAME_R(62), r62

	/* Last touch */
	ld.q	SP, FRAME_R(15), SP
	rte
	nop

/*
 * Third level handlers for VBR-based exceptions. Adapting args to
 * and/or deflecting to fourth level handlers.
 *
 * Fourth level handlers interface.
 * Most are C-coded handlers directly pointed by the trap_jtable.
 * (Third = Fourth level)
 * Inputs:
 * (r2)   fault/interrupt code, entry number (e.g. NMI = 14,
 *	  IRL0-3 (0000) = 16, RTLBMISS = 2, SYSCALL = 11, etc ...)
 * (r3)   struct pt_regs *, original register's frame pointer
 * (r4)   Event (0 = interrupt, 1 = TLB miss fault, 2 = Not TLB miss fault)
 * (r5)   TRA control register (for syscall/debug benefit only)
 * (LINK) return address
 * (SP)   = r3
 *
 * Kernel TLB fault handlers will get a slightly different interface.
 * (r2)   struct pt_regs *, original register's frame pointer
 * (r3)   page fault error code (see asm/thread_info.h)
 * (r4)   Effective Address of fault
 * (LINK) return address
 * (SP)   = r2
 *
 * fpu_error_or_IRQ? is a helper to deflect to the right cause.
 *
 */
#ifdef CONFIG_MMU
tlb_miss_load:
	or	SP, ZERO, r2
	or	ZERO, ZERO, r3		/* Read */
	getcon	TEA, r4
	pta	call_do_page_fault, tr0
	beq	ZERO, ZERO, tr0

tlb_miss_store:
	or	SP, ZERO, r2
	movi	FAULT_CODE_WRITE, r3		/* Write */
	getcon	TEA, r4
	pta	call_do_page_fault, tr0
	beq	ZERO, ZERO, tr0

itlb_miss_or_IRQ:
	pta	its_IRQ, tr0
	beqi/u	r4, EVENT_INTERRUPT, tr0

	/* ITLB miss */
	or	SP, ZERO, r2
	movi	FAULT_CODE_ITLB, r3
	getcon	TEA, r4
	/* Fall through */

call_do_page_fault:
	movi	do_page_fault, r6
        ptabs	r6, tr0
        blink	tr0, ZERO
#endif /* CONFIG_MMU */

fpu_error_or_IRQA:
	pta	its_IRQ, tr0
	beqi/l	r4, EVENT_INTERRUPT, tr0
#ifdef CONFIG_SH_FPU
	movi	fpu_state_restore_trap_handler, r6
#else
	movi	do_exception_error, r6
#endif
	ptabs	r6, tr0
	blink	tr0, ZERO

fpu_error_or_IRQB:
	pta	its_IRQ, tr0
	beqi/l	r4, EVENT_INTERRUPT, tr0
#ifdef CONFIG_SH_FPU
	movi	fpu_state_restore_trap_handler, r6
#else
	movi	do_exception_error, r6
#endif
	ptabs	r6, tr0
	blink	tr0, ZERO

its_IRQ:
	movi	do_IRQ, r6
	ptabs	r6, tr0
	blink	tr0, ZERO

/*
 * system_call/unknown_trap third level handler:
 *
 * Inputs:
 * (r2)   fault/interrupt code, entry number (TRAP = 11)
 * (r3)   struct pt_regs *, original register's frame pointer
 * (r4)   Not used. Event (0=interrupt, 1=TLB miss fault, 2=Not TLB miss fault)
 * (r5)   TRA Control Reg (0x00xyzzzz: x=1 SYSCALL, y = #args, z=nr)
 * (SP)   = r3
 * (LINK) return address: ret_from_exception
 * (*r3)  Syscall parms: SC#, arg0, arg1, ..., arg5 in order (Saved r2/r7)
 *
 * Outputs:
 * (*r3)  Syscall reply (Saved r2)
 * (LINK) In case of syscall only it can be scrapped.
 *        Common second level post handler will be ret_from_syscall.
 *        Common (non-trace) exit point to that is syscall_ret (saving
 *        result to r2). Common bad exit point is syscall_bad (returning
 *        ENOSYS then saved to r2).
 *
 */

unknown_trap:
	/* Unknown Trap or User Trace */
	movi	do_unknown_trapa, r6
	ptabs	r6, tr0
        ld.q    r3, FRAME_R(9), r2	/* r2 = #arg << 16 | syscall # */
        andi    r2, 0x1ff, r2		/* r2 = syscall # */
	blink	tr0, LINK

	pta	syscall_ret, tr0
	blink	tr0, ZERO

        /* New syscall implementation*/
system_call:
	pta	unknown_trap, tr0
        or      r5, ZERO, r4            /* TRA (=r5) -> r4 */
        shlri   r4, 20, r4
	bnei	r4, 1, tr0		/* unknown_trap if not 0x1yzzzz */

        /* It's a system call */
	st.q    r3, FRAME_S(FSYSCALL_ID), r5 	/* ID (0x1yzzzz) -> stack */
	andi    r5, 0x1ff, r5			/* syscall # -> r5	  */

	STI()

	pta	syscall_allowed, tr0
	movi	NR_syscalls - 1, r4	/* Last valid */
	bgeu/l	r4, r5, tr0

syscall_bad:
	/* Return ENOSYS ! */
	movi	-(ENOSYS), r2		/* Fall-through */

	.global syscall_ret
syscall_ret:
	st.q	SP, FRAME_R(9), r2	/* Expecting SP back to BASIC frame */
	ld.q	SP, FRAME_S(FSPC), r2
	addi	r2, 4, r2		/* Move PC, being pre-execution event */
	st.q	SP, FRAME_S(FSPC), r2
	pta	ret_from_syscall, tr0
	blink	tr0, ZERO


/*  A different return path for ret_from_fork, because we now need
 *  to call schedule_tail with the later kernels. Because prev is
 *  loaded into r2 by switch_to() means we can just call it straight  away
 */

.global	ret_from_fork
ret_from_fork:

	movi	schedule_tail,r5
	ori	r5, 1, r5
	ptabs	r5, tr0
	blink	tr0, LINK

	ld.q	SP, FRAME_S(FSPC), r2
	addi	r2, 4, r2		/* Move PC, being pre-execution event */
	st.q	SP, FRAME_S(FSPC), r2
	pta	ret_from_syscall, tr0
	blink	tr0, ZERO

.global	ret_from_kernel_thread
ret_from_kernel_thread:

	movi	schedule_tail,r5
	ori	r5, 1, r5
	ptabs	r5, tr0
	blink	tr0, LINK

	ld.q	SP, FRAME_R(2), r2
	ld.q	SP, FRAME_R(3), r3
	ptabs	r3, tr0
	blink	tr0, LINK

	ld.q	SP, FRAME_S(FSPC), r2
	addi	r2, 4, r2		/* Move PC, being pre-execution event */
	st.q	SP, FRAME_S(FSPC), r2
	pta	ret_from_syscall, tr0
	blink	tr0, ZERO

syscall_allowed:
	/* Use LINK to deflect the exit point, default is syscall_ret */
	pta	syscall_ret, tr0
	gettr	tr0, LINK
	pta	syscall_notrace, tr0

	getcon	KCR0, r2
	ld.l	r2, TI_FLAGS, r4
	movi	_TIF_WORK_SYSCALL_MASK, r6
	and	r6, r4, r6
	beq/l	r6, ZERO, tr0

	/* Trace it by calling syscall_trace before and after */
	movi	do_syscall_trace_enter, r4
	or	SP, ZERO, r2
	ptabs	r4, tr0
	blink	tr0, LINK

	/* Save the retval */
	st.q	SP, FRAME_R(2), r2

	/* Reload syscall number as r5 is trashed by do_syscall_trace_enter */
	ld.q	SP, FRAME_S(FSYSCALL_ID), r5
	andi	r5, 0x1ff, r5

	pta	syscall_ret_trace, tr0
	gettr	tr0, LINK

syscall_notrace:
	/* Now point to the appropriate 4th level syscall handler */
	movi	sys_call_table, r4
	shlli	r5, 2, r5
	ldx.l	r4, r5, r5
	ptabs	r5, tr0

	/* Prepare original args */
	ld.q	SP, FRAME_R(2), r2
	ld.q	SP, FRAME_R(3), r3
	ld.q	SP, FRAME_R(4), r4
	ld.q	SP, FRAME_R(5), r5
	ld.q	SP, FRAME_R(6), r6
	ld.q	SP, FRAME_R(7), r7

	/* And now the trick for those syscalls requiring regs * ! */
	or	SP, ZERO, r8

	/* Call it */
	blink	tr0, ZERO	/* LINK is already properly set */

syscall_ret_trace:
	/* We get back here only if under trace */
	st.q	SP, FRAME_R(9), r2	/* Save return value */

	movi	do_syscall_trace_leave, LINK
	or	SP, ZERO, r2
	ptabs	LINK, tr0
	blink	tr0, LINK

	/* This needs to be done after any syscall tracing */
	ld.q	SP, FRAME_S(FSPC), r2
	addi	r2, 4, r2	/* Move PC, being pre-execution event */
	st.q	SP, FRAME_S(FSPC), r2

	pta	ret_from_syscall, tr0
	blink	tr0, ZERO		/* Resume normal return sequence */

/*
 * --- Switch to running under a particular ASID and return the previous ASID value
 * --- The caller is assumed to have done a cli before calling this.
 *
 * Input r2 : new ASID
 * Output r2 : old ASID
 */

	.global switch_and_save_asid
switch_and_save_asid:
	getcon	sr, r0
	movi	255, r4
	shlli 	r4, 16, r4	/* r4 = mask to select ASID */
	and	r0, r4, r3	/* r3 = shifted old ASID */
	andi	r2, 255, r2	/* mask down new ASID */
	shlli	r2, 16, r2	/* align new ASID against SR.ASID */
	andc	r0, r4, r0	/* efface old ASID from SR */
	or	r0, r2, r0	/* insert the new ASID */
	putcon	r0, ssr
	movi	1f, r0
	putcon	r0, spc
	rte
	nop
1:
	ptabs	LINK, tr0
	shlri	r3, 16, r2	/* r2 = old ASID */
	blink tr0, r63

	.global	route_to_panic_handler
route_to_panic_handler:
	/* Switch to real mode, goto panic_handler, don't return.  Useful for
	   last-chance debugging, e.g. if no output wants to go to the console.
	   */

	movi	panic_handler - CONFIG_PAGE_OFFSET, r1
	ptabs	r1, tr0
	pta	1f, tr1
	gettr	tr1, r0
	putcon	r0, spc
	getcon	sr, r0
	movi	1, r1
	shlli	r1, 31, r1
	andc	r0, r1, r0
	putcon	r0, ssr
	rte
	nop
1:	/* Now in real mode */
	blink tr0, r63
	nop

	.global peek_real_address_q
peek_real_address_q:
	/* Two args:
	   r2 : real mode address to peek
	   r2(out) : result quadword

	   This is provided as a cheapskate way of manipulating device
	   registers for debugging (to avoid the need to ioremap the debug
	   module, and to avoid the need to ioremap the watchpoint
	   controller in a way that identity maps sufficient bits to avoid the
	   SH5-101 cut2 silicon defect).

	   This code is not performance critical
	*/

	add.l	r2, r63, r2	/* sign extend address */
	getcon	sr, r0		/* r0 = saved original SR */
	movi	1, r1
	shlli	r1, 28, r1
	or	r0, r1, r1	/* r0 with block bit set */
	putcon	r1, sr		/* now in critical section */
	movi	1, r36
	shlli	r36, 31, r36
	andc	r1, r36, r1	/* turn sr.mmu off in real mode section */

	putcon	r1, ssr
	movi	.peek0 - CONFIG_PAGE_OFFSET, r36 /* real mode target address */
	movi	1f, r37		/* virtual mode return addr */
	putcon	r36, spc

	synco
	rte
	nop

.peek0:	/* come here in real mode, don't touch caches!!
           still in critical section (sr.bl==1) */
	putcon	r0, ssr
	putcon	r37, spc
	/* Here's the actual peek.  If the address is bad, all bets are now off
	 * what will happen (handlers invoked in real-mode = bad news) */
	ld.q	r2, 0, r2
	synco
	rte	/* Back to virtual mode */
	nop

1:
	ptabs	LINK, tr0
	blink	tr0, r63

	.global poke_real_address_q
poke_real_address_q:
	/* Two args:
	   r2 : real mode address to poke
	   r3 : quadword value to write.

	   This is provided as a cheapskate way of manipulating device
	   registers for debugging (to avoid the need to ioremap the debug
	   module, and to avoid the need to ioremap the watchpoint
	   controller in a way that identity maps sufficient bits to avoid the
	   SH5-101 cut2 silicon defect).

	   This code is not performance critical
	*/

	add.l	r2, r63, r2	/* sign extend address */
	getcon	sr, r0		/* r0 = saved original SR */
	movi	1, r1
	shlli	r1, 28, r1
	or	r0, r1, r1	/* r0 with block bit set */
	putcon	r1, sr		/* now in critical section */
	movi	1, r36
	shlli	r36, 31, r36
	andc	r1, r36, r1	/* turn sr.mmu off in real mode section */

	putcon	r1, ssr
	movi	.poke0-CONFIG_PAGE_OFFSET, r36 /* real mode target address */
	movi	1f, r37		/* virtual mode return addr */
	putcon	r36, spc

	synco
	rte
	nop

.poke0:	/* come here in real mode, don't touch caches!!
           still in critical section (sr.bl==1) */
	putcon	r0, ssr
	putcon	r37, spc
	/* Here's the actual poke.  If the address is bad, all bets are now off
	 * what will happen (handlers invoked in real-mode = bad news) */
	st.q	r2, 0, r3
	synco
	rte	/* Back to virtual mode */
	nop

1:
	ptabs	LINK, tr0
	blink	tr0, r63

#ifdef CONFIG_MMU
/*
 * --- User Access Handling Section
 */

/*
 * User Access support. It all moved to non inlined Assembler
 * functions in here.
 *
 * __kernel_size_t __copy_user(void *__to, const void *__from,
 *			       __kernel_size_t __n)
 *
 * Inputs:
 * (r2)  target address
 * (r3)  source address
 * (r4)  size in bytes
 *
 * Ouputs:
 * (*r2) target data
 * (r2)  non-copied bytes
 *
 * If a fault occurs on the user pointer, bail out early and return the
 * number of bytes not copied in r2.
 * Strategy : for large blocks, call a real memcpy function which can
 * move >1 byte at a time using unaligned ld/st instructions, and can
 * manipulate the cache using prefetch + alloco to improve the speed
 * further.  If a fault occurs in that function, just revert to the
 * byte-by-byte approach used for small blocks; this is rare so the
 * performance hit for that case does not matter.
 *
 * For small blocks it's not worth the overhead of setting up and calling
 * the memcpy routine; do the copy a byte at a time.
 *
 */
	.global	__copy_user
__copy_user:
	pta	__copy_user_byte_by_byte, tr1
	movi	16, r0 ! this value is a best guess, should tune it by benchmarking
	bge/u	r0, r4, tr1
	pta copy_user_memcpy, tr0
	addi	SP, -32, SP
	/* Save arguments in case we have to fix-up unhandled page fault */
	st.q	SP, 0, r2
	st.q	SP, 8, r3
	st.q	SP, 16, r4
	st.q	SP, 24, r35 ! r35 is callee-save
	/* Save LINK in a register to reduce RTS time later (otherwise
	   ld SP,*,LINK;ptabs LINK;trn;blink trn,r63 becomes a critical path) */
	ori	LINK, 0, r35
	blink	tr0, LINK

	/* Copy completed normally if we get back here */
	ptabs	r35, tr0
	ld.q	SP, 24, r35
	/* don't restore r2-r4, pointless */
	/* set result=r2 to zero as the copy must have succeeded. */
	or	r63, r63, r2
	addi	SP, 32, SP
	blink	tr0, r63 ! RTS

	.global __copy_user_fixup
__copy_user_fixup:
	/* Restore stack frame */
	ori	r35, 0, LINK
	ld.q	SP, 24, r35
	ld.q	SP, 16, r4
	ld.q	SP,  8, r3
	ld.q	SP,  0, r2
	addi	SP, 32, SP
	/* Fall through to original code, in the 'same' state we entered with */

/* The slow byte-by-byte method is used if the fast copy traps due to a bad
   user address.  In that rare case, the speed drop can be tolerated. */
__copy_user_byte_by_byte:
	pta	___copy_user_exit, tr1
	pta	___copy_user1, tr0
	beq/u	r4, r63, tr1	/* early exit for zero length copy */
	sub	r2, r3, r0
	addi	r0, -1, r0

___copy_user1:
	ld.b	r3, 0, r5		/* Fault address 1 */

	/* Could rewrite this to use just 1 add, but the second comes 'free'
	   due to load latency */
	addi	r3, 1, r3
	addi	r4, -1, r4		/* No real fixup required */
___copy_user2:
	stx.b	r3, r0, r5		/* Fault address 2 */
	bne     r4, ZERO, tr0

___copy_user_exit:
	or	r4, ZERO, r2
	ptabs	LINK, tr0
	blink	tr0, ZERO

/*
 * __kernel_size_t __clear_user(void *addr, __kernel_size_t size)
 *
 * Inputs:
 * (r2)  target address
 * (r3)  size in bytes
 *
 * Ouputs:
 * (*r2) zero-ed target data
 * (r2)  non-zero-ed bytes
 */
	.global	__clear_user
__clear_user:
	pta	___clear_user_exit, tr1
	pta	___clear_user1, tr0
	beq/u	r3, r63, tr1

___clear_user1:
	st.b	r2, 0, ZERO		/* Fault address */
	addi	r2, 1, r2
	addi	r3, -1, r3		/* No real fixup required */
	bne     r3, ZERO, tr0

___clear_user_exit:
	or	r3, ZERO, r2
	ptabs	LINK, tr0
	blink	tr0, ZERO

#endif /* CONFIG_MMU */

/*
 * extern long __get_user_asm_?(void *val, long addr)
 *
 * Inputs:
 * (r2)  dest address
 * (r3)  source address (in User Space)
 *
 * Ouputs:
 * (r2)  -EFAULT (faulting)
 *       0 	 (not faulting)
 */
	.global	__get_user_asm_b
__get_user_asm_b:
	or	r2, ZERO, r4
	movi	-(EFAULT), r2		/* r2 = reply, no real fixup */

___get_user_asm_b1:
	ld.b	r3, 0, r5		/* r5 = data */
	st.b	r4, 0, r5
	or	ZERO, ZERO, r2

___get_user_asm_b_exit:
	ptabs	LINK, tr0
	blink	tr0, ZERO


	.global	__get_user_asm_w
__get_user_asm_w:
	or	r2, ZERO, r4
	movi	-(EFAULT), r2		/* r2 = reply, no real fixup */

___get_user_asm_w1:
	ld.w	r3, 0, r5		/* r5 = data */
	st.w	r4, 0, r5
	or	ZERO, ZERO, r2

___get_user_asm_w_exit:
	ptabs	LINK, tr0
	blink	tr0, ZERO


	.global	__get_user_asm_l
__get_user_asm_l:
	or	r2, ZERO, r4
	movi	-(EFAULT), r2		/* r2 = reply, no real fixup */

___get_user_asm_l1:
	ld.l	r3, 0, r5		/* r5 = data */
	st.l	r4, 0, r5
	or	ZERO, ZERO, r2

___get_user_asm_l_exit:
	ptabs	LINK, tr0
	blink	tr0, ZERO


	.global	__get_user_asm_q
__get_user_asm_q:
	or	r2, ZERO, r4
	movi	-(EFAULT), r2		/* r2 = reply, no real fixup */

___get_user_asm_q1:
	ld.q	r3, 0, r5		/* r5 = data */
	st.q	r4, 0, r5
	or	ZERO, ZERO, r2

___get_user_asm_q_exit:
	ptabs	LINK, tr0
	blink	tr0, ZERO

/*
 * extern long __put_user_asm_?(void *pval, long addr)
 *
 * Inputs:
 * (r2)  kernel pointer to value
 * (r3)  dest address (in User Space)
 *
 * Ouputs:
 * (r2)  -EFAULT (faulting)
 *       0 	 (not faulting)
 */
	.global	__put_user_asm_b
__put_user_asm_b:
	ld.b	r2, 0, r4		/* r4 = data */
	movi	-(EFAULT), r2		/* r2 = reply, no real fixup */

___put_user_asm_b1:
	st.b	r3, 0, r4
	or	ZERO, ZERO, r2

___put_user_asm_b_exit:
	ptabs	LINK, tr0
	blink	tr0, ZERO


	.global	__put_user_asm_w
__put_user_asm_w:
	ld.w	r2, 0, r4		/* r4 = data */
	movi	-(EFAULT), r2		/* r2 = reply, no real fixup */

___put_user_asm_w1:
	st.w	r3, 0, r4
	or	ZERO, ZERO, r2

___put_user_asm_w_exit:
	ptabs	LINK, tr0
	blink	tr0, ZERO


	.global	__put_user_asm_l
__put_user_asm_l:
	ld.l	r2, 0, r4		/* r4 = data */
	movi	-(EFAULT), r2		/* r2 = reply, no real fixup */

___put_user_asm_l1:
	st.l	r3, 0, r4
	or	ZERO, ZERO, r2

___put_user_asm_l_exit:
	ptabs	LINK, tr0
	blink	tr0, ZERO


	.global	__put_user_asm_q
__put_user_asm_q:
	ld.q	r2, 0, r4		/* r4 = data */
	movi	-(EFAULT), r2		/* r2 = reply, no real fixup */

___put_user_asm_q1:
	st.q	r3, 0, r4
	or	ZERO, ZERO, r2

___put_user_asm_q_exit:
	ptabs	LINK, tr0
	blink	tr0, ZERO

panic_stash_regs:
	/* The idea is : when we get an unhandled panic, we dump the registers
	   to a known memory location, the just sit in a tight loop.
	   This allows the human to look at the memory region through the GDB
	   session (assuming the debug module's SHwy initiator isn't locked up
	   or anything), to hopefully analyze the cause of the panic. */

	/* On entry, former r15 (SP) is in DCR
	   former r0  is at resvec_saved_area + 0
	   former r1  is at resvec_saved_area + 8
	   former tr0 is at resvec_saved_area + 32
	   DCR is the only register whose value is lost altogether.
	*/

	movi	0xffffffff80000000, r0 ! phy of dump area
	ld.q	SP, 0x000, r1	! former r0
	st.q	r0,  0x000, r1
	ld.q	SP, 0x008, r1	! former r1
	st.q	r0,  0x008, r1
	st.q	r0,  0x010, r2
	st.q	r0,  0x018, r3
	st.q	r0,  0x020, r4
	st.q	r0,  0x028, r5
	st.q	r0,  0x030, r6
	st.q	r0,  0x038, r7
	st.q	r0,  0x040, r8
	st.q	r0,  0x048, r9
	st.q	r0,  0x050, r10
	st.q	r0,  0x058, r11
	st.q	r0,  0x060, r12
	st.q	r0,  0x068, r13
	st.q	r0,  0x070, r14
	getcon	dcr, r14
	st.q	r0,  0x078, r14
	st.q	r0,  0x080, r16
	st.q	r0,  0x088, r17
	st.q	r0,  0x090, r18
	st.q	r0,  0x098, r19
	st.q	r0,  0x0a0, r20
	st.q	r0,  0x0a8, r21
	st.q	r0,  0x0b0, r22
	st.q	r0,  0x0b8, r23
	st.q	r0,  0x0c0, r24
	st.q	r0,  0x0c8, r25
	st.q	r0,  0x0d0, r26
	st.q	r0,  0x0d8, r27
	st.q	r0,  0x0e0, r28
	st.q	r0,  0x0e8, r29
	st.q	r0,  0x0f0, r30
	st.q	r0,  0x0f8, r31
	st.q	r0,  0x100, r32
	st.q	r0,  0x108, r33
	st.q	r0,  0x110, r34
	st.q	r0,  0x118, r35
	st.q	r0,  0x120, r36
	st.q	r0,  0x128, r37
	st.q	r0,  0x130, r38
	st.q	r0,  0x138, r39
	st.q	r0,  0x140, r40
	st.q	r0,  0x148, r41
	st.q	r0,  0x150, r42
	st.q	r0,  0x158, r43
	st.q	r0,  0x160, r44
	st.q	r0,  0x168, r45
	st.q	r0,  0x170, r46
	st.q	r0,  0x178, r47
	st.q	r0,  0x180, r48
	st.q	r0,  0x188, r49
	st.q	r0,  0x190, r50
	st.q	r0,  0x198, r51
	st.q	r0,  0x1a0, r52
	st.q	r0,  0x1a8, r53
	st.q	r0,  0x1b0, r54
	st.q	r0,  0x1b8, r55
	st.q	r0,  0x1c0, r56
	st.q	r0,  0x1c8, r57
	st.q	r0,  0x1d0, r58
	st.q	r0,  0x1d8, r59
	st.q	r0,  0x1e0, r60
	st.q	r0,  0x1e8, r61
	st.q	r0,  0x1f0, r62
	st.q	r0,  0x1f8, r63	! bogus, but for consistency's sake...

	ld.q	SP, 0x020, r1  ! former tr0
	st.q	r0,  0x200, r1
	gettr	tr1, r1
	st.q	r0,  0x208, r1
	gettr	tr2, r1
	st.q	r0,  0x210, r1
	gettr	tr3, r1
	st.q	r0,  0x218, r1
	gettr	tr4, r1
	st.q	r0,  0x220, r1
	gettr	tr5, r1
	st.q	r0,  0x228, r1
	gettr	tr6, r1
	st.q	r0,  0x230, r1
	gettr	tr7, r1
	st.q	r0,  0x238, r1

	getcon	sr,  r1
	getcon	ssr,  r2
	getcon	pssr,  r3
	getcon	spc,  r4
	getcon	pspc,  r5
	getcon	intevt,  r6
	getcon	expevt,  r7
	getcon	pexpevt,  r8
	getcon	tra,  r9
	getcon	tea,  r10
	getcon	kcr0, r11
	getcon	kcr1, r12
	getcon	vbr,  r13
	getcon	resvec,  r14

	st.q	r0,  0x240, r1
	st.q	r0,  0x248, r2
	st.q	r0,  0x250, r3
	st.q	r0,  0x258, r4
	st.q	r0,  0x260, r5
	st.q	r0,  0x268, r6
	st.q	r0,  0x270, r7
	st.q	r0,  0x278, r8
	st.q	r0,  0x280, r9
	st.q	r0,  0x288, r10
	st.q	r0,  0x290, r11
	st.q	r0,  0x298, r12
	st.q	r0,  0x2a0, r13
	st.q	r0,  0x2a8, r14

	getcon	SPC,r2
	getcon	SSR,r3
	getcon	EXPEVT,r4
	/* Prepare to jump to C - physical address */
	movi	panic_handler-CONFIG_PAGE_OFFSET, r1
	ori	r1, 1, r1
	ptabs   r1, tr0
	getcon	DCR, SP
	blink	tr0, ZERO
	nop
	nop
	nop
	nop




/*
 * --- Signal Handling Section
 */

/*
 * extern long long _sa_default_rt_restorer
 * extern long long _sa_default_restorer
 *
 *		 or, better,
 *
 * extern void _sa_default_rt_restorer(void)
 * extern void _sa_default_restorer(void)
 *
 * Code prototypes to do a sys_rt_sigreturn() or sys_sysreturn()
 * from user space. Copied into user space by signal management.
 * Both must be quad aligned and 2 quad long (4 instructions).
 *
 */
	.balign 8
	.global sa_default_rt_restorer
sa_default_rt_restorer:
	movi	0x10, r9
	shori	__NR_rt_sigreturn, r9
	trapa	r9
	nop

	.balign 8
	.global sa_default_restorer
sa_default_restorer:
	movi	0x10, r9
	shori	__NR_sigreturn, r9
	trapa	r9
	nop

/*
 * --- __ex_table Section
 */

/*
 * User Access Exception Table.
 */
	.section	__ex_table,  "a"

	.global asm_uaccess_start	/* Just a marker */
asm_uaccess_start:

#ifdef CONFIG_MMU
	.long	___copy_user1, ___copy_user_exit
	.long	___copy_user2, ___copy_user_exit
	.long	___clear_user1, ___clear_user_exit
#endif
	.long	___get_user_asm_b1, ___get_user_asm_b_exit
	.long	___get_user_asm_w1, ___get_user_asm_w_exit
	.long	___get_user_asm_l1, ___get_user_asm_l_exit
	.long	___get_user_asm_q1, ___get_user_asm_q_exit
	.long	___put_user_asm_b1, ___put_user_asm_b_exit
	.long	___put_user_asm_w1, ___put_user_asm_w_exit
	.long	___put_user_asm_l1, ___put_user_asm_l_exit
	.long	___put_user_asm_q1, ___put_user_asm_q_exit

	.global asm_uaccess_end		/* Just a marker */
asm_uaccess_end:




/*
 * --- .init.text Section
 */

	__INIT

/*
 * void trap_init (void)
 *
 */
	.global	trap_init
trap_init:
	addi	SP, -24, SP			/* Room to save r28/r29/r30 */
	st.q	SP, 0, r28
	st.q	SP, 8, r29
	st.q	SP, 16, r30

	/* Set VBR and RESVEC */
	movi	LVBR_block, r19
	andi	r19, -4, r19			/* reset MMUOFF + reserved */
	/* For RESVEC exceptions we force the MMU off, which means we need the
	   physical address. */
	movi	LRESVEC_block-CONFIG_PAGE_OFFSET, r20
	andi	r20, -4, r20			/* reset reserved */
	ori	r20, 1, r20			/* set MMUOFF */
	putcon	r19, VBR
	putcon	r20, RESVEC

	/* Sanity check */
	movi	LVBR_block_end, r21
	andi	r21, -4, r21
	movi	BLOCK_SIZE, r29			/* r29 = expected size */
	or	r19, ZERO, r30
	add	r19, r29, r19

	/*
	 * Ugly, but better loop forever now than crash afterwards.
	 * We should print a message, but if we touch LVBR or
	 * LRESVEC blocks we should not be surprised if we get stuck
	 * in trap_init().
	 */
	pta	trap_init_loop, tr1
	gettr	tr1, r28			/* r28 = trap_init_loop */
	sub	r21, r30, r30			/* r30 = actual size */

	/*
	 * VBR/RESVEC handlers overlap by being bigger than
	 * allowed. Very bad. Just loop forever.
	 * (r28) panic/loop address
	 * (r29) expected size
	 * (r30) actual size
	 */
trap_init_loop:
	bne	r19, r21, tr1

	/* Now that exception vectors are set up reset SR.BL */
	getcon 	SR, r22
	movi	SR_UNBLOCK_EXC, r23
	and	r22, r23, r22
	putcon	r22, SR

	addi	SP, 24, SP
	ptabs	LINK, tr0
	blink	tr0, ZERO

