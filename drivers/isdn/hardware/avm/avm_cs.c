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
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/major.h>
#include <asm/io.h>
#include <asm/system.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
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

static void avmcs_config(dev_link_t *link);
static void avmcs_release(dev_link_t *link);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static void avmcs_detach(struct pcmcia_device *p_dev);

/*
   A linked list of "instances" of the skeleton device.  Each actual
   PCMCIA card corresponds to one device instance, and is described
   by one dev_link_t structure (defined in ds.h).

   You may not want to use a linked list for this -- for example, the
   memory card driver uses an array of dev_link_t pointers, where minor
   device numbers are used to derive the corresponding array index.
*/

/*
   A driver needs to provide a dev_node_t structure for each device
   on a card.  In some cases, there is only one device per card (for
   example, ethernet cards, modems).  In other cases, there may be
   many actual or logical devices (SCSI adapters, memory cards with
   multiple partitions).  The dev_node_t structures need to be kept
   in a linked list starting at the 'dev' field of a dev_link_t
   structure.  We allocate them in the card's private data structure,
   because they generally can't be allocated dynamically.
*/
   
typedef struct local_info_t {
    dev_node_t	node;
} local_info_t;

/*======================================================================

    avmcs_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

    The dev_link structure is initialized, but we don't actually
    configure the card at this point -- we wait until we receive a
    card insertion event.
    
======================================================================*/

static int avmcs_attach(struct pcmcia_device *p_dev)
{
    local_info_t *local;

    /* The io structure describes IO port mapping */
    p_dev->io.NumPorts1 = 16;
    p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
    p_dev->io.NumPorts2 = 0;

    /* Interrupt setup */
    p_dev->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
    p_dev->irq.Attributes = IRQ_TYPE_DYNAMIC_SHARING|IRQ_FIRST_SHARED;

    p_dev->irq.IRQInfo1 = IRQ_LEVEL_ID;

    /* General socket configuration */
    p_dev->conf.Attributes = CONF_ENABLE_IRQ;
    p_dev->conf.IntType = INT_MEMORY_AND_IO;
    p_dev->conf.ConfigIndex = 1;
    p_dev->conf.Present = PRESENT_OPTION;

    /* Allocate space for private device-specific data */
    local = kmalloc(sizeof(local_info_t), GFP_KERNEL);
    if (!local)
        goto err;
    memset(local, 0, sizeof(local_info_t));
    p_dev->priv = local;

    p_dev->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
    avmcs_config(p_dev);

    return 0;

 err:
    return -EINVAL;
} /* avmcs_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void avmcs_detach(struct pcmcia_device *p_dev)
{
    dev_link_t *link = dev_to_instance(p_dev);

    if (link->state & DEV_CONFIG)
	avmcs_release(link);

    kfree(link->priv);
} /* avmcs_detach */

/*======================================================================

    avmcs_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    ethernet device available to the system.
    
======================================================================*/

static int get_tuple(client_handle_t handle, tuple_t *tuple,
		     cisparse_t *parse)
{
    int i = pcmcia_get_tuple_data(handle, tuple);
    if (i != CS_SUCCESS) return i;
    return pcmcia_parse_tuple(handle, tuple, parse);
}

static int first_tuple(client_handle_t handle, tuple_t *tuple,
		     cisparse_t *parse)
{
    int i = pcmcia_get_first_tuple(handle, tuple);
    if (i != CS_SUCCESS) return i;
    return get_tuple(handle, tuple, parse);
}

static int next_tuple(client_handle_t handle, tuple_t *tuple,
		     cisparse_t *parse)
{
    int i = pcmcia_get_next_tuple(handle, tuple);
    if (i != CS_SUCCESS) return i;
    return get_tuple(handle, tuple, parse);
}

