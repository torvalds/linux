// SPDX-License-Identifier: GPL-2.0
/*
 * Nuvoton NCT6694 GPIO controller driver based on USB interface.
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 */

#include <linux/bits.h>
#include <linux/gpio/driver.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/mfd/nct6694.h>
#include <linux/module.h>
#include <linux/platform_device.h>

/*
 * USB command module type for NCT6694 GPIO controller.
 * This defines the module type used for communication with the NCT6694
 * GPIO controller over the USB interface.
 */
#define NCT6694_GPIO_MOD	0xFF

#define NCT6694_GPIO_VER	0x90
#define NCT6694_GPIO_VALID	0x110
#define NCT6694_GPI_DATA	0x120
#define NCT6694_GPO_DIR		0x170
#define NCT6694_GPO_TYPE	0x180
#define NCT6694_GPO_DATA	0x190

#define NCT6694_GPI_STS		0x130
#define NCT6694_GPI_CLR		0x140
#define NCT6694_GPI_FALLING	0x150
#define NCT6694_GPI_RISING	0x160

#define NCT6694_NR_GPIO		8

struct nct6694_gpio_data {
	struct nct6694 *nct6694;
	struct gpio_chip gpio;
	struct mutex lock;
	/* Protect irq operation */
	struct mutex irq_lock;

	unsigned char reg_val;
	unsigned char irq_trig_falling;
	unsigned char irq_trig_rising;

	/* Current gpio group */
	unsigned char group;
	int irq;
};

static int nct6694_get_direction(struct gpio_chip *gpio, unsigned int offset)
{
	struct nct6694_gpio_data *data = gpiochip_get_data(gpio);
	const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_GPIO_MOD,
		.offset = cpu_to_le16(NCT6694_GPO_DIR + data->group),
		.len = cpu_to_le16(sizeof(data->reg_val))
	};
	int ret;

	guard(mutex)(&data->lock);

	ret = nct6694_read_msg(data->nct6694, &cmd_hd, &data->reg_val);
	if (ret < 0)
		return ret;

	return !(BIT(offset) & data->reg_val);
}

static int nct6694_direction_input(struct gpio_chip *gpio, unsigned int offset)
{
	struct nct6694_gpio_data *data = gpiochip_get_data(gpio);
	const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_GPIO_MOD,
		.offset = cpu_to_le16(NCT6694_GPO_DIR + data->group),
		.len = cpu_to_le16(sizeof(data->reg_val))
	};
	int ret;

	guard(mutex)(&data->lock);

	ret = nct6694_read_msg(data->nct6694, &cmd_hd, &data->reg_val);
	if (ret < 0)
		return ret;

	data->reg_val &= ~BIT(offset);

	return nct6694_write_msg(data->nct6694, &cmd_hd, &data->reg_val);
}

static int nct6694_direction_output(struct gpio_chip *gpio,
				    unsigned int offset, int val)
{
	struct nct6694_gpio_data *data = gpiochip_get_data(gpio);
	struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_GPIO_MOD,
		.offset = cpu_to_le16(NCT6694_GPO_DIR + data->group),
		.len = cpu_to_le16(sizeof(data->reg_val))
	};
	int ret;

	guard(mutex)(&data->lock);

	/* Set direction to output */
	ret = nct6694_read_msg(data->nct6694, &cmd_hd, &data->reg_val);
	if (ret < 0)
		return ret;

	data->reg_val |= BIT(offset);
	ret = nct6694_write_msg(data->nct6694, &cmd_hd, &data->reg_val);
	if (ret < 0)
		return ret;

	/* Then set output level */
	cmd_hd.offset = cpu_to_le16(NCT6694_GPO_DATA + data->group);
	ret = nct6694_read_msg(data->nct6694, &cmd_hd, &data->reg_val);
	if (ret < 0)
		return ret;

	if (val)
		data->reg_val |= BIT(offset);
	else
		data->reg_val &= ~BIT(offset);

	return nct6694_write_msg(data->nct6694, &cmd_hd, &data->reg_val);
}

