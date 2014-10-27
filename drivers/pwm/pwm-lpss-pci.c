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

#include "pwm-lpss.h"

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
	return 0;
}

static void pwm_lpss_remove_pci(struct pci_dev *pdev)
{
	struct pwm_lpss_chip *lpwm = pci_get_drvdata(pdev);

	pwm_lpss_remove(lpwm);
}

static const struct pci_device_id pwm_lpss_pci_ids[] = {
	{ PCI_VDEVICE(INTEL, 0x0f08), (unsigned long)&pwm_lpss_byt_info},
	{ PCI_VDEVICE(INTEL, 0x0f09), (unsigned long)&pwm_lpss_byt_info},
	{ PCI_VDEVICE(INTEL, 0x2288), (unsigned long)&pwm_lpss_bsw_info},
	{ PCI_VDEVICE(INTEL, 0x2289), (unsigned long)&pwm_lpss_bsw_info},
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
