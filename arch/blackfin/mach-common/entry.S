/*
 * Contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all
 * interrupts and faults that can result in a task-switch.
 *
 * Copyright 2005-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

/* NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/unistd.h>
#include <asm/blackfin.h>
#include <asm/errno.h>
#include <asm/fixed_code.h>
#include <asm/thread_info.h>  /* TIF_NEED_RESCHED */
#include <asm/asm-offsets.h>
#include <asm/trace.h>
#include <asm/traps.h>

#include <asm/context.S>

#if defined(CONFIG_BFIN_SCRATCH_REG_RETN)
# define EX_SCRATCH_REG RETN
#elif defined(CONFIG_BFIN_SCRATCH_REG_RETE)
# define EX_SCRATCH_REG RETE
#else
# define EX_SCRATCH_REG CYCLES
#endif

#ifdef CONFIG_EXCPT_IRQ_SYSC_L1
.section .l1.text
#else
.text
#endif

/* Slightly simplified and streamlined entry point for CPLB misses.
 * This one does not lower the level to IRQ5, and thus can be used to
 * patch up CPLB misses on the kernel stack.
 */
#if ANOMALY_05000261
#define _ex_dviol _ex_workaround_261
#define _ex_dmiss _ex_workaround_261
#define _ex_dmult _ex_workaround_261

ENTRY(_ex_workaround_261)
	/*
	 * Work around an anomaly: if we see a new DCPLB fault, return
	 * without doing anything.  Then, if we get the same fault again,
	 * handle it.
	 */
	P4 = R7;	/* Store EXCAUSE */

	GET_PDA(p5, r7);
	r7 = [p5 + PDA_LFRETX];
	r6 = retx;
	[p5 + PDA_LFRETX] = r6;
	cc = r6 == r7;
	if !cc jump _bfin_return_from_exception;
	/* fall through */
	R7 = P4;
	R6 = VEC_CPLB_M;	/* Data CPLB Miss */
	cc = R6 == R7;
	if cc jump _ex_dcplb_miss (BP);
#ifdef CONFIG_MPU
	R6 = VEC_CPLB_VL;	/* Data CPLB Violation */
	cc = R6 == R7;
	if cc jump _ex_dcplb_viol (BP);
#endif
	/* Handle Data CPLB Protection Violation
	 * and Data CPLB Multiple Hits - Linux Trap Zero
	 */
	jump _ex_trap_c;
ENDPROC(_ex_workaround_261)

#else
#ifdef CONFIG_MPU
#define _ex_dviol _ex_dcplb_viol
#else
#define _ex_dviol _ex_trap_c
#endif
#define _ex_dmiss _ex_dcplb_miss
#define _ex_dmult _ex_trap_c
#endif


ENTRY(_ex_dcplb_viol)
ENTRY(_ex_dcplb_miss)
ENTRY(_ex_icplb_miss)
	(R7:6,P5:4) = [sp++];
	/* We leave the previously pushed ASTAT on the stack.  */
	SAVE_CONTEXT_CPLB

	/* We must load R1 here, _before_ DEBUG_HWTRACE_SAVE, since that
	 * will change the stack pointer.  */
	R0 = SEQSTAT;
	R1 = SP;

	DEBUG_HWTRACE_SAVE(p5, r7)

	sp += -12;
	call _cplb_hdr;
	sp += 12;
	CC = R0 == 0;
	IF !CC JUMP _handle_bad_cplb;

#ifdef CONFIG_DEBUG_DOUBLEFAULT
	/* While we were processing this, did we double fault? */
	r7 = SEQSTAT;           /* reason code is in bit 5:0 */
	r6.l = lo(SEQSTAT_EXCAUSE);
	r6.h = hi(SEQSTAT_EXCAUSE);
	r7 = r7 & r6;
	r6 = 0x25;
	CC = R7 == R6;
	if CC JUMP _double_fault;
#endif

	DEBUG_HWTRACE_RESTORE(p5, r7)
	RESTORE_CONTEXT_CPLB
	ASTAT = [SP++];
	SP = EX_SCRATCH_REG;
	rtx;
ENDPROC(_ex_icplb_miss)

ENTRY(_ex_syscall)
	raise 15;		/* invoked by TRAP #0, for sys call */
	jump.s _bfin_return_from_exception;
ENDPROC(_ex_syscall)

ENTRY(_ex_single_step)
	/* If we just returned from an interrupt, the single step event is
	   for the RTI instruction.  */
	r7 = retx;
	r6 = reti;
	cc = r7 == r6;
	if cc jump _bfin_return_from_exception;

#ifdef CONFIG_KGDB
	/* Don't do single step in hardware exception handler */
        p5.l = lo(IPEND);
        p5.h = hi(IPEND);
	r6 = [p5];
	cc = bittst(r6, 4);
	if cc jump _bfin_return_from_exception;
	cc = bittst(r6, 5);
	if cc jump _bfin_return_from_exception;

	/* skip single step if current interrupt priority is higher than
	 * that of the first instruction, from which gdb starts single step */
	r6 >>= 6;
	r7 = 10;
.Lfind_priority_start:
	cc = bittst(r6, 0);
	if cc jump .Lfind_priority_done;
	r6 >>= 1;
	r7 += -1;
	cc = r7 == 0;
	if cc jump .Lfind_priority_done;
	jump.s .Lfind_priority_start;
.Lfind_priority_done:
	p4.l = _kgdb_single_step;
	p4.h = _kgdb_single_step;
	r6 = [p4];
	cc = r6 == 0;
	if cc jump .Ldo_single_step;
	r6 += -1;
	cc = r6 < r7;
	if cc jump 1f;
.Ldo_single_step:
#else
	/* If we were in user mode, do the single step normally.  */
	p5.l = lo(IPEND);
	p5.h = hi(IPEND);
	r6 = [p5];
	r7 = 0xffe0 (z);
	r7 = r7 & r6;
	cc = r7 == 0;
	if !cc jump 1f;
#endif
#ifdef CONFIG_EXACT_HWERR
	/* Read the ILAT, and to check to see if the process we are
	 * single stepping caused a previous hardware error
	 * If so, do not single step, (which lowers to IRQ5, and makes
	 * us miss the error).
	 */
	p5.l = lo(ILAT);
	p5.h = hi(ILAT);
	r7 = [p5];
	cc = bittst(r7, EVT_IVHW_P);
	if cc jump 1f;
#endif
	/* Single stepping only a single instruction, so clear the trace
	 * bit here.  */
	r7 = syscfg;
	bitclr (r7, SYSCFG_SSSTEP_P);
	syscfg = R7;
	jump _ex_trap_c;

1:
	/*
	 * We were in an interrupt handler.  By convention, all of them save
	 * SYSCFG with their first instruction, so by checking whether our
	 * RETX points at the entry point, we can determine whether to allow
	 * a single step, or whether to clear SYSCFG.
	 *
	 * First, find out the interrupt level and the event vector for it.
	 */
	p5.l = lo(EVT0);
	p5.h = hi(EVT0);
	p5 += -4;
2:
	r7 = rot r7 by -1;
	p5 += 4;
	if !cc jump 2b;

	/* What we actually do is test for the _second_ instruction in the
	 * IRQ handler.  That way, if there are insns following the restore
	 * of SYSCFG after leaving the handler, we will not turn off SYSCFG
	 * for them.  */

	r7 = [p5];
	r7 += 2;
	r6 = RETX;
	cc = R7 == R6;
	if !cc jump _bfin_return_from_exception;

	r7 = syscfg;
	bitclr (r7, SYSCFG_SSSTEP_P);	/* Turn off single step */
	syscfg = R7;

	/* Fall through to _bfin_return_from_exception.  */
ENDPROC(_ex_single_step)

ENTRY(_bfin_return_from_exception)
#if ANOMALY_05000257
	R7=LC0;
	LC0=R7;
	R7=LC1;
	LC1=R7;
#endif

