/*======================================================================

    A Sedlbauer PCMCIA client driver

    This driver is for the Sedlbauer Speed Star and Speed Star II, 
    which are ISDN PCMCIA Cards.
    
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

    Modifications from dummy_cs.c are Copyright (C) 1999-2001 Marcus Niemann
    <maniemann@users.sourceforge.net>. All Rights Reserved.

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

#include <linux/kernel.h>
#include <linux/module.h>
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

MODULE_DESCRIPTION("ISDN4Linux: PCMCIA client driver for Sedlbauer cards");
MODULE_AUTHOR("Marcus Niemann");
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
"sedlbauer_cs.c 1.1a 2001/01/28 15:04:04 (M.Niemann)";
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
   used to configure or release a socket, in response to card
   insertion and ejection events.  They are invoked from the sedlbauer
   event handler. 
*/

static void sedlbauer_config(dev_link_t *link);
static void sedlbauer_release(dev_link_t *link);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static void sedlbauer_detach(struct pcmcia_device *p_dev);

/*
   You'll also need to prototype all the functions that will actually
   be used to talk to your device.  See 'memory_cs' for a good example
   of a fully self-sufficient driver; the other drivers rely more or
   less on other parts of the kernel.
*/

/*
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
	struct pcmcia_device	*p_dev;
    dev_node_t		node;
    int			stop;
    int			cardnr;
} local_info_t;

/*======================================================================

    sedlbauer_attach() creates an "instance" of the driver, allocating
    local data structures for one device.  The device is registered
    with Card Services.

    The dev_link structure is initialized, but we don't actually
    configure the card at this point -- we wait until we receive a
    card insertion event.
    
======================================================================*/

static int sedlbauer_attach(struct pcmcia_device *p_dev)
{
    local_info_t *local;
    dev_link_t *link = dev_to_instance(p_dev);
    
    DEBUG(0, "sedlbauer_attach()\n");

    /* Allocate space for private device-specific data */
    local = kmalloc(sizeof(local_info_t), GFP_KERNEL);
    if (!local) return -ENOMEM;
    memset(local, 0, sizeof(local_info_t));
    local->cardnr = -1;

    local->p_dev = p_dev;
    link->priv = local;

    /* Interrupt setup */
    link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
    link->irq.IRQInfo1 = IRQ_LEVEL_ID;
    link->irq.Handler = NULL;

    /*
      General socket configuration defaults can go here.  In this
      client, we assume very little, and rely on the CIS for almost
      everything.  In most clients, many details (i.e., number, sizes,
      and attributes of IO windows) are fixed by the nature of the
      device, and can be hard-wired here.
    */

    /* from old sedl_cs 
    */
    /* The io structure describes IO port mapping */
    link->io.NumPorts1 = 8;
    link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
    link->io.IOAddrLines = 3;

    link->conf.Attributes = 0;
    link->conf.IntType = INT_MEMORY_AND_IO;

    link->state |= DEV_PRESENT | DEV_CONFIG_PENDING;
    sedlbauer_config(link);

    return 0;
} /* sedlbauer_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void sedlbauer_detach(struct pcmcia_device *p_dev)
{
    dev_link_t *link = dev_to_instance(p_dev);

    DEBUG(0, "sedlbauer_detach(0x%p)\n", link);

    if (link->state & DEV_CONFIG) {
	    ((local_info_t *)link->priv)->stop = 1;
	    sedlbauer_release(link);
    }

    /* This points to the parent local_info_t struct */
    kfree(link->priv);
} /* sedlbauer_detach */

/*======================================================================

    sedlbauer_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    device available to the system.
    
======================================================================*/
#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

