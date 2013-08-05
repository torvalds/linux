#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/prctl.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/clockchips.h>
#include <linux/random.h>
#include <linux/user-return-notifier.h>
#include <linux/dmi.h>
#include <linux/utsname.h>
#include <linux/stackprotector.h>
#include <linux/tick.h>
#include <linux/cpuidle.h>
#include <trace/events/power.h>
#include <linux/hw_breakpoint.h>
#include <asm/cpu.h>
#include <asm/apic.h>
#include <asm/syscalls.h>
#include <asm/idle.h>
#include <asm/uaccess.h>
#include <asm/i387.h>
#include <asm/fpu-internal.h>
#include <asm/debugreg.h>
#include <asm/nmi.h>

/*
 * per-CPU TSS segments. Threads are completely 'soft' on Linux,
 * no more per-task TSS's. The TSS size is kept cacheline-aligned
 * so they are allowed to end up in the .data..cacheline_aligned
 * section. Since TSS's are completely CPU-local, we want them
 * on exact cacheline boundaries, to eliminate cacheline ping-pong.
 */
DEFINE_PER_CPU_SHARED_ALIGNED(struct tss_struct, init_tss) = INIT_TSS;

#ifdef CONFIG_X86_64
static DEFINE_PER_CPU(unsigned char, is_idle);
static ATOMIC_NOTIFIER_HEAD(idle_notifier);

void idle_notifier_register(struct notifier_block *n)
{
	atomic_notifier_chain_register(&idle_notifier, n);
}
EXPORT_SYMBOL_GPL(idle_notifier_register);

void idle_notifier_unregister(struct notifier_block *n)
{
	atomic_notifier_chain_unregister(&idle_notifier, n);
}
EXPORT_SYMBOL_GPL(idle_notifier_unregister);
#endif

struct kmem_cache *task_xstate_cachep;
EXPORT_SYMBOL_GPL(task_xstate_cachep);

/*
 * this gets called so that we can store lazy state into memory and copy the
 * current task into the new thread.
 */
int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	int ret;

	*dst = *src;
	if (fpu_allocated(&src->thread.fpu)) {
		memset(&dst->thread.fpu, 0, sizeof(dst->thread.fpu));
		ret = fpu_alloc(&dst->thread.fpu);
		if (ret)
			return ret;
		fpu_copy(dst, src);
	}
	return 0;
}

void free_thread_xstate(struct task_struct *tsk)
{
	fpu_free(&tsk->thread.fpu);
}

void arch_release_task_struct(struct task_struct *tsk)
{
	free_thread_xstate(tsk);
}

