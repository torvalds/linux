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
};
#define gpio_chip_to_gb_gpio_controller(chip) \
	container_of(chip, struct gb_gpio_controller, chip)

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
#define	GB_GPIO_TYPE_RESPONSE		0x80	/* OR'd with rest */

#define	GB_GPIO_DEBOUNCE_USEC_DEFAULT	0	/* microseconds */

/* version request has no payload */
struct gb_gpio_proto_version_response {
	__u8	major;
	__u8	minor;
};

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
	__le16	usec;
};
/* debounce response has no payload */


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

	if (which > ggc->line_max)
		return -EINVAL;

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

	if (which > ggc->line_max)
		return -EINVAL;

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

	if (which > ggc->line_max)
		return -EINVAL;

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

	if (which > ggc->line_max)
		return -EINVAL;

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

	if (which > ggc->line_max)
		return -EINVAL;

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

	if (which > ggc->line_max)
		return -EINVAL;

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

	if (which > ggc->line_max)
		return -EINVAL;

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

	if (which > ggc->line_max)
		return -EINVAL;

	request.which = which;
	request.usec = cpu_to_le16(debounce_usec);
	ret = gb_operation_sync(ggc->connection, GB_GPIO_TYPE_SET_DEBOUNCE,
				&request, sizeof(request), NULL, 0);
	if (!ret)
		ggc->lines[which].debounce_usec = debounce_usec;
	return ret;
}

static int gb_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct gb_gpio_controller *gb_gpio_controller = gpio_chip_to_gb_gpio_controller(chip);
	int ret;

	if (offset >= chip->ngpio)
		return -EINVAL;
	ret = gb_gpio_activate_operation(gb_gpio_controller, (u8)offset);
	if (ret)
		;	/* return ret; */
	return 0;
}

static void gb_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct gb_gpio_controller *gb_gpio_controller = gpio_chip_to_gb_gpio_controller(chip);
	int ret;

	if (offset >= chip->ngpio) {
		pr_err("bad offset %u supplied (must be 0..%u)\n",
			offset, chip->ngpio - 1);
		return;
	}
	ret = gb_gpio_deactivate_operation(gb_gpio_controller, (u8)offset);
	if (ret)
		;	/* return ret; */
}

static int gb_gpio_get_direction(struct gpio_chip *chip, unsigned offset)
{
	struct gb_gpio_controller *gb_gpio_controller = gpio_chip_to_gb_gpio_controller(chip);
	u8 which;
	int ret;

	if (offset >= chip->ngpio)
		return -EINVAL;
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

	if (offset >= chip->ngpio)
		return -EINVAL;
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

	if (offset >= chip->ngpio)
		return -EINVAL;
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

	if (offset >= chip->ngpio)
		return -EINVAL;
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

	if (offset < 0 || offset >= chip->ngpio) {
		pr_err("bad offset %u supplied (must be 0..%u)\n",
			offset, chip->ngpio - 1);
		return;
	}
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

	if (offset >= chip->ngpio)
		return -EINVAL;
	if (debounce > (unsigned int)U16_MAX)
		return -EINVAL;
	usec = (u8)debounce;
	ret = gb_gpio_set_debounce_operation(gb_gpio_controller, (u8)offset, usec);
	if (ret)
		;	/* return ret; */

	return 0;	/* XXX */
}

static int gb_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	if (offset >= chip->ngpio)
		return -EINVAL;

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

static int gb_gpio_connection_init(struct gb_connection *connection)
{
	struct gb_gpio_controller *gb_gpio_controller;
	struct gpio_chip *gpio;
	int ret;

	gb_gpio_controller = kzalloc(sizeof(*gb_gpio_controller), GFP_KERNEL);
	if (!gb_gpio_controller)
		return -ENOMEM;
	gb_gpio_controller->connection = connection;
	connection->private = gb_gpio_controller;

	ret = gb_gpio_controller_setup(gb_gpio_controller);
	if (ret)
		goto out_err;

	gpio = &gb_gpio_controller->chip;

	gpio->label = "greybus_gpio";
	gpio->owner = THIS_MODULE;	/* XXX Module get? */

	gpio->request = gb_gpio_request;
	gpio->free = gb_gpio_free;
	gpio->get_direction = gb_gpio_get_direction;
	gpio->direction_input = gb_gpio_direction_input;
	gpio->direction_output = gb_gpio_direction_output;
	gpio->get = gb_gpio_get;
	gpio->set = gb_gpio_set;
	gpio->set_debounce = gb_gpio_set_debounce;
	gpio->to_irq = gb_gpio_to_irq;
	gpio->dbg_show = gb_gpio_dbg_show;

	gpio->base = -1;		/* Allocate base dynamically */
	gpio->ngpio = gb_gpio_controller->line_max + 1;
	gpio->can_sleep = true;		/* XXX */

	ret = gpiochip_add(gpio);
	if (ret) {
		pr_err("Failed to register GPIO\n");
		return ret;
	}

	return 0;
out_err:
	kfree(gb_gpio_controller);
	return ret;
}

static void gb_gpio_connection_exit(struct gb_connection *connection)
{
	struct gb_gpio_controller *gb_gpio_controller = connection->private;

	if (!gb_gpio_controller)
		return;

	gb_gpiochip_remove(&gb_gpio_controller->chip);
	/* kref_put(gb_gpio_controller->connection) */
	kfree(gb_gpio_controller);
}

static struct gb_protocol gpio_protocol = {
	.name			= "gpio",
	.id			= GREYBUS_PROTOCOL_GPIO,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_gpio_connection_init,
	.connection_exit	= gb_gpio_connection_exit,
	.request_recv		= NULL,	/* no incoming requests */
};

int gb_gpio_protocol_init(void)
{
	return gb_protocol_register(&gpio_protocol);
}

void gb_gpio_protocol_exit(void)
{
	gb_protocol_deregister(&gpio_protocol);
}
