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

//#include <pcmcia/version.h>		// Slavius 21.10.2009 removed from kernel
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>

#include "ft1000_cs.h"			// Slavius 21.10.2009 because CS_SUCCESS constant is missing due to removed pcmcia/version.h

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

struct net_device *init_ft1000_card(int, int, unsigned char *,
					void *ft1000_reset, struct pcmcia_device * link,
					struct device *fdev);
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
	conf_reg_t reg;

	DEBUG(0, "ft1000_cs:ft1000_reset is called................\n");

	/* Soft-Reset card */
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	reg.Value = COR_SOFT_RESET;
	pcmcia_access_configuration_register(link, &reg);

	/* Wait until the card has acknowledged our reset */
	udelay(2);

	/* Restore original COR configuration index */
	/* Need at least 2 write to respond */
	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	reg.Value = COR_DEFAULT;
	pcmcia_access_configuration_register(link, &reg);

	/* Wait until the card has finished restarting */
	udelay(1);

	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	reg.Value = COR_DEFAULT;
	pcmcia_access_configuration_register(link, &reg);

	/* Wait until the card has finished restarting */
	udelay(1);

	reg.Action = CS_WRITE;
	reg.Offset = CISREG_COR;
	reg.Value = COR_DEFAULT;
	pcmcia_access_configuration_register(link, &reg);

	/* Wait until the card has finished restarting */
	udelay(1);

}

/*====================================================================*/

static int get_tuple_first(struct pcmcia_device *link, tuple_t * tuple,
			   cisparse_t * parse)
{
	int i;
	i = pcmcia_get_first_tuple(link, tuple);
	if (i != CS_SUCCESS)
		return i;
	i = pcmcia_get_tuple_data(link, tuple);
	if (i != CS_SUCCESS)
		return i;
	return pcmcia_parse_tuple(tuple, parse);	// Slavius 21.10.2009 removed unused link parameter
}

static int get_tuple_next(struct pcmcia_device *link, tuple_t * tuple,
			  cisparse_t * parse)
{
	int i;
	i = pcmcia_get_next_tuple(link, tuple);
	if (i != CS_SUCCESS)
		return i;
	i = pcmcia_get_tuple_data(link, tuple);
	if (i != CS_SUCCESS)
		return i;
	return pcmcia_parse_tuple(tuple, parse);	// Slavius 21.10.2009 removed unused link parameter
}

/*======================================================================


======================================================================*/

static int ft1000_attach(struct pcmcia_device *link)
{

	local_info_t *local;

	DEBUG(0, "ft1000_cs: ft1000_attach()\n");

	local = kmalloc(sizeof(local_info_t), GFP_KERNEL);
	if (!local) {
		return -ENOMEM;
	}
	memset(local, 0, sizeof(local_info_t));
	local->link = link;

	link->priv = local;
	local->dev = NULL;

	link->irq.Attributes = IRQ_TYPE_EXCLUSIVE;
	link->irq.IRQInfo1 = IRQ_LEVEL_ID;
	link->conf.Attributes = CONF_ENABLE_IRQ;
	link->conf.IntType = INT_MEMORY_AND_IO;
	link->irq.Handler = NULL;

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

	ft1000_release(link);

	/* This points to the parent local_info_t struct */
	free_netdev(dev);

}				/* ft1000_detach */

/*======================================================================

    ft1000_config() is scheduled to run after a CARD_INSERTION event
    is received, to configure the PCMCIA socket, and to make the
    device available to the system.

======================================================================*/

#define CS_CHECK(fn, ret) \
	do { last_fn = (fn); if ((last_ret = (ret)) != 0) goto cs_failed; } while (0)

#define CFG_CHECK(fn, ret) \
	last_fn = (fn); if ((last_ret = (ret)) != 0) goto next_entry

