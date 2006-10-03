/*
 * arch/v850/kernel/entry.S -- Low-level system-call handling, trap handlers,
 *	and context-switching
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#include <linux/sys.h>

#include <asm/entry.h>
#include <asm/current.h>
#include <asm/thread_info.h>
#include <asm/clinkage.h>
#include <asm/processor.h>
#include <asm/irq.h>
#include <asm/errno.h>

#include <asm/asm-offsets.h>


/* Make a slightly more convenient alias for C_SYMBOL_NAME.  */
#define CSYM	C_SYMBOL_NAME


/* The offset of the struct pt_regs in a state-save-frame on the stack.  */
#define PTO	STATE_SAVE_PT_OFFSET


/* Save argument registers to the state-save-frame pointed to by EP.  */
#define SAVE_ARG_REGS							      \
	sst.w	r6, PTO+PT_GPR(6)[ep];					      \
	sst.w	r7, PTO+PT_GPR(7)[ep];					      \
	sst.w	r8, PTO+PT_GPR(8)[ep];					      \
	sst.w	r9, PTO+PT_GPR(9)[ep]
/* Restore argument registers from the state-save-frame pointed to by EP.  */
#define RESTORE_ARG_REGS						      \
	sld.w	PTO+PT_GPR(6)[ep], r6;					      \
	sld.w	PTO+PT_GPR(7)[ep], r7;					      \
	sld.w	PTO+PT_GPR(8)[ep], r8;					      \
	sld.w	PTO+PT_GPR(9)[ep], r9

/* Save value return registers to the state-save-frame pointed to by EP.  */
#define SAVE_RVAL_REGS							      \
	sst.w	r10, PTO+PT_GPR(10)[ep];				      \
	sst.w	r11, PTO+PT_GPR(11)[ep]
/* Restore value return registers from the state-save-frame pointed to by EP.  */
#define RESTORE_RVAL_REGS						      \
	sld.w	PTO+PT_GPR(10)[ep], r10;				      \
	sld.w	PTO+PT_GPR(11)[ep], r11


#define SAVE_CALL_CLOBBERED_REGS_BEFORE_ARGS				      \
	sst.w	r1, PTO+PT_GPR(1)[ep];					      \
	sst.w	r5, PTO+PT_GPR(5)[ep]
#define SAVE_CALL_CLOBBERED_REGS_AFTER_RVAL				      \
	sst.w	r12, PTO+PT_GPR(12)[ep];				      \
	sst.w	r13, PTO+PT_GPR(13)[ep];				      \
	sst.w	r14, PTO+PT_GPR(14)[ep];				      \
	sst.w	r15, PTO+PT_GPR(15)[ep];				      \
	sst.w	r16, PTO+PT_GPR(16)[ep];				      \
	sst.w	r17, PTO+PT_GPR(17)[ep];				      \
	sst.w	r18, PTO+PT_GPR(18)[ep];				      \
	sst.w	r19, PTO+PT_GPR(19)[ep]
#define RESTORE_CALL_CLOBBERED_REGS_BEFORE_ARGS				      \
	sld.w	PTO+PT_GPR(1)[ep], r1;					      \
	sld.w	PTO+PT_GPR(5)[ep], r5
#define RESTORE_CALL_CLOBBERED_REGS_AFTER_RVAL				      \
	sld.w	PTO+PT_GPR(12)[ep], r12;				      \
	sld.w	PTO+PT_GPR(13)[ep], r13;				      \
	sld.w	PTO+PT_GPR(14)[ep], r14;				      \
	sld.w	PTO+PT_GPR(15)[ep], r15;				      \
	sld.w	PTO+PT_GPR(16)[ep], r16;				      \
	sld.w	PTO+PT_GPR(17)[ep], r17;				      \
	sld.w	PTO+PT_GPR(18)[ep], r18;				      \
	sld.w	PTO+PT_GPR(19)[ep], r19

/* Save `call clobbered' registers to the state-save-frame pointed to by EP.  */
#define SAVE_CALL_CLOBBERED_REGS					      \
	SAVE_CALL_CLOBBERED_REGS_BEFORE_ARGS;				      \
	SAVE_ARG_REGS;							      \
	SAVE_RVAL_REGS;							      \
	SAVE_CALL_CLOBBERED_REGS_AFTER_RVAL
/* Restore `call clobbered' registers from the state-save-frame pointed to
   by EP.  */
#define RESTORE_CALL_CLOBBERED_REGS					      \
	RESTORE_CALL_CLOBBERED_REGS_BEFORE_ARGS;			      \
	RESTORE_ARG_REGS;						      \
	RESTORE_RVAL_REGS;						      \
	RESTORE_CALL_CLOBBERED_REGS_AFTER_RVAL

/* Save `call clobbered' registers except for the return-value registers
   to the state-save-frame pointed to by EP.  */
#define SAVE_CALL_CLOBBERED_REGS_NO_RVAL				      \
	SAVE_CALL_CLOBBERED_REGS_BEFORE_ARGS;				      \
	SAVE_ARG_REGS;							      \
	SAVE_CALL_CLOBBERED_REGS_AFTER_RVAL
/* Restore `call clobbered' registers except for the return-value registers
   from the state-save-frame pointed to by EP.  */
#define RESTORE_CALL_CLOBBERED_REGS_NO_RVAL				      \
	RESTORE_CALL_CLOBBERED_REGS_BEFORE_ARGS;			      \
	RESTORE_ARG_REGS;						      \
	RESTORE_CALL_CLOBBERED_REGS_AFTER_RVAL

/* Save `call saved' registers to the state-save-frame pointed to by EP.  */
#define SAVE_CALL_SAVED_REGS						      \
	sst.w	r2, PTO+PT_GPR(2)[ep];					      \
	sst.w	r20, PTO+PT_GPR(20)[ep];				      \
	sst.w	r21, PTO+PT_GPR(21)[ep];				      \
	sst.w	r22, PTO+PT_GPR(22)[ep];				      \
	sst.w	r23, PTO+PT_GPR(23)[ep];				      \
	sst.w	r24, PTO+PT_GPR(24)[ep];				      \
	sst.w	r25, PTO+PT_GPR(25)[ep];				      \
	sst.w	r26, PTO+PT_GPR(26)[ep];				      \
	sst.w	r27, PTO+PT_GPR(27)[ep];				      \
	sst.w	r28, PTO+PT_GPR(28)[ep];				      \
	sst.w	r29, PTO+PT_GPR(29)[ep]
