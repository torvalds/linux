/*
    comedi/drivers/ni_daq_dio24.c
    Driver for National Instruments PCMCIA DAQ-Card DIO-24
    Copyright (C) 2002 Daniel Vecino Castel <dvecino@able.es>

    PCMCIA crap at end of file is adapted from dummy_cs.c 1.31 2001/08/24 12:13:13
    from the pcmcia package.
    The initial developer of the pcmcia dummy_cs.c code is David A. Hinds
    <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

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

************************************************************************
*/
/*
Driver: ni_daq_dio24
Description: National Instruments PCMCIA DAQ-Card DIO-24
Author: Daniel Vecino Castel <dvecino@able.es>
Devices: [National Instruments] PCMCIA DAQ-Card DIO-24 (ni_daq_dio24)
Status: ?
Updated: Thu, 07 Nov 2002 21:53:06 -0800

This is just a wrapper around the 8255.o driver to properly handle
the PCMCIA interface.
*/

#include <linux/interrupt.h>
#include <linux/slab.h>
#include "../comedidev.h"

#include <linux/ioport.h>

#include "8255.h"

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

static struct pcmcia_device *pcmcia_cur_dev;

#define DIO24_SIZE 4		/*  size of io region used by board */

enum dio24_bustype { pcmcia_bustype };

struct dio24_board_struct {
	const char *name;
	int device_id;		/*  device id for pcmcia board */
	enum dio24_bustype bustype;	/*  PCMCIA */
	int have_dio;		/*  have 8255 chip */
	/*  function pointers so we can use inb/outb or readb/writeb as appropriate */
	unsigned int (*read_byte) (unsigned int address);
	void (*write_byte) (unsigned int byte, unsigned int address);
};

static const struct dio24_board_struct dio24_boards[] = {
	{
	 .name = "daqcard-dio24",
	 .device_id = 0x475c,	/*  0x10b is manufacturer id, 0x475c is device id */
	 .bustype = pcmcia_bustype,
	 .have_dio = 1,
	 },
	{
	 .name = "ni_daq_dio24",
	 .device_id = 0x475c,	/*  0x10b is manufacturer id, 0x475c is device id */
	 .bustype = pcmcia_bustype,
	 .have_dio = 1,
	 },
};

static int dio24_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct dio24_board_struct *thisboard = comedi_board(dev);
	struct comedi_subdevice *s;
	unsigned long iobase = 0;
	struct pcmcia_device *link;
	int ret;

	/*  get base address, irq etc. based on bustype */
	switch (thisboard->bustype) {
	case pcmcia_bustype:
		link = pcmcia_cur_dev;	/* XXX hack */
		if (!link)
			return -EIO;
		iobase = link->resource[0]->start;
		break;
	default:
		pr_err("bug! couldn't determine board type\n");
		return -EINVAL;
		break;
	}
	pr_debug("comedi%d: ni_daq_dio24: %s, io 0x%lx", dev->minor,
		 thisboard->name, iobase);

	if (iobase == 0) {
		pr_err("io base address is zero!\n");
		return -EINVAL;
	}

	dev->iobase = iobase;

	dev->board_name = thisboard->name;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	/* 8255 dio */
	s = &dev->subdevices[0];
	subdev_8255_init(dev, s, NULL, dev->iobase);

	return 0;
};

static void dio24_detach(struct comedi_device *dev)
{
	if (dev->subdevices)
		subdev_8255_cleanup(dev, &dev->subdevices[0]);
}

static struct comedi_driver driver_dio24 = {
	.driver_name	= "ni_daq_dio24",
	.module		= THIS_MODULE,
	.attach		= dio24_attach,
	.detach		= dio24_detach,
	.num_names	= ARRAY_SIZE(dio24_boards),
	.board_name	= &dio24_boards[0].name,
	.offset		= sizeof(struct dio24_board_struct),
};

static int dio24_pcmcia_config_loop(struct pcmcia_device *p_dev,
				    void *priv_data)
{
	if (p_dev->config_index == 0)
		return -EINVAL;

	return pcmcia_request_io(p_dev);
}

static int dio24_cs_attach(struct pcmcia_device *link)
{
	int ret;

	link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_AUDIO |
		CONF_AUTO_SET_IO;

	ret = pcmcia_loop_config(link, dio24_pcmcia_config_loop, NULL);
	if (ret) {
		dev_warn(&link->dev, "no configuration found\n");
		goto failed;
	}

	if (!link->irq)
		goto failed;

	ret = pcmcia_enable_device(link);
	if (ret)
		goto failed;

	pcmcia_cur_dev = link;

	return 0;

failed:
	pcmcia_disable_device(link);
	return ret;
}

static void dio24_cs_detach(struct pcmcia_device *link)
{
	pcmcia_disable_device(link);
}

static const struct pcmcia_device_id dio24_cs_ids[] = {
	/* N.B. These IDs should match those in dio24_boards */
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x475c),	/* daqcard-dio24 */
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, dio24_cs_ids);

static struct pcmcia_driver dio24_cs_driver = {
	.name		= "ni_daq_dio24",
	.owner		= THIS_MODULE,
	.id_table	= dio24_cs_ids,
	.probe		= dio24_cs_attach,
	.remove		= dio24_cs_detach,
};
module_comedi_pcmcia_driver(driver_dio24, dio24_cs_driver);

MODULE_AUTHOR("Daniel Vecino Castel <dvecino@able.es>");
MODULE_DESCRIPTION(
	"Comedi driver for National Instruments PCMCIA DAQ-Card DIO-24");
MODULE_LICENSE("GPL");
