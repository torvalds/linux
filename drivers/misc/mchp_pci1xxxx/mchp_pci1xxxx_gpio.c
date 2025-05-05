// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022 Microchip Technology Inc.
// pci1xxxx gpio driver

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/gpio/driver.h>
#include <linux/bio.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>

#include "mchp_pci1xxxx_gp.h"

#define PCI1XXXX_NR_PINS		93
#define PERI_GEN_RESET			0
#define OUT_EN_OFFSET(x)		((((x) / 32) * 4) + 0x400)
#define INP_EN_OFFSET(x)		((((x) / 32) * 4) + 0x400 + 0x10)
#define OUT_OFFSET(x)			((((x) / 32) * 4) + 0x400 + 0x20)
#define INP_OFFSET(x)			((((x) / 32) * 4) + 0x400 + 0x30)
#define PULLUP_OFFSET(x)		((((x) / 32) * 4) + 0x400 + 0x40)
#define PULLDOWN_OFFSET(x)		((((x) / 32) * 4) + 0x400 + 0x50)
#define OPENDRAIN_OFFSET(x)		((((x) / 32) * 4) + 0x400 + 0x60)
#define WAKEMASK_OFFSET(x)		((((x) / 32) * 4) + 0x400 + 0x70)
#define MODE_OFFSET(x)			((((x) / 32) * 4) + 0x400 + 0x80)
#define INTR_LO_TO_HI_EDGE_CONFIG(x)	((((x) / 32) * 4) + 0x400 + 0x90)
#define INTR_HI_TO_LO_EDGE_CONFIG(x)	((((x) / 32) * 4) + 0x400 + 0xA0)
#define INTR_LEVEL_CONFIG_OFFSET(x)	((((x) / 32) * 4) + 0x400 + 0xB0)
#define INTR_LEVEL_MASK_OFFSET(x)	((((x) / 32) * 4) + 0x400 + 0xC0)
#define INTR_STAT_OFFSET(x)		((((x) / 32) * 4) + 0x400 + 0xD0)
#define DEBOUNCE_OFFSET(x)		((((x) / 32) * 4) + 0x400 + 0xE0)
#define PIO_GLOBAL_CONFIG_OFFSET	(0x400 + 0xF0)
#define PIO_PCI_CTRL_REG_OFFSET	(0x400 + 0xF4)
#define INTR_MASK_OFFSET(x)		((((x) / 32) * 4) + 0x400 + 0x100)
#define INTR_STATUS_OFFSET(x)		(((x) * 4) + 0x400 + 0xD0)

struct pci1xxxx_gpio {
	struct auxiliary_device *aux_dev;
	void __iomem *reg_base;
	raw_spinlock_t wa_lock;
	struct gpio_chip gpio;
	spinlock_t lock;
	int irq_base;
};

static int pci1xxxx_gpio_get_direction(struct gpio_chip *gpio, unsigned int nr)
{
	struct pci1xxxx_gpio *priv = gpiochip_get_data(gpio);
	u32 data;
	int ret = -EINVAL;

	data = readl(priv->reg_base + INP_EN_OFFSET(nr));
	if (data & BIT(nr % 32)) {
		ret =  1;
	} else {
		data = readl(priv->reg_base + OUT_EN_OFFSET(nr));
		if (data & BIT(nr % 32))
			ret =  0;
	}

	return ret;
}

static inline void pci1xxx_assign_bit(void __iomem *base_addr, unsigned int reg_offset,
				      unsigned int bitpos, bool set)
{
	u32 data;

	data = readl(base_addr + reg_offset);
	if (set)
		data |= BIT(bitpos);
	else
		data &= ~BIT(bitpos);
	writel(data, base_addr + reg_offset);
}

