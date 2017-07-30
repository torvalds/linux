/*
 * platform.c - platform 'pseudo' bus for legacy devices
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 *
 * This file is released under the GPLv2
 *
 * Please see Documentation/driver-model/platform.txt for more
 * information.
 */

#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/bootmem.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/idr.h>
#include <linux/acpi.h>
#include <linux/clk/clk-conf.h>
#include <linux/limits.h>
#include <linux/property.h>

#include "base.h"
#include "power/power.h"

/* For automatically allocated device IDs */
static DEFINE_IDA(platform_devid_ida);

struct device platform_bus = {
	.init_name	= "platform",
};
EXPORT_SYMBOL_GPL(platform_bus);

/**
 * arch_setup_pdev_archdata - Allow manipulation of archdata before its used
 * @pdev: platform device
 *
 * This is called before platform_device_add() such that any pdev_archdata may
 * be setup before the platform_notifier is called.  So if a user needs to
 * manipulate any relevant information in the pdev_archdata they can do:
 *
 *	platform_device_alloc()
 *	... manipulate ...
 *	platform_device_add()
 *
 * And if they don't care they can just call platform_device_register() and
 * everything will just work out.
 */
void __weak arch_setup_pdev_archdata(struct platform_device *pdev)
{
}

/**
 * platform_get_resource - get a resource for a device
 * @dev: platform device
 * @type: resource type
 * @num: resource index
 */
