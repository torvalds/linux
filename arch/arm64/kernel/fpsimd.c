// SPDX-License-Identifier: GPL-2.0-only
/*
 * FP/SIMD context switching and fault handling
 *
 * Copyright (C) 2012 ARM Ltd.
 * Author: Catalin Marinas <catalin.marinas@arm.com>
 */

#include <linux/bitmap.h>
#include <linux/bitops.h>
#include <linux/bottom_half.h>
#include <linux/bug.h>
#include <linux/cache.h>
#include <linux/compat.h>
#include <linux/compiler.h>
#include <linux/cpu.h>
#include <linux/cpu_pm.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/irqflags.h>
#include <linux/init.h>
#include <linux/percpu.h>
#include <linux/prctl.h>
#include <linux/preempt.h>
#include <linux/ptrace.h>
#include <linux/sched/signal.h>
#include <linux/sched/task_stack.h>
#include <linux/signal.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/sysctl.h>
#include <linux/swab.h>

#include <asm/esr.h>
#include <asm/exception.h>
#include <asm/fpsimd.h>
#include <asm/cpufeature.h>
#include <asm/cputype.h>
#include <asm/neon.h>
#include <asm/processor.h>
#include <asm/simd.h>
#include <asm/sigcontext.h>
#include <asm/sysreg.h>
#include <asm/traps.h>
#include <asm/virt.h>

#define FPEXC_IOF	(1 << 0)
#define FPEXC_DZF	(1 << 1)
#define FPEXC_OFF	(1 << 2)
#define FPEXC_UFF	(1 << 3)
#define FPEXC_IXF	(1 << 4)
#define FPEXC_IDF	(1 << 7)

/*
 * (Note: in this discussion, statements about FPSIMD apply equally to SVE.)
 *
 * In order to reduce the number of times the FPSIMD state is needlessly saved
 * and restored, we need to keep track of two things:
 * (a) for each task, we need to remember which CPU was the last one to have
 *     the task's FPSIMD state loaded into its FPSIMD registers;
 * (b) for each CPU, we need to remember which task's userland FPSIMD state has
 *     been loaded into its FPSIMD registers most recently, or whether it has
 *     been used to perform kernel mode NEON in the meantime.
 *
 * For (a), we add a fpsimd_cpu field to thread_struct, which gets updated to
 * the id of the current CPU every time the state is loaded onto a CPU. For (b),
 * we add the per-cpu variable 'fpsimd_last_state' (below), which contains the
 * address of the userland FPSIMD state of the task that was loaded onto the CPU
 * the most recently, or NULL if kernel mode NEON has been performed after that.
 *
 * With this in place, we no longer have to restore the next FPSIMD state right
 * when switching between tasks. Instead, we can defer this check to userland
 * resume, at which time we verify whether the CPU's fpsimd_last_state and the
 * task's fpsimd_cpu are still mutually in sync. If this is the case, we
 * can omit the FPSIMD restore.
 *
 * As an optimization, we use the thread_info flag TIF_FOREIGN_FPSTATE to
 * indicate whether or not the userland FPSIMD state of the current task is
 * present in the registers. The flag is set unless the FPSIMD registers of this
 * CPU currently contain the most recent userland FPSIMD state of the current
 * task. If the task is behaving as a VMM, then this is will be managed by
 * KVM which will clear it to indicate that the vcpu FPSIMD state is currently
 * loaded on the CPU, allowing the state to be saved if a FPSIMD-aware
 * softirq kicks in. Upon vcpu_put(), KVM will save the vcpu FP state and
 * flag the register state as invalid.
 *
 * In order to allow softirq handlers to use FPSIMD, kernel_neon_begin() may be
 * called from softirq context, which will save the task's FPSIMD context back
 * to task_struct. To prevent this from racing with the manipulation of the
 * task's FPSIMD state from task context and thereby corrupting the state, it
 * is necessary to protect any manipulation of a task's fpsimd_state or
 * TIF_FOREIGN_FPSTATE flag with get_cpu_fpsimd_context(), which will suspend
 * softirq servicing entirely until put_cpu_fpsimd_context() is called.
 *
 * For a certain task, the sequence may look something like this:
 * - the task gets scheduled in; if both the task's fpsimd_cpu field
 *   contains the id of the current CPU, and the CPU's fpsimd_last_state per-cpu
 *   variable points to the task's fpsimd_state, the TIF_FOREIGN_FPSTATE flag is
 *   cleared, otherwise it is set;
 *
 * - the task returns to userland; if TIF_FOREIGN_FPSTATE is set, the task's
 *   userland FPSIMD state is copied from memory to the registers, the task's
 *   fpsimd_cpu field is set to the id of the current CPU, the current
 *   CPU's fpsimd_last_state pointer is set to this task's fpsimd_state and the
 *   TIF_FOREIGN_FPSTATE flag is cleared;
 *
 * - the task executes an ordinary syscall; upon return to userland, the
 *   TIF_FOREIGN_FPSTATE flag will still be cleared, so no FPSIMD state is
 *   restored;
 *
 * - the task executes a syscall which executes some NEON instructions; this is
 *   preceded by a call to kernel_neon_begin(), which copies the task's FPSIMD
 *   register contents to memory, clears the fpsimd_last_state per-cpu variable
 *   and sets the TIF_FOREIGN_FPSTATE flag;
 *
 * - the task gets preempted after kernel_neon_end() is called; as we have not
 *   returned from the 2nd syscall yet, TIF_FOREIGN_FPSTATE is still set so
 *   whatever is in the FPSIMD registers is not saved to memory, but discarded.
 */

static DEFINE_PER_CPU(struct cpu_fp_state, fpsimd_last_state);

__ro_after_init struct vl_info vl_info[ARM64_VEC_MAX] = {
#ifdef CONFIG_ARM64_SVE
	[ARM64_VEC_SVE] = {
		.type			= ARM64_VEC_SVE,
		.name			= "SVE",
		.min_vl			= SVE_VL_MIN,
		.max_vl			= SVE_VL_MIN,
		.max_virtualisable_vl	= SVE_VL_MIN,
	},
#endif
#ifdef CONFIG_ARM64_SME
	[ARM64_VEC_SME] = {
		.type			= ARM64_VEC_SME,
		.name			= "SME",
	},
#endif
};

static unsigned int vec_vl_inherit_flag(enum vec_type type)
{
	switch (type) {
	case ARM64_VEC_SVE:
		return TIF_SVE_VL_INHERIT;
	case ARM64_VEC_SME:
		return TIF_SME_VL_INHERIT;
	default:
		WARN_ON_ONCE(1);
		return 0;
	}
}

struct vl_config {
	int __default_vl;		/* Default VL for tasks */
};

static struct vl_config vl_config[ARM64_VEC_MAX];

static inline int get_default_vl(enum vec_type type)
{
	return READ_ONCE(vl_config[type].__default_vl);
}

#ifdef CONFIG_ARM64_SVE

static inline int get_sve_default_vl(void)
{
	return get_default_vl(ARM64_VEC_SVE);
}

static inline void set_default_vl(enum vec_type type, int val)
{
	WRITE_ONCE(vl_config[type].__default_vl, val);
}

static inline void set_sve_default_vl(int val)
{
	set_default_vl(ARM64_VEC_SVE, val);
}

static void __percpu *efi_sve_state;

#else /* ! CONFIG_ARM64_SVE */

/* Dummy declaration for code that will be optimised out: */
extern void __percpu *efi_sve_state;

#endif /* ! CONFIG_ARM64_SVE */

#ifdef CONFIG_ARM64_SME

static int get_sme_default_vl(void)
{
	return get_default_vl(ARM64_VEC_SME);
}

static void set_sme_default_vl(int val)
{
	set_default_vl(ARM64_VEC_SME, val);
}

static void sme_free(struct task_struct *);

#else

static inline void sme_free(struct task_struct *t) { }

#endif

static void fpsimd_bind_task_to_cpu(void);

/*
 * Claim ownership of the CPU FPSIMD context for use by the calling context.
 *
 * The caller may freely manipulate the FPSIMD context metadata until
 * put_cpu_fpsimd_context() is called.
 *
 * On RT kernels local_bh_disable() is not sufficient because it only
 * serializes soft interrupt related sections via a local lock, but stays
 * preemptible. Disabling preemption is the right choice here as bottom
 * half processing is always in thread context on RT kernels so it
 * implicitly prevents bottom half processing as well.
 */
static void get_cpu_fpsimd_context(void)
{
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_bh_disable();
	else
		preempt_disable();
}

/*
 * Release the CPU FPSIMD context.
 *
 * Must be called from a context in which get_cpu_fpsimd_context() was
 * previously called, with no call to put_cpu_fpsimd_context() in the
 * meantime.
 */
static void put_cpu_fpsimd_context(void)
{
	if (!IS_ENABLED(CONFIG_PREEMPT_RT))
		local_bh_enable();
	else
		preempt_enable();
}

unsigned int task_get_vl(const struct task_struct *task, enum vec_type type)
{
	return task->thread.vl[type];
}

void task_set_vl(struct task_struct *task, enum vec_type type,
		 unsigned long vl)
{
	task->thread.vl[type] = vl;
}

