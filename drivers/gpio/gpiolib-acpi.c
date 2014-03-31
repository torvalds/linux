/*
 * ACPI helpers for GPIO API
 *
 * Copyright (C) 2012, Intel Corporation
 * Authors: Mathias Nyman <mathias.nyman@linux.intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>

#include "gpiolib.h"

struct acpi_gpio_event {
	struct list_head node;
	acpi_handle handle;
	unsigned int pin;
	unsigned int irq;
};

struct acpi_gpio_connection {
	struct list_head node;
	struct gpio_desc *desc;
};

struct acpi_gpio_chip {
	/*
	 * ACPICA requires that the first field of the context parameter
	 * passed to acpi_install_address_space_handler() is large enough
	 * to hold struct acpi_connection_info.
	 */
	struct acpi_connection_info conn_info;
	struct list_head conns;
	struct mutex conn_lock;
	struct gpio_chip *chip;
	struct list_head events;
};

static int acpi_gpiochip_find(struct gpio_chip *gc, void *data)
{
	if (!gc->dev)
		return false;

	return ACPI_HANDLE(gc->dev) == data;
}

/**
 * acpi_get_gpiod() - Translate ACPI GPIO pin to GPIO descriptor usable with GPIO API
 * @path:	ACPI GPIO controller full path name, (e.g. "\\_SB.GPO1")
 * @pin:	ACPI GPIO pin number (0-based, controller-relative)
 *
 * Returns GPIO descriptor to use with Linux generic GPIO API, or ERR_PTR
 * error value
 */

static struct gpio_desc *acpi_get_gpiod(char *path, int pin)
{
	struct gpio_chip *chip;
	acpi_handle handle;
	acpi_status status;

	status = acpi_get_handle(NULL, path, &handle);
	if (ACPI_FAILURE(status))
		return ERR_PTR(-ENODEV);

	chip = gpiochip_find(handle, acpi_gpiochip_find);
	if (!chip)
		return ERR_PTR(-ENODEV);

	if (pin < 0 || pin > chip->ngpio)
		return ERR_PTR(-EINVAL);

	return gpiochip_get_desc(chip, pin);
}

static irqreturn_t acpi_gpio_irq_handler(int irq, void *data)
{
	struct acpi_gpio_event *event = data;

	acpi_evaluate_object(event->handle, NULL, NULL, NULL);

	return IRQ_HANDLED;
}

static irqreturn_t acpi_gpio_irq_handler_evt(int irq, void *data)
{
	struct acpi_gpio_event *event = data;

	acpi_execute_simple_method(event->handle, NULL, event->pin);

	return IRQ_HANDLED;
}

static void acpi_gpio_chip_dh(acpi_handle handle, void *data)
{
	/* The address of this function is used as a key. */
}

