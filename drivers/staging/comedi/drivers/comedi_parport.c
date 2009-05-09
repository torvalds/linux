/*
    comedi/drivers/comedi_parport.c
    hardware driver for standard parallel port

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998,2001 David A. Schleef <ds@schleef.org>

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
Driver: comedi_parport
Description: Standard PC parallel port
Author: ds
Status: works in immediate mode
Devices: [standard] parallel port (comedi_parport)
Updated: Tue, 30 Apr 2002 21:11:45 -0700

A cheap and easy way to get a few more digital I/O lines.  Steal
additional parallel ports from old computers or your neighbors'
computers.

Option list:
 0: I/O port base for the parallel port.
 1: IRQ

Parallel Port Lines:

pin     subdev  chan    aka
---     ------  ----    ---
1       2       0       strobe
2       0       0       data 0
3       0       1       data 1
4       0       2       data 2
5       0       3       data 3
6       0       4       data 4
7       0       5       data 5
8       0       6       data 6
9       0       7       data 7
10      1       3       acknowledge
11      1       4       busy
12      1       2       output
13      1       1       printer selected
14      2       1       auto LF
15      1       0       error
16      2       2       init
17      2       3       select printer
18-25   ground

Notes:

Subdevices 0 is digital I/O, subdevice 1 is digital input, and
subdevice 2 is digital output.  Unlike other Comedi devices,
subdevice 0 defaults to output.

Pins 13 and 14 are inverted once by Comedi and once by the
hardware, thus cancelling the effect.

Pin 1 is a strobe, thus acts like one.  There's no way in software
to change this, at least on a standard parallel port.

Subdevice 3 pretends to be a digital input subdevice, but it always
returns 0 when read.  However, if you run a command with
scan_begin_src=TRIG_EXT, it uses pin 10 as a external triggering
pin, which can be used to wake up tasks.
*/
/*
   see http://www.beyondlogic.org/ for information.
   or http://www.linux-magazin.de/ausgabe/1999/10/IO/io.html
 */

#include "../comedidev.h"
#include <linux/ioport.h>

#define PARPORT_SIZE 3

#define PARPORT_A 0
#define PARPORT_B 1
#define PARPORT_C 2

static int parport_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int parport_detach(struct comedi_device *dev);
static struct comedi_driver driver_parport = {
      .driver_name =	"comedi_parport",
      .module =		THIS_MODULE,
      .attach =		parport_attach,
      .detach =		parport_detach,
};

COMEDI_INITCLEANUP(driver_parport);

struct parport_private {
	unsigned int a_data;
	unsigned int c_data;
	int enable_irq;
};
#define devpriv ((struct parport_private *)(dev->private))

static int parport_insn_a(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	if (data[0]) {
		devpriv->a_data &= ~data[0];
		devpriv->a_data |= (data[0] & data[1]);

		outb(devpriv->a_data, dev->iobase + PARPORT_A);
	}

	data[1] = inb(dev->iobase + PARPORT_A);

	return 2;
}

static int parport_insn_config_a(struct comedi_device *dev, struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	if (data[0]) {
		s->io_bits = 0xff;
		devpriv->c_data &= ~(1 << 5);
	} else {
		s->io_bits = 0;
		devpriv->c_data |= (1 << 5);
	}
	outb(devpriv->c_data, dev->iobase + PARPORT_C);

	return 1;
}

static int parport_insn_b(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	if (data[0]) {
		/* should writes be ignored? */
		/* anyone??? */
	}

	data[1] = (inb(dev->iobase + PARPORT_B) >> 3);

	return 2;
}

static int parport_insn_c(struct comedi_device *dev, struct comedi_subdevice *s,
			  struct comedi_insn *insn, unsigned int *data)
{
	data[0] &= 0x0f;
	if (data[0]) {
		devpriv->c_data &= ~data[0];
		devpriv->c_data |= (data[0] & data[1]);

		outb(devpriv->c_data, dev->iobase + PARPORT_C);
	}

	data[1] = devpriv->c_data & 0xf;

	return 2;
}

static int parport_intr_insn(struct comedi_device *dev, struct comedi_subdevice *s,
			     struct comedi_insn *insn, unsigned int *data)
{
	if (insn->n < 1)
		return -EINVAL;

