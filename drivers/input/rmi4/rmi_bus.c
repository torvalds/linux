// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2011-2016 Synaptics Incorporated
 * Copyright (c) 2011 Unixphere
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/list.h>
#include <linux/pm.h>
#include <linux/rmi.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/of.h>
#include "rmi_bus.h"
#include "rmi_driver.h"

static int debug_flags;
module_param(debug_flags, int, 0644);
MODULE_PARM_DESC(debug_flags, "control debugging information");

void rmi_dbg(int flags, struct device *dev, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (flags & debug_flags) {
		va_start(args, fmt);

		vaf.fmt = fmt;
		vaf.va = &args;

		dev_printk(KERN_DEBUG, dev, "%pV", &vaf);

		va_end(args);
	}
}
EXPORT_SYMBOL_GPL(rmi_dbg);

/*
 * RMI Physical devices
 *
 * Physical RMI device consists of several functions serving particular
 * purpose. For example F11 is a 2D touch sensor while F01 is a generic
 * function present in every RMI device.
 */

static void rmi_release_device(struct device *dev)
{
	struct rmi_device *rmi_dev = to_rmi_device(dev);

	kfree(rmi_dev);
}

static const struct device_type rmi_device_type = {
	.name		= "rmi4_sensor",
	.release	= rmi_release_device,
};

bool rmi_is_physical_device(struct device *dev)
{
	return dev->type == &rmi_device_type;
}

/**
 * rmi_register_transport_device - register a transport device connection
 * on the RMI bus.  Transport drivers provide communication from the devices
 * on a bus (such as SPI, I2C, and so on) to the RMI4 sensor.
 *
 * @xport: the transport device to register
 */
int rmi_register_transport_device(struct rmi_transport_dev *xport)
{
	static atomic_t transport_device_count = ATOMIC_INIT(0);
	struct rmi_device *rmi_dev;
	int error;

	rmi_dev = kzalloc(sizeof(struct rmi_device), GFP_KERNEL);
	if (!rmi_dev)
		return -ENOMEM;

	device_initialize(&rmi_dev->dev);

	rmi_dev->xport = xport;
	rmi_dev->number = atomic_inc_return(&transport_device_count) - 1;

	dev_set_name(&rmi_dev->dev, "rmi4-%02d", rmi_dev->number);

	rmi_dev->dev.bus = &rmi_bus_type;
	rmi_dev->dev.type = &rmi_device_type;
	rmi_dev->dev.parent = xport->dev;

	xport->rmi_dev = rmi_dev;

	error = device_add(&rmi_dev->dev);
	if (error)
		goto err_put_device;

	rmi_dbg(RMI_DEBUG_CORE, xport->dev,
		"%s: Registered %s as %s.\n", __func__,
		dev_name(rmi_dev->xport->dev), dev_name(&rmi_dev->dev));

	return 0;

err_put_device:
	put_device(&rmi_dev->dev);
	return error;
}
EXPORT_SYMBOL_GPL(rmi_register_transport_device);

/**
 * rmi_unregister_transport_device - unregister a transport device connection
 * @xport: the transport driver to unregister
 *
 */
void rmi_unregister_transport_device(struct rmi_transport_dev *xport)
{
	struct rmi_device *rmi_dev = xport->rmi_dev;

	device_del(&rmi_dev->dev);
	put_device(&rmi_dev->dev);
}
EXPORT_SYMBOL(rmi_unregister_transport_device);


/* Function specific stuff */

static void rmi_release_function(struct device *dev)
{
	struct rmi_function *fn = to_rmi_function(dev);

	kfree(fn);
}

static const struct device_type rmi_function_type = {
	.name		= "rmi4_function",
	.release	= rmi_release_function,
};

bool rmi_is_function_device(struct device *dev)
{
	return dev->type == &rmi_function_type;
}

static int rmi_function_match(struct device *dev, const struct device_driver *drv)
{
	const struct rmi_function_handler *handler = to_rmi_function_handler(drv);
	struct rmi_function *fn = to_rmi_function(dev);

	return fn->fd.function_number == handler->func;
}

#ifdef CONFIG_OF
static void rmi_function_of_probe(struct rmi_function *fn)
{
	char of_name[9];
	struct device_node *node = fn->rmi_dev->xport->dev->of_node;

	snprintf(of_name, sizeof(of_name), "rmi4-f%02x",
		fn->fd.function_number);
	fn->dev.of_node = of_get_child_by_name(node, of_name);
}
#else
static inline void rmi_function_of_probe(struct rmi_function *fn)
{}
#endif

