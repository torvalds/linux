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
#include <linux/tick.h>
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
#include <asm/mwait.h>
#include <asm/fpu/internal.h>
#include <asm/debugreg.h>
#include <asm/nmi.h>
#include <asm/tlbflush.h>
#include <asm/mce.h>
#include <asm/vm86.h>
#include <asm/spec-ctrl.h>

/*
 * per-CPU TSS segments. Threads are completely 'soft' on Linux,
 * no more per-task TSS's. The TSS size is kept cacheline-aligned
 * so they are allowed to end up in the .data..cacheline_aligned
 * section. Since TSS's are completely CPU-local, we want them
 * on exact cacheline boundaries, to eliminate cacheline ping-pong.
 */
__visible DEFINE_PER_CPU_SHARED_ALIGNED_USER_MAPPED(struct tss_struct, cpu_tss) = {
	.x86_tss = {
		.sp0 = TOP_OF_INIT_STACK,
#ifdef CONFIG_X86_32
		.ss0 = __KERNEL_DS,
		.ss1 = __KERNEL_CS,
		.io_bitmap_base	= INVALID_IO_BITMAP_OFFSET,
#endif
	 },
#ifdef CONFIG_X86_32
	 /*
	  * Note that the .io_bitmap member must be extra-big. This is because
	  * the CPU will access an additional byte beyond the end of the IO
	  * permission bitmap. The extra byte must be all 1 bits, and must
	  * be within the limit.
	  */
	.io_bitmap		= { [0 ... IO_BITMAP_LONGS] = ~0 },
#endif
};
EXPORT_PER_CPU_SYMBOL(cpu_tss);

#ifdef CONFIG_X86_64
static DEFINE_PER_CPU(unsigned char, is_idle);
#endif

/*
 * this gets called so that we can store lazy state into memory and copy the
 * current task into the new thread.
 */
int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	memcpy(dst, src, arch_task_struct_size);
#ifdef CONFIG_VM86
	dst->thread.vm86 = NULL;
#endif

	return fpu__copy(&dst->thread.fpu, &src->thread.fpu);
}

/*
 * Free current thread data structures etc..
 */
