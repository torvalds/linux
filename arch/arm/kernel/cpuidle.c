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

/**
 * arm_cpuidle_simple_enter() - a wrapper to cpu_do_idle()
 * @dev: not used
 * @drv: not used
 * @index: not used
 *
 * A trivial wrapper to allow the cpu_do_idle function to be assigned as a
 * cpuidle callback by matching the function signature.
 *
 * Returns the index passed as parameter
 */
int arm_cpuidle_simple_enter(struct cpuidle_device *dev,
		struct cpuidle_driver *drv, int index)
{
	cpu_do_idle();

	return index;
}

/**
 * arm_cpuidle_suspend() - function to enter low power idle states
 * @index: an integer used as an identifier for the low level PM callbacks
 *
 * This function calls the underlying arch specific low level PM code as
 * registered at the init time.
 *
 * Returns -EOPNOTSUPP if no suspend callback is defined, the result of the
 * callback otherwise.
 */
int arm_cpuidle_suspend(int index)
{
	int ret = -EOPNOTSUPP;
	int cpu = smp_processor_id();

	if (cpuidle_ops[cpu].suspend)
		ret = cpuidle_ops[cpu].suspend(cpu, index);

	return ret;
}

/**
 * arm_cpuidle_get_ops() - find a registered cpuidle_ops by name
 * @method: the method name
 *
 * Search in the __cpuidle_method_of_table array the cpuidle ops matching the
 * method name.
 *
 * Returns a struct cpuidle_ops pointer, NULL if not found.
 */
static struct cpuidle_ops *__init arm_cpuidle_get_ops(const char *method)
{
	struct of_cpuidle_method *m = __cpuidle_method_of_table;

	for (; m->method; m++)
		if (!strcmp(m->method, method))
			return m->ops;

	return NULL;
}

/**
 * arm_cpuidle_read_ops() - Initialize the cpuidle ops with the device tree
 * @dn: a pointer to a struct device node corresponding to a cpu node
 * @cpu: the cpu identifier
 *
 * Get the method name defined in the 'enable-method' property, retrieve the
 * associated cpuidle_ops and do a struct copy. This copy is needed because all
 * cpuidle_ops are tagged __initdata and will be unloaded after the init
 * process.
 *
 * Return 0 on sucess, -ENOENT if no 'enable-method' is defined, -EOPNOTSUPP if
 * no cpuidle_ops is registered for the 'enable-method'.
 */
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

/**
 * arm_cpuidle_init() - Initialize cpuidle_ops for a specific cpu
 * @cpu: the cpu to be initialized
 *
 * Initialize the cpuidle ops with the device for the cpu and then call
 * the cpu's idle initialization callback. This may fail if the underlying HW
 * is not operational.
 *
 * Returns:
 *  0 on success,
 *  -ENODEV if it fails to find the cpu node in the device tree,
 *  -EOPNOTSUPP if it does not find a registered cpuidle_ops for this cpu,
 *  -ENOENT if it fails to find an 'enable-method' property,
 *  -ENXIO if the HW reports a failure or a misconfiguration,
 *  -ENOMEM if the HW report an memory allocation failure 
 */
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
