// SPDX-License-Identifier: GPL-2.0+
// Copyright (C) 2008-2009 The GameCube Linux Team
// Copyright (C) 2008,2009 Albert Herranz
// Copyright (C) 2017-2018 Jonathan Neuschäfer
//
// Nintendo Wii (Hollywood) GPIO driver

#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/slab.h>

/*
 * Register names and offsets courtesy of WiiBrew:
 * https://wiibrew.org/wiki/Hardware/Hollywood_GPIOs
 *
 * Note that for most registers, there are two versions:
 * - HW_GPIOB_* Is always accessible by the Broadway PowerPC core, but does
 *   always give access to all GPIO lines
 * - HW_GPIO_* Is only accessible by the Broadway PowerPC code if the memory
 *   firewall (AHBPROT) in the Hollywood chipset has been configured to allow
 *   such access.
 *
 * The ownership of each GPIO line can be configured in the HW_GPIO_OWNER
 * register: A one bit configures the line for access via the HW_GPIOB_*
 * registers, a zero bit indicates access via HW_GPIO_*. This driver uses
 * HW_GPIOB_*.
 */
#define HW_GPIOB_OUT		0x00
#define HW_GPIOB_DIR		0x04
#define HW_GPIOB_IN		0x08
#define HW_GPIOB_INTLVL		0x0c
#define HW_GPIOB_INTFLAG	0x10
#define HW_GPIOB_INTMASK	0x14
#define HW_GPIOB_INMIR		0x18
#define HW_GPIO_ENABLE		0x1c
#define HW_GPIO_OUT		0x20
#define HW_GPIO_DIR		0x24
#define HW_GPIO_IN		0x28
#define HW_GPIO_INTLVL		0x2c
#define HW_GPIO_INTFLAG		0x30
#define HW_GPIO_INTMASK		0x34
#define HW_GPIO_INMIR		0x38
#define HW_GPIO_OWNER		0x3c

struct hlwd_gpio {
	struct gpio_chip gpioc;
	struct irq_chip irqc;
	void __iomem *regs;
	int irq;
	u32 edge_emulation;
	u32 rising_edge, falling_edge;
};

static void hlwd_gpio_irqhandler(struct irq_desc *desc)
{
	struct hlwd_gpio *hlwd =
		gpiochip_get_data(irq_desc_get_handler_data(desc));
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned long flags;
	unsigned long pending;
	int hwirq;
	u32 emulated_pending;

	spin_lock_irqsave(&hlwd->gpioc.bgpio_lock, flags);
	pending = ioread32be(hlwd->regs + HW_GPIOB_INTFLAG);
	pending &= ioread32be(hlwd->regs + HW_GPIOB_INTMASK);

	/* Treat interrupts due to edge trigger emulation separately */
	emulated_pending = hlwd->edge_emulation & pending;
	pending &= ~emulated_pending;
	if (emulated_pending) {
		u32 level, rising, falling;

		level = ioread32be(hlwd->regs + HW_GPIOB_INTLVL);
		rising = level & emulated_pending;
		falling = ~level & emulated_pending;

		/* Invert the levels */
		iowrite32be(level ^ emulated_pending,
			    hlwd->regs + HW_GPIOB_INTLVL);

		/* Ack all emulated-edge interrupts */
		iowrite32be(emulated_pending, hlwd->regs + HW_GPIOB_INTFLAG);

		/* Signal interrupts only on the correct edge */
		rising &= hlwd->rising_edge;
		falling &= hlwd->falling_edge;

		/* Mark emulated interrupts as pending */
		pending |= rising | falling;
	}
	spin_unlock_irqrestore(&hlwd->gpioc.bgpio_lock, flags);

	chained_irq_enter(chip, desc);

	for_each_set_bit(hwirq, &pending, 32)
		generic_handle_domain_irq(hlwd->gpioc.irq.domain, hwirq);

	chained_irq_exit(chip, desc);
}

