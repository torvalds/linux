/*
 * ACPI PCI HotPlug PCI configuration space management
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001,2002 IBM Corp.
 * Copyright (C) 2002 Takayoshi Kochi (t-kochi@bq.jp.nec.com)
 * Copyright (C) 2002 Hiroshi Aono (h-aono@ap.jp.nec.com)
 * Copyright (C) 2002 NEC Corporation
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
 * Send feedback to <t-kochi@bq.jp.nec.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/acpi.h>
#include "../pci.h"
#include "pci_hotplug.h"
#include "acpiphp.h"

#define MY_NAME "acpiphp_pci"


/* allocate mem/pmem/io resource to a new function */
static int init_config_space (struct acpiphp_func *func)
{
	u32 bar, len;
	u32 address[] = {
		PCI_BASE_ADDRESS_0,
		PCI_BASE_ADDRESS_1,
		PCI_BASE_ADDRESS_2,
		PCI_BASE_ADDRESS_3,
		PCI_BASE_ADDRESS_4,
		PCI_BASE_ADDRESS_5,
		0
	};
	int count;
	struct acpiphp_bridge *bridge;
	struct pci_resource *res;
	struct pci_bus *pbus;
	int bus, device, function;
	unsigned int devfn;
	u16 tmp;

	bridge = func->slot->bridge;
	pbus = bridge->pci_bus;
	bus = bridge->bus;
	device = func->slot->device;
	function = func->function;
	devfn = PCI_DEVFN(device, function);

	for (count = 0; address[count]; count++) {	/* for 6 BARs */
		pci_bus_write_config_dword(pbus, devfn,
					   address[count], 0xFFFFFFFF);
		pci_bus_read_config_dword(pbus, devfn, address[count], &bar);

		if (!bar)	/* This BAR is not implemented */
			continue;

		dbg("Device %02x.%02x BAR %d wants %x\n", device, function, count, bar);

		if (bar & PCI_BASE_ADDRESS_SPACE_IO) {
			/* This is IO */

			len = bar & (PCI_BASE_ADDRESS_IO_MASK & 0xFFFF);
			len = len & ~(len - 1);

			dbg("len in IO %x, BAR %d\n", len, count);

			spin_lock(&bridge->res_lock);
			res = acpiphp_get_io_resource(&bridge->io_head, len);
			spin_unlock(&bridge->res_lock);

			if (!res) {
				err("cannot allocate requested io for %02x:%02x.%d len %x\n",
				    bus, device, function, len);
				return -1;
			}
			pci_bus_write_config_dword(pbus, devfn,
						   address[count],
						   (u32)res->base);
			res->next = func->io_head;
			func->io_head = res;

		} else {
			/* This is Memory */
			if (bar & PCI_BASE_ADDRESS_MEM_PREFETCH) {
				/* pfmem */

				len = bar & 0xFFFFFFF0;
				len = ~len + 1;

				dbg("len in PFMEM %x, BAR %d\n", len, count);

				spin_lock(&bridge->res_lock);
				res = acpiphp_get_resource(&bridge->p_mem_head, len);
				spin_unlock(&bridge->res_lock);

				if (!res) {
					err("cannot allocate requested pfmem for %02x:%02x.%d len %x\n",
					    bus, device, function, len);
					return -1;
				}

				pci_bus_write_config_dword(pbus, devfn,
							   address[count],
							   (u32)res->base);

				if (bar & PCI_BASE_ADDRESS_MEM_TYPE_64) {	/* takes up another dword */
					dbg("inside the pfmem 64 case, count %d\n", count);
					count += 1;
					pci_bus_write_config_dword(pbus, devfn,
								   address[count],
								   (u32)(res->base >> 32));
				}

				res->next = func->p_mem_head;
				func->p_mem_head = res;

			} else {
				/* regular memory */

				len = bar & 0xFFFFFFF0;
				len = ~len + 1;

				dbg("len in MEM %x, BAR %d\n", len, count);

				spin_lock(&bridge->res_lock);
				res = acpiphp_get_resource(&bridge->mem_head, len);
				spin_unlock(&bridge->res_lock);

				if (!res) {
					err("cannot allocate requested pfmem for %02x:%02x.%d len %x\n",
					    bus, device, function, len);
					return -1;
				}

				pci_bus_write_config_dword(pbus, devfn,
							   address[count],
							   (u32)res->base);

				if (bar & PCI_BASE_ADDRESS_MEM_TYPE_64) {
					/* takes up another dword */
					dbg("inside mem 64 case, reg. mem, count %d\n", count);
					count += 1;
					pci_bus_write_config_dword(pbus, devfn,
								   address[count],
								   (u32)(res->base >> 32));
				}

				res->next = func->mem_head;
				func->mem_head = res;

			}
		}
	}

	/* disable expansion rom */
	pci_bus_write_config_dword(pbus, devfn, PCI_ROM_ADDRESS, 0x00000000);

	/* set PCI parameters from _HPP */
	pci_bus_write_config_byte(pbus, devfn, PCI_CACHE_LINE_SIZE,
				  bridge->hpp.cache_line_size);
	pci_bus_write_config_byte(pbus, devfn, PCI_LATENCY_TIMER,
				  bridge->hpp.latency_timer);

	pci_bus_read_config_word(pbus, devfn, PCI_COMMAND, &tmp);
	if (bridge->hpp.enable_SERR)
		tmp |= PCI_COMMAND_SERR;
	if (bridge->hpp.enable_PERR)
		tmp |= PCI_COMMAND_PARITY;
	pci_bus_write_config_word(pbus, devfn, PCI_COMMAND, tmp);

	return 0;
}

