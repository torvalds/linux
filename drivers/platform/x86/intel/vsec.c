// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Vendor Specific Extended Capabilities auxiliary bus driver
 *
 * Copyright (c) 2021, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: David E. Box <david.e.box@linux.intel.com>
 *
 * This driver discovers and creates auxiliary devices for Intel defined PCIe
 * "Vendor Specific" and "Designated Vendor Specific" Extended Capabilities,
 * VSEC and DVSEC respectively. The driver supports features on specific PCIe
 * endpoints that exist primarily to expose them.
 */

#include <linux/auxiliary_bus.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "vsec.h"

#define PMT_XA_START			0
#define PMT_XA_MAX			INT_MAX
#define PMT_XA_LIMIT			XA_LIMIT(PMT_XA_START, PMT_XA_MAX)

static DEFINE_IDA(intel_vsec_ida);
static DEFINE_IDA(intel_vsec_sdsi_ida);
static DEFINE_XARRAY_ALLOC(auxdev_array);

static const char *intel_vsec_name(enum intel_vsec_id id)
{
	switch (id) {
	case VSEC_ID_TELEMETRY:
		return "telemetry";

	case VSEC_ID_WATCHER:
		return "watcher";

	case VSEC_ID_CRASHLOG:
		return "crashlog";

	case VSEC_ID_SDSI:
		return "sdsi";

	case VSEC_ID_TPMI:
		return "tpmi";

	default:
		return NULL;
	}
}

static bool intel_vsec_supported(u16 id, unsigned long caps)
{
	switch (id) {
	case VSEC_ID_TELEMETRY:
		return !!(caps & VSEC_CAP_TELEMETRY);
	case VSEC_ID_WATCHER:
		return !!(caps & VSEC_CAP_WATCHER);
	case VSEC_ID_CRASHLOG:
		return !!(caps & VSEC_CAP_CRASHLOG);
	case VSEC_ID_SDSI:
		return !!(caps & VSEC_CAP_SDSI);
	case VSEC_ID_TPMI:
		return !!(caps & VSEC_CAP_TPMI);
	default:
		return false;
	}
}

static void intel_vsec_remove_aux(void *data)
{
	auxiliary_device_delete(data);
	auxiliary_device_uninit(data);
}

static DEFINE_MUTEX(vsec_ida_lock);

static void intel_vsec_dev_release(struct device *dev)
{
	struct intel_vsec_device *intel_vsec_dev = dev_to_ivdev(dev);

	xa_erase(&auxdev_array, intel_vsec_dev->id);

	mutex_lock(&vsec_ida_lock);
	ida_free(intel_vsec_dev->ida, intel_vsec_dev->auxdev.id);
	mutex_unlock(&vsec_ida_lock);

	kfree(intel_vsec_dev->resource);
	kfree(intel_vsec_dev);
}

int intel_vsec_add_aux(struct pci_dev *pdev, struct device *parent,
		       struct intel_vsec_device *intel_vsec_dev,
		       const char *name)
{
	struct auxiliary_device *auxdev = &intel_vsec_dev->auxdev;
	int ret, id;

	if (!parent)
		return -EINVAL;

	ret = xa_alloc(&auxdev_array, &intel_vsec_dev->id, intel_vsec_dev,
		       PMT_XA_LIMIT, GFP_KERNEL);
	if (ret < 0) {
		kfree(intel_vsec_dev->resource);
		kfree(intel_vsec_dev);
		return ret;
	}

	mutex_lock(&vsec_ida_lock);
	id = ida_alloc(intel_vsec_dev->ida, GFP_KERNEL);
	mutex_unlock(&vsec_ida_lock);
	if (id < 0) {
		xa_erase(&auxdev_array, intel_vsec_dev->id);
		kfree(intel_vsec_dev->resource);
		kfree(intel_vsec_dev);
		return id;
	}

	auxdev->id = id;
	auxdev->name = name;
	auxdev->dev.parent = parent;
	auxdev->dev.release = intel_vsec_dev_release;

	ret = auxiliary_device_init(auxdev);
	if (ret < 0) {
		intel_vsec_dev_release(&auxdev->dev);
		return ret;
	}

	ret = auxiliary_device_add(auxdev);
	if (ret < 0) {
		auxiliary_device_uninit(auxdev);
		return ret;
	}

	return devm_add_action_or_reset(parent, intel_vsec_remove_aux,
				       auxdev);
}
EXPORT_SYMBOL_NS_GPL(intel_vsec_add_aux, INTEL_VSEC);

