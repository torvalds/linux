/*
 * ImgTec IR Decoder found in PowerDown Controller.
 *
 * Copyright 2010-2014 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This contains core img-ir code for setting up the driver. The two interfaces
 * (raw and hardware decode) are handled separately.
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include "img-ir.h"

static irqreturn_t img_ir_isr(int irq, void *dev_id)
{
	struct img_ir_priv *priv = dev_id;
	u32 irq_status;

	spin_lock(&priv->lock);
	/* we have to clear irqs before reading */
	irq_status = img_ir_read(priv, IMG_IR_IRQ_STATUS);
	img_ir_write(priv, IMG_IR_IRQ_CLEAR, irq_status);

	/* don't handle valid data irqs if we're only interested in matches */
	irq_status &= img_ir_read(priv, IMG_IR_IRQ_ENABLE);

	/* hand off edge interrupts to raw decode handler */
	if (irq_status & IMG_IR_IRQ_EDGE && img_ir_raw_enabled(&priv->raw))
		img_ir_isr_raw(priv, irq_status);

	/* hand off hardware match interrupts to hardware decode handler */
	if (irq_status & (IMG_IR_IRQ_DATA_MATCH |
			  IMG_IR_IRQ_DATA_VALID |
			  IMG_IR_IRQ_DATA2_VALID) &&
	    img_ir_hw_enabled(&priv->hw))
		img_ir_isr_hw(priv, irq_status);

	spin_unlock(&priv->lock);
	return IRQ_HANDLED;
}

static void img_ir_setup(struct img_ir_priv *priv)
{
	/* start off with interrupts disabled */
	img_ir_write(priv, IMG_IR_IRQ_ENABLE, 0);

	img_ir_setup_raw(priv);
	img_ir_setup_hw(priv);

	if (!IS_ERR(priv->clk))
		clk_prepare_enable(priv->clk);
}

static void img_ir_ident(struct img_ir_priv *priv)
{
	u32 core_rev = img_ir_read(priv, IMG_IR_CORE_REV);

	dev_info(priv->dev,
		 "IMG IR Decoder (%d.%d.%d.%d) probed successfully\n",
		 (core_rev & IMG_IR_DESIGNER) >> IMG_IR_DESIGNER_SHIFT,
		 (core_rev & IMG_IR_MAJOR_REV) >> IMG_IR_MAJOR_REV_SHIFT,
		 (core_rev & IMG_IR_MINOR_REV) >> IMG_IR_MINOR_REV_SHIFT,
		 (core_rev & IMG_IR_MAINT_REV) >> IMG_IR_MAINT_REV_SHIFT);
	dev_info(priv->dev, "Modes:%s%s\n",
		 img_ir_hw_enabled(&priv->hw) ? " hardware" : "",
		 img_ir_raw_enabled(&priv->raw) ? " raw" : "");
}

static int img_ir_probe(struct platform_device *pdev)
{
	struct img_ir_priv *priv;
	struct resource *res_regs;
	int irq, error, error2;

	/* Get resources from platform device */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "cannot find IRQ resource\n");
		return irq;
	}

	/* Private driver data */
	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "cannot allocate device data\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, priv);
	priv->dev = &pdev->dev;
	spin_lock_init(&priv->lock);

	/* Ioremap the registers */
	res_regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->reg_base = devm_ioremap_resource(&pdev->dev, res_regs);
	if (IS_ERR(priv->reg_base))
		return PTR_ERR(priv->reg_base);

	/* Get core clock */
	priv->clk = devm_clk_get(&pdev->dev, "core");
	if (IS_ERR(priv->clk))
		dev_warn(&pdev->dev, "cannot get core clock resource\n");
	/*
	 * The driver doesn't need to know about the system ("sys") or power
	 * modulation ("mod") clocks yet
	 */

	/* Set up raw & hw decoder */
	error = img_ir_probe_raw(priv);
	error2 = img_ir_probe_hw(priv);
	if (error && error2)
		return (error == -ENODEV) ? error2 : error;

	/* Get the IRQ */
	priv->irq = irq;
	error = request_irq(priv->irq, img_ir_isr, 0, "img-ir", priv);
	if (error) {
		dev_err(&pdev->dev, "cannot register IRQ %u\n",
			priv->irq);
		error = -EIO;
		goto err_irq;
	}

	img_ir_ident(priv);
	img_ir_setup(priv);

	return 0;

err_irq:
	img_ir_remove_hw(priv);
	img_ir_remove_raw(priv);
	return error;
}

static int img_ir_remove(struct platform_device *pdev)
{
	struct img_ir_priv *priv = platform_get_drvdata(pdev);

	free_irq(priv->irq, img_ir_isr);
	img_ir_remove_hw(priv);
	img_ir_remove_raw(priv);

	if (!IS_ERR(priv->clk))
		clk_disable_unprepare(priv->clk);
	return 0;
}

static SIMPLE_DEV_PM_OPS(img_ir_pmops, img_ir_suspend, img_ir_resume);

static const struct of_device_id img_ir_match[] = {
	{ .compatible = "img,ir-rev1" },
	{}
};
MODULE_DEVICE_TABLE(of, img_ir_match);

static struct platform_driver img_ir_driver = {
	.driver = {
		.name = "img-ir",
		.of_match_table	= img_ir_match,
		.pm = &img_ir_pmops,
	},
	.probe = img_ir_probe,
	.remove = img_ir_remove,
};

module_platform_driver(img_ir_driver);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("ImgTec IR");
MODULE_LICENSE("GPL");
