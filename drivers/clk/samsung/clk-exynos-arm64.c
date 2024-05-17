// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 Linaro Ltd.
 * Copyright (C) 2021 Dávid Virág <virag.david003@gmail.com>
 * Author: Sam Protsenko <semen.protsenko@linaro.org>
 * Author: Dávid Virág <virag.david003@gmail.com>
 *
 * This file contains shared functions used by some arm64 Exynos SoCs,
 * such as Exynos7885 or Exynos850 to register and init CMUs.
 */
#include <linux/clk.h>
#include <linux/of_address.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include "clk-exynos-arm64.h"

/* PLL register bits */
#define PLL_CON1_MANUAL		BIT(1)

/* Gate register bits */
#define GATE_MANUAL		BIT(20)
#define GATE_ENABLE_HWACG	BIT(28)

/* PLL_CONx_PLL register offsets range */
#define PLL_CON_OFF_START	0x100
#define PLL_CON_OFF_END		0x600

/* Gate register offsets range */
#define GATE_OFF_START		0x2000
#define GATE_OFF_END		0x2fff

struct exynos_arm64_cmu_data {
	struct samsung_clk_reg_dump *clk_save;
	unsigned int nr_clk_save;
	const struct samsung_clk_reg_dump *clk_suspend;
	unsigned int nr_clk_suspend;

	struct clk *clk;
	struct clk **pclks;
	int nr_pclks;

	struct samsung_clk_provider *ctx;
};

/* Check if the register offset is a GATE register */
static bool is_gate_reg(unsigned long off)
{
	return off >= GATE_OFF_START && off <= GATE_OFF_END;
}

/* Check if the register offset is a PLL_CONx register */
static bool is_pll_conx_reg(unsigned long off)
{
	return off >= PLL_CON_OFF_START && off <= PLL_CON_OFF_END;
}

/* Check if the register offset is a PLL_CON1 register */
static bool is_pll_con1_reg(unsigned long off)
{
	return is_pll_conx_reg(off) && (off & 0xf) == 0x4 && !(off & 0x10);
}

/**
 * exynos_arm64_init_clocks - Set clocks initial configuration
 * @np:		CMU device tree node with "reg" property (CMU addr)
 * @cmu:	CMU data
 *
 * Set manual control mode for all gate and PLL clocks.
 */
static void __init exynos_arm64_init_clocks(struct device_node *np,
					    const struct samsung_cmu_info *cmu)
{
	const unsigned long *reg_offs = cmu->clk_regs;
	size_t reg_offs_len = cmu->nr_clk_regs;
	void __iomem *reg_base;
	size_t i;

	reg_base = of_iomap(np, 0);
	if (!reg_base)
		panic("%s: failed to map registers\n", __func__);

	for (i = 0; i < reg_offs_len; ++i) {
		void __iomem *reg = reg_base + reg_offs[i];
		u32 val;

		if (cmu->manual_plls && is_pll_con1_reg(reg_offs[i])) {
			writel(PLL_CON1_MANUAL, reg);
		} else if (is_gate_reg(reg_offs[i])) {
			val = readl(reg);
			val |= GATE_MANUAL;
			val &= ~GATE_ENABLE_HWACG;
			writel(val, reg);
		}
	}

	iounmap(reg_base);
}

/**
 * exynos_arm64_enable_bus_clk - Enable parent clock of specified CMU
 *
 * @dev:	Device object; may be NULL if this function is not being
 *		called from platform driver probe function
 * @np:		CMU device tree node
 * @cmu:	CMU data
 *
 * Keep CMU parent clock running (needed for CMU registers access).
 *
 * Return: 0 on success or a negative error code on failure.
 */
static int __init exynos_arm64_enable_bus_clk(struct device *dev,
		struct device_node *np, const struct samsung_cmu_info *cmu)
{
	struct clk *parent_clk;

	if (!cmu->clk_name)
		return 0;

	if (dev) {
		struct exynos_arm64_cmu_data *data;

		parent_clk = clk_get(dev, cmu->clk_name);
		data = dev_get_drvdata(dev);
		if (data)
			data->clk = parent_clk;
	} else {
		parent_clk = of_clk_get_by_name(np, cmu->clk_name);
	}

	if (IS_ERR(parent_clk))
		return PTR_ERR(parent_clk);

	return clk_prepare_enable(parent_clk);
}

