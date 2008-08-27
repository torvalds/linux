#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/pm.h>
#include <linux/clockchips.h>
#include <asm/system.h>

unsigned long idle_halt;
EXPORT_SYMBOL(idle_halt);
unsigned long idle_nomwait;
EXPORT_SYMBOL(idle_nomwait);

struct kmem_cache *task_xstate_cachep;
static int force_mwait __cpuinitdata;

int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	*dst = *src;
	if (src->thread.xstate) {
		dst->thread.xstate = kmem_cache_alloc(task_xstate_cachep,
						      GFP_KERNEL);
		if (!dst->thread.xstate)
			return -ENOMEM;
		WARN_ON((unsigned long)dst->thread.xstate & 15);
		memcpy(dst->thread.xstate, src->thread.xstate, xstate_size);
	}
	return 0;
}

void free_thread_xstate(struct task_struct *tsk)
{
	if (tsk->thread.xstate) {
		kmem_cache_free(task_xstate_cachep, tsk->thread.xstate);
		tsk->thread.xstate = NULL;
	}
}

void free_thread_info(struct thread_info *ti)
{
	free_thread_xstate(ti->task);
	free_pages((unsigned long)ti, get_order(THREAD_SIZE));
}

void arch_task_cache_init(void)
{
        task_xstate_cachep =
        	kmem_cache_create("task_xstate", xstate_size,
				  __alignof__(union thread_xstate),
				  SLAB_PANIC, NULL);
}

/*
 * Idle related variables and functions
 */
unsigned long boot_option_idle_override = 0;
EXPORT_SYMBOL(boot_option_idle_override);

/*
 * Powermanagement idle function, if any..
 */
void (*pm_idle)(void);
EXPORT_SYMBOL(pm_idle);

#ifdef CONFIG_X86_32
/*
 * This halt magic was a workaround for ancient floppy DMA
 * wreckage. It should be safe to remove.
 */
static int hlt_counter;
void disable_hlt(void)
{
	hlt_counter++;
}
EXPORT_SYMBOL(disable_hlt);

void enable_hlt(void)
{
	hlt_counter--;
}
EXPORT_SYMBOL(enable_hlt);

static inline int hlt_use_halt(void)
{
	return (!hlt_counter && boot_cpu_data.hlt_works_ok);
}
#else
static inline int hlt_use_halt(void)
{
	return 1;
}
#endif

/*
 * We use this if we don't have any better
 * idle routine..
 */
void default_idle(void)
{
	if (hlt_use_halt()) {
		current_thread_info()->status &= ~TS_POLLING;
		/*
		 * TS_POLLING-cleared state must be visible before we
		 * test NEED_RESCHED:
		 */
		smp_mb();

		if (!need_resched())
			safe_halt();	/* enables interrupts racelessly */
		else
			local_irq_enable();
		current_thread_info()->status |= TS_POLLING;
	} else {
		local_irq_enable();
		/* loop is done by the caller */
		cpu_relax();
	}
}
#ifdef CONFIG_APM_MODULE
EXPORT_SYMBOL(default_idle);
#endif

static void do_nothing(void *unused)
{
}

/*
 * cpu_idle_wait - Used to ensure that all the CPUs discard old value of
 * pm_idle and update to new pm_idle value. Required while changing pm_idle
 * handler on SMP systems.
 *
 * Caller must have changed pm_idle to the new value before the call. Old
 * pm_idle value will not be used by any CPU after the return of this function.
 */
void cpu_idle_wait(void)
{
	smp_mb();
	/* kick all the CPUs so that they exit out of pm_idle */
	smp_call_function(do_nothing, NULL, 1);
}
EXPORT_SYMBOL_GPL(cpu_idle_wait);

/*
 * This uses new MONITOR/MWAIT instructions on P4 processors with PNI,
 * which can obviate IPI to trigger checking of need_resched.
 * We execute MONITOR against need_resched and enter optimized wait state
 * through MWAIT. Whenever someone changes need_resched, we would be woken
 * up from MWAIT (without an IPI).
 *
 * New with Core Duo processors, MWAIT can take some hints based on CPU
 * capability.
 */
void mwait_idle_with_hints(unsigned long ax, unsigned long cx)
{
	if (!need_resched()) {
		__monitor((void *)&current_thread_info()->flags, 0, 0);
		smp_mb();
		if (!need_resched())
			__mwait(ax, cx);
	}
}

/* Default MONITOR/MWAIT with no hints, used for default C1 state */
static void mwait_idle(void)
{
	if (!need_resched()) {
		__monitor((void *)&current_thread_info()->flags, 0, 0);
		smp_mb();
		if (!need_resched())
			__sti_mwait(0, 0);
		else
			local_irq_enable();
	} else
		local_irq_enable();
}

/*
 * On SMP it's slightly faster (but much more power-consuming!)
 * to poll the ->work.need_resched flag instead of waiting for the
 * cross-CPU IPI to arrive. Use this option with caution.
 */
static void poll_idle(void)
{
	local_irq_enable();
	while (!need_resched())
		cpu_relax();
}