struct resource *platform_get_resource(struct platform_device *dev,
				       unsigned int type, unsigned int num)
{
	int i;

	for (i = 0; i < dev->num_resources; i++) {
		struct resource *r = &dev->resource[i];

		if (type == resource_type(r) && num-- == 0)
			return r;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(platform_get_resource);

/**
 * platform_get_irq - get an IRQ for a device
 * @dev: platform device
 * @num: IRQ number index
 */
int platform_get_irq(struct platform_device *dev, unsigned int num)
{
#ifdef CONFIG_SPARC
	/* sparc does not have irqs represented as IORESOURCE_IRQ resources */
	if (!dev || num >= dev->archdata.num_irqs)
		return -ENXIO;
	return dev->archdata.irqs[num];
#else
	struct resource *r;
	if (IS_ENABLED(CONFIG_OF_IRQ) && dev->dev.of_node) {
		int ret;

		ret = of_irq_get(dev->dev.of_node, num);
		if (ret > 0 || ret == -EPROBE_DEFER)
			return ret;
	}

	r = platform_get_resource(dev, IORESOURCE_IRQ, num);
	if (has_acpi_companion(&dev->dev)) {
		if (r && r->flags & IORESOURCE_DISABLED) {
			int ret;

			ret = acpi_irq_get(ACPI_HANDLE(&dev->dev), num, r);
			if (ret)
				return ret;
		}
	}

	/*
	 * The resources may pass trigger flags to the irqs that need
	 * to be set up. It so happens that the trigger flags for
	 * IORESOURCE_BITS correspond 1-to-1 to the IRQF_TRIGGER*
	 * settings.
	 */
	if (r && r->flags & IORESOURCE_BITS) {
		struct irq_data *irqd;

		irqd = irq_get_irq_data(r->start);
		if (!irqd)
			return -ENXIO;
		irqd_set_trigger_type(irqd, r->flags & IORESOURCE_BITS);
	}

	return r ? r->start : -ENXIO;
#endif
}
EXPORT_SYMBOL_GPL(platform_get_irq);

/**
 * platform_irq_count - Count the number of IRQs a platform device uses
 * @dev: platform device
 *
 * Return: Number of IRQs a platform device uses or EPROBE_DEFER
 */
int platform_irq_count(struct platform_device *dev)
{
	int ret, nr = 0;

	while ((ret = platform_get_irq(dev, nr)) >= 0)
		nr++;

	if (ret == -EPROBE_DEFER)
		return ret;

	return nr;
}
EXPORT_SYMBOL_GPL(platform_irq_count);

/**
 * platform_get_resource_byname - get a resource for a device by name
 * @dev: platform device
 * @type: resource type
 * @name: resource name
 */
struct resource *platform_get_resource_byname(struct platform_device *dev,
					      unsigned int type,
					      const char *name)
{
	int i;

	for (i = 0; i < dev->num_resources; i++) {
		struct resource *r = &dev->resource[i];

		if (unlikely(!r->name))
			continue;

		if (type == resource_type(r) && !strcmp(r->name, name))
			return r;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(platform_get_resource_byname);

/**
 * platform_get_irq_byname - get an IRQ for a device by name
 * @dev: platform device
 * @name: IRQ name
 */
int platform_get_irq_byname(struct platform_device *dev, const char *name)
{
	struct resource *r;

	if (IS_ENABLED(CONFIG_OF_IRQ) && dev->dev.of_node) {
		int ret;

		ret = of_irq_get_byname(dev->dev.of_node, name);
		if (ret > 0 || ret == -EPROBE_DEFER)
			return ret;
	}

	r = platform_get_resource_byname(dev, IORESOURCE_IRQ, name);
	return r ? r->start : -ENXIO;
}
EXPORT_SYMBOL_GPL(platform_get_irq_byname);

/**
 * platform_add_devices - add a numbers of platform devices
 * @devs: array of platform devices to add
 * @num: number of platform devices in array
 */
int platform_add_devices(struct platform_device **devs, int num)
{
	int i, ret = 0;

	for (i = 0; i < num; i++) {
		ret = platform_device_register(devs[i]);
		if (ret) {
			while (--i >= 0)
				platform_device_unregister(devs[i]);
			break;
		}
	}

	return ret;
}
EXPORT_SYMBOL_GPL(platform_add_devices);

struct platform_object {
	struct platform_device pdev;
	char name[];
};

/**
 * platform_device_put - destroy a platform device
 * @pdev: platform device to free
 *
 * Free all memory associated with a platform device.  This function must
 * _only_ be externally called in error cases.  All other usage is a bug.
 */
void platform_device_put(struct platform_device *pdev)
{
	if (pdev)
		put_device(&pdev->dev);
}
EXPORT_SYMBOL_GPL(platform_device_put);

static void platform_device_release(struct device *dev)
{
	struct platform_object *pa = container_of(dev, struct platform_object,
						  pdev.dev);

	of_device_node_put(&pa->pdev.dev);
	kfree(pa->pdev.dev.platform_data);
	kfree(pa->pdev.mfd_cell);
	kfree(pa->pdev.resource);
	kfree(pa->pdev.driver_override);
	kfree(pa);
}

/**
 * platform_device_alloc - create a platform device
 * @name: base name of the device we're adding
 * @id: instance id
 *
 * Create a platform device object which can have other objects attached
 * to it, and which will have attached objects freed when it is released.
 */
struct platform_device *platform_device_alloc(const char *name, int id)
{
	struct platform_object *pa;

	pa = kzalloc(sizeof(*pa) + strlen(name) + 1, GFP_KERNEL);
	if (pa) {
		strcpy(pa->name, name);
		pa->pdev.name = pa->name;
		pa->pdev.id = id;
		device_initialize(&pa->pdev.dev);
		pa->pdev.dev.release = platform_device_release;
		arch_setup_pdev_archdata(&pa->pdev);
	}

	return pa ? &pa->pdev : NULL;
}
EXPORT_SYMBOL_GPL(platform_device_alloc);

/**
 * platform_device_add_resources - add resources to a platform device
 * @pdev: platform device allocated by platform_device_alloc to add resources to
 * @res: set of resources that needs to be allocated for the device
 * @num: number of resources
 *
 * Add a copy of the resources to the platform device.  The memory
 * associated with the resources will be freed when the platform device is
 * released.
 */
int platform_device_add_resources(struct platform_device *pdev,
				  const struct resource *res, unsigned int num)
{
	struct resource *r = NULL;

	if (res) {
		r = kmemdup(res, sizeof(struct resource) * num, GFP_KERNEL);
		if (!r)
			return -ENOMEM;
	}

	kfree(pdev->resource);
	pdev->resource = r;
	pdev->num_resources = num;
	return 0;
}
EXPORT_SYMBOL_GPL(platform_device_add_resources);

/**
 * platform_device_add_data - add platform-specific data to a platform device
 * @pdev: platform device allocated by platform_device_alloc to add resources to
 * @data: platform specific data for this platform device
 * @size: size of platform specific data
 *
 * Add a copy of platform specific data to the platform device's
 * platform_data pointer.  The memory associated with the platform data
 * will be freed when the platform device is released.
 */
int platform_device_add_data(struct platform_device *pdev, const void *data,
			     size_t size)
{
	void *d = NULL;

	if (data) {
		d = kmemdup(data, size, GFP_KERNEL);
		if (!d)
			return -ENOMEM;
	}

	kfree(pdev->dev.platform_data);
	pdev->dev.platform_data = d;
	return 0;
}
EXPORT_SYMBOL_GPL(platform_device_add_data);

/**
 * platform_device_add_properties - add built-in properties to a platform device
 * @pdev: platform device to add properties to
 * @properties: null terminated array of properties to add
 *
 * The function will take deep copy of @properties and attach the copy to the
 * platform device. The memory associated with properties will be freed when the
 * platform device is released.
 */
int platform_device_add_properties(struct platform_device *pdev,
				   const struct property_entry *properties)
{
	return device_add_properties(&pdev->dev, properties);
}
EXPORT_SYMBOL_GPL(platform_device_add_properties);

/**
 * platform_device_add - add a platform device to device hierarchy
 * @pdev: platform device we're adding
 *
 * This is part 2 of platform_device_register(), though may be called
 * separately _iff_ pdev was allocated by platform_device_alloc().
 */
int platform_device_add(struct platform_device *pdev)
{
	int i, ret;

	if (!pdev)
		return -EINVAL;

	if (!pdev->dev.parent)
		pdev->dev.parent = &platform_bus;

	pdev->dev.bus = &platform_bus_type;

	switch (pdev->id) {
	default:
		dev_set_name(&pdev->dev, "%s.%d", pdev->name,  pdev->id);
		break;
	case PLATFORM_DEVID_NONE:
		dev_set_name(&pdev->dev, "%s", pdev->name);
		break;
	case PLATFORM_DEVID_AUTO:
		/*
		 * Automatically allocated device ID. We mark it as such so
		 * that we remember it must be freed, and we append a suffix
		 * to avoid namespace collision with explicit IDs.
		 */
		ret = ida_simple_get(&platform_devid_ida, 0, 0, GFP_KERNEL);
		if (ret < 0)
			goto err_out;
		pdev->id = ret;
		pdev->id_auto = true;
		dev_set_name(&pdev->dev, "%s.%d.auto", pdev->name, pdev->id);
		break;
	}

	for (i = 0; i < pdev->num_resources; i++) {
		struct resource *p, *r = &pdev->resource[i];

		if (r->name == NULL)
			r->name = dev_name(&pdev->dev);

		p = r->parent;
		if (!p) {
			if (resource_type(r) == IORESOURCE_MEM)
				p = &iomem_resource;
			else if (resource_type(r) == IORESOURCE_IO)
				p = &ioport_resource;
		}

		if (p && insert_resource(p, r)) {
			dev_err(&pdev->dev, "failed to claim resource %d: %pR\n", i, r);
			ret = -EBUSY;
			goto failed;
		}
	}

	pr_debug("Registering platform device '%s'. Parent at %s\n",
		 dev_name(&pdev->dev), dev_name(pdev->dev.parent));

	ret = device_add(&pdev->dev);
	if (ret == 0)
		return ret;

 failed:
	if (pdev->id_auto) {
		ida_simple_remove(&platform_devid_ida, pdev->id);
		pdev->id = PLATFORM_DEVID_AUTO;
	}

	while (--i >= 0) {
		struct resource *r = &pdev->resource[i];
		if (r->parent)
			release_resource(r);
	}

 err_out:
	return ret;
}
EXPORT_SYMBOL_GPL(platform_device_add);

/**
 * platform_device_del - remove a platform-level device
 * @pdev: platform device we're removing
 *
 * Note that this function will also release all memory- and port-based
 * resources owned by the device (@dev->resource).  This function must
 * _only_ be externally called in error cases.  All other usage is a bug.
 */
void platform_device_del(struct platform_device *pdev)
{
	int i;

	if (pdev) {
		device_remove_properties(&pdev->dev);
		device_del(&pdev->dev);

		if (pdev->id_auto) {
			ida_simple_remove(&platform_devid_ida, pdev->id);
			pdev->id = PLATFORM_DEVID_AUTO;
		}

		for (i = 0; i < pdev->num_resources; i++) {
			struct resource *r = &pdev->resource[i];
			if (r->parent)
				release_resource(r);
		}
	}
}
EXPORT_SYMBOL_GPL(platform_device_del);

/**
 * platform_device_register - add a platform-level device
 * @pdev: platform device we're adding
 */
int platform_device_register(struct platform_device *pdev)
{
	device_initialize(&pdev->dev);
	arch_setup_pdev_archdata(pdev);
	return platform_device_add(pdev);
}
EXPORT_SYMBOL_GPL(platform_device_register);

/**
 * platform_device_unregister - unregister a platform-level device
 * @pdev: platform device we're unregistering
 *
 * Unregistration is done in 2 steps. First we release all resources
 * and remove it from the subsystem, then we drop reference count by
 * calling platform_device_put().
 */
void platform_device_unregister(struct platform_device *pdev)
{
	platform_device_del(pdev);
	platform_device_put(pdev);
}
EXPORT_SYMBOL_GPL(platform_device_unregister);

/**
 * platform_device_register_full - add a platform-level device with
 * resources and platform-specific data
 *
 * @pdevinfo: data used to create device
 *
 * Returns &struct platform_device pointer on success, or ERR_PTR() on error.
 */
struct platform_device *platform_device_register_full(
		const struct platform_device_info *pdevinfo)
{
	int ret = -ENOMEM;
	struct platform_device *pdev;

	pdev = platform_device_alloc(pdevinfo->name, pdevinfo->id);
	if (!pdev)
		goto err_alloc;

	pdev->dev.parent = pdevinfo->parent;
	pdev->dev.fwnode = pdevinfo->fwnode;

	if (pdevinfo->dma_mask) {
		/*
		 * This memory isn't freed when the device is put,
		 * I don't have a nice idea for that though.  Conceptually
		 * dma_mask in struct device should not be a pointer.
		 * See http://thread.gmane.org/gmane.linux.kernel.pci/9081
		 */
		pdev->dev.dma_mask =
			kmalloc(sizeof(*pdev->dev.dma_mask), GFP_KERNEL);
		if (!pdev->dev.dma_mask)
			goto err;

		*pdev->dev.dma_mask = pdevinfo->dma_mask;
		pdev->dev.coherent_dma_mask = pdevinfo->dma_mask;
	}

	ret = platform_device_add_resources(pdev,
			pdevinfo->res, pdevinfo->num_res);
	if (ret)
		goto err;

	ret = platform_device_add_data(pdev,
			pdevinfo->data, pdevinfo->size_data);
	if (ret)
		goto err;

	if (pdevinfo->properties) {
		ret = platform_device_add_properties(pdev,
						     pdevinfo->properties);
		if (ret)
			goto err;
	}

	ret = platform_device_add(pdev);
	if (ret) {
err:
		ACPI_COMPANION_SET(&pdev->dev, NULL);
		kfree(pdev->dev.dma_mask);

err_alloc:
		platform_device_put(pdev);
		return ERR_PTR(ret);
	}

	return pdev;
}
EXPORT_SYMBOL_GPL(platform_device_register_full);

static int platform_drv_probe(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);
	int ret;

	ret = of_clk_set_defaults(_dev->of_node, false);
	if (ret < 0)
		return ret;

	ret = dev_pm_domain_attach(_dev, true);
	if (ret != -EPROBE_DEFER) {
		if (drv->probe) {
			ret = drv->probe(dev);
			if (ret)
				dev_pm_domain_detach(_dev, true);
		} else {
			/* don't fail if just dev_pm_domain_attach failed */
			ret = 0;
		}
	}

	if (drv->prevent_deferred_probe && ret == -EPROBE_DEFER) {
		dev_warn(_dev, "probe deferral not supported\n");
		ret = -ENXIO;
	}

	return ret;
}

static int platform_drv_probe_fail(struct device *_dev)
{
	return -ENXIO;
}

static int platform_drv_remove(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);
	int ret = 0;

	if (drv->remove)
		ret = drv->remove(dev);
	dev_pm_domain_detach(_dev, true);

	return ret;
}

static void platform_drv_shutdown(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);

	if (drv->shutdown)
		drv->shutdown(dev);
}

/**
 * __platform_driver_register - register a driver for platform-level devices
 * @drv: platform driver structure
 * @owner: owning module/driver
 */
int __platform_driver_register(struct platform_driver *drv,
				struct module *owner)
{
	drv->driver.owner = owner;
	drv->driver.bus = &platform_bus_type;
	drv->driver.probe = platform_drv_probe;
	drv->driver.remove = platform_drv_remove;
	drv->driver.shutdown = platform_drv_shutdown;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__platform_driver_register);

/**
 * platform_driver_unregister - unregister a driver for platform-level devices
 * @drv: platform driver structure
 */
void platform_driver_unregister(struct platform_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(platform_driver_unregister);

/**
 * __platform_driver_probe - register driver for non-hotpluggable device
 * @drv: platform driver structure
 * @probe: the driver probe routine, probably from an __init section
 * @module: module which will be the owner of the driver
 *
 * Use this instead of platform_driver_register() when you know the device
 * is not hotpluggable and has already been registered, and you want to
 * remove its run-once probe() infrastructure from memory after the driver
 * has bound to the device.
 *
 * One typical use for this would be with drivers for controllers integrated
 * into system-on-chip processors, where the controller devices have been
 * configured as part of board setup.
 *
 * Note that this is incompatible with deferred probing.
 *
 * Returns zero if the driver registered and bound to a device, else returns
 * a negative error code and with the driver not registered.
 */
int __init_or_module __platform_driver_probe(struct platform_driver *drv,
		int (*probe)(struct platform_device *), struct module *module)
{
	int retval, code;

	if (drv->driver.probe_type == PROBE_PREFER_ASYNCHRONOUS) {
		pr_err("%s: drivers registered with %s can not be probed asynchronously\n",
			 drv->driver.name, __func__);
		return -EINVAL;
	}

	/*
	 * We have to run our probes synchronously because we check if
	 * we find any devices to bind to and exit with error if there
	 * are any.
	 */
	drv->driver.probe_type = PROBE_FORCE_SYNCHRONOUS;

	/*
	 * Prevent driver from requesting probe deferral to avoid further
	 * futile probe attempts.
	 */
	drv->prevent_deferred_probe = true;

	/* make sure driver won't have bind/unbind attributes */
	drv->driver.suppress_bind_attrs = true;

	/* temporary section violation during probe() */
	drv->probe = probe;
	retval = code = __platform_driver_register(drv, module);

	/*
	 * Fixup that section violation, being paranoid about code scanning
	 * the list of drivers in order to probe new devices.  Check to see
	 * if the probe was successful, and make sure any forced probes of
	 * new devices fail.
	 */
	spin_lock(&drv->driver.bus->p->klist_drivers.k_lock);
	drv->probe = NULL;
	if (code == 0 && list_empty(&drv->driver.p->klist_devices.k_list))
		retval = -ENODEV;
	drv->driver.probe = platform_drv_probe_fail;
	spin_unlock(&drv->driver.bus->p->klist_drivers.k_lock);

	if (code != retval)
		platform_driver_unregister(drv);
	return retval;
}
EXPORT_SYMBOL_GPL(__platform_driver_probe);

/**
 * __platform_create_bundle - register driver and create corresponding device
 * @driver: platform driver structure
 * @probe: the driver probe routine, probably from an __init section
 * @res: set of resources that needs to be allocated for the device
 * @n_res: number of resources
 * @data: platform specific data for this platform device
 * @size: size of platform specific data
 * @module: module which will be the owner of the driver
 *
 * Use this in legacy-style modules that probe hardware directly and
 * register a single platform device and corresponding platform driver.
 *
 * Returns &struct platform_device pointer on success, or ERR_PTR() on error.
 */
struct platform_device * __init_or_module __platform_create_bundle(
			struct platform_driver *driver,
			int (*probe)(struct platform_device *),
			struct resource *res, unsigned int n_res,
			const void *data, size_t size, struct module *module)
{
	struct platform_device *pdev;
	int error;

	pdev = platform_device_alloc(driver->driver.name, -1);
	if (!pdev) {
		error = -ENOMEM;
		goto err_out;
	}

	error = platform_device_add_resources(pdev, res, n_res);
	if (error)
		goto err_pdev_put;

	error = platform_device_add_data(pdev, data, size);
	if (error)
		goto err_pdev_put;

	error = platform_device_add(pdev);
	if (error)
		goto err_pdev_put;

	error = __platform_driver_probe(driver, probe, module);
	if (error)
		goto err_pdev_del;

	return pdev;

err_pdev_del:
	platform_device_del(pdev);
err_pdev_put:
	platform_device_put(pdev);
err_out:
	return ERR_PTR(error);
}
EXPORT_SYMBOL_GPL(__platform_create_bundle);

/**
 * __platform_register_drivers - register an array of platform drivers
 * @drivers: an array of drivers to register
 * @count: the number of drivers to register
 * @owner: module owning the drivers
 *
 * Registers platform drivers specified by an array. On failure to register a
 * driver, all previously registered drivers will be unregistered. Callers of
 * this API should use platform_unregister_drivers() to unregister drivers in
 * the reverse order.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
int __platform_register_drivers(struct platform_driver * const *drivers,
				unsigned int count, struct module *owner)
{
	unsigned int i;
	int err;

	for (i = 0; i < count; i++) {
		pr_debug("registering platform driver %ps\n", drivers[i]);

		err = __platform_driver_register(drivers[i], owner);
		if (err < 0) {
			pr_err("failed to register platform driver %ps: %d\n",
			       drivers[i], err);
			goto error;
		}
	}

	return 0;

error:
	while (i--) {
		pr_debug("unregistering platform driver %ps\n", drivers[i]);
		platform_driver_unregister(drivers[i]);
	}

	return err;
}
EXPORT_SYMBOL_GPL(__platform_register_drivers);

/**
 * platform_unregister_drivers - unregister an array of platform drivers
 * @drivers: an array of drivers to unregister
 * @count: the number of drivers to unregister
 *
 * Unegisters platform drivers specified by an array. This is typically used
 * to complement an earlier call to platform_register_drivers(). Drivers are
 * unregistered in the reverse order in which they were registered.
 */
void platform_unregister_drivers(struct platform_driver * const *drivers,
				 unsigned int count)
{
	while (count--) {
		pr_debug("unregistering platform driver %ps\n", drivers[count]);
		platform_driver_unregister(drivers[count]);
	}
}
EXPORT_SYMBOL_GPL(platform_unregister_drivers);

/* modalias support enables more hands-off userspace setup:
 * (a) environment variable lets new-style hotplug events work once system is
 *     fully running:  "modprobe $MODALIAS"
 * (b) sysfs attribute lets new-style coldplug recover from hotplug events
 *     mishandled before system is fully running:  "modprobe $(cat modalias)"
 */
static ssize_t modalias_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	struct platform_device	*pdev = to_platform_device(dev);
	int len;

	len = of_device_modalias(dev, buf, PAGE_SIZE);
	if (len != -ENODEV)
		return len;

	len = acpi_device_modalias(dev, buf, PAGE_SIZE -1);
	if (len != -ENODEV)
		return len;

	len = snprintf(buf, PAGE_SIZE, "platform:%s\n", pdev->name);

	return (len >= PAGE_SIZE) ? (PAGE_SIZE - 1) : len;
}
static DEVICE_ATTR_RO(modalias);

static ssize_t driver_override_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	char *driver_override, *old, *cp;

	if (count > PATH_MAX)
		return -EINVAL;

	driver_override = kstrndup(buf, count, GFP_KERNEL);
	if (!driver_override)
		return -ENOMEM;

	cp = strchr(driver_override, '\n');
	if (cp)
		*cp = '\0';

	device_lock(dev);
	old = pdev->driver_override;
	if (strlen(driver_override)) {
		pdev->driver_override = driver_override;
	} else {
		kfree(driver_override);
		pdev->driver_override = NULL;
	}
	device_unlock(dev);

	kfree(old);

	return count;
}