static int intel_vsec_add_dev(struct pci_dev *pdev, struct intel_vsec_header *header,
			      struct intel_vsec_platform_info *info)
{
	struct intel_vsec_device __free(kfree) *intel_vsec_dev = NULL;
	struct resource __free(kfree) *res = NULL;
	struct resource *tmp;
	struct device *parent;
	unsigned long quirks = info->quirks;
	u64 base_addr;
	int i;

	if (info->parent)
		parent = info->parent;
	else
		parent = &pdev->dev;

	if (!intel_vsec_supported(header->id, info->caps))
		return -EINVAL;

	if (!header->num_entries) {
		dev_dbg(&pdev->dev, "Invalid 0 entry count for header id %d\n", header->id);
		return -EINVAL;
	}

	if (!header->entry_size) {
		dev_dbg(&pdev->dev, "Invalid 0 entry size for header id %d\n", header->id);
		return -EINVAL;
	}

	intel_vsec_dev = kzalloc(sizeof(*intel_vsec_dev), GFP_KERNEL);
	if (!intel_vsec_dev)
		return -ENOMEM;

	res = kcalloc(header->num_entries, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	if (quirks & VSEC_QUIRK_TABLE_SHIFT)
		header->offset >>= TABLE_OFFSET_SHIFT;

	if (info->base_addr)
		base_addr = info->base_addr;
	else
		base_addr = pdev->resource[header->tbir].start;

	/*
	 * The DVSEC/VSEC contains the starting offset and count for a block of
	 * discovery tables. Create a resource array of these tables to the
	 * auxiliary device driver.
	 */
	for (i = 0, tmp = res; i < header->num_entries; i++, tmp++) {
		tmp->start = base_addr + header->offset + i * (header->entry_size * sizeof(u32));
		tmp->end = tmp->start + (header->entry_size * sizeof(u32)) - 1;
		tmp->flags = IORESOURCE_MEM;

		/* Check resource is not in use */
		if (!request_mem_region(tmp->start, resource_size(tmp), ""))
			return -EBUSY;

		release_mem_region(tmp->start, resource_size(tmp));
	}

	intel_vsec_dev->pcidev = pdev;
	intel_vsec_dev->resource = no_free_ptr(res);
	intel_vsec_dev->num_resources = header->num_entries;
	intel_vsec_dev->quirks = info->quirks;
	intel_vsec_dev->base_addr = info->base_addr;

	if (header->id == VSEC_ID_SDSI)
		intel_vsec_dev->ida = &intel_vsec_sdsi_ida;
	else
		intel_vsec_dev->ida = &intel_vsec_ida;

	/*
	 * Pass the ownership of intel_vsec_dev and resource within it to
	 * intel_vsec_add_aux()
	 */
	return intel_vsec_add_aux(pdev, parent, no_free_ptr(intel_vsec_dev),
				  intel_vsec_name(header->id));
}

static bool intel_vsec_walk_header(struct pci_dev *pdev,
				   struct intel_vsec_platform_info *info)
{
	struct intel_vsec_header **header = info->headers;
	bool have_devices = false;
	int ret;

	for ( ; *header; header++) {
		ret = intel_vsec_add_dev(pdev, *header, info);
		if (ret)
			dev_info(&pdev->dev, "Could not add device for VSEC id %d\n",
				 (*header)->id);
		else
			have_devices = true;
	}

	return have_devices;
}

static bool intel_vsec_walk_dvsec(struct pci_dev *pdev,
				  struct intel_vsec_platform_info *info)
{
	bool have_devices = false;
	int pos = 0;

