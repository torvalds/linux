/* Core PCI functionality used only by PCI hotplug */

#include <linux/pci.h>
#include "pci.h"


unsigned int __devinit pci_do_scan_bus(struct pci_bus *bus)
{
	unsigned int max;

	max = pci_scan_child_bus(bus);

	/*
	 * Make the discovered devices available.
	 */
	pci_bus_add_devices(bus);

	return max;
}
EXPORT_SYMBOL(pci_do_scan_bus);
