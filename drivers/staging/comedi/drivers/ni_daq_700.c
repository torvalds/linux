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
Updated: Wed, 19 Sep 2012 12:07:20 +0000

The daqcard-700 appears in Comedi as a  digital I/O subdevice (0) with
16 channels and a analog input subdevice (1) with 16 single-ended channels.

Digital:  The channel 0 corresponds to the daqcard-700's output
port, bit 0; channel 8 corresponds to the input port, bit 0.

Digital direction configuration: channels 0-7 output, 8-15 input (8225 device
emu as port A output, port B input, port C N/A).

Analog: The input  range is 0 to 4095 for -10 to +10 volts 
IRQ is assigned but not used.

Version 0.1	Original DIO only driver
Version 0.2	DIO and basic AI analog input support on 16 se channels

Manuals:	Register level:	http://www.ni.com/pdf/manuals/340698.pdf
		User Manual:	http://www.ni.com/pdf/manuals/320676d.pdf
*/

#include <linux/interrupt.h>
#include <linux/slab.h>
#include "../comedidev.h"

#include <linux/ioport.h>

#include <pcmcia/cistpl.h>
#include <pcmcia/cisreg.h>
#include <pcmcia/ds.h>

static struct pcmcia_device *pcmcia_cur_dev;

struct daq700_board {
	const char *name;
};

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

static int daq700_dio_insn_bits(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= (data[0] & data[1]);

		if (data[0] & 0xff)
			outb(s->state & 0xff, dev->iobase + DIO_W);
	}

	data[1] = s->state & 0xff;
	data[1] |= inb(dev->iobase + DIO_R) << 8;

	return insn->n;
}

static int daq700_dio_insn_config(struct comedi_device *dev,
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

static int daq700_ai_rinsn(struct comedi_device *dev,
			   struct comedi_subdevice *s,
			   struct comedi_insn *insn, unsigned int *data)
{
	int n, i, chan;
	int d;
	unsigned int status;
	enum { TIMEOUT = 100 };

	chan = CR_CHAN(insn->chanspec);
	/* write channel to multiplexer */
	/* set mask scan bit high to disable scanning */
	outb(chan | 0x80, dev->iobase + CMD_R1);

	/* convert n samples */
	for (n = 0; n < insn->n; n++) {
		/* trigger conversion with out0 L to H */
		outb(0x00, dev->iobase + CMD_R2); /* enable ADC conversions */
		outb(0x30, dev->iobase + CMO_R); /* mode 0 out0 L, from H */
		/* mode 1 out0 H, L to H, start conversion */
		outb(0x32, dev->iobase + CMO_R);
		/* wait for conversion to end */
		for (i = 0; i < TIMEOUT; i++) {
			status = inb(dev->iobase + STA_R2);
			if ((status & 0x03) != 0) {
				dev_info(dev->class_dev,
					 "Overflow/run Error\n");
				return -EOVERFLOW;
			}
			status = inb(dev->iobase + STA_R1);
			if ((status & 0x02) != 0) {
				dev_info(dev->class_dev, "Data Error\n");
				return -ENODATA;
			}
			if ((status & 0x11) == 0x01) {
				/* ADC conversion complete */
				break;
			}
			udelay(1);
		}
		if (i == TIMEOUT) {
			dev_info(dev->class_dev,
				 "timeout during ADC conversion\n");
			return -ETIMEDOUT;
		}
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

static int daq700_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	const struct daq700_board *thisboard = comedi_board(dev);
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
	s->state	= 0;
	s->io_bits	= 0x00ff;

	/* DAQCard-700 ai */
	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_AI;
	/* we support single-ended (ground)  */
	s->subdev_flags = SDF_READABLE | SDF_GROUND;
	s->n_chan = 16;
	s->maxdata = (1 << 12) - 1;
	s->range_table = &range_bipolar10;
	s->insn_read = daq700_ai_rinsn;
	daq700_ai_config(dev, s);

	dev_info(dev->class_dev, "%s: %s, io 0x%lx\n",
		dev->driver->driver_name,
		dev->board_name,
		dev->iobase);

	return 0;
}

static void daq700_detach(struct comedi_device *dev)
{
	/* nothing to cleanup */
}

static const struct daq700_board daq700_boards[] = {
	{
		.name		= "daqcard-700",
	}, {
		.name		= "ni_daq_700",
	},
};

static struct comedi_driver daq700_driver = {
	.driver_name	= "ni_daq_700",
	.module		= THIS_MODULE,
	.attach		= daq700_attach,
	.detach		= daq700_detach,
	.board_name	= &daq700_boards[0].name,
	.num_names	= ARRAY_SIZE(daq700_boards),
	.offset		= sizeof(struct daq700_board),
};

static int daq700_pcmcia_config_loop(struct pcmcia_device *p_dev,
				void *priv_data)
{
	if (p_dev->config_index == 0)
		return -EINVAL;

	return pcmcia_request_io(p_dev);
}

static int daq700_cs_attach(struct pcmcia_device *link)
{
	int ret;

	link->config_flags |= CONF_ENABLE_IRQ | CONF_AUTO_AUDIO |
		CONF_AUTO_SET_IO;

	ret = pcmcia_loop_config(link, daq700_pcmcia_config_loop, NULL);
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

static void daq700_cs_detach(struct pcmcia_device *link)
{
	pcmcia_disable_device(link);
	pcmcia_cur_dev = NULL;
}

static const struct pcmcia_device_id daq700_cs_ids[] = {
	PCMCIA_DEVICE_MANF_CARD(0x010b, 0x4743),
	PCMCIA_DEVICE_NULL
};
MODULE_DEVICE_TABLE(pcmcia, daq700_cs_ids);

static struct pcmcia_driver daq700_cs_driver = {
	.name		= "ni_daq_700",
	.owner		= THIS_MODULE,
	.probe		= daq700_cs_attach,
	.remove		= daq700_cs_detach,
	.id_table	= daq700_cs_ids,
};

static int __init daq700_cs_init(void)
{
	int ret;

	ret = comedi_driver_register(&daq700_driver);
	if (ret < 0)
		return ret;

	ret = pcmcia_register_driver(&daq700_cs_driver);
	if (ret < 0) {
		comedi_driver_unregister(&daq700_driver);
		return ret;
	}

	return 0;
}
module_init(daq700_cs_init);

static void __exit daq700_cs_exit(void)
{
	pcmcia_unregister_driver(&daq700_cs_driver);
	comedi_driver_unregister(&daq700_driver);
}
module_exit(daq700_cs_exit);

MODULE_AUTHOR("Fred Brooks <nsaspook@nsaspook.com>");
MODULE_DESCRIPTION(
	"Comedi driver for National Instruments PCMCIA DAQCard-700 DIO/AI");
MODULE_VERSION("0.2.00");
MODULE_LICENSE("GPL");
