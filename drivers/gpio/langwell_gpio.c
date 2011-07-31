/* langwell_gpio.c Moorestown platform Langwell chip GPIO driver
 * Copyright (c) 2008 - 2009,  Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Supports:
 * Moorestown platform Langwell chip.
 * Medfield platform Penwell chip.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/stddef.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/slab.h>

/*
 * Langwell chip has 64 pins and thus there are 2 32bit registers to control
 * each feature, while Penwell chip has 96 pins for each block, and need 3 32bit
 * registers to control them, so we only define the order here instead of a
 * structure, to get a bit offset for a pin (use GPDR as an example):
 *
 * nreg = ngpio / 32;
 * reg = offset / 32;
 * bit = offset % 32;
 * reg_addr = reg_base + GPDR * nreg * 4 + reg * 4;
 *
 * so the bit of reg_addr is to control pin offset's GPDR feature
*/

enum GPIO_REG {
	GPLR = 0,	/* pin level read-only */
	GPDR,		/* pin direction */
	GPSR,		/* pin set */
	GPCR,		/* pin clear */
	GRER,		/* rising edge detect */
	GFER,		/* falling edge detect */
	GEDR,		/* edge detect result */
};

struct lnw_gpio {
	struct gpio_chip		chip;
	void				*reg_base;
	spinlock_t			lock;
	unsigned			irq_base;
};

static void __iomem *gpio_reg(struct gpio_chip *chip, unsigned offset,
			enum GPIO_REG reg_type)
{
	struct lnw_gpio *lnw = container_of(chip, struct lnw_gpio, chip);
	unsigned nreg = chip->ngpio / 32;
	u8 reg = offset / 32;
	void __iomem *ptr;

	ptr = (void __iomem *)(lnw->reg_base + reg_type * nreg * 4 + reg * 4);
	return ptr;
}

static int lnw_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	void __iomem *gplr = gpio_reg(chip, offset, GPLR);

	return readl(gplr) & BIT(offset % 32);
}

static void lnw_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	void __iomem *gpsr, *gpcr;

	if (value) {
		gpsr = gpio_reg(chip, offset, GPSR);
		writel(BIT(offset % 32), gpsr);
	} else {
		gpcr = gpio_reg(chip, offset, GPCR);
		writel(BIT(offset % 32), gpcr);
	}
}

static int lnw_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct lnw_gpio *lnw = container_of(chip, struct lnw_gpio, chip);
	void __iomem *gpdr = gpio_reg(chip, offset, GPDR);
	u32 value;
	unsigned long flags;

	spin_lock_irqsave(&lnw->lock, flags);
	value = readl(gpdr);
	value &= ~BIT(offset % 32);
	writel(value, gpdr);
	spin_unlock_irqrestore(&lnw->lock, flags);
	return 0;
}

static int lnw_gpio_direction_output(struct gpio_chip *chip,
			unsigned offset, int value)
{
	struct lnw_gpio *lnw = container_of(chip, struct lnw_gpio, chip);
	void __iomem *gpdr = gpio_reg(chip, offset, GPDR);
	unsigned long flags;

	lnw_gpio_set(chip, offset, value);
	spin_lock_irqsave(&lnw->lock, flags);
	value = readl(gpdr);
	value |= BIT(offset % 32);;
	writel(value, gpdr);
	spin_unlock_irqrestore(&lnw->lock, flags);
	return 0;
}

static int lnw_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct lnw_gpio *lnw = container_of(chip, struct lnw_gpio, chip);
	return lnw->irq_base + offset;
}

static int lnw_irq_type(unsigned irq, unsigned type)
{
	struct lnw_gpio *lnw = get_irq_chip_data(irq);
	u32 gpio = irq - lnw->irq_base;
	unsigned long flags;
	u32 value;
	void __iomem *grer = gpio_reg(&lnw->chip, gpio, GRER);
	void __iomem *gfer = gpio_reg(&lnw->chip, gpio, GFER);

	if (gpio >= lnw->chip.ngpio)
		return -EINVAL;
	spin_lock_irqsave(&lnw->lock, flags);
	if (type & IRQ_TYPE_EDGE_RISING)
		value = readl(grer) | BIT(gpio % 32);
	else
		value = readl(grer) & (~BIT(gpio % 32));
	writel(value, grer);

	if (type & IRQ_TYPE_EDGE_FALLING)
		value = readl(gfer) | BIT(gpio % 32);
	else
		value = readl(gfer) & (~BIT(gpio % 32));
	writel(value, gfer);
	spin_unlock_irqrestore(&lnw->lock, flags);

	return 0;
};

static void lnw_irq_unmask(unsigned irq)
{
};

static void lnw_irq_mask(unsigned irq)
{
};

