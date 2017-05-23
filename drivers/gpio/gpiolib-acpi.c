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
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio/driver.h>
#include <linux/gpio/machine.h>
#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/pinctrl/pinctrl.h>

#include "gpiolib.h"

struct acpi_gpio_event {
	struct list_head node;
	acpi_handle handle;
	unsigned int pin;
	unsigned int irq;
	struct gpio_desc *desc;
};

struct acpi_gpio_connection {
	struct list_head node;
	unsigned int pin;
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
	if (!gc->parent)
		return false;

	return ACPI_HANDLE(gc->parent) == data;
}

#ifdef CONFIG_PINCTRL
/**
 * acpi_gpiochip_pin_to_gpio_offset() - translates ACPI GPIO to Linux GPIO
 * @chip: GPIO chip
 * @pin: ACPI GPIO pin number from GpioIo/GpioInt resource
 *
 * Function takes ACPI GpioIo/GpioInt pin number as a parameter and
 * translates it to a corresponding offset suitable to be passed to a
 * GPIO controller driver.
 *
 * Typically the returned offset is same as @pin, but if the GPIO
 * controller uses pin controller and the mapping is not contiguous the
 * offset might be different.
 */
static int acpi_gpiochip_pin_to_gpio_offset(struct gpio_device *gdev, int pin)
{
	struct gpio_pin_range *pin_range;

	/* If there are no ranges in this chip, use 1:1 mapping */
	if (list_empty(&gdev->pin_ranges))
		return pin;

	list_for_each_entry(pin_range, &gdev->pin_ranges, node) {
		const struct pinctrl_gpio_range *range = &pin_range->range;
		int i;

		if (range->pins) {
			for (i = 0; i < range->npins; i++) {
				if (range->pins[i] == pin)
					return range->base + i - gdev->base;
			}
		} else {
			if (pin >= range->pin_base &&
			    pin < range->pin_base + range->npins) {
				unsigned gpio_base;

				gpio_base = range->base - gdev->base;
				return gpio_base + pin - range->pin_base;
			}
		}
	}

	return -EINVAL;
}
#else
static inline int acpi_gpiochip_pin_to_gpio_offset(struct gpio_device *gdev,
						   int pin)
{
	return pin;
}
#endif

/**
 * acpi_get_gpiod() - Translate ACPI GPIO pin to GPIO descriptor usable with GPIO API
 * @path:	ACPI GPIO controller full path name, (e.g. "\\_SB.GPO1")
 * @pin:	ACPI GPIO pin number (0-based, controller-relative)
 *
 * Return: GPIO descriptor to use with Linux generic GPIO API, or ERR_PTR
 * error value. Specifically returns %-EPROBE_DEFER if the referenced GPIO
 * controller does not have gpiochip registered at the moment. This is to
 * support probe deferral.
 */
static struct gpio_desc *acpi_get_gpiod(char *path, int pin)
{
	struct gpio_chip *chip;
	acpi_handle handle;
	acpi_status status;
	int offset;

	status = acpi_get_handle(NULL, path, &handle);
	if (ACPI_FAILURE(status))
		return ERR_PTR(-ENODEV);

	chip = gpiochip_find(handle, acpi_gpiochip_find);
	if (!chip)
		return ERR_PTR(-EPROBE_DEFER);

	offset = acpi_gpiochip_pin_to_gpio_offset(chip->gpiodev, pin);
	if (offset < 0)
		return ERR_PTR(offset);

	return gpiochip_get_desc(chip, offset);
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

	handle = ACPI_HANDLE(chip->parent);
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

	pin = acpi_gpiochip_pin_to_gpio_offset(chip->gpiodev, pin);
	if (pin < 0)
		return AE_BAD_PARAMETER;

	desc = gpiochip_request_own_desc(chip, pin, "ACPI:Event");
	if (IS_ERR(desc)) {
		dev_err(chip->parent, "Failed to request GPIO\n");
		return AE_ERROR;
	}

	gpiod_direction_input(desc);

