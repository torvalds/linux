// SPDX-License-Identifier: GPL-2.0+
/*
 * Voltage regulators coupler for NVIDIA Tegra30
 * Copyright (C) 2019 GRATE-DRIVER project
 *
 * Voltage constraints borrowed from downstream kernel sources
 * Copyright (C) 2010-2011 NVIDIA Corporation
 */

#define pr_fmt(fmt)	"tegra voltage-coupler: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/regulator/coupler.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#include <soc/tegra/fuse.h>

struct tegra_regulator_coupler {
	struct regulator_coupler coupler;
	struct regulator_dev *core_rdev;
	struct regulator_dev *cpu_rdev;
	int core_min_uV;
};

static inline struct tegra_regulator_coupler *
to_tegra_coupler(struct regulator_coupler *coupler)
{
	return container_of(coupler, struct tegra_regulator_coupler, coupler);
}

static int tegra30_core_limit(struct tegra_regulator_coupler *tegra,
			      struct regulator_dev *core_rdev)
{
	int core_min_uV = 0;
	int core_max_uV;
	int core_cur_uV;
	int err;

	if (tegra->core_min_uV > 0)
		return tegra->core_min_uV;

	core_cur_uV = regulator_get_voltage_rdev(core_rdev);
	if (core_cur_uV < 0)
		return core_cur_uV;

	core_max_uV = max(core_cur_uV, 1200000);

	err = regulator_check_voltage(core_rdev, &core_min_uV, &core_max_uV);
	if (err)
		return err;

	/*
	 * Limit minimum CORE voltage to a value left from bootloader or,
	 * if it's unreasonably low value, to the most common 1.2v or to
	 * whatever maximum value defined via board's device-tree.
	 */
	tegra->core_min_uV = core_max_uV;

	pr_info("core minimum voltage limited to %duV\n", tegra->core_min_uV);

	return tegra->core_min_uV;
}

static int tegra30_core_cpu_limit(int cpu_uV)
{
	if (cpu_uV < 800000)
		return 950000;

	if (cpu_uV < 900000)
		return 1000000;

	if (cpu_uV < 1000000)
		return 1100000;

	if (cpu_uV < 1100000)
		return 1200000;

	if (cpu_uV < 1250000) {
		switch (tegra_sku_info.cpu_speedo_id) {
		case 0 ... 1:
		case 4:
		case 7 ... 8:
			return 1200000;

		default:
			return 1300000;
		}
	}

	return -EINVAL;
}

static int tegra30_voltage_update(struct tegra_regulator_coupler *tegra,
				  struct regulator_dev *cpu_rdev,
				  struct regulator_dev *core_rdev)
{
	int core_min_uV, core_max_uV = INT_MAX;
	int cpu_min_uV, cpu_max_uV = INT_MAX;
	int cpu_min_uV_consumers = 0;
	int core_min_limited_uV;
	int core_target_uV;
	int cpu_target_uV;
	int core_max_step;
	int cpu_max_step;
	int max_spread;
	int core_uV;
	int cpu_uV;
	int err;

	/*
	 * CPU voltage should not got lower than 300mV from the CORE.
	 * CPU voltage should stay below the CORE by 100mV+, depending
	 * by the CORE voltage. This applies to all Tegra30 SoC's.
	 */
	max_spread = cpu_rdev->constraints->max_spread[0];
	cpu_max_step = cpu_rdev->constraints->max_uV_step;
	core_max_step = core_rdev->constraints->max_uV_step;

	if (!max_spread) {
		pr_err_once("cpu-core max-spread is undefined in device-tree\n");
		max_spread = 300000;
	}

	if (!cpu_max_step) {
		pr_err_once("cpu max-step is undefined in device-tree\n");
		cpu_max_step = 150000;
	}

	if (!core_max_step) {
		pr_err_once("core max-step is undefined in device-tree\n");
		core_max_step = 150000;
	}

	/*
	 * The CORE voltage scaling is currently not hooked up in drivers,
	 * hence we will limit the minimum CORE voltage to a reasonable value.
	 * This should be good enough for the time being.
	 */
	core_min_uV = tegra30_core_limit(tegra, core_rdev);
	if (core_min_uV < 0)
		return core_min_uV;

	err = regulator_check_consumers(core_rdev, &core_min_uV, &core_max_uV,
					PM_SUSPEND_ON);
	if (err)
		return err;

	core_uV = regulator_get_voltage_rdev(core_rdev);
	if (core_uV < 0)
		return core_uV;

	cpu_min_uV = core_min_uV - max_spread;

	err = regulator_check_consumers(cpu_rdev, &cpu_min_uV, &cpu_max_uV,
					PM_SUSPEND_ON);
	if (err)
		return err;

	err = regulator_check_consumers(cpu_rdev, &cpu_min_uV_consumers,
					&cpu_max_uV, PM_SUSPEND_ON);
	if (err)
		return err;

	err = regulator_check_voltage(cpu_rdev, &cpu_min_uV, &cpu_max_uV);
	if (err)
		return err;

