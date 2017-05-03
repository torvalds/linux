/*
 * Copyright (C) STMicroelectronics 2016
 *
 * Author: Benjamin Gaignard <benjamin.gaignard@st.com>
 *
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/mfd/stm32-timers.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/reset.h>

static const struct regmap_config stm32_timers_regmap_cfg = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = sizeof(u32),
	.max_register = 0x3fc,
};

static void stm32_timers_get_arr_size(struct stm32_timers *ddata)
{
	/*
	 * Only the available bits will be written so when readback
	 * we get the maximum value of auto reload register
	 */
	regmap_write(ddata->regmap, TIM_ARR, ~0L);
	regmap_read(ddata->regmap, TIM_ARR, &ddata->max_arr);
	regmap_write(ddata->regmap, TIM_ARR, 0x0);
}

static int stm32_timers_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stm32_timers *ddata;
	struct resource *res;
	void __iomem *mmio;

	ddata = devm_kzalloc(dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	mmio = devm_ioremap_resource(dev, res);
	if (IS_ERR(mmio))
		return PTR_ERR(mmio);

	ddata->regmap = devm_regmap_init_mmio_clk(dev, "int", mmio,
						  &stm32_timers_regmap_cfg);
	if (IS_ERR(ddata->regmap))
		return PTR_ERR(ddata->regmap);

	ddata->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(ddata->clk))
		return PTR_ERR(ddata->clk);

	stm32_timers_get_arr_size(ddata);

	platform_set_drvdata(pdev, ddata);

	return of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
}

static int stm32_timers_remove(struct platform_device *pdev)
{
	of_platform_depopulate(&pdev->dev);

	return 0;
}

static const struct of_device_id stm32_timers_of_match[] = {
	{ .compatible = "st,stm32-timers", },
	{ /* end node */ },
};
MODULE_DEVICE_TABLE(of, stm32_timers_of_match);

static struct platform_driver stm32_timers_driver = {
	.probe = stm32_timers_probe,
	.remove = stm32_timers_remove,
	.driver	= {
		.name = "stm32-timers",
		.of_match_table = stm32_timers_of_match,
	},
};
module_platform_driver(stm32_timers_driver);

MODULE_DESCRIPTION("STMicroelectronics STM32 Timers");
MODULE_LICENSE("GPL v2");
