/*
 * Copyright (c) 2016-2017 NVIDIA Corporation
 *
 * Author: Thierry Reding <treding@nvidia.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <dt-bindings/gpio/tegra186-gpio.h>

#define TEGRA186_GPIO_ENABLE_CONFIG 0x00
#define  TEGRA186_GPIO_ENABLE_CONFIG_ENABLE BIT(0)
#define  TEGRA186_GPIO_ENABLE_CONFIG_OUT BIT(1)
#define  TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_NONE (0x0 << 2)
#define  TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_LEVEL (0x1 << 2)
#define  TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_SINGLE_EDGE (0x2 << 2)
#define  TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_DOUBLE_EDGE (0x3 << 2)
#define  TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_MASK (0x3 << 2)
#define  TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_LEVEL BIT(4)
#define  TEGRA186_GPIO_ENABLE_CONFIG_INTERRUPT BIT(6)

#define TEGRA186_GPIO_DEBOUNCE_CONTROL 0x04
#define  TEGRA186_GPIO_DEBOUNCE_CONTROL_THRESHOLD(x) ((x) & 0xff)

#define TEGRA186_GPIO_INPUT 0x08
#define  TEGRA186_GPIO_INPUT_HIGH BIT(0)

#define TEGRA186_GPIO_OUTPUT_CONTROL 0x0c
#define  TEGRA186_GPIO_OUTPUT_CONTROL_FLOATED BIT(0)

#define TEGRA186_GPIO_OUTPUT_VALUE 0x10
#define  TEGRA186_GPIO_OUTPUT_VALUE_HIGH BIT(0)

#define TEGRA186_GPIO_INTERRUPT_CLEAR 0x14

#define TEGRA186_GPIO_INTERRUPT_STATUS(x) (0x100 + (x) * 4)

struct tegra_gpio_port {
	const char *name;
	unsigned int offset;
	unsigned int pins;
	unsigned int irq;
};

struct tegra_gpio_soc {
	const struct tegra_gpio_port *ports;
	unsigned int num_ports;
	const char *name;
};

struct tegra_gpio {
	struct gpio_chip gpio;
	struct irq_chip intc;
	unsigned int num_irq;
	unsigned int *irq;

	const struct tegra_gpio_soc *soc;

	void __iomem *base;
};

static const struct tegra_gpio_port *
tegra186_gpio_get_port(struct tegra_gpio *gpio, unsigned int *pin)
{
	unsigned int start = 0, i;

	for (i = 0; i < gpio->soc->num_ports; i++) {
		const struct tegra_gpio_port *port = &gpio->soc->ports[i];

		if (*pin >= start && *pin < start + port->pins) {
			*pin -= start;
			return port;
		}

		start += port->pins;
	}

	return NULL;
}

static void __iomem *tegra186_gpio_get_base(struct tegra_gpio *gpio,
					    unsigned int pin)
{
	const struct tegra_gpio_port *port;

	port = tegra186_gpio_get_port(gpio, &pin);
	if (!port)
		return NULL;

	return gpio->base + port->offset + pin * 0x20;
}

static int tegra186_gpio_get_direction(struct gpio_chip *chip,
				       unsigned int offset)
{
	struct tegra_gpio *gpio = gpiochip_get_data(chip);
	void __iomem *base;
	u32 value;

	base = tegra186_gpio_get_base(gpio, offset);
	if (WARN_ON(base == NULL))
		return -ENODEV;

	value = readl(base + TEGRA186_GPIO_ENABLE_CONFIG);
	if (value & TEGRA186_GPIO_ENABLE_CONFIG_OUT)
		return 0;

	return 1;
}

static int tegra186_gpio_direction_input(struct gpio_chip *chip,
					 unsigned int offset)
{
	struct tegra_gpio *gpio = gpiochip_get_data(chip);
	void __iomem *base;
	u32 value;

	base = tegra186_gpio_get_base(gpio, offset);
	if (WARN_ON(base == NULL))
		return -ENODEV;

	value = readl(base + TEGRA186_GPIO_OUTPUT_CONTROL);
	value |= TEGRA186_GPIO_OUTPUT_CONTROL_FLOATED;
	writel(value, base + TEGRA186_GPIO_OUTPUT_CONTROL);

	value = readl(base + TEGRA186_GPIO_ENABLE_CONFIG);
	value |= TEGRA186_GPIO_ENABLE_CONFIG_ENABLE;
	value &= ~TEGRA186_GPIO_ENABLE_CONFIG_OUT;
	writel(value, base + TEGRA186_GPIO_ENABLE_CONFIG);

	return 0;
}

static int tegra186_gpio_direction_output(struct gpio_chip *chip,
					  unsigned int offset, int level)
{
	struct tegra_gpio *gpio = gpiochip_get_data(chip);
	void __iomem *base;
	u32 value;

	/* configure output level first */
	chip->set(chip, offset, level);

	base = tegra186_gpio_get_base(gpio, offset);
	if (WARN_ON(base == NULL))
		return -EINVAL;

	/* set the direction */
	value = readl(base + TEGRA186_GPIO_OUTPUT_CONTROL);
	value &= ~TEGRA186_GPIO_OUTPUT_CONTROL_FLOATED;
	writel(value, base + TEGRA186_GPIO_OUTPUT_CONTROL);

	value = readl(base + TEGRA186_GPIO_ENABLE_CONFIG);
	value |= TEGRA186_GPIO_ENABLE_CONFIG_ENABLE;
	value |= TEGRA186_GPIO_ENABLE_CONFIG_OUT;
	writel(value, base + TEGRA186_GPIO_ENABLE_CONFIG);

	return 0;
}

