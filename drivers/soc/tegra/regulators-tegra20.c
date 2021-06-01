// SPDX-License-Identifier: GPL-2.0+
/*
 * Voltage regulators coupler for NVIDIA Tegra20
 * Copyright (C) 2019 GRATE-DRIVER project
 *
 * Voltage constraints borrowed from downstream kernel sources
 * Copyright (C) 2010-2011 NVIDIA Corporation
 */

#define pr_fmt(fmt)	"tegra voltage-coupler: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/reboot.h>
#include <linux/regulator/coupler.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>

#include <soc/tegra/pmc.h>

struct tegra_regulator_coupler {
	struct regulator_coupler coupler;
	struct regulator_dev *core_rdev;
	struct regulator_dev *cpu_rdev;
	struct regulator_dev *rtc_rdev;
	struct notifier_block reboot_notifier;
	int core_min_uV, cpu_min_uV;
	bool sys_reboot_mode_req;
	bool sys_reboot_mode;
};

static inline struct tegra_regulator_coupler *
to_tegra_coupler(struct regulator_coupler *coupler)
{
	return container_of(coupler, struct tegra_regulator_coupler, coupler);
}

static int tegra20_core_limit(struct tegra_regulator_coupler *tegra,
			      struct regulator_dev *core_rdev)
{
	int core_min_uV = 0;
	int core_max_uV;
	int core_cur_uV;
	int err;

	/*
	 * Tegra20 SoC has critical DVFS-capable devices that are
	 * permanently-active or active at a boot time, like EMC
	 * (DRAM controller) or Display controller for example.
	 *
	 * The voltage of a CORE SoC power domain shall not be dropped below
	 * a minimum level, which is determined by device's clock rate.
	 * This means that we can't fully allow CORE voltage scaling until
	 * the state of all DVFS-critical CORE devices is synced.
	 */
	if (tegra_pmc_core_domain_state_synced() && !tegra->sys_reboot_mode) {
		pr_info_once("voltage state synced\n");
		return 0;
	}

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

	pr_info("core voltage initialized to %duV\n", tegra->core_min_uV);

	return tegra->core_min_uV;
}

static int tegra20_core_rtc_max_spread(struct regulator_dev *core_rdev,
				       struct regulator_dev *rtc_rdev)
{
	struct coupling_desc *c_desc = &core_rdev->coupling_desc;
	struct regulator_dev *rdev;
	int max_spread;
	unsigned int i;

	for (i = 1; i < c_desc->n_coupled; i++) {
		max_spread = core_rdev->constraints->max_spread[i - 1];
		rdev = c_desc->coupled_rdevs[i];

		if (rdev == rtc_rdev && max_spread)
			return max_spread;
	}

	pr_err_once("rtc-core max-spread is undefined in device-tree\n");

	return 150000;
}

static int tegra20_core_rtc_update(struct tegra_regulator_coupler *tegra,
				   struct regulator_dev *core_rdev,
				   struct regulator_dev *rtc_rdev,
				   int cpu_uV, int cpu_min_uV)
{
	int core_min_uV, core_max_uV = INT_MAX;
	int rtc_min_uV, rtc_max_uV = INT_MAX;
	int core_target_uV;
	int rtc_target_uV;
	int max_spread;
	int core_uV;
	int rtc_uV;
	int err;

	/*
	 * RTC and CORE voltages should be no more than 170mV from each other,
	 * CPU should be below RTC and CORE by at least 120mV. This applies
	 * to all Tegra20 SoC's.
	 */
	max_spread = tegra20_core_rtc_max_spread(core_rdev, rtc_rdev);

	/*
	 * The core voltage scaling is currently not hooked up in drivers,
	 * hence we will limit the minimum core voltage to a reasonable value.
	 * This should be good enough for the time being.
	 */
	core_min_uV = tegra20_core_limit(tegra, core_rdev);
	if (core_min_uV < 0)
		return core_min_uV;

	err = regulator_check_voltage(core_rdev, &core_min_uV, &core_max_uV);
	if (err)
		return err;

	err = regulator_check_consumers(core_rdev, &core_min_uV, &core_max_uV,
					PM_SUSPEND_ON);
	if (err)
		return err;

	core_uV = regulator_get_voltage_rdev(core_rdev);
	if (core_uV < 0)
		return core_uV;

	core_min_uV = max(cpu_min_uV + 125000, core_min_uV);
	if (core_min_uV > core_max_uV)
		return -EINVAL;

	if (cpu_uV + 120000 > core_uV)
		pr_err("core-cpu voltage constraint violated: %d %d\n",
		       core_uV, cpu_uV + 120000);

	rtc_uV = regulator_get_voltage_rdev(rtc_rdev);
	if (rtc_uV < 0)
		return rtc_uV;

	if (cpu_uV + 120000 > rtc_uV)
		pr_err("rtc-cpu voltage constraint violated: %d %d\n",
		       rtc_uV, cpu_uV + 120000);

	if (abs(core_uV - rtc_uV) > 170000)
		pr_err("core-rtc voltage constraint violated: %d %d\n",
		       core_uV, rtc_uV);

	rtc_min_uV = max(cpu_min_uV + 125000, core_min_uV - max_spread);

	err = regulator_check_voltage(rtc_rdev, &rtc_min_uV, &rtc_max_uV);
	if (err)
		return err;

	while (core_uV != core_min_uV || rtc_uV != rtc_min_uV) {
		if (core_uV < core_min_uV) {
			core_target_uV = min(core_uV + max_spread, core_min_uV);
			core_target_uV = min(rtc_uV + max_spread, core_target_uV);
		} else {
			core_target_uV = max(core_uV - max_spread, core_min_uV);
			core_target_uV = max(rtc_uV - max_spread, core_target_uV);
		}

		if (core_uV == core_target_uV)
			goto update_rtc;

		err = regulator_set_voltage_rdev(core_rdev,
						 core_target_uV,
						 core_max_uV,
						 PM_SUSPEND_ON);
		if (err)
			return err;

		core_uV = core_target_uV;
update_rtc:
		if (rtc_uV < rtc_min_uV) {
			rtc_target_uV = min(rtc_uV + max_spread, rtc_min_uV);
			rtc_target_uV = min(core_uV + max_spread, rtc_target_uV);
		} else {
			rtc_target_uV = max(rtc_uV - max_spread, rtc_min_uV);
			rtc_target_uV = max(core_uV - max_spread, rtc_target_uV);
		}

		if (rtc_uV == rtc_target_uV)
			continue;

		err = regulator_set_voltage_rdev(rtc_rdev,
						 rtc_target_uV,
						 rtc_max_uV,
						 PM_SUSPEND_ON);
		if (err)
			return err;

		rtc_uV = rtc_target_uV;
	}

	return 0;
}

