// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/arm/common/locomo.c
 *
 * Sharp LoCoMo support
 *
 * This file contains all generic LoCoMo support.
 *
 * All initialization functions provided here are intended to be called
 * from machine specific code with proper arguments when required.
 *
 * Based on sa1111.c
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>

#include <asm/irq.h>
#include <asm/mach/irq.h>

#include <asm/hardware/locomo.h>

/* LoCoMo Interrupts */
#define IRQ_LOCOMO_KEY		(0)
#define IRQ_LOCOMO_GPIO		(1)
#define IRQ_LOCOMO_LT		(2)
#define IRQ_LOCOMO_SPI		(3)

/* M62332 output channel selection */
#define M62332_EVR_CH	1	/* M62332 volume channel number  */
				/*   0 : CH.1 , 1 : CH. 2        */
/* DAC send data */
#define	M62332_SLAVE_ADDR	0x4e	/* Slave address  */
#define	M62332_W_BIT		0x00	/* W bit (0 only) */
#define	M62332_SUB_ADDR		0x00	/* Sub address    */
#define	M62332_A_BIT		0x00	/* A bit (0 only) */

/* DAC setup and hold times (expressed in us) */
#define DAC_BUS_FREE_TIME	5	/*   4.7 us */
#define DAC_START_SETUP_TIME	5	/*   4.7 us */
#define DAC_STOP_SETUP_TIME	4	/*   4.0 us */
#define DAC_START_HOLD_TIME	5	/*   4.7 us */
#define DAC_SCL_LOW_HOLD_TIME	5	/*   4.7 us */
#define DAC_SCL_HIGH_HOLD_TIME	4	/*   4.0 us */
#define DAC_DATA_SETUP_TIME	1	/*   250 ns */
#define DAC_DATA_HOLD_TIME	1	/*   300 ns */
#define DAC_LOW_SETUP_TIME	1	/*   300 ns */
#define DAC_HIGH_SETUP_TIME	1	/*  1000 ns */

/* the following is the overall data for the locomo chip */
struct locomo {
	struct device *dev;
	unsigned long phys;
	unsigned int irq;
	int irq_base;
	spinlock_t lock;
	void __iomem *base;
#ifdef CONFIG_PM
	void *saved_state;
#endif
};

static const struct bus_type locomo_bus_type;

struct locomo_dev_info {
	unsigned long	offset;
	unsigned long	length;
	unsigned int	devid;
	unsigned int	irq[1];
	const char *	name;
};

/* All the locomo devices.  If offset is non-zero, the mapbase for the
 * locomo_dev will be set to the chip base plus offset.  If offset is
 * zero, then the mapbase for the locomo_dev will be set to zero.  An
 * offset of zero means the device only uses GPIOs or other helper
 * functions inside this file */
static struct locomo_dev_info locomo_devices[] = {
	{
		.devid 		= LOCOMO_DEVID_KEYBOARD,
		.irq		= { IRQ_LOCOMO_KEY },
		.name		= "locomo-keyboard",
		.offset		= LOCOMO_KEYBOARD,
		.length		= 16,
	},
	{
		.devid		= LOCOMO_DEVID_FRONTLIGHT,
		.irq		= {},
		.name		= "locomo-frontlight",
		.offset		= LOCOMO_FRONTLIGHT,
		.length		= 8,

	},
	{
		.devid		= LOCOMO_DEVID_BACKLIGHT,
		.irq		= {},
		.name		= "locomo-backlight",
		.offset		= LOCOMO_BACKLIGHT,
		.length		= 8,
	},
	{
		.devid		= LOCOMO_DEVID_AUDIO,
		.irq		= {},
		.name		= "locomo-audio",
		.offset		= LOCOMO_AUDIO,
		.length		= 4,
	},
	{
		.devid		= LOCOMO_DEVID_LED,
		.irq 		= {},
		.name		= "locomo-led",
		.offset		= LOCOMO_LED,
		.length		= 8,
	},
	{
		.devid		= LOCOMO_DEVID_UART,
		.irq		= {},
		.name		= "locomo-uart",
		.offset		= 0,
		.length		= 0,
	},
	{
		.devid		= LOCOMO_DEVID_SPI,
		.irq		= {},
		.name		= "locomo-spi",
		.offset		= LOCOMO_SPI,
		.length		= 0x30,
	},
};

