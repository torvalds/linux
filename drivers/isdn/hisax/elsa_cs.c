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

#include <pcmcia/version.h>
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

static void elsa_cs_config(dev_link_t *link);
static void elsa_cs_release(dev_link_t *link);
static int elsa_cs_event(event_t event, int priority,
                          event_callback_args_t *args);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static dev_link_t *elsa_cs_attach(void);
static void elsa_cs_detach(dev_link_t *);

/*
   The dev_info variable is the "key" that is used to match up this
   device driver with appropriate cards, through the card configuration
   database.
*/

static dev_info_t dev_info = "elsa_cs";

/*
   A linked list of "instances" of the elsa_cs device.  Each actual
   PCMCIA card corresponds to one device instance, and is described
   by one dev_link_t structure (defined in ds.h).

   You may not want to use a linked list for this -- for example, the
   memory card driver uses an array of dev_link_t pointers, where minor
   device numbers are used to derive the corresponding array index.
*/

static dev_link_t *dev_list = NULL;

/*
   A dev_link_t structure has fields for most things that are needed
   to keep track of a socket, but there will usually be some device
   specific information that also needs to be kept track of.  The
   'priv' pointer in a dev_link_t structure can be used to point to
   a device-specific private data structure, like this.

   To simplify the data structure handling, we actually include the
   dev_link_t structure in the device's private data structure.

   A driver needs to provide a dev_node_t structure for each device
   on a card.  In some cases, there is only one device per card (for
   example, ethernet cards, modems).  In other cases, there may be
   many actual or logical devices (SCSI adapters, memory cards with
   multiple partitions).  The dev_node_t structures need to be kept
   in a linked list starting at the 'dev' field of a dev_link_t
   structure.  We allocate them in the card's private data structure,
   because they generally shouldn't be allocated dynamically.
   In this case, we also provide a flag to indicate if a device is
   "stopped" due to a power management event, or card ejection.  The
   device IO routines can use a flag like this to throttle IO to a
   card that is not ready to accept it.
*/

typedef struct local_info_t {
    dev_link_t          link;
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

static dev_link_t *elsa_cs_attach(void)
{
    client_reg_t client_reg;
    dev_link_t *link;
    local_info_t *local;
    int ret;

    DEBUG(0, "elsa_cs_attach()\n");

    /* Allocate space for private device-specific data */
    local = kmalloc(sizeof(local_info_t), GFP_KERNEL);
    if (!local) return NULL;
    memset(local, 0, sizeof(local_info_t));
    local->cardnr = -1;
    link = &local->link; link->priv = local;

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
    link->conf.Vcc = 50;
    link->conf.IntType = INT_MEMORY_AND_IO;

    /* Register with Card Services */
    link->next = dev_list;
    dev_list = link;
    client_reg.dev_info = &dev_info;
    client_reg.EventMask =
        CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
        CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET |
        CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
    client_reg.event_handler = &elsa_cs_event;
    client_reg.Version = 0x0210;
    client_reg.event_callback_args.client_data = link;
    ret = pcmcia_register_client(&link->handle, &client_reg);
    if (ret != CS_SUCCESS) {
        cs_error(link->handle, RegisterClient, ret);
        elsa_cs_detach(link);
        return NULL;
    }

    return link;
} /* elsa_cs_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void elsa_cs_detach(dev_link_t *link)
{
    dev_link_t **linkp;
    local_info_t *info = link->priv;
    int ret;

    DEBUG(0, "elsa_cs_detach(0x%p)\n", link);

    /* Locate device structure */
    for (linkp = &dev_list; *linkp; linkp = &(*linkp)->next)
        if (*linkp == link) break;
    if (*linkp == NULL)
        return;

    if (link->state & DEV_CONFIG)
        elsa_cs_release(link);

    /* Break the link with Card Services */
    if (link->handle) {
        ret = pcmcia_deregister_client(link->handle);
	if (ret != CS_SUCCESS)
	    cs_error(link->handle, DeregisterClient, ret);
    }

    /* Unlink device structure and free it */
    *linkp = link->next;
    kfree(info);

} /* elsa_cs_detach */