static ssize_t driver_override_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	ssize_t len;

	device_lock(dev);
	len = sprintf(buf, "%s\n", pdev->driver_override);
	device_unlock(dev);
	return len;
}
static DEVICE_ATTR_RW(driver_override);


static struct attribute *platform_dev_attrs[] = {
	&dev_attr_modalias.attr,
	&dev_attr_driver_override.attr,
	NULL,
};
ATTRIBUTE_GROUPS(platform_dev);

static int platform_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct platform_device	*pdev = to_platform_device(dev);
	int rc;

	/* Some devices have extra OF data and an OF-style MODALIAS */
	rc = of_device_uevent_modalias(dev, env);
	if (rc != -ENODEV)
		return rc;

	rc = acpi_device_uevent_modalias(dev, env);
	if (rc != -ENODEV)
		return rc;

	add_uevent_var(env, "MODALIAS=%s%s", PLATFORM_MODULE_PREFIX,
			pdev->name);
	return 0;
}

static const struct platform_device_id *platform_match_id(
			const struct platform_device_id *id,
			struct platform_device *pdev)
{
	while (id->name[0]) {
		if (strcmp(pdev->name, id->name) == 0) {
			pdev->id_entry = id;
			return id;
		}
		id++;
	}
	return NULL;
}

/**
 * platform_match - bind platform device to platform driver.
 * @dev: device.
 * @drv: driver.
 *
 * Platform device IDs are assumed to be encoded like this:
 * "<name><instance>", where <name> is a short description of the type of
 * device, like "pci" or "floppy", and <instance> is the enumerated
 * instance of the device, like '0' or '42'.  Driver IDs are simply
 * "<name>".  So, extract the <name> from the platform_device structure,
 * and compare it against the name of the driver. Return whether they match
 * or not.
 */
