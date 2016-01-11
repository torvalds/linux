/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (C) 2013 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 */

#define pr_fmt(fmt) "psci: " fmt

#include <linux/init.h>
#include <linux/of.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include <linux/psci.h>
#include <linux/slab.h>

#include <uapi/linux/psci.h>

#include <asm/compiler.h>
#include <asm/cpu_ops.h>
#include <asm/errno.h>
#include <asm/smp_plat.h>
#include <asm/suspend.h>

static DEFINE_PER_CPU_READ_MOSTLY(u32 *, psci_power_state);

static int __maybe_unused cpu_psci_cpu_init_idle(unsigned int cpu)
{
	int i, ret, count = 0;
	u32 *psci_states;
	struct device_node *state_node, *cpu_node;

	cpu_node = of_get_cpu_node(cpu, NULL);
	if (!cpu_node)
		return -ENODEV;

	/*
	 * If the PSCI cpu_suspend function hook has not been initialized
	 * idle states must not be enabled, so bail out
	 */
	if (!psci_ops.cpu_suspend)
		return -EOPNOTSUPP;

	/* Count idle states */
	while ((state_node = of_parse_phandle(cpu_node, "cpu-idle-states",
					      count))) {
		count++;
		of_node_put(state_node);
	}

	if (!count)
		return -ENODEV;

	psci_states = kcalloc(count, sizeof(*psci_states), GFP_KERNEL);
	if (!psci_states)
		return -ENOMEM;

	for (i = 0; i < count; i++) {
		u32 state;

		state_node = of_parse_phandle(cpu_node, "cpu-idle-states", i);

		ret = of_property_read_u32(state_node,
					   "arm,psci-suspend-param",
					   &state);
		if (ret) {
			pr_warn(" * %s missing arm,psci-suspend-param property\n",
				state_node->full_name);
			of_node_put(state_node);
			goto free_mem;
		}

		of_node_put(state_node);
		pr_debug("psci-power-state %#x index %d\n", state, i);
		if (!psci_power_state_is_valid(state)) {
			pr_warn("Invalid PSCI power state %#x\n", state);
			ret = -EINVAL;
			goto free_mem;
		}
		psci_states[i] = state;
	}
	/* Idle states parsed correctly, initialize per-cpu pointer */
	per_cpu(psci_power_state, cpu) = psci_states;
	return 0;

free_mem:
	kfree(psci_states);
	return ret;
}

static int __init cpu_psci_cpu_init(unsigned int cpu)
{
	return 0;
}

static int __init cpu_psci_cpu_prepare(unsigned int cpu)
{
	if (!psci_ops.cpu_on) {
		pr_err("no cpu_on method, not booting CPU%d\n", cpu);
		return -ENODEV;
	}

	return 0;
}

static int cpu_psci_cpu_boot(unsigned int cpu)
{
	int err = psci_ops.cpu_on(cpu_logical_map(cpu), __pa(secondary_entry));
	if (err)
		pr_err("failed to boot CPU%d (%d)\n", cpu, err);

	return err;
}

#ifdef CONFIG_HOTPLUG_CPU
static int cpu_psci_cpu_disable(unsigned int cpu)
{
	/* Fail early if we don't have CPU_OFF support */
	if (!psci_ops.cpu_off)
		return -EOPNOTSUPP;

	/* Trusted OS will deny CPU_OFF */
	if (psci_tos_resident_on(cpu))
		return -EPERM;

	return 0;
}

static void cpu_psci_cpu_die(unsigned int cpu)
{
	int ret;
	/*
	 * There are no known implementations of PSCI actually using the
	 * power state field, pass a sensible default for now.
	 */
	u32 state = PSCI_POWER_STATE_TYPE_POWER_DOWN <<
		    PSCI_0_2_POWER_STATE_TYPE_SHIFT;

	ret = psci_ops.cpu_off(state);

	pr_crit("unable to power off CPU%u (%d)\n", cpu, ret);
}

static int cpu_psci_cpu_kill(unsigned int cpu)
{
	int err, i;

	if (!psci_ops.affinity_info)
		return 0;
	/*
	 * cpu_kill could race with cpu_die and we can
	 * potentially end up declaring this cpu undead
	 * while it is dying. So, try again a few times.
	 */

	for (i = 0; i < 10; i++) {
		err = psci_ops.affinity_info(cpu_logical_map(cpu), 0);
		if (err == PSCI_0_2_AFFINITY_LEVEL_OFF) {
			pr_info("CPU%d killed.\n", cpu);
			return 0;
		}

		msleep(10);
		pr_info("Retrying again to check for CPU kill\n");
	}

	pr_warn("CPU%d may not have shut down cleanly (AFFINITY_INFO reports %d)\n",
			cpu, err);
	return -ETIMEDOUT;
}
#endif

static int psci_suspend_finisher(unsigned long index)
{
	u32 *state = __this_cpu_read(psci_power_state);

	return psci_ops.cpu_suspend(state[index - 1],
				    virt_to_phys(cpu_resume));
}

static int __maybe_unused cpu_psci_cpu_suspend(unsigned long index)
{
	int ret;
	u32 *state = __this_cpu_read(psci_power_state);
	/*
	 * idle state index 0 corresponds to wfi, should never be called
	 * from the cpu_suspend operations
	 */
	if (WARN_ON_ONCE(!index))
		return -EINVAL;

	if (!psci_power_state_loses_context(state[index - 1]))
		ret = psci_ops.cpu_suspend(state[index - 1], 0);
	else
		ret = cpu_suspend(index, psci_suspend_finisher);

	return ret;
}

const struct cpu_operations cpu_psci_ops = {
	.name		= "psci",
#ifdef CONFIG_CPU_IDLE
	.cpu_init_idle	= cpu_psci_cpu_init_idle,
	.cpu_suspend	= cpu_psci_cpu_suspend,
#endif
	.cpu_init	= cpu_psci_cpu_init,
	.cpu_prepare	= cpu_psci_cpu_prepare,
	.cpu_boot	= cpu_psci_cpu_boot,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable	= cpu_psci_cpu_disable,
	.cpu_die	= cpu_psci_cpu_die,
	.cpu_kill	= cpu_psci_cpu_kill,
#endif
};

