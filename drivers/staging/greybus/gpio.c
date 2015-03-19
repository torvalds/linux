/*
 * GPIO Greybus driver.
 *
 * Copyright 2014 Google Inc.
 * Copyright 2014 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include "greybus.h"

struct gb_gpio_line {
	/* The following has to be an array of line_max entries */
	/* --> make them just a flags field */
	u8			active:    1,
				direction: 1,	/* 0 = output, 1 = input */
				value:     1;	/* 0 = low, 1 = high */
	u16			debounce_usec;
};

struct gb_gpio_controller {
	struct gb_connection	*connection;
	u8			version_major;
	u8			version_minor;
	u8			line_max;	/* max line number */
	struct gb_gpio_line	*lines;

	struct gpio_chip	chip;
	struct irq_chip		irqc;
	struct irq_chip		*irqchip;
	struct irq_domain	*irqdomain;
	unsigned int		irq_base;
	irq_flow_handler_t	irq_handler;
	unsigned int		irq_default_type;
};
#define gpio_chip_to_gb_gpio_controller(chip) \
	container_of(chip, struct gb_gpio_controller, chip)
#define irq_data_to_gpio_chip(d) (d->domain->host_data)

/* Version of the Greybus GPIO protocol we support */
#define	GB_GPIO_VERSION_MAJOR		0x00
#define	GB_GPIO_VERSION_MINOR		0x01

/* Greybus GPIO request types */
#define	GB_GPIO_TYPE_INVALID		0x00
#define	GB_GPIO_TYPE_PROTOCOL_VERSION	0x01
#define	GB_GPIO_TYPE_LINE_COUNT		0x02
#define	GB_GPIO_TYPE_ACTIVATE		0x03
#define	GB_GPIO_TYPE_DEACTIVATE		0x04
#define	GB_GPIO_TYPE_GET_DIRECTION	0x05
#define	GB_GPIO_TYPE_DIRECTION_IN	0x06
#define	GB_GPIO_TYPE_DIRECTION_OUT	0x07
#define	GB_GPIO_TYPE_GET_VALUE		0x08
#define	GB_GPIO_TYPE_SET_VALUE		0x09
#define	GB_GPIO_TYPE_SET_DEBOUNCE	0x0a
#define GB_GPIO_TYPE_IRQ_TYPE		0x0b
#define GB_GPIO_TYPE_IRQ_ACK		0x0c
#define GB_GPIO_TYPE_IRQ_MASK		0x0d
#define GB_GPIO_TYPE_IRQ_UNMASK		0x0e
#define GB_GPIO_TYPE_IRQ_EVENT		0x0f
#define	GB_GPIO_TYPE_RESPONSE		0x80	/* OR'd with rest */

#define	GB_GPIO_DEBOUNCE_USEC_DEFAULT	0	/* microseconds */

/* line count request has no payload */
struct gb_gpio_line_count_response {
	__u8	count;
};

struct gb_gpio_activate_request {
	__u8	which;
};
/* activate response has no payload */

struct gb_gpio_deactivate_request {
	__u8	which;
};
/* deactivate response has no payload */

struct gb_gpio_get_direction_request {
	__u8	which;
};
struct gb_gpio_get_direction_response {
	__u8	direction;
};

struct gb_gpio_direction_in_request {
	__u8	which;
};
/* direction in response has no payload */

struct gb_gpio_direction_out_request {
	__u8	which;
	__u8	value;
};
/* direction out response has no payload */

struct gb_gpio_get_value_request {
	__u8	which;
};
struct gb_gpio_get_value_response {
	__u8	value;
};

struct gb_gpio_set_value_request {
	__u8	which;
	__u8	value;
};
/* set value response has no payload */

struct gb_gpio_set_debounce_request {
	__u8	which;
	__le16	usec __packed;
};
/* debounce response has no payload */

struct gb_gpio_irq_type_request {
	__u8	which;
	__u8	type;
};
/* irq type response has no payload */

struct gb_gpio_irq_mask_request {
	__u8	which;
};
/* irq mask response has no payload */

struct gb_gpio_irq_unmask_request {
	__u8	which;
};
/* irq unmask response has no payload */

struct gb_gpio_irq_ack_request {
	__u8	which;
};
/* irq ack response has no payload */

/* irq event requests originate on another module and are handled on the AP */
struct gb_gpio_irq_event_request {
	__u8	which;
};
/* irq event response has no payload */

