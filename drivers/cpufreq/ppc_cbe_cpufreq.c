// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * cpufreq driver for the cell processor
 *
 * (C) Copyright IBM Deutschland Entwicklung GmbH 2005-2007
 *
 * Author: Christian Krafft <krafft@de.ibm.com>
 */

#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/of.h>

#include <asm/machdep.h>
#include <asm/cell-regs.h>

#include "ppc_cbe_cpufreq.h"

/* the CBE supports an 8 step frequency scaling */
static struct cpufreq_frequency_table cbe_freqs[] = {
	{0, 1,	0},
	{0, 2,	0},
	{0, 3,	0},
	{0, 4,	0},
	{0, 5,	0},
	{0, 6,	0},
	{0, 8,	0},
	{0, 10,	0},
	{0, 0,	CPUFREQ_TABLE_END},
};

/*
 * hardware specific functions
 */

static int set_pmode(unsigned int cpu, unsigned int slow_mode)
{
	int rc;

	if (cbe_cpufreq_has_pmi)
		rc = cbe_cpufreq_set_pmode_pmi(cpu, slow_mode);
	else
		rc = cbe_cpufreq_set_pmode(cpu, slow_mode);

	pr_debug("register contains slow mode %d\n", cbe_cpufreq_get_pmode(cpu));

	return rc;
}

/*
 * cpufreq functions
 */

static int cbe_cpufreq_cpu_init(struct cpufreq_policy *policy)
{
	struct cpufreq_frequency_table *pos;
	const u32 *max_freqp;
	u32 max_freq;
	int cur_pmode;
	struct device_node *cpu;

	cpu = of_get_cpu_node(policy->cpu, NULL);

	if (!cpu)
		return -ENODEV;

	pr_debug("init cpufreq on CPU %d\n", policy->cpu);

	/*
	 * Let's check we can actually get to the CELL regs
	 */
	if (!cbe_get_cpu_pmd_regs(policy->cpu) ||
	    !cbe_get_cpu_mic_tm_regs(policy->cpu)) {
		pr_info("invalid CBE regs pointers for cpufreq\n");
		of_node_put(cpu);
		return -EINVAL;
	}

	max_freqp = of_get_property(cpu, "clock-frequency", NULL);

	of_node_put(cpu);

	if (!max_freqp)
		return -EINVAL;

	/* we need the freq in kHz */
	max_freq = *max_freqp / 1000;

	pr_debug("max clock-frequency is at %u kHz\n", max_freq);
	pr_debug("initializing frequency table\n");

	/* initialize frequency table */
	cpufreq_for_each_entry(pos, cbe_freqs) {
		pos->frequency = max_freq / pos->driver_data;
		pr_debug("%d: %d\n", (int)(pos - cbe_freqs), pos->frequency);
	}

	/* if DEBUG is enabled set_pmode() measures the latency
	 * of a transition */
	policy->cpuinfo.transition_latency = 25000;

	cur_pmode = cbe_cpufreq_get_pmode(policy->cpu);
	pr_debug("current pmode is at %d\n",cur_pmode);

	policy->cur = cbe_freqs[cur_pmode].frequency;

#ifdef CONFIG_SMP
	cpumask_copy(policy->cpus, cpu_sibling_mask(policy->cpu));
#endif

	policy->freq_table = cbe_freqs;
	cbe_cpufreq_pmi_policy_init(policy);
	return 0;
}

static int cbe_cpufreq_cpu_exit(struct cpufreq_policy *policy)
{
	cbe_cpufreq_pmi_policy_exit(policy);
	return 0;
}

static int cbe_cpufreq_target(struct cpufreq_policy *policy,
			      unsigned int cbe_pmode_new)
{
	pr_debug("setting frequency for cpu %d to %d kHz, " \
		 "1/%d of max frequency\n",
		 policy->cpu,
		 cbe_freqs[cbe_pmode_new].frequency,
		 cbe_freqs[cbe_pmode_new].driver_data);

	return set_pmode(policy->cpu, cbe_pmode_new);
}

static struct cpufreq_driver cbe_cpufreq_driver = {
	.verify		= cpufreq_generic_frequency_table_verify,
	.target_index	= cbe_cpufreq_target,
	.init		= cbe_cpufreq_cpu_init,
	.exit		= cbe_cpufreq_cpu_exit,
	.name		= "cbe-cpufreq",
	.flags		= CPUFREQ_CONST_LOOPS,
};

/*
 * module init and destoy
 */

static int __init cbe_cpufreq_init(void)
{
	int ret;

	if (!machine_is(cell))
		return -ENODEV;

	cbe_cpufreq_pmi_init();

	ret = cpufreq_register_driver(&cbe_cpufreq_driver);
	if (ret)
		cbe_cpufreq_pmi_exit();

	return ret;
}

static void __exit cbe_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&cbe_cpufreq_driver);
	cbe_cpufreq_pmi_exit();
}

module_init(cbe_cpufreq_init);
module_exit(cbe_cpufreq_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Christian Krafft <krafft@de.ibm.com>");
