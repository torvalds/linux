/*
 * PCI IRQ handling code
 *
 * Copyright (c) 2008 James Bottomley <James.Bottomley@HansenPartnership.com>
 * Copyright (C) 2017 Christoph Hellwig.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/pci.h>

static void pci_note_irq_problem(struct pci_dev *pdev, const char *reason)
{
	struct pci_dev *parent = to_pci_dev(pdev->dev.parent);

	dev_err(&pdev->dev,
		"Potentially misrouted IRQ (Bridge %s %04x:%04x)\n",
		dev_name(&parent->dev), parent->vendor, parent->device);
	dev_err(&pdev->dev, "%s\n", reason);
	dev_err(&pdev->dev, "Please report to linux-kernel@vger.kernel.org\n");
	WARN_ON(1);
}

/**
 * pci_lost_interrupt - reports a lost PCI interrupt
 * @pdev:	device whose interrupt is lost
 *
 * The primary function of this routine is to report a lost interrupt
 * in a standard way which users can recognise (instead of blaming the
 * driver).
 *
 * Returns:
 *  a suggestion for fixing it (although the driver is not required to
 * act on this).
 */
enum pci_lost_interrupt_reason pci_lost_interrupt(struct pci_dev *pdev)
{
	if (pdev->msi_enabled || pdev->msix_enabled) {
		enum pci_lost_interrupt_reason ret;

		if (pdev->msix_enabled) {
			pci_note_irq_problem(pdev, "MSIX routing failure");
			ret = PCI_LOST_IRQ_DISABLE_MSIX;
		} else {
			pci_note_irq_problem(pdev, "MSI routing failure");
			ret = PCI_LOST_IRQ_DISABLE_MSI;
		}
		return ret;
	}
#ifdef CONFIG_ACPI
	if (!(acpi_disabled || acpi_noirq)) {
		pci_note_irq_problem(pdev, "Potential ACPI misrouting please reboot with acpi=noirq");
		/* currently no way to fix acpi on the fly */
		return PCI_LOST_IRQ_DISABLE_ACPI;
	}
#endif
	pci_note_irq_problem(pdev, "unknown cause (not MSI or ACPI)");
	return PCI_LOST_IRQ_NO_INFORMATION;
}
EXPORT_SYMBOL(pci_lost_interrupt);

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

	va_start(ap, fmt);
	devname = kvasprintf(GFP_KERNEL, fmt, ap);
	va_end(ap);

	ret = request_threaded_irq(pci_irq_vector(dev, nr), handler, thread_fn,
			IRQF_SHARED, devname, dev_id);
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
