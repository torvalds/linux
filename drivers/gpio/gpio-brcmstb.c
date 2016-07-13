/*
 * Copyright (C) 2015 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/interrupt.h>
#include <linux/reboot.h>

#define GIO_BANK_SIZE           0x20
#define GIO_ODEN(bank)          (((bank) * GIO_BANK_SIZE) + 0x00)
#define GIO_DATA(bank)          (((bank) * GIO_BANK_SIZE) + 0x04)
#define GIO_IODIR(bank)         (((bank) * GIO_BANK_SIZE) + 0x08)
#define GIO_EC(bank)            (((bank) * GIO_BANK_SIZE) + 0x0c)
#define GIO_EI(bank)            (((bank) * GIO_BANK_SIZE) + 0x10)
#define GIO_MASK(bank)          (((bank) * GIO_BANK_SIZE) + 0x14)
#define GIO_LEVEL(bank)         (((bank) * GIO_BANK_SIZE) + 0x18)
#define GIO_STAT(bank)          (((bank) * GIO_BANK_SIZE) + 0x1c)

struct brcmstb_gpio_bank {
	struct list_head node;
	int id;
	struct gpio_chip gc;
	struct brcmstb_gpio_priv *parent_priv;
	u32 width;
	struct irq_chip irq_chip;
};

struct brcmstb_gpio_priv {
	struct list_head bank_list;
	void __iomem *reg_base;
	struct platform_device *pdev;
	int parent_irq;
	int gpio_base;
	bool can_wake;
	int parent_wake_irq;
	struct notifier_block reboot_notifier;
};

#define MAX_GPIO_PER_BANK           32
#define GPIO_BANK(gpio)         ((gpio) >> 5)
/* assumes MAX_GPIO_PER_BANK is a multiple of 2 */
#define GPIO_BIT(gpio)          ((gpio) & (MAX_GPIO_PER_BANK - 1))

static inline struct brcmstb_gpio_priv *
brcmstb_gpio_gc_to_priv(struct gpio_chip *gc)
{
	struct brcmstb_gpio_bank *bank = gpiochip_get_data(gc);
	return bank->parent_priv;
}

static void brcmstb_gpio_set_imask(struct brcmstb_gpio_bank *bank,
		unsigned int offset, bool enable)
{
	struct gpio_chip *gc = &bank->gc;
	struct brcmstb_gpio_priv *priv = bank->parent_priv;
	u32 mask = gc->pin2mask(gc, offset);
	u32 imask;
	unsigned long flags;

	spin_lock_irqsave(&gc->bgpio_lock, flags);
	imask = gc->read_reg(priv->reg_base + GIO_MASK(bank->id));
	if (enable)
		imask |= mask;
	else
		imask &= ~mask;
	gc->write_reg(priv->reg_base + GIO_MASK(bank->id), imask);
	spin_unlock_irqrestore(&gc->bgpio_lock, flags);
}

/* -------------------- IRQ chip functions -------------------- */

static void brcmstb_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct brcmstb_gpio_bank *bank = gpiochip_get_data(gc);

	brcmstb_gpio_set_imask(bank, d->hwirq, false);
}

static void brcmstb_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct brcmstb_gpio_bank *bank = gpiochip_get_data(gc);

	brcmstb_gpio_set_imask(bank, d->hwirq, true);
}

static int brcmstb_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct brcmstb_gpio_bank *bank = gpiochip_get_data(gc);
	struct brcmstb_gpio_priv *priv = bank->parent_priv;
	u32 mask = BIT(d->hwirq);
	u32 edge_insensitive, iedge_insensitive;
	u32 edge_config, iedge_config;
	u32 level, ilevel;
	unsigned long flags;

	switch (type) {
	case IRQ_TYPE_LEVEL_LOW:
		level = 0;
		edge_config = 0;
		edge_insensitive = 0;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		level = mask;
		edge_config = 0;
		edge_insensitive = 0;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		level = 0;
		edge_config = 0;
		edge_insensitive = 0;
		break;
	case IRQ_TYPE_EDGE_RISING:
		level = 0;
		edge_config = mask;
		edge_insensitive = 0;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		level = 0;
		edge_config = 0;  /* don't care, but want known value */
		edge_insensitive = mask;
		break;
	default:
		return -EINVAL;
	}

	spin_lock_irqsave(&bank->gc.bgpio_lock, flags);

	iedge_config = bank->gc.read_reg(priv->reg_base +
			GIO_EC(bank->id)) & ~mask;
	iedge_insensitive = bank->gc.read_reg(priv->reg_base +
			GIO_EI(bank->id)) & ~mask;
	ilevel = bank->gc.read_reg(priv->reg_base +
			GIO_LEVEL(bank->id)) & ~mask;

	bank->gc.write_reg(priv->reg_base + GIO_EC(bank->id),
			iedge_config | edge_config);
	bank->gc.write_reg(priv->reg_base + GIO_EI(bank->id),
			iedge_insensitive | edge_insensitive);
	bank->gc.write_reg(priv->reg_base + GIO_LEVEL(bank->id),
			ilevel | level);

	spin_unlock_irqrestore(&bank->gc.bgpio_lock, flags);
	return 0;
}

