// SPDX-License-Identifier: GPL-2.0
/*
 * Intel PMC SSRAM TELEMETRY PCI Driver
 *
 * Copyright (c) 2023, Intel Corporation.
 */

#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/device.h>
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
#define SSRAM_BASE_ADDR_MASK	GENMASK_ULL(63, 3)
#define SSRAM_PCI_PMC_MASK	(BIT(PMC_IDX_MAIN) | BIT(PMC_IDX_IOE) | BIT(PMC_IDX_PCH))

DEFINE_FREE(pmc_ssram_telemetry_iounmap, void __iomem *, if (_T) iounmap(_T))

enum resource_method {
	RES_METHOD_PCI,
	RES_METHOD_ACPI,
};

struct ssram_type {
	enum resource_method method;
	enum pmc_index p_index;
};

static const struct ssram_type pci_main = {
	.method = RES_METHOD_PCI,
	.p_index = PMC_IDX_MAIN,
};

static const struct ssram_type acpi_main = {
	.method = RES_METHOD_ACPI,
	.p_index = PMC_IDX_MAIN,
};

static const struct ssram_type acpi_pch = {
	.method = RES_METHOD_ACPI,
	.p_index = PMC_IDX_PCH,
};

enum pmc_ssram_state {
	PMC_SSRAM_UNPROBED = 0,
	PMC_SSRAM_PROBING,
	PMC_SSRAM_PRESENT,
	PMC_SSRAM_ABSENT,
};

static enum pmc_ssram_state pmc_ssram_state[MAX_NUM_PMC];
static struct pmc_ssram_telemetry pmc_ssram_telems[MAX_NUM_PMC];

struct pmc_ssram_probe_cache {
	/* Per-index values staged by probe before publishing globally. */
	struct pmc_ssram_telemetry telems[MAX_NUM_PMC];
	/* PMCs this probe instance is responsible for publishing. */
	unsigned long owned_mask;
	/* Subset of owned_mask that was discovered successfully. */
	unsigned long valid_mask;
};

struct pmc_ssram_drvdata {
	/* PMCs published by this bound device; used to unpublish on remove. */
	unsigned long owned_mask;
};

static inline u64 get_base(void __iomem *addr, u32 offset)
{
	return lo_hi_readq(addr + offset) & SSRAM_BASE_ADDR_MASK;
}

static void pmc_ssram_get_devid_pwrmbase(struct pmc_ssram_probe_cache *probe_cache,
					 void __iomem *ssram, unsigned int pmc_idx)
{
	u64 pwrm_base;
	u16 devid;

	pwrm_base = get_base(ssram, SSRAM_PWRM_OFFSET);
	devid = readw(ssram + SSRAM_DEVID_OFFSET);

	probe_cache->telems[pmc_idx].base_addr = pwrm_base;
	probe_cache->telems[pmc_idx].devid = devid;
}

static void pmc_ssram_publish_absent(unsigned int pmc_idx)
{
	/*
	 * Publish only the state without modifying base_addr and devid. This
	 * lets a reader that already observed PRESENT finish copying the
	 * previous values even if unbind concurrently publishes ABSENT. Readers
	 * that observe ABSENT return -ENODEV without accessing data.
	 */
	smp_store_release(&pmc_ssram_state[pmc_idx], PMC_SSRAM_ABSENT);
}

static void pmc_ssram_publish_present(struct pmc_ssram_probe_cache *probe_cache,
				      unsigned int pmc_idx)
{
	/*
	 * The devid and base_addr fields are read from hardware MMIO registers
	 * whose values are stable for a given PMC index. A reader that observed
	 * PRESENT from an earlier probe can safely copy them while a concurrent
	 * rebind republishes those fields because both probes read the same
	 * hardware values.
	 */
	pmc_ssram_telems[pmc_idx] = probe_cache->telems[pmc_idx];
	/*
	 * Publish base_addr and devid before publishing PRESENT. Pairs with the
	 * acquire load in the reader that consumes them after observing PRESENT.
	 */
	smp_store_release(&pmc_ssram_state[pmc_idx], PMC_SSRAM_PRESENT);
}

