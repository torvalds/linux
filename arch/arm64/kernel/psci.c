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
#include <linux/reboot.h>
#include <linux/pm.h>
#include <linux/delay.h>
#include <uapi/linux/psci.h>

#include <asm/compiler.h>
#include <asm/cpu_ops.h>
#include <asm/errno.h>
#include <asm/psci.h>
#include <asm/smp_plat.h>
#include <asm/system_misc.h>

#define PSCI_POWER_STATE_TYPE_STANDBY		0
#define PSCI_POWER_STATE_TYPE_POWER_DOWN	1

struct psci_power_state {
	u16	id;
	u8	type;
	u8	affinity_level;
};

struct psci_operations {
	int (*cpu_suspend)(struct psci_power_state state,
			   unsigned long entry_point);
	int (*cpu_off)(struct psci_power_state state);
	int (*cpu_on)(unsigned long cpuid, unsigned long entry_point);
	int (*migrate)(unsigned long cpuid);
	int (*affinity_info)(unsigned long target_affinity,
			unsigned long lowest_affinity_level);
	int (*migrate_info_type)(void);
};

static struct psci_operations psci_ops;

static int (*invoke_psci_fn)(u64, u64, u64, u64);
typedef int (*psci_initcall_t)(const struct device_node *);

enum psci_function {
	PSCI_FN_CPU_SUSPEND,
	PSCI_FN_CPU_ON,
	PSCI_FN_CPU_OFF,
	PSCI_FN_MIGRATE,
	PSCI_FN_AFFINITY_INFO,
	PSCI_FN_MIGRATE_INFO_TYPE,
	PSCI_FN_MAX,
};

static u32 psci_function_id[PSCI_FN_MAX];

static int psci_to_linux_errno(int errno)
{
	switch (errno) {
	case PSCI_RET_SUCCESS:
		return 0;
	case PSCI_RET_NOT_SUPPORTED:
		return -EOPNOTSUPP;
	case PSCI_RET_INVALID_PARAMS:
		return -EINVAL;
	case PSCI_RET_DENIED:
		return -EPERM;
	};

	return -EINVAL;
}

static u32 psci_power_state_pack(struct psci_power_state state)
{
	return ((state.id << PSCI_0_2_POWER_STATE_ID_SHIFT)
			& PSCI_0_2_POWER_STATE_ID_MASK) |
		((state.type << PSCI_0_2_POWER_STATE_TYPE_SHIFT)
		 & PSCI_0_2_POWER_STATE_TYPE_MASK) |
		((state.affinity_level << PSCI_0_2_POWER_STATE_AFFL_SHIFT)
		 & PSCI_0_2_POWER_STATE_AFFL_MASK);
}

/*
 * The following two functions are invoked via the invoke_psci_fn pointer
 * and will not be inlined, allowing us to piggyback on the AAPCS.
 */
static noinline int __invoke_psci_fn_hvc(u64 function_id, u64 arg0, u64 arg1,
					 u64 arg2)
{
	asm volatile(
			__asmeq("%0", "x0")
			__asmeq("%1", "x1")
			__asmeq("%2", "x2")
			__asmeq("%3", "x3")
			"hvc	#0\n"
		: "+r" (function_id)
		: "r" (arg0), "r" (arg1), "r" (arg2));

	return function_id;
}

static noinline int __invoke_psci_fn_smc(u64 function_id, u64 arg0, u64 arg1,
					 u64 arg2)
{
	asm volatile(
			__asmeq("%0", "x0")
			__asmeq("%1", "x1")
			__asmeq("%2", "x2")
			__asmeq("%3", "x3")
			"smc	#0\n"
		: "+r" (function_id)
		: "r" (arg0), "r" (arg1), "r" (arg2));

	return function_id;
}

static int psci_get_version(void)
{
	int err;

	err = invoke_psci_fn(PSCI_0_2_FN_PSCI_VERSION, 0, 0, 0);
	return err;
}

static int psci_cpu_suspend(struct psci_power_state state,
			    unsigned long entry_point)
{
	int err;
	u32 fn, power_state;

	fn = psci_function_id[PSCI_FN_CPU_SUSPEND];
	power_state = psci_power_state_pack(state);
	err = invoke_psci_fn(fn, power_state, entry_point, 0);
	return psci_to_linux_errno(err);
}

