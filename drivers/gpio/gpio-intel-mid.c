/*
 * Intel MID GPIO driver
 *
 * Copyright (c) 2008-2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/* Supports:
 * Moorestown platform Langwell chip.
 * Medfield platform Penwell chip.
 * Clovertrail platform Cloverview chip.
 * Merrifield platform Tangier chip.
 */

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/stddef.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/gpio/driver.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#define INTEL_MID_IRQ_TYPE_EDGE		(1 << 0)
#define INTEL_MID_IRQ_TYPE_LEVEL	(1 << 1)

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

/* intel_mid gpio driver data */
struct intel_mid_gpio_ddata {
	u16 ngpio;		/* number of gpio pins */
	u32 gplr_offset;	/* offset of first GPLR register from base */
	u32 flis_base;		/* base address of FLIS registers */
	u32 flis_len;		/* length of FLIS registers */
	u32 (*get_flis_offset)(int gpio);
	u32 chip_irq_type;	/* chip interrupt type */
};

struct intel_mid_gpio {
	struct gpio_chip		chip;
	void __iomem			*reg_base;
	spinlock_t			lock;
	struct pci_dev			*pdev;
};

static inline struct intel_mid_gpio *to_intel_gpio_priv(struct gpio_chip *gc)
{
	return container_of(gc, struct intel_mid_gpio, chip);
}

static void __iomem *gpio_reg(struct gpio_chip *chip, unsigned offset,
			      enum GPIO_REG reg_type)
{
	struct intel_mid_gpio *priv = to_intel_gpio_priv(chip);
	unsigned nreg = chip->ngpio / 32;
	u8 reg = offset / 32;

	return priv->reg_base + reg_type * nreg * 4 + reg * 4;
}

static void __iomem *gpio_reg_2bit(struct gpio_chip *chip, unsigned offset,
				   enum GPIO_REG reg_type)
{
	struct intel_mid_gpio *priv = to_intel_gpio_priv(chip);
	unsigned nreg = chip->ngpio / 32;
	u8 reg = offset / 16;

	return priv->reg_base + reg_type * nreg * 4 + reg * 4;
}

static int intel_gpio_request(struct gpio_chip *chip, unsigned offset)
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

static int intel_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	void __iomem *gplr = gpio_reg(chip, offset, GPLR);

	return readl(gplr) & BIT(offset % 32);
}

static void intel_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
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

static int intel_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct intel_mid_gpio *priv = to_intel_gpio_priv(chip);
	void __iomem *gpdr = gpio_reg(chip, offset, GPDR);
	u32 value;
	unsigned long flags;

	if (priv->pdev)
		pm_runtime_get(&priv->pdev->dev);

	spin_lock_irqsave(&priv->lock, flags);
	value = readl(gpdr);
	value &= ~BIT(offset % 32);
	writel(value, gpdr);
	spin_unlock_irqrestore(&priv->lock, flags);

	if (priv->pdev)
		pm_runtime_put(&priv->pdev->dev);

	return 0;
}

static int intel_gpio_direction_output(struct gpio_chip *chip,
			unsigned offset, int value)
{
	struct intel_mid_gpio *priv = to_intel_gpio_priv(chip);
	void __iomem *gpdr = gpio_reg(chip, offset, GPDR);
	unsigned long flags;

	intel_gpio_set(chip, offset, value);

	if (priv->pdev)
		pm_runtime_get(&priv->pdev->dev);

	spin_lock_irqsave(&priv->lock, flags);
	value = readl(gpdr);
	value |= BIT(offset % 32);
	writel(value, gpdr);
	spin_unlock_irqrestore(&priv->lock, flags);

	if (priv->pdev)
		pm_runtime_put(&priv->pdev->dev);

	return 0;
}

static int intel_mid_irq_type(struct irq_data *d, unsigned type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct intel_mid_gpio *priv = to_intel_gpio_priv(gc);
	u32 gpio = irqd_to_hwirq(d);
	unsigned long flags;
	u32 value;
	void __iomem *grer = gpio_reg(&priv->chip, gpio, GRER);
	void __iomem *gfer = gpio_reg(&priv->chip, gpio, GFER);

	if (gpio >= priv->chip.ngpio)
		return -EINVAL;

	if (priv->pdev)
		pm_runtime_get(&priv->pdev->dev);

	spin_lock_irqsave(&priv->lock, flags);
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
	spin_unlock_irqrestore(&priv->lock, flags);

	if (priv->pdev)
		pm_runtime_put(&priv->pdev->dev);

	return 0;
}

static void intel_mid_irq_unmask(struct irq_data *d)
{
}

static void intel_mid_irq_mask(struct irq_data *d)
{
}

static struct irq_chip intel_mid_irqchip = {
	.name		= "INTEL_MID-GPIO",
	.irq_mask	= intel_mid_irq_mask,
	.irq_unmask	= intel_mid_irq_unmask,
	.irq_set_type	= intel_mid_irq_type,
};

static const struct intel_mid_gpio_ddata gpio_lincroft = {
	.ngpio = 64,
};

static const struct intel_mid_gpio_ddata gpio_penwell_aon = {
	.ngpio = 96,
	.chip_irq_type = INTEL_MID_IRQ_TYPE_EDGE,
};

static const struct intel_mid_gpio_ddata gpio_penwell_core = {
	.ngpio = 96,
	.chip_irq_type = INTEL_MID_IRQ_TYPE_EDGE,
};

static const struct intel_mid_gpio_ddata gpio_cloverview_aon = {
	.ngpio = 96,
	.chip_irq_type = INTEL_MID_IRQ_TYPE_EDGE | INTEL_MID_IRQ_TYPE_LEVEL,
};