static void pmc_ssram_mark_probing(unsigned long mask)
{
	unsigned long bit;

	for_each_set_bit(bit, &mask, MAX_NUM_PMC)
		WRITE_ONCE(pmc_ssram_state[bit], PMC_SSRAM_PROBING);
}

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

	return intel_vsec_register(&pcidev->dev, &info);
}

static int
pmc_ssram_telemetry_get_pmc_pci(struct pci_dev *pcidev,
				struct pmc_ssram_probe_cache *probe_cache,
				unsigned int pmc_idx, u32 offset)
{
	void __iomem __free(pmc_ssram_telemetry_iounmap) *tmp_ssram = NULL;
	void __iomem __free(pmc_ssram_telemetry_iounmap) *ssram = NULL;
	u64 ssram_base;
	int ret;

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

	pmc_ssram_get_devid_pwrmbase(probe_cache, ssram, pmc_idx);

	/* Find and register and PMC telemetry entries */
	ret = pmc_ssram_telemetry_add_pmt(pcidev, ssram_base, ssram);
	if (ret)
		dev_warn(&pcidev->dev, "could not register PMT\n");

	probe_cache->valid_mask |= BIT(pmc_idx);

	return 0;
}

static int pmc_ssram_telemetry_pci_init(struct pci_dev *pcidev,
					struct pmc_ssram_probe_cache *probe_cache)
{
	int ret;

	ret = pmc_ssram_telemetry_get_pmc_pci(pcidev, probe_cache, PMC_IDX_MAIN, 0);
	if (ret)
		return ret;

	/*
	 * If MAIN PMC enumeration is successful but either IOE or PCH fail,
	 * don't fail probe as the MAIN PMC is still useful as it provides the
	 * global reset and slp_s0 counter access. Failed or missing secondary
	 * PMCs are left out of valid_mask and published as absent.
	 */
	pmc_ssram_telemetry_get_pmc_pci(pcidev, probe_cache, PMC_IDX_IOE,
					SSRAM_IOE_OFFSET);

	pmc_ssram_telemetry_get_pmc_pci(pcidev, probe_cache, PMC_IDX_PCH,
					SSRAM_PCH_OFFSET);

	return 0;
}

static int pmc_ssram_telemetry_get_pmc_acpi(struct pci_dev *pcidev,
					    struct pmc_ssram_probe_cache *probe_cache,
					    unsigned int pmc_idx)
{
	u64 ssram_base;

	ssram_base = pci_resource_start(pcidev, 0);
	if (!ssram_base)
		return -ENODEV;

	void __iomem __free(pmc_ssram_telemetry_iounmap) *ssram =
		ioremap(ssram_base, SSRAM_HDR_SIZE);
	if (!ssram)
		return -ENOMEM;

	pmc_ssram_get_devid_pwrmbase(probe_cache, ssram, pmc_idx);
	probe_cache->valid_mask |= BIT(pmc_idx);

	return 0;
}

static int pmc_ssram_telemetry_acpi_init(struct pci_dev *pcidev,
					 struct pmc_ssram_probe_cache *probe_cache,
					 enum pmc_index index)
{
	struct intel_vsec_header header;
	struct intel_vsec_header *headers[2] = { &header, NULL };
	struct acpi_buffer buf = { ACPI_ALLOCATE_BUFFER, NULL };
	struct intel_vsec_platform_info info = { };
	union acpi_object *dsd;
	acpi_handle handle;
	acpi_status status;
	int ret;

	handle = ACPI_HANDLE(&pcidev->dev);
	if (!handle)
		return -ENODEV;

	status = acpi_evaluate_object(handle, "_DSD", NULL, &buf);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	void *dsd_buf __free(pmc_acpi_free) = buf.pointer;

