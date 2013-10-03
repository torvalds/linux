/*
 * Moorestown platform Langwell chip GPIO driver
 *
 * Copyright (c) 2008, 2009, 2013, Intel Corporation.
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
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/stddef.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/irqdomain.h>

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
	GAFR,		/* alt function */
};

struct lnw_gpio {
	struct gpio_chip		chip;
	void __iomem			*reg_base;
	spinlock_t			lock;
	struct pci_dev			*pdev;
	struct irq_domain		*domain;
};

#define to_lnw_priv(chip)	container_of(chip, struct lnw_gpio, chip)

static void __iomem *gpio_reg(struct gpio_chip *chip, unsigned offset,
			      enum GPIO_REG reg_type)
{
	struct lnw_gpio *lnw = to_lnw_priv(chip);
	unsigned nreg = chip->ngpio / 32;
	u8 reg = offset / 32;

	return lnw->reg_base + reg_type * nreg * 4 + reg * 4;
}

static void __iomem *gpio_reg_2bit(struct gpio_chip *chip, unsigned offset,
				   enum GPIO_REG reg_type)
{
	struct lnw_gpio *lnw = to_lnw_priv(chip);
	unsigned nreg = chip->ngpio / 32;
	u8 reg = offset / 16;

	return lnw->reg_base + reg_type * nreg * 4 + reg * 4;
}

static int lnw_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	void __iomem *gafr = gpio_reg_2bit(chip, offset, GAFR);
	u32 value = readl(gafr);
	int shift = (offset % 16) << 1, af = (value >> shift) & 3;

	if (af) {
		value &= ~(3 << shift);
		writel(value, gafr);
	}
	return 0;
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
	struct lnw_gpio *lnw = to_lnw_priv(chip);
	void __iomem *gpdr = gpio_reg(chip, offset, GPDR);
	u32 value;
	unsigned long flags;

	if (lnw->pdev)
		pm_runtime_get(&lnw->pdev->dev);

	spin_lock_irqsave(&lnw->lock, flags);
	value = readl(gpdr);
	value &= ~BIT(offset % 32);
	writel(value, gpdr);
	spin_unlock_irqrestore(&lnw->lock, flags);

	if (lnw->pdev)
		pm_runtime_put(&lnw->pdev->dev);

	return 0;
}

static int lnw_gpio_direction_output(struct gpio_chip *chip,
			unsigned offset, int value)
{
	struct lnw_gpio *lnw = to_lnw_priv(chip);
	void __iomem *gpdr = gpio_reg(chip, offset, GPDR);
	unsigned long flags;

	lnw_gpio_set(chip, offset, value);

	if (lnw->pdev)
		pm_runtime_get(&lnw->pdev->dev);

	spin_lock_irqsave(&lnw->lock, flags);
	value = readl(gpdr);
	value |= BIT(offset % 32);
	writel(value, gpdr);
	spin_unlock_irqrestore(&lnw->lock, flags);

	if (lnw->pdev)
		pm_runtime_put(&lnw->pdev->dev);

	return 0;
}

static int lnw_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct lnw_gpio *lnw = to_lnw_priv(chip);
	return irq_create_mapping(lnw->domain, offset);
}

static int lnw_irq_type(struct irq_data *d, unsigned type)
{
	struct lnw_gpio *lnw = irq_data_get_irq_chip_data(d);
	u32 gpio = irqd_to_hwirq(d);
	unsigned long flags;
	u32 value;
	void __iomem *grer = gpio_reg(&lnw->chip, gpio, GRER);
	void __iomem *gfer = gpio_reg(&lnw->chip, gpio, GFER);

	if (gpio >= lnw->chip.ngpio)
		return -EINVAL;

	if (lnw->pdev)
		pm_runtime_get(&lnw->pdev->dev);

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

	if (lnw->pdev)
		pm_runtime_put(&lnw->pdev->dev);

	return 0;
}

static void lnw_irq_unmask(struct irq_data *d)
{
}

static void lnw_irq_mask(struct irq_data *d)
{
}

static struct irq_chip lnw_irqchip = {
	.name		= "LNW-GPIO",
	.irq_mask	= lnw_irq_mask,
	.irq_unmask	= lnw_irq_unmask,
	.irq_set_type	= lnw_irq_type,
};

static DEFINE_PCI_DEVICE_TABLE(lnw_gpio_ids) = {   /* pin number */
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x080f), .driver_data = 64 },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x081f), .driver_data = 96 },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x081a), .driver_data = 96 },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x08eb), .driver_data = 96 },
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x08f7), .driver_data = 96 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, lnw_gpio_ids);