static void locomo_handler(struct irq_desc *desc)
{
	struct locomo *lchip = irq_desc_get_handler_data(desc);
	int req, i;

	/* Acknowledge the parent IRQ */
	desc->irq_data.chip->irq_ack(&desc->irq_data);

	/* check why this interrupt was generated */
	req = locomo_readl(lchip->base + LOCOMO_ICR) & 0x0f00;

	if (req) {
		unsigned int irq;

		/* generate the next interrupt(s) */
		irq = lchip->irq_base;
		for (i = 0; i <= 3; i++, irq++) {
			if (req & (0x0100 << i)) {
				generic_handle_irq(irq);
			}

		}
	}
}

static void locomo_ack_irq(struct irq_data *d)
{
}

static void locomo_mask_irq(struct irq_data *d)
{
	struct locomo *lchip = irq_data_get_irq_chip_data(d);
	unsigned int r;
	r = locomo_readl(lchip->base + LOCOMO_ICR);
	r &= ~(0x0010 << (d->irq - lchip->irq_base));
	locomo_writel(r, lchip->base + LOCOMO_ICR);
}

static void locomo_unmask_irq(struct irq_data *d)
{
	struct locomo *lchip = irq_data_get_irq_chip_data(d);
	unsigned int r;
	r = locomo_readl(lchip->base + LOCOMO_ICR);
	r |= (0x0010 << (d->irq - lchip->irq_base));
	locomo_writel(r, lchip->base + LOCOMO_ICR);
}

static struct irq_chip locomo_chip = {
	.name		= "LOCOMO",
	.irq_ack	= locomo_ack_irq,
	.irq_mask	= locomo_mask_irq,
	.irq_unmask	= locomo_unmask_irq,
};

static void locomo_setup_irq(struct locomo *lchip)
{
	int irq = lchip->irq_base;

	/*
	 * Install handler for IRQ_LOCOMO_HW.
	 */
	irq_set_irq_type(lchip->irq, IRQ_TYPE_EDGE_FALLING);
	irq_set_chained_handler_and_data(lchip->irq, locomo_handler, lchip);

	/* Install handlers for IRQ_LOCOMO_* */
	for ( ; irq <= lchip->irq_base + 3; irq++) {
		irq_set_chip_and_handler(irq, &locomo_chip, handle_level_irq);
		irq_set_chip_data(irq, lchip);
		irq_clear_status_flags(irq, IRQ_NOREQUEST | IRQ_NOPROBE);
	}
}


static void locomo_dev_release(struct device *_dev)
{
	struct locomo_dev *dev = LOCOMO_DEV(_dev);

	kfree(dev);
}

