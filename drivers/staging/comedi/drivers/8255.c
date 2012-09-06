/*
    comedi/drivers/8255.c
    Driver for 8255

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

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/
/*
Driver: 8255
Description: generic 8255 support
Devices: [standard] 8255 (8255)
Author: ds
Status: works
Updated: Fri,  7 Jun 2002 12:56:45 -0700

The classic in digital I/O.  The 8255 appears in Comedi as a single
digital I/O subdevice with 24 channels.  The channel 0 corresponds
to the 8255's port A, bit 0; channel 23 corresponds to port C, bit
7.  Direction configuration is done in blocks, with channels 0-7,
8-15, 16-19, and 20-23 making up the 4 blocks.  The only 8255 mode
supported is mode 0.

You should enable compilation this driver if you plan to use a board
that has an 8255 chip.  For multifunction boards, the main driver will
configure the 8255 subdevice automatically.

This driver also works independently with ISA and PCI cards that
directly map the 8255 registers to I/O ports, including cards with
multiple 8255 chips.  To configure the driver for such a card, the
option list should be a list of the I/O port bases for each of the
8255 chips.  For example,

  comedi_config /dev/comedi0 8255 0x200,0x204,0x208,0x20c

Note that most PCI 8255 boards do NOT work with this driver, and
need a separate driver as a wrapper.  For those that do work, the
I/O port base address can be found in the output of 'lspci -v'.

*/

/*
   This file contains an exported subdevice for driving an 8255.

   To use this subdevice as part of another driver, you need to
   set up the subdevice in the attach function of the driver by
   calling:

     subdev_8255_init(device, subdevice, io_function, iobase)

   device and subdevice are pointers to the device and subdevice
   structures.  io_function will be called to provide the
   low-level input/output to the device, i.e., actual register
   access.  io_function will be called with the value of iobase
   as the last parameter.  If the 8255 device is mapped as 4
   consecutive I/O ports, you can use NULL for io_function
   and the I/O port base for iobase, and an internal function will
   handle the register access.

   In addition, if the main driver handles interrupts, you can
   enable commands on the subdevice by calling subdev_8255_init_irq()
   instead.  Then, when you get an interrupt that is likely to be
   from the 8255, you should call subdev_8255_interrupt(), which
   will copy the latched value to a Comedi buffer.
 */

#include "../comedidev.h"

#include <linux/ioport.h>
#include <linux/slab.h>
#include "8255.h"

#define _8255_SIZE	4

#define _8255_DATA	0
#define _8255_CR	3

#define CR_C_LO_IO	0x01
#define CR_B_IO		0x02
#define CR_B_MODE	0x04
#define CR_C_HI_IO	0x08
#define CR_A_IO		0x10
#define CR_A_MODE(a)	((a)<<5)
#define CR_CW		0x80

struct subdev_8255_private {
	unsigned long iobase;
	int (*io) (int, int, int, unsigned long);
};

static int subdev_8255_io(int dir, int port, int data, unsigned long iobase)
{
	if (dir) {
		outb(data, iobase + port);
		return 0;
	} else {
		return inb(iobase + port);
	}
}

void subdev_8255_interrupt(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	struct subdev_8255_private *spriv = s->private;
	unsigned long iobase = spriv->iobase;
	short d;

	d = spriv->io(0, _8255_DATA, 0, iobase);
	d |= (spriv->io(0, _8255_DATA + 1, 0, iobase) << 8);

	comedi_buf_put(s->async, d);
	s->async->events |= COMEDI_CB_EOS;

	comedi_event(dev, s);
}
EXPORT_SYMBOL(subdev_8255_interrupt);

static int subdev_8255_insn(struct comedi_device *dev,
			    struct comedi_subdevice *s,
			    struct comedi_insn *insn, unsigned int *data)
{
	struct subdev_8255_private *spriv = s->private;
	unsigned long iobase = spriv->iobase;
	unsigned int mask;
	unsigned int bits;
	unsigned int v;

	mask = data[0];
	bits = data[1];

	if (mask) {
		v = s->state;
		v &= ~mask;
		v |= (bits & mask);

		if (mask & 0xff)
			spriv->io(1, _8255_DATA, v & 0xff, iobase);
		if (mask & 0xff00)
			spriv->io(1, _8255_DATA + 1, (v >> 8) & 0xff, iobase);
		if (mask & 0xff0000)
			spriv->io(1, _8255_DATA + 2, (v >> 16) & 0xff, iobase);

		s->state = v;
	}

	v = spriv->io(0, _8255_DATA, 0, iobase);
	v |= (spriv->io(0, _8255_DATA + 1, 0, iobase) << 8);
	v |= (spriv->io(0, _8255_DATA + 2, 0, iobase) << 16);

	data[1] = v;

	return insn->n;
}

static void subdev_8255_do_config(struct comedi_device *dev,
				  struct comedi_subdevice *s)
{
	struct subdev_8255_private *spriv = s->private;
	unsigned long iobase = spriv->iobase;
	int config;

	config = CR_CW;
	/* 1 in io_bits indicates output, 1 in config indicates input */
	if (!(s->io_bits & 0x0000ff))
		config |= CR_A_IO;
	if (!(s->io_bits & 0x00ff00))
		config |= CR_B_IO;
	if (!(s->io_bits & 0x0f0000))
		config |= CR_C_LO_IO;
	if (!(s->io_bits & 0xf00000))
		config |= CR_C_HI_IO;

