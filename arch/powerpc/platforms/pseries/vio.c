/*
 * IBM PowerPC pSeries Virtual I/O Infrastructure Support.
 *
 *    Copyright (c) 2003-2005 IBM Corp.
 *     Dave Engebretsen engebret@us.ibm.com
 *     Santiago Leon santil@us.ibm.com
 *     Hollis Blanchard <hollisb@us.ibm.com>
 *     Stephen Rothwell
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/kobject.h>
#include <asm/iommu.h>
#include <asm/dma.h>
#include <asm/prom.h>
#include <asm/vio.h>
#include <asm/hvcall.h>
#include <asm/tce.h>

extern struct subsystem devices_subsys; /* needed for vio_find_name() */

static void probe_bus_pseries(void)
{
	struct device_node *node_vroot, *of_node;

	node_vroot = find_devices("vdevice");
	if ((node_vroot == NULL) || (node_vroot->child == NULL))
		/* this machine doesn't do virtual IO, and that's ok */
		return;

	/*
	 * Create struct vio_devices for each virtual device in the device tree.
	 * Drivers will associate with them later.
	 */
	for (of_node = node_vroot->child; of_node != NULL;
			of_node = of_node->sibling) {
		printk(KERN_DEBUG "%s: processing %p\n", __FUNCTION__, of_node);
		vio_register_device_node(of_node);
	}
}

/**
 * vio_match_device_pseries: - Tell if a pSeries VIO device matches a
 *	vio_device_id
 */
static int vio_match_device_pseries(const struct vio_device_id *id,
		const struct vio_dev *dev)
{
	return (strncmp(dev->type, id->type, strlen(id->type)) == 0) &&
			device_is_compatible(dev->dev.platform_data, id->compat);
}

static void vio_release_device_pseries(struct device *dev)
{
	/* XXX free TCE table */
	of_node_put(dev->platform_data);
}

static ssize_t viodev_show_devspec(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct device_node *of_node = dev->platform_data;

	return sprintf(buf, "%s\n", of_node->full_name);
}
DEVICE_ATTR(devspec, S_IRUSR | S_IRGRP | S_IROTH, viodev_show_devspec, NULL);

static void vio_unregister_device_pseries(struct vio_dev *viodev)
{
	device_remove_file(&viodev->dev, &dev_attr_devspec);
}

static struct vio_bus_ops vio_bus_ops_pseries = {
	.match = vio_match_device_pseries,
	.unregister_device = vio_unregister_device_pseries,
	.release_device = vio_release_device_pseries,
};

/**
 * vio_bus_init_pseries: - Initialize the pSeries virtual IO bus
 */
static int __init vio_bus_init_pseries(void)
{
	int err;

	err = vio_bus_init(&vio_bus_ops_pseries);
	if (err == 0)
		probe_bus_pseries();
	return err;
}

__initcall(vio_bus_init_pseries);

/**
 * vio_build_iommu_table: - gets the dma information from OF and
 *	builds the TCE tree.
 * @dev: the virtual device.
 *
 * Returns a pointer to the built tce tree, or NULL if it can't
 * find property.
*/
static struct iommu_table *vio_build_iommu_table(struct vio_dev *dev)
{
	unsigned int *dma_window;
	struct iommu_table *newTceTable;
	unsigned long offset;
	int dma_window_property_size;

	dma_window = (unsigned int *) get_property(dev->dev.platform_data, "ibm,my-dma-window", &dma_window_property_size);
	if(!dma_window) {
		return NULL;
	}

	newTceTable = (struct iommu_table *) kmalloc(sizeof(struct iommu_table), GFP_KERNEL);

	/*  There should be some code to extract the phys-encoded offset
		using prom_n_addr_cells(). However, according to a comment
		on earlier versions, it's always zero, so we don't bother */
	offset = dma_window[1] >>  PAGE_SHIFT;

	/* TCE table size - measured in tce entries */
	newTceTable->it_size		= dma_window[4] >> PAGE_SHIFT;
	/* offset for VIO should always be 0 */
	newTceTable->it_offset		= offset;
	newTceTable->it_busno		= 0;
	newTceTable->it_index		= (unsigned long)dma_window[0];
	newTceTable->it_type		= TCE_VB;

	return iommu_init_table(newTceTable);
}

/**
 * vio_register_device_node: - Register a new vio device.
 * @of_node:	The OF node for this device.
 *
 * Creates and initializes a vio_dev structure from the data in
 * of_node (dev.platform_data) and adds it to the list of virtual devices.
 * Returns a pointer to the created vio_dev or NULL if node has
 * NULL device_type or compatible fields.
 */