static int
locomo_init_one_child(struct locomo *lchip, struct locomo_dev_info *info)
{
	struct locomo_dev *dev;
	int ret;

	dev = kzalloc(sizeof(struct locomo_dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto out;
	}

	/*
	 * If the parent device has a DMA mask associated with it,
	 * propagate it down to the children.
	 */
	if (lchip->dev->dma_mask) {
		dev->dma_mask = *lchip->dev->dma_mask;
		dev->dev.dma_mask = &dev->dma_mask;
	}

	dev_set_name(&dev->dev, "%s", info->name);
	dev->devid	 = info->devid;
	dev->dev.parent  = lchip->dev;
	dev->dev.bus     = &locomo_bus_type;
	dev->dev.release = locomo_dev_release;
	dev->dev.coherent_dma_mask = lchip->dev->coherent_dma_mask;

	if (info->offset)
		dev->mapbase = lchip->base + info->offset;
	else
		dev->mapbase = 0;
	dev->length = info->length;

	dev->irq[0] = (lchip->irq_base == NO_IRQ) ?
			NO_IRQ : lchip->irq_base + info->irq[0];

	ret = device_register(&dev->dev);
	if (ret) {
 out:
		kfree(dev);
	}
	return ret;
}

#ifdef CONFIG_PM

struct locomo_save_data {
	u16	LCM_GPO;
	u16	LCM_SPICT;
	u16	LCM_GPE;
	u16	LCM_ASD;
	u16	LCM_SPIMD;
};

static int locomo_suspend(struct platform_device *dev, pm_message_t state)
{
	struct locomo *lchip = platform_get_drvdata(dev);
	struct locomo_save_data *save;
	unsigned long flags;

	save = kmalloc(sizeof(struct locomo_save_data), GFP_KERNEL);
	if (!save)
		return -ENOMEM;

	lchip->saved_state = save;

	spin_lock_irqsave(&lchip->lock, flags);

	save->LCM_GPO     = locomo_readl(lchip->base + LOCOMO_GPO);	/* GPIO */
	locomo_writel(0x00, lchip->base + LOCOMO_GPO);
	save->LCM_SPICT   = locomo_readl(lchip->base + LOCOMO_SPI + LOCOMO_SPICT);	/* SPI */
	locomo_writel(0x40, lchip->base + LOCOMO_SPI + LOCOMO_SPICT);
	save->LCM_GPE     = locomo_readl(lchip->base + LOCOMO_GPE);	/* GPIO */
	locomo_writel(0x00, lchip->base + LOCOMO_GPE);
	save->LCM_ASD     = locomo_readl(lchip->base + LOCOMO_ASD);	/* ADSTART */
	locomo_writel(0x00, lchip->base + LOCOMO_ASD);
	save->LCM_SPIMD   = locomo_readl(lchip->base + LOCOMO_SPI + LOCOMO_SPIMD);	/* SPI */
	locomo_writel(0x3C14, lchip->base + LOCOMO_SPI + LOCOMO_SPIMD);

	locomo_writel(0x00, lchip->base + LOCOMO_PAIF);
	locomo_writel(0x00, lchip->base + LOCOMO_DAC);
	locomo_writel(0x00, lchip->base + LOCOMO_BACKLIGHT + LOCOMO_TC);

	if ((locomo_readl(lchip->base + LOCOMO_LED + LOCOMO_LPT0) & 0x88) && (locomo_readl(lchip->base + LOCOMO_LED + LOCOMO_LPT1) & 0x88))
		locomo_writel(0x00, lchip->base + LOCOMO_C32K); 	/* CLK32 off */
	else
		/* 18MHz already enabled, so no wait */
		locomo_writel(0xc1, lchip->base + LOCOMO_C32K); 	/* CLK32 on */

	locomo_writel(0x00, lchip->base + LOCOMO_TADC);		/* 18MHz clock off*/
	locomo_writel(0x00, lchip->base + LOCOMO_AUDIO + LOCOMO_ACC);			/* 22MHz/24MHz clock off */
	locomo_writel(0x00, lchip->base + LOCOMO_FRONTLIGHT + LOCOMO_ALS);			/* FL */

	spin_unlock_irqrestore(&lchip->lock, flags);

	return 0;
}

static int locomo_resume(struct platform_device *dev)
{
	struct locomo *lchip = platform_get_drvdata(dev);
	struct locomo_save_data *save;
	unsigned long r;
	unsigned long flags;

	save = lchip->saved_state;
	if (!save)
		return 0;

	spin_lock_irqsave(&lchip->lock, flags);

	locomo_writel(save->LCM_GPO, lchip->base + LOCOMO_GPO);
	locomo_writel(save->LCM_SPICT, lchip->base + LOCOMO_SPI + LOCOMO_SPICT);
	locomo_writel(save->LCM_GPE, lchip->base + LOCOMO_GPE);
	locomo_writel(save->LCM_ASD, lchip->base + LOCOMO_ASD);
	locomo_writel(save->LCM_SPIMD, lchip->base + LOCOMO_SPI + LOCOMO_SPIMD);

	locomo_writel(0x00, lchip->base + LOCOMO_C32K);
	locomo_writel(0x90, lchip->base + LOCOMO_TADC);

	locomo_writel(0, lchip->base + LOCOMO_KEYBOARD + LOCOMO_KSC);
	r = locomo_readl(lchip->base + LOCOMO_KEYBOARD + LOCOMO_KIC);
	r &= 0xFEFF;
	locomo_writel(r, lchip->base + LOCOMO_KEYBOARD + LOCOMO_KIC);
	locomo_writel(0x1, lchip->base + LOCOMO_KEYBOARD + LOCOMO_KCMD);

	spin_unlock_irqrestore(&lchip->lock, flags);

	lchip->saved_state = NULL;
	kfree(save);

	return 0;
}
#endif

static int
__locomo_probe(struct device *me, struct resource *mem, int irq)
{
	struct locomo_platform_data *pdata = me->platform_data;
	struct locomo *lchip;
	unsigned long r;
	int i, ret = -ENODEV;

	lchip = kzalloc(sizeof(struct locomo), GFP_KERNEL);
	if (!lchip)
		return -ENOMEM;

	spin_lock_init(&lchip->lock);

	lchip->dev = me;
	dev_set_drvdata(lchip->dev, lchip);

	lchip->phys = mem->start;
	lchip->irq = irq;
	lchip->irq_base = (pdata) ? pdata->irq_base : NO_IRQ;

	/*
	 * Map the whole region.  This also maps the
	 * registers for our children.
	 */
	lchip->base = ioremap(mem->start, PAGE_SIZE);
	if (!lchip->base) {
		ret = -ENOMEM;
		goto out;
	}

	/* locomo initialize */
	locomo_writel(0, lchip->base + LOCOMO_ICR);
	/* KEYBOARD */
	locomo_writel(0, lchip->base + LOCOMO_KEYBOARD + LOCOMO_KIC);

	/* GPIO */
	locomo_writel(0, lchip->base + LOCOMO_GPO);
	locomo_writel((LOCOMO_GPIO(1) | LOCOMO_GPIO(2) | LOCOMO_GPIO(13) | LOCOMO_GPIO(14))
			, lchip->base + LOCOMO_GPE);
	locomo_writel((LOCOMO_GPIO(1) | LOCOMO_GPIO(2) | LOCOMO_GPIO(13) | LOCOMO_GPIO(14))
			, lchip->base + LOCOMO_GPD);
	locomo_writel(0, lchip->base + LOCOMO_GIE);

	/* Frontlight */
	locomo_writel(0, lchip->base + LOCOMO_FRONTLIGHT + LOCOMO_ALS);
	locomo_writel(0, lchip->base + LOCOMO_FRONTLIGHT + LOCOMO_ALD);

	/* Longtime timer */
	locomo_writel(0, lchip->base + LOCOMO_LTINT);
	/* SPI */
	locomo_writel(0, lchip->base + LOCOMO_SPI + LOCOMO_SPIIE);

	locomo_writel(6 + 8 + 320 + 30 - 10, lchip->base + LOCOMO_ASD);
	r = locomo_readl(lchip->base + LOCOMO_ASD);
	r |= 0x8000;
	locomo_writel(r, lchip->base + LOCOMO_ASD);

	locomo_writel(6 + 8 + 320 + 30 - 10 - 128 + 4, lchip->base + LOCOMO_HSD);
	r = locomo_readl(lchip->base + LOCOMO_HSD);
	r |= 0x8000;
	locomo_writel(r, lchip->base + LOCOMO_HSD);

	locomo_writel(128 / 8, lchip->base + LOCOMO_HSC);

	/* XON */
	locomo_writel(0x80, lchip->base + LOCOMO_TADC);
	udelay(1000);
	/* CLK9MEN */
	r = locomo_readl(lchip->base + LOCOMO_TADC);
	r |= 0x10;
	locomo_writel(r, lchip->base + LOCOMO_TADC);
	udelay(100);

	/* init DAC */
	r = locomo_readl(lchip->base + LOCOMO_DAC);
	r |= LOCOMO_DAC_SCLOEB | LOCOMO_DAC_SDAOEB;
	locomo_writel(r, lchip->base + LOCOMO_DAC);

	r = locomo_readl(lchip->base + LOCOMO_VER);
	printk(KERN_INFO "LoCoMo Chip: %lu%lu\n", (r >> 8), (r & 0xff));

	/*
	 * The interrupt controller must be initialised before any
	 * other device to ensure that the interrupts are available.
	 */
	if (lchip->irq != NO_IRQ && lchip->irq_base != NO_IRQ)
		locomo_setup_irq(lchip);

	for (i = 0; i < ARRAY_SIZE(locomo_devices); i++)
		locomo_init_one_child(lchip, &locomo_devices[i]);
	return 0;

 out:
	kfree(lchip);
	return ret;
}

static int locomo_remove_child(struct device *dev, void *data)
{
	device_unregister(dev);
	return 0;
} 

static void __locomo_remove(struct locomo *lchip)
{
	device_for_each_child(lchip->dev, NULL, locomo_remove_child);

	if (lchip->irq != NO_IRQ) {
		irq_set_chained_handler_and_data(lchip->irq, NULL, NULL);
	}

	iounmap(lchip->base);
	kfree(lchip);
}

/**
 *	locomo_probe - probe for a single LoCoMo chip.
 *	@dev: platform device
 *
 *	Probe for a LoCoMo chip.  This must be called
 *	before any other locomo-specific code.
 *
 *	Returns:
 *	* %-EINVAL	- device's IORESOURCE_MEM not found
 *	* %-ENXIO	- could not allocate an IRQ for the device
 *	* %-ENODEV	- device not found.
 *	* %-EBUSY	- physical address already marked in-use.
 *	* %-ENOMEM	- could not allocate or iomap memory.
 *	* %0		- successful.
 */
static int locomo_probe(struct platform_device *dev)
{
	struct resource *mem;
	int irq;

	mem = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!mem)
		return -EINVAL;
	irq = platform_get_irq(dev, 0);
	if (irq < 0)
		return -ENXIO;

	return __locomo_probe(&dev->dev, mem, irq);
}