/*======================================================================

    elsa_cs_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    device available to the system.

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

static void elsa_cs_config(dev_link_t *link)
{
    client_handle_t handle;
    tuple_t tuple;
    cisparse_t parse;
    local_info_t *dev;
    int i, j, last_fn;
    u_short buf[128];
    cistpl_cftable_entry_t *cf = &parse.cftable_entry;
    IsdnCard_t icard;

    DEBUG(0, "elsa_config(0x%p)\n", link);
    handle = link->handle;
    dev = link->priv;

    /*
       This reads the card's CONFIG tuple to find its configuration
       registers.
    */
    tuple.DesiredTuple = CISTPL_CONFIG;
    tuple.TupleData = (cisdata_t *)buf;
    tuple.TupleDataMax = 255;
    tuple.TupleOffset = 0;
    tuple.Attributes = 0;
    i = first_tuple(handle, &tuple, &parse);
    if (i != CS_SUCCESS) {
        last_fn = ParseTuple;
	goto cs_failed;
    }
    link->conf.ConfigBase = parse.config.base;
    link->conf.Present = parse.config.rmask[0];

    /* Configure card */
    link->state |= DEV_CONFIG;

    tuple.TupleData = (cisdata_t *)buf;
    tuple.TupleOffset = 0; tuple.TupleDataMax = 255;
    tuple.Attributes = 0;
    tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
    i = first_tuple(handle, &tuple, &parse);
    while (i == CS_SUCCESS) {
        if ( (cf->io.nwin > 0) && cf->io.win[0].base) {
            printk(KERN_INFO "(elsa_cs: looks like the 96 model)\n");
            link->conf.ConfigIndex = cf->index;
            link->io.BasePort1 = cf->io.win[0].base;
            i = pcmcia_request_io(link->handle, &link->io);
            if (i == CS_SUCCESS) break;
        } else {
          printk(KERN_INFO "(elsa_cs: looks like the 97 model)\n");
          link->conf.ConfigIndex = cf->index;
          for (i = 0, j = 0x2f0; j > 0x100; j -= 0x10) {
            link->io.BasePort1 = j;
            i = pcmcia_request_io(link->handle, &link->io);
            if (i == CS_SUCCESS) break;
          }
          break;
        }
        i = next_tuple(handle, &tuple, &parse);
    }

    if (i != CS_SUCCESS) {
	last_fn = RequestIO;
	goto cs_failed;
    }

    i = pcmcia_request_irq(link->handle, &link->irq);
    if (i != CS_SUCCESS) {
        link->irq.AssignedIRQ = 0;
	last_fn = RequestIRQ;
        goto cs_failed;
    }

    i = pcmcia_request_configuration(link->handle, &link->conf);
    if (i != CS_SUCCESS) {
      last_fn = RequestConfiguration;
      goto cs_failed;
    }

    /* At this point, the dev_node_t structure(s) should be
       initialized and arranged in a linked list at link->dev. *//*  */
    sprintf(dev->node.dev_name, "elsa");
    dev->node.major = dev->node.minor = 0x0;

    link->dev = &dev->node;

    /* Finally, report what we've done */
    printk(KERN_INFO "%s: index 0x%02x: Vcc %d.%d",
           dev->node.dev_name, link->conf.ConfigIndex,
           link->conf.Vcc/10, link->conf.Vcc%10);
    if (link->conf.Vpp1)
        printk(", Vpp %d.%d", link->conf.Vpp1/10, link->conf.Vpp1%10);
    if (link->conf.Attributes & CONF_ENABLE_IRQ)
        printk(", irq %d", link->irq.AssignedIRQ);
    if (link->io.NumPorts1)
        printk(", io 0x%04x-0x%04x", link->io.BasePort1,
               link->io.BasePort1+link->io.NumPorts1-1);
    if (link->io.NumPorts2)
        printk(" & 0x%04x-0x%04x", link->io.BasePort2,
               link->io.BasePort2+link->io.NumPorts2-1);
    printk("\n");

    link->state &= ~DEV_CONFIG_PENDING;

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

    return;
cs_failed:
    cs_error(link->handle, last_fn, i);
    elsa_cs_release(link);
} /* elsa_cs_config */

/*======================================================================

    After a card is removed, elsa_cs_release() will unregister the net
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.

======================================================================*/

static void elsa_cs_release(dev_link_t *link)
{
    local_info_t *local = link->priv;

    DEBUG(0, "elsa_cs_release(0x%p)\n", link);

    if (local) {
    	if (local->cardnr >= 0) {
    	    /* no unregister function with hisax */
	    HiSax_closecard(local->cardnr);
	}
    }
    /* Unlink the device chain */
    link->dev = NULL;

    /* Don't bother checking to see if these succeed or not */
    if (link->win)
        pcmcia_release_window(link->win);
    pcmcia_release_configuration(link->handle);
    pcmcia_release_io(link->handle, &link->io);
    pcmcia_release_irq(link->handle, &link->irq);
    link->state &= ~DEV_CONFIG;
} /* elsa_cs_release */

/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.  A CARD_REMOVAL event
    also sets some flags to discourage the net drivers from trying
    to talk to the card any more.

    When a CARD_REMOVAL event is received, we immediately set a flag
    to block future accesses to this device.  All the functions that
    actually access the device should check this flag to make sure
    the card is still present.

======================================================================*/

static int elsa_cs_event(event_t event, int priority,
                          event_callback_args_t *args)
{
    dev_link_t *link = args->client_data;
    local_info_t *dev = link->priv;

    DEBUG(1, "elsa_cs_event(%d)\n", event);

    switch (event) {
    case CS_EVENT_CARD_REMOVAL:
        link->state &= ~DEV_PRESENT;
        if (link->state & DEV_CONFIG) {
            ((local_info_t*)link->priv)->busy = 1;
	    elsa_cs_release(link);
        }
        break;
    case CS_EVENT_CARD_INSERTION:
        link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
        elsa_cs_config(link);
        break;
    case CS_EVENT_PM_SUSPEND:
        link->state |= DEV_SUSPEND;
        /* Fall through... */
    case CS_EVENT_RESET_PHYSICAL:
        /* Mark the device as stopped, to block IO until later */
        dev->busy = 1;
        if (link->state & DEV_CONFIG)
            pcmcia_release_configuration(link->handle);
        break;
    case CS_EVENT_PM_RESUME:
        link->state &= ~DEV_SUSPEND;
        /* Fall through... */
    case CS_EVENT_CARD_RESET:
        if (link->state & DEV_CONFIG)
            pcmcia_request_configuration(link->handle, &link->conf);
        dev->busy = 0;
        break;
    }
    return 0;
} /* elsa_cs_event */

static struct pcmcia_driver elsa_cs_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "elsa_cs",
	},
	.attach		= elsa_cs_attach,
	.detach		= elsa_cs_detach,
};

static int __init init_elsa_cs(void)
{
	return pcmcia_register_driver(&elsa_cs_driver);
}

static void __exit exit_elsa_cs(void)
{
	pcmcia_unregister_driver(&elsa_cs_driver);
	BUG_ON(dev_list != NULL);
}

module_init(init_elsa_cs);
module_exit(exit_elsa_cs);