static acpi_status acpi_gpiochip_request_interrupt(struct acpi_resource *ares,
						   void *context)
{
	struct acpi_gpio_chip *acpi_gpio = context;
	struct gpio_chip *chip = acpi_gpio->chip;
	struct acpi_resource_gpio *agpio;
	acpi_handle handle, evt_handle;
	struct acpi_gpio_event *event;
	irq_handler_t handler = NULL;
	struct gpio_desc *desc;
	unsigned long irqflags;
	int ret, pin, irq;

	if (ares->type != ACPI_RESOURCE_TYPE_GPIO)
		return AE_OK;

	agpio = &ares->data.gpio;
	if (agpio->connection_type != ACPI_RESOURCE_GPIO_TYPE_INT)
		return AE_OK;

	handle = ACPI_HANDLE(chip->dev);
	pin = agpio->pin_table[0];

	if (pin <= 255) {
		char ev_name[5];
		sprintf(ev_name, "_%c%02X",
			agpio->triggering == ACPI_EDGE_SENSITIVE ? 'E' : 'L',
			pin);
		if (ACPI_SUCCESS(acpi_get_handle(handle, ev_name, &evt_handle)))
			handler = acpi_gpio_irq_handler;
	}
	if (!handler) {
		if (ACPI_SUCCESS(acpi_get_handle(handle, "_EVT", &evt_handle)))
			handler = acpi_gpio_irq_handler_evt;
	}
	if (!handler)
		return AE_BAD_PARAMETER;

	desc = gpiochip_get_desc(chip, pin);
	if (IS_ERR(desc)) {
		dev_err(chip->dev, "Failed to get GPIO descriptor\n");
		return AE_ERROR;
	}

	ret = gpiochip_request_own_desc(desc, "ACPI:Event");
	if (ret) {
		dev_err(chip->dev, "Failed to request GPIO\n");
		return AE_ERROR;
	}

	gpiod_direction_input(desc);

	ret = gpiod_lock_as_irq(desc);
	if (ret) {
		dev_err(chip->dev, "Failed to lock GPIO as interrupt\n");
		goto fail_free_desc;
	}

	irq = gpiod_to_irq(desc);
	if (irq < 0) {
		dev_err(chip->dev, "Failed to translate GPIO to IRQ\n");
		goto fail_unlock_irq;
	}

	irqflags = IRQF_ONESHOT;
	if (agpio->triggering == ACPI_LEVEL_SENSITIVE) {
		if (agpio->polarity == ACPI_ACTIVE_HIGH)
			irqflags |= IRQF_TRIGGER_HIGH;
		else
			irqflags |= IRQF_TRIGGER_LOW;
	} else {
		switch (agpio->polarity) {
		case ACPI_ACTIVE_HIGH:
			irqflags |= IRQF_TRIGGER_RISING;
			break;
		case ACPI_ACTIVE_LOW:
			irqflags |= IRQF_TRIGGER_FALLING;
			break;
		default:
			irqflags |= IRQF_TRIGGER_RISING |
				    IRQF_TRIGGER_FALLING;
			break;
		}
	}

	event = kzalloc(sizeof(*event), GFP_KERNEL);
	if (!event)
		goto fail_unlock_irq;

	event->handle = evt_handle;
	event->irq = irq;
	event->pin = pin;

	ret = request_threaded_irq(event->irq, NULL, handler, irqflags,
				   "ACPI:Event", event);
	if (ret) {
		dev_err(chip->dev, "Failed to setup interrupt handler for %d\n",
			event->irq);
		goto fail_free_event;
	}

	list_add_tail(&event->node, &acpi_gpio->events);
	return AE_OK;

fail_free_event:
	kfree(event);
fail_unlock_irq:
	gpiod_unlock_as_irq(desc);
fail_free_desc:
	gpiochip_free_own_desc(desc);

	return AE_ERROR;
}

/**
 * acpi_gpiochip_request_interrupts() - Register isr for gpio chip ACPI events
 * @acpi_gpio:      ACPI GPIO chip
 *
 * ACPI5 platforms can use GPIO signaled ACPI events. These GPIO interrupts are
 * handled by ACPI event methods which need to be called from the GPIO
 * chip's interrupt handler. acpi_gpiochip_request_interrupts finds out which
 * gpio pins have acpi event methods and assigns interrupt handlers that calls
 * the acpi event methods for those pins.
 */
static void acpi_gpiochip_request_interrupts(struct acpi_gpio_chip *acpi_gpio)
{
	struct gpio_chip *chip = acpi_gpio->chip;

	if (!chip->to_irq)
		return;

	INIT_LIST_HEAD(&acpi_gpio->events);
	acpi_walk_resources(ACPI_HANDLE(chip->dev), "_AEI",
			    acpi_gpiochip_request_interrupt, acpi_gpio);
}

/**
 * acpi_gpiochip_free_interrupts() - Free GPIO ACPI event interrupts.
 * @acpi_gpio:      ACPI GPIO chip
 *
 * Free interrupts associated with GPIO ACPI event method for the given
 * GPIO chip.
 */
static void acpi_gpiochip_free_interrupts(struct acpi_gpio_chip *acpi_gpio)
{
	struct acpi_gpio_event *event, *ep;
	struct gpio_chip *chip = acpi_gpio->chip;

	if (!chip->to_irq)
		return;

	list_for_each_entry_safe_reverse(event, ep, &acpi_gpio->events, node) {
		struct gpio_desc *desc;

		free_irq(event->irq, event);
		desc = gpiochip_get_desc(chip, event->pin);
		if (WARN_ON(IS_ERR(desc)))
			continue;
		gpiod_unlock_as_irq(desc);
		gpiochip_free_own_desc(desc);
		list_del(&event->node);
		kfree(event);
	}
}

struct acpi_gpio_lookup {
	struct acpi_gpio_info info;
	int index;
	struct gpio_desc *desc;
	int n;
};