static void locomo_remove(struct platform_device *dev)
{
	struct locomo *lchip = platform_get_drvdata(dev);

	if (lchip) {
		__locomo_remove(lchip);
		platform_set_drvdata(dev, NULL);
	}
}

/*
 *	Not sure if this should be on the system bus or not yet.
 *	We really want some way to register a system device at
 *	the per-machine level, and then have this driver pick
 *	up the registered devices.
 */
static struct platform_driver locomo_device_driver = {
	.probe		= locomo_probe,
	.remove		= locomo_remove,
#ifdef CONFIG_PM
	.suspend	= locomo_suspend,
	.resume		= locomo_resume,
#endif
	.driver		= {
		.name	= "locomo",
	},
};

/*
 *	Get the parent device driver (us) structure
 *	from a child function device
 */
static inline struct locomo *locomo_chip_driver(struct locomo_dev *ldev)
{
	return (struct locomo *)dev_get_drvdata(ldev->dev.parent);
}

void locomo_gpio_set_dir(struct device *dev, unsigned int bits, unsigned int dir)
{
	struct locomo *lchip = dev_get_drvdata(dev);
	unsigned long flags;
	unsigned int r;

	if (!lchip)
		return;

	spin_lock_irqsave(&lchip->lock, flags);

	r = locomo_readl(lchip->base + LOCOMO_GPD);
	if (dir)
		r |= bits;
	else
		r &= ~bits;
	locomo_writel(r, lchip->base + LOCOMO_GPD);

	r = locomo_readl(lchip->base + LOCOMO_GPE);
	if (dir)
		r |= bits;
	else
		r &= ~bits;
	locomo_writel(r, lchip->base + LOCOMO_GPE);

	spin_unlock_irqrestore(&lchip->lock, flags);
}
EXPORT_SYMBOL(locomo_gpio_set_dir);

