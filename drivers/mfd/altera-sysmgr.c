// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 2018-2019, Intel Corporation.
 *  Copyright (C) 2012 Freescale Semiconductor, Inc.
 *  Copyright (C) 2012 Linaro Ltd.
 *
 *  Based on syscon driver.
 */

#include <linux/arm-smccc.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mfd/altera-sysmgr.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/slab.h>

/**
 * struct altr_sysmgr - Altera SOCFPGA System Manager
 * @regmap: the regmap used for System Manager accesses.
 */
struct altr_sysmgr {
	struct regmap   *regmap;
};

static struct platform_driver altr_sysmgr_driver;

/**
 * s10_protected_reg_write
 * Write to a protected SMC register.
 * @base: Base address of System Manager
 * @reg:  Address offset of register
 * @val:  Value to write
 * Return: INTEL_SIP_SMC_STATUS_OK (0) on success
 *	   INTEL_SIP_SMC_REG_ERROR on error
 *	   INTEL_SIP_SMC_RETURN_UNKNOWN_FUNCTION if not supported
 */
static int s10_protected_reg_write(void *base,
				   unsigned int reg, unsigned int val)
{
	struct arm_smccc_res result;
	unsigned long sysmgr_base = (unsigned long)base;

	arm_smccc_smc(INTEL_SIP_SMC_REG_WRITE, sysmgr_base + reg,
		      val, 0, 0, 0, 0, 0, &result);

	return (int)result.a0;
}

/**
 * s10_protected_reg_read
 * Read the status of a protected SMC register
 * @base: Base address of System Manager.
 * @reg:  Address of register
 * @val:  Value read.
 * Return: INTEL_SIP_SMC_STATUS_OK (0) on success
 *	   INTEL_SIP_SMC_REG_ERROR on error
 *	   INTEL_SIP_SMC_RETURN_UNKNOWN_FUNCTION if not supported
 */
static int s10_protected_reg_read(void *base,
				  unsigned int reg, unsigned int *val)
{
	struct arm_smccc_res result;
	unsigned long sysmgr_base = (unsigned long)base;

	arm_smccc_smc(INTEL_SIP_SMC_REG_READ, sysmgr_base + reg,
		      0, 0, 0, 0, 0, 0, &result);

	*val = (unsigned int)result.a1;

	return (int)result.a0;
}

static struct regmap_config altr_sysmgr_regmap_cfg = {
	.name = "altr_sysmgr",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.use_single_read = true,
	.use_single_write = true,
};

/**
 * altr_sysmgr_regmap_lookup_by_phandle
 * Find the sysmgr previous configured in probe() and return regmap property.
 * Return: regmap if found or error if not found.
 *
 * @np: Pointer to device's Device Tree node
 * @property: Device Tree property name which references the sysmgr
 */
struct regmap *altr_sysmgr_regmap_lookup_by_phandle(struct device_node *np,
						    const char *property)
{
	struct device *dev;
	struct altr_sysmgr *sysmgr;
	struct device_node *sysmgr_np;

	if (property)
		sysmgr_np = of_parse_phandle(np, property, 0);
	else
		sysmgr_np = np;

	if (!sysmgr_np)
		return ERR_PTR(-ENODEV);

	dev = driver_find_device_by_of_node(&altr_sysmgr_driver.driver,
					    (void *)sysmgr_np);
	of_node_put(sysmgr_np);
	if (!dev)
		return ERR_PTR(-EPROBE_DEFER);

	sysmgr = dev_get_drvdata(dev);

	return sysmgr->regmap;
}
EXPORT_SYMBOL_GPL(altr_sysmgr_regmap_lookup_by_phandle);

static int sysmgr_probe(struct platform_device *pdev)
{
	struct altr_sysmgr *sysmgr;
	struct regmap *regmap;
	struct resource *res;
	struct regmap_config sysmgr_config = altr_sysmgr_regmap_cfg;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void __iomem *base;

	sysmgr = devm_kzalloc(dev, sizeof(*sysmgr), GFP_KERNEL);
	if (!sysmgr)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOENT;

	sysmgr_config.max_register = resource_size(res) -
				     sysmgr_config.reg_stride;
	if (of_device_is_compatible(np, "altr,sys-mgr-s10")) {
		sysmgr_config.reg_read = s10_protected_reg_read;
		sysmgr_config.reg_write = s10_protected_reg_write;

		/* Need physical address for SMCC call */
		regmap = devm_regmap_init(dev, NULL,
					  (void *)(uintptr_t)res->start,
					  &sysmgr_config);
	} else {
		base = devm_ioremap(dev, res->start, resource_size(res));
		if (!base)
			return -ENOMEM;

		sysmgr_config.max_register = resource_size(res) - 4;
		regmap = devm_regmap_init_mmio(dev, base, &sysmgr_config);
	}

	if (IS_ERR(regmap)) {
		pr_err("regmap init failed\n");
		return PTR_ERR(regmap);
	}

	sysmgr->regmap = regmap;

	platform_set_drvdata(pdev, sysmgr);

	return 0;
}

static const struct of_device_id altr_sysmgr_of_match[] = {
	{ .compatible = "altr,sys-mgr" },
	{ .compatible = "altr,sys-mgr-s10" },
	{},
};
MODULE_DEVICE_TABLE(of, altr_sysmgr_of_match);

static struct platform_driver altr_sysmgr_driver = {
	.probe =  sysmgr_probe,
	.driver = {
		.name = "altr,system_manager",
		.of_match_table = altr_sysmgr_of_match,
	},
};

static int __init altr_sysmgr_init(void)
{
	return platform_driver_register(&altr_sysmgr_driver);
}
core_initcall(altr_sysmgr_init);

static void __exit altr_sysmgr_exit(void)
{
	platform_driver_unregister(&altr_sysmgr_driver);
}
module_exit(altr_sysmgr_exit);

MODULE_AUTHOR("Thor Thayer <>");
MODULE_DESCRIPTION("SOCFPGA System Manager driver");
MODULE_LICENSE("GPL v2");