#ifdef CONFIG_DEBUG_DOUBLEFAULT
	/* While we were processing the current exception,
	 * did we cause another, and double fault?
	 */
	r7 = SEQSTAT;           /* reason code is in bit 5:0 */
	r6.l = lo(SEQSTAT_EXCAUSE);
	r6.h = hi(SEQSTAT_EXCAUSE);
	r7 = r7 & r6;
	r6 = VEC_UNCOV;
	CC = R7 == R6;
	if CC JUMP _double_fault;
#endif

	(R7:6,P5:4) = [sp++];
	ASTAT = [sp++];
	sp = EX_SCRATCH_REG;
	rtx;
ENDPROC(_bfin_return_from_exception)

ENTRY(_handle_bad_cplb)
	DEBUG_HWTRACE_RESTORE(p5, r7)
	/* To get here, we just tried and failed to change a CPLB
	 * so, handle things in trap_c (C code), by lowering to
	 * IRQ5, just like we normally do. Since this is not a
	 * "normal" return path, we have a do a lot of stuff to
	 * the stack to get ready so, we can fall through - we
	 * need to make a CPLB exception look like a normal exception
	 */
	RESTORE_CONTEXT_CPLB
	/* ASTAT is still on the stack, where it is needed.  */
	[--sp] = (R7:6,P5:4);

ENTRY(_ex_replaceable)
	nop;

ENTRY(_ex_trap_c)
	/* The only thing that has been saved in this context is
	 * (R7:6,P5:4), ASTAT & SP - don't use anything else
	 */

	GET_PDA(p5, r6);

	/* Make sure we are not in a double fault */
	p4.l = lo(IPEND);
	p4.h = hi(IPEND);
	r7 = [p4];
	CC = BITTST (r7, 5);
	if CC jump _double_fault;
	[p5 + PDA_EXIPEND] = r7;

	/* Call C code (trap_c) to handle the exception, which most
	 * likely involves sending a signal to the current process.
	 * To avoid double faults, lower our priority to IRQ5 first.
	 */
	r7.h = _exception_to_level5;
	r7.l = _exception_to_level5;
	p4.l = lo(EVT5);
	p4.h = hi(EVT5);
	[p4] = r7;
	csync;

	/*
	 * Save these registers, as they are only valid in exception context
	 *  (where we are now - as soon as we defer to IRQ5, they can change)
	 * DCPLB_STATUS and ICPLB_STATUS are also only valid in EVT3,
	 * but they are not very interesting, so don't save them
	 */

	p4.l = lo(DCPLB_FAULT_ADDR);
	p4.h = hi(DCPLB_FAULT_ADDR);
	r7 = [p4];
	[p5 + PDA_DCPLB] = r7;

	p4.l = lo(ICPLB_FAULT_ADDR);
	p4.h = hi(ICPLB_FAULT_ADDR);
	r6 = [p4];
	[p5 + PDA_ICPLB] = r6;

	r6 = retx;
	[p5 + PDA_RETX] = r6;

	r6 = SEQSTAT;
	[p5 + PDA_SEQSTAT] = r6;

	/* Save the state of single stepping */
	r6 = SYSCFG;
	[p5 + PDA_SYSCFG] = r6;
	/* Clear it while we handle the exception in IRQ5 mode */
	BITCLR(r6, SYSCFG_SSSTEP_P);
	SYSCFG = r6;

	/* Save the current IMASK, since we change in order to jump to level 5 */
	cli r6;
	[p5 + PDA_EXIMASK] = r6;

	p4.l = lo(SAFE_USER_INSTRUCTION);
	p4.h = hi(SAFE_USER_INSTRUCTION);
	retx = p4;

	/* Disable all interrupts, but make sure level 5 is enabled so
	 * we can switch to that level.
	 */
	r6 = 0x3f;
	sti r6;

	/* In case interrupts are disabled IPEND[4] (global interrupt disable bit)
	 * clear it (re-enabling interrupts again) by the special sequence of pushing
	 * RETI onto the stack.  This way we can lower ourselves to IVG5 even if the
	 * exception was taken after the interrupt handler was called but before it
	 * got a chance to enable global interrupts itself.
	 */
	[--sp] = reti;
	sp += 4;

	raise 5;
	jump.s _bfin_return_from_exception;
ENDPROC(_ex_trap_c)

/* We just realized we got an exception, while we were processing a different
 * exception. This is a unrecoverable event, so crash.
 * Note: this cannot be ENTRY() as we jump here with "if cc jump" ...
 */
ENTRY(_double_fault)
	/* Turn caches & protection off, to ensure we don't get any more
	 * double exceptions
	 */

	P4.L = LO(IMEM_CONTROL);
	P4.H = HI(IMEM_CONTROL);

	R5 = [P4];              /* Control Register*/
	BITCLR(R5,ENICPLB_P);
	CSYNC;          /* Disabling of CPLBs should be proceeded by a CSYNC */
	[P4] = R5;
	SSYNC;

	P4.L = LO(DMEM_CONTROL);
	P4.H = HI(DMEM_CONTROL);
	R5 = [P4];
	BITCLR(R5,ENDCPLB_P);
	CSYNC;          /* Disabling of CPLBs should be proceeded by a CSYNC */
	[P4] = R5;
	SSYNC;

	/* Fix up the stack */
	(R7:6,P5:4) = [sp++];
	ASTAT = [sp++];
	SP = EX_SCRATCH_REG;

	/* We should be out of the exception stack, and back down into
	 * kernel or user space stack
	 */
	SAVE_ALL_SYS

	/* The dumping functions expect the return address in the RETI
	 * slot.  */
	r6 = retx;
	[sp + PT_PC] = r6;

	r0 = sp;        /* stack frame pt_regs pointer argument ==> r0 */
	SP += -12;
	pseudo_long_call _double_fault_c, p5;
	SP += 12;
.L_double_fault_panic:
        JUMP .L_double_fault_panic

ENDPROC(_double_fault)

ENTRY(_exception_to_level5)
	SAVE_ALL_SYS

	GET_PDA(p5, r7);        /* Fetch current PDA */
	r6 = [p5 + PDA_RETX];
	[sp + PT_PC] = r6;

	r6 = [p5 + PDA_SYSCFG];
	[sp + PT_SYSCFG] = r6;

	r6 = [p5 + PDA_SEQSTAT]; /* Read back seqstat */
	[sp + PT_SEQSTAT] = r6;

	/* Restore the hardware error vector.  */
	r7.h = _evt_ivhw;
	r7.l = _evt_ivhw;
	p4.l = lo(EVT5);
	p4.h = hi(EVT5);
	[p4] = r7;
	csync;

#ifdef CONFIG_DEBUG_DOUBLEFAULT
	/* Now that we have the hardware error vector programmed properly
	 * we can re-enable interrupts (IPEND[4]), so if the _trap_c causes
	 * another hardware error, we can catch it (self-nesting).
	 */
	[--sp] = reti;
	sp += 4;
#endif

	r7 = [p5 + PDA_EXIPEND]	/* Read the IPEND from the Exception state */
	[sp + PT_IPEND] = r7;   /* Store IPEND onto the stack */

	r0 = sp; 	/* stack frame pt_regs pointer argument ==> r0 */
	SP += -12;
	pseudo_long_call _trap_c, p4;
	SP += 12;

	/* If interrupts were off during the exception (IPEND[4] = 1), turn them off
	 * before we return.
	 */
	CC = BITTST(r7, EVT_IRPTEN_P)
	if !CC jump 1f;
	/* this will load a random value into the reti register - but that is OK,
	 * since we do restore it to the correct value in the 'RESTORE_ALL_SYS' macro
	 */
	sp += -4;
	reti = [sp++];
1:
	/* restore the interrupt mask (IMASK) */
	r6 = [p5 + PDA_EXIMASK];
	sti r6;

	call _ret_from_exception;
	RESTORE_ALL_SYS
	rti;
ENDPROC(_exception_to_level5)

