/*---------------------------------------------------------------------------
   FT1000 driver for Flarion Flash OFDM NIC Device

   Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
   Copyright (C) 2002 Flarion Technologies, All rights reserved.
   Copyright (C) 2006 Patrik Ostrihon, All rights reserved.
   Copyright (C) 2006 ProWeb Consulting, a.s, All rights reserved.

   The initial developer of the original code is David A. Hinds
   <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds.

   This file was modified to support the Flarion Flash OFDM NIC Device
   by Wai Chan (w.chan@flarion.com).

   Port for kernel 2.6 created by Patrik Ostrihon (patrik.ostrihon@pwc.sk)

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your option) any
   later version. This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details. You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place -
   Suite 330, Boston, MA 02111-1307, USA.
-----------------------------------------------------------------------------*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/ioport.h>
#include <linux/delay.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

/*====================================================================*/

/* Module parameters */

#define INT_MODULE_PARM(n, v) static int n = v; MODULE_PARM(n, "i")

MODULE_AUTHOR("Wai Chan");
MODULE_DESCRIPTION("FT1000 PCMCIA driver");
MODULE_LICENSE("GPL");

/* Newer, simpler way of listing specific interrupts */

/* The old way: bit map of interrupts to choose from */
/* This means pick from 15, 14, 12, 11, 10, 9, 7, 5, 4, and 3 */

/*
   All the PCMCIA modules use PCMCIA_DEBUG to control debugging.  If
   you do not define PCMCIA_DEBUG at all, all the debug code will be
   left out.  If you compile with PCMCIA_DEBUG=0, the debug code will
   be present but disabled.
*/
#ifdef FT_DEBUG
#define DEBUG(n, args...) printk(KERN_DEBUG args)
#else
#define DEBUG(n, args...)
#endif

/*====================================================================*/

struct net_device *init_ft1000_card(struct pcmcia_device *link,
					void *ft1000_reset);
void stop_ft1000_card(struct net_device *);

static int ft1000_config(struct pcmcia_device *link);
static void ft1000_release(struct pcmcia_device *link);

/*
   The attach() and detach() entry points are used to create and destroy
   "instances" of the driver, where each instance represents everything
   needed to manage one actual PCMCIA card.
*/

static void ft1000_detach(struct pcmcia_device *link);
static int  ft1000_attach(struct pcmcia_device *link);

typedef struct local_info_t {
	struct pcmcia_device *link;
	struct net_device *dev;
} local_info_t;

#define MAX_ASIC_RESET_CNT     10
#define COR_DEFAULT            0x55

/*====================================================================*/

static void ft1000_reset(struct pcmcia_device * link)
{
	pcmcia_reset_card(link->socket);
}

/*======================================================================


======================================================================*/

static int ft1000_attach(struct pcmcia_device *link)
{

	local_info_t *local;

	DEBUG(0, "ft1000_cs: ft1000_attach()\n");

	local = kzalloc(sizeof(local_info_t), GFP_KERNEL);
	if (!local) {
		return -ENOMEM;
	}
	local->link = link;

	link->priv = local;
	local->dev = NULL;

	link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;

	return ft1000_config(link);

}				/* ft1000_attach */

/*======================================================================

    This deletes a driver "instance".  The device is de-registered
    with Card Services.  If it has been released, all local data
    structures are freed.  Otherwise, the structures will be freed
    when the device is released.

======================================================================*/

static void ft1000_detach(struct pcmcia_device *link)
{
	struct net_device *dev = ((local_info_t *) link->priv)->dev;

	DEBUG(0, "ft1000_cs: ft1000_detach(0x%p)\n", link);

	if (link == NULL) {
		DEBUG(0,"ft1000_cs:ft1000_detach: Got a NULL pointer\n");
		return;
	}

	if (dev) {
		stop_ft1000_card(dev);
	}

	pcmcia_disable_device(link);

	/* This points to the parent local_info_t struct */
	free_netdev(dev);

}				/* ft1000_detach */