/* Define get_version() routine */
define_get_version(gb_gpio_controller, GPIO);

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
	int ret;

	request.which = which;
	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_ACTIVATE,
				 &request, sizeof(request), NULL, 0);
	if (!ret)
		ggc->lines[which].active = true;
	return ret;
}

static int gb_gpio_deactivate_operation(struct gb_gpio_controller *ggc,
					u8 which)
{
	struct gb_gpio_deactivate_request request;
	int ret;

	request.which = which;
	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_DEACTIVATE,
				 &request, sizeof(request), NULL, 0);
	if (!ret)
		ggc->lines[which].active = false;
	return ret;
}

static int gb_gpio_get_direction_operation(struct gb_gpio_controller *ggc,
					u8 which)
{
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
	if (direction && direction != 1)
		pr_warn("gpio %u direction was %u (should be 0 or 1)\n",
			which, direction);
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
	struct gb_gpio_get_value_request request;
	struct gb_gpio_get_value_response response;
	int ret;
	u8 value;

	request.which = which;
	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_GET_VALUE,
				&request, sizeof(request),
				&response, sizeof(response));
	if (ret)
		return ret;

	value = response.value;
	if (value && value != 1)
		pr_warn("gpio %u value was %u (should be 0 or 1)\n",
			which, value);
	ggc->lines[which].value = value ? 1 : 0;
	return 0;
}

static int gb_gpio_set_value_operation(struct gb_gpio_controller *ggc,
					u8 which, bool value_high)
{
	struct gb_gpio_set_value_request request;
	int ret;

	request.which = which;
	request.value = value_high ? 1 : 0;
	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_SET_VALUE,
				&request, sizeof(request), NULL, 0);
	if (!ret) {
		/* XXX should this set direction to out? */
		ggc->lines[which].value = request.value;
	}
	return ret;
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

static void gb_gpio_ack_irq(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_to_gpio_chip(d);
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);
	struct gb_gpio_irq_ack_request request;
	int ret;

	request.which = d->hwirq;
	ret = gb_operation_sync(ggc->connection,
				GB_GPIO_TYPE_IRQ_ACK,
				&request, sizeof(request), NULL, 0);
	if (ret)
		pr_err("irq ack operation failed (%d)\n", ret);
}

static void gb_gpio_mask_irq(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_to_gpio_chip(d);
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);
	struct gb_gpio_irq_mask_request request;
	int ret;

	request.which = d->hwirq;
	ret = gb_operation_sync(ggc->connection,
				GB_GPIO_TYPE_IRQ_MASK,
				&request, sizeof(request), NULL, 0);
	if (ret)
		pr_err("irq mask operation failed (%d)\n", ret);
}

static void gb_gpio_unmask_irq(struct irq_data *d)
{
	struct gpio_chip *chip = irq_data_to_gpio_chip(d);
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);
	struct gb_gpio_irq_unmask_request request;
	int ret;

	request.which = d->hwirq;
	ret = gb_operation_sync(ggc->connection,
				GB_GPIO_TYPE_IRQ_UNMASK,
				&request, sizeof(request), NULL, 0);
	if (ret)
		pr_err("irq unmask operation failed (%d)\n", ret);
}

static int gb_gpio_irq_set_type(struct irq_data *d, unsigned int type)
{
	struct gpio_chip *chip = irq_data_to_gpio_chip(d);
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);
	struct gb_gpio_irq_type_request request;
	int ret = 0;

	request.which = d->hwirq;
	request.type = type;

	switch (type) {
	case IRQ_TYPE_NONE:
		break;
	case IRQ_TYPE_EDGE_RISING:
	case IRQ_TYPE_EDGE_FALLING:
	case IRQ_TYPE_EDGE_BOTH:
	case IRQ_TYPE_LEVEL_LOW:
	case IRQ_TYPE_LEVEL_HIGH:
		ret = gb_operation_sync(ggc->connection,
					GB_GPIO_TYPE_IRQ_TYPE,
					&request, sizeof(request), NULL, 0);
		if (ret)
			pr_err("irq type operation failed (%d)\n", ret);
                break;
	default:
		pr_err("No such irq type %d", type);
		ret = -EINVAL;
	}

	return ret;
}