static void sedlbauer_config(dev_link_t *link)
{
    client_handle_t handle = link->handle;
    local_info_t *dev = link->priv;
    tuple_t tuple;
    cisparse_t parse;
    int last_fn, last_ret;
    u8 buf[64];
    config_info_t conf;
    win_req_t req;
    memreq_t map;
    IsdnCard_t  icard;

    DEBUG(0, "sedlbauer_config(0x%p)\n", link);

    /*
       This reads the card's CONFIG tuple to find its configuration
       registers.
    */
    tuple.DesiredTuple = CISTPL_CONFIG;
    tuple.Attributes = 0;
    tuple.TupleData = buf;
    tuple.TupleDataMax = sizeof(buf);
    tuple.TupleOffset = 0;
    CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
    CS_CHECK(GetTupleData, pcmcia_get_tuple_data(handle, &tuple));
    CS_CHECK(ParseTuple, pcmcia_parse_tuple(handle, &tuple, &parse));
    link->conf.ConfigBase = parse.config.base;
    link->conf.Present = parse.config.rmask[0];
    
    /* Configure card */
    link->state |= DEV_CONFIG;

    CS_CHECK(GetConfigurationInfo, pcmcia_get_configuration_info(handle, &conf));

    /*
      In this loop, we scan the CIS for configuration table entries,
      each of which describes a valid card configuration, including
      voltage, IO window, memory window, and interrupt settings.

      We make no assumptions about the card to be configured: we use
      just the information available in the CIS.  In an ideal world,
      this would work for any PCMCIA card, but it requires a complete
      and accurate CIS.  In practice, a driver usually "knows" most of
      these things without consulting the CIS, and most client drivers
      will only use the CIS to fill in implementation-defined details.
    */
    tuple.DesiredTuple = CISTPL_CFTABLE_ENTRY;
    CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(handle, &tuple));
    while (1) {
	cistpl_cftable_entry_t dflt = { 0 };
	cistpl_cftable_entry_t *cfg = &(parse.cftable_entry);
	if (pcmcia_get_tuple_data(handle, &tuple) != 0 ||
		pcmcia_parse_tuple(handle, &tuple, &parse) != 0)
	    goto next_entry;

	if (cfg->flags & CISTPL_CFTABLE_DEFAULT) dflt = *cfg;
	if (cfg->index == 0) goto next_entry;
	link->conf.ConfigIndex = cfg->index;
	
	/* Does this card need audio output? */
	if (cfg->flags & CISTPL_CFTABLE_AUDIO) {
	    link->conf.Attributes |= CONF_ENABLE_SPKR;
	    link->conf.Status = CCSR_AUDIO_ENA;
	}
	
	/* Use power settings for Vcc and Vpp if present */
	/*  Note that the CIS values need to be rescaled */
	if (cfg->vcc.present & (1<<CISTPL_POWER_VNOM)) {
	    if (conf.Vcc != cfg->vcc.param[CISTPL_POWER_VNOM]/10000)
		goto next_entry;
	} else if (dflt.vcc.present & (1<<CISTPL_POWER_VNOM)) {
	    if (conf.Vcc != dflt.vcc.param[CISTPL_POWER_VNOM]/10000)
		goto next_entry;
	}
	    
	if (cfg->vpp1.present & (1<<CISTPL_POWER_VNOM))
	    link->conf.Vpp =
		cfg->vpp1.param[CISTPL_POWER_VNOM]/10000;
	else if (dflt.vpp1.present & (1<<CISTPL_POWER_VNOM))
	    link->conf.Vpp =
		dflt.vpp1.param[CISTPL_POWER_VNOM]/10000;
	
	/* Do we need to allocate an interrupt? */
	if (cfg->irq.IRQInfo1 || dflt.irq.IRQInfo1)
	    link->conf.Attributes |= CONF_ENABLE_IRQ;
	
	/* IO window settings */
	link->io.NumPorts1 = link->io.NumPorts2 = 0;
	if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
	    cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt.io;
	    link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
	    if (!(io->flags & CISTPL_IO_8BIT))
		link->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
	    if (!(io->flags & CISTPL_IO_16BIT))
		link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
/* new in dummy.cs 2001/01/28 MN 
            link->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
*/
	    link->io.BasePort1 = io->win[0].base;
	    link->io.NumPorts1 = io->win[0].len;
	    if (io->nwin > 1) {
		link->io.Attributes2 = link->io.Attributes1;
		link->io.BasePort2 = io->win[1].base;
		link->io.NumPorts2 = io->win[1].len;
	    }
	    /* This reserves IO space but doesn't actually enable it */
	    if (pcmcia_request_io(link->handle, &link->io) != 0)
		goto next_entry;
	}

	/*
	  Now set up a common memory window, if needed.  There is room
	  in the dev_link_t structure for one memory window handle,
	  but if the base addresses need to be saved, or if multiple
	  windows are needed, the info should go in the private data
	  structure for this device.

	  Note that the memory window base is a physical address, and
	  needs to be mapped to virtual space with ioremap() before it
	  is used.
	*/
	if ((cfg->mem.nwin > 0) || (dflt.mem.nwin > 0)) {
	    cistpl_mem_t *mem =
		(cfg->mem.nwin) ? &cfg->mem : &dflt.mem;
	    req.Attributes = WIN_DATA_WIDTH_16|WIN_MEMORY_TYPE_CM;
	    req.Attributes |= WIN_ENABLE;
	    req.Base = mem->win[0].host_addr;
	    req.Size = mem->win[0].len;
/* new in dummy.cs 2001/01/28 MN 
            if (req.Size < 0x1000)
                req.Size = 0x1000;
*/
	    req.AccessSpeed = 0;
	    if (pcmcia_request_window(&link->handle, &req, &link->win) != 0)
		goto next_entry;
	    map.Page = 0; map.CardOffset = mem->win[0].card_addr;
	    if (pcmcia_map_mem_page(link->win, &map) != 0)
		goto next_entry;
	}
	/* If we got this far, we're cool! */
	break;

    next_entry:
	CS_CHECK(GetNextTuple, pcmcia_get_next_tuple(handle, &tuple));
    }

    /*
       Allocate an interrupt line.  Note that this does not assign a
       handler to the interrupt, unless the 'Handler' member of the
       irq structure is initialized.
    */
    if (link->conf.Attributes & CONF_ENABLE_IRQ)
	CS_CHECK(RequestIRQ, pcmcia_request_irq(link->handle, &link->irq));
	
    /*
       This actually configures the PCMCIA socket -- setting up
       the I/O windows and the interrupt mapping, and putting the
       card and host interface into "Memory and IO" mode.
    */
    CS_CHECK(RequestConfiguration, pcmcia_request_configuration(link->handle, &link->conf));

    /*
      At this point, the dev_node_t structure(s) need to be
      initialized and arranged in a linked list at link->dev.
    */
    sprintf(dev->node.dev_name, "sedlbauer");
    dev->node.major = dev->node.minor = 0;
    link->dev_node = &dev->node;

    /* Finally, report what we've done */
    printk(KERN_INFO "%s: index 0x%02x:",
	   dev->node.dev_name, link->conf.ConfigIndex);
    if (link->conf.Vpp)
	printk(", Vpp %d.%d", link->conf.Vpp/10, link->conf.Vpp%10);
    if (link->conf.Attributes & CONF_ENABLE_IRQ)
	printk(", irq %d", link->irq.AssignedIRQ);
    if (link->io.NumPorts1)
	printk(", io 0x%04x-0x%04x", link->io.BasePort1,
	       link->io.BasePort1+link->io.NumPorts1-1);
    if (link->io.NumPorts2)
	printk(" & 0x%04x-0x%04x", link->io.BasePort2,
	       link->io.BasePort2+link->io.NumPorts2-1);
    if (link->win)
	printk(", mem 0x%06lx-0x%06lx", req.Base,
	       req.Base+req.Size-1);
    printk("\n");
    
    link->state &= ~DEV_CONFIG_PENDING;

    icard.para[0] = link->irq.AssignedIRQ;
    icard.para[1] = link->io.BasePort1;
    icard.protocol = protocol;
    icard.typ = ISDN_CTYPE_SEDLBAUER_PCMCIA;
    
    last_ret = hisax_init_pcmcia(link, &(((local_info_t*)link->priv)->stop), &icard);
    if (last_ret < 0) {
    	printk(KERN_ERR "sedlbauer_cs: failed to initialize SEDLBAUER PCMCIA %d at i/o %#x\n",
    		last_ret, link->io.BasePort1);
    	sedlbauer_release(link);
    } else
    	((local_info_t*)link->priv)->cardnr = last_ret;

    return;

