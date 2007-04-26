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

static int __init pci_eisa_init(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	int rc;

	if ((rc = pci_enable_device (pdev))) {
		printk (KERN_ERR "pci_eisa : Could not enable device %s\n",
			pci_name(pdev));
		return rc;
	}

	pci_eisa_root.dev              = &pdev->dev;
	pci_eisa_root.dev->driver_data = &pci_eisa_root;
	pci_eisa_root.res	       = pdev->bus->resource[0];
	pci_eisa_root.bus_base_addr    = pdev->bus->resource[0]->start;
	pci_eisa_root.slots	       = EISA_MAX_SLOTS;
	pci_eisa_root.dma_mask         = pdev->dma_mask;

	if (eisa_root_register (&pci_eisa_root)) {
		printk (KERN_ERR "pci_eisa : Could not register EISA root\n");
		return -1;
	}

	return 0;
}

static struct pci_device_id pci_eisa_pci_tbl[] = {
	{ PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID, PCI_ANY_ID,
	  PCI_CLASS_BRIDGE_EISA << 8, 0xffff00, 0 },
	{ 0, }
};

static struct pci_driver pci_eisa_driver = {
	.name		= "pci_eisa",
	.id_table	= pci_eisa_pci_tbl,
	.probe		= pci_eisa_init,
};

static int __init pci_eisa_init_module (void)
{
	return pci_register_driver (&pci_eisa_driver);
}

device_initcall(pci_eisa_init_module);
MODULE_DEVICE_TABLE(pci, pci_eisa_pci_tbl);
