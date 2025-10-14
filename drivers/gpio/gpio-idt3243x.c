// SPDX-License-Identifier: GPL-2.0
/* Driver for IDT/Renesas 79RC3243x Interrupt Controller  */

#include <linux/bitops.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/generic.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#define IDT_PIC_IRQ_PEND	0x00
#define IDT_PIC_IRQ_MASK	0x08

#define IDT_GPIO_DIR		0x00
#define IDT_GPIO_DATA		0x04
#define IDT_GPIO_ILEVEL		0x08
#define IDT_GPIO_ISTAT		0x0C

struct idt_gpio_ctrl {
	struct gpio_generic_chip chip;
	void __iomem *pic;
	void __iomem *gpio;
	u32 mask_cache;
};

static void idt_gpio_dispatch(struct irq_desc *desc)
{
	struct gpio_chip *gc = irq_desc_get_handler_data(desc);
	struct idt_gpio_ctrl *ctrl = gpiochip_get_data(gc);
	struct irq_chip *host_chip = irq_desc_get_chip(desc);
	unsigned int bit, virq;
	unsigned long pending;

	chained_irq_enter(host_chip, desc);

	pending = readl(ctrl->pic + IDT_PIC_IRQ_PEND);
	pending &= ~ctrl->mask_cache;
	for_each_set_bit(bit, &pending, gc->ngpio) {
		virq = irq_find_mapping(gc->irq.domain, bit);
		if (virq)
			generic_handle_irq(virq);
	}

	chained_irq_exit(host_chip, desc);
}

static int idt_gpio_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct idt_gpio_ctrl *ctrl = gpiochip_get_data(gc);
	unsigned int sense = flow_type & IRQ_TYPE_SENSE_MASK;
	u32 ilevel;

	/* hardware only supports level triggered */
	if (sense == IRQ_TYPE_NONE || (sense & IRQ_TYPE_EDGE_BOTH))
		return -EINVAL;

	guard(gpio_generic_lock_irqsave)(&ctrl->chip);

	ilevel = readl(ctrl->gpio + IDT_GPIO_ILEVEL);
	if (sense & IRQ_TYPE_LEVEL_HIGH)
		ilevel |= BIT(d->hwirq);
	else if (sense & IRQ_TYPE_LEVEL_LOW)
		ilevel &= ~BIT(d->hwirq);

	writel(ilevel, ctrl->gpio + IDT_GPIO_ILEVEL);
	irq_set_handler_locked(d, handle_level_irq);

	return 0;
}

static void idt_gpio_ack(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct idt_gpio_ctrl *ctrl = gpiochip_get_data(gc);

	writel(~BIT(d->hwirq), ctrl->gpio + IDT_GPIO_ISTAT);
}

static void idt_gpio_mask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct idt_gpio_ctrl *ctrl = gpiochip_get_data(gc);

	scoped_guard(gpio_generic_lock_irqsave, &ctrl->chip) {
		ctrl->mask_cache |= BIT(d->hwirq);
		writel(ctrl->mask_cache, ctrl->pic + IDT_PIC_IRQ_MASK);
	}

	gpiochip_disable_irq(gc, irqd_to_hwirq(d));
}

static void idt_gpio_unmask(struct irq_data *d)
{
	struct gpio_chip *gc = irq_data_get_irq_chip_data(d);
	struct idt_gpio_ctrl *ctrl = gpiochip_get_data(gc);

	gpiochip_enable_irq(gc, irqd_to_hwirq(d));

	guard(gpio_generic_lock_irqsave)(&ctrl->chip);

	ctrl->mask_cache &= ~BIT(d->hwirq);
	writel(ctrl->mask_cache, ctrl->pic + IDT_PIC_IRQ_MASK);
}

static int idt_gpio_irq_init_hw(struct gpio_chip *gc)
{
	struct idt_gpio_ctrl *ctrl = gpiochip_get_data(gc);

	/* Mask interrupts. */
	ctrl->mask_cache = 0xffffffff;
	writel(ctrl->mask_cache, ctrl->pic + IDT_PIC_IRQ_MASK);

	return 0;
}

static const struct irq_chip idt_gpio_irqchip = {
	.name = "IDTGPIO",
	.irq_mask = idt_gpio_mask,
	.irq_ack = idt_gpio_ack,
	.irq_unmask = idt_gpio_unmask,
	.irq_set_type = idt_gpio_irq_set_type,
	.flags = IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static int idt_gpio_probe(struct platform_device *pdev)
{
	struct gpio_generic_chip_config config;
	struct device *dev = &pdev->dev;
	struct gpio_irq_chip *girq;
	struct idt_gpio_ctrl *ctrl;
	int parent_irq;
	int ngpios;
	int ret;


	ctrl = devm_kzalloc(dev, sizeof(*ctrl), GFP_KERNEL);
	if (!ctrl)
		return -ENOMEM;

	ctrl->gpio = devm_platform_ioremap_resource_byname(pdev, "gpio");
	if (IS_ERR(ctrl->gpio))
		return PTR_ERR(ctrl->gpio);

	ctrl->chip.gc.parent = dev;

	config = (struct gpio_generic_chip_config) {
		.dev = &pdev->dev,
		.sz = 4,
		.dat = ctrl->gpio + IDT_GPIO_DATA,
		.dirout = ctrl->gpio + IDT_GPIO_DIR,
	};

	ret = gpio_generic_chip_init(&ctrl->chip, &config);
	if (ret) {
		dev_err(dev, "failed to initialize the generic GPIO chip\n");
		return ret;
	}

	ret = device_property_read_u32(dev, "ngpios", &ngpios);
	if (!ret)
		ctrl->chip.gc.ngpio = ngpios;

	if (device_property_read_bool(dev, "interrupt-controller")) {
		ctrl->pic = devm_platform_ioremap_resource_byname(pdev, "pic");
		if (IS_ERR(ctrl->pic))
			return PTR_ERR(ctrl->pic);

		parent_irq = platform_get_irq(pdev, 0);
		if (parent_irq < 0)
			return parent_irq;

		girq = &ctrl->chip.gc.irq;
		gpio_irq_chip_set_chip(girq, &idt_gpio_irqchip);
		girq->init_hw = idt_gpio_irq_init_hw;
		girq->parent_handler = idt_gpio_dispatch;
		girq->num_parents = 1;
		girq->parents = devm_kcalloc(dev, girq->num_parents,
					     sizeof(*girq->parents),
					     GFP_KERNEL);
		if (!girq->parents)
			return -ENOMEM;

		girq->parents[0] = parent_irq;
		girq->default_type = IRQ_TYPE_NONE;
		girq->handler = handle_bad_irq;
	}

	return devm_gpiochip_add_data(&pdev->dev, &ctrl->chip.gc, ctrl);
}

static const struct of_device_id idt_gpio_of_match[] = {
	{ .compatible = "idt,32434-gpio" },
	{ }
};
MODULE_DEVICE_TABLE(of, idt_gpio_of_match);

static struct platform_driver idt_gpio_driver = {
	.probe = idt_gpio_probe,
	.driver = {
		.name = "idt3243x-gpio",
		.of_match_table = idt_gpio_of_match,
	},
};
module_platform_driver(idt_gpio_driver);

MODULE_DESCRIPTION("IDT 79RC3243x GPIO/PIC Driver");
MODULE_AUTHOR("Thomas Bogendoerfer <tsbogend@alpha.franken.de>");
MODULE_LICENSE("GPL");
