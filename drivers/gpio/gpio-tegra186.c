// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2017 NVIDIA Corporation
 *
 * Author: Thierry Reding <treding@nvidia.com>
 */

#include <linux/gpio/driver.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include <dt-bindings/gpio/tegra186-gpio.h>
#include <dt-bindings/gpio/tegra194-gpio.h>

/* security registers */
#define TEGRA186_GPIO_CTL_SCR 0x0c
#define  TEGRA186_GPIO_CTL_SCR_SEC_WEN BIT(28)
#define  TEGRA186_GPIO_CTL_SCR_SEC_REN BIT(27)

#define TEGRA186_GPIO_INT_ROUTE_MAPPING(p, x) (0x14 + (p) * 0x20 + (x) * 4)

/* control registers */
#define TEGRA186_GPIO_ENABLE_CONFIG 0x00
#define  TEGRA186_GPIO_ENABLE_CONFIG_ENABLE BIT(0)
#define  TEGRA186_GPIO_ENABLE_CONFIG_OUT BIT(1)
#define  TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_NONE (0x0 << 2)
#define  TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_LEVEL (0x1 << 2)
#define  TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_SINGLE_EDGE (0x2 << 2)
#define  TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_DOUBLE_EDGE (0x3 << 2)
#define  TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_TYPE_MASK (0x3 << 2)
#define  TEGRA186_GPIO_ENABLE_CONFIG_TRIGGER_LEVEL BIT(4)
#define  TEGRA186_GPIO_ENABLE_CONFIG_DEBOUNCE BIT(5)
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
	unsigned int bank;
	unsigned int port;
	unsigned int pins;
};

struct tegra186_pin_range {
	unsigned int offset;
	const char *group;
};

struct tegra_gpio_soc {
	const struct tegra_gpio_port *ports;
	unsigned int num_ports;
	const char *name;
	unsigned int instance;

	const struct tegra186_pin_range *pin_ranges;
	unsigned int num_pin_ranges;
	const char *pinmux;
};

struct tegra_gpio {
	struct gpio_chip gpio;
	struct irq_chip intc;
	unsigned int num_irq;
	unsigned int *irq;

	const struct tegra_gpio_soc *soc;

	void __iomem *secure;
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
	unsigned int offset;

	port = tegra186_gpio_get_port(gpio, &pin);
	if (!port)
		return NULL;

	offset = port->bank * 0x1000 + port->port * 0x200;

	return gpio->base + offset + pin * 0x20;
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
		return GPIO_LINE_DIRECTION_OUT;