static int platform_match(struct device *dev, struct device_driver *drv)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct platform_driver *pdrv = to_platform_driver(drv);

	/* When driver_override is set, only bind to the matching driver */
	if (pdev->driver_override)
		return !strcmp(pdev->driver_override, drv->name);

	/* Attempt an OF style match first */
	if (of_driver_match_device(dev, drv))
		return 1;

	/* Then try ACPI style match */
	if (acpi_driver_match_device(dev, drv))
		return 1;

	/* Then try to match against the id table */
	if (pdrv->id_table)
		return platform_match_id(pdrv->id_table, pdev) != NULL;

	/* fall-back to driver name match */
	return (strcmp(pdev->name, drv->name) == 0);
}

#ifdef CONFIG_PM_SLEEP

static int platform_legacy_suspend(struct device *dev, pm_message_t mesg)
{
	struct platform_driver *pdrv = to_platform_driver(dev->driver);
	struct platform_device *pdev = to_platform_device(dev);
	int ret = 0;

	if (dev->driver && pdrv->suspend)
		ret = pdrv->suspend(pdev, mesg);

	return ret;
}

static int platform_legacy_resume(struct device *dev)
{
	struct platform_driver *pdrv = to_platform_driver(dev->driver);
	struct platform_device *pdev = to_platform_device(dev);
	int ret = 0;

	if (dev->driver && pdrv->resume)
		ret = pdrv->resume(pdev);

	return ret;
}

