/*
 *
 * Toshiba T7L66XB core mfd support
 *
 * Copyright (c) 2005, 2007, 2008 Ian Molton
 * Copyright (c) 2008 Dmitry Baryshkov
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * T7L66 features:
 *
 * Supported in this driver:
 * SD/MMC
 * SM/NAND flash controller
 *
 * As yet not supported
 * GPIO interface (on NAND pins)
 * Serial interface
 * TFT 'interface converter'
 * PCMCIA interface logic
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/tmio.h>
#include <linux/mfd/t7l66xb.h>

enum {
	T7L66XB_CELL_NAND,
	T7L66XB_CELL_MMC,
};

static const struct resource t7l66xb_mmc_resources[] = {
	{
		.start = 0x800,
		.end	= 0x9ff,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_T7L66XB_MMC,
		.end	= IRQ_T7L66XB_MMC,
		.flags = IORESOURCE_IRQ,
	},
};

#define SCR_REVID	0x08		/* b Revision ID	*/
#define SCR_IMR		0x42		/* b Interrupt Mask	*/
#define SCR_DEV_CTL	0xe0		/* b Device control	*/
#define SCR_ISR		0xe1		/* b Interrupt Status	*/
#define SCR_GPO_OC	0xf0		/* b GPO output control	*/
#define SCR_GPO_OS	0xf1		/* b GPO output enable	*/
#define SCR_GPI_S	0xf2		/* w GPI status		*/
#define SCR_APDC	0xf8		/* b Active pullup down ctrl */

#define SCR_DEV_CTL_USB		BIT(0)	/* USB enable		*/
#define SCR_DEV_CTL_MMC		BIT(1)	/* MMC enable		*/

/*--------------------------------------------------------------------------*/

struct t7l66xb {
	void __iomem		*scr;
	/* Lock to protect registers requiring read/modify/write ops. */
	spinlock_t		lock;

	struct resource		rscr;
	struct clk		*clk48m;
	struct clk		*clk32k;
	int			irq;
	int			irq_base;
};

/*--------------------------------------------------------------------------*/

static int t7l66xb_mmc_enable(struct platform_device *mmc)
{
	struct platform_device *dev = to_platform_device(mmc->dev.parent);
	struct t7l66xb *t7l66xb = platform_get_drvdata(dev);
	unsigned long flags;
	u8 dev_ctl;

	clk_enable(t7l66xb->clk32k);

	spin_lock_irqsave(&t7l66xb->lock, flags);

	dev_ctl = tmio_ioread8(t7l66xb->scr + SCR_DEV_CTL);
	dev_ctl |= SCR_DEV_CTL_MMC;
	tmio_iowrite8(dev_ctl, t7l66xb->scr + SCR_DEV_CTL);

	spin_unlock_irqrestore(&t7l66xb->lock, flags);

	tmio_core_mmc_enable(t7l66xb->scr + 0x200, 0,
		t7l66xb_mmc_resources[0].start & 0xfffe);

	return 0;
}

static int t7l66xb_mmc_disable(struct platform_device *mmc)
{
	struct platform_device *dev = to_platform_device(mmc->dev.parent);
	struct t7l66xb *t7l66xb = platform_get_drvdata(dev);
	unsigned long flags;
	u8 dev_ctl;

	spin_lock_irqsave(&t7l66xb->lock, flags);

	dev_ctl = tmio_ioread8(t7l66xb->scr + SCR_DEV_CTL);
	dev_ctl &= ~SCR_DEV_CTL_MMC;
	tmio_iowrite8(dev_ctl, t7l66xb->scr + SCR_DEV_CTL);

	spin_unlock_irqrestore(&t7l66xb->lock, flags);

	clk_disable(t7l66xb->clk32k);

	return 0;
}

static void t7l66xb_mmc_pwr(struct platform_device *mmc, int state)
{
	struct platform_device *dev = to_platform_device(mmc->dev.parent);
	struct t7l66xb *t7l66xb = platform_get_drvdata(dev);

	tmio_core_mmc_pwr(t7l66xb->scr + 0x200, 0, state);
}

static void t7l66xb_mmc_clk_div(struct platform_device *mmc, int state)
{
	struct platform_device *dev = to_platform_device(mmc->dev.parent);
	struct t7l66xb *t7l66xb = platform_get_drvdata(dev);

	tmio_core_mmc_clk_div(t7l66xb->scr + 0x200, 0, state);
}

/*--------------------------------------------------------------------------*/

static struct tmio_mmc_data t7166xb_mmc_data = {
	.hclk = 24000000,
	.set_pwr = t7l66xb_mmc_pwr,
	.set_clk_div = t7l66xb_mmc_clk_div,
};

