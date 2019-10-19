// SPDX-License-Identifier: GPL-2.0
/*
 * GPIO Greybus driver.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/gpio/driver.h>
#include <linux/mutex.h>
#include <linux/greybus.h>

#include "gbphy.h"

struct gb_gpio_line {
	/* The following has to be an array of line_max entries */
	/* --> make them just a flags field */
	u8			active:    1,
				direction: 1,	/* 0 = output, 1 = input */
				value:     1;	/* 0 = low, 1 = high */
	u16			debounce_usec;

	u8			irq_type;
	bool			irq_type_pending;
	bool			masked;
	bool			masked_pending;
};

struct gb_gpio_controller {
	struct gbphy_device	*gbphy_dev;
	struct gb_connection	*connection;
	u8			line_max;	/* max line number */
	struct gb_gpio_line	*lines;

	struct gpio_chip	chip;
	struct irq_chip		irqc;
	struct mutex		irq_lock;
};
#define gpio_chip_to_gb_gpio_controller(chip) \
	container_of(chip, struct gb_gpio_controller, chip)
#define irq_data_to_gpio_chip(d) (d->domain->host_data)

static int gb_gpio_line_count_operation(struct gb_gpio_controller *ggc)
{
	struct gb_gpio_line_count_response response;
	int ret;

	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_LINE_COUNT,
				NULL, 0, &response, sizeof(response));
	if (!ret)
		ggc->line_max = response.count;
	return ret;
}

static int gb_gpio_activate_operation(struct gb_gpio_controller *ggc, u8 which)
{
	struct gb_gpio_activate_request request;
	struct gbphy_device *gbphy_dev = ggc->gbphy_dev;
	int ret;

	ret = gbphy_runtime_get_sync(gbphy_dev);
	if (ret)
		return ret;

	request.which = which;
	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_ACTIVATE,
				&request, sizeof(request), NULL, 0);
	if (ret) {
		gbphy_runtime_put_autosuspend(gbphy_dev);
		return ret;
	}

	ggc->lines[which].active = true;

	return 0;
}

static void gb_gpio_deactivate_operation(struct gb_gpio_controller *ggc,
					 u8 which)
{
	struct gbphy_device *gbphy_dev = ggc->gbphy_dev;
	struct device *dev = &gbphy_dev->dev;
	struct gb_gpio_deactivate_request request;
	int ret;

	request.which = which;
	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_DEACTIVATE,
				&request, sizeof(request), NULL, 0);
	if (ret) {
		dev_err(dev, "failed to deactivate gpio %u\n", which);
		goto out_pm_put;
	}

	ggc->lines[which].active = false;

out_pm_put:
	gbphy_runtime_put_autosuspend(gbphy_dev);
}

static int gb_gpio_get_direction_operation(struct gb_gpio_controller *ggc,
					   u8 which)
{
	struct device *dev = &ggc->gbphy_dev->dev;
	struct gb_gpio_get_direction_request request;
	struct gb_gpio_get_direction_response response;
	int ret;
	u8 direction;

	request.which = which;
	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_GET_DIRECTION,
				&request, sizeof(request),
				&response, sizeof(response));
	if (ret)
		return ret;

	direction = response.direction;
	if (direction && direction != 1) {
		dev_warn(dev, "gpio %u direction was %u (should be 0 or 1)\n",
			 which, direction);
	}
	ggc->lines[which].direction = direction ? 1 : 0;
	return 0;
}

static int gb_gpio_direction_in_operation(struct gb_gpio_controller *ggc,
					  u8 which)
{
	struct gb_gpio_direction_in_request request;
	int ret;

	request.which = which;
	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_DIRECTION_IN,
				&request, sizeof(request), NULL, 0);
	if (!ret)
		ggc->lines[which].direction = 1;
	return ret;
}

static int gb_gpio_direction_out_operation(struct gb_gpio_controller *ggc,
					   u8 which, bool value_high)
{
	struct gb_gpio_direction_out_request request;
	int ret;

	request.which = which;
	request.value = value_high ? 1 : 0;
	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_DIRECTION_OUT,
				&request, sizeof(request), NULL, 0);
	if (!ret)
		ggc->lines[which].direction = 0;
	return ret;
}

static int gb_gpio_get_value_operation(struct gb_gpio_controller *ggc,
				       u8 which)
{
	struct device *dev = &ggc->gbphy_dev->dev;
	struct gb_gpio_get_value_request request;
	struct gb_gpio_get_value_response response;
	int ret;
	u8 value;

	request.which = which;
	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_GET_VALUE,
				&request, sizeof(request),
				&response, sizeof(response));
	if (ret) {
		dev_err(dev, "failed to get value of gpio %u\n", which);
		return ret;
	}

	value = response.value;
	if (value && value != 1) {
		dev_warn(dev, "gpio %u value was %u (should be 0 or 1)\n",
			 which, value);
	}
	ggc->lines[which].value = value ? 1 : 0;
	return 0;
}