struct vio_dev * __devinit vio_register_device_node(struct device_node *of_node)
{
	struct vio_dev *viodev;
	unsigned int *unit_address;
	unsigned int *irq_p;

	/* we need the 'device_type' property, in order to match with drivers */
	if ((NULL == of_node->type)) {
		printk(KERN_WARNING
			"%s: node %s missing 'device_type'\n", __FUNCTION__,
			of_node->name ? of_node->name : "<unknown>");
		return NULL;
	}

	unit_address = (unsigned int *)get_property(of_node, "reg", NULL);
	if (!unit_address) {
		printk(KERN_WARNING "%s: node %s missing 'reg'\n", __FUNCTION__,
			of_node->name ? of_node->name : "<unknown>");
		return NULL;
	}

	/* allocate a vio_dev for this node */
	viodev = kmalloc(sizeof(struct vio_dev), GFP_KERNEL);
	if (!viodev) {
		return NULL;
	}
	memset(viodev, 0, sizeof(struct vio_dev));

	viodev->dev.platform_data = of_node_get(of_node);

	viodev->irq = NO_IRQ;
	irq_p = (unsigned int *)get_property(of_node, "interrupts", NULL);
	if (irq_p) {
		int virq = virt_irq_create_mapping(*irq_p);
		if (virq == NO_IRQ) {
			printk(KERN_ERR "Unable to allocate interrupt "
			       "number for %s\n", of_node->full_name);
		} else
			viodev->irq = irq_offset_up(virq);
	}

	snprintf(viodev->dev.bus_id, BUS_ID_SIZE, "%x", *unit_address);
	viodev->name = of_node->name;
	viodev->type = of_node->type;
	viodev->unit_address = *unit_address;
	viodev->iommu_table = vio_build_iommu_table(viodev);

	/* register with generic device framework */
	if (vio_register_device(viodev) == NULL) {
		/* XXX free TCE table */
		kfree(viodev);
		return NULL;
	}
	device_create_file(&viodev->dev, &dev_attr_devspec);

	return viodev;
}
EXPORT_SYMBOL(vio_register_device_node);

/**
 * vio_get_attribute: - get attribute for virtual device
 * @vdev:	The vio device to get property.
 * @which:	The property/attribute to be extracted.
 * @length:	Pointer to length of returned data size (unused if NULL).
 *
 * Calls prom.c's get_property() to return the value of the
 * attribute specified by the preprocessor constant @which
*/
const void * vio_get_attribute(struct vio_dev *vdev, void* which, int* length)
{
	return get_property(vdev->dev.platform_data, (char*)which, length);
}
EXPORT_SYMBOL(vio_get_attribute);

/* vio_find_name() - internal because only vio.c knows how we formatted the
 * kobject name
 * XXX once vio_bus_type.devices is actually used as a kset in
 * drivers/base/bus.c, this function should be removed in favor of
 * "device_find(kobj_name, &vio_bus_type)"
 */
static struct vio_dev *vio_find_name(const char *kobj_name)
{
	struct kobject *found;

	found = kset_find_obj(&devices_subsys.kset, kobj_name);
	if (!found)
		return NULL;

	return to_vio_dev(container_of(found, struct device, kobj));
}

/**
 * vio_find_node - find an already-registered vio_dev
 * @vnode: device_node of the virtual device we're looking for
 */
struct vio_dev *vio_find_node(struct device_node *vnode)
{
	uint32_t *unit_address;
	char kobj_name[BUS_ID_SIZE];

	/* construct the kobject name from the device node */
	unit_address = (uint32_t *)get_property(vnode, "reg", NULL);
	if (!unit_address)
		return NULL;
	snprintf(kobj_name, BUS_ID_SIZE, "%x", *unit_address);

	return vio_find_name(kobj_name);
}
EXPORT_SYMBOL(vio_find_node);

int vio_enable_interrupts(struct vio_dev *dev)
{
	int rc = h_vio_signal(dev->unit_address, VIO_IRQ_ENABLE);
	if (rc != H_Success)
		printk(KERN_ERR "vio: Error 0x%x enabling interrupts\n", rc);
	return rc;
}
EXPORT_SYMBOL(vio_enable_interrupts);

int vio_disable_interrupts(struct vio_dev *dev)
{
	int rc = h_vio_signal(dev->unit_address, VIO_IRQ_DISABLE);
	if (rc != H_Success)
		printk(KERN_ERR "vio: Error 0x%x disabling interrupts\n", rc);
	return rc;
}
EXPORT_SYMBOL(vio_disable_interrupts);