static int tegra186_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct tegra_gpio *gpio = gpiochip_get_data(chip);
	void __iomem *base;
	u32 value;

	base = tegra186_gpio_get_base(gpio, offset);
	if (WARN_ON(base == NULL))
		return -ENODEV;

	value = readl(base + TEGRA186_GPIO_ENABLE_CONFIG);
	if (value & TEGRA186_GPIO_ENABLE_CONFIG_OUT)
		value = readl(base + TEGRA186_GPIO_OUTPUT_VALUE);
	else
		value = readl(base + TEGRA186_GPIO_INPUT);

	return value & BIT(0);
}

static void tegra186_gpio_set(struct gpio_chip *chip, unsigned int offset,
			      int level)
{
	struct tegra_gpio *gpio = gpiochip_get_data(chip);
	void __iomem *base;
	u32 value;

	base = tegra186_gpio_get_base(gpio, offset);
	if (WARN_ON(base == NULL))
		return;

	value = readl(base + TEGRA186_GPIO_OUTPUT_VALUE);
	if (level == 0)
		value &= ~TEGRA186_GPIO_OUTPUT_VALUE_HIGH;
	else
		value |= TEGRA186_GPIO_OUTPUT_VALUE_HIGH;

	writel(value, base + TEGRA186_GPIO_OUTPUT_VALUE);
}

static int tegra186_gpio_of_xlate(struct gpio_chip *chip,
				  const struct of_phandle_args *spec,
				  u32 *flags)
{
	struct tegra_gpio *gpio = gpiochip_get_data(chip);
	unsigned int port, pin, i, offset = 0;

	if (WARN_ON(chip->of_gpio_n_cells < 2))
		return -EINVAL;

	if (WARN_ON(spec->args_count < chip->of_gpio_n_cells))
		return -EINVAL;

	port = spec->args[0] / 8;
	pin = spec->args[0] % 8;

	if (port >= gpio->soc->num_ports) {
		dev_err(chip->parent, "invalid port number: %u\n", port);
		return -EINVAL;
	}

	for (i = 0; i < port; i++)
		offset += gpio->soc->ports[i].pins;

	if (flags)
		*flags = spec->args[1];

	return offset + pin;
}

static void tegra186_irq_ack(struct irq_data *data)
{
	struct tegra_gpio *gpio = irq_data_get_irq_chip_data(data);
	void __iomem *base;

	base = tegra186_gpio_get_base(gpio, data->hwirq);
	if (WARN_ON(base == NULL))
		return;

	writel(1, base + TEGRA186_GPIO_INTERRUPT_CLEAR);
}

static void tegra186_irq_mask(struct irq_data *data)
{
	struct tegra_gpio *gpio = irq_data_get_irq_chip_data(data);
	void __iomem *base;
	u32 value;

	base = tegra186_gpio_get_base(gpio, data->hwirq);
	if (WARN_ON(base == NULL))
		return;

	value = readl(base + TEGRA186_GPIO_ENABLE_CONFIG);
	value &= ~TEGRA186_GPIO_ENABLE_CONFIG_INTERRUPT;
	writel(value, base + TEGRA186_GPIO_ENABLE_CONFIG);
}

