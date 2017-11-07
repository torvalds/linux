// SPDX-License-Identifier: GPL-2.0+
/*
 *     comedi/drivers/ni_daq_700.c
 *     Driver for DAQCard-700 DIO/AI
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
 */

/*
 * Driver: ni_daq_700
 * Description: National Instruments PCMCIA DAQCard-700
 * Author: Fred Brooks <nsaspook@nsaspook.com>,
 *   based on ni_daq_dio24 by Daniel Vecino Castel <dvecino@able.es>
 * Devices: [National Instruments] PCMCIA DAQ-Card-700 (ni_daq_700)
 * Status: works
 * Updated: Wed, 21 May 2014 12:07:20 +0000
 *
 * The daqcard-700 appears in Comedi as a  digital I/O subdevice (0) with
 * 16 channels and a analog input subdevice (1) with 16 single-ended channels
 * or 8 differential channels, and three input ranges.
 *
 * Digital:  The channel 0 corresponds to the daqcard-700's output
 * port, bit 0; channel 8 corresponds to the input port, bit 0.
 *
 * Digital direction configuration: channels 0-7 output, 8-15 input.
 *
 * Analog: The input  range is 0 to 4095 with a default of -10 to +10 volts.
 * Valid ranges:
 *       0 for -10 to 10V bipolar
 *       1 for -5 to 5V bipolar
 *       2 for -2.5 to 2.5V bipolar
 *
 * IRQ is assigned but not used.
 *
 * Manuals:	Register level:	http://www.ni.com/pdf/manuals/340698.pdf
 *		User Manual:	http://www.ni.com/pdf/manuals/320676d.pdf
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#include "../comedi_pcmcia.h"

/* daqcard700 registers */
#define DIO_W		0x04	/* WO 8bit */
#define DIO_R		0x05	/* RO 8bit */
#define CMD_R1		0x00	/* WO 8bit */
#define CMD_R2		0x07	/* RW 8bit */
#define CMD_R3		0x05	/* W0 8bit */
#define STA_R1		0x00	/* RO 8bit */
#define STA_R2		0x01	/* RO 8bit */
#define ADFIFO_R	0x02	/* RO 16bit */
#define ADCLEAR_R	0x01	/* WO 8bit */
#define CDA_R0		0x08	/* RW 8bit */
#define CDA_R1		0x09	/* RW 8bit */
#define CDA_R2		0x0A	/* RW 8bit */
#define CMO_R		0x0B	/* RO 8bit */
#define TIC_R		0x06	/* WO 8bit */
/* daqcard700 modes */
#define CMD_R3_DIFF     0x04    /* diff mode */

static const struct comedi_lrange range_daq700_ai = {
	3,
	{
		BIP_RANGE(10),
		BIP_RANGE(5),
		BIP_RANGE(2.5)
	}
};

static int daq700_dio_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn,
				unsigned int *data)
{
	unsigned int mask;
	unsigned int val;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		if (mask & 0xff)
			outb(s->state & 0xff, dev->iobase + DIO_W);
	}

	val = s->state & 0xff;
	val |= inb(dev->iobase + DIO_R) << 8;

	data[1] = val;

	return insn->n;
}

static int daq700_dio_insn_config(struct comedi_device *dev,
				  struct comedi_subdevice *s,
				  struct comedi_insn *insn,
				  unsigned int *data)
{
	int ret;

	ret = comedi_dio_insn_config(dev, s, insn, data, 0);
	if (ret)
		return ret;

	/* The DIO channels are not configurable, fix the io_bits */
	s->io_bits = 0x00ff;

	return insn->n;
}

static int daq700_ai_eoc(struct comedi_device *dev,
			 struct comedi_subdevice *s,
			 struct comedi_insn *insn,
			 unsigned long context)
{
	unsigned int status;

	status = inb(dev->iobase + STA_R2);
	if ((status & 0x03))
		return -EOVERFLOW;
	status = inb(dev->iobase + STA_R1);
	if ((status & 0x02))
		return -ENODATA;
	if ((status & 0x11) == 0x01)
		return 0;
	return -EBUSY;
}

