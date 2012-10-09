/*
 *     comedi/drivers/ni_daq_700.c
 *     Driver for DAQCard-700 DIO only
 *     copied from 8255
 *
 *     COMEDI - Linux Control and Measurement Device Interface
 *     Copyright (C) 1998 David A. Schleef <ds@schleef.org>
 *
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

/*
Driver: ni_daq_700
Description: National Instruments PCMCIA DAQCard-700 DIO only
Author: Fred Brooks <nsaspook@nsaspook.com>,
  based on ni_daq_dio24 by Daniel Vecino Castel <dvecino@able.es>
Devices: [National Instruments] PCMCIA DAQ-Card-700 (ni_daq_700)
Status: works
Updated: Thu, 21 Feb 2008 12:07:20 +0000

The daqcard-700 appears in Comedi as a single digital I/O subdevice with
16 channels.  The channel 0 corresponds to the daqcard-700's output
port, bit 0; channel 8 corresponds to the input port, bit 0.

Direction configuration: channels 0-7 output, 8-15 input (8225 device
emu as port A output, port B input, port C N/A).
*/

#include <linux/interrupt.h>
#include <linux/slab.h>
#include "../comedidev.h"

#include <linux/ioport.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

static struct pcmcia_device *pcmcia_cur_dev;

struct dio700_board {
	const char *name;
};

#define DIO_W		0x04
#define DIO_R		0x05

static int subdev_700_insn(struct comedi_device *dev,
			   struct comedi_subdevice *s, struct comedi_insn *insn,
			   unsigned int *data)
{
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);

		if (data[0] & 0xff)
			outb(s->state & 0xff, dev->iobase + DIO_W);
	}

	data[1] = s->state & 0xff;
	data[1] |= inb(dev->iobase + DIO_R);

	return insn->n;
}

static int subdev_700_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn, unsigned int *data)
{
	unsigned int chan = 1 << CR_CHAN(insn->chanspec);

	switch (data[0]) {
	case INSN_CONFIG_DIO_INPUT:
		break;
	case INSN_CONFIG_DIO_OUTPUT:
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] = (s->io_bits & chan) ? COMEDI_OUTPUT : COMEDI_INPUT;
		break;
	default:
		return -EINVAL;
	}

	return insn->n;
}

static int dio700_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct dio700_board *thisboard = comedi_board(dev);
	struct comedi_subdevice *s;
	struct pcmcia_device *link;
	int ret;

	link = pcmcia_cur_dev;	/* XXX hack */
	if (!link)
		return -EIO;

	dev->iobase = link->resource[0]->start;
	if (!dev->iobase) {
		dev_err(dev->class_dev, "io base address is zero!\n");
		return -EINVAL;
	}

	dev->board_name = thisboard->name;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	/* DAQCard-700 dio */
	s = dev->subdevices + 0;
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 16;
	s->range_table	= &range_digital;
	s->maxdata	= 1;
	s->insn_bits	= subdev_700_insn;
	s->insn_config	= subdev_700_insn_config;

	s->state	= 0;
	s->io_bits	= 0x00ff;

	dev_info(dev->class_dev, "%s: %s, io 0x%lx\n",
		dev->driver->driver_name,
		dev->board_name,
		dev->iobase);

	return 0;
}

static void dio700_detach(struct comedi_device *dev)
{
	/* nothing to cleanup */
}

static const struct dio700_board dio700_boards[] = {
	{
		.name		= "daqcard-700",
	}, {
		.name		= "ni_daq_700",
	},
};

static struct comedi_driver driver_dio700 = {
	.driver_name	= "ni_daq_700",
	.module		= THIS_MODULE,
	.attach		= dio700_attach,
	.detach		= dio700_detach,
	.board_name	= &dio700_boards[0].name,
	.num_names	= ARRAY_SIZE(dio700_boards),
	.offset		= sizeof(struct dio700_board),
};

static int dio700_pcmcia_config_loop(struct pcmcia_device *p_dev,
				void *priv_data)
{
	if (p_dev->config_index == 0)
		return -EINVAL;

	return pcmcia_request_io(p_dev);
}

static int dio700_cs_attach(struct pcmcia_device *link)
{
	int ret;

	link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_AUDIO |
		CONF_AUTO_SET_IO;

	ret = pcmcia_loop_config(link, dio700_pcmcia_config_loop, NULL);
	if (ret)
		goto failed;

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

static void dio700_cs_detach(struct pcmcia_device *link)
{
	pcmcia_disable_device(link);
	pcmcia_cur_dev = NULL;
}

static const struct pcmcia_device_id dio700_cs_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x4743),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, dio700_cs_ids);

static struct pcmcia_driver dio700_cs_driver = {
	.name		= "ni_daq_700",
	.owner		= THIS_MODULE,
	.probe		= dio700_cs_attach,
	.remove		= dio700_cs_detach,
	.id_table	= dio700_cs_ids,
};

static int __init dio700_cs_init(void)
{
	int ret;

	ret = comedi_driver_register(&driver_dio700);
	if (ret < 0)
		return ret;

	ret = pcmcia_register_driver(&dio700_cs_driver);
	if (ret < 0) {
		comedi_driver_unregister(&driver_dio700);
		return ret;
	}

	return 0;
}
module_init(dio700_cs_init);

static void __exit dio700_cs_exit(void)
{
	pcmcia_unregister_driver(&dio700_cs_driver);
	comedi_driver_unregister(&driver_dio700);
}
module_exit(dio700_cs_exit);

MODULE_AUTHOR("Fred Brooks <nsaspook@nsaspook.com>");
MODULE_DESCRIPTION(
	"Comedi driver for National Instruments PCMCIA DAQCard-700 DIO");
MODULE_LICENSE("GPL");
