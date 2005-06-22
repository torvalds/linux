/*
 * IBM PowerPC Virtual I/O Infrastructure Support.
 *
 *    Copyright (c) 2003 IBM Corp.
 *     Dave Engebretsen engebret@us.ibm.com
 *     Santiago Leon santil@us.ibm.com
 *     Hollis Blanchard <hollisb@us.ibm.com>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/init.h>
#include <linux/console.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/kobject.h>
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <asm/rtas.h>
#include <asm/iommu.h>
#include <asm/dma.h>
#include <asm/ppcdebug.h>
#include <asm/vio.h>
#include <asm/hvcall.h>
#include <asm/iSeries/vio.h>
#include <asm/iSeries/HvTypes.h>
#include <asm/iSeries/HvCallXm.h>
#include <asm/iSeries/HvLpConfig.h>

#define DBGENTER() pr_debug("%s entered\n", __FUNCTION__)

extern struct subsystem devices_subsys; /* needed for vio_find_name() */

static const struct vio_device_id *vio_match_device(
		const struct vio_device_id *, const struct vio_dev *);

#ifdef CONFIG_PPC_PSERIES
static struct iommu_table *vio_build_iommu_table(struct vio_dev *);
static int vio_num_address_cells;
#endif
#ifdef CONFIG_PPC_ISERIES
static struct iommu_table veth_iommu_table;
static struct iommu_table vio_iommu_table;
#endif
static struct vio_dev vio_bus_device  = { /* fake "parent" device */
	.name = vio_bus_device.dev.bus_id,
	.type = "",
#ifdef CONFIG_PPC_ISERIES
	.iommu_table = &vio_iommu_table,
#endif
	.dev.bus_id = "vio",
	.dev.bus = &vio_bus_type,
};

#ifdef CONFIG_PPC_ISERIES
static struct vio_dev *__init vio_register_device_iseries(char *type,
		uint32_t unit_num);

struct device *iSeries_vio_dev = &vio_bus_device.dev;
EXPORT_SYMBOL(iSeries_vio_dev);

#define device_is_compatible(a, b)	1

#endif

/* convert from struct device to struct vio_dev and pass to driver.
 * dev->driver has already been set by generic code because vio_bus_match
 * succeeded. */
static int vio_bus_probe(struct device *dev)
{
	struct vio_dev *viodev = to_vio_dev(dev);
	struct vio_driver *viodrv = to_vio_driver(dev->driver);
	const struct vio_device_id *id;
	int error = -ENODEV;

	DBGENTER();

	if (!viodrv->probe)
		return error;

	id = vio_match_device(viodrv->id_table, viodev);
	if (id) {
		error = viodrv->probe(viodev, id);
	}

	return error;
}

/* convert from struct device to struct vio_dev and pass to driver. */
static int vio_bus_remove(struct device *dev)
{
	struct vio_dev *viodev = to_vio_dev(dev);
	struct vio_driver *viodrv = to_vio_driver(dev->driver);

	DBGENTER();

	if (viodrv->remove) {
		return viodrv->remove(viodev);
	}

	/* driver can't remove */
	return 1;
}

/**
 * vio_register_driver: - Register a new vio driver
 * @drv:	The vio_driver structure to be registered.
 */
int vio_register_driver(struct vio_driver *viodrv)
{
	printk(KERN_DEBUG "%s: driver %s registering\n", __FUNCTION__,
		viodrv->name);

	/* fill in 'struct driver' fields */
	viodrv->driver.name = viodrv->name;
	viodrv->driver.bus = &vio_bus_type;
	viodrv->driver.probe = vio_bus_probe;
	viodrv->driver.remove = vio_bus_remove;

	return driver_register(&viodrv->driver);
}
EXPORT_SYMBOL(vio_register_driver);

/**
 * vio_unregister_driver - Remove registration of vio driver.
 * @driver:	The vio_driver struct to be removed form registration
 */
void vio_unregister_driver(struct vio_driver *viodrv)
{
	driver_unregister(&viodrv->driver);
}
EXPORT_SYMBOL(vio_unregister_driver);

