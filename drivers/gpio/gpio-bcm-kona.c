/*
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
#include <linux/gpio.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/irqdomain.h>
#include <linux/irqchip/chained_irq.h>

#define BCM_GPIO_PASSWD				0x00a5a501
#define GPIO_PER_BANK				32
#define GPIO_MAX_BANK_NUM			8

#define GPIO_BANK(gpio)				((gpio) >> 5)
#define GPIO_BIT(gpio)				((gpio) & (GPIO_PER_BANK - 1))

#define GPIO_OUT_STATUS(bank)			(0x00000000 + ((bank) << 2))
#define GPIO_IN_STATUS(bank)			(0x00000020 + ((bank) << 2))
#define GPIO_OUT_SET(bank)			(0x00000040 + ((bank) << 2))
#define GPIO_OUT_CLEAR(bank)			(0x00000060 + ((bank) << 2))
#define GPIO_INT_STATUS(bank)			(0x00000080 + ((bank) << 2))
#define GPIO_INT_MASK(bank)			(0x000000a0 + ((bank) << 2))
#define GPIO_INT_MSKCLR(bank)			(0x000000c0 + ((bank) << 2))
#define GPIO_CONTROL(bank)			(0x00000100 + ((bank) << 2))
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
	spinlock_t lock;
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

static inline struct bcm_kona_gpio *to_kona_gpio(struct gpio_chip *chip)
{
	return container_of(chip, struct bcm_kona_gpio, gpio_chip);
}

static void bcm_kona_gpio_set_lockcode_bank(void __iomem *reg_base,
					    int bank_id, int lockcode)
{
	writel(BCM_GPIO_PASSWD, reg_base + GPIO_GPPWR_OFFSET);
	writel(lockcode, reg_base + GPIO_PWD_STATUS(bank_id));
}

static inline void bcm_kona_gpio_lock_bank(void __iomem *reg_base, int bank_id)
{
	bcm_kona_gpio_set_lockcode_bank(reg_base, bank_id, LOCK_CODE);
}

static inline void bcm_kona_gpio_unlock_bank(void __iomem *reg_base,
					     int bank_id)
{
	bcm_kona_gpio_set_lockcode_bank(reg_base, bank_id, UNLOCK_CODE);
}

static void bcm_kona_gpio_set(struct gpio_chip *chip, unsigned gpio, int value)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	int bank_id = GPIO_BANK(gpio);
	int bit = GPIO_BIT(gpio);
	u32 val, reg_offset;
	unsigned long flags;

	kona_gpio = to_kona_gpio(chip);
	reg_base = kona_gpio->reg_base;
	spin_lock_irqsave(&kona_gpio->lock, flags);
	bcm_kona_gpio_unlock_bank(reg_base, bank_id);

	/* determine the GPIO pin direction */
	val = readl(reg_base + GPIO_CONTROL(gpio));
	val &= GPIO_GPCTR0_IOTR_MASK;

	/* this function only applies to output pin */
	if (GPIO_GPCTR0_IOTR_CMD_INPUT == val)
		goto out;

	reg_offset = value ? GPIO_OUT_SET(bank_id) : GPIO_OUT_CLEAR(bank_id);

	val = readl(reg_base + reg_offset);
	val |= BIT(bit);
	writel(val, reg_base + reg_offset);

out:
	bcm_kona_gpio_lock_bank(reg_base, bank_id);
	spin_unlock_irqrestore(&kona_gpio->lock, flags);
}

static int bcm_kona_gpio_get(struct gpio_chip *chip, unsigned gpio)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	int bank_id = GPIO_BANK(gpio);
	int bit = GPIO_BIT(gpio);
	u32 val, reg_offset;
	unsigned long flags;

	kona_gpio = to_kona_gpio(chip);
	reg_base = kona_gpio->reg_base;
	spin_lock_irqsave(&kona_gpio->lock, flags);
	bcm_kona_gpio_unlock_bank(reg_base, bank_id);

	/* determine the GPIO pin direction */
	val = readl(reg_base + GPIO_CONTROL(gpio));
	val &= GPIO_GPCTR0_IOTR_MASK;

	/* read the GPIO bank status */
	reg_offset = (GPIO_GPCTR0_IOTR_CMD_INPUT == val) ?
	    GPIO_IN_STATUS(bank_id) : GPIO_OUT_STATUS(bank_id);
	val = readl(reg_base + reg_offset);

	bcm_kona_gpio_lock_bank(reg_base, bank_id);
	spin_unlock_irqrestore(&kona_gpio->lock, flags);

	/* return the specified bit status */
	return !!(val & BIT(bit));
}