#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_SUSPEND

int platform_pm_suspend(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->suspend)
			ret = drv->pm->suspend(dev);
	} else {
		ret = platform_legacy_suspend(dev, PMSG_SUSPEND);
	}

	return ret;
}

int platform_pm_resume(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->resume)
			ret = drv->pm->resume(dev);
	} else {
		ret = platform_legacy_resume(dev);
	}

	return ret;
}

#endif /* CONFIG_SUSPEND */

#ifdef CONFIG_HIBERNATE_CALLBACKS

int platform_pm_freeze(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->freeze)
			ret = drv->pm->freeze(dev);
	} else {
		ret = platform_legacy_suspend(dev, PMSG_FREEZE);
	}

	return ret;
}

int platform_pm_thaw(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->thaw)
			ret = drv->pm->thaw(dev);
	} else {
		ret = platform_legacy_resume(dev);
	}

	return ret;
}

int platform_pm_poweroff(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->poweroff)
			ret = drv->pm->poweroff(dev);
	} else {
		ret = platform_legacy_suspend(dev, PMSG_HIBERNATE);
	}

	return ret;
}

int platform_pm_restore(struct device *dev)
{
	struct device_driver *drv = dev->driver;
	int ret = 0;

	if (!drv)
		return 0;

	if (drv->pm) {
		if (drv->pm->restore)
			ret = drv->pm->restore(dev);
	} else {
		ret = platform_legacy_resume(dev);
	}

	return ret;
}

