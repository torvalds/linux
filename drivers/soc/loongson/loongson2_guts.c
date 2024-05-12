// SPDX-License-Identifier: GPL-2.0+
/*
 * Author: Yinbo Zhu <zhuyinbo@loongson.cn>
 * Copyright (C) 2022-2023 Loongson Technology Corporation Limited
 */

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of_fdt.h>
#include <linux/sys_soc.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

static struct soc_device_attribute soc_dev_attr;
static struct soc_device *soc_dev;

/*
 * Global Utility Registers.
 *
 * Not all registers defined in this structure are available on all chips, so
 * you are expected to know whether a given register actually exists on your
 * chip before you access it.
 *
 * Also, some registers are similar on different chips but have slightly
 * different names.  In these cases, one name is chosen to avoid extraneous
 * #ifdefs.
 */
struct scfg_guts {
	u32     svr;            /* Version Register */
	u8      res0[4];
	u16     feature;        /* Feature Register */
	u32     vendor;         /* Vendor Register */
	u8      res1[6];
	u32     id;
	u8      res2[0x3ff8 - 0x18];
	u32     chip;
};

static struct guts {
	struct scfg_guts __iomem *regs;
	bool little_endian;
} *guts;

struct loongson2_soc_die_attr {
	char	*die;
	u32	svr;
	u32	mask;
};

/* SoC die attribute definition for Loongson-2 platform */
static const struct loongson2_soc_die_attr loongson2_soc_die[] = {

	/*
	 * LoongArch-based SoCs Loongson-2 Series
	 */

	/* Die: 2k1000, SoC: 2k1000 */
	{ .die		= "2K1000",
	  .svr		= 0x00000013,
	  .mask		= 0x000000ff,
	},
	{ },
};

static const struct loongson2_soc_die_attr *loongson2_soc_die_match(
	u32 svr, const struct loongson2_soc_die_attr *matches)
{
	while (matches->svr) {
		if (matches->svr == (svr & matches->mask))
			return matches;
		matches++;
	};

	return NULL;
}

static u32 loongson2_guts_get_svr(void)
{
	u32 svr = 0;

	if (!guts || !guts->regs)
		return svr;

	if (guts->little_endian)
		svr = ioread32(&guts->regs->svr);
	else
		svr = ioread32be(&guts->regs->svr);

	return svr;
}

static int loongson2_guts_probe(struct platform_device *pdev)
{
	struct device_node *root, *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct resource *res;
	const struct loongson2_soc_die_attr *soc_die;
	const char *machine;
	u32 svr;

	/* Initialize guts */
	guts = devm_kzalloc(dev, sizeof(*guts), GFP_KERNEL);
	if (!guts)
		return -ENOMEM;

	guts->little_endian = of_property_read_bool(np, "little-endian");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	guts->regs = ioremap(res->start, res->end - res->start + 1);
	if (IS_ERR(guts->regs))
		return PTR_ERR(guts->regs);

	/* Register soc device */
	root = of_find_node_by_path("/");
	if (of_property_read_string(root, "model", &machine))
		of_property_read_string_index(root, "compatible", 0, &machine);
	of_node_put(root);
	if (machine)
		soc_dev_attr.machine = devm_kstrdup(dev, machine, GFP_KERNEL);

	svr = loongson2_guts_get_svr();
	soc_die = loongson2_soc_die_match(svr, loongson2_soc_die);
	if (soc_die) {
		soc_dev_attr.family = devm_kasprintf(dev, GFP_KERNEL,
						     "Loongson %s", soc_die->die);
	} else {
		soc_dev_attr.family = devm_kasprintf(dev, GFP_KERNEL, "Loongson");
	}
	if (!soc_dev_attr.family)
		return -ENOMEM;
	soc_dev_attr.soc_id = devm_kasprintf(dev, GFP_KERNEL,
					     "svr:0x%08x", svr);
	if (!soc_dev_attr.soc_id)
		return -ENOMEM;
	soc_dev_attr.revision = devm_kasprintf(dev, GFP_KERNEL, "%d.%d",
					       (svr >>  4) & 0xf, svr & 0xf);
	if (!soc_dev_attr.revision)
		return -ENOMEM;

	soc_dev = soc_device_register(&soc_dev_attr);
	if (IS_ERR(soc_dev))
		return PTR_ERR(soc_dev);

	pr_info("Machine: %s\n", soc_dev_attr.machine);
	pr_info("SoC family: %s\n", soc_dev_attr.family);
	pr_info("SoC ID: %s, Revision: %s\n",
		soc_dev_attr.soc_id, soc_dev_attr.revision);

	return 0;
}

static int loongson2_guts_remove(struct platform_device *dev)
{
	soc_device_unregister(soc_dev);

	return 0;
}

/*
 * Table for matching compatible strings, for device tree
 * guts node, for Loongson-2 SoCs.
 */
static const struct of_device_id loongson2_guts_of_match[] = {
	{ .compatible = "loongson,ls2k-chipid", },
	{}
};
MODULE_DEVICE_TABLE(of, loongson2_guts_of_match);

static struct platform_driver loongson2_guts_driver = {
	.driver = {
		.name = "loongson2-guts",
		.of_match_table = loongson2_guts_of_match,
	},
	.probe = loongson2_guts_probe,
	.remove = loongson2_guts_remove,
};

static int __init loongson2_guts_init(void)
{
	return platform_driver_register(&loongson2_guts_driver);
}
core_initcall(loongson2_guts_init);

static void __exit loongson2_guts_exit(void)
{
	platform_driver_unregister(&loongson2_guts_driver);
}
module_exit(loongson2_guts_exit);

MODULE_DESCRIPTION("Loongson2 GUTS driver");
MODULE_LICENSE("GPL");
