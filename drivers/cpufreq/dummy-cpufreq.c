// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2019 Google, Inc.
 */
#include <linux/cpufreq.h>
#include <linux/module.h>

static struct cpufreq_frequency_table freq_table[] = {
	{ .frequency = 1 },
	{ .frequency = 2 },
	{ .frequency = CPUFREQ_TABLE_END },
};

static int dummy_cpufreq_target_index(struct cpufreq_policy *policy,
				   unsigned int index)
{
	return 0;
}

static int dummy_cpufreq_driver_init(struct cpufreq_policy *policy)
{
	policy->freq_table = freq_table;
	return 0;
}

static int dummy_cpufreq_verify(struct cpufreq_policy *policy)
{
	return 0;
}

static struct cpufreq_driver dummy_cpufreq_driver = {
	.name = "dummy",
	.target_index = dummy_cpufreq_target_index,
	.init = dummy_cpufreq_driver_init,
	.verify = dummy_cpufreq_verify,
	.attr = cpufreq_generic_attr,
};

static int __init dummy_cpufreq_init(void)
{
	return cpufreq_register_driver(&dummy_cpufreq_driver);
}

static void __exit dummy_cpufreq_exit(void)
{
	cpufreq_unregister_driver(&dummy_cpufreq_driver);
}

module_init(dummy_cpufreq_init);
module_exit(dummy_cpufreq_exit);

MODULE_AUTHOR("Connor O'Brien <connoro@google.com>");
MODULE_DESCRIPTION("dummy cpufreq driver");
MODULE_LICENSE("GPL");
