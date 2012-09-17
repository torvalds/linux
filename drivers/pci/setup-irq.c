/*
 *	drivers/pci/setup-irq.c
 *
 * Extruded from code written by
 *      Dave Rusling (david.rusling@reo.mts.dec.com)
 *      David Mosberger (davidm@cs.arizona.edu)
 *	David Miller (davem@redhat.com)
 *
 * Support routines for initializing a PCI subsystem.
 */


#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/cache.h>

void __weak pcibios_update_irq(struct pci_dev *dev, int irq)
{
	dev_dbg(&dev->dev, "assigning IRQ %02d\n", irq);
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);
}

static void
pdev_fixup_irq(struct pci_dev *dev,
	       u8 (*swizzle)(struct pci_dev *, u8 *),
	       int (*map_irq)(const struct pci_dev *, u8, u8))
{
	u8 pin, slot;
	int irq = 0;

	/* If this device is not on the primary bus, we need to figure out
	   which interrupt pin it will come in on.   We know which slot it
	   will come in on 'cos that slot is where the bridge is.   Each
	   time the interrupt line passes through a PCI-PCI bridge we must
	   apply the swizzle function.  */

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
	/* Cope with illegal. */
	if (pin > 4)
		pin = 1;

	if (pin != 0) {
		/* Follow the chain of bridges, swizzling as we go.  */
		slot = (*swizzle)(dev, &pin);

		irq = (*map_irq)(dev, slot, pin);
		if (irq == -1)
			irq = 0;
	}
	dev->irq = irq;

	dev_dbg(&dev->dev, "fixup irq: got %d\n", dev->irq);

	/* Always tell the device, so the driver knows what is
	   the real IRQ to use; the device does not use it. */
	pcibios_update_irq(dev, irq);
}

void
pci_fixup_irqs(u8 (*swizzle)(struct pci_dev *, u8 *),
	       int (*map_irq)(const struct pci_dev *, u8, u8))
{
	struct pci_dev *dev = NULL;
	for_each_pci_dev(dev)
		pdev_fixup_irq(dev, swizzle, map_irq);
}