static int nct6694_get_value(struct gpio_chip *gpio, unsigned int offset)
{
	struct nct6694_gpio_data *data = gpiochip_get_data(gpio);
	struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_GPIO_MOD,
		.offset = cpu_to_le16(NCT6694_GPO_DIR + data->group),
		.len = cpu_to_le16(sizeof(data->reg_val))
	};
	int ret;

	guard(mutex)(&data->lock);

	ret = nct6694_read_msg(data->nct6694, &cmd_hd, &data->reg_val);
	if (ret < 0)
		return ret;

	if (BIT(offset) & data->reg_val) {
		cmd_hd.offset = cpu_to_le16(NCT6694_GPO_DATA + data->group);
		ret = nct6694_read_msg(data->nct6694, &cmd_hd, &data->reg_val);
		if (ret < 0)
			return ret;

		return !!(BIT(offset) & data->reg_val);
	}

	cmd_hd.offset = cpu_to_le16(NCT6694_GPI_DATA + data->group);
	ret = nct6694_read_msg(data->nct6694, &cmd_hd, &data->reg_val);
	if (ret < 0)
		return ret;

	return !!(BIT(offset) & data->reg_val);
}

static int nct6694_set_value(struct gpio_chip *gpio, unsigned int offset,
			     int val)
{
	struct nct6694_gpio_data *data = gpiochip_get_data(gpio);
	const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_GPIO_MOD,
		.offset = cpu_to_le16(NCT6694_GPO_DATA + data->group),
		.len = cpu_to_le16(sizeof(data->reg_val))
	};
	int ret;

	guard(mutex)(&data->lock);

	ret = nct6694_read_msg(data->nct6694, &cmd_hd, &data->reg_val);
	if (ret < 0)
		return ret;

	if (val)
		data->reg_val |= BIT(offset);
	else
		data->reg_val &= ~BIT(offset);

	return nct6694_write_msg(data->nct6694, &cmd_hd, &data->reg_val);
}

static int nct6694_set_config(struct gpio_chip *gpio, unsigned int offset,
			      unsigned long config)
{
	struct nct6694_gpio_data *data = gpiochip_get_data(gpio);
	const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_GPIO_MOD,
		.offset = cpu_to_le16(NCT6694_GPO_TYPE + data->group),
		.len = cpu_to_le16(sizeof(data->reg_val))
	};
	int ret;

	guard(mutex)(&data->lock);

	ret = nct6694_read_msg(data->nct6694, &cmd_hd, &data->reg_val);
	if (ret < 0)
		return ret;

	switch (pinconf_to_config_param(config)) {
	case PIN_CONFIG_DRIVE_OPEN_DRAIN:
		data->reg_val |= BIT(offset);
		break;
	case PIN_CONFIG_DRIVE_PUSH_PULL:
		data->reg_val &= ~BIT(offset);
		break;
	default:
		return -ENOTSUPP;
	}

	return nct6694_write_msg(data->nct6694, &cmd_hd, &data->reg_val);
}

static int nct6694_init_valid_mask(struct gpio_chip *gpio,
				   unsigned long *valid_mask,
				   unsigned int ngpios)
{
	struct nct6694_gpio_data *data = gpiochip_get_data(gpio);
	const struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_GPIO_MOD,
		.offset = cpu_to_le16(NCT6694_GPIO_VALID + data->group),
		.len = cpu_to_le16(sizeof(data->reg_val))
	};
	int ret;

	guard(mutex)(&data->lock);

	ret = nct6694_read_msg(data->nct6694, &cmd_hd, &data->reg_val);
	if (ret < 0)
		return ret;

	*valid_mask = data->reg_val;

	return ret;
}

