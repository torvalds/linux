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
	int rc, i;
	struct resource *res, *bus_res = NULL;

	if ((rc = pci_enable_device (pdev))) {
		printk (KERN_ERR "pci_eisa : Could not enable device %s\n",
			pci_name(pdev));
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
	pci_bus_for_each_resource(pdev->bus, res, i)
		if (res && (res->flags & IORESOURCE_IO)) {
			bus_res = res;
			break;
		}

	if (!bus_res) {
		dev_err(&pdev->dev, "No resources available\n");
		return -1;
	}

	pci_eisa_root.dev              = &pdev->dev;
	pci_eisa_root.res	       = bus_res;
	pci_eisa_root.bus_base_addr    = bus_res->start;
	pci_eisa_root.slots	       = EISA_MAX_SLOTS;
	pci_eisa_root.dma_mask         = pdev->dma_mask;
	dev_set_drvdata(pci_eisa_root.dev, &pci_eisa_root);

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

static struct pci_driver __refdata pci_eisa_driver = {
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
