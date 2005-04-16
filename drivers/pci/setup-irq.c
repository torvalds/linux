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


static void __init
pdev_fixup_irq(struct pci_dev *dev,
	       u8 (*swizzle)(struct pci_dev *, u8 *),
	       int (*map_irq)(struct pci_dev *, u8, u8))
{
	u8 pin, slot;
	int irq;

	/* If this device is not on the primary bus, we need to figure out
	   which interrupt pin it will come in on.   We know which slot it
	   will come in on 'cos that slot is where the bridge is.   Each
	   time the interrupt line passes through a PCI-PCI bridge we must
	   apply the swizzle function.  */

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
	/* Cope with 0 and illegal. */
	if (pin == 0 || pin > 4)
		pin = 1;

	/* Follow the chain of bridges, swizzling as we go.  */
	slot = (*swizzle)(dev, &pin);

	irq = (*map_irq)(dev, slot, pin);
	if (irq == -1)
		irq = 0;
	dev->irq = irq;

	pr_debug("PCI: fixup irq: (%s) got %d\n",
		dev->dev.kobj.name, dev->irq);

	/* Always tell the device, so the driver knows what is
	   the real IRQ to use; the device does not use it. */
	pcibios_update_irq(dev, irq);
}

void __init
pci_fixup_irqs(u8 (*swizzle)(struct pci_dev *, u8 *),
	       int (*map_irq)(struct pci_dev *, u8, u8))
{
	struct pci_dev *dev = NULL;
	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL) {
		pdev_fixup_irq(dev, swizzle, map_irq);
	}
}