static int psci_cpu_off(struct psci_power_state state)
{
	int err;
	u32 fn, power_state;

	fn = psci_function_id[PSCI_FN_CPU_OFF];
	power_state = psci_power_state_pack(state);
	err = invoke_psci_fn(fn, power_state, 0, 0);
	return psci_to_linux_errno(err);
}

static int psci_cpu_on(unsigned long cpuid, unsigned long entry_point)
{
	int err;
	u32 fn;

	fn = psci_function_id[PSCI_FN_CPU_ON];
	err = invoke_psci_fn(fn, cpuid, entry_point, 0);
	return psci_to_linux_errno(err);
}

static int psci_migrate(unsigned long cpuid)
{
	int err;
	u32 fn;

	fn = psci_function_id[PSCI_FN_MIGRATE];
	err = invoke_psci_fn(fn, cpuid, 0, 0);
	return psci_to_linux_errno(err);
}

static int psci_affinity_info(unsigned long target_affinity,
		unsigned long lowest_affinity_level)
{
	int err;
	u32 fn;

	fn = psci_function_id[PSCI_FN_AFFINITY_INFO];
	err = invoke_psci_fn(fn, target_affinity, lowest_affinity_level, 0);
	return err;
}

static int psci_migrate_info_type(void)
{
	int err;
	u32 fn;

	fn = psci_function_id[PSCI_FN_MIGRATE_INFO_TYPE];
	err = invoke_psci_fn(fn, 0, 0, 0);
	return err;
}

static int get_set_conduit_method(struct device_node *np)
{
	const char *method;

	pr_info("probing for conduit method from DT.\n");

	if (of_property_read_string(np, "method", &method)) {
		pr_warn("missing \"method\" property\n");
		return -ENXIO;
	}

	if (!strcmp("hvc", method)) {
		invoke_psci_fn = __invoke_psci_fn_hvc;
	} else if (!strcmp("smc", method)) {
		invoke_psci_fn = __invoke_psci_fn_smc;
	} else {
		pr_warn("invalid \"method\" property: %s\n", method);
		return -EINVAL;
	}
	return 0;
}

static void psci_sys_reset(enum reboot_mode reboot_mode, const char *cmd)
{
	invoke_psci_fn(PSCI_0_2_FN_SYSTEM_RESET, 0, 0, 0);
}

static void psci_sys_poweroff(void)
{
	invoke_psci_fn(PSCI_0_2_FN_SYSTEM_OFF, 0, 0, 0);
}

/*
 * PSCI Function IDs for v0.2+ are well defined so use
 * standard values.
 */
static int __init psci_0_2_init(struct device_node *np)
{
	int err, ver;

	err = get_set_conduit_method(np);

	if (err)
		goto out_put_node;

	ver = psci_get_version();

	if (ver == PSCI_RET_NOT_SUPPORTED) {
		/* PSCI v0.2 mandates implementation of PSCI_ID_VERSION. */
		pr_err("PSCI firmware does not comply with the v0.2 spec.\n");
		err = -EOPNOTSUPP;
		goto out_put_node;
	} else {
		pr_info("PSCIv%d.%d detected in firmware.\n",
				PSCI_VERSION_MAJOR(ver),
				PSCI_VERSION_MINOR(ver));

		if (PSCI_VERSION_MAJOR(ver) == 0 &&
				PSCI_VERSION_MINOR(ver) < 2) {
			err = -EINVAL;
			pr_err("Conflicting PSCI version detected.\n");
			goto out_put_node;
		}
	}

	pr_info("Using standard PSCI v0.2 function IDs\n");
	psci_function_id[PSCI_FN_CPU_SUSPEND] = PSCI_0_2_FN64_CPU_SUSPEND;
	psci_ops.cpu_suspend = psci_cpu_suspend;

	psci_function_id[PSCI_FN_CPU_OFF] = PSCI_0_2_FN_CPU_OFF;
	psci_ops.cpu_off = psci_cpu_off;

	psci_function_id[PSCI_FN_CPU_ON] = PSCI_0_2_FN64_CPU_ON;
	psci_ops.cpu_on = psci_cpu_on;

	psci_function_id[PSCI_FN_MIGRATE] = PSCI_0_2_FN64_MIGRATE;
	psci_ops.migrate = psci_migrate;

	psci_function_id[PSCI_FN_AFFINITY_INFO] = PSCI_0_2_FN64_AFFINITY_INFO;
	psci_ops.affinity_info = psci_affinity_info;

	psci_function_id[PSCI_FN_MIGRATE_INFO_TYPE] =
		PSCI_0_2_FN_MIGRATE_INFO_TYPE;
	psci_ops.migrate_info_type = psci_migrate_info_type;

	arm_pm_restart = psci_sys_reset;

	pm_power_off = psci_sys_poweroff;

out_put_node:
	of_node_put(np);
	return err;
}

