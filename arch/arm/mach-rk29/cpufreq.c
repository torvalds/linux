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
#include <linux/regulator/consumer.h>

static struct cpufreq_frequency_table freq_table[] = {
//	{ .index =  950000, .frequency =  204000 },
//	{ .index = 1050000, .frequency =  300000 },
	{ .index = 1050000, .frequency =  408000 },
//	{ .index = 1125000, .frequency =  600000 },
	{ .index = 1200000, .frequency =  816000 },
//	{ .index = 1250000, .frequency = 1008000 },
//	{ .index = 1300000, .frequency = 1200000 },
	{ .frequency = CPUFREQ_TABLE_END },
};
static struct clk *arm_clk;
static struct regulator *vcore;

static int rk29_cpufreq_verify(struct cpufreq_policy *policy)
{
	return cpufreq_frequency_table_verify(policy, freq_table);
}

static int rk29_cpufreq_target(struct cpufreq_policy *policy, unsigned int target_freq, unsigned int relation)
{
	int index;
	struct cpufreq_freqs freqs;
	const struct cpufreq_frequency_table *freq;
	int err = 0;

	if (policy->cpu != 0)
		return -EINVAL;
	if (cpufreq_frequency_table_target(policy, freq_table, target_freq, relation, &index)) {
		pr_err("cpufreq: invalid target_freq: %d\n", target_freq);
		return -EINVAL;
	}
	freq = &freq_table[index];

	if (policy->cur == freq->frequency)
		return 0;

	freqs.old = policy->cur;
	freqs.new = freq->frequency;
	freqs.cpu = 0;
#ifdef CONFIG_CPU_FREQ_DEBUG
	printk(KERN_DEBUG "%s %d r %d (%d-%d) selected %d (%duV)\n", __func__, target_freq, relation, policy->min, policy->max, freq->frequency, freq->index);
#endif

#ifdef CONFIG_REGULATOR
	if (vcore && freqs.new > freqs.old) {
		err = regulator_set_voltage(vcore, freq->index, freq->index);
		if (err) {
			pr_err("cpufreq: fail to set vcore (%duV) for %dkHz: %d\n",
				freq->index, freqs.new, err);
			goto err_vol;
		}
	}
#endif

	cpufreq_notify_transition(&freqs, CPUFREQ_PRECHANGE);
	clk_set_rate(arm_clk, freqs.new * 1000);
	freqs.new = clk_get_rate(arm_clk) / 1000;
	cpufreq_notify_transition(&freqs, CPUFREQ_POSTCHANGE);

#ifdef CONFIG_REGULATOR
	if (vcore && freqs.new < freqs.old) {
		err = regulator_set_voltage(vcore, freq->index, freq->index);
		if (err) {
			pr_err("cpufreq: fail to set vcore (%duV) for %dkHz: %d\n",
				freq->index, freqs.new, err);
		}
	}
#endif

err_vol:
	return err;
}

extern void clk_init_cpufreq_table(struct cpufreq_frequency_table **table);

static int __init rk29_cpufreq_init(struct cpufreq_policy *policy)
{
	arm_clk = clk_get(NULL, "arm_pll");
	if (IS_ERR(arm_clk))
		return PTR_ERR(arm_clk);

	if (policy->cpu != 0)
		return -EINVAL;

#ifdef CONFIG_REGULATOR
	vcore = regulator_get(NULL, "vcore");
	if (IS_ERR(vcore)) {
		pr_err("cpufreq: fail to get regulator vcore: %ld\n", PTR_ERR(vcore));
		vcore = NULL;
	}
#endif

	BUG_ON(cpufreq_frequency_table_cpuinfo(policy, freq_table));
	cpufreq_frequency_table_get_attr(freq_table, policy->cpu);
	policy->cur = clk_get_rate(arm_clk) / 1000;

	policy->cpuinfo.transition_latency = 400 * NSEC_PER_USEC; // make default sampling_rate to 40000

	return 0;
}

static int rk29_cpufreq_exit(struct cpufreq_policy *policy)
{
	if (vcore)
		regulator_put(vcore);
	clk_put(arm_clk);
	return 0;
}

static struct freq_attr *rk29_cpufreq_attr[] = {
	&cpufreq_freq_attr_scaling_available_freqs,
	NULL,
};

static struct cpufreq_driver rk29_cpufreq_driver = {
	.flags		= CPUFREQ_STICKY,
	.init		= rk29_cpufreq_init,
	.exit		= rk29_cpufreq_exit,
	.verify		= rk29_cpufreq_verify,
	.target		= rk29_cpufreq_target,
	.name		= "rk29",
	.attr		= rk29_cpufreq_attr,
};

static int __init rk29_cpufreq_register(void)
{
	return cpufreq_register_driver(&rk29_cpufreq_driver);
}

device_initcall(rk29_cpufreq_register);

