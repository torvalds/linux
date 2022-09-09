// SPDX-License-Identifier: GPL-2.0
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
#include <linux/slab.h>

#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/opal.h>
#include <asm/runlatch.h>
#include <asm/cpuidle.h>

/*
 * Expose only those Hardware idle states via the cpuidle framework
 * that have latency value below POWERNV_THRESHOLD_LATENCY_NS.
 */
#define POWERNV_THRESHOLD_LATENCY_NS 200000

static struct cpuidle_driver powernv_idle_driver = {
	.name             = "powernv_idle",
	.owner            = THIS_MODULE,
};

static int max_idle_state __read_mostly;
static struct cpuidle_state *cpuidle_state_table __read_mostly;

struct stop_psscr_table {
	u64 val;
	u64 mask;
};

static struct stop_psscr_table stop_psscr_table[CPUIDLE_STATE_MAX] __read_mostly;

static u64 default_snooze_timeout __read_mostly;
static bool snooze_timeout_en __read_mostly;

static u64 get_snooze_timeout(struct cpuidle_device *dev,
			      struct cpuidle_driver *drv,
			      int index)
{
	int i;

	if (unlikely(!snooze_timeout_en))
		return default_snooze_timeout;

	for (i = index + 1; i < drv->state_count; i++) {
		if (dev->states_usage[i].disable)
			continue;

		return drv->states[i].target_residency * tb_ticks_per_usec;
	}

	return default_snooze_timeout;
}

static int snooze_loop(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	u64 snooze_exit_time;

	set_thread_flag(TIF_POLLING_NRFLAG);

	local_irq_enable();

	snooze_exit_time = get_tb() + get_snooze_timeout(dev, drv, index);
	ppc64_runlatch_off();
	HMT_very_low();
	while (!need_resched()) {
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
	ppc64_runlatch_on();
	clear_thread_flag(TIF_POLLING_NRFLAG);

	local_irq_disable();

	return index;
}

static int nap_loop(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	power7_idle_type(PNV_THREAD_NAP);

	return index;
}

/* Register for fastsleep only in oneshot mode of broadcast */
#ifdef CONFIG_TICK_ONESHOT
static int fastsleep_loop(struct cpuidle_device *dev,
				struct cpuidle_driver *drv,
				int index)
{
	unsigned long old_lpcr = mfspr(SPRN_LPCR);
	unsigned long new_lpcr;

	if (unlikely(system_state < SYSTEM_RUNNING))
		return index;

	new_lpcr = old_lpcr;
	/* Do not exit powersave upon decrementer as we've setup the timer
	 * offload.
	 */
	new_lpcr &= ~LPCR_PECE1;

	mtspr(SPRN_LPCR, new_lpcr);

	power7_idle_type(PNV_THREAD_SLEEP);

	mtspr(SPRN_LPCR, old_lpcr);

	return index;
}
#endif

static int stop_loop(struct cpuidle_device *dev,
		     struct cpuidle_driver *drv,
		     int index)
{
	arch300_idle_type(stop_psscr_table[index].val,
			 stop_psscr_table[index].mask);
	return index;
}

/*
 * States for dedicated partition case.
 */
static struct cpuidle_state powernv_states[CPUIDLE_STATE_MAX] = {
	{ /* Snooze */
		.name = "snooze",
		.desc = "snooze",
		.exit_latency = 0,
		.target_residency = 0,
		.enter = snooze_loop },
};

static int powernv_cpuidle_cpu_online(unsigned int cpu)
{
	struct cpuidle_device *dev = per_cpu(cpuidle_devices, cpu);

	if (dev && cpuidle_get_driver()) {
		cpuidle_pause_and_lock();
		cpuidle_enable_device(dev);
		cpuidle_resume_and_unlock();
	}
	return 0;
}

static int powernv_cpuidle_cpu_dead(unsigned int cpu)
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

	/*
	 * On the PowerNV platform cpu_present may be less than cpu_possible in
	 * cases when firmware detects the CPU, but it is not available to the
	 * OS.  If CONFIG_HOTPLUG_CPU=n, then such CPUs are not hotplugable at
	 * run time and hence cpu_devices are not created for those CPUs by the
	 * generic topology_init().
	 *
	 * drv->cpumask defaults to cpu_possible_mask in
	 * __cpuidle_driver_init().  This breaks cpuidle on PowerNV where
	 * cpu_devices are not created for CPUs in cpu_possible_mask that
	 * cannot be hot-added later at run time.
	 *
	 * Trying cpuidle_register_device() on a CPU without a cpu_device is
	 * incorrect, so pass a correct CPU mask to the generic cpuidle driver.
	 */

	drv->cpumask = (struct cpumask *)cpu_present_mask;

	return 0;
}

static inline void add_powernv_state(int index, const char *name,
				     unsigned int flags,
				     int (*idle_fn)(struct cpuidle_device *,
						    struct cpuidle_driver *,
						    int),
				     unsigned int target_residency,
				     unsigned int exit_latency,
				     u64 psscr_val, u64 psscr_mask)
{
	strlcpy(powernv_states[index].name, name, CPUIDLE_NAME_LEN);
	strlcpy(powernv_states[index].desc, name, CPUIDLE_NAME_LEN);
	powernv_states[index].flags = flags;
	powernv_states[index].target_residency = target_residency;
	powernv_states[index].exit_latency = exit_latency;
	powernv_states[index].enter = idle_fn;
	/* For power8 and below psscr_* will be 0 */
	stop_psscr_table[index].val = psscr_val;
	stop_psscr_table[index].mask = psscr_mask;
}

