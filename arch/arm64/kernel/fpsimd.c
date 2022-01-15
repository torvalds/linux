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
 * task.
 *
 * In order to allow softirq handlers to use FPSIMD, kernel_neon_begin() may
 * save the task's FPSIMD context back to task_struct from softirq context.
 * To prevent this from racing with the manipulation of the task's FPSIMD state
 * from task context and thereby corrupting the state, it is necessary to
 * protect any manipulation of a task's fpsimd_state or TIF_FOREIGN_FPSTATE
 * flag with {, __}get_cpu_fpsimd_context(). This will still allow softirqs to
 * run but prevent them to use FPSIMD.
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
struct fpsimd_last_state_struct {
	struct user_fpsimd_state *st;
	void *sve_state;
	unsigned int sve_vl;
};

static DEFINE_PER_CPU(struct fpsimd_last_state_struct, fpsimd_last_state);

/* Default VL for tasks that don't set it explicitly: */
static int __sve_default_vl = -1;

static int get_sve_default_vl(void)
{
	return READ_ONCE(__sve_default_vl);
}

#ifdef CONFIG_ARM64_SVE

static void set_sve_default_vl(int val)
{
	WRITE_ONCE(__sve_default_vl, val);
}

/* Maximum supported vector length across all CPUs (initially poisoned) */
int __ro_after_init sve_max_vl = SVE_VL_MIN;
int __ro_after_init sve_max_virtualisable_vl = SVE_VL_MIN;

/*
 * Set of available vector lengths,
 * where length vq encoded as bit __vq_to_bit(vq):
 */
__ro_after_init DECLARE_BITMAP(sve_vq_map, SVE_VQ_MAX);
/* Set of vector lengths present on at least one cpu: */
static __ro_after_init DECLARE_BITMAP(sve_vq_partial_map, SVE_VQ_MAX);

static void __percpu *efi_sve_state;

#else /* ! CONFIG_ARM64_SVE */

/* Dummy declaration for code that will be optimised out: */
extern __ro_after_init DECLARE_BITMAP(sve_vq_map, SVE_VQ_MAX);
extern __ro_after_init DECLARE_BITMAP(sve_vq_partial_map, SVE_VQ_MAX);
extern void __percpu *efi_sve_state;

#endif /* ! CONFIG_ARM64_SVE */

DEFINE_PER_CPU(bool, fpsimd_context_busy);
EXPORT_PER_CPU_SYMBOL(fpsimd_context_busy);

static void fpsimd_bind_task_to_cpu(void);

static void __get_cpu_fpsimd_context(void)
{
	bool busy = __this_cpu_xchg(fpsimd_context_busy, true);

	WARN_ON(busy);
}

/*
 * Claim ownership of the CPU FPSIMD context for use by the calling context.
 *
 * The caller may freely manipulate the FPSIMD context metadata until
 * put_cpu_fpsimd_context() is called.
 *
 * The double-underscore version must only be called if you know the task
 * can't be preempted.
 */
static void get_cpu_fpsimd_context(void)
{
	local_bh_disable();
	__get_cpu_fpsimd_context();
}

static void __put_cpu_fpsimd_context(void)
{
	bool busy = __this_cpu_xchg(fpsimd_context_busy, false);

	WARN_ON(!busy); /* No matching get_cpu_fpsimd_context()? */
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
	__put_cpu_fpsimd_context();
	local_bh_enable();
}

