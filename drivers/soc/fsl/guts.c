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

struct fsl_soc_die_attr {
	char	*die;
	u32	svr;
	u32	mask;
};

struct fsl_soc_data {
	const char *sfp_compat;
	u32 uid_offset;
};

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
	/* Die: LX2160A, SoC: LX2160A/LX2120A/LX2080A */
	{ .die          = "LX2160A",
	  .svr          = 0x87360000,
	  .mask         = 0xff3f0000,
	},
	/* Die: LS1028A, SoC: LS1028A */
	{ .die          = "LS1028A",
	  .svr          = 0x870b0000,
	  .mask         = 0xff3f0000,
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
	}
	return NULL;
}

static u64 fsl_guts_get_soc_uid(const char *compat, unsigned int offset)
{
	struct device_node *np;
	void __iomem *sfp_base;
	u64 uid;

	np = of_find_compatible_node(NULL, NULL, compat);
	if (!np)
		return 0;

	sfp_base = of_iomap(np, 0);
	if (!sfp_base) {
		of_node_put(np);
		return 0;
	}

	uid = ioread32(sfp_base + offset);
	uid <<= 32;
	uid |= ioread32(sfp_base + offset + 4);

	iounmap(sfp_base);
	of_node_put(np);

	return uid;
}

static const struct fsl_soc_data ls1028a_data = {
	.sfp_compat = "fsl,ls1028a-sfp",
	.uid_offset = 0x21c,
};

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
	{ .compatible = "fsl,lx2160a-dcfg", },
	{ .compatible = "fsl,ls1028a-dcfg", .data = &ls1028a_data},
	{}
};

static int __init fsl_guts_init(void)
{
	struct soc_device_attribute *soc_dev_attr;
	static struct soc_device *soc_dev;
	const struct fsl_soc_die_attr *soc_die;
	const struct fsl_soc_data *soc_data;
	const struct of_device_id *match;
	struct ccsr_guts __iomem *regs;
	const char *machine = NULL;
	struct device_node *np;
	bool little_endian;
	u64 soc_uid = 0;
	u32 svr;
	int ret;

	np = of_find_matching_node_and_match(NULL, fsl_guts_of_match, &match);
	if (!np)
		return 0;
	soc_data = match->data;

	regs = of_iomap(np, 0);
	if (!regs) {
		of_node_put(np);
		return -ENOMEM;
	}

	little_endian = of_property_read_bool(np, "little-endian");
	if (little_endian)
		svr = ioread32(&regs->svr);
	else
		svr = ioread32be(&regs->svr);
	iounmap(regs);
	of_node_put(np);

	/* Register soc device */
	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return -ENOMEM;

	if (of_property_read_string(of_root, "model", &machine))
		of_property_read_string_index(of_root, "compatible", 0, &machine);
	if (machine) {
		soc_dev_attr->machine = kstrdup(machine, GFP_KERNEL);
		if (!soc_dev_attr->machine)
			goto err_nomem;
	}

	soc_die = fsl_soc_die_match(svr, fsl_soc_die);
	if (soc_die) {
		soc_dev_attr->family = kasprintf(GFP_KERNEL, "QorIQ %s",
						 soc_die->die);
	} else {
		soc_dev_attr->family = kasprintf(GFP_KERNEL, "QorIQ");
	}
	if (!soc_dev_attr->family)
		goto err_nomem;

	soc_dev_attr->soc_id = kasprintf(GFP_KERNEL, "svr:0x%08x", svr);
	if (!soc_dev_attr->soc_id)
		goto err_nomem;

	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%d.%d",
					   (svr >>  4) & 0xf, svr & 0xf);
	if (!soc_dev_attr->revision)
		goto err_nomem;

	if (soc_data)
		soc_uid = fsl_guts_get_soc_uid(soc_data->sfp_compat,
					       soc_data->uid_offset);
	if (soc_uid)
		soc_dev_attr->serial_number = kasprintf(GFP_KERNEL, "%016llX",
							soc_uid);

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR(soc_dev)) {
		ret = PTR_ERR(soc_dev);
		goto err;
	}

	pr_info("Machine: %s\n", soc_dev_attr->machine);
	pr_info("SoC family: %s\n", soc_dev_attr->family);
	pr_info("SoC ID: %s, Revision: %s\n",
		soc_dev_attr->soc_id, soc_dev_attr->revision);

	return 0;

err_nomem:
	ret = -ENOMEM;
err:
	kfree(soc_dev_attr->machine);
	kfree(soc_dev_attr->family);
	kfree(soc_dev_attr->soc_id);
	kfree(soc_dev_attr->revision);
	kfree(soc_dev_attr->serial_number);
	kfree(soc_dev_attr);

	return ret;
}
core_initcall(fsl_guts_init);