static const struct resource t7l66xb_nand_resources[] = {
	{
		.start	= 0xc00,
		.end	= 0xc07,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= 0x0100,
		.end	= 0x01ff,
		.flags	= IORESOURCE_MEM,
	},
	{
		.start	= IRQ_T7L66XB_NAND,
		.end	= IRQ_T7L66XB_NAND,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct mfd_cell t7l66xb_cells[] = {
	[T7L66XB_CELL_MMC] = {
		.name = "tmio-mmc",
		.enable = t7l66xb_mmc_enable,
		.disable = t7l66xb_mmc_disable,
		.platform_data = &t7166xb_mmc_data,
		.pdata_size    = sizeof(t7166xb_mmc_data),
		.num_resources = ARRAY_SIZE(t7l66xb_mmc_resources),
		.resources = t7l66xb_mmc_resources,
	},
	[T7L66XB_CELL_NAND] = {
		.name = "tmio-nand",
		.num_resources = ARRAY_SIZE(t7l66xb_nand_resources),
		.resources = t7l66xb_nand_resources,
	},
};

/*--------------------------------------------------------------------------*/

/* Handle the T7L66XB interrupt mux */
static void t7l66xb_irq(unsigned int irq, struct irq_desc *desc)
{
	struct t7l66xb *t7l66xb = irq_get_handler_data(irq);
	unsigned int isr;
	unsigned int i, irq_base;

	irq_base = t7l66xb->irq_base;

	while ((isr = tmio_ioread8(t7l66xb->scr + SCR_ISR) &
				~tmio_ioread8(t7l66xb->scr + SCR_IMR)))
		for (i = 0; i < T7L66XB_NR_IRQS; i++)
			if (isr & (1 << i))
				generic_handle_irq(irq_base + i);
}

static void t7l66xb_irq_mask(struct irq_data *data)
{
	struct t7l66xb *t7l66xb = irq_data_get_irq_chip_data(data);
	unsigned long			flags;
	u8 imr;

	spin_lock_irqsave(&t7l66xb->lock, flags);
	imr = tmio_ioread8(t7l66xb->scr + SCR_IMR);
	imr |= 1 << (data->irq - t7l66xb->irq_base);
	tmio_iowrite8(imr, t7l66xb->scr + SCR_IMR);
	spin_unlock_irqrestore(&t7l66xb->lock, flags);
}

static void t7l66xb_irq_unmask(struct irq_data *data)
{
	struct t7l66xb *t7l66xb = irq_data_get_irq_chip_data(data);
	unsigned long flags;
	u8 imr;

	spin_lock_irqsave(&t7l66xb->lock, flags);
	imr = tmio_ioread8(t7l66xb->scr + SCR_IMR);
	imr &= ~(1 << (data->irq - t7l66xb->irq_base));
	tmio_iowrite8(imr, t7l66xb->scr + SCR_IMR);
	spin_unlock_irqrestore(&t7l66xb->lock, flags);
}

static struct irq_chip t7l66xb_chip = {
	.name		= "t7l66xb",
	.irq_ack	= t7l66xb_irq_mask,
	.irq_mask	= t7l66xb_irq_mask,
	.irq_unmask	= t7l66xb_irq_unmask,
};

/*--------------------------------------------------------------------------*/

/* Install the IRQ handler */
static void t7l66xb_attach_irq(struct platform_device *dev)
{
	struct t7l66xb *t7l66xb = platform_get_drvdata(dev);
	unsigned int irq, irq_base;

	irq_base = t7l66xb->irq_base;

	for (irq = irq_base; irq < irq_base + T7L66XB_NR_IRQS; irq++) {
		irq_set_chip_and_handler(irq, &t7l66xb_chip, handle_level_irq);
		irq_set_chip_data(irq, t7l66xb);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
#endif
	}

	irq_set_irq_type(t7l66xb->irq, IRQ_TYPE_EDGE_FALLING);
	irq_set_handler_data(t7l66xb->irq, t7l66xb);
	irq_set_chained_handler(t7l66xb->irq, t7l66xb_irq);
}

static void t7l66xb_detach_irq(struct platform_device *dev)
{
	struct t7l66xb *t7l66xb = platform_get_drvdata(dev);
	unsigned int irq, irq_base;

	irq_base = t7l66xb->irq_base;

	irq_set_chained_handler(t7l66xb->irq, NULL);
	irq_set_handler_data(t7l66xb->irq, NULL);

	for (irq = irq_base; irq < irq_base + T7L66XB_NR_IRQS; irq++) {
#ifdef CONFIG_ARM
		set_irq_flags(irq, 0);
#endif
		irq_set_chip(irq, NULL);
		irq_set_chip_data(irq, NULL);
	}
}

/*--------------------------------------------------------------------------*/

#ifdef CONFIG_PM
static int t7l66xb_suspend(struct platform_device *dev, pm_message_t state)
{
	struct t7l66xb *t7l66xb = platform_get_drvdata(dev);
	struct t7l66xb_platform_data *pdata = dev->dev.platform_data;

	if (pdata && pdata->suspend)
		pdata->suspend(dev);
	clk_disable(t7l66xb->clk48m);

	return 0;
}

static int t7l66xb_resume(struct platform_device *dev)
{
	struct t7l66xb *t7l66xb = platform_get_drvdata(dev);
	struct t7l66xb_platform_data *pdata = dev->dev.platform_data;

	clk_enable(t7l66xb->clk48m);
	if (pdata && pdata->resume)
		pdata->resume(dev);

	tmio_core_mmc_enable(t7l66xb->scr + 0x200, 0,
		t7l66xb_mmc_resources[0].start & 0xfffe);

	return 0;
}
#else
#define t7l66xb_suspend NULL
#define t7l66xb_resume	NULL
#endif

/*--------------------------------------------------------------------------*/

static int t7l66xb_probe(struct platform_device *dev)
{
	struct t7l66xb_platform_data *pdata = dev->dev.platform_data;
	struct t7l66xb *t7l66xb;
	struct resource *iomem, *rscr;
	int ret;

	if (pdata == NULL)
		return -EINVAL;

	iomem = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!iomem)
		return -EINVAL;

	t7l66xb = kzalloc(sizeof *t7l66xb, GFP_KERNEL);
	if (!t7l66xb)
		return -ENOMEM;

	spin_lock_init(&t7l66xb->lock);

	platform_set_drvdata(dev, t7l66xb);

	ret = platform_get_irq(dev, 0);
	if (ret >= 0)
		t7l66xb->irq = ret;
	else
		goto err_noirq;

	t7l66xb->irq_base = pdata->irq_base;

	t7l66xb->clk32k = clk_get(&dev->dev, "CLK_CK32K");
	if (IS_ERR(t7l66xb->clk32k)) {
		ret = PTR_ERR(t7l66xb->clk32k);
		goto err_clk32k_get;
	}

	t7l66xb->clk48m = clk_get(&dev->dev, "CLK_CK48M");
	if (IS_ERR(t7l66xb->clk48m)) {
		ret = PTR_ERR(t7l66xb->clk48m);
		goto err_clk48m_get;
	}

	rscr = &t7l66xb->rscr;
	rscr->name = "t7l66xb-core";
	rscr->start = iomem->start;
	rscr->end = iomem->start + 0xff;
	rscr->flags = IORESOURCE_MEM;

	ret = request_resource(iomem, rscr);
	if (ret)
		goto err_request_scr;

	t7l66xb->scr = ioremap(rscr->start, resource_size(rscr));
	if (!t7l66xb->scr) {
		ret = -ENOMEM;
		goto err_ioremap;
	}

	clk_enable(t7l66xb->clk48m);

	if (pdata && pdata->enable)
		pdata->enable(dev);

	/* Mask all interrupts */
	tmio_iowrite8(0xbf, t7l66xb->scr + SCR_IMR);

	printk(KERN_INFO "%s rev %d @ 0x%08lx, irq %d\n",
		dev->name, tmio_ioread8(t7l66xb->scr + SCR_REVID),
		(unsigned long)iomem->start, t7l66xb->irq);

	t7l66xb_attach_irq(dev);

	t7l66xb_cells[T7L66XB_CELL_NAND].platform_data = pdata->nand_data;
	t7l66xb_cells[T7L66XB_CELL_NAND].pdata_size = sizeof(*pdata->nand_data);

	ret = mfd_add_devices(&dev->dev, dev->id,
			      t7l66xb_cells, ARRAY_SIZE(t7l66xb_cells),
			      iomem, t7l66xb->irq_base, NULL);

	if (!ret)
		return 0;

	t7l66xb_detach_irq(dev);
	iounmap(t7l66xb->scr);
err_ioremap:
	release_resource(&t7l66xb->rscr);
err_request_scr:
	clk_put(t7l66xb->clk48m);
err_clk48m_get:
	clk_put(t7l66xb->clk32k);
err_clk32k_get:
err_noirq:
	kfree(t7l66xb);
	return ret;
}

static int t7l66xb_remove(struct platform_device *dev)
{
	struct t7l66xb_platform_data *pdata = dev->dev.platform_data;
	struct t7l66xb *t7l66xb = platform_get_drvdata(dev);
	int ret;

	ret = pdata->disable(dev);
	clk_disable(t7l66xb->clk48m);
	clk_put(t7l66xb->clk48m);
	clk_disable(t7l66xb->clk32k);
	clk_put(t7l66xb->clk32k);
	t7l66xb_detach_irq(dev);
	iounmap(t7l66xb->scr);
	release_resource(&t7l66xb->rscr);
	mfd_remove_devices(&dev->dev);
	platform_set_drvdata(dev, NULL);
	kfree(t7l66xb);

	return ret;

}

static struct platform_driver t7l66xb_platform_driver = {
	.driver = {
		.name	= "t7l66xb",
		.owner	= THIS_MODULE,
	},
	.suspend	= t7l66xb_suspend,
	.resume		= t7l66xb_resume,
	.probe		= t7l66xb_probe,
	.remove		= t7l66xb_remove,
};

/*--------------------------------------------------------------------------*/

module_platform_driver(t7l66xb_platform_driver);

MODULE_DESCRIPTION("Toshiba T7L66XB core driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Ian Molton");
MODULE_ALIAS("platform:t7l66xb");
