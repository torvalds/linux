// SPDX-License-Identifier: GPL-2.0
/*
 * PCI IRQ handling code
 *
 * Copyright (c) 2008 James Bottomley <James.Bottomley@HansenPartnership.com>
 * Copyright (C) 2017 Christoph Hellwig.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/pci.h>

/**
 * pci_request_irq - allocate an interrupt line for a PCI device
 * @dev:	PCI device to operate on
 * @nr:		device-relative interrupt vector index (0-based).
 * @handler:	Function to be called when the IRQ occurs.
 *		Primary handler for threaded interrupts.
 *		If NULL and thread_fn != NULL the default primary handler is
 *		installed.
 * @thread_fn:	Function called from the IRQ handler thread
 *		If NULL, no IRQ thread is created
 * @dev_id:	Cookie passed back to the handler function
 * @fmt:	Printf-like format string naming the handler
 *
 * This call allocates interrupt resources and enables the interrupt line and
 * IRQ handling. From the point this call is made @handler and @thread_fn may
 * be invoked.  All interrupts requested using this function might be shared.
 *
 * @dev_id must not be NULL and must be globally unique.
 */
int pci_request_irq(struct pci_dev *dev, unsigned int nr, irq_handler_t handler,
		irq_handler_t thread_fn, void *dev_id, const char *fmt, ...)
{
	va_list ap;
	int ret;
	char *devname;
	unsigned long irqflags = IRQF_SHARED;

	if (!handler)
		irqflags |= IRQF_ONESHOT;

	va_start(ap, fmt);
	devname = kvasprintf(GFP_KERNEL, fmt, ap);
	va_end(ap);
	if (!devname)
		return -ENOMEM;

	ret = request_threaded_irq(pci_irq_vector(dev, nr), handler, thread_fn,
				   irqflags, devname, dev_id);
	if (ret)
		kfree(devname);
	return ret;
}
EXPORT_SYMBOL(pci_request_irq);

/**
 * pci_free_irq - free an interrupt allocated with pci_request_irq
 * @dev:	PCI device to operate on
 * @nr:		device-relative interrupt vector index (0-based).
 * @dev_id:	Device identity to free
 *
 * Remove an interrupt handler. The handler is removed and if the interrupt
 * line is no longer in use by any driver it is disabled.  The caller must
 * ensure the interrupt is disabled on the device before calling this function.
 * The function does not return until any executing interrupts for this IRQ
 * have completed.
 *
 * This function must not be called from interrupt context.
 */
void pci_free_irq(struct pci_dev *dev, unsigned int nr, void *dev_id)
{
	kfree(free_irq(pci_irq_vector(dev, nr), dev_id));
}
EXPORT_SYMBOL(pci_free_irq);
