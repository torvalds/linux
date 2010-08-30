/* arch/arm/mach-rk2818/cpufreq.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/err.h>
#include <linux/init.h>

static struct cpufreq_frequency_table freq_table[] = {
	{ .frequency = 192000 },
	{ .frequency = 576000 },
	{ .frequency = CPUFREQ_TABLE_END },
};
static struct clk *arm_clk;

static int rk2818_cpufreq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, freq_table);
}

static int rk2818_cpufreq_target(struct cpufreq_policy *policy, unsigned int target_freq, unsigned int relation)
{
	int index;
	struct cpufreq_freqs freqs;

	if (policy->cpu != 0)
		return -EINVAL;
	if (cpufreq_frequency_table_target(policy, freq_table, target_freq, relation, &index)) {
		pr_err("cpufreq: invalid target_freq: %d\n", target_freq);
		return -EINVAL;
	}

	if (policy->cur == freq_table[index].frequency)
		return 0;

#ifdef CONFIG_CPU_FREQ_DEBUG
	printk(KERN_DEBUG "%s %d r %d (%d-%d) selected %d\n", __func__, target_freq, relation, policy->min, policy->max, freq_table[index].frequency);
#endif
	freqs.old = policy->cur;
	freqs.new = freq_table[index].frequency;
	freqs.cpu = 0;
	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	clk_set_rate(arm_clk, freqs.new * 1000);
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

	return 0;
}

extern void clk_init_cpufreq_table(struct cpufreq_frequency_table **table);

static int __init rk2818_cpufreq_init(struct cpufreq_policy *policy)
{
	arm_clk = clk_get(NULL, "arm");
	if (IS_ERR(arm_clk))
		return PTR_ERR(arm_clk);

	if (policy->cpu != 0)
		return -EINVAL;

	BUG_ON(cpufreq_frequency_table_cpuinfo(policy, freq_table));
	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);
	policy->cur = clk_get_rate(arm_clk) / 1000;
	policy->cpuinfo.transition_latency = 300 * NSEC_PER_USEC; // FIXME: 0.3ms?

	return 0;
}

static int rk2818_cpufreq_exit(struct cpufreq_policy *policy)
{
	clk_put(arm_clk);
	return 0;
}

static struct freq_attr *rk2818_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver rk2818_cpufreq_driver = {
	.flags		= CPUFREQ_STICKY,
	.init		= rk2818_cpufreq_init,
	.exit		= rk2818_cpufreq_exit,
	.verify		= rk2818_cpufreq_verify,
	.target		= rk2818_cpufreq_target,
	.name		= "rk2818",
	.attr		= rk2818_cpufreq_attr,
};

static int __init rk2818_cpufreq_register(void)
{
	return cpufreq_register_driver(&rk2818_cpufreq_driver);
}

device_initcall(rk2818_cpufreq_register);

