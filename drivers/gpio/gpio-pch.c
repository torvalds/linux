/*
 * Copyright (C) 2011 LAPIS Semiconductor Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>

#define PCH_EDGE_FALLING	0
#define PCH_EDGE_RISING		BIT(0)
#define PCH_LEVEL_L		BIT(1)
#define PCH_LEVEL_H		(BIT(0) | BIT(1))
#define PCH_EDGE_BOTH		BIT(2)
#define PCH_IM_MASK		(BIT(0) | BIT(1) | BIT(2))

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

static void pch_gpio_set(struct gpio_chip *gpio, unsigned nr, int val)
{
	u32 reg_val;
	struct pch_gpio *chip =	gpiochip_get_data(gpio);
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	reg_val = ioread32(&chip->reg->po);
	if (val)
		reg_val |= (1 << nr);
	else
		reg_val &= ~(1 << nr);

	iowrite32(reg_val, &chip->reg->po);
	spin_unlock_irqrestore(&chip->spinlock, flags);
}

static int pch_gpio_get(struct gpio_chip *gpio, unsigned nr)
{
	struct pch_gpio *chip =	gpiochip_get_data(gpio);

	return (ioread32(&chip->reg->pi) >> nr) & 1;
}

static int pch_gpio_direction_output(struct gpio_chip *gpio, unsigned nr,
				     int val)
{
	struct pch_gpio *chip =	gpiochip_get_data(gpio);
	u32 pm;
	u32 reg_val;
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);

	reg_val = ioread32(&chip->reg->po);
	if (val)
		reg_val |= (1 << nr);
	else
		reg_val &= ~(1 << nr);
	iowrite32(reg_val, &chip->reg->po);

	pm = ioread32(&chip->reg->pm) & ((1 << gpio_pins[chip->ioh]) - 1);
	pm |= (1 << nr);
	iowrite32(pm, &chip->reg->pm);

	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

static int pch_gpio_direction_input(struct gpio_chip *gpio, unsigned nr)
{
	struct pch_gpio *chip =	gpiochip_get_data(gpio);
	u32 pm;
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	pm = ioread32(&chip->reg->pm) & ((1 << gpio_pins[chip->ioh]) - 1);
	pm &= ~(1 << nr);
	iowrite32(pm, &chip->reg->pm);
	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}

#ifdef CONFIG_PM
/*
 * Save register configuration and disable interrupts.
 */
static void pch_gpio_save_reg_conf(struct pch_gpio *chip)
{
	chip->pch_gpio_reg.ien_reg = ioread32(&chip->reg->ien);
	chip->pch_gpio_reg.imask_reg = ioread32(&chip->reg->imask);
	chip->pch_gpio_reg.po_reg = ioread32(&chip->reg->po);
	chip->pch_gpio_reg.pm_reg = ioread32(&chip->reg->pm);
	chip->pch_gpio_reg.im0_reg = ioread32(&chip->reg->im0);
	if (chip->ioh == INTEL_EG20T_PCH)
		chip->pch_gpio_reg.im1_reg = ioread32(&chip->reg->im1);
	if (chip->ioh == OKISEMI_ML7223n_IOH)
		chip->pch_gpio_reg.gpio_use_sel_reg =\
					    ioread32(&chip->reg->gpio_use_sel);
}

/*
 * This function restores the register configuration of the GPIO device.
 */
static void pch_gpio_restore_reg_conf(struct pch_gpio *chip)
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
		iowrite32(chip->pch_gpio_reg.gpio_use_sel_reg,
			  &chip->reg->gpio_use_sel);
}
#endif

static int pch_gpio_to_irq(struct gpio_chip *gpio, unsigned offset)
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
	gpio->dbg_show = NULL;
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
	if (irq <= chip->irq_base + 7) {
		im_reg = &chip->reg->im0;
		im_pos = ch;
	} else {
		im_reg = &chip->reg->im1;
		im_pos = ch - 8;
	}
	dev_dbg(chip->dev, "%s:irq=%d type=%d ch=%d pos=%d\n",
		__func__, irq, type, ch, im_pos);

	spin_lock_irqsave(&chip->spinlock, flags);

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
		goto unlock;
	}

	/* Set interrupt mode */
	im = ioread32(im_reg) & ~(PCH_IM_MASK << (im_pos * 4));
	iowrite32(im | (val << (im_pos * 4)), im_reg);

	/* And the handler */
	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_LEVEL_HIGH))
		irq_set_handler_locked(d, handle_level_irq);
	else if (type & (IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING))
		irq_set_handler_locked(d, handle_edge_irq);

unlock:
	spin_unlock_irqrestore(&chip->spinlock, flags);
	return 0;
}

static void pch_irq_unmask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct pch_gpio *chip = gc->private;

	iowrite32(1 << (d->irq - chip->irq_base), &chip->reg->imaskclr);
}

static void pch_irq_mask(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct pch_gpio *chip = gc->private;

	iowrite32(1 << (d->irq - chip->irq_base), &chip->reg->imask);
}

static void pch_irq_ack(struct irq_data *d)
{
	struct irq_chip_generic *gc = irq_data_get_irq_chip_data(d);
	struct pch_gpio *chip = gc->private;

	iowrite32(1 << (d->irq - chip->irq_base), &chip->reg->iclr);
}

