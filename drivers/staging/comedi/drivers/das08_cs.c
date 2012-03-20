/*
    comedi/drivers/das08_cs.c
    DAS08 driver

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>
    Copyright (C) 2001,2002,2003 Frank Mori Hess <fmhess@users.sourceforge.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*****************************************************************

*/
/*
Driver: das08_cs
Description: DAS-08 PCMCIA boards
Author: Warren Jasper, ds, Frank Hess
Devices: [ComputerBoards] PCM-DAS08 (pcm-das08)
Status: works

This is the PCMCIA-specific support split off from the
das08 driver.

Options (for pcm-das08):
	NONE

Command support does not exist, but could be added for this board.
*/

#include "../comedidev.h"

#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include "das08.h"

/* pcmcia includes */
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

static struct pcmcia_device *cur_dev;

#define thisboard ((const struct das08_board_struct *)dev->board_ptr)

static int das08_cs_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it);

static struct comedi_driver driver_das08_cs = {
	.driver_name = "das08_cs",
	.module = THIS_MODULE,
	.attach = das08_cs_attach,
	.detach = das08_common_detach,
	.board_name = &das08_cs_boards[0].name,
	.num_names = ARRAY_SIZE(das08_cs_boards),
	.offset = sizeof(struct das08_board_struct),
};

static int das08_cs_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it)
{
	int ret;
	unsigned long iobase;
	struct pcmcia_device *link = cur_dev;	/*  XXX hack */

	ret = alloc_private(dev, sizeof(struct das08_private_struct));
	if (ret < 0)
		return ret;

	dev_info(dev->hw_dev, "comedi%d: das08_cs:\n", dev->minor);
	/*  deal with a pci board */

	if (thisboard->bustype == pcmcia) {
		if (link == NULL) {
			dev_err(dev->hw_dev, "no pcmcia cards found\n");
			return -EIO;
		}
		iobase = link->resource[0]->start;
	} else {
		dev_err(dev->hw_dev, "bug! board does not have PCMCIA bustype\n");
		return -EINVAL;
	}

	return das08_common_attach(dev, iobase);
}

/*======================================================================

    The following pcmcia code for the pcm-das08 is adapted from the
    dummy_cs.c driver of the Linux PCMCIA Card Services package.

    The initial developer of the original code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

======================================================================*/

static void das08_pcmcia_config(struct pcmcia_device *link);
static void das08_pcmcia_release(struct pcmcia_device *link);
static int das08_pcmcia_suspend(struct pcmcia_device *p_dev);
static int das08_pcmcia_resume(struct pcmcia_device *p_dev);

static int das08_pcmcia_attach(struct pcmcia_device *);
static void das08_pcmcia_detach(struct pcmcia_device *);

struct local_info_t {
	struct pcmcia_device *link;
	int stop;
	struct bus_operations *bus;
};

static int das08_pcmcia_attach(struct pcmcia_device *link)
{
	struct local_info_t *local;

	dev_dbg(&link->dev, "das08_pcmcia_attach()\n");

	/* Allocate space for private device-specific data */
	local = kzalloc(sizeof(struct local_info_t), GFP_KERNEL);
	if (!local)
		return -ENOMEM;
	local->link = link;
	link->priv = local;

	cur_dev = link;

	das08_pcmcia_config(link);

	return 0;
}				/* das08_pcmcia_attach */

static void das08_pcmcia_detach(struct pcmcia_device *link)
{

	dev_dbg(&link->dev, "das08_pcmcia_detach\n");

	((struct local_info_t *)link->priv)->stop = 1;
	das08_pcmcia_release(link);

	/* This points to the parent struct local_info_t struct */
	kfree(link->priv);

}				/* das08_pcmcia_detach */


static int das08_pcmcia_config_loop(struct pcmcia_device *p_dev,
				void *priv_data)
{
	if (p_dev->config_index == 0)
		return -EINVAL;

	return pcmcia_request_io(p_dev);
}

static void das08_pcmcia_config(struct pcmcia_device *link)
{
	int ret;

	dev_dbg(&link->dev, "das08_pcmcia_config\n");

	link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;

	ret = pcmcia_loop_config(link, das08_pcmcia_config_loop, NULL);
	if (ret) {
		dev_warn(&link->dev, "no configuration found\n");
		goto failed;
	}

	if (!link->irq)
		goto failed;

	ret = pcmcia_enable_device(link);
	if (ret)
		goto failed;

	return;

failed:
	das08_pcmcia_release(link);

}				/* das08_pcmcia_config */

static void das08_pcmcia_release(struct pcmcia_device *link)
{
	dev_dbg(&link->dev, "das08_pcmcia_release\n");
	pcmcia_disable_device(link);
}				/* das08_pcmcia_release */

static int das08_pcmcia_suspend(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;
	/* Mark the device as stopped, to block IO until later */
	local->stop = 1;

	return 0;
}				/* das08_pcmcia_suspend */

static int das08_pcmcia_resume(struct pcmcia_device *link)
{
	struct local_info_t *local = link->priv;

	local->stop = 0;
	return 0;
}				/* das08_pcmcia_resume */

/*====================================================================*/

static const struct pcmcia_device_id das08_cs_id_table[] = {
	PCMCIA_DEVICE_MANF_CARD(0x01c5, 0x4001),
	PCMCIA_DEVICE_NULL
};

MODULE_DEVICE_TABLE(pcmcia, das08_cs_id_table);
MODULE_AUTHOR("David A. Schleef <ds@schleef.org>, "
	      "Frank Mori Hess <fmhess@users.sourceforge.net>");
MODULE_DESCRIPTION("Comedi driver for ComputerBoards DAS-08 PCMCIA boards");
MODULE_LICENSE("GPL");

struct pcmcia_driver das08_cs_driver = {
	.probe = das08_pcmcia_attach,
	.remove = das08_pcmcia_detach,
	.suspend = das08_pcmcia_suspend,
	.resume = das08_pcmcia_resume,
	.id_table = das08_cs_id_table,
	.owner = THIS_MODULE,
	.name = "pcm-das08",
};

static int __init init_das08_pcmcia_cs(void)
{
	pcmcia_register_driver(&das08_cs_driver);
	return 0;
}

static void __exit exit_das08_pcmcia_cs(void)
{
	pr_debug("das08_pcmcia_cs: unloading\n");
	pcmcia_unregister_driver(&das08_cs_driver);
}

static int __init das08_cs_init_module(void)
{
	int ret;

	ret = init_das08_pcmcia_cs();
	if (ret < 0)
		return ret;

	return comedi_driver_register(&driver_das08_cs);
}

static void __exit das08_cs_exit_module(void)
{
	exit_das08_pcmcia_cs();
	comedi_driver_unregister(&driver_das08_cs);
}

module_init(das08_cs_init_module);
module_exit(das08_cs_exit_module);