static int __init exynos_arm64_cmu_prepare_pm(struct device *dev,
		const struct samsung_cmu_info *cmu)
{
	struct exynos_arm64_cmu_data *data = dev_get_drvdata(dev);
	int i;

	data->clk_save = samsung_clk_alloc_reg_dump(cmu->clk_regs,
						    cmu->nr_clk_regs);
	if (!data->clk_save)
		return -ENOMEM;

	data->nr_clk_save = cmu->nr_clk_regs;
	data->clk_suspend = cmu->suspend_regs;
	data->nr_clk_suspend = cmu->nr_suspend_regs;
	data->nr_pclks = of_clk_get_parent_count(dev->of_node);
	if (!data->nr_pclks)
		return 0;

	data->pclks = devm_kcalloc(dev, sizeof(struct clk *), data->nr_pclks,
				   GFP_KERNEL);
	if (!data->pclks) {
		kfree(data->clk_save);
		return -ENOMEM;
	}

	for (i = 0; i < data->nr_pclks; i++) {
		struct clk *clk = of_clk_get(dev->of_node, i);

		if (IS_ERR(clk)) {
			kfree(data->clk_save);
			while (--i >= 0)
				clk_put(data->pclks[i]);
			return PTR_ERR(clk);
		}
		data->pclks[i] = clk;
	}

	return 0;
}

/**
 * exynos_arm64_register_cmu - Register specified Exynos CMU domain
 * @dev:	Device object; may be NULL if this function is not being
 *		called from platform driver probe function
 * @np:		CMU device tree node
 * @cmu:	CMU data
 *
 * Register specified CMU domain, which includes next steps:
 *
 * 1. Enable parent clock of @cmu CMU
 * 2. Set initial registers configuration for @cmu CMU clocks
 * 3. Register @cmu CMU clocks using Samsung clock framework API
 */
void __init exynos_arm64_register_cmu(struct device *dev,
		struct device_node *np, const struct samsung_cmu_info *cmu)
{
	int err;

	/*
	 * Try to boot even if the parent clock enablement fails, as it might be
	 * already enabled by bootloader.
	 */
	err = exynos_arm64_enable_bus_clk(dev, np, cmu);
	if (err)
		pr_err("%s: could not enable bus clock %s; err = %d\n",
		       __func__, cmu->clk_name, err);

	exynos_arm64_init_clocks(np, cmu);
	samsung_cmu_register_one(np, cmu);
}

/**
 * exynos_arm64_register_cmu_pm - Register Exynos CMU domain with PM support
 *
 * @pdev:	Platform device object
 * @set_manual:	If true, set gate clocks to manual mode
 *
 * It's a version of exynos_arm64_register_cmu() with PM support. Should be
 * called from probe function of platform driver.
 *
 * Return: 0 on success, or negative error code on error.
 */
int __init exynos_arm64_register_cmu_pm(struct platform_device *pdev,
					bool set_manual)
{
	const struct samsung_cmu_info *cmu;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct exynos_arm64_cmu_data *data;
	void __iomem *reg_base;
	int ret;

	cmu = of_device_get_match_data(dev);

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);

	ret = exynos_arm64_cmu_prepare_pm(dev, cmu);
	if (ret)
		return ret;

	/*
	 * Try to boot even if the parent clock enablement fails, as it might be
	 * already enabled by bootloader.
	 */
	ret = exynos_arm64_enable_bus_clk(dev, NULL, cmu);
	if (ret)
		dev_err(dev, "%s: could not enable bus clock %s; err = %d\n",
		       __func__, cmu->clk_name, ret);

	if (set_manual)
		exynos_arm64_init_clocks(np, cmu);

	reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg_base))
		return PTR_ERR(reg_base);

	data->ctx = samsung_clk_init(dev, reg_base, cmu->nr_clk_ids);

	/*
	 * Enable runtime PM here to allow the clock core using runtime PM
	 * for the registered clocks. Additionally, we increase the runtime
	 * PM usage count before registering the clocks, to prevent the
	 * clock core from runtime suspending the device.
	 */
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);

	samsung_cmu_register_clocks(data->ctx, cmu);
	samsung_clk_of_add_provider(dev->of_node, data->ctx);
	pm_runtime_put_sync(dev);

	return 0;
}

int exynos_arm64_cmu_suspend(struct device *dev)
{
	struct exynos_arm64_cmu_data *data = dev_get_drvdata(dev);
	int i;

	samsung_clk_save(data->ctx->reg_base, data->clk_save,
			 data->nr_clk_save);

	for (i = 0; i < data->nr_pclks; i++)
		clk_prepare_enable(data->pclks[i]);

	/* For suspend some registers have to be set to certain values */
	samsung_clk_restore(data->ctx->reg_base, data->clk_suspend,
			    data->nr_clk_suspend);

	for (i = 0; i < data->nr_pclks; i++)
		clk_disable_unprepare(data->pclks[i]);

	clk_disable_unprepare(data->clk);

	return 0;
}

int exynos_arm64_cmu_resume(struct device *dev)
{
	struct exynos_arm64_cmu_data *data = dev_get_drvdata(dev);
	int i;

	clk_prepare_enable(data->clk);

	for (i = 0; i < data->nr_pclks; i++)
		clk_prepare_enable(data->pclks[i]);

	samsung_clk_restore(data->ctx->reg_base, data->clk_save,
			    data->nr_clk_save);

	for (i = 0; i < data->nr_pclks; i++)
		clk_disable_unprepare(data->pclks[i]);

	return 0;
}