static int tegra20_core_voltage_update(struct tegra_regulator_coupler *tegra,
				       struct regulator_dev *cpu_rdev,
				       struct regulator_dev *core_rdev,
				       struct regulator_dev *rtc_rdev)
{
	int cpu_uV;

	cpu_uV = regulator_get_voltage_rdev(cpu_rdev);
	if (cpu_uV < 0)
		return cpu_uV;

	return tegra20_core_rtc_update(tegra, core_rdev, rtc_rdev,
				       cpu_uV, cpu_uV);
}

static int tegra20_cpu_voltage_update(struct tegra_regulator_coupler *tegra,
				      struct regulator_dev *cpu_rdev,
				      struct regulator_dev *core_rdev,
				      struct regulator_dev *rtc_rdev)
{
	int cpu_min_uV_consumers = 0;
	int cpu_max_uV = INT_MAX;
	int cpu_min_uV = 0;
	int cpu_uV;
	int err;

	err = regulator_check_voltage(cpu_rdev, &cpu_min_uV, &cpu_max_uV);
	if (err)
		return err;

	err = regulator_check_consumers(cpu_rdev, &cpu_min_uV, &cpu_max_uV,
					PM_SUSPEND_ON);
	if (err)
		return err;

	err = regulator_check_consumers(cpu_rdev, &cpu_min_uV_consumers,
					&cpu_max_uV, PM_SUSPEND_ON);
	if (err)
		return err;

	cpu_uV = regulator_get_voltage_rdev(cpu_rdev);
	if (cpu_uV < 0)
		return cpu_uV;

	/* store boot voltage level */
	if (!tegra->cpu_min_uV)
		tegra->cpu_min_uV = cpu_uV;

	/*
	 * CPU's regulator may not have any consumers, hence the voltage
	 * must not be changed in that case because CPU simply won't
	 * survive the voltage drop if it's running on a higher frequency.
	 */
	if (!cpu_min_uV_consumers)
		cpu_min_uV = cpu_uV;

	/* restore boot voltage level */
	if (tegra->sys_reboot_mode)
		cpu_min_uV = max(cpu_min_uV, tegra->cpu_min_uV);

	if (cpu_min_uV > cpu_uV) {
		err = tegra20_core_rtc_update(tegra, core_rdev, rtc_rdev,
					      cpu_uV, cpu_min_uV);
		if (err)
			return err;

		err = regulator_set_voltage_rdev(cpu_rdev, cpu_min_uV,
						 cpu_max_uV, PM_SUSPEND_ON);
		if (err)
			return err;
	} else if (cpu_min_uV < cpu_uV)  {
		err = regulator_set_voltage_rdev(cpu_rdev, cpu_min_uV,
						 cpu_max_uV, PM_SUSPEND_ON);
		if (err)
			return err;

		err = tegra20_core_rtc_update(tegra, core_rdev, rtc_rdev,
					      cpu_uV, cpu_min_uV);
		if (err)
			return err;
	}

	return 0;
}