unsigned int task_get_vl_onexec(const struct task_struct *task,
				enum vec_type type)
{
	return task->thread.vl_onexec[type];
}

void task_set_vl_onexec(struct task_struct *task, enum vec_type type,
			unsigned long vl)
{
	task->thread.vl_onexec[type] = vl;
}

/*
 * TIF_SME controls whether a task can use SME without trapping while
 * in userspace, when TIF_SME is set then we must have storage
 * allocated in sve_state and sme_state to store the contents of both ZA
 * and the SVE registers for both streaming and non-streaming modes.
 *
 * If both SVCR.ZA and SVCR.SM are disabled then at any point we
 * may disable TIF_SME and reenable traps.
 */


/*
 * TIF_SVE controls whether a task can use SVE without trapping while
 * in userspace, and also (together with TIF_SME) the way a task's
 * FPSIMD/SVE state is stored in thread_struct.
 *
 * The kernel uses this flag to track whether a user task is actively
 * using SVE, and therefore whether full SVE register state needs to
 * be tracked.  If not, the cheaper FPSIMD context handling code can
 * be used instead of the more costly SVE equivalents.
 *
 *  * TIF_SVE or SVCR.SM set:
 *
 *    The task can execute SVE instructions while in userspace without
 *    trapping to the kernel.
 *
 *    During any syscall, the kernel may optionally clear TIF_SVE and
 *    discard the vector state except for the FPSIMD subset.
 *
 *  * TIF_SVE clear:
 *
 *    An attempt by the user task to execute an SVE instruction causes
 *    do_sve_acc() to be called, which does some preparation and then
 *    sets TIF_SVE.
 *
 * During any syscall, the kernel may optionally clear TIF_SVE and
 * discard the vector state except for the FPSIMD subset.
 *
 * The data will be stored in one of two formats:
 *
 *  * FPSIMD only - FP_STATE_FPSIMD:
 *
 *    When the FPSIMD only state stored task->thread.fp_type is set to
 *    FP_STATE_FPSIMD, the FPSIMD registers V0-V31 are encoded in
 *    task->thread.uw.fpsimd_state; bits [max : 128] for each of Z0-Z31 are
 *    logically zero but not stored anywhere; P0-P15 and FFR are not
 *    stored and have unspecified values from userspace's point of
 *    view.  For hygiene purposes, the kernel zeroes them on next use,
 *    but userspace is discouraged from relying on this.
 *
 *    task->thread.sve_state does not need to be non-NULL, valid or any
 *    particular size: it must not be dereferenced and any data stored
 *    there should be considered stale and not referenced.
 *
 *  * SVE state - FP_STATE_SVE:
 *
 *    When the full SVE state is stored task->thread.fp_type is set to
 *    FP_STATE_SVE and Z0-Z31 (incorporating Vn in bits[127:0] or the
 *    corresponding Zn), P0-P15 and FFR are encoded in in
 *    task->thread.sve_state, formatted appropriately for vector
 *    length task->thread.sve_vl or, if SVCR.SM is set,
 *    task->thread.sme_vl. The storage for the vector registers in
 *    task->thread.uw.fpsimd_state should be ignored.
 *
 *    task->thread.sve_state must point to a valid buffer at least
 *    sve_state_size(task) bytes in size. The data stored in
 *    task->thread.uw.fpsimd_state.vregs should be considered stale
 *    and not referenced.
 *
 *  * FPSR and FPCR are always stored in task->thread.uw.fpsimd_state
 *    irrespective of whether TIF_SVE is clear or set, since these are
 *    not vector length dependent.
 */

/*
 * Update current's FPSIMD/SVE registers from thread_struct.
 *
 * This function should be called only when the FPSIMD/SVE state in
 * thread_struct is known to be up to date, when preparing to enter
 * userspace.
 */
static void task_fpsimd_load(void)
{
	bool restore_sve_regs = false;
	bool restore_ffr;

	WARN_ON(!system_supports_fpsimd());
	WARN_ON(preemptible());
	WARN_ON(test_thread_flag(TIF_KERNEL_FPSTATE));

	if (system_supports_fpmr())
		write_sysreg_s(current->thread.uw.fpmr, SYS_FPMR);

	if (system_supports_sve() || system_supports_sme()) {
		switch (current->thread.fp_type) {
		case FP_STATE_FPSIMD:
			/* Stop tracking SVE for this task until next use. */
			if (test_and_clear_thread_flag(TIF_SVE))
				sve_user_disable();
			break;
		case FP_STATE_SVE:
			if (!thread_sm_enabled(&current->thread) &&
			    !WARN_ON_ONCE(!test_and_set_thread_flag(TIF_SVE)))
				sve_user_enable();

			if (test_thread_flag(TIF_SVE))
				sve_set_vq(sve_vq_from_vl(task_get_sve_vl(current)) - 1);

			restore_sve_regs = true;
			restore_ffr = true;
			break;
		default:
			/*
			 * This indicates either a bug in
			 * fpsimd_save_user_state() or memory corruption, we
			 * should always record an explicit format
			 * when we save. We always at least have the
			 * memory allocated for FPSMID registers so
			 * try that and hope for the best.
			 */
			WARN_ON_ONCE(1);
			clear_thread_flag(TIF_SVE);
			break;
		}
	}

	/* Restore SME, override SVE register configuration if needed */
	if (system_supports_sme()) {
		unsigned long sme_vl = task_get_sme_vl(current);

		/* Ensure VL is set up for restoring data */
		if (test_thread_flag(TIF_SME))
			sme_set_vq(sve_vq_from_vl(sme_vl) - 1);

		write_sysreg_s(current->thread.svcr, SYS_SVCR);

		if (thread_za_enabled(&current->thread))
			sme_load_state(current->thread.sme_state,
				       system_supports_sme2());

		if (thread_sm_enabled(&current->thread))
			restore_ffr = system_supports_fa64();
	}

	if (restore_sve_regs) {
		WARN_ON_ONCE(current->thread.fp_type != FP_STATE_SVE);
		sve_load_state(sve_pffr(&current->thread),
			       &current->thread.uw.fpsimd_state.fpsr,
			       restore_ffr);
	} else {
		WARN_ON_ONCE(current->thread.fp_type != FP_STATE_FPSIMD);
		fpsimd_load_state(&current->thread.uw.fpsimd_state);
	}
}

/*
 * Ensure FPSIMD/SVE storage in memory for the loaded context is up to
 * date with respect to the CPU registers. Note carefully that the
 * current context is the context last bound to the CPU stored in
 * last, if KVM is involved this may be the guest VM context rather
 * than the host thread for the VM pointed to by current. This means
 * that we must always reference the state storage via last rather
 * than via current, if we are saving KVM state then it will have
 * ensured that the type of registers to save is set in last->to_save.
 */
static void fpsimd_save_user_state(void)
{
	struct cpu_fp_state const *last =
		this_cpu_ptr(&fpsimd_last_state);
	/* set by fpsimd_bind_task_to_cpu() or fpsimd_bind_state_to_cpu() */
	bool save_sve_regs = false;
	bool save_ffr;
	unsigned int vl;

	WARN_ON(!system_supports_fpsimd());
	WARN_ON(preemptible());

	if (test_thread_flag(TIF_FOREIGN_FPSTATE))
		return;

	if (system_supports_fpmr())
		*(last->fpmr) = read_sysreg_s(SYS_FPMR);

	/*
	 * If a task is in a syscall the ABI allows us to only
	 * preserve the state shared with FPSIMD so don't bother
	 * saving the full SVE state in that case.
	 */
	if ((last->to_save == FP_STATE_CURRENT && test_thread_flag(TIF_SVE) &&
	     !in_syscall(current_pt_regs())) ||
	    last->to_save == FP_STATE_SVE) {
		save_sve_regs = true;
		save_ffr = true;
		vl = last->sve_vl;
	}

	if (system_supports_sme()) {
		u64 *svcr = last->svcr;

		*svcr = read_sysreg_s(SYS_SVCR);

		if (*svcr & SVCR_ZA_MASK)
			sme_save_state(last->sme_state,
				       system_supports_sme2());

		/* If we are in streaming mode override regular SVE. */
		if (*svcr & SVCR_SM_MASK) {
			save_sve_regs = true;
			save_ffr = system_supports_fa64();
			vl = last->sme_vl;
		}
	}

	if (IS_ENABLED(CONFIG_ARM64_SVE) && save_sve_regs) {
		/* Get the configured VL from RDVL, will account for SM */
		if (WARN_ON(sve_get_vl() != vl)) {
			/*
			 * Can't save the user regs, so current would
			 * re-enter user with corrupt state.
			 * There's no way to recover, so kill it:
			 */
			force_signal_inject(SIGKILL, SI_KERNEL, 0, 0);
			return;
		}

		sve_save_state((char *)last->sve_state +
					sve_ffr_offset(vl),
			       &last->st->fpsr, save_ffr);
		*last->fp_type = FP_STATE_SVE;
	} else {
		fpsimd_save_state(last->st);
		*last->fp_type = FP_STATE_FPSIMD;
	}
}

