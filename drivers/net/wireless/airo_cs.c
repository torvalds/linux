/*======================================================================

    Aironet driver for 4500 and 4800 series cards

    This code is released under both the GPL version 2 and BSD licenses.
    Either license may be used.  The respective licenses are found at
    the end of this file.

    This code was developed by Benjamin Reed <breed@users.sourceforge.net>
    including portions of which come from the Aironet PC4500
    Developer's Reference Manual and used with permission.  Copyright
    (C) 1999 Benjamin Reed.  All Rights Reserved.  Permission to use
    code in the Developer's manual was granted for this driver by
    Aironet.

    In addition this module was derived from dummy_cs.
    The initial developer of dummy_cs is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.    
    
======================================================================*/

#ifdef __IN_PCMCIA_PACKAGE__
#include <pcmcia/k_compat.h>
#endif
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/netdevice.h>

#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include <asm/io.h>
#include <asm/system.h>

#include "airo.h"

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
static char *version = "$Revision: 1.2 $";
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args);
#else
#define DEBUG(n, args...)
#endif

/*====================================================================*/

MODULE_AUTHOR("Benjamin Reed");
MODULE_DESCRIPTION("Support for Cisco/Aironet 802.11 wireless ethernet \
                   cards.  This is the module that links the PCMCIA card \
		   with the airo module.");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_SUPPORTED_DEVICE("Aironet 4500, 4800 and Cisco 340 PCMCIA cards");

/*====================================================================*/

/*
   The event() function is this driver's Card Services event handler.
   It will be called by Card Services when an appropriate card status
   event is received.  The config() and release() entry points are
   used to configure or release a socket, in response to card
   insertion and ejection events.  They are invoked from the airo_cs
   event handler. 
*/

static int airo_config(struct pcmcia_device *link);
static void airo_release(struct pcmcia_device *link);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static void airo_detach(struct pcmcia_device *p_dev);

/*
   You'll also need to prototype all the functions that will actually
   be used to talk to your device.  See 'pcmem_cs' for a good example
   of a fully self-sufficient driver; the other drivers rely more or
   less on other parts of the kernel.
*/

/*
   A linked list of "instances" of the  aironet device.  Each actual
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
   because they generally shouldn't be allocated dynamically.

   In this case, we also provide a flag to indicate if a device is
   "stopped" due to a power management event, or card ejection.  The
   device IO routines can use a flag like this to throttle IO to a
   card that is not ready to accept it.
*/
   
typedef struct local_info_t {
	dev_node_t	node;
	struct net_device *eth_dev;
} local_info_t;

/*======================================================================
  
  airo_attach() creates an "instance" of the driver, allocating
  local data structures for one device.  The device is registered
  with Card Services.
  
  The dev_link structure is initialized, but we don't actually
  configure the card at this point -- we wait until we receive a
  card insertion event.
  
  ======================================================================*/

static int airo_probe(struct pcmcia_device *p_dev)
{
	local_info_t *local;

	DEBUG(0, "airo_attach()\n");

	/* Interrupt setup */
	p_dev->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
	p_dev->irq.IRQInfo1 = IRQ_LEVEL_ID;
	p_dev->irq.Handler = NULL;
	
	/*
	  General socket configuration defaults can go here.  In this
	  client, we assume very little, and rely on the CIS for almost
	  everything.  In most clients, many details (i.e., number, sizes,
	  and attributes of IO windows) are fixed by the nature of the
	  device, and can be hard-wired here.
	*/
	p_dev->conf.Attributes = 0;
	p_dev->conf.IntType = INT_MEMORY_AND_IO;
	
	/* Allocate space for private device-specific data */
	local = kzalloc(sizeof(local_info_t), GFP_KERNEL);
	if (!local) {
		printk(KERN_ERR "airo_cs: no memory for new device\n");
		return -ENOMEM;
	}
	p_dev->priv = local;

	return airo_config(p_dev);
} /* airo_attach */

/*======================================================================
  
  This deletes a driver "instance".  The device is de-registered
  with Card Services.  If it has been released, all local data
  structures are freed.  Otherwise, the structures will be freed
  when the device is released.
  
  ======================================================================*/

static void airo_detach(struct pcmcia_device *link)
{
	DEBUG(0, "airo_detach(0x%p)\n", link);

	airo_release(link);

	if ( ((local_info_t*)link->priv)->eth_dev ) {
		stop_airo_card( ((local_info_t*)link->priv)->eth_dev, 0 );
	}
	((local_info_t*)link->priv)->eth_dev = NULL;

	kfree(link->priv);
} /* airo_detach */