void arch_task_cache_init(void)
{
        task_xstate_cachep =
        	kmem_cache_create("task_xstate", xstate_size,
				  __alignof__(union thread_xstate),
				  SLAB_PANIC | SLAB_NOTRACK, NULL);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(void)
{
	struct task_struct *me = current;
	struct thread_struct *t = &me->thread;
	unsigned long *bp = t->io_bitmap_ptr;

	if (bp) {
		struct tss_struct *tss = &per_cpu(init_tss, get_cpu());

		t->io_bitmap_ptr = NULL;
		clear_thread_flag(TIF_IO_BITMAP);
		/*
		 * Careful, clear this in the TSS too:
		 */
		memset(tss->io_bitmap, 0xff, t->io_bitmap_max);
		t->io_bitmap_max = 0;
		put_cpu();
		kfree(bp);
	}

	drop_fpu(me);
}

void flush_thread(void)
{
	struct task_struct *tsk = current;

	flush_ptrace_hw_breakpoint(tsk);
	memset(tsk->thread.tls_array, 0, sizeof(tsk->thread.tls_array));
	drop_init_fpu(tsk);
	/*
	 * Free the FPU state for non xsave platforms. They get reallocated
	 * lazily at the first use.
	 */
	if (!use_eager_fpu())
		free_thread_xstate(tsk);
}

static void hard_disable_TSC(void)
{
	write_cr4(read_cr4() | X86_CR4_TSD);
}

void disable_TSC(void)
{
	preempt_disable();
	if (!test_and_set_thread_flag(TIF_NOTSC))
		/*
		 * Must flip the CPU state synchronously with
		 * TIF_NOTSC in the current running context.
		 */
		hard_disable_TSC();
	preempt_enable();
}

static void hard_enable_TSC(void)
{
	write_cr4(read_cr4() & ~X86_CR4_TSD);
}

static void enable_TSC(void)
{
	preempt_disable();
	if (test_and_clear_thread_flag(TIF_NOTSC))
		/*
		 * Must flip the CPU state synchronously with
		 * TIF_NOTSC in the current running context.
		 */
		hard_enable_TSC();
	preempt_enable();
}

int get_tsc_mode(unsigned long adr)
{
	unsigned int val;

	if (test_thread_flag(TIF_NOTSC))
		val = PR_TSC_SIGSEGV;
	else
		val = PR_TSC_ENABLE;

	return put_user(val, (unsigned int __user *)adr);
}

int set_tsc_mode(unsigned int val)
{
	if (val == PR_TSC_SIGSEGV)
		disable_TSC();
	else if (val == PR_TSC_ENABLE)
		enable_TSC();
	else
		return -EINVAL;

	return 0;
}

void __switch_to_xtra(struct task_struct *prev_p, struct task_struct *next_p,
		      struct tss_struct *tss)
{
	struct thread_struct *prev, *next;

	prev = &prev_p->thread;
	next = &next_p->thread;

	if (test_tsk_thread_flag(prev_p, TIF_BLOCKSTEP) ^
	    test_tsk_thread_flag(next_p, TIF_BLOCKSTEP)) {
		unsigned long debugctl = get_debugctlmsr();

		debugctl &= ~DEBUGCTLMSR_BTF;
		if (test_tsk_thread_flag(next_p, TIF_BLOCKSTEP))
			debugctl |= DEBUGCTLMSR_BTF;

		update_debugctlmsr(debugctl);
	}

	if (test_tsk_thread_flag(prev_p, TIF_NOTSC) ^
	    test_tsk_thread_flag(next_p, TIF_NOTSC)) {
		/* prev and next are different */
		if (test_tsk_thread_flag(next_p, TIF_NOTSC))
			hard_disable_TSC();
		else
			hard_enable_TSC();
	}

	if (test_tsk_thread_flag(next_p, TIF_IO_BITMAP)) {
		/*
		 * Copy the relevant range of the IO bitmap.
		 * Normally this is 128 bytes or less:
		 */
		memcpy(tss->io_bitmap, next->io_bitmap_ptr,
		       max(prev->io_bitmap_max, next->io_bitmap_max));
	} else if (test_tsk_thread_flag(prev_p, TIF_IO_BITMAP)) {
		/*
		 * Clear any possible leftover bits:
		 */
		memset(tss->io_bitmap, 0xff, prev->io_bitmap_max);
	}
	propagate_user_return_notify(prev_p, next_p);
}

/*
 * Idle related variables and functions
 */
unsigned long boot_option_idle_override = IDLE_NO_OVERRIDE;
EXPORT_SYMBOL(boot_option_idle_override);

static void (*x86_idle)(void);

#ifndef CONFIG_SMP
static inline void play_dead(void)
{
	BUG();
}
#endif

#ifdef CONFIG_X86_64
void enter_idle(void)
{
	this_cpu_write(is_idle, 1);
	atomic_notifier_call_chain(&idle_notifier, IDLE_START, NULL);
}

static void __exit_idle(void)
{
	if (x86_test_and_clear_bit_percpu(0, is_idle) == 0)
		return;
	atomic_notifier_call_chain(&idle_notifier, IDLE_END, NULL);
}

/* Called from interrupts to signify idle end */
void exit_idle(void)
{
	/* idle loop has pid 0 */
	if (current->pid)
		return;
	__exit_idle();
}
#endif

void arch_cpu_idle_prepare(void)
{
	/*
	 * If we're the non-boot CPU, nothing set the stack canary up
	 * for us.  CPU0 already has it initialized but no harm in
	 * doing it again.  This is a good place for updating it, as
	 * we wont ever return from this function (so the invalid
	 * canaries already on the stack wont ever trigger).
	 */
	boot_init_stack_canary();
}

void arch_cpu_idle_enter(void)
{
	local_touch_nmi();
	enter_idle();
}

void arch_cpu_idle_exit(void)
{
	__exit_idle();
}

void arch_cpu_idle_dead(void)
{
	play_dead();
}

/*
 * Called from the generic idle code.
 */
void arch_cpu_idle(void)
{
	if (cpuidle_idle_call())
		x86_idle();
	else
		local_irq_enable();
}

/*
 * We use this if we don't have any better idle routine..
 */
void default_idle(void)
{
	trace_cpu_idle_rcuidle(1, smp_processor_id());
	safe_halt();
	trace_cpu_idle_rcuidle(PWR_EVENT_EXIT, smp_processor_id());
}
#ifdef CONFIG_APM_MODULE
EXPORT_SYMBOL(default_idle);
#endif

#ifdef CONFIG_XEN
bool xen_set_default_idle(void)
{
	bool ret = !!x86_idle;

	x86_idle = default_idle;

	return ret;
}
#endif
void stop_this_cpu(void *dummy)
{
	local_irq_disable();
	/*
	 * Remove this CPU:
	 */
	set_cpu_online(smp_processor_id(), false);
	disable_local_APIC();

	for (;;)
		halt();
}

bool amd_e400_c1e_detected;
EXPORT_SYMBOL(amd_e400_c1e_detected);

static cpumask_var_t amd_e400_c1e_mask;

void amd_e400_remove_cpu(int cpu)
{
	if (amd_e400_c1e_mask != NULL)
		cpumask_clear_cpu(cpu, amd_e400_c1e_mask);
}

/*
 * AMD Erratum 400 aware idle routine. We check for C1E active in the interrupt
 * pending message MSR. If we detect C1E, then we handle it the same
 * way as C3 power states (local apic timer and TSC stop)
 */
static void amd_e400_idle(void)
{
	if (!amd_e400_c1e_detected) {
		u32 lo, hi;

		rdmsr(MSR_K8_INT_PENDING_MSG, lo, hi);

		if (lo & K8_INTP_C1E_ACTIVE_MASK) {
			amd_e400_c1e_detected = true;
			if (!boot_cpu_has(X86_FEATURE_NONSTOP_TSC))
				mark_tsc_unstable("TSC halt in AMD C1E");
			pr_info("System has AMD C1E enabled\n");
		}
	}

	if (amd_e400_c1e_detected) {
		int cpu = smp_processor_id();

		if (!cpumask_test_cpu(cpu, amd_e400_c1e_mask)) {
			cpumask_set_cpu(cpu, amd_e400_c1e_mask);
			/*
			 * Force broadcast so ACPI can not interfere.
			 */
			clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_FORCE,
					   &cpu);
			pr_info("Switch to broadcast mode on CPU%d\n", cpu);
		}
		clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_ENTER, &cpu);

		default_idle();

		/*
		 * The switch back from broadcast mode needs to be
		 * called with interrupts disabled.
		 */
		 local_irq_disable();
		 clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_EXIT, &cpu);
		 local_irq_enable();
	} else
		default_idle();
}