/*
 * All vector length selection from userspace comes through here.
 * We're on a slow path, so some sanity-checks are included.
 * If things go wrong there's a bug somewhere, but try to fall back to a
 * safe choice.
 */
static unsigned int find_supported_vector_length(enum vec_type type,
						 unsigned int vl)
{
	struct vl_info *info = &vl_info[type];
	int bit;
	int max_vl = info->max_vl;

	if (WARN_ON(!sve_vl_valid(vl)))
		vl = info->min_vl;

	if (WARN_ON(!sve_vl_valid(max_vl)))
		max_vl = info->min_vl;

	if (vl > max_vl)
		vl = max_vl;
	if (vl < info->min_vl)
		vl = info->min_vl;

	bit = find_next_bit(info->vq_map, SVE_VQ_MAX,
			    __vq_to_bit(sve_vq_from_vl(vl)));
	return sve_vl_from_vq(__bit_to_vq(bit));
}

#if defined(CONFIG_ARM64_SVE) && defined(CONFIG_SYSCTL)

static int vec_proc_do_default_vl(struct ctl_table *table, int write,
				  void *buffer, size_t *lenp, loff_t *ppos)
{
	struct vl_info *info = table->extra1;
	enum vec_type type = info->type;
	int ret;
	int vl = get_default_vl(type);
	struct ctl_table tmp_table = {
		.data = &vl,
		.maxlen = sizeof(vl),
	};

	ret = proc_dointvec(&tmp_table, write, buffer, lenp, ppos);
	if (ret || !write)
		return ret;

	/* Writing -1 has the special meaning "set to max": */
	if (vl == -1)
		vl = info->max_vl;

	if (!sve_vl_valid(vl))
		return -EINVAL;

	set_default_vl(type, find_supported_vector_length(type, vl));
	return 0;
}

static struct ctl_table sve_default_vl_table[] = {
	{
		.procname	= "sve_default_vector_length",
		.mode		= 0644,
		.proc_handler	= vec_proc_do_default_vl,
		.extra1		= &vl_info[ARM64_VEC_SVE],
	},
};

static int __init sve_sysctl_init(void)
{
	if (system_supports_sve())
		if (!register_sysctl("abi", sve_default_vl_table))
			return -EINVAL;

	return 0;
}

#else /* ! (CONFIG_ARM64_SVE && CONFIG_SYSCTL) */
static int __init sve_sysctl_init(void) { return 0; }
#endif /* ! (CONFIG_ARM64_SVE && CONFIG_SYSCTL) */

#if defined(CONFIG_ARM64_SME) && defined(CONFIG_SYSCTL)
static struct ctl_table sme_default_vl_table[] = {
	{
		.procname	= "sme_default_vector_length",
		.mode		= 0644,
		.proc_handler	= vec_proc_do_default_vl,
		.extra1		= &vl_info[ARM64_VEC_SME],
	},
};

static int __init sme_sysctl_init(void)
{
	if (system_supports_sme())
		if (!register_sysctl("abi", sme_default_vl_table))
			return -EINVAL;

	return 0;
}

#else /* ! (CONFIG_ARM64_SME && CONFIG_SYSCTL) */
static int __init sme_sysctl_init(void) { return 0; }
#endif /* ! (CONFIG_ARM64_SME && CONFIG_SYSCTL) */

#define ZREG(sve_state, vq, n) ((char *)(sve_state) +		\
	(SVE_SIG_ZREG_OFFSET(vq, n) - SVE_SIG_REGS_OFFSET))

#ifdef CONFIG_CPU_BIG_ENDIAN
static __uint128_t arm64_cpu_to_le128(__uint128_t x)
{
	u64 a = swab64(x);
	u64 b = swab64(x >> 64);

	return ((__uint128_t)a << 64) | b;
}
#else
static __uint128_t arm64_cpu_to_le128(__uint128_t x)
{
	return x;
}
#endif

#define arm64_le128_to_cpu(x) arm64_cpu_to_le128(x)

static void __fpsimd_to_sve(void *sst, struct user_fpsimd_state const *fst,
			    unsigned int vq)
{
	unsigned int i;
	__uint128_t *p;

	for (i = 0; i < SVE_NUM_ZREGS; ++i) {
		p = (__uint128_t *)ZREG(sst, vq, i);
		*p = arm64_cpu_to_le128(fst->vregs[i]);
	}
}

/*
 * Transfer the FPSIMD state in task->thread.uw.fpsimd_state to
 * task->thread.sve_state.
 *
 * Task can be a non-runnable task, or current.  In the latter case,
 * the caller must have ownership of the cpu FPSIMD context before calling
 * this function.
 * task->thread.sve_state must point to at least sve_state_size(task)
 * bytes of allocated kernel memory.
 * task->thread.uw.fpsimd_state must be up to date before calling this
 * function.
 */
static void fpsimd_to_sve(struct task_struct *task)
{
	unsigned int vq;
	void *sst = task->thread.sve_state;
	struct user_fpsimd_state const *fst = &task->thread.uw.fpsimd_state;

	if (!system_supports_sve() && !system_supports_sme())
		return;

	vq = sve_vq_from_vl(thread_get_cur_vl(&task->thread));
	__fpsimd_to_sve(sst, fst, vq);
}

/*
 * Transfer the SVE state in task->thread.sve_state to
 * task->thread.uw.fpsimd_state.
 *
 * Task can be a non-runnable task, or current.  In the latter case,
 * the caller must have ownership of the cpu FPSIMD context before calling
 * this function.
 * task->thread.sve_state must point to at least sve_state_size(task)
 * bytes of allocated kernel memory.
 * task->thread.sve_state must be up to date before calling this function.
 */
static void sve_to_fpsimd(struct task_struct *task)
{
	unsigned int vq, vl;
	void const *sst = task->thread.sve_state;
	struct user_fpsimd_state *fst = &task->thread.uw.fpsimd_state;
	unsigned int i;
	__uint128_t const *p;

	if (!system_supports_sve() && !system_supports_sme())
		return;

	vl = thread_get_cur_vl(&task->thread);
	vq = sve_vq_from_vl(vl);
	for (i = 0; i < SVE_NUM_ZREGS; ++i) {
		p = (__uint128_t const *)ZREG(sst, vq, i);
		fst->vregs[i] = arm64_le128_to_cpu(*p);
	}
}

void cpu_enable_fpmr(const struct arm64_cpu_capabilities *__always_unused p)
{
	write_sysreg_s(read_sysreg_s(SYS_SCTLR_EL1) | SCTLR_EL1_EnFPM_MASK,
		       SYS_SCTLR_EL1);
}

#ifdef CONFIG_ARM64_SVE
/*
 * Call __sve_free() directly only if you know task can't be scheduled
 * or preempted.
 */
static void __sve_free(struct task_struct *task)
{
	kfree(task->thread.sve_state);
	task->thread.sve_state = NULL;
}

static void sve_free(struct task_struct *task)
{
	WARN_ON(test_tsk_thread_flag(task, TIF_SVE));

	__sve_free(task);
}

/*
 * Return how many bytes of memory are required to store the full SVE
 * state for task, given task's currently configured vector length.
 */
size_t sve_state_size(struct task_struct const *task)
{
	unsigned int vl = 0;

	if (system_supports_sve())
		vl = task_get_sve_vl(task);
	if (system_supports_sme())
		vl = max(vl, task_get_sme_vl(task));

	return SVE_SIG_REGS_SIZE(sve_vq_from_vl(vl));
}

/*
 * Ensure that task->thread.sve_state is allocated and sufficiently large.
 *
 * This function should be used only in preparation for replacing
 * task->thread.sve_state with new data.  The memory is always zeroed
 * here to prevent stale data from showing through: this is done in
 * the interest of testability and predictability: except in the
 * do_sve_acc() case, there is no ABI requirement to hide stale data
 * written previously be task.
 */
void sve_alloc(struct task_struct *task, bool flush)
{
	if (task->thread.sve_state) {
		if (flush)
			memset(task->thread.sve_state, 0,
			       sve_state_size(task));
		return;
	}

	/* This is a small allocation (maximum ~8KB) and Should Not Fail. */
	task->thread.sve_state =
		kzalloc(sve_state_size(task), GFP_KERNEL);
}


/*
 * Force the FPSIMD state shared with SVE to be updated in the SVE state
 * even if the SVE state is the current active state.
 *
 * This should only be called by ptrace.  task must be non-runnable.
 * task->thread.sve_state must point to at least sve_state_size(task)
 * bytes of allocated kernel memory.
 */
void fpsimd_force_sync_to_sve(struct task_struct *task)
{
	fpsimd_to_sve(task);
}

/*
 * Ensure that task->thread.sve_state is up to date with respect to
 * the user task, irrespective of when SVE is in use or not.
 *
 * This should only be called by ptrace.  task must be non-runnable.
 * task->thread.sve_state must point to at least sve_state_size(task)
 * bytes of allocated kernel memory.
 */
void fpsimd_sync_to_sve(struct task_struct *task)
{
	if (!test_tsk_thread_flag(task, TIF_SVE) &&
	    !thread_sm_enabled(&task->thread))
		fpsimd_to_sve(task);
}