static void gb_gpio_request_recv(u8 type, struct gb_operation *op)
{
	struct gb_gpio_controller *ggc;
	struct gb_connection *connection;
	struct gb_message *request;
	struct gb_gpio_irq_event_request *event;
	int irq;
	struct irq_desc *desc;
	int ret;

	if (type != GB_GPIO_TYPE_IRQ_EVENT) {
		pr_err("unsupported unsolicited request\n");
		return;
	}

	connection = op->connection;
	ggc = connection->private;

	request = op->request;
	event = request->payload;
	if (event->which > ggc->line_max) {
		pr_err("Unsupported hw irq %d\n", event->which);
		return;
	}
	irq = gpio_to_irq(ggc->chip.base + event->which);
	desc = irq_to_desc(irq);

	/* Dispatch interrupt */
	local_irq_disable();
	handle_simple_irq(irq, desc);
	local_irq_enable();

	ret = gb_operation_response_send(op, 0);
	if (ret)
		pr_err("error %d sending response status %d\n", ret, 0);
}

static int gb_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct gb_gpio_controller *gb_gpio_controller = gpio_chip_to_gb_gpio_controller(chip);
	int ret;

	ret = gb_gpio_activate_operation(gb_gpio_controller, (u8)offset);
	if (ret)
		;	/* return ret; */
	return 0;
}

static void gb_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct gb_gpio_controller *gb_gpio_controller = gpio_chip_to_gb_gpio_controller(chip);
	int ret;

	ret = gb_gpio_deactivate_operation(gb_gpio_controller, (u8)offset);
	if (ret)
		;	/* return ret; */
}

static int gb_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct gb_gpio_controller *gb_gpio_controller = gpio_chip_to_gb_gpio_controller(chip);
	u8 which;
	int ret;

	which = (u8)offset;
	ret = gb_gpio_get_direction_operation(gb_gpio_controller, which);
	if (ret)
		;	/* return ret; */
	return gb_gpio_controller->lines[which].direction ? 1 : 0;
}

static int gb_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct gb_gpio_controller *gb_gpio_controller = gpio_chip_to_gb_gpio_controller(chip);
	int ret;

	ret = gb_gpio_direction_in_operation(gb_gpio_controller, (u8)offset);
	if (ret)
		;	/* return ret; */
	return 0;
}

static int gb_gpio_direction_output(struct gpio_chip *chip, unsigned offset,
					int value)
{
	struct gb_gpio_controller *gb_gpio_controller = gpio_chip_to_gb_gpio_controller(chip);
	int ret;

	ret = gb_gpio_direction_out_operation(gb_gpio_controller, (u8)offset, !!value);
	if (ret)
		;	/* return ret; */
	return 0;
}

static int gb_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct gb_gpio_controller *gb_gpio_controller = gpio_chip_to_gb_gpio_controller(chip);
	u8 which;
	int ret;

	which = (u8)offset;
	ret = gb_gpio_get_value_operation(gb_gpio_controller, which);
	if (ret)
		return ret;
	return (int)gb_gpio_controller->lines[which].value;
}

static void gb_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct gb_gpio_controller *gb_gpio_controller = gpio_chip_to_gb_gpio_controller(chip);
	int ret;

	ret = gb_gpio_set_value_operation(gb_gpio_controller, (u8)offset, !!value);
	if (ret)
		;	/* return ret; */
}

static int gb_gpio_set_debounce(struct gpio_chip *chip, unsigned offset,
					unsigned debounce)
{
	struct gb_gpio_controller *gb_gpio_controller = gpio_chip_to_gb_gpio_controller(chip);
	u16 usec;
	int ret;

	if (debounce > U16_MAX)
		return -EINVAL;
	usec = (u16)debounce;
	ret = gb_gpio_set_debounce_operation(gb_gpio_controller, (u8)offset, usec);
	if (ret)
		;	/* return ret; */

	return 0;	/* XXX */
}

static void gb_gpio_dbg_show(struct seq_file *s, struct gpio_chip *chip)
{
	return;	/* XXX */
}

static int gb_gpio_controller_setup(struct gb_gpio_controller *gb_gpio_controller)
{
	u32 line_count;
	size_t size;
	int ret;

	/* First thing we need to do is check the version */
	ret = get_version(gb_gpio_controller);
	if (ret)
		;	/* return ret; */

	/* Now find out how many lines there are */
	ret = gb_gpio_line_count_operation(gb_gpio_controller);
	if (ret)
		;	/* return ret; */
	line_count = (u32)gb_gpio_controller->line_max + 1;
	size = line_count * sizeof(*gb_gpio_controller->lines);
	gb_gpio_controller->lines = kzalloc(size, GFP_KERNEL);
	if (!gb_gpio_controller->lines)
		return -ENOMEM;

	return ret;
}