	dsd = pmc_find_telem_guid(dsd_buf);
	if (!dsd)
		return -ENODEV;

	acpi_disc_t disc __free(kfree) = pmc_parse_telem_dsd(dsd, &header);
	if (IS_ERR(disc))
		return PTR_ERR(disc);

	info.headers = headers;
	info.caps = VSEC_CAP_TELEMETRY;
	info.acpi_disc = disc;
	info.src = INTEL_VSEC_DISC_ACPI;

	/* This is an ACPI companion device. PCI BAR will be used for base addr. */
	info.base_addr = 0;

	ret = intel_vsec_register(&pcidev->dev, &info);
	if (ret)
		dev_warn(&pcidev->dev, "could not register PMT\n");

	return pmc_ssram_telemetry_get_pmc_acpi(pcidev, probe_cache, index);
}

/**
 * pmc_ssram_telemetry_get_pmc_info() - Get a PMC devid and base_addr information
 * @pmc_idx:               Index of the PMC
 * @pmc_ssram_telemetry:   pmc_ssram_telemetry structure to store the PMC information
 *
 * State flow per PMC index:
 * - PMC_SSRAM_UNPROBED: Probe has not started for this index.
 * - PMC_SSRAM_PROBING:  Probe is currently discovering data for this index.
 * - PMC_SSRAM_PRESENT:  base_addr/devid were published and can be read.
 * - PMC_SSRAM_ABSENT:   No data is available for this index.
 *
 * Readers use an acquire load of the state. If state is PRESENT, reads of
 * base_addr/devid are ordered after the state observation and pair with the
 * writer's release-store when publishing PRESENT.
 *
 * Return:
 * * 0           - Success
 * * -EAGAIN     - Probe function has not finished yet. Try again.
 * * -EINVAL     - Invalid pmc_idx
 * * -ENODEV     - PMC device is not available (hardware absent or driver failed to initialize)
 */
int pmc_ssram_telemetry_get_pmc_info(unsigned int pmc_idx,
				     struct pmc_ssram_telemetry *pmc_ssram_telemetry)
{
	enum pmc_ssram_state state;

	if (pmc_idx >= MAX_NUM_PMC)
		return -EINVAL;

	/*
	 * PMCs are discovered in probe function. If this function is called before
	 * probe function complete, the result would be invalid. Use per-PMC state
	 * to inform the consumer to call again later.
	 */
	state = smp_load_acquire(&pmc_ssram_state[pmc_idx]);
	if (state == PMC_SSRAM_UNPROBED || state == PMC_SSRAM_PROBING)
		return -EAGAIN;

	if (state == PMC_SSRAM_ABSENT)
		return -ENODEV;

	/*
	 * Acquire semantics order reads of devid and base_addr after observing
	 * PRESENT and pair with the writer's release-store.
	 */
	pmc_ssram_telemetry->devid = pmc_ssram_telems[pmc_idx].devid;
	pmc_ssram_telemetry->base_addr = pmc_ssram_telems[pmc_idx].base_addr;
	return 0;
}
EXPORT_SYMBOL_GPL(pmc_ssram_telemetry_get_pmc_info);

static void pmc_ssram_publish_absent_mask(unsigned long mask)
{
	unsigned long bit;

	for_each_set_bit(bit, &mask, MAX_NUM_PMC)
		pmc_ssram_publish_absent(bit);
}

static void pmc_ssram_publish_telems(struct pmc_ssram_probe_cache *probe_cache, int ret)
{
	unsigned long bit;

	/* If probe failed, all owned indexes become absent for readers. */
	if (ret) {
		pmc_ssram_publish_absent_mask(probe_cache->owned_mask);
		return;
	}

	/* Publish each owned index independently based on discovery result. */
	for_each_set_bit(bit, &probe_cache->owned_mask, MAX_NUM_PMC) {
		if (probe_cache->valid_mask & BIT(bit))
			pmc_ssram_publish_present(probe_cache, bit);
		else
			pmc_ssram_publish_absent(bit);
	}
}