static int daq700_ai_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn, unsigned int *data)
{
	int n;
	int d;
	int ret;
	unsigned int chan	= CR_CHAN(insn->chanspec);
	unsigned int aref	= CR_AREF(insn->chanspec);
	unsigned int range	= CR_RANGE(insn->chanspec);
	unsigned int r3_bits	= 0;

	/* set channel input modes */
	if (aref == AREF_DIFF)
		r3_bits |= CMD_R3_DIFF;
	/* write channel mode/range */
	if (range >= 1)
		range++;        /* convert range to hardware value */
	outb(r3_bits | (range & 0x03), dev->iobase + CMD_R3);

	/* write channel to multiplexer */
	/* set mask scan bit high to disable scanning */
	outb(chan | 0x80, dev->iobase + CMD_R1);
	/* mux needs 2us to really settle [Fred Brooks]. */
	udelay(2);

	/* convert n samples */
	for (n = 0; n < insn->n; n++) {
		/* trigger conversion with out0 L to H */
		outb(0x00, dev->iobase + CMD_R2); /* enable ADC conversions */
		outb(0x30, dev->iobase + CMO_R); /* mode 0 out0 L, from H */
		outb(0x00, dev->iobase + ADCLEAR_R);	/* clear the ADC FIFO */
		/* read 16bit junk from FIFO to clear */
		inw(dev->iobase + ADFIFO_R);
		/* mode 1 out0 H, L to H, start conversion */
		outb(0x32, dev->iobase + CMO_R);

		/* wait for conversion to end */
		ret = comedi_timeout(dev, s, insn, daq700_ai_eoc, 0);
		if (ret)
			return ret;

		/* read data */
		d = inw(dev->iobase + ADFIFO_R);
		/* mangle the data as necessary */
		/* Bipolar Offset Binary: 0 to 4095 for -10 to +10 */
		d &= 0x0fff;
		d ^= 0x0800;
		data[n] = d;
	}
	return n;
}

/*
 * Data acquisition is enabled.
 * The counter 0 output is high.
 * The I/O connector pin CLK1 drives counter 1 source.
 * Multiple-channel scanning is disabled.
 * All interrupts are disabled.
 * The analog input range is set to +-10 V
 * The analog input mode is single-ended.
 * The analog input circuitry is initialized to channel 0.
 * The A/D FIFO is cleared.
 */
static void daq700_ai_config(struct comedi_device *dev,
			     struct comedi_subdevice *s)
{
	unsigned long iobase = dev->iobase;

	outb(0x80, iobase + CMD_R1);	/* disable scanning, ADC to chan 0 */
	outb(0x00, iobase + CMD_R2);	/* clear all bits */
	outb(0x00, iobase + CMD_R3);	/* set +-10 range */
	outb(0x32, iobase + CMO_R);	/* config counter mode1, out0 to H */
	outb(0x00, iobase + TIC_R);	/* clear counter interrupt */
	outb(0x00, iobase + ADCLEAR_R);	/* clear the ADC FIFO */
	inw(iobase + ADFIFO_R);		/* read 16bit junk from FIFO to clear */
}

static int daq700_auto_attach(struct comedi_device *dev,
			      unsigned long context)
{
	struct pcmcia_device *link = comedi_to_pcmcia_dev(dev);
	struct comedi_subdevice *s;
	int ret;

	link->config_flags |= CONF_AUTO_SET_IO;
	ret = comedi_pcmcia_enable(dev, NULL);
	if (ret)
		return ret;
	dev->iobase = link->resource[0]->start;

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	/* DAQCard-700 dio */
	s = &dev->subdevices[0];
	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 16;
	s->range_table	= &range_digital;
	s->maxdata	= 1;
	s->insn_bits	= daq700_dio_insn_bits;
	s->insn_config	= daq700_dio_insn_config;
	s->io_bits	= 0x00ff;

	/* DAQCard-700 ai */
	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_DIFF;
	s->n_chan = 16;
	s->maxdata = (1 << 12) - 1;
	s->range_table = &range_daq700_ai;
	s->insn_read = daq700_ai_rinsn;
	daq700_ai_config(dev, s);

	return 0;
}

static struct comedi_driver daq700_driver = {
	.driver_name	= "ni_daq_700",
	.module		= THIS_MODULE,
	.auto_attach	= daq700_auto_attach,
	.detach		= comedi_pcmcia_disable,
};

static int daq700_cs_attach(struct pcmcia_device *link)
{
	return comedi_pcmcia_auto_config(link, &daq700_driver);
}

static const struct pcmcia_device_id daq700_cs_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x4743),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, daq700_cs_ids);

static struct pcmcia_driver daq700_cs_driver = {
	.name		= "ni_daq_700",
	.owner		= THIS_MODULE,
	.id_table	= daq700_cs_ids,
	.probe		= daq700_cs_attach,
	.remove		= comedi_pcmcia_auto_unconfig,
};
module_comedi_pcmcia_driver(daq700_driver, daq700_cs_driver);

MODULE_AUTHOR("Fred Brooks <nsaspook@nsaspook.com>");
MODULE_DESCRIPTION(
	"Comedi driver for National Instruments PCMCIA DAQCard-700 DIO/AI");
MODULE_LICENSE("GPL");
