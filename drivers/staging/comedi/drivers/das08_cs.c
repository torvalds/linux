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

    PCMCIA support code for this driver is adapted from the dummy_cs.c
    driver of the Linux PCMCIA Card Services package.

    The initial developer of the original code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

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

static const struct das08_board_struct das08_cs_boards[] = {
	{
		.name = "pcm-das08",
		.id = 0x0,		/*  XXX */
		.bustype = pcmcia,
		.ai_nbits = 12,
		.ai_pg = das08_bipolar5,
		.ai_encoding = das08_pcm_encode12,
		.di_nchan = 3,
		.do_nchan = 3,
		.iosize = 16,
	},
	/*  duplicate so driver name can be used also */
	{
		.name = "das08_cs",
		.id = 0x0,		/*  XXX */
		.bustype = pcmcia,
		.ai_nbits = 12,
		.ai_pg = das08_bipolar5,
		.ai_encoding = das08_pcm_encode12,
		.di_nchan = 3,
		.do_nchan = 3,
		.iosize = 16,
	},
};

static struct pcmcia_device *cur_dev;

static int das08_cs_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it)
{
	const struct das08_board_struct *thisboard = comedi_board(dev);
	struct das08_private_struct *devpriv;
	unsigned long iobase;
	struct pcmcia_device *link = cur_dev;	/*  XXX hack */

	devpriv = kzalloc(sizeof(*devpriv), GFP_KERNEL);
	if (!devpriv)
		return -ENOMEM;
	dev->private = devpriv;

	dev_info(dev->class_dev, "das08_cs: attach\n");
	/*  deal with a pci board */

	if (thisboard->bustype == pcmcia) {
		if (link == NULL) {
			dev_err(dev->class_dev, "no pcmcia cards found\n");
			return -EIO;
		}
		iobase = link->resource[0]->start;
	} else {
		dev_err(dev->class_dev,
			"bug! board does not have PCMCIA bustype\n");
		return -EINVAL;
	}

	return das08_common_attach(dev, iobase);
}

static struct comedi_driver driver_das08_cs = {
	.driver_name	= "das08_cs",
	.module		= THIS_MODULE,
	.attach		= das08_cs_attach,
	.detach		= das08_common_detach,
	.board_name	= &das08_cs_boards[0].name,
	.num_names	= ARRAY_SIZE(das08_cs_boards),
	.offset		= sizeof(struct das08_board_struct),
};

static int das08_pcmcia_config_loop(struct pcmcia_device *p_dev,
				void *priv_data)
{
	if (p_dev->config_index == 0)
		return -EINVAL;

	return pcmcia_request_io(p_dev);
}

static int das08_pcmcia_attach(struct pcmcia_device *link)
{
	int ret;

	link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;

	ret = pcmcia_loop_config(link, das08_pcmcia_config_loop, NULL);
	if (ret)
		goto failed;

	if (!link->irq)
		goto failed;

	ret = pcmcia_enable_device(link);
	if (ret)
		goto failed;

	cur_dev = link;
	return 0;

failed:
	pcmcia_disable_device(link);
	return ret;
}

static void das08_pcmcia_detach(struct pcmcia_device *link)
{
	pcmcia_disable_device(link);
	cur_dev = NULL;
}

static const struct pcmcia_device_id das08_cs_id_table[] = {
	PCMCIA_DEVICE_MANF_CARD(0x01c5, 0x4001),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, das08_cs_id_table);

static struct pcmcia_driver das08_cs_driver = {
	.name		= "pcm-das08",
	.owner		= THIS_MODULE,
	.probe		= das08_pcmcia_attach,
	.remove		= das08_pcmcia_detach,
	.id_table	= das08_cs_id_table,
};

static int __init das08_cs_init_module(void)
{
	int ret;

	ret = comedi_driver_register(&driver_das08_cs);
	if (ret < 0)
		return ret;

	ret = pcmcia_register_driver(&das08_cs_driver);
	if (ret < 0) {
		comedi_driver_unregister(&driver_das08_cs);
		return ret;
	}

	return 0;

}
module_init(das08_cs_init_module);

static void __exit das08_cs_exit_module(void)
{
	pcmcia_unregister_driver(&das08_cs_driver);
	comedi_driver_unregister(&driver_das08_cs);
}
module_exit(das08_cs_exit_module);

MODULE_AUTHOR("David A. Schleef <ds@schleef.org>, "
	      "Frank Mori Hess <fmhess@users.sourceforge.net>");
MODULE_DESCRIPTION("Comedi driver for ComputerBoards DAS-08 PCMCIA boards");
MODULE_LICENSE("GPL");