static void tegra186_irq_unmask(struct irq_data *data)
{
	struct tegra_gpio *gpio = irq_data_get_irq_chip_data(data);
	void __iomem *base;
	u32 value;

	base = tegra186_gpio_get_base(gpio, data->hwirq);
	if (WARN_ON(base == NULL))
		return;

	value = readl(base + TEGRA186_GPIO_ENABLE_CONFIG);
	value |= TEGRA186_GPIO_ENABLE_CONFIG_INTERRUPT;
	writel(value, base + TEGRA186_GPIO_ENABLE_CONFIG);
}

static int tegra186_irq_set_type(struct irq_data *data, unsigned int flow)
{
	struct tegra_gpio *gpio = irq_data_get_irq_chip_data(data);
	void __iomem *base;
	u32 value;

	base = tegra186_gpio_get_base(gpio, data->hwirq);
	if (WARN_ON(base == NULL))
		return -ENODEV;

	value = readl(base + TEGRA186_GPIO_ENABLE_CONFIG);
	value &= ~TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_MASK;
	value &= ~TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_LEVEL;

	switch (flow & IRQ_TYPE_SENSE_MASK) {
	case IRQ_TYPE_NONE:
		break;

	case IRQ_TYPE_EDGE_RISING:
		value |= TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_SINGLE_EDGE;
		value |= TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_LEVEL;
		break;

	case IRQ_TYPE_EDGE_FALLING:
		value |= TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_SINGLE_EDGE;
		break;

	case IRQ_TYPE_EDGE_BOTH:
		value |= TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_DOUBLE_EDGE;
		break;

	case IRQ_TYPE_LEVEL_HIGH:
		value |= TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_LEVEL;
		value |= TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_LEVEL;
		break;

	case IRQ_TYPE_LEVEL_LOW:
		value |= TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_LEVEL;
		break;

	default:
		return -EINVAL;
	}

	writel(value, base + TEGRA186_GPIO_ENABLE_CONFIG);

	if ((flow & IRQ_TYPE_EDGE_BOTH) == 0)
		irq_set_handler_locked(data, handle_level_irq);
	else
		irq_set_handler_locked(data, handle_edge_irq);

	return 0;
}

static void tegra186_gpio_irq(struct irq_desc *desc)
{
	struct tegra_gpio *gpio = irq_desc_get_handler_data(desc);
	struct irq_domain *domain = gpio->gpio.irq.domain;
	struct irq_chip *chip = irq_desc_get_chip(desc);
	unsigned int parent = irq_desc_get_irq(desc);
	unsigned int i, offset = 0;

	chained_irq_enter(chip, desc);

	for (i = 0; i < gpio->soc->num_ports; i++) {
		const struct tegra_gpio_port *port = &gpio->soc->ports[i];
		void __iomem *base = gpio->base + port->offset;
		unsigned int pin, irq;
		unsigned long value;

		/* skip ports that are not associated with this controller */
		if (parent != gpio->irq[port->irq])
			goto skip;

		value = readl(base + TEGRA186_GPIO_INTERRUPT_STATUS(1));

		for_each_set_bit(pin, &value, port->pins) {
			irq = irq_find_mapping(domain, offset + pin);
			if (WARN_ON(irq == 0))
				continue;

			generic_handle_irq(irq);
		}

skip:
		offset += port->pins;
	}

	chained_irq_exit(chip, desc);
}

static int tegra186_gpio_irq_domain_xlate(struct irq_domain *domain,
					  struct device_node *np,
					  const u32 *spec, unsigned int size,
					  unsigned long *hwirq,
					  unsigned int *type)
{
	struct tegra_gpio *gpio = gpiochip_get_data(domain->host_data);
	unsigned int port, pin, i, offset = 0;

	if (size < 2)
		return -EINVAL;

	port = spec[0] / 8;
	pin = spec[0] % 8;

	if (port >= gpio->soc->num_ports) {
		dev_err(gpio->gpio.parent, "invalid port number: %u\n", port);
		return -EINVAL;
	}

	for (i = 0; i < port; i++)
		offset += gpio->soc->ports[i].pins;

	*type = spec[1] & IRQ_TYPE_SENSE_MASK;
	*hwirq = offset + pin;

	return 0;
}

static const struct irq_domain_ops tegra186_gpio_irq_domain_ops = {
	.map = gpiochip_irq_map,
	.unmap = gpiochip_irq_unmap,
	.xlate = tegra186_gpio_irq_domain_xlate,
};