static int brcmstb_gpio_priv_set_wake(struct brcmstb_gpio_priv *priv,
		unsigned int enable)
{
	int ret = 0;

	/*
	 * Only enable wake IRQ once for however many hwirqs can wake
	 * since they all use the same wake IRQ.  Mask will be set
	 * up appropriately thanks to IRQCHIP_MASK_ON_SUSPEND flag.
	 */
	if (enable)
		ret = enable_irq_wake(priv->parent_wake_irq);
	else
		ret = disable_irq_wake(priv->parent_wake_irq);
	if (ret)
		dev_err(&priv->pdev->dev, "failed to %s wake-up interrupt\n",
				enable ? "enable" : "disable");
	return ret;
}

static int brcmstb_gpio_irq_set_wake(struct irq_data *d, unsigned int enable)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct brcmstb_gpio_priv *priv = brcmstb_gpio_gc_to_priv(gc);

	return brcmstb_gpio_priv_set_wake(priv, enable);
}

static irqreturn_t brcmstb_gpio_wake_irq_handler(int irq, void *data)
{
	struct brcmstb_gpio_priv *priv = data;

	if (!priv || irq != priv->parent_wake_irq)
		return IRQ_NONE;
	pm_wakeup_event(&priv->pdev->dev, 0);
	return IRQ_HANDLED;
}

static void brcmstb_gpio_irq_bank_handler(struct brcmstb_gpio_bank *bank)
{
	struct brcmstb_gpio_priv *priv = bank->parent_priv;
	struct irq_domain *irq_domain = bank->gc.irqdomain;
	void __iomem *reg_base = priv->reg_base;
	unsigned long status;
	unsigned long flags;

	spin_lock_irqsave(&bank->gc.bgpio_lock, flags);
	while ((status = bank->gc.read_reg(reg_base + GIO_STAT(bank->id)) &
			 bank->gc.read_reg(reg_base + GIO_MASK(bank->id)))) {
		int bit;

		for_each_set_bit(bit, &status, 32) {
			u32 stat = bank->gc.read_reg(reg_base +
						      GIO_STAT(bank->id));
			if (bit >= bank->width)
				dev_warn(&priv->pdev->dev,
					 "IRQ for invalid GPIO (bank=%d, offset=%d)\n",
					 bank->id, bit);
			bank->gc.write_reg(reg_base + GIO_STAT(bank->id),
					    stat | BIT(bit));
			generic_handle_irq(irq_find_mapping(irq_domain, bit));
		}
	}
	spin_unlock_irqrestore(&bank->gc.bgpio_lock, flags);
}

/* Each UPG GIO block has one IRQ for all banks */
static void brcmstb_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct brcmstb_gpio_priv *priv = brcmstb_gpio_gc_to_priv(gc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct brcmstb_gpio_bank *bank;

	/* Interrupts weren't properly cleared during probe */
	BUG_ON(!priv || !chip);

	chained_irq_enter(chip, desc);
	list_for_each_entry(bank, &priv->bank_list, node)
		brcmstb_gpio_irq_bank_handler(bank);
	chained_irq_exit(chip, desc);
}

static int brcmstb_gpio_reboot(struct notifier_block *nb,
		unsigned long action, void *data)
{
	struct brcmstb_gpio_priv *priv =
		container_of(nb, struct brcmstb_gpio_priv, reboot_notifier);

	/* Enable GPIO for S5 cold boot */
	if (action == SYS_POWER_OFF)
		brcmstb_gpio_priv_set_wake(priv, 1);

	return NOTIFY_DONE;
}

/* Make sure that the number of banks matches up between properties */
static int brcmstb_gpio_sanity_check_banks(struct device *dev,
		struct device_node *np, struct resource *res)
{
	int res_num_banks = resource_size(res) / GIO_BANK_SIZE;
	int num_banks =
		of_property_count_u32_elems(np, "brcm,gpio-bank-widths");

