/*
 *  cpuidle-powernv - idle state cpuidle driver.
 *  Adapted from drivers/cpuidle/cpuidle-pseries
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/cpuidle.h>
#include <linux/cpu.h>
#include <linux/notifier.h>
#include <linux/clockchips.h>
#include <linux/of.h>

#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/runlatch.h>

/* Flags and constants used in PowerNV platform */

#define MAX_POWERNV_IDLE_STATES	8
#define IDLE_USE_INST_NAP	0x00010000 /* Use nap instruction */
#define IDLE_USE_INST_SLEEP	0x00020000 /* Use sleep instruction */

struct cpuidle_driver powernv_idle_driver = {
	.name             = "powernv_idle",
	.owner            = THIS_MODULE,
};

static int max_idle_state;
static struct cpuidle_state *cpuidle_state_table;

static int snooze_loop(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	local_irq_enable();
	set_thread_flag(TIF_POLLING_NRFLAG);

	ppc64_runlatch_off();
	while (!need_resched()) {
		HMT_low();
		HMT_very_low();
	}

	HMT_medium();
	ppc64_runlatch_on();
	clear_thread_flag(TIF_POLLING_NRFLAG);
	smp_mb();
	return index;
}

static int nap_loop(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	ppc64_runlatch_off();
	power7_idle();
	ppc64_runlatch_on();
	return index;
}

static int fastsleep_loop(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	unsigned long old_lpcr = mfspr(SPRN_LPCR);
	unsigned long new_lpcr;

	if (unlikely(system_state < SYSTEM_RUNNING))
		return index;

	new_lpcr = old_lpcr;
	new_lpcr &= ~(LPCR_MER | LPCR_PECE); /* lpcr[mer] must be 0 */

	/* exit powersave upon external interrupt, but not decrementer
	 * interrupt.
	 */
	new_lpcr |= LPCR_PECE0;

	mtspr(SPRN_LPCR, new_lpcr);
	power7_sleep();

	mtspr(SPRN_LPCR, old_lpcr);

	return index;
}

/*
 * States for dedicated partition case.
 */
static struct cpuidle_state powernv_states[MAX_POWERNV_IDLE_STATES] = {
	{ /* Snooze */
		.name = "snooze",
		.desc = "snooze",
		.flags = CPUIDLE_FLAG_TIME_VALID,
		.exit_latency = 0,
		.target_residency = 0,
		.enter = &snooze_loop },
};

static int powernv_cpuidle_add_cpu_notifier(struct notifier_block *n,
			unsigned long action, void *hcpu)
{
	int hotcpu = (unsigned long)hcpu;
	struct cpuidle_device *dev =
				per_cpu(cpuidle_devices, hotcpu);

	if (dev && cpuidle_get_driver()) {
		switch (action) {
		case CPU_ONLINE:
		case CPU_ONLINE_FROZEN:
			cpuidle_pause_and_lock();
			cpuidle_enable_device(dev);
			cpuidle_resume_and_unlock();
			break;

		case CPU_DEAD:
		case CPU_DEAD_FROZEN:
			cpuidle_pause_and_lock();
			cpuidle_disable_device(dev);
			cpuidle_resume_and_unlock();
			break;

		default:
			return NOTIFY_DONE;
		}
	}
	return NOTIFY_OK;
}

static struct notifier_block setup_hotplug_notifier = {
	.notifier_call = powernv_cpuidle_add_cpu_notifier,
};

/*
 * powernv_cpuidle_driver_init()
 */
static int powernv_cpuidle_driver_init(void)
{
	int idle_state;
	struct cpuidle_driver *drv = &powernv_idle_driver;

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

static int powernv_add_idle_states(void)
{
	struct device_node *power_mgt;
	struct property *prop;
	int nr_idle_states = 1; /* Snooze */
	int dt_idle_states;
	u32 *flags;
	int i;

	/* Currently we have snooze statically defined */

	power_mgt = of_find_node_by_path("/ibm,opal/power-mgt");
	if (!power_mgt) {
		pr_warn("opal: PowerMgmt Node not found\n");
		return nr_idle_states;
	}

	prop = of_find_property(power_mgt, "ibm,cpu-idle-state-flags", NULL);
	if (!prop) {
		pr_warn("DT-PowerMgmt: missing ibm,cpu-idle-state-flags\n");
		return nr_idle_states;
	}

	dt_idle_states = prop->length / sizeof(u32);
	flags = (u32 *) prop->value;

	for (i = 0; i < dt_idle_states; i++) {

		if (flags[i] & IDLE_USE_INST_NAP) {
			/* Add NAP state */
			strcpy(powernv_states[nr_idle_states].name, "Nap");
			strcpy(powernv_states[nr_idle_states].desc, "Nap");
			powernv_states[nr_idle_states].flags = CPUIDLE_FLAG_TIME_VALID;
			powernv_states[nr_idle_states].exit_latency = 10;
			powernv_states[nr_idle_states].target_residency = 100;
			powernv_states[nr_idle_states].enter = &nap_loop;
			nr_idle_states++;
		}

		if (flags[i] & IDLE_USE_INST_SLEEP) {
			/* Add FASTSLEEP state */
			strcpy(powernv_states[nr_idle_states].name, "FastSleep");
			strcpy(powernv_states[nr_idle_states].desc, "FastSleep");
			powernv_states[nr_idle_states].flags =
				CPUIDLE_FLAG_TIME_VALID | CPUIDLE_FLAG_TIMER_STOP;
			powernv_states[nr_idle_states].exit_latency = 300;
			powernv_states[nr_idle_states].target_residency = 1000000;
			powernv_states[nr_idle_states].enter = &fastsleep_loop;
			nr_idle_states++;
		}
	}

	return nr_idle_states;
}

/*
 * powernv_idle_probe()
 * Choose state table for shared versus dedicated partition
 */
static int powernv_idle_probe(void)
{
	if (cpuidle_disable != IDLE_NO_OVERRIDE)
		return -ENODEV;

	if (firmware_has_feature(FW_FEATURE_OPALv3)) {
		cpuidle_state_table = powernv_states;
		/* Device tree can indicate more idle states */
		max_idle_state = powernv_add_idle_states();
 	} else
 		return -ENODEV;

	return 0;
}

static int __init powernv_processor_idle_init(void)
{
	int retval;

	retval = powernv_idle_probe();
	if (retval)
		return retval;

	powernv_cpuidle_driver_init();
	retval = cpuidle_register(&powernv_idle_driver, NULL);
	if (retval) {
		printk(KERN_DEBUG "Registration of powernv driver failed.\n");
		return retval;
	}

	register_cpu_notifier(&setup_hotplug_notifier);
	printk(KERN_DEBUG "powernv_idle_driver registered\n");
	return 0;
}

device_initcall(powernv_processor_idle_init);
