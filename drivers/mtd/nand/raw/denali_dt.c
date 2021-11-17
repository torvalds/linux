// SPDX-License-Identifier: GPL-2.0
/*
 * NAND Flash Controller Device Driver for DT
 *
 * Copyright © 2011, Picochip.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "denali.h"

struct denali_dt {
	struct denali_controller controller;
	struct clk *clk;	/* core clock */
	struct clk *clk_x;	/* bus interface clock */
	struct clk *clk_ecc;	/* ECC circuit clock */
	struct reset_control *rst;	/* core reset */
	struct reset_control *rst_reg;	/* register reset */
};

struct denali_dt_data {
	unsigned int revision;
	unsigned int caps;
	unsigned int oob_skip_bytes;
	const struct nand_ecc_caps *ecc_caps;
};

NAND_ECC_CAPS_SINGLE(denali_socfpga_ecc_caps, denali_calc_ecc_bytes,
		     512, 8, 15);
static const struct denali_dt_data denali_socfpga_data = {
	.caps = DENALI_CAP_HW_ECC_FIXUP,
	.oob_skip_bytes = 2,
	.ecc_caps = &denali_socfpga_ecc_caps,
};

NAND_ECC_CAPS_SINGLE(denali_uniphier_v5a_ecc_caps, denali_calc_ecc_bytes,
		     1024, 8, 16, 24);
static const struct denali_dt_data denali_uniphier_v5a_data = {
	.caps = DENALI_CAP_HW_ECC_FIXUP |
		DENALI_CAP_DMA_64BIT,
	.oob_skip_bytes = 8,
	.ecc_caps = &denali_uniphier_v5a_ecc_caps,
};

NAND_ECC_CAPS_SINGLE(denali_uniphier_v5b_ecc_caps, denali_calc_ecc_bytes,
		     1024, 8, 16);
static const struct denali_dt_data denali_uniphier_v5b_data = {
	.revision = 0x0501,
	.caps = DENALI_CAP_HW_ECC_FIXUP |
		DENALI_CAP_DMA_64BIT,
	.oob_skip_bytes = 8,
	.ecc_caps = &denali_uniphier_v5b_ecc_caps,
};