static void gb_gpio_set_value_operation(struct gb_gpio_controller *ggc,
					u8 which, bool value_high)
{
	struct device *dev = &ggc->gbphy_dev->dev;
	struct gb_gpio_set_value_request request;
	int ret;

	if (ggc->lines[which].direction == 1) {
		dev_warn(dev, "refusing to set value of input gpio %u\n",
			 which);
		return;
	}

	request.which = which;
	request.value = value_high ? 1 : 0;
	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_SET_VALUE,
				&request, sizeof(request), NULL, 0);
	if (ret) {
		dev_err(dev, "failed to set value of gpio %u\n", which);
		return;
	}

	ggc->lines[which].value = request.value;
}

static int gb_gpio_set_debounce_operation(struct gb_gpio_controller *ggc,
					  u8 which, u16 debounce_usec)
{
	struct gb_gpio_set_debounce_request request;
	int ret;

	request.which = which;
	request.usec = cpu_to_le16(debounce_usec);
	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_SET_DEBOUNCE,
				&request, sizeof(request), NULL, 0);
	if (!ret)
		ggc->lines[which].debounce_usec = debounce_usec;
	return ret;
}

static void _gb_gpio_irq_mask(struct gb_gpio_controller *ggc, u8 hwirq)
{
	struct device *dev = &ggc->gbphy_dev->dev;
	struct gb_gpio_irq_mask_request request;
	int ret;

	request.which = hwirq;
	ret = gb_operation_sync(ggc->connection,
				GB_GPIO_TYPE_IRQ_MASK,
				&request, sizeof(request), NULL, 0);
	if (ret)
		dev_err(dev, "failed to mask irq: %d\n", ret);
}

static void _gb_gpio_irq_unmask(struct gb_gpio_controller *ggc, u8 hwirq)
{
	struct device *dev = &ggc->gbphy_dev->dev;
	struct gb_gpio_irq_unmask_request request;
	int ret;

	request.which = hwirq;
	ret = gb_operation_sync(ggc->connection,
				GB_GPIO_TYPE_IRQ_UNMASK,
				&request, sizeof(request), NULL, 0);
	if (ret)
		dev_err(dev, "failed to unmask irq: %d\n", ret);
}

static void _gb_gpio_irq_set_type(struct gb_gpio_controller *ggc,
				  u8 hwirq, u8 type)
{
	struct device *dev = &ggc->gbphy_dev->dev;
	struct gb_gpio_irq_type_request request;
	int ret;

	request.which = hwirq;
	request.type = type;

	ret = gb_operation_sync(ggc->connection,
				GB_GPIO_TYPE_IRQ_TYPE,
				&request, sizeof(request), NULL, 0);
	if (ret)
		dev_err(dev, "failed to set irq type: %d\n", ret);
}

static void gb_gpio_irq_mask(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_to_gpio_chip(d);
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);
	struct gb_gpio_line *line = &ggc->lines[d->hwirq];

	line->masked = true;
	line->masked_pending = true;
}

static void gb_gpio_irq_unmask(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_to_gpio_chip(d);
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);
	struct gb_gpio_line *line = &ggc->lines[d->hwirq];

	line->masked = false;
	line->masked_pending = true;
}

static int gb_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *chip = irq_data_to_gpio_chip(d);
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);
	struct gb_gpio_line *line = &ggc->lines[d->hwirq];
	struct device *dev = &ggc->gbphy_dev->dev;
	u8 irq_type;

	switch (type) {
	case IRQ_TYPE_NONE:
		irq_type = GB_GPIO_IRQ_TYPE_NONE;
		break;
	case IRQ_TYPE_EDGE_RISING:
		irq_type = GB_GPIO_IRQ_TYPE_EDGE_RISING;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		irq_type = GB_GPIO_IRQ_TYPE_EDGE_FALLING;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		irq_type = GB_GPIO_IRQ_TYPE_EDGE_BOTH;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		irq_type = GB_GPIO_IRQ_TYPE_LEVEL_LOW;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		irq_type = GB_GPIO_IRQ_TYPE_LEVEL_HIGH;
		break;
	default:
		dev_err(dev, "unsupported irq type: %u\n", type);
		return -EINVAL;
	}

	line->irq_type = irq_type;
	line->irq_type_pending = true;

	return 0;
}

