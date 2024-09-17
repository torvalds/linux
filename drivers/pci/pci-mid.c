// SPDX-License-Identifier: GPL-2.0
/*
 * Intel MID platform PM support
 *
 * Copyright (C) 2016, Intel Corporation
 *
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/init.h>
#include <linux/pci.h>

#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/intel-mid.h>

#include "pci.h"

static bool pci_mid_pm_enabled __read_mostly;

bool pci_use_mid_pm(void)
{
	return pci_mid_pm_enabled;
}

int mid_pci_set_power_state(struct pci_dev *pdev, pci_power_t state)
{
	return intel_mid_pci_set_power_state(pdev, state);
}

pci_power_t mid_pci_get_power_state(struct pci_dev *pdev)
{
	return intel_mid_pci_get_power_state(pdev);
}

/*
 * This table should be in sync with the one in
 * arch/x86/platform/intel-mid/pwr.c.
 */
static const struct x86_cpu_id lpss_cpu_ids[] = {
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_SALTWELL_MID, NULL),
	X86_MATCH_INTEL_FAM6_MODEL(ATOM_SILVERMONT_MID, NULL),
	{}
};

static int __init mid_pci_init(void)
{
	const struct x86_cpu_id *id;

	id = x86_match_cpu(lpss_cpu_ids);
	if (id)
		pci_mid_pm_enabled = true;

	return 0;
}
arch_initcall(mid_pci_init);