void __cpuinit select_idle_routine(const struct cpuinfo_x86 *c)
{
#ifdef CONFIG_SMP
	if (boot_option_idle_override == IDLE_POLL && smp_num_siblings > 1)
		pr_warn_once("WARNING: polling idle and HT enabled, performance may degrade\n");
#endif
	if (x86_idle || boot_option_idle_override == IDLE_POLL)
		return;

	if (cpu_has_bug(c, X86_BUG_AMD_APIC_C1E)) {
		/* E400: APIC timer interrupt does not wake up CPU from C1e */
		pr_info("using AMD E400 aware idle routine\n");
		x86_idle = amd_e400_idle;
	} else
		x86_idle = default_idle;
}

void __init init_amd_e400_c1e_mask(void)
{
	/* If we're using amd_e400_idle, we need to allocate amd_e400_c1e_mask. */
	if (x86_idle == amd_e400_idle)
		zalloc_cpumask_var(&amd_e400_c1e_mask, GFP_KERNEL);
}

static int __init idle_setup(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "poll")) {
		pr_info("using polling idle threads\n");
		boot_option_idle_override = IDLE_POLL;
		cpu_idle_poll_ctrl(true);
	} else if (!strcmp(str, "halt")) {
		/*
		 * When the boot option of idle=halt is added, halt is
		 * forced to be used for CPU idle. In such case CPU C2/C3
		 * won't be used again.
		 * To continue to load the CPU idle driver, don't touch
		 * the boot_option_idle_override.
		 */
		x86_idle = default_idle;
		boot_option_idle_override = IDLE_HALT;
	} else if (!strcmp(str, "nomwait")) {
		/*
		 * If the boot option of "idle=nomwait" is added,
		 * it means that mwait will be disabled for CPU C2/C3
		 * states. In such case it won't touch the variable
		 * of boot_option_idle_override.
		 */
		boot_option_idle_override = IDLE_NOMWAIT;
	} else
		return -1;

	return 0;
}
early_param("idle", idle_setup);

unsigned long arch_align_stack(unsigned long sp)
{
	if (!(current->personality & ADDR_NO_RANDOMIZE) && randomize_va_space)
		sp -= get_random_int() % 8192;
	return sp & ~0xf;
}

unsigned long arch_randomize_brk(struct mm_struct *mm)
{
	unsigned long range_end = mm->brk + 0x02000000;
	return randomize_range(mm->brk, range_end, 0) ? : mm->brk;
}

