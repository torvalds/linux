/*
 *  cpuidle-pseries - idle state cpuidle driver.
 *  Adapted from drivers/idle/intel_idle.c and
 *  drivers/acpi/processor_idle.c
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/cpuidle.h>
#include <linux/cpu.h>
#include <linux/notifier.h>

#include <asm/paca.h>
#include <asm/reg.h>
#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/runlatch.h>
#include <asm/plpar_wrappers.h>

struct cpuidle_driver pseries_idle_driver = {
	.name             = "pseries_idle",
	.owner            = THIS_MODULE,
};

static int max_idle_state __read_mostly;
static struct cpuidle_state *cpuidle_state_table __read_mostly;
static u64 snooze_timeout __read_mostly;
static bool snooze_timeout_en __read_mostly;

static inline void idle_loop_prolog(unsigned long *in_purr)
{
	ppc64_runlatch_off();
	*in_purr = mfspr(SPRN_PURR);
	/*
	 * Indicate to the HV that we are idle. Now would be
	 * a good time to find other work to dispatch.
	 */
	get_lppaca()->idle = 1;
}

static inline void idle_loop_epilog(unsigned long in_purr)
{
	u64 wait_cycles;

	wait_cycles = be64_to_cpu(get_lppaca()->wait_state_cycles);
	wait_cycles += mfspr(SPRN_PURR) - in_purr;
	get_lppaca()->wait_state_cycles = cpu_to_be64(wait_cycles);
	get_lppaca()->idle = 0;

	if (irqs_disabled())
		local_irq_enable();
	ppc64_runlatch_on();
}

static int snooze_loop(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	unsigned long in_purr;
	u64 snooze_exit_time;

	set_thread_flag(TIF_POLLING_NRFLAG);

	idle_loop_prolog(&in_purr);
	local_irq_enable();
	snooze_exit_time = get_tb() + snooze_timeout;

	while (!need_resched()) {
		HMT_low();
		HMT_very_low();
		if (likely(snooze_timeout_en) && get_tb() > snooze_exit_time) {
			/*
			 * Task has not woken up but we are exiting the polling
			 * loop anyway. Require a barrier after polling is
			 * cleared to order subsequent test of need_resched().
			 */
			clear_thread_flag(TIF_POLLING_NRFLAG);
			smp_mb();
			break;
		}
	}

	HMT_medium();
	clear_thread_flag(TIF_POLLING_NRFLAG);

	idle_loop_epilog(in_purr);

	return index;
}

static void check_and_cede_processor(void)
{
	/*
	 * Ensure our interrupt state is properly tracked,
	 * also checks if no interrupt has occurred while we
	 * were soft-disabled
	 */
	if (prep_irq_for_idle()) {
		cede_processor();
#ifdef CONFIG_TRACE_IRQFLAGS
		/* Ensure that H_CEDE returns with IRQs on */
		if (WARN_ON(!(mfmsr() & MSR_EE)))
			__hard_irq_enable();
#endif
	}
}

static int dedicated_cede_loop(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	unsigned long in_purr;

	idle_loop_prolog(&in_purr);
	get_lppaca()->donate_dedicated_cpu = 1;

	HMT_medium();
	check_and_cede_processor();

	get_lppaca()->donate_dedicated_cpu = 0;

	idle_loop_epilog(in_purr);

	return index;
}

static int shared_cede_loop(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	unsigned long in_purr;

	idle_loop_prolog(&in_purr);

	/*
	 * Yield the processor to the hypervisor.  We return if
	 * an external interrupt occurs (which are driven prior
	 * to returning here) or if a prod occurs from another
	 * processor. When returning here, external interrupts
	 * are enabled.
	 */
	check_and_cede_processor();

	idle_loop_epilog(in_purr);

	return index;
}

/*
 * States for dedicated partition case.
 */
static struct cpuidle_state dedicated_states[] = {
	{ /* Snooze */
		.name = "snooze",
		.desc = "snooze",
		.exit_latency = 0,
		.target_residency = 0,
		.enter = &snooze_loop },
	{ /* CEDE */
		.name = "CEDE",
		.desc = "CEDE",
		.exit_latency = 10,
		.target_residency = 100,
		.enter = &dedicated_cede_loop },
};

/*
 * States for shared partition case.
 */
static struct cpuidle_state shared_states[] = {
	{ /* Shared Cede */
		.name = "Shared Cede",
		.desc = "Shared Cede",
		.exit_latency = 0,
		.target_residency = 0,
		.enter = &shared_cede_loop },
};

static int pseries_cpuidle_cpu_online(unsigned int cpu)
{
	struct cpuidle_device *dev = per_cpu(cpuidle_devices, cpu);

	if (dev && cpuidle_get_driver()) {
		cpuidle_pause_and_lock();
		cpuidle_enable_device(dev);
		cpuidle_resume_and_unlock();
	}
	return 0;
}

static int pseries_cpuidle_cpu_dead(unsigned int cpu)
{
	struct cpuidle_device *dev = per_cpu(cpuidle_devices, cpu);

	if (dev && cpuidle_get_driver()) {
		cpuidle_pause_and_lock();
		cpuidle_disable_device(dev);
		cpuidle_resume_and_unlock();
	}
	return 0;
}

/*
 * pseries_cpuidle_driver_init()
 */
static int pseries_cpuidle_driver_init(void)
{
	int idle_state;
	struct cpuidle_driver *drv = &pseries_idle_driver;

	drv->state_count = 0;

	for (idle_state = 0; idle_state < max_idle_state; ++idle_state) {
		/* Is the state not enabled? */
		if (cpuidle_state_table[idle_state].enter == NULL)
			continue;

		drv->states[drv->state_count] =	/* structure copy */
			cpuidle_state_table[idle_state];

		drv->state_count += 1;
	}

	return 0;
}

/*
 * pseries_idle_probe()
 * Choose state table for shared versus dedicated partition
 */
static int pseries_idle_probe(void)
{

	if (cpuidle_disable != IDLE_NO_OVERRIDE)
		return -ENODEV;

	if (firmware_has_feature(FW_FEATURE_SPLPAR)) {
		if (lppaca_shared_proc(get_lppaca())) {
			cpuidle_state_table = shared_states;
			max_idle_state = ARRAY_SIZE(shared_states);
		} else {
			cpuidle_state_table = dedicated_states;
			max_idle_state = ARRAY_SIZE(dedicated_states);
		}
	} else
		return -ENODEV;

	if (max_idle_state > 1) {
		snooze_timeout_en = true;
		snooze_timeout = cpuidle_state_table[1].target_residency *
				 tb_ticks_per_usec;
	}
	return 0;
}

static int __init pseries_processor_idle_init(void)
{
	int retval;

	retval = pseries_idle_probe();
	if (retval)
		return retval;

	pseries_cpuidle_driver_init();
	retval = cpuidle_register(&pseries_idle_driver, NULL);
	if (retval) {
		printk(KERN_DEBUG "Registration of pseries driver failed.\n");
		return retval;
	}

	retval = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					   "cpuidle/pseries:online",
					   pseries_cpuidle_cpu_online, NULL);
	WARN_ON(retval < 0);
	retval = cpuhp_setup_state_nocalls(CPUHP_CPUIDLE_DEAD,
					   "cpuidle/pseries:DEAD", NULL,
					   pseries_cpuidle_cpu_dead);
	WARN_ON(retval < 0);
	printk(KERN_DEBUG "pseries_idle_driver registered\n");
	return 0;
}

device_initcall(pseries_processor_idle_init);