/*======================================================================
  
  airo_config() is scheduled to run after a CARD_INSERTION event
  is received, to configure the PCMCIA socket, and to make the
  device available to the system.
  
  ======================================================================*/

#define CS_CHECK(fn, ret) \
do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

struct airo_cs_config_data {
	cistpl_cftable_entry_t dflt;
	win_req_t req;
};

static int airo_cs_config_check(struct pcmcia_device *p_dev,
				cistpl_cftable_entry_t *cfg,
				void *priv_data)
{
	struct airo_cs_config_data *cfg_mem = priv_data;

	if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
		cfg_mem->dflt = *cfg;

	if (cfg->index == 0)
		return -ENODEV;

	/* Does this card need audio output? */
	if (cfg->flags & CISTPL_CFTABLE_AUDIO) {
		p_dev->conf.Attributes |= CONF_ENABLE_SPKR;
		p_dev->conf.Status = CCSR_AUDIO_ENA;
	}

	/* Use power settings for Vcc and Vpp if present */
	/*  Note that the CIS values need to be rescaled */
	if (cfg->vpp1.present & (1<<CISTPL_POWER_VNOM))
		p_dev->conf.Vpp = cfg->vpp1.param[CISTPL_POWER_VNOM]/10000;
	else if (cfg_mem->dflt.vpp1.present & (1<<CISTPL_POWER_VNOM))
		p_dev->conf.Vpp = cfg_mem->dflt.vpp1.param[CISTPL_POWER_VNOM]/10000;

	/* Do we need to allocate an interrupt? */
	if (cfg->irq.IRQInfo1 || cfg_mem->dflt.irq.IRQInfo1)
		p_dev->conf.Attributes |= CONF_ENABLE_IRQ;

	/* IO window settings */
	p_dev->io.NumPorts1 = p_dev->io.NumPorts2 = 0;
	if ((cfg->io.nwin > 0) || (cfg_mem->dflt.io.nwin > 0)) {
		cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &cfg_mem->dflt.io;
		p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
		if (!(io->flags & CISTPL_IO_8BIT))
			p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
		if (!(io->flags & CISTPL_IO_16BIT))
			p_dev->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
		p_dev->io.BasePort1 = io->win[0].base;
		p_dev->io.NumPorts1 = io->win[0].len;
		if (io->nwin > 1) {
			p_dev->io.Attributes2 = p_dev->io.Attributes1;
			p_dev->io.BasePort2 = io->win[1].base;
			p_dev->io.NumPorts2 = io->win[1].len;
		}
	}

	/* This reserves IO space but doesn't actually enable it */
	if (pcmcia_request_io(p_dev, &p_dev->io) != 0)
		return -ENODEV;

	/*
	  Now set up a common memory window, if needed.  There is room
	  in the struct pcmcia_device structure for one memory window handle,
	  but if the base addresses need to be saved, or if multiple
	  windows are needed, the info should go in the private data
	  structure for this device.

	  Note that the memory window base is a physical address, and
	  needs to be mapped to virtual space with ioremap() before it
	  is used.
	*/
	if ((cfg->mem.nwin > 0) || (cfg_mem->dflt.mem.nwin > 0)) {
		cistpl_mem_t *mem = (cfg->mem.nwin) ? &cfg->mem : &cfg_mem->dflt.mem;
		memreq_t map;
		cfg_mem->req.Attributes = WIN_DATA_WIDTH_16|WIN_MEMORY_TYPE_CM;
		cfg_mem->req.Base = mem->win[0].host_addr;
		cfg_mem->req.Size = mem->win[0].len;
		cfg_mem->req.AccessSpeed = 0;
		if (pcmcia_request_window(&p_dev, &cfg_mem->req, &p_dev->win) != 0)
			return -ENODEV;
		map.Page = 0;
		map.CardOffset = mem->win[0].card_addr;
		if (pcmcia_map_mem_page(p_dev->win, &map) != 0)
			return -ENODEV;
	}
	/* If we got this far, we're cool! */
	return 0;
}


