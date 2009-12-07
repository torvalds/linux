#include <linux/init.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include "pci.h"

/**
 * pci_find_device - begin or continue searching for a PCI device by vendor/device id
 * @vendor: PCI vendor id to match, or %PCI_ANY_ID to match all vendor ids
 * @device: PCI device id to match, or %PCI_ANY_ID to match all device ids
 * @from: Previous PCI device found in search, or %NULL for new search.
 *
 * Iterates through the list of known PCI devices.  If a PCI device is found
 * with a matching @vendor and @device, a pointer to its device structure is
 * returned.  Otherwise, %NULL is returned.
 * A new search is initiated by passing %NULL as the @from argument.
 * Otherwise if @from is not %NULL, searches continue from next device
 * on the global list.
 *
 * NOTE: Do not use this function any more; use pci_get_device() instead, as
 * the PCI device returned by this function can disappear at any moment in
 * time.
 */
struct pci_dev *pci_find_device(unsigned int vendor, unsigned int device,
				struct pci_dev *from)
{
	struct pci_dev *pdev;

	pci_dev_get(from);
	pdev = pci_get_subsys(vendor, device, PCI_ANY_ID, PCI_ANY_ID, from);
	pci_dev_put(pdev);
	return pdev;
}
EXPORT_SYMBOL(pci_find_device);
