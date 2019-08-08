// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 Spreadtrum Communications Inc.
 * Copyright (C) 2018 Linaro Ltd.
 */

#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

/* GPIO registers definition */
#define SPRD_GPIO_DATA		0x0
#define SPRD_GPIO_DMSK		0x4
#define SPRD_GPIO_DIR		0x8
#define SPRD_GPIO_IS		0xc
#define SPRD_GPIO_IBE		0x10
#define SPRD_GPIO_IEV		0x14
#define SPRD_GPIO_IE		0x18
#define SPRD_GPIO_RIS		0x1c
#define SPRD_GPIO_MIS		0x20
#define SPRD_GPIO_IC		0x24
#define SPRD_GPIO_INEN		0x28

/* We have 16 banks GPIOs and each bank contain 16 GPIOs */
#define SPRD_GPIO_BANK_NR	16
#define SPRD_GPIO_NR		256
#define SPRD_GPIO_BANK_SIZE	0x80
#define SPRD_GPIO_BANK_MASK	GENMASK(15, 0)
#define SPRD_GPIO_BIT(x)	((x) & (SPRD_GPIO_BANK_NR - 1))

struct sprd_gpio {
	struct gpio_chip chip;
	void __iomem *base;
	spinlock_t lock;
	int irq;
};

static inline void __iomem *sprd_gpio_bank_base(struct sprd_gpio *sprd_gpio,
						unsigned int bank)
{
	return sprd_gpio->base + SPRD_GPIO_BANK_SIZE * bank;
}

static void sprd_gpio_update(struct gpio_chip *chip, unsigned int offset,
			     u16 reg, int val)
{
	struct sprd_gpio *sprd_gpio = gpiochip_get_data(chip);
	void __iomem *base = sprd_gpio_bank_base(sprd_gpio,
						 offset / SPRD_GPIO_BANK_NR);
	unsigned long flags;
	u32 tmp;

	spin_lock_irqsave(&sprd_gpio->lock, flags);
	tmp = readl_relaxed(base + reg);

	if (val)
		tmp |= BIT(SPRD_GPIO_BIT(offset));
	else
		tmp &= ~BIT(SPRD_GPIO_BIT(offset));

	writel_relaxed(tmp, base + reg);
	spin_unlock_irqrestore(&sprd_gpio->lock, flags);
}

static int sprd_gpio_read(struct gpio_chip *chip, unsigned int offset, u16 reg)
{
	struct sprd_gpio *sprd_gpio = gpiochip_get_data(chip);
	void __iomem *base = sprd_gpio_bank_base(sprd_gpio,
						 offset / SPRD_GPIO_BANK_NR);

	return !!(readl_relaxed(base + reg) & BIT(SPRD_GPIO_BIT(offset)));
}

static int sprd_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	sprd_gpio_update(chip, offset, SPRD_GPIO_DMSK, 1);
	return 0;
}

static void sprd_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	sprd_gpio_update(chip, offset, SPRD_GPIO_DMSK, 0);
}

static int sprd_gpio_direction_input(struct gpio_chip *chip,
				     unsigned int offset)
{
	sprd_gpio_update(chip, offset, SPRD_GPIO_DIR, 0);
	sprd_gpio_update(chip, offset, SPRD_GPIO_INEN, 1);
	return 0;
}

static int sprd_gpio_direction_output(struct gpio_chip *chip,
				      unsigned int offset, int value)
{
	sprd_gpio_update(chip, offset, SPRD_GPIO_DIR, 1);
	sprd_gpio_update(chip, offset, SPRD_GPIO_INEN, 0);
	sprd_gpio_update(chip, offset, SPRD_GPIO_DATA, value);
	return 0;
}

static int sprd_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	return sprd_gpio_read(chip, offset, SPRD_GPIO_DATA);
}

static void sprd_gpio_set(struct gpio_chip *chip, unsigned int offset,
			  int value)
{
	sprd_gpio_update(chip, offset, SPRD_GPIO_DATA, value);
}

static void sprd_gpio_irq_mask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	u32 offset = irqd_to_hwirq(data);

	sprd_gpio_update(chip, offset, SPRD_GPIO_IE, 0);
}

static void sprd_gpio_irq_ack(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	u32 offset = irqd_to_hwirq(data);

	sprd_gpio_update(chip, offset, SPRD_GPIO_IC, 1);
}

static void sprd_gpio_irq_unmask(struct irq_data *data)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	u32 offset = irqd_to_hwirq(data);

	sprd_gpio_update(chip, offset, SPRD_GPIO_IE, 1);
}

