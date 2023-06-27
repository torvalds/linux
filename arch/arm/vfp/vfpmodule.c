// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/vfp/vfpmodule.c
 *
 *  Copyright (C) 2004 ARM Limited.
 *  Written by Deep Blue Solutions Limited.
 */
#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/hardirq.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/uaccess.h>
#include <linux/user.h>
#include <linux/export.h>
#include <linux/perf_event.h>

#include <asm/cp15.h>
#include <asm/cputype.h>
#include <asm/system_info.h>
#include <asm/thread_notify.h>
#include <asm/traps.h>
#include <asm/vfp.h>

#include "vfpinstr.h"
#include "vfp.h"

static bool have_vfp __ro_after_init;

/*
 * Dual-use variable.
 * Used in startup: set to non-zero if VFP checks fail
 * After startup, holds VFP architecture
 */
static unsigned int VFP_arch;

#ifdef CONFIG_CPU_FEROCEON
extern unsigned int VFP_arch_feroceon __alias(VFP_arch);
#endif

/*
 * The pointer to the vfpstate structure of the thread which currently
 * owns the context held in the VFP hardware, or NULL if the hardware
 * context is invalid.
 *
 * For UP, this is sufficient to tell which thread owns the VFP context.
 * However, for SMP, we also need to check the CPU number stored in the
 * saved state too to catch migrations.
 */
union vfp_state *vfp_current_hw_state[NR_CPUS];

/*
 * Is 'thread's most up to date state stored in this CPUs hardware?
 * Must be called from non-preemptible context.
 */
static bool vfp_state_in_hw(unsigned int cpu, struct thread_info *thread)
{
#ifdef CONFIG_SMP
	if (thread->vfpstate.hard.cpu != cpu)
		return false;
#endif
	return vfp_current_hw_state[cpu] == &thread->vfpstate;
}

/*
 * Force a reload of the VFP context from the thread structure.  We do
 * this by ensuring that access to the VFP hardware is disabled, and
 * clear vfp_current_hw_state.  Must be called from non-preemptible context.
 */
static void vfp_force_reload(unsigned int cpu, struct thread_info *thread)
{
	if (vfp_state_in_hw(cpu, thread)) {
		fmxr(FPEXC, fmrx(FPEXC) & ~FPEXC_EN);
		vfp_current_hw_state[cpu] = NULL;
	}
#ifdef CONFIG_SMP
	thread->vfpstate.hard.cpu = NR_CPUS;
#endif
}

/*
 * Per-thread VFP initialization.
 */
static void vfp_thread_flush(struct thread_info *thread)
{
	union vfp_state *vfp = &thread->vfpstate;
	unsigned int cpu;

	/*
	 * Disable VFP to ensure we initialize it first.  We must ensure
	 * that the modification of vfp_current_hw_state[] and hardware
	 * disable are done for the same CPU and without preemption.
	 *
	 * Do this first to ensure that preemption won't overwrite our
	 * state saving should access to the VFP be enabled at this point.
	 */
	cpu = get_cpu();
	if (vfp_current_hw_state[cpu] == vfp)
		vfp_current_hw_state[cpu] = NULL;
	fmxr(FPEXC, fmrx(FPEXC) & ~FPEXC_EN);
	put_cpu();

	memset(vfp, 0, sizeof(union vfp_state));

	vfp->hard.fpexc = FPEXC_EN;
	vfp->hard.fpscr = FPSCR_ROUND_NEAREST;
#ifdef CONFIG_SMP
	vfp->hard.cpu = NR_CPUS;
#endif
}

static void vfp_thread_exit(struct thread_info *thread)
{
	/* release case: Per-thread VFP cleanup. */
	union vfp_state *vfp = &thread->vfpstate;
	unsigned int cpu = get_cpu();

	if (vfp_current_hw_state[cpu] == vfp)
		vfp_current_hw_state[cpu] = NULL;
	put_cpu();
}

static void vfp_thread_copy(struct thread_info *thread)
{
	struct thread_info *parent = current_thread_info();

	vfp_sync_hwstate(parent);
	thread->vfpstate = parent->vfpstate;
#ifdef CONFIG_SMP
	thread->vfpstate.hard.cpu = NR_CPUS;
#endif
}