/**
 * vio_match_device: - Tell if a VIO device has a matching VIO device id structure.
 * @ids: 	array of VIO device id structures to search in
 * @dev: 	the VIO device structure to match against
 *
 * Used by a driver to check whether a VIO device present in the
 * system is in its list of supported devices. Returns the matching
 * vio_device_id structure or NULL if there is no match.
 */
static const struct vio_device_id * vio_match_device(const struct vio_device_id *ids,
	const struct vio_dev *dev)
{
	DBGENTER();

	while (ids->type) {
		if ((strncmp(dev->type, ids->type, strlen(ids->type)) == 0) &&
			device_is_compatible(dev->dev.platform_data, ids->compat))
			return ids;
		ids++;
	}
	return NULL;
}

#ifdef CONFIG_PPC_ISERIES
void __init iommu_vio_init(void)
{
	struct iommu_table *t;
	struct iommu_table_cb cb;
	unsigned long cbp;
	unsigned long itc_entries;

	cb.itc_busno = 255;    /* Bus 255 is the virtual bus */
	cb.itc_virtbus = 0xff; /* Ask for virtual bus */

	cbp = virt_to_abs(&cb);
	HvCallXm_getTceTableParms(cbp);

	itc_entries = cb.itc_size * PAGE_SIZE / sizeof(union tce_entry);
	veth_iommu_table.it_size        = itc_entries / 2;
	veth_iommu_table.it_busno       = cb.itc_busno;
	veth_iommu_table.it_offset      = cb.itc_offset;
	veth_iommu_table.it_index       = cb.itc_index;
	veth_iommu_table.it_type        = TCE_VB;
	veth_iommu_table.it_blocksize	= 1;

	t = iommu_init_table(&veth_iommu_table);

	if (!t)
		printk("Virtual Bus VETH TCE table failed.\n");

	vio_iommu_table.it_size         = itc_entries - veth_iommu_table.it_size;
	vio_iommu_table.it_busno        = cb.itc_busno;
	vio_iommu_table.it_offset       = cb.itc_offset +
					  veth_iommu_table.it_size;
	vio_iommu_table.it_index        = cb.itc_index;
	vio_iommu_table.it_type         = TCE_VB;
	vio_iommu_table.it_blocksize	= 1;

	t = iommu_init_table(&vio_iommu_table);

	if (!t)
		printk("Virtual Bus VIO TCE table failed.\n");
}
#endif

