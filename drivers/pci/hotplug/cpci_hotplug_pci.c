/*
 * CompactPCI Hot Plug Driver PCI functions
 *
 * Copyright (C) 2002 by SOMA Networks, Inc.
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include "../pci.h"
#include "pci_hotplug.h"
#include "cpci_hotplug.h"

#if !defined(MODULE)
#define MY_NAME	"cpci_hotplug"
#else
#define MY_NAME	THIS_MODULE->name
#endif

extern int cpci_debug;

#define dbg(format, arg...)					\
	do {							\
		if(cpci_debug)					\
			printk (KERN_DEBUG "%s: " format "\n",	\
				MY_NAME , ## arg); 		\
	} while(0)
#define err(format, arg...) printk(KERN_ERR "%s: " format "\n", MY_NAME , ## arg)
#define info(format, arg...) printk(KERN_INFO "%s: " format "\n", MY_NAME , ## arg)
#define warn(format, arg...) printk(KERN_WARNING "%s: " format "\n", MY_NAME , ## arg)

#define ROUND_UP(x, a)		(((x) + (a) - 1) & ~((a) - 1))


u8 cpci_get_attention_status(struct slot* slot)
{
	int hs_cap;
	u16 hs_csr;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if(!hs_cap) {
		return 0;
	}

	if(pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr)) {
		return 0;
	}
	return hs_csr & 0x0008 ? 1 : 0;
}

int cpci_set_attention_status(struct slot* slot, int status)
{
	int hs_cap;
	u16 hs_csr;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if(!hs_cap) {
		return 0;
	}

	if(pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr)) {
		return 0;
	}
	if(status) {
		hs_csr |= HS_CSR_LOO;
	} else {
		hs_csr &= ~HS_CSR_LOO;
	}
	if(pci_bus_write_config_word(slot->bus,
				      slot->devfn,
				      hs_cap + 2,
				      hs_csr)) {
		return 0;
	}
	return 1;
}

u16 cpci_get_hs_csr(struct slot* slot)
{
	int hs_cap;
	u16 hs_csr;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if(!hs_cap) {
		return 0xFFFF;
	}

	if(pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr)) {
		return 0xFFFF;
	}
	return hs_csr;
}

#if 0
u16 cpci_set_hs_csr(struct slot* slot, u16 hs_csr)
{
	int hs_cap;
	u16 new_hs_csr;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if(!hs_cap) {
		return 0xFFFF;
	}

	/* Write out the new value */
	if(pci_bus_write_config_word(slot->bus,
				      slot->devfn,
				      hs_cap + 2,
				      hs_csr)) {
		return 0xFFFF;
	}

	/* Read back what we just wrote out */
	if(pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &new_hs_csr)) {
		return 0xFFFF;
	}
	return new_hs_csr;
}
#endif

int cpci_check_and_clear_ins(struct slot* slot)
{
	int hs_cap;
	u16 hs_csr;
	int ins = 0;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if(!hs_cap) {
		return 0;
	}
	if(pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr)) {
		return 0;
	}
	if(hs_csr & HS_CSR_INS) {
		/* Clear INS (by setting it) */
		if(pci_bus_write_config_word(slot->bus,
					      slot->devfn,
					      hs_cap + 2,
					      hs_csr)) {
			ins = 0;
		}
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
	if(!hs_cap) {
		return 0;
	}
	if(pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr)) {
		return 0;
	}
	if(hs_csr & HS_CSR_EXT) {
		ext = 1;
	}
	return ext;
}