extern u32 pnv_get_supported_cpuidle_states(void);
static int powernv_add_idle_states(void)
{
	int nr_idle_states = 1; /* Snooze */
	int dt_idle_states;
	u32 has_stop_states = 0;
	int i;
	u32 supported_flags = pnv_get_supported_cpuidle_states();


	/* Currently we have snooze statically defined */
	if (nr_pnv_idle_states <= 0) {
		pr_warn("cpuidle-powernv : Only Snooze is available\n");
		goto out;
	}

	/* TODO: Count only states which are eligible for cpuidle */
	dt_idle_states = nr_pnv_idle_states;

	/*
	 * Since snooze is used as first idle state, max idle states allowed is
	 * CPUIDLE_STATE_MAX -1
	 */
	if (nr_pnv_idle_states > CPUIDLE_STATE_MAX - 1) {
		pr_warn("cpuidle-powernv: discovered idle states more than allowed");
		dt_idle_states = CPUIDLE_STATE_MAX - 1;
	}

	/*
	 * If the idle states use stop instruction, probe for psscr values
	 * and psscr mask which are necessary to specify required stop level.
	 */
	has_stop_states = (pnv_idle_states[0].flags &
			   (OPAL_PM_STOP_INST_FAST | OPAL_PM_STOP_INST_DEEP));

	for (i = 0; i < dt_idle_states; i++) {
		unsigned int exit_latency, target_residency;
		bool stops_timebase = false;
		struct pnv_idle_states_t *state = &pnv_idle_states[i];

		/*
		 * Skip the platform idle state whose flag isn't in
		 * the supported_cpuidle_states flag mask.
		 */
		if ((state->flags & supported_flags) != state->flags)
			continue;
		/*
		 * If an idle state has exit latency beyond
		 * POWERNV_THRESHOLD_LATENCY_NS then don't use it
		 * in cpu-idle.
		 */
		if (state->latency_ns > POWERNV_THRESHOLD_LATENCY_NS)
			continue;
		/*
		 * Firmware passes residency and latency values in ns.
		 * cpuidle expects it in us.
		 */
		exit_latency = DIV_ROUND_UP(state->latency_ns, 1000);
		target_residency = DIV_ROUND_UP(state->residency_ns, 1000);

		if (has_stop_states && !(state->valid))
				continue;

		if (state->flags & OPAL_PM_TIMEBASE_STOP)
			stops_timebase = true;

		if (state->flags & OPAL_PM_NAP_ENABLED) {
			/* Add NAP state */
			add_powernv_state(nr_idle_states, "Nap",
					  CPUIDLE_FLAG_NONE, nap_loop,
					  target_residency, exit_latency, 0, 0);
		} else if (has_stop_states && !stops_timebase) {
			add_powernv_state(nr_idle_states, state->name,
					  CPUIDLE_FLAG_NONE, stop_loop,
					  target_residency, exit_latency,
					  state->psscr_val,
					  state->psscr_mask);
		}

		/*
		 * All cpuidle states with CPUIDLE_FLAG_TIMER_STOP set must come
		 * within this config dependency check.
		 */
#ifdef CONFIG_TICK_ONESHOT
		else if (state->flags & OPAL_PM_SLEEP_ENABLED ||
			 state->flags & OPAL_PM_SLEEP_ENABLED_ER1) {
			/* Add FASTSLEEP state */
			add_powernv_state(nr_idle_states, "FastSleep",
					  CPUIDLE_FLAG_TIMER_STOP,
					  fastsleep_loop,
					  target_residency, exit_latency, 0, 0);
		} else if (has_stop_states && stops_timebase) {
			add_powernv_state(nr_idle_states, state->name,
					  CPUIDLE_FLAG_TIMER_STOP, stop_loop,
					  target_residency, exit_latency,
					  state->psscr_val,
					  state->psscr_mask);
		}
#endif
		else
			continue;
		nr_idle_states++;
	}
out:
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

	if (firmware_has_feature(FW_FEATURE_OPAL)) {
		cpuidle_state_table = powernv_states;
		/* Device tree can indicate more idle states */
		max_idle_state = powernv_add_idle_states();
		default_snooze_timeout = TICK_USEC * tb_ticks_per_usec;
		if (max_idle_state > 1)
			snooze_timeout_en = true;
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

	retval = cpuhp_setup_state_nocalls(CPUHP_AP_ONLINE_DYN,
					   "cpuidle/powernv:online",
					   powernv_cpuidle_cpu_online, NULL);
	WARN_ON(retval < 0);
	retval = cpuhp_setup_state_nocalls(CPUHP_CPUIDLE_DEAD,
					   "cpuidle/powernv:dead", NULL,
					   powernv_cpuidle_cpu_dead);
	WARN_ON(retval < 0);
	printk(KERN_DEBUG "powernv_idle_driver registered\n");
	return 0;
}

device_initcall(powernv_processor_idle_init);