static void avmcs_config(dev_link_t *link)
{
    client_handle_t handle;
    tuple_t tuple;
    cisparse_t parse;
    cistpl_cftable_entry_t *cf = &parse.cftable_entry;
    local_info_t *dev;
    int i;
    u_char buf[64];
    char devname[128];
    int cardtype;
    int (*addcard)(unsigned int port, unsigned irq);
    
    handle = link->handle;
    dev = link->priv;

    /*
       This reads the card's CONFIG tuple to find its configuration
       registers.
    */
    do {
	tuple.DesiredTuple = CISTPL_CONFIG;
	i = pcmcia_get_first_tuple(handle, &tuple);
	if (i != CS_SUCCESS) break;
	tuple.TupleData = buf;
	tuple.TupleDataMax = 64;
	tuple.TupleOffset = 0;
	i = pcmcia_get_tuple_data(handle, &tuple);
	if (i != CS_SUCCESS) break;
	i = pcmcia_parse_tuple(handle, &tuple, &parse);
	if (i != CS_SUCCESS) break;
	link->conf.ConfigBase = parse.config.base;
    } while (0);
    if (i != CS_SUCCESS) {
	cs_error(link->handle, ParseTuple, i);
	link->state &= ~DEV_CONFIG_PENDING;
	return;
    }
    
    /* Configure card */
    link->state |= DEV_CONFIG;

    do {

	tuple.Attributes = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = 254;
	tuple.TupleOffset = 0;
	tuple.DesiredTuple = CISTPL_VERS_1;

	devname[0] = 0;
	if( !first_tuple(handle, &tuple, &parse) && parse.version_1.ns > 1 ) {
	    strlcpy(devname,parse.version_1.str + parse.version_1.ofs[1], 
			sizeof(devname));
	}
	/*
         * find IO port
         */
	tuple.TupleData = (cisdata_t *)buf;
	tuple.TupleOffset = 0; tuple.TupleDataMax = 255;
	tuple.Attributes = 0;
	tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
	i = first_tuple(handle, &tuple, &parse);
	while (i == CS_SUCCESS) {
	    if (cf->io.nwin > 0) {
		link->conf.ConfigIndex = cf->index;
		link->io.BasePort1 = cf->io.win[0].base;
		link->io.NumPorts1 = cf->io.win[0].len;
		link->io.NumPorts2 = 0;
                printk(KERN_INFO "avm_cs: testing i/o %#x-%#x\n",
			link->io.BasePort1,
		        link->io.BasePort1+link->io.NumPorts1-1);
		i = pcmcia_request_io(link->handle, &link->io);
		if (i == CS_SUCCESS) goto found_port;
	    }
	    i = next_tuple(handle, &tuple, &parse);
	}

found_port:
	if (i != CS_SUCCESS) {
	    cs_error(link->handle, RequestIO, i);
	    break;
	}

	/*
	 * allocate an interrupt line
	 */
	i = pcmcia_request_irq(link->handle, &link->irq);
	if (i != CS_SUCCESS) {
	    cs_error(link->handle, RequestIRQ, i);
	    /* undo */
	    pcmcia_disable_device(link->handle);
	    break;
	}

	/*
         * configure the PCMCIA socket
	  */
	i = pcmcia_request_configuration(link->handle, &link->conf);
	if (i != CS_SUCCESS) {
	    cs_error(link->handle, RequestConfiguration, i);
	    pcmcia_disable_device(link->handle);
	    break;
	}

    } while (0);

    /* At this point, the dev_node_t structure(s) should be
       initialized and arranged in a linked list at link->dev. */

    if (devname[0]) {
	char *s = strrchr(devname, ' ');
	if (!s)
	   s = devname;
	else s++;
	strcpy(dev->node.dev_name, s);
        if (strcmp("M1", s) == 0) {
           cardtype = AVM_CARDTYPE_M1;
        } else if (strcmp("M2", s) == 0) {
           cardtype = AVM_CARDTYPE_M2;
	} else {
           cardtype = AVM_CARDTYPE_B1;
	}
    } else {
        strcpy(dev->node.dev_name, "b1");
        cardtype = AVM_CARDTYPE_B1;
    }

    dev->node.major = 64;
    dev->node.minor = 0;
    link->dev_node = &dev->node;
    
    link->state &= ~DEV_CONFIG_PENDING;
    /* If any step failed, release any partially configured state */
    if (i != 0) {
	avmcs_release(link);
	return;
    }


    switch (cardtype) {
        case AVM_CARDTYPE_M1: addcard = b1pcmcia_addcard_m1; break;
        case AVM_CARDTYPE_M2: addcard = b1pcmcia_addcard_m2; break;
	default:
        case AVM_CARDTYPE_B1: addcard = b1pcmcia_addcard_b1; break;
    }
    if ((i = (*addcard)(link->io.BasePort1, link->irq.AssignedIRQ)) < 0) {
        printk(KERN_ERR "avm_cs: failed to add AVM-%s-Controller at i/o %#x, irq %d\n",
		dev->node.dev_name, link->io.BasePort1, link->irq.AssignedIRQ);
	avmcs_release(link);
	return;
    }
    dev->node.minor = i;

} /* avmcs_config */

/*======================================================================

    After a card is removed, avmcs_release() will unregister the net
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.
    
======================================================================*/

static void avmcs_release(dev_link_t *link)
{
	b1pcmcia_delcard(link->io.BasePort1, link->irq.AssignedIRQ);
	pcmcia_disable_device(link->handle);
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
	.probe = avmcs_attach,
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
