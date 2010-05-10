/*
 * PCMCIA client driver for AVM A1 / Fritz!PCMCIA
 *
 * Author       Carsten Paeth
 * Copyright    1998-2001 by Carsten Paeth <calle@calle.in-berlin.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>


#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/io.h>
#include <asm/system.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>
#include "hisax_cfg.h"

MODULE_DESCRIPTION("ISDN4Linux: PCMCIA client driver for AVM A1/Fritz!PCMCIA cards");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");


/*====================================================================*/

/* Parameters that can be set with 'insmod' */

static int isdnprot = 2;

module_param(isdnprot, int, 0);

/*====================================================================*/

/*
   The event() function is this driver's Card Services event handler.
   It will be called by Card Services when an appropriate card status
   event is received.  The config() and release() entry points are
   used to configure or release a socket, in response to card insertion
   and ejection events.  They are invoked from the skeleton event
   handler.
*/

static int avma1cs_config(struct pcmcia_device *link) __devinit ;
static void avma1cs_release(struct pcmcia_device *link);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static void avma1cs_detach(struct pcmcia_device *p_dev) __devexit ;


/*
   A linked list of "instances" of the skeleton device.  Each actual
   PCMCIA card corresponds to one device instance, and is described
   by one struct pcmcia_device structure (defined in ds.h).

   You may not want to use a linked list for this -- for example, the
   memory card driver uses an array of struct pcmcia_device pointers, where minor
   device numbers are used to derive the corresponding array index.
*/

/*
   A driver needs to provide a dev_node_t structure for each device
   on a card.  In some cases, there is only one device per card (for
   example, ethernet cards, modems).  In other cases, there may be
   many actual or logical devices (SCSI adapters, memory cards with
   multiple partitions).  The dev_node_t structures need to be kept
   in a linked list starting at the 'dev' field of a struct pcmcia_device
   structure.  We allocate them in the card's private data structure,
   because they generally can't be allocated dynamically.
*/
   
typedef struct local_info_t {
    dev_node_t	node;
} local_info_t;

/*======================================================================

    avma1cs_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

    The dev_link structure is initialized, but we don't actually
    configure the card at this point -- we wait until we receive a
    card insertion event.
    
======================================================================*/

static int __devinit avma1cs_probe(struct pcmcia_device *p_dev)
{
    local_info_t *local;

    dev_dbg(&p_dev->dev, "avma1cs_attach()\n");

    /* Allocate space for private device-specific data */
    local = kzalloc(sizeof(local_info_t), GFP_KERNEL);
    if (!local)
	return -ENOMEM;

    p_dev->priv = local;

    /* The io structure describes IO port mapping */
    p_dev->io.NumPorts1 = 16;
    p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
    p_dev->io.NumPorts2 = 16;
    p_dev->io.Attributes2 = IO_DATA_PATH_WIDTH_16;
    p_dev->io.IOAddrLines = 5;

    /* Interrupt setup */
    p_dev->irq.Attributes = IRQ_TYPE_DYNAMIC_SHARING;

    /* General socket configuration */
    p_dev->conf.Attributes = CONF_ENABLE_IRQ;
    p_dev->conf.IntType = INT_MEMORY_AND_IO;
    p_dev->conf.ConfigIndex = 1;
    p_dev->conf.Present = PRESENT_OPTION;

    return avma1cs_config(p_dev);
} /* avma1cs_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void __devexit avma1cs_detach(struct pcmcia_device *link)
{
	dev_dbg(&link->dev, "avma1cs_detach(0x%p)\n", link);
	avma1cs_release(link);
	kfree(link->priv);
} /* avma1cs_detach */

/*======================================================================

    avma1cs_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    ethernet device available to the system.
    
======================================================================*/