static int tegra186_gpio_probe(struct platform_device *pdev)
{
	unsigned int i, j, offset;
	struct gpio_irq_chip *irq;
	struct tegra_gpio *gpio;
	struct resource *res;
	char **names;
	int err;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->soc = of_device_get_match_data(&pdev->dev);

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "gpio");
	gpio->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(gpio->base))
		return PTR_ERR(gpio->base);

	err = platform_irq_count(pdev);
	if (err < 0)
		return err;

	gpio->num_irq = err;

	gpio->irq = devm_kcalloc(&pdev->dev, gpio->num_irq, sizeof(*gpio->irq),
				 GFP_KERNEL);
	if (!gpio->irq)
		return -ENOMEM;

	for (i = 0; i < gpio->num_irq; i++) {
		err = platform_get_irq(pdev, i);
		if (err < 0)
			return err;

		gpio->irq[i] = err;
	}

	gpio->gpio.label = gpio->soc->name;
	gpio->gpio.parent = &pdev->dev;

	gpio->gpio.get_direction = tegra186_gpio_get_direction;
	gpio->gpio.direction_input = tegra186_gpio_direction_input;
	gpio->gpio.direction_output = tegra186_gpio_direction_output;
	gpio->gpio.get = tegra186_gpio_get,
	gpio->gpio.set = tegra186_gpio_set;

	gpio->gpio.base = -1;

	for (i = 0; i < gpio->soc->num_ports; i++)
		gpio->gpio.ngpio += gpio->soc->ports[i].pins;

	names = devm_kcalloc(gpio->gpio.parent, gpio->gpio.ngpio,
			     sizeof(*names), GFP_KERNEL);
	if (!names)
		return -ENOMEM;

	for (i = 0, offset = 0; i < gpio->soc->num_ports; i++) {
		const struct tegra_gpio_port *port = &gpio->soc->ports[i];
		char *name;

		for (j = 0; j < port->pins; j++) {
			name = devm_kasprintf(gpio->gpio.parent, GFP_KERNEL,
					      "P%s.%02x", port->name, j);
			if (!name)
				return -ENOMEM;

			names[offset + j] = name;
		}

		offset += port->pins;
	}

	gpio->gpio.names = (const char * const *)names;

	gpio->gpio.of_node = pdev->dev.of_node;
	gpio->gpio.of_gpio_n_cells = 2;
	gpio->gpio.of_xlate = tegra186_gpio_of_xlate;

	gpio->intc.name = pdev->dev.of_node->name;
	gpio->intc.irq_ack = tegra186_irq_ack;
	gpio->intc.irq_mask = tegra186_irq_mask;
	gpio->intc.irq_unmask = tegra186_irq_unmask;
	gpio->intc.irq_set_type = tegra186_irq_set_type;

	irq = &gpio->gpio.irq;
	irq->chip = &gpio->intc;
	irq->domain_ops = &tegra186_gpio_irq_domain_ops;
	irq->handler = handle_simple_irq;
	irq->default_type = IRQ_TYPE_NONE;
	irq->parent_handler = tegra186_gpio_irq;
	irq->parent_handler_data = gpio;
	irq->num_parents = gpio->num_irq;
	irq->parents = gpio->irq;

	irq->map = devm_kcalloc(&pdev->dev, gpio->gpio.ngpio,
				sizeof(*irq->map), GFP_KERNEL);
	if (!irq->map)
		return -ENOMEM;

	for (i = 0, offset = 0; i < gpio->soc->num_ports; i++) {
		const struct tegra_gpio_port *port = &gpio->soc->ports[i];

		for (j = 0; j < port->pins; j++)
			irq->map[offset + j] = irq->parents[port->irq];

		offset += port->pins;
	}

	platform_set_drvdata(pdev, gpio);

	err = devm_gpiochip_add_data(&pdev->dev, &gpio->gpio, gpio);
	if (err < 0)
		return err;

	return 0;
}

static int tegra186_gpio_remove(struct platform_device *pdev)
{
	return 0;
}

#define TEGRA_MAIN_GPIO_PORT(port, base, count, controller)	\
	[TEGRA_MAIN_GPIO_PORT_##port] = {			\
		.name = #port,					\
		.offset = base,					\
		.pins = count,					\
		.irq = controller,				\
	}

