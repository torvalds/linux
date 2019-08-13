// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Samsung Electronics Co., Ltd.
 *	      http://www.samsung.com/
 *
 * EXYNOS - CHIP ID support
 * Author: Pankaj Dubey <pankaj.dubey@samsung.com>
 * Author: Bartlomiej Zolnierkiewicz <b.zolnierkie@samsung.com>
 */

#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>

#define EXYNOS_SUBREV_MASK	(0xF << 4)
#define EXYNOS_MAINREV_MASK	(0xF << 0)
#define EXYNOS_REV_MASK		(EXYNOS_SUBREV_MASK | EXYNOS_MAINREV_MASK)
#define EXYNOS_MASK		0xFFFFF000

static const struct exynos_soc_id {
	const char *name;
	unsigned int id;
} soc_ids[] = {
	{ "EXYNOS3250", 0xE3472000 },
	{ "EXYNOS4210", 0x43200000 },	/* EVT0 revision */
	{ "EXYNOS4210", 0x43210000 },
	{ "EXYNOS4212", 0x43220000 },
	{ "EXYNOS4412", 0xE4412000 },
	{ "EXYNOS5250", 0x43520000 },
	{ "EXYNOS5260", 0xE5260000 },
	{ "EXYNOS5410", 0xE5410000 },
	{ "EXYNOS5420", 0xE5420000 },
	{ "EXYNOS5440", 0xE5440000 },
	{ "EXYNOS5800", 0xE5422000 },
	{ "EXYNOS7420", 0xE7420000 },
	{ "EXYNOS5433", 0xE5433000 },
};

static const char * __init product_id_to_soc_id(unsigned int product_id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(soc_ids); i++)
		if ((product_id & EXYNOS_MASK) == soc_ids[i].id)
			return soc_ids[i].name;
	return NULL;
}

int __init exynos_chipid_early_init(void)
{
	struct soc_device_attribute *soc_dev_attr;
	void __iomem *exynos_chipid_base;
	struct soc_device *soc_dev;
	struct device_node *root;
	struct device_node *np;
	u32 product_id;
	u32 revision;

	/* look up for chipid node */
	np = of_find_compatible_node(NULL, NULL, "samsung,exynos4210-chipid");
	if (!np)
		return -ENODEV;

	exynos_chipid_base = of_iomap(np, 0);
	of_node_put(np);

	if (!exynos_chipid_base) {
		pr_err("Failed to map SoC chipid\n");
		return -ENXIO;
	}

	product_id = readl_relaxed(exynos_chipid_base);
	revision = product_id & EXYNOS_REV_MASK;
	iounmap(exynos_chipid_base);

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	soc_dev_attr->family = "Samsung Exynos";

	root = of_find_node_by_path("/");
	of_property_read_string(root, "model", &soc_dev_attr->machine);
	of_node_put(root);

	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%x", revision);
	soc_dev_attr->soc_id = product_id_to_soc_id(product_id);
	if (!soc_dev_attr->soc_id) {
		pr_err("Unknown SoC\n");
		return -ENODEV;
	}

	/* please note that the actual registration will be deferred */
	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		kfree(soc_dev_attr->revision);
		kfree(soc_dev_attr);
		return PTR_ERR(soc_dev);
	}

	/* it is too early to use dev_info() here (soc_dev is NULL) */
	pr_info("soc soc0: Exynos: CPU[%s] PRO_ID[0x%x] REV[0x%x] Detected\n",
		soc_dev_attr->soc_id, product_id, revision);

	return 0;
}
early_initcall(exynos_chipid_early_init);