static bool have_cpu_fpsimd_context(void)
{
	return !preemptible() && __this_cpu_read(fpsimd_context_busy);
}

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
 * TIF_SVE controls whether a task can use SVE without trapping while
 * in userspace, and also the way a task's FPSIMD/SVE state is stored
 * in thread_struct.
 *
 * The kernel uses this flag to track whether a user task is actively
 * using SVE, and therefore whether full SVE register state needs to
 * be tracked.  If not, the cheaper FPSIMD context handling code can
 * be used instead of the more costly SVE equivalents.
 *
 *  * TIF_SVE set:
 *
 *    The task can execute SVE instructions while in userspace without
 *    trapping to the kernel.
 *
 *    When stored, Z0-Z31 (incorporating Vn in bits[127:0] or the
 *    corresponding Zn), P0-P15 and FFR are encoded in in
 *    task->thread.sve_state, formatted appropriately for vector
 *    length task->thread.sve_vl.
 *
 *    task->thread.sve_state must point to a valid buffer at least
 *    sve_state_size(task) bytes in size.
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
 *    When stored, FPSIMD registers V0-V31 are encoded in
 *    task->thread.uw.fpsimd_state; bits [max : 128] for each of Z0-Z31 are
 *    logically zero but not stored anywhere; P0-P15 and FFR are not
 *    stored and have unspecified values from userspace's point of
 *    view.  For hygiene purposes, the kernel zeroes them on next use,
 *    but userspace is discouraged from relying on this.
 *
 *    task->thread.sve_state does not need to be non-NULL, valid or any
 *    particular size: it must not be dereferenced.
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
	WARN_ON(!system_supports_fpsimd());
	WARN_ON(!have_cpu_fpsimd_context());

	if (IS_ENABLED(CONFIG_ARM64_SVE) && test_thread_flag(TIF_SVE))
		sve_load_state(sve_pffr(&current->thread),
			       &current->thread.uw.fpsimd_state.fpsr,
			       sve_vq_from_vl(current->thread.sve_vl) - 1);
	else
		fpsimd_load_state(&current->thread.uw.fpsimd_state);
}

/*
 * Ensure FPSIMD/SVE storage in memory for the loaded context is up to
 * date with respect to the CPU registers.
 */
static void fpsimd_save(void)
{
	struct fpsimd_last_state_struct const *last =
		this_cpu_ptr(&fpsimd_last_state);
	/* set by fpsimd_bind_task_to_cpu() or fpsimd_bind_state_to_cpu() */

	WARN_ON(!system_supports_fpsimd());
	WARN_ON(!have_cpu_fpsimd_context());

	if (!test_thread_flag(TIF_FOREIGN_FPSTATE)) {
		if (IS_ENABLED(CONFIG_ARM64_SVE) &&
		    test_thread_flag(TIF_SVE)) {
			if (WARN_ON(sve_get_vl() != last->sve_vl)) {
				/*
				 * Can't save the user regs, so current would
				 * re-enter user with corrupt state.
				 * There's no way to recover, so kill it:
				 */
				force_signal_inject(SIGKILL, SI_KERNEL, 0, 0);
				return;
			}

			sve_save_state((char *)last->sve_state +
						sve_ffr_offset(last->sve_vl),
				       &last->st->fpsr);
		} else
			fpsimd_save_state(last->st);
	}
}

/*
 * All vector length selection from userspace comes through here.
 * We're on a slow path, so some sanity-checks are included.
 * If things go wrong there's a bug somewhere, but try to fall back to a
 * safe choice.
 */
static unsigned int find_supported_vector_length(unsigned int vl)
{
	int bit;
	int max_vl = sve_max_vl;

	if (WARN_ON(!sve_vl_valid(vl)))
		vl = SVE_VL_MIN;

	if (WARN_ON(!sve_vl_valid(max_vl)))
		max_vl = SVE_VL_MIN;

	if (vl > max_vl)
		vl = max_vl;

	bit = find_next_bit(sve_vq_map, SVE_VQ_MAX,
			    __vq_to_bit(sve_vq_from_vl(vl)));
	return sve_vl_from_vq(__bit_to_vq(bit));
}

#if defined(CONFIG_ARM64_SVE) && defined(CONFIG_SYSCTL)

static int sve_proc_do_default_vl(struct ctl_table *table, int write,
				  void *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;
	int vl = get_sve_default_vl();
	struct ctl_table tmp_table = {
		.data = &vl,
		.maxlen = sizeof(vl),
	};

	ret = proc_dointvec(&tmp_table, write, buffer, lenp, ppos);
	if (ret || !write)
		return ret;

	/* Writing -1 has the special meaning "set to max": */
	if (vl == -1)
		vl = sve_max_vl;

	if (!sve_vl_valid(vl))
		return -EINVAL;

	set_sve_default_vl(find_supported_vector_length(vl));
	return 0;
}

static struct ctl_table sve_default_vl_table[] = {
	{
		.procname	= "sve_default_vector_length",
		.mode		= 0644,
		.proc_handler	= sve_proc_do_default_vl,
	},
	{ }
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