#endif /* CONFIG_HIBERNATE_CALLBACKS */

static const struct dev_pm_ops platform_dev_pm_ops = {
	.runtime_suspend = pm_generic_runtime_suspend,
	.runtime_resume = pm_generic_runtime_resume,
	USE_PLATFORM_PM_SLEEP_OPS
};

struct bus_type platform_bus_type = {
	.name		= "platform",
	.dev_groups	= platform_dev_groups,
	.match		= platform_match,
	.uevent		= platform_uevent,
	.pm		= &platform_dev_pm_ops,
};
EXPORT_SYMBOL_GPL(platform_bus_type);

int __init platform_bus_init(void)
{
	int error;

	early_platform_cleanup();

	error = device_register(&platform_bus);
	if (error)
		return error;
	error =  bus_register(&platform_bus_type);
	if (error)
		device_unregister(&platform_bus);
	of_platform_register_reconfig_notifier();
	return error;
}

#ifndef ARCH_HAS_DMA_GET_REQUIRED_MASK
u64 dma_get_required_mask(struct device *dev)
{
	u32 low_totalram = ((max_pfn - 1) << PAGE_SHIFT);
	u32 high_totalram = ((max_pfn - 1) >> (32 - PAGE_SHIFT));
	u64 mask;

	if (!high_totalram) {
		/* convert to mask just covering totalram */
		low_totalram = (1 << (fls(low_totalram) - 1));
		low_totalram += low_totalram - 1;
		mask = low_totalram;
	} else {
		high_totalram = (1 << (fls(high_totalram) - 1));
		high_totalram += high_totalram - 1;
		mask = (((u64)high_totalram) << 32) + 0xffffffff;
	}
	return mask;
}
EXPORT_SYMBOL_GPL(dma_get_required_mask);
#endif

