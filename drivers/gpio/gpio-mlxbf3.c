// SPDX-License-Identifier: GPL-2.0-only or BSD-3-Clause
/* Copyright (C) 2022 NVIDIA CORPORATION & AFFILIATES */

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/*
 * There are 2 YU GPIO blocks:
 * gpio[0]: HOST_GPIO0->HOST_GPIO31
 * gpio[1]: HOST_GPIO32->HOST_GPIO55
 */
#define MLXBF3_GPIO_MAX_PINS_PER_BLOCK 32

/*
 * fw_gpio[x] block registers and their offset
 */
#define MLXBF_GPIO_FW_OUTPUT_ENABLE_SET	  0x00
#define MLXBF_GPIO_FW_DATA_OUT_SET        0x04

#define MLXBF_GPIO_FW_OUTPUT_ENABLE_CLEAR 0x00
#define MLXBF_GPIO_FW_DATA_OUT_CLEAR      0x04

#define MLXBF_GPIO_CAUSE_RISE_EN          0x00
#define MLXBF_GPIO_CAUSE_FALL_EN          0x04
#define MLXBF_GPIO_READ_DATA_IN           0x08

#define MLXBF_GPIO_CAUSE_OR_CAUSE_EVTEN0  0x00
#define MLXBF_GPIO_CAUSE_OR_EVTEN0        0x14
#define MLXBF_GPIO_CAUSE_OR_CLRCAUSE      0x18

struct mlxbf3_gpio_context {
	struct gpio_chip gc;

	/* YU GPIO block address */
	void __iomem *gpio_set_io;
	void __iomem *gpio_clr_io;
	void __iomem *gpio_io;

	/* YU GPIO cause block address */
	void __iomem *gpio_cause_io;
};

static void mlxbf3_gpio_irq_enable(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct mlxbf3_gpio_context *gs = gpiochip_get_data(gc);
	irq_hw_number_t offset = irqd_to_hwirq(irqd);
	unsigned long flags;
	u32 val;

	gpiochip_enable_irq(gc, offset);

	raw_spin_lock_irqsave(&gs->gc.bgpio_lock, flags);
	writel(BIT(offset), gs->gpio_cause_io + MLXBF_GPIO_CAUSE_OR_CLRCAUSE);

	val = readl(gs->gpio_cause_io + MLXBF_GPIO_CAUSE_OR_EVTEN0);
	val |= BIT(offset);
	writel(val, gs->gpio_cause_io + MLXBF_GPIO_CAUSE_OR_EVTEN0);
	raw_spin_unlock_irqrestore(&gs->gc.bgpio_lock, flags);
}

static void mlxbf3_gpio_irq_disable(struct irq_data *irqd)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct mlxbf3_gpio_context *gs = gpiochip_get_data(gc);
	irq_hw_number_t offset = irqd_to_hwirq(irqd);
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&gs->gc.bgpio_lock, flags);
	val = readl(gs->gpio_cause_io + MLXBF_GPIO_CAUSE_OR_EVTEN0);
	val &= ~BIT(offset);
	writel(val, gs->gpio_cause_io + MLXBF_GPIO_CAUSE_OR_EVTEN0);
	raw_spin_unlock_irqrestore(&gs->gc.bgpio_lock, flags);

	gpiochip_disable_irq(gc, offset);
}

static irqreturn_t mlxbf3_gpio_irq_handler(int irq, void *ptr)
{
	struct mlxbf3_gpio_context *gs = ptr;
	struct gpio_chip *gc = &gs->gc;
	unsigned long pending;
	u32 level;

	pending = readl(gs->gpio_cause_io + MLXBF_GPIO_CAUSE_OR_CAUSE_EVTEN0);
	writel(pending, gs->gpio_cause_io + MLXBF_GPIO_CAUSE_OR_CLRCAUSE);

	for_each_set_bit(level, &pending, gc->ngpio)
		generic_handle_domain_irq(gc->irq.domain, level);

	return IRQ_RETVAL(pending);
}

static int
mlxbf3_gpio_irq_set_type(struct irq_data *irqd, unsigned int type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(irqd);
	struct mlxbf3_gpio_context *gs = gpiochip_get_data(gc);
	irq_hw_number_t offset = irqd_to_hwirq(irqd);
	unsigned long flags;
	u32 val;

	raw_spin_lock_irqsave(&gs->gc.bgpio_lock, flags);

	switch (type & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_EDGE_BOTH:
		val = readl(gs->gpio_io + MLXBF_GPIO_CAUSE_FALL_EN);
		val |= BIT(offset);
		writel(val, gs->gpio_io + MLXBF_GPIO_CAUSE_FALL_EN);
		val = readl(gs->gpio_io + MLXBF_GPIO_CAUSE_RISE_EN);
		val |= BIT(offset);
		writel(val, gs->gpio_io + MLXBF_GPIO_CAUSE_RISE_EN);
		break;
	case IRQ_TYPE_EDGE_RISING:
		val = readl(gs->gpio_io + MLXBF_GPIO_CAUSE_RISE_EN);
		val |= BIT(offset);
		writel(val, gs->gpio_io + MLXBF_GPIO_CAUSE_RISE_EN);
		break;
	case IRQ_TYPE_EDGE_FALLING:
		val = readl(gs->gpio_io + MLXBF_GPIO_CAUSE_FALL_EN);
		val |= BIT(offset);
		writel(val, gs->gpio_io + MLXBF_GPIO_CAUSE_FALL_EN);
		break;
	default:
		raw_spin_unlock_irqrestore(&gs->gc.bgpio_lock, flags);
		return -EINVAL;
	}

	raw_spin_unlock_irqrestore(&gs->gc.bgpio_lock, flags);

	irq_set_handler_locked(irqd, handle_edge_irq);

	return 0;
}