static const struct of_device_id denali_nand_dt_ids[] = {
	{
		.compatible = "altr,socfpga-denali-nand",
		.data = &denali_socfpga_data,
	},
	{
		.compatible = "socionext,uniphier-denali-nand-v5a",
		.data = &denali_uniphier_v5a_data,
	},
	{
		.compatible = "socionext,uniphier-denali-nand-v5b",
		.data = &denali_uniphier_v5b_data,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, denali_nand_dt_ids);

static int denali_dt_chip_init(struct denali_controller *denali,
			       struct device_node *chip_np)
{
	struct denali_chip *dchip;
	u32 bank;
	int nsels, i, ret;

	nsels = of_property_count_u32_elems(chip_np, "reg");
	if (nsels < 0)
		return nsels;

	dchip = devm_kzalloc(denali->dev, struct_size(dchip, sels, nsels),
			     GFP_KERNEL);
	if (!dchip)
		return -ENOMEM;

	dchip->nsels = nsels;

	for (i = 0; i < nsels; i++) {
		ret = of_property_read_u32_index(chip_np, "reg", i, &bank);
		if (ret)
			return ret;

		dchip->sels[i].bank = bank;

		nand_set_flash_node(&dchip->chip, chip_np);
	}

	return denali_chip_init(denali, dchip);
}

static int denali_dt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct denali_dt *dt;
	const struct denali_dt_data *data;
	struct denali_controller *denali;
	struct device_node *np;
	int ret;

	dt = devm_kzalloc(dev, sizeof(*dt), GFP_KERNEL);
	if (!dt)
		return -ENOMEM;
	denali = &dt->controller;

	data = of_device_get_match_data(dev);
	if (WARN_ON(!data))
		return -EINVAL;

	denali->revision = data->revision;
	denali->caps = data->caps;
	denali->oob_skip_bytes = data->oob_skip_bytes;
	denali->ecc_caps = data->ecc_caps;

	denali->dev = dev;
	denali->irq = platform_get_irq(pdev, 0);
	if (denali->irq < 0)
		return denali->irq;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "denali_reg");
	denali->reg = devm_ioremap_resource(dev, res);
	if (IS_ERR(denali->reg))
		return PTR_ERR(denali->reg);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "nand_data");
	denali->host = devm_ioremap_resource(dev, res);
	if (IS_ERR(denali->host))
		return PTR_ERR(denali->host);

	dt->clk = devm_clk_get(dev, "nand");
	if (IS_ERR(dt->clk))
		return PTR_ERR(dt->clk);

	dt->clk_x = devm_clk_get(dev, "nand_x");
	if (IS_ERR(dt->clk_x))
		return PTR_ERR(dt->clk_x);

	dt->clk_ecc = devm_clk_get(dev, "ecc");
	if (IS_ERR(dt->clk_ecc))
		return PTR_ERR(dt->clk_ecc);

	dt->rst = devm_reset_control_get_optional_shared(dev, "nand");
	if (IS_ERR(dt->rst))
		return PTR_ERR(dt->rst);

	dt->rst_reg = devm_reset_control_get_optional_shared(dev, "reg");
	if (IS_ERR(dt->rst_reg))
		return PTR_ERR(dt->rst_reg);

	ret = clk_prepare_enable(dt->clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(dt->clk_x);
	if (ret)
		goto out_disable_clk;

	ret = clk_prepare_enable(dt->clk_ecc);
	if (ret)
		goto out_disable_clk_x;

	denali->clk_rate = clk_get_rate(dt->clk);
	denali->clk_x_rate = clk_get_rate(dt->clk_x);

	/*
	 * Deassert the register reset, and the core reset in this order.
	 * Deasserting the core reset while the register reset is asserted
	 * will cause unpredictable behavior in the controller.
	 */
	ret = reset_control_deassert(dt->rst_reg);
	if (ret)
		goto out_disable_clk_ecc;

	ret = reset_control_deassert(dt->rst);
	if (ret)
		goto out_assert_rst_reg;

	/*
	 * When the reset is deasserted, the initialization sequence is kicked
	 * (bootstrap process). The driver must wait until it finished.
	 * Otherwise, it will result in unpredictable behavior.
	 */
	usleep_range(200, 1000);

	ret = denali_init(denali);
	if (ret)
		goto out_assert_rst;

	for_each_child_of_node(dev->of_node, np) {
		ret = denali_dt_chip_init(denali, np);
		if (ret) {
			of_node_put(np);
			goto out_remove_denali;
		}
	}

	platform_set_drvdata(pdev, dt);

	return 0;

out_remove_denali:
	denali_remove(denali);
out_assert_rst:
	reset_control_assert(dt->rst);
out_assert_rst_reg:
	reset_control_assert(dt->rst_reg);
out_disable_clk_ecc:
	clk_disable_unprepare(dt->clk_ecc);
out_disable_clk_x:
	clk_disable_unprepare(dt->clk_x);
out_disable_clk:
	clk_disable_unprepare(dt->clk);

	return ret;
}

static int denali_dt_remove(struct platform_device *pdev)
{
	struct denali_dt *dt = platform_get_drvdata(pdev);

	denali_remove(&dt->controller);
	reset_control_assert(dt->rst);
	reset_control_assert(dt->rst_reg);
	clk_disable_unprepare(dt->clk_ecc);
	clk_disable_unprepare(dt->clk_x);
	clk_disable_unprepare(dt->clk);

	return 0;
}

static struct platform_driver denali_dt_driver = {
	.probe		= denali_dt_probe,
	.remove		= denali_dt_remove,
	.driver		= {
		.name	= "denali-nand-dt",
		.of_match_table	= denali_nand_dt_ids,
	},
};
module_platform_driver(denali_dt_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jamie Iles");
MODULE_DESCRIPTION("DT driver for Denali NAND controller");
