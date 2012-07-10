/*
 *  processor_idle - idle state cpuidle driver.
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

#include "plpar_wrappers.h"
#include "pseries.h"

struct cpuidle_driver pseries_idle_driver = {
	.name =		"pseries_idle",
	.owner =	THIS_MODULE,
};

#define MAX_IDLE_STATE_COUNT	2

static int max_idle_state = MAX_IDLE_STATE_COUNT - 1;
static struct cpuidle_device __percpu *pseries_cpuidle_devices;
static struct cpuidle_state *cpuidle_state_table;

void update_smt_snooze_delay(int snooze)
{
	struct cpuidle_driver *drv = cpuidle_get_driver();
	if (drv)
		drv->states[0].target_residency = snooze;
}

static inline void idle_loop_prolog(unsigned long *in_purr, ktime_t *kt_before)
{

	*kt_before = ktime_get_real();
	*in_purr = mfspr(SPRN_PURR);
	/*
	 * Indicate to the HV that we are idle. Now would be
	 * a good time to find other work to dispatch.
	 */
	get_lppaca()->idle = 1;
}

static inline  s64 idle_loop_epilog(unsigned long in_purr, ktime_t kt_before)
{
	get_lppaca()->wait_state_cycles += mfspr(SPRN_PURR) - in_purr;
	get_lppaca()->idle = 0;

	return ktime_to_us(ktime_sub(ktime_get_real(), kt_before));
}

static int snooze_loop(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	unsigned long in_purr;
	ktime_t kt_before;
	unsigned long start_snooze;
	long snooze = drv->states[0].target_residency;

	idle_loop_prolog(&in_purr, &kt_before);

	if (snooze) {
		start_snooze = get_tb() + snooze * tb_ticks_per_usec;
		local_irq_enable();
		set_thread_flag(TIF_POLLING_NRFLAG);

		while ((snooze < 0) || (get_tb() < start_snooze)) {
			if (need_resched() || cpu_is_offline(dev->cpu))
				goto out;
			ppc64_runlatch_off();
			HMT_low();
			HMT_very_low();
		}

		HMT_medium();
		clear_thread_flag(TIF_POLLING_NRFLAG);
		smp_mb();
		local_irq_disable();
	}

out:
	HMT_medium();
	dev->last_residency =
		(int)idle_loop_epilog(in_purr, kt_before);
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
	ktime_t kt_before;

	idle_loop_prolog(&in_purr, &kt_before);
	get_lppaca()->donate_dedicated_cpu = 1;

	ppc64_runlatch_off();
	HMT_medium();
	check_and_cede_processor();

	get_lppaca()->donate_dedicated_cpu = 0;
	dev->last_residency =
		(int)idle_loop_epilog(in_purr, kt_before);
	return index;
}

static int shared_cede_loop(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	unsigned long in_purr;
	ktime_t kt_before;

	idle_loop_prolog(&in_purr, &kt_before);

	/*
	 * Yield the processor to the hypervisor.  We return if
	 * an external interrupt occurs (which are driven prior
	 * to returning here) or if a prod occurs from another
	 * processor. When returning here, external interrupts
	 * are enabled.
	 */
	check_and_cede_processor();

	dev->last_residency =
		(int)idle_loop_epilog(in_purr, kt_before);
	return index;
}

/*
 * States for dedicated partition case.
 */
static struct cpuidle_state dedicated_states[MAX_IDLE_STATE_COUNT] = {
	{ /* Snooze */
		.name = "snooze",
		.desc = "snooze",
		.flags = CPUIDLE_FLAG_TIME_VALID,
		.exit_latency = 0,
		.target_residency = 0,
		.enter = &snooze_loop },
	{ /* CEDE */
		.name = "CEDE",
		.desc = "CEDE",
		.flags = CPUIDLE_FLAG_TIME_VALID,
		.exit_latency = 1,
		.target_residency = 10,
		.enter = &dedicated_cede_loop },
};

/*
 * States for shared partition case.
 */
static struct cpuidle_state shared_states[MAX_IDLE_STATE_COUNT] = {
	{ /* Shared Cede */
		.name = "Shared Cede",
		.desc = "Shared Cede",
		.flags = CPUIDLE_FLAG_TIME_VALID,
		.exit_latency = 0,
		.target_residency = 0,
		.enter = &shared_cede_loop },
};