	spriv->io(1, _8255_CR, config, iobase);
}

static int subdev_8255_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	unsigned int mask;
	unsigned int bits;

	mask = 1 << CR_CHAN(insn->chanspec);
	if (mask & 0x0000ff)
		bits = 0x0000ff;
	else if (mask & 0x00ff00)
		bits = 0x00ff00;
	else if (mask & 0x0f0000)
		bits = 0x0f0000;
	else
		bits = 0xf00000;

	switch (data[0]) {
	case INSN_CONFIG_DIO_INPUT:
		s->io_bits &= ~bits;
		break;
	case INSN_CONFIG_DIO_OUTPUT:
		s->io_bits |= bits;
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] = (s->io_bits & bits) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
		break;
	default:
		return -EINVAL;
	}

	subdev_8255_do_config(dev, s);

	return 1;
}

static int subdev_8255_cmdtest(struct comedi_device *dev,
			       struct comedi_subdevice *s,
			       struct comedi_cmd *cmd)
{
	int err = 0;
	unsigned int tmp;

	/* step 1 */

	tmp = cmd->start_src;
	cmd->start_src &= TRIG_NOW;
	if (!cmd->start_src || tmp != cmd->start_src)
		err++;

	tmp = cmd->scan_begin_src;
	cmd->scan_begin_src &= TRIG_EXT;
	if (!cmd->scan_begin_src || tmp != cmd->scan_begin_src)
		err++;

	tmp = cmd->convert_src;
	cmd->convert_src &= TRIG_FOLLOW;
	if (!cmd->convert_src || tmp != cmd->convert_src)
		err++;

	tmp = cmd->scan_end_src;
	cmd->scan_end_src &= TRIG_COUNT;
	if (!cmd->scan_end_src || tmp != cmd->scan_end_src)
		err++;

	tmp = cmd->stop_src;
	cmd->stop_src &= TRIG_NONE;
	if (!cmd->stop_src || tmp != cmd->stop_src)
		err++;

	if (err)
		return 1;

	/* step 2 */

	if (err)
		return 2;

	/* step 3 */

	if (cmd->start_arg != 0) {
		cmd->start_arg = 0;
		err++;
	}
	if (cmd->scan_begin_arg != 0) {
		cmd->scan_begin_arg = 0;
		err++;
	}
	if (cmd->convert_arg != 0) {
		cmd->convert_arg = 0;
		err++;
	}
	if (cmd->scan_end_arg != 1) {
		cmd->scan_end_arg = 1;
		err++;
	}
	if (cmd->stop_arg != 0) {
		cmd->stop_arg = 0;
		err++;
	}

	if (err)
		return 3;

	/* step 4 */

	if (err)
		return 4;

	return 0;
}

static int subdev_8255_cmd(struct comedi_device *dev,
			   struct comedi_subdevice *s)
{
	/* FIXME */

	return 0;
}

static int subdev_8255_cancel(struct comedi_device *dev,
			      struct comedi_subdevice *s)
{
	/* FIXME */

	return 0;
}

int subdev_8255_init(struct comedi_device *dev, struct comedi_subdevice *s,
		     int (*io) (int, int, int, unsigned long),
		     unsigned long iobase)
{
	struct subdev_8255_private *spriv;

	spriv = kzalloc(sizeof(*spriv), GFP_KERNEL);
	if (!spriv)
		return -ENOMEM;

	spriv->iobase	= iobase;
	spriv->io	= io ? io : subdev_8255_io;

	s->private	= spriv;

	s->type		= COMEDI_SUBD_DIO;
	s->subdev_flags	= SDF_READABLE | SDF_WRITABLE;
	s->n_chan	= 24;
	s->range_table	= &range_digital;
	s->maxdata	= 1;
	s->insn_bits	= subdev_8255_insn;
	s->insn_config	= subdev_8255_insn_config;

	s->state	= 0;
	s->io_bits	= 0;

	subdev_8255_do_config(dev, s);

	return 0;
}
EXPORT_SYMBOL(subdev_8255_init);

int subdev_8255_init_irq(struct comedi_device *dev, struct comedi_subdevice *s,
			 int (*io) (int, int, int, unsigned long),
			 unsigned long iobase)
{
	int ret;

	ret = subdev_8255_init(dev, s, io, iobase);
	if (ret)
		return ret;

	s->do_cmdtest	= subdev_8255_cmdtest;
	s->do_cmd	= subdev_8255_cmd;
	s->cancel	= subdev_8255_cancel;

	return 0;
}
EXPORT_SYMBOL(subdev_8255_init_irq);

void subdev_8255_cleanup(struct comedi_device *dev, struct comedi_subdevice *s)
{
	kfree(s->private);
}
EXPORT_SYMBOL(subdev_8255_cleanup);

/*

   Start of the 8255 standalone device

 */

static int dev_8255_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret;
	unsigned long iobase;
	int i;

	dev->board_name = "8255";

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

		if (!request_region(iobase, _8255_SIZE, "8255")) {
			dev_warn(dev->class_dev,
				"0x%04lx (I/O port conflict)\n", iobase);

			s->type = COMEDI_SUBD_UNUSED;
		} else {
			ret = subdev_8255_init(dev, s, NULL, iobase);
			if (ret)
				return ret;
			dev_info(dev->class_dev, "0x%04lx\n", iobase);
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
			release_region(spriv->iobase, _8255_SIZE);
		}
		subdev_8255_cleanup(dev, s);
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
