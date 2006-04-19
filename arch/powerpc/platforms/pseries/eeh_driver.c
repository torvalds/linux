/*
 * PCI Error Recovery Driver for RPA-compliant PPC64 platform.
 * Copyright (C) 2004, 2005 Linas Vepstas <linas@linas.org>
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
 * Send feedback to <linas@us.ibm.com>
 *
 */
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/pci.h>
#include <asm/eeh.h>
#include <asm/eeh_event.h>
#include <asm/ppc-pci.h>
#include <asm/pci-bridge.h>
#include <asm/prom.h>
#include <asm/rtas.h>


static inline const char * pcid_name (struct pci_dev *pdev)
{
	if (pdev && pdev->dev.driver)
		return pdev->dev.driver->name;
	return "";
}

#ifdef DEBUG
static void print_device_node_tree (struct pci_dn *pdn, int dent)
{
	int i;
	if (!pdn) return;
	for (i=0;i<dent; i++)
		printk(" ");
	printk("dn=%s mode=%x \tcfg_addr=%x pe_addr=%x \tfull=%s\n",
		pdn->node->name, pdn->eeh_mode, pdn->eeh_config_addr,
		pdn->eeh_pe_config_addr, pdn->node->full_name);
	dent += 3;
	struct device_node *pc = pdn->node->child;
	while (pc) {
		print_device_node_tree(PCI_DN(pc), dent);
		pc = pc->sibling;
	}
}
#endif

/** 
 * irq_in_use - return true if this irq is being used 
 */
static int irq_in_use(unsigned int irq)
{
	int rc = 0;
	unsigned long flags;
   struct irq_desc *desc = irq_desc + irq;

	spin_lock_irqsave(&desc->lock, flags);
	if (desc->action)
		rc = 1;
	spin_unlock_irqrestore(&desc->lock, flags);
	return rc;
}

/* ------------------------------------------------------- */
/** eeh_report_error - report an EEH error to each device,
 *  collect up and merge the device responses.
 */

static void eeh_report_error(struct pci_dev *dev, void *userdata)
{
	enum pci_ers_result rc, *res = userdata;
	struct pci_driver *driver = dev->driver;

	dev->error_state = pci_channel_io_frozen;

	if (!driver)
		return;

	if (irq_in_use (dev->irq)) {
		struct device_node *dn = pci_device_to_OF_node(dev);
		PCI_DN(dn)->eeh_mode |= EEH_MODE_IRQ_DISABLED;
		disable_irq_nosync(dev->irq);
	}
	if (!driver->err_handler)
		return;
	if (!driver->err_handler->error_detected)
		return;

	rc = driver->err_handler->error_detected (dev, pci_channel_io_frozen);
	if (*res == PCI_ERS_RESULT_NONE) *res = rc;
	if (*res == PCI_ERS_RESULT_NEED_RESET) return;
	if (*res == PCI_ERS_RESULT_DISCONNECT &&
	     rc == PCI_ERS_RESULT_NEED_RESET) *res = rc;
}

/** eeh_report_reset -- tell this device that the pci slot
 *  has been reset.
 */

static void eeh_report_reset(struct pci_dev *dev, void *userdata)
{
	struct pci_driver *driver = dev->driver;
	struct device_node *dn = pci_device_to_OF_node(dev);

	if (!driver)
		return;

	if ((PCI_DN(dn)->eeh_mode) & EEH_MODE_IRQ_DISABLED) {
		PCI_DN(dn)->eeh_mode &= ~EEH_MODE_IRQ_DISABLED;
		enable_irq(dev->irq);
	}
	if (!driver->err_handler)
		return;
	if (!driver->err_handler->slot_reset)
		return;

	driver->err_handler->slot_reset(dev);
}

static void eeh_report_resume(struct pci_dev *dev, void *userdata)
{
	struct pci_driver *driver = dev->driver;

	dev->error_state = pci_channel_io_normal;

	if (!driver)
		return;
	if (!driver->err_handler)
		return;
	if (!driver->err_handler->resume)
		return;

	driver->err_handler->resume(dev);
}

