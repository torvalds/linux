/*
 * Broadcom Kona GPIO Driver
 *
 * Author: Broadcom Corporation <bcm-kernel-feedback-list@broadcom.com>
 * Copyright (C) 2012-2014 Broadcom Corporation
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
#include <linux/err.h>
#include <linux/io.h>
#include <linux/gpio/driver.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/init.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>

#define BCM_GPIO_PASSWD				0x00a5a501
#define GPIO_PER_BANK				32
#define GPIO_MAX_BANK_NUM			8

#define GPIO_BANK(gpio)				((gpio) >> 5)
#define GPIO_BIT(gpio)				((gpio) & (GPIO_PER_BANK - 1))

/* There is a GPIO control register for each GPIO */
#define GPIO_CONTROL(gpio)			(0x00000100 + ((gpio) << 2))

/* The remaining registers are per GPIO bank */
#define GPIO_OUT_STATUS(bank)			(0x00000000 + ((bank) << 2))
#define GPIO_IN_STATUS(bank)			(0x00000020 + ((bank) << 2))
#define GPIO_OUT_SET(bank)			(0x00000040 + ((bank) << 2))
#define GPIO_OUT_CLEAR(bank)			(0x00000060 + ((bank) << 2))
#define GPIO_INT_STATUS(bank)			(0x00000080 + ((bank) << 2))
#define GPIO_INT_MASK(bank)			(0x000000a0 + ((bank) << 2))
#define GPIO_INT_MSKCLR(bank)			(0x000000c0 + ((bank) << 2))
#define GPIO_PWD_STATUS(bank)			(0x00000500 + ((bank) << 2))

#define GPIO_GPPWR_OFFSET			0x00000520

#define GPIO_GPCTR0_DBR_SHIFT			5
#define GPIO_GPCTR0_DBR_MASK			0x000001e0

#define GPIO_GPCTR0_ITR_SHIFT			3
#define GPIO_GPCTR0_ITR_MASK			0x00000018
#define GPIO_GPCTR0_ITR_CMD_RISING_EDGE		0x00000001
#define GPIO_GPCTR0_ITR_CMD_FALLING_EDGE	0x00000002
#define GPIO_GPCTR0_ITR_CMD_BOTH_EDGE		0x00000003

#define GPIO_GPCTR0_IOTR_MASK			0x00000001
#define GPIO_GPCTR0_IOTR_CMD_0UTPUT		0x00000000
#define GPIO_GPCTR0_IOTR_CMD_INPUT		0x00000001

#define GPIO_GPCTR0_DB_ENABLE_MASK		0x00000100

#define LOCK_CODE				0xffffffff
#define UNLOCK_CODE				0x00000000

struct bcm_kona_gpio {
	void __iomem *reg_base;
	int num_bank;
	raw_spinlock_t lock;
	struct gpio_chip gpio_chip;
	struct irq_domain *irq_domain;
	struct bcm_kona_gpio_bank *banks;
	struct platform_device *pdev;
};

struct bcm_kona_gpio_bank {
	int id;
	int irq;
	/* Used in the interrupt handler */
	struct bcm_kona_gpio *kona_gpio;
};

static inline void bcm_kona_gpio_write_lock_regs(void __iomem *reg_base,
						int bank_id, u32 lockcode)
{
	writel(BCM_GPIO_PASSWD, reg_base + GPIO_GPPWR_OFFSET);
	writel(lockcode, reg_base + GPIO_PWD_STATUS(bank_id));
}

static void bcm_kona_gpio_lock_gpio(struct bcm_kona_gpio *kona_gpio,
					unsigned gpio)
{
	u32 val;
	unsigned long flags;
	int bank_id = GPIO_BANK(gpio);

	raw_spin_lock_irqsave(&kona_gpio->lock, flags);

	val = readl(kona_gpio->reg_base + GPIO_PWD_STATUS(bank_id));
	val |= BIT(gpio);
	bcm_kona_gpio_write_lock_regs(kona_gpio->reg_base, bank_id, val);

	raw_spin_unlock_irqrestore(&kona_gpio->lock, flags);
}