/* Restore `call saved' registers from the state-save-frame pointed to by EP.  */
#define RESTORE_CALL_SAVED_REGS						      \
	sld.w	PTO+PT_GPR(2)[ep], r2;					      \
	sld.w	PTO+PT_GPR(20)[ep], r20;				      \
	sld.w	PTO+PT_GPR(21)[ep], r21;				      \
	sld.w	PTO+PT_GPR(22)[ep], r22;				      \
	sld.w	PTO+PT_GPR(23)[ep], r23;				      \
	sld.w	PTO+PT_GPR(24)[ep], r24;				      \
	sld.w	PTO+PT_GPR(25)[ep], r25;				      \
	sld.w	PTO+PT_GPR(26)[ep], r26;				      \
	sld.w	PTO+PT_GPR(27)[ep], r27;				      \
	sld.w	PTO+PT_GPR(28)[ep], r28;				      \
	sld.w	PTO+PT_GPR(29)[ep], r29


/* Save the PC stored in the special register SAVEREG to the state-save-frame
   pointed to by EP.  r19 is clobbered.  */
#define SAVE_PC(savereg)						      \
	stsr	SR_ ## savereg, r19;					      \
	sst.w	r19, PTO+PT_PC[ep]
/* Restore the PC from the state-save-frame pointed to by EP, to the special
   register SAVEREG.  LP is clobbered (it is used as a scratch register
   because the POP_STATE macro restores it, and this macro is usually used
   inside POP_STATE).  */
#define RESTORE_PC(savereg)						      \
	sld.w	PTO+PT_PC[ep], lp;					      \
	ldsr	lp, SR_ ## savereg
/* Save the PSW register stored in the special register SAVREG to the
   state-save-frame pointed to by EP.  r19 is clobbered.  */
#define SAVE_PSW(savereg)						      \
	stsr	SR_ ## savereg, r19;					      \
	sst.w	r19, PTO+PT_PSW[ep]
/* Restore the PSW register from the state-save-frame pointed to by EP, to
   the special register SAVEREG.  LP is clobbered (it is used as a scratch
   register because the POP_STATE macro restores it, and this macro is
   usually used inside POP_STATE).  */
#define RESTORE_PSW(savereg)						      \
	sld.w	PTO+PT_PSW[ep], lp;					      \
	ldsr	lp, SR_ ## savereg

/* Save CTPC/CTPSW/CTBP registers to the state-save-frame pointed to by REG.
   r19 is clobbered.  */
#define SAVE_CT_REGS							      \
	stsr	SR_CTPC, r19;						      \
	sst.w	r19, PTO+PT_CTPC[ep];					      \
	stsr	SR_CTPSW, r19;						      \
	sst.w	r19, PTO+PT_CTPSW[ep];					      \
	stsr	SR_CTBP, r19;						      \
	sst.w	r19, PTO+PT_CTBP[ep]
/* Restore CTPC/CTPSW/CTBP registers from the state-save-frame pointed to by EP.
   LP is clobbered (it is used as a scratch register because the POP_STATE
   macro restores it, and this macro is usually used inside POP_STATE).  */
#define RESTORE_CT_REGS							      \
	sld.w	PTO+PT_CTPC[ep], lp;					      \
	ldsr	lp, SR_CTPC;						      \
	sld.w	PTO+PT_CTPSW[ep], lp;					      \
	ldsr	lp, SR_CTPSW;						      \
	sld.w	PTO+PT_CTBP[ep], lp;					      \
	ldsr	lp, SR_CTBP


/* Push register state, except for the stack pointer, on the stack in the
   form of a state-save-frame (plus some extra padding), in preparation for
   a system call.  This macro makes sure that the EP, GP, and LP
   registers are saved, and TYPE identifies the set of extra registers to
   be saved as well.  Also copies (the new value of) SP to EP.  */
#define PUSH_STATE(type)						      \
	addi	-STATE_SAVE_SIZE, sp, sp; /* Make room on the stack.  */      \
	st.w	ep, PTO+PT_GPR(GPR_EP)[sp];				      \
	mov	sp, ep;							      \
	sst.w	gp, PTO+PT_GPR(GPR_GP)[ep];				      \
	sst.w	lp, PTO+PT_GPR(GPR_LP)[ep];				      \
	type ## _STATE_SAVER
/* Pop a register state pushed by PUSH_STATE, except for the stack pointer,
   from the stack.  */
#define POP_STATE(type)							      \
	mov	sp, ep;							      \
	type ## _STATE_RESTORER;					      \
	sld.w	PTO+PT_GPR(GPR_GP)[ep], gp;				      \
	sld.w	PTO+PT_GPR(GPR_LP)[ep], lp;				      \
	sld.w	PTO+PT_GPR(GPR_EP)[ep], ep;				      \
	addi	STATE_SAVE_SIZE, sp, sp /* Clean up our stack space.  */


/* Switch to the kernel stack if necessary, and push register state on the
   stack in the form of a state-save-frame.  Also load the current task
   pointer if switching from user mode.  The stack-pointer (r3) should have
   already been saved to the memory location SP_SAVE_LOC (the reason for
   this is that the interrupt vectors may be beyond a 22-bit signed offset
   jump from the actual interrupt handler, and this allows them to save the
   stack-pointer and use that register to do an indirect jump).  This macro
   makes sure that `special' registers, system registers, and the stack
   pointer are saved; TYPE identifies the set of extra registers to be
   saved as well.  SYSCALL_NUM is the register in which the system-call
   number this state is for is stored (r0 if this isn't a system call).
   Interrupts should already be disabled when calling this.  */
#define SAVE_STATE(type, syscall_num, sp_save_loc)			      \
	tst1	0, KM;			/* See if already in kernel mode.  */ \
	bz	1f;							      \
	ld.w	sp_save_loc, sp;	/* ... yes, use saved SP.  */	      \
	br	2f;							      \
1:	ld.w	KSP, sp;		/* ... no, switch to kernel stack. */ \
2:	PUSH_STATE(type);						      \
	ld.b	KM, r19;		/* Remember old kernel-mode.  */      \
	sst.w	r19, PTO+PT_KERNEL_MODE[ep];				      \
	ld.w	sp_save_loc, r19;	/* Remember old SP.  */		      \
	sst.w	r19, PTO+PT_GPR(GPR_SP)[ep];				      \
	mov	1, r19;			/* Now definitely in kernel-mode. */  \
	st.b	r19, KM;						      \
	GET_CURRENT_TASK(CURRENT_TASK);	/* Fetch the current task pointer. */ \
	/* Save away the syscall number.  */				      \
	sst.w	syscall_num, PTO+PT_CUR_SYSCALL[ep]


/* Save register state not normally saved by PUSH_STATE for TYPE, to the
   state-save-frame on the stack; also copies SP to EP.  r19 may be trashed. */
#define SAVE_EXTRA_STATE(type)						      \
	mov	sp, ep;							      \
	type ## _EXTRA_STATE_SAVER
