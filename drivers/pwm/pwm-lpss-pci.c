/*
 * Intel Low Power Subsystem PWM controller PCI driver
 *
 * Copyright (C) 2014, Intel Corporation
 *
 * Derived from the original pwm-lpss.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
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
};

/* Broxton */
static const struct pwm_lpss_boardinfo pwm_lpss_bxt_info = {
	.clk_rate = 19200000,
	.npwm = 4,
	.base_unit_bits = 22,
	.bypass = true,
};

/* Tangier */
static const struct pwm_lpss_boardinfo pwm_lpss_tng_info = {
	.clk_rate = 19200000,
	.npwm = 4,
	.base_unit_bits = 22,
};

static int pwm_lpss_probe_pci(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	const struct pwm_lpss_boardinfo *info;
	struct pwm_lpss_chip *lpwm;
	int err;

	err = pcim_enable_device(pdev);
	if (err < 0)
		return err;

	info = (struct pwm_lpss_boardinfo *)id->driver_data;
	lpwm = pwm_lpss_probe(&pdev->dev, &pdev->resource[0], info);
	if (IS_ERR(lpwm))
		return PTR_ERR(lpwm);

	pci_set_drvdata(pdev, lpwm);

	pm_runtime_put(&pdev->dev);
	pm_runtime_allow(&pdev->dev);

	return 0;
}

static void pwm_lpss_remove_pci(struct pci_dev *pdev)
{
	struct pwm_lpss_chip *lpwm = pci_get_drvdata(pdev);

	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	pwm_lpss_remove(lpwm);
}

#ifdef CONFIG_PM
static int pwm_lpss_runtime_suspend_pci(struct device *dev)
{
	/*
	 * The PCI core will handle transition to D3 automatically. We only
	 * need to provide runtime PM hooks for that to happen.
	 */
	return 0;
}

static int pwm_lpss_runtime_resume_pci(struct device *dev)
{
	return 0;
}
#endif

static const struct dev_pm_ops pwm_lpss_pci_pm = {
	SET_RUNTIME_PM_OPS(pwm_lpss_runtime_suspend_pci,
			   pwm_lpss_runtime_resume_pci, NULL)
};

static const struct pci_device_id pwm_lpss_pci_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x0ac8), (unsigned long)&pwm_lpss_bxt_info},
	{ PCI_VDEVICE(INTEL, 0x0f08), (unsigned long)&pwm_lpss_byt_info},
	{ PCI_VDEVICE(INTEL, 0x0f09), (unsigned long)&pwm_lpss_byt_info},
	{ PCI_VDEVICE(INTEL, 0x11a5), (unsigned long)&pwm_lpss_tng_info},
	{ PCI_VDEVICE(INTEL, 0x1ac8), (unsigned long)&pwm_lpss_bxt_info},
	{ PCI_VDEVICE(INTEL, 0x2288), (unsigned long)&pwm_lpss_bsw_info},
	{ PCI_VDEVICE(INTEL, 0x2289), (unsigned long)&pwm_lpss_bsw_info},
	{ PCI_VDEVICE(INTEL, 0x31c8), (unsigned long)&pwm_lpss_bxt_info},
	{ PCI_VDEVICE(INTEL, 0x5ac8), (unsigned long)&pwm_lpss_bxt_info},
	{ },
};
MODULE_DEVICE_TABLE(pci, pwm_lpss_pci_ids);

static struct pci_driver pwm_lpss_driver_pci = {
	.name = "pwm-lpss",
	.id_table = pwm_lpss_pci_ids,
	.probe = pwm_lpss_probe_pci,
	.remove = pwm_lpss_remove_pci,
	.driver = {
		.pm = &pwm_lpss_pci_pm,
	},
};
module_pci_driver(pwm_lpss_driver_pci);

MODULE_DESCRIPTION("PWM PCI driver for Intel LPSS");
MODULE_LICENSE("GPL v2");