/**
 * gb_gpio_irq_map() - maps an IRQ into a GB gpio irqchip
 * @d: the irqdomain used by this irqchip
 * @irq: the global irq number used by this GB gpio irqchip irq
 * @hwirq: the local IRQ/GPIO line offset on this GB gpio
 *
 * This function will set up the mapping for a certain IRQ line on a
 * GB gpio by assigning the GB gpio as chip data, and using the irqchip
 * stored inside the GB gpio.
 */
static int gb_gpio_irq_map(struct irq_domain *domain, unsigned int irq,
			   irq_hw_number_t hwirq)
{
	struct gpio_chip *chip = domain->host_data;
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);

	irq_set_chip_data(irq, ggc);
	irq_set_chip_and_handler(irq, ggc->irqchip, ggc->irq_handler);
#ifdef CONFIG_ARM
	set_irq_flags(irq, IRQF_VALID);
#else
	irq_set_noprobe(irq);
#endif
	/*
	 * No set-up of the hardware will happen if IRQ_TYPE_NONE
	 * is passed as default type.
	 */
	if (ggc->irq_default_type != IRQ_TYPE_NONE)
		irq_set_irq_type(irq, ggc->irq_default_type);

	return 0;
}

static void gb_gpio_irq_unmap(struct irq_domain *d, unsigned int irq)
{
#ifdef CONFIG_ARM
	set_irq_flags(irq, 0);
#endif
	irq_set_chip_and_handler(irq, NULL, NULL);
	irq_set_chip_data(irq, NULL);
}

static const struct irq_domain_ops gb_gpio_domain_ops = {
	.map	= gb_gpio_irq_map,
	.unmap	= gb_gpio_irq_unmap,
};

/**
 * gb_gpio_irqchip_remove() - removes an irqchip added to a gb_gpio_controller
 * @ggc: the gb_gpio_controller to remove the irqchip from
 *
 * This is called only from gb_gpio_remove()
 */
static void gb_gpio_irqchip_remove(struct gb_gpio_controller *ggc)
{
	unsigned int offset;

	/* Remove all IRQ mappings and delete the domain */
	if (ggc->irqdomain) {
		for (offset = 0; offset < (ggc->line_max + 1); offset++)
			irq_dispose_mapping(irq_find_mapping(ggc->irqdomain, offset));
		irq_domain_remove(ggc->irqdomain);
	}

	if (ggc->irqchip) {
		ggc->irqchip = NULL;
	}
}


/**
 * gb_gpio_irqchip_add() - adds an irqchip to a gpio chip
 * @chip: the gpio chip to add the irqchip to
 * @irqchip: the irqchip to add to the adapter
 * @first_irq: if not dynamically assigned, the base (first) IRQ to
 * allocate gpio irqs from
 * @handler: the irq handler to use (often a predefined irq core function)
 * @type: the default type for IRQs on this irqchip, pass IRQ_TYPE_NONE
 * to have the core avoid setting up any default type in the hardware.
 *
 * This function closely associates a certain irqchip with a certain
 * gpio chip, providing an irq domain to translate the local IRQs to
 * global irqs, and making sure that the gpio chip
 * is passed as chip data to all related functions. Driver callbacks
 * need to use container_of() to get their local state containers back
 * from the gpio chip passed as chip data. An irqdomain will be stored
 * in the gpio chip that shall be used by the driver to handle IRQ number
 * translation. The gpio chip will need to be initialized and registered
 * before calling this function.
 */