static irqreturn_t nct6694_irq_handler(int irq, void *priv)
{
	struct nct6694_gpio_data *data = priv;
	struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_GPIO_MOD,
		.offset = cpu_to_le16(NCT6694_GPI_STS + data->group),
		.len = cpu_to_le16(sizeof(data->reg_val))
	};
	unsigned char status;
	int ret;

	guard(mutex)(&data->lock);

	ret = nct6694_read_msg(data->nct6694, &cmd_hd, &data->reg_val);
	if (ret)
		return IRQ_NONE;

	status = data->reg_val;

	while (status) {
		int bit = __ffs(status);

		data->reg_val = BIT(bit);
		handle_nested_irq(irq_find_mapping(data->gpio.irq.domain, bit));
		status &= ~BIT(bit);
		cmd_hd.offset = cpu_to_le16(NCT6694_GPI_CLR + data->group);
		nct6694_write_msg(data->nct6694, &cmd_hd, &data->reg_val);
	}

	return IRQ_HANDLED;
}

static int nct6694_get_irq_trig(struct nct6694_gpio_data *data)
{
	struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_GPIO_MOD,
		.offset = cpu_to_le16(NCT6694_GPI_FALLING + data->group),
		.len = cpu_to_le16(sizeof(data->reg_val))
	};
	int ret;

	guard(mutex)(&data->lock);

	ret = nct6694_read_msg(data->nct6694, &cmd_hd, &data->irq_trig_falling);
	if (ret)
		return ret;

	cmd_hd.offset = cpu_to_le16(NCT6694_GPI_RISING + data->group);
	return nct6694_read_msg(data->nct6694, &cmd_hd, &data->irq_trig_rising);
}

static void nct6694_irq_mask(struct irq_data *d)
{
	struct gpio_chip *gpio = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	gpiochip_disable_irq(gpio, hwirq);
}

static void nct6694_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *gpio = irq_data_get_irq_chip_data(d);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	gpiochip_enable_irq(gpio, hwirq);
}

static int nct6694_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *gpio = irq_data_get_irq_chip_data(d);
	struct nct6694_gpio_data *data = gpiochip_get_data(gpio);
	irq_hw_number_t hwirq = irqd_to_hwirq(d);

	guard(mutex)(&data->lock);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		data->irq_trig_rising |= BIT(hwirq);
		break;

	case IRQ_TYPE_EDGE_FALLING:
		data->irq_trig_falling |= BIT(hwirq);
		break;

	case IRQ_TYPE_EDGE_BOTH:
		data->irq_trig_rising |= BIT(hwirq);
		data->irq_trig_falling |= BIT(hwirq);
		break;

	default:
		return -ENOTSUPP;
	}

	return 0;
}

static void nct6694_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *gpio = irq_data_get_irq_chip_data(d);
	struct nct6694_gpio_data *data = gpiochip_get_data(gpio);

	mutex_lock(&data->irq_lock);
}

static void nct6694_irq_bus_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *gpio = irq_data_get_irq_chip_data(d);
	struct nct6694_gpio_data *data = gpiochip_get_data(gpio);
	struct nct6694_cmd_header cmd_hd = {
		.mod = NCT6694_GPIO_MOD,
		.offset = cpu_to_le16(NCT6694_GPI_FALLING + data->group),
		.len = cpu_to_le16(sizeof(data->reg_val))
	};

	scoped_guard(mutex, &data->lock) {
		nct6694_write_msg(data->nct6694, &cmd_hd, &data->irq_trig_falling);

		cmd_hd.offset = cpu_to_le16(NCT6694_GPI_RISING + data->group);
		nct6694_write_msg(data->nct6694, &cmd_hd, &data->irq_trig_rising);
	}

	mutex_unlock(&data->irq_lock);
}

static const struct irq_chip nct6694_irq_chip = {
	.name			= "gpio-nct6694",
	.irq_mask		= nct6694_irq_mask,
	.irq_unmask		= nct6694_irq_unmask,
	.irq_set_type		= nct6694_irq_set_type,
	.irq_bus_lock		= nct6694_irq_bus_lock,
	.irq_bus_sync_unlock	= nct6694_irq_bus_sync_unlock,
	.flags			= IRQCHIP_IMMUTABLE,
	GPIOCHIP_IRQ_RESOURCE_HELPERS,
};