static const struct intel_mid_gpio_ddata gpio_cloverview_core = {
	.ngpio = 96,
	.chip_irq_type = INTEL_MID_IRQ_TYPE_EDGE,
};

static const struct intel_mid_gpio_ddata gpio_tangier = {
	.ngpio = 192,
	.gplr_offset = 4,
	.flis_base = 0xff0c0000,
	.flis_len = 0x8000,
	.get_flis_offset = NULL,
	.chip_irq_type = INTEL_MID_IRQ_TYPE_EDGE,
};

static const struct pci_device_id intel_gpio_ids[] = {
	{
		/* Lincroft */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x080f),
		.driver_data = (kernel_ulong_t)&gpio_lincroft,
	},
	{
		/* Penwell AON */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x081f),
		.driver_data = (kernel_ulong_t)&gpio_penwell_aon,
	},
	{
		/* Penwell Core */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x081a),
		.driver_data = (kernel_ulong_t)&gpio_penwell_core,
	},
	{
		/* Cloverview Aon */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x08eb),
		.driver_data = (kernel_ulong_t)&gpio_cloverview_aon,
	},
	{
		/* Cloverview Core */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x08f7),
		.driver_data = (kernel_ulong_t)&gpio_cloverview_core,
	},
	{
		/* Tangier */
		PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x1199),
		.driver_data = (kernel_ulong_t)&gpio_tangier,
	},
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, intel_gpio_ids);

static void intel_mid_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct intel_mid_gpio *priv = to_intel_gpio_priv(gc);
	struct irq_data *data = irq_desc_get_irq_data(desc);
	struct irq_chip *chip = irq_data_get_irq_chip(data);
	u32 base, gpio, mask;
	unsigned long pending;
	void __iomem *gedr;

	/* check GPIO controller to check which pin triggered the interrupt */
	for (base = 0; base < priv->chip.ngpio; base += 32) {
		gedr = gpio_reg(&priv->chip, base, GEDR);
		while ((pending = readl(gedr))) {
			gpio = __ffs(pending);
			mask = BIT(gpio);
			/* Clear before handling so we can't lose an edge */
			writel(mask, gedr);
			generic_handle_irq(irq_find_mapping(gc->irqdomain,
							    base + gpio));
		}
	}

	chip->irq_eoi(data);
}

static void intel_mid_irq_init_hw(struct intel_mid_gpio *priv)
{
	void __iomem *reg;
	unsigned base;

	for (base = 0; base < priv->chip.ngpio; base += 32) {
		/* Clear the rising-edge detect register */
		reg = gpio_reg(&priv->chip, base, GRER);
		writel(0, reg);
		/* Clear the falling-edge detect register */
		reg = gpio_reg(&priv->chip, base, GFER);
		writel(0, reg);
		/* Clear the edge detect status register */
		reg = gpio_reg(&priv->chip, base, GEDR);
		writel(~0, reg);
	}
}

static int intel_gpio_runtime_idle(struct device *dev)
{
	int err = pm_schedule_suspend(dev, 500);
	return err ?: -EBUSY;
}

static const struct dev_pm_ops intel_gpio_pm_ops = {
	SET_RUNTIME_PM_OPS(NULL, NULL, intel_gpio_runtime_idle)
};

static int intel_gpio_probe(struct pci_dev *pdev,
			  const struct pci_device_id *id)
{
	void __iomem *base;
	struct intel_mid_gpio *priv;
	u32 gpio_base;
	u32 irq_base;
	int retval;
	struct intel_mid_gpio_ddata *ddata =
				(struct intel_mid_gpio_ddata *)id->driver_data;

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

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "can't allocate chip data\n");
		return -ENOMEM;
	}

	priv->reg_base = pcim_iomap_table(pdev)[0];
	priv->chip.label = dev_name(&pdev->dev);
	priv->chip.parent = &pdev->dev;
	priv->chip.request = intel_gpio_request;
	priv->chip.direction_input = intel_gpio_direction_input;
	priv->chip.direction_output = intel_gpio_direction_output;
	priv->chip.get = intel_gpio_get;
	priv->chip.set = intel_gpio_set;
	priv->chip.base = gpio_base;
	priv->chip.ngpio = ddata->ngpio;
	priv->chip.can_sleep = false;
	priv->pdev = pdev;

	spin_lock_init(&priv->lock);

	pci_set_drvdata(pdev, priv);
	retval = gpiochip_add(&priv->chip);
	if (retval) {
		dev_err(&pdev->dev, "gpiochip_add error %d\n", retval);
		return retval;
	}

	retval = gpiochip_irqchip_add(&priv->chip,
				      &intel_mid_irqchip,
				      irq_base,
				      handle_simple_irq,
				      IRQ_TYPE_NONE);
	if (retval) {
		dev_err(&pdev->dev,
			"could not connect irqchip to gpiochip\n");
		return retval;
	}

	intel_mid_irq_init_hw(priv);

	gpiochip_set_chained_irqchip(&priv->chip,
				     &intel_mid_irqchip,
				     pdev->irq,
				     intel_mid_irq_handler);

	pm_runtime_put_noidle(&pdev->dev);
	pm_runtime_allow(&pdev->dev);

	return 0;
}

static struct pci_driver intel_gpio_driver = {
	.name		= "intel_mid_gpio",
	.id_table	= intel_gpio_ids,
	.probe		= intel_gpio_probe,
	.driver		= {
		.pm	= &intel_gpio_pm_ops,
	},
};

static int __init intel_gpio_init(void)
{
	return pci_register_driver(&intel_gpio_driver);
}

device_initcall(intel_gpio_init);
