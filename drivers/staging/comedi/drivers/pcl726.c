/*
    comedi/drivers/pcl726.c

    hardware driver for Advantech cards:
     card:   PCL-726, PCL-727, PCL-728
     driver: pcl726,  pcl727,  pcl728
    and for ADLink cards:
     card:   ACL-6126, ACL-6128
     driver: acl6126,  acl6128

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998 David A. Schleef <ds@schleef.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/
/*
Driver: pcl726
Description: Advantech PCL-726 & compatibles
Author: ds
Status: untested
Devices: [Advantech] PCL-726 (pcl726), PCL-727 (pcl727), PCL-728 (pcl728),
  [ADLink] ACL-6126 (acl6126), ACL-6128 (acl6128)

Interrupts are not supported.

    Options for PCL-726:
     [0] - IO Base
     [2]...[7] - D/A output range for channel 1-6:
		0: 0-5V, 1: 0-10V, 2: +/-5V, 3: +/-10V,
		4: 4-20mA, 5: unknown (external reference)

    Options for PCL-727:
     [0] - IO Base
     [2]...[13] - D/A output range for channel 1-12:
		0: 0-5V, 1: 0-10V, 2: +/-5V,
		3: 4-20mA

    Options for PCL-728 and ACL-6128:
     [0] - IO Base
     [2], [3] - D/A output range for channel 1 and 2:
		0: 0-5V, 1: 0-10V, 2: +/-5V, 3: +/-10V,
		4: 4-20mA, 5: 0-20mA

    Options for ACL-6126:
     [0] - IO Base
     [1] - IRQ (0=disable, 3, 5, 6, 7, 9, 10, 11, 12, 15) (currently ignored)
     [2]...[7] - D/A output range for channel 1-6:
		0: 0-5V, 1: 0-10V, 2: +/-5V, 3: +/-10V,
		4: 4-20mA
*/

/*
    Thanks to Circuit Specialists for having programming info (!) on
    their web page.  (http://www.cir.com/)
*/

#include <linux/module.h>
#include <linux/interrupt.h>

#include "../comedidev.h"

#define PCL726_SIZE 16
#define PCL727_SIZE 32
#define PCL728_SIZE 8

#define PCL726_AO_MSB_REG(x)	(0x00 + ((x) * 2))
#define PCL726_AO_LSB_REG(x)	(0x01 + ((x) * 2))
#define PCL726_DO_MSB_REG	0x0c
#define PCL726_DO_LSB_REG	0x0d
#define PCL726_DI_MSB_REG	0x0e
#define PCL726_DI_LSB_REG	0x0f

#define PCL727_DI_MSB_REG	0x00
#define PCL727_DI_LSB_REG	0x01
#define PCL727_DO_MSB_REG	0x18
#define PCL727_DO_LSB_REG	0x19

static const struct comedi_lrange *const rangelist_726[] = {
	&range_unipolar5,
	&range_unipolar10,
	&range_bipolar5,
	&range_bipolar10,
	&range_4_20mA,
	&range_unknown
};

static const struct comedi_lrange *const rangelist_727[] = {
	&range_unipolar5,
	&range_unipolar10,
	&range_bipolar5,
	&range_4_20mA
};

static const struct comedi_lrange *const rangelist_728[] = {
	&range_unipolar5,
	&range_unipolar10,
	&range_bipolar5,
	&range_bipolar10,
	&range_4_20mA,
	&range_0_20mA
};

struct pcl726_board {
	const char *name;	/*  driver name */
	int n_aochan;		/*  num of D/A chans */
	unsigned int IRQbits;	/*  allowed interrupts */
	unsigned int io_range;	/*  len of IO space */
	unsigned int have_dio:1;
	unsigned int is_pcl727:1;
	const struct comedi_lrange *const *ao_ranges;
	int ao_num_ranges;
};

static const struct pcl726_board boardtypes[] = {
	{
		.name		= "pcl726",
		.n_aochan	= 6,
		.io_range	= PCL726_SIZE,
		.have_dio	= 1,
		.ao_ranges	= &rangelist_726[0],
		.ao_num_ranges	= ARRAY_SIZE(rangelist_726),
	}, {
		.name		= "pcl727",
		.n_aochan	= 12,
		.io_range	= PCL727_SIZE,
		.have_dio	= 1,
		.is_pcl727	= 1,
		.ao_ranges	= &rangelist_727[0],
		.ao_num_ranges	= ARRAY_SIZE(rangelist_727),
	}, {
		.name		= "pcl728",
		.n_aochan	= 2,
		.io_range	= PCL728_SIZE,
		.ao_num_ranges	= ARRAY_SIZE(rangelist_728),
		.ao_ranges	= &rangelist_728[0],
	}, {
		.name		= "acl6126",
		.n_aochan	= 6,
		.IRQbits	= 0x96e8,
		.io_range	= PCL726_SIZE,
		.have_dio	= 1,
		.ao_num_ranges	= ARRAY_SIZE(rangelist_726),
		.ao_ranges	= &rangelist_726[0],
	}, {
		.name		= "acl6128",
		.n_aochan	= 2,
		.io_range	= PCL728_SIZE,
		.ao_num_ranges	= ARRAY_SIZE(rangelist_728),
		.ao_ranges	= &rangelist_728[0],
	},
};