static int pseries_cpuidle_add_cpu_notifier(struct notifier_block *n,
			unsigned long action, void *hcpu)
{
	int hotcpu = (unsigned long)hcpu;
	struct cpuidle_device *dev =
			per_cpu_ptr(pseries_cpuidle_devices, hotcpu);

	switch (action & 0xf) {
	case CPU_ONLINE:
		if (dev && cpuidle_get_driver()) {
			cpuidle_disable_device(dev);
			cpuidle_enable_device(dev);
		}
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block setup_hotplug_notifier = {
	.notifier_call = pseries_cpuidle_add_cpu_notifier,
};

/*
 * pseries_cpuidle_driver_init()
 */
static int pseries_cpuidle_driver_init(void)
{
	int idle_state;
	struct cpuidle_driver *drv = &pseries_idle_driver;

	drv->state_count = 0;

	for (idle_state = 0; idle_state < MAX_IDLE_STATE_COUNT; ++idle_state) {

		if (idle_state > max_idle_state)
			break;

		/* is the state not enabled? */
		if (cpuidle_state_table[idle_state].enter == NULL)
			continue;

		drv->states[drv->state_count] =	/* structure copy */
			cpuidle_state_table[idle_state];

		if (cpuidle_state_table == dedicated_states)
			drv->states[drv->state_count].target_residency =
				__get_cpu_var(smt_snooze_delay);

		drv->state_count += 1;
	}

	return 0;
}

/* pseries_idle_devices_uninit(void)
 * unregister cpuidle devices and de-allocate memory
 */
static void pseries_idle_devices_uninit(void)
{
	int i;
	struct cpuidle_device *dev;

	for_each_possible_cpu(i) {
		dev = per_cpu_ptr(pseries_cpuidle_devices, i);
		cpuidle_unregister_device(dev);
	}

	free_percpu(pseries_cpuidle_devices);
	return;
}

/* pseries_idle_devices_init()
 * allocate, initialize and register cpuidle device
 */
static int pseries_idle_devices_init(void)
{
	int i;
	struct cpuidle_driver *drv = &pseries_idle_driver;
	struct cpuidle_device *dev;

	pseries_cpuidle_devices = alloc_percpu(struct cpuidle_device);
	if (pseries_cpuidle_devices == NULL)
		return -ENOMEM;

	for_each_possible_cpu(i) {
		dev = per_cpu_ptr(pseries_cpuidle_devices, i);
		dev->state_count = drv->state_count;
		dev->cpu = i;
		if (cpuidle_register_device(dev)) {
			printk(KERN_DEBUG \
				"cpuidle_register_device %d failed!\n", i);
			return -EIO;
		}
	}

	return 0;
}

/*
 * pseries_idle_probe()
 * Choose state table for shared versus dedicated partition
 */
static int pseries_idle_probe(void)
{

	if (!firmware_has_feature(FW_FEATURE_SPLPAR))
		return -ENODEV;

	if (cpuidle_disable != IDLE_NO_OVERRIDE)
		return -ENODEV;

	if (max_idle_state == 0) {
		printk(KERN_DEBUG "pseries processor idle disabled.\n");
		return -EPERM;
	}

	if (get_lppaca()->shared_proc)
		cpuidle_state_table = shared_states;
	else
		cpuidle_state_table = dedicated_states;

	return 0;
}

static int __init pseries_processor_idle_init(void)
{
	int retval;

	retval = pseries_idle_probe();
	if (retval)
		return retval;

	pseries_cpuidle_driver_init();
	retval = cpuidle_register_driver(&pseries_idle_driver);
	if (retval) {
		printk(KERN_DEBUG "Registration of pseries driver failed.\n");
		return retval;
	}

	retval = pseries_idle_devices_init();
	if (retval) {
		pseries_idle_devices_uninit();
		cpuidle_unregister_driver(&pseries_idle_driver);
		return retval;
	}

	register_cpu_notifier(&setup_hotplug_notifier);
	printk(KERN_DEBUG "pseries_idle_driver registered\n");

	return 0;
}

static void __exit pseries_processor_idle_exit(void)
{

	pseries_idle_devices_uninit();
	cpuidle_unregister_driver(&pseries_idle_driver);

	return;
}

module_init(pseries_processor_idle_init);
module_exit(pseries_processor_idle_exit);

MODULE_AUTHOR("Deepthi Dharwar <deepthi@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("Cpuidle driver for POWER");
MODULE_LICENSE("GPL");
