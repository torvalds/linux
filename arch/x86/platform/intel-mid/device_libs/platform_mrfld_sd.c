// SPDX-License-Identifier: GPL-2.0-only
/*
 * SDHCI platform data initilisation file
 *
 * (C) Copyright 2016 Intel Corporation
 * Author: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 */

#include <linux/init.h>
#include <linux/pci.h>

#include <linux/mmc/sdhci-pci-data.h>

#include <asm/intel-mid.h>

#define INTEL_MRFLD_SD			2
#define INTEL_MRFLD_SD_CD_GPIO		77

static struct sdhci_pci_data mrfld_sdhci_pci_data = {
	.rst_n_gpio	= -EINVAL,
	.cd_gpio	= INTEL_MRFLD_SD_CD_GPIO,
};

static struct sdhci_pci_data *
mrfld_sdhci_pci_get_data(struct pci_dev *pdev, int slotno)
{
	unsigned int func = PCI_FUNC(pdev->devfn);

	if (func == INTEL_MRFLD_SD)
		return &mrfld_sdhci_pci_data;

	return NULL;
}

static int __init mrfld_sd_init(void)
{
	if (intel_mid_identify_cpu() != INTEL_MID_CPU_CHIP_TANGIER)
		return -ENODEV;

	sdhci_pci_get_data = mrfld_sdhci_pci_get_data;
	return 0;
}
arch_initcall(mrfld_sd_init);
