/*
 * GPIO Greybus driver.
 *
 * Copyright 2014 Google Inc.
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


/*
 * This request only uses the connection field, and if successful,
 * fills in the major and minor protocol version of the target.
 */
static int gb_gpio_proto_version_operation(struct gb_gpio_controller *gb_gpio_controller)
{
	struct gb_connection *connection = gb_gpio_controller->connection;
	struct gb_operation *operation;
	struct gb_gpio_proto_version_response *response;
	int ret;

	/* protocol version request has no payload */
	operation = gb_operation_create(connection,
					GB_GPIO_TYPE_PROTOCOL_VERSION,
					0, sizeof(*response));
	if (!operation)
		return -ENOMEM;

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("version operation failed (%d)\n", ret);
		goto out;
	}

	if (operation->result) {
		ret = gb_operation_status_map(operation->result);
		gb_connection_err(connection, "version result %hhu",
			operation->result);
	} else {
		response = operation->response.payload;
		if (response->major > GB_GPIO_VERSION_MAJOR) {
			pr_err("unsupported major version (%hhu > %hhu)\n",
				response->major, GB_GPIO_VERSION_MAJOR);
			ret = -ENOTSUPP;
			goto out;
		}
		gb_gpio_controller->version_major = response->major;
		gb_gpio_controller->version_minor = response->minor;

		pr_debug("%s: version_major = %u version_minor = %u\n", __func__,
			  gb_gpio_controller->version_major,
			  gb_gpio_controller->version_minor);
	}
out:
	gb_operation_destroy(operation);

	return ret;
}

static int gb_gpio_line_count_operation(struct gb_gpio_controller *gb_gpio_controller)
{
	struct gb_connection *connection = gb_gpio_controller->connection;
	struct gb_operation *operation;
	struct gb_gpio_line_count_response *response;
	int ret;

	/* line count request has no payload */
	operation = gb_operation_create(connection,
					GB_GPIO_TYPE_LINE_COUNT,
					0, sizeof(*response));
	if (!operation)
		return -ENOMEM;

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("line count operation failed (%d)\n", ret);
		goto out;
	}

	if (operation->result) {
		ret = gb_operation_status_map(operation->result);
		gb_connection_err(connection, "line count result %hhu",
			operation->result);
	} else {
		response = operation->response.payload;
		gb_gpio_controller->line_max = response->count;

		pr_debug("%s: count = %u\n", __func__,
			  gb_gpio_controller->line_max + 1);
	}
out:
	gb_operation_destroy(operation);

	return ret;
}

static int gb_gpio_activate_operation(struct gb_gpio_controller *gb_gpio_controller,
					u8 which)
{
	struct gb_connection *connection = gb_gpio_controller->connection;
	struct gb_operation *operation;
	struct gb_gpio_activate_request *request;
	int ret;

	if (which > gb_gpio_controller->line_max)
		return -EINVAL;

	/* activate response has no payload */
	operation = gb_operation_create(connection,
					GB_GPIO_TYPE_ACTIVATE,
					sizeof(*request), 0);
	if (!operation)
		return -ENOMEM;
	request = operation->request.payload;
	request->which = which;

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("activate operation failed (%d)\n", ret);
		goto out;
	}

	if (operation->result) {
		ret = gb_operation_status_map(operation->result);
		gb_connection_err(connection, "activate result %hhu",
			operation->result);
	} else {
		gb_gpio_controller->lines[which].active = true;

		pr_debug("%s: %u is now active\n", __func__, which);
	}
out:
	gb_operation_destroy(operation);

	return ret;
}

static int gb_gpio_deactivate_operation(struct gb_gpio_controller *gb_gpio_controller,
					u8 which)
{
	struct gb_connection *connection = gb_gpio_controller->connection;
	struct gb_operation *operation;
	struct gb_gpio_deactivate_request *request;
	int ret;

	if (which > gb_gpio_controller->line_max)
		return -EINVAL;

	/* deactivate response has no payload */
	operation = gb_operation_create(connection,
					GB_GPIO_TYPE_DEACTIVATE,
					sizeof(*request), 0);
	if (!operation)
		return -ENOMEM;
	request = operation->request.payload;
	request->which = which;

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("deactivate operation failed (%d)\n", ret);
		goto out;
	}

	if (operation->result) {
		ret = gb_operation_status_map(operation->result);
		gb_connection_err(connection, "deactivate result %hhu",
			operation->result);
	} else {
		gb_gpio_controller->lines[which].active = false;
		pr_debug("%s: %u is now inactive\n", __func__, which);
	}
out:
	gb_operation_destroy(operation);

	return ret;
}

