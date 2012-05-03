/*

    comedi/drivers/aio_aio12_8.c

    Driver for Access I/O Products PC-104 AIO12-8 Analog I/O Board
    Copyright (C) 2006 C&C Technologies, Inc.

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
*/

/*

Driver: aio_aio12_8
Description: Access I/O Products PC-104 AIO12-8 Analog I/O Board
Author: Pablo Mejia <pablo.mejia@cctechnol.com>
Devices:
 [Access I/O] PC-104 AIO12-8
Status: experimental

Configuration Options:
  [0] - I/O port base address

Notes:

  Only synchronous operations are supported.

*/

#include "../comedidev.h"
#include <linux/ioport.h>
#include "8255.h"

#define AIO12_8_STATUS			0x00
#define AIO12_8_INTERRUPT		0x01
#define AIO12_8_ADC			0x02
#define AIO12_8_DAC_0			0x04
#define AIO12_8_DAC_1			0x06
#define AIO12_8_DAC_2			0x08
#define AIO12_8_DAC_3			0x0A
#define AIO12_8_COUNTER_0		0x0C
#define AIO12_8_COUNTER_1		0x0D
#define AIO12_8_COUNTER_2		0x0E
#define AIO12_8_COUNTER_CONTROL		0x0F
#define AIO12_8_DIO_0			0x10
#define AIO12_8_DIO_1			0x11
#define AIO12_8_DIO_2			0x12
#define AIO12_8_DIO_STATUS		0x13
#define AIO12_8_DIO_CONTROL		0x14
#define AIO12_8_ADC_TRIGGER_CONTROL	0x15
#define AIO12_8_TRIGGER			0x16
#define AIO12_8_POWER			0x17

#define STATUS_ADC_EOC			0x80

#define ADC_MODE_NORMAL			0x00
#define ADC_MODE_INTERNAL_CLOCK		0x40
#define ADC_MODE_STANDBY		0x80
#define ADC_MODE_POWERDOWN		0xC0

#define DAC_ENABLE			0x18

struct aio12_8_boardtype {
	const char *name;
};

static const struct aio12_8_boardtype board_types[] = {
	{
	 .name = "aio_aio12_8"},
};

#define	thisboard	((const struct aio12_8_boardtype  *) dev->board_ptr)

struct aio12_8_private {
	unsigned int ao_readback[4];
};

#define devpriv	((struct aio12_8_private *) dev->private)

static int aio_aio12_8_ai_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int n;
	unsigned char control =
	    ADC_MODE_NORMAL |
	    (CR_RANGE(insn->chanspec) << 3) | CR_CHAN(insn->chanspec);

	/* read status to clear EOC latch */
	inb(dev->iobase + AIO12_8_STATUS);

	for (n = 0; n < insn->n; n++) {
		int timeout = 5;

		/*  Setup and start conversion */
		outb(control, dev->iobase + AIO12_8_ADC);

		/*  Wait for conversion to complete */
		while (timeout &&
		       !(inb(dev->iobase + AIO12_8_STATUS) & STATUS_ADC_EOC)) {
			timeout--;
			printk(KERN_ERR "timeout %d\n", timeout);
			udelay(1);
		}
		if (timeout == 0) {
			comedi_error(dev, "ADC timeout");
			return -EIO;
		}

		data[n] = inw(dev->iobase + AIO12_8_ADC) & 0x0FFF;
	}
	return n;
}

static int aio_aio12_8_ao_read(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int val = devpriv->ao_readback[CR_CHAN(insn->chanspec)];

	for (i = 0; i < insn->n; i++)
		data[i] = val;
	return insn->n;
}

static int aio_aio12_8_ao_write(struct comedi_device *dev,
				struct comedi_subdevice *s,
				struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);
	unsigned long port = dev->iobase + AIO12_8_DAC_0 + (2 * chan);

	/* enable DACs */
	outb(0x01, dev->iobase + DAC_ENABLE);

	for (i = 0; i < insn->n; i++) {
		outb(data[i] & 0xFF, port);	/*  LSB */
		outb((data[i] >> 8) & 0x0F, port + 1);	/*  MSB */
		devpriv->ao_readback[chan] = data[i];
	}
	return insn->n;
}

static const struct comedi_lrange range_aio_aio12_8 = {
	4,
	{
	 UNI_RANGE(5),
	 BIP_RANGE(5),
	 UNI_RANGE(10),
	 BIP_RANGE(10),
	 }
};

static int aio_aio12_8_attach(struct comedi_device *dev,
			      struct comedi_devconfig *it)
{
	int iobase;
	struct comedi_subdevice *s;

	iobase = it->options[0];
	if (!request_region(iobase, 24, "aio_aio12_8")) {
		printk(KERN_ERR "I/O port conflict");
		return -EIO;
	}

	dev->board_name = thisboard->name;

	dev->iobase = iobase;

	if (alloc_private(dev, sizeof(struct aio12_8_private)) < 0)
		return -ENOMEM;

	if (alloc_subdevices(dev, 3) < 0)
		return -ENOMEM;

	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_AI;
	s->subdev_flags = SDF_READABLE | SDF_GROUND | SDF_DIFF;
	s->n_chan = 8;
	s->maxdata = (1 << 12) - 1;
	s->range_table = &range_aio_aio12_8;
	s->insn_read = aio_aio12_8_ai_read;

	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE | SDF_GROUND | SDF_DIFF;
	s->n_chan = 4;
	s->maxdata = (1 << 12) - 1;
	s->range_table = &range_aio_aio12_8;
	s->insn_read = aio_aio12_8_ao_read;
	s->insn_write = aio_aio12_8_ao_write;

	s = &dev->subdevices[2];
	subdev_8255_init(dev, s, NULL, dev->iobase + AIO12_8_DIO_0);

	return 0;
}

static int aio_aio12_8_detach(struct comedi_device *dev)
{
	subdev_8255_cleanup(dev, &dev->subdevices[2]);
	if (dev->iobase)
		release_region(dev->iobase, 24);
	return 0;
}

static struct comedi_driver aio_aio12_8_driver = {
	.driver_name	= "aio_aio12_8",
	.module		= THIS_MODULE,
	.attach		= aio_aio12_8_attach,
	.detach		= aio_aio12_8_detach,
	.board_name	= &board_types[0].name,
	.num_names	= ARRAY_SIZE(board_types),
	.offset		= sizeof(struct aio12_8_boardtype),
};
module_comedi_driver(aio_aio12_8_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