static void bcm_kona_gpio_unlock_gpio(struct bcm_kona_gpio *kona_gpio,
					unsigned gpio)
{
	u32 val;
	unsigned long flags;
	int bank_id = GPIO_BANK(gpio);

	raw_spin_lock_irqsave(&kona_gpio->lock, flags);

	val = readl(kona_gpio->reg_base + GPIO_PWD_STATUS(bank_id));
	val &= ~BIT(gpio);
	bcm_kona_gpio_write_lock_regs(kona_gpio->reg_base, bank_id, val);

	raw_spin_unlock_irqrestore(&kona_gpio->lock, flags);
}

static int bcm_kona_gpio_get_dir(struct gpio_chip *chip, unsigned gpio)
{
	struct bcm_kona_gpio *kona_gpio = gpiochip_get_data(chip);
	void __iomem *reg_base = kona_gpio->reg_base;
	u32 val;

	val = readl(reg_base + GPIO_CONTROL(gpio)) & GPIO_GPCTR0_IOTR_MASK;
	return !!val;
}

static void bcm_kona_gpio_set(struct gpio_chip *chip, unsigned gpio, int value)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	int bank_id = GPIO_BANK(gpio);
	int bit = GPIO_BIT(gpio);
	u32 val, reg_offset;
	unsigned long flags;

	kona_gpio = gpiochip_get_data(chip);
	reg_base = kona_gpio->reg_base;
	raw_spin_lock_irqsave(&kona_gpio->lock, flags);

	/* this function only applies to output pin */
	if (bcm_kona_gpio_get_dir(chip, gpio) == 1)
		goto out;

	reg_offset = value ? GPIO_OUT_SET(bank_id) : GPIO_OUT_CLEAR(bank_id);

	val = readl(reg_base + reg_offset);
	val |= BIT(bit);
	writel(val, reg_base + reg_offset);

out:
	raw_spin_unlock_irqrestore(&kona_gpio->lock, flags);
}

static int bcm_kona_gpio_get(struct gpio_chip *chip, unsigned gpio)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	int bank_id = GPIO_BANK(gpio);
	int bit = GPIO_BIT(gpio);
	u32 val, reg_offset;
	unsigned long flags;

	kona_gpio = gpiochip_get_data(chip);
	reg_base = kona_gpio->reg_base;
	raw_spin_lock_irqsave(&kona_gpio->lock, flags);

	if (bcm_kona_gpio_get_dir(chip, gpio) == 1)
		reg_offset = GPIO_IN_STATUS(bank_id);
	else
		reg_offset = GPIO_OUT_STATUS(bank_id);

	/* read the GPIO bank status */
	val = readl(reg_base + reg_offset);

	raw_spin_unlock_irqrestore(&kona_gpio->lock, flags);

	/* return the specified bit status */
	return !!(val & BIT(bit));
}

static int bcm_kona_gpio_request(struct gpio_chip *chip, unsigned gpio)
{
	struct bcm_kona_gpio *kona_gpio = gpiochip_get_data(chip);

	bcm_kona_gpio_unlock_gpio(kona_gpio, gpio);
	return 0;
}

static void bcm_kona_gpio_free(struct gpio_chip *chip, unsigned gpio)
{
	struct bcm_kona_gpio *kona_gpio = gpiochip_get_data(chip);

	bcm_kona_gpio_lock_gpio(kona_gpio, gpio);
}

static int bcm_kona_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	u32 val;
	unsigned long flags;

	kona_gpio = gpiochip_get_data(chip);
	reg_base = kona_gpio->reg_base;
	raw_spin_lock_irqsave(&kona_gpio->lock, flags);

	val = readl(reg_base + GPIO_CONTROL(gpio));
	val &= ~GPIO_GPCTR0_IOTR_MASK;
	val |= GPIO_GPCTR0_IOTR_CMD_INPUT;
	writel(val, reg_base + GPIO_CONTROL(gpio));

	raw_spin_unlock_irqrestore(&kona_gpio->lock, flags);

	return 0;
}

