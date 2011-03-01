#undef DEBUG

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/pci_regs.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/etherdevice.h>
#include <linux/of_address.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>

#ifdef CONFIG_PCI
int of_irq_map_pci(struct pci_dev *pdev, struct of_irq *out_irq)
{
	struct device_node *dn, *ppnode;
	struct pci_dev *ppdev;
	u32 lspec;
	u32 laddr[3];
	u8 pin;
	int rc;

	/* Check if we have a device node, if yes, fallback to standard OF
	 * parsing
	 */
	dn = pci_device_to_OF_node(pdev);
	if (dn)
		return of_irq_map_one(dn, 0, out_irq);

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
			struct pci_controller *host;
			host = pci_bus_to_host(pdev->bus);
			ppnode = host ? host->dn : NULL;
			/* No node for host bridge ? give up */
			if (ppnode == NULL)
				return -EINVAL;
		} else
			/* We found a P2P bridge, check if it has a node */
			ppnode = pci_device_to_OF_node(ppdev);

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

	laddr[0] = (pdev->bus->number << 16)
		| (pdev->devfn << 8);
	laddr[1]  = laddr[2] = 0;
	return of_irq_map_raw(ppnode, &lspec, 1, laddr, out_irq);
}
EXPORT_SYMBOL_GPL(of_irq_map_pci);
#endif /* CONFIG_PCI */

void of_parse_dma_window(struct device_node *dn, const void *dma_window_prop,
		unsigned long *busno, unsigned long *phys, unsigned long *size)
{
	const u32 *dma_window;
	u32 cells;
	const unsigned char *prop;

	dma_window = dma_window_prop;

	/* busno is always one cell */
	*busno = *(dma_window++);

	prop = of_get_property(dn, "ibm,#dma-address-cells", NULL);
	if (!prop)
		prop = of_get_property(dn, "#address-cells", NULL);

	cells = prop ? *(u32 *)prop : of_n_addr_cells(dn);
	*phys = of_read_number(dma_window, cells);

	dma_window += cells;

	prop = of_get_property(dn, "ibm,#dma-size-cells", NULL);
	cells = prop ? *(u32 *)prop : of_n_size_cells(dn);
	*size = of_read_number(dma_window, cells);
}
