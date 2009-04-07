/*
 * SuperH KEYSC Keypad Driver
 *
 * Copyright (C) 2008 Magnus Damm
 *
 * Based on gpio_keys.c, Copyright 2005 Phil Blundell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <asm/sh_keysc.h>

#define KYCR1_OFFS   0x00
#define KYCR2_OFFS   0x04
#define KYINDR_OFFS  0x08
#define KYOUTDR_OFFS 0x0c

#define KYCR2_IRQ_LEVEL    0x10
#define KYCR2_IRQ_DISABLED 0x00

static const struct {
	unsigned char kymd, keyout, keyin;
} sh_keysc_mode[] = {
	[SH_KEYSC_MODE_1] = { 0, 6, 5 },
	[SH_KEYSC_MODE_2] = { 1, 5, 6 },
	[SH_KEYSC_MODE_3] = { 2, 4, 7 },
};

struct sh_keysc_priv {
	void __iomem *iomem_base;
	struct clk *clk;
	unsigned long last_keys;
	struct input_dev *input;
	struct sh_keysc_info pdata;
};

static irqreturn_t sh_keysc_isr(int irq, void *dev_id)
{
	struct platform_device *pdev = dev_id;
	struct sh_keysc_priv *priv = platform_get_drvdata(pdev);
	struct sh_keysc_info *pdata = &priv->pdata;
	unsigned long keys, keys1, keys0, mask;
	unsigned char keyin_set, tmp;
	int i, k;

	dev_dbg(&pdev->dev, "isr!\n");

	keys1 = ~0;
	keys0 = 0;

	do {
		keys = 0;
		keyin_set = 0;

		iowrite16(KYCR2_IRQ_DISABLED, priv->iomem_base + KYCR2_OFFS);

		for (i = 0; i < sh_keysc_mode[pdata->mode].keyout; i++) {
			iowrite16(0xfff ^ (3 << (i * 2)),
				  priv->iomem_base + KYOUTDR_OFFS);
			udelay(pdata->delay);
			tmp = ioread16(priv->iomem_base + KYINDR_OFFS);
			keys |= tmp << (sh_keysc_mode[pdata->mode].keyin * i);
			tmp ^= (1 << sh_keysc_mode[pdata->mode].keyin) - 1;
			keyin_set |= tmp;
		}

		iowrite16(0, priv->iomem_base + KYOUTDR_OFFS);
		iowrite16(KYCR2_IRQ_LEVEL | (keyin_set << 8),
			  priv->iomem_base + KYCR2_OFFS);

		keys ^= ~0;
		keys &= (1 << (sh_keysc_mode[pdata->mode].keyin *
			       sh_keysc_mode[pdata->mode].keyout)) - 1;
		keys1 &= keys;
		keys0 |= keys;

		dev_dbg(&pdev->dev, "keys 0x%08lx\n", keys);

	} while (ioread16(priv->iomem_base + KYCR2_OFFS) & 0x01);

	dev_dbg(&pdev->dev, "last_keys 0x%08lx keys0 0x%08lx keys1 0x%08lx\n",
		priv->last_keys, keys0, keys1);

	for (i = 0; i < SH_KEYSC_MAXKEYS; i++) {
		k = pdata->keycodes[i];
		if (!k)
			continue;

		mask = 1 << i;

		if (!((priv->last_keys ^ keys0) & mask))
			continue;

		if ((keys1 | keys0) & mask) {
			input_event(priv->input, EV_KEY, k, 1);
			priv->last_keys |= mask;
		}

		if (!(keys1 & mask)) {
			input_event(priv->input, EV_KEY, k, 0);
			priv->last_keys &= ~mask;
		}

	}
	input_sync(priv->input);

	return IRQ_HANDLED;
}

#define res_size(res) ((res)->end - (res)->start + 1)

static int __devinit sh_keysc_probe(struct platform_device *pdev)
{
	struct sh_keysc_priv *priv;
	struct sh_keysc_info *pdata;
	struct resource *res;
	struct input_dev *input;
	char clk_name[8];
	int i, k;
	int irq, error;

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "no platform data defined\n");
		error = -EINVAL;
		goto err0;
	}

	error = -ENXIO;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "failed to get I/O memory\n");
		goto err0;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "failed to get irq\n");
		goto err0;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (priv == NULL) {
		dev_err(&pdev->dev, "failed to allocate driver data\n");
		error = -ENOMEM;
		goto err0;
	}

	platform_set_drvdata(pdev, priv);
	memcpy(&priv->pdata, pdev->dev.platform_data, sizeof(priv->pdata));
	pdata = &priv->pdata;

	priv->iomem_base = ioremap_nocache(res->start, res_size(res));
	if (priv->iomem_base == NULL) {
		dev_err(&pdev->dev, "failed to remap I/O memory\n");
		error = -ENXIO;
		goto err1;
	}

	snprintf(clk_name, sizeof(clk_name), "keysc%d", pdev->id);
	priv->clk = clk_get(&pdev->dev, clk_name);
	if (IS_ERR(priv->clk)) {
		dev_err(&pdev->dev, "cannot get clock \"%s\"\n", clk_name);
		error = PTR_ERR(priv->clk);
		goto err2;
	}

	priv->input = input_allocate_device();
	if (!priv->input) {
		dev_err(&pdev->dev, "failed to allocate input device\n");
		error = -ENOMEM;
		goto err3;
	}

	input = priv->input;
	input->evbit[0] = BIT_MASK(EV_KEY);

	input->name = pdev->name;
	input->phys = "sh-keysc-keys/input0";
	input->dev.parent = &pdev->dev;

	input->id.bustype = BUS_HOST;
	input->id.vendor = 0x0001;
	input->id.product = 0x0001;
	input->id.version = 0x0100;

	error = request_irq(irq, sh_keysc_isr, 0, pdev->name, pdev);
	if (error) {
		dev_err(&pdev->dev, "failed to request IRQ\n");
		goto err4;
	}

	for (i = 0; i < SH_KEYSC_MAXKEYS; i++) {
		k = pdata->keycodes[i];
		if (k)
			input_set_capability(input, EV_KEY, k);
	}

	error = input_register_device(input);
	if (error) {
		dev_err(&pdev->dev, "failed to register input device\n");
		goto err5;
	}

	clk_enable(priv->clk);

	iowrite16((sh_keysc_mode[pdata->mode].kymd << 8) |
		  pdata->scan_timing, priv->iomem_base + KYCR1_OFFS);
	iowrite16(0, priv->iomem_base + KYOUTDR_OFFS);
	iowrite16(KYCR2_IRQ_LEVEL, priv->iomem_base + KYCR2_OFFS);

	device_init_wakeup(&pdev->dev, 1);
	return 0;
 err5:
	free_irq(irq, pdev);
 err4:
	input_free_device(input);
 err3:
	clk_put(priv->clk);
 err2:
	iounmap(priv->iomem_base);
 err1:
	platform_set_drvdata(pdev, NULL);
	kfree(priv);
 err0:
	return error;
}

static int __devexit sh_keysc_remove(struct platform_device *pdev)
{
	struct sh_keysc_priv *priv = platform_get_drvdata(pdev);

	iowrite16(KYCR2_IRQ_DISABLED, priv->iomem_base + KYCR2_OFFS);

	input_unregister_device(priv->input);
	free_irq(platform_get_irq(pdev, 0), pdev);
	iounmap(priv->iomem_base);

	clk_disable(priv->clk);
	clk_put(priv->clk);

	platform_set_drvdata(pdev, NULL);
	kfree(priv);
	return 0;
}

static int sh_keysc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sh_keysc_priv *priv = platform_get_drvdata(pdev);
	int irq = platform_get_irq(pdev, 0);
	unsigned short value;

	value = ioread16(priv->iomem_base + KYCR1_OFFS);

	if (device_may_wakeup(dev)) {
		value |= 0x80;
		enable_irq_wake(irq);
	}
	else
		value &= ~0x80;

	iowrite16(value, priv->iomem_base + KYCR1_OFFS);
	return 0;
}

static int sh_keysc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	int irq = platform_get_irq(pdev, 0);

	if (device_may_wakeup(dev))
		disable_irq_wake(irq);

	return 0;
}

static struct dev_pm_ops sh_keysc_dev_pm_ops = {
	.suspend = sh_keysc_suspend,
	.resume = sh_keysc_resume,
};

struct platform_driver sh_keysc_device_driver = {
	.probe		= sh_keysc_probe,
	.remove		= __devexit_p(sh_keysc_remove),
	.driver		= {
		.name	= "sh_keysc",
		.pm	= &sh_keysc_dev_pm_ops,
	}
};

static int __init sh_keysc_init(void)
{
	return platform_driver_register(&sh_keysc_device_driver);
}

static void __exit sh_keysc_exit(void)
{
	platform_driver_unregister(&sh_keysc_device_driver);
}

module_init(sh_keysc_init);
module_exit(sh_keysc_exit);

MODULE_AUTHOR("Magnus Damm");
MODULE_DESCRIPTION("SuperH KEYSC Keypad Driver");
MODULE_LICENSE("GPL");