/*
 * Ensure that task->thread.uw.fpsimd_state is up to date with respect to
 * the user task, irrespective of whether SVE is in use or not.
 *
 * This should only be called by ptrace.  task must be non-runnable.
 * task->thread.sve_state must point to at least sve_state_size(task)
 * bytes of allocated kernel memory.
 */
void sve_sync_to_fpsimd(struct task_struct *task)
{
	if (task->thread.fp_type == FP_STATE_SVE)
		sve_to_fpsimd(task);
}

/*
 * Ensure that task->thread.sve_state is up to date with respect to
 * the task->thread.uw.fpsimd_state.
 *
 * This should only be called by ptrace to merge new FPSIMD register
 * values into a task for which SVE is currently active.
 * task must be non-runnable.
 * task->thread.sve_state must point to at least sve_state_size(task)
 * bytes of allocated kernel memory.
 * task->thread.uw.fpsimd_state must already have been initialised with
 * the new FPSIMD register values to be merged in.
 */
void sve_sync_from_fpsimd_zeropad(struct task_struct *task)
{
	unsigned int vq;
	void *sst = task->thread.sve_state;
	struct user_fpsimd_state const *fst = &task->thread.uw.fpsimd_state;

	if (!test_tsk_thread_flag(task, TIF_SVE) &&
	    !thread_sm_enabled(&task->thread))
		return;

	vq = sve_vq_from_vl(thread_get_cur_vl(&task->thread));

	memset(sst, 0, SVE_SIG_REGS_SIZE(vq));
	__fpsimd_to_sve(sst, fst, vq);
}

int vec_set_vector_length(struct task_struct *task, enum vec_type type,
			  unsigned long vl, unsigned long flags)
{
	bool free_sme = false;

	if (flags & ~(unsigned long)(PR_SVE_VL_INHERIT |
				     PR_SVE_SET_VL_ONEXEC))
		return -EINVAL;

	if (!sve_vl_valid(vl))
		return -EINVAL;

	/*
	 * Clamp to the maximum vector length that VL-agnostic code
	 * can work with.  A flag may be assigned in the future to
	 * allow setting of larger vector lengths without confusing
	 * older software.
	 */
	if (vl > VL_ARCH_MAX)
		vl = VL_ARCH_MAX;

	vl = find_supported_vector_length(type, vl);

	if (flags & (PR_SVE_VL_INHERIT |
		     PR_SVE_SET_VL_ONEXEC))
		task_set_vl_onexec(task, type, vl);
	else
		/* Reset VL to system default on next exec: */
		task_set_vl_onexec(task, type, 0);

	/* Only actually set the VL if not deferred: */
	if (flags & PR_SVE_SET_VL_ONEXEC)
		goto out;

	if (vl == task_get_vl(task, type))
		goto out;

	/*
	 * To ensure the FPSIMD bits of the SVE vector registers are preserved,
	 * write any live register state back to task_struct, and convert to a
	 * regular FPSIMD thread.
	 */
	if (task == current) {
		get_cpu_fpsimd_context();

		fpsimd_save_user_state();
	}

	fpsimd_flush_task_state(task);
	if (test_and_clear_tsk_thread_flag(task, TIF_SVE) ||
	    thread_sm_enabled(&task->thread)) {
		sve_to_fpsimd(task);
		task->thread.fp_type = FP_STATE_FPSIMD;
	}

	if (system_supports_sme()) {
		if (type == ARM64_VEC_SME ||
		    !(task->thread.svcr & (SVCR_SM_MASK | SVCR_ZA_MASK))) {
			/*
			 * We are changing the SME VL or weren't using
			 * SME anyway, discard the state and force a
			 * reallocation.
			 */
			task->thread.svcr &= ~(SVCR_SM_MASK |
					       SVCR_ZA_MASK);
			clear_tsk_thread_flag(task, TIF_SME);
			free_sme = true;
		}
	}

	if (task == current)
		put_cpu_fpsimd_context();

	task_set_vl(task, type, vl);

	/*
	 * Free the changed states if they are not in use, SME will be
	 * reallocated to the correct size on next use and we just
	 * allocate SVE now in case it is needed for use in streaming
	 * mode.
	 */
	sve_free(task);
	sve_alloc(task, true);

	if (free_sme)
		sme_free(task);

out:
	update_tsk_thread_flag(task, vec_vl_inherit_flag(type),
			       flags & PR_SVE_VL_INHERIT);

	return 0;
}

/*
 * Encode the current vector length and flags for return.
 * This is only required for prctl(): ptrace has separate fields.
 * SVE and SME use the same bits for _ONEXEC and _INHERIT.
 *
 * flags are as for vec_set_vector_length().
 */
static int vec_prctl_status(enum vec_type type, unsigned long flags)
{
	int ret;

	if (flags & PR_SVE_SET_VL_ONEXEC)
		ret = task_get_vl_onexec(current, type);
	else
		ret = task_get_vl(current, type);

	if (test_thread_flag(vec_vl_inherit_flag(type)))
		ret |= PR_SVE_VL_INHERIT;

	return ret;
}

/* PR_SVE_SET_VL */
int sve_set_current_vl(unsigned long arg)
{
	unsigned long vl, flags;
	int ret;

	vl = arg & PR_SVE_VL_LEN_MASK;
	flags = arg & ~vl;

	if (!system_supports_sve() || is_compat_task())
		return -EINVAL;

	ret = vec_set_vector_length(current, ARM64_VEC_SVE, vl, flags);
	if (ret)
		return ret;

	return vec_prctl_status(ARM64_VEC_SVE, flags);
}

/* PR_SVE_GET_VL */
int sve_get_current_vl(void)
{
	if (!system_supports_sve() || is_compat_task())
		return -EINVAL;

	return vec_prctl_status(ARM64_VEC_SVE, 0);
}

#ifdef CONFIG_ARM64_SME
/* PR_SME_SET_VL */
int sme_set_current_vl(unsigned long arg)
{
	unsigned long vl, flags;
	int ret;

	vl = arg & PR_SME_VL_LEN_MASK;
	flags = arg & ~vl;

	if (!system_supports_sme() || is_compat_task())
		return -EINVAL;

	ret = vec_set_vector_length(current, ARM64_VEC_SME, vl, flags);
	if (ret)
		return ret;

	return vec_prctl_status(ARM64_VEC_SME, flags);
}

/* PR_SME_GET_VL */
int sme_get_current_vl(void)
{
	if (!system_supports_sme() || is_compat_task())
		return -EINVAL;

	return vec_prctl_status(ARM64_VEC_SME, 0);
}
#endif /* CONFIG_ARM64_SME */

static void vec_probe_vqs(struct vl_info *info,
			  DECLARE_BITMAP(map, SVE_VQ_MAX))
{
	unsigned int vq, vl;

	bitmap_zero(map, SVE_VQ_MAX);

	for (vq = SVE_VQ_MAX; vq >= SVE_VQ_MIN; --vq) {
		write_vl(info->type, vq - 1); /* self-syncing */

		switch (info->type) {
		case ARM64_VEC_SVE:
			vl = sve_get_vl();
			break;
		case ARM64_VEC_SME:
			vl = sme_get_vl();
			break;
		default:
			vl = 0;
			break;
		}

		/* Minimum VL identified? */
		if (sve_vq_from_vl(vl) > vq)
			break;

		vq = sve_vq_from_vl(vl); /* skip intervening lengths */
		set_bit(__vq_to_bit(vq), map);
	}
}

/*
 * Initialise the set of known supported VQs for the boot CPU.
 * This is called during kernel boot, before secondary CPUs are brought up.
 */
void __init vec_init_vq_map(enum vec_type type)
{
	struct vl_info *info = &vl_info[type];
	vec_probe_vqs(info, info->vq_map);
	bitmap_copy(info->vq_partial_map, info->vq_map, SVE_VQ_MAX);
}

/*
 * If we haven't committed to the set of supported VQs yet, filter out
 * those not supported by the current CPU.
 * This function is called during the bring-up of early secondary CPUs only.
 */
void vec_update_vq_map(enum vec_type type)
{
	struct vl_info *info = &vl_info[type];
	DECLARE_BITMAP(tmp_map, SVE_VQ_MAX);

	vec_probe_vqs(info, tmp_map);
	bitmap_and(info->vq_map, info->vq_map, tmp_map, SVE_VQ_MAX);
	bitmap_or(info->vq_partial_map, info->vq_partial_map, tmp_map,
		  SVE_VQ_MAX);
}

/*
 * Check whether the current CPU supports all VQs in the committed set.
 * This function is called during the bring-up of late secondary CPUs only.
 */