static struct irq_chip rmi_irq_chip = {
	.name = "rmi4",
};

static int rmi_create_function_irq(struct rmi_function *fn,
				   struct rmi_function_handler *handler)
{
	struct rmi_driver_data *drvdata = dev_get_drvdata(&fn->rmi_dev->dev);
	int i, error;

	for (i = 0; i < fn->num_of_irqs; i++) {
		set_bit(fn->irq_pos + i, fn->irq_mask);

		fn->irq[i] = irq_create_mapping(drvdata->irqdomain,
						fn->irq_pos + i);

		irq_set_chip_data(fn->irq[i], fn);
		irq_set_chip_and_handler(fn->irq[i], &rmi_irq_chip,
					 handle_simple_irq);
		irq_set_nested_thread(fn->irq[i], 1);

		error = devm_request_threaded_irq(&fn->dev, fn->irq[i], NULL,
					handler->attention, IRQF_ONESHOT,
					dev_name(&fn->dev), fn);
		if (error) {
			dev_err(&fn->dev, "Error %d registering IRQ\n", error);
			return error;
		}
	}

	return 0;
}

static int rmi_function_probe(struct device *dev)
{
	struct rmi_function *fn = to_rmi_function(dev);
	struct rmi_function_handler *handler =
					to_rmi_function_handler(dev->driver);
	int error;

	rmi_function_of_probe(fn);

	if (handler->probe) {
		error = handler->probe(fn);
		if (error)
			return error;
	}

	if (fn->num_of_irqs && handler->attention) {
		error = rmi_create_function_irq(fn, handler);
		if (error)
			return error;
	}

	return 0;
}

static int rmi_function_remove(struct device *dev)
{
	struct rmi_function *fn = to_rmi_function(dev);
	struct rmi_function_handler *handler =
					to_rmi_function_handler(dev->driver);

	if (handler->remove)
		handler->remove(fn);

	return 0;
}

int rmi_register_function(struct rmi_function *fn)
{
	struct rmi_device *rmi_dev = fn->rmi_dev;
	int error;

	device_initialize(&fn->dev);

	dev_set_name(&fn->dev, "%s.fn%02x",
		     dev_name(&rmi_dev->dev), fn->fd.function_number);

	fn->dev.parent = &rmi_dev->dev;
	fn->dev.type = &rmi_function_type;
	fn->dev.bus = &rmi_bus_type;

	error = device_add(&fn->dev);
	if (error) {
		dev_err(&rmi_dev->dev,
			"Failed device_register function device %s\n",
			dev_name(&fn->dev));
		goto err_put_device;
	}

	rmi_dbg(RMI_DEBUG_CORE, &rmi_dev->dev, "Registered F%02X.\n",
			fn->fd.function_number);

	return 0;

err_put_device:
	put_device(&fn->dev);
	return error;
}

void rmi_unregister_function(struct rmi_function *fn)
{
	int i;

	rmi_dbg(RMI_DEBUG_CORE, &fn->dev, "Unregistering F%02X.\n",
			fn->fd.function_number);

	device_del(&fn->dev);
	of_node_put(fn->dev.of_node);

	for (i = 0; i < fn->num_of_irqs; i++)
		irq_dispose_mapping(fn->irq[i]);

	put_device(&fn->dev);
}

/**
 * __rmi_register_function_handler - register a handler for an RMI function
 * @handler: RMI handler that should be registered.
 * @owner: pointer to module that implements the handler
 * @mod_name: name of the module implementing the handler
 *
 * This function performs additional setup of RMI function handler and
 * registers it with the RMI core so that it can be bound to
 * RMI function devices.
 */