static int bcm_kona_gpio_direction_output(struct gpio_chip *chip,
					  unsigned gpio, int value)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	int bank_id = GPIO_BANK(gpio);
	int bit = GPIO_BIT(gpio);
	u32 val, reg_offset;
	unsigned long flags;

	kona_gpio = gpiochip_get_data(chip);
	reg_base = kona_gpio->reg_base;
	raw_spin_lock_irqsave(&kona_gpio->lock, flags);

	val = readl(reg_base + GPIO_CONTROL(gpio));
	val &= ~GPIO_GPCTR0_IOTR_MASK;
	val |= GPIO_GPCTR0_IOTR_CMD_0UTPUT;
	writel(val, reg_base + GPIO_CONTROL(gpio));
	reg_offset = value ? GPIO_OUT_SET(bank_id) : GPIO_OUT_CLEAR(bank_id);

	val = readl(reg_base + reg_offset);
	val |= BIT(bit);
	writel(val, reg_base + reg_offset);

	raw_spin_unlock_irqrestore(&kona_gpio->lock, flags);

	return 0;
}

static int bcm_kona_gpio_to_irq(struct gpio_chip *chip, unsigned gpio)
{
	struct bcm_kona_gpio *kona_gpio;

	kona_gpio = gpiochip_get_data(chip);
	if (gpio >= kona_gpio->gpio_chip.ngpio)
		return -ENXIO;
	return irq_create_mapping(kona_gpio->irq_domain, gpio);
}

static int bcm_kona_gpio_set_debounce(struct gpio_chip *chip, unsigned gpio,
				      unsigned debounce)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	u32 val, res;
	unsigned long flags;

	kona_gpio = gpiochip_get_data(chip);
	reg_base = kona_gpio->reg_base;
	/* debounce must be 1-128ms (or 0) */
	if ((debounce > 0 && debounce < 1000) || debounce > 128000) {
		dev_err(chip->parent, "Debounce value %u not in range\n",
			debounce);
		return -EINVAL;
	}

	/* calculate debounce bit value */
	if (debounce != 0) {
		/* Convert to ms */
		debounce /= 1000;
		/* find the MSB */
		res = fls(debounce) - 1;
		/* Check if MSB-1 is set (round up or down) */
		if (res > 0 && (debounce & BIT(res - 1)))
			res++;
	}

	/* spin lock for read-modify-write of the GPIO register */
	raw_spin_lock_irqsave(&kona_gpio->lock, flags);

	val = readl(reg_base + GPIO_CONTROL(gpio));
	val &= ~GPIO_GPCTR0_DBR_MASK;

	if (debounce == 0) {
		/* disable debounce */
		val &= ~GPIO_GPCTR0_DB_ENABLE_MASK;
	} else {
		val |= GPIO_GPCTR0_DB_ENABLE_MASK |
		    (res << GPIO_GPCTR0_DBR_SHIFT);
	}

	writel(val, reg_base + GPIO_CONTROL(gpio));

	raw_spin_unlock_irqrestore(&kona_gpio->lock, flags);

	return 0;
}

static int bcm_kona_gpio_set_config(struct gpio_chip *chip, unsigned gpio,
				    unsigned long config)
{
	u32 debounce;

	if (pinconf_to_config_param(config) != PIN_CONFIG_INPUT_DEBOUNCE)
		return -ENOTSUPP;

	debounce = pinconf_to_config_argument(config);
	return bcm_kona_gpio_set_debounce(chip, gpio, debounce);
}

static const struct gpio_chip template_chip = {
	.label = "bcm-kona-gpio",
	.owner = THIS_MODULE,
	.request = bcm_kona_gpio_request,
	.free = bcm_kona_gpio_free,
	.get_direction = bcm_kona_gpio_get_dir,
	.direction_input = bcm_kona_gpio_direction_input,
	.get = bcm_kona_gpio_get,
	.direction_output = bcm_kona_gpio_direction_output,
	.set = bcm_kona_gpio_set,
	.set_config = bcm_kona_gpio_set_config,
	.to_irq = bcm_kona_gpio_to_irq,
	.base = 0,
};