#ifdef CONFIG_PPC_PSERIES
static void probe_bus_pseries(void)
{
	struct device_node *node_vroot, *of_node;

	node_vroot = find_devices("vdevice");
	if ((node_vroot == NULL) || (node_vroot->child == NULL))
		/* this machine doesn't do virtual IO, and that's ok */
		return;

	vio_num_address_cells = prom_n_addr_cells(node_vroot->child);

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
#endif

#ifdef CONFIG_PPC_ISERIES
static void probe_bus_iseries(void)
{
	HvLpIndexMap vlan_map = HvLpConfig_getVirtualLanIndexMap();
	struct vio_dev *viodev;
	int i;

	/* there is only one of each of these */
	vio_register_device_iseries("viocons", 0);
	vio_register_device_iseries("vscsi", 0);

	vlan_map = HvLpConfig_getVirtualLanIndexMap();
	for (i = 0; i < HVMAXARCHITECTEDVIRTUALLANS; i++) {
		if ((vlan_map & (0x8000 >> i)) == 0)
			continue;
		viodev = vio_register_device_iseries("vlan", i);
		/* veth is special and has it own iommu_table */
		viodev->iommu_table = &veth_iommu_table;
	}
	for (i = 0; i < HVMAXARCHITECTEDVIRTUALDISKS; i++)
		vio_register_device_iseries("viodasd", i);
	for (i = 0; i < HVMAXARCHITECTEDVIRTUALCDROMS; i++)
		vio_register_device_iseries("viocd", i);
	for (i = 0; i < HVMAXARCHITECTEDVIRTUALTAPES; i++)
		vio_register_device_iseries("viotape", i);
}
#endif

/**
 * vio_bus_init: - Initialize the virtual IO bus
 */
static int __init vio_bus_init(void)
{
	int err;

	err = bus_register(&vio_bus_type);
	if (err) {
		printk(KERN_ERR "failed to register VIO bus\n");
		return err;
	}

	/* the fake parent of all vio devices, just to give us a nice directory */
	err = device_register(&vio_bus_device.dev);
	if (err) {
		printk(KERN_WARNING "%s: device_register returned %i\n", __FUNCTION__,
			err);
		return err;
	}

#ifdef CONFIG_PPC_PSERIES
	probe_bus_pseries();
#endif
#ifdef CONFIG_PPC_ISERIES
	probe_bus_iseries();
#endif

	return 0;
}

__initcall(vio_bus_init);

/* vio_dev refcount hit 0 */
static void __devinit vio_dev_release(struct device *dev)
{
	DBGENTER();

#ifdef CONFIG_PPC_PSERIES
	/* XXX free TCE table */
	of_node_put(dev->platform_data);
#endif
	kfree(to_vio_dev(dev));
}

#ifdef CONFIG_PPC_PSERIES
static ssize_t viodev_show_devspec(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct device_node *of_node = dev->platform_data;

	return sprintf(buf, "%s\n", of_node->full_name);
}
DEVICE_ATTR(devspec, S_IRUSR | S_IRGRP | S_IROTH, viodev_show_devspec, NULL);
#endif

static ssize_t viodev_show_name(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", to_vio_dev(dev)->name);
}
DEVICE_ATTR(name, S_IRUSR | S_IRGRP | S_IROTH, viodev_show_name, NULL);

static struct vio_dev * __devinit vio_register_device_common(
		struct vio_dev *viodev, char *name, char *type,
		uint32_t unit_address, struct iommu_table *iommu_table)
{
	DBGENTER();

	viodev->name = name;
	viodev->type = type;
	viodev->unit_address = unit_address;
	viodev->iommu_table = iommu_table;
	/* init generic 'struct device' fields: */
	viodev->dev.parent = &vio_bus_device.dev;
	viodev->dev.bus = &vio_bus_type;
	viodev->dev.release = vio_dev_release;

	/* register with generic device framework */
	if (device_register(&viodev->dev)) {
		printk(KERN_ERR "%s: failed to register device %s\n",
				__FUNCTION__, viodev->dev.bus_id);
		return NULL;
	}
	device_create_file(&viodev->dev, &dev_attr_name);

	return viodev;
}

#ifdef CONFIG_PPC_PSERIES
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

	DBGENTER();

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

	/* register with generic device framework */
	if (vio_register_device_common(viodev, of_node->name, of_node->type,
				*unit_address, vio_build_iommu_table(viodev))
			== NULL) {
		/* XXX free TCE table */
		kfree(viodev);
		return NULL;
	}
	device_create_file(&viodev->dev, &dev_attr_devspec);

	return viodev;
}
EXPORT_SYMBOL(vio_register_device_node);
#endif

#ifdef CONFIG_PPC_ISERIES
/**
 * vio_register_device: - Register a new vio device.
 * @voidev:	The device to register.
 */
static struct vio_dev *__init vio_register_device_iseries(char *type,
		uint32_t unit_num)
{
	struct vio_dev *viodev;

	DBGENTER();

	/* allocate a vio_dev for this node */
	viodev = kmalloc(sizeof(struct vio_dev), GFP_KERNEL);
	if (!viodev)
		return NULL;
	memset(viodev, 0, sizeof(struct vio_dev));

	snprintf(viodev->dev.bus_id, BUS_ID_SIZE, "%s%d", type, unit_num);

	return vio_register_device_common(viodev, viodev->dev.bus_id, type,
			unit_num, &vio_iommu_table);
}
#endif

