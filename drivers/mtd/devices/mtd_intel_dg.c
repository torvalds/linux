// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(c) 2019-2025, Intel Corporation. All rights reserved.
 */

#include <linux/device.h>
#include <linux/intel_dg_nvm_aux.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/types.h>

struct intel_dg_nvm {
	struct kref refcnt;
	void __iomem *base;
	size_t size;
	unsigned int nregions;
	struct {
		const char *name;
		u8 id;
		u64 offset;
		u64 size;
	} regions[] __counted_by(nregions);
};

static void intel_dg_nvm_release(struct kref *kref)
{
	struct intel_dg_nvm *nvm = container_of(kref, struct intel_dg_nvm, refcnt);
	int i;

	pr_debug("freeing intel_dg nvm\n");
	for (i = 0; i < nvm->nregions; i++)
		kfree(nvm->regions[i].name);
	kfree(nvm);
}

static int intel_dg_mtd_probe(struct auxiliary_device *aux_dev,
			      const struct auxiliary_device_id *aux_dev_id)
{
	struct intel_dg_nvm_dev *invm = auxiliary_dev_to_intel_dg_nvm_dev(aux_dev);
	struct intel_dg_nvm *nvm;
	struct device *device;
	unsigned int nregions;
	unsigned int i, n;
	int ret;

	device = &aux_dev->dev;

	/* count available regions */
	for (nregions = 0, i = 0; i < INTEL_DG_NVM_REGIONS; i++) {
		if (invm->regions[i].name)
			nregions++;
	}

	if (!nregions) {
		dev_err(device, "no regions defined\n");
		return -ENODEV;
	}

	nvm = kzalloc(struct_size(nvm, regions, nregions), GFP_KERNEL);
	if (!nvm)
		return -ENOMEM;

	kref_init(&nvm->refcnt);

	for (n = 0, i = 0; i < INTEL_DG_NVM_REGIONS; i++) {
		if (!invm->regions[i].name)
			continue;

		char *name = kasprintf(GFP_KERNEL, "%s.%s",
				       dev_name(&aux_dev->dev), invm->regions[i].name);
		if (!name)
			continue;
		nvm->regions[n].name = name;
		nvm->regions[n].id = i;
		n++;
	}
	nvm->nregions = n; /* in case where kasprintf fail */

	nvm->base = devm_ioremap_resource(device, &invm->bar);
	if (IS_ERR(nvm->base)) {
		ret = PTR_ERR(nvm->base);
		goto err;
	}

	dev_set_drvdata(&aux_dev->dev, nvm);

	return 0;

err:
	kref_put(&nvm->refcnt, intel_dg_nvm_release);
	return ret;
}

static void intel_dg_mtd_remove(struct auxiliary_device *aux_dev)
{
	struct intel_dg_nvm *nvm = dev_get_drvdata(&aux_dev->dev);

	if (!nvm)
		return;

	dev_set_drvdata(&aux_dev->dev, NULL);

	kref_put(&nvm->refcnt, intel_dg_nvm_release);
}

static const struct auxiliary_device_id intel_dg_mtd_id_table[] = {
	{
		.name = "i915.nvm",
	},
	{
		.name = "xe.nvm",
	},
	{
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(auxiliary, intel_dg_mtd_id_table);

static struct auxiliary_driver intel_dg_mtd_driver = {
	.probe  = intel_dg_mtd_probe,
	.remove = intel_dg_mtd_remove,
	.driver = {
		/* auxiliary_driver_register() sets .name to be the modname */
	},
	.id_table = intel_dg_mtd_id_table
};
module_auxiliary_driver(intel_dg_mtd_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Intel Corporation");
MODULE_DESCRIPTION("Intel DGFX MTD driver");