int cpci_clear_ext(struct slot* slot)
{
	int hs_cap;
	u16 hs_csr;

	hs_cap = pci_bus_find_capability(slot->bus,
					 slot->devfn,
					 PCI_CAP_ID_CHSWP);
	if(!hs_cap) {
		return -ENODEV;
	}
	if(pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr)) {
		return -ENODEV;
	}
	if(hs_csr & HS_CSR_EXT) {
		/* Clear EXT (by setting it) */
		if(pci_bus_write_config_word(slot->bus,
					      slot->devfn,
					      hs_cap + 2,
					      hs_csr)) {
			return -ENODEV;
		}
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
	if(!hs_cap) {
		return -ENODEV;
	}
	if(pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr)) {
		return -ENODEV;
	}
	if((hs_csr & HS_CSR_LOO) != HS_CSR_LOO) {
		/* Set LOO */
		hs_csr |= HS_CSR_LOO;
		if(pci_bus_write_config_word(slot->bus,
					      slot->devfn,
					      hs_cap + 2,
					      hs_csr)) {
			err("Could not set LOO for slot %s",
			    slot->hotplug_slot->name);
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
	if(!hs_cap) {
		return -ENODEV;
	}
	if(pci_bus_read_config_word(slot->bus,
				     slot->devfn,
				     hs_cap + 2,
				     &hs_csr)) {
		return -ENODEV;
	}
	if(hs_csr & HS_CSR_LOO) {
		/* Clear LOO */
		hs_csr &= ~HS_CSR_LOO;
		if(pci_bus_write_config_word(slot->bus,
					      slot->devfn,
					      hs_cap + 2,
					      hs_csr)) {
			err("Could not clear LOO for slot %s",
			    slot->hotplug_slot->name);
			return -ENODEV;
		}
	}
	return 0;
}


/*
 * Device configuration functions
 */

static int cpci_configure_dev(struct pci_bus *bus, struct pci_dev *dev)
{
	u8 irq_pin;
	int r;

	dbg("%s - enter", __FUNCTION__);

	/* NOTE: device already setup from prior scan */

	/* FIXME: How would we know if we need to enable the expansion ROM? */
	pci_write_config_word(dev, PCI_ROM_ADDRESS, 0x00L);

	/* Assign resources */
	dbg("assigning resources for %02x:%02x.%x",
	    dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	for (r = 0; r < 6; r++) {
		struct resource *res = dev->resource + r;
		if(res->flags)
			pci_assign_resource(dev, r);
	}
	dbg("finished assigning resources for %02x:%02x.%x",
	    dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));

	/* Does this function have an interrupt at all? */
	dbg("checking for function interrupt");
	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &irq_pin);
	if(irq_pin) {
		dbg("function uses interrupt pin %d", irq_pin);
	}

	/*
	 * Need to explicitly set irq field to 0 so that it'll get assigned
	 * by the pcibios platform dependent code called by pci_enable_device.
	 */
	dev->irq = 0;

	dbg("enabling device");
	pci_enable_device(dev);	/* XXX check return */
	dbg("now dev->irq = %d", dev->irq);
	if(irq_pin && dev->irq) {
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
	}

	/* Can't use pci_insert_device at the moment, do it manually for now */
	pci_proc_attach_device(dev);
	dbg("notifying drivers");
	//pci_announce_device_to_drivers(dev);
	dbg("%s - exit", __FUNCTION__);
	return 0;
}