/* Restore register state not normally restored by POP_STATE for TYPE,
   from the state-save-frame on the stack; also copies SP to EP.
   r19 may be trashed.  */
#define RESTORE_EXTRA_STATE(type)					      \
	mov	sp, ep;							      \
	type ## _EXTRA_STATE_RESTORER

/* Save any call-clobbered registers not normally saved by PUSH_STATE for
   TYPE, to the state-save-frame on the stack.
   EP may be trashed, but is not guaranteed to contain a copy of SP
   (unlike after most SAVE_... macros).  r19 may be trashed.  */
#define SAVE_EXTRA_STATE_FOR_SCHEDULE(type)				      \
	type ## _SCHEDULE_EXTRA_STATE_SAVER
/* Restore any call-clobbered registers not normally restored by
   POP_STATE for TYPE, to the state-save-frame on the stack.
   EP may be trashed, but is not guaranteed to contain a copy of SP
   (unlike after most RESTORE_... macros).  r19 may be trashed.  */
#define RESTORE_EXTRA_STATE_FOR_SCHEDULE(type)				      \
	type ## _SCHEDULE_EXTRA_STATE_RESTORER


/* These are extra_state_saver/restorer values for a user trap.  Note
   that we save the argument registers so that restarted syscalls will
   function properly (otherwise it wouldn't be necessary), and we must
   _not_ restore the return-value registers (so that traps can return a
   value!), but call-clobbered registers are not saved at all, as the
   caller of the syscall function should have saved them.  */

#define TRAP_RET reti
/* Traps don't save call-clobbered registers (but do still save arg regs).
   We preserve PSw to keep long-term state, namely interrupt status (for traps
   from kernel-mode), and the single-step flag (for user traps).  */
#define TRAP_STATE_SAVER						      \
	SAVE_ARG_REGS;							      \
	SAVE_PC(EIPC);							      \
	SAVE_PSW(EIPSW)
/* When traps return, they just leave call-clobbered registers (except for arg
   regs) with whatever value they have from the kernel.  Traps don't preserve
   the PSW, but we zero EIPSW to ensure it doesn't contain anything dangerous
   (in particular, the single-step flag).  */
#define TRAP_STATE_RESTORER						      \
	RESTORE_ARG_REGS;						      \
	RESTORE_PC(EIPC);						      \
	RESTORE_PSW(EIPSW)
/* Save registers not normally saved by traps.  We need to save r12, even
   though it's nominally call-clobbered, because it's used when restarting
   a system call (the signal-handling path uses SAVE_EXTRA_STATE, and
   expects r12 to be restored when the trap returns).  */
#define TRAP_EXTRA_STATE_SAVER						      \
	SAVE_RVAL_REGS;							      \
	sst.w	r12, PTO+PT_GPR(12)[ep];				      \
	SAVE_CALL_SAVED_REGS;						      \
	SAVE_CT_REGS
#define TRAP_EXTRA_STATE_RESTORER					      \
	RESTORE_RVAL_REGS;						      \
	sld.w	PTO+PT_GPR(12)[ep], r12;				      \
	RESTORE_CALL_SAVED_REGS;					      \
	RESTORE_CT_REGS
/* Save registers prior to calling scheduler (just before trap returns).
   We have to save the return-value registers to preserve the trap's return
   value.  Note that ..._SCHEDULE_EXTRA_STATE_SAVER, unlike most ..._SAVER
   macros, is required to setup EP itself if EP is needed (this is because
   in many cases, the macro is empty).  */
#define TRAP_SCHEDULE_EXTRA_STATE_SAVER					      \
	mov sp, ep;							      \
	SAVE_RVAL_REGS
/* Note that ..._SCHEDULE_EXTRA_STATE_RESTORER, unlike most ..._RESTORER
   macros, is required to setup EP itself if EP is needed (this is because
   in many cases, the macro is empty).  */
#define TRAP_SCHEDULE_EXTRA_STATE_RESTORER				      \
	mov sp, ep;							      \
	RESTORE_RVAL_REGS

/* Register saving/restoring for maskable interrupts.  */
#define IRQ_RET reti
#define IRQ_STATE_SAVER							      \
	SAVE_CALL_CLOBBERED_REGS;					      \
	SAVE_PC(EIPC);							      \
	SAVE_PSW(EIPSW)
#define IRQ_STATE_RESTORER						      \
	RESTORE_CALL_CLOBBERED_REGS;					      \
	RESTORE_PC(EIPC);						      \
	RESTORE_PSW(EIPSW)
#define IRQ_EXTRA_STATE_SAVER						      \
	SAVE_CALL_SAVED_REGS;						      \
	SAVE_CT_REGS
#define IRQ_EXTRA_STATE_RESTORER					      \
	RESTORE_CALL_SAVED_REGS;					      \
	RESTORE_CT_REGS
#define IRQ_SCHEDULE_EXTRA_STATE_SAVER	     /* nothing */
#define IRQ_SCHEDULE_EXTRA_STATE_RESTORER    /* nothing */

/* Register saving/restoring for non-maskable interrupts.  */
#define NMI_RET reti
#define NMI_STATE_SAVER							      \
	SAVE_CALL_CLOBBERED_REGS;					      \
	SAVE_PC(FEPC);							      \
	SAVE_PSW(FEPSW);
#define NMI_STATE_RESTORER						      \
	RESTORE_CALL_CLOBBERED_REGS;					      \
	RESTORE_PC(FEPC);						      \
	RESTORE_PSW(FEPSW);
#define NMI_EXTRA_STATE_SAVER						      \
	SAVE_CALL_SAVED_REGS;						      \
	SAVE_CT_REGS
#define NMI_EXTRA_STATE_RESTORER					      \
	RESTORE_CALL_SAVED_REGS;					      \
	RESTORE_CT_REGS
#define NMI_SCHEDULE_EXTRA_STATE_SAVER	     /* nothing */
#define NMI_SCHEDULE_EXTRA_STATE_RESTORER    /* nothing */

/* Register saving/restoring for debug traps.  */
#define DBTRAP_RET .long 0x014607E0 /* `dbret', but gas doesn't support it. */
#define DBTRAP_STATE_SAVER						      \
	SAVE_CALL_CLOBBERED_REGS;					      \
	SAVE_PC(DBPC);							      \
	SAVE_PSW(DBPSW)
#define DBTRAP_STATE_RESTORER						      \
	RESTORE_CALL_CLOBBERED_REGS;					      \
	RESTORE_PC(DBPC);						      \
	RESTORE_PSW(DBPSW)
#define DBTRAP_EXTRA_STATE_SAVER					      \
	SAVE_CALL_SAVED_REGS;						      \
	SAVE_CT_REGS
