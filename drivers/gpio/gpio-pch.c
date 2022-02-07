// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 LAPIS Semiconductor Co., Ltd.
 */
#include <linux/bits.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#define PCH_EDGE_FALLING	0
#define PCH_EDGE_RISING		1
#define PCH_LEVEL_L		2
#define PCH_LEVEL_H		3
#define PCH_EDGE_BOTH		4
#define PCH_IM_MASK		GENMASK(2, 0)

#define PCH_IRQ_BASE		24

struct pch_regs {
	u32	ien;
	u32	istatus;
	u32	idisp;
	u32	iclr;
	u32	imask;
	u32	imaskclr;
	u32	po;
	u32	pi;
	u32	pm;
	u32	im0;
	u32	im1;
	u32	reserved[3];
	u32	gpio_use_sel;
	u32	reset;
};

enum pch_type_t {
	INTEL_EG20T_PCH,
	OKISEMI_ML7223m_IOH, /* LAPIS Semiconductor ML7223 IOH PCIe Bus-m */
	OKISEMI_ML7223n_IOH  /* LAPIS Semiconductor ML7223 IOH PCIe Bus-n */
};

/* Specifies number of GPIO PINS */
static int gpio_pins[] = {
	[INTEL_EG20T_PCH] = 12,
	[OKISEMI_ML7223m_IOH] = 8,
	[OKISEMI_ML7223n_IOH] = 8,
};

/**
 * struct pch_gpio_reg_data - The register store data.
 * @ien_reg:	To store contents of IEN register.
 * @imask_reg:	To store contents of IMASK register.
 * @po_reg:	To store contents of PO register.
 * @pm_reg:	To store contents of PM register.
 * @im0_reg:	To store contents of IM0 register.
 * @im1_reg:	To store contents of IM1 register.
 * @gpio_use_sel_reg : To store contents of GPIO_USE_SEL register.
 *		       (Only ML7223 Bus-n)
 */
struct pch_gpio_reg_data {
	u32 ien_reg;
	u32 imask_reg;
	u32 po_reg;
	u32 pm_reg;
	u32 im0_reg;
	u32 im1_reg;
	u32 gpio_use_sel_reg;
};

/**
 * struct pch_gpio - GPIO private data structure.
 * @base:			PCI base address of Memory mapped I/O register.
 * @reg:			Memory mapped PCH GPIO register list.
 * @dev:			Pointer to device structure.
 * @gpio:			Data for GPIO infrastructure.
 * @pch_gpio_reg:		Memory mapped Register data is saved here
 *				when suspend.
 * @lock:			Used for register access protection
 * @irq_base:		Save base of IRQ number for interrupt
 * @ioh:		IOH ID
 * @spinlock:		Used for register access protection
 */
struct pch_gpio {
	void __iomem *base;
	struct pch_regs __iomem *reg;
	struct device *dev;
	struct gpio_chip gpio;
	struct pch_gpio_reg_data pch_gpio_reg;
	int irq_base;
	enum pch_type_t ioh;
	spinlock_t spinlock;
};

static void pch_gpio_set(struct gpio_chip *gpio, unsigned int nr, int val)
{
	u32 reg_val;
	struct pch_gpio *chip =	gpiochip_get_data(gpio);
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	reg_val = ioread32(&chip->reg->po);
	if (val)
		reg_val |= BIT(nr);
	else
		reg_val &= ~BIT(nr);

	iowrite32(reg_val, &chip->reg->po);
	spin_unlock_irqrestore(&chip->spinlock, flags);
}

static int pch_gpio_get(struct gpio_chip *gpio, unsigned int nr)
{
	struct pch_gpio *chip =	gpiochip_get_data(gpio);

	return !!(ioread32(&chip->reg->pi) & BIT(nr));
}

static int pch_gpio_direction_output(struct gpio_chip *gpio, unsigned int nr,
				     int val)
{
	struct pch_gpio *chip =	gpiochip_get_data(gpio);
	u32 pm;
	u32 reg_val;
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);

	reg_val = ioread32(&chip->reg->po);
	if (val)
		reg_val |= BIT(nr);
	else
		reg_val &= ~BIT(nr);
	iowrite32(reg_val, &chip->reg->po);

	pm = ioread32(&chip->reg->pm);
	pm &= BIT(gpio_pins[chip->ioh]) - 1;
	pm |= BIT(nr);
	iowrite32(pm, &chip->reg->pm);

	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

