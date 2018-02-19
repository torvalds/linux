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

static u64 snooze_timeout __read_mostly;
static bool snooze_timeout_en __read_mostly;

static int snooze_loop(struct cpuidle_device *dev,
			struct cpuidle_driver *drv,
			int index)
{
	u64 snooze_exit_time;

	set_thread_flag(TIF_POLLING_NRFLAG);

	local_irq_enable();

	snooze_exit_time = get_tb() + snooze_timeout;
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
	power9_idle_type(stop_psscr_table[index].val,
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
	stop_psscr_table[index].val = psscr_val;
	stop_psscr_table[index].mask = psscr_mask;
}

/*
 * Returns 0 if prop1_len == prop2_len. Else returns -1
 */
static inline int validate_dt_prop_sizes(const char *prop1, int prop1_len,
					 const char *prop2, int prop2_len)
{
	if (prop1_len == prop2_len)
		return 0;

	pr_warn("cpuidle-powernv: array sizes don't match for %s and %s\n",
		prop1, prop2);
	return -1;
}

extern u32 pnv_get_supported_cpuidle_states(void);
static int powernv_add_idle_states(void)
{
	struct device_node *power_mgt;
	int nr_idle_states = 1; /* Snooze */
	int dt_idle_states, count;
	u32 latency_ns[CPUIDLE_STATE_MAX];
	u32 residency_ns[CPUIDLE_STATE_MAX];
	u32 flags[CPUIDLE_STATE_MAX];
	u64 psscr_val[CPUIDLE_STATE_MAX];
	u64 psscr_mask[CPUIDLE_STATE_MAX];
	const char *names[CPUIDLE_STATE_MAX];
	u32 has_stop_states = 0;
	int i, rc;
	u32 supported_flags = pnv_get_supported_cpuidle_states();


	/* Currently we have snooze statically defined */

	power_mgt = of_find_node_by_path("/ibm,opal/power-mgt");
	if (!power_mgt) {
		pr_warn("opal: PowerMgmt Node not found\n");
		goto out;
	}

	/* Read values of any property to determine the num of idle states */
	dt_idle_states = of_property_count_u32_elems(power_mgt, "ibm,cpu-idle-state-flags");
	if (dt_idle_states < 0) {
		pr_warn("cpuidle-powernv: no idle states found in the DT\n");
		goto out;
	}

	count = of_property_count_u32_elems(power_mgt,
					    "ibm,cpu-idle-state-latencies-ns");

	if (validate_dt_prop_sizes("ibm,cpu-idle-state-flags", dt_idle_states,
				   "ibm,cpu-idle-state-latencies-ns",
				   count) != 0)
		goto out;

	count = of_property_count_strings(power_mgt,
					  "ibm,cpu-idle-state-names");
	if (validate_dt_prop_sizes("ibm,cpu-idle-state-flags", dt_idle_states,
				   "ibm,cpu-idle-state-names",
				   count) != 0)
		goto out;

	/*
	 * Since snooze is used as first idle state, max idle states allowed is
	 * CPUIDLE_STATE_MAX -1
	 */
	if (dt_idle_states > CPUIDLE_STATE_MAX - 1) {
		pr_warn("cpuidle-powernv: discovered idle states more than allowed");
		dt_idle_states = CPUIDLE_STATE_MAX - 1;
	}

	if (of_property_read_u32_array(power_mgt,
			"ibm,cpu-idle-state-flags", flags, dt_idle_states)) {
		pr_warn("cpuidle-powernv : missing ibm,cpu-idle-state-flags in DT\n");
		goto out;
	}

	if (of_property_read_u32_array(power_mgt,
		"ibm,cpu-idle-state-latencies-ns", latency_ns,
		dt_idle_states)) {
		pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-latencies-ns in DT\n");
		goto out;
	}
	if (of_property_read_string_array(power_mgt,
		"ibm,cpu-idle-state-names", names, dt_idle_states) < 0) {
		pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-names in DT\n");
		goto out;
	}

	/*
	 * If the idle states use stop instruction, probe for psscr values
	 * and psscr mask which are necessary to specify required stop level.
	 */
	has_stop_states = (flags[0] &
			   (OPAL_PM_STOP_INST_FAST | OPAL_PM_STOP_INST_DEEP));
	if (has_stop_states) {
		count = of_property_count_u64_elems(power_mgt,
						    "ibm,cpu-idle-state-psscr");
		if (validate_dt_prop_sizes("ibm,cpu-idle-state-flags",
					   dt_idle_states,
					   "ibm,cpu-idle-state-psscr",
					   count) != 0)
			goto out;

		count = of_property_count_u64_elems(power_mgt,
						    "ibm,cpu-idle-state-psscr-mask");
		if (validate_dt_prop_sizes("ibm,cpu-idle-state-flags",
					   dt_idle_states,
					   "ibm,cpu-idle-state-psscr-mask",
					   count) != 0)
			goto out;

		if (of_property_read_u64_array(power_mgt,
		    "ibm,cpu-idle-state-psscr", psscr_val, dt_idle_states)) {
			pr_warn("cpuidle-powernv: missing ibm,cpu-idle-state-psscr in DT\n");
			goto out;
		}

		if (of_property_read_u64_array(power_mgt,
					       "ibm,cpu-idle-state-psscr-mask",
						psscr_mask, dt_idle_states)) {
			pr_warn("cpuidle-powernv:Missing ibm,cpu-idle-state-psscr-mask in DT\n");
			goto out;
		}
	}

	count = of_property_count_u32_elems(power_mgt,
					    "ibm,cpu-idle-state-residency-ns");

	if (count < 0) {
		rc = count;
	} else if (validate_dt_prop_sizes("ibm,cpu-idle-state-flags",
					  dt_idle_states,
					  "ibm,cpu-idle-state-residency-ns",
					  count) != 0) {
		goto out;
	} else {
		rc = of_property_read_u32_array(power_mgt,
						"ibm,cpu-idle-state-residency-ns",
						residency_ns, dt_idle_states);
	}

	for (i = 0; i < dt_idle_states; i++) {
		unsigned int exit_latency, target_residency;
		bool stops_timebase = false;

		/*
		 * Skip the platform idle state whose flag isn't in
		 * the supported_cpuidle_states flag mask.
		 */
		if ((flags[i] & supported_flags) != flags[i])
			continue;
		/*
		 * If an idle state has exit latency beyond
		 * POWERNV_THRESHOLD_LATENCY_NS then don't use it
		 * in cpu-idle.
		 */
		if (latency_ns[i] > POWERNV_THRESHOLD_LATENCY_NS)
			continue;
		/*
		 * Firmware passes residency and latency values in ns.
		 * cpuidle expects it in us.
		 */
		exit_latency = DIV_ROUND_UP(latency_ns[i], 1000);
		if (!rc)
			target_residency = DIV_ROUND_UP(residency_ns[i], 1000);
		else
			target_residency = 0;

		if (has_stop_states) {
			int err = validate_psscr_val_mask(&psscr_val[i],
							  &psscr_mask[i],
							  flags[i]);
			if (err) {
				report_invalid_psscr_val(psscr_val[i], err);
				continue;
			}
		}

		if (flags[i] & OPAL_PM_TIMEBASE_STOP)
			stops_timebase = true;

		/*
		 * For nap and fastsleep, use default target_residency
		 * values if f/w does not expose it.
		 */
		if (flags[i] & OPAL_PM_NAP_ENABLED) {
			if (!rc)
				target_residency = 100;
			/* Add NAP state */
			add_powernv_state(nr_idle_states, "Nap",
					  CPUIDLE_FLAG_NONE, nap_loop,
					  target_residency, exit_latency, 0, 0);
		} else if (has_stop_states && !stops_timebase) {
			add_powernv_state(nr_idle_states, names[i],
					  CPUIDLE_FLAG_NONE, stop_loop,
					  target_residency, exit_latency,
					  psscr_val[i], psscr_mask[i]);
		}

		/*
		 * All cpuidle states with CPUIDLE_FLAG_TIMER_STOP set must come
		 * within this config dependency check.
		 */
#ifdef CONFIG_TICK_ONESHOT
		else if (flags[i] & OPAL_PM_SLEEP_ENABLED ||
			 flags[i] & OPAL_PM_SLEEP_ENABLED_ER1) {
			if (!rc)
				target_residency = 300000;
			/* Add FASTSLEEP state */
			add_powernv_state(nr_idle_states, "FastSleep",
					  CPUIDLE_FLAG_TIMER_STOP,
					  fastsleep_loop,
					  target_residency, exit_latency, 0, 0);
		} else if (has_stop_states && stops_timebase) {
			add_powernv_state(nr_idle_states, names[i],
					  CPUIDLE_FLAG_TIMER_STOP, stop_loop,
					  target_residency, exit_latency,
					  psscr_val[i], psscr_mask[i]);
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
		if (max_idle_state > 1) {
			snooze_timeout_en = true;
			snooze_timeout = powernv_states[1].target_residency *
					 tb_ticks_per_usec;
		}
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
