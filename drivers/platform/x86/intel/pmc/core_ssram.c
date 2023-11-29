// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains functions to handle discovery of PMC metrics located
 * in the PMC SSRAM PCI device.
 *
 * Copyright (c) 2023, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include <linux/cleanup.h>
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

DEFINE_FREE(pmc_core_iounmap, void __iomem *, iounmap(_T));

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

static int
pmc_core_pmc_add(struct pmc_dev *pmcdev, u64 pwrm_base,
		 const struct pmc_reg_map *reg_map, int pmc_index)
{
	struct pmc *pmc = pmcdev->pmcs[pmc_index];

	if (!pwrm_base)
		return -ENODEV;

	/* Memory for primary PMC has been allocated in core.c */
	if (!pmc) {
		pmc = devm_kzalloc(&pmcdev->pdev->dev, sizeof(*pmc), GFP_KERNEL);
		if (!pmc)
			return -ENOMEM;
	}

	pmc->map = reg_map;
	pmc->base_addr = pwrm_base;
	pmc->regbase = ioremap(pmc->base_addr, pmc->map->regmap_length);

	if (!pmc->regbase) {
		devm_kfree(&pmcdev->pdev->dev, pmc);
		return -ENOMEM;
	}

	pmcdev->pmcs[pmc_index] = pmc;

	return 0;
}

static int
pmc_core_ssram_get_pmc(struct pmc_dev *pmcdev, int pmc_idx, u32 offset)
{
	struct pci_dev *ssram_pcidev = pmcdev->ssram_pcidev;
	void __iomem __free(pmc_core_iounmap) *tmp_ssram = NULL;
	void __iomem __free(pmc_core_iounmap) *ssram = NULL;
	const struct pmc_reg_map *map;
	u64 ssram_base, pwrm_base;
	u16 devid;

	if (!pmcdev->regmap_list)
		return -ENOENT;

	ssram_base = ssram_pcidev->resource[0].start;
	tmp_ssram = ioremap(ssram_base, SSRAM_HDR_SIZE);

	if (pmc_idx != PMC_IDX_MAIN) {
		/*
		 * The secondary PMC BARS (which are behind hidden PCI devices)
		 * are read from fixed offsets in MMIO of the primary PMC BAR.
		 */
		ssram_base = get_base(tmp_ssram, offset);
		ssram = ioremap(ssram_base, SSRAM_HDR_SIZE);
		if (!ssram)
			return -ENOMEM;

	} else {
		ssram = no_free_ptr(tmp_ssram);
	}

	pwrm_base = get_base(ssram, SSRAM_PWRM_OFFSET);
	devid = readw(ssram + SSRAM_DEVID_OFFSET);

	map = pmc_core_find_regmap(pmcdev->regmap_list, devid);
	if (!map)
		return -ENODEV;

	return pmc_core_pmc_add(pmcdev, pwrm_base, map, PMC_IDX_MAIN);
}

int pmc_core_ssram_init(struct pmc_dev *pmcdev)
{
	struct pci_dev *pcidev;
	int ret;

	pcidev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(20, 2));
	if (!pcidev)
		return -ENODEV;

	ret = pcim_enable_device(pcidev);
	if (ret)
		goto release_dev;

	pmcdev->ssram_pcidev = pcidev;

	ret = pmc_core_ssram_get_pmc(pmcdev, PMC_IDX_MAIN, 0);
	if (ret)
		goto disable_dev;

	pmc_core_ssram_get_pmc(pmcdev, PMC_IDX_IOE, SSRAM_IOE_OFFSET);
	pmc_core_ssram_get_pmc(pmcdev, PMC_IDX_PCH, SSRAM_PCH_OFFSET);

	return 0;

disable_dev:
	pmcdev->ssram_pcidev = NULL;
	pci_disable_device(pcidev);
release_dev:
	pci_dev_put(pcidev);

	return ret;
}