static int pmc_ssram_telemetry_probe(struct pci_dev *pcidev, const struct pci_device_id *id)
{
	struct pmc_ssram_probe_cache probe_cache = {};
	struct pmc_ssram_drvdata *drvdata;
	const struct ssram_type *ssram_type;
	enum resource_method method;
	enum pmc_index index;
	int ret;

	ssram_type = (const struct ssram_type *)id->driver_data;
	if (!ssram_type) {
		dev_dbg(&pcidev->dev, "missing driver data\n");
		return -EINVAL;
	}

	index = ssram_type->p_index;
	method = ssram_type->method;
	if (method == RES_METHOD_PCI)
		probe_cache.owned_mask = SSRAM_PCI_PMC_MASK;
	else if (method == RES_METHOD_ACPI)
		probe_cache.owned_mask = BIT(index);
	else
		return -EINVAL;

	pmc_ssram_mark_probing(probe_cache.owned_mask);

	drvdata = devm_kzalloc(&pcidev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata) {
		ret = -ENOMEM;
		goto probe_finish;
	}

	ret = pcim_enable_device(pcidev);
	if (ret) {
		dev_dbg(&pcidev->dev, "failed to enable PMC SSRAM device\n");
		goto probe_finish;
	}

	if (method == RES_METHOD_PCI)
		ret = pmc_ssram_telemetry_pci_init(pcidev, &probe_cache);
	else if (method == RES_METHOD_ACPI)
		ret = pmc_ssram_telemetry_acpi_init(pcidev, &probe_cache, index);
	else
		ret = -EINVAL;

	if (!ret) {
		drvdata->owned_mask = probe_cache.owned_mask;
		pci_set_drvdata(pcidev, drvdata);
	}

probe_finish:
	pmc_ssram_publish_telems(&probe_cache, ret);

	return ret;
}

static void pmc_ssram_telemetry_remove(struct pci_dev *pcidev)
{
	struct pmc_ssram_drvdata *drvdata = pci_get_drvdata(pcidev);

	if (drvdata)
		pmc_ssram_publish_absent_mask(drvdata->owned_mask);
}

static const struct pci_device_id pmc_ssram_telemetry_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_MTL_SOCM),
		.driver_data = (kernel_ulong_t)&pci_main },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_ARL_SOCS),
		.driver_data = (kernel_ulong_t)&pci_main },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_ARL_SOCM),
		.driver_data = (kernel_ulong_t)&pci_main },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_LNL_SOCM),
		.driver_data = (kernel_ulong_t)&pci_main },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_PTL_PCDH),
		.driver_data = (kernel_ulong_t)&pci_main },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_PTL_PCDP),
		.driver_data = (kernel_ulong_t)&pci_main },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_WCL_PCDN),
		.driver_data = (kernel_ulong_t)&pci_main },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_NVL_PCDH),
		.driver_data = (kernel_ulong_t)&acpi_main },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_NVL_PCDS),
		.driver_data = (kernel_ulong_t)&acpi_main },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PMC_DEVID_NVL_PCHS),
		.driver_data = (kernel_ulong_t)&acpi_pch },
	{ }
};
MODULE_DEVICE_TABLE(pci, pmc_ssram_telemetry_pci_ids);

static struct pci_driver pmc_ssram_telemetry_driver = {
	.name = "intel_pmc_ssram_telemetry",
	.id_table = pmc_ssram_telemetry_pci_ids,
	.probe = pmc_ssram_telemetry_probe,
	.remove = pmc_ssram_telemetry_remove,
};
module_pci_driver(pmc_ssram_telemetry_driver);

MODULE_IMPORT_NS("INTEL_VSEC");
MODULE_IMPORT_NS("INTEL_PMC_CORE");
MODULE_AUTHOR("Xi Pardee <xi.pardee@intel.com>");
MODULE_DESCRIPTION("Intel PMC SSRAM Telemetry driver");
MODULE_LICENSE("GPL");