/* detect_used_resource - subtract resource under dev from bridge */
static int detect_used_resource (struct acpiphp_bridge *bridge, struct pci_dev *dev)
{
	int count;

	dbg("Device %s\n", pci_name(dev));

	for (count = 0; count < DEVICE_COUNT_RESOURCE; count++) {
		struct pci_resource *res;
		struct pci_resource **head;
		unsigned long base = dev->resource[count].start;
		unsigned long len = dev->resource[count].end - base + 1;
		unsigned long flags = dev->resource[count].flags;

		if (!flags)
			continue;

		dbg("BAR[%d] 0x%lx - 0x%lx (0x%lx)\n", count, base,
				base + len - 1, flags);

		if (flags & IORESOURCE_IO) {
			head = &bridge->io_head;
		} else if (flags & IORESOURCE_PREFETCH) {
			head = &bridge->p_mem_head;
		} else {
			head = &bridge->mem_head;
		}

		spin_lock(&bridge->res_lock);
		res = acpiphp_get_resource_with_base(head, base, len);
		spin_unlock(&bridge->res_lock);
		if (res)
			kfree(res);
	}

	return 0;
}


/**
 * acpiphp_detect_pci_resource - detect resources under bridge
 * @bridge: detect all resources already used under this bridge
 *
 * collect all resources already allocated for all devices under a bridge.
 */
int acpiphp_detect_pci_resource (struct acpiphp_bridge *bridge)
{
	struct list_head *l;
	struct pci_dev *dev;

	list_for_each (l, &bridge->pci_bus->devices) {
		dev = pci_dev_b(l);
		detect_used_resource(bridge, dev);
	}

	return 0;
}


/**
 * acpiphp_init_slot_resource - gather resource usage information of a slot
 * @slot: ACPI slot object to be checked, should have valid pci_dev member
 *
 * TBD: PCI-to-PCI bridge case
 *      use pci_dev->resource[]
 */