	cpu_uV = regulator_get_voltage_rdev(cpu_rdev);
	if (cpu_uV < 0)
		return cpu_uV;

	/*
	 * CPU's regulator may not have any consumers, hence the voltage
	 * must not be changed in that case because CPU simply won't
	 * survive the voltage drop if it's running on a higher frequency.
	 */
	if (!cpu_min_uV_consumers)
		cpu_min_uV = max(cpu_uV, cpu_min_uV);

	/*
	 * Bootloader shall set up voltages correctly, but if it
	 * happens that there is a violation, then try to fix it
	 * at first.
	 */
	core_min_limited_uV = tegra30_core_cpu_limit(cpu_uV);
	if (core_min_limited_uV < 0)
		return core_min_limited_uV;

	core_min_uV = max(core_min_uV, tegra30_core_cpu_limit(cpu_min_uV));

	err = regulator_check_voltage(core_rdev, &core_min_uV, &core_max_uV);
	if (err)
		return err;

	if (core_min_limited_uV > core_uV) {
		pr_err("core voltage constraint violated: %d %d %d\n",
		       core_uV, core_min_limited_uV, cpu_uV);
		goto update_core;
	}

	while (cpu_uV != cpu_min_uV || core_uV != core_min_uV) {
		if (cpu_uV < cpu_min_uV) {
			cpu_target_uV = min(cpu_uV + cpu_max_step, cpu_min_uV);
		} else {
			cpu_target_uV = max(cpu_uV - cpu_max_step, cpu_min_uV);
			cpu_target_uV = max(core_uV - max_spread, cpu_target_uV);
		}

		if (cpu_uV == cpu_target_uV)
			goto update_core;

		err = regulator_set_voltage_rdev(cpu_rdev,
						 cpu_target_uV,
						 cpu_max_uV,
						 PM_SUSPEND_ON);
		if (err)
			return err;

		cpu_uV = cpu_target_uV;
update_core:
		core_min_limited_uV = tegra30_core_cpu_limit(cpu_uV);
		if (core_min_limited_uV < 0)
			return core_min_limited_uV;

		core_target_uV = max(core_min_limited_uV, core_min_uV);

		if (core_uV < core_target_uV) {
			core_target_uV = min(core_target_uV, core_uV + core_max_step);
			core_target_uV = min(core_target_uV, cpu_uV + max_spread);
		} else {
			core_target_uV = max(core_target_uV, core_uV - core_max_step);
		}

		if (core_uV == core_target_uV)
			continue;

		err = regulator_set_voltage_rdev(core_rdev,
						 core_target_uV,
						 core_max_uV,
						 PM_SUSPEND_ON);
		if (err)
			return err;

		core_uV = core_target_uV;
	}

	return 0;
}

static int tegra30_regulator_balance_voltage(struct regulator_coupler *coupler,
					     struct regulator_dev *rdev,
					     suspend_state_t state)
{
	struct tegra_regulator_coupler *tegra = to_tegra_coupler(coupler);
	struct regulator_dev *core_rdev = tegra->core_rdev;
	struct regulator_dev *cpu_rdev = tegra->cpu_rdev;

	if ((core_rdev != rdev && cpu_rdev != rdev) || state != PM_SUSPEND_ON) {
		pr_err("regulators are not coupled properly\n");
		return -EINVAL;
	}

	return tegra30_voltage_update(tegra, cpu_rdev, core_rdev);
}

static int tegra30_regulator_attach(struct regulator_coupler *coupler,
				    struct regulator_dev *rdev)
{
	struct tegra_regulator_coupler *tegra = to_tegra_coupler(coupler);
	struct device_node *np = rdev->dev.of_node;

	if (of_property_read_bool(np, "nvidia,tegra-core-regulator") &&
	    !tegra->core_rdev) {
		tegra->core_rdev = rdev;
		return 0;
	}

	if (of_property_read_bool(np, "nvidia,tegra-cpu-regulator") &&
	    !tegra->cpu_rdev) {
		tegra->cpu_rdev = rdev;
		return 0;
	}

	return -EINVAL;
}

static int tegra30_regulator_detach(struct regulator_coupler *coupler,
				    struct regulator_dev *rdev)
{
	struct tegra_regulator_coupler *tegra = to_tegra_coupler(coupler);

	if (tegra->core_rdev == rdev) {
		tegra->core_rdev = NULL;
		return 0;
	}

	if (tegra->cpu_rdev == rdev) {
		tegra->cpu_rdev = NULL;
		return 0;
	}

	return -EINVAL;
}

static struct tegra_regulator_coupler tegra30_coupler = {
	.coupler = {
		.attach_regulator = tegra30_regulator_attach,
		.detach_regulator = tegra30_regulator_detach,
		.balance_voltage = tegra30_regulator_balance_voltage,
	},
};

static int __init tegra_regulator_coupler_init(void)
{
	if (!of_machine_is_compatible("nvidia,tegra30"))
		return 0;

	return regulator_coupler_register(&tegra30_coupler.coupler);
}
arch_initcall(tegra_regulator_coupler_init);
