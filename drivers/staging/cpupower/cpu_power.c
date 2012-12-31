/*
 * arch/arm/kernel/topology.c
 *
 * Copyright (C) 2011 Linaro Limited.
 * Written by: Vincent Guittot
 *
 * based on arch/sh/kernel/topology.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/notifier.h>
#include <linux/cpufreq.h>
#include <linux/power/cpupower.h>

static struct cputopo_power **table_config = NULL;

struct cputopo_scale {
	int id;
	int freq;
	struct cputopo_power *power;
};

/*
 * The table will be mostly used by one cpu which will update the
 * configuration for all cpu on a cpufreq notification
 * or a sched_mc level change
 */
static struct cputopo_scale cpu_power[NR_CPUS];

static void set_cpufreq_scale(unsigned int cpuid, unsigned int freq)
{
	unsigned int idx;

	cpu_power[cpuid].freq = freq;

	idx = freq / cpu_power[cpuid].power->step;
	if (idx >= cpu_power[cpuid].power->max)
		idx = cpu_power[cpuid].power->max - 1;

	set_power_scale(cpuid, cpu_power[cpuid].power->table[idx]);
	smp_wmb();
}

static void update_power_scale(unsigned int cpu, unsigned int idx)
{
	cpu_power[cpu].id = idx;
	cpu_power[cpu].power = table_config[idx];

	set_cpufreq_scale(cpu, cpu_power[cpu].freq);
}

static int topo_cpufreq_transition(struct notifier_block *nb,
	unsigned long state, void *data)
{
	struct cpufreq_freqs *freqs = data;

	if (state == CPUFREQ_POSTCHANGE || state == CPUFREQ_RESUMECHANGE)
		set_cpufreq_scale(freqs->cpu, freqs->new);

	return NOTIFY_OK;
}

static struct notifier_block topo_cpufreq_nb = {
	.notifier_call = topo_cpufreq_transition,
};

static int topo_cpufreq_init(struct platform_device *pdev)
{
	unsigned int cpu;

	/* get cpu_power table */
	table_config = dev_get_platdata(&pdev->dev);

	/* init core mask */
	for_each_possible_cpu(cpu) {
		cpu_power[cpu].freq = 0;
		update_power_scale(cpu, ARM_DEFAULT_SCALE);
	}

	/* register cpufreq notification */
	return cpufreq_register_notifier(&topo_cpufreq_nb,
			CPUFREQ_TRANSITION_NOTIFIER);
}

static int topo_cpufreq_exit(struct platform_device *pdev)
{
	unsigned int cpu;

	/* unregister cpufreq notification */
	cpufreq_unregister_notifier(&topo_cpufreq_nb,
			CPUFREQ_TRANSITION_NOTIFIER);

	/* cleay core mask */
	for_each_possible_cpu(cpu) {
		cpu_power[cpu].freq = 0;
		cpu_power[cpu].power = NULL;
	}

	/* clear cpu_power table */
	table_config = NULL;

	return 0;
}

static int topo_policy_transition(struct notifier_block *nb,
	unsigned long state, void *data)
{
	int cpu, idx, level = (int)data;

	if (level == POWERSAVINGS_BALANCE_NONE)
		idx = ARM_DEFAULT_SCALE;
	else
		idx = ARM_POWER_SCALE;

	for_each_possible_cpu(cpu)
		update_power_scale(cpu, cpu ? ARM_DEFAULT_SCALE : idx );

	return NOTIFY_OK;
}

static struct notifier_block topo_policy_nb = {
	.notifier_call = topo_policy_transition,
};

static int __devinit cpupower_probe(struct platform_device *pdev)
{
	topo_cpufreq_init(pdev);

	/* register cpufreq notifer */
	topology_register_notifier(&topo_policy_nb);

	return 0;
}

static int __devexit cpupower_remove(struct platform_device *pdev)
{
	/* unregister cpufreq notifer */
	topology_unregister_notifier(&topo_policy_nb);

	topo_cpufreq_exit(pdev);

	return 0;
}


static struct platform_driver cpupower_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "cpupower",
	},
	.probe = cpupower_probe,
	.remove = __devexit_p(cpupower_remove),
};

static int __init cpupower_init(void)
{
	return platform_driver_register(&cpupower_driver);
}

static void __exit cpupower_exit(void)
{
	platform_driver_unregister(&cpupower_driver);
}

core_initcall(cpupower_init);
module_exit(cpupower_exit);

MODULE_AUTHOR("vincent Guittot");
MODULE_DESCRIPTION("update cpu_power according to current cpu load driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:" DRIVER_NAME);
