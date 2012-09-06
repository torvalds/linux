/*
    comedi/drivers/das16cs.c
    Driver for Computer Boards PC-CARD DAS16/16.

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000, 2001, 2002 David A. Schleef <ds@schleef.org>

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

*/
/*
Driver: cb_das16_cs
Description: Computer Boards PC-CARD DAS16/16
Devices: [ComputerBoards] PC-CARD DAS16/16 (cb_das16_cs), PC-CARD DAS16/16-AO
Author: ds
Updated: Mon, 04 Nov 2002 20:04:21 -0800
Status: experimental


*/

#include <linux/interrupt.h>
#include <linux/slab.h>
#include "../comedidev.h"
#include <linux/delay.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#include "8253.h"

#define DAS16CS_SIZE			18

#define DAS16CS_ADC_DATA		0
#define DAS16CS_DIO_MUX			2
#define DAS16CS_MISC1			4
#define DAS16CS_MISC2			6
#define DAS16CS_CTR0			8
#define DAS16CS_CTR1			10
#define DAS16CS_CTR2			12
#define DAS16CS_CTR_CONTROL		14
#define DAS16CS_DIO			16

struct das16cs_board {
	const char *name;
	int device_id;
	int n_ao_chans;
};

static const struct das16cs_board das16cs_boards[] = {
	{
		.name		= "PC-CARD DAS16/16-AO",
		.device_id	= 0x0039,
		.n_ao_chans	= 2,
	}, {
		.name		= "PCM-DAS16s/16",
		.device_id	= 0x4009,
		.n_ao_chans	= 0,
	}, {
		.name		= "PC-CARD DAS16/16",
		.device_id	= 0x0000,	/* unknown */
		.n_ao_chans	= 0,
	},
};

struct das16cs_private {
	unsigned int ao_readback[2];
	unsigned short status1;
	unsigned short status2;
};

static struct pcmcia_device *cur_dev;

static const struct comedi_lrange das16cs_ai_range = {
	4, {
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5),
		BIP_RANGE(1.25),
	}
};

static irqreturn_t das16cs_interrupt(int irq, void *d)
{
	/* struct comedi_device *dev = d; */
	return IRQ_HANDLED;
}

static int das16cs_ai_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct das16cs_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);
	int range = CR_RANGE(insn->chanspec);
	int aref = CR_AREF(insn->chanspec);
	int i;
	int to;

	outw(chan, dev->iobase + DAS16CS_DIO_MUX);

	devpriv->status1 &= ~0xf320;
	devpriv->status1 |= (aref == AREF_DIFF) ? 0 : 0x0020;
	outw(devpriv->status1, dev->iobase + DAS16CS_MISC1);

	devpriv->status2 &= ~0xff00;
	switch (range) {
	case 0:
		devpriv->status2 |= 0x800;
		break;
	case 1:
		devpriv->status2 |= 0x000;
		break;
	case 2:
		devpriv->status2 |= 0x100;
		break;
	case 3:
		devpriv->status2 |= 0x200;
		break;
	}
	outw(devpriv->status2, dev->iobase + DAS16CS_MISC2);

	for (i = 0; i < insn->n; i++) {
		outw(0, dev->iobase + DAS16CS_ADC_DATA);

#define TIMEOUT 1000
		for (to = 0; to < TIMEOUT; to++) {
			if (inw(dev->iobase + DAS16CS_MISC1) & 0x0080)
				break;
		}
		if (to == TIMEOUT) {
			dev_dbg(dev->class_dev, "cb_das16_cs: ai timeout\n");
			return -ETIME;
		}
		data[i] = inw(dev->iobase + DAS16CS_ADC_DATA);
	}

	return i;
}

static int das16cs_ai_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	return -EINVAL;
}

static int das16cs_ai_cmdtest(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;

	/* step 1: make sure trigger sources are trivially valid */

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_TIMER | TRIG_EXT;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_TIMER | TRIG_EXT;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_COUNT | TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/* step 2: make sure trigger sources are unique and
	 * mutually compatible */

	/* note that mutual compatibility is not an issue here */
	if (cmd->scan_begin_src != TRIG_TIMER &&
	    cmd->scan_begin_src != TRIG_EXT)
		err++;
	if (cmd->convert_src != TRIG_TIMER && cmd->convert_src != TRIG_EXT)
		err++;
	if (cmd->stop_src != TRIG_COUNT && cmd->stop_src != TRIG_NONE)
		err++;

	if (err)
		return 2;

	/* step 3: make sure arguments are trivially compatible */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}
