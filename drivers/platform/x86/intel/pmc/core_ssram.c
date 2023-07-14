// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains functions to handle discovery of PMC metrics located
 * in the PMC SSRAM PCI device.
 *
 * Copyright (c) 2023, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include <linux/pci.h>
#include <linux/io-64-nonatomic-lo-hi.h>

#include "core.h"

#define SSRAM_HDR_SIZE		0x100
#define SSRAM_PWRM_OFFSET	0x14
#define SSRAM_DVSEC_OFFSET	0x1C
#define SSRAM_DVSEC_SIZE	0x10
#define SSRAM_PCH_OFFSET	0x60
#define SSRAM_IOE_OFFSET	0x68
#define SSRAM_DEVID_OFFSET	0x70

static const struct pmc_reg_map *pmc_core_find_regmap(struct pmc_info *list, u16 devid)
{
	for (; list->map; ++list)
		if (devid == list->devid)
			return list->map;

	return NULL;
}

static inline u64 get_base(void __iomem *addr, u32 offset)
{
	return lo_hi_readq(addr + offset) & GENMASK_ULL(63, 3);
}

static void
pmc_core_pmc_add(struct pmc_dev *pmcdev, u64 pwrm_base,
		 const struct pmc_reg_map *reg_map, int pmc_index)
{
	struct pmc *pmc = pmcdev->pmcs[pmc_index];

	if (!pwrm_base)
		return;

	/* Memory for primary PMC has been allocated in core.c */
	if (!pmc) {
		pmc = devm_kzalloc(&pmcdev->pdev->dev, sizeof(*pmc), GFP_KERNEL);
		if (!pmc)
			return;
	}

	pmc->map = reg_map;
	pmc->base_addr = pwrm_base;
	pmc->regbase = ioremap(pmc->base_addr, pmc->map->regmap_length);

	if (!pmc->regbase) {
		devm_kfree(&pmcdev->pdev->dev, pmc);
		return;
	}

	pmcdev->pmcs[pmc_index] = pmc;
}

static void
pmc_core_ssram_get_pmc(struct pmc_dev *pmcdev, void __iomem *ssram, u32 offset,
		       int pmc_idx)
{
	u64 pwrm_base;
	u16 devid;

	if (pmc_idx != PMC_IDX_SOC) {
		u64 ssram_base = get_base(ssram, offset);

		if (!ssram_base)
			return;

		ssram = ioremap(ssram_base, SSRAM_HDR_SIZE);
		if (!ssram)
			return;
	}

	pwrm_base = get_base(ssram, SSRAM_PWRM_OFFSET);
	devid = readw(ssram + SSRAM_DEVID_OFFSET);

	if (pmcdev->regmap_list) {
		const struct pmc_reg_map *map;

		map = pmc_core_find_regmap(pmcdev->regmap_list, devid);
		if (map)
			pmc_core_pmc_add(pmcdev, pwrm_base, map, pmc_idx);
	}

	if (pmc_idx != PMC_IDX_SOC)
		iounmap(ssram);
}

void pmc_core_ssram_init(struct pmc_dev *pmcdev)
{
	void __iomem *ssram;
	struct pci_dev *pcidev;
	u64 ssram_base;
	int ret;

	pcidev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(20, 2));
	if (!pcidev)
		goto out;

	ret = pcim_enable_device(pcidev);
	if (ret)
		goto release_dev;

	ssram_base = pcidev->resource[0].start;
	ssram = ioremap(ssram_base, SSRAM_HDR_SIZE);
	if (!ssram)
		goto disable_dev;

	pmcdev->ssram_pcidev = pcidev;

	pmc_core_ssram_get_pmc(pmcdev, ssram, 0, PMC_IDX_SOC);
	pmc_core_ssram_get_pmc(pmcdev, ssram, SSRAM_IOE_OFFSET, PMC_IDX_IOE);
	pmc_core_ssram_get_pmc(pmcdev, ssram, SSRAM_PCH_OFFSET, PMC_IDX_PCH);

	iounmap(ssram);
out:
	return;

disable_dev:
	pci_disable_device(pcidev);
release_dev:
	pci_dev_put(pcidev);
}
