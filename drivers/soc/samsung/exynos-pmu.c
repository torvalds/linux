// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2011-2014 Samsung Electronics Co., Ltd.
//		http://www.samsung.com/
//
// Exynos - CPU PMU(Power Management Unit) support

#include <linux/array_size.h>
#include <linux/arm-smccc.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/mfd/core.h>
#include <linux/mfd/syscon.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/regmap.h>

#include <linux/soc/samsung/exynos-regs-pmu.h>
#include <linux/soc/samsung/exynos-pmu.h>

#include "exynos-pmu.h"

#define PMUALIVE_MASK			GENMASK(13, 0)
#define TENSOR_SET_BITS			(BIT(15) | BIT(14))
#define TENSOR_CLR_BITS			BIT(15)
#define TENSOR_SMC_PMU_SEC_REG		0x82000504
#define TENSOR_PMUREG_READ		0
#define TENSOR_PMUREG_WRITE		1
#define TENSOR_PMUREG_RMW		2

struct exynos_pmu_context {
	struct device *dev;
	const struct exynos_pmu_data *pmu_data;
	struct regmap *pmureg;
};

void __iomem *pmu_base_addr;
static struct exynos_pmu_context *pmu_context;
/* forward declaration */
static struct platform_driver exynos_pmu_driver;

/*
 * Tensor SoCs are configured so that PMU_ALIVE registers can only be written
 * from EL3, but are still read accessible. As Linux needs to write some of
 * these registers, the following functions are provided and exposed via
 * regmap.
 *
 * Note: This SMC interface is known to be implemented on gs101 and derivative
 * SoCs.
 */

/* Write to a protected PMU register. */
static int tensor_sec_reg_write(void *context, unsigned int reg,
				unsigned int val)
{
	struct arm_smccc_res res;
	unsigned long pmu_base = (unsigned long)context;

	arm_smccc_smc(TENSOR_SMC_PMU_SEC_REG, pmu_base + reg,
		      TENSOR_PMUREG_WRITE, val, 0, 0, 0, 0, &res);

	/* returns -EINVAL if access isn't allowed or 0 */
	if (res.a0)
		pr_warn("%s(): SMC failed: %d\n", __func__, (int)res.a0);

	return (int)res.a0;
}

/* Read/Modify/Write a protected PMU register. */
static int tensor_sec_reg_rmw(void *context, unsigned int reg,
			      unsigned int mask, unsigned int val)
{
	struct arm_smccc_res res;
	unsigned long pmu_base = (unsigned long)context;

	arm_smccc_smc(TENSOR_SMC_PMU_SEC_REG, pmu_base + reg,
		      TENSOR_PMUREG_RMW, mask, val, 0, 0, 0, &res);

	/* returns -EINVAL if access isn't allowed or 0 */
	if (res.a0)
		pr_warn("%s(): SMC failed: %d\n", __func__, (int)res.a0);

	return (int)res.a0;
}

/*
 * Read a protected PMU register. All PMU registers can be read by Linux.
 * Note: The SMC read register is not used, as only registers that can be
 * written are readable via SMC.
 */
static int tensor_sec_reg_read(void *context, unsigned int reg,
			       unsigned int *val)
{
	*val = pmu_raw_readl(reg);
	return 0;
}

/*
 * For SoCs that have set/clear bit hardware this function can be used when
 * the PMU register will be accessed by multiple masters.
 *
 * For example, to set bits 13:8 in PMU reg offset 0x3e80
 * tensor_set_bits_atomic(ctx, 0x3e80, 0x3f00, 0x3f00);
 *
 * Set bit 8, and clear bits 13:9 PMU reg offset 0x3e80
 * tensor_set_bits_atomic(0x3e80, 0x100, 0x3f00);
 */
static int tensor_set_bits_atomic(void *ctx, unsigned int offset, u32 val,
				  u32 mask)
{
	int ret;
	unsigned int i;

	for (i = 0; i < 32; i++) {
		if (!(mask & BIT(i)))
			continue;

		offset &= ~TENSOR_SET_BITS;

		if (val & BIT(i))
			offset |= TENSOR_SET_BITS;
		else
			offset |= TENSOR_CLR_BITS;

		ret = tensor_sec_reg_write(ctx, offset, i);
		if (ret)
			return ret;
	}
	return 0;
}

