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
	struct pci_dev *pci_dev;
	struct controller *ctrl;
	char * out = buf;
	int index;
	struct pci_resource *res;

	pci_dev = container_of (dev, struct pci_dev, dev);
	ctrl = pci_get_drvdata(pci_dev);

	out += sprintf(buf, "Free resources: memory\n");
	index = 11;
	res = ctrl->mem_head;
	while (res && index--) {
		out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
		res = res->next;
	}
	out += sprintf(out, "Free resources: prefetchable memory\n");
	index = 11;
	res = ctrl->p_mem_head;
	while (res && index--) {
		out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
		res = res->next;
	}
	out += sprintf(out, "Free resources: IO\n");
	index = 11;
	res = ctrl->io_head;
	while (res && index--) {
		out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
		res = res->next;
	}
	out += sprintf(out, "Free resources: bus numbers\n");
	index = 11;
	res = ctrl->bus_head;
	while (res && index--) {
		out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
		res = res->next;
	}

	return out - buf;
}
static DEVICE_ATTR (ctrl, S_IRUGO, show_ctrl, NULL);

static ssize_t show_dev (struct device *dev, struct device_attribute *attr, char *buf)
{
	struct pci_dev *pci_dev;
	struct controller *ctrl;
	char * out = buf;
	int index;
	struct pci_resource *res;
	struct pci_func *new_slot;
	struct slot *slot;

	pci_dev = container_of (dev, struct pci_dev, dev);
	ctrl = pci_get_drvdata(pci_dev);

	slot=ctrl->slot;

	while (slot) {
		new_slot = shpchp_slot_find(slot->bus, slot->device, 0);
		if (!new_slot)
			break;
		out += sprintf(out, "assigned resources: memory\n");
		index = 11;
		res = new_slot->mem_head;
		while (res && index--) {
			out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
			res = res->next;
		}
		out += sprintf(out, "assigned resources: prefetchable memory\n");
		index = 11;
		res = new_slot->p_mem_head;
		while (res && index--) {
			out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
			res = res->next;
		}
		out += sprintf(out, "assigned resources: IO\n");
		index = 11;
		res = new_slot->io_head;
		while (res && index--) {
			out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
			res = res->next;
		}
		out += sprintf(out, "assigned resources: bus numbers\n");
		index = 11;
		res = new_slot->bus_head;
		while (res && index--) {
			out += sprintf(out, "start = %8.8x, length = %8.8x\n", res->base, res->length);
			res = res->next;
		}
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
