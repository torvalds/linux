// SPDX-License-Identifier: GPL-2.0+
/*
 * Comedi driver for DAS008 PCMCIA boards
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2001,2002,2003 Frank Mori Hess <fmhess@users.sourceforge.net>
 *
 * PCMCIA support code for this driver is adapted from the dummy_cs.c
 * driver of the Linux PCMCIA Card Services package.
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 */

/*
 * Driver: das08_cs
 * Description: DAS-08 PCMCIA boards
 * Author: Warren Jasper, ds, Frank Hess
 * Devices: [ComputerBoards] PCM-DAS08 (pcm-das08)
 * Status: works
 *
 * This is the PCMCIA-specific support split off from the
 * das08 driver.
 *
 * Configuration Options: none, uses PCMCIA auto config
 *
 * Command support does not exist, but could be added for this board.
 */

#include <linux/module.h>
#include <linux/comedi/comedi_pcmcia.h>

#include "das08.h"

static const struct das08_board_struct das08_cs_boards[] = {
	{
		.name		= "pcm-das08",
		.ai_nbits	= 12,
		.ai_pg		= das08_bipolar5,
		.ai_encoding	= das08_pcm_encode12,
		.di_nchan	= 3,
		.do_nchan	= 3,
		.iosize		= 16,
	},
};

static int das08_cs_auto_attach(struct comedi_device *dev,
				unsigned long context)
{
	struct pcmcia_device *link = comedi_to_pcmcia_dev(dev);
	struct das08_private_struct *devpriv;
	unsigned long iobase;
	int ret;

	/* The das08 driver needs the board_ptr */
	dev->board_ptr = &das08_cs_boards[0];

	link->config_flags |= CONF_AUTO_SET_IO;
	ret = comedi_pcmcia_enable(dev, NULL);
	if (ret)
		return ret;
	iobase = link->resource[0]->start;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	return das08_common_attach(dev, iobase);
}

static struct comedi_driver driver_das08_cs = {
	.driver_name	= "das08_cs",
	.module		= THIS_MODULE,
	.auto_attach	= das08_cs_auto_attach,
	.detach		= comedi_pcmcia_disable,
};

static int das08_pcmcia_attach(struct pcmcia_device *link)
{
	return comedi_pcmcia_auto_config(link, &driver_das08_cs);
}

static const struct pcmcia_device_id das08_cs_id_table[] = {
	PCMCIA_DEVICE_MANF_CARD(0x01c5, 0x4001),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, das08_cs_id_table);

static struct pcmcia_driver das08_cs_driver = {
	.name		= "pcm-das08",
	.owner		= THIS_MODULE,
	.id_table	= das08_cs_id_table,
	.probe		= das08_pcmcia_attach,
	.remove		= comedi_pcmcia_auto_unconfig,
};
module_comedi_pcmcia_driver(driver_das08_cs, das08_cs_driver);

MODULE_AUTHOR("David A. Schleef <ds@schleef.org>");
MODULE_AUTHOR("Frank Mori Hess <fmhess@users.sourceforge.net>");
MODULE_DESCRIPTION("Comedi driver for ComputerBoards DAS-08 PCMCIA boards");
MODULE_LICENSE("GPL");
