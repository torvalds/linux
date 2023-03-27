// SPDX-License-Identifier: GPL-2.0-only
/*
 * intel-tpmi : Driver to enumerate TPMI features and create devices
 *
 * Copyright (c) 2023, Intel Corporation.
 * All Rights Reserved.
 *
 * The TPMI (Topology Aware Register and PM Capsule Interface) provides a
 * flexible, extendable and PCIe enumerable MMIO interface for PM features.
 *
 * For example Intel RAPL (Running Average Power Limit) provides a MMIO
 * interface using TPMI. This has advantage over traditional MSR
 * (Model Specific Register) interface, where a thread needs to be scheduled
 * on the target CPU to read or write. Also the RAPL features vary between
 * CPU models, and hence lot of model specific code. Here TPMI provides an
 * architectural interface by providing hierarchical tables and fields,
 * which will not need any model specific implementation.
 *
 * The TPMI interface uses a PCI VSEC structure to expose the location of
 * MMIO region.
 *
 * This VSEC structure is present in the PCI configuration space of the
 * Intel Out-of-Band (OOB) device, which  is handled by the Intel VSEC
 * driver. The Intel VSEC driver parses VSEC structures present in the PCI
 * configuration space of the given device and creates an auxiliary device
 * object for each of them. In particular, it creates an auxiliary device
 * object representing TPMI that can be bound by an auxiliary driver.
 *
 * This TPMI driver will bind to the TPMI auxiliary device object created
 * by the Intel VSEC driver.
 *
 * The TPMI specification defines a PFS (PM Feature Structure) table.
 * This table is present in the TPMI MMIO region. The starting address
 * of PFS is derived from the tBIR (Bar Indicator Register) and "Address"
 * field from the VSEC header.
 *
 * Each TPMI PM feature has one entry in the PFS with a unique TPMI
 * ID and its access details. The TPMI driver creates device nodes
 * for the supported PM features.
 *
 * The names of the devices created by the TPMI driver start with the
 * "intel_vsec.tpmi-" prefix which is followed by a specific name of the
 * given PM feature (for example, "intel_vsec.tpmi-rapl.0").
 *
 * The device nodes are create by using interface "intel_vsec_add_aux()"
 * provided by the Intel VSEC driver.
 */

#include <linux/auxiliary_bus.h>
#include <linux/intel_tpmi.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/pci.h>

#include "vsec.h"

/**
 * struct intel_tpmi_pfs_entry - TPMI PM Feature Structure (PFS) entry
 * @tpmi_id:	TPMI feature identifier (what the feature is and its data format).
 * @num_entries: Number of feature interface instances present in the PFS.
 *		 This represents the maximum number of Power domains in the SoC.
 * @entry_size:	Interface instance entry size in 32-bit words.
 * @cap_offset:	Offset from the PM_Features base address to the base of the PM VSEC
 *		register bank in KB.
 * @attribute:	Feature attribute: 0=BIOS. 1=OS. 2-3=Reserved.
 * @reserved:	Bits for use in the future.
 *
 * Represents one TPMI feature entry data in the PFS retrieved as is
 * from the hardware.
 */
struct intel_tpmi_pfs_entry {
	u64 tpmi_id:8;
	u64 num_entries:8;
	u64 entry_size:16;
	u64 cap_offset:16;
	u64 attribute:2;
	u64 reserved:14;
} __packed;

/**
 * struct intel_tpmi_pm_feature - TPMI PM Feature information for a TPMI ID
 * @pfs_header:	PFS header retireved from the hardware.
 * @vsec_offset: Starting MMIO address for this feature in bytes. Essentially
 *		 this offset = "Address" from VSEC header + PFS Capability
 *		 offset for this feature entry.
 *
 * Represents TPMI instance information for one TPMI ID.
 */
struct intel_tpmi_pm_feature {
	struct intel_tpmi_pfs_entry pfs_header;
	unsigned int vsec_offset;
};

/**
 * struct intel_tpmi_info - TPMI information for all IDs in an instance
 * @tpmi_features:	Pointer to a list of TPMI feature instances
 * @vsec_dev:		Pointer to intel_vsec_device structure for this TPMI device
 * @feature_count:	Number of TPMI of TPMI instances pointed by tpmi_features
 * @pfs_start:		Start of PFS offset for the TPMI instances in this device
 * @plat_info:		Stores platform info which can be used by the client drivers
 *
 * Stores the information for all TPMI devices enumerated from a single PCI device.
 */
struct intel_tpmi_info {
	struct intel_tpmi_pm_feature *tpmi_features;
	struct intel_vsec_device *vsec_dev;
	int feature_count;
	u64 pfs_start;
	struct intel_tpmi_plat_info plat_info;
};

/**
 * struct tpmi_info_header - CPU package ID to PCI device mapping information
 * @fn:		PCI function number
 * @dev:	PCI device number
 * @bus:	PCI bus number
 * @pkg:	CPU Package id
 * @reserved:	Reserved for future use
 * @lock:	When set to 1 the register is locked and becomes read-only
 *		until next reset. Not for use by the OS driver.
 *
 * The structure to read hardware provided mapping information.
 */
