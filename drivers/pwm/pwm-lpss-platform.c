/*
 * Intel Low Power Subsystem PWM controller driver
 *
 * Copyright (C) 2014, Intel Corporation
 *
 * Derived from the original pwm-lpss.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#include "pwm-lpss.h"

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
