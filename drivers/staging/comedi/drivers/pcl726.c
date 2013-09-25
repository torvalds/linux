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

#define PCL726_DO_HI 12
#define PCL726_DO_LO 13
#define PCL726_DI_HI 14
#define PCL726_DI_LO 15

#define PCL727_DO_HI 24
#define PCL727_DO_LO 25
#define PCL727_DI_HI  0
#define PCL727_DI_LO  1

static const struct comedi_lrange *const rangelist_726[] = {
	&range_unipolar5, &range_unipolar10,
	&range_bipolar5, &range_bipolar10,
	&range_4_20mA, &range_unknown
};

static const struct comedi_lrange *const rangelist_727[] = {
	&range_unipolar5, &range_unipolar10,
	&range_bipolar5,
	&range_4_20mA
};

static const struct comedi_lrange *const rangelist_728[] = {
	&range_unipolar5, &range_unipolar10,
	&range_bipolar5, &range_bipolar10,
	&range_4_20mA, &range_0_20mA
};

struct pcl726_board {

	const char *name;	/*  driver name */
	int n_aochan;		/*  num of D/A chans */
	int num_of_ranges;	/*  num of ranges */
	unsigned int IRQbits;	/*  allowed interrupts */
	unsigned int io_range;	/*  len of IO space */
	char have_dio;		/*  1=card have DI/DO ports */
	int di_hi;		/*  ports for DI/DO operations */
	int di_lo;
	int do_hi;
	int do_lo;
	const struct comedi_lrange *const *range_type_list;
	/*  list of supported ranges */
};

static const struct pcl726_board boardtypes[] = {
	{
		.name		= "pcl726",
		.n_aochan	= 6,
		.num_of_ranges	= 6,
		.io_range	= PCL726_SIZE,
		.have_dio	= 1,
		.di_hi		= PCL726_DI_HI,
		.di_lo		= PCL726_DI_LO,
		.do_hi		= PCL726_DO_HI,
		.do_lo		= PCL726_DO_LO,
		.range_type_list = &rangelist_726[0],
	}, {
		.name		= "pcl727",
		.n_aochan	= 12,
		.num_of_ranges	= 4,
		.io_range	= PCL727_SIZE,
		.have_dio	= 1,
		.di_hi		= PCL727_DI_HI,
		.di_lo		= PCL727_DI_LO,
		.do_hi		= PCL727_DO_HI,
		.do_lo		= PCL727_DO_LO,
		.range_type_list = &rangelist_727[0],
	}, {
		.name		= "pcl728",
		.n_aochan	= 2,
		.num_of_ranges	= 6,
		.io_range	= PCL728_SIZE,
		.range_type_list = &rangelist_728[0],
	}, {
		.name		= "acl6126",
		.n_aochan	= 6,
		.num_of_ranges	= 5,
		.IRQbits	= 0x96e8,
		.io_range	= PCL726_SIZE,
		.have_dio	= 1,
		.di_hi		= PCL726_DI_HI,
		.di_lo		= PCL726_DI_LO,
		.do_hi		= PCL726_DO_HI,
		.do_lo		= PCL726_DO_LO,
		.range_type_list = &rangelist_726[0],
	}, {
		.name		= "acl6128",
		.n_aochan	= 2,
		.num_of_ranges	= 6,
		.io_range	= PCL728_SIZE,
		.range_type_list = &rangelist_728[0],
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
			       struct comedi_insn *insn, unsigned int *data)
{
	const struct pcl726_board *board = comedi_board(dev);

	data[1] = inb(dev->iobase + board->di_lo) |
	    (inb(dev->iobase + board->di_hi) << 8);

	return insn->n;
}

static int pcl726_do_insn_bits(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn,
			       unsigned int *data)
{
	const struct pcl726_board *board = comedi_board(dev);
	unsigned int mask;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		if (mask & 0x00ff)
			outb(s->state & 0xff, dev->iobase + board->do_lo);
		if (mask & 0xff00)
			outb((s->state >> 8), dev->iobase + board->do_hi);
	}

	data[1] = s->state;

	return insn->n;
}

static int pcl726_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct pcl726_board *board = comedi_board(dev);
	struct pcl726_private *devpriv;
	struct comedi_subdevice *s;
	int ret, i;

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

		if (opt < board->num_of_ranges && i < board->n_aochan)
			devpriv->rangelist[i] = board->range_type_list[opt];
		else
			devpriv->rangelist[i] = &range_unknown;
	}

	ret = comedi_alloc_subdevices(dev, 3);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	/* ao */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND;
	s->n_chan = board->n_aochan;
	s->maxdata = 0xfff;
	s->len_chanlist = 1;
	s->insn_write = pcl726_ao_insn_write;
	s->insn_read = pcl726_ao_insn_read;
	s->range_table_list = devpriv->rangelist;

	s = &dev->subdevices[1];
	/* di */
	if (!board->have_dio) {
		s->type = COMEDI_SUBD_UNUSED;
	} else {
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE | SDF_GROUND;
		s->n_chan = 16;
		s->maxdata = 1;
		s->len_chanlist = 1;
		s->insn_bits = pcl726_di_insn_bits;
		s->range_table = &range_digital;
	}

	s = &dev->subdevices[2];
	/* do */
	if (!board->have_dio) {
		s->type = COMEDI_SUBD_UNUSED;
	} else {
		s->type = COMEDI_SUBD_DO;
		s->subdev_flags = SDF_WRITABLE | SDF_GROUND;
		s->n_chan = 16;
		s->maxdata = 1;
		s->len_chanlist = 1;
		s->insn_bits = pcl726_do_insn_bits;
		s->range_table = &range_digital;
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
