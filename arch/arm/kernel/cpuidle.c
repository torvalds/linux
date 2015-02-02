/*
 * Copyright 2012 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/cpuidle.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <asm/cpuidle.h>

extern struct of_cpuidle_method __cpuidle_method_of_table[];

static const struct of_cpuidle_method __cpuidle_method_of_table_sentinel
	__used __section(__cpuidle_method_of_table_end);

static struct cpuidle_ops cpuidle_ops[NR_CPUS];

int arm_cpuidle_simple_enter(struct cpuidle_device *dev,
		struct cpuidle_driver *drv, int index)
{
	cpu_do_idle();

	return index;
}

int arm_cpuidle_suspend(int index)
{
	int ret = -EOPNOTSUPP;
	int cpu = smp_processor_id();

	if (cpuidle_ops[cpu].suspend)
		ret = cpuidle_ops[cpu].suspend(cpu, index);

	return ret;
}

static struct cpuidle_ops *__init arm_cpuidle_get_ops(const char *method)
{
	struct of_cpuidle_method *m = __cpuidle_method_of_table;

	for (; m->method; m++)
		if (!strcmp(m->method, method))
			return m->ops;

	return NULL;
}

static int __init arm_cpuidle_read_ops(struct device_node *dn, int cpu)
{
	const char *enable_method;
	struct cpuidle_ops *ops;

	enable_method = of_get_property(dn, "enable-method", NULL);
	if (!enable_method)
		return -ENOENT;

	ops = arm_cpuidle_get_ops(enable_method);
	if (!ops) {
		pr_warn("%s: unsupported enable-method property: %s\n",
			dn->full_name, enable_method);
		return -EOPNOTSUPP;
	}

	cpuidle_ops[cpu] = *ops; /* structure copy */

	pr_notice("cpuidle: enable-method property '%s'"
		  " found operations\n", enable_method);

	return 0;
}

int __init arm_cpuidle_init(int cpu)
{
	struct device_node *cpu_node = of_cpu_device_node_get(cpu);
	int ret;

	if (!cpu_node)
		return -ENODEV;

	ret = arm_cpuidle_read_ops(cpu_node, cpu);
	if (!ret && cpuidle_ops[cpu].init)
		ret = cpuidle_ops[cpu].init(cpu_node, cpu);

	of_node_put(cpu_node);

	return ret;
}