void exit_thread(struct task_struct *tsk)
{
	struct thread_struct *t = &tsk->thread;
	unsigned long *bp = t->io_bitmap_ptr;
	struct fpu *fpu = &t->fpu;

	if (bp) {
		struct tss_struct *tss = &per_cpu(cpu_tss, get_cpu());

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

	free_vm86(t);

	fpu__drop(fpu);
}

void flush_thread(void)
{
	struct task_struct *tsk = current;

	flush_ptrace_hw_breakpoint(tsk);
	memset(tsk->thread.tls_array, 0, sizeof(tsk->thread.tls_array));

	fpu__clear(&tsk->thread.fpu);
}

void disable_TSC(void)
{
	preempt_disable();
	if (!test_and_set_thread_flag(TIF_NOTSC))
		/*
		 * Must flip the CPU state synchronously with
		 * TIF_NOTSC in the current running context.
		 */
		cr4_set_bits(X86_CR4_TSD);
	preempt_enable();
}

static void enable_TSC(void)
{
	preempt_disable();
	if (test_and_clear_thread_flag(TIF_NOTSC))
		/*
		 * Must flip the CPU state synchronously with
		 * TIF_NOTSC in the current running context.
		 */
		cr4_clear_bits(X86_CR4_TSD);
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

static inline void switch_to_bitmap(struct tss_struct *tss,
				    struct thread_struct *prev,
				    struct thread_struct *next,
				    unsigned long tifp, unsigned long tifn)
{
	if (tifn & _TIF_IO_BITMAP) {
		/*
		 * Copy the relevant range of the IO bitmap.
		 * Normally this is 128 bytes or less:
		 */
		memcpy(tss->io_bitmap, next->io_bitmap_ptr,
		       max(prev->io_bitmap_max, next->io_bitmap_max));
	} else if (tifp & _TIF_IO_BITMAP) {
		/*
		 * Clear any possible leftover bits:
		 */
		memset(tss->io_bitmap, 0xff, prev->io_bitmap_max);
	}
}

#ifdef CONFIG_SMP

struct ssb_state {
	struct ssb_state	*shared_state;
	raw_spinlock_t		lock;
	unsigned int		disable_state;
	unsigned long		local_state;
};

#define LSTATE_SSB	0

static DEFINE_PER_CPU(struct ssb_state, ssb_state);

void speculative_store_bypass_ht_init(void)
{
	struct ssb_state *st = this_cpu_ptr(&ssb_state);
	unsigned int this_cpu = smp_processor_id();
	unsigned int cpu;

	st->local_state = 0;

	/*
	 * Shared state setup happens once on the first bringup
	 * of the CPU. It's not destroyed on CPU hotunplug.
	 */
	if (st->shared_state)
		return;

	raw_spin_lock_init(&st->lock);

	/*
	 * Go over HT siblings and check whether one of them has set up the
	 * shared state pointer already.
	 */
	for_each_cpu(cpu, topology_sibling_cpumask(this_cpu)) {
		if (cpu == this_cpu)
			continue;

		if (!per_cpu(ssb_state, cpu).shared_state)
			continue;

		/* Link it to the state of the sibling: */
		st->shared_state = per_cpu(ssb_state, cpu).shared_state;
		return;
	}

	/*
	 * First HT sibling to come up on the core.  Link shared state of
	 * the first HT sibling to itself. The siblings on the same core
	 * which come up later will see the shared state pointer and link
	 * themself to the state of this CPU.
	 */
	st->shared_state = st;
}

/*
 * Logic is: First HT sibling enables SSBD for both siblings in the core
 * and last sibling to disable it, disables it for the whole core. This how
 * MSR_SPEC_CTRL works in "hardware":
 *
 *  CORE_SPEC_CTRL = THREAD0_SPEC_CTRL | THREAD1_SPEC_CTRL
 */
static __always_inline void amd_set_core_ssb_state(unsigned long tifn)
{
	struct ssb_state *st = this_cpu_ptr(&ssb_state);
	u64 msr = x86_amd_ls_cfg_base;

	if (!static_cpu_has(X86_FEATURE_ZEN)) {
		msr |= ssbd_tif_to_amd_ls_cfg(tifn);
		wrmsrl(MSR_AMD64_LS_CFG, msr);
		return;
	}

	if (tifn & _TIF_SSBD) {
		/*
		 * Since this can race with prctl(), block reentry on the
		 * same CPU.
		 */
		if (__test_and_set_bit(LSTATE_SSB, &st->local_state))
			return;

		msr |= x86_amd_ls_cfg_ssbd_mask;

		raw_spin_lock(&st->shared_state->lock);
		/* First sibling enables SSBD: */
		if (!st->shared_state->disable_state)
			wrmsrl(MSR_AMD64_LS_CFG, msr);
		st->shared_state->disable_state++;
		raw_spin_unlock(&st->shared_state->lock);
	} else {
		if (!__test_and_clear_bit(LSTATE_SSB, &st->local_state))
			return;

		raw_spin_lock(&st->shared_state->lock);
		st->shared_state->disable_state--;
		if (!st->shared_state->disable_state)
			wrmsrl(MSR_AMD64_LS_CFG, msr);
		raw_spin_unlock(&st->shared_state->lock);
	}
}
#else
static __always_inline void amd_set_core_ssb_state(unsigned long tifn)
{
	u64 msr = x86_amd_ls_cfg_base | ssbd_tif_to_amd_ls_cfg(tifn);

	wrmsrl(MSR_AMD64_LS_CFG, msr);
}
#endif

static __always_inline void amd_set_ssb_virt_state(unsigned long tifn)
{
	/*
	 * SSBD has the same definition in SPEC_CTRL and VIRT_SPEC_CTRL,
	 * so ssbd_tif_to_spec_ctrl() just works.
	 */
	wrmsrl(MSR_AMD64_VIRT_SPEC_CTRL, ssbd_tif_to_spec_ctrl(tifn));
}

static __always_inline void intel_set_ssb_state(unsigned long tifn)
{
	u64 msr = x86_spec_ctrl_base | ssbd_tif_to_spec_ctrl(tifn);

	wrmsrl(MSR_IA32_SPEC_CTRL, msr);
}

static __always_inline void __speculative_store_bypass_update(unsigned long tifn)
{
	if (static_cpu_has(X86_FEATURE_VIRT_SSBD))
		amd_set_ssb_virt_state(tifn);
	else if (static_cpu_has(X86_FEATURE_LS_CFG_SSBD))
		amd_set_core_ssb_state(tifn);
	else
		intel_set_ssb_state(tifn);
}

void speculative_store_bypass_update(unsigned long tif)
{
	preempt_disable();
	__speculative_store_bypass_update(tif);
	preempt_enable();
}

void __switch_to_xtra(struct task_struct *prev_p, struct task_struct *next_p,
		      struct tss_struct *tss)
{
	struct thread_struct *prev, *next;
	unsigned long tifp, tifn;

	prev = &prev_p->thread;
	next = &next_p->thread;

	tifn = READ_ONCE(task_thread_info(next_p)->flags);
	tifp = READ_ONCE(task_thread_info(prev_p)->flags);
	switch_to_bitmap(tss, prev, next, tifp, tifn);

	propagate_user_return_notify(prev_p, next_p);

	if ((tifp & _TIF_BLOCKSTEP || tifn & _TIF_BLOCKSTEP) &&
	    arch_has_block_step()) {
		unsigned long debugctl, msk;

		rdmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
		debugctl &= ~DEBUGCTLMSR_BTF;
		msk = tifn & _TIF_BLOCKSTEP;
		debugctl |= (msk >> TIF_BLOCKSTEP) << DEBUGCTLMSR_BTF_SHIFT;
		wrmsrl(MSR_IA32_DEBUGCTLMSR, debugctl);
	}

	if ((tifp ^ tifn) & _TIF_NOTSC)
		cr4_toggle_bits(X86_CR4_TSD);

	if ((tifp ^ tifn) & _TIF_SSBD)
		__speculative_store_bypass_update(tifn);
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
	idle_notifier_call_chain(IDLE_START);
}

static void __exit_idle(void)
{
	if (x86_test_and_clear_bit_percpu(0, is_idle) == 0)
		return;
	idle_notifier_call_chain(IDLE_END);
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
	x86_idle();
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
	mcheck_cpu_clear(this_cpu_ptr(&cpu_info));

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
			/* Force broadcast so ACPI can not interfere. */
			tick_broadcast_force();
			pr_info("Switch to broadcast mode on CPU%d\n", cpu);
		}
		tick_broadcast_enter();

		default_idle();

		/*
		 * The switch back from broadcast mode needs to be
		 * called with interrupts disabled.
		 */
		local_irq_disable();
		tick_broadcast_exit();
		local_irq_enable();
	} else
		default_idle();
}