static int gb_gpio_get_direction_operation(struct gb_gpio_controller *gb_gpio_controller,
					u8 which)
{
	struct gb_connection *connection = gb_gpio_controller->connection;
	struct gb_operation *operation;
	struct gb_gpio_get_direction_request *request;
	struct gb_gpio_get_direction_response *response;
	int ret;

	if (which > gb_gpio_controller->line_max)
		return -EINVAL;

	operation = gb_operation_create(connection,
					GB_GPIO_TYPE_GET_DIRECTION,
					sizeof(*request), sizeof(*response));
	if (!operation)
		return -ENOMEM;
	request = operation->request.payload;
	request->which = which;

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("get direction operation failed (%d)\n", ret);
		goto out;
	}

	if (operation->result) {
		ret = gb_operation_status_map(operation->result);
		gb_connection_err(connection, "get direction result %hhu",
			operation->result);
	} else {
		u8 direction;

		response = operation->response.payload;
		direction = response->direction;
		if (direction && direction != 1)
			pr_warn("gpio %u direction was %u (should be 0 or 1)\n",
				which, direction);
		gb_gpio_controller->lines[which].direction = direction ? 1 : 0;
		pr_debug("%s: direction of %u is %s\n", __func__, which,
			  direction ? "in" : "out");
	}
out:
	gb_operation_destroy(operation);

	return ret;
}

static int gb_gpio_direction_in_operation(struct gb_gpio_controller *gb_gpio_controller,
					u8 which)
{
	struct gb_connection *connection = gb_gpio_controller->connection;
	struct gb_operation *operation;
	struct gb_gpio_direction_in_request *request;
	int ret;

	if (which > gb_gpio_controller->line_max)
		return -EINVAL;

	/* direction_in response has no payload */
	operation = gb_operation_create(connection,
					GB_GPIO_TYPE_DIRECTION_IN,
					sizeof(*request), 0);
	if (!operation)
		return -ENOMEM;
	request = operation->request.payload;
	request->which = which;

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("direction in operation failed (%d)\n", ret);
		goto out;
	}

	if (operation->result) {
		ret = gb_operation_status_map(operation->result);
		gb_connection_err(connection, "direction in result %hhu",
			operation->result);
	} else {
		gb_gpio_controller->lines[which].direction = 1;
		pr_debug("%s: direction of %u is now in\n", __func__, which);
	}
out:
	gb_operation_destroy(operation);

	return ret;
}

static int gb_gpio_direction_out_operation(struct gb_gpio_controller *gb_gpio_controller,
					u8 which, bool value_high)
{
	struct gb_connection *connection = gb_gpio_controller->connection;
	struct gb_operation *operation;
	struct gb_gpio_direction_out_request *request;
	int ret;

	if (which > gb_gpio_controller->line_max)
		return -EINVAL;

	/* direction_out response has no payload */
	operation = gb_operation_create(connection,
					GB_GPIO_TYPE_DIRECTION_OUT,
					sizeof(*request), 0);
	if (!operation)
		return -ENOMEM;
	request = operation->request.payload;
	request->which = which;
	request->value = value_high ? 1 : 0;

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("direction out operation failed (%d)\n", ret);
		goto out;
	}

	if (operation->result) {
		ret = gb_operation_status_map(operation->result);
		gb_connection_err(connection, "direction out result %hhu",
			operation->result);
	} else {
		gb_gpio_controller->lines[which].direction = 0;
		pr_debug("%s: direction of %u is now out, value %s\n", __func__,
			  which, value_high ? "high" : "low");
	}
out:
	gb_operation_destroy(operation);

	return ret;
}

static int gb_gpio_get_value_operation(struct gb_gpio_controller *gb_gpio_controller,
					u8 which)
{
	struct gb_connection *connection = gb_gpio_controller->connection;
	struct gb_operation *operation;
	struct gb_gpio_get_value_request *request;
	struct gb_gpio_get_value_response *response;
	int ret;

	if (which > gb_gpio_controller->line_max)
		return -EINVAL;

	operation = gb_operation_create(connection,
					GB_GPIO_TYPE_GET_VALUE,
					sizeof(*request), sizeof(*response));
	if (!operation)
		return -ENOMEM;
	request = operation->request.payload;
	request->which = which;

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("get value operation failed (%d)\n", ret);
		goto out;
	}

	if (operation->result) {
		ret = gb_operation_status_map(operation->result);
		gb_connection_err(connection, "get value result %hhu",
			operation->result);
	} else {
		u8 value;

		response = operation->response.payload;
		value = response->value;
		if (value && value != 1)
			pr_warn("gpio %u value was %u (should be 0 or 1)\n",
				which, value);
		gb_gpio_controller->lines[which].value = value ? 1 : 0;
		/* XXX should this set direction to out? */
		pr_debug("%s: value of %u is %s\n", __func__, which,
			  gb_gpio_controller->lines[which].value ? "high" :
			  "low");
	}
