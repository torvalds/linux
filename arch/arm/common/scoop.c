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

#define SCOOP_REG(d,adr) (*(volatile unsigned short*)(d +(adr)))

struct  scoop_dev {
	void  *base;
	spinlock_t scoop_lock;
	unsigned short suspend_clr;
	unsigned short suspend_set;
	u32 scoop_gpwr;
};

void reset_scoop(struct device *dev)
{
	struct scoop_dev *sdev = dev_get_drvdata(dev);

	SCOOP_REG(sdev->base,SCOOP_MCR) = 0x0100;  // 00
	SCOOP_REG(sdev->base,SCOOP_CDR) = 0x0000;  // 04
	SCOOP_REG(sdev->base,SCOOP_CCR) = 0x0000;  // 10
	SCOOP_REG(sdev->base,SCOOP_IMR) = 0x0000;  // 18
	SCOOP_REG(sdev->base,SCOOP_IRM) = 0x00FF;  // 14
	SCOOP_REG(sdev->base,SCOOP_ISR) = 0x0000;  // 1C
	SCOOP_REG(sdev->base,SCOOP_IRM) = 0x0000;
}

unsigned short set_scoop_gpio(struct device *dev, unsigned short bit)
{
	unsigned short gpio_bit;
	unsigned long flag;
	struct scoop_dev *sdev = dev_get_drvdata(dev);

	spin_lock_irqsave(&sdev->scoop_lock, flag);
	gpio_bit = SCOOP_REG(sdev->base, SCOOP_GPWR) | bit;
	SCOOP_REG(sdev->base, SCOOP_GPWR) = gpio_bit;
	spin_unlock_irqrestore(&sdev->scoop_lock, flag);

	return gpio_bit;
}

unsigned short reset_scoop_gpio(struct device *dev, unsigned short bit)
{
	unsigned short gpio_bit;
	unsigned long flag;
	struct scoop_dev *sdev = dev_get_drvdata(dev);

	spin_lock_irqsave(&sdev->scoop_lock, flag);
	gpio_bit = SCOOP_REG(sdev->base, SCOOP_GPWR) & ~bit;
	SCOOP_REG(sdev->base,SCOOP_GPWR) = gpio_bit;
	spin_unlock_irqrestore(&sdev->scoop_lock, flag);

	return gpio_bit;
}

EXPORT_SYMBOL(set_scoop_gpio);
EXPORT_SYMBOL(reset_scoop_gpio);

unsigned short read_scoop_reg(struct device *dev, unsigned short reg)
{
	struct scoop_dev *sdev = dev_get_drvdata(dev);
	return SCOOP_REG(sdev->base,reg);
}

void write_scoop_reg(struct device *dev, unsigned short reg, unsigned short data)
{
	struct scoop_dev *sdev = dev_get_drvdata(dev);
	SCOOP_REG(sdev->base,reg)=data;
}

EXPORT_SYMBOL(reset_scoop);
EXPORT_SYMBOL(read_scoop_reg);
EXPORT_SYMBOL(write_scoop_reg);

static void check_scoop_reg(struct scoop_dev *sdev)
{
	unsigned short mcr;

	mcr = SCOOP_REG(sdev->base, SCOOP_MCR);
	if ((mcr & 0x100) == 0)
		SCOOP_REG(sdev->base, SCOOP_MCR) = 0x0101;
}

#ifdef CONFIG_PM
static int scoop_suspend(struct platform_device *dev, pm_message_t state)
{
	struct scoop_dev *sdev = platform_get_drvdata(dev);

	check_scoop_reg(sdev);
	sdev->scoop_gpwr = SCOOP_REG(sdev->base, SCOOP_GPWR);
	SCOOP_REG(sdev->base, SCOOP_GPWR) = (sdev->scoop_gpwr & ~sdev->suspend_clr) | sdev->suspend_set;

	return 0;
}

static int scoop_resume(struct platform_device *dev)
{
	struct scoop_dev *sdev = platform_get_drvdata(dev);

	check_scoop_reg(sdev);
	SCOOP_REG(sdev->base,SCOOP_GPWR) = sdev->scoop_gpwr;

	return 0;
}
#else
#define scoop_suspend	NULL
#define scoop_resume	NULL
#endif

int __init scoop_probe(struct platform_device *pdev)
{
	struct scoop_dev *devptr;
	struct scoop_config *inf;
	struct resource *mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!mem)
		return -EINVAL;

	devptr = kmalloc(sizeof(struct scoop_dev), GFP_KERNEL);

	if (!devptr)
		return  -ENOMEM;

	memset(devptr, 0, sizeof(struct scoop_dev));
	spin_lock_init(&devptr->scoop_lock);

	inf = pdev->dev.platform_data;
	devptr->base = ioremap(mem->start, mem->end - mem->start + 1);

	if (!devptr->base) {
		kfree(devptr);
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, devptr);

	printk("Sharp Scoop Device found at 0x%08x -> 0x%08x\n",(unsigned int)mem->start,(unsigned int)devptr->base);

	SCOOP_REG(devptr->base, SCOOP_MCR) = 0x0140;
	reset_scoop(&pdev->dev);
	SCOOP_REG(devptr->base, SCOOP_CPR) = 0x0000;
	SCOOP_REG(devptr->base, SCOOP_GPCR) = inf->io_dir & 0xffff;
	SCOOP_REG(devptr->base, SCOOP_GPWR) = inf->io_out & 0xffff;

	devptr->suspend_clr = inf->suspend_clr;
	devptr->suspend_set = inf->suspend_set;

	return 0;
}

static int scoop_remove(struct platform_device *pdev)
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
	.remove 	= scoop_remove,
	.suspend	= scoop_suspend,
	.resume		= scoop_resume,
	.driver		= {
		.name	= "sharp-scoop",
	},
};

int __init scoop_init(void)
{
	return platform_driver_register(&scoop_driver);
}

subsys_initcall(scoop_init);