int locomo_gpio_read_level(struct device *dev, unsigned int bits)
{
	struct locomo *lchip = dev_get_drvdata(dev);
	unsigned long flags;
	unsigned int ret;

	if (!lchip)
		return -ENODEV;

	spin_lock_irqsave(&lchip->lock, flags);
	ret = locomo_readl(lchip->base + LOCOMO_GPL);
	spin_unlock_irqrestore(&lchip->lock, flags);

	ret &= bits;
	return ret;
}
EXPORT_SYMBOL(locomo_gpio_read_level);

int locomo_gpio_read_output(struct device *dev, unsigned int bits)
{
	struct locomo *lchip = dev_get_drvdata(dev);
	unsigned long flags;
	unsigned int ret;

	if (!lchip)
		return -ENODEV;

	spin_lock_irqsave(&lchip->lock, flags);
	ret = locomo_readl(lchip->base + LOCOMO_GPO);
	spin_unlock_irqrestore(&lchip->lock, flags);

	ret &= bits;
	return ret;
}
EXPORT_SYMBOL(locomo_gpio_read_output);

void locomo_gpio_write(struct device *dev, unsigned int bits, unsigned int set)
{
	struct locomo *lchip = dev_get_drvdata(dev);
	unsigned long flags;
	unsigned int r;

	if (!lchip)
		return;

	spin_lock_irqsave(&lchip->lock, flags);

	r = locomo_readl(lchip->base + LOCOMO_GPO);
	if (set)
		r |= bits;
	else
		r &= ~bits;
	locomo_writel(r, lchip->base + LOCOMO_GPO);

	spin_unlock_irqrestore(&lchip->lock, flags);
}
EXPORT_SYMBOL(locomo_gpio_write);