#define DBTRAP_EXTRA_STATE_RESTORER					      \
	RESTORE_CALL_SAVED_REGS;					      \
	RESTORE_CT_REGS
#define DBTRAP_SCHEDULE_EXTRA_STATE_SAVER	/* nothing */
#define DBTRAP_SCHEDULE_EXTRA_STATE_RESTORER	/* nothing */

/* Register saving/restoring for a context switch.  We don't need to save
   too many registers, because context-switching looks like a function call
   (via the function `switch_thread'), so callers will save any
   call-clobbered registers themselves.  We do need to save the CT regs, as
   they're normally not saved during kernel entry (the kernel doesn't use
   them).  We save PSW so that interrupt-status state will correctly follow
   each thread (mostly NMI vs. normal-IRQ/trap), though for the most part
   it doesn't matter since threads are always in almost exactly the same
   processor state during a context switch.  The stack pointer and return
   value are handled by switch_thread itself.  */
#define SWITCH_STATE_SAVER						      \
	SAVE_CALL_SAVED_REGS;						      \
	SAVE_PSW(PSW);							      \
	SAVE_CT_REGS
#define SWITCH_STATE_RESTORER						      \
	RESTORE_CALL_SAVED_REGS;					      \
	RESTORE_PSW(PSW);						      \
	RESTORE_CT_REGS


/* Restore register state from the state-save-frame on the stack, switch back
   to the user stack if necessary, and return from the trap/interrupt.
   EXTRA_STATE_RESTORER is a sequence of assembly language statements to
   restore anything not restored by this macro.  Only registers not saved by
   the C compiler are restored (that is, R3(sp), R4(gp), R31(lp), and
   anything restored by EXTRA_STATE_RESTORER).  */
#define RETURN(type)							      \
	ld.b	PTO+PT_KERNEL_MODE[sp], r19;				      \
	di;				/* Disable interrupts */	      \
	cmp	r19, r0;		/* See if returning to kernel mode, */\
	bne	2f;			/* ... if so, skip resched &c.  */    \
									      \
	/* We're returning to user mode, so check for various conditions that \
	   trigger rescheduling. */					      \
	GET_CURRENT_THREAD(r18);					      \
	ld.w	TI_FLAGS[r18], r19;					      \
	andi	_TIF_NEED_RESCHED, r19, r0;				      \
	bnz	3f;			/* Call the scheduler.  */	      \
5:	andi	_TIF_SIGPENDING, r19, r18;				      \
	ld.w	TASK_PTRACE[CURRENT_TASK], r19; /* ptrace flags */	      \
	or	r18, r19;		/* see if either is non-zero */	      \
	bnz	4f;			/* if so, handle them */	      \
									      \
/* Return to user state.  */						      \
1:	st.b	r0, KM;			/* Now officially in user state. */   \
									      \
/* Final return.  The stack-pointer fiddling is not needed when returning     \
   to kernel-mode, but they don't hurt, and this way we can share the	      \
   (sometimes rather lengthy) POP_STATE macro.  */			      \
2:	POP_STATE(type);						      \
	st.w	sp, KSP;		/* Save the kernel stack pointer. */  \
	ld.w	PT_GPR(GPR_SP)-PT_SIZE[sp], sp; /* Restore stack pointer. */  \
	type ## _RET;			/* Return from the trap/interrupt. */ \
									      \
/* Call the scheduler before returning from a syscall/trap. */		      \
3:	SAVE_EXTRA_STATE_FOR_SCHEDULE(type); /* Prepare to call scheduler. */ \
	jarl	call_scheduler, lp;	/* Call scheduler */		      \
	di;				/* The scheduler enables interrupts */\
	RESTORE_EXTRA_STATE_FOR_SCHEDULE(type);				      \
	GET_CURRENT_THREAD(r18);					      \
	ld.w	TI_FLAGS[r18], r19;					      \
	br	5b;			/* Continue with return path. */      \
									      \
/* Handle a signal or ptraced process return.				      \
   r18 should be non-zero if there are pending signals.  */		      \
4:	/* Not all registers are saved by the normal trap/interrupt entry     \
	   points (for instance, call-saved registers (because the normal     \
	   C-compiler calling sequence in the kernel makes sure they're	      \
	   preserved), and call-clobbered registers in the case of	      \
	   traps), but signal handlers may want to examine or change the      \
	   complete register state.  Here we save anything not saved by	      \
	   the normal entry sequence, so that it may be safely restored	      \
	   (in a possibly modified form) after do_signal returns.  */	      \
	SAVE_EXTRA_STATE(type);		/* Save state not saved by entry. */  \
	jarl	handle_signal_or_ptrace_return, lp;			      \
	RESTORE_EXTRA_STATE(type);	/* Restore extra regs.  */	      \
	br	1b


/* Jump to the appropriate function for the system call number in r12
   (r12 is not preserved), or return an error if r12 is not valid.  The
   LP register should point to the location where the called function
   should return.  [note that MAKE_SYS_CALL uses label 1]  */
#define MAKE_SYS_CALL							      \
	/* Figure out which function to use for this system call.  */	      \
	shl	2, r12;							      \
	/* See if the system call number is valid.  */			      \
	addi	lo(CSYM(sys_call_table) - sys_call_table_end), r12, r0;	      \
	bnh	1f;							      \
	mov	hilo(CSYM(sys_call_table)), r19;			      \
	add	r19, r12;						      \
	ld.w	0[r12], r12;						      \
	/* Make the system call.  */					      \
	jmp	[r12];							      \
	/* The syscall number is invalid, return an error.  */		      \
1:	addi	-ENOSYS, r0, r10;					      \
	jmp	[lp]


	.text

/*
 * User trap.
 *
 * Trap 0 system calls are also handled here.
 *
 * The stack-pointer (r3) should have already been saved to the memory
 * location ENTRY_SP (the reason for this is that the interrupt vectors may be
 * beyond a 22-bit signed offset jump from the actual interrupt handler, and
 * this allows them to save the stack-pointer and use that register to do an
 * indirect jump).
 *
 * Syscall protocol:
 *   Syscall number in r12, args in r6-r9
 *   Return value in r10
 */
G_ENTRY(trap):
	SAVE_STATE (TRAP, r12, ENTRY_SP) // Save registers.
	stsr	SR_ECR, r19		// Find out which trap it was.
	ei				// Enable interrupts.
	mov	hilo(ret_from_trap), lp	// where the trap should return

	// The following two shifts (1) clear out extraneous NMI data in the
	// upper 16-bits, (2) convert the 0x40 - 0x5f range of trap ECR
	// numbers into the (0-31) << 2 range we want, (3) set the flags.
	shl	27, r19			// chop off all high bits
	shr	25, r19			// scale back down and then << 2
	bnz	2f			// See if not trap 0.

	// Trap 0 is a `short' system call, skip general trap table.
	MAKE_SYS_CALL			// Jump to the syscall function.

