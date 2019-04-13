// SPDX-License-Identifier: GPL-2.0
/*
 * STM32 Factory-programmed memory read access driver
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com> for STMicroelectronics.
 */

#include <linux/io.h>
#include <linux/module.h>
#include <linux/nvmem-provider.h>
#include <linux/of_device.h>

struct stm32_romem_priv {
	void __iomem *base;
	struct nvmem_config cfg;
};

static int stm32_romem_read(void *context, unsigned int offset, void *buf,
			    size_t bytes)
{
	struct stm32_romem_priv *priv = context;
	u8 *buf8 = buf;
	int i;

	for (i = offset; i < offset + bytes; i++)
		*buf8++ = readb_relaxed(priv->base + i);

	return 0;
}

static int stm32_romem_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stm32_romem_priv *priv;
	struct resource *res;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->cfg.name = "stm32-romem";
	priv->cfg.read_only = true;
	priv->cfg.word_size = 1;
	priv->cfg.stride = 1;
	priv->cfg.size = resource_size(res);
	priv->cfg.reg_read = stm32_romem_read;
	priv->cfg.dev = dev;
	priv->cfg.priv = priv;
	priv->cfg.owner = THIS_MODULE;

	return PTR_ERR_OR_ZERO(devm_nvmem_register(dev, &priv->cfg));
}

static const struct of_device_id stm32_romem_of_match[] = {
	{ .compatible = "st,stm32f4-otp", },
	{},
};
MODULE_DEVICE_TABLE(of, stm32_romem_of_match);

static struct platform_driver stm32_romem_driver = {
	.probe = stm32_romem_probe,
	.driver = {
		.name = "stm32-romem",
		.of_match_table = of_match_ptr(stm32_romem_of_match),
	},
};
module_platform_driver(stm32_romem_driver);

MODULE_AUTHOR("Fabrice Gasnier <fabrice.gasnier@st.com>");
MODULE_DESCRIPTION("STMicroelectronics STM32 RO-MEM");
MODULE_ALIAS("platform:nvmem-stm32-romem");
MODULE_LICENSE("GPL v2");