/*======================================================================

   Check if the io window is configured

======================================================================*/
int ft1000_confcheck(struct pcmcia_device *link, void *priv_data)
{

	return pcmcia_request_io(link);
}				/* ft1000_confcheck */

/*======================================================================

    ft1000_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    device available to the system.

======================================================================*/

static int ft1000_config(struct pcmcia_device *link)
{
	int  ret;

	dev_dbg(&link->dev, "ft1000_cs: ft1000_config(0x%p)\n", link);

	/* setup IO window */
	ret = pcmcia_loop_config(link, ft1000_confcheck, NULL);
	if (ret) {
		printk(KERN_INFO "ft1000: Could not configure pcmcia\n");
		return -ENODEV;
	}

	/* configure device */
	ret = pcmcia_enable_device(link);
	if (ret) {
		printk(KERN_INFO "ft1000: could not enable pcmcia\n");
		goto failed;
	}

	((local_info_t *) link->priv)->dev = init_ft1000_card(link,
								&ft1000_reset);
	if (((local_info_t *) link->priv)->dev == NULL) {
		printk(KERN_INFO "ft1000: Could not register as network device\n");
		goto failed;
	}

	/* Finally, report what we've done */

	return 0;
failed:
	ft1000_release(link);
	return -ENODEV;

}				/* ft1000_config */

/*======================================================================

    After a card is removed, ft1000_release() will unregister the
    device, and release the PCMCIA configuration.  If the device is
    still open, this will be postponed until it is closed.

======================================================================*/

static void ft1000_release(struct pcmcia_device * link)
{

	DEBUG(0, "ft1000_cs: ft1000_release(0x%p)\n", link);

	/*
	   If the device is currently in use, we won't release until it
	   is actually closed, because until then, we can't be sure that
	   no one will try to access the device or its data structures.
	 */

	/*
	   In a normal driver, additional code may be needed to release
	   other kernel data structures associated with this device.
	 */
	kfree((local_info_t *) link->priv);
	/* Don't bother checking to see if these succeed or not */

	 pcmcia_disable_device(link);
}				/* ft1000_release */

/*======================================================================

    The card status event handler.  Mostly, this schedules other
    stuff to run after an event is received.

    When a CARD_REMOVAL event is received, we immediately set a
    private flag to block future accesses to this device.  All the
    functions that actually access the device should check this flag
    to make sure the card is still present.

======================================================================*/

static int ft1000_suspend(struct pcmcia_device *link)
{
	struct net_device *dev = ((local_info_t *) link->priv)->dev;

	DEBUG(1, "ft1000_cs: ft1000_event(0x%06x)\n", event);

	if (link->open)
		netif_device_detach(dev);
	return 0;
}

static int ft1000_resume(struct pcmcia_device *link)
{
/*	struct net_device *dev = link->priv;
 */
	return 0;
}



/*====================================================================*/

static const struct pcmcia_device_id ft1000_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x02cc, 0x0100),
	PCMCIA_DEVICE_MANF_CARD(0x02cc, 0x1000),
	PCMCIA_DEVICE_MANF_CARD(0x02cc, 0x1300),
	PCMCIA_DEVICE_NULL,
};

MODULE_DEVICE_TABLE(pcmcia, ft1000_ids);

static struct pcmcia_driver ft1000_cs_driver = {
	.owner = THIS_MODULE,
	.drv = {
		.name = "ft1000_cs",
		},
	.probe      = ft1000_attach,
	.remove     = ft1000_detach,
	.id_table	= ft1000_ids,
	.suspend    = ft1000_suspend,
	.resume     = ft1000_resume,
};

static int __init init_ft1000_cs(void)
{
	DEBUG(0, "ft1000_cs: loading\n");
	return pcmcia_register_driver(&ft1000_cs_driver);
}

static void __exit exit_ft1000_cs(void)
{
	DEBUG(0, "ft1000_cs: unloading\n");
	pcmcia_unregister_driver(&ft1000_cs_driver);
}

module_init(init_ft1000_cs);
module_exit(exit_ft1000_cs);
