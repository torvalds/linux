/*
 * Compaq Hot Plug Controller Driver
 *
 * Copyright (c) 1995,2001 Compaq Computer Corporation
 * Copyright (c) 2001,2003 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (c) 2001 IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <greg@kroah.com>
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/workqueue.h>
#include <linux/pci.h>
#include "shpchp.h"


/* A few routines that create sysfs entries for the hot plug controller */

static ssize_t show_ctrl (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev;
	char * out = buf;
	int index, busnr;
	struct resource *res;
	struct pci_bus *bus;

	pdev = container_of (dev, struct pci_dev, dev);
	bus = pdev->subordinate;

	out += sprintf(buf, "Free resources: memory\n");
	for (index = 0; index < PCI_BUS_NUM_RESOURCES; index++) {
		res = bus->resource[index];
		if (res && (res->flags & IORESOURCE_MEM) &&
				!(res->flags & IORESOURCE_PREFETCH)) {
			out += sprintf(out, "start = %8.8lx, length = %8.8lx\n",
					res->start, (res->end - res->start));
		}
	}
	out += sprintf(out, "Free resources: prefetchable memory\n");
	for (index = 0; index < PCI_BUS_NUM_RESOURCES; index++) {
		res = bus->resource[index];
		if (res && (res->flags & IORESOURCE_MEM) &&
			       (res->flags & IORESOURCE_PREFETCH)) {
			out += sprintf(out, "start = %8.8lx, length = %8.8lx\n",
					res->start, (res->end - res->start));
		}
	}
	out += sprintf(out, "Free resources: IO\n");
	for (index = 0; index < PCI_BUS_NUM_RESOURCES; index++) {
		res = bus->resource[index];
		if (res && (res->flags & IORESOURCE_IO)) {
			out += sprintf(out, "start = %8.8lx, length = %8.8lx\n",
					res->start, (res->end - res->start));
		}
	}
	out += sprintf(out, "Free resources: bus numbers\n");
	for (busnr = bus->secondary; busnr <= bus->subordinate; busnr++) {
		if (!pci_find_bus(pci_domain_nr(bus), busnr))
			break;
	}
	if (busnr < bus->subordinate)
		out += sprintf(out, "start = %8.8x, length = %8.8x\n",
				busnr, (bus->subordinate - busnr));

	return out - buf;
}
static DEVICE_ATTR (ctrl, S_IRUGO, show_ctrl, NULL);

static ssize_t show_dev (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev, *fdev;
	struct controller *ctrl;
	char * out = buf;
	int index;
	struct resource *res;
	struct pci_func *new_slot;
	struct slot *slot;

	pdev = container_of (dev, struct pci_dev, dev);
	ctrl = pci_get_drvdata(pdev);

	slot=ctrl->slot;

	while (slot) {
		new_slot = shpchp_slot_find(slot->bus, slot->device, 0);
		if (!new_slot)
			break;
		fdev = new_slot->pci_dev;
		if (!fdev)
			break;
		out += sprintf(out, "assigned resources: memory\n");
		for (index=0; index <= PCI_NUM_RESOURCES; index++) {
			res = &(fdev->resource[index]);
			if (res && (res->flags & IORESOURCE_MEM) &&
					!(res->flags & IORESOURCE_PREFETCH)) {
				out += sprintf(out,
					"start = %8.8lx, length = %8.8lx\n",
					res->start, (res->end - res->start));
			}
		}
		out += sprintf(out, "assigned resources: prefetchable memory\n");
		for (index=0; index <= PCI_NUM_RESOURCES; index++) {
			res = &(fdev->resource[index]);
			if (res && (res->flags & (IORESOURCE_MEM |
						IORESOURCE_PREFETCH))) {
				out += sprintf(out,
					"start = %8.8lx, length = %8.8lx\n",
					res->start, (res->end - res->start));
			}
		}
		out += sprintf(out, "assigned resources: IO\n");
		for (index=0; index <= PCI_NUM_RESOURCES; index++) {
			res = &(fdev->resource[index]);
			if (res && (res->flags & IORESOURCE_IO)) {
				out += sprintf(out,
					"start = %8.8lx, length = %8.8lx\n",
					res->start, (res->end - res->start));
			}
		}
		out += sprintf(out, "assigned resources: bus numbers\n");
		if (fdev->subordinate)
			out += sprintf(out, "start = %8.8x, length = %8.8x\n",
				fdev->subordinate->secondary,
				(fdev->subordinate->subordinate -
				 fdev->subordinate->secondary));
		else
			out += sprintf(out, "start = %8.8x, length = %8.8x\n",
					fdev->bus->number, 1);
		slot=slot->next;
	}

	return out - buf;
}
static DEVICE_ATTR (dev, S_IRUGO, show_dev, NULL);

void shpchp_create_ctrl_files (struct controller *ctrl)
{
	device_create_file (&ctrl->pci_dev->dev, &dev_attr_ctrl);
	device_create_file (&ctrl->pci_dev->dev, &dev_attr_dev);
}