static void eeh_report_failure(struct pci_dev *dev, void *userdata)
{
	struct pci_driver *driver = dev->driver;

	dev->error_state = pci_channel_io_perm_failure;

	if (!driver)
		return;

	if (irq_in_use (dev->irq)) {
		struct device_node *dn = pci_device_to_OF_node(dev);
		PCI_DN(dn)->eeh_mode |= EEH_MODE_IRQ_DISABLED;
		disable_irq_nosync(dev->irq);
	}
	if (!driver->err_handler)
		return;
	if (!driver->err_handler->error_detected)
		return;
	driver->err_handler->error_detected(dev, pci_channel_io_perm_failure);
}

/* ------------------------------------------------------- */
/**
 * handle_eeh_events -- reset a PCI device after hard lockup.
 *
 * pSeries systems will isolate a PCI slot if the PCI-Host
 * bridge detects address or data parity errors, DMA's
 * occuring to wild addresses (which usually happen due to
 * bugs in device drivers or in PCI adapter firmware).
 * Slot isolations also occur if #SERR, #PERR or other misc
 * PCI-related errors are detected.
 *
 * Recovery process consists of unplugging the device driver
 * (which generated hotplug events to userspace), then issuing
 * a PCI #RST to the device, then reconfiguring the PCI config
 * space for all bridges & devices under this slot, and then
 * finally restarting the device drivers (which cause a second
 * set of hotplug events to go out to userspace).
 */

/**
 * eeh_reset_device() -- perform actual reset of a pci slot
 * Args: bus: pointer to the pci bus structure corresponding
 *            to the isolated slot. A non-null value will
 *            cause all devices under the bus to be removed
 *            and then re-added.
 *     pe_dn: pointer to a "Partionable Endpoint" device node.
 *            This is the top-level structure on which pci
 *            bus resets can be performed.
 */

static int eeh_reset_device (struct pci_dn *pe_dn, struct pci_bus *bus)
{
	int rc;
	if (bus)
		pcibios_remove_pci_devices(bus);

	/* Reset the pci controller. (Asserts RST#; resets config space).
	 * Reconfigure bridges and devices. Don't try to bring the system
	 * up if the reset failed for some reason. */
	rc = rtas_set_slot_reset(pe_dn);
	if (rc)
		return rc;

 	/* New-style config addrs might be shared across multiple devices,
 	 * Walk over all functions on this device */
 	if (pe_dn->eeh_pe_config_addr) {
 		struct device_node *pe = pe_dn->node;
 		pe = pe->parent->child;
 		while (pe) {
 			struct pci_dn *ppe = PCI_DN(pe);
 			if (pe_dn->eeh_pe_config_addr == ppe->eeh_pe_config_addr) {
 				rtas_configure_bridge(ppe);
 				eeh_restore_bars(ppe);
 			}
 			pe = pe->sibling;
 		}
 	} else {
 		rtas_configure_bridge(pe_dn);
 		eeh_restore_bars(pe_dn);
 	}

	/* Give the system 5 seconds to finish running the user-space
	 * hotplug shutdown scripts, e.g. ifdown for ethernet.  Yes, 
	 * this is a hack, but if we don't do this, and try to bring 
	 * the device up before the scripts have taken it down, 
	 * potentially weird things happen.
	 */
	if (bus) {
		ssleep (5);
		pcibios_add_pci_devices(bus);
	}

	return 0;
}

/* The longest amount of time to wait for a pci device
 * to come back on line, in seconds.
 */
#define MAX_WAIT_FOR_RECOVERY 15

struct pci_dn * handle_eeh_events (struct eeh_event *event)
{
	struct device_node *frozen_dn;
	struct pci_dn *frozen_pdn;
	struct pci_bus *frozen_bus;
	int rc = 0;
	enum pci_ers_result result = PCI_ERS_RESULT_NONE;
	const char *pci_str, *drv_str;

	frozen_dn = find_device_pe(event->dn);
	frozen_bus = pcibios_find_pci_bus(frozen_dn);

	if (!frozen_dn) {
		printk(KERN_ERR "EEH: Error: Cannot find partition endpoint for %s\n",
		        pci_name(event->dev));
		return NULL;
	}

	/* There are two different styles for coming up with the PE.
	 * In the old style, it was the highest EEH-capable device
	 * which was always an EADS pci bridge.  In the new style,
	 * there might not be any EADS bridges, and even when there are,
	 * the firmware marks them as "EEH incapable". So another
	 * two-step is needed to find the pci bus.. */
	if (!frozen_bus)
		frozen_bus = pcibios_find_pci_bus (frozen_dn->parent);

