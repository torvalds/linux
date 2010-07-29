/* $Id: avm_cs.c,v 1.4.6.3 2001/09/23 22:24:33 kai Exp $
 *
 * A PCMCIA client driver for AVM B1/M1/M2
 *
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/major.h>
#include <asm/io.h>
#include <asm/system.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ciscode.h>
#include <pcmcia/ds.h>
#include <pcmcia/cisreg.h>

#include <linux/skbuff.h>
#include <linux/capi.h>
#include <linux/b1lli.h>
#include <linux/b1pcmcia.h>

/*====================================================================*/

MODULE_DESCRIPTION("CAPI4Linux: PCMCIA client driver for AVM B1/M1/M2");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

/*====================================================================*/

/*
   The event() function is this driver's Card Services event handler.
   It will be called by Card Services when an appropriate card status
   event is received.  The config() and release() entry points are
   used to configure or release a socket, in response to card insertion
   and ejection events.  They are invoked from the skeleton event
   handler.
*/

static int avmcs_config(struct pcmcia_device *link);
static void avmcs_release(struct pcmcia_device *link);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static void avmcs_detach(struct pcmcia_device *p_dev);

/*======================================================================

    avmcs_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

    The dev_link structure is initialized, but we don't actually
    configure the card at this point -- we wait until we receive a
    card insertion event.
    
======================================================================*/

static int avmcs_probe(struct pcmcia_device *p_dev)
{

    /* The io structure describes IO port mapping */
    p_dev->resource[0]->end = 16;
    p_dev->resource[0]->flags |= IO_DATA_PATH_WIDTH_8;

    /* General socket configuration */
    p_dev->config_flags |= CONF_ENABLE_IRQ;
    p_dev->config_index = 1;
    p_dev->config_regs = PRESENT_OPTION;

    return avmcs_config(p_dev);
} /* avmcs_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void avmcs_detach(struct pcmcia_device *link)
{
	avmcs_release(link);
} /* avmcs_detach */

/*======================================================================

    avmcs_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    ethernet device available to the system.
    
======================================================================*/

static int avmcs_configcheck(struct pcmcia_device *p_dev,
			     cistpl_cftable_entry_t *cf,
			     cistpl_cftable_entry_t *dflt,
			     unsigned int vcc,
			     void *priv_data)
{
	if (cf->io.nwin <= 0)
		return -ENODEV;

	p_dev->resource[0]->start = cf->io.win[0].base;
	p_dev->resource[0]->end = cf->io.win[0].len;
	return pcmcia_request_io(p_dev);
}

static int avmcs_config(struct pcmcia_device *link)
{
    int i = -1;
    char devname[128];
    int cardtype;
    int (*addcard)(unsigned int port, unsigned irq);

    devname[0] = 0;
    if (link->prod_id[1])
	    strlcpy(devname, link->prod_id[1], sizeof(devname));

    /*
     * find IO port
     */
    if (pcmcia_loop_config(link, avmcs_configcheck, NULL))
	    return -ENODEV;

    do {
	if (!link->irq) {
	    /* undo */
	    pcmcia_disable_device(link);
	    break;
	}

	/*
         * configure the PCMCIA socket
	  */
	i = pcmcia_enable_device(link);
	if (i != 0) {
	    pcmcia_disable_device(link);
	    break;
	}

    } while (0);

    if (devname[0]) {
	char *s = strrchr(devname, ' ');
	if (!s)
	   s = devname;
	else s++;
        if (strcmp("M1", s) == 0) {
           cardtype = AVM_CARDTYPE_M1;
        } else if (strcmp("M2", s) == 0) {
           cardtype = AVM_CARDTYPE_M2;
	} else {
           cardtype = AVM_CARDTYPE_B1;
	}
    } else
        cardtype = AVM_CARDTYPE_B1;

    /* If any step failed, release any partially configured state */
    if (i != 0) {
	avmcs_release(link);
	return -ENODEV;
    }


    switch (cardtype) {
        case AVM_CARDTYPE_M1: addcard = b1pcmcia_addcard_m1; break;
        case AVM_CARDTYPE_M2: addcard = b1pcmcia_addcard_m2; break;
	default:
        case AVM_CARDTYPE_B1: addcard = b1pcmcia_addcard_b1; break;
    }
    if ((i = (*addcard)(link->resource[0]->start, link->irq)) < 0) {
	    dev_err(&link->dev,
		    "avm_cs: failed to add AVM-Controller at i/o %#x, irq %d\n",
		    (unsigned int) link->resource[0]->start, link->irq);
	    avmcs_release(link);
	    return -ENODEV;
    }
    return 0;

} /* avmcs_config */

/*======================================================================

    After a card is removed, avmcs_release() will unregister the net
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.
    
======================================================================*/

static void avmcs_release(struct pcmcia_device *link)
{
	b1pcmcia_delcard(link->resource[0]->start, link->irq);
	pcmcia_disable_device(link);
} /* avmcs_release */


static struct pcmcia_device_id avmcs_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("AVM", "ISDN-Controller B1", 0x95d42008, 0x845dc335),
	PCMCIA_DEVICE_PROD_ID12("AVM", "Mobile ISDN-Controller M1", 0x95d42008, 0x81e10430),
	PCMCIA_DEVICE_PROD_ID12("AVM", "Mobile ISDN-Controller M2", 0x95d42008, 0x18e8558a),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, avmcs_ids);

static struct pcmcia_driver avmcs_driver = {
	.owner	= THIS_MODULE,
	.drv	= {
		.name	= "avm_cs",
	},
	.probe = avmcs_probe,
	.remove	= avmcs_detach,
	.id_table = avmcs_ids,
};

static int __init avmcs_init(void)
{
	return pcmcia_register_driver(&avmcs_driver);
}

static void __exit avmcs_exit(void)
{
	pcmcia_unregister_driver(&avmcs_driver);
}

module_init(avmcs_init);
module_exit(avmcs_exit);
