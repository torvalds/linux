/*

    comedi/drivers/aio_iiro_16.c

    Driver for Access I/O Products PC-104 AIO-IIRO-16 Digital I/O board
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

Driver: aio_iiro_16
Description: Access I/O Products PC-104 IIRO16 Relay And Isolated Input Board
Author: Zachary Ware <zach.ware@cctechnol.com>
Devices:
 [Access I/O] PC-104 AIO12-8
Status: experimental

Configuration Options:
  [0] - I/O port base address

*/

#include "../comedidev.h"
#include <linux/ioport.h>

#define AIO_IIRO_16_SIZE	0x08
#define AIO_IIRO_16_RELAY_0_7	0x00
#define AIO_IIRO_16_INPUT_0_7	0x01
#define AIO_IIRO_16_IRQ		0x02
#define AIO_IIRO_16_RELAY_8_15	0x04
#define AIO_IIRO_16_INPUT_8_15	0x05

static int aio_iiro_16_dio_insn_bits_write(struct comedi_device *dev,
					   struct comedi_subdevice *s,
					   struct comedi_insn *insn,
					   unsigned int *data)
{
	if (data[0]) {
		s->state &= ~data[0];
		s->state |= data[0] & data[1];
		outb(s->state & 0xff, dev->iobase + AIO_IIRO_16_RELAY_0_7);
		outb((s->state >> 8) & 0xff,
		     dev->iobase + AIO_IIRO_16_RELAY_8_15);
	}

	data[1] = s->state;

	return insn->n;
}

static int aio_iiro_16_dio_insn_bits_read(struct comedi_device *dev,
					  struct comedi_subdevice *s,
					  struct comedi_insn *insn,
					  unsigned int *data)
{
	data[1] = 0;
	data[1] |= inb(dev->iobase + AIO_IIRO_16_INPUT_0_7);
	data[1] |= inb(dev->iobase + AIO_IIRO_16_INPUT_8_15) << 8;

	return insn->n;
}

static int aio_iiro_16_attach(struct comedi_device *dev,
			      struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], AIO_IIRO_16_SIZE);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 16;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = aio_iiro_16_dio_insn_bits_write;

	s = &dev->subdevices[1];
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 16;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = aio_iiro_16_dio_insn_bits_read;

	return 1;
}

static struct comedi_driver aio_iiro_16_driver = {
	.driver_name	= "aio_iiro_16",
	.module		= THIS_MODULE,
	.attach		= aio_iiro_16_attach,
	.detach		= comedi_legacy_detach,
};
module_comedi_driver(aio_iiro_16_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
