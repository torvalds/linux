// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2018 - All Rights Reserved
 * Author: Gabriel Fernandez <gabriel.fernandez@st.com> for STMicroelectronics.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reset-controller.h>

#define CLR_OFFSET 0x4

struct stm32_reset_data {
	struct reset_controller_dev	rcdev;
	void __iomem			*membase;
};

static inline struct stm32_reset_data *
to_stm32_reset_data(struct reset_controller_dev *rcdev)
{
	return container_of(rcdev, struct stm32_reset_data, rcdev);
}

static int stm32_reset_update(struct reset_controller_dev *rcdev,
			      unsigned long id, bool assert)
{
	struct stm32_reset_data *data = to_stm32_reset_data(rcdev);
	int reg_width = sizeof(u32);
	int bank = id / (reg_width * BITS_PER_BYTE);
	int offset = id % (reg_width * BITS_PER_BYTE);
	void __iomem *addr;

	addr = data->membase + (bank * reg_width);
	if (!assert)
		addr += CLR_OFFSET;

	writel(BIT(offset), addr);

	return 0;
}

static int stm32_reset_assert(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	return stm32_reset_update(rcdev, id, true);
}

static int stm32_reset_deassert(struct reset_controller_dev *rcdev,
				unsigned long id)
{
	return stm32_reset_update(rcdev, id, false);
}

static int stm32_reset_status(struct reset_controller_dev *rcdev,
			      unsigned long id)
{
	struct stm32_reset_data *data = to_stm32_reset_data(rcdev);
	int reg_width = sizeof(u32);
	int bank = id / (reg_width * BITS_PER_BYTE);
	int offset = id % (reg_width * BITS_PER_BYTE);
	u32 reg;

	reg = readl(data->membase + (bank * reg_width));

	return !!(reg & BIT(offset));
}

static const struct reset_control_ops stm32_reset_ops = {
	.assert		= stm32_reset_assert,
	.deassert	= stm32_reset_deassert,
	.status		= stm32_reset_status,
};

static const struct of_device_id stm32_reset_dt_ids[] = {
	{ .compatible = "st,stm32mp1-rcc"},
	{ /* sentinel */ },
};

static int stm32_reset_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct stm32_reset_data *data;
	void __iomem *membase;
	struct resource *res;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	membase = devm_ioremap_resource(dev, res);
	if (IS_ERR(membase))
		return PTR_ERR(membase);

	data->membase = membase;
	data->rcdev.owner = THIS_MODULE;
	data->rcdev.nr_resets = resource_size(res) * BITS_PER_BYTE;
	data->rcdev.ops = &stm32_reset_ops;
	data->rcdev.of_node = dev->of_node;

	return devm_reset_controller_register(dev, &data->rcdev);
}

static struct platform_driver stm32_reset_driver = {
	.probe	= stm32_reset_probe,
	.driver = {
		.name		= "stm32mp1-reset",
		.of_match_table	= stm32_reset_dt_ids,
	},
};

builtin_platform_driver(stm32_reset_driver);
