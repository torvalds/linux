// SPDX-License-Identifier: GPL-2.0
/*
 * Support routines for initializing a PCI subsystem
 *
 * Extruded from code written by
 *      Dave Rusling (david.rusling@reo.mts.dec.com)
 *      David Mosberger (davidm@cs.arizona.edu)
 *	David Miller (davem@redhat.com)
 */


#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/cache.h>
#include "pci.h"

void pci_assign_irq(struct pci_dev *dev)
{
	u8 pin;
	u8 slot = -1;
	int irq = 0;
	struct pci_host_bridge *hbrg = pci_find_host_bridge(dev->bus);

	if (!(hbrg->map_irq)) {
		pci_dbg(dev, "runtime IRQ mapping not provided by arch\n");
		return;
	}

	/* If this device is not on the primary bus, we need to figure out
	   which interrupt pin it will come in on.   We know which slot it
	   will come in on 'cos that slot is where the bridge is.   Each
	   time the interrupt line passes through a PCI-PCI bridge we must
	   apply the swizzle function.  */

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
	/* Cope with illegal. */
	if (pin > 4)
		pin = 1;

	if (pin) {
		/* Follow the chain of bridges, swizzling as we go.  */
		if (hbrg->swizzle_irq)
			slot = (*(hbrg->swizzle_irq))(dev, &pin);

		/*
		 * If a swizzling function is not used map_irq must
		 * ignore slot
		 */
		irq = (*(hbrg->map_irq))(dev, slot, pin);
		if (irq == -1)
			irq = 0;
	}
	dev->irq = irq;

	pci_dbg(dev, "assign IRQ: got %d\n", dev->irq);

	/* Always tell the device, so the driver knows what is
	   the real IRQ to use; the device does not use it. */
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}