int acpiphp_init_func_resource (struct acpiphp_func *func)
{
	u64 base;
	u32 bar, len;
	u32 address[] = {
		PCI_BASE_ADDRESS_0,
		PCI_BASE_ADDRESS_1,
		PCI_BASE_ADDRESS_2,
		PCI_BASE_ADDRESS_3,
		PCI_BASE_ADDRESS_4,
		PCI_BASE_ADDRESS_5,
		0
	};
	int count;
	struct pci_resource *res;
	struct pci_dev *dev;

	dev = func->pci_dev;
	dbg("Hot-pluggable device %s\n", pci_name(dev));

	for (count = 0; address[count]; count++) {	/* for 6 BARs */
		pci_read_config_dword(dev, address[count], &bar);

		if (!bar)	/* This BAR is not implemented */
			continue;

		pci_write_config_dword(dev, address[count], 0xFFFFFFFF);
		pci_read_config_dword(dev, address[count], &len);

		if (len & PCI_BASE_ADDRESS_SPACE_IO) {
			/* This is IO */
			base = bar & 0xFFFFFFFC;
			len = len & (PCI_BASE_ADDRESS_IO_MASK & 0xFFFF);
			len = len & ~(len - 1);

			dbg("BAR[%d] %08x - %08x (IO)\n", count, (u32)base, (u32)base + len - 1);

			res = acpiphp_make_resource(base, len);
			if (!res)
				goto no_memory;

			res->next = func->io_head;
			func->io_head = res;

		} else {
			/* This is Memory */
			base = bar & 0xFFFFFFF0;
			if (len & PCI_BASE_ADDRESS_MEM_PREFETCH) {
				/* pfmem */

				len &= 0xFFFFFFF0;
				len = ~len + 1;

				if (len & PCI_BASE_ADDRESS_MEM_TYPE_64) {	/* takes up another dword */
					dbg("prefetch mem 64\n");
					count += 1;
				}
				dbg("BAR[%d] %08x - %08x (PMEM)\n", count, (u32)base, (u32)base + len - 1);
				res = acpiphp_make_resource(base, len);
				if (!res)
					goto no_memory;

				res->next = func->p_mem_head;
				func->p_mem_head = res;

			} else {
				/* regular memory */

				len &= 0xFFFFFFF0;
				len = ~len + 1;

				if (len & PCI_BASE_ADDRESS_MEM_TYPE_64) {
					/* takes up another dword */
					dbg("mem 64\n");
					count += 1;
				}
				dbg("BAR[%d] %08x - %08x (MEM)\n", count, (u32)base, (u32)base + len - 1);
				res = acpiphp_make_resource(base, len);
				if (!res)
					goto no_memory;

				res->next = func->mem_head;
				func->mem_head = res;

			}
		}

		pci_write_config_dword(dev, address[count], bar);
	}
#if 1
	acpiphp_dump_func_resource(func);
#endif

	return 0;

 no_memory:
	err("out of memory\n");
	acpiphp_free_resource(&func->io_head);
	acpiphp_free_resource(&func->mem_head);
	acpiphp_free_resource(&func->p_mem_head);

	return -1;
}


/**
 * acpiphp_configure_slot - allocate PCI resources
 * @slot: slot to be configured
 *
 * initializes a PCI functions on a device inserted
 * into the slot
 *
 */
int acpiphp_configure_slot (struct acpiphp_slot *slot)
{
	struct acpiphp_func *func;
	struct list_head *l;
	u8 hdr;
	u32 dvid;
	int retval = 0;
	int is_multi = 0;

	pci_bus_read_config_byte(slot->bridge->pci_bus,
				 PCI_DEVFN(slot->device, 0),
				 PCI_HEADER_TYPE, &hdr);

	if (hdr & 0x80)
		is_multi = 1;

	list_for_each (l, &slot->funcs) {
		func = list_entry(l, struct acpiphp_func, sibling);
		if (is_multi || func->function == 0) {
			pci_bus_read_config_dword(slot->bridge->pci_bus,
						  PCI_DEVFN(slot->device,
							    func->function),
						  PCI_VENDOR_ID, &dvid);
			if (dvid != 0xffffffff) {
				retval = init_config_space(func);
				if (retval)
					break;
			}
		}
	}

	return retval;
}

/**
 * acpiphp_configure_function - configure PCI function
 * @func: function to be configured
 *
 * initializes a PCI functions on a device inserted
 * into the slot
 *
 */
int acpiphp_configure_function (struct acpiphp_func *func)
{
	/* all handled by the pci core now */
	return 0;
}

/**
 * acpiphp_unconfigure_function - unconfigure PCI function
 * @func: function to be unconfigured
 *
 */
void acpiphp_unconfigure_function (struct acpiphp_func *func)
{
	struct acpiphp_bridge *bridge;

	/* if pci_dev is NULL, ignore it */
	if (!func->pci_dev)
		return;

	pci_remove_bus_device(func->pci_dev);

	/* free all resources */
	bridge = func->slot->bridge;

	spin_lock(&bridge->res_lock);
	acpiphp_move_resource(&func->io_head, &bridge->io_head);
	acpiphp_move_resource(&func->mem_head, &bridge->mem_head);
	acpiphp_move_resource(&func->p_mem_head, &bridge->p_mem_head);
	acpiphp_move_resource(&func->bus_head, &bridge->bus_head);
	spin_unlock(&bridge->res_lock);
}
