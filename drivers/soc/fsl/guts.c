// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Freescale QorIQ Platforms GUTS Driver
 *
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 */

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/of_fdt.h>
#include <linux/sys_soc.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/fsl/guts.h>

struct guts {
	struct ccsr_guts __iomem *regs;
	bool little_endian;
};

struct fsl_soc_die_attr {
	char	*die;
	u32	svr;
	u32	mask;
};

static struct guts *guts;
static struct soc_device_attribute soc_dev_attr;
static struct soc_device *soc_dev;
static struct device_node *root;


/* SoC die attribute definition for QorIQ platform */
static const struct fsl_soc_die_attr fsl_soc_die[] = {
	/*
	 * Power Architecture-based SoCs T Series
	 */

	/* Die: T4240, SoC: T4240/T4160/T4080 */
	{ .die		= "T4240",
	  .svr		= 0x82400000,
	  .mask		= 0xfff00000,
	},
	/* Die: T1040, SoC: T1040/T1020/T1042/T1022 */
	{ .die		= "T1040",
	  .svr		= 0x85200000,
	  .mask		= 0xfff00000,
	},
	/* Die: T2080, SoC: T2080/T2081 */
	{ .die		= "T2080",
	  .svr		= 0x85300000,
	  .mask		= 0xfff00000,
	},
	/* Die: T1024, SoC: T1024/T1014/T1023/T1013 */
	{ .die		= "T1024",
	  .svr		= 0x85400000,
	  .mask		= 0xfff00000,
	},

	/*
	 * ARM-based SoCs LS Series
	 */

	/* Die: LS1043A, SoC: LS1043A/LS1023A */
	{ .die		= "LS1043A",
	  .svr		= 0x87920000,
	  .mask		= 0xffff0000,
	},
	/* Die: LS2080A, SoC: LS2080A/LS2040A/LS2085A */
	{ .die		= "LS2080A",
	  .svr		= 0x87010000,
	  .mask		= 0xff3f0000,
	},
	/* Die: LS1088A, SoC: LS1088A/LS1048A/LS1084A/LS1044A */
	{ .die		= "LS1088A",
	  .svr		= 0x87030000,
	  .mask		= 0xff3f0000,
	},
	/* Die: LS1012A, SoC: LS1012A */
	{ .die		= "LS1012A",
	  .svr		= 0x87040000,
	  .mask		= 0xffff0000,
	},
	/* Die: LS1046A, SoC: LS1046A/LS1026A */
	{ .die		= "LS1046A",
	  .svr		= 0x87070000,
	  .mask		= 0xffff0000,
	},
	/* Die: LS2088A, SoC: LS2088A/LS2048A/LS2084A/LS2044A */
	{ .die		= "LS2088A",
	  .svr		= 0x87090000,
	  .mask		= 0xff3f0000,
	},
	/* Die: LS1021A, SoC: LS1021A/LS1020A/LS1022A */
	{ .die		= "LS1021A",
	  .svr		= 0x87000000,
	  .mask		= 0xfff70000,
	},
	{ },
};

static const struct fsl_soc_die_attr *fsl_soc_die_match(
	u32 svr, const struct fsl_soc_die_attr *matches)
{
	while (matches->svr) {
		if (matches->svr == (svr & matches->mask))
			return matches;
		matches++;
	};
	return NULL;
}

static u32 fsl_guts_get_svr(void)
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

static int fsl_guts_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct resource *res;
	const struct fsl_soc_die_attr *soc_die;
	const char *machine;
	u32 svr;

	/* Initialize guts */
	guts = devm_kzalloc(dev, sizeof(*guts), GFP_KERNEL);
	if (!guts)
		return -ENOMEM;

	guts->little_endian = of_property_read_bool(np, "little-endian");

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	guts->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(guts->regs))
		return PTR_ERR(guts->regs);

	/* Register soc device */
	root = of_find_node_by_path("/");
	if (of_property_read_string(root, "model", &machine))
		of_property_read_string_index(root, "compatible", 0, &machine);
	if (machine)
		soc_dev_attr.machine = machine;

	svr = fsl_guts_get_svr();
	soc_die = fsl_soc_die_match(svr, fsl_soc_die);
	if (soc_die) {
		soc_dev_attr.family = devm_kasprintf(dev, GFP_KERNEL,
						     "QorIQ %s", soc_die->die);
	} else {
		soc_dev_attr.family = devm_kasprintf(dev, GFP_KERNEL, "QorIQ");
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

static int fsl_guts_remove(struct platform_device *dev)
{
	soc_device_unregister(soc_dev);
	of_node_put(root);
	return 0;
}

/*
 * Table for matching compatible strings, for device tree
 * guts node, for Freescale QorIQ SOCs.
 */
static const struct of_device_id fsl_guts_of_match[] = {
	{ .compatible = "fsl,qoriq-device-config-1.0", },
	{ .compatible = "fsl,qoriq-device-config-2.0", },
	{ .compatible = "fsl,p1010-guts", },
	{ .compatible = "fsl,p1020-guts", },
	{ .compatible = "fsl,p1021-guts", },
	{ .compatible = "fsl,p1022-guts", },
	{ .compatible = "fsl,p1023-guts", },
	{ .compatible = "fsl,p2020-guts", },
	{ .compatible = "fsl,bsc9131-guts", },
	{ .compatible = "fsl,bsc9132-guts", },
	{ .compatible = "fsl,mpc8536-guts", },
	{ .compatible = "fsl,mpc8544-guts", },
	{ .compatible = "fsl,mpc8548-guts", },
	{ .compatible = "fsl,mpc8568-guts", },
	{ .compatible = "fsl,mpc8569-guts", },
	{ .compatible = "fsl,mpc8572-guts", },
	{ .compatible = "fsl,ls1021a-dcfg", },
	{ .compatible = "fsl,ls1043a-dcfg", },
	{ .compatible = "fsl,ls2080a-dcfg", },
	{ .compatible = "fsl,ls1088a-dcfg", },
	{ .compatible = "fsl,ls1012a-dcfg", },
	{ .compatible = "fsl,ls1046a-dcfg", },
	{}
};
MODULE_DEVICE_TABLE(of, fsl_guts_of_match);

static struct platform_driver fsl_guts_driver = {
	.driver = {
		.name = "fsl-guts",
		.of_match_table = fsl_guts_of_match,
	},
	.probe = fsl_guts_probe,
	.remove = fsl_guts_remove,
};

static int __init fsl_guts_init(void)
{
	return platform_driver_register(&fsl_guts_driver);
}
core_initcall(fsl_guts_init);

static void __exit fsl_guts_exit(void)
{
	platform_driver_unregister(&fsl_guts_driver);
}
module_exit(fsl_guts_exit);
