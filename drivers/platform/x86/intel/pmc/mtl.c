// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains platform specific structure definitions
 * and init function used by Meteor Lake PCH.
 *
 * Copyright (c) 2022, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include <linux/pci.h>
#include "core.h"

const struct pmc_reg_map mtl_reg_map = {
	.pfear_sts = ext_tgl_pfear_map,
	.slp_s0_offset = CNP_PMC_SLP_S0_RES_COUNTER_OFFSET,
	.slp_s0_res_counter_step = TGL_PMC_SLP_S0_RES_COUNTER_STEP,
	.ltr_show_sts = adl_ltr_show_map,
	.msr_sts = msr_map,
	.ltr_ignore_offset = CNP_PMC_LTR_IGNORE_OFFSET,
	.regmap_length = CNP_PMC_MMIO_REG_LEN,
	.ppfear0_offset = CNP_PMC_HOST_PPFEAR0A,
	.ppfear_buckets = ICL_PPFEAR_NUM_ENTRIES,
	.pm_cfg_offset = CNP_PMC_PM_CFG_OFFSET,
	.pm_read_disable_bit = CNP_PMC_READ_DISABLE_BIT,
	.ltr_ignore_max = ADL_NUM_IP_IGN_ALLOWED,
	.lpm_num_modes = ADL_LPM_NUM_MODES,
	.lpm_num_maps = ADL_LPM_NUM_MAPS,
	.lpm_res_counter_step_x2 = TGL_PMC_LPM_RES_COUNTER_STEP_X2,
	.etr3_offset = ETR3_OFFSET,
	.lpm_sts_latch_en_offset = MTL_LPM_STATUS_LATCH_EN_OFFSET,
	.lpm_priority_offset = MTL_LPM_PRI_OFFSET,
	.lpm_en_offset = MTL_LPM_EN_OFFSET,
	.lpm_residency_offset = MTL_LPM_RESIDENCY_OFFSET,
	.lpm_sts = adl_lpm_maps,
	.lpm_status_offset = MTL_LPM_STATUS_OFFSET,
	.lpm_live_status_offset = MTL_LPM_LIVE_STATUS_OFFSET,
};

void mtl_core_configure(struct pmc_dev *pmcdev)
{
	/* Due to a hardware limitation, the GBE LTR blocks PC10
	 * when a cable is attached. Tell the PMC to ignore it.
	 */
	dev_dbg(&pmcdev->pdev->dev, "ignoring GBE LTR\n");
	pmc_core_send_ltr_ignore(pmcdev, 3);
}

#define MTL_GNA_PCI_DEV	0x7e4c
#define MTL_IPU_PCI_DEV	0x7d19
#define MTL_VPU_PCI_DEV	0x7d1d
static void mtl_set_device_d3(unsigned int device)
{
	struct pci_dev *pcidev;

	pcidev = pci_get_device(PCI_VENDOR_ID_INTEL, device, NULL);
	if (pcidev) {
		if (!device_trylock(&pcidev->dev)) {
			pci_dev_put(pcidev);
			return;
		}
		if (!pcidev->dev.driver) {
			dev_info(&pcidev->dev, "Setting to D3hot\n");
			pci_set_power_state(pcidev, PCI_D3hot);
		}
		device_unlock(&pcidev->dev);
		pci_dev_put(pcidev);
	}
}

void mtl_core_init(struct pmc_dev *pmcdev)
{
	pmcdev->map = &mtl_reg_map;
	pmcdev->core_configure = mtl_core_configure;

	/*
	 * Set power state of select devices that do not have drivers to D3
	 * so that they do not block Package C entry.
	 */
	mtl_set_device_d3(MTL_GNA_PCI_DEV);
	mtl_set_device_d3(MTL_IPU_PCI_DEV);
	mtl_set_device_d3(MTL_VPU_PCI_DEV);
}