static void hlwd_gpio_irq_ack(struct irq_data *data)
{
	struct hlwd_gpio *hlwd =
		gpiochip_get_data(irq_data_get_irq_chip_data(data));

	iowrite32be(BIT(data->hwirq), hlwd->regs + HW_GPIOB_INTFLAG);
}

static void hlwd_gpio_irq_mask(struct irq_data *data)
{
	struct hlwd_gpio *hlwd =
		gpiochip_get_data(irq_data_get_irq_chip_data(data));
	unsigned long flags;
	u32 mask;

	spin_lock_irqsave(&hlwd->gpioc.bgpio_lock, flags);
	mask = ioread32be(hlwd->regs + HW_GPIOB_INTMASK);
	mask &= ~BIT(data->hwirq);
	iowrite32be(mask, hlwd->regs + HW_GPIOB_INTMASK);
	spin_unlock_irqrestore(&hlwd->gpioc.bgpio_lock, flags);
}

static void hlwd_gpio_irq_unmask(struct irq_data *data)
{
	struct hlwd_gpio *hlwd =
		gpiochip_get_data(irq_data_get_irq_chip_data(data));
	unsigned long flags;
	u32 mask;

	spin_lock_irqsave(&hlwd->gpioc.bgpio_lock, flags);
	mask = ioread32be(hlwd->regs + HW_GPIOB_INTMASK);
	mask |= BIT(data->hwirq);
	iowrite32be(mask, hlwd->regs + HW_GPIOB_INTMASK);
	spin_unlock_irqrestore(&hlwd->gpioc.bgpio_lock, flags);
}

static void hlwd_gpio_irq_enable(struct irq_data *data)
{
	hlwd_gpio_irq_ack(data);
	hlwd_gpio_irq_unmask(data);
}

static void hlwd_gpio_irq_setup_emulation(struct hlwd_gpio *hlwd, int hwirq,
					  unsigned int flow_type)
{
	u32 level, state;

	/* Set the trigger level to the inactive level */
	level = ioread32be(hlwd->regs + HW_GPIOB_INTLVL);
	state = ioread32be(hlwd->regs + HW_GPIOB_IN) & BIT(hwirq);
	level &= ~BIT(hwirq);
	level |= state ^ BIT(hwirq);
	iowrite32be(level, hlwd->regs + HW_GPIOB_INTLVL);

	hlwd->edge_emulation |= BIT(hwirq);
	hlwd->rising_edge &= ~BIT(hwirq);
	hlwd->falling_edge &= ~BIT(hwirq);
	if (flow_type & IRQ_TYPE_EDGE_RISING)
		hlwd->rising_edge |= BIT(hwirq);
	if (flow_type & IRQ_TYPE_EDGE_FALLING)
		hlwd->falling_edge |= BIT(hwirq);
}

static int hlwd_gpio_irq_set_type(struct irq_data *data, unsigned int flow_type)
{
	struct hlwd_gpio *hlwd =
		gpiochip_get_data(irq_data_get_irq_chip_data(data));
	unsigned long flags;
	u32 level;

	spin_lock_irqsave(&hlwd->gpioc.bgpio_lock, flags);

	hlwd->edge_emulation &= ~BIT(data->hwirq);

	switch (flow_type) {
	case IRQ_TYPE_LEVEL_HIGH:
		level = ioread32be(hlwd->regs + HW_GPIOB_INTLVL);
		level |= BIT(data->hwirq);
		iowrite32be(level, hlwd->regs + HW_GPIOB_INTLVL);
		break;
	case IRQ_TYPE_LEVEL_LOW:
		level = ioread32be(hlwd->regs + HW_GPIOB_INTLVL);
		level &= ~BIT(data->hwirq);
		iowrite32be(level, hlwd->regs + HW_GPIOB_INTLVL);
		break;
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_BOTH:
		hlwd_gpio_irq_setup_emulation(hlwd, data->hwirq, flow_type);
		break;
	default:
		spin_unlock_irqrestore(&hlwd->gpioc.bgpio_lock, flags);
		return -EINVAL;
	}

	spin_unlock_irqrestore(&hlwd->gpioc.bgpio_lock, flags);
	return 0;
}