static void bcm_kona_gpio_irq_ack(struct irq_data *d)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	unsigned gpio = d->hwirq;
	int bank_id = GPIO_BANK(gpio);
	int bit = GPIO_BIT(gpio);
	u32 val;
	unsigned long flags;

	kona_gpio = irq_data_get_irq_chip_data(d);
	reg_base = kona_gpio->reg_base;
	raw_spin_lock_irqsave(&kona_gpio->lock, flags);

	val = readl(reg_base + GPIO_INT_STATUS(bank_id));
	val |= BIT(bit);
	writel(val, reg_base + GPIO_INT_STATUS(bank_id));

	raw_spin_unlock_irqrestore(&kona_gpio->lock, flags);
}

static void bcm_kona_gpio_irq_mask(struct irq_data *d)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	unsigned gpio = d->hwirq;
	int bank_id = GPIO_BANK(gpio);
	int bit = GPIO_BIT(gpio);
	u32 val;
	unsigned long flags;

	kona_gpio = irq_data_get_irq_chip_data(d);
	reg_base = kona_gpio->reg_base;
	raw_spin_lock_irqsave(&kona_gpio->lock, flags);

	val = readl(reg_base + GPIO_INT_MASK(bank_id));
	val |= BIT(bit);
	writel(val, reg_base + GPIO_INT_MASK(bank_id));
	gpiochip_disable_irq(&kona_gpio->gpio_chip, gpio);

	raw_spin_unlock_irqrestore(&kona_gpio->lock, flags);
}

static void bcm_kona_gpio_irq_unmask(struct irq_data *d)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	unsigned gpio = d->hwirq;
	int bank_id = GPIO_BANK(gpio);
	int bit = GPIO_BIT(gpio);
	u32 val;
	unsigned long flags;

	kona_gpio = irq_data_get_irq_chip_data(d);
	reg_base = kona_gpio->reg_base;
	raw_spin_lock_irqsave(&kona_gpio->lock, flags);

	val = readl(reg_base + GPIO_INT_MSKCLR(bank_id));
	val |= BIT(bit);
	writel(val, reg_base + GPIO_INT_MSKCLR(bank_id));
	gpiochip_enable_irq(&kona_gpio->gpio_chip, gpio);

	raw_spin_unlock_irqrestore(&kona_gpio->lock, flags);
}

static int bcm_kona_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	unsigned gpio = d->hwirq;
	u32 lvl_type;
	u32 val;
	unsigned long flags;

	kona_gpio = irq_data_get_irq_chip_data(d);
	reg_base = kona_gpio->reg_base;
	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_RISING:
		lvl_type = GPIO_GPCTR0_ITR_CMD_RISING_EDGE;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		lvl_type = GPIO_GPCTR0_ITR_CMD_FALLING_EDGE;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		lvl_type = GPIO_GPCTR0_ITR_CMD_BOTH_EDGE;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
	case IRQ_TYPE_LEVEL_LOW:
		/* BCM GPIO doesn't support level triggering */
	default:
		dev_err(kona_gpio->gpio_chip.parent,
			"Invalid BCM GPIO irq type 0x%x\n", type);
		return -EINVAL;
	}

	raw_spin_lock_irqsave(&kona_gpio->lock, flags);

	val = readl(reg_base + GPIO_CONTROL(gpio));
	val &= ~GPIO_GPCTR0_ITR_MASK;
	val |= lvl_type << GPIO_GPCTR0_ITR_SHIFT;
	writel(val, reg_base + GPIO_CONTROL(gpio));

	raw_spin_unlock_irqrestore(&kona_gpio->lock, flags);

	return 0;
}

