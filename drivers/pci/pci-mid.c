/*
 * Intel MID platform PM support
 *
 * Copyright (C) 2016, Intel Corporation
 *
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/pci.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/intel-mid.h>

#include "pci.h"

static bool mid_pci_power_manageable(struct pci_dev *dev)
{
	return true;
}

static int mid_pci_set_power_state(struct pci_dev *pdev, pci_power_t state)
{
	return intel_mid_pci_set_power_state(pdev, state);
}

static pci_power_t mid_pci_choose_state(struct pci_dev *pdev)
{
	return PCI_D3hot;
}

static int mid_pci_sleep_wake(struct pci_dev *dev, bool enable)
{
	return 0;
}

static int mid_pci_run_wake(struct pci_dev *dev, bool enable)
{
	return 0;
}

static bool mid_pci_need_resume(struct pci_dev *dev)
{
	return false;
}

static struct pci_platform_pm_ops mid_pci_platform_pm = {
	.is_manageable	= mid_pci_power_manageable,
	.set_state	= mid_pci_set_power_state,
	.choose_state	= mid_pci_choose_state,
	.sleep_wake	= mid_pci_sleep_wake,
	.run_wake	= mid_pci_run_wake,
	.need_resume	= mid_pci_need_resume,
};

#define ICPU(model)	{ X86_VENDOR_INTEL, 6, model, X86_FEATURE_ANY, }

static const struct x86_cpu_id lpss_cpu_ids[] = {
	ICPU(INTEL_FAM6_ATOM_MERRIFIELD1),
	{}
};

static int __init mid_pci_init(void)
{
	const struct x86_cpu_id *id;

	id = x86_match_cpu(lpss_cpu_ids);
	if (id)
		pci_set_platform_pm(&mid_pci_platform_pm);
	return 0;
}
arch_initcall(mid_pci_init);