	if (res_num_banks != num_banks) {
		dev_err(dev, "Mismatch in banks: res had %d, bank-widths had %d\n",
				res_num_banks, num_banks);
		return -EINVAL;
	} else {
		return 0;
	}
}

static int brcmstb_gpio_remove(struct platform_device *pdev)
{
	struct brcmstb_gpio_priv *priv = platform_get_drvdata(pdev);
	struct brcmstb_gpio_bank *bank;
	int ret = 0;

	if (!priv) {
		dev_err(&pdev->dev, "called %s without drvdata!\n", __func__);
		return -EFAULT;
	}

	/*
	 * You can lose return values below, but we report all errors, and it's
	 * more important to actually perform all of the steps.
	 */
	list_for_each_entry(bank, &priv->bank_list, node)
		gpiochip_remove(&bank->gc);

	if (priv->reboot_notifier.notifier_call) {
		ret = unregister_reboot_notifier(&priv->reboot_notifier);
		if (ret)
			dev_err(&pdev->dev,
				"failed to unregister reboot notifier\n");
	}
	return ret;
}

static int brcmstb_gpio_of_xlate(struct gpio_chip *gc,
		const struct of_phandle_args *gpiospec, u32 *flags)
{
	struct brcmstb_gpio_priv *priv = brcmstb_gpio_gc_to_priv(gc);
	struct brcmstb_gpio_bank *bank = gpiochip_get_data(gc);
	int offset;

	if (gc->of_gpio_n_cells != 2) {
		WARN_ON(1);
		return -EINVAL;
	}

	if (WARN_ON(gpiospec->args_count < gc->of_gpio_n_cells))
		return -EINVAL;

	offset = gpiospec->args[0] - (gc->base - priv->gpio_base);
	if (offset >= gc->ngpio || offset < 0)
		return -EINVAL;

	if (unlikely(offset >= bank->width)) {
		dev_warn_ratelimited(&priv->pdev->dev,
			"Received request for invalid GPIO offset %d\n",
			gpiospec->args[0]);
	}

	if (flags)
		*flags = gpiospec->args[1];

	return offset;
}

/* Before calling, must have bank->parent_irq set and gpiochip registered */
static int brcmstb_gpio_irq_setup(struct platform_device *pdev,
		struct brcmstb_gpio_bank *bank)
{
	struct brcmstb_gpio_priv *priv = bank->parent_priv;
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;

	bank->irq_chip.name = dev_name(dev);
	bank->irq_chip.irq_mask = brcmstb_gpio_irq_mask;
	bank->irq_chip.irq_unmask = brcmstb_gpio_irq_unmask;
	bank->irq_chip.irq_set_type = brcmstb_gpio_irq_set_type;

	/* Ensures that all non-wakeup IRQs are disabled at suspend */
	bank->irq_chip.flags = IRQCHIP_MASK_ON_SUSPEND;

	if (IS_ENABLED(CONFIG_PM_SLEEP) && !priv->can_wake &&
			of_property_read_bool(np, "wakeup-source")) {
		priv->parent_wake_irq = platform_get_irq(pdev, 1);
		if (priv->parent_wake_irq < 0) {
			dev_warn(dev,
				"Couldn't get wake IRQ - GPIOs will not be able to wake from sleep");
		} else {
			int err;

			/*
			 * Set wakeup capability before requesting wakeup
			 * interrupt, so we can process boot-time "wakeups"
			 * (e.g., from S5 cold boot)
			 */
			device_set_wakeup_capable(dev, true);
			device_wakeup_enable(dev);
			err = devm_request_irq(dev, priv->parent_wake_irq,
					brcmstb_gpio_wake_irq_handler, 0,
					"brcmstb-gpio-wake", priv);

			if (err < 0) {
				dev_err(dev, "Couldn't request wake IRQ");
				return err;
			}

			priv->reboot_notifier.notifier_call =
				brcmstb_gpio_reboot;
			register_reboot_notifier(&priv->reboot_notifier);
			priv->can_wake = true;
		}
	}

	if (priv->can_wake)
		bank->irq_chip.irq_set_wake = brcmstb_gpio_irq_set_wake;

	gpiochip_irqchip_add(&bank->gc, &bank->irq_chip, 0,
			handle_simple_irq, IRQ_TYPE_NONE);
	gpiochip_set_chained_irqchip(&bank->gc, &bank->irq_chip,
			priv->parent_irq, brcmstb_gpio_irq_handler);

	return 0;
}