/* This function needs to be defined for handle_edge_irq() */
static void mlxbf3_gpio_irq_ack(struct irq_data *data)
{
}

static const struct irq_chip gpio_mlxbf3_irqchip = {
	.name = "MLNXBF33",
	.irq_ack = mlxbf3_gpio_irq_ack,
	.irq_set_type = mlxbf3_gpio_irq_set_type,
	.irq_enable = mlxbf3_gpio_irq_enable,
	.irq_disable = mlxbf3_gpio_irq_disable,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int mlxbf3_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mlxbf3_gpio_context *gs;
	struct gpio_irq_chip *girq;
	struct gpio_chip *gc;
	int ret, irq;

	gs = devm_kzalloc(dev, sizeof(*gs), GFP_KERNEL);
	if (!gs)
		return -ENOMEM;

	gs->gpio_io = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(gs->gpio_io))
		return PTR_ERR(gs->gpio_io);

	gs->gpio_cause_io = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(gs->gpio_cause_io))
		return PTR_ERR(gs->gpio_cause_io);

	gs->gpio_set_io = devm_platform_ioremap_resource(pdev, 2);
	if (IS_ERR(gs->gpio_set_io))
		return PTR_ERR(gs->gpio_set_io);

	gs->gpio_clr_io = devm_platform_ioremap_resource(pdev, 3);
	if (IS_ERR(gs->gpio_clr_io))
		return PTR_ERR(gs->gpio_clr_io);
	gc = &gs->gc;

	ret = bgpio_init(gc, dev, 4,
			gs->gpio_io + MLXBF_GPIO_READ_DATA_IN,
			gs->gpio_set_io + MLXBF_GPIO_FW_DATA_OUT_SET,
			gs->gpio_clr_io + MLXBF_GPIO_FW_DATA_OUT_CLEAR,
			gs->gpio_set_io + MLXBF_GPIO_FW_OUTPUT_ENABLE_SET,
			gs->gpio_clr_io + MLXBF_GPIO_FW_OUTPUT_ENABLE_CLEAR, 0);

	gc->request = gpiochip_generic_request;
	gc->free = gpiochip_generic_free;
	gc->owner = THIS_MODULE;

	irq = platform_get_irq(pdev, 0);
	if (irq >= 0) {
		girq = &gs->gc.irq;
		gpio_irq_chip_set_chip(girq, &gpio_mlxbf3_irqchip);
		girq->default_type = IRQ_TYPE_NONE;
		/* This will let us handle the parent IRQ in the driver */
		girq->num_parents = 0;
		girq->parents = NULL;
		girq->parent_handler = NULL;
		girq->handler = handle_bad_irq;

		/*
		 * Directly request the irq here instead of passing
		 * a flow-handler because the irq is shared.
		 */
		ret = devm_request_irq(dev, irq, mlxbf3_gpio_irq_handler,
				       IRQF_SHARED, dev_name(dev), gs);
		if (ret)
			return dev_err_probe(dev, ret, "failed to request IRQ");
	}

	platform_set_drvdata(pdev, gs);

	ret = devm_gpiochip_add_data(dev, &gs->gc, gs);
	if (ret)
		dev_err_probe(dev, ret, "Failed adding memory mapped gpiochip\n");

	return 0;
}

static const struct acpi_device_id mlxbf3_gpio_acpi_match[] = {
	{ "MLNXBF33", 0 },
	{}
};
MODULE_DEVICE_TABLE(acpi, mlxbf3_gpio_acpi_match);

static struct platform_driver mlxbf3_gpio_driver = {
	.driver = {
		.name = "mlxbf3_gpio",
		.acpi_match_table = mlxbf3_gpio_acpi_match,
	},
	.probe    = mlxbf3_gpio_probe,
};
module_platform_driver(mlxbf3_gpio_driver);

MODULE_DESCRIPTION("NVIDIA BlueField-3 GPIO Driver");
MODULE_AUTHOR("Asmaa Mnebhi <asmaa@nvidia.com>");
MODULE_LICENSE("Dual BSD/GPL");