static int cpci_configure_bridge(struct pci_bus* bus, struct pci_dev* dev)
{
	int rc;
	struct pci_bus* child;
	struct resource* r;
	u8 max, n;
	u16 command;

	dbg("%s - enter", __FUNCTION__);

	/* Do basic bridge initialization */
	rc = pci_write_config_byte(dev, PCI_LATENCY_TIMER, 0x40);
	if(rc) {
		printk(KERN_ERR "%s - write of PCI_LATENCY_TIMER failed\n", __FUNCTION__);
	}
	rc = pci_write_config_byte(dev, PCI_SEC_LATENCY_TIMER, 0x40);
	if(rc) {
		printk(KERN_ERR "%s - write of PCI_SEC_LATENCY_TIMER failed\n", __FUNCTION__);
	}
	rc = pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, L1_CACHE_BYTES / 4);
	if(rc) {
		printk(KERN_ERR "%s - write of PCI_CACHE_LINE_SIZE failed\n", __FUNCTION__);
	}

	/*
	 * Set parent bridge's subordinate field so that configuration space
	 * access will work in pci_scan_bridge and friends.
	 */
	max = pci_max_busnr();
	bus->subordinate = max + 1;
	pci_write_config_byte(bus->self, PCI_SUBORDINATE_BUS, max + 1);

	/* Scan behind bridge */
	n = pci_scan_bridge(bus, dev, max, 2);
	child = pci_find_bus(0, max + 1);
	if (!child)
		return -ENODEV;
	pci_proc_attach_bus(child);

	/*
	 * Update parent bridge's subordinate field if there were more bridges
	 * behind the bridge that was scanned.
	 */
	if(n > max) {
		bus->subordinate = n;
		pci_write_config_byte(bus->self, PCI_SUBORDINATE_BUS, n);
	}

	/*
	 * Update the bridge resources of the bridge to accommodate devices
	 * behind it.
	 */
	pci_bus_size_bridges(child);
	pci_bus_assign_resources(child);

	/* Enable resource mapping via command register */
	command = PCI_COMMAND_MASTER | PCI_COMMAND_INVALIDATE | PCI_COMMAND_PARITY | PCI_COMMAND_SERR;
	r = child->resource[0];
	if(r && r->start) {
		command |= PCI_COMMAND_IO;
	}
	r = child->resource[1];
	if(r && r->start) {
		command |= PCI_COMMAND_MEMORY;
	}
	r = child->resource[2];
	if(r && r->start) {
		command |= PCI_COMMAND_MEMORY;
	}
	rc = pci_write_config_word(dev, PCI_COMMAND, command);
	if(rc) {
		err("Error setting command register");
		return rc;
	}

	/* Set bridge control register */
	command = PCI_BRIDGE_CTL_PARITY | PCI_BRIDGE_CTL_SERR | PCI_BRIDGE_CTL_NO_ISA;
	rc = pci_write_config_word(dev, PCI_BRIDGE_CONTROL, command);
	if(rc) {
		err("Error setting bridge control register");
		return rc;
	}
	dbg("%s - exit", __FUNCTION__);
	return 0;
}

static int configure_visit_pci_dev(struct pci_dev_wrapped *wrapped_dev,
				   struct pci_bus_wrapped *wrapped_bus)
{
	int rc;
	struct pci_dev *dev = wrapped_dev->dev;
	struct pci_bus *bus = wrapped_bus->bus;
	struct slot* slot;

	dbg("%s - enter", __FUNCTION__);

	/*
	 * We need to fix up the hotplug representation with the Linux
	 * representation.
	 */
	if(wrapped_dev->data) {
		slot = (struct slot*) wrapped_dev->data;
		slot->dev = dev;
	}

	/* If it's a bridge, scan behind it for devices */
	if(dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
		rc = cpci_configure_bridge(bus, dev);
		if(rc)
			return rc;
	}

	/* Actually configure device */
	if(dev) {
		rc = cpci_configure_dev(bus, dev);
		if(rc)
			return rc;
	}
	dbg("%s - exit", __FUNCTION__);
	return 0;
}

static int unconfigure_visit_pci_dev_phase2(struct pci_dev_wrapped *wrapped_dev,
					    struct pci_bus_wrapped *wrapped_bus)
{
	struct pci_dev *dev = wrapped_dev->dev;
	struct slot* slot;

	dbg("%s - enter", __FUNCTION__);
	if(!dev)
		return -ENODEV;

	/* Remove the Linux representation */
	if(pci_remove_device_safe(dev)) {
		err("Could not remove device\n");
		return -1;
	}

	/*
	 * Now remove the hotplug representation.
	 */
	if(wrapped_dev->data) {
		slot = (struct slot*) wrapped_dev->data;
		slot->dev = NULL;
	} else {
		dbg("No hotplug representation for %02x:%02x.%x",
		    dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	}
	dbg("%s - exit", __FUNCTION__);
	return 0;
}

static int unconfigure_visit_pci_bus_phase2(struct pci_bus_wrapped *wrapped_bus,
					    struct pci_dev_wrapped *wrapped_dev)
{
	struct pci_bus *bus = wrapped_bus->bus;
	struct pci_bus *parent = bus->self->bus;

	dbg("%s - enter", __FUNCTION__);

	/* The cleanup code for proc entries regarding buses should be in the kernel... */
	if(bus->procdir)
		dbg("detach_pci_bus %s", bus->procdir->name);
	pci_proc_detach_bus(bus);

	/* The cleanup code should live in the kernel... */
	bus->self->subordinate = NULL;

	/* unlink from parent bus */
	list_del(&bus->node);

	/* Now, remove */
	if(bus)
		kfree(bus);