int __rmi_register_function_handler(struct rmi_function_handler *handler,
				     struct module *owner,
				     const char *mod_name)
{
	struct device_driver *driver = &handler->driver;
	int error;

	driver->bus = &rmi_bus_type;
	driver->owner = owner;
	driver->mod_name = mod_name;
	driver->probe = rmi_function_probe;
	driver->remove = rmi_function_remove;

	error = driver_register(driver);
	if (error) {
		pr_err("driver_register() failed for %s, error: %d\n",
			driver->name, error);
		return error;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(__rmi_register_function_handler);

/**
 * rmi_unregister_function_handler - unregister given RMI function handler
 * @handler: RMI handler that should be unregistered.
 *
 * This function unregisters given function handler from RMI core which
 * causes it to be unbound from the function devices.
 */
void rmi_unregister_function_handler(struct rmi_function_handler *handler)
{
	driver_unregister(&handler->driver);
}
EXPORT_SYMBOL_GPL(rmi_unregister_function_handler);

/* Bus specific stuff */

static int rmi_bus_match(struct device *dev, const struct device_driver *drv)
{
	bool physical = rmi_is_physical_device(dev);

	/* First see if types are not compatible */
	if (physical != rmi_is_physical_driver(drv))
		return 0;

	return physical || rmi_function_match(dev, drv);
}

const struct bus_type rmi_bus_type = {
	.match		= rmi_bus_match,
	.name		= "rmi4",
};

static struct rmi_function_handler *fn_handlers[] = {
	&rmi_f01_handler,
#ifdef CONFIG_RMI4_F03
	&rmi_f03_handler,
#endif
#ifdef CONFIG_RMI4_F11
	&rmi_f11_handler,
#endif
#ifdef CONFIG_RMI4_F12
	&rmi_f12_handler,
#endif
#ifdef CONFIG_RMI4_F30
	&rmi_f30_handler,
#endif
#ifdef CONFIG_RMI4_F34
	&rmi_f34_handler,
#endif
#ifdef CONFIG_RMI4_F3A
	&rmi_f3a_handler,
#endif
#ifdef CONFIG_RMI4_F54
	&rmi_f54_handler,
#endif
#ifdef CONFIG_RMI4_F55
	&rmi_f55_handler,
#endif
};

static void __rmi_unregister_function_handlers(int start_idx)
{
	int i;

	for (i = start_idx; i >= 0; i--)
		rmi_unregister_function_handler(fn_handlers[i]);
}

static void rmi_unregister_function_handlers(void)
{
	__rmi_unregister_function_handlers(ARRAY_SIZE(fn_handlers) - 1);
}

static int rmi_register_function_handlers(void)
{
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(fn_handlers); i++)	{
		ret = rmi_register_function_handler(fn_handlers[i]);
		if (ret) {
			pr_err("%s: error registering the RMI F%02x handler: %d\n",
				__func__, fn_handlers[i]->func, ret);
			goto err_unregister_function_handlers;
		}
	}

	return 0;

err_unregister_function_handlers:
	__rmi_unregister_function_handlers(i - 1);
	return ret;
}

int rmi_of_property_read_u32(struct device *dev, u32 *result,
				const char *prop, bool optional)
{
	int retval;
	u32 val = 0;

	retval = of_property_read_u32(dev->of_node, prop, &val);
	if (retval && (!optional && retval == -EINVAL)) {
		dev_err(dev, "Failed to get %s value: %d\n",
			prop, retval);
		return retval;
	}
	*result = val;

	return 0;
}
EXPORT_SYMBOL_GPL(rmi_of_property_read_u32);

static int __init rmi_bus_init(void)
{
	int error;

	error = bus_register(&rmi_bus_type);
	if (error) {
		pr_err("%s: error registering the RMI bus: %d\n",
			__func__, error);
		return error;
	}

	error = rmi_register_function_handlers();
	if (error)
		goto err_unregister_bus;

	error = rmi_register_physical_driver();
	if (error) {
		pr_err("%s: error registering the RMI physical driver: %d\n",
			__func__, error);
		goto err_unregister_bus;
	}

	return 0;

err_unregister_bus:
	bus_unregister(&rmi_bus_type);
	return error;
}
module_init(rmi_bus_init);

static void __exit rmi_bus_exit(void)
{
	/*
	 * We should only ever get here if all drivers are unloaded, so
	 * all we have to do at this point is unregister ourselves.
	 */

	rmi_unregister_physical_driver();
	rmi_unregister_function_handlers();
	bus_unregister(&rmi_bus_type);
}
module_exit(rmi_bus_exit);

MODULE_AUTHOR("Christopher Heiny <cheiny@synaptics.com");
MODULE_AUTHOR("Andrew Duggan <aduggan@synaptics.com");
MODULE_DESCRIPTION("RMI bus");
MODULE_LICENSE("GPL");