static bool tensor_is_atomic(unsigned int reg)
{
	/*
	 * Use atomic operations for PMU_ALIVE registers (offset 0~0x3FFF)
	 * as the target registers can be accessed by multiple masters. SFRs
	 * that don't support atomic are added to the switch statement below.
	 */
	if (reg > PMUALIVE_MASK)
		return false;

	switch (reg) {
	case GS101_SYSIP_DAT0:
	case GS101_SYSTEM_CONFIGURATION:
		return false;
	default:
		return true;
	}
}

static int tensor_sec_update_bits(void *ctx, unsigned int reg,
				  unsigned int mask, unsigned int val)
{

	if (!tensor_is_atomic(reg))
		return tensor_sec_reg_rmw(ctx, reg, mask, val);

	return tensor_set_bits_atomic(ctx, reg, val, mask);
}

void pmu_raw_writel(u32 val, u32 offset)
{
	writel_relaxed(val, pmu_base_addr + offset);
}

u32 pmu_raw_readl(u32 offset)
{
	return readl_relaxed(pmu_base_addr + offset);
}

void exynos_sys_powerdown_conf(enum sys_powerdown mode)
{
	unsigned int i;
	const struct exynos_pmu_data *pmu_data;

	if (!pmu_context || !pmu_context->pmu_data)
		return;

	pmu_data = pmu_context->pmu_data;

	if (pmu_data->powerdown_conf)
		pmu_data->powerdown_conf(mode);

	if (pmu_data->pmu_config) {
		for (i = 0; (pmu_data->pmu_config[i].offset != PMU_TABLE_END); i++)
			pmu_raw_writel(pmu_data->pmu_config[i].val[mode],
					pmu_data->pmu_config[i].offset);
	}

	if (pmu_data->powerdown_conf_extra)
		pmu_data->powerdown_conf_extra(mode);

	if (pmu_data->pmu_config_extra) {
		for (i = 0; pmu_data->pmu_config_extra[i].offset != PMU_TABLE_END; i++)
			pmu_raw_writel(pmu_data->pmu_config_extra[i].val[mode],
				       pmu_data->pmu_config_extra[i].offset);
	}
}

/*
 * Split the data between ARM architectures because it is relatively big
 * and useless on other arch.
 */
#ifdef CONFIG_EXYNOS_PMU_ARM_DRIVERS
#define exynos_pmu_data_arm_ptr(data)	(&data)
#else
#define exynos_pmu_data_arm_ptr(data)	NULL
#endif

static const struct regmap_config regmap_smccfg = {
	.name = "pmu_regs",
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,
	.use_single_read = true,
	.use_single_write = true,
	.reg_read = tensor_sec_reg_read,
	.reg_write = tensor_sec_reg_write,
	.reg_update_bits = tensor_sec_update_bits,
};

static const struct exynos_pmu_data gs101_pmu_data = {
	.pmu_secure = true
};

/*
 * PMU platform driver and devicetree bindings.
 */
static const struct of_device_id exynos_pmu_of_device_ids[] = {
	{
		.compatible = "google,gs101-pmu",
		.data = &gs101_pmu_data,
	}, {
		.compatible = "samsung,exynos3250-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos3250_pmu_data),
	}, {
		.compatible = "samsung,exynos4210-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos4210_pmu_data),
	}, {
		.compatible = "samsung,exynos4212-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos4212_pmu_data),
	}, {
		.compatible = "samsung,exynos4412-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos4412_pmu_data),
	}, {
		.compatible = "samsung,exynos5250-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos5250_pmu_data),
	}, {
		.compatible = "samsung,exynos5410-pmu",
	}, {
		.compatible = "samsung,exynos5420-pmu",
		.data = exynos_pmu_data_arm_ptr(exynos5420_pmu_data),
	}, {
		.compatible = "samsung,exynos5433-pmu",
	}, {
		.compatible = "samsung,exynos7-pmu",
	}, {
		.compatible = "samsung,exynos850-pmu",
	},
	{ /*sentinel*/ },
};

static const struct mfd_cell exynos_pmu_devs[] = {
	{ .name = "exynos-clkout", },
};

/**
 * exynos_get_pmu_regmap() - Obtain pmureg regmap
 *
 * Find the pmureg regmap previously configured in probe() and return regmap
 * pointer.
 *
 * Return: A pointer to regmap if found or ERR_PTR error value.
 */