	data[1] = 0;
	return 2;
}

static int parport_intr_cmdtest(struct comedi_device *dev, struct comedi_subdevice *s,
				struct comedi_cmd *cmd)
{
	int err = 0;
	int tmp;

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

	/* step 2: ignored */

	if (err)
		return 2;

	/* step 3: */

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

	/* step 4: ignored */

	if (err)
		return 4;

	return 0;
}

static int parport_intr_cmd(struct comedi_device *dev, struct comedi_subdevice *s)
{
	devpriv->c_data |= 0x10;
	outb(devpriv->c_data, dev->iobase + PARPORT_C);

	devpriv->enable_irq = 1;

	return 0;
}

static int parport_intr_cancel(struct comedi_device *dev, struct comedi_subdevice *s)
{
	printk(KERN_DEBUG "parport_intr_cancel()\n");

	devpriv->c_data &= ~0x10;
	outb(devpriv->c_data, dev->iobase + PARPORT_C);

	devpriv->enable_irq = 0;

	return 0;
}

static irqreturn_t parport_interrupt(int irq, void *d PT_REGS_ARG)
{
	struct comedi_device *dev = d;
	struct comedi_subdevice *s = dev->subdevices + 3;

	if (!devpriv->enable_irq) {
		printk(KERN_ERR "comedi_parport: bogus irq, ignored\n");
		return IRQ_NONE;
	}

	comedi_buf_put(s->async, 0);
	s->async->events |= COMEDI_CB_BLOCK | COMEDI_CB_EOS;

	comedi_event(dev, s);
	return IRQ_HANDLED;
}

static int parport_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	int ret;
	unsigned int irq;
	unsigned long iobase;
	struct comedi_subdevice *s;

	iobase = it->options[0];
	printk(KERN_INFO "comedi%d: parport: 0x%04lx ", dev->minor, iobase);
	if (!request_region(iobase, PARPORT_SIZE, "parport (comedi)")) {
		printk("I/O port conflict\n");
		return -EIO;
	}
	dev->iobase = iobase;

	irq = it->options[1];
	if (irq) {
		printk(" irq=%u", irq);
		ret = comedi_request_irq(irq, parport_interrupt, 0,
			"comedi_parport", dev);
		if (ret < 0) {
			printk(" irq not available\n");
			return -EINVAL;
		}
		dev->irq = irq;
	}
	dev->board_name = "parport";

	ret = alloc_subdevices(dev, 4);
	if (ret < 0)
		return ret;
	ret = alloc_private(dev, sizeof(struct parport_private));
	if (ret < 0)
		return ret;

	s = dev->subdevices + 0;
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = 8;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = parport_insn_a;
	s->insn_config = parport_insn_config_a;

	s = dev->subdevices + 1;
	s->type = COMEDI_SUBD_DI;
	s->subdev_flags = SDF_READABLE;
	s->n_chan = 5;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = parport_insn_b;

	s = dev->subdevices + 2;
	s->type = COMEDI_SUBD_DO;
	s->subdev_flags = SDF_WRITABLE;
	s->n_chan = 4;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = parport_insn_c;

	s = dev->subdevices + 3;
	if (irq) {
		dev->read_subdev = s;
		s->type = COMEDI_SUBD_DI;
		s->subdev_flags = SDF_READABLE | SDF_CMD_READ;
		s->n_chan = 1;
		s->maxdata = 1;
		s->range_table = &range_digital;
		s->insn_bits = parport_intr_insn;
		s->do_cmdtest = parport_intr_cmdtest;
		s->do_cmd = parport_intr_cmd;
		s->cancel = parport_intr_cancel;
	} else {
		s->type = COMEDI_SUBD_UNUSED;
	}

	devpriv->a_data = 0;
	outb(devpriv->a_data, dev->iobase + PARPORT_A);
	devpriv->c_data = 0;
	outb(devpriv->c_data, dev->iobase + PARPORT_C);

	printk("\n");
	return 1;
}

static int parport_detach(struct comedi_device *dev)
{
	printk("comedi%d: parport: remove\n", dev->minor);

	if (dev->iobase)
		release_region(dev->iobase, PARPORT_SIZE);

	if (dev->irq)
		comedi_free_irq(dev->irq, dev);

	return 0;
}