static int bcm_kona_gpio_direction_input(struct gpio_chip *chip, unsigned gpio)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	u32 val;
	unsigned long flags;
	int bank_id = GPIO_BANK(gpio);

	kona_gpio = to_kona_gpio(chip);
	reg_base = kona_gpio->reg_base;
	spin_lock_irqsave(&kona_gpio->lock, flags);
	bcm_kona_gpio_unlock_bank(reg_base, bank_id);

	val = readl(reg_base + GPIO_CONTROL(gpio));
	val &= ~GPIO_GPCTR0_IOTR_MASK;
	val |= GPIO_GPCTR0_IOTR_CMD_INPUT;
	writel(val, reg_base + GPIO_CONTROL(gpio));

	bcm_kona_gpio_lock_bank(reg_base, bank_id);
	spin_unlock_irqrestore(&kona_gpio->lock, flags);

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

	kona_gpio = to_kona_gpio(chip);
	reg_base = kona_gpio->reg_base;
	spin_lock_irqsave(&kona_gpio->lock, flags);
	bcm_kona_gpio_unlock_bank(reg_base, bank_id);

	val = readl(reg_base + GPIO_CONTROL(gpio));
	val &= ~GPIO_GPCTR0_IOTR_MASK;
	val |= GPIO_GPCTR0_IOTR_CMD_0UTPUT;
	writel(val, reg_base + GPIO_CONTROL(gpio));
	reg_offset = value ? GPIO_OUT_SET(bank_id) : GPIO_OUT_CLEAR(bank_id);

	val = readl(reg_base + reg_offset);
	val |= BIT(bit);
	writel(val, reg_base + reg_offset);

	bcm_kona_gpio_lock_bank(reg_base, bank_id);
	spin_unlock_irqrestore(&kona_gpio->lock, flags);

	return 0;
}

static int bcm_kona_gpio_to_irq(struct gpio_chip *chip, unsigned gpio)
{
	struct bcm_kona_gpio *kona_gpio;

	kona_gpio = to_kona_gpio(chip);
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
	int bank_id = GPIO_BANK(gpio);

	kona_gpio = to_kona_gpio(chip);
	reg_base = kona_gpio->reg_base;
	/* debounce must be 1-128ms (or 0) */
	if ((debounce > 0 && debounce < 1000) || debounce > 128000) {
		dev_err(chip->dev, "Debounce value %u not in range\n",
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
	spin_lock_irqsave(&kona_gpio->lock, flags);
	bcm_kona_gpio_unlock_bank(reg_base, bank_id);

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

	bcm_kona_gpio_lock_bank(reg_base, bank_id);
	spin_unlock_irqrestore(&kona_gpio->lock, flags);

	return 0;
}

static struct gpio_chip template_chip = {
	.label = "bcm-kona-gpio",
	.owner = THIS_MODULE,
	.direction_input = bcm_kona_gpio_direction_input,
	.get = bcm_kona_gpio_get,
	.direction_output = bcm_kona_gpio_direction_output,
	.set = bcm_kona_gpio_set,
	.set_debounce = bcm_kona_gpio_set_debounce,
	.to_irq = bcm_kona_gpio_to_irq,
	.base = 0,
};

static void bcm_kona_gpio_irq_ack(struct irq_data *d)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	int gpio = d->hwirq;
	int bank_id = GPIO_BANK(gpio);
	int bit = GPIO_BIT(gpio);
	u32 val;
	unsigned long flags;

	kona_gpio = irq_data_get_irq_chip_data(d);
	reg_base = kona_gpio->reg_base;
	spin_lock_irqsave(&kona_gpio->lock, flags);
	bcm_kona_gpio_unlock_bank(reg_base, bank_id);

	val = readl(reg_base + GPIO_INT_STATUS(bank_id));
	val |= BIT(bit);
	writel(val, reg_base + GPIO_INT_STATUS(bank_id));

	bcm_kona_gpio_lock_bank(reg_base, bank_id);
	spin_unlock_irqrestore(&kona_gpio->lock, flags);
}

static void bcm_kona_gpio_irq_mask(struct irq_data *d)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	int gpio = d->hwirq;
	int bank_id = GPIO_BANK(gpio);
	int bit = GPIO_BIT(gpio);
	u32 val;
	unsigned long flags;

	kona_gpio = irq_data_get_irq_chip_data(d);
	reg_base = kona_gpio->reg_base;
	spin_lock_irqsave(&kona_gpio->lock, flags);
	bcm_kona_gpio_unlock_bank(reg_base, bank_id);

	val = readl(reg_base + GPIO_INT_MASK(bank_id));
	val |= BIT(bit);
	writel(val, reg_base + GPIO_INT_MASK(bank_id));

	bcm_kona_gpio_lock_bank(reg_base, bank_id);
	spin_unlock_irqrestore(&kona_gpio->lock, flags);
}

