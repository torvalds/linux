/* $Id: b1pcmcia.c,v 1.1.2.2 2004/01/16 21:09:27 keil Exp $
 * 
 * Module for AVM B1/M1/M2 PCMCIA-card.
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
#include <linux/init.h>
#include <asm/io.h>
#include <linux/capi.h>
#include <linux/b1pcmcia.h>
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>
#include <linux/isdn/capilli.h>
#include "avmcard.h"

/* ------------------------------------------------------------- */

static char *revision = "$Revision: 1.1.2.2 $";

/* ------------------------------------------------------------- */

MODULE_DESCRIPTION("CAPI4Linux: Driver for AVM PCMCIA cards");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------- */

static void b1pcmcia_remove_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;

	b1_reset(port);
	b1_reset(port);

	detach_capi_ctr(ctrl);
	free_irq(card->irq, card);
	b1_free_card(card);
}

/* ------------------------------------------------------------- */

static LIST_HEAD(cards);

static char *b1pcmcia_procinfo(struct capi_ctr *ctrl);

static int b1pcmcia_add_card(unsigned int port, unsigned irq,
			     enum avmcardtype cardtype)
{
	avmctrl_info *cinfo;
	avmcard *card;
	char *cardname;
	int retval;

	card = b1_alloc_card(1);
	if (!card) {
		printk(KERN_WARNING "b1pcmcia: no memory.\n");
		retval = -ENOMEM;
		goto err;
	}
	cinfo = card->ctrlinfo;

	switch (cardtype) {
		case avm_m1: sprintf(card->name, "m1-%x", port); break;
		case avm_m2: sprintf(card->name, "m2-%x", port); break;
		default: sprintf(card->name, "b1pcmcia-%x", port); break;
	}
	card->port = port;
	card->irq = irq;
	card->cardtype = cardtype;

	retval = request_irq(card->irq, b1_interrupt, IRQF_SHARED, card->name, card);
	if (retval) {
		printk(KERN_ERR "b1pcmcia: unable to get IRQ %d.\n",
		       card->irq);
		retval = -EBUSY;
		goto err_free;
	}
	b1_reset(card->port);
	if ((retval = b1_detect(card->port, card->cardtype)) != 0) {
		printk(KERN_NOTICE "b1pcmcia: NO card at 0x%x (%d)\n",
		       card->port, retval);
		retval = -ENODEV;
		goto err_free_irq;
	}
	b1_reset(card->port);
	b1_getrevision(card);

	cinfo->capi_ctrl.owner         = THIS_MODULE;
	cinfo->capi_ctrl.driver_name   = "b1pcmcia";
	cinfo->capi_ctrl.driverdata    = cinfo;
	cinfo->capi_ctrl.register_appl = b1_register_appl;
	cinfo->capi_ctrl.release_appl  = b1_release_appl;
	cinfo->capi_ctrl.send_message  = b1_send_message;
	cinfo->capi_ctrl.load_firmware = b1_load_firmware;
	cinfo->capi_ctrl.reset_ctr     = b1_reset_ctr;
	cinfo->capi_ctrl.procinfo      = b1pcmcia_procinfo;
	cinfo->capi_ctrl.ctr_read_proc = b1ctl_read_proc;
	strcpy(cinfo->capi_ctrl.name, card->name);

	retval = attach_capi_ctr(&cinfo->capi_ctrl);
	if (retval) {
		printk(KERN_ERR "b1pcmcia: attach controller failed.\n");
		goto err_free_irq;
	}
	switch (cardtype) {
		case avm_m1: cardname = "M1"; break;
		case avm_m2: cardname = "M2"; break;
		default    : cardname = "B1 PCMCIA"; break;
	}

	printk(KERN_INFO "b1pcmcia: AVM %s at i/o %#x, irq %d, revision %d\n",
	       cardname, card->port, card->irq, card->revision);

	list_add(&card->list, &cards);
	return cinfo->capi_ctrl.cnr;

 err_free_irq:
	free_irq(card->irq, card);
 err_free:
	b1_free_card(card);
 err:
	return retval;
}

/* ------------------------------------------------------------- */

static char *b1pcmcia_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d r%d",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0,
		cinfo->card ? cinfo->card->revision : 0
		);
	return cinfo->infobuf;
}

/* ------------------------------------------------------------- */

int b1pcmcia_addcard_b1(unsigned int port, unsigned irq)
{
	return b1pcmcia_add_card(port, irq, avm_b1pcmcia);
}

int b1pcmcia_addcard_m1(unsigned int port, unsigned irq)
{
	return b1pcmcia_add_card(port, irq, avm_m1);
}

int b1pcmcia_addcard_m2(unsigned int port, unsigned irq)
{
	return b1pcmcia_add_card(port, irq, avm_m2);
}

int b1pcmcia_delcard(unsigned int port, unsigned irq)
{
	struct list_head *l;
	avmcard *card;
	
	list_for_each(l, &cards) {
		card = list_entry(l, avmcard, list);
		if (card->port == port && card->irq == irq) {
			b1pcmcia_remove_ctr(&card->ctrlinfo[0].capi_ctrl);
			return 0;
		}
	}
	return -ESRCH;
}

EXPORT_SYMBOL(b1pcmcia_addcard_b1);
EXPORT_SYMBOL(b1pcmcia_addcard_m1);
EXPORT_SYMBOL(b1pcmcia_addcard_m2);
EXPORT_SYMBOL(b1pcmcia_delcard);

static struct capi_driver capi_driver_b1pcmcia = {
	.name		= "b1pcmcia",
	.revision	= "1.0",
};

static int __init b1pcmcia_init(void)
{
	char *p;
	char rev[32];

	if ((p = strchr(revision, ':')) != NULL && p[1]) {
		strlcpy(rev, p + 2, 32);
		if ((p = strchr(rev, '$')) != NULL && p > rev)
		   *(p-1) = 0;
	} else
		strcpy(rev, "1.0");

	strlcpy(capi_driver_b1pcmcia.revision, rev, 32);
	register_capi_driver(&capi_driver_b1pcmcia);
	printk(KERN_INFO "b1pci: revision %s\n", rev);

	return 0;
}

static void __exit b1pcmcia_exit(void)
{
	unregister_capi_driver(&capi_driver_b1pcmcia);
}

module_init(b1pcmcia_init);
module_exit(b1pcmcia_exit);
