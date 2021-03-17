/*
 * Copyright (C) 2015-2017 Broadcom
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

enum gio_reg_index {
	GIO_REG_ODEN = 0,
	GIO_REG_DATA,
	GIO_REG_IODIR,
	GIO_REG_EC,
	GIO_REG_EI,
	GIO_REG_MASK,
	GIO_REG_LEVEL,
	GIO_REG_STAT,
	NUMBER_OF_GIO_REGISTERS
};

#define GIO_BANK_SIZE           (NUMBER_OF_GIO_REGISTERS * sizeof(u32))
#define GIO_BANK_OFF(bank, off)	(((bank) * GIO_BANK_SIZE) + (off * sizeof(u32)))
#define GIO_ODEN(bank)          GIO_BANK_OFF(bank, GIO_REG_ODEN)
#define GIO_DATA(bank)          GIO_BANK_OFF(bank, GIO_REG_DATA)
#define GIO_IODIR(bank)         GIO_BANK_OFF(bank, GIO_REG_IODIR)
#define GIO_EC(bank)            GIO_BANK_OFF(bank, GIO_REG_EC)
#define GIO_EI(bank)            GIO_BANK_OFF(bank, GIO_REG_EI)
#define GIO_MASK(bank)          GIO_BANK_OFF(bank, GIO_REG_MASK)
#define GIO_LEVEL(bank)         GIO_BANK_OFF(bank, GIO_REG_LEVEL)
#define GIO_STAT(bank)          GIO_BANK_OFF(bank, GIO_REG_STAT)

struct brcmstb_gpio_bank {
	struct list_head node;
	int id;
	struct gpio_chip gc;
	struct brcmstb_gpio_priv *parent_priv;
	u32 width;
	u32 wake_active;
	u32 saved_regs[GIO_REG_STAT]; /* Don't save and restore GIO_REG_STAT */
};

struct brcmstb_gpio_priv {
	struct list_head bank_list;
	void __iomem *reg_base;
	struct platform_device *pdev;
	struct irq_domain *irq_domain;
	struct irq_chip irq_chip;
	int parent_irq;
	int gpio_base;
	int num_gpios;
	int parent_wake_irq;
};

#define MAX_GPIO_PER_BANK       32
#define GPIO_BANK(gpio)         ((gpio) >> 5)
/* assumes MAX_GPIO_PER_BANK is a multiple of 2 */
#define GPIO_BIT(gpio)          ((gpio) & (MAX_GPIO_PER_BANK - 1))

static inline struct brcmstb_gpio_priv *
brcmstb_gpio_gc_to_priv(struct gpio_chip *gc)
{
	struct brcmstb_gpio_bank *bank = gpiochip_get_data(gc);
	return bank->parent_priv;
}

static unsigned long
__brcmstb_gpio_get_active_irqs(struct brcmstb_gpio_bank *bank)
{
	void __iomem *reg_base = bank->parent_priv->reg_base;

	return bank->gc.read_reg(reg_base + GIO_STAT(bank->id)) &
	       bank->gc.read_reg(reg_base + GIO_MASK(bank->id));
}

static unsigned long
brcmstb_gpio_get_active_irqs(struct brcmstb_gpio_bank *bank)
{
	unsigned long status;
	unsigned long flags;

	spin_lock_irqsave(&bank->gc.bgpio_lock, flags);
	status = __brcmstb_gpio_get_active_irqs(bank);
	spin_unlock_irqrestore(&bank->gc.bgpio_lock, flags);

	return status;
}

static int brcmstb_gpio_hwirq_to_offset(irq_hw_number_t hwirq,
					struct brcmstb_gpio_bank *bank)
{
	return hwirq - (bank->gc.base - bank->parent_priv->gpio_base);
}

