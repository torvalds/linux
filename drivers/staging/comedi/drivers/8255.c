/*
 * comedi/drivers/8255.c
 * Driver for 8255
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1998 David A. Schleef <ds@schleef.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*
 * Driver: 8255
 * Description: generic 8255 support
 * Devices: [standard] 8255 (8255)
 * Author: ds
 * Status: works
 * Updated: Fri,  7 Jun 2002 12:56:45 -0700
 *
 * The classic in digital I/O.  The 8255 appears in Comedi as a single
 * digital I/O subdevice with 24 channels.  The channel 0 corresponds
 * to the 8255's port A, bit 0; channel 23 corresponds to port C, bit
 * 7.  Direction configuration is done in blocks, with channels 0-7,
 * 8-15, 16-19, and 20-23 making up the 4 blocks.  The only 8255 mode
 * supported is mode 0.
 *
 * You should enable compilation this driver if you plan to use a board
 * that has an 8255 chip.  For multifunction boards, the main driver will
 * configure the 8255 subdevice automatically.
 *
 * This driver also works independently with ISA and PCI cards that
 * directly map the 8255 registers to I/O ports, including cards with
 * multiple 8255 chips.  To configure the driver for such a card, the
 * option list should be a list of the I/O port bases for each of the
 * 8255 chips.  For example,
 *
 *   comedi_config /dev/comedi0 8255 0x200,0x204,0x208,0x20c
 *
 * Note that most PCI 8255 boards do NOT work with this driver, and
 * need a separate driver as a wrapper.  For those that do work, the
 * I/O port base address can be found in the output of 'lspci -v'.
 */

#include <linux/module.h>
#include "../comedidev.h"

#include "8255.h"

static int dev_8255_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	unsigned long iobase;
	int ret;
	int i;

	for (i = 0; i < COMEDI_NDEVCONFOPTS; i++) {
		iobase = it->options[i];
		if (!iobase)
			break;
	}
	if (i == 0) {
		dev_warn(dev->class_dev, "no devices specified\n");
		return -EINVAL;
	}

	ret = comedi_alloc_subdevices(dev, i);
	if (ret)
		return ret;

	for (i = 0; i < dev->n_subdevices; i++) {
		s = &dev->subdevices[i];
		iobase = it->options[i];

		/*
		 * __comedi_request_region() does not set dev->iobase.
		 *
		 * For 8255 devices that are manually attached using
		 * comedi_config, the 'iobase' is the actual I/O port
		 * base address of the chip.
		 */
		ret = __comedi_request_region(dev, iobase, I8255_SIZE);
		if (ret) {
			s->type = COMEDI_SUBD_UNUSED;
		} else {
			ret = subdev_8255_init(dev, s, NULL, iobase);
			if (ret) {
				/*
				 * Release the I/O port region here, as the
				 * "detach" handler cannot find it.
				 */
				release_region(iobase, I8255_SIZE);
				s->type = COMEDI_SUBD_UNUSED;
				return ret;
			}
		}
	}

	return 0;
}

static void dev_8255_detach(struct comedi_device *dev)
{
	struct comedi_subdevice *s;
	int i;

	for (i = 0; i < dev->n_subdevices; i++) {
		s = &dev->subdevices[i];
		if (s->type != COMEDI_SUBD_UNUSED) {
			unsigned long regbase = subdev_8255_regbase(s);

			release_region(regbase, I8255_SIZE);
		}
	}
}

static struct comedi_driver dev_8255_driver = {
	.driver_name	= "8255",
	.module		= THIS_MODULE,
	.attach		= dev_8255_attach,
	.detach		= dev_8255_detach,
};
module_comedi_driver(dev_8255_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi driver for standalone 8255 devices");
MODULE_LICENSE("GPL");