	do {
		struct intel_vsec_header header;
		u32 table, hdr;
		u16 vid;
		int ret;

		pos = pci_find_next_ext_capability(pdev, pos, PCI_EXT_CAP_ID_DVSEC);
		if (!pos)
			break;

		pci_read_config_dword(pdev, pos + PCI_DVSEC_HEADER1, &hdr);
		vid = PCI_DVSEC_HEADER1_VID(hdr);
		if (vid != PCI_VENDOR_ID_INTEL)
			continue;

		/* Support only revision 1 */
		header.rev = PCI_DVSEC_HEADER1_REV(hdr);
		if (header.rev != 1) {
			dev_info(&pdev->dev, "Unsupported DVSEC revision %d\n", header.rev);
			continue;
		}

		header.length = PCI_DVSEC_HEADER1_LEN(hdr);

		pci_read_config_byte(pdev, pos + INTEL_DVSEC_ENTRIES, &header.num_entries);
		pci_read_config_byte(pdev, pos + INTEL_DVSEC_SIZE, &header.entry_size);
		pci_read_config_dword(pdev, pos + INTEL_DVSEC_TABLE, &table);

		header.tbir = INTEL_DVSEC_TABLE_BAR(table);
		header.offset = INTEL_DVSEC_TABLE_OFFSET(table);

		pci_read_config_dword(pdev, pos + PCI_DVSEC_HEADER2, &hdr);
		header.id = PCI_DVSEC_HEADER2_ID(hdr);

		ret = intel_vsec_add_dev(pdev, &header, info);
		if (ret)
			continue;

		have_devices = true;
	} while (true);

	return have_devices;
}

static bool intel_vsec_walk_vsec(struct pci_dev *pdev,
				 struct intel_vsec_platform_info *info)
{
	bool have_devices = false;
	int pos = 0;

	do {
		struct intel_vsec_header header;
		u32 table, hdr;
		int ret;

		pos = pci_find_next_ext_capability(pdev, pos, PCI_EXT_CAP_ID_VNDR);
		if (!pos)
			break;

		pci_read_config_dword(pdev, pos + PCI_VNDR_HEADER, &hdr);

		/* Support only revision 1 */
		header.rev = PCI_VNDR_HEADER_REV(hdr);
		if (header.rev != 1) {
			dev_info(&pdev->dev, "Unsupported VSEC revision %d\n", header.rev);
			continue;
		}

		header.id = PCI_VNDR_HEADER_ID(hdr);
		header.length = PCI_VNDR_HEADER_LEN(hdr);

		/* entry, size, and table offset are the same as DVSEC */
		pci_read_config_byte(pdev, pos + INTEL_DVSEC_ENTRIES, &header.num_entries);
		pci_read_config_byte(pdev, pos + INTEL_DVSEC_SIZE, &header.entry_size);
		pci_read_config_dword(pdev, pos + INTEL_DVSEC_TABLE, &table);

		header.tbir = INTEL_DVSEC_TABLE_BAR(table);
		header.offset = INTEL_DVSEC_TABLE_OFFSET(table);

		ret = intel_vsec_add_dev(pdev, &header, info);
		if (ret)
			continue;

		have_devices = true;
	} while (true);

	return have_devices;
}

void intel_vsec_register(struct pci_dev *pdev,
			 struct intel_vsec_platform_info *info)
{
	if (!pdev || !info)
		return;

	intel_vsec_walk_header(pdev, info);
}
EXPORT_SYMBOL_NS_GPL(intel_vsec_register, INTEL_VSEC);

static int intel_vsec_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct intel_vsec_platform_info *info;
	bool have_devices = false;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	pci_save_state(pdev);
	info = (struct intel_vsec_platform_info *)id->driver_data;
	if (!info)
		return -EINVAL;

	if (intel_vsec_walk_dvsec(pdev, info))
		have_devices = true;

	if (intel_vsec_walk_vsec(pdev, info))
		have_devices = true;

	if (info && (info->quirks & VSEC_QUIRK_NO_DVSEC) &&
	    intel_vsec_walk_header(pdev, info))
		have_devices = true;

	if (!have_devices)
		return -ENODEV;

	return 0;
}

/* DG1 info */
static struct intel_vsec_header dg1_header = {
	.length = 0x10,
	.id = 2,
	.num_entries = 1,
	.entry_size = 3,
	.tbir = 0,
	.offset = 0x466000,
};

static struct intel_vsec_header *dg1_headers[] = {
	&dg1_header,
	NULL
};

static const struct intel_vsec_platform_info dg1_info = {
	.caps = VSEC_CAP_TELEMETRY,
	.headers = dg1_headers,
	.quirks = VSEC_QUIRK_NO_DVSEC | VSEC_QUIRK_EARLY_HW,
};

/* MTL info */
static const struct intel_vsec_platform_info mtl_info = {
	.caps = VSEC_CAP_TELEMETRY,
};

