/* $Id: hysdn_init.c,v 1.6.6.6 2001/09/23 22:24:54 kai Exp $
 *
 * Linux driver for HYSDN cards, init functions.
 *
 * Author    Werner Cornelius (werner@titro.de) for Hypercope GmbH
 * Copyright 1999 by Werner Cornelius (werner@titro.de)
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/pci.h>

#include "hysdn_defs.h"

static struct pci_device_id hysdn_pci_tbl[] = {
	{ PCI_VENDOR_ID_HYPERCOPE, PCI_DEVICE_ID_HYPERCOPE_PLX,
	  PCI_ANY_ID, PCI_SUBDEVICE_ID_HYPERCOPE_METRO, 0, 0, BD_METRO },
	{ PCI_VENDOR_ID_HYPERCOPE, PCI_DEVICE_ID_HYPERCOPE_PLX,
	  PCI_ANY_ID, PCI_SUBDEVICE_ID_HYPERCOPE_CHAMP2, 0, 0, BD_CHAMP2 },
	{ PCI_VENDOR_ID_HYPERCOPE, PCI_DEVICE_ID_HYPERCOPE_PLX,
	  PCI_ANY_ID, PCI_SUBDEVICE_ID_HYPERCOPE_ERGO, 0, 0, BD_ERGO },
	{ PCI_VENDOR_ID_HYPERCOPE, PCI_DEVICE_ID_HYPERCOPE_PLX,
	  PCI_ANY_ID, PCI_SUBDEVICE_ID_HYPERCOPE_OLD_ERGO, 0, 0, BD_ERGO },

	{ }				/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, hysdn_pci_tbl);
MODULE_DESCRIPTION("ISDN4Linux: Driver for HYSDN cards");
MODULE_AUTHOR("Werner Cornelius");
MODULE_LICENSE("GPL");

static int cardmax;		/* number of found cards */
hysdn_card *card_root = NULL;	/* pointer to first card */
static hysdn_card *card_last = NULL;	/* pointer to first card */


/****************************************************************************/
/* The module startup and shutdown code. Only compiled when used as module. */
/* Using the driver as module is always advisable, because the booting      */
/* image becomes smaller and the driver code is only loaded when needed.    */
/* Additionally newer versions may be activated without rebooting.          */
/****************************************************************************/

/****************************************************************************/
/* init_module is called once when the module is loaded to do all necessary */
/* things like autodetect...                                                */
/* If the return value of this function is 0 the init has been successful   */
/* and the module is added to the list in /proc/modules, otherwise an error */
/* is assumed and the module will not be kept in memory.                    */
/****************************************************************************/

static int hysdn_pci_init_one(struct pci_dev *akt_pcidev,
			      const struct pci_device_id *ent)
{
	hysdn_card *card;
	int rc;

	rc = pci_enable_device(akt_pcidev);
	if (rc)
		return rc;

	if (!(card = kzalloc(sizeof(hysdn_card), GFP_KERNEL))) {
		printk(KERN_ERR "HYSDN: unable to alloc device mem \n");
		rc = -ENOMEM;
		goto err_out;
	}
	card->myid = cardmax;	/* set own id */
	card->bus = akt_pcidev->bus->number;
	card->devfn = akt_pcidev->devfn;	/* slot + function */
	card->subsysid = akt_pcidev->subsystem_device;
	card->irq = akt_pcidev->irq;
	card->iobase = pci_resource_start(akt_pcidev, PCI_REG_PLX_IO_BASE);
	card->plxbase = pci_resource_start(akt_pcidev, PCI_REG_PLX_MEM_BASE);
	card->membase = pci_resource_start(akt_pcidev, PCI_REG_MEMORY_BASE);
	card->brdtype = BD_NONE;	/* unknown */
	card->debug_flags = DEF_DEB_FLAGS;	/* set default debug */
	card->faxchans = 0;	/* default no fax channels */
	card->bchans = 2;	/* and 2 b-channels */
	card->brdtype = ent->driver_data;

	if (ergo_inithardware(card)) {
		printk(KERN_WARNING "HYSDN: card at io 0x%04x already in use\n", card->iobase);
		rc = -EBUSY;
		goto err_out_card;
	}

	cardmax++;
	card->next = NULL;	/*end of chain */
	if (card_last)
		card_last->next = card;		/* pointer to next card */
	else
		card_root = card;
	card_last = card;	/* new chain end */

	pci_set_drvdata(akt_pcidev, card);
	return 0;

err_out_card:
	kfree(card);
err_out:
	pci_disable_device(akt_pcidev);
	return rc;
}

static void hysdn_pci_remove_one(struct pci_dev *akt_pcidev)
{
	hysdn_card *card = pci_get_drvdata(akt_pcidev);

	pci_set_drvdata(akt_pcidev, NULL);

	if (card->stopcard)
		card->stopcard(card);

#ifdef CONFIG_HYSDN_CAPI
	hycapi_capi_release(card);
#endif

	if (card->releasehardware)
		card->releasehardware(card);   /* free all hardware resources */

	if (card == card_root) {
		card_root = card_root->next;
		if (!card_root)
			card_last = NULL;
	} else {
		hysdn_card *tmp = card_root;
		while (tmp) {
			if (tmp->next == card)
				tmp->next = card->next;
			card_last = tmp;
			tmp = tmp->next;
		}
	}

	kfree(card);
	pci_disable_device(akt_pcidev);
}

static struct pci_driver hysdn_pci_driver = {
	.name		= "hysdn",
	.id_table	= hysdn_pci_tbl,
	.probe		= hysdn_pci_init_one,
	.remove		= hysdn_pci_remove_one,
};

static int hysdn_have_procfs;

static int __init
hysdn_init(void)
{
	int rc;

	printk(KERN_NOTICE "HYSDN: module loaded\n");

	rc = pci_register_driver(&hysdn_pci_driver);
	if (rc)
		return rc;

	printk(KERN_INFO "HYSDN: %d card(s) found.\n", cardmax);

	if (!hysdn_procconf_init())
		hysdn_have_procfs = 1;

#ifdef CONFIG_HYSDN_CAPI
	if (cardmax > 0) {
		if (hycapi_init()) {
			printk(KERN_ERR "HYCAPI: init failed\n");

			if (hysdn_have_procfs)
				hysdn_procconf_release();

			pci_unregister_driver(&hysdn_pci_driver);
			return -ESPIPE;
		}
	}
#endif /* CONFIG_HYSDN_CAPI */

	return 0;		/* no error */
}				/* init_module */


/***********************************************************************/
/* cleanup_module is called when the module is released by the kernel. */
/* The routine is only called if init_module has been successful and   */
/* the module counter has a value of 0. Otherwise this function will   */
/* not be called. This function must release all resources still allo- */
/* cated as after the return from this function the module code will   */
/* be removed from memory.                                             */
/***********************************************************************/
static void __exit
hysdn_exit(void)
{
	if (hysdn_have_procfs)
		hysdn_procconf_release();

	pci_unregister_driver(&hysdn_pci_driver);

#ifdef CONFIG_HYSDN_CAPI
	hycapi_cleanup();
#endif /* CONFIG_HYSDN_CAPI */

	printk(KERN_NOTICE "HYSDN: module unloaded\n");
}				/* cleanup_module */

module_init(hysdn_init);
module_exit(hysdn_exit);