ENTRY(_trap) /* Exception: 4th entry into system event table(supervisor mode)*/
	/* Since the kernel stack can be anywhere, it's not guaranteed to be
	 * covered by a CPLB.  Switch to an exception stack; use RETN as a
	 * scratch register (for want of a better option).
	 */
	EX_SCRATCH_REG = sp;
	GET_PDA_SAFE(sp);
	sp = [sp + PDA_EXSTACK];
	/* Try to deal with syscalls quickly.  */
	[--sp] = ASTAT;
	[--sp] = (R7:6,P5:4);

	ANOMALY_283_315_WORKAROUND(p5, r7)

#ifdef CONFIG_EXACT_HWERR
	/* Make sure all pending read/writes complete. This will ensure any
	 * accesses which could cause hardware errors completes, and signal
	 * the the hardware before we do something silly, like crash the
	 * kernel. We don't need to work around anomaly 05000312, since
	 * we are already atomic
	 */
	ssync;
#endif

#ifdef CONFIG_DEBUG_DOUBLEFAULT
	/*
	 * Save these registers, as they are only valid in exception context
	 * (where we are now - as soon as we defer to IRQ5, they can change)
	 * DCPLB_STATUS and ICPLB_STATUS are also only valid in EVT3,
	 * but they are not very interesting, so don't save them
	 */

	GET_PDA(p5, r7);
	p4.l = lo(DCPLB_FAULT_ADDR);
	p4.h = hi(DCPLB_FAULT_ADDR);
	r7 = [p4];
	[p5 + PDA_DF_DCPLB] = r7;

	p4.l = lo(ICPLB_FAULT_ADDR);
	p4.h = hi(ICPLB_FAULT_ADDR);
	r7 = [p4];
	[p5 + PDA_DF_ICPLB] = r7;

	r7 = retx;
	[p5 + PDA_DF_RETX] = r7;

	r7 = SEQSTAT;		/* reason code is in bit 5:0 */
	[p5 + PDA_DF_SEQSTAT] = r7;
#else
	r7 = SEQSTAT;           /* reason code is in bit 5:0 */
#endif
	r6.l = lo(SEQSTAT_EXCAUSE);
	r6.h = hi(SEQSTAT_EXCAUSE);
	r7 = r7 & r6;
	p5.h = _ex_table;
	p5.l = _ex_table;
	p4 = r7;
	p5 = p5 + (p4 << 2);
	p4 = [p5];
	jump (p4);

.Lbadsys:
	r7 = -ENOSYS; 		/* signextending enough */
	[sp + PT_R0] = r7;	/* return value from system call */
	jump .Lsyscall_really_exit;
ENDPROC(_trap)

ENTRY(_kernel_execve)
	link SIZEOF_PTREGS;
	p0 = sp;
	r3 = SIZEOF_PTREGS / 4;
	r4 = 0(x);
.Lclear_regs:
	[p0++] = r4;
	r3 += -1;
	cc = r3 == 0;
	if !cc jump .Lclear_regs (bp);

	p0 = sp;
	sp += -16;
	[sp + 12] = p0;
	pseudo_long_call _do_execve, p5;
	SP += 16;
	cc = r0 == 0;
	if ! cc jump .Lexecve_failed;
	/* Success.  Copy our temporary pt_regs to the top of the kernel
	 * stack and do a normal exception return.
	 */
	r1 = sp;
	r0 = (-KERNEL_STACK_SIZE) (x);
	r1 = r1 & r0;
	p2 = r1;
	p3 = [p2];
	r0 = KERNEL_STACK_SIZE - 4 (z);
	p1 = r0;
	p1 = p1 + p2;

	p0 = fp;
	r4 = [p0--];
	r3 = SIZEOF_PTREGS / 4;
.Lcopy_regs:
	r4 = [p0--];
	[p1--] = r4;
	r3 += -1;
	cc = r3 == 0;
	if ! cc jump .Lcopy_regs (bp);

	r0 = (KERNEL_STACK_SIZE - SIZEOF_PTREGS) (z);
	p1 = r0;
	p1 = p1 + p2;
	sp = p1;
	r0 = syscfg;
	[SP + PT_SYSCFG] = r0;
	[p3 + (TASK_THREAD + THREAD_KSP)] = sp;

	RESTORE_CONTEXT;
	rti;
.Lexecve_failed:
	unlink;
	rts;
ENDPROC(_kernel_execve)

ENTRY(_system_call)
	/* Store IPEND */
	p2.l = lo(IPEND);
	p2.h = hi(IPEND);
	csync;
	r0 = [p2];
	[sp + PT_IPEND] = r0;

	/* Store RETS for now */
	r0 = rets;
	[sp + PT_RESERVED] = r0;
	/* Set the stack for the current process */
	r7 = sp;
	r6.l = lo(ALIGN_PAGE_MASK);
	r6.h = hi(ALIGN_PAGE_MASK);
	r7 = r7 & r6;  		/* thread_info */
	p2 = r7;
	p2 = [p2];

	[p2+(TASK_THREAD+THREAD_KSP)] = sp;
#ifdef CONFIG_IPIPE
	r0 = sp;
	SP += -12;
	pseudo_long_call ___ipipe_syscall_root, p0;
	SP += 12;
	cc = r0 == 1;
	if cc jump .Lsyscall_really_exit;
	cc = r0 == -1;
	if cc jump .Lresume_userspace;
	r3 = [sp + PT_R3];
	r4 = [sp + PT_R4];
	p0 = [sp + PT_ORIG_P0];
#endif /* CONFIG_IPIPE */

	/* are we tracing syscalls?*/
	r7 = sp;
	r6.l = lo(ALIGN_PAGE_MASK);
	r6.h = hi(ALIGN_PAGE_MASK);
	r7 = r7 & r6;
	p2 = r7;
	r7 = [p2+TI_FLAGS];
	CC = BITTST(r7,TIF_SYSCALL_TRACE);
	if CC JUMP _sys_trace;
	CC = BITTST(r7,TIF_SINGLESTEP);
	if CC JUMP _sys_trace;

	/* Make sure the system call # is valid */
	p4 = __NR_syscall;
	/* System call number is passed in P0 */
	cc = p4 <= p0;
	if cc jump .Lbadsys;

	/* Execute the appropriate system call */

	p4 = p0;
	p5.l = _sys_call_table;
	p5.h = _sys_call_table;
	p5 = p5 + (p4 << 2);
	r0 = [sp + PT_R0];
	r1 = [sp + PT_R1];
	r2 = [sp + PT_R2];
	p5 = [p5];

	[--sp] = r5;
	[--sp] = r4;
	[--sp] = r3;
	SP += -12;
	call (p5);
	SP += 24;
	[sp + PT_R0] = r0;

.Lresume_userspace:
	r7 = sp;
	r4.l = lo(ALIGN_PAGE_MASK);
	r4.h = hi(ALIGN_PAGE_MASK);
	r7 = r7 & r4;		/* thread_info->flags */
	p5 = r7;
.Lresume_userspace_1:
	/* Disable interrupts.  */
	[--sp] = reti;
	reti = [sp++];

	r7 = [p5 + TI_FLAGS];
	r4.l = lo(_TIF_WORK_MASK);
	r4.h = hi(_TIF_WORK_MASK);
	r7 =  r7 & r4;

.Lsyscall_resched:
#ifdef CONFIG_IPIPE
	cc = BITTST(r7, TIF_IRQ_SYNC);
	if !cc jump .Lsyscall_no_irqsync;
	/*
	 * Clear IPEND[4] manually to undo what resume_userspace_1 just did;
	 * we need this so that high priority domain interrupts may still
	 * preempt the current domain while the pipeline log is being played
	 * back.
	 */
	[--sp] = reti;
	SP += 4; /* don't merge with next insn to keep the pattern obvious */
	SP += -12;
	pseudo_long_call ___ipipe_sync_root, p4;
	SP += 12;
	jump .Lresume_userspace_1;
