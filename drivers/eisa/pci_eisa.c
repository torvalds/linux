/*
 * Minimalist driver for a generic PCI-to-EISA bridge.
 *
 * (C) 2003 Marc Zyngier <maz@wild-wind.fr.eu.org>
 *
 * This code is released under the GPL version 2.
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
	int rc;

	if ((rc = pci_enable_device (pdev))) {
		printk (KERN_ERR "pci_eisa : Could not enable device %s\n",
			pci_name(pdev));
		return rc;
	}

	pci_eisa_root.dev              = &pdev->dev;
	pci_eisa_root.res	       = pdev->bus->resource[0];
	pci_eisa_root.bus_base_addr    = pdev->bus->resource[0]->start;
	pci_eisa_root.slots	       = EISA_MAX_SLOTS;
	pci_eisa_root.dma_mask         = pdev->dma_mask;
	dev_set_drvdata(pci_eisa_root.dev, &pci_eisa_root);

	if (eisa_root_register (&pci_eisa_root)) {
		printk (KERN_ERR "pci_eisa : Could not register EISA root\n");
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
