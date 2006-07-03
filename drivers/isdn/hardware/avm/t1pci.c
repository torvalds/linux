/* $Id: t1pci.c,v 1.1.2.2 2004/01/16 21:09:27 keil Exp $
 * 
 * Module for AVM T1 PCI-card.
 * 
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/capi.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>
#include <linux/isdn/capilli.h>
#include "avmcard.h"

#undef CONFIG_T1PCI_DEBUG
#undef CONFIG_T1PCI_POLLDEBUG

/* ------------------------------------------------------------- */
static char *revision = "$Revision: 1.1.2.2 $";
/* ------------------------------------------------------------- */

static struct pci_device_id t1pci_pci_tbl[] = {
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_T1, PCI_ANY_ID, PCI_ANY_ID },
	{ }				/* Terminating entry */
};

MODULE_DEVICE_TABLE(pci, t1pci_pci_tbl);
MODULE_DESCRIPTION("CAPI4Linux: Driver for AVM T1 PCI card");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------- */

static char *t1pci_procinfo(struct capi_ctr *ctrl);

static int t1pci_add_card(struct capicardparams *p, struct pci_dev *pdev)
{
	avmcard *card;
	avmctrl_info *cinfo;
	int retval;

	card = b1_alloc_card(1);
	if (!card) {
		printk(KERN_WARNING "t1pci: no memory.\n");
		retval = -ENOMEM;
		goto err;
	}

        card->dma = avmcard_dma_alloc("t1pci", pdev, 2048+128, 2048+128);
	if (!card->dma) {
		printk(KERN_WARNING "t1pci: no memory.\n");
		retval = -ENOMEM;
		goto err_free;
	}

	cinfo = card->ctrlinfo;
	sprintf(card->name, "t1pci-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->membase = p->membase;
	card->cardtype = avm_t1pci;

	if (!request_region(card->port, AVMB1_PORTLEN, card->name)) {
		printk(KERN_WARNING "t1pci: ports 0x%03x-0x%03x in use.\n",
		       card->port, card->port + AVMB1_PORTLEN);
		retval = -EBUSY;
		goto err_free_dma;
	}

	card->mbase = ioremap(card->membase, 64);
	if (!card->mbase) {
		printk(KERN_NOTICE "t1pci: can't remap memory at 0x%lx\n",
		       card->membase);
		retval = -EIO;
		goto err_release_region;
	}

	b1dma_reset(card);

	retval = t1pci_detect(card);
	if (retval != 0) {
		if (retval < 6)
			printk(KERN_NOTICE "t1pci: NO card at 0x%x (%d)\n",
			       card->port, retval);
		else
			printk(KERN_NOTICE "t1pci: card at 0x%x, but cable not connected or T1 has no power (%d)\n",
			       card->port, retval);
		retval = -EIO;
		goto err_unmap;
	}
	b1dma_reset(card);

	retval = request_irq(card->irq, b1dma_interrupt, IRQF_SHARED, card->name, card);
	if (retval) {
		printk(KERN_ERR "t1pci: unable to get IRQ %d.\n", card->irq);
		retval = -EBUSY;
		goto err_unmap;
	}

	cinfo->capi_ctrl.owner         = THIS_MODULE;
	cinfo->capi_ctrl.driver_name   = "t1pci";
	cinfo->capi_ctrl.driverdata    = cinfo;
	cinfo->capi_ctrl.register_appl = b1dma_register_appl;
	cinfo->capi_ctrl.release_appl  = b1dma_release_appl;
	cinfo->capi_ctrl.send_message  = b1dma_send_message;
	cinfo->capi_ctrl.load_firmware = b1dma_load_firmware;
	cinfo->capi_ctrl.reset_ctr     = b1dma_reset_ctr;
	cinfo->capi_ctrl.procinfo      = t1pci_procinfo;
	cinfo->capi_ctrl.ctr_read_proc = b1dmactl_read_proc;
	strcpy(cinfo->capi_ctrl.name, card->name);

	retval = attach_capi_ctr(&cinfo->capi_ctrl);
	if (retval) {
		printk(KERN_ERR "t1pci: attach controller failed.\n");
		retval = -EBUSY;
		goto err_free_irq;
	}
	card->cardnr = cinfo->capi_ctrl.cnr;

	printk(KERN_INFO "t1pci: AVM T1 PCI at i/o %#x, irq %d, mem %#lx\n",
	       card->port, card->irq, card->membase);

	pci_set_drvdata(pdev, card);
	return 0;

 err_free_irq:
	free_irq(card->irq, card);
 err_unmap:
	iounmap(card->mbase);
 err_release_region:
	release_region(card->port, AVMB1_PORTLEN);
 err_free_dma:
	avmcard_dma_free(card->dma);
 err_free:
	b1_free_card(card);
 err:
	return retval;
}

/* ------------------------------------------------------------- */

static void t1pci_remove(struct pci_dev *pdev)
{
	avmcard *card = pci_get_drvdata(pdev);
	avmctrl_info *cinfo = card->ctrlinfo;

 	b1dma_reset(card);

	detach_capi_ctr(&cinfo->capi_ctrl);
	free_irq(card->irq, card);
	iounmap(card->mbase);
	release_region(card->port, AVMB1_PORTLEN);
	avmcard_dma_free(card->dma);
	b1_free_card(card);
}

/* ------------------------------------------------------------- */

static char *t1pci_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d 0x%lx",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0,
		cinfo->card ? cinfo->card->membase : 0
		);
	return cinfo->infobuf;
}

/* ------------------------------------------------------------- */

static int __devinit t1pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *ent)
{
	struct capicardparams param;
	int retval;

	if (pci_enable_device(dev) < 0) {
		printk(KERN_ERR	"t1pci: failed to enable AVM-T1-PCI\n");
		return -ENODEV;
	}
	pci_set_master(dev);

	param.port = pci_resource_start(dev, 1);
	param.irq = dev->irq;
	param.membase = pci_resource_start(dev, 0);

	printk(KERN_INFO "t1pci: PCI BIOS reports AVM-T1-PCI at i/o %#x, irq %d, mem %#x\n",
	       param.port, param.irq, param.membase);

	retval = t1pci_add_card(&param, dev);
	if (retval != 0) {
		printk(KERN_ERR "t1pci: no AVM-T1-PCI at i/o %#x, irq %d detected, mem %#x\n",
		       param.port, param.irq, param.membase);
		return -ENODEV;
	}
	return 0;
}

static struct pci_driver t1pci_pci_driver = {
       .name           = "t1pci",
       .id_table       = t1pci_pci_tbl,
       .probe          = t1pci_probe,
       .remove         = t1pci_remove,
};

static struct capi_driver capi_driver_t1pci = {
	.name		= "t1pci",
	.revision	= "1.0",
};

static int __init t1pci_init(void)
{
	char *p;
	char rev[32];
	int err;

	if ((p = strchr(revision, ':')) != 0 && p[1]) {
		strlcpy(rev, p + 2, 32);
		if ((p = strchr(rev, '$')) != 0 && p > rev)
		   *(p-1) = 0;
	} else
		strcpy(rev, "1.0");

	err = pci_register_driver(&t1pci_pci_driver);
	if (!err) {
		strlcpy(capi_driver_t1pci.revision, rev, 32);
		register_capi_driver(&capi_driver_t1pci);
		printk(KERN_INFO "t1pci: revision %s\n", rev);
	}
	return err;
}

static void __exit t1pci_exit(void)
{
	unregister_capi_driver(&capi_driver_t1pci);
	pci_unregister_driver(&t1pci_pci_driver);
}

module_init(t1pci_init);
module_exit(t1pci_exit);