static int acpi_find_gpio(struct acpi_resource *ares, void *data)
{
	struct acpi_gpio_lookup *lookup = data;

	if (ares->type != ACPI_RESOURCE_TYPE_GPIO)
		return 1;

	if (lookup->n++ == lookup->index && !lookup->desc) {
		const struct acpi_resource_gpio *agpio = &ares->data.gpio;

		lookup->desc = acpi_get_gpiod(agpio->resource_source.string_ptr,
					      agpio->pin_table[0]);
		lookup->info.gpioint =
			agpio->connection_type == ACPI_RESOURCE_GPIO_TYPE_INT;
		lookup->info.active_low =
			agpio->polarity == ACPI_ACTIVE_LOW;
	}

	return 1;
}

/**
 * acpi_get_gpiod_by_index() - get a GPIO descriptor from device resources
 * @dev: pointer to a device to get GPIO from
 * @index: index of GpioIo/GpioInt resource (starting from %0)
 * @info: info pointer to fill in (optional)
 *
 * Function goes through ACPI resources for @dev and based on @index looks
 * up a GpioIo/GpioInt resource, translates it to the Linux GPIO descriptor,
 * and returns it. @index matches GpioIo/GpioInt resources only so if there
 * are total %3 GPIO resources, the index goes from %0 to %2.
 *
 * If the GPIO cannot be translated or there is an error an ERR_PTR is
 * returned.
 *
 * Note: if the GPIO resource has multiple entries in the pin list, this
 * function only returns the first.
 */
struct gpio_desc *acpi_get_gpiod_by_index(struct device *dev, int index,
					  struct acpi_gpio_info *info)
{
	struct acpi_gpio_lookup lookup;
	struct list_head resource_list;
	struct acpi_device *adev;
	acpi_handle handle;
	int ret;

	if (!dev)
		return ERR_PTR(-EINVAL);

	handle = ACPI_HANDLE(dev);
	if (!handle || acpi_bus_get_device(handle, &adev))
		return ERR_PTR(-ENODEV);

	memset(&lookup, 0, sizeof(lookup));
	lookup.index = index;

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list, acpi_find_gpio,
				     &lookup);
	if (ret < 0)
		return ERR_PTR(ret);

	acpi_dev_free_resource_list(&resource_list);

	if (lookup.desc && info)
		*info = lookup.info;

	return lookup.desc ? lookup.desc : ERR_PTR(-ENOENT);
}

static acpi_status
acpi_gpio_adr_space_handler(u32 function, acpi_physical_address address,
			    u32 bits, u64 *value, void *handler_context,
			    void *region_context)
{
	struct acpi_gpio_chip *achip = region_context;
	struct gpio_chip *chip = achip->chip;
	struct acpi_resource_gpio *agpio;
	struct acpi_resource *ares;
	acpi_status status;
	bool pull_up;
	int i;

	status = acpi_buffer_to_resource(achip->conn_info.connection,
					 achip->conn_info.length, &ares);
	if (ACPI_FAILURE(status))
		return status;

	if (WARN_ON(ares->type != ACPI_RESOURCE_TYPE_GPIO)) {
		ACPI_FREE(ares);
		return AE_BAD_PARAMETER;
	}

	agpio = &ares->data.gpio;
	pull_up = agpio->pin_config == ACPI_PIN_CONFIG_PULLUP;

	if (WARN_ON(agpio->io_restriction == ACPI_IO_RESTRICT_INPUT &&
	    function == ACPI_WRITE)) {
		ACPI_FREE(ares);
		return AE_BAD_PARAMETER;
	}

	for (i = 0; i < agpio->pin_table_length; i++) {
		unsigned pin = agpio->pin_table[i];
		struct acpi_gpio_connection *conn;
		struct gpio_desc *desc;
		bool found;

		desc = gpiochip_get_desc(chip, pin);
		if (IS_ERR(desc)) {
			status = AE_ERROR;
			goto out;
		}

		mutex_lock(&achip->conn_lock);

		found = false;
		list_for_each_entry(conn, &achip->conns, node) {
			if (conn->desc == desc) {
				found = true;
				break;
			}
		}
		if (!found) {
			int ret;

			ret = gpiochip_request_own_desc(desc, "ACPI:OpRegion");
			if (ret) {
				status = AE_ERROR;
				mutex_unlock(&achip->conn_lock);
				goto out;
			}

			switch (agpio->io_restriction) {
			case ACPI_IO_RESTRICT_INPUT:
				gpiod_direction_input(desc);
				break;
			case ACPI_IO_RESTRICT_OUTPUT:
				/*
				 * ACPI GPIO resources don't contain an
				 * initial value for the GPIO. Therefore we
				 * deduce that value from the pull field
				 * instead. If the pin is pulled up we
				 * assume default to be high, otherwise
				 * low.
				 */
				gpiod_direction_output(desc, pull_up);
				break;
			default:
				/*
				 * Assume that the BIOS has configured the
				 * direction and pull accordingly.
				 */
				break;
			}

			conn = kzalloc(sizeof(*conn), GFP_KERNEL);
			if (!conn) {
				status = AE_NO_MEMORY;
				gpiochip_free_own_desc(desc);
				mutex_unlock(&achip->conn_lock);
				goto out;
			}

			conn->desc = desc;
			list_add_tail(&conn->node, &achip->conns);
		}

		mutex_unlock(&achip->conn_lock);

		if (function == ACPI_WRITE)
			gpiod_set_raw_value(desc, !!((1 << i) & *value));
		else
			*value |= gpiod_get_raw_value(desc) << i;
	}

out:
	ACPI_FREE(ares);
	return status;
}

