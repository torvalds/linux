/*
 * CompactPCI Hot Plug Driver PCI functions
 *
 * Copyright (C) 2002,2005 by SOMA Networks, Inc.
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
 * Send feedback to <scottm@somanetworks.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/proc_fs.h>
#include "../pci.h"
#include "cpci_hotplug.h"

#define MY_NAME	"cpci_hotplug"

extern int cpci_debug;

#define dbg(format, arg...)					\
	do {							\
		if (cpci_debug)					\
			printk (KERN_DEBUG "%s: " format "\n",	\
				MY_NAME , ## arg); 		\
	} while (0)
#define err(format, arg...) printk(KERN_ERR "%s: " format "\n", MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format "\n", MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format "\n", MY_NAME , ## arg)


u8 cpci_get_attention_status(struct slot* slot)
{
	int hs_cap;
	u16 hs_csr;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if (!hs_cap)
		return 0;

	if (pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr))
		return 0;

	return hs_csr & 0x0008 ? 1 : 0;
}

int cpci_set_attention_status(struct slot* slot, int status)
{
	int hs_cap;
	u16 hs_csr;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if (!hs_cap)
		return 0;
	if (pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr))
		return 0;
	if (status)
		hs_csr |= HS_CSR_LOO;
	else
		hs_csr &= ~HS_CSR_LOO;
	if (pci_bus_write_config_word(slot->bus,
				      slot->devfn,
				      hs_cap + 2,
				      hs_csr))
		return 0;
	return 1;
}

u16 cpci_get_hs_csr(struct slot* slot)
{
	int hs_cap;
	u16 hs_csr;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if (!hs_cap)
		return 0xFFFF;
	if (pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr))
		return 0xFFFF;
	return hs_csr;
}

int cpci_check_and_clear_ins(struct slot* slot)
{
	int hs_cap;
	u16 hs_csr;
	int ins = 0;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if (!hs_cap)
		return 0;
	if (pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr))
		return 0;
	if (hs_csr & HS_CSR_INS) {
		/* Clear INS (by setting it) */
		if (pci_bus_write_config_word(slot->bus,
					      slot->devfn,
					      hs_cap + 2,
					      hs_csr))
			ins = 0;
		else
			ins = 1;
	}
	return ins;
}

int cpci_check_ext(struct slot* slot)
{
	int hs_cap;
	u16 hs_csr;
	int ext = 0;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if (!hs_cap)
		return 0;
	if (pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr))
		return 0;
	if (hs_csr & HS_CSR_EXT)
		ext = 1;
	return ext;
}

int cpci_clear_ext(struct slot* slot)
{
	int hs_cap;
	u16 hs_csr;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if (!hs_cap)
		return -ENODEV;
	if (pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr))
		return -ENODEV;
	if (hs_csr & HS_CSR_EXT) {
		/* Clear EXT (by setting it) */
		if (pci_bus_write_config_word(slot->bus,
					      slot->devfn,
					      hs_cap + 2,
					      hs_csr))
			return -ENODEV;
	}
	return 0;
}

int cpci_led_on(struct slot* slot)
{
	int hs_cap;
	u16 hs_csr;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if (!hs_cap)
		return -ENODEV;
	if (pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr))
		return -ENODEV;
	if ((hs_csr & HS_CSR_LOO) != HS_CSR_LOO) {
		hs_csr |= HS_CSR_LOO;
		if (pci_bus_write_config_word(slot->bus,
					      slot->devfn,
					      hs_cap + 2,
					      hs_csr)) {
			err("Could not set LOO for slot %s",
			    hotplug_slot_name(slot->hotplug_slot));
			return -ENODEV;
		}
	}
	return 0;
}

int cpci_led_off(struct slot* slot)
{
	int hs_cap;
	u16 hs_csr;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if (!hs_cap)
		return -ENODEV;
	if (pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr))
		return -ENODEV;
	if (hs_csr & HS_CSR_LOO) {
		hs_csr &= ~HS_CSR_LOO;
		if (pci_bus_write_config_word(slot->bus,
					      slot->devfn,
					      hs_cap + 2,
					      hs_csr)) {
			err("Could not clear LOO for slot %s",
			    hotplug_slot_name(slot->hotplug_slot));
			return -ENODEV;
		}
	}
	return 0;
}


/*
 * Device configuration functions
 */

int __ref cpci_configure_slot(struct slot *slot)
{
	struct pci_bus *parent;
	int fn;

	dbg("%s - enter", __func__);

	if (slot->dev == NULL) {
		dbg("pci_dev null, finding %02x:%02x:%x",
		    slot->bus->number, PCI_SLOT(slot->devfn), PCI_FUNC(slot->devfn));
		slot->dev = pci_get_slot(slot->bus, slot->devfn);
	}

	/* Still NULL? Well then scan for it! */
	if (slot->dev == NULL) {
		int n;
		dbg("pci_dev still null");

		/*
		 * This will generate pci_dev structures for all functions, but
		 * we will only call this case when lookup fails.
		 */
		n = pci_scan_slot(slot->bus, slot->devfn);
		dbg("%s: pci_scan_slot returned %d", __func__, n);
		slot->dev = pci_get_slot(slot->bus, slot->devfn);
		if (slot->dev == NULL) {
			err("Could not find PCI device for slot %02x", slot->number);
			return -ENODEV;
		}
	}
	parent = slot->dev->bus;

	for (fn = 0; fn < 8; fn++) {
		struct pci_dev *dev;

		dev = pci_get_slot(parent, PCI_DEVFN(PCI_SLOT(slot->devfn), fn));
		if (!dev)
			continue;
		if ((dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) ||
		    (dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)) {
			/* Find an unused bus number for the new bridge */
			struct pci_bus *child;
			unsigned char busnr, start = parent->secondary;
			unsigned char end = parent->subordinate;

			for (busnr = start; busnr <= end; busnr++) {
				if (!pci_find_bus(pci_domain_nr(parent),
						  busnr))
					break;
			}
			if (busnr >= end) {
				err("No free bus for hot-added bridge\n");
				pci_dev_put(dev);
				continue;
			}
			child = pci_add_new_bus(parent, dev, busnr);
			if (!child) {
				err("Cannot add new bus for %s\n",
				    pci_name(dev));
				pci_dev_put(dev);
				continue;
			}
			child->subordinate = pci_do_scan_bus(child);
			pci_bus_size_bridges(child);
		}
		pci_dev_put(dev);
	}

	pci_bus_assign_resources(parent);
	pci_bus_add_devices(parent);
	pci_enable_bridges(parent);

	dbg("%s - exit", __func__);
	return 0;
}

int cpci_unconfigure_slot(struct slot* slot)
{
	int i;
	struct pci_dev *dev;

	dbg("%s - enter", __func__);
	if (!slot->dev) {
		err("No device for slot %02x\n", slot->number);
		return -ENODEV;
	}

	for (i = 0; i < 8; i++) {
		dev = pci_get_slot(slot->bus,
				    PCI_DEVFN(PCI_SLOT(slot->devfn), i));
		if (dev) {
			pci_stop_and_remove_bus_device(dev);
			pci_dev_put(dev);
		}
	}
	pci_dev_put(slot->dev);
	slot->dev = NULL;

	dbg("%s - exit", __func__);
	return 0;
}
