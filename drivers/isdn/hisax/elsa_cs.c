/*======================================================================

    An elsa_cs PCMCIA client driver

    This driver is for the Elsa PCM ISDN Cards, i.e. the MicroLink


    The contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

    Modifications from dummy_cs.c are Copyright (C) 1999-2001 Klaus
    Lichtenwalder <Lichtenwalder@ACM.org>. All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU General Public License version 2 (the "GPL"), in
    which case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.

======================================================================*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/system.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>
#include "hisax_cfg.h"

MODULE_DESCRIPTION("ISDN4Linux: PCMCIA client driver for Elsa PCM cards");
MODULE_AUTHOR("Klaus Lichtenwalder");
MODULE_LICENSE("Dual MPL/GPL");

/*
   All the PCMCIA modules use PCMCIA_DEBUG to control debugging.  If
   you do not define PCMCIA_DEBUG at all, all the debug code will be
   left out.  If you compile with PCMCIA_DEBUG=0, the debug code will
   be present but disabled -- but it can then be enabled for specific
   modules at load time with a 'pc_debug=#' option to insmod.
*/

#ifdef PCMCIA_DEBUG
static int pc_debug = PCMCIA_DEBUG;
module_param(pc_debug, int, 0);
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args);
static char *version =
"elsa_cs.c $Revision: 1.2.2.4 $ $Date: 2004/01/25 15:07:06 $ (K.Lichtenwalder)";
#else
#define DEBUG(n, args...)
#endif

/*====================================================================*/

/* Parameters that can be set with 'insmod' */

static int protocol = 2;        /* EURO-ISDN Default */
module_param(protocol, int, 0);

/*====================================================================*/

/*
   The event() function is this driver's Card Services event handler.
   It will be called by Card Services when an appropriate card status
   event is received.  The config() and release() entry points are
   used to configure or release a socket, in response to card insertion
   and ejection events.  They are invoked from the elsa_cs event
   handler.
*/

static int elsa_cs_config(struct pcmcia_device *link);
static void elsa_cs_release(struct pcmcia_device *link);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static void elsa_cs_detach(struct pcmcia_device *p_dev);

/*
   A driver needs to provide a dev_node_t structure for each device
   on a card.  In some cases, there is only one device per card (for
   example, ethernet cards, modems).  In other cases, there may be
   many actual or logical devices (SCSI adapters, memory cards with
   multiple partitions).  The dev_node_t structures need to be kept
   in a linked list starting at the 'dev' field of a struct pcmcia_device
   structure.  We allocate them in the card's private data structure,
   because they generally shouldn't be allocated dynamically.
   In this case, we also provide a flag to indicate if a device is
   "stopped" due to a power management event, or card ejection.  The
   device IO routines can use a flag like this to throttle IO to a
   card that is not ready to accept it.
*/

typedef struct local_info_t {
	struct pcmcia_device	*p_dev;
    dev_node_t          node;
    int                 busy;
    int			cardnr;
} local_info_t;

/*======================================================================

    elsa_cs_attach() creates an "instance" of the driver, allocatingx
    local data structures for one device.  The device is registered
    with Card Services.

    The dev_link structure is initialized, but we don't actually
    configure the card at this point -- we wait until we receive a
    card insertion event.

======================================================================*/

static int elsa_cs_probe(struct pcmcia_device *link)
{
    local_info_t *local;

    DEBUG(0, "elsa_cs_attach()\n");

    /* Allocate space for private device-specific data */
    local = kzalloc(sizeof(local_info_t), GFP_KERNEL);
    if (!local) return -ENOMEM;

    local->p_dev = link;
    link->priv = local;

    local->cardnr = -1;

    /* Interrupt setup */
    link->irq.Attributes = IRQ_TYPE_DYNAMIC_SHARING|IRQ_FIRST_SHARED;
    link->irq.IRQInfo1 = IRQ_LEVEL_ID|IRQ_SHARE_ID;
    link->irq.Handler = NULL;

    /*
      General socket configuration defaults can go here.  In this
      client, we assume very little, and rely on the CIS for almost
      everything.  In most clients, many details (i.e., number, sizes,
      and attributes of IO windows) are fixed by the nature of the
      device, and can be hard-wired here.
    */
    link->io.NumPorts1 = 8;
    link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
    link->io.IOAddrLines = 3;

    link->conf.Attributes = CONF_ENABLE_IRQ;
    link->conf.IntType = INT_MEMORY_AND_IO;

    return elsa_cs_config(link);
} /* elsa_cs_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void elsa_cs_detach(struct pcmcia_device *link)
{
	local_info_t *info = link->priv;

	DEBUG(0, "elsa_cs_detach(0x%p)\n", link);

	info->busy = 1;
	elsa_cs_release(link);

	kfree(info);
} /* elsa_cs_detach */

/*======================================================================

    elsa_cs_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    device available to the system.

======================================================================*/
static int get_tuple(struct pcmcia_device *handle, tuple_t *tuple,
                     cisparse_t *parse)
{
    int i = pcmcia_get_tuple_data(handle, tuple);
    if (i != CS_SUCCESS) return i;
    return pcmcia_parse_tuple(handle, tuple, parse);
}

static int first_tuple(struct pcmcia_device *handle, tuple_t *tuple,
                     cisparse_t *parse)
{
    int i = pcmcia_get_first_tuple(handle, tuple);
    if (i != CS_SUCCESS) return i;
    return get_tuple(handle, tuple, parse);
}

static int next_tuple(struct pcmcia_device *handle, tuple_t *tuple,
                     cisparse_t *parse)
{
    int i = pcmcia_get_next_tuple(handle, tuple);
    if (i != CS_SUCCESS) return i;
    return get_tuple(handle, tuple, parse);
}