2:	// For other traps, use a table lookup.
	mov	hilo(CSYM(trap_table)), r18
	add	r19, r18
	ld.w	0[r18], r18
	jmp	[r18]			// Jump to the trap handler.
END(trap)

/* This is just like ret_from_trap, but first restores extra registers
   saved by some wrappers.  */
L_ENTRY(restore_extra_regs_and_ret_from_trap):
	RESTORE_EXTRA_STATE(TRAP)
	// fall through
END(restore_extra_regs_and_ret_from_trap)

/* Entry point used to return from a syscall/trap.  */
L_ENTRY(ret_from_trap):
	RETURN(TRAP)
END(ret_from_trap)


/* This the initial entry point for a new child thread, with an appropriate
   stack in place that makes it look the the child is in the middle of an
   syscall.  This function is actually `returned to' from switch_thread
   (copy_thread makes ret_from_fork the return address in each new thread's
   saved context).  */
C_ENTRY(ret_from_fork):
	mov	r10, r6			// switch_thread returns the prev task.
	jarl	CSYM(schedule_tail), lp	// ...which is schedule_tail's arg
	mov	r0, r10			// Child's fork call should return 0.
	br	ret_from_trap		// Do normal trap return.
C_END(ret_from_fork)


/*
 * Trap 1: `long' system calls
 * `Long' syscall protocol:
 *   Syscall number in r12, args in r6-r9, r13-r14
 *   Return value in r10
 */
L_ENTRY(syscall_long):
	// Push extra arguments on the stack.  Note that by default, the trap
	// handler reserves enough stack space for 6 arguments, so we don't
	// have to make any additional room.
	st.w	r13, 16[sp]		// arg 5
	st.w	r14, 20[sp]		// arg 6

	// Make sure r13 and r14 are preserved, in case we have to restart a
	// system call because of a signal (ep has already been set by caller).
	st.w	r13, PTO+PT_GPR(13)[sp]
	st.w	r14, PTO+PT_GPR(13)[sp]
	mov	hilo(ret_from_long_syscall), lp

	MAKE_SYS_CALL			// Jump to the syscall function.
END(syscall_long)

/* Entry point used to return from a long syscall.  Only needed to restore
   r13/r14 if the general trap mechanism doesnt' do so.  */
L_ENTRY(ret_from_long_syscall):
	ld.w	PTO+PT_GPR(13)[sp], r13 // Restore the extra registers
	ld.w	PTO+PT_GPR(13)[sp], r14
	br	ret_from_trap		// The rest is the same as other traps
END(ret_from_long_syscall)


/* These syscalls need access to the struct pt_regs on the stack, so we
   implement them in assembly (they're basically all wrappers anyway).  */

L_ENTRY(sys_fork_wrapper):
#ifdef CONFIG_MMU
	addi	SIGCHLD, r0, r6		   // Arg 0: flags
	ld.w	PTO+PT_GPR(GPR_SP)[sp], r7 // Arg 1: child SP (use parent's)
	movea	PTO, sp, r8		   // Arg 2: parent context
	mov	r0, r9			   // Arg 3/4/5: 0
	st.w	r0, 16[sp]
	st.w	r0, 20[sp]
	mov	hilo(CSYM(do_fork)), r18   // Where the real work gets done
	br	save_extra_state_tramp	   // Save state and go there
#else
	// fork almost works, enough to trick you into looking elsewhere :-(
	addi	-EINVAL, r0, r10
	jmp	[lp]
#endif
END(sys_fork_wrapper)

L_ENTRY(sys_vfork_wrapper):
	addi	CLONE_VFORK | CLONE_VM | SIGCHLD, r0, r6 // Arg 0: flags
	ld.w	PTO+PT_GPR(GPR_SP)[sp], r7 // Arg 1: child SP (use parent's)
	movea	PTO, sp, r8		   // Arg 2: parent context
	mov	r0, r9			   // Arg 3/4/5: 0
	st.w	r0, 16[sp]
	st.w	r0, 20[sp]
	mov	hilo(CSYM(do_fork)), r18   // Where the real work gets done
	br	save_extra_state_tramp	   // Save state and go there
END(sys_vfork_wrapper)

L_ENTRY(sys_clone_wrapper):
	ld.w	PTO+PT_GPR(GPR_SP)[sp], r19// parent's stack pointer
	cmp	r7, r0			   // See if child SP arg (arg 1) is 0.
	cmov	z, r19, r7, r7		   // ... and use the parent's if so.
	movea	PTO, sp, r8		   // Arg 2: parent context
	mov	r0, r9			   // Arg 3/4/5: 0
	st.w	r0, 16[sp]
	st.w	r0, 20[sp]
	mov	hilo(CSYM(do_fork)), r18   // Where the real work gets done
	br	save_extra_state_tramp	   // Save state and go there
END(sys_clone_wrapper)


L_ENTRY(sys_execve_wrapper):
	movea	PTO, sp, r9		// add user context as 4th arg
	jr	CSYM(sys_execve)	// Do real work (tail-call).
END(sys_execve_wrapper)


L_ENTRY(sys_sigsuspend_wrapper):
	movea	PTO, sp, r7		// add user context as 2nd arg
	mov	hilo(CSYM(sys_sigsuspend)), r18	// syscall function
	jarl	save_extra_state_tramp, lp	// Save state and do it
	br	restore_extra_regs_and_ret_from_trap
END(sys_sigsuspend_wrapper)
L_ENTRY(sys_rt_sigsuspend_wrapper):
	movea	PTO, sp, r8		// add user context as 3rd arg
	mov	hilo(CSYM(sys_rt_sigsuspend)), r18 // syscall function
	jarl	save_extra_state_tramp, lp	   // Save state and do it
	br	restore_extra_regs_and_ret_from_trap
END(sys_rt_sigsuspend_wrapper)

L_ENTRY(sys_sigreturn_wrapper):
	movea	PTO, sp, r6		// add user context as 1st arg
	mov	hilo(CSYM(sys_sigreturn)), r18	// syscall function
	jarl	save_extra_state_tramp, lp	// Save state and do it
	br	restore_extra_regs_and_ret_from_trap
END(sys_sigreturn_wrapper)
L_ENTRY(sys_rt_sigreturn_wrapper):
	movea	PTO, sp, r6		// add user context as 1st arg
	mov	hilo(CSYM(sys_rt_sigreturn)), r18// syscall function
	jarl	save_extra_state_tramp, lp	 // Save state and do it
	br	restore_extra_regs_and_ret_from_trap