static int ft1000_config(struct pcmcia_device * link)
{
	tuple_t tuple;
	cisparse_t parse;
	int last_fn, last_ret, i;
	u_char buf[64];
	cistpl_lan_node_id_t *node_id;
	cistpl_cftable_entry_t dflt = { 0 };
	cistpl_cftable_entry_t *cfg;
	unsigned char mac_address[6];

	DEBUG(0, "ft1000_cs: ft1000_config(0x%p)\n", link);

	/*
	   This reads the card's CONFIG tuple to find its configuration
	   registers.
	 */
//	tuple.DesiredTuple = CISTPL_CONFIG;
//	tuple.Attributes = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);
	tuple.TupleOffset = 0;
//	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(link, &tuple));
//	CS_CHECK(GetTupleData, pcmcia_get_tuple_data(link, &tuple));
//	CS_CHECK(ParseTuple, pcmcia_parse_tuple(link, &tuple, &parse));
//	link->conf.ConfigBase = parse.config.base;
//	link->conf.Present = parse.config.rmask[0];

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
	tuple.Attributes = 0;
	CS_CHECK(GetFirstTuple, pcmcia_get_first_tuple(link, &tuple));
	while (1) {
		cfg = &(parse.cftable_entry);
		CFG_CHECK(GetTupleData, pcmcia_get_tuple_data(link, &tuple));
		CFG_CHECK(ParseTuple,
			  pcmcia_parse_tuple(&tuple, &parse));		// Slavius 21.10.2009 removed unused link parameter

		if (cfg->flags & CISTPL_CFTABLE_DEFAULT)
			dflt = *cfg;
		if (cfg->index == 0)
			goto next_entry;
		link->conf.ConfigIndex = cfg->index;

		/* Do we need to allocate an interrupt? */
		if (cfg->irq.IRQInfo1 || dflt.irq.IRQInfo1)
			link->conf.Attributes |= CONF_ENABLE_IRQ;

		/* IO window settings */
		link->io.NumPorts1 = link->io.NumPorts2 = 0;
		if ((cfg->io.nwin > 0) || (dflt.io.nwin > 0)) {
			cistpl_io_t *io = (cfg->io.nwin) ? &cfg->io : &dflt.io;
			link->io.Attributes1 = IO_DATA_PATH_WIDTH_AUTO;
			if (!(io->flags & CISTPL_IO_8BIT)) {
				DEBUG(0, "ft1000_cs: IO_DATA_PATH_WIDTH_16\n");
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_16;
			}
			if (!(io->flags & CISTPL_IO_16BIT)) {
				DEBUG(0, "ft1000_cs: IO_DATA_PATH_WIDTH_8\n");
				link->io.Attributes1 = IO_DATA_PATH_WIDTH_8;
			}
			link->io.IOAddrLines = io->flags & CISTPL_IO_LINES_MASK;
			link->io.BasePort1 = io->win[0].base;
			link->io.NumPorts1 = io->win[0].len;
			if (io->nwin > 1) {
				link->io.Attributes2 = link->io.Attributes1;
				link->io.BasePort2 = io->win[1].base;
				link->io.NumPorts2 = io->win[1].len;
			}
			/* This reserves IO space but doesn't actually enable it */
			pcmcia_request_io(link, &link->io);
		}

		break;

	 next_entry:
		last_ret = pcmcia_get_next_tuple(link, &tuple);
	}
	if (last_ret != CS_SUCCESS) {
		cs_error(link, RequestIO, last_ret);
		goto failed;
	}

	/*
	   Allocate an interrupt line.  Note that this does not assign a
	   handler to the interrupt, unless the 'Handler' member of the
	   irq structure is initialized.
	 */
		CS_CHECK(RequestIRQ, pcmcia_request_irq(link, &link->irq));

	/*
	   This actually configures the PCMCIA socket -- setting up
	   the I/O windows and the interrupt mapping, and putting the
	   card and host interface into "Memory and IO" mode.
	 */
	CS_CHECK(RequestConfiguration,
		 pcmcia_request_configuration(link, &link->conf));

	/* Get MAC address from tuples */

	tuple.Attributes = tuple.TupleOffset = 0;
	tuple.TupleData = buf;
	tuple.TupleDataMax = sizeof(buf);

	/* Check for a LAN function extension tuple */
	tuple.DesiredTuple = CISTPL_FUNCE;
	i = get_tuple_first(link, &tuple, &parse);
	while (i == CS_SUCCESS) {
		if (parse.funce.type == CISTPL_FUNCE_LAN_NODE_ID)
			break;
		i = get_tuple_next(link, &tuple, &parse);
	}

	if (i == CS_SUCCESS) {
		node_id = (cistpl_lan_node_id_t *) parse.funce.data;
		if (node_id->nb == 6) {
			for (i = 0; i < 6; i++)
				mac_address[i] = node_id->id[i];
		}
	}

	((local_info_t *) link->priv)->dev =
		init_ft1000_card(link->irq.AssignedIRQ, link->io.BasePort1,
				 &mac_address[0], ft1000_reset, link,
				 &handle_to_dev(link));

	/*
	   At this point, the dev_node_t structure(s) need to be
	   initialized and arranged in a linked list at link->dev.
	 */

	/* Finally, report what we've done */

	return 0;

cs_failed:
	cs_error(link, last_fn, last_ret);
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

	/* Unlink the device chain */
	link->dev_node = NULL;

	/*
	   In a normal driver, additional code may be needed to release
	   other kernel data structures associated with this device.
	 */

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

static struct pcmcia_device_id ft1000_ids[] = {
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
