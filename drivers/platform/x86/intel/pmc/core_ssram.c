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
#include "../pmt/telemetry.h"

#define SSRAM_HDR_SIZE		0x100
#define SSRAM_PWRM_OFFSET	0x14
#define SSRAM_DVSEC_OFFSET	0x1C
#define SSRAM_DVSEC_SIZE	0x10
#define SSRAM_PCH_OFFSET	0x60
#define SSRAM_IOE_OFFSET	0x68
#define SSRAM_DEVID_OFFSET	0x70

/* PCH query */
#define LPM_HEADER_OFFSET	1
#define LPM_REG_COUNT		28
#define LPM_MODE_OFFSET		1

DEFINE_FREE(pmc_core_iounmap, void __iomem *, if (_T) iounmap(_T))

static u32 pmc_core_find_guid(struct pmc_info *list, const struct pmc_reg_map *map)
{
	for (; list->map; ++list)
		if (list->map == map)
			return list->guid;

	return 0;
}

static int pmc_core_get_lpm_req(struct pmc_dev *pmcdev, struct pmc *pmc)
{
	struct telem_endpoint *ep;
	const u8 *lpm_indices;
	int num_maps, mode_offset = 0;
	int ret, mode;
	int lpm_size;
	u32 guid;

	lpm_indices = pmc->map->lpm_reg_index;
	num_maps = pmc->map->lpm_num_maps;
	lpm_size = LPM_MAX_NUM_MODES * num_maps;

	guid = pmc_core_find_guid(pmcdev->regmap_list, pmc->map);
	if (!guid)
		return -ENXIO;

	ep = pmt_telem_find_and_register_endpoint(pmcdev->ssram_pcidev, guid, 0);
	if (IS_ERR(ep)) {
		dev_dbg(&pmcdev->pdev->dev, "couldn't get telem endpoint %ld",
			PTR_ERR(ep));
		return -EPROBE_DEFER;
	}

	pmc->lpm_req_regs = devm_kzalloc(&pmcdev->pdev->dev,
					 lpm_size * sizeof(u32),
					 GFP_KERNEL);
	if (!pmc->lpm_req_regs) {
		ret = -ENOMEM;
		goto unregister_ep;
	}

	/*
	 * PMC Low Power Mode (LPM) table
	 *
	 * In telemetry space, the LPM table contains a 4 byte header followed
	 * by 8 consecutive mode blocks (one for each LPM mode). Each block
	 * has a 4 byte header followed by a set of registers that describe the
	 * IP state requirements for the given mode. The IP mapping is platform
	 * specific but the same for each block, making for easy analysis.
	 * Platforms only use a subset of the space to track the requirements
	 * for their IPs. Callers provide the requirement registers they use as
	 * a list of indices. Each requirement register is associated with an
	 * IP map that's maintained by the caller.
	 *
	 * Header
	 * +----+----------------------------+----------------------------+
	 * |  0 |      REVISION              |      ENABLED MODES         |
	 * +----+--------------+-------------+-------------+--------------+
	 *
	 * Low Power Mode 0 Block
	 * +----+--------------+-------------+-------------+--------------+
	 * |  1 |     SUB ID   |     SIZE    |   MAJOR     |   MINOR      |
	 * +----+--------------+-------------+-------------+--------------+
	 * |  2 |           LPM0 Requirements 0                           |
	 * +----+---------------------------------------------------------+
	 * |    |                  ...                                    |
	 * +----+---------------------------------------------------------+
	 * | 29 |           LPM0 Requirements 27                          |
	 * +----+---------------------------------------------------------+
	 *
	 * ...
	 *
	 * Low Power Mode 7 Block
	 * +----+--------------+-------------+-------------+--------------+
	 * |    |     SUB ID   |     SIZE    |   MAJOR     |   MINOR      |
	 * +----+--------------+-------------+-------------+--------------+
	 * | 60 |           LPM7 Requirements 0                           |
	 * +----+---------------------------------------------------------+
	 * |    |                  ...                                    |
	 * +----+---------------------------------------------------------+
	 * | 87 |           LPM7 Requirements 27                          |
	 * +----+---------------------------------------------------------+
	 *
	 */
	mode_offset = LPM_HEADER_OFFSET + LPM_MODE_OFFSET;
	pmc_for_each_mode(mode, pmcdev) {
		u32 *req_offset = pmc->lpm_req_regs + (mode * num_maps);
		int m;

		for (m = 0; m < num_maps; m++) {
			u8 sample_id = lpm_indices[m] + mode_offset;

			ret = pmt_telem_read32(ep, sample_id, req_offset, 1);
			if (ret) {
				dev_err(&pmcdev->pdev->dev,
					"couldn't read Low Power Mode requirements: %d\n", ret);
				devm_kfree(&pmcdev->pdev->dev, pmc->lpm_req_regs);
				goto unregister_ep;
			}
			++req_offset;
		}
		mode_offset += LPM_REG_COUNT + LPM_MODE_OFFSET;
	}

unregister_ep:
	pmt_telem_unregister_endpoint(ep);

	return ret;
}

int pmc_core_ssram_get_lpm_reqs(struct pmc_dev *pmcdev)
{
	int ret, i;

	if (!pmcdev->ssram_pcidev)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(pmcdev->pmcs); ++i) {
		if (!pmcdev->pmcs[i])
			continue;

		ret = pmc_core_get_lpm_req(pmcdev, pmcdev->pmcs[i]);
		if (ret)
			return ret;
	}

	return 0;
}

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

	ssram = ioremap(ssram_base, SSRAM_HDR_SIZE);
	if (!ssram)
		return;

	dvsec_offset = readl(ssram + SSRAM_DVSEC_OFFSET);
	iounmap(ssram);

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
MODULE_IMPORT_NS(INTEL_VSEC);
MODULE_IMPORT_NS(INTEL_PMT_TELEMETRY);