static void bcm_kona_gpio_irq_unmask(struct irq_data *d)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	int gpio = d->hwirq;
	int bank_id = GPIO_BANK(gpio);
	int bit = GPIO_BIT(gpio);
	u32 val;
	unsigned long flags;

	kona_gpio = irq_data_get_irq_chip_data(d);
	reg_base = kona_gpio->reg_base;
	spin_lock_irqsave(&kona_gpio->lock, flags);
	bcm_kona_gpio_unlock_bank(reg_base, bank_id);

	val = readl(reg_base + GPIO_INT_MSKCLR(bank_id));
	val |= BIT(bit);
	writel(val, reg_base + GPIO_INT_MSKCLR(bank_id));

	bcm_kona_gpio_lock_bank(reg_base, bank_id);
	spin_unlock_irqrestore(&kona_gpio->lock, flags);
}

static int bcm_kona_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct bcm_kona_gpio *kona_gpio;
	void __iomem *reg_base;
	int gpio = d->hwirq;
	u32 lvl_type;
	u32 val;
	unsigned long flags;
	int bank_id = GPIO_BANK(gpio);

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
		dev_err(kona_gpio->gpio_chip.dev,
			"Invalid BCM GPIO irq type 0x%x\n", type);
		return -EINVAL;
	}

	spin_lock_irqsave(&kona_gpio->lock, flags);
	bcm_kona_gpio_unlock_bank(reg_base, bank_id);

	val = readl(reg_base + GPIO_CONTROL(gpio));
	val &= ~GPIO_GPCTR0_ITR_MASK;
	val |= lvl_type << GPIO_GPCTR0_ITR_SHIFT;
	writel(val, reg_base + GPIO_CONTROL(gpio));

	bcm_kona_gpio_lock_bank(reg_base, bank_id);
	spin_unlock_irqrestore(&kona_gpio->lock, flags);

	return 0;
}

static void bcm_kona_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	void __iomem *reg_base;
	int bit, bank_id;
	unsigned long sta;
	struct bcm_kona_gpio_bank *bank = irq_get_handler_data(irq);
	struct irq_chip *chip = irq_desc_get_chip(desc);

	chained_irq_enter(chip, desc);

	/*
	 * For bank interrupts, we can't use chip_data to store the kona_gpio
	 * pointer, since GIC needs it for its own purposes. Therefore, we get
	 * our pointer from the bank structure.
	 */
	reg_base = bank->kona_gpio->reg_base;
	bank_id = bank->id;
	bcm_kona_gpio_unlock_bank(reg_base, bank_id);

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

	bcm_kona_gpio_lock_bank(reg_base, bank_id);

	chained_irq_exit(chip, desc);
}

static unsigned int bcm_kona_gpio_irq_startup(struct irq_data *d)
{
	struct bcm_kona_gpio *kona_gpio = irq_data_get_irq_chip_data(d);

	if (gpio_lock_as_irq(&kona_gpio->gpio_chip, d->hwirq))
		dev_err(kona_gpio->gpio_chip.dev,
			"unable to lock HW IRQ %lu for IRQ\n",
			d->hwirq);
	bcm_kona_gpio_irq_unmask(d);
	return 0;
}

