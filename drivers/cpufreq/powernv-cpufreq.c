/*
 * POWERNV cpufreq driver for the IBM POWER processors
 *
 * (C) Copyright IBM 2014
 *
 * Author: Vaidyanathan Srinivasan <svaidy at linux.vnet.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#define pr_fmt(fmt)	"powernv-cpufreq: " fmt

#include <linux/kernel.h>
#include <linux/sysfs.h>
#include <linux/cpumask.h>
#include <linux/module.h>
#include <linux/cpufreq.h>
#include <linux/smp.h>
#include <linux/of.h>
#include <linux/reboot.h>

#include <asm/cputhreads.h>
#include <asm/firmware.h>
#include <asm/reg.h>
#include <asm/smp.h> /* Required for cpu_sibling_mask() in UP configs */

#define POWERNV_MAX_PSTATES	256
#define PMSR_PSAFE_ENABLE	(1UL << 30)
#define PMSR_SPR_EM_DISABLE	(1UL << 31)
#define PMSR_MAX(x)		((x >> 32) & 0xFF)
#define PMSR_LP(x)		((x >> 48) & 0xFF)

static struct cpufreq_frequency_table powernv_freqs[POWERNV_MAX_PSTATES+1];
static bool rebooting, throttled;

/*
 * Note: The set of pstates consists of contiguous integers, the
 * smallest of which is indicated by powernv_pstate_info.min, the
 * largest of which is indicated by powernv_pstate_info.max.
 *
 * The nominal pstate is the highest non-turbo pstate in this
 * platform. This is indicated by powernv_pstate_info.nominal.
 */
static struct powernv_pstate_info {
	int min;
	int max;
	int nominal;
	int nr_pstates;
} powernv_pstate_info;

/*
 * Initialize the freq table based on data obtained
 * from the firmware passed via device-tree
 */
static int init_powernv_pstates(void)
{
	struct device_node *power_mgt;
	int i, pstate_min, pstate_max, pstate_nominal, nr_pstates = 0;
	const __be32 *pstate_ids, *pstate_freqs;
	u32 len_ids, len_freqs;

	power_mgt = of_find_node_by_path("/ibm,opal/power-mgt");
	if (!power_mgt) {
		pr_warn("power-mgt node not found\n");
		return -ENODEV;
	}

	if (of_property_read_u32(power_mgt, "ibm,pstate-min", &pstate_min)) {
		pr_warn("ibm,pstate-min node not found\n");
		return -ENODEV;
	}

	if (of_property_read_u32(power_mgt, "ibm,pstate-max", &pstate_max)) {
		pr_warn("ibm,pstate-max node not found\n");
		return -ENODEV;
	}

	if (of_property_read_u32(power_mgt, "ibm,pstate-nominal",
				 &pstate_nominal)) {
		pr_warn("ibm,pstate-nominal not found\n");
		return -ENODEV;
	}
	pr_info("cpufreq pstate min %d nominal %d max %d\n", pstate_min,
		pstate_nominal, pstate_max);

	pstate_ids = of_get_property(power_mgt, "ibm,pstate-ids", &len_ids);
	if (!pstate_ids) {
		pr_warn("ibm,pstate-ids not found\n");
		return -ENODEV;
	}

	pstate_freqs = of_get_property(power_mgt, "ibm,pstate-frequencies-mhz",
				      &len_freqs);
	if (!pstate_freqs) {
		pr_warn("ibm,pstate-frequencies-mhz not found\n");
		return -ENODEV;
	}

	if (len_ids != len_freqs) {
		pr_warn("Entries in ibm,pstate-ids and "
			"ibm,pstate-frequencies-mhz does not match\n");
	}

	nr_pstates = min(len_ids, len_freqs) / sizeof(u32);
	if (!nr_pstates) {
		pr_warn("No PStates found\n");
		return -ENODEV;
	}

	pr_debug("NR PStates %d\n", nr_pstates);
	for (i = 0; i < nr_pstates; i++) {
		u32 id = be32_to_cpu(pstate_ids[i]);
		u32 freq = be32_to_cpu(pstate_freqs[i]);

		pr_debug("PState id %d freq %d MHz\n", id, freq);
		powernv_freqs[i].frequency = freq * 1000; /* kHz */
		powernv_freqs[i].driver_data = id;
	}
	/* End of list marker entry */
	powernv_freqs[i].frequency = CPUFREQ_TABLE_END;

	powernv_pstate_info.min = pstate_min;
	powernv_pstate_info.max = pstate_max;
	powernv_pstate_info.nominal = pstate_nominal;
	powernv_pstate_info.nr_pstates = nr_pstates;

	return 0;
}