.Lsyscall_no_irqsync:
#endif
	cc = BITTST(r7, TIF_NEED_RESCHED);
	if !cc jump .Lsyscall_sigpending;

	/* Reenable interrupts.  */
	[--sp] = reti;
	sp += 4;

	SP += -12;
	pseudo_long_call _schedule, p4;
	SP += 12;

	jump .Lresume_userspace_1;

.Lsyscall_sigpending:
	cc = BITTST(r7, TIF_RESTORE_SIGMASK);
	if cc jump .Lsyscall_do_signals;
	cc = BITTST(r7, TIF_SIGPENDING);
	if cc jump .Lsyscall_do_signals;
	cc = BITTST(r7, TIF_NOTIFY_RESUME);
	if !cc jump .Lsyscall_really_exit;
.Lsyscall_do_signals:
	/* Reenable interrupts.  */
	[--sp] = reti;
	sp += 4;

	r0 = sp;
	SP += -12;
	pseudo_long_call _do_notify_resume, p5;
	SP += 12;

.Lsyscall_really_exit:
	r5 = [sp + PT_RESERVED];
	rets = r5;
	rts;
ENDPROC(_system_call)

/* Do not mark as ENTRY() to avoid error in assembler ...
 * this symbol need not be global anyways, so ...
 */
_sys_trace:
	r0 = sp;
	pseudo_long_call _syscall_trace_enter, p5;

	/* Make sure the system call # is valid */
	p4 = [SP + PT_P0];
	p3 = __NR_syscall;
	cc = p3 <= p4;
	r0 = -ENOSYS;
	if cc jump .Lsys_trace_badsys;

	/* Execute the appropriate system call */
	p5.l = _sys_call_table;
	p5.h = _sys_call_table;
	p5 = p5 + (p4 << 2);
	r0 = [sp + PT_R0];
	r1 = [sp + PT_R1];
	r2 = [sp + PT_R2];
	r3 = [sp + PT_R3];
	r4 = [sp + PT_R4];
	r5 = [sp + PT_R5];
	p5 = [p5];

	[--sp] = r5;
	[--sp] = r4;
	[--sp] = r3;
	SP += -12;
	call (p5);
	SP += 24;
.Lsys_trace_badsys:
	[sp + PT_R0] = r0;

	r0 = sp;
	pseudo_long_call _syscall_trace_leave, p5;
	jump .Lresume_userspace;
ENDPROC(_sys_trace)

ENTRY(_resume)
	/*
	 * Beware - when entering resume, prev (the current task) is
	 * in r0, next (the new task) is in r1.
	 */
	p0 = r0;
	p1 = r1;
	[--sp] = rets;
	[--sp] = fp;
	[--sp] = (r7:4, p5:3);

	/* save usp */
	p2 = usp;
	[p0+(TASK_THREAD+THREAD_USP)] = p2;

	/* save current kernel stack pointer */
	[p0+(TASK_THREAD+THREAD_KSP)] = sp;

	/* save program counter */
	r1.l = _new_old_task;
	r1.h = _new_old_task;
	[p0+(TASK_THREAD+THREAD_PC)] = r1;

	/* restore the kernel stack pointer */
	sp = [p1+(TASK_THREAD+THREAD_KSP)];

	/* restore user stack pointer */
	p0 = [p1+(TASK_THREAD+THREAD_USP)];
	usp = p0;

	/* restore pc */
	p0 = [p1+(TASK_THREAD+THREAD_PC)];
	jump (p0);

	/*
	 * Following code actually lands up in a new (old) task.
	 */

_new_old_task:
	(r7:4, p5:3) = [sp++];
	fp = [sp++];
	rets = [sp++];

	/*
	 * When we come out of resume, r0 carries "old" task, because we are
	 * in "new" task.
	 */
	rts;
ENDPROC(_resume)

ENTRY(_ret_from_exception)
#ifdef CONFIG_IPIPE
	p2.l = _ipipe_percpu_domain;
	p2.h = _ipipe_percpu_domain;
	r0.l = _ipipe_root;
	r0.h = _ipipe_root;
	r2 = [p2];
	cc = r0 == r2;
	if !cc jump 4f;  /* not on behalf of the root domain, get out */
#endif /* CONFIG_IPIPE */
	p2.l = lo(IPEND);
	p2.h = hi(IPEND);

	csync;
	r0 = [p2];
	[sp + PT_IPEND] = r0;

1:
	r2 = LO(~0x37) (Z);
	r0 = r2 & r0;
	cc = r0 == 0;
	if !cc jump 4f;	/* if not return to user mode, get out */

	/* Make sure any pending system call or deferred exception
	 * return in ILAT for this process to get executed, otherwise
	 * in case context switch happens, system call of
	 * first process (i.e in ILAT) will be carried
	 * forward to the switched process
	 */

	p2.l = lo(ILAT);
	p2.h = hi(ILAT);
	r0 = [p2];
	r1 = (EVT_IVG14 | EVT_IVG15) (z);
	r0 = r0 & r1;
	cc = r0 == 0;
	if !cc jump 5f;

	/* Set the stack for the current process */
	r7 = sp;
	r4.l = lo(ALIGN_PAGE_MASK);
	r4.h = hi(ALIGN_PAGE_MASK);
	r7 = r7 & r4;		/* thread_info->flags */
	p5 = r7;
	r7 = [p5 + TI_FLAGS];
	r4.l = lo(_TIF_WORK_MASK);
	r4.h = hi(_TIF_WORK_MASK);
	r7 =  r7 & r4;
	cc = r7 == 0;
	if cc jump 4f;

	p0.l = lo(EVT15);
	p0.h = hi(EVT15);
	p1.l = _schedule_and_signal;
	p1.h = _schedule_and_signal;
	[p0] = p1;
	csync;
	raise 15;		/* raise evt15 to do signal or reschedule */
4:
	r0 = syscfg;
	bitclr(r0, SYSCFG_SSSTEP_P);		/* Turn off single step */
	syscfg = r0;
5:
	rts;
ENDPROC(_ret_from_exception)

#if defined(CONFIG_PREEMPT)

ENTRY(_up_to_irq14)
#if ANOMALY_05000281 || ANOMALY_05000461
	r0.l = lo(SAFE_USER_INSTRUCTION);
	r0.h = hi(SAFE_USER_INSTRUCTION);
	reti = r0;
#endif

#ifdef CONFIG_DEBUG_HWERR
	/* enable irq14 & hwerr interrupt, until we transition to _evt_evt14 */
	r0 = (EVT_IVG14 | EVT_IVHW | EVT_IRPTEN | EVT_EVX | EVT_NMI | EVT_RST | EVT_EMU);
#else
	/* Only enable irq14 interrupt, until we transition to _evt_evt14 */
	r0 = (EVT_IVG14 | EVT_IRPTEN | EVT_EVX | EVT_NMI | EVT_RST | EVT_EMU);
#endif
	sti r0;

	p0.l = lo(EVT14);
	p0.h = hi(EVT14);
	p1.l = _evt_up_evt14;
	p1.h = _evt_up_evt14;
	[p0] = p1;
	csync;

	raise 14;
1:
	jump 1b;
ENDPROC(_up_to_irq14)

ENTRY(_evt_up_evt14)
#ifdef CONFIG_DEBUG_HWERR
	r0 = (EVT_IVHW | EVT_IRPTEN | EVT_EVX | EVT_NMI | EVT_RST | EVT_EMU);
	sti r0;
#else
	cli r0;
#endif
#ifdef CONFIG_TRACE_IRQFLAGS
	[--sp] = rets;
	sp += -12;
	call _trace_hardirqs_off;
	sp += 12;
	rets = [sp++];
#endif
	[--sp] = RETI;
	SP += 4;

	/* restore normal evt14 */
	p0.l = lo(EVT14);
	p0.h = hi(EVT14);
	p1.l = _evt_evt14;
	p1.h = _evt_evt14;
	[p0] = p1;
	csync;

	rts;
ENDPROC(_evt_up_evt14)

#endif

#ifdef CONFIG_IPIPE

