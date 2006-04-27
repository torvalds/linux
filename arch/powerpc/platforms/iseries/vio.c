/*
 * IBM PowerPC iSeries Virtual I/O Infrastructure Support.
 *
 *    Copyright (c) 2005 Stephen Rothwell, IBM Corp.
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/types.h>
#include <linux/device.h>
#include <linux/init.h>

#include <asm/vio.h>
#include <asm/iommu.h>
#include <asm/tce.h>
#include <asm/abs_addr.h>
#include <asm/page.h>
#include <asm/iseries/vio.h>
#include <asm/iseries/hv_types.h>
#include <asm/iseries/hv_lp_config.h>
#include <asm/iseries/hv_call_xm.h>

#include "iommu.h"

struct device *iSeries_vio_dev = &vio_bus_device.dev;
EXPORT_SYMBOL(iSeries_vio_dev);

static struct iommu_table veth_iommu_table;
static struct iommu_table vio_iommu_table;

static void __init iommu_vio_init(void)
{
	iommu_table_getparms_iSeries(255, 0, 0xff, &veth_iommu_table);
	veth_iommu_table.it_size /= 2;
	vio_iommu_table = veth_iommu_table;
	vio_iommu_table.it_offset += veth_iommu_table.it_size;

	if (!iommu_init_table(&veth_iommu_table))
		printk("Virtual Bus VETH TCE table failed.\n");
	if (!iommu_init_table(&vio_iommu_table))
		printk("Virtual Bus VIO TCE table failed.\n");
}

static struct iommu_table *vio_build_iommu_table(struct vio_dev *dev)
{
	if (strcmp(dev->type, "vlan") == 0)
		return &veth_iommu_table;
	return &vio_iommu_table;
}

/**
 * vio_match_device_iseries: - Tell if a iSeries VIO device matches a
 *	vio_device_id
 */
static int vio_match_device_iseries(const struct vio_device_id *id,
		const struct vio_dev *dev)
{
	return strncmp(dev->type, id->type, strlen(id->type)) == 0;
}

static struct vio_bus_ops vio_bus_ops_iseries = {
	.match = vio_match_device_iseries,
	.build_iommu_table = vio_build_iommu_table,
};

/**
 * vio_bus_init_iseries: - Initialize the iSeries virtual IO bus
 */
static int __init vio_bus_init_iseries(void)
{
	iommu_vio_init();
	vio_bus_device.iommu_table = &vio_iommu_table;
	iSeries_vio_dev = &vio_bus_device.dev;
	return vio_bus_init(&vio_bus_ops_iseries);
}

__initcall(vio_bus_init_iseries);