/*
 * When this function is called with the following 'cmd's, the following
 * is true while this function is being run:
 *  THREAD_NOFTIFY_SWTICH:
 *   - the previously running thread will not be scheduled onto another CPU.
 *   - the next thread to be run (v) will not be running on another CPU.
 *   - thread->cpu is the local CPU number
 *   - not preemptible as we're called in the middle of a thread switch
 *  THREAD_NOTIFY_FLUSH:
 *   - the thread (v) will be running on the local CPU, so
 *	v === current_thread_info()
 *   - thread->cpu is the local CPU number at the time it is accessed,
 *	but may change at any time.
 *   - we could be preempted if tree preempt rcu is enabled, so
 *	it is unsafe to use thread->cpu.
 *  THREAD_NOTIFY_EXIT
 *   - we could be preempted if tree preempt rcu is enabled, so
 *	it is unsafe to use thread->cpu.
 */
static int vfp_notifier(struct notifier_block *self, unsigned long cmd, void *v)
{
	struct thread_info *thread = v;
	u32 fpexc;
#ifdef CONFIG_SMP
	unsigned int cpu;
#endif

	switch (cmd) {
	case THREAD_NOTIFY_SWITCH:
		fpexc = fmrx(FPEXC);

#ifdef CONFIG_SMP
		cpu = thread->cpu;

		/*
		 * On SMP, if VFP is enabled, save the old state in
		 * case the thread migrates to a different CPU. The
		 * restoring is done lazily.
		 */
		if ((fpexc & FPEXC_EN) && vfp_current_hw_state[cpu])
			vfp_save_state(vfp_current_hw_state[cpu], fpexc);
#endif

		/*
		 * Always disable VFP so we can lazily save/restore the
		 * old state.
		 */
		fmxr(FPEXC, fpexc & ~FPEXC_EN);
		break;

	case THREAD_NOTIFY_FLUSH:
		vfp_thread_flush(thread);
		break;

	case THREAD_NOTIFY_EXIT:
		vfp_thread_exit(thread);
		break;

	case THREAD_NOTIFY_COPY:
		vfp_thread_copy(thread);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block vfp_notifier_block = {
	.notifier_call	= vfp_notifier,
};

/*
 * Raise a SIGFPE for the current process.
 * sicode describes the signal being raised.
 */
static void vfp_raise_sigfpe(unsigned int sicode, struct pt_regs *regs)
{
	/*
	 * This is the same as NWFPE, because it's not clear what
	 * this is used for
	 */
	current->thread.error_code = 0;
	current->thread.trap_no = 6;

	send_sig_fault(SIGFPE, sicode,
		       (void __user *)(instruction_pointer(regs) - 4),
		       current);
}

static void vfp_panic(char *reason, u32 inst)
{
	int i;

	pr_err("VFP: Error: %s\n", reason);
	pr_err("VFP: EXC 0x%08x SCR 0x%08x INST 0x%08x\n",
		fmrx(FPEXC), fmrx(FPSCR), inst);
	for (i = 0; i < 32; i += 2)
		pr_err("VFP: s%2u: 0x%08x s%2u: 0x%08x\n",
		       i, vfp_get_float(i), i+1, vfp_get_float(i+1));
}

/*
 * Process bitmask of exception conditions.
 */
static void vfp_raise_exceptions(u32 exceptions, u32 inst, u32 fpscr, struct pt_regs *regs)
{
	int si_code = 0;

	pr_debug("VFP: raising exceptions %08x\n", exceptions);

	if (exceptions == VFP_EXCEPTION_ERROR) {
		vfp_panic("unhandled bounce", inst);
		vfp_raise_sigfpe(FPE_FLTINV, regs);
		return;
	}

	/*
	 * If any of the status flags are set, update the FPSCR.
	 * Comparison instructions always return at least one of
	 * these flags set.
	 */
	if (exceptions & (FPSCR_N|FPSCR_Z|FPSCR_C|FPSCR_V))
		fpscr &= ~(FPSCR_N|FPSCR_Z|FPSCR_C|FPSCR_V);

	fpscr |= exceptions;

	fmxr(FPSCR, fpscr);

#define RAISE(stat,en,sig)				\
	if (exceptions & stat && fpscr & en)		\
		si_code = sig;

	/*
	 * These are arranged in priority order, least to highest.
	 */
	RAISE(FPSCR_DZC, FPSCR_DZE, FPE_FLTDIV);
	RAISE(FPSCR_IXC, FPSCR_IXE, FPE_FLTRES);
	RAISE(FPSCR_UFC, FPSCR_UFE, FPE_FLTUND);
	RAISE(FPSCR_OFC, FPSCR_OFE, FPE_FLTOVF);
	RAISE(FPSCR_IOC, FPSCR_IOE, FPE_FLTINV);

	if (si_code)
		vfp_raise_sigfpe(si_code, regs);
}

/*
 * Emulate a VFP instruction.
 */
static u32 vfp_emulate_instruction(u32 inst, u32 fpscr, struct pt_regs *regs)
{
	u32 exceptions = VFP_EXCEPTION_ERROR;

	pr_debug("VFP: emulate: INST=0x%08x SCR=0x%08x\n", inst, fpscr);

	if (INST_CPRTDO(inst)) {
		if (!INST_CPRT(inst)) {
			/*
			 * CPDO
			 */
			if (vfp_single(inst)) {
				exceptions = vfp_single_cpdo(inst, fpscr);
			} else {
				exceptions = vfp_double_cpdo(inst, fpscr);
			}
		} else {
			/*
			 * A CPRT instruction can not appear in FPINST2, nor
			 * can it cause an exception.  Therefore, we do not
			 * have to emulate it.
			 */
		}
	} else {
		/*
		 * A CPDT instruction can not appear in FPINST2, nor can
		 * it cause an exception.  Therefore, we do not have to
		 * emulate it.
		 */
	}
	perf_sw_event(PERF_COUNT_SW_EMULATION_FAULTS, 1, regs, regs->ARM_pc);
	return exceptions & ~VFP_NAN_FLAG;
}

/*
 * Package up a bounce condition.
 */
static void VFP_bounce(u32 trigger, u32 fpexc, struct pt_regs *regs)
{
	u32 fpscr, orig_fpscr, fpsid, exceptions;

	pr_debug("VFP: bounce: trigger %08x fpexc %08x\n", trigger, fpexc);

	/*
	 * At this point, FPEXC can have the following configuration:
	 *
	 *  EX DEX IXE
	 *  0   1   x   - synchronous exception
	 *  1   x   0   - asynchronous exception
	 *  1   x   1   - sychronous on VFP subarch 1 and asynchronous on later
	 *  0   0   1   - synchronous on VFP9 (non-standard subarch 1
	 *                implementation), undefined otherwise
	 *
	 * Clear various bits and enable access to the VFP so we can
	 * handle the bounce.
	 */
	fmxr(FPEXC, fpexc & ~(FPEXC_EX|FPEXC_DEX|FPEXC_FP2V|FPEXC_VV|FPEXC_TRAP_MASK));

	fpsid = fmrx(FPSID);
	orig_fpscr = fpscr = fmrx(FPSCR);

	/*
	 * Check for the special VFP subarch 1 and FPSCR.IXE bit case
	 */
	if ((fpsid & FPSID_ARCH_MASK) == (1 << FPSID_ARCH_BIT)
	    && (fpscr & FPSCR_IXE)) {
		/*
		 * Synchronous exception, emulate the trigger instruction
		 */
		goto emulate;
	}

	if (fpexc & FPEXC_EX) {
		/*
		 * Asynchronous exception. The instruction is read from FPINST
		 * and the interrupted instruction has to be restarted.
		 */
		trigger = fmrx(FPINST);
		regs->ARM_pc -= 4;
	} else if (!(fpexc & FPEXC_DEX)) {
		/*
		 * Illegal combination of bits. It can be caused by an
		 * unallocated VFP instruction but with FPSCR.IXE set and not
		 * on VFP subarch 1.
		 */
		 vfp_raise_exceptions(VFP_EXCEPTION_ERROR, trigger, fpscr, regs);
		return;
	}

	/*
	 * Modify fpscr to indicate the number of iterations remaining.
	 * If FPEXC.EX is 0, FPEXC.DEX is 1 and the FPEXC.VV bit indicates
	 * whether FPEXC.VECITR or FPSCR.LEN is used.
	 */
	if (fpexc & (FPEXC_EX | FPEXC_VV)) {
		u32 len;

		len = fpexc + (1 << FPEXC_LENGTH_BIT);

		fpscr &= ~FPSCR_LENGTH_MASK;
		fpscr |= (len & FPEXC_LENGTH_MASK) << (FPSCR_LENGTH_BIT - FPEXC_LENGTH_BIT);
	}

	/*
	 * Handle the first FP instruction.  We used to take note of the
	 * FPEXC bounce reason, but this appears to be unreliable.
	 * Emulate the bounced instruction instead.
	 */
	exceptions = vfp_emulate_instruction(trigger, fpscr, regs);
	if (exceptions)
		vfp_raise_exceptions(exceptions, trigger, orig_fpscr, regs);

	/*
	 * If there isn't a second FP instruction, exit now. Note that
	 * the FPEXC.FP2V bit is valid only if FPEXC.EX is 1.
	 */
	if ((fpexc & (FPEXC_EX | FPEXC_FP2V)) != (FPEXC_EX | FPEXC_FP2V))
		return;

	/*
	 * The barrier() here prevents fpinst2 being read
	 * before the condition above.
	 */
	barrier();
	trigger = fmrx(FPINST2);

 emulate:
	exceptions = vfp_emulate_instruction(trigger, orig_fpscr, regs);
	if (exceptions)
		vfp_raise_exceptions(exceptions, trigger, orig_fpscr, regs);
}

static void vfp_enable(void *unused)
{
	u32 access;

	BUG_ON(preemptible());
	access = get_copro_access();

	/*
	 * Enable full access to VFP (cp10 and cp11)
	 */
	set_copro_access(access | CPACC_FULL(10) | CPACC_FULL(11));
}

/* Called by platforms on which we want to disable VFP because it may not be
 * present on all CPUs within a SMP complex. Needs to be called prior to
 * vfp_init().
 */
void __init vfp_disable(void)
{
	if (VFP_arch) {
		pr_debug("%s: should be called prior to vfp_init\n", __func__);
		return;
	}
	VFP_arch = 1;
}

#ifdef CONFIG_CPU_PM
static int vfp_pm_suspend(void)
{
	struct thread_info *ti = current_thread_info();
	u32 fpexc = fmrx(FPEXC);

	/* if vfp is on, then save state for resumption */
	if (fpexc & FPEXC_EN) {
		pr_debug("%s: saving vfp state\n", __func__);
		vfp_save_state(&ti->vfpstate, fpexc);

		/* disable, just in case */
		fmxr(FPEXC, fmrx(FPEXC) & ~FPEXC_EN);
	} else if (vfp_current_hw_state[ti->cpu]) {
#ifndef CONFIG_SMP
		fmxr(FPEXC, fpexc | FPEXC_EN);
		vfp_save_state(vfp_current_hw_state[ti->cpu], fpexc);
		fmxr(FPEXC, fpexc);
#endif
	}

	/* clear any information we had about last context state */
	vfp_current_hw_state[ti->cpu] = NULL;

	return 0;
}

static void vfp_pm_resume(void)
{
	/* ensure we have access to the vfp */
	vfp_enable(NULL);

	/* and disable it to ensure the next usage restores the state */
	fmxr(FPEXC, fmrx(FPEXC) & ~FPEXC_EN);
}

static int vfp_cpu_pm_notifier(struct notifier_block *self, unsigned long cmd,
	void *v)
{
	switch (cmd) {
	case CPU_PM_ENTER:
		vfp_pm_suspend();
		break;
	case CPU_PM_ENTER_FAILED:
	case CPU_PM_EXIT:
		vfp_pm_resume();
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block vfp_cpu_pm_notifier_block = {
	.notifier_call = vfp_cpu_pm_notifier,
};

static void vfp_pm_init(void)
{
	cpu_pm_register_notifier(&vfp_cpu_pm_notifier_block);
}

#else
static inline void vfp_pm_init(void) { }
#endif /* CONFIG_CPU_PM */

/*
 * Ensure that the VFP state stored in 'thread->vfpstate' is up to date
 * with the hardware state.
 */
void vfp_sync_hwstate(struct thread_info *thread)
{
	unsigned int cpu = get_cpu();

	local_bh_disable();

	if (vfp_state_in_hw(cpu, thread)) {
		u32 fpexc = fmrx(FPEXC);

		/*
		 * Save the last VFP state on this CPU.
		 */
		fmxr(FPEXC, fpexc | FPEXC_EN);
		vfp_save_state(&thread->vfpstate, fpexc | FPEXC_EN);
		fmxr(FPEXC, fpexc);
	}

	local_bh_enable();
	put_cpu();
}

/* Ensure that the thread reloads the hardware VFP state on the next use. */
void vfp_flush_hwstate(struct thread_info *thread)
{
	unsigned int cpu = get_cpu();

	vfp_force_reload(cpu, thread);

	put_cpu();
}

/*
 * Save the current VFP state into the provided structures and prepare
 * for entry into a new function (signal handler).
 */
int vfp_preserve_user_clear_hwstate(struct user_vfp *ufp,
				    struct user_vfp_exc *ufp_exc)
{
	struct thread_info *thread = current_thread_info();
	struct vfp_hard_struct *hwstate = &thread->vfpstate.hard;

	/* Ensure that the saved hwstate is up-to-date. */
	vfp_sync_hwstate(thread);

	/*
	 * Copy the floating point registers. There can be unused
	 * registers see asm/hwcap.h for details.
	 */
	memcpy(&ufp->fpregs, &hwstate->fpregs, sizeof(hwstate->fpregs));

	/*
	 * Copy the status and control register.
	 */
	ufp->fpscr = hwstate->fpscr;

	/*
	 * Copy the exception registers.
	 */
	ufp_exc->fpexc = hwstate->fpexc;
	ufp_exc->fpinst = hwstate->fpinst;
	ufp_exc->fpinst2 = hwstate->fpinst2;

	/* Ensure that VFP is disabled. */
	vfp_flush_hwstate(thread);

	/*
	 * As per the PCS, clear the length and stride bits for function
	 * entry.
	 */
	hwstate->fpscr &= ~(FPSCR_LENGTH_MASK | FPSCR_STRIDE_MASK);
	return 0;
}

/* Sanitise and restore the current VFP state from the provided structures. */
int vfp_restore_user_hwstate(struct user_vfp *ufp, struct user_vfp_exc *ufp_exc)
{
	struct thread_info *thread = current_thread_info();
	struct vfp_hard_struct *hwstate = &thread->vfpstate.hard;
	unsigned long fpexc;

	/* Disable VFP to avoid corrupting the new thread state. */
	vfp_flush_hwstate(thread);

	/*
	 * Copy the floating point registers. There can be unused
	 * registers see asm/hwcap.h for details.
	 */
	memcpy(&hwstate->fpregs, &ufp->fpregs, sizeof(hwstate->fpregs));
	/*
	 * Copy the status and control register.
	 */
	hwstate->fpscr = ufp->fpscr;

	/*
	 * Sanitise and restore the exception registers.
	 */
	fpexc = ufp_exc->fpexc;

	/* Ensure the VFP is enabled. */
	fpexc |= FPEXC_EN;

	/* Ensure FPINST2 is invalid and the exception flag is cleared. */
	fpexc &= ~(FPEXC_EX | FPEXC_FP2V);
	hwstate->fpexc = fpexc;

	hwstate->fpinst = ufp_exc->fpinst;
	hwstate->fpinst2 = ufp_exc->fpinst2;

	return 0;
}

/*
 * VFP hardware can lose all context when a CPU goes offline.
 * As we will be running in SMP mode with CPU hotplug, we will save the
 * hardware state at every thread switch.  We clear our held state when
 * a CPU has been killed, indicating that the VFP hardware doesn't contain
 * a threads VFP state.  When a CPU starts up, we re-enable access to the
 * VFP hardware. The callbacks below are called on the CPU which
 * is being offlined/onlined.
 */
static int vfp_dying_cpu(unsigned int cpu)
{
	vfp_current_hw_state[cpu] = NULL;
	return 0;
}

static int vfp_starting_cpu(unsigned int unused)
{
	vfp_enable(NULL);
	return 0;
}

static int vfp_kmode_exception(struct pt_regs *regs, unsigned int instr)
{
	/*
	 * If we reach this point, a floating point exception has been raised
	 * while running in kernel mode. If the NEON/VFP unit was enabled at the
	 * time, it means a VFP instruction has been issued that requires
	 * software assistance to complete, something which is not currently
	 * supported in kernel mode.
	 * If the NEON/VFP unit was disabled, and the location pointed to below
	 * is properly preceded by a call to kernel_neon_begin(), something has
	 * caused the task to be scheduled out and back in again. In this case,
	 * rebuilding and running with CONFIG_DEBUG_ATOMIC_SLEEP enabled should
	 * be helpful in localizing the problem.
	 */
	if (fmrx(FPEXC) & FPEXC_EN)
		pr_crit("BUG: unsupported FP instruction in kernel mode\n");
	else
		pr_crit("BUG: FP instruction issued in kernel mode with FP unit disabled\n");
	pr_crit("FPEXC == 0x%08x\n", fmrx(FPEXC));
	return 1;
}

/*
 * vfp_support_entry - Handle VFP exception
 *
 * @regs:	pt_regs structure holding the register state at exception entry
 * @trigger:	The opcode of the instruction that triggered the exception
 *
 * Returns 0 if the exception was handled, or an error code otherwise.
 */
static int vfp_support_entry(struct pt_regs *regs, u32 trigger)
{
	struct thread_info *ti = current_thread_info();
	u32 fpexc;

	if (unlikely(!have_vfp))
		return -ENODEV;

	if (!user_mode(regs))
		return vfp_kmode_exception(regs, trigger);

	local_bh_disable();
	fpexc = fmrx(FPEXC);

	/*
	 * If the VFP unit was not enabled yet, we have to check whether the
	 * VFP state in the CPU's registers is the most recent VFP state
	 * associated with the process. On UP systems, we don't save the VFP
	 * state eagerly on a context switch, so we may need to save the
	 * VFP state to memory first, as it may belong to another process.
	 */
	if (!(fpexc & FPEXC_EN)) {
		/*
		 * Enable the VFP unit but mask the FP exception flag for the
		 * time being, so we can access all the registers.
		 */
		fpexc |= FPEXC_EN;
		fmxr(FPEXC, fpexc & ~FPEXC_EX);

		/*
		 * Check whether or not the VFP state in the CPU's registers is
		 * the most recent VFP state associated with this task. On SMP,
		 * migration may result in multiple CPUs holding VFP states
		 * that belong to the same task, but only the most recent one
		 * is valid.
		 */
		if (!vfp_state_in_hw(ti->cpu, ti)) {
			if (!IS_ENABLED(CONFIG_SMP) &&
			    vfp_current_hw_state[ti->cpu] != NULL) {
				/*
				 * This CPU is currently holding the most
				 * recent VFP state associated with another
				 * task, and we must save that to memory first.
				 */
				vfp_save_state(vfp_current_hw_state[ti->cpu],
					       fpexc);
			}

			/*
			 * We can now proceed with loading the task's VFP state
			 * from memory into the CPU registers.
			 */
			fpexc = vfp_load_state(&ti->vfpstate);
			vfp_current_hw_state[ti->cpu] = &ti->vfpstate;
#ifdef CONFIG_SMP
			/*
			 * Record that this CPU is now the one holding the most
			 * recent VFP state of the task.
			 */
			ti->vfpstate.hard.cpu = ti->cpu;
#endif
		}

		if (fpexc & FPEXC_EX)
			/*
			 * Might as well handle the pending exception before
			 * retrying branch out before setting an FPEXC that
			 * stops us reading stuff.
			 */
			goto bounce;

		/*
		 * No FP exception is pending: just enable the VFP and
		 * replay the instruction that trapped.
		 */
		fmxr(FPEXC, fpexc);
	} else {
		/* Check for synchronous or asynchronous exceptions */
		if (!(fpexc & (FPEXC_EX | FPEXC_DEX))) {
			u32 fpscr = fmrx(FPSCR);

			/*
			 * On some implementations of the VFP subarch 1,
			 * setting FPSCR.IXE causes all the CDP instructions to
			 * be bounced synchronously without setting the
			 * FPEXC.EX bit
			 */
			if (!(fpscr & FPSCR_IXE)) {
				if (!(fpscr & FPSCR_LENGTH_MASK)) {
					pr_debug("not VFP\n");
					local_bh_enable();
					return -ENOEXEC;
				}
				fpexc |= FPEXC_DEX;
			}
		}
bounce:		regs->ARM_pc += 4;
		VFP_bounce(trigger, fpexc, regs);
	}

	local_bh_enable();
	return 0;
}

static struct undef_hook neon_support_hook[] = {{
	.instr_mask	= 0xfe000000,
	.instr_val	= 0xf2000000,
	.cpsr_mask	= PSR_T_BIT,
	.cpsr_val	= 0,
	.fn		= vfp_support_entry,
}, {
	.instr_mask	= 0xff100000,
	.instr_val	= 0xf4000000,
	.cpsr_mask	= PSR_T_BIT,
	.cpsr_val	= 0,
	.fn		= vfp_support_entry,
}, {
	.instr_mask	= 0xef000000,
	.instr_val	= 0xef000000,
	.cpsr_mask	= PSR_T_BIT,
	.cpsr_val	= PSR_T_BIT,
	.fn		= vfp_support_entry,
}, {
	.instr_mask	= 0xff100000,
	.instr_val	= 0xf9000000,
	.cpsr_mask	= PSR_T_BIT,
	.cpsr_val	= PSR_T_BIT,
	.fn		= vfp_support_entry,
}};

static struct undef_hook vfp_support_hook = {
	.instr_mask	= 0x0c000e00,
	.instr_val	= 0x0c000a00,
	.fn		= vfp_support_entry,
};

#ifdef CONFIG_KERNEL_MODE_NEON

/*
 * Kernel-side NEON support functions
 */
void kernel_neon_begin(void)
{
	struct thread_info *thread = current_thread_info();
	unsigned int cpu;
	u32 fpexc;

	local_bh_disable();

	/*
	 * Kernel mode NEON is only allowed outside of hardirq context with
	 * preemption and softirq processing disabled. This will make sure that
	 * the kernel mode NEON register contents never need to be preserved.
	 */
	BUG_ON(in_hardirq());
	cpu = __smp_processor_id();

	fpexc = fmrx(FPEXC) | FPEXC_EN;
	fmxr(FPEXC, fpexc);

	/*
	 * Save the userland NEON/VFP state. Under UP,
	 * the owner could be a task other than 'current'
	 */
	if (vfp_state_in_hw(cpu, thread))
		vfp_save_state(&thread->vfpstate, fpexc);
#ifndef CONFIG_SMP
	else if (vfp_current_hw_state[cpu] != NULL)
		vfp_save_state(vfp_current_hw_state[cpu], fpexc);
#endif
	vfp_current_hw_state[cpu] = NULL;
}
EXPORT_SYMBOL(kernel_neon_begin);

void kernel_neon_end(void)
{
	/* Disable the NEON/VFP unit. */
	fmxr(FPEXC, fmrx(FPEXC) & ~FPEXC_EN);
	local_bh_enable();
}
EXPORT_SYMBOL(kernel_neon_end);

#endif /* CONFIG_KERNEL_MODE_NEON */

static int __init vfp_detect(struct pt_regs *regs, unsigned int instr)
{
	VFP_arch = UINT_MAX;	/* mark as not present */
	regs->ARM_pc += 4;
	return 0;
}

static struct undef_hook vfp_detect_hook __initdata = {
	.instr_mask	= 0x0c000e00,
	.instr_val	= 0x0c000a00,
	.cpsr_mask	= MODE_MASK,
	.cpsr_val	= SVC_MODE,
	.fn		= vfp_detect,
};

/*
 * VFP support code initialisation.
 */
static int __init vfp_init(void)
{
	unsigned int vfpsid;
	unsigned int cpu_arch = cpu_architecture();
	unsigned int isar6;

	/*
	 * Enable the access to the VFP on all online CPUs so the
	 * following test on FPSID will succeed.
	 */
	if (cpu_arch >= CPU_ARCH_ARMv6)
		on_each_cpu(vfp_enable, NULL, 1);

	/*
	 * First check that there is a VFP that we can use.
	 * The handler is already setup to just log calls, so
	 * we just need to read the VFPSID register.
	 */
	register_undef_hook(&vfp_detect_hook);
	barrier();
	vfpsid = fmrx(FPSID);
	barrier();
	unregister_undef_hook(&vfp_detect_hook);

	pr_info("VFP support v0.3: ");
	if (VFP_arch) {
		pr_cont("not present\n");
		return 0;
	/* Extract the architecture on CPUID scheme */
	} else if ((read_cpuid_id() & 0x000f0000) == 0x000f0000) {
		VFP_arch = vfpsid & FPSID_CPUID_ARCH_MASK;
		VFP_arch >>= FPSID_ARCH_BIT;
		/*
		 * Check for the presence of the Advanced SIMD
		 * load/store instructions, integer and single
		 * precision floating point operations. Only check
		 * for NEON if the hardware has the MVFR registers.
		 */
		if (IS_ENABLED(CONFIG_NEON) &&
		    (fmrx(MVFR1) & 0x000fff00) == 0x00011100) {
			elf_hwcap |= HWCAP_NEON;
			for (int i = 0; i < ARRAY_SIZE(neon_support_hook); i++)
				register_undef_hook(&neon_support_hook[i]);
		}

		if (IS_ENABLED(CONFIG_VFPv3)) {
			u32 mvfr0 = fmrx(MVFR0);
			if (((mvfr0 & MVFR0_DP_MASK) >> MVFR0_DP_BIT) == 0x2 ||
			    ((mvfr0 & MVFR0_SP_MASK) >> MVFR0_SP_BIT) == 0x2) {
				elf_hwcap |= HWCAP_VFPv3;
				/*
				 * Check for VFPv3 D16 and VFPv4 D16.  CPUs in
				 * this configuration only have 16 x 64bit
				 * registers.
				 */
				if ((mvfr0 & MVFR0_A_SIMD_MASK) == 1)
					/* also v4-D16 */
					elf_hwcap |= HWCAP_VFPv3D16;
				else
					elf_hwcap |= HWCAP_VFPD32;
			}

			if ((fmrx(MVFR1) & 0xf0000000) == 0x10000000)
				elf_hwcap |= HWCAP_VFPv4;
			if (((fmrx(MVFR1) & MVFR1_ASIMDHP_MASK) >> MVFR1_ASIMDHP_BIT) == 0x2)
				elf_hwcap |= HWCAP_ASIMDHP;
			if (((fmrx(MVFR1) & MVFR1_FPHP_MASK) >> MVFR1_FPHP_BIT) == 0x3)
				elf_hwcap |= HWCAP_FPHP;
		}

		/*
		 * Check for the presence of Advanced SIMD Dot Product
		 * instructions.
		 */
		isar6 = read_cpuid_ext(CPUID_EXT_ISAR6);
		if (cpuid_feature_extract_field(isar6, 4) == 0x1)
			elf_hwcap |= HWCAP_ASIMDDP;
		/*
		 * Check for the presence of Advanced SIMD Floating point
		 * half-precision multiplication instructions.
		 */
		if (cpuid_feature_extract_field(isar6, 8) == 0x1)
			elf_hwcap |= HWCAP_ASIMDFHM;
		/*
		 * Check for the presence of Advanced SIMD Bfloat16
		 * floating point instructions.
		 */
		if (cpuid_feature_extract_field(isar6, 20) == 0x1)
			elf_hwcap |= HWCAP_ASIMDBF16;
		/*
		 * Check for the presence of Advanced SIMD and floating point
		 * Int8 matrix multiplication instructions instructions.
		 */
		if (cpuid_feature_extract_field(isar6, 24) == 0x1)
			elf_hwcap |= HWCAP_I8MM;

	/* Extract the architecture version on pre-cpuid scheme */
	} else {
		if (vfpsid & FPSID_NODOUBLE) {
			pr_cont("no double precision support\n");
			return 0;
		}

		VFP_arch = (vfpsid & FPSID_ARCH_MASK) >> FPSID_ARCH_BIT;
	}

	cpuhp_setup_state_nocalls(CPUHP_AP_ARM_VFP_STARTING,
				  "arm/vfp:starting", vfp_starting_cpu,
				  vfp_dying_cpu);

	have_vfp = true;

	register_undef_hook(&vfp_support_hook);
	thread_register_notifier(&vfp_notifier_block);
	vfp_pm_init();

	/*
	 * We detected VFP, and the support code is
	 * in place; report VFP support to userspace.
	 */
	elf_hwcap |= HWCAP_VFP;

	pr_cont("implementor %02x architecture %d part %02x variant %x rev %x\n",
		(vfpsid & FPSID_IMPLEMENTER_MASK) >> FPSID_IMPLEMENTER_BIT,
		VFP_arch,
		(vfpsid & FPSID_PART_MASK) >> FPSID_PART_BIT,
		(vfpsid & FPSID_VARIANT_MASK) >> FPSID_VARIANT_BIT,
		(vfpsid & FPSID_REV_MASK) >> FPSID_REV_BIT);

	return 0;
}

core_initcall(vfp_init);