static void gb_gpio_irq_bus_lock(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_to_gpio_chip(d);
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);

	mutex_lock(&ggc->irq_lock);
}

static void gb_gpio_irq_bus_sync_unlock(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_to_gpio_chip(d);
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);
	struct gb_gpio_line *line = &ggc->lines[d->hwirq];

	if (line->irq_type_pending) {
		_gb_gpio_irq_set_type(ggc, d->hwirq, line->irq_type);
		line->irq_type_pending = false;
	}

	if (line->masked_pending) {
		if (line->masked)
			_gb_gpio_irq_mask(ggc, d->hwirq);
		else
			_gb_gpio_irq_unmask(ggc, d->hwirq);
		line->masked_pending = false;
	}

	mutex_unlock(&ggc->irq_lock);
}

static int gb_gpio_request_handler(struct gb_operation *op)
{
	struct gb_connection *connection = op->connection;
	struct gb_gpio_controller *ggc = gb_connection_get_data(connection);
	struct device *dev = &ggc->gbphy_dev->dev;
	struct gb_message *request;
	struct gb_gpio_irq_event_request *event;
	u8 type = op->type;
	int irq;
	struct irq_desc *desc;

	if (type != GB_GPIO_TYPE_IRQ_EVENT) {
		dev_err(dev, "unsupported unsolicited request: %u\n", type);
		return -EINVAL;
	}

	request = op->request;

	if (request->payload_size < sizeof(*event)) {
		dev_err(dev, "short event received (%zu < %zu)\n",
			request->payload_size, sizeof(*event));
		return -EINVAL;
	}

	event = request->payload;
	if (event->which > ggc->line_max) {
		dev_err(dev, "invalid hw irq: %d\n", event->which);
		return -EINVAL;
	}

	irq = irq_find_mapping(ggc->chip.irq.domain, event->which);
	if (!irq) {
		dev_err(dev, "failed to find IRQ\n");
		return -EINVAL;
	}
	desc = irq_to_desc(irq);
	if (!desc) {
		dev_err(dev, "failed to look up irq\n");
		return -EINVAL;
	}

	local_irq_disable();
	generic_handle_irq_desc(desc);
	local_irq_enable();

	return 0;
}

static int gb_gpio_request(struct gpio_chip *chip, unsigned int offset)
{
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);

	return gb_gpio_activate_operation(ggc, (u8)offset);
}

static void gb_gpio_free(struct gpio_chip *chip, unsigned int offset)
{
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);

	gb_gpio_deactivate_operation(ggc, (u8)offset);
}

static int gb_gpio_get_direction(struct gpio_chip *chip, unsigned int offset)
{
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);
	u8 which;
	int ret;

	which = (u8)offset;
	ret = gb_gpio_get_direction_operation(ggc, which);
	if (ret)
		return ret;

	return ggc->lines[which].direction ? 1 : 0;
}

static int gb_gpio_direction_input(struct gpio_chip *chip, unsigned int offset)
{
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);

	return gb_gpio_direction_in_operation(ggc, (u8)offset);
}

static int gb_gpio_direction_output(struct gpio_chip *chip, unsigned int offset,
				    int value)
{
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);

	return gb_gpio_direction_out_operation(ggc, (u8)offset, !!value);
}

static int gb_gpio_get(struct gpio_chip *chip, unsigned int offset)
{
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);
	u8 which;
	int ret;

	which = (u8)offset;
	ret = gb_gpio_get_value_operation(ggc, which);
	if (ret)
		return ret;

	return ggc->lines[which].value;
}

static void gb_gpio_set(struct gpio_chip *chip, unsigned int offset, int value)
{
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);

	gb_gpio_set_value_operation(ggc, (u8)offset, !!value);
}

static int gb_gpio_set_config(struct gpio_chip *chip, unsigned int offset,
			      unsigned long config)
{
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);
	u32 debounce;

	if (pinconf_to_config_param(config) != PIN_CONFIG_INPUT_DEBOUNCE)
		return -ENOTSUPP;

	debounce = pinconf_to_config_argument(config);
	if (debounce > U16_MAX)
		return -EINVAL;

	return gb_gpio_set_debounce_operation(ggc, (u8)offset, (u16)debounce);
}

static int gb_gpio_controller_setup(struct gb_gpio_controller *ggc)
{
	int ret;

	/* Now find out how many lines there are */
	ret = gb_gpio_line_count_operation(ggc);
	if (ret)
		return ret;

	ggc->lines = kcalloc(ggc->line_max + 1, sizeof(*ggc->lines),
			     GFP_KERNEL);
	if (!ggc->lines)
		return -ENOMEM;

	return ret;
}