	if (!system_supports_sve())
		return;

	vq = sve_vq_from_vl(task->thread.sve_vl);
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
	unsigned int vq;
	void const *sst = task->thread.sve_state;
	struct user_fpsimd_state *fst = &task->thread.uw.fpsimd_state;
	unsigned int i;
	__uint128_t const *p;

	if (!system_supports_sve())
		return;

	vq = sve_vq_from_vl(task->thread.sve_vl);
	for (i = 0; i < SVE_NUM_ZREGS; ++i) {
		p = (__uint128_t const *)ZREG(sst, vq, i);
		fst->vregs[i] = arm64_le128_to_cpu(*p);
	}
}

#ifdef CONFIG_ARM64_SVE

/*
 * Return how many bytes of memory are required to store the full SVE
 * state for task, given task's currently configured vector length.
 */
size_t sve_state_size(struct task_struct const *task)
{
	return SVE_SIG_REGS_SIZE(sve_vq_from_vl(task->thread.sve_vl));
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
void sve_alloc(struct task_struct *task)
{
	if (task->thread.sve_state) {
		memset(task->thread.sve_state, 0, sve_state_size(task));
		return;
	}

	/* This is a small allocation (maximum ~8KB) and Should Not Fail. */
	task->thread.sve_state =
		kzalloc(sve_state_size(task), GFP_KERNEL);
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
	if (!test_tsk_thread_flag(task, TIF_SVE))
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
	if (test_tsk_thread_flag(task, TIF_SVE))
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

	if (!test_tsk_thread_flag(task, TIF_SVE))
		return;

	vq = sve_vq_from_vl(task->thread.sve_vl);

	memset(sst, 0, SVE_SIG_REGS_SIZE(vq));
	__fpsimd_to_sve(sst, fst, vq);
}

int sve_set_vector_length(struct task_struct *task,
			  unsigned long vl, unsigned long flags)
{
	if (flags & ~(unsigned long)(PR_SVE_VL_INHERIT |
				     PR_SVE_SET_VL_ONEXEC))
		return -EINVAL;

	if (!sve_vl_valid(vl))
		return -EINVAL;

	/*
	 * Clamp to the maximum vector length that VL-agnostic SVE code can
	 * work with.  A flag may be assigned in the future to allow setting
	 * of larger vector lengths without confusing older software.
	 */
	if (vl > SVE_VL_ARCH_MAX)
		vl = SVE_VL_ARCH_MAX;

	vl = find_supported_vector_length(vl);

	if (flags & (PR_SVE_VL_INHERIT |
		     PR_SVE_SET_VL_ONEXEC))
		task->thread.sve_vl_onexec = vl;
	else
		/* Reset VL to system default on next exec: */
		task->thread.sve_vl_onexec = 0;

	/* Only actually set the VL if not deferred: */
	if (flags & PR_SVE_SET_VL_ONEXEC)
		goto out;

	if (vl == task->thread.sve_vl)
		goto out;

	/*
	 * To ensure the FPSIMD bits of the SVE vector registers are preserved,
	 * write any live register state back to task_struct, and convert to a
	 * non-SVE thread.
	 */
	if (task == current) {
		get_cpu_fpsimd_context();

		fpsimd_save();
	}

	fpsimd_flush_task_state(task);
	if (test_and_clear_tsk_thread_flag(task, TIF_SVE))
		sve_to_fpsimd(task);

	if (task == current)
		put_cpu_fpsimd_context();

	/*
	 * Force reallocation of task SVE state to the correct size
	 * on next use:
	 */
	sve_free(task);

	task->thread.sve_vl = vl;

out:
	update_tsk_thread_flag(task, TIF_SVE_VL_INHERIT,
			       flags & PR_SVE_VL_INHERIT);

	return 0;
}

/*
 * Encode the current vector length and flags for return.
 * This is only required for prctl(): ptrace has separate fields
 *
 * flags are as for sve_set_vector_length().
 */
static int sve_prctl_status(unsigned long flags)
{
	int ret;

	if (flags & PR_SVE_SET_VL_ONEXEC)
		ret = current->thread.sve_vl_onexec;
	else
		ret = current->thread.sve_vl;

	if (test_thread_flag(TIF_SVE_VL_INHERIT))
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

	ret = sve_set_vector_length(current, vl, flags);
	if (ret)
		return ret;

	return sve_prctl_status(flags);
}

/* PR_SVE_GET_VL */
int sve_get_current_vl(void)
{
	if (!system_supports_sve() || is_compat_task())
		return -EINVAL;

	return sve_prctl_status(0);
}

static void sve_probe_vqs(DECLARE_BITMAP(map, SVE_VQ_MAX))
{
	unsigned int vq, vl;
	unsigned long zcr;

	bitmap_zero(map, SVE_VQ_MAX);

	zcr = ZCR_ELx_LEN_MASK;
	zcr = read_sysreg_s(SYS_ZCR_EL1) & ~zcr;

	for (vq = SVE_VQ_MAX; vq >= SVE_VQ_MIN; --vq) {
		write_sysreg_s(zcr | (vq - 1), SYS_ZCR_EL1); /* self-syncing */
		vl = sve_get_vl();
		vq = sve_vq_from_vl(vl); /* skip intervening lengths */
		set_bit(__vq_to_bit(vq), map);
	}
}

/*
 * Initialise the set of known supported VQs for the boot CPU.
 * This is called during kernel boot, before secondary CPUs are brought up.
 */
void __init sve_init_vq_map(void)
{
	sve_probe_vqs(sve_vq_map);
	bitmap_copy(sve_vq_partial_map, sve_vq_map, SVE_VQ_MAX);
}

/*
 * If we haven't committed to the set of supported VQs yet, filter out
 * those not supported by the current CPU.
 * This function is called during the bring-up of early secondary CPUs only.
 */
void sve_update_vq_map(void)
{
	DECLARE_BITMAP(tmp_map, SVE_VQ_MAX);

	sve_probe_vqs(tmp_map);
	bitmap_and(sve_vq_map, sve_vq_map, tmp_map, SVE_VQ_MAX);
	bitmap_or(sve_vq_partial_map, sve_vq_partial_map, tmp_map, SVE_VQ_MAX);
}

/*
 * Check whether the current CPU supports all VQs in the committed set.
 * This function is called during the bring-up of late secondary CPUs only.
 */
int sve_verify_vq_map(void)
{
	DECLARE_BITMAP(tmp_map, SVE_VQ_MAX);
	unsigned long b;

	sve_probe_vqs(tmp_map);

	bitmap_complement(tmp_map, tmp_map, SVE_VQ_MAX);
	if (bitmap_intersects(tmp_map, sve_vq_map, SVE_VQ_MAX)) {
		pr_warn("SVE: cpu%d: Required vector length(s) missing\n",
			smp_processor_id());
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
	bitmap_andnot(tmp_map, tmp_map, sve_vq_map, SVE_VQ_MAX);

	/* Find the lowest such VQ, if any: */
	b = find_last_bit(tmp_map, SVE_VQ_MAX);
	if (b >= SVE_VQ_MAX)
		return 0; /* no mismatches */

	/*
	 * Mismatches above sve_max_virtualisable_vl are fine, since
	 * no guest is allowed to configure ZCR_EL2.LEN to exceed this:
	 */
	if (sve_vl_from_vq(__bit_to_vq(b)) <= sve_max_virtualisable_vl) {
		pr_warn("SVE: cpu%d: Unsupported vector length(s) present\n",
			smp_processor_id());
		return -EINVAL;
	}

	return 0;
}

static void __init sve_efi_setup(void)
{
	if (!IS_ENABLED(CONFIG_EFI))
		return;

	/*
	 * alloc_percpu() warns and prints a backtrace if this goes wrong.
	 * This is evidence of a crippled system and we are returning void,
	 * so no attempt is made to handle this situation here.
	 */
	if (!sve_vl_valid(sve_max_vl))
		goto fail;

	efi_sve_state = __alloc_percpu(
		SVE_SIG_REGS_SIZE(sve_vq_from_vl(sve_max_vl)), SVE_VQ_BYTES);
	if (!efi_sve_state)
		goto fail;

	return;

fail:
	panic("Cannot allocate percpu memory for EFI SVE save/restore");
}

/*
 * Enable SVE for EL1.
 * Intended for use by the cpufeatures code during CPU boot.
 */
void sve_kernel_enable(const struct arm64_cpu_capabilities *__always_unused p)
{
	write_sysreg(read_sysreg(CPACR_EL1) | CPACR_EL1_ZEN_EL1EN, CPACR_EL1);
	isb();
}

/*
 * Read the pseudo-ZCR used by cpufeatures to identify the supported SVE
 * vector length.
 *
 * Use only if SVE is present.
 * This function clobbers the SVE vector length.
 */
u64 read_zcr_features(void)
{
	u64 zcr;
	unsigned int vq_max;

	/*
	 * Set the maximum possible VL, and write zeroes to all other
	 * bits to see if they stick.
	 */
	sve_kernel_enable(NULL);
	write_sysreg_s(ZCR_ELx_LEN_MASK, SYS_ZCR_EL1);

	zcr = read_sysreg_s(SYS_ZCR_EL1);
	zcr &= ~(u64)ZCR_ELx_LEN_MASK; /* find sticky 1s outside LEN field */
	vq_max = sve_vq_from_vl(sve_get_vl());
	zcr |= vq_max - 1; /* set LEN field to maximum effective value */

	return zcr;
}

void __init sve_setup(void)
{
	u64 zcr;
	DECLARE_BITMAP(tmp_map, SVE_VQ_MAX);
	unsigned long b;

	if (!system_supports_sve())
		return;

	/*
	 * The SVE architecture mandates support for 128-bit vectors,
	 * so sve_vq_map must have at least SVE_VQ_MIN set.
	 * If something went wrong, at least try to patch it up:
	 */
	if (WARN_ON(!test_bit(__vq_to_bit(SVE_VQ_MIN), sve_vq_map)))
		set_bit(__vq_to_bit(SVE_VQ_MIN), sve_vq_map);

	zcr = read_sanitised_ftr_reg(SYS_ZCR_EL1);
	sve_max_vl = sve_vl_from_vq((zcr & ZCR_ELx_LEN_MASK) + 1);

	/*
	 * Sanity-check that the max VL we determined through CPU features
	 * corresponds properly to sve_vq_map.  If not, do our best:
	 */
	if (WARN_ON(sve_max_vl != find_supported_vector_length(sve_max_vl)))
		sve_max_vl = find_supported_vector_length(sve_max_vl);

	/*
	 * For the default VL, pick the maximum supported value <= 64.
	 * VL == 64 is guaranteed not to grow the signal frame.
	 */
	set_sve_default_vl(find_supported_vector_length(64));

	bitmap_andnot(tmp_map, sve_vq_partial_map, sve_vq_map,
		      SVE_VQ_MAX);

	b = find_last_bit(tmp_map, SVE_VQ_MAX);
	if (b >= SVE_VQ_MAX)
		/* No non-virtualisable VLs found */
		sve_max_virtualisable_vl = SVE_VQ_MAX;
	else if (WARN_ON(b == SVE_VQ_MAX - 1))
		/* No virtualisable VLs?  This is architecturally forbidden. */
		sve_max_virtualisable_vl = SVE_VQ_MIN;
	else /* b + 1 < SVE_VQ_MAX */
		sve_max_virtualisable_vl = sve_vl_from_vq(__bit_to_vq(b + 1));

	if (sve_max_virtualisable_vl > sve_max_vl)
		sve_max_virtualisable_vl = sve_max_vl;

	pr_info("SVE: maximum available vector length %u bytes per vector\n",
		sve_max_vl);
	pr_info("SVE: default vector length %u bytes per vector\n",
		get_sve_default_vl());

	/* KVM decides whether to support mismatched systems. Just warn here: */
	if (sve_max_virtualisable_vl < sve_max_vl)
		pr_warn("SVE: unvirtualisable vector lengths present\n");

	sve_efi_setup();
}

/*
 * Called from the put_task_struct() path, which cannot get here
 * unless dead_task is really dead and not schedulable.
 */
void fpsimd_release_task(struct task_struct *dead_task)
{
	__sve_free(dead_task);
}

#endif /* CONFIG_ARM64_SVE */

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
void do_sve_acc(unsigned int esr, struct pt_regs *regs)
{
	/* Even if we chose not to use SVE, the hardware could still trap: */
	if (unlikely(!system_supports_sve()) || WARN_ON(is_compat_task())) {
		force_signal_inject(SIGILL, ILL_ILLOPC, regs->pc, 0);
		return;
	}

	sve_alloc(current);
	if (!current->thread.sve_state) {
		force_sig(SIGKILL);
		return;
	}

	get_cpu_fpsimd_context();

	if (test_and_set_thread_flag(TIF_SVE))
		WARN_ON(1); /* SVE access shouldn't have trapped */

	/*
	 * Convert the FPSIMD state to SVE, zeroing all the state that
	 * is not shared with FPSIMD. If (as is likely) the current
	 * state is live in the registers then do this there and
	 * update our metadata for the current task including
	 * disabling the trap, otherwise update our in-memory copy.
	 */
	if (!test_thread_flag(TIF_FOREIGN_FPSTATE)) {
		unsigned long vq_minus_one =
			sve_vq_from_vl(current->thread.sve_vl) - 1;
		sve_set_vq(vq_minus_one);
		sve_flush_live(vq_minus_one);
		fpsimd_bind_task_to_cpu();
	} else {
		fpsimd_to_sve(current);
	}

	put_cpu_fpsimd_context();
}

/*
 * Trapped FP/ASIMD access.
 */
void do_fpsimd_acc(unsigned int esr, struct pt_regs *regs)
{
	/* TODO: implement lazy context saving/restoring */
	WARN_ON(1);
}

/*
 * Raise a SIGFPE for the current process.
 */
void do_fpsimd_exc(unsigned int esr, struct pt_regs *regs)
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

void fpsimd_thread_switch(struct task_struct *next)
{
	bool wrong_task, wrong_cpu;

	if (!system_supports_fpsimd())
		return;

	__get_cpu_fpsimd_context();

	/* Save unsaved fpsimd state, if any: */
	fpsimd_save();

	/*
	 * Fix up TIF_FOREIGN_FPSTATE to correctly describe next's
	 * state.  For kernel threads, FPSIMD registers are never loaded
	 * and wrong_task and wrong_cpu will always be true.
	 */
	wrong_task = __this_cpu_read(fpsimd_last_state.st) !=
					&next->thread.uw.fpsimd_state;
	wrong_cpu = next->thread.fpsimd_cpu != smp_processor_id();

	update_tsk_thread_flag(next, TIF_FOREIGN_FPSTATE,
			       wrong_task || wrong_cpu);

	__put_cpu_fpsimd_context();
}

void fpsimd_flush_thread(void)
{
	int vl, supported_vl;

	if (!system_supports_fpsimd())
		return;

	get_cpu_fpsimd_context();

	fpsimd_flush_task_state(current);
	memset(&current->thread.uw.fpsimd_state, 0,
	       sizeof(current->thread.uw.fpsimd_state));

	if (system_supports_sve()) {
		clear_thread_flag(TIF_SVE);
		sve_free(current);

		/*
		 * Reset the task vector length as required.
		 * This is where we ensure that all user tasks have a valid
		 * vector length configured: no kernel task can become a user
		 * task without an exec and hence a call to this function.
		 * By the time the first call to this function is made, all
		 * early hardware probing is complete, so __sve_default_vl
		 * should be valid.
		 * If a bug causes this to go wrong, we make some noise and
		 * try to fudge thread.sve_vl to a safe value here.
		 */
		vl = current->thread.sve_vl_onexec ?
			current->thread.sve_vl_onexec : get_sve_default_vl();

		if (WARN_ON(!sve_vl_valid(vl)))
			vl = SVE_VL_MIN;

		supported_vl = find_supported_vector_length(vl);
		if (WARN_ON(supported_vl != vl))
			vl = supported_vl;

		current->thread.sve_vl = vl;

		/*
		 * If the task is not set to inherit, ensure that the vector
		 * length will be reset by a subsequent exec:
		 */
		if (!test_thread_flag(TIF_SVE_VL_INHERIT))
			current->thread.sve_vl_onexec = 0;
	}

	put_cpu_fpsimd_context();
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
	fpsimd_save();
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
	if (test_thread_flag(TIF_SVE))
		sve_to_fpsimd(current);
}

/*
 * Associate current's FPSIMD context with this cpu
 * The caller must have ownership of the cpu FPSIMD context before calling
 * this function.
 */
static void fpsimd_bind_task_to_cpu(void)
{
	struct fpsimd_last_state_struct *last =
		this_cpu_ptr(&fpsimd_last_state);

	WARN_ON(!system_supports_fpsimd());
	last->st = &current->thread.uw.fpsimd_state;
	last->sve_state = current->thread.sve_state;
	last->sve_vl = current->thread.sve_vl;
	current->thread.fpsimd_cpu = smp_processor_id();

	if (system_supports_sve()) {
		/* Toggle SVE trapping for userspace if needed */
		if (test_thread_flag(TIF_SVE))
			sve_user_enable();
		else
			sve_user_disable();

		/* Serialised by exception return to user */
	}
}

void fpsimd_bind_state_to_cpu(struct user_fpsimd_state *st, void *sve_state,
			      unsigned int sve_vl)
{
	struct fpsimd_last_state_struct *last =
		this_cpu_ptr(&fpsimd_last_state);

	WARN_ON(!system_supports_fpsimd());
	WARN_ON(!in_softirq() && !irqs_disabled());

	last->st = st;
	last->sve_state = sve_state;
	last->sve_vl = sve_vl;
}

/*
 * Load the userland FPSIMD state of 'current' from memory, but only if the
 * FPSIMD state already held in the registers is /not/ the most recent FPSIMD
 * state of 'current'
 */
void fpsimd_restore_current_state(void)
{
	/*
	 * For the tasks that were created before we detected the absence of
	 * FP/SIMD, the TIF_FOREIGN_FPSTATE could be set via fpsimd_thread_switch(),
	 * e.g, init. This could be then inherited by the children processes.
	 * If we later detect that the system doesn't support FP/SIMD,
	 * we must clear the flag for  all the tasks to indicate that the
	 * FPSTATE is clean (as we can't have one) to avoid looping for ever in
	 * do_notify_resume().
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
 * FPSIMD state of 'current'
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
 * Invalidate any task's FPSIMD state that is present on this cpu.
 * The FPSIMD context should be acquired with get_cpu_fpsimd_context()
 * before calling this function.
 */
static void fpsimd_flush_cpu_state(void)
{
	WARN_ON(!system_supports_fpsimd());
	__this_cpu_write(fpsimd_last_state.st, NULL);
	set_thread_flag(TIF_FOREIGN_FPSTATE);
}

/*
 * Save the FPSIMD state to memory and invalidate cpu view.
 * This function must be called with preemption disabled.
 */
void fpsimd_save_and_flush_cpu_state(void)
{
	if (!system_supports_fpsimd())
		return;
	WARN_ON(preemptible());
	__get_cpu_fpsimd_context();
	fpsimd_save();
	fpsimd_flush_cpu_state();
	__put_cpu_fpsimd_context();
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
	fpsimd_save();

	/* Invalidate any task state remaining in the fpsimd regs: */
	fpsimd_flush_cpu_state();
}
EXPORT_SYMBOL(kernel_neon_begin);

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

	put_cpu_fpsimd_context();
}
EXPORT_SYMBOL(kernel_neon_end);

#ifdef CONFIG_EFI

static DEFINE_PER_CPU(struct user_fpsimd_state, efi_fpsimd_state);
static DEFINE_PER_CPU(bool, efi_fpsimd_state_used);
static DEFINE_PER_CPU(bool, efi_sve_state_used);

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

			__this_cpu_write(efi_sve_state_used, true);

			sve_save_state(sve_state + sve_ffr_offset(sve_max_vl),
				       &this_cpu_ptr(&efi_fpsimd_state)->fpsr);
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

			sve_load_state(sve_state + sve_ffr_offset(sve_max_vl),
				       &this_cpu_ptr(&efi_fpsimd_state)->fpsr,
				       sve_vq_from_vl(sve_get_vl()) - 1);

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

	return sve_sysctl_init();
}
core_initcall(fpsimd_init);