int vec_verify_vq_map(enum vec_type type)
{
	struct vl_info *info = &vl_info[type];
	DECLARE_BITMAP(tmp_map, SVE_VQ_MAX);
	unsigned long b;

	vec_probe_vqs(info, tmp_map);

	bitmap_complement(tmp_map, tmp_map, SVE_VQ_MAX);
	if (bitmap_intersects(tmp_map, info->vq_map, SVE_VQ_MAX)) {
		pr_warn("%s: cpu%d: Required vector length(s) missing\n",
			info->name, smp_processor_id());
		return -EINVAL;
	}

	if (!IS_ENABLED(CONFIG_KVM) || !is_hyp_mode_available())
		return 0;

	/*
	 * For KVM, it is necessary to ensure that this CPU doesn't
	 * support any vector length that guests may have probed as
	 * unsupported.
	 */

	/* Recover the set of supported VQs: */
	bitmap_complement(tmp_map, tmp_map, SVE_VQ_MAX);
	/* Find VQs supported that are not globally supported: */
	bitmap_andnot(tmp_map, tmp_map, info->vq_map, SVE_VQ_MAX);

	/* Find the lowest such VQ, if any: */
	b = find_last_bit(tmp_map, SVE_VQ_MAX);
	if (b >= SVE_VQ_MAX)
		return 0; /* no mismatches */

	/*
	 * Mismatches above sve_max_virtualisable_vl are fine, since
	 * no guest is allowed to configure ZCR_EL2.LEN to exceed this:
	 */
	if (sve_vl_from_vq(__bit_to_vq(b)) <= info->max_virtualisable_vl) {
		pr_warn("%s: cpu%d: Unsupported vector length(s) present\n",
			info->name, smp_processor_id());
		return -EINVAL;
	}

	return 0;
}

static void __init sve_efi_setup(void)
{
	int max_vl = 0;
	int i;

	if (!IS_ENABLED(CONFIG_EFI))
		return;

	for (i = 0; i < ARRAY_SIZE(vl_info); i++)
		max_vl = max(vl_info[i].max_vl, max_vl);

	/*
	 * alloc_percpu() warns and prints a backtrace if this goes wrong.
	 * This is evidence of a crippled system and we are returning void,
	 * so no attempt is made to handle this situation here.
	 */
	if (!sve_vl_valid(max_vl))
		goto fail;

	efi_sve_state = __alloc_percpu(
		SVE_SIG_REGS_SIZE(sve_vq_from_vl(max_vl)), SVE_VQ_BYTES);
	if (!efi_sve_state)
		goto fail;

	return;

fail:
	panic("Cannot allocate percpu memory for EFI SVE save/restore");
}

void cpu_enable_sve(const struct arm64_cpu_capabilities *__always_unused p)
{
	write_sysreg(read_sysreg(CPACR_EL1) | CPACR_EL1_ZEN_EL1EN, CPACR_EL1);
	isb();

	write_sysreg_s(0, SYS_ZCR_EL1);
}

void __init sve_setup(void)
{
	struct vl_info *info = &vl_info[ARM64_VEC_SVE];
	DECLARE_BITMAP(tmp_map, SVE_VQ_MAX);
	unsigned long b;
	int max_bit;

	if (!system_supports_sve())
		return;

	/*
	 * The SVE architecture mandates support for 128-bit vectors,
	 * so sve_vq_map must have at least SVE_VQ_MIN set.
	 * If something went wrong, at least try to patch it up:
	 */
	if (WARN_ON(!test_bit(__vq_to_bit(SVE_VQ_MIN), info->vq_map)))
		set_bit(__vq_to_bit(SVE_VQ_MIN), info->vq_map);

	max_bit = find_first_bit(info->vq_map, SVE_VQ_MAX);
	info->max_vl = sve_vl_from_vq(__bit_to_vq(max_bit));

	/*
	 * For the default VL, pick the maximum supported value <= 64.
	 * VL == 64 is guaranteed not to grow the signal frame.
	 */
	set_sve_default_vl(find_supported_vector_length(ARM64_VEC_SVE, 64));

	bitmap_andnot(tmp_map, info->vq_partial_map, info->vq_map,
		      SVE_VQ_MAX);

	b = find_last_bit(tmp_map, SVE_VQ_MAX);
	if (b >= SVE_VQ_MAX)
		/* No non-virtualisable VLs found */
		info->max_virtualisable_vl = SVE_VQ_MAX;
	else if (WARN_ON(b == SVE_VQ_MAX - 1))
		/* No virtualisable VLs?  This is architecturally forbidden. */
		info->max_virtualisable_vl = SVE_VQ_MIN;
	else /* b + 1 < SVE_VQ_MAX */
		info->max_virtualisable_vl = sve_vl_from_vq(__bit_to_vq(b + 1));

	if (info->max_virtualisable_vl > info->max_vl)
		info->max_virtualisable_vl = info->max_vl;

	pr_info("%s: maximum available vector length %u bytes per vector\n",
		info->name, info->max_vl);
	pr_info("%s: default vector length %u bytes per vector\n",
		info->name, get_sve_default_vl());

	/* KVM decides whether to support mismatched systems. Just warn here: */
	if (sve_max_virtualisable_vl() < sve_max_vl())
		pr_warn("%s: unvirtualisable vector lengths present\n",
			info->name);

	sve_efi_setup();
}

/*
 * Called from the put_task_struct() path, which cannot get here
 * unless dead_task is really dead and not schedulable.
 */
void fpsimd_release_task(struct task_struct *dead_task)
{
	__sve_free(dead_task);
	sme_free(dead_task);
}

#endif /* CONFIG_ARM64_SVE */

#ifdef CONFIG_ARM64_SME

/*
 * Ensure that task->thread.sme_state is allocated and sufficiently large.
 *
 * This function should be used only in preparation for replacing
 * task->thread.sme_state with new data.  The memory is always zeroed
 * here to prevent stale data from showing through: this is done in
 * the interest of testability and predictability, the architecture
 * guarantees that when ZA is enabled it will be zeroed.
 */
void sme_alloc(struct task_struct *task, bool flush)
{
	if (task->thread.sme_state) {
		if (flush)
			memset(task->thread.sme_state, 0,
			       sme_state_size(task));
		return;
	}

	/* This could potentially be up to 64K. */
	task->thread.sme_state =
		kzalloc(sme_state_size(task), GFP_KERNEL);
}

static void sme_free(struct task_struct *task)
{
	kfree(task->thread.sme_state);
	task->thread.sme_state = NULL;
}

void cpu_enable_sme(const struct arm64_cpu_capabilities *__always_unused p)
{
	/* Set priority for all PEs to architecturally defined minimum */
	write_sysreg_s(read_sysreg_s(SYS_SMPRI_EL1) & ~SMPRI_EL1_PRIORITY_MASK,
		       SYS_SMPRI_EL1);

	/* Allow SME in kernel */
	write_sysreg(read_sysreg(CPACR_EL1) | CPACR_EL1_SMEN_EL1EN, CPACR_EL1);
	isb();

	/* Ensure all bits in SMCR are set to known values */
	write_sysreg_s(0, SYS_SMCR_EL1);

	/* Allow EL0 to access TPIDR2 */
	write_sysreg(read_sysreg(SCTLR_EL1) | SCTLR_ELx_ENTP2, SCTLR_EL1);
	isb();
}

void cpu_enable_sme2(const struct arm64_cpu_capabilities *__always_unused p)
{
	/* This must be enabled after SME */
	BUILD_BUG_ON(ARM64_SME2 <= ARM64_SME);

	/* Allow use of ZT0 */
	write_sysreg_s(read_sysreg_s(SYS_SMCR_EL1) | SMCR_ELx_EZT0_MASK,
		       SYS_SMCR_EL1);
}

void cpu_enable_fa64(const struct arm64_cpu_capabilities *__always_unused p)
{
	/* This must be enabled after SME */
	BUILD_BUG_ON(ARM64_SME_FA64 <= ARM64_SME);

	/* Allow use of FA64 */
	write_sysreg_s(read_sysreg_s(SYS_SMCR_EL1) | SMCR_ELx_FA64_MASK,
		       SYS_SMCR_EL1);
}

void __init sme_setup(void)
{
	struct vl_info *info = &vl_info[ARM64_VEC_SME];
	int min_bit, max_bit;

	if (!system_supports_sme())
		return;

	/*
	 * SME doesn't require any particular vector length be
	 * supported but it does require at least one.  We should have
	 * disabled the feature entirely while bringing up CPUs but
	 * let's double check here.  The bitmap is SVE_VQ_MAP sized for
	 * sharing with SVE.
	 */
	WARN_ON(bitmap_empty(info->vq_map, SVE_VQ_MAX));

	min_bit = find_last_bit(info->vq_map, SVE_VQ_MAX);
	info->min_vl = sve_vl_from_vq(__bit_to_vq(min_bit));

	max_bit = find_first_bit(info->vq_map, SVE_VQ_MAX);
	info->max_vl = sve_vl_from_vq(__bit_to_vq(max_bit));

	WARN_ON(info->min_vl > info->max_vl);

	/*
	 * For the default VL, pick the maximum supported value <= 32
	 * (256 bits) if there is one since this is guaranteed not to
	 * grow the signal frame when in streaming mode, otherwise the
	 * minimum available VL will be used.
	 */
	set_sme_default_vl(find_supported_vector_length(ARM64_VEC_SME, 32));

	pr_info("SME: minimum available vector length %u bytes per vector\n",
		info->min_vl);
	pr_info("SME: maximum available vector length %u bytes per vector\n",
		info->max_vl);
	pr_info("SME: default vector length %u bytes per vector\n",
		get_sme_default_vl());
}