static void locomo_m62332_sendbit(void *mapbase, int bit)
{
	unsigned int r;

	r = locomo_readl(mapbase + LOCOMO_DAC);
	r &=  ~(LOCOMO_DAC_SCLOEB);
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_LOW_SETUP_TIME);	/* 300 nsec */
	udelay(DAC_DATA_HOLD_TIME);	/* 300 nsec */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r &=  ~(LOCOMO_DAC_SCLOEB);
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_LOW_SETUP_TIME);	/* 300 nsec */
	udelay(DAC_SCL_LOW_HOLD_TIME);	/* 4.7 usec */

	if (bit & 1) {
		r = locomo_readl(mapbase + LOCOMO_DAC);
		r |=  LOCOMO_DAC_SDAOEB;
		locomo_writel(r, mapbase + LOCOMO_DAC);
		udelay(DAC_HIGH_SETUP_TIME);	/* 1000 nsec */
	} else {
		r = locomo_readl(mapbase + LOCOMO_DAC);
		r &=  ~(LOCOMO_DAC_SDAOEB);
		locomo_writel(r, mapbase + LOCOMO_DAC);
		udelay(DAC_LOW_SETUP_TIME);	/* 300 nsec */
	}

	udelay(DAC_DATA_SETUP_TIME);	/* 250 nsec */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r |=  LOCOMO_DAC_SCLOEB;
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_HIGH_SETUP_TIME);	/* 1000 nsec */
	udelay(DAC_SCL_HIGH_HOLD_TIME);	/*  4.0 usec */
}