	/* Update parent's subordinate field */
	if(parent) {
		u8 n = pci_bus_max_busnr(parent);
		if(n < parent->subordinate) {
			parent->subordinate = n;
			pci_write_config_byte(parent->self, PCI_SUBORDINATE_BUS, n);
		}
	}
	dbg("%s - exit", __FUNCTION__);
	return 0;
}

static struct pci_visit configure_functions = {
	.visit_pci_dev = configure_visit_pci_dev,
};

static struct pci_visit unconfigure_functions_phase2 = {
	.post_visit_pci_bus = unconfigure_visit_pci_bus_phase2,
	.post_visit_pci_dev = unconfigure_visit_pci_dev_phase2
};


int cpci_configure_slot(struct slot* slot)
{
	int rc = 0;

	dbg("%s - enter", __FUNCTION__);

	if(slot->dev == NULL) {
		dbg("pci_dev null, finding %02x:%02x:%x",
		    slot->bus->number, PCI_SLOT(slot->devfn), PCI_FUNC(slot->devfn));
		slot->dev = pci_find_slot(slot->bus->number, slot->devfn);
	}

	/* Still NULL? Well then scan for it! */
	if(slot->dev == NULL) {
		int n;
		dbg("pci_dev still null");

		/*
		 * This will generate pci_dev structures for all functions, but
		 * we will only call this case when lookup fails.
		 */
		n = pci_scan_slot(slot->bus, slot->devfn);
		dbg("%s: pci_scan_slot returned %d", __FUNCTION__, n);
		if(n > 0)
			pci_bus_add_devices(slot->bus);
		slot->dev = pci_find_slot(slot->bus->number, slot->devfn);
		if(slot->dev == NULL) {
			err("Could not find PCI device for slot %02x", slot->number);
			return 0;
		}
	}
	dbg("slot->dev = %p", slot->dev);
	if(slot->dev) {
		struct pci_dev *dev;
		struct pci_dev_wrapped wrapped_dev;
		struct pci_bus_wrapped wrapped_bus;
		int i;

		memset(&wrapped_dev, 0, sizeof (struct pci_dev_wrapped));
		memset(&wrapped_bus, 0, sizeof (struct pci_bus_wrapped));

		for (i = 0; i < 8; i++) {
			dev = pci_find_slot(slot->bus->number,
					    PCI_DEVFN(PCI_SLOT(slot->dev->devfn), i));
			if(!dev)
				continue;
			wrapped_dev.dev = dev;
			wrapped_bus.bus = slot->dev->bus;
			if(i)
				wrapped_dev.data = NULL;
			else
				wrapped_dev.data = (void*) slot;
			rc = pci_visit_dev(&configure_functions, &wrapped_dev, &wrapped_bus);
		}
	}

	dbg("%s - exit, rc = %d", __FUNCTION__, rc);
	return rc;
}

int cpci_unconfigure_slot(struct slot* slot)
{
	int rc = 0;
	int i;
	struct pci_dev_wrapped wrapped_dev;
	struct pci_bus_wrapped wrapped_bus;
	struct pci_dev *dev;

	dbg("%s - enter", __FUNCTION__);

	if(!slot->dev) {
		err("No device for slot %02x\n", slot->number);
		return -ENODEV;
	}

	memset(&wrapped_dev, 0, sizeof (struct pci_dev_wrapped));
	memset(&wrapped_bus, 0, sizeof (struct pci_bus_wrapped));

	for (i = 0; i < 8; i++) {
		dev = pci_find_slot(slot->bus->number,
				    PCI_DEVFN(PCI_SLOT(slot->devfn), i));
		if(dev) {
			wrapped_dev.dev = dev;
			wrapped_bus.bus = dev->bus;
 			if(i)
 				wrapped_dev.data = NULL;
 			else
 				wrapped_dev.data = (void*) slot;
			dbg("%s - unconfigure phase 2", __FUNCTION__);
			rc = pci_visit_dev(&unconfigure_functions_phase2,
					   &wrapped_dev,
					   &wrapped_bus);
			if(rc)
				break;
		}
	}
	dbg("%s - exit, rc = %d", __FUNCTION__, rc);
	return rc;
}