static void bcm_kona_gpio_irq_handler(struct irq_desc *desc)
{
	void __iomem *reg_base;
	int bit, bank_id;
	unsigned long sta;
	struct bcm_kona_gpio_bank *bank = irq_desc_get_handler_data(desc);
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	/*
	 * For bank interrupts, we can't use chip_data to store the kona_gpio
	 * pointer, since GIC needs it for its own purposes. Therefore, we get
	 * our pointer from the bank structure.
	 */
	reg_base = bank->kona_gpio->reg_base;
	bank_id = bank->id;

	while ((sta = readl(reg_base + GPIO_INT_STATUS(bank_id)) &
		    (~(readl(reg_base + GPIO_INT_MASK(bank_id)))))) {
		for_each_set_bit(bit, &sta, 32) {
			int hwirq = GPIO_PER_BANK * bank_id + bit;
			int child_irq =
				irq_find_mapping(bank->kona_gpio->irq_domain,
						 hwirq);
			/*
			 * Clear interrupt before handler is called so we don't
			 * miss any interrupt occurred during executing them.
			 */
			writel(readl(reg_base + GPIO_INT_STATUS(bank_id)) |
			       BIT(bit), reg_base + GPIO_INT_STATUS(bank_id));
			/* Invoke interrupt handler */
			generic_handle_irq(child_irq);
		}
	}

	chained_irq_exit(chip, desc);
}

static int bcm_kona_gpio_irq_reqres(struct irq_data *d)
{
	struct bcm_kona_gpio *kona_gpio = irq_data_get_irq_chip_data(d);

	return gpiochip_reqres_irq(&kona_gpio->gpio_chip, d->hwirq);
}

static void bcm_kona_gpio_irq_relres(struct irq_data *d)
{
	struct bcm_kona_gpio *kona_gpio = irq_data_get_irq_chip_data(d);

	gpiochip_relres_irq(&kona_gpio->gpio_chip, d->hwirq);
}

static struct irq_chip bcm_gpio_irq_chip = {
	.name = "bcm-kona-gpio",
	.irq_ack = bcm_kona_gpio_irq_ack,
	.irq_mask = bcm_kona_gpio_irq_mask,
	.irq_unmask = bcm_kona_gpio_irq_unmask,
	.irq_set_type = bcm_kona_gpio_irq_set_type,
	.irq_request_resources = bcm_kona_gpio_irq_reqres,
	.irq_release_resources = bcm_kona_gpio_irq_relres,
};

static struct of_device_id const bcm_kona_gpio_of_match[] = {
	{ .compatible = "brcm,kona-gpio" },
	{}
};

/*
 * This lock class tells lockdep that GPIO irqs are in a different
 * category than their parents, so it won't report false recursion.
 */
static struct lock_class_key gpio_lock_class;
static struct lock_class_key gpio_request_class;

static int bcm_kona_gpio_irq_map(struct irq_domain *d, unsigned int irq,
				 irq_hw_number_t hwirq)
{
	int ret;

	ret = irq_set_chip_data(irq, d->host_data);
	if (ret < 0)
		return ret;
	irq_set_lockdep_class(irq, &gpio_lock_class, &gpio_request_class);
	irq_set_chip_and_handler(irq, &bcm_gpio_irq_chip, handle_simple_irq);
	irq_set_noprobe(irq);

	return 0;
}