static int pch_gpio_direction_input(struct gpio_chip *gpio, unsigned int nr)
{
	struct pch_gpio *chip =	gpiochip_get_data(gpio);
	u32 pm;
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	pm = ioread32(&chip->reg->pm);
	pm &= BIT(gpio_pins[chip->ioh]) - 1;
	pm &= ~BIT(nr);
	iowrite32(pm, &chip->reg->pm);
	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

/*
 * Save register configuration and disable interrupts.
 */
static void __maybe_unused pch_gpio_save_reg_conf(struct pch_gpio *chip)
{
	chip->pch_gpio_reg.ien_reg = ioread32(&chip->reg->ien);
	chip->pch_gpio_reg.imask_reg = ioread32(&chip->reg->imask);
	chip->pch_gpio_reg.po_reg = ioread32(&chip->reg->po);
	chip->pch_gpio_reg.pm_reg = ioread32(&chip->reg->pm);
	chip->pch_gpio_reg.im0_reg = ioread32(&chip->reg->im0);
	if (chip->ioh == INTEL_EG20T_PCH)
		chip->pch_gpio_reg.im1_reg = ioread32(&chip->reg->im1);
	if (chip->ioh == OKISEMI_ML7223n_IOH)
		chip->pch_gpio_reg.gpio_use_sel_reg = ioread32(&chip->reg->gpio_use_sel);
}

/*
 * This function restores the register configuration of the GPIO device.
 */
static void __maybe_unused pch_gpio_restore_reg_conf(struct pch_gpio *chip)
{
	iowrite32(chip->pch_gpio_reg.ien_reg, &chip->reg->ien);
	iowrite32(chip->pch_gpio_reg.imask_reg, &chip->reg->imask);
	/* to store contents of PO register */
	iowrite32(chip->pch_gpio_reg.po_reg, &chip->reg->po);
	/* to store contents of PM register */
	iowrite32(chip->pch_gpio_reg.pm_reg, &chip->reg->pm);
	iowrite32(chip->pch_gpio_reg.im0_reg, &chip->reg->im0);
	if (chip->ioh == INTEL_EG20T_PCH)
		iowrite32(chip->pch_gpio_reg.im1_reg, &chip->reg->im1);
	if (chip->ioh == OKISEMI_ML7223n_IOH)
		iowrite32(chip->pch_gpio_reg.gpio_use_sel_reg, &chip->reg->gpio_use_sel);
}

static int pch_gpio_to_irq(struct gpio_chip *gpio, unsigned int offset)
{
	struct pch_gpio *chip = gpiochip_get_data(gpio);

	return chip->irq_base + offset;
}

static void pch_gpio_setup(struct pch_gpio *chip)
{
	struct gpio_chip *gpio = &chip->gpio;

	gpio->label = dev_name(chip->dev);
	gpio->parent = chip->dev;
	gpio->owner = THIS_MODULE;
	gpio->direction_input = pch_gpio_direction_input;
	gpio->get = pch_gpio_get;
	gpio->direction_output = pch_gpio_direction_output;
	gpio->set = pch_gpio_set;
	gpio->base = -1;
	gpio->ngpio = gpio_pins[chip->ioh];
	gpio->can_sleep = false;
	gpio->to_irq = pch_gpio_to_irq;
}

static int pch_irq_type(struct irq_data *d, unsigned int type)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct pch_gpio *chip = gc->private;
	u32 im, im_pos, val;
	u32 __iomem *im_reg;
	unsigned long flags;
	int ch, irq = d->irq;

	ch = irq - chip->irq_base;
	if (irq < chip->irq_base + 8) {
		im_reg = &chip->reg->im0;
		im_pos = ch - 0;
	} else {
		im_reg = &chip->reg->im1;
		im_pos = ch - 8;
	}
	dev_dbg(chip->dev, "irq=%d type=%d ch=%d pos=%d\n", irq, type, ch, im_pos);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		val = PCH_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		val = PCH_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		val = PCH_EDGE_BOTH;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		val = PCH_LEVEL_H;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		val = PCH_LEVEL_L;
		break;
	default:
		return 0;
	}

	spin_lock_irqsave(&chip->spinlock, flags);

	/* Set interrupt mode */
	im = ioread32(im_reg) & ~(PCH_IM_MASK << (im_pos * 4));
	iowrite32(im | (val << (im_pos * 4)), im_reg);

	/* And the handler */
	if (type & IRQ_TYPE_LEVEL_MASK)
		irq_set_handler_locked(d, handle_level_irq);
	else if (type & IRQ_TYPE_EDGE_BOTH)
		irq_set_handler_locked(d, handle_edge_irq);

	spin_unlock_irqrestore(&chip->spinlock, flags);
	return 0;
}

static void pch_irq_unmask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct pch_gpio *chip = gc->private;

	iowrite32(BIT(d->irq - chip->irq_base), &chip->reg->imaskclr);
}

static void pch_irq_mask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct pch_gpio *chip = gc->private;

	iowrite32(BIT(d->irq - chip->irq_base), &chip->reg->imask);
}

static void pch_irq_ack(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct pch_gpio *chip = gc->private;

	iowrite32(BIT(d->irq - chip->irq_base), &chip->reg->iclr);
}

