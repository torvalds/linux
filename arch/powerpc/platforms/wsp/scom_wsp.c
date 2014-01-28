/*
 *  SCOM backend for WSP
 *
 *  Copyright 2010 Benjamin Herrenschmidt, IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/cpumask.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/of_address.h>

#include <asm/cputhreads.h>
#include <asm/reg_a2.h>
#include <asm/scom.h>
#include <asm/udbg.h>

#include "wsp.h"


static scom_map_t wsp_scom_map(struct device_node *dev, u64 reg, u64 count)
{
	struct resource r;
	u64 xscom_addr;

	if (!of_get_property(dev, "scom-controller", NULL)) {
		pr_err("%s: device %s is not a SCOM controller\n",
			__func__, dev->full_name);
		return SCOM_MAP_INVALID;
	}

	if (of_address_to_resource(dev, 0, &r)) {
		pr_debug("Failed to find SCOM controller address\n");
		return 0;
	}

	/* Transform the SCOM address into an XSCOM offset */
	xscom_addr = ((reg & 0x7f000000) >> 1) | ((reg & 0xfffff) << 3);

	return (scom_map_t)ioremap(r.start + xscom_addr, count << 3);
}

static void wsp_scom_unmap(scom_map_t map)
{
	iounmap((void *)map);
}

static int wsp_scom_read(scom_map_t map, u64 reg, u64 *value)
{
	u64 __iomem *addr = (u64 __iomem *)map;

	*value = in_be64(addr + reg);

	return 0;
}

static int wsp_scom_write(scom_map_t map, u64 reg, u64 value)
{
	u64 __iomem *addr = (u64 __iomem *)map;

	out_be64(addr + reg, value);

	return 0;
}

static const struct scom_controller wsp_scom_controller = {
	.map	= wsp_scom_map,
	.unmap	= wsp_scom_unmap,
	.read	= wsp_scom_read,
	.write	= wsp_scom_write
};

void scom_init_wsp(void)
{
	scom_init(&wsp_scom_controller);
}