END(sys_rt_sigreturn_wrapper)


/* Save any state not saved by SAVE_STATE(TRAP), and jump to r18.
   It's main purpose is to share the rather lengthy code sequence that
   SAVE_STATE expands into among the above wrapper functions.  */
L_ENTRY(save_extra_state_tramp):
	SAVE_EXTRA_STATE(TRAP)		// Save state not saved by entry.
	jmp	[r18]			// Do the work the caller wants
END(save_extra_state_tramp)


/*
 * Hardware maskable interrupts.
 *
 * The stack-pointer (r3) should have already been saved to the memory
 * location ENTRY_SP (the reason for this is that the interrupt vectors may be
 * beyond a 22-bit signed offset jump from the actual interrupt handler, and
 * this allows them to save the stack-pointer and use that register to do an
 * indirect jump).
 */
G_ENTRY(irq):
	SAVE_STATE (IRQ, r0, ENTRY_SP)	// Save registers.

	stsr	SR_ECR, r6		// Find out which interrupt it was.
	movea	PTO, sp, r7		// User regs are arg2

	// All v850 implementations I know about encode their interrupts as
	// multiples of 0x10, starting at 0x80 (after NMIs and software
	// interrupts).  Convert this number into a simple IRQ index for the
	// rest of the kernel.  We also clear the upper 16 bits, which hold
	// NMI info, and don't appear to be cleared when a NMI returns.
	shl	16, r6			// clear upper 16 bits
	shr	20, r6			// shift back, and remove lower nibble
	add	-8, r6			// remove bias for irqs

	// Call the high-level interrupt handling code.
	jarl	CSYM(handle_irq), lp

	RETURN(IRQ)
END(irq)


/*
 * Debug trap / illegal-instruction exception
 *
 * The stack-pointer (r3) should have already been saved to the memory
 * location ENTRY_SP (the reason for this is that the interrupt vectors may be
 * beyond a 22-bit signed offset jump from the actual interrupt handler, and
 * this allows them to save the stack-pointer and use that register to do an
 * indirect jump).
 */
G_ENTRY(dbtrap):
	SAVE_STATE (DBTRAP, r0, ENTRY_SP)// Save registers.

	/* First see if we came from kernel mode; if so, the dbtrap
	   instruction has a special meaning, to set the DIR (`debug
	   information register') register.  This is because the DIR register
	   can _only_ be manipulated/read while in `debug mode,' and debug
	   mode is only active while we're inside the dbtrap handler.  The
	   exact functionality is:  { DIR = (DIR | r6) & ~r7; return DIR; }. */
	ld.b	PTO+PT_KERNEL_MODE[sp], r19
	cmp	r19, r0
	bz	1f

	stsr	SR_DIR, r10
	or	r6, r10
	not	r7, r7
	and	r7, r10
	ldsr	r10, SR_DIR
	stsr	SR_DIR, r10		// Confirm the value we set
	st.w	r10, PTO+PT_GPR(10)[sp]	// return it
	br	3f

1:	ei				// Enable interrupts.

	/* The default signal type we raise.  */
	mov	SIGTRAP, r6

	/* See if it's a single-step trap.  */
	stsr	SR_DBPSW, r19
	andi	0x0800, r19, r19
	bnz	2f

	/* Look to see if the preceding instruction was is a dbtrap or not,
	   to decide which signal we should use.  */
	stsr	SR_DBPC, r19		// PC following trapping insn
	ld.hu	-2[r19], r19
	ori	0xf840, r0, r20		// DBTRAP insn
	cmp	r19, r20		// Was this trap caused by DBTRAP?
	cmov	ne, SIGILL, r6, r6	// Choose signal appropriately

	/* Raise the desired signal.  */
2:	mov	CURRENT_TASK, r7	// Arg 1: task
	jarl	CSYM(send_sig), lp	// tail call

3:	RETURN(DBTRAP)
END(dbtrap)


/*
 * Hardware non-maskable interrupts.
 *
 * The stack-pointer (r3) should have already been saved to the memory
 * location ENTRY_SP (the reason for this is that the interrupt vectors may be
 * beyond a 22-bit signed offset jump from the actual interrupt handler, and
 * this allows them to save the stack-pointer and use that register to do an
 * indirect jump).
 */
G_ENTRY(nmi):
	SAVE_STATE (NMI, r0, NMI_ENTRY_SP); /* Save registers.  */

	stsr	SR_ECR, r6;		/* Find out which nmi it was.  */
	shr	20, r6;			/* Extract NMI code in bits 20-24. */
	movea	PTO, sp, r7;		/* User regs are arg2.  */

	/* Non-maskable interrupts always lie right after maskable interrupts.
	   Call the generic IRQ handler, with two arguments, the IRQ number,
	   and a pointer to the user registers, to handle the specifics.
	   (we subtract one because the first NMI has code 1).  */
	addi	FIRST_NMI - 1, r6, r6
	jarl	CSYM(handle_irq), lp

	RETURN(NMI)
END(nmi)


/*
 * Trap with no handler
 */
L_ENTRY(bad_trap_wrapper):
	mov	r19, r6			// Arg 0: trap number
	movea	PTO, sp, r7		// Arg 1: user regs
	jr	CSYM(bad_trap)		// tail call handler
END(bad_trap_wrapper)


/*
 * Invoke the scheduler, called from the trap/irq kernel exit path.
 *
 * This basically just calls `schedule', but also arranges for extra
 * registers to be saved for ptrace'd processes, so ptrace can modify them.
 */
L_ENTRY(call_scheduler):
	ld.w	TASK_PTRACE[CURRENT_TASK], r19	// See if task is ptrace'd
	cmp	r19, r0
	bnz	1f			// ... yes, do special stuff
	jr	CSYM(schedule)		// ... no, just tail-call scheduler

	// Save extra regs for ptrace'd task.  We want to save anything
	// that would otherwise only be `implicitly' saved by the normal
	// compiler calling-convention.
1:	mov	sp, ep			// Setup EP for SAVE_CALL_SAVED_REGS
	SAVE_CALL_SAVED_REGS		// Save call-saved registers to stack
	mov	lp, r20			// Save LP in a callee-saved register

	jarl	CSYM(schedule), lp	// Call scheduler

	mov	r20, lp
	mov	sp, ep			// We can't rely on EP after return
	RESTORE_CALL_SAVED_REGS		// Restore (possibly modified) regs
	jmp	[lp]			// Return to the return path
END(call_scheduler)