_resume_kernel_from_int:
	r1 = LO(~0x8000) (Z);
	r1 = r0 & r1;
	r0 = 1;
	r0 = r1 - r0;
	r2 = r1 & r0;
	cc = r2 == 0;
	/* Sync the root stage only from the outer interrupt level. */
	if !cc jump .Lnosync;
	r0.l = ___ipipe_sync_root;
	r0.h = ___ipipe_sync_root;
	[--sp] = reti;
	[--sp] = rets;
	[--sp] = ( r7:4, p5:3 );
	SP += -12;
	call ___ipipe_call_irqtail
	SP += 12;
	( r7:4, p5:3 ) = [sp++];
	rets = [sp++];
	reti = [sp++];
.Lnosync:
	rts
#elif defined(CONFIG_PREEMPT)

_resume_kernel_from_int:
	/* check preempt_count */
	r7 = sp;
	r4.l = lo(ALIGN_PAGE_MASK);
	r4.h = hi(ALIGN_PAGE_MASK);
	r7 = r7 & r4;
	p5 = r7;
	r7 = [p5 + TI_PREEMPT];
	cc = r7 == 0x0;
	if !cc jump .Lreturn_to_kernel;
.Lneed_schedule:
	r7 = [p5 + TI_FLAGS];
	r4.l = lo(_TIF_WORK_MASK);
	r4.h = hi(_TIF_WORK_MASK);
	r7 =  r7 & r4;
	cc = BITTST(r7, TIF_NEED_RESCHED);
	if !cc jump .Lreturn_to_kernel;
	/*
	 * let schedule done at level 15, otherwise sheduled process will run
	 * at high level and block low level interrupt
	 */
	r6 = reti;  /* save reti */
	r5.l = .Lkernel_schedule;
	r5.h = .Lkernel_schedule;
	reti = r5;
	rti;
.Lkernel_schedule:
	[--sp] = rets;
	sp += -12;
	pseudo_long_call _preempt_schedule_irq, p4;
	sp += 12;
	rets = [sp++];

	[--sp] = rets;
	sp += -12;
	/* up to irq14 so that reti after restore_all can return to irq15(kernel) */
	pseudo_long_call _up_to_irq14, p4;
	sp += 12;
	rets = [sp++];

	reti = r6; /* restore reti so that origin process can return to interrupted point */

	jump .Lneed_schedule;
#else

#define _resume_kernel_from_int	.Lreturn_to_kernel
#endif

ENTRY(_return_from_int)
	/* If someone else already raised IRQ 15, do nothing.  */
	csync;
	p2.l = lo(ILAT);
	p2.h = hi(ILAT);
	r0 = [p2];
	cc = bittst (r0, EVT_IVG15_P);
	if cc jump .Lreturn_to_kernel;

	/* if not return to user mode, get out */
	p2.l = lo(IPEND);
	p2.h = hi(IPEND);
	r0 = [p2];
	r1 = 0x17(Z);
	r2 = ~r1;
	r2.h = 0;
	r0 = r2 & r0;
	r1 = 1;
	r1 = r0 - r1;
	r2 = r0 & r1;
	cc = r2 == 0;
	if !cc jump _resume_kernel_from_int;

	/* Lower the interrupt level to 15.  */
	p0.l = lo(EVT15);
	p0.h = hi(EVT15);
	p1.l = _schedule_and_signal_from_int;
	p1.h = _schedule_and_signal_from_int;
	[p0] = p1;
	csync;
#if ANOMALY_05000281 || ANOMALY_05000461
	r0.l = lo(SAFE_USER_INSTRUCTION);
	r0.h = hi(SAFE_USER_INSTRUCTION);
	reti = r0;
#endif
	r0 = 0x801f (z);
	STI r0;
	raise 15;	/* raise evt15 to do signal or reschedule */
	rti;
.Lreturn_to_kernel:
	rts;
ENDPROC(_return_from_int)

ENTRY(_lower_to_irq14)
#if ANOMALY_05000281 || ANOMALY_05000461
	r0.l = lo(SAFE_USER_INSTRUCTION);
	r0.h = hi(SAFE_USER_INSTRUCTION);
	reti = r0;
#endif

#ifdef CONFIG_DEBUG_HWERR
	/* enable irq14 & hwerr interrupt, until we transition to _evt_evt14 */
	r0 = (EVT_IVG14 | EVT_IVHW | EVT_IRPTEN | EVT_EVX | EVT_NMI | EVT_RST | EVT_EMU);
#else
	/* Only enable irq14 interrupt, until we transition to _evt_evt14 */
	r0 = (EVT_IVG14 | EVT_IRPTEN | EVT_EVX | EVT_NMI | EVT_RST | EVT_EMU);
#endif
	sti r0;
	raise 14;
	rti;
ENDPROC(_lower_to_irq14)

ENTRY(_evt_evt14)
#ifdef CONFIG_DEBUG_HWERR
	r0 = (EVT_IVHW | EVT_IRPTEN | EVT_EVX | EVT_NMI | EVT_RST | EVT_EMU);
	sti r0;
#else
	cli r0;
#endif
#ifdef CONFIG_TRACE_IRQFLAGS
	[--sp] = rets;
	sp += -12;
	call _trace_hardirqs_off;
	sp += 12;
	rets = [sp++];
#endif
	[--sp] = RETI;
	SP += 4;
	rts;
ENDPROC(_evt_evt14)

ENTRY(_schedule_and_signal_from_int)
	/* To end up here, vector 15 was changed - so we have to change it
	 * back.
	 */
	p0.l = lo(EVT15);
	p0.h = hi(EVT15);
	p1.l = _evt_system_call;
	p1.h = _evt_system_call;
	[p0] = p1;
	csync;

	/* Set orig_p0 to -1 to indicate this isn't the end of a syscall.  */
	r0 = -1 (x);
	[sp + PT_ORIG_P0] = r0;

	p1 = rets;
	[sp + PT_RESERVED] = p1;

#ifdef CONFIG_TRACE_IRQFLAGS
	/* trace_hardirqs_on() checks if all irqs are disabled. But here IRQ 15
	 * is turned on, so disable all irqs. */
	cli r0;
	sp += -12;
	call _trace_hardirqs_on;
	sp += 12;
#endif
#ifdef CONFIG_SMP
	GET_PDA(p0, r0); 	/* Fetch current PDA (can't migrate to other CPU here) */
	r0 = [p0 + PDA_IRQFLAGS];
#else
	p0.l = _bfin_irq_flags;
	p0.h = _bfin_irq_flags;
	r0 = [p0];
#endif
	sti r0;

	/* finish the userspace "atomic" functions for it */
	r1 = FIXED_CODE_END;
	r2 = [sp + PT_PC];
	cc = r1 <= r2;
	if cc jump .Lresume_userspace (bp);

	r0 = sp;
	sp += -12;

	pseudo_long_call _finish_atomic_sections, p5;
	sp += 12;
	jump.s .Lresume_userspace;
ENDPROC(_schedule_and_signal_from_int)

ENTRY(_schedule_and_signal)
	SAVE_CONTEXT_SYSCALL
	/* To end up here, vector 15 was changed - so we have to change it
	 * back.
	 */
	p0.l = lo(EVT15);
	p0.h = hi(EVT15);
	p1.l = _evt_system_call;
	p1.h = _evt_system_call;
	[p0] = p1;
	csync;
	p0.l = 1f;
	p0.h = 1f;
	[sp + PT_RESERVED] = P0;
	call .Lresume_userspace;
1:
	RESTORE_CONTEXT
	rti;
ENDPROC(_schedule_and_signal)