static void brcmstb_gpio_set_imask(struct brcmstb_gpio_bank *bank,
		unsigned int hwirq, bool enable)
{
	struct gpio_chip *gc = &bank->gc;
	struct brcmstb_gpio_priv *priv = bank->parent_priv;
	u32 mask = BIT(brcmstb_gpio_hwirq_to_offset(hwirq, bank));
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

static int brcmstb_gpio_to_irq(struct gpio_chip *gc, unsigned offset)
{
	struct brcmstb_gpio_priv *priv = brcmstb_gpio_gc_to_priv(gc);
	/* gc_offset is relative to this gpio_chip; want real offset */
	int hwirq = offset + (gc->base - priv->gpio_base);

	if (hwirq >= priv->num_gpios)
		return -ENXIO;
	return irq_create_mapping(priv->irq_domain, hwirq);
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

static void brcmstb_gpio_irq_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct brcmstb_gpio_bank *bank = gpiochip_get_data(gc);
	struct brcmstb_gpio_priv *priv = bank->parent_priv;
	u32 mask = BIT(brcmstb_gpio_hwirq_to_offset(d->hwirq, bank));

	gc->write_reg(priv->reg_base + GIO_STAT(bank->id), mask);
}

static int brcmstb_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct brcmstb_gpio_bank *bank = gpiochip_get_data(gc);
	struct brcmstb_gpio_priv *priv = bank->parent_priv;
	u32 mask = BIT(brcmstb_gpio_hwirq_to_offset(d->hwirq, bank));
	u32 edge_insensitive, iedge_insensitive;
	u32 edge_config, iedge_config;
	u32 level, ilevel;
	unsigned long flags;

	switch (type) {
	case IRQ_TYPE_LEVEL_LOW:
		level = mask;
		edge_config = 0;
		edge_insensitive = 0;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		level = mask;
		edge_config = mask;
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
	struct brcmstb_gpio_bank *bank = gpiochip_get_data(gc);
	struct brcmstb_gpio_priv *priv = bank->parent_priv;
	u32 mask = BIT(brcmstb_gpio_hwirq_to_offset(d->hwirq, bank));

	/*
	 * Do not do anything specific for now, suspend/resume callbacks will
	 * configure the interrupt mask appropriately
	 */
	if (enable)
		bank->wake_active |= mask;
	else
		bank->wake_active &= ~mask;

	return brcmstb_gpio_priv_set_wake(priv, enable);
}

static irqreturn_t brcmstb_gpio_wake_irq_handler(int irq, void *data)
{
	struct brcmstb_gpio_priv *priv = data;

	if (!priv || irq != priv->parent_wake_irq)
		return IRQ_NONE;

	/* Nothing to do */
	return IRQ_HANDLED;
}

static void brcmstb_gpio_irq_bank_handler(struct brcmstb_gpio_bank *bank)
{
	struct brcmstb_gpio_priv *priv = bank->parent_priv;
	struct irq_domain *domain = priv->irq_domain;
	int hwbase = bank->gc.base - priv->gpio_base;
	unsigned long status;

	while ((status = brcmstb_gpio_get_active_irqs(bank))) {
		unsigned int irq, offset;

		for_each_set_bit(offset, &status, 32) {
			if (offset >= bank->width)
				dev_warn(&priv->pdev->dev,
					 "IRQ for invalid GPIO (bank=%d, offset=%d)\n",
					 bank->id, offset);
			irq = irq_linear_revmap(domain, hwbase + offset);
			generic_handle_irq(irq);
		}
	}
}

/* Each UPG GIO block has one IRQ for all banks */
static void brcmstb_gpio_irq_handler(struct irq_desc *desc)
{
	struct brcmstb_gpio_priv *priv = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct brcmstb_gpio_bank *bank;

	/* Interrupts weren't properly cleared during probe */
	BUG_ON(!priv || !chip);

	chained_irq_enter(chip, desc);
	list_for_each_entry(bank, &priv->bank_list, node)
		brcmstb_gpio_irq_bank_handler(bank);
	chained_irq_exit(chip, desc);
}

static struct brcmstb_gpio_bank *brcmstb_gpio_hwirq_to_bank(
		struct brcmstb_gpio_priv *priv, irq_hw_number_t hwirq)
{
	struct brcmstb_gpio_bank *bank;
	int i = 0;

	/* banks are in descending order */
	list_for_each_entry_reverse(bank, &priv->bank_list, node) {
		i += bank->gc.ngpio;
		if (hwirq < i)
			return bank;
	}
	return NULL;
}

/*
 * This lock class tells lockdep that GPIO irqs are in a different
 * category than their parents, so it won't report false recursion.
 */
static struct lock_class_key brcmstb_gpio_irq_lock_class;
static struct lock_class_key brcmstb_gpio_irq_request_class;


static int brcmstb_gpio_irq_map(struct irq_domain *d, unsigned int irq,
		irq_hw_number_t hwirq)
{
	struct brcmstb_gpio_priv *priv = d->host_data;
	struct brcmstb_gpio_bank *bank =
		brcmstb_gpio_hwirq_to_bank(priv, hwirq);
	struct platform_device *pdev = priv->pdev;
	int ret;

	if (!bank)
		return -EINVAL;

	dev_dbg(&pdev->dev, "Mapping irq %d for gpio line %d (bank %d)\n",
		irq, (int)hwirq, bank->id);
	ret = irq_set_chip_data(irq, &bank->gc);
	if (ret < 0)
		return ret;
	irq_set_lockdep_class(irq, &brcmstb_gpio_irq_lock_class,
			      &brcmstb_gpio_irq_request_class);
	irq_set_chip_and_handler(irq, &priv->irq_chip, handle_level_irq);
	irq_set_noprobe(irq);
	return 0;
}

static void brcmstb_gpio_irq_unmap(struct irq_domain *d, unsigned int irq)
{
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops brcmstb_gpio_irq_domain_ops = {
	.map = brcmstb_gpio_irq_map,
	.unmap = brcmstb_gpio_irq_unmap,
	.xlate = irq_domain_xlate_twocell,
};

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
	int offset, ret = 0, virq;

	if (!priv) {
		dev_err(&pdev->dev, "called %s without drvdata!\n", __func__);
		return -EFAULT;
	}

	if (priv->parent_irq > 0)
		irq_set_chained_handler_and_data(priv->parent_irq, NULL, NULL);

	/* Remove all IRQ mappings and delete the domain */
	if (priv->irq_domain) {
		for (offset = 0; offset < priv->num_gpios; offset++) {
			virq = irq_find_mapping(priv->irq_domain, offset);
			irq_dispose_mapping(virq);
		}
		irq_domain_remove(priv->irq_domain);
	}

	/*
	 * You can lose return values below, but we report all errors, and it's
	 * more important to actually perform all of the steps.
	 */
	list_for_each_entry(bank, &priv->bank_list, node)
		gpiochip_remove(&bank->gc);

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

/* priv->parent_irq and priv->num_gpios must be set before calling */
static int brcmstb_gpio_irq_setup(struct platform_device *pdev,
		struct brcmstb_gpio_priv *priv)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	int err;

	priv->irq_domain =
		irq_domain_add_linear(np, priv->num_gpios,
				      &brcmstb_gpio_irq_domain_ops,
				      priv);
	if (!priv->irq_domain) {
		dev_err(dev, "Couldn't allocate IRQ domain\n");
		return -ENXIO;
	}

	if (of_property_read_bool(np, "wakeup-source")) {
		priv->parent_wake_irq = platform_get_irq(pdev, 1);
		if (priv->parent_wake_irq < 0) {
			priv->parent_wake_irq = 0;
			dev_warn(dev,
				"Couldn't get wake IRQ - GPIOs will not be able to wake from sleep");
		} else {
			/*
			 * Set wakeup capability so we can process boot-time
			 * "wakeups" (e.g., from S5 cold boot)
			 */
			device_set_wakeup_capable(dev, true);
			device_wakeup_enable(dev);
			err = devm_request_irq(dev, priv->parent_wake_irq,
					       brcmstb_gpio_wake_irq_handler,
					       IRQF_SHARED,
					       "brcmstb-gpio-wake", priv);

			if (err < 0) {
				dev_err(dev, "Couldn't request wake IRQ");
				goto out_free_domain;
			}
		}
	}

	priv->irq_chip.name = dev_name(dev);
	priv->irq_chip.irq_disable = brcmstb_gpio_irq_mask;
	priv->irq_chip.irq_mask = brcmstb_gpio_irq_mask;
	priv->irq_chip.irq_unmask = brcmstb_gpio_irq_unmask;
	priv->irq_chip.irq_ack = brcmstb_gpio_irq_ack;
	priv->irq_chip.irq_set_type = brcmstb_gpio_irq_set_type;

	if (priv->parent_wake_irq)
		priv->irq_chip.irq_set_wake = brcmstb_gpio_irq_set_wake;

	irq_set_chained_handler_and_data(priv->parent_irq,
					 brcmstb_gpio_irq_handler, priv);
	irq_set_status_flags(priv->parent_irq, IRQ_DISABLE_UNLAZY);

	return 0;

out_free_domain:
	irq_domain_remove(priv->irq_domain);

	return err;
}

static void brcmstb_gpio_bank_save(struct brcmstb_gpio_priv *priv,
				   struct brcmstb_gpio_bank *bank)
{
	struct gpio_chip *gc = &bank->gc;
	unsigned int i;

	for (i = 0; i < GIO_REG_STAT; i++)
		bank->saved_regs[i] = gc->read_reg(priv->reg_base +
						   GIO_BANK_OFF(bank->id, i));
}

static void brcmstb_gpio_quiesce(struct device *dev, bool save)
{
	struct brcmstb_gpio_priv *priv = dev_get_drvdata(dev);
	struct brcmstb_gpio_bank *bank;
	struct gpio_chip *gc;
	u32 imask;

	/* disable non-wake interrupt */
	if (priv->parent_irq >= 0)
		disable_irq(priv->parent_irq);

	list_for_each_entry(bank, &priv->bank_list, node) {
		gc = &bank->gc;

		if (save)
			brcmstb_gpio_bank_save(priv, bank);

		/* Unmask GPIOs which have been flagged as wake-up sources */
		if (priv->parent_wake_irq)
			imask = bank->wake_active;
		else
			imask = 0;
		gc->write_reg(priv->reg_base + GIO_MASK(bank->id),
			       imask);
	}
}

static void brcmstb_gpio_shutdown(struct platform_device *pdev)
{
	/* Enable GPIO for S5 cold boot */
	brcmstb_gpio_quiesce(&pdev->dev, false);
}

#ifdef CONFIG_PM_SLEEP
static void brcmstb_gpio_bank_restore(struct brcmstb_gpio_priv *priv,
				      struct brcmstb_gpio_bank *bank)
{
	struct gpio_chip *gc = &bank->gc;
	unsigned int i;

	for (i = 0; i < GIO_REG_STAT; i++)
		gc->write_reg(priv->reg_base + GIO_BANK_OFF(bank->id, i),
			      bank->saved_regs[i]);
}

static int brcmstb_gpio_suspend(struct device *dev)
{
	brcmstb_gpio_quiesce(dev, true);
	return 0;
}

static int brcmstb_gpio_resume(struct device *dev)
{
	struct brcmstb_gpio_priv *priv = dev_get_drvdata(dev);
	struct brcmstb_gpio_bank *bank;
	bool need_wakeup_event = false;

	list_for_each_entry(bank, &priv->bank_list, node) {
		need_wakeup_event |= !!__brcmstb_gpio_get_active_irqs(bank);
		brcmstb_gpio_bank_restore(priv, bank);
	}

	if (priv->parent_wake_irq && need_wakeup_event)
		pm_wakeup_event(dev, 0);

	/* enable non-wake interrupt */
	if (priv->parent_irq >= 0)
		enable_irq(priv->parent_irq);

	return 0;
}

#else
#define brcmstb_gpio_suspend	NULL
#define brcmstb_gpio_resume	NULL
#endif /* CONFIG_PM_SLEEP */

static const struct dev_pm_ops brcmstb_gpio_pm_ops = {
	.suspend_noirq	= brcmstb_gpio_suspend,
	.resume_noirq = brcmstb_gpio_resume,
};

static void brcmstb_gpio_set_names(struct device *dev,
				   struct brcmstb_gpio_bank *bank)
{
	struct device_node *np = dev->of_node;
	const char **names;
	int nstrings, base;
	unsigned int i;

	base = bank->id * MAX_GPIO_PER_BANK;

	nstrings = of_property_count_strings(np, "gpio-line-names");
	if (nstrings <= base)
		/* Line names not present */
		return;

	names = devm_kcalloc(dev, MAX_GPIO_PER_BANK, sizeof(*names),
			     GFP_KERNEL);
	if (!names)
		return;

	/*
	 * Make sure to not index beyond the end of the number of descriptors
	 * of the GPIO device.
	 */
	for (i = 0; i < bank->width; i++) {
		const char *name;
		int ret;

		ret = of_property_read_string_index(np, "gpio-line-names",
						    base + i, &name);
		if (ret) {
			if (ret != -ENODATA)
				dev_err(dev, "unable to name line %d: %d\n",
					base + i, ret);
			break;
		}
		if (*name)
			names[i] = name;
	}

	bank->gc.names = names;
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
	bool need_wakeup_event = false;

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
		if (priv->parent_irq <= 0)
			return -ENOENT;
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

		/*
		 * If bank_width is 0, then there is an empty bank in the
		 * register block. Special handling for this case.
		 */
		if (bank_width == 0) {
			dev_dbg(dev, "Width 0 found: Empty bank @ %d\n",
				num_banks);
			num_banks++;
			gpio_base += MAX_GPIO_PER_BANK;
			continue;
		}

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
		gc->label = devm_kasprintf(dev, GFP_KERNEL, "%pOF", dev->of_node);
		if (!gc->label) {
			err = -ENOMEM;
			goto fail;
		}
		gc->base = gpio_base;
		gc->of_gpio_n_cells = 2;
		gc->of_xlate = brcmstb_gpio_of_xlate;
		/* not all ngpio lines are valid, will use bank width later */
		gc->ngpio = MAX_GPIO_PER_BANK;
		if (priv->parent_irq > 0)
			gc->to_irq = brcmstb_gpio_to_irq;

		/*
		 * Mask all interrupts by default, since wakeup interrupts may
		 * be retained from S5 cold boot
		 */
		need_wakeup_event |= !!__brcmstb_gpio_get_active_irqs(bank);
		gc->write_reg(reg_base + GIO_MASK(bank->id), 0);

		brcmstb_gpio_set_names(dev, bank);
		err = gpiochip_add_data(gc, bank);
		if (err) {
			dev_err(dev, "Could not add gpiochip for bank %d\n",
					bank->id);
			goto fail;
		}
		gpio_base += gc->ngpio;

		dev_dbg(dev, "bank=%d, base=%d, ngpio=%d, width=%d\n", bank->id,
			gc->base, gc->ngpio, bank->width);

		/* Everything looks good, so add bank to list */
		list_add(&bank->node, &priv->bank_list);

		num_banks++;
	}

	priv->num_gpios = gpio_base - priv->gpio_base;
	if (priv->parent_irq > 0) {
		err = brcmstb_gpio_irq_setup(pdev, priv);
		if (err)
			goto fail;
	}

	if (priv->parent_wake_irq && need_wakeup_event)
		pm_wakeup_event(dev, 0);

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
		.pm = &brcmstb_gpio_pm_ops,
	},
	.probe = brcmstb_gpio_probe,
	.remove = brcmstb_gpio_remove,
	.shutdown = brcmstb_gpio_shutdown,
};
module_platform_driver(brcmstb_gpio_driver);

MODULE_AUTHOR("Gregory Fong");
MODULE_DESCRIPTION("Driver for Broadcom BRCMSTB SoC UPG GPIO");
MODULE_LICENSE("GPL v2");