static __initdata LIST_HEAD(early_platform_driver_list);
static __initdata LIST_HEAD(early_platform_device_list);

/**
 * early_platform_driver_register - register early platform driver
 * @epdrv: early_platform driver structure
 * @buf: string passed from early_param()
 *
 * Helper function for early_platform_init() / early_platform_init_buffer()
 */
int __init early_platform_driver_register(struct early_platform_driver *epdrv,
					  char *buf)
{
	char *tmp;
	int n;

	/* Simply add the driver to the end of the global list.
	 * Drivers will by default be put on the list in compiled-in order.
	 */
	if (!epdrv->list.next) {
		INIT_LIST_HEAD(&epdrv->list);
		list_add_tail(&epdrv->list, &early_platform_driver_list);
	}

	/* If the user has specified device then make sure the driver
	 * gets prioritized. The driver of the last device specified on
	 * command line will be put first on the list.
	 */
	n = strlen(epdrv->pdrv->driver.name);
	if (buf && !strncmp(buf, epdrv->pdrv->driver.name, n)) {
		list_move(&epdrv->list, &early_platform_driver_list);

		/* Allow passing parameters after device name */
		if (buf[n] == '\0' || buf[n] == ',')
			epdrv->requested_id = -1;
		else {
			epdrv->requested_id = simple_strtoul(&buf[n + 1],
							     &tmp, 10);

			if (buf[n] != '.' || (tmp == &buf[n + 1])) {
				epdrv->requested_id = EARLY_PLATFORM_ID_ERROR;
				n = 0;
			} else
				n += strcspn(&buf[n + 1], ",") + 1;
		}

		if (buf[n] == ',')
			n++;

		if (epdrv->bufsize) {
			memcpy(epdrv->buffer, &buf[n],
			       min_t(int, epdrv->bufsize, strlen(&buf[n]) + 1));
			epdrv->buffer[epdrv->bufsize - 1] = '\0';
		}
	}

	return 0;
}

/**
 * early_platform_add_devices - adds a number of early platform devices
 * @devs: array of early platform devices to add
 * @num: number of early platform devices in array
 *
 * Used by early architecture code to register early platform devices and
 * their platform data.
 */
void __init early_platform_add_devices(struct platform_device **devs, int num)
{
	struct device *dev;
	int i;

	/* simply add the devices to list */
	for (i = 0; i < num; i++) {
		dev = &devs[i]->dev;

		if (!dev->devres_head.next) {
			pm_runtime_early_init(dev);
			INIT_LIST_HEAD(&dev->devres_head);
			list_add_tail(&dev->devres_head,
				      &early_platform_device_list);
		}
	}
}

/**
 * early_platform_driver_register_all - register early platform drivers
 * @class_str: string to identify early platform driver class
 *
 * Used by architecture code to register all early platform drivers
 * for a certain class. If omitted then only early platform drivers
 * with matching kernel command line class parameters will be registered.
 */