void sme_suspend_exit(void)
{
	u64 smcr = 0;

	if (!system_supports_sme())
		return;

	if (system_supports_fa64())
		smcr |= SMCR_ELx_FA64;
	if (system_supports_sme2())
		smcr |= SMCR_ELx_EZT0;

	write_sysreg_s(smcr, SYS_SMCR_EL1);
	write_sysreg_s(0, SYS_SMPRI_EL1);
}

#endif /* CONFIG_ARM64_SME */

static void sve_init_regs(void)
{
	/*
	 * Convert the FPSIMD state to SVE, zeroing all the state that
	 * is not shared with FPSIMD. If (as is likely) the current
	 * state is live in the registers then do this there and
	 * update our metadata for the current task including
	 * disabling the trap, otherwise update our in-memory copy.
	 * We are guaranteed to not be in streaming mode, we can only
	 * take a SVE trap when not in streaming mode and we can't be
	 * in streaming mode when taking a SME trap.
	 */
	if (!test_thread_flag(TIF_FOREIGN_FPSTATE)) {
		unsigned long vq_minus_one =
			sve_vq_from_vl(task_get_sve_vl(current)) - 1;
		sve_set_vq(vq_minus_one);
		sve_flush_live(true, vq_minus_one);
		fpsimd_bind_task_to_cpu();
	} else {
		fpsimd_to_sve(current);
		current->thread.fp_type = FP_STATE_SVE;
	}
}

/*
 * Trapped SVE access
 *
 * Storage is allocated for the full SVE state, the current FPSIMD
 * register contents are migrated across, and the access trap is
 * disabled.
 *
 * TIF_SVE should be clear on entry: otherwise, fpsimd_restore_current_state()
 * would have disabled the SVE access trap for userspace during
 * ret_to_user, making an SVE access trap impossible in that case.
 */
void do_sve_acc(unsigned long esr, struct pt_regs *regs)
{
	/* Even if we chose not to use SVE, the hardware could still trap: */
	if (unlikely(!system_supports_sve()) || WARN_ON(is_compat_task())) {
		force_signal_inject(SIGILL, ILL_ILLOPC, regs->pc, 0);
		return;
	}

	sve_alloc(current, true);
	if (!current->thread.sve_state) {
		force_sig(SIGKILL);
		return;
	}

	get_cpu_fpsimd_context();

	if (test_and_set_thread_flag(TIF_SVE))
		WARN_ON(1); /* SVE access shouldn't have trapped */

	/*
	 * Even if the task can have used streaming mode we can only
	 * generate SVE access traps in normal SVE mode and
	 * transitioning out of streaming mode may discard any
	 * streaming mode state.  Always clear the high bits to avoid
	 * any potential errors tracking what is properly initialised.
	 */
	sve_init_regs();

	put_cpu_fpsimd_context();
}

/*
 * Trapped SME access
 *
 * Storage is allocated for the full SVE and SME state, the current
 * FPSIMD register contents are migrated to SVE if SVE is not already
 * active, and the access trap is disabled.
 *
 * TIF_SME should be clear on entry: otherwise, fpsimd_restore_current_state()
 * would have disabled the SME access trap for userspace during
 * ret_to_user, making an SME access trap impossible in that case.
 */
void do_sme_acc(unsigned long esr, struct pt_regs *regs)
{
	/* Even if we chose not to use SME, the hardware could still trap: */
	if (unlikely(!system_supports_sme()) || WARN_ON(is_compat_task())) {
		force_signal_inject(SIGILL, ILL_ILLOPC, regs->pc, 0);
		return;
	}

	/*
	 * If this not a trap due to SME being disabled then something
	 * is being used in the wrong mode, report as SIGILL.
	 */
	if (ESR_ELx_ISS(esr) != ESR_ELx_SME_ISS_SME_DISABLED) {
		force_signal_inject(SIGILL, ILL_ILLOPC, regs->pc, 0);
		return;
	}

	sve_alloc(current, false);
	sme_alloc(current, true);
	if (!current->thread.sve_state || !current->thread.sme_state) {
		force_sig(SIGKILL);
		return;
	}

	get_cpu_fpsimd_context();

	/* With TIF_SME userspace shouldn't generate any traps */
	if (test_and_set_thread_flag(TIF_SME))
		WARN_ON(1);

	if (!test_thread_flag(TIF_FOREIGN_FPSTATE)) {
		unsigned long vq_minus_one =
			sve_vq_from_vl(task_get_sme_vl(current)) - 1;
		sme_set_vq(vq_minus_one);

		fpsimd_bind_task_to_cpu();
	}

	put_cpu_fpsimd_context();
}

/*
 * Trapped FP/ASIMD access.
 */
void do_fpsimd_acc(unsigned long esr, struct pt_regs *regs)
{
	/* Even if we chose not to use FPSIMD, the hardware could still trap: */
	if (!system_supports_fpsimd()) {
		force_signal_inject(SIGILL, ILL_ILLOPC, regs->pc, 0);
		return;
	}

	/*
	 * When FPSIMD is enabled, we should never take a trap unless something
	 * has gone very wrong.
	 */
	BUG();
}

/*
 * Raise a SIGFPE for the current process.
 */
void do_fpsimd_exc(unsigned long esr, struct pt_regs *regs)
{
	unsigned int si_code = FPE_FLTUNK;

	if (esr & ESR_ELx_FP_EXC_TFV) {
		if (esr & FPEXC_IOF)
			si_code = FPE_FLTINV;
		else if (esr & FPEXC_DZF)
			si_code = FPE_FLTDIV;
		else if (esr & FPEXC_OFF)
			si_code = FPE_FLTOVF;
		else if (esr & FPEXC_UFF)
			si_code = FPE_FLTUND;
		else if (esr & FPEXC_IXF)
			si_code = FPE_FLTRES;
	}

	send_sig_fault(SIGFPE, si_code,
		       (void __user *)instruction_pointer(regs),
		       current);
}

static void fpsimd_load_kernel_state(struct task_struct *task)
{
	struct cpu_fp_state *last = this_cpu_ptr(&fpsimd_last_state);

	/*
	 * Elide the load if this CPU holds the most recent kernel mode
	 * FPSIMD context of the current task.
	 */
	if (last->st == &task->thread.kernel_fpsimd_state &&
	    task->thread.kernel_fpsimd_cpu == smp_processor_id())
		return;

	fpsimd_load_state(&task->thread.kernel_fpsimd_state);
}

static void fpsimd_save_kernel_state(struct task_struct *task)
{
	struct cpu_fp_state cpu_fp_state = {
		.st		= &task->thread.kernel_fpsimd_state,
		.to_save	= FP_STATE_FPSIMD,
	};

	fpsimd_save_state(&task->thread.kernel_fpsimd_state);
	fpsimd_bind_state_to_cpu(&cpu_fp_state);

	task->thread.kernel_fpsimd_cpu = smp_processor_id();
}

/*
 * Invalidate any task's FPSIMD state that is present on this cpu.
 * The FPSIMD context should be acquired with get_cpu_fpsimd_context()
 * before calling this function.
 */
static void fpsimd_flush_cpu_state(void)
{
	WARN_ON(!system_supports_fpsimd());
	__this_cpu_write(fpsimd_last_state.st, NULL);

	/*
	 * Leaving streaming mode enabled will cause issues for any kernel
	 * NEON and leaving streaming mode or ZA enabled may increase power
	 * consumption.
	 */
	if (system_supports_sme())
		sme_smstop();

	set_thread_flag(TIF_FOREIGN_FPSTATE);
}

void fpsimd_thread_switch(struct task_struct *next)
{
	bool wrong_task, wrong_cpu;

	if (!system_supports_fpsimd())
		return;

	WARN_ON_ONCE(!irqs_disabled());

	/* Save unsaved fpsimd state, if any: */
	if (test_thread_flag(TIF_KERNEL_FPSTATE))
		fpsimd_save_kernel_state(current);
	else
		fpsimd_save_user_state();

	if (test_tsk_thread_flag(next, TIF_KERNEL_FPSTATE)) {
		fpsimd_load_kernel_state(next);
		fpsimd_flush_cpu_state();
	} else {
		/*
		 * Fix up TIF_FOREIGN_FPSTATE to correctly describe next's
		 * state.  For kernel threads, FPSIMD registers are never
		 * loaded with user mode FPSIMD state and so wrong_task and
		 * wrong_cpu will always be true.
		 */
		wrong_task = __this_cpu_read(fpsimd_last_state.st) !=
			&next->thread.uw.fpsimd_state;
		wrong_cpu = next->thread.fpsimd_cpu != smp_processor_id();

		update_tsk_thread_flag(next, TIF_FOREIGN_FPSTATE,
				       wrong_task || wrong_cpu);
	}
}

