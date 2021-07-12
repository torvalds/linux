// SPDX-License-Identifier: GPL-2.0-only
/*
 * Device driver for MFD hi655x PMIC
 *
 * Copyright (c) 2016 HiSilicon Ltd.
 *
 * Authors:
 * Chen Feng <puck.chen@hisilicon.com>
 * Fei  Wang <w.f@huawei.com>
 */

#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/mfd/core.h>
#include <linux/mfd/hi655x-pmic.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

static const struct regmap_irq hi655x_irqs[] = {
	{ .reg_offset = 0, .mask = OTMP_D1R_INT_MASK },
	{ .reg_offset = 0, .mask = VSYS_2P5_R_INT_MASK },
	{ .reg_offset = 0, .mask = VSYS_UV_D3R_INT_MASK },
	{ .reg_offset = 0, .mask = VSYS_6P0_D200UR_INT_MASK },
	{ .reg_offset = 0, .mask = PWRON_D4SR_INT_MASK },
	{ .reg_offset = 0, .mask = PWRON_D20F_INT_MASK },
	{ .reg_offset = 0, .mask = PWRON_D20R_INT_MASK },
	{ .reg_offset = 0, .mask = RESERVE_INT_MASK },
};

static const struct regmap_irq_chip hi655x_irq_chip = {
	.name = "hi655x-pmic",
	.irqs = hi655x_irqs,
	.num_regs = 1,
	.num_irqs = ARRAY_SIZE(hi655x_irqs),
	.status_base = HI655X_IRQ_STAT_BASE,
	.ack_base = HI655X_IRQ_STAT_BASE,
	.mask_base = HI655X_IRQ_MASK_BASE,
};

static struct regmap_config hi655x_regmap_config = {
	.reg_bits = 32,
	.reg_stride = HI655X_STRIDE,
	.val_bits = 8,
	.max_register = HI655X_BUS_ADDR(0x400) - HI655X_STRIDE,
};

static const struct resource pwrkey_resources[] = {
	{
		.name	= "down",
		.start	= PWRON_D20R_INT,
		.end	= PWRON_D20R_INT,
		.flags	= IORESOURCE_IRQ,
	}, {
		.name	= "up",
		.start	= PWRON_D20F_INT,
		.end	= PWRON_D20F_INT,
		.flags	= IORESOURCE_IRQ,
	}, {
		.name	= "hold 4s",
		.start	= PWRON_D4SR_INT,
		.end	= PWRON_D4SR_INT,
		.flags	= IORESOURCE_IRQ,
	},
};

static const struct mfd_cell hi655x_pmic_devs[] = {
	{
		.name		= "hi65xx-powerkey",
		.num_resources	= ARRAY_SIZE(pwrkey_resources),
		.resources	= &pwrkey_resources[0],
	},
	{	.name		= "hi655x-regulator",	},
	{	.name		= "hi655x-clk",		},
};

static void hi655x_local_irq_clear(struct regmap *map)
{
	int i;

	regmap_write(map, HI655X_ANA_IRQM_BASE, HI655X_IRQ_CLR);
	for (i = 0; i < HI655X_IRQ_ARRAY; i++) {
		regmap_write(map, HI655X_IRQ_STAT_BASE + i * HI655X_STRIDE,
			     HI655X_IRQ_CLR);
	}
}

static int hi655x_pmic_probe(struct platform_device *pdev)
{
	int ret;
	struct hi655x_pmic *pmic;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void __iomem *base;

	pmic = devm_kzalloc(dev, sizeof(*pmic), GFP_KERNEL);
	if (!pmic)
		return -ENOMEM;
	pmic->dev = dev;

	pmic->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, pmic->res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	pmic->regmap = devm_regmap_init_mmio_clk(dev, NULL, base,
						 &hi655x_regmap_config);
	if (IS_ERR(pmic->regmap))
		return PTR_ERR(pmic->regmap);

	regmap_read(pmic->regmap, HI655X_BUS_ADDR(HI655X_VER_REG), &pmic->ver);
	if ((pmic->ver < PMU_VER_START) || (pmic->ver > PMU_VER_END)) {
		dev_warn(dev, "PMU version %d unsupported\n", pmic->ver);
		return -EINVAL;
	}

	hi655x_local_irq_clear(pmic->regmap);

	pmic->gpio = of_get_named_gpio(np, "pmic-gpios", 0);
	if (!gpio_is_valid(pmic->gpio)) {
		dev_err(dev, "Failed to get the pmic-gpios\n");
		return -ENODEV;
	}

	ret = devm_gpio_request_one(dev, pmic->gpio, GPIOF_IN,
				    "hi655x_pmic_irq");
	if (ret < 0) {
		dev_err(dev, "Failed to request gpio %d  ret = %d\n",
			pmic->gpio, ret);
		return ret;
	}

	ret = regmap_add_irq_chip(pmic->regmap, gpio_to_irq(pmic->gpio),
				  IRQF_TRIGGER_LOW | IRQF_NO_SUSPEND, 0,
				  &hi655x_irq_chip, &pmic->irq_data);
	if (ret) {
		dev_err(dev, "Failed to obtain 'hi655x_pmic_irq' %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, pmic);

	ret = mfd_add_devices(dev, PLATFORM_DEVID_AUTO, hi655x_pmic_devs,
			      ARRAY_SIZE(hi655x_pmic_devs), NULL, 0,
			      regmap_irq_get_domain(pmic->irq_data));
	if (ret) {
		dev_err(dev, "Failed to register device %d\n", ret);
		regmap_del_irq_chip(gpio_to_irq(pmic->gpio), pmic->irq_data);
		return ret;
	}

	return 0;
}

static int hi655x_pmic_remove(struct platform_device *pdev)
{
	struct hi655x_pmic *pmic = platform_get_drvdata(pdev);

	regmap_del_irq_chip(gpio_to_irq(pmic->gpio), pmic->irq_data);
	mfd_remove_devices(&pdev->dev);
	return 0;
}

static const struct of_device_id hi655x_pmic_match[] = {
	{ .compatible = "hisilicon,hi655x-pmic", },
	{},
};
MODULE_DEVICE_TABLE(of, hi655x_pmic_match);

static struct platform_driver hi655x_pmic_driver = {
	.driver	= {
		.name =	"hi655x-pmic",
		.of_match_table = of_match_ptr(hi655x_pmic_match),
	},
	.probe  = hi655x_pmic_probe,
	.remove = hi655x_pmic_remove,
};
module_platform_driver(hi655x_pmic_driver);

MODULE_AUTHOR("Chen Feng <puck.chen@hisilicon.com>");
MODULE_DESCRIPTION("Hisilicon hi655x PMIC driver");
MODULE_LICENSE("GPL v2");
