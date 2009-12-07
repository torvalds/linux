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

struct lnw_gpio_register {
	u32	GPLR[2];
	u32	GPDR[2];
	u32	GPSR[2];
	u32	GPCR[2];
	u32	GRER[2];
	u32	GFER[2];
	u32	GEDR[2];
};

struct lnw_gpio {
	struct gpio_chip		chip;
	struct lnw_gpio_register 	*reg_base;
	spinlock_t			lock;
	unsigned			irq_base;
};

static int lnw_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct lnw_gpio *lnw = container_of(chip, struct lnw_gpio, chip);
	u8 reg = offset / 32;
	void __iomem *gplr;

	gplr = (void __iomem *)(&lnw->reg_base->GPLR[reg]);
	return readl(gplr) & BIT(offset % 32);
}

static void lnw_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct lnw_gpio *lnw = container_of(chip, struct lnw_gpio, chip);
	u8 reg = offset / 32;
	void __iomem *gpsr, *gpcr;

	if (value) {
		gpsr = (void __iomem *)(&lnw->reg_base->GPSR[reg]);
		writel(BIT(offset % 32), gpsr);
	} else {
		gpcr = (void __iomem *)(&lnw->reg_base->GPCR[reg]);
		writel(BIT(offset % 32), gpcr);
	}
}

static int lnw_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct lnw_gpio *lnw = container_of(chip, struct lnw_gpio, chip);
	u8 reg = offset / 32;
	u32 value;
	unsigned long flags;
	void __iomem *gpdr;

	gpdr = (void __iomem *)(&lnw->reg_base->GPDR[reg]);
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
	u8 reg = offset / 32;
	unsigned long flags;
	void __iomem *gpdr;

	lnw_gpio_set(chip, offset, value);
	gpdr = (void __iomem *)(&lnw->reg_base->GPDR[reg]);
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
	u8 reg = gpio / 32;
	unsigned long flags;
	u32 value;
	void __iomem *grer = (void __iomem *)(&lnw->reg_base->GRER[reg]);
	void __iomem *gfer = (void __iomem *)(&lnw->reg_base->GFER[reg]);

	if (gpio < 0 || gpio > lnw->chip.ngpio)
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

static struct pci_device_id lnw_gpio_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x080f) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, lnw_gpio_ids);

static void lnw_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct lnw_gpio *lnw = (struct lnw_gpio *)get_irq_data(irq);
	u32 reg, gpio;
	void __iomem *gedr;
	u32 gedr_v;

	/* check GPIO controller to check which pin triggered the interrupt */
	for (reg = 0; reg < lnw->chip.ngpio / 32; reg++) {
		gedr = (void __iomem *)(&lnw->reg_base->GEDR[reg]);
		gedr_v = readl(gedr);
		if (!gedr_v)
			continue;
		for (gpio = reg*32; gpio < reg*32+32; gpio++)
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
	lnw->chip.ngpio = 64;
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