/*
 * PSCI < v0.2 get PSCI Function IDs via DT.
 */
static int __init psci_0_1_init(struct device_node *np)
{
	u32 id;
	int err;

	err = get_set_conduit_method(np);

	if (err)
		goto out_put_node;

	pr_info("Using PSCI v0.1 Function IDs from DT\n");

	if (!of_property_read_u32(np, "cpu_suspend", &id)) {
		psci_function_id[PSCI_FN_CPU_SUSPEND] = id;
		psci_ops.cpu_suspend = psci_cpu_suspend;
	}

	if (!of_property_read_u32(np, "cpu_off", &id)) {
		psci_function_id[PSCI_FN_CPU_OFF] = id;
		psci_ops.cpu_off = psci_cpu_off;
	}

	if (!of_property_read_u32(np, "cpu_on", &id)) {
		psci_function_id[PSCI_FN_CPU_ON] = id;
		psci_ops.cpu_on = psci_cpu_on;
	}

	if (!of_property_read_u32(np, "migrate", &id)) {
		psci_function_id[PSCI_FN_MIGRATE] = id;
		psci_ops.migrate = psci_migrate;
	}

out_put_node:
	of_node_put(np);
	return err;
}

static const struct of_device_id psci_of_match[] __initconst = {
	{ .compatible = "arm,psci",	.data = psci_0_1_init},
	{ .compatible = "arm,psci-0.2",	.data = psci_0_2_init},
	{},
};

int __init psci_init(void)
{
	struct device_node *np;
	const struct of_device_id *matched_np;
	psci_initcall_t init_fn;

	np = of_find_matching_node_and_match(NULL, psci_of_match, &matched_np);

	if (!np)
		return -ENODEV;

	init_fn = (psci_initcall_t)matched_np->data;
	return init_fn(np);
}

#ifdef CONFIG_SMP

static int __init cpu_psci_cpu_init(struct device_node *dn, unsigned int cpu)
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
	return 0;
}

static void cpu_psci_cpu_die(unsigned int cpu)
{
	int ret;
	/*
	 * There are no known implementations of PSCI actually using the
	 * power state field, pass a sensible default for now.
	 */
	struct psci_power_state state = {
		.type = PSCI_POWER_STATE_TYPE_POWER_DOWN,
	};

	ret = psci_ops.cpu_off(state);

	pr_crit("unable to power off CPU%u (%d)\n", cpu, ret);
}

static int cpu_psci_cpu_kill(unsigned int cpu)
{
	int err, i;

	if (!psci_ops.affinity_info)
		return 1;
	/*
	 * cpu_kill could race with cpu_die and we can
	 * potentially end up declaring this cpu undead
	 * while it is dying. So, try again a few times.
	 */

	for (i = 0; i < 10; i++) {
		err = psci_ops.affinity_info(cpu_logical_map(cpu), 0);
		if (err == PSCI_0_2_AFFINITY_LEVEL_OFF) {
			pr_info("CPU%d killed.\n", cpu);
			return 1;
		}

		msleep(10);
		pr_info("Retrying again to check for CPU kill\n");
	}

	pr_warn("CPU%d may not have shut down cleanly (AFFINITY_INFO reports %d)\n",
			cpu, err);
	/* Make op_cpu_kill() fail. */
	return 0;
}
#endif
#endif

const struct cpu_operations cpu_psci_ops = {
	.name		= "psci",
#ifdef CONFIG_SMP
	.cpu_init	= cpu_psci_cpu_init,
	.cpu_prepare	= cpu_psci_cpu_prepare,
	.cpu_boot	= cpu_psci_cpu_boot,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable	= cpu_psci_cpu_disable,
	.cpu_die	= cpu_psci_cpu_die,
	.cpu_kill	= cpu_psci_cpu_kill,
#endif
#endif
};