struct pcl726_private {
	const struct comedi_lrange *rangelist[12];
	unsigned int ao_readback[12];
};

static irqreturn_t pcl818_interrupt(int irq, void *d)
{
	return IRQ_HANDLED;
}

static int pcl726_ao_insn_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	struct pcl726_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int range = CR_RANGE(insn->chanspec);
	unsigned int val;
	int i;

	for (i = 0; i < insn->n; i++) {
		val = data[i];
		devpriv->ao_readback[chan] = val;

		/* bipolar data to the DAC is two's complement */
		if (comedi_chan_range_is_bipolar(s, chan, range))
			val = comedi_offset_munge(s, val);

		/* order is important, MSB then LSB */
		outb((val >> 8) & 0xff, dev->iobase + PCL726_AO_MSB_REG(chan));
		outb(val & 0xff, dev->iobase + PCL726_AO_LSB_REG(chan));
	}

	return insn->n;
}

static int pcl726_ao_insn_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	struct pcl726_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++)
		data[i] = devpriv->ao_readback[chan];

	return insn->n;
}

static int pcl726_di_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	const struct pcl726_board *board = comedi_board(dev);
	unsigned int val;

	if (board->is_pcl727) {
		val = inb(dev->iobase + PCL727_DI_LSB_REG);
		val |= (inb(dev->iobase + PCL727_DI_MSB_REG) << 8);
	} else {
		val = inb(dev->iobase + PCL726_DI_LSB_REG);
		val |= (inb(dev->iobase + PCL726_DI_MSB_REG) << 8);
	}

	data[1] = val;

	return insn->n;
}

static int pcl726_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	const struct pcl726_board *board = comedi_board(dev);
	unsigned long io = dev->iobase;
	unsigned int mask;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		if (board->is_pcl727) {
			if (mask & 0x00ff)
				outb(s->state & 0xff, io + PCL727_DO_LSB_REG);
			if (mask & 0xff00)
				outb((s->state >> 8), io + PCL727_DO_MSB_REG);
		} else {
			if (mask & 0x00ff)
				outb(s->state & 0xff, io + PCL726_DO_LSB_REG);
			if (mask & 0xff00)
				outb((s->state >> 8), io + PCL726_DO_MSB_REG);
		}
	}

	data[1] = s->state;

	return insn->n;
}

static int pcl726_attach(struct comedi_device *dev,
			 struct comedi_devconfig *it)
{
	const struct pcl726_board *board = comedi_board(dev);
	struct pcl726_private *devpriv;
	struct comedi_subdevice *s;
	int ret;
	int i;

	ret = comedi_request_region(dev, it->options[0], board->io_range);
	if (ret)
		return ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	/*
	 * Hook up the external trigger source interrupt only if the
	 * user config option is valid and the board supports interrupts.
	 */
	if (it->options[1] && (board->IRQbits & (1 << it->options[1]))) {
		ret = request_irq(it->options[1], pcl818_interrupt, 0,
				  dev->board_name, dev);
		if (ret == 0) {
			/* External trigger source is from Pin-17 of CN3 */
			dev->irq = it->options[1];
		}
	}

	/* setup the per-channel analog output range_table_list */
	for (i = 0; i < 12; i++) {
		unsigned int opt = it->options[2 + i];

		if (opt < board->ao_num_ranges && i < board->n_aochan)
			devpriv->rangelist[i] = board->ao_ranges[opt];
		else
			devpriv->rangelist[i] = &range_unknown;
	}

	ret = comedi_alloc_subdevices(dev, board->have_dio ? 3 : 1);
	if (ret)
		return ret;

	/* Analog Output subdevice */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_AO;
	s->subdev_flags	= SDF_WRITABLE | SDF_GROUND;
	s->n_chan	= board->n_aochan;
	s->maxdata	= 0x0fff;
	s->range_table_list = devpriv->rangelist;
	s->insn_write	= pcl726_ao_insn_write;
	s->insn_read	= pcl726_ao_insn_read;

	if (board->have_dio) {
		/* Digital Input subdevice */
		s = &dev->subdevices[1];
		s->type		= COMEDI_SUBD_DI;
		s->subdev_flags	= SDF_READABLE;
		s->n_chan	= 16;
		s->maxdata	= 1;
		s->insn_bits	= pcl726_di_insn_bits;
		s->range_table	= &range_digital;

		/* Digital Output subdevice */
		s = &dev->subdevices[2];
		s->type		= COMEDI_SUBD_DO;
		s->subdev_flags	= SDF_WRITABLE;
		s->n_chan	= 16;
		s->maxdata	= 1;
		s->insn_bits	= pcl726_do_insn_bits;
		s->range_table	= &range_digital;
	}

	return 0;
}

static struct comedi_driver pcl726_driver = {
	.driver_name	= "pcl726",
	.module		= THIS_MODULE,
	.attach		= pcl726_attach,
	.detach		= comedi_legacy_detach,
	.board_name	= &boardtypes[0].name,
	.num_names	= ARRAY_SIZE(boardtypes),
	.offset		= sizeof(struct pcl726_board),
};
module_comedi_driver(pcl726_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
