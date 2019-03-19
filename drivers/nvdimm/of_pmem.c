// SPDX-License-Identifier: GPL-2.0+

#define pr_fmt(fmt) "of_pmem: " fmt

#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/libnvdimm.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/slab.h>

static const struct attribute_group *region_attr_groups[] = {
	&nd_region_attribute_group,
	&nd_device_attribute_group,
	NULL,
};

static const struct attribute_group *bus_attr_groups[] = {
	&nvdimm_bus_attribute_group,
	NULL,
};

struct of_pmem_private {
	struct nvdimm_bus_descriptor bus_desc;
	struct nvdimm_bus *bus;
};

static int of_pmem_region_probe(struct platform_device *pdev)
{
	struct of_pmem_private *priv;
	struct device_node *np;
	struct nvdimm_bus *bus;
	bool is_volatile;
	int i;

	np = dev_of_node(&pdev->dev);
	if (!np)
		return -ENXIO;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->bus_desc.attr_groups = bus_attr_groups;
	priv->bus_desc.provider_name = "of_pmem";
	priv->bus_desc.module = THIS_MODULE;
	priv->bus_desc.of_node = np;

	priv->bus = bus = nvdimm_bus_register(&pdev->dev, &priv->bus_desc);
	if (!bus) {
		kfree(priv);
		return -ENODEV;
	}
	platform_set_drvdata(pdev, priv);

	is_volatile = !!of_find_property(np, "volatile", NULL);
	dev_dbg(&pdev->dev, "Registering %s regions from %pOF\n",
			is_volatile ? "volatile" : "non-volatile",  np);

	for (i = 0; i < pdev->num_resources; i++) {
		struct nd_region_desc ndr_desc;
		struct nd_region *region;

		/*
		 * NB: libnvdimm copies the data from ndr_desc into it's own
		 * structures so passing a stack pointer is fine.
		 */
		memset(&ndr_desc, 0, sizeof(ndr_desc));
		ndr_desc.attr_groups = region_attr_groups;
		ndr_desc.numa_node = dev_to_node(&pdev->dev);
		ndr_desc.target_node = ndr_desc.numa_node;
		ndr_desc.res = &pdev->resource[i];
		ndr_desc.of_node = np;
		set_bit(ND_REGION_PAGEMAP, &ndr_desc.flags);

		if (is_volatile)
			region = nvdimm_volatile_region_create(bus, &ndr_desc);
		else
			region = nvdimm_pmem_region_create(bus, &ndr_desc);

		if (!region)
			dev_warn(&pdev->dev, "Unable to register region %pR from %pOF\n",
					ndr_desc.res, np);
		else
			dev_dbg(&pdev->dev, "Registered region %pR from %pOF\n",
					ndr_desc.res, np);
	}

	return 0;
}

static int of_pmem_region_remove(struct platform_device *pdev)
{
	struct of_pmem_private *priv = platform_get_drvdata(pdev);

	nvdimm_bus_unregister(priv->bus);
	kfree(priv);

	return 0;
}

static const struct of_device_id of_pmem_region_match[] = {
	{ .compatible = "pmem-region" },
	{ },
};

static struct platform_driver of_pmem_region_driver = {
	.probe = of_pmem_region_probe,
	.remove = of_pmem_region_remove,
	.driver = {
		.name = "of_pmem",
		.of_match_table = of_pmem_region_match,
	},
};

module_platform_driver(of_pmem_region_driver);
MODULE_DEVICE_TABLE(of, of_pmem_region_match);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("IBM Corporation");
