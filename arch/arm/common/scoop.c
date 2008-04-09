/*
 * Support code for the SCOOP interface found on various Sharp PDAs
 *
 * Copyright (c) 2004 Richard Purdie
 *
 *	Based on code written by Sharp/Lineo for 2.4 kernels
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/device.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <asm/io.h>
#include <asm/hardware/scoop.h>

/* PCMCIA to Scoop linkage

   There is no easy way to link multiple scoop devices into one
   single entity for the pxa2xx_pcmcia device so this structure
   is used which is setup by the platform code.

   This file is never modular so this symbol is always
   accessile to the board support files.
*/
struct scoop_pcmcia_config *platform_scoop_config;
EXPORT_SYMBOL(platform_scoop_config);

struct  scoop_dev {
	void __iomem *base;
	spinlock_t scoop_lock;
	unsigned short suspend_clr;
	unsigned short suspend_set;
	u32 scoop_gpwr;
};

void reset_scoop(struct device *dev)
{
	struct scoop_dev *sdev = dev_get_drvdata(dev);

	iowrite16(0x0100, sdev->base + SCOOP_MCR);  // 00
	iowrite16(0x0000, sdev->base + SCOOP_CDR);  // 04
	iowrite16(0x0000, sdev->base + SCOOP_CCR);  // 10
	iowrite16(0x0000, sdev->base + SCOOP_IMR);  // 18
	iowrite16(0x00FF, sdev->base + SCOOP_IRM);  // 14
	iowrite16(0x0000, sdev->base + SCOOP_ISR);  // 1C
	iowrite16(0x0000, sdev->base + SCOOP_IRM);
}

unsigned short set_scoop_gpio(struct device *dev, unsigned short bit)
{
	unsigned short gpio_bit;
	unsigned long flag;
	struct scoop_dev *sdev = dev_get_drvdata(dev);

	spin_lock_irqsave(&sdev->scoop_lock, flag);
	gpio_bit = ioread16(sdev->base + SCOOP_GPWR) | bit;
	iowrite16(gpio_bit, sdev->base + SCOOP_GPWR);
	spin_unlock_irqrestore(&sdev->scoop_lock, flag);

	return gpio_bit;
}

unsigned short reset_scoop_gpio(struct device *dev, unsigned short bit)
{
	unsigned short gpio_bit;
	unsigned long flag;
	struct scoop_dev *sdev = dev_get_drvdata(dev);

	spin_lock_irqsave(&sdev->scoop_lock, flag);
	gpio_bit = ioread16(sdev->base + SCOOP_GPWR) & ~bit;
	iowrite16(gpio_bit, sdev->base + SCOOP_GPWR);
	spin_unlock_irqrestore(&sdev->scoop_lock, flag);

	return gpio_bit;
}

EXPORT_SYMBOL(set_scoop_gpio);
EXPORT_SYMBOL(reset_scoop_gpio);

unsigned short read_scoop_reg(struct device *dev, unsigned short reg)
{
	struct scoop_dev *sdev = dev_get_drvdata(dev);
	return ioread16(sdev->base + reg);
}

void write_scoop_reg(struct device *dev, unsigned short reg, unsigned short data)
{
	struct scoop_dev *sdev = dev_get_drvdata(dev);
	iowrite16(data, sdev->base + reg);
}

EXPORT_SYMBOL(reset_scoop);
EXPORT_SYMBOL(read_scoop_reg);
EXPORT_SYMBOL(write_scoop_reg);

static void check_scoop_reg(struct scoop_dev *sdev)
{
	unsigned short mcr;

	mcr = ioread16(sdev->base + SCOOP_MCR);
	if ((mcr & 0x100) == 0)
		iowrite16(0x0101, sdev->base + SCOOP_MCR);
}

#ifdef CONFIG_PM
static int scoop_suspend(struct platform_device *dev, pm_message_t state)
{
	struct scoop_dev *sdev = platform_get_drvdata(dev);

	check_scoop_reg(sdev);
	sdev->scoop_gpwr = ioread16(sdev->base + SCOOP_GPWR);
	iowrite16((sdev->scoop_gpwr & ~sdev->suspend_clr) | sdev->suspend_set, sdev->base + SCOOP_GPWR);

	return 0;
}

static int scoop_resume(struct platform_device *dev)
{
	struct scoop_dev *sdev = platform_get_drvdata(dev);

	check_scoop_reg(sdev);
	iowrite16(sdev->scoop_gpwr, sdev->base + SCOOP_GPWR);

	return 0;
}
#else
#define scoop_suspend	NULL
#define scoop_resume	NULL
#endif

static int __devinit scoop_probe(struct platform_device *pdev)
{
	struct scoop_dev *devptr;
	struct scoop_config *inf;
	struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!mem)
		return -EINVAL;

	devptr = kzalloc(sizeof(struct scoop_dev), GFP_KERNEL);
	if (!devptr)
		return -ENOMEM;

	spin_lock_init(&devptr->scoop_lock);

	inf = pdev->dev.platform_data;
	devptr->base = ioremap(mem->start, mem->end - mem->start + 1);

	if (!devptr->base) {
		kfree(devptr);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, devptr);

	printk("Sharp Scoop Device found at 0x%08x -> 0x%8p\n",(unsigned int)mem->start, devptr->base);

	iowrite16(0x0140, devptr->base + SCOOP_MCR);
	reset_scoop(&pdev->dev);
	iowrite16(0x0000, devptr->base + SCOOP_CPR);
	iowrite16(inf->io_dir & 0xffff, devptr->base + SCOOP_GPCR);
	iowrite16(inf->io_out & 0xffff, devptr->base + SCOOP_GPWR);

	devptr->suspend_clr = inf->suspend_clr;
	devptr->suspend_set = inf->suspend_set;

	return 0;
}

static int __devexit scoop_remove(struct platform_device *pdev)
{
	struct scoop_dev *sdev = platform_get_drvdata(pdev);
	if (sdev) {
		iounmap(sdev->base);
		kfree(sdev);
		platform_set_drvdata(pdev, NULL);
	}
	return 0;
}

static struct platform_driver scoop_driver = {
	.probe		= scoop_probe,
	.remove		= __devexit_p(scoop_remove),
	.suspend	= scoop_suspend,
	.resume		= scoop_resume,
	.driver		= {
		.name	= "sharp-scoop",
	},
};

static int __init scoop_init(void)
{
	return platform_driver_register(&scoop_driver);
}

subsys_initcall(scoop_init);