static int gb_gpio_irqchip_add(struct gpio_chip *chip,
			 struct irq_chip *irqchip,
			 unsigned int first_irq,
			 irq_flow_handler_t handler,
			 unsigned int type)
{
	struct gb_gpio_controller *ggc;
	unsigned int offset;
	unsigned irq_base;

	if (!chip || !irqchip)
		return -EINVAL;

	ggc = gpio_chip_to_gb_gpio_controller(chip);

	ggc->irqchip = irqchip;
	ggc->irq_handler = handler;
	ggc->irq_default_type = type;
	ggc->irqdomain = irq_domain_add_simple(NULL,
					ggc->line_max + 1, first_irq,
					&gb_gpio_domain_ops, chip);
	if (!ggc->irqdomain) {
		ggc->irqchip = NULL;
		return -EINVAL;
	}

	/*
	 * Prepare the mapping since the irqchip shall be orthogonal to
	 * any gpio calls. If the first_irq was zero, this is
	 * necessary to allocate descriptors for all IRQs.
	 */
	for (offset = 0; offset < (ggc->line_max + 1); offset++) {
		irq_base = irq_create_mapping(ggc->irqdomain, offset);
		if (offset == 0)
			ggc->irq_base = irq_base;
	}

	return 0;
}

static int gb_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct gb_gpio_controller *ggc = gpio_chip_to_gb_gpio_controller(chip);

	return irq_find_mapping(ggc->irqdomain, offset);
}

static int gb_gpio_connection_init(struct gb_connection *connection)
{
	struct gb_gpio_controller *gb_gpio_controller;
	struct gpio_chip *gpio;
	struct irq_chip *irqc;
	int ret;

	gb_gpio_controller = kzalloc(sizeof(*gb_gpio_controller), GFP_KERNEL);
	if (!gb_gpio_controller)
		return -ENOMEM;
	gb_gpio_controller->connection = connection;
	connection->private = gb_gpio_controller;

	ret = gb_gpio_controller_setup(gb_gpio_controller);
	if (ret)
		goto err_free_controller;

	irqc = &gb_gpio_controller->irqc;
	irqc->irq_ack = gb_gpio_ack_irq;
	irqc->irq_mask = gb_gpio_mask_irq;
	irqc->irq_unmask = gb_gpio_unmask_irq;
	irqc->irq_set_type = gb_gpio_irq_set_type;
	irqc->name = "greybus_gpio";

	gpio = &gb_gpio_controller->chip;

	gpio->label = "greybus_gpio";
	gpio->dev = &connection->dev;
	gpio->owner = THIS_MODULE;

	gpio->request = gb_gpio_request;
	gpio->free = gb_gpio_free;
	gpio->get_direction = gb_gpio_get_direction;
	gpio->direction_input = gb_gpio_direction_input;
	gpio->direction_output = gb_gpio_direction_output;
	gpio->get = gb_gpio_get;
	gpio->set = gb_gpio_set;
	gpio->set_debounce = gb_gpio_set_debounce;
	gpio->dbg_show = gb_gpio_dbg_show;
	gpio->to_irq = gb_gpio_to_irq;
	gpio->base = -1;		/* Allocate base dynamically */
	gpio->ngpio = gb_gpio_controller->line_max + 1;
	gpio->can_sleep = true;

	ret = gpiochip_add(gpio);
	if (ret) {
		pr_err("Failed to register GPIO\n");
		goto err_free_lines;
	}

	ret = gb_gpio_irqchip_add(gpio, irqc, 0,
				   handle_simple_irq, IRQ_TYPE_NONE);
	if (ret) {
		pr_err("Couldn't add irqchip to Greybus GPIO controller %d\n", ret);
		goto irqchip_err;
	}

	return 0;

irqchip_err:
	gb_gpiochip_remove(gpio);
err_free_lines:
	kfree(gb_gpio_controller->lines);
err_free_controller:
	kfree(gb_gpio_controller);
	return ret;
}

static void gb_gpio_connection_exit(struct gb_connection *connection)
{
	struct gb_gpio_controller *gb_gpio_controller = connection->private;

	if (!gb_gpio_controller)
		return;

	gb_gpio_irqchip_remove(gb_gpio_controller);
	gb_gpiochip_remove(&gb_gpio_controller->chip);
	/* kref_put(gb_gpio_controller->connection) */
	kfree(gb_gpio_controller->lines);
	kfree(gb_gpio_controller);
}

static struct gb_protocol gpio_protocol = {
	.name			= "gpio",
	.id			= GREYBUS_PROTOCOL_GPIO,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_gpio_connection_init,
	.connection_exit	= gb_gpio_connection_exit,
	.request_recv		= gb_gpio_request_recv,
};

int gb_gpio_protocol_init(void)
{
	return gb_protocol_register(&gpio_protocol);
}

void gb_gpio_protocol_exit(void)
{
	gb_protocol_deregister(&gpio_protocol);
}
