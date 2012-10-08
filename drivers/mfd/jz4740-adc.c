/*
 * Copyright (C) 2009-2010, Lars-Peter Clausen <lars@metafoo.de>
 * JZ4740 SoC ADC driver
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * This driver synchronizes access to the JZ4740 ADC core between the
 * JZ4740 battery and hwmon drivers.
 */

#include <linux/err.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <linux/clk.h>
#include <linux/mfd/core.h>

#include <linux/jz4740-adc.h>


#define JZ_REG_ADC_ENABLE	0x00
#define JZ_REG_ADC_CFG		0x04
#define JZ_REG_ADC_CTRL		0x08
#define JZ_REG_ADC_STATUS	0x0c

#define JZ_REG_ADC_TOUCHSCREEN_BASE	0x10
#define JZ_REG_ADC_BATTERY_BASE	0x1c
#define JZ_REG_ADC_HWMON_BASE	0x20

#define JZ_ADC_ENABLE_TOUCH	BIT(2)
#define JZ_ADC_ENABLE_BATTERY	BIT(1)
#define JZ_ADC_ENABLE_ADCIN	BIT(0)

enum {
	JZ_ADC_IRQ_ADCIN = 0,
	JZ_ADC_IRQ_BATTERY,
	JZ_ADC_IRQ_TOUCH,
	JZ_ADC_IRQ_PENUP,
	JZ_ADC_IRQ_PENDOWN,
};

struct jz4740_adc {
	struct resource *mem;
	void __iomem *base;

	int irq;
	struct irq_chip_generic *gc;

	struct clk *clk;
	atomic_t clk_ref;

	spinlock_t lock;
};

static void jz4740_adc_irq_demux(unsigned int irq, struct irq_desc *desc)
{
	struct irq_chip_generic *gc = irq_desc_get_handler_data(desc);
	uint8_t status;
	unsigned int i;

	status = readb(gc->reg_base + JZ_REG_ADC_STATUS);

	for (i = 0; i < 5; ++i) {
		if (status & BIT(i))
			generic_handle_irq(gc->irq_base + i);
	}
}


/* Refcounting for the ADC clock is done in here instead of in the clock
 * framework, because it is the only clock which is shared between multiple
 * devices and thus is the only clock which needs refcounting */
static inline void jz4740_adc_clk_enable(struct jz4740_adc *adc)
{
	if (atomic_inc_return(&adc->clk_ref) == 1)
		clk_enable(adc->clk);
}

static inline void jz4740_adc_clk_disable(struct jz4740_adc *adc)
{
	if (atomic_dec_return(&adc->clk_ref) == 0)
		clk_disable(adc->clk);
}

static inline void jz4740_adc_set_enabled(struct jz4740_adc *adc, int engine,
	bool enabled)
{
	unsigned long flags;
	uint8_t val;

	spin_lock_irqsave(&adc->lock, flags);

	val = readb(adc->base + JZ_REG_ADC_ENABLE);
	if (enabled)
		val |= BIT(engine);
	else
		val &= ~BIT(engine);
	writeb(val, adc->base + JZ_REG_ADC_ENABLE);

	spin_unlock_irqrestore(&adc->lock, flags);
}

static int jz4740_adc_cell_enable(struct platform_device *pdev)
{
	struct jz4740_adc *adc = dev_get_drvdata(pdev->dev.parent);

	jz4740_adc_clk_enable(adc);
	jz4740_adc_set_enabled(adc, pdev->id, true);

	return 0;
}

static int jz4740_adc_cell_disable(struct platform_device *pdev)
{
	struct jz4740_adc *adc = dev_get_drvdata(pdev->dev.parent);

	jz4740_adc_set_enabled(adc, pdev->id, false);
	jz4740_adc_clk_disable(adc);

	return 0;
}