	ret = gpiochip_lock_as_irq(chip, pin);
	if (ret) {
		dev_err(chip->parent, "Failed to lock GPIO as interrupt\n");
		goto fail_free_desc;
	}

	irq = gpiod_to_irq(desc);
	if (irq < 0) {
		dev_err(chip->parent, "Failed to translate GPIO to IRQ\n");
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
	event->desc = desc;

	ret = request_threaded_irq(event->irq, NULL, handler, irqflags,
				   "ACPI:Event", event);
	if (ret) {
		dev_err(chip->parent,
			"Failed to setup interrupt handler for %d\n",
			event->irq);
		goto fail_free_event;
	}

	if (agpio->wake_capable == ACPI_WAKE_CAPABLE)
		enable_irq_wake(irq);

	list_add_tail(&event->node, &acpi_gpio->events);
	return AE_OK;

fail_free_event:
	kfree(event);
fail_unlock_irq:
	gpiochip_unlock_as_irq(chip, pin);
fail_free_desc:
	gpiochip_free_own_desc(desc);

	return AE_ERROR;
}

/**
 * acpi_gpiochip_request_interrupts() - Register isr for gpio chip ACPI events
 * @chip:      GPIO chip
 *
 * ACPI5 platforms can use GPIO signaled ACPI events. These GPIO interrupts are
 * handled by ACPI event methods which need to be called from the GPIO
 * chip's interrupt handler. acpi_gpiochip_request_interrupts finds out which
 * gpio pins have acpi event methods and assigns interrupt handlers that calls
 * the acpi event methods for those pins.
 */
void acpi_gpiochip_request_interrupts(struct gpio_chip *chip)
{
	struct acpi_gpio_chip *acpi_gpio;
	acpi_handle handle;
	acpi_status status;

	if (!chip->parent || !chip->to_irq)
		return;

	handle = ACPI_HANDLE(chip->parent);
	if (!handle)
		return;

	status = acpi_get_data(handle, acpi_gpio_chip_dh, (void **)&acpi_gpio);
	if (ACPI_FAILURE(status))
		return;

	acpi_walk_resources(handle, "_AEI",
			    acpi_gpiochip_request_interrupt, acpi_gpio);
}
EXPORT_SYMBOL_GPL(acpi_gpiochip_request_interrupts);

/**
 * acpi_gpiochip_free_interrupts() - Free GPIO ACPI event interrupts.
 * @chip:      GPIO chip
 *
 * Free interrupts associated with GPIO ACPI event method for the given
 * GPIO chip.
 */
void acpi_gpiochip_free_interrupts(struct gpio_chip *chip)
{
	struct acpi_gpio_chip *acpi_gpio;
	struct acpi_gpio_event *event, *ep;
	acpi_handle handle;
	acpi_status status;

	if (!chip->parent || !chip->to_irq)
		return;

	handle = ACPI_HANDLE(chip->parent);
	if (!handle)
		return;

	status = acpi_get_data(handle, acpi_gpio_chip_dh, (void **)&acpi_gpio);
	if (ACPI_FAILURE(status))
		return;

	list_for_each_entry_safe_reverse(event, ep, &acpi_gpio->events, node) {
		struct gpio_desc *desc;

		if (irqd_is_wakeup_set(irq_get_irq_data(event->irq)))
			disable_irq_wake(event->irq);

		free_irq(event->irq, event);
		desc = event->desc;
		if (WARN_ON(IS_ERR(desc)))
			continue;
		gpiochip_unlock_as_irq(chip, event->pin);
		gpiochip_free_own_desc(desc);
		list_del(&event->node);
		kfree(event);
	}
}
EXPORT_SYMBOL_GPL(acpi_gpiochip_free_interrupts);

int acpi_dev_add_driver_gpios(struct acpi_device *adev,
			      const struct acpi_gpio_mapping *gpios)
{
	if (adev && gpios) {
		adev->driver_gpios = gpios;
		return 0;
	}
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(acpi_dev_add_driver_gpios);

static void devm_acpi_dev_release_driver_gpios(struct device *dev, void *res)
{
	acpi_dev_remove_driver_gpios(ACPI_COMPANION(dev));
}

int devm_acpi_dev_add_driver_gpios(struct device *dev,
				   const struct acpi_gpio_mapping *gpios)
{
	void *res;
	int ret;

	res = devres_alloc(devm_acpi_dev_release_driver_gpios, 0, GFP_KERNEL);
	if (!res)
		return -ENOMEM;

	ret = acpi_dev_add_driver_gpios(ACPI_COMPANION(dev), gpios);
	if (ret) {
		devres_free(res);
		return ret;
	}
	devres_add(dev, res);
	return 0;
}
EXPORT_SYMBOL_GPL(devm_acpi_dev_add_driver_gpios);

void devm_acpi_dev_remove_driver_gpios(struct device *dev)
{
	WARN_ON(devres_release(dev, devm_acpi_dev_release_driver_gpios, NULL, NULL));
}
EXPORT_SYMBOL_GPL(devm_acpi_dev_remove_driver_gpios);

static bool acpi_get_driver_gpio_data(struct acpi_device *adev,
				      const char *name, int index,
				      struct acpi_reference_args *args)
{
	const struct acpi_gpio_mapping *gm;

	if (!adev->driver_gpios)
		return false;

	for (gm = adev->driver_gpios; gm->name; gm++)
		if (!strcmp(name, gm->name) && gm->data && index < gm->size) {
			const struct acpi_gpio_params *par = gm->data + index;

			args->adev = adev;
			args->args[0] = par->crs_entry_index;
			args->args[1] = par->line_index;
			args->args[2] = par->active_low;
			args->nargs = 3;
			return true;
		}

	return false;
}

struct acpi_gpio_lookup {
	struct acpi_gpio_info info;
	int index;
	int pin_index;
	bool active_low;
	struct acpi_device *adev;
	struct gpio_desc *desc;
	int n;
};

static int acpi_populate_gpio_lookup(struct acpi_resource *ares, void *data)
{
	struct acpi_gpio_lookup *lookup = data;

	if (ares->type != ACPI_RESOURCE_TYPE_GPIO)
		return 1;

	if (lookup->n++ == lookup->index && !lookup->desc) {
		const struct acpi_resource_gpio *agpio = &ares->data.gpio;
		int pin_index = lookup->pin_index;

		if (pin_index >= agpio->pin_table_length)
			return 1;

		lookup->desc = acpi_get_gpiod(agpio->resource_source.string_ptr,
					      agpio->pin_table[pin_index]);
		lookup->info.gpioint =
			agpio->connection_type == ACPI_RESOURCE_GPIO_TYPE_INT;

		/*
		 * Polarity and triggering are only specified for GpioInt
		 * resource.
		 * Note: we expect here:
		 * - ACPI_ACTIVE_LOW == GPIO_ACTIVE_LOW
		 * - ACPI_ACTIVE_HIGH == GPIO_ACTIVE_HIGH
		 */
		if (lookup->info.gpioint) {
			lookup->info.polarity = agpio->polarity;
			lookup->info.triggering = agpio->triggering;
		}

	}

	return 1;
}

static int acpi_gpio_resource_lookup(struct acpi_gpio_lookup *lookup,
				     struct acpi_gpio_info *info)
{
	struct list_head res_list;
	int ret;

	INIT_LIST_HEAD(&res_list);

	ret = acpi_dev_get_resources(lookup->adev, &res_list,
				     acpi_populate_gpio_lookup,
				     lookup);
	if (ret < 0)
		return ret;

	acpi_dev_free_resource_list(&res_list);

	if (!lookup->desc)
		return -ENOENT;

	if (info) {
		*info = lookup->info;
		if (lookup->active_low)
			info->polarity = lookup->active_low;
	}
	return 0;
}

static int acpi_gpio_property_lookup(struct fwnode_handle *fwnode,
				     const char *propname, int index,
				     struct acpi_gpio_lookup *lookup)
{
	struct acpi_reference_args args;
	int ret;

	memset(&args, 0, sizeof(args));
	ret = __acpi_node_get_property_reference(fwnode, propname, index, 3,
						 &args);
	if (ret) {
		struct acpi_device *adev = to_acpi_device_node(fwnode);

		if (!adev)
			return ret;

		if (!acpi_get_driver_gpio_data(adev, propname, index, &args))
			return ret;
	}
	/*
	 * The property was found and resolved, so need to lookup the GPIO based
	 * on returned args.
	 */
	lookup->adev = args.adev;
	if (args.nargs != 3)
		return -EPROTO;

	lookup->index = args.args[0];
	lookup->pin_index = args.args[1];
	lookup->active_low = !!args.args[2];

	return 0;
}

/**
 * acpi_get_gpiod_by_index() - get a GPIO descriptor from device resources
 * @adev: pointer to a ACPI device to get GPIO from
 * @propname: Property name of the GPIO (optional)
 * @index: index of GpioIo/GpioInt resource (starting from %0)
 * @info: info pointer to fill in (optional)
 *
 * Function goes through ACPI resources for @adev and based on @index looks
 * up a GpioIo/GpioInt resource, translates it to the Linux GPIO descriptor,
 * and returns it. @index matches GpioIo/GpioInt resources only so if there
 * are total %3 GPIO resources, the index goes from %0 to %2.
 *
 * If @propname is specified the GPIO is looked using device property. In
 * that case @index is used to select the GPIO entry in the property value
 * (in case of multiple).
 *
 * If the GPIO cannot be translated or there is an error an ERR_PTR is
 * returned.
 *
 * Note: if the GPIO resource has multiple entries in the pin list, this
 * function only returns the first.
 */
static struct gpio_desc *acpi_get_gpiod_by_index(struct acpi_device *adev,
					  const char *propname, int index,
					  struct acpi_gpio_info *info)
{
	struct acpi_gpio_lookup lookup;
	int ret;

	if (!adev)
		return ERR_PTR(-ENODEV);

	memset(&lookup, 0, sizeof(lookup));
	lookup.index = index;

	if (propname) {
		dev_dbg(&adev->dev, "GPIO: looking up %s\n", propname);

		ret = acpi_gpio_property_lookup(acpi_fwnode_handle(adev),
						propname, index, &lookup);
		if (ret)
			return ERR_PTR(ret);

		dev_dbg(&adev->dev, "GPIO: _DSD returned %s %d %d %u\n",
			dev_name(&lookup.adev->dev), lookup.index,
			lookup.pin_index, lookup.active_low);
	} else {
		dev_dbg(&adev->dev, "GPIO: looking up %d in _CRS\n", index);
		lookup.adev = adev;
	}

	ret = acpi_gpio_resource_lookup(&lookup, info);
	return ret ? ERR_PTR(ret) : lookup.desc;
}

struct gpio_desc *acpi_find_gpio(struct device *dev,
				 const char *con_id,
				 unsigned int idx,
				 enum gpiod_flags flags,
				 enum gpio_lookup_flags *lookupflags)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	struct acpi_gpio_info info;
	struct gpio_desc *desc;
	char propname[32];
	int i;

	/* Try first from _DSD */
	for (i = 0; i < ARRAY_SIZE(gpio_suffixes); i++) {
		if (con_id) {
			snprintf(propname, sizeof(propname), "%s-%s",
				 con_id, gpio_suffixes[i]);
		} else {
			snprintf(propname, sizeof(propname), "%s",
				 gpio_suffixes[i]);
		}

		desc = acpi_get_gpiod_by_index(adev, propname, idx, &info);
		if (!IS_ERR(desc))
			break;
		if (PTR_ERR(desc) == -EPROBE_DEFER)
			return ERR_CAST(desc);
	}

	/* Then from plain _CRS GPIOs */
	if (IS_ERR(desc)) {
		if (!acpi_can_fallback_to_crs(adev, con_id))
			return ERR_PTR(-ENOENT);

		desc = acpi_get_gpiod_by_index(adev, NULL, idx, &info);
		if (IS_ERR(desc))
			return desc;

		if ((flags == GPIOD_OUT_LOW || flags == GPIOD_OUT_HIGH) &&
		    info.gpioint) {
			dev_dbg(dev, "refusing GpioInt() entry when doing GPIOD_OUT_* lookup\n");
			return ERR_PTR(-ENOENT);
		}
	}

	if (info.polarity == GPIO_ACTIVE_LOW)
		*lookupflags |= GPIO_ACTIVE_LOW;

	return desc;
}

/**
 * acpi_node_get_gpiod() - get a GPIO descriptor from ACPI resources
 * @fwnode: pointer to an ACPI firmware node to get the GPIO information from
 * @propname: Property name of the GPIO
 * @index: index of GpioIo/GpioInt resource (starting from %0)
 * @info: info pointer to fill in (optional)
 *
 * If @fwnode is an ACPI device object, call %acpi_get_gpiod_by_index() for it.
 * Otherwise (ie. it is a data-only non-device object), use the property-based
 * GPIO lookup to get to the GPIO resource with the relevant information and use
 * that to obtain the GPIO descriptor to return.
 */
struct gpio_desc *acpi_node_get_gpiod(struct fwnode_handle *fwnode,
				      const char *propname, int index,
				      struct acpi_gpio_info *info)
{
	struct acpi_gpio_lookup lookup;
	struct acpi_device *adev;
	int ret;