static int gb_gpio_probe(struct gbphy_device *gbphy_dev,
			 const struct gbphy_device_id *id)
{
	struct gb_connection *connection;
	struct gb_gpio_controller *ggc;
	struct gpio_chip *gpio;
	struct irq_chip *irqc;
	int ret;

	ggc = kzalloc(sizeof(*ggc), GFP_KERNEL);
	if (!ggc)
		return -ENOMEM;

	connection =
		gb_connection_create(gbphy_dev->bundle,
				     le16_to_cpu(gbphy_dev->cport_desc->id),
				     gb_gpio_request_handler);
	if (IS_ERR(connection)) {
		ret = PTR_ERR(connection);
		goto exit_ggc_free;
	}

	ggc->connection = connection;
	gb_connection_set_data(connection, ggc);
	ggc->gbphy_dev = gbphy_dev;
	gb_gbphy_set_data(gbphy_dev, ggc);

	ret = gb_connection_enable_tx(connection);
	if (ret)
		goto exit_connection_destroy;

	ret = gb_gpio_controller_setup(ggc);
	if (ret)
		goto exit_connection_disable;

	irqc = &ggc->irqc;
	irqc->irq_mask = gb_gpio_irq_mask;
	irqc->irq_unmask = gb_gpio_irq_unmask;
	irqc->irq_set_type = gb_gpio_irq_set_type;
	irqc->irq_bus_lock = gb_gpio_irq_bus_lock;
	irqc->irq_bus_sync_unlock = gb_gpio_irq_bus_sync_unlock;
	irqc->name = "greybus_gpio";

	mutex_init(&ggc->irq_lock);

	gpio = &ggc->chip;

	gpio->label = "greybus_gpio";
	gpio->parent = &gbphy_dev->dev;
	gpio->owner = THIS_MODULE;

	gpio->request = gb_gpio_request;
	gpio->free = gb_gpio_free;
	gpio->get_direction = gb_gpio_get_direction;
	gpio->direction_input = gb_gpio_direction_input;
	gpio->direction_output = gb_gpio_direction_output;
	gpio->get = gb_gpio_get;
	gpio->set = gb_gpio_set;
	gpio->set_config = gb_gpio_set_config;
	gpio->base = -1;		/* Allocate base dynamically */
	gpio->ngpio = ggc->line_max + 1;
	gpio->can_sleep = true;

	ret = gb_connection_enable(connection);
	if (ret)
		goto exit_line_free;

	ret = gpiochip_add(gpio);
	if (ret) {
		dev_err(&gbphy_dev->dev, "failed to add gpio chip: %d\n", ret);
		goto exit_line_free;
	}

	ret = gpiochip_irqchip_add(gpio, irqc, 0, handle_level_irq,
				   IRQ_TYPE_NONE);
	if (ret) {
		dev_err(&gbphy_dev->dev, "failed to add irq chip: %d\n", ret);
		goto exit_gpiochip_remove;
	}

	gbphy_runtime_put_autosuspend(gbphy_dev);
	return 0;

exit_gpiochip_remove:
	gpiochip_remove(gpio);
exit_line_free:
	kfree(ggc->lines);
exit_connection_disable:
	gb_connection_disable(connection);
exit_connection_destroy:
	gb_connection_destroy(connection);
exit_ggc_free:
	kfree(ggc);
	return ret;
}

static void gb_gpio_remove(struct gbphy_device *gbphy_dev)
{
	struct gb_gpio_controller *ggc = gb_gbphy_get_data(gbphy_dev);
	struct gb_connection *connection = ggc->connection;
	int ret;

	ret = gbphy_runtime_get_sync(gbphy_dev);
	if (ret)
		gbphy_runtime_get_noresume(gbphy_dev);

	gb_connection_disable_rx(connection);
	gpiochip_remove(&ggc->chip);
	gb_connection_disable(connection);
	gb_connection_destroy(connection);
	kfree(ggc->lines);
	kfree(ggc);
}

static const struct gbphy_device_id gb_gpio_id_table[] = {
	{ GBPHY_PROTOCOL(GREYBUS_PROTOCOL_GPIO) },
	{ },
};
MODULE_DEVICE_TABLE(gbphy, gb_gpio_id_table);

static struct gbphy_driver gpio_driver = {
	.name		= "gpio",
	.probe		= gb_gpio_probe,
	.remove		= gb_gpio_remove,
	.id_table	= gb_gpio_id_table,
};

module_gbphy_driver(gpio_driver);
MODULE_LICENSE("GPL v2");
