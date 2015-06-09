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

struct subdev_8255_private {
	unsigned long regbase;
	int (*io)(struct comedi_device *, int, int, int, unsigned long);
};

static int subdev_8255_io(struct comedi_device *dev,
			  int dir, int port, int data, unsigned long regbase)
{
	if (dir) {
		outb(data, dev->iobase + regbase + port);
		return 0;
	}
	return inb(dev->iobase + regbase + port);
}

static int subdev_8255_mmio(struct comedi_device *dev,
			    int dir, int port, int data, unsigned long regbase)
{
	if (dir) {
		writeb(data, dev->mmio + regbase + port);
		return 0;
	}
	return readb(dev->mmio + regbase + port);
}

static int subdev_8255_insn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn,
			    unsigned int *data)
{
	struct subdev_8255_private *spriv = s->private;
	unsigned long regbase = spriv->regbase;
	unsigned int mask;
	unsigned int v;

	mask = comedi_dio_update_state(s, data);
	if (mask) {
		if (mask & 0xff)
			spriv->io(dev, 1, I8255_DATA_A_REG,
				  s->state & 0xff, regbase);
		if (mask & 0xff00)
			spriv->io(dev, 1, I8255_DATA_B_REG,
				  (s->state >> 8) & 0xff, regbase);
		if (mask & 0xff0000)
			spriv->io(dev, 1, I8255_DATA_C_REG,
				  (s->state >> 16) & 0xff, regbase);
	}

	v = spriv->io(dev, 0, I8255_DATA_A_REG, 0, regbase);
	v |= (spriv->io(dev, 0, I8255_DATA_B_REG, 0, regbase) << 8);
	v |= (spriv->io(dev, 0, I8255_DATA_C_REG, 0, regbase) << 16);

	data[1] = v;

	return insn->n;
}

static void subdev_8255_do_config(struct comedi_device *dev,
				  struct comedi_subdevice *s)
{
	struct subdev_8255_private *spriv = s->private;
	unsigned long regbase = spriv->regbase;
	int config;

	config = I8255_CTRL_CW;
	/* 1 in io_bits indicates output, 1 in config indicates input */
	if (!(s->io_bits & 0x0000ff))
		config |= I8255_CTRL_A_IO;
	if (!(s->io_bits & 0x00ff00))
		config |= I8255_CTRL_B_IO;
	if (!(s->io_bits & 0x0f0000))
		config |= I8255_CTRL_C_LO_IO;
	if (!(s->io_bits & 0xf00000))
		config |= I8255_CTRL_C_HI_IO;

	spriv->io(dev, 1, I8255_CTRL_REG, config, regbase);
}

static int subdev_8255_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	unsigned int mask;
	int ret;

	if (chan < 8)
		mask = 0x0000ff;
	else if (chan < 16)
		mask = 0x00ff00;
	else if (chan < 20)
		mask = 0x0f0000;
	else
		mask = 0xf00000;

	ret = comedi_dio_insn_config(dev, s, insn, data, mask);
	if (ret)
		return ret;

	subdev_8255_do_config(dev, s);

	return insn->n;
}

static int __subdev_8255_init(struct comedi_device *dev,
			      struct comedi_subdevice *s,
			      int (*io)(struct comedi_device *,
					int, int, int, unsigned long),
			      unsigned long regbase,
			      bool is_mmio)
{
	struct subdev_8255_private *spriv;

	spriv = comedi_alloc_spriv(s, sizeof(*spriv));
	if (!spriv)
		return -ENOMEM;

	if (io)
		spriv->io = io;
	else if (is_mmio)
		spriv->io = subdev_8255_mmio;
	else
		spriv->io = subdev_8255_io;
	spriv->regbase	= regbase;

	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 24;
	s->range_table	= &range_digital;
	s->maxdata	= 1;
	s->insn_bits	= subdev_8255_insn;
	s->insn_config	= subdev_8255_insn_config;

	subdev_8255_do_config(dev, s);

	return 0;
}

/**
 * subdev_8255_init - initialize DIO subdevice for driving I/O mapped 8255
 * @dev: comedi device owning subdevice
 * @s: comedi subdevice to initialize
 * @io: (optional) register I/O call-back function
 * @regbase: offset of 8255 registers from dev->iobase, or call-back context
 *
 * Initializes a comedi subdevice as a DIO subdevice driving an 8255 chip.
 *
 * If the optional I/O call-back function is provided, its prototype is of
 * the following form:
 *
 *   int my_8255_callback(struct comedi_device *dev,
 *                        struct comedi_subdevice *s, int dir, int port,
 *                        int data, unsigned long regbase);
 *
 * where 'dev', 's', and 'regbase' match the values passed to this function,
 * 'port' is the 8255 port number 0 to 3 (including the control port), 'dir'
 * is the direction (0 for read, 1 for write) and 'data' is the value to be
 * written.  It should return 0 if writing or the value read if reading.
 *
 * If the optional I/O call-back function is not provided, an internal
 * call-back function is used which uses consecutive I/O port addresses
 * starting at dev->iobase + regbase.
 *
 * Return: -ENOMEM if failed to allocate memory, zero on success.
 */
int subdev_8255_init(struct comedi_device *dev, struct comedi_subdevice *s,
		     int (*io)(struct comedi_device *,
			       int, int, int, unsigned long),
		     unsigned long regbase)
{
	return __subdev_8255_init(dev, s, io, regbase, false);
}
EXPORT_SYMBOL_GPL(subdev_8255_init);

/**
 * subdev_8255_mm_init - initialize DIO subdevice for driving mmio-mapped 8255
 * @dev: comedi device owning subdevice
 * @s: comedi subdevice to initialize
 * @io: (optional) register I/O call-back function
 * @regbase: offset of 8255 registers from dev->mmio, or call-back context
 *
 * Initializes a comedi subdevice as a DIO subdevice driving an 8255 chip.
 *
 * If the optional I/O call-back function is provided, its prototype is of
 * the following form:
 *
 *   int my_8255_callback(struct comedi_device *dev,
 *                        struct comedi_subdevice *s, int dir, int port,
 *                        int data, unsigned long regbase);
 *
 * where 'dev', 's', and 'regbase' match the values passed to this function,
 * 'port' is the 8255 port number 0 to 3 (including the control port), 'dir'
 * is the direction (0 for read, 1 for write) and 'data' is the value to be
 * written.  It should return 0 if writing or the value read if reading.
 *
 * If the optional I/O call-back function is not provided, an internal
 * call-back function is used which uses consecutive MMIO virtual addresses
 * starting at dev->mmio + regbase.
 *
 * Return: -ENOMEM if failed to allocate memory, zero on success.
 */
int subdev_8255_mm_init(struct comedi_device *dev, struct comedi_subdevice *s,
			int (*io)(struct comedi_device *,
				  int, int, int, unsigned long),
			unsigned long regbase)
{
	return __subdev_8255_init(dev, s, io, regbase, true);
}
EXPORT_SYMBOL_GPL(subdev_8255_mm_init);

/*
 * Start of the 8255 standalone device
 */

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
			if (ret)
				return ret;
		}
	}

	return 0;
}

static void dev_8255_detach(struct comedi_device *dev)
{
	struct comedi_subdevice *s;
	struct subdev_8255_private *spriv;
	int i;

	for (i = 0; i < dev->n_subdevices; i++) {
		s = &dev->subdevices[i];
		if (s->type != COMEDI_SUBD_UNUSED) {
			spriv = s->private;
			release_region(spriv->regbase, I8255_SIZE);
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
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