static irqreturn_t pch_gpio_handler(int irq, void *dev_id)
{
	struct pch_gpio *chip = dev_id;
	u32 reg_val = ioread32(&chip->reg->istatus);
	int i, ret = IRQ_NONE;

	for (i = 0; i < gpio_pins[chip->ioh]; i++) {
		if (reg_val & BIT(i)) {
			dev_dbg(chip->dev, "%s:[%d]:irq=%d  status=0x%x\n",
				__func__, i, irq, reg_val);
			generic_handle_irq(chip->irq_base + i);
			ret = IRQ_HANDLED;
		}
	}
	return ret;
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
	s32 ret;
	struct pch_gpio *chip;
	int irq_base;
	u32 msk;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "%s : pci_enable_device FAILED", __func__);
		goto err_pci_enable;
	}

	ret = pci_request_regions(pdev, KBUILD_MODNAME);
	if (ret) {
		dev_err(&pdev->dev, "pci_request_regions FAILED-%d", ret);
		goto err_request_regions;
	}

	chip->base = pci_iomap(pdev, 1, 0);
	if (!chip->base) {
		dev_err(&pdev->dev, "%s : pci_iomap FAILED", __func__);
		ret = -ENOMEM;
		goto err_iomap;
	}

	if (pdev->device == 0x8803)
		chip->ioh = INTEL_EG20T_PCH;
	else if (pdev->device == 0x8014)
		chip->ioh = OKISEMI_ML7223m_IOH;
	else if (pdev->device == 0x8043)
		chip->ioh = OKISEMI_ML7223n_IOH;

	chip->reg = chip->base;
	pci_set_drvdata(pdev, chip);
	spin_lock_init(&chip->spinlock);
	pch_gpio_setup(chip);
#ifdef CONFIG_OF_GPIO
	chip->gpio.of_node = pdev->dev.of_node;
#endif
	ret = gpiochip_add_data(&chip->gpio, chip);
	if (ret) {
		dev_err(&pdev->dev, "PCH gpio: Failed to register GPIO\n");
		goto err_gpiochip_add;
	}

	irq_base = devm_irq_alloc_descs(&pdev->dev, -1, 0,
					gpio_pins[chip->ioh], NUMA_NO_NODE);
	if (irq_base < 0) {
		dev_warn(&pdev->dev, "PCH gpio: Failed to get IRQ base num\n");
		chip->irq_base = -1;
		goto end;
	}
	chip->irq_base = irq_base;

	/* Mask all interrupts, but enable them */
	msk = (1 << gpio_pins[chip->ioh]) - 1;
	iowrite32(msk, &chip->reg->imask);
	iowrite32(msk, &chip->reg->ien);

	ret = devm_request_irq(&pdev->dev, pdev->irq, pch_gpio_handler,
			       IRQF_SHARED, KBUILD_MODNAME, chip);
	if (ret != 0) {
		dev_err(&pdev->dev,
			"%s request_irq failed\n", __func__);
		goto err_request_irq;
	}

	ret = pch_gpio_alloc_generic_chip(chip, irq_base,
					  gpio_pins[chip->ioh]);
	if (ret)
		goto err_request_irq;

end:
	return 0;

err_request_irq:
	gpiochip_remove(&chip->gpio);

err_gpiochip_add:
	pci_iounmap(pdev, chip->base);

err_iomap:
	pci_release_regions(pdev);

err_request_regions:
	pci_disable_device(pdev);

err_pci_enable:
	kfree(chip);
	dev_err(&pdev->dev, "%s Failed returns %d\n", __func__, ret);
	return ret;
}

static void pch_gpio_remove(struct pci_dev *pdev)
{
	struct pch_gpio *chip = pci_get_drvdata(pdev);

	gpiochip_remove(&chip->gpio);
	pci_iounmap(pdev, chip->base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	kfree(chip);
}

#ifdef CONFIG_PM
static int pch_gpio_suspend(struct pci_dev *pdev, pm_message_t state)
{
	s32 ret;
	struct pch_gpio *chip = pci_get_drvdata(pdev);
	unsigned long flags;

	spin_lock_irqsave(&chip->spinlock, flags);
	pch_gpio_save_reg_conf(chip);
	spin_unlock_irqrestore(&chip->spinlock, flags);

	ret = pci_save_state(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_save_state Failed-%d\n", ret);
		return ret;
	}
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D0);
	ret = pci_enable_wake(pdev, PCI_D0, 1);
	if (ret)
		dev_err(&pdev->dev, "pci_enable_wake Failed -%d\n", ret);

	return 0;
}

static int pch_gpio_resume(struct pci_dev *pdev)
{
	s32 ret;
	struct pch_gpio *chip = pci_get_drvdata(pdev);
	unsigned long flags;

	ret = pci_enable_wake(pdev, PCI_D0, 0);

	pci_set_power_state(pdev, PCI_D0);
	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "pci_enable_device Failed-%d ", ret);
		return ret;
	}
	pci_restore_state(pdev);

	spin_lock_irqsave(&chip->spinlock, flags);
	iowrite32(0x01, &chip->reg->reset);
	iowrite32(0x00, &chip->reg->reset);
	pch_gpio_restore_reg_conf(chip);
	spin_unlock_irqrestore(&chip->spinlock, flags);

	return 0;
}
#else
#define pch_gpio_suspend NULL
#define pch_gpio_resume NULL
#endif

static const struct pci_device_id pch_gpio_pcidev_id[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, 0x8803) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ROHM, 0x8014) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ROHM, 0x8043) },
	{ PCI_DEVICE(PCI_VENDOR_ID_ROHM, 0x8803) },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pch_gpio_pcidev_id);

static struct pci_driver pch_gpio_driver = {
	.name = "pch_gpio",
	.id_table = pch_gpio_pcidev_id,
	.probe = pch_gpio_probe,
	.remove = pch_gpio_remove,
	.suspend = pch_gpio_suspend,
	.resume = pch_gpio_resume
};

module_pci_driver(pch_gpio_driver);

MODULE_DESCRIPTION("PCH GPIO PCI Driver");
MODULE_LICENSE("GPL");
