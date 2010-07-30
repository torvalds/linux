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

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include <linux/io.h>
#include <asm/system.h>

#include "airo.h"


/*====================================================================*/

MODULE_AUTHOR("Benjamin Reed");
MODULE_DESCRIPTION("Support for Cisco/Aironet 802.11 wireless ethernet "
		   "cards.  This is the module that links the PCMCIA card "
		   "with the airo module.");
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

typedef struct local_info_t {
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

	dev_dbg(&p_dev->dev, "airo_attach()\n");

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
	dev_dbg(&link->dev, "airo_detach\n");

	airo_release(link);

	if (((local_info_t *)link->priv)->eth_dev) {
		stop_airo_card(((local_info_t *)link->priv)->eth_dev, 0);
	}
	((local_info_t *)link->priv)->eth_dev = NULL;

	kfree(link->priv);
} /* airo_detach */

/*======================================================================

  airo_config() is scheduled to run after a CARD_INSERTION event
  is received, to configure the PCMCIA socket, and to make the
  device available to the system.

  ======================================================================*/

static int airo_cs_config_check(struct pcmcia_device *p_dev,
				cistpl_cftable_entry_t *cfg,
				cistpl_cftable_entry_t *dflt,
				void *priv_data)
{
	if (cfg->index == 0)
		return -ENODEV;

	/* IO window settings */
	p_dev->resource[0]->end = p_dev->resource[1]->end = 0;
	if ((cfg->io.nwin > 0) || (dflt->io.nwin > 0)) {
		cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt->io;
		p_dev->resource[0]->flags &= ~IO_DATA_PATH_WIDTH;
		p_dev->resource[0]->flags |=
					pcmcia_io_cfg_data_width(io->flags);
		p_dev->resource[0]->start = io->win[0].base;
		p_dev->resource[0]->end = io->win[0].len;
		if (io->nwin > 1) {
			p_dev->resource[1]->flags = p_dev->resource[0]->flags;
			p_dev->resource[1]->start = io->win[1].base;
			p_dev->resource[1]->end = io->win[1].len;
		}
	}

	/* This reserves IO space but doesn't actually enable it */
	if (pcmcia_request_io(p_dev) != 0)
		return -ENODEV;

	/* If we got this far, we're cool! */
	return 0;
}


static int airo_config(struct pcmcia_device *link)
{
	local_info_t *dev;
	int ret;

	dev = link->priv;

	dev_dbg(&link->dev, "airo_config\n");

	link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_VPP |
		CONF_AUTO_AUDIO;

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
	ret = pcmcia_loop_config(link, airo_cs_config_check, NULL);
	if (ret)
		goto failed;

	if (!link->irq)
		goto failed;

	/*
	  This actually configures the PCMCIA socket -- setting up
	  the I/O windows and the interrupt mapping, and putting the
	  card and host interface into "Memory and IO" mode.
	*/
	ret = pcmcia_enable_device(link);
	if (ret)
		goto failed;
	((local_info_t *)link->priv)->eth_dev =
		init_airo_card(link->irq,
			       link->resource[0]->start, 1, &link->dev);
	if (!((local_info_t *)link->priv)->eth_dev)
		goto failed;

	/* Finally, report what we've done */
	dev_info(&link->dev, "index 0x%02x: ",
	       link->config_index);
	if (link->vpp)
		printk(", Vpp %d.%d", link->vpp/10, link->vpp%10);
	printk(", irq %d", link->irq);
	if (link->resource[0])
		printk(" & %pR", link->resource[0]);
	if (link->resource[1])
		printk(" & %pR", link->resource[1]);
	printk("\n");
	return 0;

 failed:
	airo_release(link);
	return -ENODEV;
} /* airo_config */

/*======================================================================

  After a card is removed, airo_release() will unregister the
  device, and release the PCMCIA configuration.  If the device is
  still open, this will be postponed until it is closed.

  ======================================================================*/

static void airo_release(struct pcmcia_device *link)
{
	dev_dbg(&link->dev, "airo_release\n");
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