/*
 * Intel Core2 and older machines prefer MWAIT over HALT for C1.
 * We can't rely on cpuidle installing MWAIT, because it will not load
 * on systems that support only C1 -- so the boot default must be MWAIT.
 *
 * Some AMD machines are the opposite, they depend on using HALT.
 *
 * So for default C1, which is used during boot until cpuidle loads,
 * use MWAIT-C1 on Intel HW that has it, else use HALT.
 */
static int prefer_mwait_c1_over_halt(const struct cpuinfo_x86 *c)
{
	if (c->x86_vendor != X86_VENDOR_INTEL)
		return 0;

	if (!cpu_has(c, X86_FEATURE_MWAIT))
		return 0;

	return 1;
}

/*
 * MONITOR/MWAIT with no hints, used for default C1 state. This invokes MWAIT
 * with interrupts enabled and no flags, which is backwards compatible with the
 * original MWAIT implementation.
 */
static void mwait_idle(void)
{
	if (!current_set_polling_and_test()) {
		trace_cpu_idle_rcuidle(1, smp_processor_id());
		if (this_cpu_has(X86_BUG_CLFLUSH_MONITOR)) {
			smp_mb(); /* quirk */
			clflush((void *)&current_thread_info()->flags);
			smp_mb(); /* quirk */
		}

		__monitor((void *)&current_thread_info()->flags, 0, 0);
		if (!need_resched())
			__sti_mwait(0, 0);
		else
			local_irq_enable();
		trace_cpu_idle_rcuidle(PWR_EVENT_EXIT, smp_processor_id());
	} else {
		local_irq_enable();
	}
	__current_clr_polling();
}

void select_idle_routine(const struct cpuinfo_x86 *c)
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
	} else if (prefer_mwait_c1_over_halt(c)) {
		pr_info("using mwait in idle threads\n");
		x86_idle = mwait_idle;
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

/*
 * Called from fs/proc with a reference on @p to find the function
 * which called into schedule(). This needs to be done carefully
 * because the task might wake up and we might look at a stack
 * changing under us.
 */
unsigned long get_wchan(struct task_struct *p)
{
	unsigned long start, bottom, top, sp, fp, ip;
	int count = 0;

	if (!p || p == current || p->state == TASK_RUNNING)
		return 0;

	start = (unsigned long)task_stack_page(p);
	if (!start)
		return 0;

	/*
	 * Layout of the stack page:
	 *
	 * ----------- topmax = start + THREAD_SIZE - sizeof(unsigned long)
	 * PADDING
	 * ----------- top = topmax - TOP_OF_KERNEL_STACK_PADDING
	 * stack
	 * ----------- bottom = start + sizeof(thread_info)
	 * thread_info
	 * ----------- start
	 *
	 * The tasks stack pointer points at the location where the
	 * framepointer is stored. The data on the stack is:
	 * ... IP FP ... IP FP
	 *
	 * We need to read FP and IP, so we need to adjust the upper
	 * bound by another unsigned long.
	 */
	top = start + THREAD_SIZE - TOP_OF_KERNEL_STACK_PADDING;
	top -= 2 * sizeof(unsigned long);
	bottom = start + sizeof(struct thread_info);

	sp = READ_ONCE(p->thread.sp);
	if (sp < bottom || sp > top)
		return 0;

	fp = READ_ONCE_NOCHECK(*(unsigned long *)sp);
	do {
		if (fp < bottom || fp > top)
			return 0;
		ip = READ_ONCE_NOCHECK(*(unsigned long *)(fp + sizeof(unsigned long)));
		if (!in_sched_functions(ip))
			return ip;
		fp = READ_ONCE_NOCHECK(*(unsigned long *)fp);
	} while (count++ < 16 && p->state != TASK_RUNNING);
	return 0;
}