static void nct6694_irq_dispose_mapping(void *d)
{
	struct nct6694_gpio_data *data = d;

	irq_dispose_mapping(data->irq);
}

static void nct6694_gpio_ida_free(void *d)
{
	struct nct6694_gpio_data *data = d;
	struct nct6694 *nct6694 = data->nct6694;

	ida_free(&nct6694->gpio_ida, data->group);
}

static int nct6694_gpio_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nct6694 *nct6694 = dev_get_drvdata(dev->parent);
	struct nct6694_gpio_data *data;
	struct gpio_irq_chip *girq;
	int ret, i;
	char **names;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->nct6694 = nct6694;

	ret = ida_alloc(&nct6694->gpio_ida, GFP_KERNEL);
	if (ret < 0)
		return ret;
	data->group = ret;

	ret = devm_add_action_or_reset(dev, nct6694_gpio_ida_free, data);
	if (ret)
		return ret;

	names = devm_kcalloc(dev, NCT6694_NR_GPIO, sizeof(char *),
			     GFP_KERNEL);
	if (!names)
		return -ENOMEM;

	for (i = 0; i < NCT6694_NR_GPIO; i++) {
		names[i] = devm_kasprintf(dev, GFP_KERNEL, "GPIO%X%d",
					  data->group, i);
		if (!names[i])
			return -ENOMEM;
	}

	data->irq = irq_create_mapping(nct6694->domain,
				       NCT6694_IRQ_GPIO0 + data->group);
	if (!data->irq)
		return -EINVAL;

	ret = devm_add_action_or_reset(dev, nct6694_irq_dispose_mapping, data);
	if (ret)
		return ret;

	data->gpio.names		= (const char * const*)names;
	data->gpio.label		= pdev->name;
	data->gpio.direction_input	= nct6694_direction_input;
	data->gpio.get			= nct6694_get_value;
	data->gpio.direction_output	= nct6694_direction_output;
	data->gpio.set			= nct6694_set_value;
	data->gpio.get_direction	= nct6694_get_direction;
	data->gpio.set_config		= nct6694_set_config;
	data->gpio.init_valid_mask	= nct6694_init_valid_mask;
	data->gpio.base			= -1;
	data->gpio.can_sleep		= false;
	data->gpio.owner		= THIS_MODULE;
	data->gpio.ngpio		= NCT6694_NR_GPIO;

	platform_set_drvdata(pdev, data);

	ret = devm_mutex_init(dev, &data->lock);
	if (ret)
		return ret;

	ret = devm_mutex_init(dev, &data->irq_lock);
	if (ret)
		return ret;

	ret = nct6694_get_irq_trig(data);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to get irq trigger type\n");
		return ret;
	}

	girq = &data->gpio.irq;
	gpio_irq_chip_set_chip(girq, &nct6694_irq_chip);
	girq->parent_handler = NULL;
	girq->num_parents = 0;
	girq->parents = NULL;
	girq->default_type = IRQ_TYPE_NONE;
	girq->handler = handle_level_irq;
	girq->threaded = true;

	ret = devm_request_threaded_irq(dev, data->irq, NULL, nct6694_irq_handler,
					IRQF_ONESHOT | IRQF_SHARED,
					"gpio-nct6694", data);
	if (ret) {
		dev_err_probe(dev, ret, "Failed to request irq\n");
		return ret;
	}

	return devm_gpiochip_add_data(dev, &data->gpio, data);
}

static struct platform_driver nct6694_gpio_driver = {
	.driver = {
		.name	= "nct6694-gpio",
	},
	.probe		= nct6694_gpio_probe,
};

module_platform_driver(nct6694_gpio_driver);

MODULE_DESCRIPTION("USB-GPIO controller driver for NCT6694");
MODULE_AUTHOR("Ming Yu <tmyu0@nuvoton.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:nct6694-gpio");