struct tpmi_info_header {
	u64 fn:3;
	u64 dev:5;
	u64 bus:8;
	u64 pkg:8;
	u64 reserved:39;
	u64 lock:1;
} __packed;

/*
 * List of supported TMPI IDs.
 * Some TMPI IDs are not used by Linux, so the numbers are not consecutive.
 */
enum intel_tpmi_id {
	TPMI_ID_RAPL = 0, /* Running Average Power Limit */
	TPMI_ID_PEM = 1, /* Power and Perf excursion Monitor */
	TPMI_ID_UNCORE = 2, /* Uncore Frequency Scaling */
	TPMI_ID_SST = 5, /* Speed Select Technology */
	TPMI_INFO_ID = 0x81, /* Special ID for PCI BDF and Package ID information */
};

/* Used during auxbus device creation */
static DEFINE_IDA(intel_vsec_tpmi_ida);

struct intel_tpmi_plat_info *tpmi_get_platform_data(struct auxiliary_device *auxdev)
{
	struct intel_vsec_device *vsec_dev = auxdev_to_ivdev(auxdev);

	return vsec_dev->priv_data;
}
EXPORT_SYMBOL_NS_GPL(tpmi_get_platform_data, INTEL_TPMI);

int tpmi_get_resource_count(struct auxiliary_device *auxdev)
{
	struct intel_vsec_device *vsec_dev = auxdev_to_ivdev(auxdev);

	if (vsec_dev)
		return vsec_dev->num_resources;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(tpmi_get_resource_count, INTEL_TPMI);

struct resource *tpmi_get_resource_at_index(struct auxiliary_device *auxdev, int index)
{
	struct intel_vsec_device *vsec_dev = auxdev_to_ivdev(auxdev);

	if (vsec_dev && index < vsec_dev->num_resources)
		return &vsec_dev->resource[index];

	return NULL;
}
EXPORT_SYMBOL_NS_GPL(tpmi_get_resource_at_index, INTEL_TPMI);

static const char *intel_tpmi_name(enum intel_tpmi_id id)
{
	switch (id) {
	case TPMI_ID_RAPL:
		return "rapl";
	case TPMI_ID_PEM:
		return "pem";
	case TPMI_ID_UNCORE:
		return "uncore";
	case TPMI_ID_SST:
		return "sst";
	default:
		return NULL;
	}
}

/* String Length for tpmi-"feature_name(upto 8 bytes)" */
#define TPMI_FEATURE_NAME_LEN	14

static int tpmi_create_device(struct intel_tpmi_info *tpmi_info,
			      struct intel_tpmi_pm_feature *pfs,
			      u64 pfs_start)
{
	struct intel_vsec_device *vsec_dev = tpmi_info->vsec_dev;
	char feature_id_name[TPMI_FEATURE_NAME_LEN];
	struct intel_vsec_device *feature_vsec_dev;
	struct resource *res, *tmp;
	const char *name;
	int i;

	name = intel_tpmi_name(pfs->pfs_header.tpmi_id);
	if (!name)
		return -EOPNOTSUPP;

	res = kcalloc(pfs->pfs_header.num_entries, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	feature_vsec_dev = kzalloc(sizeof(*feature_vsec_dev), GFP_KERNEL);
	if (!feature_vsec_dev) {
		kfree(res);
		return -ENOMEM;
	}

	snprintf(feature_id_name, sizeof(feature_id_name), "tpmi-%s", name);

	for (i = 0, tmp = res; i < pfs->pfs_header.num_entries; i++, tmp++) {
		u64 entry_size_bytes = pfs->pfs_header.entry_size * 4;

		tmp->start = pfs->vsec_offset + entry_size_bytes * i;
		tmp->end = tmp->start + entry_size_bytes - 1;
		tmp->flags = IORESOURCE_MEM;
	}

	feature_vsec_dev->pcidev = vsec_dev->pcidev;
	feature_vsec_dev->resource = res;
	feature_vsec_dev->num_resources = pfs->pfs_header.num_entries;
	feature_vsec_dev->priv_data = &tpmi_info->plat_info;
	feature_vsec_dev->priv_data_size = sizeof(tpmi_info->plat_info);
	feature_vsec_dev->ida = &intel_vsec_tpmi_ida;

	/*
	 * intel_vsec_add_aux() is resource managed, no explicit
	 * delete is required on error or on module unload.
	 * feature_vsec_dev and res memory are also freed as part of
	 * device deletion.
	 */
	return intel_vsec_add_aux(vsec_dev->pcidev, &vsec_dev->auxdev.dev,
				  feature_vsec_dev, feature_id_name);
}

static int tpmi_create_devices(struct intel_tpmi_info *tpmi_info)
{
	struct intel_vsec_device *vsec_dev = tpmi_info->vsec_dev;
	int ret, i;

	for (i = 0; i < vsec_dev->num_resources; i++) {
		ret = tpmi_create_device(tpmi_info, &tpmi_info->tpmi_features[i],
					 tpmi_info->pfs_start);
		/*
		 * Fail, if the supported features fails to create device,
		 * otherwise, continue. Even if one device failed to create,
		 * fail the loading of driver. Since intel_vsec_add_aux()
		 * is resource managed, no clean up is required for the
		 * successfully created devices.
		 */
		if (ret && ret != -EOPNOTSUPP)
			return ret;
	}

	return 0;
}

#define TPMI_INFO_BUS_INFO_OFFSET	0x08

static int tpmi_process_info(struct intel_tpmi_info *tpmi_info,
			     struct intel_tpmi_pm_feature *pfs)
{
	struct tpmi_info_header header;
	void __iomem *info_mem;

	info_mem = ioremap(pfs->vsec_offset + TPMI_INFO_BUS_INFO_OFFSET,
			   pfs->pfs_header.entry_size * 4 - TPMI_INFO_BUS_INFO_OFFSET);
	if (!info_mem)
		return -ENOMEM;

	memcpy_fromio(&header, info_mem, sizeof(header));

	tpmi_info->plat_info.package_id = header.pkg;
	tpmi_info->plat_info.bus_number = header.bus;
	tpmi_info->plat_info.device_number = header.dev;
	tpmi_info->plat_info.function_number = header.fn;

	iounmap(info_mem);

	return 0;
}

static int tpmi_fetch_pfs_header(struct intel_tpmi_pm_feature *pfs, u64 start, int size)
{
	void __iomem *pfs_mem;

	pfs_mem = ioremap(start, size);
	if (!pfs_mem)
		return -ENOMEM;

	memcpy_fromio(&pfs->pfs_header, pfs_mem, sizeof(pfs->pfs_header));

	iounmap(pfs_mem);

	return 0;
}

static int intel_vsec_tpmi_init(struct auxiliary_device *auxdev)
{
	struct intel_vsec_device *vsec_dev = auxdev_to_ivdev(auxdev);
	struct pci_dev *pci_dev = vsec_dev->pcidev;
	struct intel_tpmi_info *tpmi_info;
	u64 pfs_start = 0;
	int i;

	tpmi_info = devm_kzalloc(&auxdev->dev, sizeof(*tpmi_info), GFP_KERNEL);
	if (!tpmi_info)
		return -ENOMEM;

	tpmi_info->vsec_dev = vsec_dev;
	tpmi_info->feature_count = vsec_dev->num_resources;
	tpmi_info->plat_info.bus_number = pci_dev->bus->number;

	tpmi_info->tpmi_features = devm_kcalloc(&auxdev->dev, vsec_dev->num_resources,
						sizeof(*tpmi_info->tpmi_features),
						GFP_KERNEL);
	if (!tpmi_info->tpmi_features)
		return -ENOMEM;

	for (i = 0; i < vsec_dev->num_resources; i++) {
		struct intel_tpmi_pm_feature *pfs;
		struct resource *res;
		u64 res_start;
		int size, ret;

		pfs = &tpmi_info->tpmi_features[i];

		res = &vsec_dev->resource[i];
		if (!res)
			continue;

		res_start = res->start;
		size = resource_size(res);
		if (size < 0)
			continue;

		ret = tpmi_fetch_pfs_header(pfs, res_start, size);
		if (ret)
			continue;

		if (!pfs_start)
			pfs_start = res_start;

		pfs->pfs_header.cap_offset *= 1024;

		pfs->vsec_offset = pfs_start + pfs->pfs_header.cap_offset;

		/*
		 * Process TPMI_INFO to get PCI device to CPU package ID.
		 * Device nodes for TPMI features are not created in this
		 * for loop. So, the mapping information will be available
		 * when actual device nodes created outside this
		 * loop via tpmi_create_devices().
		 */
		if (pfs->pfs_header.tpmi_id == TPMI_INFO_ID)
			tpmi_process_info(tpmi_info, pfs);
	}

	tpmi_info->pfs_start = pfs_start;

	auxiliary_set_drvdata(auxdev, tpmi_info);

	return tpmi_create_devices(tpmi_info);
}

static int tpmi_probe(struct auxiliary_device *auxdev,
		      const struct auxiliary_device_id *id)
{
	return intel_vsec_tpmi_init(auxdev);
}

/*
 * Remove callback is not needed currently as there is no
 * cleanup required. All memory allocs are device managed. All
 * devices created by this modules are also device managed.
 */

static const struct auxiliary_device_id tpmi_id_table[] = {
	{ .name = "intel_vsec.tpmi" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, tpmi_id_table);

static struct auxiliary_driver tpmi_aux_driver = {
	.id_table	= tpmi_id_table,
	.probe		= tpmi_probe,
};

module_auxiliary_driver(tpmi_aux_driver);

MODULE_IMPORT_NS(INTEL_VSEC);
MODULE_DESCRIPTION("Intel TPMI enumeration module");
MODULE_LICENSE("GPL");