static int avma1cs_configcheck(struct pcmcia_device *p_dev,
			       cistpl_cftable_entry_t *cf,
			       cistpl_cftable_entry_t *dflt,
			       unsigned int vcc,
			       void *priv_data)
{
	if (cf->io.nwin <= 0)
		return -ENODEV;

	p_dev->io.BasePort1 = cf->io.win[0].base;
	p_dev->io.NumPorts1 = cf->io.win[0].len;
	p_dev->io.NumPorts2 = 0;
	printk(KERN_INFO "avma1_cs: testing i/o %#x-%#x\n",
	       p_dev->io.BasePort1,
	       p_dev->io.BasePort1+p_dev->io.NumPorts1-1);
	return pcmcia_request_io(p_dev, &p_dev->io);
}


static int __devinit avma1cs_config(struct pcmcia_device *link)
{
    local_info_t *dev;
    int i;
    char devname[128];
    IsdnCard_t	icard;
    int busy = 0;

    dev = link->priv;

    dev_dbg(&link->dev, "avma1cs_config(0x%p)\n", link);

    devname[0] = 0;
    if (link->prod_id[1])
	    strlcpy(devname, link->prod_id[1], sizeof(devname));

    if (pcmcia_loop_config(link, avma1cs_configcheck, NULL))
	    return -ENODEV;

    do {
	/*
	 * allocate an interrupt line
	 */
	i = pcmcia_request_irq(link, &link->irq);
	if (i != 0) {
	    /* undo */
	    pcmcia_disable_device(link);
	    break;
	}

	/*
	 * configure the PCMCIA socket
	 */
	i = pcmcia_request_configuration(link, &link->conf);
	if (i != 0) {
	    pcmcia_disable_device(link);
	    break;
	}

    } while (0);

    /* At this point, the dev_node_t structure(s) should be
       initialized and arranged in a linked list at link->dev. */

    strcpy(dev->node.dev_name, "A1");
    dev->node.major = 45;
    dev->node.minor = 0;
    link->dev_node = &dev->node;

    /* If any step failed, release any partially configured state */
    if (i != 0) {
	avma1cs_release(link);
	return -ENODEV;
    }

    printk(KERN_NOTICE "avma1_cs: checking at i/o %#x, irq %d\n",
				link->io.BasePort1, link->irq.AssignedIRQ);

    icard.para[0] = link->irq.AssignedIRQ;
    icard.para[1] = link->io.BasePort1;
    icard.protocol = isdnprot;
    icard.typ = ISDN_CTYPE_A1_PCMCIA;
    
    i = hisax_init_pcmcia(link, &busy, &icard);
    if (i < 0) {
    	printk(KERN_ERR "avma1_cs: failed to initialize AVM A1 PCMCIA %d at i/o %#x\n", i, link->io.BasePort1);
	avma1cs_release(link);
	return -ENODEV;
    }
    dev->node.minor = i;

    return 0;
} /* avma1cs_config */

/*======================================================================

    After a card is removed, avma1cs_release() will unregister the net
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.
    
======================================================================*/

static void avma1cs_release(struct pcmcia_device *link)
{
	local_info_t *local = link->priv;

	dev_dbg(&link->dev, "avma1cs_release(0x%p)\n", link);

	/* now unregister function with hisax */
	HiSax_closecard(local->node.minor);

	pcmcia_disable_device(link);
} /* avma1cs_release */


static struct pcmcia_device_id avma1cs_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("AVM", "ISDN A", 0x95d42008, 0xadc9d4bb),
	PCMCIA_DEVICE_PROD_ID12("ISDN", "CARD", 0x8d9761c8, 0x01c5aa7b),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, avma1cs_ids);

static struct pcmcia_driver avma1cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "avma1_cs",
	},
	.probe		= avma1cs_probe,
	.remove		= __devexit_p(avma1cs_detach),
	.id_table	= avma1cs_ids,
};

/*====================================================================*/

static int __init init_avma1_cs(void)
{
	return(pcmcia_register_driver(&avma1cs_driver));
}

static void __exit exit_avma1_cs(void)
{
	pcmcia_unregister_driver(&avma1cs_driver);
}

module_init(init_avma1_cs);
module_exit(exit_avma1_cs);
