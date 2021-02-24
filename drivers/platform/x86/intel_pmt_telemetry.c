// SPDX-License-Identifier: GPL-2.0
/*
 * Intel Platform Monitory Technology Telemetry driver
 *
 * Copyright (c) 2020, Intel Corporation.
 * All Rights Reserved.
 *
 * Author: "David E. Box" <david.e.box@linux.intel.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/overflow.h>

#include "intel_pmt_class.h"

#define TELEM_DEV_NAME		"pmt_telemetry"

#define TELEM_SIZE_OFFSET	0x0
#define TELEM_GUID_OFFSET	0x4
#define TELEM_BASE_OFFSET	0x8
#define TELEM_ACCESS(v)		((v) & GENMASK(3, 0))
/* size is in bytes */
#define TELEM_SIZE(v)		(((v) & GENMASK(27, 12)) >> 10)

/* Used by client hardware to identify a fixed telemetry entry*/
#define TELEM_CLIENT_FIXED_BLOCK_GUID	0x10000000

struct pmt_telem_priv {
	int				num_entries;
	struct intel_pmt_entry		entry[];
};

static bool pmt_telem_region_overlaps(struct intel_pmt_entry *entry,
				      struct device *dev)
{
	u32 guid = readl(entry->disc_table + TELEM_GUID_OFFSET);

	if (guid != TELEM_CLIENT_FIXED_BLOCK_GUID)
		return false;

	return intel_pmt_is_early_client_hw(dev);
}

static int pmt_telem_header_decode(struct intel_pmt_entry *entry,
				   struct intel_pmt_header *header,
				   struct device *dev)
{
	void __iomem *disc_table = entry->disc_table;

	if (pmt_telem_region_overlaps(entry, dev))
		return 1;

	header->access_type = TELEM_ACCESS(readl(disc_table));
	header->guid = readl(disc_table + TELEM_GUID_OFFSET);
	header->base_offset = readl(disc_table + TELEM_BASE_OFFSET);

	/* Size is measured in DWORDS, but accessor returns bytes */
	header->size = TELEM_SIZE(readl(disc_table));

	return 0;
}

static DEFINE_XARRAY_ALLOC(telem_array);
static struct intel_pmt_namespace pmt_telem_ns = {
	.name = "telem",
	.xa = &telem_array,
	.pmt_header_decode = pmt_telem_header_decode,
};

static int pmt_telem_remove(struct platform_device *pdev)
{
	struct pmt_telem_priv *priv = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < priv->num_entries; i++)
		intel_pmt_dev_destroy(&priv->entry[i], &pmt_telem_ns);

	return 0;
}

static int pmt_telem_probe(struct platform_device *pdev)
{
	struct pmt_telem_priv *priv;
	size_t size;
	int i, ret;

	size = struct_size(priv, entry, pdev->num_resources);
	priv = devm_kzalloc(&pdev->dev, size, GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	for (i = 0; i < pdev->num_resources; i++) {
		struct intel_pmt_entry *entry = &priv->entry[i];

		ret = intel_pmt_dev_create(entry, &pmt_telem_ns, pdev, i);
		if (ret < 0)
			goto abort_probe;
		if (ret)
			continue;

		priv->num_entries++;
	}

	return 0;
abort_probe:
	pmt_telem_remove(pdev);
	return ret;
}

static struct platform_driver pmt_telem_driver = {
	.driver = {
		.name   = TELEM_DEV_NAME,
	},
	.remove = pmt_telem_remove,
	.probe  = pmt_telem_probe,
};

static int __init pmt_telem_init(void)
{
	return platform_driver_register(&pmt_telem_driver);
}
module_init(pmt_telem_init);

static void __exit pmt_telem_exit(void)
{
	platform_driver_unregister(&pmt_telem_driver);
	xa_destroy(&telem_array);
}
module_exit(pmt_telem_exit);

MODULE_AUTHOR("David E. Box <david.e.box@linux.intel.com>");
MODULE_DESCRIPTION("Intel PMT Telemetry driver");
MODULE_ALIAS("platform:" TELEM_DEV_NAME);
MODULE_LICENSE("GPL v2");
