// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 OKI SEMICONDUCTOR Co., LTD.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#define IOH_EDGE_FALLING	0
#define IOH_EDGE_RISING		BIT(0)
#define IOH_LEVEL_L		BIT(1)
#define IOH_LEVEL_H		(BIT(0) | BIT(1))
#define IOH_EDGE_BOTH		BIT(2)
#define IOH_IM_MASK		(BIT(0) | BIT(1) | BIT(2))

#define IOH_IRQ_BASE		0

struct ioh_reg_comn {
	u32	ien;
	u32	istatus;
	u32	idisp;
	u32	iclr;
	u32	imask;
	u32	imaskclr;
	u32	po;
	u32	pi;
	u32	pm;
	u32	im_0;
	u32	im_1;
	u32	reserved;
};

struct ioh_regs {
	struct ioh_reg_comn regs[8];
	u32 reserve1[16];
	u32 ioh_sel_reg[4];
	u32 reserve2[11];
	u32 srst;
};

/**
 * struct ioh_gpio_reg_data - The register store data.
 * @ien_reg:	To store contents of interrupt enable register.
 * @imask_reg:	To store contents of interrupt mask regist
 * @po_reg:	To store contents of PO register.
 * @pm_reg:	To store contents of PM register.
 * @im0_reg:	To store contents of interrupt mode regist0
 * @im1_reg:	To store contents of interrupt mode regist1
 * @use_sel_reg: To store contents of GPIO_USE_SEL0~3
 */
struct ioh_gpio_reg_data {
	u32 ien_reg;
	u32 imask_reg;
	u32 po_reg;
	u32 pm_reg;
	u32 im0_reg;
	u32 im1_reg;
	u32 use_sel_reg;
};

/**
 * struct ioh_gpio - GPIO private data structure.
 * @base:			PCI base address of Memory mapped I/O register.
 * @reg:			Memory mapped IOH GPIO register list.
 * @dev:			Pointer to device structure.
 * @gpio:			Data for GPIO infrastructure.
 * @ioh_gpio_reg:		Memory mapped Register data is saved here
 *				when suspend.
 * @gpio_use_sel:		Save GPIO_USE_SEL1~4 register for PM
 * @ch:				Indicate GPIO channel
 * @irq_base:		Save base of IRQ number for interrupt
 * @spinlock:		Used for register access protection
 */
struct ioh_gpio {
	void __iomem *base;
	struct ioh_regs __iomem *reg;
	struct device *dev;
	struct gpio_chip gpio;
	struct ioh_gpio_reg_data ioh_gpio_reg;
	u32 gpio_use_sel;
	int ch;
	int irq_base;
	spinlock_t spinlock;
};

static const int num_ports[] = {6, 12, 16, 16, 15, 16, 16, 12};

