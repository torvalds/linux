// SPDX-License-Identifier: GPL-2.0-only
/*
 * Enumerate TPMI features from a PCI VSEC structure
 *
 * Copyright (c) 2023, Intel Corporation.
 * All Rights Reserved.
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
 * The starting address of the PFS table in the TPMI MMIO region is derived
 * from the tBIR (Bar Indicator Register) and "Address" field from the VSEC
 * header by the Intel VSEC driver, which passes the result on as the
 * resources of the auxiliary device. Everything that happens from there on
 * is enumeration independent and lives in tpmi_common.c.
 */

#include <linux/auxiliary_bus.h>
#include <linux/intel_vsec.h>
#include <linux/module.h>

#include "tpmi_common.h"

static int tpmi_probe(struct auxiliary_device *auxdev,
		      const struct auxiliary_device_id *id)
{
	struct intel_vsec_device *vsec_dev = auxdev_to_ivdev(auxdev);
	struct intel_tpmi_info *tpmi_info;

	tpmi_info = devm_kzalloc(&auxdev->dev, sizeof(*tpmi_info), GFP_KERNEL);
	if (!tpmi_info)
		return -ENOMEM;

	return intel_tpmi_init(tpmi_info, &auxdev->dev, vsec_dev->dev,
			       vsec_dev->resource, vsec_dev->num_resources);
}

static void tpmi_remove(struct auxiliary_device *auxdev)
{
	struct intel_tpmi_info *tpmi_info = auxiliary_get_drvdata(auxdev);

	intel_tpmi_deinit(tpmi_info);
}

static const struct auxiliary_device_id tpmi_id_table[] = {
	{ .name = "intel_vsec.tpmi" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, tpmi_id_table);

static struct auxiliary_driver tpmi_aux_driver = {
	.id_table	= tpmi_id_table,
	.probe		= tpmi_probe,
	.remove         = tpmi_remove,
};

module_auxiliary_driver(tpmi_aux_driver);

MODULE_IMPORT_NS("INTEL_TPMI");
MODULE_DESCRIPTION("Intel TPMI enumeration module");
MODULE_LICENSE("GPL");