static const struct tegra_gpio_port tegra186_main_ports[] = {
	TEGRA_MAIN_GPIO_PORT( A, 0x2000, 7, 2),
	TEGRA_MAIN_GPIO_PORT( B, 0x3000, 7, 3),
	TEGRA_MAIN_GPIO_PORT( C, 0x3200, 7, 3),
	TEGRA_MAIN_GPIO_PORT( D, 0x3400, 6, 3),
	TEGRA_MAIN_GPIO_PORT( E, 0x2200, 8, 2),
	TEGRA_MAIN_GPIO_PORT( F, 0x2400, 6, 2),
	TEGRA_MAIN_GPIO_PORT( G, 0x4200, 6, 4),
	TEGRA_MAIN_GPIO_PORT( H, 0x1000, 7, 1),
	TEGRA_MAIN_GPIO_PORT( I, 0x0800, 8, 0),
	TEGRA_MAIN_GPIO_PORT( J, 0x5000, 8, 5),
	TEGRA_MAIN_GPIO_PORT( K, 0x5200, 1, 5),
	TEGRA_MAIN_GPIO_PORT( L, 0x1200, 8, 1),
	TEGRA_MAIN_GPIO_PORT( M, 0x5600, 6, 5),
	TEGRA_MAIN_GPIO_PORT( N, 0x0000, 7, 0),
	TEGRA_MAIN_GPIO_PORT( O, 0x0200, 4, 0),
	TEGRA_MAIN_GPIO_PORT( P, 0x4000, 7, 4),
	TEGRA_MAIN_GPIO_PORT( Q, 0x0400, 6, 0),
	TEGRA_MAIN_GPIO_PORT( R, 0x0a00, 6, 0),
	TEGRA_MAIN_GPIO_PORT( T, 0x0600, 4, 0),
	TEGRA_MAIN_GPIO_PORT( X, 0x1400, 8, 1),
	TEGRA_MAIN_GPIO_PORT( Y, 0x1600, 7, 1),
	TEGRA_MAIN_GPIO_PORT(BB, 0x2600, 2, 2),
	TEGRA_MAIN_GPIO_PORT(CC, 0x5400, 4, 5),
};

static const struct tegra_gpio_soc tegra186_main_soc = {
	.num_ports = ARRAY_SIZE(tegra186_main_ports),
	.ports = tegra186_main_ports,
	.name = "tegra186-gpio",
};

#define TEGRA_AON_GPIO_PORT(port, base, count, controller)	\
	[TEGRA_AON_GPIO_PORT_##port] = {			\
		.name = #port,					\
		.offset = base,					\
		.pins = count,					\
		.irq = controller,				\
	}

static const struct tegra_gpio_port tegra186_aon_ports[] = {
	TEGRA_AON_GPIO_PORT( S, 0x0200, 5, 0),
	TEGRA_AON_GPIO_PORT( U, 0x0400, 6, 0),
	TEGRA_AON_GPIO_PORT( V, 0x0800, 8, 0),
	TEGRA_AON_GPIO_PORT( W, 0x0a00, 8, 0),
	TEGRA_AON_GPIO_PORT( Z, 0x0e00, 4, 0),
	TEGRA_AON_GPIO_PORT(AA, 0x0c00, 8, 0),
	TEGRA_AON_GPIO_PORT(EE, 0x0600, 3, 0),
	TEGRA_AON_GPIO_PORT(FF, 0x0000, 5, 0),
};

static const struct tegra_gpio_soc tegra186_aon_soc = {
	.num_ports = ARRAY_SIZE(tegra186_aon_ports),
	.ports = tegra186_aon_ports,
	.name = "tegra186-gpio-aon",
};

static const struct of_device_id tegra186_gpio_of_match[] = {
	{
		.compatible = "nvidia,tegra186-gpio",
		.data = &tegra186_main_soc
	}, {
		.compatible = "nvidia,tegra186-gpio-aon",
		.data = &tegra186_aon_soc
	}, {
		/* sentinel */
	}
};

static struct platform_driver tegra186_gpio_driver = {
	.driver = {
		.name = "tegra186-gpio",
		.of_match_table = tegra186_gpio_of_match,
	},
	.probe = tegra186_gpio_probe,
	.remove = tegra186_gpio_remove,
};
module_platform_driver(tegra186_gpio_driver);

MODULE_DESCRIPTION("NVIDIA Tegra186 GPIO controller driver");
MODULE_AUTHOR("Thierry Reding <treding@nvidia.com>");
MODULE_LICENSE("GPL v2");
