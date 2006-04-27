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

static struct vio_bus_ops vio_bus_ops_pseries = {
	.build_iommu_table = vio_build_iommu_table,
};

/**
 * vio_bus_init_pseries: - Initialize the pSeries virtual IO bus
 */
static int __init vio_bus_init_pseries(void)
{
	return vio_bus_init(&vio_bus_ops_pseries);
}

__initcall(vio_bus_init_pseries);

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
	if (rc != H_SUCCESS)
		printk(KERN_ERR "vio: Error 0x%x enabling interrupts\n", rc);
	return rc;
}
EXPORT_SYMBOL(vio_enable_interrupts);

int vio_disable_interrupts(struct vio_dev *dev)
{
	int rc = h_vio_signal(dev->unit_address, VIO_IRQ_DISABLE);
	if (rc != H_SUCCESS)
		printk(KERN_ERR "vio: Error 0x%x disabling interrupts\n", rc);
	return rc;
}
EXPORT_SYMBOL(vio_disable_interrupts);