static int sprd_gpio_irq_set_type(struct irq_data *data,
				  unsigned int flow_type)
{
	struct gpio_chip *chip = irq_data_get_irq_chip_data(data);
	u32 offset = irqd_to_hwirq(data);

	switch (flow_type) {
	case IRQ_TYPE_EDGE_RISING:
		sprd_gpio_update(chip, offset, SPRD_GPIO_IS, 0);
		sprd_gpio_update(chip, offset, SPRD_GPIO_IBE, 0);
		sprd_gpio_update(chip, offset, SPRD_GPIO_IEV, 1);
		irq_set_handler_locked(data, handle_edge_irq);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		sprd_gpio_update(chip, offset, SPRD_GPIO_IS, 0);
		sprd_gpio_update(chip, offset, SPRD_GPIO_IBE, 0);
		sprd_gpio_update(chip, offset, SPRD_GPIO_IEV, 0);
		irq_set_handler_locked(data, handle_edge_irq);
		break;
	case IRQ_TYPE_EDGE_BOTH:
		sprd_gpio_update(chip, offset, SPRD_GPIO_IS, 0);
		sprd_gpio_update(chip, offset, SPRD_GPIO_IBE, 1);
		irq_set_handler_locked(data, handle_edge_irq);
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		sprd_gpio_update(chip, offset, SPRD_GPIO_IS, 1);
		sprd_gpio_update(chip, offset, SPRD_GPIO_IBE, 0);
		sprd_gpio_update(chip, offset, SPRD_GPIO_IEV, 1);
		irq_set_handler_locked(data, handle_level_irq);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		sprd_gpio_update(chip, offset, SPRD_GPIO_IS, 1);
		sprd_gpio_update(chip, offset, SPRD_GPIO_IBE, 0);
		sprd_gpio_update(chip, offset, SPRD_GPIO_IEV, 0);
		irq_set_handler_locked(data, handle_level_irq);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void sprd_gpio_irq_handler(struct irq_desc *desc)
{
	struct gpio_chip *chip = irq_desc_get_handler_data(desc);
	struct irq_chip *ic = irq_desc_get_chip(desc);
	struct sprd_gpio *sprd_gpio = gpiochip_get_data(chip);
	u32 bank, n, girq;

	chained_irq_enter(ic, desc);

	for (bank = 0; bank * SPRD_GPIO_BANK_NR < chip->ngpio; bank++) {
		void __iomem *base = sprd_gpio_bank_base(sprd_gpio, bank);
		unsigned long reg = readl_relaxed(base + SPRD_GPIO_MIS) &
			SPRD_GPIO_BANK_MASK;

		for_each_set_bit(n, &reg, SPRD_GPIO_BANK_NR) {
			girq = irq_find_mapping(chip->irq.domain,
						bank * SPRD_GPIO_BANK_NR + n);

			generic_handle_irq(girq);
		}

	}
	chained_irq_exit(ic, desc);
}

static struct irq_chip sprd_gpio_irqchip = {
	.name = "sprd-gpio",
	.irq_ack = sprd_gpio_irq_ack,
	.irq_mask = sprd_gpio_irq_mask,
	.irq_unmask = sprd_gpio_irq_unmask,
	.irq_set_type = sprd_gpio_irq_set_type,
	.flags = IRQCHIP_SKIP_SET_WAKE,
};

static int sprd_gpio_probe(struct platform_device *pdev)
{
	struct gpio_irq_chip *irq;
	struct sprd_gpio *sprd_gpio;
	int ret;

	sprd_gpio = devm_kzalloc(&pdev->dev, sizeof(*sprd_gpio), GFP_KERNEL);
	if (!sprd_gpio)
		return -ENOMEM;

	sprd_gpio->irq = platform_get_irq(pdev, 0);
	if (sprd_gpio->irq < 0) {
		dev_err(&pdev->dev, "Failed to get GPIO interrupt.\n");
		return sprd_gpio->irq;
	}

	sprd_gpio->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(sprd_gpio->base))
		return PTR_ERR(sprd_gpio->base);

	spin_lock_init(&sprd_gpio->lock);

	sprd_gpio->chip.label = dev_name(&pdev->dev);
	sprd_gpio->chip.ngpio = SPRD_GPIO_NR;
	sprd_gpio->chip.base = -1;
	sprd_gpio->chip.parent = &pdev->dev;
	sprd_gpio->chip.of_node = pdev->dev.of_node;
	sprd_gpio->chip.request = sprd_gpio_request;
	sprd_gpio->chip.free = sprd_gpio_free;
	sprd_gpio->chip.get = sprd_gpio_get;
	sprd_gpio->chip.set = sprd_gpio_set;
	sprd_gpio->chip.direction_input = sprd_gpio_direction_input;
	sprd_gpio->chip.direction_output = sprd_gpio_direction_output;

	irq = &sprd_gpio->chip.irq;
	irq->chip = &sprd_gpio_irqchip;
	irq->handler = handle_bad_irq;
	irq->default_type = IRQ_TYPE_NONE;
	irq->parent_handler = sprd_gpio_irq_handler;
	irq->parent_handler_data = sprd_gpio;
	irq->num_parents = 1;
	irq->parents = &sprd_gpio->irq;

	ret = devm_gpiochip_add_data(&pdev->dev, &sprd_gpio->chip, sprd_gpio);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could not register gpiochip %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, sprd_gpio);
	return 0;
}

static const struct of_device_id sprd_gpio_of_match[] = {
	{ .compatible = "sprd,sc9860-gpio", },
	{ /* end of list */ }
};
MODULE_DEVICE_TABLE(of, sprd_gpio_of_match);

static struct platform_driver sprd_gpio_driver = {
	.probe = sprd_gpio_probe,
	.driver = {
		.name = "sprd-gpio",
		.of_match_table	= sprd_gpio_of_match,
	},
};

module_platform_driver_probe(sprd_gpio_driver, sprd_gpio_probe);

MODULE_DESCRIPTION("Spreadtrum GPIO driver");
MODULE_LICENSE("GPL v2");