static irqreturn_t pch_gpio_handler(int irq, void *dev_id)
{
	struct pch_gpio *chip = dev_id;
	unsigned long reg_val = ioread32(&chip->reg->istatus);
	int i;

	dev_vdbg(chip->dev, "irq=%d  status=0x%lx\n", irq, reg_val);

	reg_val &= BIT(gpio_pins[chip->ioh]) - 1;

	for_each_set_bit(i, &reg_val, gpio_pins[chip->ioh])
		generic_handle_irq(chip->irq_base + i);

	return IRQ_RETVAL(reg_val);
}

static int pch_gpio_alloc_generic_chip(struct pch_gpio *chip,
				       unsigned int irq_start,
				       unsigned int num)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	int rv;

	gc = devm_irq_alloc_generic_chip(chip->dev, "pch_gpio", 1, irq_start,
					 chip->base, handle_simple_irq);
	if (!gc)
		return -ENOMEM;

	gc->private = chip;
	ct = gc->chip_types;

	ct->chip.irq_ack = pch_irq_ack;
	ct->chip.irq_mask = pch_irq_mask;
	ct->chip.irq_unmask = pch_irq_unmask;
	ct->chip.irq_set_type = pch_irq_type;

	rv = devm_irq_setup_generic_chip(chip->dev, gc, IRQ_MSK(num),
					 IRQ_GC_INIT_MASK_CACHE,
					 IRQ_NOREQUEST | IRQ_NOPROBE, 0);

	return rv;
}

static int pch_gpio_probe(struct pci_dev *pdev,
				    const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	s32 ret;
	struct pch_gpio *chip;
	int irq_base;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->dev = dev;
	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(dev, "pci_enable_device FAILED");
		return ret;
	}

	ret = pcim_iomap_regions(pdev, BIT(1), KBUILD_MODNAME);
	if (ret) {
		dev_err(dev, "pci_request_regions FAILED-%d", ret);
		return ret;
	}

	chip->base = pcim_iomap_table(pdev)[1];
	chip->ioh = id->driver_data;
	chip->reg = chip->base;
	pci_set_drvdata(pdev, chip);
	spin_lock_init(&chip->spinlock);
	pch_gpio_setup(chip);

	ret = devm_gpiochip_add_data(dev, &chip->gpio, chip);
	if (ret) {
		dev_err(dev, "PCH gpio: Failed to register GPIO\n");
		return ret;
	}

	irq_base = devm_irq_alloc_descs(dev, -1, 0,
					gpio_pins[chip->ioh], NUMA_NO_NODE);
	if (irq_base < 0) {
		dev_warn(dev, "PCH gpio: Failed to get IRQ base num\n");
		chip->irq_base = -1;
		return 0;
	}
	chip->irq_base = irq_base;

	/* Mask all interrupts, but enable them */
	iowrite32(BIT(gpio_pins[chip->ioh]) - 1, &chip->reg->imask);
	iowrite32(BIT(gpio_pins[chip->ioh]) - 1, &chip->reg->ien);

	ret = devm_request_irq(dev, pdev->irq, pch_gpio_handler,
			       IRQF_SHARED, KBUILD_MODNAME, chip);
	if (ret) {
		dev_err(dev, "request_irq failed\n");
		return ret;
	}

	return pch_gpio_alloc_generic_chip(chip, irq_base, gpio_pins[chip->ioh]);
}

static int __maybe_unused pch_gpio_suspend(struct device *dev)
{
	struct pch_gpio *chip = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	pch_gpio_save_reg_conf(chip);
	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

static int __maybe_unused pch_gpio_resume(struct device *dev)
{
	struct pch_gpio *chip = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	iowrite32(0x01, &chip->reg->reset);
	iowrite32(0x00, &chip->reg->reset);
	pch_gpio_restore_reg_conf(chip);
	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

static SIMPLE_DEV_PM_OPS(pch_gpio_pm_ops, pch_gpio_suspend, pch_gpio_resume);

static const struct pci_device_id pch_gpio_pcidev_id[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x8803),
	  .driver_data = INTEL_EG20T_PCH },
	{ PCI_DEVICE(PCI_VENDOR_ID_ROHM, 0x8014),
	  .driver_data = OKISEMI_ML7223m_IOH },
	{ PCI_DEVICE(PCI_VENDOR_ID_ROHM, 0x8043),
	  .driver_data = OKISEMI_ML7223n_IOH },
	{ PCI_DEVICE(PCI_VENDOR_ID_ROHM, 0x8803),
	  .driver_data = INTEL_EG20T_PCH },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pch_gpio_pcidev_id);

static struct pci_driver pch_gpio_driver = {
	.name = "pch_gpio",
	.id_table = pch_gpio_pcidev_id,
	.probe = pch_gpio_probe,
	.driver = {
		.pm = &pch_gpio_pm_ops,
	},
};

module_pci_driver(pch_gpio_driver);

MODULE_DESCRIPTION("PCH GPIO PCI Driver");
MODULE_LICENSE("GPL v2");