#define MAX_SPEED	10000	/* in nanoseconds */
#define MIN_SPEED	1000000000	/* in nanoseconds */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		if (cmd->scan_begin_arg < MAX_SPEED) {
			cmd->scan_begin_arg = MAX_SPEED;
			err++;
		}
		if (cmd->scan_begin_arg > MIN_SPEED) {
			cmd->scan_begin_arg = MIN_SPEED;
			err++;
		}
	} else {
		/* external trigger */
		/* should be level/edge, hi/lo specification here */
		/* should specify multiple external triggers */
		if (cmd->scan_begin_arg > 9) {
			cmd->scan_begin_arg = 9;
			err++;
		}
	}
	if (cmd->convert_src == TRIG_TIMER) {
		if (cmd->convert_arg < MAX_SPEED) {
			cmd->convert_arg = MAX_SPEED;
			err++;
		}
		if (cmd->convert_arg > MIN_SPEED) {
			cmd->convert_arg = MIN_SPEED;
			err++;
		}
	} else {
		/* external trigger */
		/* see above */
		if (cmd->convert_arg > 9) {
			cmd->convert_arg = 9;
			err++;
		}
	}

	if (cmd->scan_end_arg != cmd->chanlist_len) {
		cmd->scan_end_arg = cmd->chanlist_len;
		err++;
	}
	if (cmd->stop_src == TRIG_COUNT) {
		if (cmd->stop_arg > 0x00ffffff) {
			cmd->stop_arg = 0x00ffffff;
			err++;
		}
	} else {
		/* TRIG_NONE */
		if (cmd->stop_arg != 0) {
			cmd->stop_arg = 0;
			err++;
		}
	}

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		unsigned int div1 = 0, div2 = 0;

		tmp = cmd->scan_begin_arg;
		i8253_cascade_ns_to_timer(100, &div1, &div2,
					  &cmd->scan_begin_arg,
					  cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->scan_begin_arg)
			err++;
	}
	if (cmd->convert_src == TRIG_TIMER) {
		unsigned int div1 = 0, div2 = 0;

		tmp = cmd->convert_arg;
		i8253_cascade_ns_to_timer(100, &div1, &div2,
					  &cmd->scan_begin_arg,
					  cmd->flags & TRIG_ROUND_MASK);
		if (tmp != cmd->convert_arg)
			err++;
		if (cmd->scan_begin_src == TRIG_TIMER &&
		    cmd->scan_begin_arg <
		    cmd->convert_arg * cmd->scan_end_arg) {
			cmd->scan_begin_arg =
			    cmd->convert_arg * cmd->scan_end_arg;
			err++;
		}
	}

	if (err)
		return 4;

	return 0;
}

static int das16cs_ao_winsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct das16cs_private *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);
	unsigned short status1;
	unsigned short d;
	int bit;

	for (i = 0; i < insn->n; i++) {
		devpriv->ao_readback[chan] = data[i];
		d = data[i];

		outw(devpriv->status1, dev->iobase + DAS16CS_MISC1);
		udelay(1);

		status1 = devpriv->status1 & ~0xf;
		if (chan)
			status1 |= 0x0001;
		else
			status1 |= 0x0008;

		outw(status1, dev->iobase + DAS16CS_MISC1);
		udelay(1);

		for (bit = 15; bit >= 0; bit--) {
			int b = (d >> bit) & 0x1;
			b <<= 1;
			outw(status1 | b | 0x0000, dev->iobase + DAS16CS_MISC1);
			udelay(1);
			outw(status1 | b | 0x0004, dev->iobase + DAS16CS_MISC1);
			udelay(1);
		}
		/*
		 * Make both DAC0CS and DAC1CS high to load
		 * the new data and update analog the output
		 */
		outw(status1 | 0x9, dev->iobase + DAS16CS_MISC1);
	}

	return i;
}

static int das16cs_ao_rinsn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct das16cs_private *devpriv = dev->private;
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return i;
}

static int das16cs_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];

		outw(s->state, dev->iobase + DAS16CS_DIO);
	}

	data[1] = inw(dev->iobase + DAS16CS_DIO);

	return insn->n;
}

static int das16cs_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	struct das16cs_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec);
	int bits;

	if (chan < 4)
		bits = 0x0f;
	else
		bits = 0xf0;

	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= bits;
		break;
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= bits;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (s->io_bits & (1 << chan)) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
		break;
	default:
		return -EINVAL;
		break;
	}

	devpriv->status2 &= ~0x00c0;
	devpriv->status2 |= (s->io_bits & 0xf0) ? 0x0080 : 0;
	devpriv->status2 |= (s->io_bits & 0x0f) ? 0x0040 : 0;

	outw(devpriv->status2, dev->iobase + DAS16CS_MISC2);

	return insn->n;
}

static const struct das16cs_board *das16cs_probe(struct comedi_device *dev,
						 struct pcmcia_device *link)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(das16cs_boards); i++) {
		if (das16cs_boards[i].device_id == link->card_id)
			return das16cs_boards + i;
	}

	dev_dbg(dev->class_dev, "unknown board!\n");

	return NULL;
}