/* We handle this 100% in exception space - to reduce overhead
 * Only potiential problem is if the software buffer gets swapped out of the
 * CPLB table - then double fault. - so we don't let this happen in other places
 */
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
ENTRY(_ex_trace_buff_full)
	[--sp] = P3;
	[--sp] = P2;
	[--sp] = LC0;
	[--sp] = LT0;
	[--sp] = LB0;
	P5.L = _trace_buff_offset;
	P5.H = _trace_buff_offset;
	P3 = [P5];              /* trace_buff_offset */
	P5.L = lo(TBUFSTAT);
	P5.H = hi(TBUFSTAT);
	R7 = [P5];
	R7 <<= 1;               /* double, since we need to read twice */
	LC0 = R7;
	R7 <<= 2;               /* need to shift over again,
				 * to get the number of bytes */
	P5.L = lo(TBUF);
	P5.H = hi(TBUF);
	R6 = ((1 << CONFIG_DEBUG_BFIN_HWTRACE_EXPAND_LEN)*1024) - 1;

	P2 = R7;
	P3 = P3 + P2;
	R7 = P3;
	R7 = R7 & R6;
	P3 = R7;
	P2.L = _trace_buff_offset;
	P2.H = _trace_buff_offset;
	[P2] = P3;

	P2.L = _software_trace_buff;
	P2.H = _software_trace_buff;

	LSETUP (.Lstart, .Lend) LC0;
.Lstart:
	R7 = [P5];      /* read TBUF */
	P4 = P3 + P2;
	[P4] = R7;
	P3 += -4;
	R7 = P3;
	R7 = R7 & R6;
.Lend:
	P3 = R7;

	LB0 = [sp++];
	LT0 = [sp++];
	LC0 = [sp++];
	P2 = [sp++];
	P3 = [sp++];
	jump _bfin_return_from_exception;
ENDPROC(_ex_trace_buff_full)

#if CONFIG_DEBUG_BFIN_HWTRACE_EXPAND_LEN == 4
.data
#else
.section .l1.data.B
#endif /* CONFIG_DEBUG_BFIN_HWTRACE_EXPAND_LEN */
ENTRY(_trace_buff_offset)
        .long 0;
ALIGN
ENTRY(_software_trace_buff)
	.rept ((1 << CONFIG_DEBUG_BFIN_HWTRACE_EXPAND_LEN)*256);
	.long 0
	.endr
#endif /* CONFIG_DEBUG_BFIN_HWTRACE_EXPAND */

#if CONFIG_EARLY_PRINTK
__INIT
ENTRY(_early_trap)
	SAVE_ALL_SYS
	trace_buffer_stop(p0,r0);

	ANOMALY_283_315_WORKAROUND(p4, r5)

	/* Turn caches off, to ensure we don't get double exceptions */

	P4.L = LO(IMEM_CONTROL);
	P4.H = HI(IMEM_CONTROL);

	R5 = [P4];              /* Control Register*/
	BITCLR(R5,ENICPLB_P);
	CSYNC;          /* Disabling of CPLBs should be proceeded by a CSYNC */
	[P4] = R5;
	SSYNC;

	P4.L = LO(DMEM_CONTROL);
	P4.H = HI(DMEM_CONTROL);
	R5 = [P4];
	BITCLR(R5,ENDCPLB_P);
	CSYNC;          /* Disabling of CPLBs should be proceeded by a CSYNC */
	[P4] = R5;
	SSYNC;

	r0 = sp;        /* stack frame pt_regs pointer argument ==> r0 */
	r1 = RETX;

	SP += -12;
	call _early_trap_c;
	SP += 12;
ENDPROC(_early_trap)
__FINIT
#endif /* CONFIG_EARLY_PRINTK */

/*
 * Put these in the kernel data section - that should always be covered by
 * a CPLB. This is needed to ensure we don't get double fault conditions
 */

#ifdef CONFIG_SYSCALL_TAB_L1
.section .l1.data
#else
.data
#endif

ENTRY(_ex_table)
	/* entry for each EXCAUSE[5:0]
	 * This table must be in sync with the table in ./kernel/traps.c
	 * EXCPT instruction can provide 4 bits of EXCAUSE, allowing 16 to be user defined
	 */
	.long _ex_syscall       /* 0x00 - User Defined - Linux Syscall */
	.long _ex_trap_c        /* 0x01 - User Defined - Software breakpoint */
#ifdef	CONFIG_KGDB
	.long _ex_trap_c	/* 0x02 - User Defined - KGDB initial connection
							 and break signal trap */
#else
	.long _ex_replaceable   /* 0x02 - User Defined */
#endif
	.long _ex_trap_c        /* 0x03 - User Defined - userspace stack overflow */
	.long _ex_trap_c        /* 0x04 - User Defined - dump trace buffer */
	.long _ex_replaceable   /* 0x05 - User Defined */
	.long _ex_replaceable   /* 0x06 - User Defined */
	.long _ex_replaceable   /* 0x07 - User Defined */
	.long _ex_replaceable   /* 0x08 - User Defined */
	.long _ex_replaceable   /* 0x09 - User Defined */
	.long _ex_replaceable   /* 0x0A - User Defined */
	.long _ex_replaceable   /* 0x0B - User Defined */
	.long _ex_replaceable   /* 0x0C - User Defined */
	.long _ex_replaceable   /* 0x0D - User Defined */
	.long _ex_replaceable   /* 0x0E - User Defined */
	.long _ex_replaceable   /* 0x0F - User Defined */
	.long _ex_single_step   /* 0x10 - HW Single step */
#ifdef CONFIG_DEBUG_BFIN_HWTRACE_EXPAND
	.long _ex_trace_buff_full /* 0x11 - Trace Buffer Full */
#else
	.long _ex_trap_c        /* 0x11 - Trace Buffer Full */
#endif
	.long _ex_trap_c        /* 0x12 - Reserved */
	.long _ex_trap_c        /* 0x13 - Reserved */
	.long _ex_trap_c        /* 0x14 - Reserved */
	.long _ex_trap_c        /* 0x15 - Reserved */
	.long _ex_trap_c        /* 0x16 - Reserved */
	.long _ex_trap_c        /* 0x17 - Reserved */
	.long _ex_trap_c        /* 0x18 - Reserved */
	.long _ex_trap_c        /* 0x19 - Reserved */
	.long _ex_trap_c        /* 0x1A - Reserved */
	.long _ex_trap_c        /* 0x1B - Reserved */
	.long _ex_trap_c        /* 0x1C - Reserved */
	.long _ex_trap_c        /* 0x1D - Reserved */
	.long _ex_trap_c        /* 0x1E - Reserved */
	.long _ex_trap_c        /* 0x1F - Reserved */
	.long _ex_trap_c        /* 0x20 - Reserved */
	.long _ex_trap_c        /* 0x21 - Undefined Instruction */
	.long _ex_trap_c        /* 0x22 - Illegal Instruction Combination */
	.long _ex_dviol         /* 0x23 - Data CPLB Protection Violation */
	.long _ex_trap_c        /* 0x24 - Data access misaligned */
	.long _ex_trap_c        /* 0x25 - Unrecoverable Event */
	.long _ex_dmiss         /* 0x26 - Data CPLB Miss */
	.long _ex_dmult         /* 0x27 - Data CPLB Multiple Hits - Linux Trap Zero */
	.long _ex_trap_c        /* 0x28 - Emulation Watchpoint */
	.long _ex_trap_c        /* 0x29 - Instruction fetch access error (535 only) */
	.long _ex_trap_c        /* 0x2A - Instruction fetch misaligned */
	.long _ex_trap_c        /* 0x2B - Instruction CPLB protection Violation */
	.long _ex_icplb_miss    /* 0x2C - Instruction CPLB miss */
	.long _ex_trap_c        /* 0x2D - Instruction CPLB Multiple Hits */
	.long _ex_trap_c        /* 0x2E - Illegal use of Supervisor Resource */
	.long _ex_trap_c        /* 0x2E - Illegal use of Supervisor Resource */
	.long _ex_trap_c        /* 0x2F - Reserved */
	.long _ex_trap_c        /* 0x30 - Reserved */
	.long _ex_trap_c        /* 0x31 - Reserved */
	.long _ex_trap_c        /* 0x32 - Reserved */
	.long _ex_trap_c        /* 0x33 - Reserved */
	.long _ex_trap_c        /* 0x34 - Reserved */
	.long _ex_trap_c        /* 0x35 - Reserved */
	.long _ex_trap_c        /* 0x36 - Reserved */
	.long _ex_trap_c        /* 0x37 - Reserved */
	.long _ex_trap_c        /* 0x38 - Reserved */
	.long _ex_trap_c        /* 0x39 - Reserved */
	.long _ex_trap_c        /* 0x3A - Reserved */
	.long _ex_trap_c        /* 0x3B - Reserved */
	.long _ex_trap_c        /* 0x3C - Reserved */
	.long _ex_trap_c        /* 0x3D - Reserved */
	.long _ex_trap_c        /* 0x3E - Reserved */
	.long _ex_trap_c        /* 0x3F - Reserved */
