/*
 * SHPCHPRM Legacy: PHP Resource Manager for Non-ACPI/Legacy platform
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001 IBM Corp.
 * Copyright (C) 2003-2004 Intel Corporation
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
 * Send feedback to <greg@kroah.com>,<kristen.c.accardi@intel.com>
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#ifdef CONFIG_IA64
#include <asm/iosapic.h>
#endif
#include "shpchp.h"
#include "shpchprm.h"
#include "shpchprm_legacy.h"

static void __iomem *shpchp_rom_start;
static u16 unused_IRQ;

void shpchprm_cleanup(void)
{
	if (shpchp_rom_start)
		iounmap(shpchp_rom_start);
}

int shpchprm_print_pirt(void)
{
	return 0;
}

int shpchprm_get_physical_slot_number(struct controller *ctrl, u32 *sun, u8 busnum, u8 devnum)
{
	int	offset = devnum - ctrl->slot_device_offset;

	*sun = (u8) (ctrl->first_slot + ctrl->slot_num_inc * offset);
	return 0;
}

/* Find the Hot Plug Resource Table in the specified region of memory */
static void __iomem *detect_HRT_floating_pointer(void __iomem *begin, void __iomem *end)
{
	void __iomem *fp;
	void __iomem *endp;
	u8 temp1, temp2, temp3, temp4;
	int status = 0;

	endp = (end - sizeof(struct hrt) + 1);

	for (fp = begin; fp <= endp; fp += 16) {
		temp1 = readb(fp + SIG0);
		temp2 = readb(fp + SIG1);
		temp3 = readb(fp + SIG2);
		temp4 = readb(fp + SIG3);
		if (temp1 == '$' && temp2 == 'H' && temp3 == 'R' && temp4 == 'T') {
			status = 1;
			break;
		}
	}

	if (!status)
		fp = NULL;

	dbg("Discovered Hotplug Resource Table at %p\n", fp);
	return fp;
}

/*
 * shpchprm_find_available_resources
 *
 *  Finds available memory, IO, and IRQ resources for programming
 *  devices which may be added to the system
 *  this function is for hot plug ADD!
 *
 * returns 0 if success
 */