static int das16cs_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	const struct das16cs_board *thisboard;
	struct pcmcia_device *link;
	struct comedi_subdevice *s;
	int ret;

	link = cur_dev;		/* XXX hack */
	if (!link)
		return -EIO;

	dev->board_ptr = das16cs_probe(dev, link);
	if (!dev->board_ptr)
		return -EIO;
	thisboard = comedi_board(dev);

	dev->board_name = thisboard->name;

	dev->iobase = link->resource[0]->start;

	ret = request_irq(link->irq, das16cs_interrupt,
			  IRQF_SHARED, "cb_das16_cs", dev);
	if (ret < 0)
		return ret;
	dev->irq = link->irq;

	if (alloc_private(dev, sizeof(struct das16cs_private)) < 0)
		return -ENOMEM;

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	dev->read_subdev = s;
	/* analog input subdevice */
	s->type		= COMEDI_SUBD_AI;
	s->subdev_flags	= SDF_READABLE | SDF_GROUND | SDF_DIFF | SDF_CMD_READ;
	s->n_chan	= 16;
	s->maxdata	= 0xffff;
	s->range_table	= &das16cs_ai_range;
	s->len_chanlist	= 16;
	s->insn_read	= das16cs_ai_rinsn;
	s->do_cmd	= das16cs_ai_cmd;
	s->do_cmdtest	= das16cs_ai_cmdtest;

	s = &dev->subdevices[1];
	/* analog output subdevice */
	if (thisboard->n_ao_chans) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= thisboard->n_ao_chans;
		s->maxdata	= 0xffff;
		s->range_table	= &range_bipolar10;
		s->insn_write	= &das16cs_ao_winsn;
		s->insn_read	= &das16cs_ao_rinsn;
	} else {
		s->type		= COMEDI_SUBD_UNUSED;
	}

	s = &dev->subdevices[2];
	/* digital i/o subdevice */
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 8;
	s->maxdata	= 1;
	s->range_table	= &range_digital;
	s->insn_bits	= das16cs_dio_insn_bits;
	s->insn_config	= das16cs_dio_insn_config;

	dev_info(dev->class_dev, "%s: %s, I/O base=0x%04lx, irq=%u\n",
		dev->driver->driver_name, dev->board_name,
		dev->iobase, dev->irq);

	return 0;
}

static void das16cs_detach(struct comedi_device *dev)
{
	if (dev->irq)
		free_irq(dev->irq, dev);
}

static struct comedi_driver driver_das16cs = {
	.driver_name	= "cb_das16_cs",
	.module		= THIS_MODULE,
	.attach		= das16cs_attach,
	.detach		= das16cs_detach,
};

static int das16cs_pcmcia_config_loop(struct pcmcia_device *p_dev,
				void *priv_data)
{
	if (p_dev->config_index == 0)
		return -EINVAL;

	return pcmcia_request_io(p_dev);
}

static int das16cs_pcmcia_attach(struct pcmcia_device *link)
{
	int ret;

	/* Do we need to allocate an interrupt? */
	link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_SET_IO;

	ret = pcmcia_loop_config(link, das16cs_pcmcia_config_loop, NULL);
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

static void das16cs_pcmcia_detach(struct pcmcia_device *link)
{
	pcmcia_disable_device(link);
	cur_dev = NULL;
}

static const struct pcmcia_device_id das16cs_id_table[] = {
	PCMCIA_DEVICE_MANF_CARD(0x01c5, 0x0039),
	PCMCIA_DEVICE_MANF_CARD(0x01c5, 0x4009),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, das16cs_id_table);

static struct pcmcia_driver das16cs_driver = {
	.name		= "cb_das16_cs",
	.owner		= THIS_MODULE,
	.probe		= das16cs_pcmcia_attach,
	.remove		= das16cs_pcmcia_detach,
	.id_table	= das16cs_id_table,
};

static int __init das16cs_init(void)
{
	int ret;

	ret = comedi_driver_register(&driver_das16cs);
	if (ret < 0)
		return ret;

	ret = pcmcia_register_driver(&das16cs_driver);
	if (ret < 0) {
		comedi_driver_unregister(&driver_das16cs);
		return ret;
	}

	return 0;
}
module_init(das16cs_init);

static void __exit das16cs_exit(void)
{
	pcmcia_unregister_driver(&das16cs_driver);
	comedi_driver_unregister(&driver_das16cs);
}
module_exit(das16cs_exit);

MODULE_AUTHOR("David A. Schleef <ds@schleef.org>");
MODULE_DESCRIPTION("Comedi driver for Computer Boards PC-CARD DAS16/16");
MODULE_LICENSE("GPL");