static void lnw_irq_handler(unsigned irq, struct irq_desc *desc)
{
	struct irq_data *data = irq_desc_get_irq_data(desc);
	struct lnw_gpio *lnw = irq_data_get_irq_handler_data(data);
	struct irq_chip *chip = irq_data_get_irq_chip(data);
	u32 base, gpio, mask;
	unsigned long pending;
	void __iomem *gedr;

	/* check GPIO controller to check which pin triggered the interrupt */
	for (base = 0; base < lnw->chip.ngpio; base += 32) {
		gedr = gpio_reg(&lnw->chip, base, GEDR);
		while ((pending = readl(gedr))) {
			gpio = __ffs(pending);
			mask = BIT(gpio);
			/* Clear before handling so we can't lose an edge */
			writel(mask, gedr);
			generic_handle_irq(irq_find_mapping(lnw->domain,
							    base + gpio));
		}
	}

	chip->irq_eoi(data);
}

static void lnw_irq_init_hw(struct lnw_gpio *lnw)
{
	void __iomem *reg;
	unsigned base;

	for (base = 0; base < lnw->chip.ngpio; base += 32) {
		/* Clear the rising-edge detect register */
		reg = gpio_reg(&lnw->chip, base, GRER);
		writel(0, reg);
		/* Clear the falling-edge detect register */
		reg = gpio_reg(&lnw->chip, base, GFER);
		writel(0, reg);
		/* Clear the edge detect status register */
		reg = gpio_reg(&lnw->chip, base, GEDR);
		writel(~0, reg);
	}
}

static int lnw_gpio_irq_map(struct irq_domain *d, unsigned int virq,
			    irq_hw_number_t hw)
{
	struct lnw_gpio *lnw = d->host_data;

	irq_set_chip_and_handler_name(virq, &lnw_irqchip, handle_simple_irq,
				      "demux");
	irq_set_chip_data(virq, lnw);
	irq_set_irq_type(virq, IRQ_TYPE_NONE);

	return 0;
}

static const struct irq_domain_ops lnw_gpio_irq_ops = {
	.map = lnw_gpio_irq_map,
	.xlate = irq_domain_xlate_twocell,
};

static int lnw_gpio_runtime_idle(struct device *dev)
{
	pm_schedule_suspend(dev, 500);
	return -EBUSY;
}

static const struct dev_pm_ops lnw_gpio_pm_ops = {
	SET_RUNTIME_PM_OPS(NULL, NULL, lnw_gpio_runtime_idle)
};

static int lnw_gpio_probe(struct pci_dev *pdev,
			  const struct pci_device_id *id)
{
	void __iomem *base;
	struct lnw_gpio *lnw;
	u32 gpio_base;
	u32 irq_base;
	int retval;
	int ngpio = id->driver_data;

	retval = pcim_enable_device(pdev);
	if (retval)
		return retval;

	retval = pcim_iomap_regions(pdev, 1 << 0 | 1 << 1, pci_name(pdev));
	if (retval) {
		dev_err(&pdev->dev, "I/O memory mapping error\n");
		return retval;
	}

	base = pcim_iomap_table(pdev)[1];

	irq_base = readl(base);
	gpio_base = readl(sizeof(u32) + base);

	/* release the IO mapping, since we already get the info from bar1 */
	pcim_iounmap_regions(pdev, 1 << 1);

	lnw = devm_kzalloc(&pdev->dev, sizeof(*lnw), GFP_KERNEL);
	if (!lnw) {
		dev_err(&pdev->dev, "can't allocate chip data\n");
		return -ENOMEM;
	}

	lnw->reg_base = pcim_iomap_table(pdev)[0];
	lnw->chip.label = dev_name(&pdev->dev);
	lnw->chip.request = lnw_gpio_request;
	lnw->chip.direction_input = lnw_gpio_direction_input;
	lnw->chip.direction_output = lnw_gpio_direction_output;
	lnw->chip.get = lnw_gpio_get;
	lnw->chip.set = lnw_gpio_set;
	lnw->chip.to_irq = lnw_gpio_to_irq;
	lnw->chip.base = gpio_base;
	lnw->chip.ngpio = ngpio;
	lnw->chip.can_sleep = 0;
	lnw->pdev = pdev;

	spin_lock_init(&lnw->lock);

	lnw->domain = irq_domain_add_simple(pdev->dev.of_node, ngpio, irq_base,
					    &lnw_gpio_irq_ops, lnw);
	if (!lnw->domain)
		return -ENOMEM;

	pci_set_drvdata(pdev, lnw);
	retval = gpiochip_add(&lnw->chip);
	if (retval) {
		dev_err(&pdev->dev, "gpiochip_add error %d\n", retval);
		return retval;
	}

	lnw_irq_init_hw(lnw);

	irq_set_handler_data(pdev->irq, lnw);
	irq_set_chained_handler(pdev->irq, lnw_irq_handler);

	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_allow(&pdev->dev);

	return 0;
}

static struct pci_driver lnw_gpio_driver = {
	.name		= "langwell_gpio",
	.id_table	= lnw_gpio_ids,
	.probe		= lnw_gpio_probe,
	.driver		= {
		.pm	= &lnw_gpio_pm_ops,
	},
};

static int __init lnw_gpio_init(void)
{
	return pci_register_driver(&lnw_gpio_driver);
}

device_initcall(lnw_gpio_init);
