// SPDX-License-Identifier: GPL-2.0
/*
 * TI K3 SoC info driver
 *
 * Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com
 */

#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/sys_soc.h>

#define CTRLMMR_WKUP_JTAGID_REG		0
/*
 * Bits:
 *  31-28 VARIANT	Device variant
 *  27-12 PARTNO	Part number
 *  11-1  MFG		Indicates TI as manufacturer (0x17)
 *  1			Always 1
 */
#define CTRLMMR_WKUP_JTAGID_VARIANT_SHIFT	(28)
#define CTRLMMR_WKUP_JTAGID_VARIANT_MASK	GENMASK(31, 28)

#define CTRLMMR_WKUP_JTAGID_PARTNO_SHIFT	(12)
#define CTRLMMR_WKUP_JTAGID_PARTNO_MASK		GENMASK(27, 12)

#define CTRLMMR_WKUP_JTAGID_MFG_SHIFT		(1)
#define CTRLMMR_WKUP_JTAGID_MFG_MASK		GENMASK(11, 1)

#define CTRLMMR_WKUP_JTAGID_MFG_TI		0x17

static const struct k3_soc_id {
	unsigned int id;
	const char *family_name;
} k3_soc_ids[] = {
	{ 0xBB5A, "AM65X" },
	{ 0xBB64, "J721E" },
	{ 0xBB6D, "J7200" },
	{ 0xBB38, "AM64X" },
	{ 0xBB75, "J721S2"},
	{ 0xBB7E, "AM62X" },
	{ 0xBB80, "J784S4" },
	{ 0xBB8D, "AM62AX" },
	{ 0xBB9D, "AM62PX" },
};

static int
k3_chipinfo_partno_to_names(unsigned int partno,
			    struct soc_device_attribute *soc_dev_attr)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(k3_soc_ids); i++)
		if (partno == k3_soc_ids[i].id) {
			soc_dev_attr->family = k3_soc_ids[i].family_name;
			return 0;
		}

	return -EINVAL;
}

static int k3_chipinfo_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct soc_device_attribute *soc_dev_attr;
	struct device *dev = &pdev->dev;
	struct soc_device *soc_dev;
	struct regmap *regmap;
	u32 partno_id;
	u32 variant;
	u32 jtag_id;
	u32 mfg;
	int ret;

	regmap = device_node_to_regmap(node);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ret = regmap_read(regmap, CTRLMMR_WKUP_JTAGID_REG, &jtag_id);
	if (ret < 0)
		return ret;

	mfg = (jtag_id & CTRLMMR_WKUP_JTAGID_MFG_MASK) >>
	       CTRLMMR_WKUP_JTAGID_MFG_SHIFT;

	if (mfg != CTRLMMR_WKUP_JTAGID_MFG_TI) {
		dev_err(dev, "Invalid MFG SoC\n");
		return -ENODEV;
	}

	variant = (jtag_id & CTRLMMR_WKUP_JTAGID_VARIANT_MASK) >>
		  CTRLMMR_WKUP_JTAGID_VARIANT_SHIFT;
	variant++;

	partno_id = (jtag_id & CTRLMMR_WKUP_JTAGID_PARTNO_MASK) >>
		 CTRLMMR_WKUP_JTAGID_PARTNO_SHIFT;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "SR%x.0", variant);
	if (!soc_dev_attr->revision) {
		ret = -ENOMEM;
		goto err;
	}

	ret = k3_chipinfo_partno_to_names(partno_id, soc_dev_attr);
	if (ret) {
		dev_err(dev, "Unknown SoC JTAGID[0x%08X]\n", jtag_id);
		ret = -ENODEV;
		goto err_free_rev;
	}

	node = of_find_node_by_path("/");
	of_property_read_string(node, "model", &soc_dev_attr->machine);
	of_node_put(node);

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		ret = PTR_ERR(soc_dev);
		goto err_free_rev;
	}

	dev_info(dev, "Family:%s rev:%s JTAGID[0x%08x] Detected\n",
		 soc_dev_attr->family,
		 soc_dev_attr->revision, jtag_id);

	return 0;

err_free_rev:
	kfree(soc_dev_attr->revision);
err:
	kfree(soc_dev_attr);
	return ret;
}

static const struct of_device_id k3_chipinfo_of_match[] = {
	{ .compatible = "ti,am654-chipid", },
	{ /* sentinel */ },
};

static struct platform_driver k3_chipinfo_driver = {
	.driver = {
		.name = "k3-chipinfo",
		.of_match_table = k3_chipinfo_of_match,
	},
	.probe = k3_chipinfo_probe,
};

static int __init k3_chipinfo_init(void)
{
	return platform_driver_register(&k3_chipinfo_driver);
}
subsys_initcall(k3_chipinfo_init);