struct regmap *exynos_get_pmu_regmap(void)
{
	struct device_node *np = of_find_matching_node(NULL,
						      exynos_pmu_of_device_ids);
	if (np)
		return exynos_get_pmu_regmap_by_phandle(np, NULL);
	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_GPL(exynos_get_pmu_regmap);

/**
 * exynos_get_pmu_regmap_by_phandle() - Obtain pmureg regmap via phandle
 * @np: Device node holding PMU phandle property
 * @propname: Name of property holding phandle value
 *
 * Find the pmureg regmap previously configured in probe() and return regmap
 * pointer.
 *
 * Return: A pointer to regmap if found or ERR_PTR error value.
 */
struct regmap *exynos_get_pmu_regmap_by_phandle(struct device_node *np,
						const char *propname)
{
	struct device_node *pmu_np;
	struct device *dev;

	if (propname)
		pmu_np = of_parse_phandle(np, propname, 0);
	else
		pmu_np = np;

	if (!pmu_np)
		return ERR_PTR(-ENODEV);

	/*
	 * Determine if exynos-pmu device has probed and therefore regmap
	 * has been created and can be returned to the caller. Otherwise we
	 * return -EPROBE_DEFER.
	 */
	dev = driver_find_device_by_of_node(&exynos_pmu_driver.driver,
					    (void *)pmu_np);

	if (propname)
		of_node_put(pmu_np);

	if (!dev)
		return ERR_PTR(-EPROBE_DEFER);

	return syscon_node_to_regmap(pmu_np);
}
EXPORT_SYMBOL_GPL(exynos_get_pmu_regmap_by_phandle);

static int exynos_pmu_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap_config pmu_regmcfg;
	struct regmap *regmap;
	struct resource *res;
	int ret;

	pmu_base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pmu_base_addr))
		return PTR_ERR(pmu_base_addr);

	pmu_context = devm_kzalloc(&pdev->dev,
			sizeof(struct exynos_pmu_context),
			GFP_KERNEL);
	if (!pmu_context)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	pmu_context->pmu_data = of_device_get_match_data(dev);

	/* For SoCs that secure PMU register writes use custom regmap */
	if (pmu_context->pmu_data && pmu_context->pmu_data->pmu_secure) {
		pmu_regmcfg = regmap_smccfg;
		pmu_regmcfg.max_register = resource_size(res) -
					   pmu_regmcfg.reg_stride;
		/* Need physical address for SMC call */
		regmap = devm_regmap_init(dev, NULL,
					  (void *)(uintptr_t)res->start,
					  &pmu_regmcfg);

		if (IS_ERR(regmap))
			return dev_err_probe(&pdev->dev, PTR_ERR(regmap),
					     "regmap init failed\n");

		ret = of_syscon_register_regmap(dev->of_node, regmap);
		if (ret)
			return ret;
	} else {
		/* let syscon create mmio regmap */
		regmap = syscon_node_to_regmap(dev->of_node);
		if (IS_ERR(regmap))
			return dev_err_probe(&pdev->dev, PTR_ERR(regmap),
					     "syscon_node_to_regmap failed\n");
	}

	pmu_context->pmureg = regmap;
	pmu_context->dev = dev;

	if (pmu_context->pmu_data && pmu_context->pmu_data->pmu_init)
		pmu_context->pmu_data->pmu_init();

	platform_set_drvdata(pdev, pmu_context);

	ret = devm_mfd_add_devices(dev, PLATFORM_DEVID_NONE, exynos_pmu_devs,
				   ARRAY_SIZE(exynos_pmu_devs), NULL, 0, NULL);
	if (ret)
		return ret;

	if (devm_of_platform_populate(dev))
		dev_err(dev, "Error populating children, reboot and poweroff might not work properly\n");

	dev_dbg(dev, "Exynos PMU Driver probe done\n");
	return 0;
}

static struct platform_driver exynos_pmu_driver = {
	.driver  = {
		.name   = "exynos-pmu",
		.of_match_table = exynos_pmu_of_device_ids,
	},
	.probe = exynos_pmu_probe,
};

static int __init exynos_pmu_init(void)
{
	return platform_driver_register(&exynos_pmu_driver);

}
postcore_initcall(exynos_pmu_init);