void __devinit vio_unregister_device(struct vio_dev *viodev)
{
	DBGENTER();
#ifdef CONFIG_PPC_PSERIES
	device_remove_file(&viodev->dev, &dev_attr_devspec);
#endif
	device_remove_file(&viodev->dev, &dev_attr_name);
	device_unregister(&viodev->dev);
}
EXPORT_SYMBOL(vio_unregister_device);

#ifdef CONFIG_PPC_PSERIES
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

/**
 * vio_build_iommu_table: - gets the dma information from OF and builds the TCE tree.
 * @dev: the virtual device.
 *
 * Returns a pointer to the built tce tree, or NULL if it can't
 * find property.
*/
static struct iommu_table * vio_build_iommu_table(struct vio_dev *dev)
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

int vio_enable_interrupts(struct vio_dev *dev)
{
	int rc = h_vio_signal(dev->unit_address, VIO_IRQ_ENABLE);
	if (rc != H_Success) {
		printk(KERN_ERR "vio: Error 0x%x enabling interrupts\n", rc);
	}
	return rc;
}
EXPORT_SYMBOL(vio_enable_interrupts);

int vio_disable_interrupts(struct vio_dev *dev)
{
	int rc = h_vio_signal(dev->unit_address, VIO_IRQ_DISABLE);
	if (rc != H_Success) {
		printk(KERN_ERR "vio: Error 0x%x disabling interrupts\n", rc);
	}
	return rc;
}
EXPORT_SYMBOL(vio_disable_interrupts);
#endif

static dma_addr_t vio_map_single(struct device *dev, void *vaddr,
			  size_t size, enum dma_data_direction direction)
{
	return iommu_map_single(to_vio_dev(dev)->iommu_table, vaddr, size,
			direction);
}

static void vio_unmap_single(struct device *dev, dma_addr_t dma_handle,
		      size_t size, enum dma_data_direction direction)
{
	iommu_unmap_single(to_vio_dev(dev)->iommu_table, dma_handle, size,
			direction);
}

static int vio_map_sg(struct device *dev, struct scatterlist *sglist,
		int nelems, enum dma_data_direction direction)
{
	return iommu_map_sg(dev, to_vio_dev(dev)->iommu_table, sglist,
			nelems, direction);
}

static void vio_unmap_sg(struct device *dev, struct scatterlist *sglist,
		int nelems, enum dma_data_direction direction)
{
	iommu_unmap_sg(to_vio_dev(dev)->iommu_table, sglist, nelems, direction);
}

static void *vio_alloc_coherent(struct device *dev, size_t size,
			   dma_addr_t *dma_handle, unsigned int __nocast flag)
{
	return iommu_alloc_coherent(to_vio_dev(dev)->iommu_table, size,
			dma_handle, flag);
}

static void vio_free_coherent(struct device *dev, size_t size,
			 void *vaddr, dma_addr_t dma_handle)
{
	iommu_free_coherent(to_vio_dev(dev)->iommu_table, size, vaddr,
			dma_handle);
}

static int vio_dma_supported(struct device *dev, u64 mask)
{
	return 1;
}

struct dma_mapping_ops vio_dma_ops = {
	.alloc_coherent = vio_alloc_coherent,
	.free_coherent = vio_free_coherent,
	.map_single = vio_map_single,
	.unmap_single = vio_unmap_single,
	.map_sg = vio_map_sg,
	.unmap_sg = vio_unmap_sg,
	.dma_supported = vio_dma_supported,
};

static int vio_bus_match(struct device *dev, struct device_driver *drv)
{
	const struct vio_dev *vio_dev = to_vio_dev(dev);
	struct vio_driver *vio_drv = to_vio_driver(drv);
	const struct vio_device_id *ids = vio_drv->id_table;
	const struct vio_device_id *found_id;

	DBGENTER();

	if (!ids)
		return 0;

	found_id = vio_match_device(ids, vio_dev);
	if (found_id)
		return 1;

	return 0;
}

struct bus_type vio_bus_type = {
	.name = "vio",
	.match = vio_bus_match,
};
