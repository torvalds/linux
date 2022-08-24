// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2022 Microchip Technology Inc.
// pci1xxxx gpio driver

#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/gpio/driver.h>
#include <linux/bio.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/kthread.h>

#include "mchp_pci1xxxx_gp.h"

#define PCI1XXXX_NR_PINS		93
#define PULLUP_OFFSET(x)		((((x) / 32) * 4) + 0x400 + 0x40)
#define PULLDOWN_OFFSET(x)		((((x) / 32) * 4) + 0x400 + 0x50)
#define OPENDRAIN_OFFSET(x)		((((x) / 32) * 4) + 0x400 + 0x60)
#define DEBOUNCE_OFFSET(x)		((((x) / 32) * 4) + 0x400 + 0xE0)
#define PIO_GLOBAL_CONFIG_OFFSET	(0x400 + 0xF0)
#define PIO_PCI_CTRL_REG_OFFSET	(0x400 + 0xF4)
#define INTR_MASK_OFFSET(x)		((((x) / 32) * 4) + 0x400 + 0x100)
#define INTR_STATUS_OFFSET(x)		(((x) * 4) + 0x400 + 0xD0)

struct pci1xxxx_gpio {
	struct auxiliary_device *aux_dev;
	void __iomem *reg_base;
	struct gpio_chip gpio;
	spinlock_t lock;
	int irq_base;
};

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
	default:
		ret = -EOPNOTSUPP;
		break;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	return ret;
}

static int pci1xxxx_gpio_setup(struct pci1xxxx_gpio *priv, int irq)
{
	struct gpio_chip *gchip = &priv->gpio;

	gchip->label = dev_name(&priv->aux_dev->dev);
	gchip->parent = &priv->aux_dev->dev;
	gchip->owner = THIS_MODULE;
	gchip->set_config = pci1xxxx_gpio_set_config;
	gchip->dbg_show = NULL;
	gchip->base = -1;
	gchip->ngpio =  PCI1XXXX_NR_PINS;
	gchip->can_sleep = false;

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

const struct auxiliary_device_id pci1xxxx_gpio_auxiliary_id_table[] = {
	{.name = "mchp_pci1xxxx_gp.gp_gpio"},
	{}
};

static struct auxiliary_driver pci1xxxx_gpio_driver = {
	.driver = {
		.name = "PCI1xxxxGPIO",
		},
	.probe = pci1xxxx_gpio_probe,
	.id_table = pci1xxxx_gpio_auxiliary_id_table
};

static int __init pci1xxxx_gpio_driver_init(void)
{
	return auxiliary_driver_register(&pci1xxxx_gpio_driver);
}

static void __exit pci1xxxx_gpio_driver_exit(void)
{
	auxiliary_driver_unregister(&pci1xxxx_gpio_driver);
}

module_init(pci1xxxx_gpio_driver_init);
module_exit(pci1xxxx_gpio_driver_exit);

MODULE_DESCRIPTION("Microchip Technology Inc. PCI1xxxx GPIO controller");
MODULE_AUTHOR("Kumaravel Thiagarajan <kumaravel.thiagarajan@microchip.com>");
MODULE_LICENSE("GPL");
