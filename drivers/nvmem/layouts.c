// SPDX-License-Identifier: GPL-2.0
/*
 * NVMEM layout bus handling
 *
 * Copyright (C) 2023 Bootlin
 * Author: Miquel Raynal <miquel.raynal@bootlin.com
 */

#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>

#include "internals.h"

#define to_nvmem_layout_driver(drv) \
	(container_of_const((drv), struct nvmem_layout_driver, driver))
#define to_nvmem_layout_device(_dev) \
	container_of((_dev), struct nvmem_layout, dev)

static int nvmem_layout_bus_match(struct device *dev, const struct device_driver *drv)
{
	return of_driver_match_device(dev, drv);
}

static int nvmem_layout_bus_probe(struct device *dev)
{
	struct nvmem_layout_driver *drv = to_nvmem_layout_driver(dev->driver);
	struct nvmem_layout *layout = to_nvmem_layout_device(dev);

	if (!drv->probe || !drv->remove)
		return -EINVAL;

	return drv->probe(layout);
}

static void nvmem_layout_bus_remove(struct device *dev)
{
	struct nvmem_layout_driver *drv = to_nvmem_layout_driver(dev->driver);
	struct nvmem_layout *layout = to_nvmem_layout_device(dev);

	return drv->remove(layout);
}

static int nvmem_layout_bus_uevent(const struct device *dev,
				   struct kobj_uevent_env *env)
{
	int ret;

	ret = of_device_uevent_modalias(dev, env);
	if (ret != ENODEV)
		return ret;

	return 0;
}

static const struct bus_type nvmem_layout_bus_type = {
	.name		= "nvmem-layout",
	.match		= nvmem_layout_bus_match,
	.probe		= nvmem_layout_bus_probe,
	.remove		= nvmem_layout_bus_remove,
	.uevent		= nvmem_layout_bus_uevent,
};

int __nvmem_layout_driver_register(struct nvmem_layout_driver *drv,
				   struct module *owner)
{
	drv->driver.bus = &nvmem_layout_bus_type;
	drv->driver.owner = owner;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__nvmem_layout_driver_register);

void nvmem_layout_driver_unregister(struct nvmem_layout_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(nvmem_layout_driver_unregister);

static void nvmem_layout_release_device(struct device *dev)
{
	struct nvmem_layout *layout = to_nvmem_layout_device(dev);

	of_node_put(layout->dev.of_node);
	kfree(layout);
}

static int nvmem_layout_create_device(struct nvmem_device *nvmem,
				      struct device_node *np)
{
	struct nvmem_layout *layout;
	struct device *dev;
	int ret;

	layout = kzalloc(sizeof(*layout), GFP_KERNEL);
	if (!layout)
		return -ENOMEM;

	/* Create a bidirectional link */
	layout->nvmem = nvmem;
	nvmem->layout = layout;

	/* Device model registration */
	dev = &layout->dev;
	device_initialize(dev);
	dev->parent = &nvmem->dev;
	dev->bus = &nvmem_layout_bus_type;
	dev->release = nvmem_layout_release_device;
	dev->coherent_dma_mask = DMA_BIT_MASK(32);
	dev->dma_mask = &dev->coherent_dma_mask;
	device_set_node(dev, of_fwnode_handle(of_node_get(np)));
	of_device_make_bus_id(dev);
	of_msi_configure(dev, dev->of_node);

	ret = device_add(dev);
	if (ret) {
		put_device(dev);
		return ret;
	}

	return 0;
}

static const struct of_device_id of_nvmem_layout_skip_table[] = {
	{ .compatible = "fixed-layout", },
	{}
};

static int nvmem_layout_bus_populate(struct nvmem_device *nvmem,
				     struct device_node *layout_dn)
{
	int ret;

	/* Make sure it has a compatible property */
	if (!of_property_present(layout_dn, "compatible")) {
		pr_debug("%s() - skipping %pOF, no compatible prop\n",
			 __func__, layout_dn);
		return 0;
	}

	/* Fixed layouts are parsed manually somewhere else for now */
	if (of_match_node(of_nvmem_layout_skip_table, layout_dn)) {
		pr_debug("%s() - skipping %pOF node\n", __func__, layout_dn);
		return 0;
	}

	if (of_node_check_flag(layout_dn, OF_POPULATED_BUS)) {
		pr_debug("%s() - skipping %pOF, already populated\n",
			 __func__, layout_dn);

		return 0;
	}

	/* NVMEM layout buses expect only a single device representing the layout */
	ret = nvmem_layout_create_device(nvmem, layout_dn);
	if (ret)
		return ret;

	of_node_set_flag(layout_dn, OF_POPULATED_BUS);

	return 0;
}

struct device_node *of_nvmem_layout_get_container(struct nvmem_device *nvmem)
{
	return of_get_child_by_name(nvmem->dev.of_node, "nvmem-layout");
}
EXPORT_SYMBOL_GPL(of_nvmem_layout_get_container);

/*
 * Returns the number of devices populated, 0 if the operation was not relevant
 * for this nvmem device, an error code otherwise.
 */
int nvmem_populate_layout(struct nvmem_device *nvmem)
{
	struct device_node *layout_dn;
	int ret;

	layout_dn = of_nvmem_layout_get_container(nvmem);
	if (!layout_dn)
		return 0;

	/* Populate the layout device */
	device_links_supplier_sync_state_pause();
	ret = nvmem_layout_bus_populate(nvmem, layout_dn);
	device_links_supplier_sync_state_resume();

	of_node_put(layout_dn);
	return ret;
}

void nvmem_destroy_layout(struct nvmem_device *nvmem)
{
	struct device *dev;

	if (!nvmem->layout)
		return;

	dev = &nvmem->layout->dev;
	of_node_clear_flag(dev->of_node, OF_POPULATED_BUS);
	device_unregister(dev);
}

int nvmem_layout_bus_register(void)
{
	return bus_register(&nvmem_layout_bus_type);
}

void nvmem_layout_bus_unregister(void)
{
	bus_unregister(&nvmem_layout_bus_type);
}