out:
	gb_operation_destroy(operation);

	return ret;
}

static int gb_gpio_set_value_operation(struct gb_gpio_controller *gb_gpio_controller,
					u8 which, bool value_high)
{
	struct gb_connection *connection = gb_gpio_controller->connection;
	struct gb_operation *operation;
	struct gb_gpio_set_value_request *request;
	int ret;

	if (which > gb_gpio_controller->line_max)
		return -EINVAL;

	/* set_value response has no payload */
	operation = gb_operation_create(connection,
					GB_GPIO_TYPE_SET_VALUE,
					sizeof(*request), 0);
	if (!operation)
		return -ENOMEM;
	request = operation->request.payload;
	request->which = which;
	request->value = value_high ? 1 : 0;

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("set value operation failed (%d)\n", ret);
		goto out;
	}

	if (operation->result) {
		ret = gb_operation_status_map(operation->result);
		gb_connection_err(connection, "set value result %hhu",
			operation->result);
	} else {
		/* XXX should this set direction to out? */
		gb_gpio_controller->lines[which].value = request->value;
		pr_debug("%s: out value of %u is now %s\n", __func__, which,
			  gb_gpio_controller->lines[which].value ? "high" :
			  "low");
	}
out:
	gb_operation_destroy(operation);

	return ret;
}

static int gb_gpio_set_debounce_operation(struct gb_gpio_controller *gb_gpio_controller,
					u8 which, u16 debounce_usec)
{
	struct gb_connection *connection = gb_gpio_controller->connection;
	struct gb_operation *operation;
	struct gb_gpio_set_debounce_request *request;
	int ret;

	if (which > gb_gpio_controller->line_max)
		return -EINVAL;

	/* set_debounce response has no payload */
	operation = gb_operation_create(connection,
					GB_GPIO_TYPE_SET_DEBOUNCE,
					sizeof(*request), 0);
	if (!operation)
		return -ENOMEM;
	request = operation->request.payload;
	request->which = which;
	request->usec = cpu_to_le16(debounce_usec);

	/* Synchronous operation--no callback */
	ret = gb_operation_request_send(operation, NULL);
	if (ret) {
		pr_err("set debounce operation failed (%d)\n", ret);
		goto out;
	}

	if (operation->result) {
		ret = gb_operation_status_map(operation->result);
		gb_connection_err(connection, "set debounce result %hhu",
			operation->result);
	} else {
		gb_gpio_controller->lines[which].debounce_usec = le16_to_cpu(request->usec);
		pr_debug("%s: debounce of %u is now %hu usec\n", __func__, which,
			  gb_gpio_controller->lines[which].debounce_usec);
	}
out:
	gb_operation_destroy(operation);

	return ret;
}

static int gb_gpio_request(struct gpio_chip *chip, unsigned offset)
{
	struct gb_gpio_controller *gb_gpio_controller = gpio_chip_to_gb_gpio_controller(chip);
	int ret;

	if (offset < 0 || offset >= chip->ngpio)
		return -EINVAL;
	pr_debug("%s: passed check\n", __func__);
	ret = gb_gpio_activate_operation(gb_gpio_controller, (u8)offset);
	if (ret)
		;	/* return ret; */
	return 0;
}

static void gb_gpio_free(struct gpio_chip *chip, unsigned offset)
{
	struct gb_gpio_controller *gb_gpio_controller = gpio_chip_to_gb_gpio_controller(chip);
	int ret;

	if (offset < 0 || offset >= chip->ngpio) {
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

	if (offset < 0 || offset >= chip->ngpio)
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

	if (offset < 0 || offset >= chip->ngpio)
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

	if (offset < 0 || offset >= chip->ngpio)
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

	if (offset < 0 || offset >= chip->ngpio)
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

	if (offset < 0 || offset >= chip->ngpio)
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
	if (offset < 0 || offset >= chip->ngpio)
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
	ret = gb_gpio_proto_version_operation(gb_gpio_controller);
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
	connection->private = gb_gpio_controller;

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
	.id			= GREYBUS_PROTOCOL_GPIO,
	.major			= 0,
	.minor			= 1,
	.connection_init	= gb_gpio_connection_init,
	.connection_exit	= gb_gpio_connection_exit,
	.request_recv		= NULL,	/* no incoming requests */
};

bool gb_gpio_protocol_init(void)
{
	return gb_protocol_register(&gpio_protocol);
}

void gb_gpio_protocol_exit(void)
{
	gb_protocol_deregister(&gpio_protocol);
}
