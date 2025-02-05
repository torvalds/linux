// SPDX-License-Identifier: GPL-2.0
/*
 * Raspberry Pi cpufreq driver
 *
 * Copyright (C) 2019, Nicolas Saenz Julienne <nsaenzjulienne@suse.de>
 */

#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>

#define RASPBERRYPI_FREQ_INTERVAL	100000000

static struct platform_device *cpufreq_dt;

static int raspberrypi_cpufreq_probe(struct platform_device *pdev)
{
	struct device *cpu_dev;
	unsigned long min, max;
	unsigned long rate;
	struct clk *clk;
	int ret;

	cpu_dev = get_cpu_device(0);
	if (!cpu_dev) {
		pr_err("Cannot get CPU for cpufreq driver\n");
		return -ENODEV;
	}

	clk = clk_get(cpu_dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(cpu_dev, "Cannot get clock for CPU0\n");
		return PTR_ERR(clk);
	}

	/*
	 * The max and min frequencies are configurable in the Raspberry Pi
	 * firmware, so we query them at runtime.
	 */
	min = roundup(clk_round_rate(clk, 0), RASPBERRYPI_FREQ_INTERVAL);
	max = roundup(clk_round_rate(clk, ULONG_MAX), RASPBERRYPI_FREQ_INTERVAL);
	clk_put(clk);

	for (rate = min; rate <= max; rate += RASPBERRYPI_FREQ_INTERVAL) {
		ret = dev_pm_opp_add(cpu_dev, rate, 0);
		if (ret)
			goto remove_opp;
	}

	cpufreq_dt = platform_device_register_simple("cpufreq-dt", -1, NULL, 0);
	ret = PTR_ERR_OR_ZERO(cpufreq_dt);
	if (ret) {
		dev_err(cpu_dev, "Failed to create platform device, %d\n", ret);
		goto remove_opp;
	}

	return 0;

remove_opp:
	dev_pm_opp_remove_all_dynamic(cpu_dev);

	return ret;
}

static void raspberrypi_cpufreq_remove(struct platform_device *pdev)
{
	struct device *cpu_dev;

	cpu_dev = get_cpu_device(0);
	if (cpu_dev)
		dev_pm_opp_remove_all_dynamic(cpu_dev);

	platform_device_unregister(cpufreq_dt);
}

/*
 * Since the driver depends on clk-raspberrypi, which may return EPROBE_DEFER,
 * all the activity is performed in the probe, which may be defered as well.
 */
static struct platform_driver raspberrypi_cpufreq_driver = {
	.driver = {
		.name = "raspberrypi-cpufreq",
	},
	.probe          = raspberrypi_cpufreq_probe,
	.remove		= raspberrypi_cpufreq_remove,
};
module_platform_driver(raspberrypi_cpufreq_driver);

MODULE_AUTHOR("Nicolas Saenz Julienne <nsaenzjulienne@suse.de");
MODULE_DESCRIPTION("Raspberry Pi cpufreq driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:raspberrypi-cpufreq");
