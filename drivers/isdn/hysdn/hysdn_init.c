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
	{PCI_VENDOR_ID_HYPERCOPE, PCI_DEVICE_ID_HYPERCOPE_PLX, PCI_ANY_ID, PCI_SUBDEVICE_ID_HYPERCOPE_METRO},
	{PCI_VENDOR_ID_HYPERCOPE, PCI_DEVICE_ID_HYPERCOPE_PLX, PCI_ANY_ID, PCI_SUBDEVICE_ID_HYPERCOPE_CHAMP2},
	{PCI_VENDOR_ID_HYPERCOPE, PCI_DEVICE_ID_HYPERCOPE_PLX, PCI_ANY_ID, PCI_SUBDEVICE_ID_HYPERCOPE_ERGO},
	{PCI_VENDOR_ID_HYPERCOPE, PCI_DEVICE_ID_HYPERCOPE_PLX, PCI_ANY_ID, PCI_SUBDEVICE_ID_HYPERCOPE_OLD_ERGO},
	{ }				/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, hysdn_pci_tbl);
MODULE_DESCRIPTION("ISDN4Linux: Driver for HYSDN cards");
MODULE_AUTHOR("Werner Cornelius");
MODULE_LICENSE("GPL");

static char *hysdn_init_revision = "$Revision: 1.6.6.6 $";
static int cardmax;		/* number of found cards */
hysdn_card *card_root = NULL;	/* pointer to first card */

/**********************************************/
/* table assigning PCI-sub ids to board types */
/* the last entry contains all 0              */
/**********************************************/
static struct {
	unsigned short subid;		/* PCI sub id */
	unsigned char cardtyp;		/* card type assigned */
} pci_subid_map[] = {

	{
		PCI_SUBDEVICE_ID_HYPERCOPE_METRO, BD_METRO
	},
	{
		PCI_SUBDEVICE_ID_HYPERCOPE_CHAMP2, BD_CHAMP2
	},
	{
		PCI_SUBDEVICE_ID_HYPERCOPE_ERGO, BD_ERGO
	},
	{
		PCI_SUBDEVICE_ID_HYPERCOPE_OLD_ERGO, BD_ERGO
	},
	{
		0, 0
	}			/* terminating entry */
};


/*********************************************************************/
/* search_cards searches for available cards in the pci config data. */
/* If a card is found, the card structure is allocated and the cards */
/* ressources are reserved. cardmax is incremented.                  */
/*********************************************************************/
static void
search_cards(void)
{
	struct pci_dev *akt_pcidev = NULL;
	hysdn_card *card, *card_last;
	int i;

	card_root = NULL;
	card_last = NULL;
	while ((akt_pcidev = pci_find_device(PCI_VENDOR_ID_HYPERCOPE, PCI_DEVICE_ID_HYPERCOPE_PLX,
					     akt_pcidev)) != NULL) {
		if (pci_enable_device(akt_pcidev))
			continue;

		if (!(card = kzalloc(sizeof(hysdn_card), GFP_KERNEL))) {
			printk(KERN_ERR "HYSDN: unable to alloc device mem \n");
			return;
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
		for (i = 0; pci_subid_map[i].subid; i++)
			if (pci_subid_map[i].subid == card->subsysid) {
				card->brdtype = pci_subid_map[i].cardtyp;
				break;
			}
		if (card->brdtype != BD_NONE) {
			if (ergo_inithardware(card)) {
				printk(KERN_WARNING "HYSDN: card at io 0x%04x already in use\n", card->iobase);
				kfree(card);
				continue;
			}
		} else {
			printk(KERN_WARNING "HYSDN: unknown card id 0x%04x\n", card->subsysid);
			kfree(card);	/* release mem */
			continue;
		}
		cardmax++;
		card->next = NULL;	/*end of chain */
		if (card_last)
			card_last->next = card;		/* pointer to next card */
		else
			card_root = card;
		card_last = card;	/* new chain end */
	}			/* device found */
}				/* search_cards */

/************************************************************************************/
/* free_resources frees the acquired PCI resources and returns the allocated memory */
/************************************************************************************/
static void
free_resources(void)
{
	hysdn_card *card;

	while (card_root) {
		card = card_root;
		if (card->releasehardware)
			card->releasehardware(card);	/* free all hardware resources */
		card_root = card_root->next;	/* remove card from chain */
		kfree(card);	/* return mem */

	}			/* while card_root */
}				/* free_resources */

/**************************************************************************/
/* stop_cards disables (hardware resets) all cards and disables interrupt */
/**************************************************************************/
static void
stop_cards(void)
{
	hysdn_card *card;

	card = card_root;	/* first in chain */
	while (card) {
		if (card->stopcard)
			card->stopcard(card);
		card = card->next;	/* remove card from chain */
	}			/* while card */
}				/* stop_cards */


/****************************************************************************/
/* The module startup and shutdown code. Only compiled when used as module. */
/* Using the driver as module is always advisable, because the booting      */
/* image becomes smaller and the driver code is only loaded when needed.    */
/* Additionally newer versions may be activated without rebooting.          */
/****************************************************************************/

/******************************************************/
/* extract revision number from string for log output */
/******************************************************/
char *
hysdn_getrev(const char *revision)
{
	char *rev;
	char *p;

	if ((p = strchr(revision, ':'))) {
		rev = p + 2;
		p = strchr(rev, '$');
		*--p = 0;
	} else
		rev = "???";
	return rev;
}


/****************************************************************************/
/* init_module is called once when the module is loaded to do all necessary */
/* things like autodetect...                                                */
/* If the return value of this function is 0 the init has been successful   */
/* and the module is added to the list in /proc/modules, otherwise an error */
/* is assumed and the module will not be kept in memory.                    */
/****************************************************************************/
static int __init
hysdn_init(void)
{
	char tmp[50];

	strcpy(tmp, hysdn_init_revision);
	printk(KERN_NOTICE "HYSDN: module Rev: %s loaded\n", hysdn_getrev(tmp));
	strcpy(tmp, hysdn_net_revision);
	printk(KERN_NOTICE "HYSDN: network interface Rev: %s \n", hysdn_getrev(tmp));
	search_cards();
	printk(KERN_INFO "HYSDN: %d card(s) found.\n", cardmax);

	if (hysdn_procconf_init()) {
		free_resources();	/* proc file_sys not created */
		return (-1);
	}
#ifdef CONFIG_HYSDN_CAPI
	if(cardmax > 0) {
		if(hycapi_init()) {
			printk(KERN_ERR "HYCAPI: init failed\n");
			return(-1);
		}
	}
#endif /* CONFIG_HYSDN_CAPI */
	return (0);		/* no error */
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
#ifdef CONFIG_HYSDN_CAPI
	hysdn_card *card;
#endif /* CONFIG_HYSDN_CAPI */
	stop_cards();
#ifdef CONFIG_HYSDN_CAPI
	card = card_root;	/* first in chain */
	while (card) {
		hycapi_capi_release(card);
		card = card->next;	/* remove card from chain */
	}			/* while card */
	hycapi_cleanup();
#endif /* CONFIG_HYSDN_CAPI */
	hysdn_procconf_release();
	free_resources();
	printk(KERN_NOTICE "HYSDN: module unloaded\n");
}				/* cleanup_module */

module_init(hysdn_init);
module_exit(hysdn_exit);