static void acpi_gpiochip_request_regions(struct acpi_gpio_chip *achip)
{
	struct gpio_chip *chip = achip->chip;
	acpi_handle handle = ACPI_HANDLE(chip->dev);
	acpi_status status;

	INIT_LIST_HEAD(&achip->conns);
	mutex_init(&achip->conn_lock);
	status = acpi_install_address_space_handler(handle, ACPI_ADR_SPACE_GPIO,
						    acpi_gpio_adr_space_handler,
						    NULL, achip);
	if (ACPI_FAILURE(status))
		dev_err(chip->dev, "Failed to install GPIO OpRegion handler\n");
}

static void acpi_gpiochip_free_regions(struct acpi_gpio_chip *achip)
{
	struct gpio_chip *chip = achip->chip;
	acpi_handle handle = ACPI_HANDLE(chip->dev);
	struct acpi_gpio_connection *conn, *tmp;
	acpi_status status;

	status = acpi_remove_address_space_handler(handle, ACPI_ADR_SPACE_GPIO,
						   acpi_gpio_adr_space_handler);
	if (ACPI_FAILURE(status)) {
		dev_err(chip->dev, "Failed to remove GPIO OpRegion handler\n");
		return;
	}

	list_for_each_entry_safe_reverse(conn, tmp, &achip->conns, node) {
		gpiochip_free_own_desc(conn->desc);
		list_del(&conn->node);
		kfree(conn);
	}
}

void acpi_gpiochip_add(struct gpio_chip *chip)
{
	struct acpi_gpio_chip *acpi_gpio;
	acpi_handle handle;
	acpi_status status;

	if (!chip || !chip->dev)
		return;

	handle = ACPI_HANDLE(chip->dev);
	if (!handle)
		return;

	acpi_gpio = kzalloc(sizeof(*acpi_gpio), GFP_KERNEL);
	if (!acpi_gpio) {
		dev_err(chip->dev,
			"Failed to allocate memory for ACPI GPIO chip\n");
		return;
	}

	acpi_gpio->chip = chip;

	status = acpi_attach_data(handle, acpi_gpio_chip_dh, acpi_gpio);
	if (ACPI_FAILURE(status)) {
		dev_err(chip->dev, "Failed to attach ACPI GPIO chip\n");
		kfree(acpi_gpio);
		return;
	}

	acpi_gpiochip_request_interrupts(acpi_gpio);
	acpi_gpiochip_request_regions(acpi_gpio);
}

void acpi_gpiochip_remove(struct gpio_chip *chip)
{
	struct acpi_gpio_chip *acpi_gpio;
	acpi_handle handle;
	acpi_status status;

	if (!chip || !chip->dev)
		return;

	handle = ACPI_HANDLE(chip->dev);
	if (!handle)
		return;

	status = acpi_get_data(handle, acpi_gpio_chip_dh, (void **)&acpi_gpio);
	if (ACPI_FAILURE(status)) {
		dev_warn(chip->dev, "Failed to retrieve ACPI GPIO chip\n");
		return;
	}

	acpi_gpiochip_free_regions(acpi_gpio);
	acpi_gpiochip_free_interrupts(acpi_gpio);

	acpi_detach_data(handle, acpi_gpio_chip_dh);
	kfree(acpi_gpio);
}