int shpchprm_find_available_resources(struct controller *ctrl)
{
	u8 populated_slot;
	u8 bridged_slot;
	void __iomem *one_slot;
	struct pci_func *func = NULL;
	int i = 10, index = 0;
	u32 temp_dword, rc;
	ulong temp_ulong;
	struct pci_resource *mem_node;
	struct pci_resource *p_mem_node;
	struct pci_resource *io_node;
	struct pci_resource *bus_node;
	void __iomem *rom_resource_table;
	struct pci_bus lpci_bus, *pci_bus;
	u8 cfgspc_irq, temp;

	memcpy(&lpci_bus, ctrl->pci_bus, sizeof(lpci_bus));
	pci_bus = &lpci_bus;
	rom_resource_table = detect_HRT_floating_pointer(shpchp_rom_start, shpchp_rom_start + 0xffff);
	dbg("rom_resource_table = %p\n", rom_resource_table);
	if (rom_resource_table == NULL)
		return -ENODEV;

	/* Sum all resources and setup resource maps */
	unused_IRQ = readl(rom_resource_table + UNUSED_IRQ);
	dbg("unused_IRQ = %x\n", unused_IRQ);

	temp = 0;
	while (unused_IRQ) {
		if (unused_IRQ & 1) {
			shpchp_disk_irq = temp;
			break;
		}
		unused_IRQ = unused_IRQ >> 1;
		temp++;
	}

	dbg("shpchp_disk_irq= %d\n", shpchp_disk_irq);
	unused_IRQ = unused_IRQ >> 1;
	temp++;

	while (unused_IRQ) {
		if (unused_IRQ & 1) {
			shpchp_nic_irq = temp;
			break;
		}
		unused_IRQ = unused_IRQ >> 1;
		temp++;
	}

	dbg("shpchp_nic_irq= %d\n", shpchp_nic_irq);
	unused_IRQ = readl(rom_resource_table + PCIIRQ);

	temp = 0;

	pci_read_config_byte(ctrl->pci_dev, PCI_INTERRUPT_LINE, &cfgspc_irq);

	if (!shpchp_nic_irq) {
		shpchp_nic_irq = cfgspc_irq;
	}

	if (!shpchp_disk_irq) {
		shpchp_disk_irq = cfgspc_irq;
	}

	dbg("shpchp_disk_irq, shpchp_nic_irq= %d, %d\n", shpchp_disk_irq, shpchp_nic_irq);

	one_slot = rom_resource_table + sizeof(struct hrt);

	i = readb(rom_resource_table + NUMBER_OF_ENTRIES);
	dbg("number_of_entries = %d\n", i);

	if (!readb(one_slot + SECONDARY_BUS))
		return (1);

	dbg("dev|IO base|length|MEMbase|length|PM base|length|PB SB MB\n");

	while (i && readb(one_slot + SECONDARY_BUS)) {
		u8 dev_func = readb(one_slot + DEV_FUNC);
		u8 primary_bus = readb(one_slot + PRIMARY_BUS);
		u8 secondary_bus = readb(one_slot + SECONDARY_BUS);
		u8 max_bus = readb(one_slot + MAX_BUS);
		u16 io_base = readw(one_slot + IO_BASE);
		u16 io_length = readw(one_slot + IO_LENGTH);
		u16 mem_base = readw(one_slot + MEM_BASE);
		u16 mem_length = readw(one_slot + MEM_LENGTH);
		u16 pre_mem_base = readw(one_slot + PRE_MEM_BASE);
		u16 pre_mem_length = readw(one_slot + PRE_MEM_LENGTH);

		dbg("%2.2x |  %4.4x | %4.4x |  %4.4x | %4.4x |  %4.4x | %4.4x |%2.2x %2.2x %2.2x\n",
				dev_func, io_base, io_length, mem_base, mem_length, pre_mem_base, pre_mem_length,
				primary_bus, secondary_bus, max_bus);

		/* If this entry isn't for our controller's bus, ignore it */
		if (primary_bus != ctrl->slot_bus) {
			i--;
			one_slot += sizeof(struct slot_rt);
			continue;
		}
		/* find out if this entry is for an occupied slot */
		temp_dword = 0xFFFFFFFF;
		pci_bus->number = primary_bus;
		pci_bus_read_config_dword(pci_bus, dev_func, PCI_VENDOR_ID, &temp_dword);

		dbg("temp_D_word = %x\n", temp_dword);

		if (temp_dword != 0xFFFFFFFF) {
			index = 0;
			func = shpchp_slot_find(primary_bus, dev_func >> 3, 0);

			while (func && (func->function != (dev_func & 0x07))) {
				dbg("func = %p b:d:f(%x:%x:%x)\n", func, primary_bus, dev_func >> 3, index);
				func = shpchp_slot_find(primary_bus, dev_func >> 3, index++);
			}

			/* If we can't find a match, skip this table entry */
			if (!func) {
				i--;
				one_slot += sizeof(struct slot_rt);
				continue;
			}
			/* this may not work and shouldn't be used */
			if (secondary_bus != primary_bus)
				bridged_slot = 1;
			else
				bridged_slot = 0;

			populated_slot = 1;
		} else {
			populated_slot = 0;
			bridged_slot = 0;
		}
		dbg("slot populated =%s \n", populated_slot?"yes":"no");

		/* If we've got a valid IO base, use it */

		temp_ulong = io_base + io_length;

		if ((io_base) && (temp_ulong <= 0x10000)) {
			io_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
			if (!io_node)
				return -ENOMEM;

			io_node->base = (ulong)io_base;
			io_node->length = (ulong)io_length;
			dbg("found io_node(base, length) = %x, %x\n", io_node->base, io_node->length);

			if (!populated_slot) {
				io_node->next = ctrl->io_head;
				ctrl->io_head = io_node;
			} else {
				io_node->next = func->io_head;
				func->io_head = io_node;
			}
		}

		/* If we've got a valid memory base, use it */
		temp_ulong = mem_base + mem_length;
		if ((mem_base) && (temp_ulong <= 0x10000)) {
			mem_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
			if (!mem_node)
				return -ENOMEM;

			mem_node->base = (ulong)mem_base << 16;
			mem_node->length = (ulong)(mem_length << 16);
			dbg("found mem_node(base, length) = %x, %x\n", mem_node->base, mem_node->length);

			if (!populated_slot) {
				mem_node->next = ctrl->mem_head;
				ctrl->mem_head = mem_node;
			} else {
				mem_node->next = func->mem_head;
				func->mem_head = mem_node;
			}
		}

		/*
		 * If we've got a valid prefetchable memory base, and
		 * the base + length isn't greater than 0xFFFF
		 */
		temp_ulong = pre_mem_base + pre_mem_length;
		if ((pre_mem_base) && (temp_ulong <= 0x10000)) {
			p_mem_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
			if (!p_mem_node)
				return -ENOMEM;

			p_mem_node->base = (ulong)pre_mem_base << 16;
			p_mem_node->length = (ulong)pre_mem_length << 16;
			dbg("found p_mem_node(base, length) = %x, %x\n", p_mem_node->base, p_mem_node->length);

			if (!populated_slot) {
				p_mem_node->next = ctrl->p_mem_head;
				ctrl->p_mem_head = p_mem_node;
			} else {
				p_mem_node->next = func->p_mem_head;
				func->p_mem_head = p_mem_node;
			}
		}

		/*
		 * If we've got a valid bus number, use it
		 * The second condition is to ignore bus numbers on
		 * populated slots that don't have PCI-PCI bridges
		 */
		if (secondary_bus && (secondary_bus != primary_bus)) {
			bus_node = (struct pci_resource *) kmalloc(sizeof(struct pci_resource), GFP_KERNEL);
			if (!bus_node)
				return -ENOMEM;

			bus_node->base = (ulong)secondary_bus;
			bus_node->length = (ulong)(max_bus - secondary_bus + 1);
			dbg("found bus_node(base, length) = %x, %x\n", bus_node->base, bus_node->length);

			if (!populated_slot) {
				bus_node->next = ctrl->bus_head;
				ctrl->bus_head = bus_node;
			} else {
				bus_node->next = func->bus_head;
				func->bus_head = bus_node;
			}
		}

		i--;
		one_slot += sizeof(struct slot_rt);
	}

	/* If all of the following fail, we don't have any resources for hot plug add */
	rc = 1;
	rc &= shpchp_resource_sort_and_combine(&(ctrl->mem_head));
	rc &= shpchp_resource_sort_and_combine(&(ctrl->p_mem_head));
	rc &= shpchp_resource_sort_and_combine(&(ctrl->io_head));
	rc &= shpchp_resource_sort_and_combine(&(ctrl->bus_head));

	return (rc);
}

