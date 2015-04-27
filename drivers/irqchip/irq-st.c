/*
 *  Copyright (C) 2014 STMicroelectronics – All Rights Reserved
 *
 *  Author: Lee Jones <lee.jones@linaro.org>
 *
 *  This is a re-write of Christophe Kerello's PMU driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <dt-bindings/interrupt-controller/irq-st.h>
#include <linux/err.h>
#include <linux/mfd/syscon.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#define STIH415_SYSCFG_642		0x0a8
#define STIH416_SYSCFG_7543		0x87c
#define STIH407_SYSCFG_5102		0x198
#define STID127_SYSCFG_734		0x088

#define ST_A9_IRQ_MASK			0x001FFFFF
#define ST_A9_IRQ_MAX_CHANS		2

#define ST_A9_IRQ_EN_CTI_0		BIT(0)
#define ST_A9_IRQ_EN_CTI_1		BIT(1)
#define ST_A9_IRQ_EN_PMU_0		BIT(2)
#define ST_A9_IRQ_EN_PMU_1		BIT(3)
#define ST_A9_IRQ_EN_PL310_L2		BIT(4)
#define ST_A9_IRQ_EN_EXT_0		BIT(5)
#define ST_A9_IRQ_EN_EXT_1		BIT(6)
#define ST_A9_IRQ_EN_EXT_2		BIT(7)

#define ST_A9_FIQ_N_SEL(dev, chan)	(dev << (8  + (chan * 3)))
#define ST_A9_IRQ_N_SEL(dev, chan)	(dev << (14 + (chan * 3)))
#define ST_A9_EXTIRQ_INV_SEL(dev)	(dev << 20)

struct st_irq_syscfg {
	struct regmap *regmap;
	unsigned int syscfg;
	unsigned int config;
	bool ext_inverted;
};

static const struct of_device_id st_irq_syscfg_match[] = {
	{
		.compatible = "st,stih415-irq-syscfg",
		.data = (void *)STIH415_SYSCFG_642,
	},
	{
		.compatible = "st,stih416-irq-syscfg",
		.data = (void *)STIH416_SYSCFG_7543,
	},
	{
		.compatible = "st,stih407-irq-syscfg",
		.data = (void *)STIH407_SYSCFG_5102,
	},
	{
		.compatible = "st,stid127-irq-syscfg",
		.data = (void *)STID127_SYSCFG_734,
	},
	{}
};

static int st_irq_xlate(struct platform_device *pdev,
			int device, int channel, bool irq)
{
	struct st_irq_syscfg *ddata = dev_get_drvdata(&pdev->dev);

	/* Set the device enable bit. */
	switch (device) {
	case ST_IRQ_SYSCFG_EXT_0:
		ddata->config |= ST_A9_IRQ_EN_EXT_0;
		break;
	case ST_IRQ_SYSCFG_EXT_1:
		ddata->config |= ST_A9_IRQ_EN_EXT_1;
		break;
	case ST_IRQ_SYSCFG_EXT_2:
		ddata->config |= ST_A9_IRQ_EN_EXT_2;
		break;
	case ST_IRQ_SYSCFG_CTI_0:
		ddata->config |= ST_A9_IRQ_EN_CTI_0;
		break;
	case ST_IRQ_SYSCFG_CTI_1:
		ddata->config |= ST_A9_IRQ_EN_CTI_1;
		break;
	case ST_IRQ_SYSCFG_PMU_0:
		ddata->config |= ST_A9_IRQ_EN_PMU_0;
		break;
	case ST_IRQ_SYSCFG_PMU_1:
		ddata->config |= ST_A9_IRQ_EN_PMU_1;
		break;
	case ST_IRQ_SYSCFG_pl310_L2:
		ddata->config |= ST_A9_IRQ_EN_PL310_L2;
		break;
	case ST_IRQ_SYSCFG_DISABLED:
		return 0;
	default:
		dev_err(&pdev->dev, "Unrecognised device %d\n", device);
		return -EINVAL;
	}

	/* Select IRQ/FIQ channel for device. */
	ddata->config |= irq ?
		ST_A9_IRQ_N_SEL(device, channel) :
		ST_A9_FIQ_N_SEL(device, channel);

	return 0;
}

static int st_irq_syscfg_enable(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct st_irq_syscfg *ddata = dev_get_drvdata(&pdev->dev);
	int channels, ret, i;
	u32 device, invert;

	channels = of_property_count_u32_elems(np, "st,irq-device");
	if (channels != ST_A9_IRQ_MAX_CHANS) {
		dev_err(&pdev->dev, "st,enable-irq-device must have 2 elems\n");
		return -EINVAL;
	}

	channels = of_property_count_u32_elems(np, "st,fiq-device");
	if (channels != ST_A9_IRQ_MAX_CHANS) {
		dev_err(&pdev->dev, "st,enable-fiq-device must have 2 elems\n");
		return -EINVAL;
	}

	for (i = 0; i < ST_A9_IRQ_MAX_CHANS; i++) {
		of_property_read_u32_index(np, "st,irq-device", i, &device);

		ret = st_irq_xlate(pdev, device, i, true);
		if (ret)
			return ret;

		of_property_read_u32_index(np, "st,fiq-device", i, &device);

		ret = st_irq_xlate(pdev, device, i, false);
		if (ret)
			return ret;
	}

	/* External IRQs may be inverted. */
	of_property_read_u32(np, "st,invert-ext", &invert);
	ddata->config |= ST_A9_EXTIRQ_INV_SEL(invert);

	return regmap_update_bits(ddata->regmap, ddata->syscfg,
				  ST_A9_IRQ_MASK, ddata->config);
}

static int st_irq_syscfg_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match;
	struct st_irq_syscfg *ddata;

	ddata = devm_kzalloc(&pdev->dev, sizeof(*ddata), GFP_KERNEL);
	if (!ddata)
		return -ENOMEM;

	match = of_match_device(st_irq_syscfg_match, &pdev->dev);
	if (!match)
		return -ENODEV;

	ddata->syscfg = (unsigned int)match->data;

	ddata->regmap = syscon_regmap_lookup_by_phandle(np, "st,syscfg");
	if (IS_ERR(ddata->regmap)) {
		dev_err(&pdev->dev, "syscfg phandle missing\n");
		return PTR_ERR(ddata->regmap);
	}

	dev_set_drvdata(&pdev->dev, ddata);

	return st_irq_syscfg_enable(pdev);
}

static int st_irq_syscfg_resume(struct device *dev)
{
	struct st_irq_syscfg *ddata = dev_get_drvdata(dev);

	return regmap_update_bits(ddata->regmap, ddata->syscfg,
				  ST_A9_IRQ_MASK, ddata->config);
}

static SIMPLE_DEV_PM_OPS(st_irq_syscfg_pm_ops, NULL, st_irq_syscfg_resume);

static struct platform_driver st_irq_syscfg_driver = {
	.driver = {
		.name = "st_irq_syscfg",
		.pm = &st_irq_syscfg_pm_ops,
		.of_match_table = st_irq_syscfg_match,
	},
	.probe = st_irq_syscfg_probe,
};

static int __init st_irq_syscfg_init(void)
{
	return platform_driver_register(&st_irq_syscfg_driver);
}
core_initcall(st_irq_syscfg_init);