int jz4740_adc_set_config(struct device *dev, uint32_t mask, uint32_t val)
{
	struct jz4740_adc *adc = dev_get_drvdata(dev);
	unsigned long flags;
	uint32_t cfg;

	if (!adc)
		return -ENODEV;

	spin_lock_irqsave(&adc->lock, flags);

	cfg = readl(adc->base + JZ_REG_ADC_CFG);

	cfg &= ~mask;
	cfg |= val;

	writel(cfg, adc->base + JZ_REG_ADC_CFG);

	spin_unlock_irqrestore(&adc->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(jz4740_adc_set_config);

static struct resource jz4740_hwmon_resources[] = {
	{
		.start = JZ_ADC_IRQ_ADCIN,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start	= JZ_REG_ADC_HWMON_BASE,
		.end	= JZ_REG_ADC_HWMON_BASE + 3,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource jz4740_battery_resources[] = {
	{
		.start = JZ_ADC_IRQ_BATTERY,
		.flags = IORESOURCE_IRQ,
	},
	{
		.start	= JZ_REG_ADC_BATTERY_BASE,
		.end	= JZ_REG_ADC_BATTERY_BASE + 3,
		.flags	= IORESOURCE_MEM,
	},
};

static struct mfd_cell jz4740_adc_cells[] = {
	{
		.id = 0,
		.name = "jz4740-hwmon",
		.num_resources = ARRAY_SIZE(jz4740_hwmon_resources),
		.resources = jz4740_hwmon_resources,

		.enable = jz4740_adc_cell_enable,
		.disable = jz4740_adc_cell_disable,
	},
	{
		.id = 1,
		.name = "jz4740-battery",
		.num_resources = ARRAY_SIZE(jz4740_battery_resources),
		.resources = jz4740_battery_resources,

		.enable = jz4740_adc_cell_enable,
		.disable = jz4740_adc_cell_disable,
	},
};

static int __devinit jz4740_adc_probe(struct platform_device *pdev)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	struct jz4740_adc *adc;
	struct resource *mem_base;
	int ret;
	int irq_base;

	adc = kmalloc(sizeof(*adc), GFP_KERNEL);
	if (!adc) {
		dev_err(&pdev->dev, "Failed to allocate driver structure\n");
		return -ENOMEM;
	}

	adc->irq = platform_get_irq(pdev, 0);
	if (adc->irq < 0) {
		ret = adc->irq;
		dev_err(&pdev->dev, "Failed to get platform irq: %d\n", ret);
		goto err_free;
	}

	irq_base = platform_get_irq(pdev, 1);
	if (irq_base < 0) {
		ret = irq_base;
		dev_err(&pdev->dev, "Failed to get irq base: %d\n", ret);
		goto err_free;
	}

	mem_base = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem_base) {
		ret = -ENOENT;
		dev_err(&pdev->dev, "Failed to get platform mmio resource\n");
		goto err_free;
	}

	/* Only request the shared registers for the MFD driver */
	adc->mem = request_mem_region(mem_base->start, JZ_REG_ADC_STATUS,
					pdev->name);
	if (!adc->mem) {
		ret = -EBUSY;
		dev_err(&pdev->dev, "Failed to request mmio memory region\n");
		goto err_free;
	}

	adc->base = ioremap_nocache(adc->mem->start, resource_size(adc->mem));
	if (!adc->base) {
		ret = -EBUSY;
		dev_err(&pdev->dev, "Failed to ioremap mmio memory\n");
		goto err_release_mem_region;
	}

	adc->clk = clk_get(&pdev->dev, "adc");
	if (IS_ERR(adc->clk)) {
		ret = PTR_ERR(adc->clk);
		dev_err(&pdev->dev, "Failed to get clock: %d\n", ret);
		goto err_iounmap;
	}

	spin_lock_init(&adc->lock);
	atomic_set(&adc->clk_ref, 0);

	platform_set_drvdata(pdev, adc);

	gc = irq_alloc_generic_chip("INTC", 1, irq_base, adc->base,
		handle_level_irq);

	ct = gc->chip_types;
	ct->regs.mask = JZ_REG_ADC_CTRL;
	ct->regs.ack = JZ_REG_ADC_STATUS;
	ct->chip.irq_mask = irq_gc_mask_set_bit;
	ct->chip.irq_unmask = irq_gc_mask_clr_bit;
	ct->chip.irq_ack = irq_gc_ack_set_bit;

	irq_setup_generic_chip(gc, IRQ_MSK(5), 0, 0, IRQ_NOPROBE | IRQ_LEVEL);

	adc->gc = gc;

	irq_set_handler_data(adc->irq, gc);
	irq_set_chained_handler(adc->irq, jz4740_adc_irq_demux);

	writeb(0x00, adc->base + JZ_REG_ADC_ENABLE);
	writeb(0xff, adc->base + JZ_REG_ADC_CTRL);

	ret = mfd_add_devices(&pdev->dev, 0, jz4740_adc_cells,
			      ARRAY_SIZE(jz4740_adc_cells), mem_base,
			      irq_base, NULL);
	if (ret < 0)
		goto err_clk_put;

	return 0;

err_clk_put:
	clk_put(adc->clk);
err_iounmap:
	platform_set_drvdata(pdev, NULL);
	iounmap(adc->base);
err_release_mem_region:
	release_mem_region(adc->mem->start, resource_size(adc->mem));
err_free:
	kfree(adc);

	return ret;
}

static int __devexit jz4740_adc_remove(struct platform_device *pdev)
{
	struct jz4740_adc *adc = platform_get_drvdata(pdev);

	mfd_remove_devices(&pdev->dev);

	irq_remove_generic_chip(adc->gc, IRQ_MSK(5), IRQ_NOPROBE | IRQ_LEVEL, 0);
	kfree(adc->gc);
	irq_set_handler_data(adc->irq, NULL);
	irq_set_chained_handler(adc->irq, NULL);

	iounmap(adc->base);
	release_mem_region(adc->mem->start, resource_size(adc->mem));

	clk_put(adc->clk);

	platform_set_drvdata(pdev, NULL);

	kfree(adc);

	return 0;
}

static struct platform_driver jz4740_adc_driver = {
	.probe	= jz4740_adc_probe,
	.remove = __devexit_p(jz4740_adc_remove),
	.driver = {
		.name = "jz4740-adc",
		.owner = THIS_MODULE,
	},
};

module_platform_driver(jz4740_adc_driver);

MODULE_DESCRIPTION("JZ4740 SoC ADC driver");
MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:jz4740-adc");
