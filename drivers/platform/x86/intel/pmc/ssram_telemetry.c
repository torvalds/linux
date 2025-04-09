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
#include <linux/intel_vsec.h>
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

DEFINE_FREE(pmc_core_iounmap, void __iomem *, if (_T) iounmap(_T))

static void
pmc_add_pmt(struct pmc_dev *pmcdev, u64 ssram_base, void __iomem *ssram)
{
	struct pci_dev *pcidev = pmcdev->ssram_pcidev;
	struct intel_vsec_platform_info info = {};
	struct intel_vsec_header *headers[2] = {};
	struct intel_vsec_header header;
	void __iomem *dvsec;
	u32 dvsec_offset;
	u32 table, hdr;

	dvsec_offset = readl(ssram + SSRAM_DVSEC_OFFSET);
	dvsec = ioremap(ssram_base + dvsec_offset, SSRAM_DVSEC_SIZE);
	if (!dvsec)
		return;

	hdr = readl(dvsec + PCI_DVSEC_HEADER1);
	header.id = readw(dvsec + PCI_DVSEC_HEADER2);
	header.rev = PCI_DVSEC_HEADER1_REV(hdr);
	header.length = PCI_DVSEC_HEADER1_LEN(hdr);
	header.num_entries = readb(dvsec + INTEL_DVSEC_ENTRIES);
	header.entry_size = readb(dvsec + INTEL_DVSEC_SIZE);

	table = readl(dvsec + INTEL_DVSEC_TABLE);
	header.tbir = INTEL_DVSEC_TABLE_BAR(table);
	header.offset = INTEL_DVSEC_TABLE_OFFSET(table);
	iounmap(dvsec);

	headers[0] = &header;
	info.caps = VSEC_CAP_TELEMETRY;
	info.headers = headers;
	info.base_addr = ssram_base;
	info.parent = &pmcdev->pdev->dev;

	intel_vsec_register(pcidev, &info);
}

static inline u64 get_base(void __iomem *addr, u32 offset)
{
	return lo_hi_readq(addr + offset) & GENMASK_ULL(63, 3);
}

static int
pmc_core_ssram_get_pmc(struct pmc_dev *pmcdev, unsigned int pmc_idx, u32 offset)
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
	if (!tmp_ssram)
		return -ENOMEM;

	if (pmc_idx != PMC_IDX_MAIN) {
		/*
		 * The secondary PMC BARS (which are behind hidden PCI devices)
		 * are read from fixed offsets in MMIO of the primary PMC BAR.
		 * If a device is not present, the value will be 0.
		 */
		ssram_base = get_base(tmp_ssram, offset);
		if (!ssram_base)
			return 0;

		ssram = ioremap(ssram_base, SSRAM_HDR_SIZE);
		if (!ssram)
			return -ENOMEM;

	} else {
		ssram = no_free_ptr(tmp_ssram);
	}

	pwrm_base = get_base(ssram, SSRAM_PWRM_OFFSET);
	devid = readw(ssram + SSRAM_DEVID_OFFSET);

	/* Find and register and PMC telemetry entries */
	pmc_add_pmt(pmcdev, ssram_base, ssram);

	map = pmc_core_find_regmap(pmcdev->regmap_list, devid);
	if (!map)
		return -ENODEV;

	return pmc_core_pmc_add(pmcdev, pwrm_base, map, pmc_idx);
}

int pmc_core_ssram_init(struct pmc_dev *pmcdev, int func)
{
	struct pci_dev *pcidev;
	int ret;

	pcidev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(20, func));
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
MODULE_IMPORT_NS("INTEL_VSEC");