static int elsa_cs_config(struct pcmcia_device *link)
{
    tuple_t tuple;
    cisparse_t parse;
    local_info_t *dev;
    int i, j, last_fn;
    u_short buf[128];
    cistpl_cftable_entry_t *cf = &parse.cftable_entry;
    IsdnCard_t icard;

    DEBUG(0, "elsa_config(0x%p)\n", link);
    dev = link->priv;

    tuple.TupleData = (cisdata_t *)buf;
    tuple.TupleOffset = 0; tuple.TupleDataMax = 255;
    tuple.Attributes = 0;
    tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
    i = first_tuple(link, &tuple, &parse);
    while (i == CS_SUCCESS) {
        if ( (cf->io.nwin > 0) && cf->io.win[0].base) {
            printk(KERN_INFO "(elsa_cs: looks like the 96 model)\n");
            link->conf.ConfigIndex = cf->index;
            link->io.BasePort1 = cf->io.win[0].base;
            i = pcmcia_request_io(link, &link->io);
            if (i == CS_SUCCESS) break;
        } else {
          printk(KERN_INFO "(elsa_cs: looks like the 97 model)\n");
          link->conf.ConfigIndex = cf->index;
          for (i = 0, j = 0x2f0; j > 0x100; j -= 0x10) {
            link->io.BasePort1 = j;
            i = pcmcia_request_io(link, &link->io);
            if (i == CS_SUCCESS) break;
          }
          break;
        }
        i = next_tuple(link, &tuple, &parse);
    }

    if (i != CS_SUCCESS) {
	last_fn = RequestIO;
	goto cs_failed;
    }

    i = pcmcia_request_irq(link, &link->irq);
    if (i != CS_SUCCESS) {
        link->irq.AssignedIRQ = 0;
	last_fn = RequestIRQ;
        goto cs_failed;
    }

    i = pcmcia_request_configuration(link, &link->conf);
    if (i != CS_SUCCESS) {
      last_fn = RequestConfiguration;
      goto cs_failed;
    }

    /* At this point, the dev_node_t structure(s) should be
       initialized and arranged in a linked list at link->dev. *//*  */
    sprintf(dev->node.dev_name, "elsa");
    dev->node.major = dev->node.minor = 0x0;

    link->dev_node = &dev->node;

    /* Finally, report what we've done */
    printk(KERN_INFO "%s: index 0x%02x: ",
           dev->node.dev_name, link->conf.ConfigIndex);
    if (link->conf.Attributes & CONF_ENABLE_IRQ)
        printk(", irq %d", link->irq.AssignedIRQ);
    if (link->io.NumPorts1)
        printk(", io 0x%04x-0x%04x", link->io.BasePort1,
               link->io.BasePort1+link->io.NumPorts1-1);
    if (link->io.NumPorts2)
        printk(" & 0x%04x-0x%04x", link->io.BasePort2,
               link->io.BasePort2+link->io.NumPorts2-1);
    printk("\n");

    icard.para[0] = link->irq.AssignedIRQ;
    icard.para[1] = link->io.BasePort1;
    icard.protocol = protocol;
    icard.typ = ISDN_CTYPE_ELSA_PCMCIA;
    
    i = hisax_init_pcmcia(link, &(((local_info_t*)link->priv)->busy), &icard);
    if (i < 0) {
    	printk(KERN_ERR "elsa_cs: failed to initialize Elsa PCMCIA %d at i/o %#x\n",
    		i, link->io.BasePort1);
    	elsa_cs_release(link);
    } else
    	((local_info_t*)link->priv)->cardnr = i;

    return 0;
cs_failed:
    cs_error(link, last_fn, i);
    elsa_cs_release(link);
    return -ENODEV;
} /* elsa_cs_config */

/*======================================================================

    After a card is removed, elsa_cs_release() will unregister the net
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.

======================================================================*/

static void elsa_cs_release(struct pcmcia_device *link)
{
    local_info_t *local = link->priv;

    DEBUG(0, "elsa_cs_release(0x%p)\n", link);

    if (local) {
    	if (local->cardnr >= 0) {
    	    /* no unregister function with hisax */
	    HiSax_closecard(local->cardnr);
	}
    }

    pcmcia_disable_device(link);
} /* elsa_cs_release */

static int elsa_suspend(struct pcmcia_device *link)
{
	local_info_t *dev = link->priv;

        dev->busy = 1;

	return 0;
}

static int elsa_resume(struct pcmcia_device *link)
{
	local_info_t *dev = link->priv;

        dev->busy = 0;

	return 0;
}

static struct pcmcia_device_id elsa_ids[] = {
	PCMCIA_DEVICE_PROD_ID12("ELSA AG (Aachen, Germany)", "MicroLink ISDN/MC ", 0x983de2c4, 0x333ba257),
	PCMCIA_DEVICE_PROD_ID12("ELSA GmbH, Aachen", "MicroLink ISDN/MC ", 0x639e5718, 0x333ba257),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, elsa_ids);

static struct pcmcia_driver elsa_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "elsa_cs",
	},
	.probe		= elsa_cs_probe,
	.remove		= elsa_cs_detach,
	.id_table	= elsa_ids,
	.suspend	= elsa_suspend,
	.resume		= elsa_resume,
};

static int __init init_elsa_cs(void)
{
	return pcmcia_register_driver(&elsa_cs_driver);
}

static void __exit exit_elsa_cs(void)
{
	pcmcia_unregister_driver(&elsa_cs_driver);
}

module_init(init_elsa_cs);
module_exit(exit_elsa_cs);