END(_ex_table)

ENTRY(_sys_call_table)
	.long _sys_restart_syscall	/* 0 */
	.long _sys_exit
	.long _sys_fork
	.long _sys_read
	.long _sys_write
	.long _sys_open		/* 5 */
	.long _sys_close
	.long _sys_ni_syscall	/* old waitpid */
	.long _sys_creat
	.long _sys_link
	.long _sys_unlink	/* 10 */
	.long _sys_execve
	.long _sys_chdir
	.long _sys_time
	.long _sys_mknod
	.long _sys_chmod		/* 15 */
	.long _sys_chown	/* chown16 */
	.long _sys_ni_syscall	/* old break syscall holder */
	.long _sys_ni_syscall	/* old stat */
	.long _sys_lseek
	.long _sys_getpid	/* 20 */
	.long _sys_mount
	.long _sys_ni_syscall	/* old umount */
	.long _sys_setuid
	.long _sys_getuid
	.long _sys_stime		/* 25 */
	.long _sys_ptrace
	.long _sys_alarm
	.long _sys_ni_syscall	/* old fstat */
	.long _sys_pause
	.long _sys_ni_syscall	/* old utime */ /* 30 */
	.long _sys_ni_syscall	/* old stty syscall holder */
	.long _sys_ni_syscall	/* old gtty syscall holder */
	.long _sys_access
	.long _sys_nice
	.long _sys_ni_syscall	/* 35 */ /* old ftime syscall holder */
	.long _sys_sync
	.long _sys_kill
	.long _sys_rename
	.long _sys_mkdir
	.long _sys_rmdir		/* 40 */
	.long _sys_dup
	.long _sys_pipe
	.long _sys_times
	.long _sys_ni_syscall	/* old prof syscall holder */
	.long _sys_brk		/* 45 */
	.long _sys_setgid
	.long _sys_getgid
	.long _sys_ni_syscall	/* old sys_signal */
	.long _sys_geteuid	/* geteuid16 */
	.long _sys_getegid	/* getegid16 */	/* 50 */
	.long _sys_acct
	.long _sys_umount	/* recycled never used phys() */
	.long _sys_ni_syscall	/* old lock syscall holder */
	.long _sys_ioctl
	.long _sys_fcntl		/* 55 */
	.long _sys_ni_syscall	/* old mpx syscall holder */
	.long _sys_setpgid
	.long _sys_ni_syscall	/* old ulimit syscall holder */
	.long _sys_ni_syscall	/* old old uname */
	.long _sys_umask		/* 60 */
	.long _sys_chroot
	.long _sys_ustat
	.long _sys_dup2
	.long _sys_getppid
	.long _sys_getpgrp	/* 65 */
	.long _sys_setsid
	.long _sys_ni_syscall	/* old sys_sigaction */
	.long _sys_sgetmask
	.long _sys_ssetmask
	.long _sys_setreuid	/* setreuid16 */	/* 70 */
	.long _sys_setregid	/* setregid16 */
	.long _sys_ni_syscall	/* old sys_sigsuspend */
	.long _sys_ni_syscall	/* old sys_sigpending */
	.long _sys_sethostname
	.long _sys_setrlimit	/* 75 */
	.long _sys_ni_syscall	/* old getrlimit */
	.long _sys_getrusage
	.long _sys_gettimeofday
	.long _sys_settimeofday
	.long _sys_getgroups	/* getgroups16 */	/* 80 */
	.long _sys_setgroups	/* setgroups16 */
	.long _sys_ni_syscall	/* old_select */
	.long _sys_symlink
	.long _sys_ni_syscall	/* old lstat */
	.long _sys_readlink	/* 85 */
	.long _sys_uselib
	.long _sys_ni_syscall	/* sys_swapon */
	.long _sys_reboot
	.long _sys_ni_syscall	/* old_readdir */
	.long _sys_ni_syscall	/* sys_mmap */	/* 90 */
	.long _sys_munmap
	.long _sys_truncate
	.long _sys_ftruncate
	.long _sys_fchmod
	.long _sys_fchown	/* fchown16 */	/* 95 */
	.long _sys_getpriority
	.long _sys_setpriority
	.long _sys_ni_syscall	/* old profil syscall holder */
	.long _sys_statfs
	.long _sys_fstatfs	/* 100 */
	.long _sys_ni_syscall
	.long _sys_ni_syscall	/* old sys_socketcall */
	.long _sys_syslog
	.long _sys_setitimer
	.long _sys_getitimer	/* 105 */
	.long _sys_newstat
	.long _sys_newlstat
	.long _sys_newfstat
	.long _sys_ni_syscall	/* old uname */
	.long _sys_ni_syscall	/* iopl for i386 */ /* 110 */
	.long _sys_vhangup
	.long _sys_ni_syscall	/* obsolete idle() syscall */
	.long _sys_ni_syscall	/* vm86old for i386 */
	.long _sys_wait4
	.long _sys_ni_syscall	/* 115 */ /* sys_swapoff */
	.long _sys_sysinfo
	.long _sys_ni_syscall	/* old sys_ipc */
	.long _sys_fsync
	.long _sys_ni_syscall	/* old sys_sigreturn */
	.long _sys_clone		/* 120 */
	.long _sys_setdomainname
	.long _sys_newuname
	.long _sys_ni_syscall	/* old sys_modify_ldt */
	.long _sys_adjtimex
	.long _sys_mprotect	/* 125 */
	.long _sys_ni_syscall	/* old sys_sigprocmask */
	.long _sys_ni_syscall	/* old "creat_module" */
	.long _sys_init_module
	.long _sys_delete_module
	.long _sys_ni_syscall	/* 130: old "get_kernel_syms" */
	.long _sys_quotactl
	.long _sys_getpgid
	.long _sys_fchdir
	.long _sys_bdflush
	.long _sys_ni_syscall	/* 135 */ /* sys_sysfs */
	.long _sys_personality
	.long _sys_ni_syscall	/* for afs_syscall */
	.long _sys_setfsuid	/* setfsuid16 */
	.long _sys_setfsgid	/* setfsgid16 */
	.long _sys_llseek	/* 140 */
	.long _sys_getdents
	.long _sys_ni_syscall	/* sys_select */
	.long _sys_flock
	.long _sys_msync
	.long _sys_readv		/* 145 */
	.long _sys_writev
	.long _sys_getsid
	.long _sys_fdatasync
	.long _sys_sysctl
	.long _sys_mlock	/* 150 */
	.long _sys_munlock
	.long _sys_mlockall
	.long _sys_munlockall
	.long _sys_sched_setparam
	.long _sys_sched_getparam /* 155 */
	.long _sys_sched_setscheduler
	.long _sys_sched_getscheduler
	.long _sys_sched_yield
	.long _sys_sched_get_priority_max
	.long _sys_sched_get_priority_min  /* 160 */
	.long _sys_sched_rr_get_interval
	.long _sys_nanosleep
	.long _sys_mremap
	.long _sys_setresuid	/* setresuid16 */
	.long _sys_getresuid	/* getresuid16 */	/* 165 */
	.long _sys_ni_syscall	/* for vm86 */
	.long _sys_ni_syscall	/* old "query_module" */
	.long _sys_ni_syscall	/* sys_poll */
	.long _sys_nfsservctl
	.long _sys_setresgid	/* setresgid16 */	/* 170 */
	.long _sys_getresgid	/* getresgid16 */
	.long _sys_prctl
	.long _sys_rt_sigreturn
	.long _sys_rt_sigaction
	.long _sys_rt_sigprocmask /* 175 */
	.long _sys_rt_sigpending
	.long _sys_rt_sigtimedwait
	.long _sys_rt_sigqueueinfo
	.long _sys_rt_sigsuspend
	.long _sys_pread64	/* 180 */
	.long _sys_pwrite64
	.long _sys_lchown	/* lchown16 */
	.long _sys_getcwd
	.long _sys_capget
	.long _sys_capset	/* 185 */
	.long _sys_sigaltstack
	.long _sys_sendfile
	.long _sys_ni_syscall	/* streams1 */
	.long _sys_ni_syscall	/* streams2 */
	.long _sys_vfork		/* 190 */
	.long _sys_getrlimit
	.long _sys_mmap_pgoff
	.long _sys_truncate64
	.long _sys_ftruncate64
	.long _sys_stat64	/* 195 */
	.long _sys_lstat64
	.long _sys_fstat64
	.long _sys_chown
	.long _sys_getuid
	.long _sys_getgid	/* 200 */
	.long _sys_geteuid
	.long _sys_getegid
	.long _sys_setreuid
	.long _sys_setregid
	.long _sys_getgroups	/* 205 */
	.long _sys_setgroups
	.long _sys_fchown
	.long _sys_setresuid
	.long _sys_getresuid
	.long _sys_setresgid	/* 210 */
	.long _sys_getresgid
	.long _sys_lchown
	.long _sys_setuid
	.long _sys_setgid
	.long _sys_setfsuid	/* 215 */
	.long _sys_setfsgid
	.long _sys_pivot_root
	.long _sys_mincore
	.long _sys_madvise
	.long _sys_getdents64	/* 220 */
	.long _sys_fcntl64
	.long _sys_ni_syscall	/* reserved for TUX */
	.long _sys_ni_syscall
	.long _sys_gettid
	.long _sys_readahead	/* 225 */
	.long _sys_setxattr
	.long _sys_lsetxattr
	.long _sys_fsetxattr
	.long _sys_getxattr
	.long _sys_lgetxattr	/* 230 */
	.long _sys_fgetxattr
	.long _sys_listxattr
	.long _sys_llistxattr
	.long _sys_flistxattr
	.long _sys_removexattr	/* 235 */
	.long _sys_lremovexattr
	.long _sys_fremovexattr
	.long _sys_tkill
	.long _sys_sendfile64
	.long _sys_futex		/* 240 */
	.long _sys_sched_setaffinity
	.long _sys_sched_getaffinity
	.long _sys_ni_syscall	/* sys_set_thread_area */
	.long _sys_ni_syscall	/* sys_get_thread_area */
	.long _sys_io_setup	/* 245 */
	.long _sys_io_destroy
	.long _sys_io_getevents
	.long _sys_io_submit
	.long _sys_io_cancel
	.long _sys_ni_syscall	/* 250 */ /* sys_alloc_hugepages */
	.long _sys_ni_syscall	/* sys_freec_hugepages */
	.long _sys_exit_group
	.long _sys_lookup_dcookie
	.long _sys_bfin_spinlock
	.long _sys_epoll_create	/* 255 */
	.long _sys_epoll_ctl
	.long _sys_epoll_wait
	.long _sys_ni_syscall /* remap_file_pages */
	.long _sys_set_tid_address
	.long _sys_timer_create	/* 260 */
	.long _sys_timer_settime
	.long _sys_timer_gettime
	.long _sys_timer_getoverrun
	.long _sys_timer_delete
	.long _sys_clock_settime /* 265 */
	.long _sys_clock_gettime
	.long _sys_clock_getres
	.long _sys_clock_nanosleep
	.long _sys_statfs64
	.long _sys_fstatfs64	/* 270 */
	.long _sys_tgkill
	.long _sys_utimes
	.long _sys_fadvise64_64
	.long _sys_ni_syscall /* vserver */
	.long _sys_mbind	/* 275 */
	.long _sys_ni_syscall /* get_mempolicy */
	.long _sys_ni_syscall /* set_mempolicy */
	.long _sys_mq_open
	.long _sys_mq_unlink
	.long _sys_mq_timedsend	/* 280 */
	.long _sys_mq_timedreceive
	.long _sys_mq_notify
	.long _sys_mq_getsetattr
	.long _sys_ni_syscall /* kexec_load */
	.long _sys_waitid	/* 285 */
	.long _sys_add_key
	.long _sys_request_key
	.long _sys_keyctl
	.long _sys_ioprio_set
	.long _sys_ioprio_get	/* 290 */
	.long _sys_inotify_init
	.long _sys_inotify_add_watch
	.long _sys_inotify_rm_watch
	.long _sys_ni_syscall /* migrate_pages */
	.long _sys_openat	/* 295 */
	.long _sys_mkdirat
	.long _sys_mknodat
	.long _sys_fchownat
	.long _sys_futimesat
	.long _sys_fstatat64	/* 300 */
	.long _sys_unlinkat
	.long _sys_renameat
	.long _sys_linkat
	.long _sys_symlinkat
	.long _sys_readlinkat	/* 305 */
	.long _sys_fchmodat
	.long _sys_faccessat
	.long _sys_pselect6
	.long _sys_ppoll
	.long _sys_unshare	/* 310 */
	.long _sys_sram_alloc
	.long _sys_sram_free
	.long _sys_dma_memcpy
	.long _sys_accept
	.long _sys_bind		/* 315 */
	.long _sys_connect
	.long _sys_getpeername
	.long _sys_getsockname
	.long _sys_getsockopt
	.long _sys_listen	/* 320 */
	.long _sys_recv
	.long _sys_recvfrom
	.long _sys_recvmsg
	.long _sys_send
	.long _sys_sendmsg	/* 325 */
	.long _sys_sendto
	.long _sys_setsockopt
	.long _sys_shutdown
	.long _sys_socket
	.long _sys_socketpair	/* 330 */
	.long _sys_semctl
	.long _sys_semget
	.long _sys_semop
	.long _sys_msgctl
	.long _sys_msgget	/* 335 */
	.long _sys_msgrcv
	.long _sys_msgsnd
	.long _sys_shmat
	.long _sys_shmctl
	.long _sys_shmdt	/* 340 */
	.long _sys_shmget
	.long _sys_splice
	.long _sys_sync_file_range
	.long _sys_tee
	.long _sys_vmsplice	/* 345 */
	.long _sys_epoll_pwait
	.long _sys_utimensat
	.long _sys_signalfd
	.long _sys_timerfd_create
	.long _sys_eventfd	/* 350 */
	.long _sys_pread64
	.long _sys_pwrite64
	.long _sys_fadvise64
	.long _sys_set_robust_list
	.long _sys_get_robust_list	/* 355 */
	.long _sys_fallocate
	.long _sys_semtimedop
	.long _sys_timerfd_settime
	.long _sys_timerfd_gettime
	.long _sys_signalfd4		/* 360 */
	.long _sys_eventfd2
	.long _sys_epoll_create1
	.long _sys_dup3
	.long _sys_pipe2
	.long _sys_inotify_init1	/* 365 */
	.long _sys_preadv
	.long _sys_pwritev
	.long _sys_rt_tgsigqueueinfo
	.long _sys_perf_event_open
	.long _sys_recvmmsg		/* 370 */
	.long _sys_fanotify_init
	.long _sys_fanotify_mark
	.long _sys_prlimit64
	.long _sys_cacheflush
	.long _sys_name_to_handle_at	/* 375 */
	.long _sys_open_by_handle_at
	.long _sys_clock_adjtime
	.long _sys_syncfs
	.long _sys_setns
	.long _sys_sendmmsg		/* 380 */

	.rept NR_syscalls-(.-_sys_call_table)/4
	.long _sys_ni_syscall
	.endr
END(_sys_call_table)