static int pci1xxxx_gpio_direction_input(struct gpio_chip *gpio, unsigned int nr)
{
	struct pci1xxxx_gpio *priv = gpiochip_get_data(gpio);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	pci1xxx_assign_bit(priv->reg_base, INP_EN_OFFSET(nr), (nr % 32), true);
	pci1xxx_assign_bit(priv->reg_base, OUT_EN_OFFSET(nr), (nr % 32), false);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int pci1xxxx_gpio_get(struct gpio_chip *gpio, unsigned int nr)
{
	struct pci1xxxx_gpio *priv = gpiochip_get_data(gpio);

	return (readl(priv->reg_base + INP_OFFSET(nr)) >> (nr % 32)) & 1;
}

static int pci1xxxx_gpio_direction_output(struct gpio_chip *gpio,
					  unsigned int nr, int val)
{
	struct pci1xxxx_gpio *priv = gpiochip_get_data(gpio);
	unsigned long flags;
	u32 data;

	spin_lock_irqsave(&priv->lock, flags);
	pci1xxx_assign_bit(priv->reg_base, INP_EN_OFFSET(nr), (nr % 32), false);
	pci1xxx_assign_bit(priv->reg_base, OUT_EN_OFFSET(nr), (nr % 32), true);
	data = readl(priv->reg_base + OUT_OFFSET(nr));
	if (val)
		data |= (1 << (nr % 32));
	else
		data &= ~(1 << (nr % 32));
	writel(data, priv->reg_base + OUT_OFFSET(nr));
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static void pci1xxxx_gpio_set(struct gpio_chip *gpio,
			      unsigned int nr, int val)
{
	struct pci1xxxx_gpio *priv = gpiochip_get_data(gpio);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	pci1xxx_assign_bit(priv->reg_base, OUT_OFFSET(nr), (nr % 32), val);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int pci1xxxx_gpio_set_config(struct gpio_chip *gpio, unsigned int offset,
				    unsigned long config)
{
	struct pci1xxxx_gpio *priv = gpiochip_get_data(gpio);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&priv->lock, flags);
	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_BIAS_PULL_UP:
		pci1xxx_assign_bit(priv->reg_base, PULLUP_OFFSET(offset), (offset % 32), true);
		break;
	case PIN_CONFIG_BIAS_PULL_DOWN:
		pci1xxx_assign_bit(priv->reg_base, PULLDOWN_OFFSET(offset), (offset % 32), true);
		break;
	case PIN_CONFIG_BIAS_DISABLE:
		pci1xxx_assign_bit(priv->reg_base, PULLUP_OFFSET(offset), (offset % 32), false);
		pci1xxx_assign_bit(priv->reg_base, PULLDOWN_OFFSET(offset), (offset % 32), false);
		break;
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		pci1xxx_assign_bit(priv->reg_base, OPENDRAIN_OFFSET(offset), (offset % 32), true);
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		pci1xxx_assign_bit(priv->reg_base, OPENDRAIN_OFFSET(offset), (offset % 32), false);
		break;
	default:
		ret = -ENOTSUPP;
		break;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

static void pci1xxxx_gpio_irq_ack(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct pci1xxxx_gpio *priv = gpiochip_get_data(chip);
	unsigned int gpio = irqd_to_hwirq(data);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	writel(BIT(gpio % 32), priv->reg_base + INTR_STAT_OFFSET(gpio));
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void pci1xxxx_gpio_irq_set_mask(struct irq_data *data, bool set)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct pci1xxxx_gpio *priv = gpiochip_get_data(chip);
	unsigned int gpio = irqd_to_hwirq(data);
	unsigned long flags;

	if (!set)
		gpiochip_enable_irq(chip, gpio);
	spin_lock_irqsave(&priv->lock, flags);
	pci1xxx_assign_bit(priv->reg_base, INTR_MASK_OFFSET(gpio), (gpio % 32), set);
	spin_unlock_irqrestore(&priv->lock, flags);
	if (set)
		gpiochip_disable_irq(chip, gpio);
}

static void pci1xxxx_gpio_irq_mask(struct irq_data *data)
{
	pci1xxxx_gpio_irq_set_mask(data, true);
}

static void pci1xxxx_gpio_irq_unmask(struct irq_data *data)
{
	pci1xxxx_gpio_irq_set_mask(data, false);
}

static int pci1xxxx_gpio_set_type(struct irq_data *data, unsigned int trigger_type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	struct pci1xxxx_gpio *priv = gpiochip_get_data(chip);
	unsigned int gpio = irqd_to_hwirq(data);
	unsigned int bitpos = gpio % 32;

	if (trigger_type & IRQ_TYPE_EDGE_FALLING) {
		pci1xxx_assign_bit(priv->reg_base, INTR_HI_TO_LO_EDGE_CONFIG(gpio),
				   bitpos, false);
		pci1xxx_assign_bit(priv->reg_base, MODE_OFFSET(gpio),
				   bitpos, false);
		irq_set_handler_locked(data, handle_edge_irq);
	} else {
		pci1xxx_assign_bit(priv->reg_base, INTR_HI_TO_LO_EDGE_CONFIG(gpio),
				   bitpos, true);
	}

	if (trigger_type & IRQ_TYPE_EDGE_RISING) {
		pci1xxx_assign_bit(priv->reg_base, INTR_LO_TO_HI_EDGE_CONFIG(gpio),
				   bitpos, false);
		pci1xxx_assign_bit(priv->reg_base, MODE_OFFSET(gpio), bitpos,
				   false);
		irq_set_handler_locked(data, handle_edge_irq);
	} else {
		pci1xxx_assign_bit(priv->reg_base, INTR_LO_TO_HI_EDGE_CONFIG(gpio),
				   bitpos, true);
	}

	if (trigger_type & IRQ_TYPE_LEVEL_LOW) {
		pci1xxx_assign_bit(priv->reg_base, INTR_LEVEL_CONFIG_OFFSET(gpio),
				   bitpos, true);
		pci1xxx_assign_bit(priv->reg_base, INTR_LEVEL_MASK_OFFSET(gpio),
				   bitpos, false);
		pci1xxx_assign_bit(priv->reg_base, MODE_OFFSET(gpio), bitpos,
				   true);
		irq_set_handler_locked(data, handle_edge_irq);
	}

	if (trigger_type & IRQ_TYPE_LEVEL_HIGH) {
		pci1xxx_assign_bit(priv->reg_base, INTR_LEVEL_CONFIG_OFFSET(gpio),
				   bitpos, false);
		pci1xxx_assign_bit(priv->reg_base, INTR_LEVEL_MASK_OFFSET(gpio),
				   bitpos, false);
		pci1xxx_assign_bit(priv->reg_base, MODE_OFFSET(gpio), bitpos,
				   true);
		irq_set_handler_locked(data, handle_edge_irq);
	}

	if ((!(trigger_type & IRQ_TYPE_LEVEL_LOW)) && (!(trigger_type & IRQ_TYPE_LEVEL_HIGH)))
		pci1xxx_assign_bit(priv->reg_base, INTR_LEVEL_MASK_OFFSET(gpio), bitpos, true);

	return true;
}

static irqreturn_t pci1xxxx_gpio_irq_handler(int irq, void *dev_id)
{
	struct pci1xxxx_gpio *priv = dev_id;
	struct gpio_chip *gc =  &priv->gpio;
	unsigned long int_status = 0;
	unsigned long wa_flags;
	unsigned long flags;
	u8 pincount;
	int bit;
	u8 gpiobank;

	spin_lock_irqsave(&priv->lock, flags);
	pci1xxx_assign_bit(priv->reg_base, PIO_GLOBAL_CONFIG_OFFSET, 16, true);
	spin_unlock_irqrestore(&priv->lock, flags);
	for (gpiobank = 0; gpiobank < 3; gpiobank++) {
		spin_lock_irqsave(&priv->lock, flags);
		int_status = readl(priv->reg_base + INTR_STATUS_OFFSET(gpiobank));
		spin_unlock_irqrestore(&priv->lock, flags);
		if (gpiobank == 2)
			pincount = 29;
		else
			pincount = 32;
		for_each_set_bit(bit, &int_status, pincount) {
			unsigned int irq;

			spin_lock_irqsave(&priv->lock, flags);
			writel(BIT(bit), priv->reg_base + INTR_STATUS_OFFSET(gpiobank));
			spin_unlock_irqrestore(&priv->lock, flags);
			irq = irq_find_mapping(gc->irq.domain, (bit + (gpiobank * 32)));
			raw_spin_lock_irqsave(&priv->wa_lock, wa_flags);
			generic_handle_irq(irq);
			raw_spin_unlock_irqrestore(&priv->wa_lock, wa_flags);
		}
	}
	spin_lock_irqsave(&priv->lock, flags);
	pci1xxx_assign_bit(priv->reg_base, PIO_GLOBAL_CONFIG_OFFSET, 16, false);
	spin_unlock_irqrestore(&priv->lock, flags);

	return IRQ_HANDLED;
}

static const struct irq_chip pci1xxxx_gpio_irqchip = {
	.name = "pci1xxxx_gpio",
	.irq_ack = pci1xxxx_gpio_irq_ack,
	.irq_mask = pci1xxxx_gpio_irq_mask,
	.irq_unmask = pci1xxxx_gpio_irq_unmask,
	.irq_set_type = pci1xxxx_gpio_set_type,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int pci1xxxx_gpio_suspend(struct device *dev)
{
	struct pci1xxxx_gpio *priv = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	pci1xxx_assign_bit(priv->reg_base, PIO_GLOBAL_CONFIG_OFFSET,
			   16, true);
	pci1xxx_assign_bit(priv->reg_base, PIO_GLOBAL_CONFIG_OFFSET,
			   17, false);
	pci1xxx_assign_bit(priv->reg_base, PERI_GEN_RESET, 16, true);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int pci1xxxx_gpio_resume(struct device *dev)
{
	struct pci1xxxx_gpio *priv = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	pci1xxx_assign_bit(priv->reg_base, PIO_GLOBAL_CONFIG_OFFSET,
			   17, true);
	pci1xxx_assign_bit(priv->reg_base, PIO_GLOBAL_CONFIG_OFFSET,
			   16, false);
	pci1xxx_assign_bit(priv->reg_base, PERI_GEN_RESET, 16, false);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int pci1xxxx_gpio_setup(struct pci1xxxx_gpio *priv, int irq)
{
	struct gpio_chip *gchip = &priv->gpio;
	struct gpio_irq_chip *girq;
	int retval;

	gchip->label = dev_name(&priv->aux_dev->dev);
	gchip->parent = &priv->aux_dev->dev;
	gchip->owner = THIS_MODULE;
	gchip->direction_input = pci1xxxx_gpio_direction_input;
	gchip->direction_output = pci1xxxx_gpio_direction_output;
	gchip->get_direction = pci1xxxx_gpio_get_direction;
	gchip->get = pci1xxxx_gpio_get;
	gchip->set = pci1xxxx_gpio_set;
	gchip->set_config = pci1xxxx_gpio_set_config;
	gchip->dbg_show = NULL;
	gchip->base = -1;
	gchip->ngpio =  PCI1XXXX_NR_PINS;
	gchip->can_sleep = false;

	retval = devm_request_threaded_irq(&priv->aux_dev->dev, irq,
					   NULL, pci1xxxx_gpio_irq_handler,
					   IRQF_ONESHOT, "PCI1xxxxGPIO", priv);

	if (retval)
		return retval;

	girq = &priv->gpio.irq;
	gpio_irq_chip_set_chip(girq, &pci1xxxx_gpio_irqchip);
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_bad_irq;

	return 0;
}

static int pci1xxxx_gpio_probe(struct auxiliary_device *aux_dev,
			       const struct auxiliary_device_id *id)

{
	struct auxiliary_device_wrapper *aux_dev_wrapper;
	struct gp_aux_data_type *pdata;
	struct pci1xxxx_gpio *priv;
	int retval;

	aux_dev_wrapper = (struct auxiliary_device_wrapper *)
			  container_of(aux_dev, struct auxiliary_device_wrapper, aux_dev);

	pdata = &aux_dev_wrapper->gp_aux_data;

	if (!pdata)
		return -EINVAL;

	priv = devm_kzalloc(&aux_dev->dev, sizeof(struct pci1xxxx_gpio), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	spin_lock_init(&priv->lock);
	priv->aux_dev = aux_dev;

	if (!devm_request_mem_region(&aux_dev->dev, pdata->region_start, 0x800, aux_dev->name))
		return -EBUSY;

	priv->reg_base = devm_ioremap(&aux_dev->dev, pdata->region_start, 0x800);
	if (!priv->reg_base)
		return -ENOMEM;

	writel(0x0264, (priv->reg_base + 0x400 + 0xF0));

	retval = pci1xxxx_gpio_setup(priv, pdata->irq_num);

	if (retval < 0)
		return retval;

	dev_set_drvdata(&aux_dev->dev, priv);

	return devm_gpiochip_add_data(&aux_dev->dev, &priv->gpio, priv);
}

static DEFINE_SIMPLE_DEV_PM_OPS(pci1xxxx_gpio_pm_ops, pci1xxxx_gpio_suspend, pci1xxxx_gpio_resume);

static const struct auxiliary_device_id pci1xxxx_gpio_auxiliary_id_table[] = {
	{.name = "mchp_pci1xxxx_gp.gp_gpio"},
	{}
};
MODULE_DEVICE_TABLE(auxiliary, pci1xxxx_gpio_auxiliary_id_table);

static struct auxiliary_driver pci1xxxx_gpio_driver = {
	.driver = {
		.name = "PCI1xxxxGPIO",
		.pm = &pci1xxxx_gpio_pm_ops,
		},
	.probe = pci1xxxx_gpio_probe,
	.id_table = pci1xxxx_gpio_auxiliary_id_table
};
module_auxiliary_driver(pci1xxxx_gpio_driver);

MODULE_DESCRIPTION("Microchip Technology Inc. PCI1xxxx GPIO controller");
MODULE_AUTHOR("Kumaravel Thiagarajan <kumaravel.thiagarajan@microchip.com>");
MODULE_LICENSE("GPL");
