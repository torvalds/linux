/*
 * Support for IOMMU on Celleb platform.
 *
 * (C) Copyright 2006-2007 TOSHIBA CORPORATION
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/of_platform.h>

#include <asm/machdep.h>

#include "beat_wrapper.h"

#define DMA_FLAGS 0xf800000000000000UL	/* r/w permitted, coherency required,
					   strongest order */

static int __init find_dma_window(u64 *io_space_id, u64 *ioid,
				  u64 *base, u64 *size, u64 *io_page_size)
{
	struct device_node *dn;
	const unsigned long *dma_window;

	for_each_node_by_type(dn, "ioif") {
		dma_window = of_get_property(dn, "toshiba,dma-window", NULL);
		if (dma_window) {
			*io_space_id = (dma_window[0] >> 32) & 0xffffffffUL;
			*ioid = dma_window[0] & 0x7ffUL;
			*base = dma_window[1];
			*size = dma_window[2];
			*io_page_size = 1 << dma_window[3];
			of_node_put(dn);
			return 1;
		}
	}
	return 0;
}

static unsigned long celleb_dma_direct_offset;

static void __init celleb_init_direct_mapping(void)
{
	u64 lpar_addr, io_addr;
	u64 io_space_id, ioid, dma_base, dma_size, io_page_size;

	if (!find_dma_window(&io_space_id, &ioid, &dma_base, &dma_size,
			     &io_page_size)) {
		pr_info("No dma window found !\n");
		return;
	}

	for (lpar_addr = 0; lpar_addr < dma_size; lpar_addr += io_page_size) {
		io_addr = lpar_addr + dma_base;
		(void)beat_put_iopte(io_space_id, io_addr, lpar_addr,
				     ioid, DMA_FLAGS);
	}

	celleb_dma_direct_offset = dma_base;
}

static void celleb_dma_dev_setup(struct device *dev)
{
	dev->archdata.dma_ops = get_pci_dma_ops();
	set_dma_offset(dev, celleb_dma_direct_offset);
}

static void celleb_pci_dma_dev_setup(struct pci_dev *pdev)
{
	celleb_dma_dev_setup(&pdev->dev);
}

static int celleb_of_bus_notify(struct notifier_block *nb,
				unsigned long action, void *data)
{
	struct device *dev = data;

	/* We are only intereted in device addition */
	if (action != BUS_NOTIFY_ADD_DEVICE)
		return 0;

	celleb_dma_dev_setup(dev);

	return 0;
}

static struct notifier_block celleb_of_bus_notifier = {
	.notifier_call = celleb_of_bus_notify
};

static int __init celleb_init_iommu(void)
{
	celleb_init_direct_mapping();
	set_pci_dma_ops(&dma_direct_ops);
	ppc_md.pci_dma_dev_setup = celleb_pci_dma_dev_setup;
	bus_register_notifier(&of_platform_bus_type, &celleb_of_bus_notifier);

	return 0;
}

machine_arch_initcall(celleb_beat, celleb_init_iommu);
