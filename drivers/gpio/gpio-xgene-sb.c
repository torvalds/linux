/*
 * AppliedMicro X-Gene SoC GPIO-Standby Driver
 *
 * Copyright (c) 2014, Applied Micro Circuits Corporation
 * Author: 	Tin Huynh <tnhuynh@apm.com>.
 * 		Y Vo <yvo@apm.com>.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/basic_mmio_gpio.h>

#define XGENE_MAX_GPIO_DS		22
#define XGENE_MAX_GPIO_DS_IRQ		6

#define GPIO_MASK(x)			(1U << ((x) % 32))

#define MPA_GPIO_INT_LVL		0x0290
#define MPA_GPIO_OE_ADDR		0x029c
#define MPA_GPIO_OUT_ADDR		0x02a0
#define MPA_GPIO_IN_ADDR 		0x02a4
#define MPA_GPIO_SEL_LO 		0x0294

/**
 * struct xgene_gpio_sb - GPIO-Standby private data structure.
 * @bgc:			memory-mapped GPIO controllers.
 * @irq:			Mapping GPIO pins and interrupt number
 * nirq:			Number of GPIO pins that supports interrupt
 */
struct xgene_gpio_sb {
	struct bgpio_chip	bgc;
	u32 *irq;
	u32 nirq;
};

static inline struct xgene_gpio_sb *to_xgene_gpio_sb(struct gpio_chip *gc)
{
	struct bgpio_chip *bgc = to_bgpio_chip(gc);

	return container_of(bgc, struct xgene_gpio_sb, bgc);
}

static void xgene_gpio_set_bit(struct bgpio_chip *bgc, void __iomem *reg, u32 gpio, int val)
{
	u32 data;

	data = bgc->read_reg(reg);
	if (val)
		data |= GPIO_MASK(gpio);
	else
		data &= ~GPIO_MASK(gpio);
	bgc->write_reg(reg, data);
}

static int apm_gpio_sb_to_irq(struct gpio_chip *gc, u32 gpio)
{
	struct xgene_gpio_sb *priv = to_xgene_gpio_sb(gc);

	if (priv->irq[gpio])
		return priv->irq[gpio];

	return -ENXIO;
}

static int xgene_gpio_sb_probe(struct platform_device *pdev)
{
	struct xgene_gpio_sb *priv;
	u32 ret, i;
	u32 default_lines[] = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D};
	struct resource *res;
	void __iomem *regs;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	ret = bgpio_init(&priv->bgc, &pdev->dev, 4,
			regs + MPA_GPIO_IN_ADDR,
			regs + MPA_GPIO_OUT_ADDR, NULL,
			regs + MPA_GPIO_OE_ADDR, NULL, 0);
        if (ret)
                return ret;

	priv->bgc.gc.to_irq = apm_gpio_sb_to_irq;
	priv->bgc.gc.ngpio = XGENE_MAX_GPIO_DS;

	priv->nirq = XGENE_MAX_GPIO_DS_IRQ;

	priv->irq = devm_kzalloc(&pdev->dev, sizeof(u32) * XGENE_MAX_GPIO_DS,
				   GFP_KERNEL);
	if (!priv->irq)
		return -ENOMEM;
	memset(priv->irq, 0, sizeof(u32) * XGENE_MAX_GPIO_DS);

	for (i = 0; i < priv->nirq; i++) {
		priv->irq[default_lines[i]] = platform_get_irq(pdev, i);
		xgene_gpio_set_bit(&priv->bgc, regs + MPA_GPIO_SEL_LO,
                                   default_lines[i] * 2, 1);
		xgene_gpio_set_bit(&priv->bgc, regs + MPA_GPIO_INT_LVL, i, 1);
	}

	platform_set_drvdata(pdev, priv);

	ret = gpiochip_add(&priv->bgc.gc);
	if (ret)
		dev_err(&pdev->dev, "failed to register X-Gene GPIO Standby driver\n");
	else
		dev_info(&pdev->dev, "X-Gene GPIO Standby driver registered\n");

	return ret;
}

static int xgene_gpio_sb_remove(struct platform_device *pdev)
{
	struct xgene_gpio_sb *priv = platform_get_drvdata(pdev);

	return bgpio_remove(&priv->bgc);
}

static const struct of_device_id xgene_gpio_sb_of_match[] = {
	{.compatible = "apm,xgene-gpio-sb", },
	{},
};
MODULE_DEVICE_TABLE(of, xgene_gpio_sb_of_match);

static struct platform_driver xgene_gpio_sb_driver = {
	.driver = {
		   .name = "xgene-gpio-sb",
		   .of_match_table = xgene_gpio_sb_of_match,
		   },
	.probe = xgene_gpio_sb_probe,
	.remove = xgene_gpio_sb_remove,
};
module_platform_driver(xgene_gpio_sb_driver);

MODULE_AUTHOR("AppliedMicro");
MODULE_DESCRIPTION("APM X-Gene GPIO Standby driver");
MODULE_LICENSE("GPL");