static void bcm_kona_gpio_irq_unmap(struct irq_domain *d, unsigned int irq)
{
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops bcm_kona_irq_ops = {
	.map = bcm_kona_gpio_irq_map,
	.unmap = bcm_kona_gpio_irq_unmap,
	.xlate = irq_domain_xlate_twocell,
};

static void bcm_kona_gpio_reset(struct bcm_kona_gpio *kona_gpio)
{
	void __iomem *reg_base;
	int i;

	reg_base = kona_gpio->reg_base;
	/* disable interrupts and clear status */
	for (i = 0; i < kona_gpio->num_bank; i++) {
		/* Unlock the entire bank first */
		bcm_kona_gpio_write_lock_regs(reg_base, i, UNLOCK_CODE);
		writel(0xffffffff, reg_base + GPIO_INT_MASK(i));
		writel(0xffffffff, reg_base + GPIO_INT_STATUS(i));
		/* Now re-lock the bank */
		bcm_kona_gpio_write_lock_regs(reg_base, i, LOCK_CODE);
	}
}

static int bcm_kona_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct of_device_id *match;
	struct resource *res;
	struct bcm_kona_gpio_bank *bank;
	struct bcm_kona_gpio *kona_gpio;
	struct gpio_chip *chip;
	int ret;
	int i;

	match = of_match_device(bcm_kona_gpio_of_match, dev);
	if (!match) {
		dev_err(dev, "Failed to find gpio controller\n");
		return -ENODEV;
	}

	kona_gpio = devm_kzalloc(dev, sizeof(*kona_gpio), GFP_KERNEL);
	if (!kona_gpio)
		return -ENOMEM;

	kona_gpio->gpio_chip = template_chip;
	chip = &kona_gpio->gpio_chip;
	kona_gpio->num_bank = of_irq_count(dev->of_node);
	if (kona_gpio->num_bank == 0) {
		dev_err(dev, "Couldn't determine # GPIO banks\n");
		return -ENOENT;
	}
	if (kona_gpio->num_bank > GPIO_MAX_BANK_NUM) {
		dev_err(dev, "Too many GPIO banks configured (max=%d)\n",
			GPIO_MAX_BANK_NUM);
		return -ENXIO;
	}
	kona_gpio->banks = devm_kcalloc(dev,
					kona_gpio->num_bank,
					sizeof(*kona_gpio->banks),
					GFP_KERNEL);
	if (!kona_gpio->banks)
		return -ENOMEM;

	kona_gpio->pdev = pdev;
	platform_set_drvdata(pdev, kona_gpio);
	chip->of_node = dev->of_node;
	chip->ngpio = kona_gpio->num_bank * GPIO_PER_BANK;

	kona_gpio->irq_domain = irq_domain_add_linear(dev->of_node,
						      chip->ngpio,
						      &bcm_kona_irq_ops,
						      kona_gpio);
	if (!kona_gpio->irq_domain) {
		dev_err(dev, "Couldn't allocate IRQ domain\n");
		return -ENXIO;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	kona_gpio->reg_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(kona_gpio->reg_base)) {
		ret = -ENXIO;
		goto err_irq_domain;
	}

	for (i = 0; i < kona_gpio->num_bank; i++) {
		bank = &kona_gpio->banks[i];
		bank->id = i;
		bank->irq = platform_get_irq(pdev, i);
		bank->kona_gpio = kona_gpio;
		if (bank->irq < 0) {
			dev_err(dev, "Couldn't get IRQ for bank %d", i);
			ret = -ENOENT;
			goto err_irq_domain;
		}
	}

	dev_info(&pdev->dev, "Setting up Kona GPIO\n");

	bcm_kona_gpio_reset(kona_gpio);

	ret = devm_gpiochip_add_data(dev, chip, kona_gpio);
	if (ret < 0) {
		dev_err(dev, "Couldn't add GPIO chip -- %d\n", ret);
		goto err_irq_domain;
	}
	for (i = 0; i < kona_gpio->num_bank; i++) {
		bank = &kona_gpio->banks[i];
		irq_set_chained_handler_and_data(bank->irq,
						 bcm_kona_gpio_irq_handler,
						 bank);
	}

	raw_spin_lock_init(&kona_gpio->lock);

	return 0;

err_irq_domain:
	irq_domain_remove(kona_gpio->irq_domain);

	return ret;
}

static struct platform_driver bcm_kona_gpio_driver = {
	.driver = {
			.name = "bcm-kona-gpio",
			.of_match_table = bcm_kona_gpio_of_match,
	},
	.probe = bcm_kona_gpio_probe,
};
builtin_platform_driver(bcm_kona_gpio_driver);