static struct irq_chip lnw_irqchip = {
	.name		= "LNW-GPIO",
	.mask		= lnw_irq_mask,
	.unmask		= lnw_irq_unmask,
	.set_type	= lnw_irq_type,
};

static DEFINE_PCI_DEVICE_TABLE(lnw_gpio_ids) = {   /* pin number */
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x080f), .driver_data = 64 },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x081f), .driver_data = 96 },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x081a), .driver_data = 96 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, lnw_gpio_ids);

static void lnw_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct lnw_gpio *lnw = (struct lnw_gpio *)get_irq_data(irq);
	u32 base, gpio;
	void __iomem *gedr;
	u32 gedr_v;

	/* check GPIO controller to check which pin triggered the interrupt */
	for (base = 0; base < lnw->chip.ngpio; base += 32) {
		gedr = gpio_reg(&lnw->chip, base, GEDR);
		gedr_v = readl(gedr);
		if (!gedr_v)
			continue;
		for (gpio = base; gpio < base + 32; gpio++)
			if (gedr_v & BIT(gpio % 32)) {
				pr_debug("pin %d triggered\n", gpio);
				generic_handle_irq(lnw->irq_base + gpio);
			}
		/* clear the edge detect status bit */
		writel(gedr_v, gedr);
	}
	desc->chip->eoi(irq);
}

static int __devinit lnw_gpio_probe(struct pci_dev *pdev,
			const struct pci_device_id *id)
{
	void *base;
	int i;
	resource_size_t start, len;
	struct lnw_gpio *lnw;
	u32 irq_base;
	u32 gpio_base;
	int retval = 0;

	retval = pci_enable_device(pdev);
	if (retval)
		goto done;

	retval = pci_request_regions(pdev, "langwell_gpio");
	if (retval) {
		dev_err(&pdev->dev, "error requesting resources\n");
		goto err2;
	}
	/* get the irq_base from bar1 */
	start = pci_resource_start(pdev, 1);
	len = pci_resource_len(pdev, 1);
	base = ioremap_nocache(start, len);
	if (!base) {
		dev_err(&pdev->dev, "error mapping bar1\n");
		goto err3;
	}
	irq_base = *(u32 *)base;
	gpio_base = *((u32 *)base + 1);
	/* release the IO mapping, since we already get the info from bar1 */
	iounmap(base);
	/* get the register base from bar0 */
	start = pci_resource_start(pdev, 0);
	len = pci_resource_len(pdev, 0);
	base = ioremap_nocache(start, len);
	if (!base) {
		dev_err(&pdev->dev, "error mapping bar0\n");
		retval = -EFAULT;
		goto err3;
	}

	lnw = kzalloc(sizeof(struct lnw_gpio), GFP_KERNEL);
	if (!lnw) {
		dev_err(&pdev->dev, "can't allocate langwell_gpio chip data\n");
		retval = -ENOMEM;
		goto err4;
	}
	lnw->reg_base = base;
	lnw->irq_base = irq_base;
	lnw->chip.label = dev_name(&pdev->dev);
	lnw->chip.direction_input = lnw_gpio_direction_input;
	lnw->chip.direction_output = lnw_gpio_direction_output;
	lnw->chip.get = lnw_gpio_get;
	lnw->chip.set = lnw_gpio_set;
	lnw->chip.to_irq = lnw_gpio_to_irq;
	lnw->chip.base = gpio_base;
	lnw->chip.ngpio = id->driver_data;
	lnw->chip.can_sleep = 0;
	pci_set_drvdata(pdev, lnw);
	retval = gpiochip_add(&lnw->chip);
	if (retval) {
		dev_err(&pdev->dev, "langwell gpiochip_add error %d\n", retval);
		goto err5;
	}
	set_irq_data(pdev->irq, lnw);
	set_irq_chained_handler(pdev->irq, lnw_irq_handler);
	for (i = 0; i < lnw->chip.ngpio; i++) {
		set_irq_chip_and_handler_name(i + lnw->irq_base, &lnw_irqchip,
					handle_simple_irq, "demux");
		set_irq_chip_data(i + lnw->irq_base, lnw);
	}

	spin_lock_init(&lnw->lock);
	goto done;
err5:
	kfree(lnw);
err4:
	iounmap(base);
err3:
	pci_release_regions(pdev);
err2:
	pci_disable_device(pdev);
done:
	return retval;
}

static struct pci_driver lnw_gpio_driver = {
	.name		= "langwell_gpio",
	.id_table	= lnw_gpio_ids,
	.probe		= lnw_gpio_probe,
};

static int __init lnw_gpio_init(void)
{
	return pci_register_driver(&lnw_gpio_driver);
}

device_initcall(lnw_gpio_init);