static int ioh_gpio_set(struct gpio_chip *gpio, unsigned int nr, int val)
{
	u32 reg_val;
	struct ioh_gpio *chip =	gpiochip_get_data(gpio);
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	reg_val = ioread32(&chip->reg->regs[chip->ch].po);
	if (val)
		reg_val |= BIT(nr);
	else
		reg_val &= ~BIT(nr);

	iowrite32(reg_val, &chip->reg->regs[chip->ch].po);
	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

static int ioh_gpio_get(struct gpio_chip *gpio, unsigned nr)
{
	struct ioh_gpio *chip =	gpiochip_get_data(gpio);

	return !!(ioread32(&chip->reg->regs[chip->ch].pi) & BIT(nr));
}

static int ioh_gpio_direction_output(struct gpio_chip *gpio, unsigned nr,
				     int val)
{
	struct ioh_gpio *chip =	gpiochip_get_data(gpio);
	u32 pm;
	u32 reg_val;
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	pm = ioread32(&chip->reg->regs[chip->ch].pm);
	pm &= BIT(num_ports[chip->ch]) - 1;
	pm |= BIT(nr);
	iowrite32(pm, &chip->reg->regs[chip->ch].pm);

	reg_val = ioread32(&chip->reg->regs[chip->ch].po);
	if (val)
		reg_val |= BIT(nr);
	else
		reg_val &= ~BIT(nr);
	iowrite32(reg_val, &chip->reg->regs[chip->ch].po);

	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

static int ioh_gpio_direction_input(struct gpio_chip *gpio, unsigned nr)
{
	struct ioh_gpio *chip =	gpiochip_get_data(gpio);
	u32 pm;
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	pm = ioread32(&chip->reg->regs[chip->ch].pm);
	pm &= BIT(num_ports[chip->ch]) - 1;
	pm &= ~BIT(nr);
	iowrite32(pm, &chip->reg->regs[chip->ch].pm);
	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

/*
 * Save register configuration and disable interrupts.
 */
static void __maybe_unused ioh_gpio_save_reg_conf(struct ioh_gpio *chip)
{
	int i;

	for (i = 0; i < 8; i ++, chip++) {
		chip->ioh_gpio_reg.po_reg =
					ioread32(&chip->reg->regs[chip->ch].po);
		chip->ioh_gpio_reg.pm_reg =
					ioread32(&chip->reg->regs[chip->ch].pm);
		chip->ioh_gpio_reg.ien_reg =
				       ioread32(&chip->reg->regs[chip->ch].ien);
		chip->ioh_gpio_reg.imask_reg =
				     ioread32(&chip->reg->regs[chip->ch].imask);
		chip->ioh_gpio_reg.im0_reg =
				      ioread32(&chip->reg->regs[chip->ch].im_0);
		chip->ioh_gpio_reg.im1_reg =
				      ioread32(&chip->reg->regs[chip->ch].im_1);
		if (i < 4)
			chip->ioh_gpio_reg.use_sel_reg =
					   ioread32(&chip->reg->ioh_sel_reg[i]);
	}
}

/*
 * This function restores the register configuration of the GPIO device.
 */
static void __maybe_unused ioh_gpio_restore_reg_conf(struct ioh_gpio *chip)
{
	int i;

	for (i = 0; i < 8; i ++, chip++) {
		iowrite32(chip->ioh_gpio_reg.po_reg,
			  &chip->reg->regs[chip->ch].po);
		iowrite32(chip->ioh_gpio_reg.pm_reg,
			  &chip->reg->regs[chip->ch].pm);
		iowrite32(chip->ioh_gpio_reg.ien_reg,
			  &chip->reg->regs[chip->ch].ien);
		iowrite32(chip->ioh_gpio_reg.imask_reg,
			  &chip->reg->regs[chip->ch].imask);
		iowrite32(chip->ioh_gpio_reg.im0_reg,
			  &chip->reg->regs[chip->ch].im_0);
		iowrite32(chip->ioh_gpio_reg.im1_reg,
			  &chip->reg->regs[chip->ch].im_1);
		if (i < 4)
			iowrite32(chip->ioh_gpio_reg.use_sel_reg,
				  &chip->reg->ioh_sel_reg[i]);
	}
}

static int ioh_gpio_to_irq(struct gpio_chip *gpio, unsigned offset)
{
	struct ioh_gpio *chip = gpiochip_get_data(gpio);
	return chip->irq_base + offset;
}

static void ioh_gpio_setup(struct ioh_gpio *chip, int num_port)
{
	struct gpio_chip *gpio = &chip->gpio;

	gpio->label = dev_name(chip->dev);
	gpio->owner = THIS_MODULE;
	gpio->direction_input = ioh_gpio_direction_input;
	gpio->get = ioh_gpio_get;
	gpio->direction_output = ioh_gpio_direction_output;
	gpio->set = ioh_gpio_set;
	gpio->dbg_show = NULL;
	gpio->base = -1;
	gpio->ngpio = num_port;
	gpio->can_sleep = false;
	gpio->to_irq = ioh_gpio_to_irq;
}

static int ioh_irq_type(struct irq_data *d, unsigned int type)
{
	u32 im;
	void __iomem *im_reg;
	u32 ien;
	u32 im_pos;
	int ch;
	unsigned long flags;
	u32 val;
	int irq = d->irq;
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct ioh_gpio *chip = gc->private;

	ch = irq - chip->irq_base;
	if (irq <= chip->irq_base + 7) {
		im_reg = &chip->reg->regs[chip->ch].im_0;
		im_pos = ch;
	} else {
		im_reg = &chip->reg->regs[chip->ch].im_1;
		im_pos = ch - 8;
	}
	dev_dbg(chip->dev, "%s:irq=%d type=%d ch=%d pos=%d type=%d\n",
		__func__, irq, type, ch, im_pos, type);

	spin_lock_irqsave(&chip->spinlock, flags);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		val = IOH_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		val = IOH_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		val = IOH_EDGE_BOTH;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		val = IOH_LEVEL_H;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		val = IOH_LEVEL_L;
		break;
	case IRQ_TYPE_PROBE:
		goto end;
	default:
		dev_warn(chip->dev, "%s: unknown type(%dd)",
			__func__, type);
		goto end;
	}

	/* Set interrupt mode */
	im = ioread32(im_reg) & ~(IOH_IM_MASK << (im_pos * 4));
	iowrite32(im | (val << (im_pos * 4)), im_reg);

	/* iclr */
	iowrite32(BIT(ch), &chip->reg->regs[chip->ch].iclr);

	/* IMASKCLR */
	iowrite32(BIT(ch), &chip->reg->regs[chip->ch].imaskclr);

	/* Enable interrupt */
	ien = ioread32(&chip->reg->regs[chip->ch].ien);
	iowrite32(ien | BIT(ch), &chip->reg->regs[chip->ch].ien);
end:
	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

static void ioh_irq_unmask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct ioh_gpio *chip = gc->private;

	iowrite32(BIT(d->irq - chip->irq_base),
		  &chip->reg->regs[chip->ch].imaskclr);
}

static void ioh_irq_mask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct ioh_gpio *chip = gc->private;

	iowrite32(BIT(d->irq - chip->irq_base),
		  &chip->reg->regs[chip->ch].imask);
}

static void ioh_irq_disable(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct ioh_gpio *chip = gc->private;
	unsigned long flags;
	u32 ien;

	spin_lock_irqsave(&chip->spinlock, flags);
	ien = ioread32(&chip->reg->regs[chip->ch].ien);
	ien &= ~BIT(d->irq - chip->irq_base);
	iowrite32(ien, &chip->reg->regs[chip->ch].ien);
	spin_unlock_irqrestore(&chip->spinlock, flags);
}

static void ioh_irq_enable(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct ioh_gpio *chip = gc->private;
	unsigned long flags;
	u32 ien;

	spin_lock_irqsave(&chip->spinlock, flags);
	ien = ioread32(&chip->reg->regs[chip->ch].ien);
	ien |= BIT(d->irq - chip->irq_base);
	iowrite32(ien, &chip->reg->regs[chip->ch].ien);
	spin_unlock_irqrestore(&chip->spinlock, flags);
}

static irqreturn_t ioh_gpio_handler(int irq, void *dev_id)
{
	struct ioh_gpio *chip = dev_id;
	u32 reg_val;
	int i, j;
	int ret = IRQ_NONE;

	for (i = 0; i < 8; i++, chip++) {
		reg_val = ioread32(&chip->reg->regs[i].istatus);
		for (j = 0; j < num_ports[i]; j++) {
			if (reg_val & BIT(j)) {
				dev_dbg(chip->dev,
					"%s:[%d]:irq=%d status=0x%x\n",
					__func__, j, irq, reg_val);
				iowrite32(BIT(j),
					  &chip->reg->regs[chip->ch].iclr);
				generic_handle_irq(chip->irq_base + j);
				ret = IRQ_HANDLED;
			}
		}
	}
	return ret;
}

static int ioh_gpio_alloc_generic_chip(struct ioh_gpio *chip,
				       unsigned int irq_start,
				       unsigned int num)
{
	struct irq_chip_generic *gc;
	struct irq_chip_type *ct;
	int rv;

	gc = devm_irq_alloc_generic_chip(chip->dev, "ioh_gpio", 1, irq_start,
					 chip->base, handle_simple_irq);
	if (!gc)
		return -ENOMEM;

	gc->private = chip;
	ct = gc->chip_types;

	ct->chip.irq_mask = ioh_irq_mask;
	ct->chip.irq_unmask = ioh_irq_unmask;
	ct->chip.irq_set_type = ioh_irq_type;
	ct->chip.irq_disable = ioh_irq_disable;
	ct->chip.irq_enable = ioh_irq_enable;

	rv = devm_irq_setup_generic_chip(chip->dev, gc, IRQ_MSK(num),
					 IRQ_GC_INIT_MASK_CACHE,
					 IRQ_NOREQUEST | IRQ_NOPROBE, 0);

	return rv;
}

static int ioh_gpio_probe(struct pci_dev *pdev,
				    const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	int ret;
	int i, j;
	struct ioh_gpio *chip;
	void __iomem *base;
	void *chip_save;
	int irq_base;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(dev, "%s : pcim_enable_device failed", __func__);
		return ret;
	}

	ret = pcim_iomap_regions(pdev, BIT(1), KBUILD_MODNAME);
	if (ret) {
		dev_err(dev, "pcim_iomap_regions failed-%d", ret);
		return ret;
	}

	base = pcim_iomap_table(pdev)[1];
	if (!base) {
		dev_err(dev, "%s : pcim_iomap_table failed", __func__);
		return -ENOMEM;
	}

	chip_save = devm_kcalloc(dev, 8, sizeof(*chip), GFP_KERNEL);
	if (chip_save == NULL) {
		return -ENOMEM;
	}

	chip = chip_save;
	for (i = 0; i < 8; i++, chip++) {
		chip->dev = dev;
		chip->base = base;
		chip->reg = chip->base;
		chip->ch = i;
		spin_lock_init(&chip->spinlock);
		ioh_gpio_setup(chip, num_ports[i]);
		ret = devm_gpiochip_add_data(dev, &chip->gpio, chip);
		if (ret) {
			dev_err(dev, "IOH gpio: Failed to register GPIO\n");
			return ret;
		}
	}

	chip = chip_save;
	for (j = 0; j < 8; j++, chip++) {
		irq_base = devm_irq_alloc_descs(dev, -1, IOH_IRQ_BASE,
						num_ports[j], NUMA_NO_NODE);
		if (irq_base < 0) {
			dev_warn(dev,
				"ml_ioh_gpio: Failed to get IRQ base num\n");
			return irq_base;
		}
		chip->irq_base = irq_base;

		ret = ioh_gpio_alloc_generic_chip(chip,
						  irq_base, num_ports[j]);
		if (ret)
			return ret;
	}

	chip = chip_save;
	ret = devm_request_irq(dev, pdev->irq, ioh_gpio_handler,
			       IRQF_SHARED, KBUILD_MODNAME, chip);
	if (ret != 0) {
		dev_err(dev, "%s request_irq failed\n", __func__);
		return ret;
	}

	pci_set_drvdata(pdev, chip);

	return 0;
}

static int __maybe_unused ioh_gpio_suspend(struct device *dev)
{
	struct ioh_gpio *chip = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	ioh_gpio_save_reg_conf(chip);
	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

static int __maybe_unused ioh_gpio_resume(struct device *dev)
{
	struct ioh_gpio *chip = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	iowrite32(0x01, &chip->reg->srst);
	iowrite32(0x00, &chip->reg->srst);
	ioh_gpio_restore_reg_conf(chip);
	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

static SIMPLE_DEV_PM_OPS(ioh_gpio_pm_ops, ioh_gpio_suspend, ioh_gpio_resume);

static const struct pci_device_id ioh_gpio_pcidev_id[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ROHM, 0x802E) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, ioh_gpio_pcidev_id);

static struct pci_driver ioh_gpio_driver = {
	.name = "ml_ioh_gpio",
	.id_table = ioh_gpio_pcidev_id,
	.probe = ioh_gpio_probe,
	.driver = {
		.pm = &ioh_gpio_pm_ops,
	},
};

module_pci_driver(ioh_gpio_driver);

MODULE_DESCRIPTION("OKI SEMICONDUCTOR ML-IOH series GPIO Driver");
MODULE_LICENSE("GPL");