	adev = to_acpi_device_node(fwnode);
	if (adev)
		return acpi_get_gpiod_by_index(adev, propname, index, info);

	if (!is_acpi_data_node(fwnode))
		return ERR_PTR(-ENODEV);

	if (!propname)
		return ERR_PTR(-EINVAL);

	memset(&lookup, 0, sizeof(lookup));
	lookup.index = index;

	ret = acpi_gpio_property_lookup(fwnode, propname, index, &lookup);
	if (ret)
		return ERR_PTR(ret);

	ret = acpi_gpio_resource_lookup(&lookup, info);
	return ret ? ERR_PTR(ret) : lookup.desc;
}

/**
 * acpi_dev_gpio_irq_get() - Find GpioInt and translate it to Linux IRQ number
 * @adev: pointer to a ACPI device to get IRQ from
 * @index: index of GpioInt resource (starting from %0)
 *
 * If the device has one or more GpioInt resources, this function can be
 * used to translate from the GPIO offset in the resource to the Linux IRQ
 * number.
 *
 * Return: Linux IRQ number (>%0) on success, negative errno on failure.
 */
int acpi_dev_gpio_irq_get(struct acpi_device *adev, int index)
{
	int idx, i;
	unsigned int irq_flags;

	for (i = 0, idx = 0; idx <= index; i++) {
		struct acpi_gpio_info info;
		struct gpio_desc *desc;

		desc = acpi_get_gpiod_by_index(adev, NULL, i, &info);

		/* Ignore -EPROBE_DEFER, it only matters if idx matches */
		if (IS_ERR(desc) && PTR_ERR(desc) != -EPROBE_DEFER)
			return PTR_ERR(desc);

		if (info.gpioint && idx++ == index) {
			int irq;

			if (IS_ERR(desc))
				return PTR_ERR(desc);

			irq = gpiod_to_irq(desc);
			if (irq < 0)
				return irq;

			irq_flags = acpi_dev_get_irq_type(info.triggering,
							  info.polarity);

			/* Set type if specified and different than the current one */
			if (irq_flags != IRQ_TYPE_NONE &&
			    irq_flags != irq_get_trigger_type(irq))
				irq_set_irq_type(irq, irq_flags);

			return irq;
		}

	}
	return -ENOENT;
}
EXPORT_SYMBOL_GPL(acpi_dev_gpio_irq_get);

static acpi_status
acpi_gpio_adr_space_handler(u32 function, acpi_physical_address address,
			    u32 bits, u64 *value, void *handler_context,
			    void *region_context)
{
	struct acpi_gpio_chip *achip = region_context;
	struct gpio_chip *chip = achip->chip;
	struct acpi_resource_gpio *agpio;
	struct acpi_resource *ares;
	int pin_index = (int)address;
	acpi_status status;
	bool pull_up;
	int length;
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

	length = min(agpio->pin_table_length, (u16)(pin_index + bits));
	for (i = pin_index; i < length; ++i) {
		int pin = agpio->pin_table[i];
		struct acpi_gpio_connection *conn;
		struct gpio_desc *desc;
		bool found;

		pin = acpi_gpiochip_pin_to_gpio_offset(chip->gpiodev, pin);
		if (pin < 0) {
			status = AE_BAD_PARAMETER;
			goto out;
		}

		mutex_lock(&achip->conn_lock);

		found = false;
		list_for_each_entry(conn, &achip->conns, node) {
			if (conn->pin == pin) {
				found = true;
				desc = conn->desc;
				break;
			}
		}

		/*
		 * The same GPIO can be shared between operation region and
		 * event but only if the access here is ACPI_READ. In that
		 * case we "borrow" the event GPIO instead.
		 */
		if (!found && agpio->sharable == ACPI_SHARED &&
		     function == ACPI_READ) {
			struct acpi_gpio_event *event;

			list_for_each_entry(event, &achip->events, node) {
				if (event->pin == pin) {
					desc = event->desc;
					found = true;
					break;
				}
			}
		}

		if (!found) {
			desc = gpiochip_request_own_desc(chip, pin,
							 "ACPI:OpRegion");
			if (IS_ERR(desc)) {
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

			conn->pin = pin;
			conn->desc = desc;
			list_add_tail(&conn->node, &achip->conns);
		}

		mutex_unlock(&achip->conn_lock);

		if (function == ACPI_WRITE)
			gpiod_set_raw_value_cansleep(desc,
						     !!((1 << i) & *value));
		else
			*value |= (u64)gpiod_get_raw_value_cansleep(desc) << i;
	}

out:
	ACPI_FREE(ares);
	return status;
}

static void acpi_gpiochip_request_regions(struct acpi_gpio_chip *achip)
{
	struct gpio_chip *chip = achip->chip;
	acpi_handle handle = ACPI_HANDLE(chip->parent);
	acpi_status status;

	INIT_LIST_HEAD(&achip->conns);
	mutex_init(&achip->conn_lock);
	status = acpi_install_address_space_handler(handle, ACPI_ADR_SPACE_GPIO,
						    acpi_gpio_adr_space_handler,
						    NULL, achip);
	if (ACPI_FAILURE(status))
		dev_err(chip->parent,
		        "Failed to install GPIO OpRegion handler\n");
}

static void acpi_gpiochip_free_regions(struct acpi_gpio_chip *achip)
{
	struct gpio_chip *chip = achip->chip;
	acpi_handle handle = ACPI_HANDLE(chip->parent);
	struct acpi_gpio_connection *conn, *tmp;
	acpi_status status;

	status = acpi_remove_address_space_handler(handle, ACPI_ADR_SPACE_GPIO,
						   acpi_gpio_adr_space_handler);
	if (ACPI_FAILURE(status)) {
		dev_err(chip->parent,
			"Failed to remove GPIO OpRegion handler\n");
		return;
	}

	list_for_each_entry_safe_reverse(conn, tmp, &achip->conns, node) {
		gpiochip_free_own_desc(conn->desc);
		list_del(&conn->node);
		kfree(conn);
	}
}

static struct gpio_desc *acpi_gpiochip_parse_own_gpio(
	struct acpi_gpio_chip *achip, struct fwnode_handle *fwnode,
	const char **name, unsigned int *lflags, unsigned int *dflags)
{
	struct gpio_chip *chip = achip->chip;
	struct gpio_desc *desc;
	u32 gpios[2];
	int ret;

	*lflags = 0;
	*dflags = 0;
	*name = NULL;

	ret = fwnode_property_read_u32_array(fwnode, "gpios", gpios,
					     ARRAY_SIZE(gpios));
	if (ret < 0)
		return ERR_PTR(ret);

	ret = acpi_gpiochip_pin_to_gpio_offset(chip->gpiodev, gpios[0]);
	if (ret < 0)
		return ERR_PTR(ret);

	desc = gpiochip_get_desc(chip, ret);
	if (IS_ERR(desc))
		return desc;

	if (gpios[1])
		*lflags |= GPIO_ACTIVE_LOW;

	if (fwnode_property_present(fwnode, "input"))
		*dflags |= GPIOD_IN;
	else if (fwnode_property_present(fwnode, "output-low"))
		*dflags |= GPIOD_OUT_LOW;
	else if (fwnode_property_present(fwnode, "output-high"))
		*dflags |= GPIOD_OUT_HIGH;
	else
		return ERR_PTR(-EINVAL);

	fwnode_property_read_string(fwnode, "line-name", name);

	return desc;
}

static void acpi_gpiochip_scan_gpios(struct acpi_gpio_chip *achip)
{
	struct gpio_chip *chip = achip->chip;
	struct fwnode_handle *fwnode;

	device_for_each_child_node(chip->parent, fwnode) {
		unsigned int lflags, dflags;
		struct gpio_desc *desc;
		const char *name;
		int ret;

		if (!fwnode_property_present(fwnode, "gpio-hog"))
			continue;

		desc = acpi_gpiochip_parse_own_gpio(achip, fwnode, &name,
						    &lflags, &dflags);
		if (IS_ERR(desc))
			continue;

		ret = gpiod_hog(desc, name, lflags, dflags);
		if (ret) {
			dev_err(chip->parent, "Failed to hog GPIO\n");
			fwnode_handle_put(fwnode);
			return;
		}
	}
}

void acpi_gpiochip_add(struct gpio_chip *chip)
{
	struct acpi_gpio_chip *acpi_gpio;
	acpi_handle handle;
	acpi_status status;

	if (!chip || !chip->parent)
		return;

	handle = ACPI_HANDLE(chip->parent);
	if (!handle)
		return;

	acpi_gpio = kzalloc(sizeof(*acpi_gpio), GFP_KERNEL);
	if (!acpi_gpio) {
		dev_err(chip->parent,
			"Failed to allocate memory for ACPI GPIO chip\n");
		return;
	}

	acpi_gpio->chip = chip;
	INIT_LIST_HEAD(&acpi_gpio->events);

	status = acpi_attach_data(handle, acpi_gpio_chip_dh, acpi_gpio);
	if (ACPI_FAILURE(status)) {
		dev_err(chip->parent, "Failed to attach ACPI GPIO chip\n");
		kfree(acpi_gpio);
		return;
	}

	if (!chip->names)
		devprop_gpiochip_set_names(chip);

	acpi_gpiochip_request_regions(acpi_gpio);
	acpi_gpiochip_scan_gpios(acpi_gpio);
	acpi_walk_dep_device_list(handle);
}

void acpi_gpiochip_remove(struct gpio_chip *chip)
{
	struct acpi_gpio_chip *acpi_gpio;
	acpi_handle handle;
	acpi_status status;

	if (!chip || !chip->parent)
		return;

	handle = ACPI_HANDLE(chip->parent);
	if (!handle)
		return;

	status = acpi_get_data(handle, acpi_gpio_chip_dh, (void **)&acpi_gpio);
	if (ACPI_FAILURE(status)) {
		dev_warn(chip->parent, "Failed to retrieve ACPI GPIO chip\n");
		return;
	}

	acpi_gpiochip_free_regions(acpi_gpio);

	acpi_detach_data(handle, acpi_gpio_chip_dh);
	kfree(acpi_gpio);
}

static int acpi_gpio_package_count(const union acpi_object *obj)
{
	const union acpi_object *element = obj->package.elements;
	const union acpi_object *end = element + obj->package.count;
	unsigned int count = 0;

	while (element < end) {
		switch (element->type) {
		case ACPI_TYPE_LOCAL_REFERENCE:
			element += 3;
			/* Fallthrough */
		case ACPI_TYPE_INTEGER:
			element++;
			count++;
			break;

		default:
			return -EPROTO;
		}
	}

	return count;
}

static int acpi_find_gpio_count(struct acpi_resource *ares, void *data)
{
	unsigned int *count = data;

	if (ares->type == ACPI_RESOURCE_TYPE_GPIO)
		*count += ares->data.gpio.pin_table_length;

	return 1;
}

/**
 * acpi_gpio_count - return the number of GPIOs associated with a
 *		device / function or -ENOENT if no GPIO has been
 *		assigned to the requested function.
 * @dev:	GPIO consumer, can be NULL for system-global GPIOs
 * @con_id:	function within the GPIO consumer
 */
int acpi_gpio_count(struct device *dev, const char *con_id)
{
	struct acpi_device *adev = ACPI_COMPANION(dev);
	const union acpi_object *obj;
	const struct acpi_gpio_mapping *gm;
	int count = -ENOENT;
	int ret;
	char propname[32];
	unsigned int i;

	/* Try first from _DSD */
	for (i = 0; i < ARRAY_SIZE(gpio_suffixes); i++) {
		if (con_id)
			snprintf(propname, sizeof(propname), "%s-%s",
				 con_id, gpio_suffixes[i]);
		else
			snprintf(propname, sizeof(propname), "%s",
				 gpio_suffixes[i]);

		ret = acpi_dev_get_property(adev, propname, ACPI_TYPE_ANY,
					    &obj);
		if (ret == 0) {
			if (obj->type == ACPI_TYPE_LOCAL_REFERENCE)
				count = 1;
			else if (obj->type == ACPI_TYPE_PACKAGE)
				count = acpi_gpio_package_count(obj);
		} else if (adev->driver_gpios) {
			for (gm = adev->driver_gpios; gm->name; gm++)
				if (strcmp(propname, gm->name) == 0) {
					count = gm->size;
					break;
				}
		}
		if (count > 0)
			break;
	}

	/* Then from plain _CRS GPIOs */
	if (count < 0) {
		struct list_head resource_list;
		unsigned int crs_count = 0;

		INIT_LIST_HEAD(&resource_list);
		acpi_dev_get_resources(adev, &resource_list,
				       acpi_find_gpio_count, &crs_count);
		acpi_dev_free_resource_list(&resource_list);
		if (crs_count > 0)
			count = crs_count;
	}
	return count ? count : -ENOENT;
}

struct acpi_crs_lookup {
	struct list_head node;
	struct acpi_device *adev;
	const char *con_id;
};

static DEFINE_MUTEX(acpi_crs_lookup_lock);
static LIST_HEAD(acpi_crs_lookup_list);

bool acpi_can_fallback_to_crs(struct acpi_device *adev, const char *con_id)
{
	struct acpi_crs_lookup *l, *lookup = NULL;

	/* Never allow fallback if the device has properties */
	if (adev->data.properties || adev->driver_gpios)
		return false;

	mutex_lock(&acpi_crs_lookup_lock);

	list_for_each_entry(l, &acpi_crs_lookup_list, node) {
		if (l->adev == adev) {
			lookup = l;
			break;
		}
	}

	if (!lookup) {
		lookup = kmalloc(sizeof(*lookup), GFP_KERNEL);
		if (lookup) {
			lookup->adev = adev;
			lookup->con_id = kstrdup(con_id, GFP_KERNEL);
			list_add_tail(&lookup->node, &acpi_crs_lookup_list);
		}
	}

	mutex_unlock(&acpi_crs_lookup_lock);

	return lookup &&
		((!lookup->con_id && !con_id) ||
		 (lookup->con_id && con_id &&
		  strcmp(lookup->con_id, con_id) == 0));
}