/*
 * mwait selection logic:
 *
 * It depends on the CPU. For AMD CPUs that support MWAIT this is
 * wrong. Family 0x10 and 0x11 CPUs will enter C1 on HLT. Powersavings
 * then depend on a clock divisor and current Pstate of the core. If
 * all cores of a processor are in halt state (C1) the processor can
 * enter the C1E (C1 enhanced) state. If mwait is used this will never
 * happen.
 *
 * idle=mwait overrides this decision and forces the usage of mwait.
 */
static int __cpuinitdata force_mwait;

#define MWAIT_INFO			0x05
#define MWAIT_ECX_EXTENDED_INFO		0x01
#define MWAIT_EDX_C1			0xf0

static int __cpuinit mwait_usable(const struct cpuinfo_x86 *c)
{
	u32 eax, ebx, ecx, edx;

	if (force_mwait)
		return 1;

	if (c->cpuid_level < MWAIT_INFO)
		return 0;

	cpuid(MWAIT_INFO, &eax, &ebx, &ecx, &edx);
	/* Check, whether EDX has extended info about MWAIT */
	if (!(ecx & MWAIT_ECX_EXTENDED_INFO))
		return 1;

	/*
	 * edx enumeratios MONITOR/MWAIT extensions. Check, whether
	 * C1  supports MWAIT
	 */
	return (edx & MWAIT_EDX_C1);
}

/*
 * Check for AMD CPUs, which have potentially C1E support
 */
static int __cpuinit check_c1e_idle(const struct cpuinfo_x86 *c)
{
	if (c->x86_vendor != X86_VENDOR_AMD)
		return 0;

	if (c->x86 < 0x0F)
		return 0;

	/* Family 0x0f models < rev F do not have C1E */
	if (c->x86 == 0x0f && c->x86_model < 0x40)
		return 0;

	return 1;
}

/*
 * C1E aware idle routine. We check for C1E active in the interrupt
 * pending message MSR. If we detect C1E, then we handle it the same
 * way as C3 power states (local apic timer and TSC stop)
 */
static void c1e_idle(void)
{
	static cpumask_t c1e_mask = CPU_MASK_NONE;
	static int c1e_detected;

	if (need_resched())
		return;

	if (!c1e_detected) {
		u32 lo, hi;

		rdmsr(MSR_K8_INT_PENDING_MSG, lo, hi);
		if (lo & K8_INTP_C1E_ACTIVE_MASK) {
			c1e_detected = 1;
			mark_tsc_unstable("TSC halt in C1E");
			printk(KERN_INFO "System has C1E enabled\n");
		}
	}

	if (c1e_detected) {
		int cpu = smp_processor_id();

		if (!cpu_isset(cpu, c1e_mask)) {
			cpu_set(cpu, c1e_mask);
			/*
			 * Force broadcast so ACPI can not interfere. Needs
			 * to run with interrupts enabled as it uses
			 * smp_function_call.
			 */
			local_irq_enable();
			clockevents_notify(CLOCK_EVT_NOTIFY_BROADCAST_FORCE,
					   &cpu);
			printk(KERN_INFO "Switch to broadcast mode on CPU%d\n",
			       cpu);
			local_irq_disable();
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
#ifdef CONFIG_X86_SMP
	if (pm_idle == poll_idle && smp_num_siblings > 1) {
		printk(KERN_WARNING "WARNING: polling idle and HT enabled,"
			" performance may degrade.\n");
	}
#endif
	if (pm_idle)
		return;

	if (cpu_has(c, X86_FEATURE_MWAIT) && mwait_usable(c)) {
		/*
		 * One CPU supports mwait => All CPUs supports mwait
		 */
		printk(KERN_INFO "using mwait in idle threads.\n");
		pm_idle = mwait_idle;
	} else if (check_c1e_idle(c)) {
		printk(KERN_INFO "using C1E aware idle routine\n");
		pm_idle = c1e_idle;
	} else
		pm_idle = default_idle;
}

static int __init idle_setup(char *str)
{
	if (!str)
		return -EINVAL;

	if (!strcmp(str, "poll")) {
		printk("using polling idle threads.\n");
		pm_idle = poll_idle;
	} else if (!strcmp(str, "mwait"))
		force_mwait = 1;
	else if (!strcmp(str, "halt")) {
		/*
		 * When the boot option of idle=halt is added, halt is
		 * forced to be used for CPU idle. In such case CPU C2/C3
		 * won't be used again.
		 * To continue to load the CPU idle driver, don't touch
		 * the boot_option_idle_override.
		 */
		pm_idle = default_idle;
		idle_halt = 1;
		return 0;
	} else if (!strcmp(str, "nomwait")) {
		/*
		 * If the boot option of "idle=nomwait" is added,
		 * it means that mwait will be disabled for CPU C2/C3
		 * states. In such case it won't touch the variable
		 * of boot_option_idle_override.
		 */
		idle_nomwait = 1;
		return 0;
	} else
		return -1;

	boot_option_idle_override = 1;
	return 0;
}
early_param("idle", idle_setup);