int shpchprm_set_hpp(
	struct controller *ctrl,
	struct pci_func *func,
	u8	card_type)
{
	u32 rc;
	u8 temp_byte;
	struct pci_bus lpci_bus, *pci_bus;
	unsigned int	devfn;
	memcpy(&lpci_bus, ctrl->pci_bus, sizeof(lpci_bus));
	pci_bus = &lpci_bus;
	pci_bus->number = func->bus;
	devfn = PCI_DEVFN(func->device, func->function);

	temp_byte = 0x40;	/* hard coded value for LT */
	if (card_type == PCI_HEADER_TYPE_BRIDGE) {
		/* set subordinate Latency Timer */
		rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_SEC_LATENCY_TIMER, temp_byte);
		if (rc) {
			dbg("%s: set secondary LT error. b:d:f(%02x:%02x:%02x)\n", __FUNCTION__, func->bus, 
				func->device, func->function);
			return rc;
		}
	}

	/* set base Latency Timer */
	rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_LATENCY_TIMER, temp_byte);
	if (rc) {
		dbg("%s: set LT error. b:d:f(%02x:%02x:%02x)\n", __FUNCTION__, func->bus, func->device, func->function);
		return rc;
	}

	/* set Cache Line size */
	temp_byte = 0x08;	/* hard coded value for CLS */
	rc = pci_bus_write_config_byte(pci_bus, devfn, PCI_CACHE_LINE_SIZE, temp_byte);
	if (rc) {
		dbg("%s: set CLS error. b:d:f(%02x:%02x:%02x)\n", __FUNCTION__, func->bus, func->device, func->function);
	}

	/* set enable_perr */
	/* set enable_serr */

	return rc;
}

void shpchprm_enable_card(
	struct controller *ctrl,
	struct pci_func *func,
	u8 card_type)
{
	u16 command, bcommand;
	struct pci_bus lpci_bus, *pci_bus;
	unsigned int devfn;
	int rc;

	memcpy(&lpci_bus, ctrl->pci_bus, sizeof(lpci_bus));
	pci_bus = &lpci_bus;
	pci_bus->number = func->bus;
	devfn = PCI_DEVFN(func->device, func->function);

	rc = pci_bus_read_config_word(pci_bus, devfn, PCI_COMMAND, &command);
	command |= PCI_COMMAND_PARITY | PCI_COMMAND_SERR
		| PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE
		| PCI_COMMAND_IO | PCI_COMMAND_MEMORY;
	rc = pci_bus_write_config_word(pci_bus, devfn, PCI_COMMAND, command);

	if (card_type == PCI_HEADER_TYPE_BRIDGE) {
		rc = pci_bus_read_config_word(pci_bus, devfn, PCI_BRIDGE_CONTROL, &bcommand);
		bcommand |= PCI_BRIDGE_CTL_PARITY | PCI_BRIDGE_CTL_SERR
			| PCI_BRIDGE_CTL_NO_ISA;
		rc = pci_bus_write_config_word(pci_bus, devfn, PCI_BRIDGE_CONTROL, bcommand);
	}
}

static int legacy_shpchprm_init_pci(void)
{
	shpchp_rom_start = ioremap(ROM_PHY_ADDR, ROM_PHY_LEN);
	if (!shpchp_rom_start) {
		err("Could not ioremap memory region for ROM\n");
		return -EIO;
	}

	return 0;
}

int shpchprm_init(enum php_ctlr_type ctrl_type)
{
	int retval;

	switch (ctrl_type) {
	case PCI:
		retval = legacy_shpchprm_init_pci();
		break;
	default:
		retval = -ENODEV;
		break;
	}

	return retval;
}