/* Returns the CPU frequency corresponding to the pstate_id. */
static unsigned int pstate_id_to_freq(int pstate_id)
{
	int i;

	i = powernv_pstate_info.max - pstate_id;
	if (i >= powernv_pstate_info.nr_pstates || i < 0) {
		pr_warn("PState id %d outside of PState table, "
			"reporting nominal id %d instead\n",
			pstate_id, powernv_pstate_info.nominal);
		i = powernv_pstate_info.max - powernv_pstate_info.nominal;
	}

	return powernv_freqs[i].frequency;
}

/*
 * cpuinfo_nominal_freq_show - Show the nominal CPU frequency as indicated by
 * the firmware
 */
static ssize_t cpuinfo_nominal_freq_show(struct cpufreq_policy *policy,
					char *buf)
{
	return sprintf(buf, "%u\n",
		pstate_id_to_freq(powernv_pstate_info.nominal));
}

struct freq_attr cpufreq_freq_attr_cpuinfo_nominal_freq =
	__ATTR_RO(cpuinfo_nominal_freq);

static struct freq_attr *powernv_cpu_freq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	&cpufreq_freq_attr_cpuinfo_nominal_freq,
	NULL,
};

/* Helper routines */

/* Access helpers to power mgt SPR */

static inline unsigned long get_pmspr(unsigned long sprn)
{
	switch (sprn) {
	case SPRN_PMCR:
		return mfspr(SPRN_PMCR);

	case SPRN_PMICR:
		return mfspr(SPRN_PMICR);

	case SPRN_PMSR:
		return mfspr(SPRN_PMSR);
	}
	BUG();
}

static inline void set_pmspr(unsigned long sprn, unsigned long val)
{
	switch (sprn) {
	case SPRN_PMCR:
		mtspr(SPRN_PMCR, val);
		return;

	case SPRN_PMICR:
		mtspr(SPRN_PMICR, val);
		return;
	}
	BUG();
}

/*
 * Use objects of this type to query/update
 * pstates on a remote CPU via smp_call_function.
 */
struct powernv_smp_call_data {
	unsigned int freq;
	int pstate_id;
};

/*
 * powernv_read_cpu_freq: Reads the current frequency on this CPU.
 *
 * Called via smp_call_function.
 *
 * Note: The caller of the smp_call_function should pass an argument of
 * the type 'struct powernv_smp_call_data *' along with this function.
 *
 * The current frequency on this CPU will be returned via
 * ((struct powernv_smp_call_data *)arg)->freq;
 */
static void powernv_read_cpu_freq(void *arg)
{
	unsigned long pmspr_val;
	s8 local_pstate_id;
	struct powernv_smp_call_data *freq_data = arg;

	pmspr_val = get_pmspr(SPRN_PMSR);

	/*
	 * The local pstate id corresponds bits 48..55 in the PMSR.
	 * Note: Watch out for the sign!
	 */
	local_pstate_id = (pmspr_val >> 48) & 0xFF;
	freq_data->pstate_id = local_pstate_id;
	freq_data->freq = pstate_id_to_freq(freq_data->pstate_id);

	pr_debug("cpu %d pmsr %016lX pstate_id %d frequency %d kHz\n",
		raw_smp_processor_id(), pmspr_val, freq_data->pstate_id,
		freq_data->freq);
}

/*
 * powernv_cpufreq_get: Returns the CPU frequency as reported by the
 * firmware for CPU 'cpu'. This value is reported through the sysfs
 * file cpuinfo_cur_freq.
 */
static unsigned int powernv_cpufreq_get(unsigned int cpu)
{
	struct powernv_smp_call_data freq_data;

	smp_call_function_any(cpu_sibling_mask(cpu), powernv_read_cpu_freq,
			&freq_data, 1);

	return freq_data.freq;
}

/*
 * set_pstate: Sets the pstate on this CPU.
 *
 * This is called via an smp_call_function.
 *
 * The caller must ensure that freq_data is of the type
 * (struct powernv_smp_call_data *) and the pstate_id which needs to be set
 * on this CPU should be present in freq_data->pstate_id.
 */
static void set_pstate(void *freq_data)
{
	unsigned long val;
	unsigned long pstate_ul =
		((struct powernv_smp_call_data *) freq_data)->pstate_id;

	val = get_pmspr(SPRN_PMCR);
	val = val & 0x0000FFFFFFFFFFFFULL;

	pstate_ul = pstate_ul & 0xFF;

	/* Set both global(bits 56..63) and local(bits 48..55) PStates */
	val = val | (pstate_ul << 56) | (pstate_ul << 48);

	pr_debug("Setting cpu %d pmcr to %016lX\n",
			raw_smp_processor_id(), val);
	set_pmspr(SPRN_PMCR, val);
}

/*
 * get_nominal_index: Returns the index corresponding to the nominal
 * pstate in the cpufreq table
 */
static inline unsigned int get_nominal_index(void)
{
	return powernv_pstate_info.max - powernv_pstate_info.nominal;
}