cs_failed:
    cs_error(link->handle, last_fn, last_ret);
    sedlbauer_release(link);

} /* sedlbauer_config */

/*======================================================================

    After a card is removed, sedlbauer_release() will unregister the
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.
    
======================================================================*/

static void sedlbauer_release(dev_link_t *link)
{
    local_info_t *local = link->priv;
    DEBUG(0, "sedlbauer_release(0x%p)\n", link);

    if (local) {
    	if (local->cardnr >= 0) {
    	    /* no unregister function with hisax */
	    HiSax_closecard(local->cardnr);
	}
    }

    pcmcia_disable_device(link->handle);
} /* sedlbauer_release */

static int sedlbauer_suspend(struct pcmcia_device *p_dev)
{
	dev_link_t *link = dev_to_instance(p_dev);
	local_info_t *dev = link->priv;

	dev->stop = 1;

	return 0;
}

static int sedlbauer_resume(struct pcmcia_device *p_dev)
{
	dev_link_t *link = dev_to_instance(p_dev);
	local_info_t *dev = link->priv;

	dev->stop = 0;

	return 0;
}


static struct pcmcia_device_id sedlbauer_ids[] = {
	PCMCIA_DEVICE_PROD_ID123("SEDLBAUER", "speed star II", "V 3.1", 0x81fb79f5, 0xf3612e1d, 0x6b95c78a),
	PCMCIA_DEVICE_PROD_ID123("SEDLBAUER", "ISDN-Adapter", "4D67", 0x81fb79f5, 0xe4e9bc12, 0x397b7e90),
	PCMCIA_DEVICE_PROD_ID123("SEDLBAUER", "ISDN-Adapter", "4D98", 0x81fb79f5, 0xe4e9bc12, 0x2e5c7fce),
	PCMCIA_DEVICE_PROD_ID123("SEDLBAUER", "ISDN-Adapter", " (C) 93-94 VK", 0x81fb79f5, 0xe4e9bc12, 0x8db143fe),
	PCMCIA_DEVICE_PROD_ID123("SEDLBAUER", "ISDN-Adapter", " (c) 93-95 VK", 0x81fb79f5, 0xe4e9bc12, 0xb391ab4c),
	PCMCIA_DEVICE_PROD_ID12("HST High Soft Tech GmbH", "Saphir II B", 0xd79e0b84, 0x21d083ae),
/*	PCMCIA_DEVICE_PROD_ID1234("SEDLBAUER", 0x81fb79f5), */ /* too generic*/
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, sedlbauer_ids);

static struct pcmcia_driver sedlbauer_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "sedlbauer_cs",
	},
	.probe		= sedlbauer_attach,
	.remove		= sedlbauer_detach,
	.id_table	= sedlbauer_ids,
	.suspend	= sedlbauer_suspend,
	.resume		= sedlbauer_resume,
};

static int __init init_sedlbauer_cs(void)
{
	return pcmcia_register_driver(&sedlbauer_driver);
}

static void __exit exit_sedlbauer_cs(void)
{
	pcmcia_unregister_driver(&sedlbauer_driver);
}

module_init(init_sedlbauer_cs);
module_exit(exit_sedlbauer_cs);