void __init early_platform_driver_register_all(char *class_str)
{
	/* The "class_str" parameter may or may not be present on the kernel
	 * command line. If it is present then there may be more than one
	 * matching parameter.
	 *
	 * Since we register our early platform drivers using early_param()
	 * we need to make sure that they also get registered in the case
	 * when the parameter is missing from the kernel command line.
	 *
	 * We use parse_early_options() to make sure the early_param() gets
	 * called at least once. The early_param() may be called more than
	 * once since the name of the preferred device may be specified on
	 * the kernel command line. early_platform_driver_register() handles
	 * this case for us.
	 */
	parse_early_options(class_str);
}

/**
 * early_platform_match - find early platform device matching driver
 * @epdrv: early platform driver structure
 * @id: id to match against
 */
static struct platform_device * __init
early_platform_match(struct early_platform_driver *epdrv, int id)
{
	struct platform_device *pd;

	list_for_each_entry(pd, &early_platform_device_list, dev.devres_head)
		if (platform_match(&pd->dev, &epdrv->pdrv->driver))
			if (pd->id == id)
				return pd;

	return NULL;
}

/**
 * early_platform_left - check if early platform driver has matching devices
 * @epdrv: early platform driver structure
 * @id: return true if id or above exists
 */
static int __init early_platform_left(struct early_platform_driver *epdrv,
				       int id)
{
	struct platform_device *pd;

	list_for_each_entry(pd, &early_platform_device_list, dev.devres_head)
		if (platform_match(&pd->dev, &epdrv->pdrv->driver))
			if (pd->id >= id)
				return 1;

	return 0;
}

/**
 * early_platform_driver_probe_id - probe drivers matching class_str and id
 * @class_str: string to identify early platform driver class
 * @id: id to match against
 * @nr_probe: number of platform devices to successfully probe before exiting
 */
static int __init early_platform_driver_probe_id(char *class_str,
						 int id,
						 int nr_probe)
{
	struct early_platform_driver *epdrv;
	struct platform_device *match;
	int match_id;
	int n = 0;
	int left = 0;

	list_for_each_entry(epdrv, &early_platform_driver_list, list) {
		/* only use drivers matching our class_str */
		if (strcmp(class_str, epdrv->class_str))
			continue;

		if (id == -2) {
			match_id = epdrv->requested_id;
			left = 1;

		} else {
			match_id = id;
			left += early_platform_left(epdrv, id);

			/* skip requested id */
			switch (epdrv->requested_id) {
			case EARLY_PLATFORM_ID_ERROR:
			case EARLY_PLATFORM_ID_UNSET:
				break;
			default:
				if (epdrv->requested_id == id)
					match_id = EARLY_PLATFORM_ID_UNSET;
			}
		}

		switch (match_id) {
		case EARLY_PLATFORM_ID_ERROR:
			pr_warn("%s: unable to parse %s parameter\n",
				class_str, epdrv->pdrv->driver.name);
			/* fall-through */
		case EARLY_PLATFORM_ID_UNSET:
			match = NULL;
			break;
		default:
			match = early_platform_match(epdrv, match_id);
		}

		if (match) {
			/*
			 * Set up a sensible init_name to enable
			 * dev_name() and others to be used before the
			 * rest of the driver core is initialized.
			 */
			if (!match->dev.init_name && slab_is_available()) {
				if (match->id != -1)
					match->dev.init_name =
						kasprintf(GFP_KERNEL, "%s.%d",
							  match->name,
							  match->id);
				else
					match->dev.init_name =
						kasprintf(GFP_KERNEL, "%s",
							  match->name);

				if (!match->dev.init_name)
					return -ENOMEM;
			}

			if (epdrv->pdrv->probe(match))
				pr_warn("%s: unable to probe %s early.\n",
					class_str, match->name);
			else
				n++;
		}

		if (n >= nr_probe)
			break;
	}

	if (left)
		return n;
	else
		return -ENODEV;
}

/**
 * early_platform_driver_probe - probe a class of registered drivers
 * @class_str: string to identify early platform driver class
 * @nr_probe: number of platform devices to successfully probe before exiting
 * @user_only: only probe user specified early platform devices
 *
 * Used by architecture code to probe registered early platform drivers
 * within a certain class. For probe to happen a registered early platform
 * device matching a registered early platform driver is needed.
 */
int __init early_platform_driver_probe(char *class_str,
				       int nr_probe,
				       int user_only)
{
	int k, n, i;

	n = 0;
	for (i = -2; n < nr_probe; i++) {
		k = early_platform_driver_probe_id(class_str, i, nr_probe - n);

		if (k < 0)
			break;

		n += k;

		if (user_only)
			break;
	}

	return n;
}

/**
 * early_platform_cleanup - clean up early platform code
 */
void __init early_platform_cleanup(void)
{
	struct platform_device *pd, *pd2;

	/* clean up the devres list used to chain devices */
	list_for_each_entry_safe(pd, pd2, &early_platform_device_list,
				 dev.devres_head) {
		list_del(&pd->dev.devres_head);
		memset(&pd->dev.devres_head, 0, sizeof(pd->dev.devres_head));
	}
}