static int airo_config(struct pcmcia_device *link)
{
	local_info_t *dev;
	struct airo_cs_config_data *cfg_mem;
	int last_fn, last_ret;

	dev = link->priv;

	DEBUG(0, "airo_config(0x%p)\n", link);

	cfg_mem = kzalloc(sizeof(struct airo_cs_config_data), GFP_KERNEL);
	if (!cfg_mem)
		return -ENOMEM;

	/*
	 * In this loop, we scan the CIS for configuration table
	 * entries, each of which describes a valid card
	 * configuration, including voltage, IO window, memory window,
	 * and interrupt settings.
	 *
	 * We make no assumptions about the card to be configured: we
	 * use just the information available in the CIS.  In an ideal
	 * world, this would work for any PCMCIA card, but it requires
	 * a complete and accurate CIS.  In practice, a driver usually
	 * "knows" most of these things without consulting the CIS,
	 * and most client drivers will only use the CIS to fill in
	 * implementation-defined details.
	 */
	last_ret = pcmcia_loop_config(link, airo_cs_config_check, cfg_mem);
	if (last_ret)
		goto failed;

	/*
	  Allocate an interrupt line.  Note that this does not assign a
	  handler to the interrupt, unless the 'Handler' member of the
	  irq structure is initialized.
	*/
	if (link->conf.Attributes & CONF_ENABLE_IRQ)
		CS_CHECK(RequestIRQ, pcmcia_request_irq(link, &link->irq));
	
	/*
	  This actually configures the PCMCIA socket -- setting up
	  the I/O windows and the interrupt mapping, and putting the
	  card and host interface into "Memory and IO" mode.
	*/
	CS_CHECK(RequestConfiguration, pcmcia_request_configuration(link, &link->conf));
	((local_info_t*)link->priv)->eth_dev = 
		init_airo_card( link->irq.AssignedIRQ,
				link->io.BasePort1, 1, &handle_to_dev(link) );
	if (!((local_info_t*)link->priv)->eth_dev) goto cs_failed;
	
	/*
	  At this point, the dev_node_t structure(s) need to be
	  initialized and arranged in a linked list at link->dev_node.
	*/
	strcpy(dev->node.dev_name, ((local_info_t*)link->priv)->eth_dev->name );
	dev->node.major = dev->node.minor = 0;
	link->dev_node = &dev->node;
	
	/* Finally, report what we've done */
	printk(KERN_INFO "%s: index 0x%02x: ",
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
		printk(", mem 0x%06lx-0x%06lx", cfg_mem->req.Base,
		       cfg_mem->req.Base+cfg_mem->req.Size-1);
	printk("\n");
	kfree(cfg_mem);
	return 0;

 cs_failed:
	cs_error(link, last_fn, last_ret);
 failed:
	airo_release(link);
	kfree(cfg_mem);
	return -ENODEV;
} /* airo_config */

/*======================================================================
  
  After a card is removed, airo_release() will unregister the
  device, and release the PCMCIA configuration.  If the device is
  still open, this will be postponed until it is closed.
  
  ======================================================================*/

static void airo_release(struct pcmcia_device *link)
{
	DEBUG(0, "airo_release(0x%p)\n", link);
	pcmcia_disable_device(link);
}

static int airo_suspend(struct pcmcia_device *link)
{
	local_info_t *local = link->priv;

	netif_device_detach(local->eth_dev);

	return 0;
}

static int airo_resume(struct pcmcia_device *link)
{
	local_info_t *local = link->priv;

	if (link->open) {
		reset_airo_card(local->eth_dev);
		netif_device_attach(local->eth_dev);
	}

	return 0;
}

static struct pcmcia_device_id airo_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x015f, 0x000a),
	PCMCIA_DEVICE_MANF_CARD(0x015f, 0x0005),
	PCMCIA_DEVICE_MANF_CARD(0x015f, 0x0007),
	PCMCIA_DEVICE_MANF_CARD(0x0105, 0x0007),
	PCMCIA_DEVICE_NULL,
};
MODULE_DEVICE_TABLE(pcmcia, airo_ids);

static struct pcmcia_driver airo_driver = {
	.owner		= THIS_MODULE,
	.drv		= {
		.name	= "airo_cs",
	},
	.probe		= airo_probe,
	.remove		= airo_detach,
	.id_table       = airo_ids,
	.suspend	= airo_suspend,
	.resume		= airo_resume,
};

static int airo_cs_init(void)
{
	return pcmcia_register_driver(&airo_driver);
}

static void airo_cs_cleanup(void)
{
	pcmcia_unregister_driver(&airo_driver);
}

/*
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    In addition:

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions
    are met:

    1. Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
    3. The name of the author may not be used to endorse or promote
       products derived from this software without specific prior written
       permission.

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
    WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
    INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
    (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
    SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
    HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
    STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
    IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.    
*/

module_init(airo_cs_init);
module_exit(airo_cs_cleanup);