/*
 * This is an out-of-line handler for two special cases during the kernel
 * trap/irq exit sequence:
 *
 *  (1) If r18 is non-zero then a signal needs to be handled, which is
 *	done, and then the caller returned to.
 *
 *  (2) If r18 is non-zero then we're returning to a ptraced process, which
 *	has several special cases -- single-stepping and trap tracing, both
 *	of which require using the `dbret' instruction to exit the kernel
 *	instead of the normal `reti' (this is because the CPU not correctly
 *	single-step after a reti).  In this case, of course, this handler
 *	never returns to the caller.
 *
 * In either case, all registers should have been saved to the current
 * state-save-frame on the stack, except for callee-saved registers.
 *
 * [These two different cases are combined merely to avoid bloating the
 * macro-inlined code, not because they really make much sense together!]
 */
L_ENTRY(handle_signal_or_ptrace_return):
	cmp	r18, r0			// See if handling a signal
	bz	1f			// ... nope, go do ptrace return

	// Handle a signal
	mov	lp, r20			// Save link-pointer
	mov	r10, r21		// Save return-values (for trap)
	mov	r11, r22

	movea	PTO, sp, r6		// Arg 1: struct pt_regs *regs
	mov	r0, r7			// Arg 2: sigset_t *oldset
	jarl	CSYM(do_signal), lp	// Handle the signal
	di				// sig handling enables interrupts

	mov	r20, lp			// Restore link-pointer
	mov	r21, r10		// Restore return-values (for trap)
	mov	r22, r11
	ld.w	TASK_PTRACE[CURRENT_TASK], r19  // check ptrace flags too
	cmp	r19, r0
	bnz	1f			// ... some set, so look more
2:	jmp	[lp]			// ... none set, so return normally

	// ptrace return
1:	ld.w	PTO+PT_PSW[sp], r19	// Look at user-processes's flags
	andi	0x0800, r19, r19	// See if single-step flag is set
	bz	2b			// ... nope, return normally

	// Return as if from a dbtrap insn
	st.b	r0, KM			// Now officially in user state.
	POP_STATE(DBTRAP)		// Restore regs
	st.w	sp, KSP			// Save the kernel stack pointer.
	ld.w	PT_GPR(GPR_SP)-PT_SIZE[sp], sp // Restore user stack pointer.
	DBTRAP_RET			// Return from the trap/interrupt.
END(handle_signal_or_ptrace_return)


/*
 * This is where we switch between two threads.  The arguments are:
 *   r6 -- pointer to the struct thread for the `current' process
 *   r7 -- pointer to the struct thread for the `new' process.
 * when this function returns, it will return to the new thread.
 */
C_ENTRY(switch_thread):
	// Return the previous task (r10 is not clobbered by restore below)
	mov	CURRENT_TASK, r10
	// First, push the current processor state on the stack
	PUSH_STATE(SWITCH)
	// Now save the location of the kernel stack pointer for this thread;
	// since we've pushed all other state on the stack, this is enough to
	// restore it all later.
	st.w	sp, THREAD_KSP[r6]
	// Now restore the stack pointer from the new process
	ld.w	THREAD_KSP[r7], sp
	// ... and restore all state from that
	POP_STATE(SWITCH)
	// Update the current task pointer
	GET_CURRENT_TASK(CURRENT_TASK)
	// Now return into the new thread
	jmp	[lp]
C_END(switch_thread)


	.data

	.align 4
C_DATA(trap_table):
	.long bad_trap_wrapper		// trap 0, doesn't use trap table.
	.long syscall_long		// trap 1, `long' syscall.
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
	.long bad_trap_wrapper
C_END(trap_table)


	.section .rodata

	.align 4