	return GPIO_LINE_DIRECTION_IN;
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

static int tegra186_gpio_set_config(struct gpio_chip *chip,
				    unsigned int offset,
				    unsigned long config)
{
	struct tegra_gpio *gpio = gpiochip_get_data(chip);
	u32 debounce, value;
	void __iomem *base;

	base = tegra186_gpio_get_base(gpio, offset);
	if (base == NULL)
		return -ENXIO;

	if (pinconf_to_config_param(config) != PIN_CONFIG_INPUT_DEBOUNCE)
		return -ENOTSUPP;

	debounce = pinconf_to_config_argument(config);

	/*
	 * The Tegra186 GPIO controller supports a maximum of 255 ms debounce
	 * time.
	 */
	if (debounce > 255000)
		return -EINVAL;

	debounce = DIV_ROUND_UP(debounce, USEC_PER_MSEC);

	value = TEGRA186_GPIO_DEBOUNCE_CONTROL_THRESHOLD(debounce);
	writel(value, base + TEGRA186_GPIO_DEBOUNCE_CONTROL);

	value = readl(base + TEGRA186_GPIO_ENABLE_CONFIG);
	value |= TEGRA186_GPIO_ENABLE_CONFIG_DEBOUNCE;
	writel(value, base + TEGRA186_GPIO_ENABLE_CONFIG);

	return 0;
}

static int tegra186_gpio_add_pin_ranges(struct gpio_chip *chip)
{
	struct tegra_gpio *gpio = gpiochip_get_data(chip);
	struct pinctrl_dev *pctldev;
	struct device_node *np;
	unsigned int i, j;
	int err;

	if (!gpio->soc->pinmux || gpio->soc->num_pin_ranges == 0)
		return 0;

	np = of_find_compatible_node(NULL, NULL, gpio->soc->pinmux);
	if (!np)
		return -ENODEV;

	pctldev = of_pinctrl_get(np);
	of_node_put(np);
	if (!pctldev)
		return -EPROBE_DEFER;

	for (i = 0; i < gpio->soc->num_pin_ranges; i++) {
		unsigned int pin = gpio->soc->pin_ranges[i].offset, port;
		const char *group = gpio->soc->pin_ranges[i].group;

		port = pin / 8;
		pin = pin % 8;

		if (port >= gpio->soc->num_ports) {
			dev_warn(chip->parent, "invalid port %u for %s\n",
				 port, group);
			continue;
		}

		for (j = 0; j < port; j++)
			pin += gpio->soc->ports[j].pins;

		err = gpiochip_add_pingroup_range(chip, pctldev, pin, group);
		if (err < 0)
			return err;
	}

	return 0;
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

static int tegra186_irq_set_type(struct irq_data *data, unsigned int type)
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

	switch (type & IRQ_TYPE_SENSE_MASK) {
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

	if ((type & IRQ_TYPE_EDGE_BOTH) == 0)
		irq_set_handler_locked(data, handle_level_irq);
	else
		irq_set_handler_locked(data, handle_edge_irq);

	if (data->parent_data)
		return irq_chip_set_type_parent(data, type);

	return 0;
}

static int tegra186_irq_set_wake(struct irq_data *data, unsigned int on)
{
	if (data->parent_data)
		return irq_chip_set_wake_parent(data, on);

	return 0;
}

static int tegra186_irq_set_affinity(struct irq_data *data,
				     const struct cpumask *dest,
				     bool force)
{
	if (data->parent_data)
		return irq_chip_set_affinity_parent(data, dest, force);

	return -EINVAL;
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
		unsigned int pin, irq;
		unsigned long value;
		void __iomem *base;

		base = gpio->base + port->bank * 0x1000 + port->port * 0x200;

		/* skip ports that are not associated with this bank */
		if (parent != gpio->irq[port->bank])
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

static int tegra186_gpio_irq_domain_translate(struct irq_domain *domain,
					      struct irq_fwspec *fwspec,
					      unsigned long *hwirq,
					      unsigned int *type)
{
	struct tegra_gpio *gpio = gpiochip_get_data(domain->host_data);
	unsigned int port, pin, i, offset = 0;

	if (WARN_ON(gpio->gpio.of_gpio_n_cells < 2))
		return -EINVAL;

	if (WARN_ON(fwspec->param_count < gpio->gpio.of_gpio_n_cells))
		return -EINVAL;

	port = fwspec->param[0] / 8;
	pin = fwspec->param[0] % 8;

	if (port >= gpio->soc->num_ports)
		return -EINVAL;

	for (i = 0; i < port; i++)
		offset += gpio->soc->ports[i].pins;

	*type = fwspec->param[1] & IRQ_TYPE_SENSE_MASK;
	*hwirq = offset + pin;

	return 0;
}

static void *tegra186_gpio_populate_parent_fwspec(struct gpio_chip *chip,
						 unsigned int parent_hwirq,
						 unsigned int parent_type)
{
	struct tegra_gpio *gpio = gpiochip_get_data(chip);
	struct irq_fwspec *fwspec;

	fwspec = kmalloc(sizeof(*fwspec), GFP_KERNEL);
	if (!fwspec)
		return NULL;

	fwspec->fwnode = chip->irq.parent_domain->fwnode;
	fwspec->param_count = 3;
	fwspec->param[0] = gpio->soc->instance;
	fwspec->param[1] = parent_hwirq;
	fwspec->param[2] = parent_type;

	return fwspec;
}

static int tegra186_gpio_child_to_parent_hwirq(struct gpio_chip *chip,
					       unsigned int hwirq,
					       unsigned int type,
					       unsigned int *parent_hwirq,
					       unsigned int *parent_type)
{
	*parent_hwirq = chip->irq.child_offset_to_irq(chip, hwirq);
	*parent_type = type;

	return 0;
}

static unsigned int tegra186_gpio_child_offset_to_irq(struct gpio_chip *chip,
						      unsigned int offset)
{
	struct tegra_gpio *gpio = gpiochip_get_data(chip);
	unsigned int i;

	for (i = 0; i < gpio->soc->num_ports; i++) {
		if (offset < gpio->soc->ports[i].pins)
			break;

		offset -= gpio->soc->ports[i].pins;
	}

	return offset + i * 8;
}

static const struct of_device_id tegra186_pmc_of_match[] = {
	{ .compatible = "nvidia,tegra186-pmc" },
	{ .compatible = "nvidia,tegra194-pmc" },
	{ /* sentinel */ }
};

static void tegra186_gpio_init_route_mapping(struct tegra_gpio *gpio)
{
	unsigned int i, j;
	u32 value;

	for (i = 0; i < gpio->soc->num_ports; i++) {
		const struct tegra_gpio_port *port = &gpio->soc->ports[i];
		unsigned int offset, p = port->port;
		void __iomem *base;

		base = gpio->secure + port->bank * 0x1000 + 0x800;

		value = readl(base + TEGRA186_GPIO_CTL_SCR);

		/*
		 * For controllers that haven't been locked down yet, make
		 * sure to program the default interrupt route mapping.
		 */
		if ((value & TEGRA186_GPIO_CTL_SCR_SEC_REN) == 0 &&
		    (value & TEGRA186_GPIO_CTL_SCR_SEC_WEN) == 0) {
			for (j = 0; j < 8; j++) {
				offset = TEGRA186_GPIO_INT_ROUTE_MAPPING(p, j);

				value = readl(base + offset);
				value = BIT(port->pins) - 1;
				writel(value, base + offset);
			}
		}
	}
}

static int tegra186_gpio_probe(struct platform_device *pdev)
{
	unsigned int i, j, offset;
	struct gpio_irq_chip *irq;
	struct tegra_gpio *gpio;
	struct device_node *np;
	char **names;
	int err;

	gpio = devm_kzalloc(&pdev->dev, sizeof(*gpio), GFP_KERNEL);
	if (!gpio)
		return -ENOMEM;

	gpio->soc = of_device_get_match_data(&pdev->dev);

	gpio->secure = devm_platform_ioremap_resource_byname(pdev, "security");
	if (IS_ERR(gpio->secure))
		return PTR_ERR(gpio->secure);

	gpio->base = devm_platform_ioremap_resource_byname(pdev, "gpio");
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

	gpio->gpio.request = gpiochip_generic_request;
	gpio->gpio.free = gpiochip_generic_free;
	gpio->gpio.get_direction = tegra186_gpio_get_direction;
	gpio->gpio.direction_input = tegra186_gpio_direction_input;
	gpio->gpio.direction_output = tegra186_gpio_direction_output;
	gpio->gpio.get = tegra186_gpio_get;
	gpio->gpio.set = tegra186_gpio_set;
	gpio->gpio.set_config = tegra186_gpio_set_config;
	gpio->gpio.add_pin_ranges = tegra186_gpio_add_pin_ranges;

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
	gpio->intc.irq_set_wake = tegra186_irq_set_wake;
	gpio->intc.irq_set_affinity = tegra186_irq_set_affinity;

	irq = &gpio->gpio.irq;
	irq->chip = &gpio->intc;
	irq->fwnode = of_node_to_fwnode(pdev->dev.of_node);
	irq->child_to_parent_hwirq = tegra186_gpio_child_to_parent_hwirq;
	irq->populate_parent_alloc_arg = tegra186_gpio_populate_parent_fwspec;
	irq->child_offset_to_irq = tegra186_gpio_child_offset_to_irq;
	irq->child_irq_domain_ops.translate = tegra186_gpio_irq_domain_translate;
	irq->handler = handle_simple_irq;
	irq->default_type = IRQ_TYPE_NONE;
	irq->parent_handler = tegra186_gpio_irq;
	irq->parent_handler_data = gpio;
	irq->num_parents = gpio->num_irq;
	irq->parents = gpio->irq;

	np = of_find_matching_node(NULL, tegra186_pmc_of_match);
	if (np) {
		irq->parent_domain = irq_find_host(np);
		of_node_put(np);

		if (!irq->parent_domain)
			return -EPROBE_DEFER;
	}

	tegra186_gpio_init_route_mapping(gpio);

	irq->map = devm_kcalloc(&pdev->dev, gpio->gpio.ngpio,
				sizeof(*irq->map), GFP_KERNEL);
	if (!irq->map)
		return -ENOMEM;

	for (i = 0, offset = 0; i < gpio->soc->num_ports; i++) {
		const struct tegra_gpio_port *port = &gpio->soc->ports[i];

		for (j = 0; j < port->pins; j++)
			irq->map[offset + j] = irq->parents[port->bank];

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

#define TEGRA186_MAIN_GPIO_PORT(_name, _bank, _port, _pins)	\
	[TEGRA186_MAIN_GPIO_PORT_##_name] = {			\
		.name = #_name,					\
		.bank = _bank,					\
		.port = _port,					\
		.pins = _pins,					\
	}

static const struct tegra_gpio_port tegra186_main_ports[] = {
	TEGRA186_MAIN_GPIO_PORT( A, 2, 0, 7),
	TEGRA186_MAIN_GPIO_PORT( B, 3, 0, 7),
	TEGRA186_MAIN_GPIO_PORT( C, 3, 1, 7),
	TEGRA186_MAIN_GPIO_PORT( D, 3, 2, 6),
	TEGRA186_MAIN_GPIO_PORT( E, 2, 1, 8),
	TEGRA186_MAIN_GPIO_PORT( F, 2, 2, 6),
	TEGRA186_MAIN_GPIO_PORT( G, 4, 1, 6),
	TEGRA186_MAIN_GPIO_PORT( H, 1, 0, 7),
	TEGRA186_MAIN_GPIO_PORT( I, 0, 4, 8),
	TEGRA186_MAIN_GPIO_PORT( J, 5, 0, 8),
	TEGRA186_MAIN_GPIO_PORT( K, 5, 1, 1),
	TEGRA186_MAIN_GPIO_PORT( L, 1, 1, 8),
	TEGRA186_MAIN_GPIO_PORT( M, 5, 3, 6),
	TEGRA186_MAIN_GPIO_PORT( N, 0, 0, 7),
	TEGRA186_MAIN_GPIO_PORT( O, 0, 1, 4),
	TEGRA186_MAIN_GPIO_PORT( P, 4, 0, 7),
	TEGRA186_MAIN_GPIO_PORT( Q, 0, 2, 6),
	TEGRA186_MAIN_GPIO_PORT( R, 0, 5, 6),
	TEGRA186_MAIN_GPIO_PORT( T, 0, 3, 4),
	TEGRA186_MAIN_GPIO_PORT( X, 1, 2, 8),
	TEGRA186_MAIN_GPIO_PORT( Y, 1, 3, 7),
	TEGRA186_MAIN_GPIO_PORT(BB, 2, 3, 2),
	TEGRA186_MAIN_GPIO_PORT(CC, 5, 2, 4),
};

static const struct tegra_gpio_soc tegra186_main_soc = {
	.num_ports = ARRAY_SIZE(tegra186_main_ports),
	.ports = tegra186_main_ports,
	.name = "tegra186-gpio",
	.instance = 0,
};

#define TEGRA186_AON_GPIO_PORT(_name, _bank, _port, _pins)	\
	[TEGRA186_AON_GPIO_PORT_##_name] = {			\
		.name = #_name,					\
		.bank = _bank,					\
		.port = _port,					\
		.pins = _pins,					\
	}

static const struct tegra_gpio_port tegra186_aon_ports[] = {
	TEGRA186_AON_GPIO_PORT( S, 0, 1, 5),
	TEGRA186_AON_GPIO_PORT( U, 0, 2, 6),
	TEGRA186_AON_GPIO_PORT( V, 0, 4, 8),
	TEGRA186_AON_GPIO_PORT( W, 0, 5, 8),
	TEGRA186_AON_GPIO_PORT( Z, 0, 7, 4),
	TEGRA186_AON_GPIO_PORT(AA, 0, 6, 8),
	TEGRA186_AON_GPIO_PORT(EE, 0, 3, 3),
	TEGRA186_AON_GPIO_PORT(FF, 0, 0, 5),
};

static const struct tegra_gpio_soc tegra186_aon_soc = {
	.num_ports = ARRAY_SIZE(tegra186_aon_ports),
	.ports = tegra186_aon_ports,
	.name = "tegra186-gpio-aon",
	.instance = 1,
};

#define TEGRA194_MAIN_GPIO_PORT(_name, _bank, _port, _pins)	\
	[TEGRA194_MAIN_GPIO_PORT_##_name] = {			\
		.name = #_name,					\
		.bank = _bank,					\
		.port = _port,					\
		.pins = _pins,					\
	}

static const struct tegra_gpio_port tegra194_main_ports[] = {
	TEGRA194_MAIN_GPIO_PORT( A, 1, 2, 8),
	TEGRA194_MAIN_GPIO_PORT( B, 4, 7, 2),
	TEGRA194_MAIN_GPIO_PORT( C, 4, 3, 8),
	TEGRA194_MAIN_GPIO_PORT( D, 4, 4, 4),
	TEGRA194_MAIN_GPIO_PORT( E, 4, 5, 8),
	TEGRA194_MAIN_GPIO_PORT( F, 4, 6, 6),
	TEGRA194_MAIN_GPIO_PORT( G, 4, 0, 8),
	TEGRA194_MAIN_GPIO_PORT( H, 4, 1, 8),
	TEGRA194_MAIN_GPIO_PORT( I, 4, 2, 5),
	TEGRA194_MAIN_GPIO_PORT( J, 5, 1, 6),
	TEGRA194_MAIN_GPIO_PORT( K, 3, 0, 8),
	TEGRA194_MAIN_GPIO_PORT( L, 3, 1, 4),
	TEGRA194_MAIN_GPIO_PORT( M, 2, 3, 8),
	TEGRA194_MAIN_GPIO_PORT( N, 2, 4, 3),
	TEGRA194_MAIN_GPIO_PORT( O, 5, 0, 6),
	TEGRA194_MAIN_GPIO_PORT( P, 2, 5, 8),
	TEGRA194_MAIN_GPIO_PORT( Q, 2, 6, 8),
	TEGRA194_MAIN_GPIO_PORT( R, 2, 7, 6),
	TEGRA194_MAIN_GPIO_PORT( S, 3, 3, 8),
	TEGRA194_MAIN_GPIO_PORT( T, 3, 4, 8),
	TEGRA194_MAIN_GPIO_PORT( U, 3, 5, 1),
	TEGRA194_MAIN_GPIO_PORT( V, 1, 0, 8),
	TEGRA194_MAIN_GPIO_PORT( W, 1, 1, 2),
	TEGRA194_MAIN_GPIO_PORT( X, 2, 0, 8),
	TEGRA194_MAIN_GPIO_PORT( Y, 2, 1, 8),
	TEGRA194_MAIN_GPIO_PORT( Z, 2, 2, 8),
	TEGRA194_MAIN_GPIO_PORT(FF, 3, 2, 2),
	TEGRA194_MAIN_GPIO_PORT(GG, 0, 0, 2)
};

static const struct tegra186_pin_range tegra194_main_pin_ranges[] = {
	{ TEGRA194_MAIN_GPIO(GG, 0), "pex_l5_clkreq_n_pgg0" },
	{ TEGRA194_MAIN_GPIO(GG, 1), "pex_l5_rst_n_pgg1" },
};

static const struct tegra_gpio_soc tegra194_main_soc = {
	.num_ports = ARRAY_SIZE(tegra194_main_ports),
	.ports = tegra194_main_ports,
	.name = "tegra194-gpio",
	.instance = 0,
	.num_pin_ranges = ARRAY_SIZE(tegra194_main_pin_ranges),
	.pin_ranges = tegra194_main_pin_ranges,
	.pinmux = "nvidia,tegra194-pinmux",
};

#define TEGRA194_AON_GPIO_PORT(_name, _bank, _port, _pins)	\
	[TEGRA194_AON_GPIO_PORT_##_name] = {			\
		.name = #_name,					\
		.bank = _bank,					\
		.port = _port,					\
		.pins = _pins,					\
	}

static const struct tegra_gpio_port tegra194_aon_ports[] = {
	TEGRA194_AON_GPIO_PORT(AA, 0, 3, 8),
	TEGRA194_AON_GPIO_PORT(BB, 0, 4, 4),
	TEGRA194_AON_GPIO_PORT(CC, 0, 1, 8),
	TEGRA194_AON_GPIO_PORT(DD, 0, 2, 3),
	TEGRA194_AON_GPIO_PORT(EE, 0, 0, 7)
};

static const struct tegra_gpio_soc tegra194_aon_soc = {
	.num_ports = ARRAY_SIZE(tegra194_aon_ports),
	.ports = tegra194_aon_ports,
	.name = "tegra194-gpio-aon",
	.instance = 1,
};

static const struct of_device_id tegra186_gpio_of_match[] = {
	{
		.compatible = "nvidia,tegra186-gpio",
		.data = &tegra186_main_soc
	}, {
		.compatible = "nvidia,tegra186-gpio-aon",
		.data = &tegra186_aon_soc
	}, {
		.compatible = "nvidia,tegra194-gpio",
		.data = &tegra194_main_soc
	}, {
		.compatible = "nvidia,tegra194-gpio-aon",
		.data = &tegra194_aon_soc
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, tegra186_gpio_of_match);

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
