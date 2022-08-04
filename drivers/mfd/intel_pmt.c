// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Platform Monitoring Technology PMT driver
 *
 * Copyright (c) 2020, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: David E. Box <david.e.box@linux.intel.com>
 */

#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/types.h>

/* Intel DVSEC capability vendor space offsets */
#define INTEL_DVSEC_ENTRIES		0xA
#define INTEL_DVSEC_SIZE		0xB
#define INTEL_DVSEC_TABLE		0xC
#define INTEL_DVSEC_TABLE_BAR(x)	((x) & GENMASK(2, 0))
#define INTEL_DVSEC_TABLE_OFFSET(x)	((x) & GENMASK(31, 3))
#define INTEL_DVSEC_ENTRY_SIZE		4

/* PMT capabilities */
#define DVSEC_INTEL_ID_TELEMETRY	2
#define DVSEC_INTEL_ID_WATCHER		3
#define DVSEC_INTEL_ID_CRASHLOG		4

struct intel_dvsec_header {
	u16	length;
	u16	id;
	u8	num_entries;
	u8	entry_size;
	u8	tbir;
	u32	offset;
};

enum pmt_quirks {
	/* Watcher capability not supported */
	PMT_QUIRK_NO_WATCHER	= BIT(0),

	/* Crashlog capability not supported */
	PMT_QUIRK_NO_CRASHLOG	= BIT(1),

	/* Use shift instead of mask to read discovery table offset */
	PMT_QUIRK_TABLE_SHIFT	= BIT(2),
};

struct pmt_platform_info {
	unsigned long quirks;
};

static const struct pmt_platform_info tgl_info = {
	.quirks = PMT_QUIRK_NO_WATCHER | PMT_QUIRK_NO_CRASHLOG |
		  PMT_QUIRK_TABLE_SHIFT,
};

static int pmt_add_dev(struct pci_dev *pdev, struct intel_dvsec_header *header,
		       unsigned long quirks)
{
	struct device *dev = &pdev->dev;
	struct resource *res, *tmp;
	struct mfd_cell *cell;
	const char *name;
	int count = header->num_entries;
	int size = header->entry_size;
	int id = header->id;
	int i;

	switch (id) {
	case DVSEC_INTEL_ID_TELEMETRY:
		name = "pmt_telemetry";
		break;
	case DVSEC_INTEL_ID_WATCHER:
		if (quirks & PMT_QUIRK_NO_WATCHER) {
			dev_info(dev, "Watcher not supported\n");
			return 0;
		}
		name = "pmt_watcher";
		break;
	case DVSEC_INTEL_ID_CRASHLOG:
		if (quirks & PMT_QUIRK_NO_CRASHLOG) {
			dev_info(dev, "Crashlog not supported\n");
			return 0;
		}
		name = "pmt_crashlog";
		break;
	default:
		dev_err(dev, "Unrecognized PMT capability: %d\n", id);
		return -EINVAL;
	}

	if (!header->num_entries || !header->entry_size) {
		dev_err(dev, "Invalid count or size for %s header\n", name);
		return -EINVAL;
	}

	cell = devm_kzalloc(dev, sizeof(*cell), GFP_KERNEL);
	if (!cell)
		return -ENOMEM;

	res = devm_kcalloc(dev, count, sizeof(*res), GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	if (quirks & PMT_QUIRK_TABLE_SHIFT)
		header->offset >>= 3;

	/*
	 * The PMT DVSEC contains the starting offset and count for a block of
	 * discovery tables, each providing access to monitoring facilities for
	 * a section of the device. Create a resource list of these tables to
	 * provide to the driver.
	 */
	for (i = 0, tmp = res; i < count; i++, tmp++) {
		tmp->start = pdev->resource[header->tbir].start +
			     header->offset + i * (size << 2);
		tmp->end = tmp->start + (size << 2) - 1;
		tmp->flags = IORESOURCE_MEM;
	}

	cell->resources = res;
	cell->num_resources = count;
	cell->name = name;

	return devm_mfd_add_devices(dev, PLATFORM_DEVID_AUTO, cell, 1, NULL, 0,
				    NULL);
}

static int pmt_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct pmt_platform_info *info;
	unsigned long quirks = 0;
	bool found_devices = false;
	int ret, pos = 0;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	info = (struct pmt_platform_info *)id->driver_data;

	if (info)
		quirks = info->quirks;

	do {
		struct intel_dvsec_header header;
		u32 table;
		u16 vid;

		pos = pci_find_next_ext_capability(pdev, pos, PCI_EXT_CAP_ID_DVSEC);
		if (!pos)
			break;

		pci_read_config_word(pdev, pos + PCI_DVSEC_HEADER1, &vid);
		if (vid != PCI_VENDOR_ID_INTEL)
			continue;

		pci_read_config_word(pdev, pos + PCI_DVSEC_HEADER2,
				     &header.id);
		pci_read_config_byte(pdev, pos + INTEL_DVSEC_ENTRIES,
				     &header.num_entries);
		pci_read_config_byte(pdev, pos + INTEL_DVSEC_SIZE,
				     &header.entry_size);
		pci_read_config_dword(pdev, pos + INTEL_DVSEC_TABLE,
				      &table);

		header.tbir = INTEL_DVSEC_TABLE_BAR(table);
		header.offset = INTEL_DVSEC_TABLE_OFFSET(table);

		ret = pmt_add_dev(pdev, &header, quirks);
		if (ret) {
			dev_warn(&pdev->dev,
				 "Failed to add device for DVSEC id %d\n",
				 header.id);
			continue;
		}

		found_devices = true;
	} while (true);

	if (!found_devices)
		return -ENODEV;

	pm_runtime_put(&pdev->dev);
	pm_runtime_allow(&pdev->dev);

	return 0;
}

static void pmt_pci_remove(struct pci_dev *pdev)
{
	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);
}

#define PCI_DEVICE_ID_INTEL_PMT_ADL	0x467d
#define PCI_DEVICE_ID_INTEL_PMT_OOBMSM	0x09a7
#define PCI_DEVICE_ID_INTEL_PMT_TGL	0x9a0d
static const struct pci_device_id pmt_pci_ids[] = {
	{ PCI_DEVICE_DATA(INTEL, PMT_ADL, &tgl_info) },
	{ PCI_DEVICE_DATA(INTEL, PMT_OOBMSM, NULL) },
	{ PCI_DEVICE_DATA(INTEL, PMT_TGL, &tgl_info) },
	{ }
};
MODULE_DEVICE_TABLE(pci, pmt_pci_ids);

static struct pci_driver pmt_pci_driver = {
	.name = "intel-pmt",
	.id_table = pmt_pci_ids,
	.probe = pmt_pci_probe,
	.remove = pmt_pci_remove,
};
module_pci_driver(pmt_pci_driver);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel Platform Monitoring Technology PMT driver");
MODULE_LICENSE("GPL v2");
