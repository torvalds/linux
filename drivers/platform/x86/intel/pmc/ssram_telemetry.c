// SPDX-License-Identifier: GPL-2.0
/*
 * Intel PMC SSRAM TELEMETRY PCI Driver
 *
 * Copyright (c) 2023, Intel Corporation.
 */

#include <linux/cleanup.h>
#include <linux/intel_vsec.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <linux/io-64-nonatomic-lo-hi.h>

#include "core.h"
#include "ssram_telemetry.h"

#define SSRAM_HDR_SIZE		0x100
#define SSRAM_PWRM_OFFSET	0x14
#define SSRAM_DVSEC_OFFSET	0x1C
#define SSRAM_DVSEC_SIZE	0x10
#define SSRAM_PCH_OFFSET	0x60
#define SSRAM_IOE_OFFSET	0x68
#define SSRAM_DEVID_OFFSET	0x70

DEFINE_FREE(pmc_ssram_telemetry_iounmap, void __iomem *, if (_T) iounmap(_T))

static struct pmc_ssram_telemetry *pmc_ssram_telems;
static bool device_probed;

static int
pmc_ssram_telemetry_add_pmt(struct pci_dev *pcidev, u64 ssram_base, void __iomem *ssram)
{
	struct intel_vsec_platform_info info = {};
	struct intel_vsec_header *headers[2] = {};
	struct intel_vsec_header header;
	void __iomem *dvsec;
	u32 dvsec_offset;
	u32 table, hdr;

	dvsec_offset = readl(ssram + SSRAM_DVSEC_OFFSET);
	dvsec = ioremap(ssram_base + dvsec_offset, SSRAM_DVSEC_SIZE);
	if (!dvsec)
		return -ENOMEM;

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
	info.parent = &pcidev->dev;

	return intel_vsec_register(pcidev, &info);
}

static inline u64 get_base(void __iomem *addr, u32 offset)
{
	return lo_hi_readq(addr + offset) & GENMASK_ULL(63, 3);
}

static int
pmc_ssram_telemetry_get_pmc(struct pci_dev *pcidev, unsigned int pmc_idx, u32 offset)
{
	void __iomem __free(pmc_ssram_telemetry_iounmap) *tmp_ssram = NULL;
	void __iomem __free(pmc_ssram_telemetry_iounmap) *ssram = NULL;
	u64 ssram_base, pwrm_base;
	u16 devid;

	ssram_base = pci_resource_start(pcidev, 0);
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

	pmc_ssram_telems[pmc_idx].devid = devid;
	pmc_ssram_telems[pmc_idx].base_addr = pwrm_base;

	/* Find and register and PMC telemetry entries */
	return pmc_ssram_telemetry_add_pmt(pcidev, ssram_base, ssram);
}

/**
 * pmc_ssram_telemetry_get_pmc_info() - Get a PMC devid and base_addr information
 * @pmc_idx:               Index of the PMC
 * @pmc_ssram_telemetry:   pmc_ssram_telemetry structure to store the PMC information
 *
 * Return:
 * * 0           - Success
 * * -EAGAIN     - Probe function has not finished yet. Try again.
 * * -EINVAL     - Invalid pmc_idx
 * * -ENODEV     - PMC device is not available
 */
int pmc_ssram_telemetry_get_pmc_info(unsigned int pmc_idx,
				     struct pmc_ssram_telemetry *pmc_ssram_telemetry)
{
	/*
	 * PMCs are discovered in probe function. If this function is called before
	 * probe function complete, the result would be invalid. Use device_probed
	 * variable to avoid this case. Return -EAGAIN to inform the consumer to call
	 * again later.
	 */
	if (!device_probed)
		return -EAGAIN;

	/*
	 * Memory barrier is used to ensure the correct read order between
	 * device_probed variable and PMC info.
	 */
	smp_rmb();
	if (pmc_idx >= MAX_NUM_PMC)
		return -EINVAL;

	if (!pmc_ssram_telems || !pmc_ssram_telems[pmc_idx].devid)
		return -ENODEV;

	pmc_ssram_telemetry->devid = pmc_ssram_telems[pmc_idx].devid;
	pmc_ssram_telemetry->base_addr = pmc_ssram_telems[pmc_idx].base_addr;
	return 0;
}
EXPORT_SYMBOL_GPL(pmc_ssram_telemetry_get_pmc_info);

static int intel_pmc_ssram_telemetry_probe(struct pci_dev *pcidev, const struct pci_device_id *id)
{
	int ret;

	pmc_ssram_telems = devm_kzalloc(&pcidev->dev, sizeof(*pmc_ssram_telems) * MAX_NUM_PMC,
					GFP_KERNEL);
	if (!pmc_ssram_telems) {
		ret = -ENOMEM;
		goto probe_finish;
	}

	ret = pcim_enable_device(pcidev);
	if (ret) {
		dev_dbg(&pcidev->dev, "failed to enable PMC SSRAM device\n");
		goto probe_finish;
	}

	ret = pmc_ssram_telemetry_get_pmc(pcidev, PMC_IDX_MAIN, 0);
	if (ret)
		goto probe_finish;

	pmc_ssram_telemetry_get_pmc(pcidev, PMC_IDX_IOE, SSRAM_IOE_OFFSET);
	pmc_ssram_telemetry_get_pmc(pcidev, PMC_IDX_PCH, SSRAM_PCH_OFFSET);

probe_finish:
	/*
	 * Memory barrier is used to ensure the correct write order between PMC info
	 * and device_probed variable.
	 */
	smp_wmb();
	device_probed = true;
	return ret;
}

static const struct pci_device_id intel_pmc_ssram_telemetry_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_MTL_SOCM) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_ARL_SOCS) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_ARL_SOCM) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_LNL_SOCM) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_PTL_PCDH) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_PTL_PCDP) },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_WCL_PCDN) },
	{ }
};
MODULE_DEVICE_TABLE(pci, intel_pmc_ssram_telemetry_pci_ids);

static struct pci_driver intel_pmc_ssram_telemetry_driver = {
	.name = "intel_pmc_ssram_telemetry",
	.id_table = intel_pmc_ssram_telemetry_pci_ids,
	.probe = intel_pmc_ssram_telemetry_probe,
};
module_pci_driver(intel_pmc_ssram_telemetry_driver);

MODULE_IMPORT_NS("INTEL_VSEC");
MODULE_AUTHOR("Xi Pardee <xi.pardee@intel.com>");
MODULE_DESCRIPTION("Intel PMC SSRAM Telemetry driver");
MODULE_LICENSE("GPL");