static int hlwd_gpio_probe(struct platform_device *pdev)
{
	struct hlwd_gpio *hlwd;
	u32 ngpios;
	int res;

	hlwd = devm_kzalloc(&pdev->dev, sizeof(*hlwd), GFP_KERNEL);
	if (!hlwd)
		return -ENOMEM;

	hlwd->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(hlwd->regs))
		return PTR_ERR(hlwd->regs);

	/*
	 * Claim all GPIOs using the OWNER register. This will not work on
	 * systems where the AHBPROT memory firewall hasn't been configured to
	 * permit PPC access to HW_GPIO_*.
	 *
	 * Note that this has to happen before bgpio_init reads the
	 * HW_GPIOB_OUT and HW_GPIOB_DIR, because otherwise it reads the wrong
	 * values.
	 */
	iowrite32be(0xffffffff, hlwd->regs + HW_GPIO_OWNER);

	res = bgpio_init(&hlwd->gpioc, &pdev->dev, 4,
			hlwd->regs + HW_GPIOB_IN, hlwd->regs + HW_GPIOB_OUT,
			NULL, hlwd->regs + HW_GPIOB_DIR, NULL,
			BGPIOF_BIG_ENDIAN_BYTE_ORDER);
	if (res < 0) {
		dev_warn(&pdev->dev, "bgpio_init failed: %d\n", res);
		return res;
	}

	res = of_property_read_u32(pdev->dev.of_node, "ngpios", &ngpios);
	if (res)
		ngpios = 32;
	hlwd->gpioc.ngpio = ngpios;

	/* Mask and ack all interrupts */
	iowrite32be(0, hlwd->regs + HW_GPIOB_INTMASK);
	iowrite32be(0xffffffff, hlwd->regs + HW_GPIOB_INTFLAG);

	/*
	 * If this GPIO controller is not marked as an interrupt controller in
	 * the DT, skip interrupt support.
	 */
	if (of_property_read_bool(pdev->dev.of_node, "interrupt-controller")) {
		struct gpio_irq_chip *girq;

		hlwd->irq = platform_get_irq(pdev, 0);
		if (hlwd->irq < 0) {
			dev_info(&pdev->dev, "platform_get_irq returned %d\n",
				 hlwd->irq);
			return hlwd->irq;
		}

		hlwd->irqc.name = dev_name(&pdev->dev);
		hlwd->irqc.irq_mask = hlwd_gpio_irq_mask;
		hlwd->irqc.irq_unmask = hlwd_gpio_irq_unmask;
		hlwd->irqc.irq_enable = hlwd_gpio_irq_enable;
		hlwd->irqc.irq_set_type = hlwd_gpio_irq_set_type;

		girq = &hlwd->gpioc.irq;
		girq->chip = &hlwd->irqc;
		girq->parent_handler = hlwd_gpio_irqhandler;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(&pdev->dev, 1,
					     sizeof(*girq->parents),
					     GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;
		girq->parents[0] = hlwd->irq;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_level_irq;
	}

	return devm_gpiochip_add_data(&pdev->dev, &hlwd->gpioc, hlwd);
}

static const struct of_device_id hlwd_gpio_match[] = {
	{ .compatible = "nintendo,hollywood-gpio", },
	{},
};
MODULE_DEVICE_TABLE(of, hlwd_gpio_match);

static struct platform_driver hlwd_gpio_driver = {
	.driver	= {
		.name		= "gpio-hlwd",
		.of_match_table	= hlwd_gpio_match,
	},
	.probe	= hlwd_gpio_probe,
};
module_platform_driver(hlwd_gpio_driver);

MODULE_AUTHOR("Jonathan Neuschäfer <j.neuschaefer@gmx.net>");
MODULE_DESCRIPTION("Nintendo Wii GPIO driver");
MODULE_LICENSE("GPL");
