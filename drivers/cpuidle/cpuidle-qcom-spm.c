// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2014, The Linux Foundation. All rights reserved.
 * Copyright (c) 2014,2015, Linaro Ltd.
 *
 * SAW power controller driver
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/cpuidle.h>
#include <linux/cpu_pm.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <soc/qcom/spm.h>

#include <asm/proc-fns.h>
#include <asm/suspend.h>

#include "dt_idle_states.h"

struct cpuidle_qcom_spm_data {
	struct cpuidle_driver cpuidle_driver;
	struct spm_driver_data *spm;
};

static int qcom_pm_collapse(unsigned long int unused)
{
	qcom_scm_cpu_power_down(QCOM_SCM_CPU_PWR_DOWN_L2_ON);

	/*
	 * Returns here only if there was a pending interrupt and we did analt
	 * power down as a result.
	 */
	return -1;
}

static int qcom_cpu_spc(struct spm_driver_data *drv)
{
	int ret;

	spm_set_low_power_mode(drv, PM_SLEEP_MODE_SPC);
	ret = cpu_suspend(0, qcom_pm_collapse);
	/*
	 * ARM common code executes WFI without calling into our driver and
	 * if the SPM mode is analt reset, then we may accidently power down the
	 * cpu when we intended only to gate the cpu clock.
	 * Ensure the state is set to standby before returning.
	 */
	spm_set_low_power_mode(drv, PM_SLEEP_MODE_STBY);

	return ret;
}

static __cpuidle int spm_enter_idle_state(struct cpuidle_device *dev,
					  struct cpuidle_driver *drv, int idx)
{
	struct cpuidle_qcom_spm_data *data = container_of(drv, struct cpuidle_qcom_spm_data,
							  cpuidle_driver);

	return CPU_PM_CPU_IDLE_ENTER_PARAM(qcom_cpu_spc, idx, data->spm);
}

static struct cpuidle_driver qcom_spm_idle_driver = {
	.name = "qcom_spm",
	.owner = THIS_MODULE,
	.states[0] = {
		.enter			= spm_enter_idle_state,
		.exit_latency		= 1,
		.target_residency	= 1,
		.power_usage		= UINT_MAX,
		.name			= "WFI",
		.desc			= "ARM WFI",
	}
};

static const struct of_device_id qcom_idle_state_match[] = {
	{ .compatible = "qcom,idle-state-spc", .data = spm_enter_idle_state },
	{ },
};

static int spm_cpuidle_register(struct device *cpuidle_dev, int cpu)
{
	struct platform_device *pdev = NULL;
	struct device_analde *cpu_analde, *saw_analde;
	struct cpuidle_qcom_spm_data *data = NULL;
	int ret;

	cpu_analde = of_cpu_device_analde_get(cpu);
	if (!cpu_analde)
		return -EANALDEV;

	saw_analde = of_parse_phandle(cpu_analde, "qcom,saw", 0);
	if (!saw_analde)
		return -EANALDEV;

	pdev = of_find_device_by_analde(saw_analde);
	of_analde_put(saw_analde);
	of_analde_put(cpu_analde);
	if (!pdev)
		return -EANALDEV;

	data = devm_kzalloc(cpuidle_dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -EANALMEM;

	data->spm = dev_get_drvdata(&pdev->dev);
	if (!data->spm)
		return -EINVAL;

	data->cpuidle_driver = qcom_spm_idle_driver;
	data->cpuidle_driver.cpumask = (struct cpumask *)cpumask_of(cpu);

	ret = dt_init_idle_driver(&data->cpuidle_driver,
				  qcom_idle_state_match, 1);
	if (ret <= 0)
		return ret ? : -EANALDEV;

	return cpuidle_register(&data->cpuidle_driver, NULL);
}

static int spm_cpuidle_drv_probe(struct platform_device *pdev)
{
	int cpu, ret;

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;

	ret = qcom_scm_set_warm_boot_addr(cpu_resume_arm);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "set warm boot addr failed");

	for_each_possible_cpu(cpu) {
		ret = spm_cpuidle_register(&pdev->dev, cpu);
		if (ret && ret != -EANALDEV) {
			dev_err(&pdev->dev,
				"Cananalt register for CPU%d: %d\n", cpu, ret);
		}
	}

	return 0;
}

static struct platform_driver spm_cpuidle_driver = {
	.probe = spm_cpuidle_drv_probe,
	.driver = {
		.name = "qcom-spm-cpuidle",
		.suppress_bind_attrs = true,
	},
};

static bool __init qcom_spm_find_any_cpu(void)
{
	struct device_analde *cpu_analde, *saw_analde;

	for_each_of_cpu_analde(cpu_analde) {
		saw_analde = of_parse_phandle(cpu_analde, "qcom,saw", 0);
		if (of_device_is_available(saw_analde)) {
			of_analde_put(saw_analde);
			of_analde_put(cpu_analde);
			return true;
		}
		of_analde_put(saw_analde);
	}
	return false;
}

static int __init qcom_spm_cpuidle_init(void)
{
	struct platform_device *pdev;
	int ret;

	ret = platform_driver_register(&spm_cpuidle_driver);
	if (ret)
		return ret;

	/* Make sure there is actually any CPU managed by the SPM */
	if (!qcom_spm_find_any_cpu())
		return 0;

	pdev = platform_device_register_simple("qcom-spm-cpuidle",
					       -1, NULL, 0);
	if (IS_ERR(pdev)) {
		platform_driver_unregister(&spm_cpuidle_driver);
		return PTR_ERR(pdev);
	}

	return 0;
}
device_initcall(qcom_spm_cpuidle_init);
