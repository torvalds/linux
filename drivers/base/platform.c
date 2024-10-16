// SPDX-License-Identifier: GPL-2.0
/*
 * platform.c - platform 'pseudo' bus for legacy devices
 *
 * Copyright (c) 2002-3 Patrick Mochel
 * Copyright (c) 2002-3 Open Source Development Labs
 *
 * Please see Documentation/driver-api/driver-model/platform.rst for more
 * information.
 */

#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/dma-mapping.h>
#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/idr.h>
#include <linux/acpi.h>
#include <linux/clk/clk-conf.h>
#include <linux/limits.h>
#include <linux/property.h>
#include <linux/kmemleak.h>
#include <linux/types.h>
#include <linux/iommu.h>
#include <linux/dma-map-ops.h>

#include "base.h"
#include "power/power.h"

/* For automatically allocated device IDs */
static DEFINE_IDA(platform_devid_ida);

struct device platform_bus = {
	.init_name	= "platform",
};
EXPORT_SYMBOL_GPL(platform_bus);

/**
 * platform_get_resource - get a resource for a device
 * @dev: platform device
 * @type: resource type
 * @num: resource index
 *
 * Return: a pointer to the resource or NULL on failure.
 */
struct resource *platform_get_resource(struct platform_device *dev,
				       unsigned int type, unsigned int num)
{
	u32 i;

	for (i = 0; i < dev->num_resources; i++) {
		struct resource *r = &dev->resource[i];

		if (type == resource_type(r) && num-- == 0)
			return r;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(platform_get_resource);

struct resource *platform_get_mem_or_io(struct platform_device *dev,
					unsigned int num)
{
	u32 i;

	for (i = 0; i < dev->num_resources; i++) {
		struct resource *r = &dev->resource[i];

		if ((resource_type(r) & (IORESOURCE_MEM|IORESOURCE_IO)) && num-- == 0)
			return r;
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(platform_get_mem_or_io);

#ifdef CONFIG_HAS_IOMEM
/**
 * devm_platform_get_and_ioremap_resource - call devm_ioremap_resource() for a
 *					    platform device and get resource
 *
 * @pdev: platform device to use both for memory resource lookup as well as
 *        resource management
 * @index: resource index
 * @res: optional output parameter to store a pointer to the obtained resource.
 *
 * Return: a pointer to the remapped memory or an ERR_PTR() encoded error code
 * on failure.
 */
void __iomem *
devm_platform_get_and_ioremap_resource(struct platform_device *pdev,
				unsigned int index, struct resource **res)
{
	struct resource *r;

	r = platform_get_resource(pdev, IORESOURCE_MEM, index);
	if (res)
		*res = r;
	return devm_ioremap_resource(&pdev->dev, r);
}
EXPORT_SYMBOL_GPL(devm_platform_get_and_ioremap_resource);

/**
 * devm_platform_ioremap_resource - call devm_ioremap_resource() for a platform
 *				    device
 *
 * @pdev: platform device to use both for memory resource lookup as well as
 *        resource management
 * @index: resource index
 *
 * Return: a pointer to the remapped memory or an ERR_PTR() encoded error code
 * on failure.
 */
void __iomem *devm_platform_ioremap_resource(struct platform_device *pdev,
					     unsigned int index)
{
	return devm_platform_get_and_ioremap_resource(pdev, index, NULL);
}
EXPORT_SYMBOL_GPL(devm_platform_ioremap_resource);

/**
 * devm_platform_ioremap_resource_byname - call devm_ioremap_resource for
 *					   a platform device, retrieve the
 *					   resource by name
 *
 * @pdev: platform device to use both for memory resource lookup as well as
 *	  resource management
 * @name: name of the resource
 *
 * Return: a pointer to the remapped memory or an ERR_PTR() encoded error code
 * on failure.
 */
void __iomem *
devm_platform_ioremap_resource_byname(struct platform_device *pdev,
				      const char *name)
{
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, name);
	return devm_ioremap_resource(&pdev->dev, res);
}
EXPORT_SYMBOL_GPL(devm_platform_ioremap_resource_byname);
#endif /* CONFIG_HAS_IOMEM */

/**
 * platform_get_irq_optional - get an optional IRQ for a device
 * @dev: platform device
 * @num: IRQ number index
 *
 * Gets an IRQ for a platform device. Device drivers should check the return
 * value for errors so as to not pass a negative integer value to the
 * request_irq() APIs. This is the same as platform_get_irq(), except that it
 * does not print an error message if an IRQ can not be obtained.
 *
 * For example::
 *
 *		int irq = platform_get_irq_optional(pdev, 0);
 *		if (irq < 0)
 *			return irq;
 *
 * Return: non-zero IRQ number on success, negative error number on failure.
 */
int platform_get_irq_optional(struct platform_device *dev, unsigned int num)
{
	int ret;
#ifdef CONFIG_SPARC
	/* sparc does not have irqs represented as IORESOURCE_IRQ resources */
	if (!dev || num >= dev->archdata.num_irqs)
		goto out_not_found;
	ret = dev->archdata.irqs[num];
	goto out;
#else
	struct fwnode_handle *fwnode = dev_fwnode(&dev->dev);
	struct resource *r;

	if (is_of_node(fwnode)) {
		ret = of_irq_get(to_of_node(fwnode), num);
		if (ret > 0 || ret == -EPROBE_DEFER)
			goto out;
	}

	r = platform_get_resource(dev, IORESOURCE_IRQ, num);
	if (is_acpi_device_node(fwnode)) {
		if (r && r->flags & IORESOURCE_DISABLED) {
			ret = acpi_irq_get(ACPI_HANDLE_FWNODE(fwnode), num, r);
			if (ret)
				goto out;
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
			goto out_not_found;
		irqd_set_trigger_type(irqd, r->flags & IORESOURCE_BITS);
	}

	if (r) {
		ret = r->start;
		goto out;
	}

	/*
	 * For the index 0 interrupt, allow falling back to GpioInt
	 * resources. While a device could have both Interrupt and GpioInt
	 * resources, making this fallback ambiguous, in many common cases
	 * the device will only expose one IRQ, and this fallback
	 * allows a common code path across either kind of resource.
	 */
	if (num == 0 && is_acpi_device_node(fwnode)) {
		ret = acpi_dev_gpio_irq_get(to_acpi_device_node(fwnode), num);
		/* Our callers expect -ENXIO for missing IRQs. */
		if (ret >= 0 || ret == -EPROBE_DEFER)
			goto out;
	}

#endif
out_not_found:
	ret = -ENXIO;
out:
	if (WARN(!ret, "0 is an invalid IRQ number\n"))
		return -EINVAL;
	return ret;
}
EXPORT_SYMBOL_GPL(platform_get_irq_optional);

/**
 * platform_get_irq - get an IRQ for a device
 * @dev: platform device
 * @num: IRQ number index
 *
 * Gets an IRQ for a platform device and prints an error message if finding the
 * IRQ fails. Device drivers should check the return value for errors so as to
 * not pass a negative integer value to the request_irq() APIs.
 *
 * For example::
 *
 *		int irq = platform_get_irq(pdev, 0);
 *		if (irq < 0)
 *			return irq;
 *
 * Return: non-zero IRQ number on success, negative error number on failure.
 */
int platform_get_irq(struct platform_device *dev, unsigned int num)
{
	int ret;

	ret = platform_get_irq_optional(dev, num);
	if (ret < 0)
		return dev_err_probe(&dev->dev, ret,
				     "IRQ index %u not found\n", num);

	return ret;
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

	while ((ret = platform_get_irq_optional(dev, nr)) >= 0)
		nr++;

	if (ret == -EPROBE_DEFER)
		return ret;

	return nr;
}
EXPORT_SYMBOL_GPL(platform_irq_count);

struct irq_affinity_devres {
	unsigned int count;
	unsigned int irq[] __counted_by(count);
};

static void platform_disable_acpi_irq(struct platform_device *pdev, int index)
{
	struct resource *r;

	r = platform_get_resource(pdev, IORESOURCE_IRQ, index);
	if (r)
		irqresource_disabled(r, 0);
}

static void devm_platform_get_irqs_affinity_release(struct device *dev,
						    void *res)
{
	struct irq_affinity_devres *ptr = res;
	int i;

	for (i = 0; i < ptr->count; i++) {
		irq_dispose_mapping(ptr->irq[i]);

		if (is_acpi_device_node(dev_fwnode(dev)))
			platform_disable_acpi_irq(to_platform_device(dev), i);
	}
}

/**
 * devm_platform_get_irqs_affinity - devm method to get a set of IRQs for a
 *				device using an interrupt affinity descriptor
 * @dev: platform device pointer
 * @affd: affinity descriptor
 * @minvec: minimum count of interrupt vectors
 * @maxvec: maximum count of interrupt vectors
 * @irqs: pointer holder for IRQ numbers
 *
 * Gets a set of IRQs for a platform device, and updates IRQ afffinty according
 * to the passed affinity descriptor
 *
 * Return: Number of vectors on success, negative error number on failure.
 */
int devm_platform_get_irqs_affinity(struct platform_device *dev,
				    struct irq_affinity *affd,
				    unsigned int minvec,
				    unsigned int maxvec,
				    int **irqs)
{
	struct irq_affinity_devres *ptr;
	struct irq_affinity_desc *desc;
	size_t size;
	int i, ret, nvec;

	if (!affd)
		return -EPERM;

	if (maxvec < minvec)
		return -ERANGE;

	nvec = platform_irq_count(dev);
	if (nvec < 0)
		return nvec;

	if (nvec < minvec)
		return -ENOSPC;

	nvec = irq_calc_affinity_vectors(minvec, nvec, affd);
	if (nvec < minvec)
		return -ENOSPC;

	if (nvec > maxvec)
		nvec = maxvec;

	size = sizeof(*ptr) + sizeof(unsigned int) * nvec;
	ptr = devres_alloc(devm_platform_get_irqs_affinity_release, size,
			   GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ptr->count = nvec;

	for (i = 0; i < nvec; i++) {
		int irq = platform_get_irq(dev, i);
		if (irq < 0) {
			ret = irq;
			goto err_free_devres;
		}
		ptr->irq[i] = irq;
	}

	desc = irq_create_affinity_masks(nvec, affd);
	if (!desc) {
		ret = -ENOMEM;
		goto err_free_devres;
	}

	for (i = 0; i < nvec; i++) {
		ret = irq_update_affinity_desc(ptr->irq[i], &desc[i]);
		if (ret) {
			dev_err(&dev->dev, "failed to update irq%d affinity descriptor (%d)\n",
				ptr->irq[i], ret);
			goto err_free_desc;
		}
	}

	devres_add(&dev->dev, ptr);

	kfree(desc);

	*irqs = ptr->irq;

	return nvec;

err_free_desc:
	kfree(desc);
err_free_devres:
	devres_free(ptr);
	return ret;
}
EXPORT_SYMBOL_GPL(devm_platform_get_irqs_affinity);

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
	u32 i;

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

static int __platform_get_irq_byname(struct platform_device *dev,
				     const char *name)
{
	struct resource *r;
	int ret;

	ret = fwnode_irq_get_byname(dev_fwnode(&dev->dev), name);
	if (ret > 0 || ret == -EPROBE_DEFER)
		return ret;

	r = platform_get_resource_byname(dev, IORESOURCE_IRQ, name);
	if (r) {
		if (WARN(!r->start, "0 is an invalid IRQ number\n"))
			return -EINVAL;
		return r->start;
	}

	return -ENXIO;
}

/**
 * platform_get_irq_byname - get an IRQ for a device by name
 * @dev: platform device
 * @name: IRQ name
 *
 * Get an IRQ like platform_get_irq(), but then by name rather then by index.
 *
 * Return: non-zero IRQ number on success, negative error number on failure.
 */
int platform_get_irq_byname(struct platform_device *dev, const char *name)
{
	int ret;

	ret = __platform_get_irq_byname(dev, name);
	if (ret < 0)
		return dev_err_probe(&dev->dev, ret, "IRQ %s not found\n",
				     name);
	return ret;
}
EXPORT_SYMBOL_GPL(platform_get_irq_byname);

/**
 * platform_get_irq_byname_optional - get an optional IRQ for a device by name
 * @dev: platform device
 * @name: IRQ name
 *
 * Get an optional IRQ by name like platform_get_irq_byname(). Except that it
 * does not print an error message if an IRQ can not be obtained.
 *
 * Return: non-zero IRQ number on success, negative error number on failure.
 */
int platform_get_irq_byname_optional(struct platform_device *dev,
				     const char *name)
{
	return __platform_get_irq_byname(dev, name);
}
EXPORT_SYMBOL_GPL(platform_get_irq_byname_optional);

/**
 * platform_add_devices - add a numbers of platform devices
 * @devs: array of platform devices to add
 * @num: number of platform devices in array
 *
 * Return: 0 on success, negative error number on failure.
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

/*
 * Set up default DMA mask for platform devices if the they weren't
 * previously set by the architecture / DT.
 */
static void setup_pdev_dma_masks(struct platform_device *pdev)
{
	pdev->dev.dma_parms = &pdev->dma_parms;

	if (!pdev->dev.coherent_dma_mask)
		pdev->dev.coherent_dma_mask = DMA_BIT_MASK(32);
	if (!pdev->dev.dma_mask) {
		pdev->platform_dma_mask = DMA_BIT_MASK(32);
		pdev->dev.dma_mask = &pdev->platform_dma_mask;
	}
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
	if (!IS_ERR_OR_NULL(pdev))
		put_device(&pdev->dev);
}
EXPORT_SYMBOL_GPL(platform_device_put);

static void platform_device_release(struct device *dev)
{
	struct platform_object *pa = container_of(dev, struct platform_object,
						  pdev.dev);

	of_node_put(pa->pdev.dev.of_node);
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
		setup_pdev_dma_masks(&pa->pdev);
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
		r = kmemdup_array(res, num, sizeof(*r), GFP_KERNEL);
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
 * platform_device_add - add a platform device to device hierarchy
 * @pdev: platform device we're adding
 *
 * This is part 2 of platform_device_register(), though may be called
 * separately _iff_ pdev was allocated by platform_device_alloc().
 */
int platform_device_add(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	u32 i;
	int ret;

	if (!dev->parent)
		dev->parent = &platform_bus;

	dev->bus = &platform_bus_type;

	switch (pdev->id) {
	default:
		dev_set_name(dev, "%s.%d", pdev->name,  pdev->id);
		break;
	case PLATFORM_DEVID_NONE:
		dev_set_name(dev, "%s", pdev->name);
		break;
	case PLATFORM_DEVID_AUTO:
		/*
		 * Automatically allocated device ID. We mark it as such so
		 * that we remember it must be freed, and we append a suffix
		 * to avoid namespace collision with explicit IDs.
		 */
		ret = ida_alloc(&platform_devid_ida, GFP_KERNEL);
		if (ret < 0)
			return ret;
		pdev->id = ret;
		pdev->id_auto = true;
		dev_set_name(dev, "%s.%d.auto", pdev->name, pdev->id);
		break;
	}

	for (i = 0; i < pdev->num_resources; i++) {
		struct resource *p, *r = &pdev->resource[i];

		if (r->name == NULL)
			r->name = dev_name(dev);

		p = r->parent;
		if (!p) {
			if (resource_type(r) == IORESOURCE_MEM)
				p = &iomem_resource;
			else if (resource_type(r) == IORESOURCE_IO)
				p = &ioport_resource;
		}

		if (p) {
			ret = insert_resource(p, r);
			if (ret) {
				dev_err(dev, "failed to claim resource %d: %pR\n", i, r);
				goto failed;
			}
		}
	}

	pr_debug("Registering platform device '%s'. Parent at %s\n", dev_name(dev),
		 dev_name(dev->parent));

	ret = device_add(dev);
	if (ret)
		goto failed;

	return 0;

 failed:
	if (pdev->id_auto) {
		ida_free(&platform_devid_ida, pdev->id);
		pdev->id = PLATFORM_DEVID_AUTO;
	}

	while (i--) {
		struct resource *r = &pdev->resource[i];
		if (r->parent)
			release_resource(r);
	}

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
	u32 i;

	if (!IS_ERR_OR_NULL(pdev)) {
		device_del(&pdev->dev);

		if (pdev->id_auto) {
			ida_free(&platform_devid_ida, pdev->id);
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
 *
 * NOTE: _Never_ directly free @pdev after calling this function, even if it
 * returned an error! Always use platform_device_put() to give up the
 * reference initialised in this function instead.
 */
int platform_device_register(struct platform_device *pdev)
{
	device_initialize(&pdev->dev);
	setup_pdev_dma_masks(pdev);
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
	int ret;
	struct platform_device *pdev;

	pdev = platform_device_alloc(pdevinfo->name, pdevinfo->id);
	if (!pdev)
		return ERR_PTR(-ENOMEM);

	pdev->dev.parent = pdevinfo->parent;
	pdev->dev.fwnode = pdevinfo->fwnode;
	pdev->dev.of_node = of_node_get(to_of_node(pdev->dev.fwnode));
	pdev->dev.of_node_reused = pdevinfo->of_node_reused;

	if (pdevinfo->dma_mask) {
		pdev->platform_dma_mask = pdevinfo->dma_mask;
		pdev->dev.dma_mask = &pdev->platform_dma_mask;
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
		ret = device_create_managed_software_node(&pdev->dev,
							  pdevinfo->properties, NULL);
		if (ret)
			goto err;
	}

	ret = platform_device_add(pdev);
	if (ret) {
err:
		ACPI_COMPANION_SET(&pdev->dev, NULL);
		platform_device_put(pdev);
		return ERR_PTR(ret);
	}

	return pdev;
}
EXPORT_SYMBOL_GPL(platform_device_register_full);

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

static int platform_probe_fail(struct platform_device *pdev)
{
	return -ENXIO;
}

static int is_bound_to_driver(struct device *dev, void *driver)
{
	if (dev->driver == driver)
		return 1;
	return 0;
}

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
	int retval;

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
	retval = __platform_driver_register(drv, module);
	if (retval)
		return retval;

	/* Force all new probes of this driver to fail */
	drv->probe = platform_probe_fail;

	/* Walk all platform devices and see if any actually bound to this driver.
	 * If not, return an error as the device should have done so by now.
	 */
	if (!bus_for_each_dev(&platform_bus_type, NULL, &drv->driver, is_bound_to_driver)) {
		retval = -ENODEV;
		platform_driver_unregister(drv);
	}

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
 * Unregisters platform drivers specified by an array. This is typically used
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
	const struct device_driver *drv = dev->driver;
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
	const struct device_driver *drv = dev->driver;
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
	const struct device_driver *drv = dev->driver;
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
	const struct device_driver *drv = dev->driver;
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
	const struct device_driver *drv = dev->driver;
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
	const struct device_driver *drv = dev->driver;
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

/* modalias support enables more hands-off userspace setup:
 * (a) environment variable lets new-style hotplug events work once system is
 *     fully running:  "modprobe $MODALIAS"
 * (b) sysfs attribute lets new-style coldplug recover from hotplug events
 *     mishandled before system is fully running:  "modprobe $(cat modalias)"
 */
static ssize_t modalias_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	int len;

	len = of_device_modalias(dev, buf, PAGE_SIZE);
	if (len != -ENODEV)
		return len;

	len = acpi_device_modalias(dev, buf, PAGE_SIZE - 1);
	if (len != -ENODEV)
		return len;

	return sysfs_emit(buf, "platform:%s\n", pdev->name);
}
static DEVICE_ATTR_RO(modalias);

static ssize_t numa_node_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", dev_to_node(dev));
}
static DEVICE_ATTR_RO(numa_node);

static ssize_t driver_override_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	ssize_t len;

	device_lock(dev);
	len = sysfs_emit(buf, "%s\n", pdev->driver_override);
	device_unlock(dev);

	return len;
}

static ssize_t driver_override_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	int ret;

	ret = driver_set_override(dev, &pdev->driver_override, buf, count);
	if (ret)
		return ret;

	return count;
}
static DEVICE_ATTR_RW(driver_override);

static struct attribute *platform_dev_attrs[] = {
	&dev_attr_modalias.attr,
	&dev_attr_numa_node.attr,
	&dev_attr_driver_override.attr,
	NULL,
};

static umode_t platform_dev_attrs_visible(struct kobject *kobj, struct attribute *a,
		int n)
{
	struct device *dev = container_of(kobj, typeof(*dev), kobj);

	if (a == &dev_attr_numa_node.attr &&
			dev_to_node(dev) == NUMA_NO_NODE)
		return 0;

	return a->mode;
}

static const struct attribute_group platform_dev_group = {
	.attrs = platform_dev_attrs,
	.is_visible = platform_dev_attrs_visible,
};
__ATTRIBUTE_GROUPS(platform_dev);


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
static int platform_match(struct device *dev, const struct device_driver *drv)
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

static int platform_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct platform_device *pdev = to_platform_device(dev);
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

static int platform_probe(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);
	int ret;

	/*
	 * A driver registered using platform_driver_probe() cannot be bound
	 * again later because the probe function usually lives in __init code
	 * and so is gone. For these drivers .probe is set to
	 * platform_probe_fail in __platform_driver_probe(). Don't even prepare
	 * clocks and PM domains for these to match the traditional behaviour.
	 */
	if (unlikely(drv->probe == platform_probe_fail))
		return -ENXIO;

	ret = of_clk_set_defaults(_dev->of_node, false);
	if (ret < 0)
		return ret;

	ret = dev_pm_domain_attach(_dev, true);
	if (ret)
		goto out;

	if (drv->probe) {
		ret = drv->probe(dev);
		if (ret)
			dev_pm_domain_detach(_dev, true);
	}

out:
	if (drv->prevent_deferred_probe && ret == -EPROBE_DEFER) {
		dev_warn(_dev, "probe deferral not supported\n");
		ret = -ENXIO;
	}

	return ret;
}

static void platform_remove(struct device *_dev)
{
	struct platform_driver *drv = to_platform_driver(_dev->driver);
	struct platform_device *dev = to_platform_device(_dev);

	if (drv->remove)
		drv->remove(dev);
	dev_pm_domain_detach(_dev, true);
}

static void platform_shutdown(struct device *_dev)
{
	struct platform_device *dev = to_platform_device(_dev);
	struct platform_driver *drv;

	if (!_dev->driver)
		return;

	drv = to_platform_driver(_dev->driver);
	if (drv->shutdown)
		drv->shutdown(dev);
}

static int platform_dma_configure(struct device *dev)
{
	struct platform_driver *drv = to_platform_driver(dev->driver);
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	enum dev_dma_attr attr;
	int ret = 0;

	if (is_of_node(fwnode)) {
		ret = of_dma_configure(dev, to_of_node(fwnode), true);
	} else if (is_acpi_device_node(fwnode)) {
		attr = acpi_get_dma_attr(to_acpi_device_node(fwnode));
		ret = acpi_dma_configure(dev, attr);
	}
	if (ret || drv->driver_managed_dma)
		return ret;

	ret = iommu_device_use_default_domain(dev);
	if (ret)
		arch_teardown_dma_ops(dev);

	return ret;
}

static void platform_dma_cleanup(struct device *dev)
{
	struct platform_driver *drv = to_platform_driver(dev->driver);

	if (!drv->driver_managed_dma)
		iommu_device_unuse_default_domain(dev);
}

static const struct dev_pm_ops platform_dev_pm_ops = {
	SET_RUNTIME_PM_OPS(pm_generic_runtime_suspend, pm_generic_runtime_resume, NULL)
	USE_PLATFORM_PM_SLEEP_OPS
};

const struct bus_type platform_bus_type = {
	.name		= "platform",
	.dev_groups	= platform_dev_groups,
	.match		= platform_match,
	.uevent		= platform_uevent,
	.probe		= platform_probe,
	.remove		= platform_remove,
	.shutdown	= platform_shutdown,
	.dma_configure	= platform_dma_configure,
	.dma_cleanup	= platform_dma_cleanup,
	.pm		= &platform_dev_pm_ops,
};
EXPORT_SYMBOL_GPL(platform_bus_type);

static inline int __platform_match(struct device *dev, const void *drv)
{
	return platform_match(dev, (struct device_driver *)drv);
}

/**
 * platform_find_device_by_driver - Find a platform device with a given
 * driver.
 * @start: The device to start the search from.
 * @drv: The device driver to look for.
 */
struct device *platform_find_device_by_driver(struct device *start,
					      const struct device_driver *drv)
{
	return bus_find_device(&platform_bus_type, start, drv,
			       __platform_match);
}
EXPORT_SYMBOL_GPL(platform_find_device_by_driver);

void __weak __init early_platform_cleanup(void) { }

int __init platform_bus_init(void)
{
	int error;

	early_platform_cleanup();

	error = device_register(&platform_bus);
	if (error) {
		put_device(&platform_bus);
		return error;
	}
	error =  bus_register(&platform_bus_type);
	if (error)
		device_unregister(&platform_bus);

	return error;
}