static void fpsimd_flush_thread_vl(enum vec_type type)
{
	int vl, supported_vl;

	/*
	 * Reset the task vector length as required.  This is where we
	 * ensure that all user tasks have a valid vector length
	 * configured: no kernel task can become a user task without
	 * an exec and hence a call to this function.  By the time the
	 * first call to this function is made, all early hardware
	 * probing is complete, so __sve_default_vl should be valid.
	 * If a bug causes this to go wrong, we make some noise and
	 * try to fudge thread.sve_vl to a safe value here.
	 */
	vl = task_get_vl_onexec(current, type);
	if (!vl)
		vl = get_default_vl(type);

	if (WARN_ON(!sve_vl_valid(vl)))
		vl = vl_info[type].min_vl;

	supported_vl = find_supported_vector_length(type, vl);
	if (WARN_ON(supported_vl != vl))
		vl = supported_vl;

	task_set_vl(current, type, vl);

	/*
	 * If the task is not set to inherit, ensure that the vector
	 * length will be reset by a subsequent exec:
	 */
	if (!test_thread_flag(vec_vl_inherit_flag(type)))
		task_set_vl_onexec(current, type, 0);
}

void fpsimd_flush_thread(void)
{
	void *sve_state = NULL;
	void *sme_state = NULL;

	if (!system_supports_fpsimd())
		return;

	get_cpu_fpsimd_context();

	fpsimd_flush_task_state(current);
	memset(&current->thread.uw.fpsimd_state, 0,
	       sizeof(current->thread.uw.fpsimd_state));

	if (system_supports_sve()) {
		clear_thread_flag(TIF_SVE);

		/* Defer kfree() while in atomic context */
		sve_state = current->thread.sve_state;
		current->thread.sve_state = NULL;

		fpsimd_flush_thread_vl(ARM64_VEC_SVE);
	}

	if (system_supports_sme()) {
		clear_thread_flag(TIF_SME);

		/* Defer kfree() while in atomic context */
		sme_state = current->thread.sme_state;
		current->thread.sme_state = NULL;

		fpsimd_flush_thread_vl(ARM64_VEC_SME);
		current->thread.svcr = 0;
	}

	current->thread.fp_type = FP_STATE_FPSIMD;

	put_cpu_fpsimd_context();
	kfree(sve_state);
	kfree(sme_state);
}

/*
 * Save the userland FPSIMD state of 'current' to memory, but only if the state
 * currently held in the registers does in fact belong to 'current'
 */
void fpsimd_preserve_current_state(void)
{
	if (!system_supports_fpsimd())
		return;

	get_cpu_fpsimd_context();
	fpsimd_save_user_state();
	put_cpu_fpsimd_context();
}

/*
 * Like fpsimd_preserve_current_state(), but ensure that
 * current->thread.uw.fpsimd_state is updated so that it can be copied to
 * the signal frame.
 */
void fpsimd_signal_preserve_current_state(void)
{
	fpsimd_preserve_current_state();
	if (current->thread.fp_type == FP_STATE_SVE)
		sve_to_fpsimd(current);
}

/*
 * Called by KVM when entering the guest.
 */
void fpsimd_kvm_prepare(void)
{
	if (!system_supports_sve())
		return;

	/*
	 * KVM does not save host SVE state since we can only enter
	 * the guest from a syscall so the ABI means that only the
	 * non-saved SVE state needs to be saved.  If we have left
	 * SVE enabled for performance reasons then update the task
	 * state to be FPSIMD only.
	 */
	get_cpu_fpsimd_context();

	if (test_and_clear_thread_flag(TIF_SVE)) {
		sve_to_fpsimd(current);
		current->thread.fp_type = FP_STATE_FPSIMD;
	}

	put_cpu_fpsimd_context();
}

/*
 * Associate current's FPSIMD context with this cpu
 * The caller must have ownership of the cpu FPSIMD context before calling
 * this function.
 */
static void fpsimd_bind_task_to_cpu(void)
{
	struct cpu_fp_state *last = this_cpu_ptr(&fpsimd_last_state);

	WARN_ON(!system_supports_fpsimd());
	last->st = &current->thread.uw.fpsimd_state;
	last->sve_state = current->thread.sve_state;
	last->sme_state = current->thread.sme_state;
	last->sve_vl = task_get_sve_vl(current);
	last->sme_vl = task_get_sme_vl(current);
	last->svcr = &current->thread.svcr;
	last->fpmr = &current->thread.uw.fpmr;
	last->fp_type = &current->thread.fp_type;
	last->to_save = FP_STATE_CURRENT;
	current->thread.fpsimd_cpu = smp_processor_id();

	/*
	 * Toggle SVE and SME trapping for userspace if needed, these
	 * are serialsied by ret_to_user().
	 */
	if (system_supports_sme()) {
		if (test_thread_flag(TIF_SME))
			sme_user_enable();
		else
			sme_user_disable();
	}

	if (system_supports_sve()) {
		if (test_thread_flag(TIF_SVE))
			sve_user_enable();
		else
			sve_user_disable();
	}
}

void fpsimd_bind_state_to_cpu(struct cpu_fp_state *state)
{
	struct cpu_fp_state *last = this_cpu_ptr(&fpsimd_last_state);

	WARN_ON(!system_supports_fpsimd());
	WARN_ON(!in_softirq() && !irqs_disabled());

	*last = *state;
}

/*
 * Load the userland FPSIMD state of 'current' from memory, but only if the
 * FPSIMD state already held in the registers is /not/ the most recent FPSIMD
 * state of 'current'.  This is called when we are preparing to return to
 * userspace to ensure that userspace sees a good register state.
 */
void fpsimd_restore_current_state(void)
{
	/*
	 * TIF_FOREIGN_FPSTATE is set on the init task and copied by
	 * arch_dup_task_struct() regardless of whether FP/SIMD is detected.
	 * Thus user threads can have this set even when FP/SIMD hasn't been
	 * detected.
	 *
	 * When FP/SIMD is detected, begin_new_exec() will set
	 * TIF_FOREIGN_FPSTATE via flush_thread() -> fpsimd_flush_thread(),
	 * and fpsimd_thread_switch() will set TIF_FOREIGN_FPSTATE when
	 * switching tasks. We detect FP/SIMD before we exec the first user
	 * process, ensuring this has TIF_FOREIGN_FPSTATE set and
	 * do_notify_resume() will call fpsimd_restore_current_state() to
	 * install the user FP/SIMD context.
	 *
	 * When FP/SIMD is not detected, nothing else will clear or set
	 * TIF_FOREIGN_FPSTATE prior to the first return to userspace, and
	 * we must clear TIF_FOREIGN_FPSTATE to avoid do_notify_resume()
	 * looping forever calling fpsimd_restore_current_state().
	 */
	if (!system_supports_fpsimd()) {
		clear_thread_flag(TIF_FOREIGN_FPSTATE);
		return;
	}

	get_cpu_fpsimd_context();

	if (test_and_clear_thread_flag(TIF_FOREIGN_FPSTATE)) {
		task_fpsimd_load();
		fpsimd_bind_task_to_cpu();
	}

	put_cpu_fpsimd_context();
}

/*
 * Load an updated userland FPSIMD state for 'current' from memory and set the
 * flag that indicates that the FPSIMD register contents are the most recent
 * FPSIMD state of 'current'. This is used by the signal code to restore the
 * register state when returning from a signal handler in FPSIMD only cases,
 * any SVE context will be discarded.
 */
void fpsimd_update_current_state(struct user_fpsimd_state const *state)
{
	if (WARN_ON(!system_supports_fpsimd()))
		return;

	get_cpu_fpsimd_context();

	current->thread.uw.fpsimd_state = *state;
	if (test_thread_flag(TIF_SVE))
		fpsimd_to_sve(current);

	task_fpsimd_load();
	fpsimd_bind_task_to_cpu();

	clear_thread_flag(TIF_FOREIGN_FPSTATE);

	put_cpu_fpsimd_context();
}

/*
 * Invalidate live CPU copies of task t's FPSIMD state
 *
 * This function may be called with preemption enabled.  The barrier()
 * ensures that the assignment to fpsimd_cpu is visible to any
 * preemption/softirq that could race with set_tsk_thread_flag(), so
 * that TIF_FOREIGN_FPSTATE cannot be spuriously re-cleared.
 *
 * The final barrier ensures that TIF_FOREIGN_FPSTATE is seen set by any
 * subsequent code.
 */
void fpsimd_flush_task_state(struct task_struct *t)
{
	t->thread.fpsimd_cpu = NR_CPUS;
	/*
	 * If we don't support fpsimd, bail out after we have
	 * reset the fpsimd_cpu for this task and clear the
	 * FPSTATE.
	 */
	if (!system_supports_fpsimd())
		return;
	barrier();
	set_tsk_thread_flag(t, TIF_FOREIGN_FPSTATE);

	barrier();
}

/*
 * Save the FPSIMD state to memory and invalidate cpu view.
 * This function must be called with preemption disabled.
 */
void fpsimd_save_and_flush_cpu_state(void)
{
	unsigned long flags;

	if (!system_supports_fpsimd())
		return;
	WARN_ON(preemptible());
	local_irq_save(flags);
	fpsimd_save_user_state();
	fpsimd_flush_cpu_state();
	local_irq_restore(flags);
}

#ifdef CONFIG_KERNEL_MODE_NEON

/*
 * Kernel-side NEON support functions
 */

