#include <linux/kernel.h>
#include <linux/of_pci.h>
#include <linux/of_irq.h>
#include <asm/prom.h>

/**
 * of_irq_map_pci - Resolve the interrupt for a PCI device
 * @pdev:       the device whose interrupt is to be resolved
 * @out_irq:    structure of_irq filled by this function
 *
 * This function resolves the PCI interrupt for a given PCI device. If a
 * device-node exists for a given pci_dev, it will use normal OF tree
 * walking. If not, it will implement standard swizzling and walk up the
 * PCI tree until an device-node is found, at which point it will finish
 * resolving using the OF tree walking.
 */
int of_irq_map_pci(struct pci_dev *pdev, struct of_irq *out_irq)
{
	struct device_node *dn, *ppnode;
	struct pci_dev *ppdev;
	u32 lspec;
	__be32 lspec_be;
	__be32 laddr[3];
	u8 pin;
	int rc;

	/* Check if we have a device node, if yes, fallback to standard
	 * device tree parsing
	 */
	dn = pci_device_to_OF_node(pdev);
	if (dn) {
		rc = of_irq_map_one(dn, 0, out_irq);
		if (!rc)
			return rc;
	}

	/* Ok, we don't, time to have fun. Let's start by building up an
	 * interrupt spec.  we assume #interrupt-cells is 1, which is standard
	 * for PCI. If you do different, then don't use that routine.
	 */
	rc = pci_read_config_byte(pdev, PCI_INTERRUPT_PIN, &pin);
	if (rc != 0)
		return rc;
	/* No pin, exit */
	if (pin == 0)
		return -ENODEV;

	/* Now we walk up the PCI tree */
	lspec = pin;
	for (;;) {
		/* Get the pci_dev of our parent */
		ppdev = pdev->bus->self;

		/* Ouch, it's a host bridge... */
		if (ppdev == NULL) {
			ppnode = pci_bus_to_OF_node(pdev->bus);

			/* No node for host bridge ? give up */
			if (ppnode == NULL)
				return -EINVAL;
		} else {
			/* We found a P2P bridge, check if it has a node */
			ppnode = pci_device_to_OF_node(ppdev);
		}

		/* Ok, we have found a parent with a device-node, hand over to
		 * the OF parsing code.
		 * We build a unit address from the linux device to be used for
		 * resolution. Note that we use the linux bus number which may
		 * not match your firmware bus numbering.
		 * Fortunately, in most cases, interrupt-map-mask doesn't
		 * include the bus number as part of the matching.
		 * You should still be careful about that though if you intend
		 * to rely on this function (you ship  a firmware that doesn't
		 * create device nodes for all PCI devices).
		 */
		if (ppnode)
			break;

		/* We can only get here if we hit a P2P bridge with no node,
		 * let's do standard swizzling and try again
		 */
		lspec = pci_swizzle_interrupt_pin(pdev, lspec);
		pdev = ppdev;
	}

	lspec_be = cpu_to_be32(lspec);
	laddr[0] = cpu_to_be32((pdev->bus->number << 16) | (pdev->devfn << 8));
	laddr[1]  = laddr[2] = cpu_to_be32(0);
	return of_irq_map_raw(ppnode, &lspec_be, 1, laddr, out_irq);
}
EXPORT_SYMBOL_GPL(of_irq_map_pci);