	if (!frozen_bus) {
		printk(KERN_ERR "EEH: Cannot find PCI bus for %s\n",
		        frozen_dn->full_name);
		return NULL;
	}

#if 0
	/* We may get "permanent failure" messages on empty slots.
	 * These are false alarms. Empty slots have no child dn. */
	if ((event->state == pci_channel_io_perm_failure) && (frozen_device == NULL))
		return;
#endif

	frozen_pdn = PCI_DN(frozen_dn);
	frozen_pdn->eeh_freeze_count++;

	if (frozen_pdn->pcidev) {
		pci_str = pci_name (frozen_pdn->pcidev);
		drv_str = pcid_name (frozen_pdn->pcidev);
	} else {
		pci_str = pci_name (event->dev);
		drv_str = pcid_name (event->dev);
	}
	
	if (frozen_pdn->eeh_freeze_count > EEH_MAX_ALLOWED_FREEZES)
		goto excess_failures;

	/* If the reset state is a '5' and the time to reset is 0 (infinity)
	 * or is more then 15 seconds, then mark this as a permanent failure.
	 */
	if ((event->state == pci_channel_io_perm_failure) &&
	    ((event->time_unavail <= 0) ||
	     (event->time_unavail > MAX_WAIT_FOR_RECOVERY*1000)))
		goto hard_fail;

	eeh_slot_error_detail(frozen_pdn, 1 /* Temporary Error */);
	printk(KERN_WARNING
	   "EEH: This PCI device has failed %d times since last reboot: %s - %s\n",
		frozen_pdn->eeh_freeze_count, drv_str, pci_str);

	/* Walk the various device drivers attached to this slot through
	 * a reset sequence, giving each an opportunity to do what it needs
	 * to accomplish the reset.  Each child gets a report of the
	 * status ... if any child can't handle the reset, then the entire
	 * slot is dlpar removed and added.
	 */
	pci_walk_bus(frozen_bus, eeh_report_error, &result);

	/* If all device drivers were EEH-unaware, then shut
	 * down all of the device drivers, and hope they
	 * go down willingly, without panicing the system.
	 */
	if (result == PCI_ERS_RESULT_NONE) {
		rc = eeh_reset_device(frozen_pdn, frozen_bus);
		if (rc)
			goto hard_fail;
	}

	/* If any device called out for a reset, then reset the slot */
	if (result == PCI_ERS_RESULT_NEED_RESET) {
		rc = eeh_reset_device(frozen_pdn, NULL);
		if (rc)
			goto hard_fail;
		pci_walk_bus(frozen_bus, eeh_report_reset, NULL);
	}

	/* If all devices reported they can proceed, the re-enable PIO */
	if (result == PCI_ERS_RESULT_CAN_RECOVER) {
		/* XXX Not supported; we brute-force reset the device */
		rc = eeh_reset_device(frozen_pdn, NULL);
		if (rc)
			goto hard_fail;
		pci_walk_bus(frozen_bus, eeh_report_reset, NULL);
	}

	/* Tell all device drivers that they can resume operations */
	pci_walk_bus(frozen_bus, eeh_report_resume, NULL);

	return frozen_pdn;
	
excess_failures:
	/*
	 * About 90% of all real-life EEH failures in the field
	 * are due to poorly seated PCI cards. Only 10% or so are
	 * due to actual, failed cards.
	 */
	printk(KERN_ERR
	   "EEH: PCI device %s - %s has failed %d times \n"
	   "and has been permanently disabled.  Please try reseating\n"
	   "this device or replacing it.\n",
		drv_str, pci_str, frozen_pdn->eeh_freeze_count);
	goto perm_error;

hard_fail:
	printk(KERN_ERR
	   "EEH: Unable to recover from failure of PCI device %s - %s\n"
	   "Please try reseating this device or replacing it.\n",
		drv_str, pci_str);

perm_error:
	eeh_slot_error_detail(frozen_pdn, 2 /* Permanent Error */);

	/* Notify all devices that they're about to go down. */
	pci_walk_bus(frozen_bus, eeh_report_failure, NULL);

	/* Shut down the device drivers for good. */
	pcibios_remove_pci_devices(frozen_bus);

	return NULL;
}

/* ---------- end of file ---------- */
