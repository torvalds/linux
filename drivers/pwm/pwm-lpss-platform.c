// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Low Power Subsystem PWM controller driver
 *
 * Copyright (C) 2014, Intel Corporation
 *
 * Derived from the original pwm-lpss.c
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "pwm-lpss.h"

/* BayTrail */
static const struct pwm_lpss_boardinfo pwm_lpss_byt_info = {
	.clk_rate = 25000000,
	.npwm = 1,
	.base_unit_bits = 16,
};

/* Braswell */
static const struct pwm_lpss_boardinfo pwm_lpss_bsw_info = {
	.clk_rate = 19200000,
	.npwm = 1,
	.base_unit_bits = 16,
	.other_devices_aml_touches_pwm_regs = true,
};

/* Broxton */
static const struct pwm_lpss_boardinfo pwm_lpss_bxt_info = {
	.clk_rate = 19200000,
	.npwm = 4,
	.base_unit_bits = 22,
	.bypass = true,
};

static int pwm_lpss_probe_platform(struct platform_device *pdev)
{
	const struct pwm_lpss_boardinfo *info;
	const struct acpi_device_id *id;
	struct pwm_lpss_chip *lpwm;
	struct resource *r;

	id = acpi_match_device(pdev->dev.driver->acpi_match_table, &pdev->dev);
	if (!id)
		return -ENODEV;

	info = (const struct pwm_lpss_boardinfo *)id->driver_data;
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	lpwm = pwm_lpss_probe(&pdev->dev, r, info);
	if (IS_ERR(lpwm))
		return PTR_ERR(lpwm);

	platform_set_drvdata(pdev, lpwm);

	/*
	 * On Cherry Trail devices the GFX0._PS0 AML checks if the controller
	 * is on and if it is not on it turns it on and restores what it
	 * believes is the correct state to the PWM controller.
	 * Because of this we must disallow direct-complete, which keeps the
	 * controller (runtime)suspended on resume, to avoid 2 issues:
	 * 1. The controller getting turned on without the linux-pm code
	 *    knowing about this. On devices where the controller is unused
	 *    this causes it to stay on during the next suspend causing high
	 *    battery drain (because S0i3 is not reached)
	 * 2. The state restoring code unexpectedly messing with the controller
	 *
	 * Leaving the controller runtime-suspended (skipping runtime-resume +
	 * normal-suspend) during suspend is fine.
	 */
	if (info->other_devices_aml_touches_pwm_regs)
		dev_pm_set_driver_flags(&pdev->dev, DPM_FLAG_NO_DIRECT_COMPLETE|
						    DPM_FLAG_SMART_SUSPEND);

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int pwm_lpss_remove_platform(struct platform_device *pdev)
{
	struct pwm_lpss_chip *lpwm = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	return pwm_lpss_remove(lpwm);
}

static const struct acpi_device_id pwm_lpss_acpi_match[] = {
	{ "80860F09", (unsigned long)&pwm_lpss_byt_info },
	{ "80862288", (unsigned long)&pwm_lpss_bsw_info },
	{ "80862289", (unsigned long)&pwm_lpss_bsw_info },
	{ "80865AC8", (unsigned long)&pwm_lpss_bxt_info },
	{ },
};
MODULE_DEVICE_TABLE(acpi, pwm_lpss_acpi_match);

static struct platform_driver pwm_lpss_driver_platform = {
	.driver = {
		.name = "pwm-lpss",
		.acpi_match_table = pwm_lpss_acpi_match,
	},
	.probe = pwm_lpss_probe_platform,
	.remove = pwm_lpss_remove_platform,
};
module_platform_driver(pwm_lpss_driver_platform);

MODULE_DESCRIPTION("PWM platform driver for Intel LPSS");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:pwm-lpss");