static int brcmstb_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void __iomem *reg_base;
	struct brcmstb_gpio_priv *priv;
	struct resource *res;
	struct property *prop;
	const __be32 *p;
	u32 bank_width;
	int num_banks = 0;
	int err;
	static int gpio_base;
	unsigned long flags = 0;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);
	INIT_LIST_HEAD(&priv->bank_list);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(reg_base))
		return PTR_ERR(reg_base);

	priv->gpio_base = gpio_base;
	priv->reg_base = reg_base;
	priv->pdev = pdev;

	if (of_property_read_bool(np, "interrupt-controller")) {
		priv->parent_irq = platform_get_irq(pdev, 0);
		if (priv->parent_irq <= 0) {
			dev_err(dev, "Couldn't get IRQ");
			return -ENOENT;
		}
	} else {
		priv->parent_irq = -ENOENT;
	}

	if (brcmstb_gpio_sanity_check_banks(dev, np, res))
		return -EINVAL;

	/*
	 * MIPS endianness is configured by boot strap, which also reverses all
	 * bus endianness (i.e., big-endian CPU + big endian bus ==> native
	 * endian I/O).
	 *
	 * Other architectures (e.g., ARM) either do not support big endian, or
	 * else leave I/O in little endian mode.
	 */
#if defined(CONFIG_MIPS) && defined(__BIG_ENDIAN)
	flags = BGPIOF_BIG_ENDIAN_BYTE_ORDER;
#endif

	of_property_for_each_u32(np, "brcm,gpio-bank-widths", prop, p,
			bank_width) {
		struct brcmstb_gpio_bank *bank;
		struct gpio_chip *gc;

		bank = devm_kzalloc(dev, sizeof(*bank), GFP_KERNEL);
		if (!bank) {
			err = -ENOMEM;
			goto fail;
		}

		bank->parent_priv = priv;
		bank->id = num_banks;
		if (bank_width <= 0 || bank_width > MAX_GPIO_PER_BANK) {
			dev_err(dev, "Invalid bank width %d\n", bank_width);
			err = -EINVAL;
			goto fail;
		} else {
			bank->width = bank_width;
		}

		/*
		 * Regs are 4 bytes wide, have data reg, no set/clear regs,
		 * and direction bits have 0 = output and 1 = input
		 */
		gc = &bank->gc;
		err = bgpio_init(gc, dev, 4,
				reg_base + GIO_DATA(bank->id),
				NULL, NULL, NULL,
				reg_base + GIO_IODIR(bank->id), flags);
		if (err) {
			dev_err(dev, "bgpio_init() failed\n");
			goto fail;
		}

		gc->of_node = np;
		gc->owner = THIS_MODULE;
		gc->label = np->full_name;
		gc->base = gpio_base;
		gc->of_gpio_n_cells = 2;
		gc->of_xlate = brcmstb_gpio_of_xlate;
		/* not all ngpio lines are valid, will use bank width later */
		gc->ngpio = MAX_GPIO_PER_BANK;

		/*
		 * Mask all interrupts by default, since wakeup interrupts may
		 * be retained from S5 cold boot
		 */
		gc->write_reg(reg_base + GIO_MASK(bank->id), 0);

		err = gpiochip_add_data(gc, bank);
		if (err) {
			dev_err(dev, "Could not add gpiochip for bank %d\n",
					bank->id);
			goto fail;
		}
		gpio_base += gc->ngpio;

		if (priv->parent_irq > 0) {
			err = brcmstb_gpio_irq_setup(pdev, bank);
			if (err)
				goto fail;
		}

		dev_dbg(dev, "bank=%d, base=%d, ngpio=%d, width=%d\n", bank->id,
			gc->base, gc->ngpio, bank->width);

		/* Everything looks good, so add bank to list */
		list_add(&bank->node, &priv->bank_list);

		num_banks++;
	}

	dev_info(dev, "Registered %d banks (GPIO(s): %d-%d)\n",
			num_banks, priv->gpio_base, gpio_base - 1);

	return 0;

fail:
	(void) brcmstb_gpio_remove(pdev);
	return err;
}

static const struct of_device_id brcmstb_gpio_of_match[] = {
	{ .compatible = "brcm,brcmstb-gpio" },
	{},
};

MODULE_DEVICE_TABLE(of, brcmstb_gpio_of_match);

static struct platform_driver brcmstb_gpio_driver = {
	.driver = {
		.name = "brcmstb-gpio",
		.of_match_table = brcmstb_gpio_of_match,
	},
	.probe = brcmstb_gpio_probe,
	.remove = brcmstb_gpio_remove,
};
module_platform_driver(brcmstb_gpio_driver);

MODULE_AUTHOR("Gregory Fong");
MODULE_DESCRIPTION("Driver for Broadcom BRCMSTB SoC UPG GPIO");
MODULE_LICENSE("GPL v2");
