// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimalist driver for a generic PCI-to-EISA bridge.
 *
 * (C) 2003 Marc Zyngier <maz@wild-wind.fr.eu.org>
 *
 * Ivan Kokshaysky <ink@jurassic.park.msu.ru> :
 * Generalisation from i82375 to PCI_CLASS_BRIDGE_EISA.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/eisa.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <linux/init.h>

/* There is only *one* pci_eisa device per machine, right ? */
static struct eisa_root_device pci_eisa_root;

static int __init pci_eisa_init(struct pci_dev *pdev)
{
	struct resource *res, *bus_res = NULL;
	int rc;

	if ((rc = pci_enable_device (pdev))) {
		dev_err(&pdev->dev, "Could not enable device\n");
		return rc;
	}

	/*
	 * The Intel 82375 PCI-EISA bridge is a subtractive-decode PCI
	 * device, so the resources available on EISA are the same as those
	 * available on the 82375 bus.  This works the same as a PCI-PCI
	 * bridge in subtractive-decode mode (see pci_read_bridge_bases()).
	 * We assume other PCI-EISA bridges are similar.
	 *
	 * eisa_root_register() can only deal with a single io port resource,
	*  so we use the first valid io port resource.
	 */
	pci_bus_for_each_resource(pdev->bus, res)
		if (res && (res->flags & IORESOURCE_IO)) {
			bus_res = res;
			break;
		}

	if (!bus_res) {
		dev_err(&pdev->dev, "No resources available\n");
		return -1;
	}

	pci_eisa_root.dev		= &pdev->dev;
	pci_eisa_root.res		= bus_res;
	pci_eisa_root.bus_base_addr	= bus_res->start;
	pci_eisa_root.slots		= EISA_MAX_SLOTS;
	pci_eisa_root.dma_mask		= pdev->dma_mask;
	dev_set_drvdata(pci_eisa_root.dev, &pci_eisa_root);

	if (eisa_root_register (&pci_eisa_root)) {
		dev_err(&pdev->dev, "Could not register EISA root\n");
		return -1;
	}

	return 0;
}

/*
 * We have to call pci_eisa_init_early() before pnpacpi_init()/isapnp_init().
 *   Otherwise pnp resource will get enabled early and could prevent eisa
 *   to be initialized.
 * Also need to make sure pci_eisa_init_early() is called after
 * x86/pci_subsys_init().
 * So need to use subsys_initcall_sync with it.
 */
static int __init pci_eisa_init_early(void)
{
	struct pci_dev *dev = NULL;
	int ret;

	for_each_pci_dev(dev)
		if ((dev->class >> 8) == PCI_CLASS_BRIDGE_EISA) {
			ret = pci_eisa_init(dev);
			if (ret)
				return ret;
		}

	return 0;
}
subsys_initcall_sync(pci_eisa_init_early);
