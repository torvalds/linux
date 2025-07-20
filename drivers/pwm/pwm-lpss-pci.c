// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Low Power Subsystem PWM controller PCI driver
 *
 * Copyright (C) 2014, Intel Corporation
 *
 * Derived from the original pwm-lpss.c
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>

#include "pwm-lpss.h"

static int pwm_lpss_probe_pci(struct pci_dev *pdev,
			      const struct pci_device_id *id)
{
	const struct pwm_lpss_boardinfo *info;
	void __iomem *io_base;
	struct pwm_chip *chip;
	int err;

	err = pcim_enable_device(pdev);
	if (err < 0)
		return err;

	io_base = pcim_iomap_region(pdev, 0, "pwm-lpss");
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	info = (struct pwm_lpss_boardinfo *)id->driver_data;
	chip = devm_pwm_lpss_probe(&pdev->dev, io_base, info);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	pm_runtime_put(&pdev->dev);
	pm_runtime_allow(&pdev->dev);

	return 0;
}

static void pwm_lpss_remove_pci(struct pci_dev *pdev)
{
	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);
}

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
};
module_pci_driver(pwm_lpss_driver_pci);

MODULE_DESCRIPTION("PWM PCI driver for Intel LPSS");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("PWM_LPSS");