static int tegra20_regulator_balance_voltage(struct regulator_coupler *coupler,
					     struct regulator_dev *rdev,
					     suspend_state_t state)
{
	struct tegra_regulator_coupler *tegra = to_tegra_coupler(coupler);
	struct regulator_dev *core_rdev = tegra->core_rdev;
	struct regulator_dev *cpu_rdev = tegra->cpu_rdev;
	struct regulator_dev *rtc_rdev = tegra->rtc_rdev;

	if ((core_rdev != rdev && cpu_rdev != rdev && rtc_rdev != rdev) ||
	    state != PM_SUSPEND_ON) {
		pr_err("regulators are not coupled properly\n");
		return -EINVAL;
	}

	tegra->sys_reboot_mode = READ_ONCE(tegra->sys_reboot_mode_req);

	if (rdev == cpu_rdev)
		return tegra20_cpu_voltage_update(tegra, cpu_rdev,
						  core_rdev, rtc_rdev);

	if (rdev == core_rdev)
		return tegra20_core_voltage_update(tegra, cpu_rdev,
						   core_rdev, rtc_rdev);

	pr_err("changing %s voltage not permitted\n", rdev_get_name(rtc_rdev));

	return -EPERM;
}

static int tegra20_regulator_prepare_reboot(struct tegra_regulator_coupler *tegra,
					    bool sys_reboot_mode)
{
	int err;

	if (!tegra->core_rdev || !tegra->rtc_rdev || !tegra->cpu_rdev)
		return 0;

	WRITE_ONCE(tegra->sys_reboot_mode_req, true);

	/*
	 * Some devices use CPU soft-reboot method and in this case we
	 * should ensure that voltages are sane for the reboot by restoring
	 * the minimum boot levels.
	 */
	err = regulator_sync_voltage_rdev(tegra->cpu_rdev);
	if (err)
		return err;

	err = regulator_sync_voltage_rdev(tegra->core_rdev);
	if (err)
		return err;

	WRITE_ONCE(tegra->sys_reboot_mode_req, sys_reboot_mode);

	return 0;
}

static int tegra20_regulator_reboot(struct notifier_block *notifier,
				    unsigned long event, void *cmd)
{
	struct tegra_regulator_coupler *tegra;
	int ret;

	if (event != SYS_RESTART)
		return NOTIFY_DONE;

	tegra = container_of(notifier, struct tegra_regulator_coupler,
			     reboot_notifier);

	ret = tegra20_regulator_prepare_reboot(tegra, true);

	return notifier_from_errno(ret);
}

static int tegra20_regulator_attach(struct regulator_coupler *coupler,
				    struct regulator_dev *rdev)
{
	struct tegra_regulator_coupler *tegra = to_tegra_coupler(coupler);
	struct device_node *np = rdev->dev.of_node;

	if (of_property_read_bool(np, "nvidia,tegra-core-regulator") &&
	    !tegra->core_rdev) {
		tegra->core_rdev = rdev;
		return 0;
	}

	if (of_property_read_bool(np, "nvidia,tegra-rtc-regulator") &&
	    !tegra->rtc_rdev) {
		tegra->rtc_rdev = rdev;
		return 0;
	}

	if (of_property_read_bool(np, "nvidia,tegra-cpu-regulator") &&
	    !tegra->cpu_rdev) {
		tegra->cpu_rdev = rdev;
		return 0;
	}

	return -EINVAL;
}

static int tegra20_regulator_detach(struct regulator_coupler *coupler,
				    struct regulator_dev *rdev)
{
	struct tegra_regulator_coupler *tegra = to_tegra_coupler(coupler);

	/*
	 * We don't expect regulators to be decoupled during reboot,
	 * this may race with the reboot handler and shouldn't ever
	 * happen in practice.
	 */
	if (WARN_ON_ONCE(system_state > SYSTEM_RUNNING))
		return -EPERM;

	if (tegra->core_rdev == rdev) {
		tegra->core_rdev = NULL;
		return 0;
	}

	if (tegra->rtc_rdev == rdev) {
		tegra->rtc_rdev = NULL;
		return 0;
	}

	if (tegra->cpu_rdev == rdev) {
		tegra->cpu_rdev = NULL;
		return 0;
	}

	return -EINVAL;
}

static struct tegra_regulator_coupler tegra20_coupler = {
	.coupler = {
		.attach_regulator = tegra20_regulator_attach,
		.detach_regulator = tegra20_regulator_detach,
		.balance_voltage = tegra20_regulator_balance_voltage,
	},
	.reboot_notifier.notifier_call = tegra20_regulator_reboot,
};

static int __init tegra_regulator_coupler_init(void)
{
	int err;

	if (!of_machine_is_compatible("nvidia,tegra20"))
		return 0;

	err = register_reboot_notifier(&tegra20_coupler.reboot_notifier);
	WARN_ON(err);

	return regulator_coupler_register(&tegra20_coupler.coupler);
}
arch_initcall(tegra_regulator_coupler_init);