void locomo_m62332_senddata(struct locomo_dev *ldev, unsigned int dac_data, int channel)
{
	struct locomo *lchip = locomo_chip_driver(ldev);
	int i;
	unsigned char data;
	unsigned int r;
	void *mapbase = lchip->base;
	unsigned long flags;

	spin_lock_irqsave(&lchip->lock, flags);

	/* Start */
	udelay(DAC_BUS_FREE_TIME);	/* 5.0 usec */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r |=  LOCOMO_DAC_SCLOEB | LOCOMO_DAC_SDAOEB;
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_HIGH_SETUP_TIME);	/* 1000 nsec */
	udelay(DAC_SCL_HIGH_HOLD_TIME);	/* 4.0 usec */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r &=  ~(LOCOMO_DAC_SDAOEB);
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_START_HOLD_TIME);	/* 5.0 usec */
	udelay(DAC_DATA_HOLD_TIME);	/* 300 nsec */

	/* Send slave address and W bit (LSB is W bit) */
	data = (M62332_SLAVE_ADDR << 1) | M62332_W_BIT;
	for (i = 1; i <= 8; i++) {
		locomo_m62332_sendbit(mapbase, data >> (8 - i));
	}

	/* Check A bit */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r &=  ~(LOCOMO_DAC_SCLOEB);
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_LOW_SETUP_TIME);	/* 300 nsec */
	udelay(DAC_SCL_LOW_HOLD_TIME);	/* 4.7 usec */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r &=  ~(LOCOMO_DAC_SDAOEB);
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_LOW_SETUP_TIME);	/* 300 nsec */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r |=  LOCOMO_DAC_SCLOEB;
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_HIGH_SETUP_TIME);	/* 1000 nsec */
	udelay(DAC_SCL_HIGH_HOLD_TIME);	/* 4.7 usec */
	if (locomo_readl(mapbase + LOCOMO_DAC) & LOCOMO_DAC_SDAOEB) {	/* High is error */
		printk(KERN_WARNING "locomo: m62332_senddata Error 1\n");
		goto out;
	}

	/* Send Sub address (LSB is channel select) */
	/*    channel = 0 : ch1 select              */
	/*            = 1 : ch2 select              */
	data = M62332_SUB_ADDR + channel;
	for (i = 1; i <= 8; i++) {
		locomo_m62332_sendbit(mapbase, data >> (8 - i));
	}

	/* Check A bit */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r &=  ~(LOCOMO_DAC_SCLOEB);
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_LOW_SETUP_TIME);	/* 300 nsec */
	udelay(DAC_SCL_LOW_HOLD_TIME);	/* 4.7 usec */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r &=  ~(LOCOMO_DAC_SDAOEB);
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_LOW_SETUP_TIME);	/* 300 nsec */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r |=  LOCOMO_DAC_SCLOEB;
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_HIGH_SETUP_TIME);	/* 1000 nsec */
	udelay(DAC_SCL_HIGH_HOLD_TIME);	/* 4.7 usec */
	if (locomo_readl(mapbase + LOCOMO_DAC) & LOCOMO_DAC_SDAOEB) {	/* High is error */
		printk(KERN_WARNING "locomo: m62332_senddata Error 2\n");
		goto out;
	}

	/* Send DAC data */
	for (i = 1; i <= 8; i++) {
		locomo_m62332_sendbit(mapbase, dac_data >> (8 - i));
	}

	/* Check A bit */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r &=  ~(LOCOMO_DAC_SCLOEB);
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_LOW_SETUP_TIME);	/* 300 nsec */
	udelay(DAC_SCL_LOW_HOLD_TIME);	/* 4.7 usec */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r &=  ~(LOCOMO_DAC_SDAOEB);
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_LOW_SETUP_TIME);	/* 300 nsec */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r |=  LOCOMO_DAC_SCLOEB;
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_HIGH_SETUP_TIME);	/* 1000 nsec */
	udelay(DAC_SCL_HIGH_HOLD_TIME);	/* 4.7 usec */
	if (locomo_readl(mapbase + LOCOMO_DAC) & LOCOMO_DAC_SDAOEB) {	/* High is error */
		printk(KERN_WARNING "locomo: m62332_senddata Error 3\n");
	}