static void powernv_cpufreq_throttle_check(unsigned int cpu)
{
	unsigned long pmsr;
	int pmsr_pmax, pmsr_lp;

	pmsr = get_pmspr(SPRN_PMSR);

	/* Check for Pmax Capping */
	pmsr_pmax = (s8)PMSR_MAX(pmsr);
	if (pmsr_pmax != powernv_pstate_info.max) {
		throttled = true;
		pr_info("CPU %d Pmax is reduced to %d\n", cpu, pmsr_pmax);
		pr_info("Max allowed Pstate is capped\n");
	}

	/*
	 * Check for Psafe by reading LocalPstate
	 * or check if Psafe_mode_active is set in PMSR.
	 */
	pmsr_lp = (s8)PMSR_LP(pmsr);
	if ((pmsr_lp < powernv_pstate_info.min) ||
				(pmsr & PMSR_PSAFE_ENABLE)) {
		throttled = true;
		pr_info("Pstate set to safe frequency\n");
	}

	/* Check if SPR_EM_DISABLE is set in PMSR */
	if (pmsr & PMSR_SPR_EM_DISABLE) {
		throttled = true;
		pr_info("Frequency Control disabled from OS\n");
	}

	if (throttled) {
		pr_info("PMSR = %16lx\n", pmsr);
		pr_crit("CPU Frequency could be throttled\n");
	}
}

/*
 * powernv_cpufreq_target_index: Sets the frequency corresponding to
 * the cpufreq table entry indexed by new_index on the cpus in the
 * mask policy->cpus
 */
static int powernv_cpufreq_target_index(struct cpufreq_policy *policy,
					unsigned int new_index)
{
	struct powernv_smp_call_data freq_data;

	if (unlikely(rebooting) && new_index != get_nominal_index())
		return 0;

	if (!throttled)
		powernv_cpufreq_throttle_check(smp_processor_id());

	freq_data.pstate_id = powernv_freqs[new_index].driver_data;

	/*
	 * Use smp_call_function to send IPI and execute the
	 * mtspr on target CPU.  We could do that without IPI
	 * if current CPU is within policy->cpus (core)
	 */
	smp_call_function_any(policy->cpus, set_pstate, &freq_data, 1);

	return 0;
}

static int powernv_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	int base, i;

	base = cpu_first_thread_sibling(policy->cpu);

	for (i = 0; i < threads_per_core; i++)
		cpumask_set_cpu(base + i, policy->cpus);

	return cpufreq_table_validate_and_show(policy, powernv_freqs);
}

static int powernv_cpufreq_reboot_notifier(struct notifier_block *nb,
				unsigned long action, void *unused)
{
	int cpu;
	struct cpufreq_policy cpu_policy;

	rebooting = true;
	for_each_online_cpu(cpu) {
		cpufreq_get_policy(&cpu_policy, cpu);
		powernv_cpufreq_target_index(&cpu_policy, get_nominal_index());
	}

	return NOTIFY_DONE;
}

static struct notifier_block powernv_cpufreq_reboot_nb = {
	.notifier_call = powernv_cpufreq_reboot_notifier,
};

static void powernv_cpufreq_stop_cpu(struct cpufreq_policy *policy)
{
	struct powernv_smp_call_data freq_data;

	freq_data.pstate_id = powernv_pstate_info.min;
	smp_call_function_single(policy->cpu, set_pstate, &freq_data, 1);
}

static struct cpufreq_driver powernv_cpufreq_driver = {
	.name		= "powernv-cpufreq",
	.flags		= CPUFREQ_CONST_LOOPS,
	.init		= powernv_cpufreq_cpu_init,
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= powernv_cpufreq_target_index,
	.get		= powernv_cpufreq_get,
	.stop_cpu	= powernv_cpufreq_stop_cpu,
	.attr		= powernv_cpu_freq_attr,
};

static int __init powernv_cpufreq_init(void)
{
	int rc = 0;

	/* Don't probe on pseries (guest) platforms */
	if (!firmware_has_feature(FW_FEATURE_OPALv3))
		return -ENODEV;

	/* Discover pstates from device tree and init */
	rc = init_powernv_pstates();
	if (rc) {
		pr_info("powernv-cpufreq disabled. System does not support PState control\n");
		return rc;
	}

	register_reboot_notifier(&powernv_cpufreq_reboot_nb);
	return cpufreq_register_driver(&powernv_cpufreq_driver);
}
module_init(powernv_cpufreq_init);

static void __exit powernv_cpufreq_exit(void)
{
	unregister_reboot_notifier(&powernv_cpufreq_reboot_nb);
	cpufreq_unregister_driver(&powernv_cpufreq_driver);
}
module_exit(powernv_cpufreq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Vaidyanathan Srinivasan <svaidy at linux.vnet.ibm.com>");
