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
#include <linux/netdevice.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

/*====================================================================*/

MODULE_AUTHOR("Wai Chan");
MODULE_DESCRIPTION("FT1000 PCMCIA driver");
MODULE_LICENSE("GPL");

/*====================================================================*/

static int ft1000_config(struct pcmcia_device *link);
static void ft1000_detach(struct pcmcia_device *link);
static int ft1000_attach(struct pcmcia_device *link);

#include "ft1000.h"

/*====================================================================*/

static void ft1000_reset(struct pcmcia_device *link)
{
	pcmcia_reset_card(link->socket);
}

static int ft1000_attach(struct pcmcia_device *link)
{
	link->priv = NULL;
	link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;

	return ft1000_config(link);
}

static void ft1000_detach(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (dev)
		stop_ft1000_card(dev);

	pcmcia_disable_device(link);
	free_netdev(dev);
}

static int ft1000_confcheck(struct pcmcia_device *link, void *priv_data)
{
	return pcmcia_request_io(link);
}

/*======================================================================

  ft1000_config() is scheduled to run after a CARD_INSERTION event
  is received, to configure the PCMCIA socket, and to make the
  device available to the system.

  ======================================================================*/

static int ft1000_config(struct pcmcia_device *link)
{
	int ret;

	dev_dbg(&link->dev, "ft1000_cs: ft1000_config(0x%p)\n", link);

	/* setup IO window */
	ret = pcmcia_loop_config(link, ft1000_confcheck, NULL);
	if (ret) {
		dev_err(&link->dev, "Could not configure pcmcia\n");
		return -ENODEV;
	}

	/* configure device */
	ret = pcmcia_enable_device(link);
	if (ret) {
		dev_err(&link->dev, "Could not enable pcmcia\n");
		goto failed;
	}

	link->priv = init_ft1000_card(link, &ft1000_reset);
	if (!link->priv) {
		dev_err(&link->dev, "Could not register as network device\n");
		goto failed;
	}

	/* Finally, report what we've done */

	return 0;
failed:
	pcmcia_disable_device(link);
	return -ENODEV;
}

static int ft1000_suspend(struct pcmcia_device *link)
{
	struct net_device *dev = link->priv;

	if (link->open)
		netif_device_detach(dev);
	return 0;
}

static int ft1000_resume(struct pcmcia_device *link)
{
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
	.owner		= THIS_MODULE,
	.name		= "ft1000_cs",
	.probe		= ft1000_attach,
	.remove		= ft1000_detach,
	.id_table	= ft1000_ids,
	.suspend	= ft1000_suspend,
	.resume		= ft1000_resume,
};

module_pcmcia_driver(ft1000_cs_driver);