static void bcm_kona_gpio_irq_shutdown(struct irq_data *d)
{
	struct bcm_kona_gpio *kona_gpio = irq_data_get_irq_chip_data(d);

	bcm_kona_gpio_irq_mask(d);
	gpio_unlock_as_irq(&kona_gpio->gpio_chip, d->hwirq);
}

static struct irq_chip bcm_gpio_irq_chip = {
	.name = "bcm-kona-gpio",
	.irq_ack = bcm_kona_gpio_irq_ack,
	.irq_mask = bcm_kona_gpio_irq_mask,
	.irq_unmask = bcm_kona_gpio_irq_unmask,
	.irq_set_type = bcm_kona_gpio_irq_set_type,
	.irq_startup = bcm_kona_gpio_irq_startup,
	.irq_shutdown = bcm_kona_gpio_irq_shutdown,
};

static struct __initconst of_device_id bcm_kona_gpio_of_match[] = {
	{ .compatible = "brcm,kona-gpio" },
	{}
};

MODULE_DEVICE_TABLE(of, bcm_kona_gpio_of_match);

/*
 * This lock class tells lockdep that GPIO irqs are in a different
 * category than their parents, so it won't report false recursion.
 */
static struct lock_class_key gpio_lock_class;

static int bcm_kona_gpio_irq_map(struct irq_domain *d, unsigned int irq,
				 irq_hw_number_t hwirq)
{
	int ret;

	ret = irq_set_chip_data(irq, d->host_data);
	if (ret < 0)
		return ret;
	irq_set_lockdep_class(irq, &gpio_lock_class);
	irq_set_chip_and_handler(irq, &bcm_gpio_irq_chip, handle_simple_irq);
#ifdef CONFIG_ARM
	set_irq_flags(irq, IRQF_VALID);
#else
	irq_set_noprobe(irq);
#endif

	return 0;
}

static void bcm_kona_gpio_irq_unmap(struct irq_domain *d, unsigned int irq)
{
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static struct irq_domain_ops bcm_kona_irq_ops = {
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
		bcm_kona_gpio_unlock_bank(reg_base, i);
		writel(0xffffffff, reg_base + GPIO_INT_MASK(i));
		writel(0xffffffff, reg_base + GPIO_INT_STATUS(i));
		bcm_kona_gpio_lock_bank(reg_base, i);
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
	kona_gpio->banks = devm_kzalloc(dev,
					kona_gpio->num_bank *
					sizeof(*kona_gpio->banks), GFP_KERNEL);
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

	ret = gpiochip_add(chip);
	if (ret < 0) {
		dev_err(dev, "Couldn't add GPIO chip -- %d\n", ret);
		goto err_irq_domain;
	}
	for (i = 0; i < chip->ngpio; i++) {
		int irq = bcm_kona_gpio_to_irq(chip, i);
		irq_set_lockdep_class(irq, &gpio_lock_class);
		irq_set_chip_and_handler(irq, &bcm_gpio_irq_chip,
					 handle_simple_irq);
#ifdef CONFIG_ARM
		set_irq_flags(irq, IRQF_VALID);
#else
		irq_set_noprobe(irq);
#endif
	}
	for (i = 0; i < kona_gpio->num_bank; i++) {
		bank = &kona_gpio->banks[i];
		irq_set_chained_handler(bank->irq, bcm_kona_gpio_irq_handler);
		irq_set_handler_data(bank->irq, bank);
	}

	spin_lock_init(&kona_gpio->lock);

	return 0;

err_irq_domain:
	irq_domain_remove(kona_gpio->irq_domain);

	return ret;
}

static struct platform_driver bcm_kona_gpio_driver = {
	.driver = {
			.name = "bcm-kona-gpio",
			.owner = THIS_MODULE,
			.of_match_table = bcm_kona_gpio_of_match,
	},
	.probe = bcm_kona_gpio_probe,
};

module_platform_driver(bcm_kona_gpio_driver);

MODULE_AUTHOR("Broadcom Corporation <bcm-kernel-feedback-list@broadcom.com>");
MODULE_DESCRIPTION("Broadcom Kona GPIO Driver");
MODULE_LICENSE("GPL v2");
