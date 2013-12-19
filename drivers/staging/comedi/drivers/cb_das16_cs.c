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

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/delay.h>

#include "../comedidev.h"

#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#include "comedi_fc.h"
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

	/* Step 1 : check if triggers are trivially valid */

	err |= cfc_check_trigger_src(&cmd->start_src, TRIG_NOW);
	err |= cfc_check_trigger_src(&cmd->scan_begin_src,
					TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->convert_src,
					TRIG_TIMER | TRIG_EXT);
	err |= cfc_check_trigger_src(&cmd->scan_end_src, TRIG_COUNT);
	err |= cfc_check_trigger_src(&cmd->stop_src, TRIG_COUNT | TRIG_NONE);

	if (err)
		return 1;

	/* Step 2a : make sure trigger sources are unique */

	err |= cfc_check_trigger_is_unique(cmd->scan_begin_src);
	err |= cfc_check_trigger_is_unique(cmd->convert_src);
	err |= cfc_check_trigger_is_unique(cmd->stop_src);

	/* Step 2b : and mutually compatible */

	if (err)
		return 2;

	/* Step 3: check if arguments are trivially valid */

	err |= cfc_check_trigger_arg_is(&cmd->start_arg, 0);

#define MAX_SPEED	10000	/* in nanoseconds */
#define MIN_SPEED	1000000000	/* in nanoseconds */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		err |= cfc_check_trigger_arg_min(&cmd->scan_begin_arg,
						 MAX_SPEED);
		err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg,
						 MIN_SPEED);
	} else {
		/* external trigger */
		/* should be level/edge, hi/lo specification here */
		/* should specify multiple external triggers */
		err |= cfc_check_trigger_arg_max(&cmd->scan_begin_arg, 9);
	}
	if (cmd->convert_src == TRIG_TIMER) {
		err |= cfc_check_trigger_arg_min(&cmd->convert_arg,
						 MAX_SPEED);
		err |= cfc_check_trigger_arg_max(&cmd->convert_arg,
						 MIN_SPEED);
	} else {
		/* external trigger */
		/* see above */
		err |= cfc_check_trigger_arg_max(&cmd->convert_arg, 9);
	}

	err |= cfc_check_trigger_arg_is(&cmd->scan_end_arg, cmd->chanlist_len);

	if (cmd->stop_src == TRIG_COUNT)
		err |= cfc_check_trigger_arg_max(&cmd->stop_arg, 0x00ffffff);
	else	/* TRIG_NONE */
		err |= cfc_check_trigger_arg_is(&cmd->stop_arg, 0);

	if (err)
		return 3;

	/* step 4: fix up any arguments */

	if (cmd->scan_begin_src == TRIG_TIMER) {
		unsigned int div1 = 0, div2 = 0;

		tmp = cmd->scan_begin_arg;
		i8253_cascade_ns_to_timer(I8254_OSC_BASE_10MHZ,
					  &div1, &div2,
					  &cmd->scan_begin_arg, cmd->flags);
		if (tmp != cmd->scan_begin_arg)
			err++;
	}
	if (cmd->convert_src == TRIG_TIMER) {
		unsigned int div1 = 0, div2 = 0;

		tmp = cmd->convert_arg;
		i8253_cascade_ns_to_timer(I8254_OSC_BASE_10MHZ,
					  &div1, &div2,
					  &cmd->scan_begin_arg, cmd->flags);
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
				 struct comedi_insn *insn,
				 unsigned int *data)
{
	if (comedi_dio_update_state(s, data))
		outw(s->state, dev->iobase + DAS16CS_DIO);

	data[1] = inw(dev->iobase + DAS16CS_DIO);

	return insn->n;
}

static int das16cs_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	struct das16cs_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask;
	int ret;

	if (chan < 4)
		mask = 0x0f;
	else
		mask = 0xf0;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	devpriv->status2 &= ~0x00c0;
	devpriv->status2 |= (s->io_bits & 0xf0) ? 0x0080 : 0;
	devpriv->status2 |= (s->io_bits & 0x0f) ? 0x0040 : 0;

	outw(devpriv->status2, dev->iobase + DAS16CS_MISC2);

	return insn->n;
}

static const void *das16cs_find_boardinfo(struct comedi_device *dev,
					  struct pcmcia_device *link)
{
	const struct das16cs_board *board;
	int i;

	for (i = 0; i < ARRAY_SIZE(das16cs_boards); i++) {
		board = &das16cs_boards[i];
		if (board->device_id == link->card_id)
			return board;
	}

	return NULL;
}

static int das16cs_auto_attach(struct comedi_device *dev,
			       unsigned long context)
{
	struct pcmcia_device *link = comedi_to_pcmcia_dev(dev);
	const struct das16cs_board *board;
	struct das16cs_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	board = das16cs_find_boardinfo(dev, link);
	if (!board)
		return -ENODEV;
	dev->board_ptr = board;
	dev->board_name = board->name;

	link->config_flags |= CONF_AUTO_SET_IO | CONF_ENABLE_IRQ;
	ret = comedi_pcmcia_enable(dev, NULL);
	if (ret)
		return ret;
	dev->iobase = link->resource[0]->start;

	link->priv = dev;
	ret = pcmcia_request_irq(link, das16cs_interrupt);
	if (ret)
		return ret;
	dev->irq = link->irq;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
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
	if (board->n_ao_chans) {
		s->type		= COMEDI_SUBD_AO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= board->n_ao_chans;
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

static struct comedi_driver driver_das16cs = {
	.driver_name	= "cb_das16_cs",
	.module		= THIS_MODULE,
	.auto_attach	= das16cs_auto_attach,
	.detach		= comedi_pcmcia_disable,
};

static int das16cs_pcmcia_attach(struct pcmcia_device *link)
{
	return comedi_pcmcia_auto_config(link, &driver_das16cs);
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
	.id_table	= das16cs_id_table,
	.probe		= das16cs_pcmcia_attach,
	.remove		= comedi_pcmcia_auto_unconfig,
};
module_comedi_pcmcia_driver(driver_das16cs, das16cs_driver);

MODULE_AUTHOR("David A. Schleef <ds@schleef.org>");
MODULE_DESCRIPTION("Comedi driver for Computer Boards PC-CARD DAS16/16");
MODULE_LICENSE("GPL");