/* OOBMSM info */
static const struct intel_vsec_platform_info oobmsm_info = {
	.caps = VSEC_CAP_TELEMETRY | VSEC_CAP_SDSI | VSEC_CAP_TPMI,
};

/* TGL info */
static const struct intel_vsec_platform_info tgl_info = {
	.caps = VSEC_CAP_TELEMETRY,
	.quirks = VSEC_QUIRK_TABLE_SHIFT | VSEC_QUIRK_EARLY_HW,
};

/* LNL info */
static const struct intel_vsec_platform_info lnl_info = {
	.caps = VSEC_CAP_TELEMETRY | VSEC_CAP_WATCHER,
};

#define PCI_DEVICE_ID_INTEL_VSEC_ADL		0x467d
#define PCI_DEVICE_ID_INTEL_VSEC_DG1		0x490e
#define PCI_DEVICE_ID_INTEL_VSEC_MTL_M		0x7d0d
#define PCI_DEVICE_ID_INTEL_VSEC_MTL_S		0xad0d
#define PCI_DEVICE_ID_INTEL_VSEC_OOBMSM		0x09a7
#define PCI_DEVICE_ID_INTEL_VSEC_RPL		0xa77d
#define PCI_DEVICE_ID_INTEL_VSEC_TGL		0x9a0d
#define PCI_DEVICE_ID_INTEL_VSEC_LNL_M		0x647d
static const struct pci_device_id intel_vsec_pci_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, VSEC_ADL, &tgl_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_DG1, &dg1_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_MTL_M, &mtl_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_MTL_S, &mtl_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_OOBMSM, &oobmsm_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_RPL, &tgl_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_TGL, &tgl_info) },
	{ PCI_DEVICE_DATA(INTEL, VSEC_LNL_M, &lnl_info) },
	{ }
};
MODULE_DEVICE_TABLE(pci, intel_vsec_pci_ids);

static pci_ers_result_t intel_vsec_pci_error_detected(struct pci_dev *pdev,
						      pci_channel_state_t state)
{
	pci_ers_result_t status = PCI_ERS_RESULT_NEED_RESET;

	dev_info(&pdev->dev, "PCI error detected, state %d", state);

	if (state == pci_channel_io_perm_failure)
		status = PCI_ERS_RESULT_DISCONNECT;
	else
		pci_disable_device(pdev);

	return status;
}

static pci_ers_result_t intel_vsec_pci_slot_reset(struct pci_dev *pdev)
{
	struct intel_vsec_device *intel_vsec_dev;
	pci_ers_result_t status = PCI_ERS_RESULT_DISCONNECT;
	const struct pci_device_id *pci_dev_id;
	unsigned long index;

	dev_info(&pdev->dev, "Resetting PCI slot\n");

	msleep(2000);
	if (pci_enable_device(pdev)) {
		dev_info(&pdev->dev,
			 "Failed to re-enable PCI device after reset.\n");
		goto out;
	}

	status = PCI_ERS_RESULT_RECOVERED;

	xa_for_each(&auxdev_array, index, intel_vsec_dev) {
		/* check if pdev doesn't match */
		if (pdev != intel_vsec_dev->pcidev)
			continue;
		devm_release_action(&pdev->dev, intel_vsec_remove_aux,
				    &intel_vsec_dev->auxdev);
	}
	pci_disable_device(pdev);
	pci_restore_state(pdev);
	pci_dev_id = pci_match_id(intel_vsec_pci_ids, pdev);
	intel_vsec_pci_probe(pdev, pci_dev_id);

out:
	return status;
}

static void intel_vsec_pci_resume(struct pci_dev *pdev)
{
	dev_info(&pdev->dev, "Done resuming PCI device\n");
}

static const struct pci_error_handlers intel_vsec_pci_err_handlers = {
	.error_detected = intel_vsec_pci_error_detected,
	.slot_reset = intel_vsec_pci_slot_reset,
	.resume = intel_vsec_pci_resume,
};

static struct pci_driver intel_vsec_pci_driver = {
	.name = "intel_vsec",
	.id_table = intel_vsec_pci_ids,
	.probe = intel_vsec_pci_probe,
	.err_handler = &intel_vsec_pci_err_handlers,
};
module_pci_driver(intel_vsec_pci_driver);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel Extended Capabilities auxiliary bus driver");
MODULE_LICENSE("GPL v2");