out:
	/* stop */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r &=  ~(LOCOMO_DAC_SCLOEB);
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_LOW_SETUP_TIME);	/* 300 nsec */
	udelay(DAC_SCL_LOW_HOLD_TIME);	/* 4.7 usec */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r |=  LOCOMO_DAC_SCLOEB;
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_HIGH_SETUP_TIME);	/* 1000 nsec */
	udelay(DAC_SCL_HIGH_HOLD_TIME);	/* 4 usec */
	r = locomo_readl(mapbase + LOCOMO_DAC);
	r |=  LOCOMO_DAC_SDAOEB;
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_HIGH_SETUP_TIME);	/* 1000 nsec */
	udelay(DAC_SCL_HIGH_HOLD_TIME);	/* 4 usec */

	r = locomo_readl(mapbase + LOCOMO_DAC);
	r |=  LOCOMO_DAC_SCLOEB | LOCOMO_DAC_SDAOEB;
	locomo_writel(r, mapbase + LOCOMO_DAC);
	udelay(DAC_LOW_SETUP_TIME);	/* 1000 nsec */
	udelay(DAC_SCL_LOW_HOLD_TIME);	/* 4.7 usec */

	spin_unlock_irqrestore(&lchip->lock, flags);
}
EXPORT_SYMBOL(locomo_m62332_senddata);

/*
 *	Frontlight control
 */

void locomo_frontlight_set(struct locomo_dev *dev, int duty, int vr, int bpwf)
{
	unsigned long flags;
	struct locomo *lchip = locomo_chip_driver(dev);

	if (vr)
		locomo_gpio_write(dev->dev.parent, LOCOMO_GPIO_FL_VR, 1);
	else
		locomo_gpio_write(dev->dev.parent, LOCOMO_GPIO_FL_VR, 0);

	spin_lock_irqsave(&lchip->lock, flags);
	locomo_writel(bpwf, lchip->base + LOCOMO_FRONTLIGHT + LOCOMO_ALS);
	udelay(100);
	locomo_writel(duty, lchip->base + LOCOMO_FRONTLIGHT + LOCOMO_ALD);
	locomo_writel(bpwf | LOCOMO_ALC_EN, lchip->base + LOCOMO_FRONTLIGHT + LOCOMO_ALS);
	spin_unlock_irqrestore(&lchip->lock, flags);
}
EXPORT_SYMBOL(locomo_frontlight_set);

/*
 *	LoCoMo "Register Access Bus."
 *
 *	We model this as a regular bus type, and hang devices directly
 *	off this.
 */
static int locomo_match(struct device *_dev, const struct device_driver *_drv)
{
	struct locomo_dev *dev = LOCOMO_DEV(_dev);
	const struct locomo_driver *drv = LOCOMO_DRV(_drv);

	return dev->devid == drv->devid;
}

static int locomo_bus_probe(struct device *dev)
{
	struct locomo_dev *ldev = LOCOMO_DEV(dev);
	struct locomo_driver *drv = LOCOMO_DRV(dev->driver);
	int ret = -ENODEV;

	if (drv->probe)
		ret = drv->probe(ldev);
	return ret;
}

static void locomo_bus_remove(struct device *dev)
{
	struct locomo_dev *ldev = LOCOMO_DEV(dev);
	struct locomo_driver *drv = LOCOMO_DRV(dev->driver);

	if (drv->remove)
		drv->remove(ldev);
}

static const struct bus_type locomo_bus_type = {
	.name		= "locomo-bus",
	.match		= locomo_match,
	.probe		= locomo_bus_probe,
	.remove		= locomo_bus_remove,
};

int locomo_driver_register(struct locomo_driver *driver)
{
	driver->drv.bus = &locomo_bus_type;
	return driver_register(&driver->drv);
}
EXPORT_SYMBOL(locomo_driver_register);

void locomo_driver_unregister(struct locomo_driver *driver)
{
	driver_unregister(&driver->drv);
}
EXPORT_SYMBOL(locomo_driver_unregister);

static int __init locomo_init(void)
{
	int ret = bus_register(&locomo_bus_type);
	if (ret == 0)
		platform_driver_register(&locomo_device_driver);
	return ret;
}

static void __exit locomo_exit(void)
{
	platform_driver_unregister(&locomo_device_driver);
	bus_unregister(&locomo_bus_type);
}

module_init(locomo_init);
module_exit(locomo_exit);

MODULE_DESCRIPTION("Sharp LoCoMo core driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Lenz <lenz@cs.wisc.edu>");