/*
 * kernel_neon_begin(): obtain the CPU FPSIMD registers for use by the calling
 * context
 *
 * Must not be called unless may_use_simd() returns true.
 * Task context in the FPSIMD registers is saved back to memory as necessary.
 *
 * A matching call to kernel_neon_end() must be made before returning from the
 * calling context.
 *
 * The caller may freely use the FPSIMD registers until kernel_neon_end() is
 * called.
 */
void kernel_neon_begin(void)
{
	if (WARN_ON(!system_supports_fpsimd()))
		return;

	BUG_ON(!may_use_simd());

	get_cpu_fpsimd_context();

	/* Save unsaved fpsimd state, if any: */
	if (test_thread_flag(TIF_KERNEL_FPSTATE)) {
		BUG_ON(IS_ENABLED(CONFIG_PREEMPT_RT) || !in_serving_softirq());
		fpsimd_save_kernel_state(current);
	} else {
		fpsimd_save_user_state();

		/*
		 * Set the thread flag so that the kernel mode FPSIMD state
		 * will be context switched along with the rest of the task
		 * state.
		 *
		 * On non-PREEMPT_RT, softirqs may interrupt task level kernel
		 * mode FPSIMD, but the task will not be preemptible so setting
		 * TIF_KERNEL_FPSTATE for those would be both wrong (as it
		 * would mark the task context FPSIMD state as requiring a
		 * context switch) and unnecessary.
		 *
		 * On PREEMPT_RT, softirqs are serviced from a separate thread,
		 * which is scheduled as usual, and this guarantees that these
		 * softirqs are not interrupting use of the FPSIMD in kernel
		 * mode in task context. So in this case, setting the flag here
		 * is always appropriate.
		 */
		if (IS_ENABLED(CONFIG_PREEMPT_RT) || !in_serving_softirq())
			set_thread_flag(TIF_KERNEL_FPSTATE);
	}

	/* Invalidate any task state remaining in the fpsimd regs: */
	fpsimd_flush_cpu_state();

	put_cpu_fpsimd_context();
}
EXPORT_SYMBOL_GPL(kernel_neon_begin);

/*
 * kernel_neon_end(): give the CPU FPSIMD registers back to the current task
 *
 * Must be called from a context in which kernel_neon_begin() was previously
 * called, with no call to kernel_neon_end() in the meantime.
 *
 * The caller must not use the FPSIMD registers after this function is called,
 * unless kernel_neon_begin() is called again in the meantime.
 */
void kernel_neon_end(void)
{
	if (!system_supports_fpsimd())
		return;

	/*
	 * If we are returning from a nested use of kernel mode FPSIMD, restore
	 * the task context kernel mode FPSIMD state. This can only happen when
	 * running in softirq context on non-PREEMPT_RT.
	 */
	if (!IS_ENABLED(CONFIG_PREEMPT_RT) && in_serving_softirq() &&
	    test_thread_flag(TIF_KERNEL_FPSTATE))
		fpsimd_load_kernel_state(current);
	else
		clear_thread_flag(TIF_KERNEL_FPSTATE);
}
EXPORT_SYMBOL_GPL(kernel_neon_end);

#ifdef CONFIG_EFI

static DEFINE_PER_CPU(struct user_fpsimd_state, efi_fpsimd_state);
static DEFINE_PER_CPU(bool, efi_fpsimd_state_used);
static DEFINE_PER_CPU(bool, efi_sve_state_used);
static DEFINE_PER_CPU(bool, efi_sm_state);

/*
 * EFI runtime services support functions
 *
 * The ABI for EFI runtime services allows EFI to use FPSIMD during the call.
 * This means that for EFI (and only for EFI), we have to assume that FPSIMD
 * is always used rather than being an optional accelerator.
 *
 * These functions provide the necessary support for ensuring FPSIMD
 * save/restore in the contexts from which EFI is used.
 *
 * Do not use them for any other purpose -- if tempted to do so, you are
 * either doing something wrong or you need to propose some refactoring.
 */

/*
 * __efi_fpsimd_begin(): prepare FPSIMD for making an EFI runtime services call
 */
void __efi_fpsimd_begin(void)
{
	if (!system_supports_fpsimd())
		return;

	WARN_ON(preemptible());

	if (may_use_simd()) {
		kernel_neon_begin();
	} else {
		/*
		 * If !efi_sve_state, SVE can't be in use yet and doesn't need
		 * preserving:
		 */
		if (system_supports_sve() && likely(efi_sve_state)) {
			char *sve_state = this_cpu_ptr(efi_sve_state);
			bool ffr = true;
			u64 svcr;

			__this_cpu_write(efi_sve_state_used, true);

			if (system_supports_sme()) {
				svcr = read_sysreg_s(SYS_SVCR);

				__this_cpu_write(efi_sm_state,
						 svcr & SVCR_SM_MASK);

				/*
				 * Unless we have FA64 FFR does not
				 * exist in streaming mode.
				 */
				if (!system_supports_fa64())
					ffr = !(svcr & SVCR_SM_MASK);
			}

			sve_save_state(sve_state + sve_ffr_offset(sve_max_vl()),
				       &this_cpu_ptr(&efi_fpsimd_state)->fpsr,
				       ffr);

			if (system_supports_sme())
				sysreg_clear_set_s(SYS_SVCR,
						   SVCR_SM_MASK, 0);

		} else {
			fpsimd_save_state(this_cpu_ptr(&efi_fpsimd_state));
		}

		__this_cpu_write(efi_fpsimd_state_used, true);
	}
}

/*
 * __efi_fpsimd_end(): clean up FPSIMD after an EFI runtime services call
 */
void __efi_fpsimd_end(void)
{
	if (!system_supports_fpsimd())
		return;

	if (!__this_cpu_xchg(efi_fpsimd_state_used, false)) {
		kernel_neon_end();
	} else {
		if (system_supports_sve() &&
		    likely(__this_cpu_read(efi_sve_state_used))) {
			char const *sve_state = this_cpu_ptr(efi_sve_state);
			bool ffr = true;

			/*
			 * Restore streaming mode; EFI calls are
			 * normal function calls so should not return in
			 * streaming mode.
			 */
			if (system_supports_sme()) {
				if (__this_cpu_read(efi_sm_state)) {
					sysreg_clear_set_s(SYS_SVCR,
							   0,
							   SVCR_SM_MASK);

					/*
					 * Unless we have FA64 FFR does not
					 * exist in streaming mode.
					 */
					if (!system_supports_fa64())
						ffr = false;
				}
			}

			sve_load_state(sve_state + sve_ffr_offset(sve_max_vl()),
				       &this_cpu_ptr(&efi_fpsimd_state)->fpsr,
				       ffr);

			__this_cpu_write(efi_sve_state_used, false);
		} else {
			fpsimd_load_state(this_cpu_ptr(&efi_fpsimd_state));
		}
	}
}

#endif /* CONFIG_EFI */

#endif /* CONFIG_KERNEL_MODE_NEON */

#ifdef CONFIG_CPU_PM
static int fpsimd_cpu_pm_notifier(struct notifier_block *self,
				  unsigned long cmd, void *v)
{
	switch (cmd) {
	case CPU_PM_ENTER:
		fpsimd_save_and_flush_cpu_state();
		break;
	case CPU_PM_EXIT:
		break;
	case CPU_PM_ENTER_FAILED:
	default:
		return NOTIFY_DONE;
	}
	return NOTIFY_OK;
}

static struct notifier_block fpsimd_cpu_pm_notifier_block = {
	.notifier_call = fpsimd_cpu_pm_notifier,
};

static void __init fpsimd_pm_init(void)
{
	cpu_pm_register_notifier(&fpsimd_cpu_pm_notifier_block);
}

#else
static inline void fpsimd_pm_init(void) { }
#endif /* CONFIG_CPU_PM */

#ifdef CONFIG_HOTPLUG_CPU
static int fpsimd_cpu_dead(unsigned int cpu)
{
	per_cpu(fpsimd_last_state.st, cpu) = NULL;
	return 0;
}

static inline void fpsimd_hotplug_init(void)
{
	cpuhp_setup_state_nocalls(CPUHP_ARM64_FPSIMD_DEAD, "arm64/fpsimd:dead",
				  NULL, fpsimd_cpu_dead);
}

#else
static inline void fpsimd_hotplug_init(void) { }
#endif

void cpu_enable_fpsimd(const struct arm64_cpu_capabilities *__always_unused p)
{
	unsigned long enable = CPACR_EL1_FPEN_EL1EN | CPACR_EL1_FPEN_EL0EN;
	write_sysreg(read_sysreg(CPACR_EL1) | enable, CPACR_EL1);
	isb();
}

/*
 * FP/SIMD support code initialisation.
 */
static int __init fpsimd_init(void)
{
	if (cpu_have_named_feature(FP)) {
		fpsimd_pm_init();
		fpsimd_hotplug_init();
	} else {
		pr_notice("Floating-point is not implemented\n");
	}

	if (!cpu_have_named_feature(ASIMD))
		pr_notice("Advanced SIMD is not implemented\n");


	sve_sysctl_init();
	sme_sysctl_init();

	return 0;
}
core_initcall(fpsimd_init);