C_DATA(sys_call_table):
	.long CSYM(sys_restart_syscall)	// 0
	.long CSYM(sys_exit)
	.long sys_fork_wrapper
	.long CSYM(sys_read)
	.long CSYM(sys_write)
	.long CSYM(sys_open)		// 5
	.long CSYM(sys_close)
	.long CSYM(sys_waitpid)
	.long CSYM(sys_creat)
	.long CSYM(sys_link)
	.long CSYM(sys_unlink)		// 10
	.long sys_execve_wrapper
	.long CSYM(sys_chdir)
	.long CSYM(sys_time)
	.long CSYM(sys_mknod)
	.long CSYM(sys_chmod)		// 15
	.long CSYM(sys_chown)
	.long CSYM(sys_ni_syscall)	// was: break
	.long CSYM(sys_ni_syscall)	// was: oldstat (aka stat)
	.long CSYM(sys_lseek)
	.long CSYM(sys_getpid)		// 20
	.long CSYM(sys_mount)
	.long CSYM(sys_oldumount)
	.long CSYM(sys_setuid)
	.long CSYM(sys_getuid)
	.long CSYM(sys_stime)		// 25
	.long CSYM(sys_ptrace)
	.long CSYM(sys_alarm)
	.long CSYM(sys_ni_syscall)	// was: oldfstat (aka fstat)
	.long CSYM(sys_pause)
	.long CSYM(sys_utime)		// 30
	.long CSYM(sys_ni_syscall)	// was: stty
	.long CSYM(sys_ni_syscall)	// was: gtty
	.long CSYM(sys_access)
	.long CSYM(sys_nice)
	.long CSYM(sys_ni_syscall)	// 35, was: ftime
	.long CSYM(sys_sync)
	.long CSYM(sys_kill)
	.long CSYM(sys_rename)
	.long CSYM(sys_mkdir)
	.long CSYM(sys_rmdir)		// 40
	.long CSYM(sys_dup)
	.long CSYM(sys_pipe)
	.long CSYM(sys_times)
	.long CSYM(sys_ni_syscall)	// was: prof
	.long CSYM(sys_brk)		// 45
	.long CSYM(sys_setgid)
	.long CSYM(sys_getgid)
	.long CSYM(sys_signal)
	.long CSYM(sys_geteuid)
	.long CSYM(sys_getegid)		// 50
	.long CSYM(sys_acct)
	.long CSYM(sys_umount)		// recycled never used phys()
	.long CSYM(sys_ni_syscall)	// was: lock
	.long CSYM(sys_ioctl)
	.long CSYM(sys_fcntl)		// 55
	.long CSYM(sys_ni_syscall)	// was: mpx
	.long CSYM(sys_setpgid)
	.long CSYM(sys_ni_syscall)	// was: ulimit
	.long CSYM(sys_ni_syscall)
	.long CSYM(sys_umask)		// 60
	.long CSYM(sys_chroot)
	.long CSYM(sys_ustat)
	.long CSYM(sys_dup2)
	.long CSYM(sys_getppid)
	.long CSYM(sys_getpgrp)		// 65
	.long CSYM(sys_setsid)
	.long CSYM(sys_sigaction)
	.long CSYM(sys_sgetmask)
	.long CSYM(sys_ssetmask)
	.long CSYM(sys_setreuid)	// 70
	.long CSYM(sys_setregid)
	.long sys_sigsuspend_wrapper
	.long CSYM(sys_sigpending)
	.long CSYM(sys_sethostname)
	.long CSYM(sys_setrlimit)	// 75
	.long CSYM(sys_getrlimit)
	.long CSYM(sys_getrusage)
	.long CSYM(sys_gettimeofday)
	.long CSYM(sys_settimeofday)
	.long CSYM(sys_getgroups)	// 80
	.long CSYM(sys_setgroups)
	.long CSYM(sys_select)
	.long CSYM(sys_symlink)
	.long CSYM(sys_ni_syscall)	// was: oldlstat (aka lstat)
	.long CSYM(sys_readlink)	// 85
	.long CSYM(sys_uselib)
	.long CSYM(sys_swapon)
	.long CSYM(sys_reboot)
	.long CSYM(old_readdir)
	.long CSYM(sys_mmap)		// 90
	.long CSYM(sys_munmap)
	.long CSYM(sys_truncate)
	.long CSYM(sys_ftruncate)
	.long CSYM(sys_fchmod)
	.long CSYM(sys_fchown)		// 95
	.long CSYM(sys_getpriority)
	.long CSYM(sys_setpriority)
	.long CSYM(sys_ni_syscall)	// was: profil
	.long CSYM(sys_statfs)
	.long CSYM(sys_fstatfs)		// 100
	.long CSYM(sys_ni_syscall)	// i386: ioperm
	.long CSYM(sys_socketcall)
	.long CSYM(sys_syslog)
	.long CSYM(sys_setitimer)
	.long CSYM(sys_getitimer)	// 105
	.long CSYM(sys_newstat)
	.long CSYM(sys_newlstat)
	.long CSYM(sys_newfstat)
	.long CSYM(sys_ni_syscall)	// was: olduname (aka uname)
	.long CSYM(sys_ni_syscall)	// 110, i386: iopl
	.long CSYM(sys_vhangup)
	.long CSYM(sys_ni_syscall)	// was: idle
	.long CSYM(sys_ni_syscall)	// i386: vm86old
	.long CSYM(sys_wait4)
	.long CSYM(sys_swapoff)		// 115
	.long CSYM(sys_sysinfo)
	.long CSYM(sys_ipc)
	.long CSYM(sys_fsync)
	.long sys_sigreturn_wrapper
	.long sys_clone_wrapper		// 120
	.long CSYM(sys_setdomainname)
	.long CSYM(sys_newuname)
	.long CSYM(sys_ni_syscall)	// i386: modify_ldt, m68k: cacheflush
	.long CSYM(sys_adjtimex)
	.long CSYM(sys_ni_syscall)	// 125 - sys_mprotect
	.long CSYM(sys_sigprocmask)
	.long CSYM(sys_ni_syscall)	// sys_create_module
	.long CSYM(sys_init_module)
	.long CSYM(sys_delete_module)
	.long CSYM(sys_ni_syscall)	// 130 - sys_get_kernel_syms
	.long CSYM(sys_quotactl)
	.long CSYM(sys_getpgid)
	.long CSYM(sys_fchdir)
	.long CSYM(sys_bdflush)
	.long CSYM(sys_sysfs)		// 135
	.long CSYM(sys_personality)
	.long CSYM(sys_ni_syscall)	// for afs_syscall
	.long CSYM(sys_setfsuid)
	.long CSYM(sys_setfsgid)
	.long CSYM(sys_llseek)		// 140
	.long CSYM(sys_getdents)
	.long CSYM(sys_select)		// for backward compat; remove someday
	.long CSYM(sys_flock)
	.long CSYM(sys_ni_syscall)	// sys_msync
	.long CSYM(sys_readv)		// 145
	.long CSYM(sys_writev)
	.long CSYM(sys_getsid)
	.long CSYM(sys_fdatasync)
	.long CSYM(sys_sysctl)
	.long CSYM(sys_ni_syscall)	// 150 - sys_mlock
	.long CSYM(sys_ni_syscall)	// sys_munlock
	.long CSYM(sys_ni_syscall)	// sys_mlockall
	.long CSYM(sys_ni_syscall)	// sys_munlockall
	.long CSYM(sys_sched_setparam)
	.long CSYM(sys_sched_getparam)	// 155
	.long CSYM(sys_sched_setscheduler)
	.long CSYM(sys_sched_getscheduler)
	.long CSYM(sys_sched_yield)
	.long CSYM(sys_sched_get_priority_max)
	.long CSYM(sys_sched_get_priority_min)	// 160
	.long CSYM(sys_sched_rr_get_interval)
	.long CSYM(sys_nanosleep)
	.long CSYM(sys_ni_syscall)	// sys_mremap
	.long CSYM(sys_setresuid)
	.long CSYM(sys_getresuid)	// 165
	.long CSYM(sys_ni_syscall)	// for vm86
	.long CSYM(sys_ni_syscall)	// sys_query_module
	.long CSYM(sys_poll)
	.long CSYM(sys_nfsservctl)
	.long CSYM(sys_setresgid)	// 170
	.long CSYM(sys_getresgid)
	.long CSYM(sys_prctl)
	.long sys_rt_sigreturn_wrapper
	.long CSYM(sys_rt_sigaction)
	.long CSYM(sys_rt_sigprocmask)	// 175
	.long CSYM(sys_rt_sigpending)
	.long CSYM(sys_rt_sigtimedwait)
	.long CSYM(sys_rt_sigqueueinfo)
	.long sys_rt_sigsuspend_wrapper
	.long CSYM(sys_pread64)		// 180
	.long CSYM(sys_pwrite64)
	.long CSYM(sys_lchown)
	.long CSYM(sys_getcwd)
	.long CSYM(sys_capget)
	.long CSYM(sys_capset)		// 185
	.long CSYM(sys_sigaltstack)
	.long CSYM(sys_sendfile)
	.long CSYM(sys_ni_syscall)	// streams1
	.long CSYM(sys_ni_syscall)	// streams2
	.long sys_vfork_wrapper		// 190
	.long CSYM(sys_ni_syscall)
	.long CSYM(sys_mmap2)
	.long CSYM(sys_truncate64)
	.long CSYM(sys_ftruncate64)
	.long CSYM(sys_stat64)		// 195
	.long CSYM(sys_lstat64)
	.long CSYM(sys_fstat64)
	.long CSYM(sys_fcntl64)
	.long CSYM(sys_getdents64)
	.long CSYM(sys_pivot_root)	// 200
	.long CSYM(sys_gettid)
	.long CSYM(sys_tkill)
sys_call_table_end:
C_END(sys_call_table)
