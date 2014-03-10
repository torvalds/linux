/*
   comedi/drivers/c6xdigio.c

   Hardware driver for Mechatronic Systems Inc. C6x_DIGIO DSP daughter card.
   (http://robot0.ge.uiuc.edu/~spong/mecha/)

   COMEDI - Linux Control and Measurement Device Interface
   Copyright (C) 1999 Dan Block

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
Driver: c6xdigio
Description: Mechatronic Systems Inc. C6x_DIGIO DSP daughter card
Author: Dan Block
Status: unknown
Devices: [Mechatronic Systems Inc.] C6x_DIGIO DSP daughter card (c6xdigio)
Updated: Sun Nov 20 20:18:34 EST 2005

This driver will not work with a 2.4 kernel.
http://robot0.ge.uiuc.edu/~spong/mecha/

*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/pnp.h>

#include "../comedidev.h"

/*
 * port offsets
 */
#define C6XDIGIO_PARALLEL_DATA 0
#define C6XDIGIO_PARALLEL_STATUS 1
#define C6XDIGIO_PARALLEL_CONTROL 2
struct pwmbitstype {
	unsigned sb0:2;
	unsigned sb1:2;
	unsigned sb2:2;
	unsigned sb3:2;
	unsigned sb4:2;
};
union pwmcmdtype {
	unsigned cmd;		/*  assuming here that int is 32bit */
	struct pwmbitstype bits;
};
struct encbitstype {
	unsigned sb0:3;
	unsigned sb1:3;
	unsigned sb2:3;
	unsigned sb3:3;
	unsigned sb4:3;
	unsigned sb5:3;
	unsigned sb6:3;
	unsigned sb7:3;
};
union encvaluetype {
	unsigned value;
	struct encbitstype bits;
};

#define C6XDIGIO_TIME_OUT 20

static int c6xdigio_chk_status(struct comedi_device *dev, unsigned long context)
{
	unsigned int status;
	int timeout = 0;

	do {
		status = inb(dev->iobase + 1);
		if ((status & 0x80) != context)
			return 0;
		timeout++;
	} while  (timeout < C6XDIGIO_TIME_OUT);

	return -EBUSY;
}

static int c6xdigio_write_data(struct comedi_device *dev,
			       unsigned int val, unsigned int status)
{
	outb_p(val, dev->iobase);
	return c6xdigio_chk_status(dev, status);
}

static void c6xdigio_pwm_init(struct comedi_device *dev)
{
	c6xdigio_write_data(dev, 0x70, 0x00);
	c6xdigio_write_data(dev, 0x74, 0x80);
	c6xdigio_write_data(dev, 0x70, 0x00);
	c6xdigio_write_data(dev, 0x00, 0x80);
}

static void c6xdigio_pwm_write(struct comedi_device *dev,
			       unsigned int chan, unsigned int val)
{
	unsigned ppcmd;
	union pwmcmdtype pwm;

	pwm.cmd = val;
	if (pwm.cmd > 498)
		pwm.cmd = 498;
	if (pwm.cmd < 2)
		pwm.cmd = 2;

	if (chan == 0)
		ppcmd = 0x28;
	else
		ppcmd = 0x30;

	c6xdigio_write_data(dev, ppcmd + pwm.bits.sb0, 0x00);
	c6xdigio_write_data(dev, ppcmd + pwm.bits.sb1 + 0x4, 0x80);
	c6xdigio_write_data(dev, ppcmd + pwm.bits.sb2, 0x00);
	c6xdigio_write_data(dev, ppcmd + pwm.bits.sb3 + 0x4, 0x80);
	c6xdigio_write_data(dev, ppcmd + pwm.bits.sb4, 0x00);
	c6xdigio_write_data(dev, 0x00, 0x80);
}

static int c6xdigio_encoder_read(struct comedi_device *dev,
				 unsigned int chan)
{
	unsigned ppcmd;
	union encvaluetype enc;

	enc.value = 0;
	if (chan == 0)
		ppcmd = 0x48;
	else
		ppcmd = 0x50;

	c6xdigio_write_data(dev, ppcmd, 0x00);

	enc.bits.sb0 = ((inb(dev->iobase + 1) >> 3) & 0x7);
	c6xdigio_write_data(dev, ppcmd + 0x4, 0x80);

	enc.bits.sb1 = ((inb(dev->iobase + 1) >> 3) & 0x7);
	c6xdigio_write_data(dev, ppcmd, 0x00);

	enc.bits.sb2 = ((inb(dev->iobase + 1) >> 3) & 0x7);
	c6xdigio_write_data(dev, ppcmd + 0x4, 0x80);

	enc.bits.sb3 = ((inb(dev->iobase + 1) >> 3) & 0x7);
	c6xdigio_write_data(dev, ppcmd, 0x00);

	enc.bits.sb4 = ((inb(dev->iobase + 1) >> 3) & 0x7);
	c6xdigio_write_data(dev, ppcmd + 0x4, 0x80);

	enc.bits.sb5 = ((inb(dev->iobase + 1) >> 3) & 0x7);
	c6xdigio_write_data(dev, ppcmd, 0x00);

	enc.bits.sb6 = ((inb(dev->iobase + 1) >> 3) & 0x7);
	c6xdigio_write_data(dev, ppcmd + 0x4, 0x80);

	enc.bits.sb7 = ((inb(dev->iobase + 1) >> 3) & 0x7);
	c6xdigio_write_data(dev, ppcmd, 0x00);

	c6xdigio_write_data(dev, 0x00, 0x80);

	return enc.value ^ 0x800000;
}

static void c6xdigio_encoder_reset(struct comedi_device *dev)
{
	c6xdigio_write_data(dev, 0x68, 0x00);
	c6xdigio_write_data(dev, 0x6c, 0x80);
	c6xdigio_write_data(dev, 0x68, 0x00);
	c6xdigio_write_data(dev, 0x00, 0x80);
}

static int c6xdigio_pwmo_insn_write(struct comedi_device *dev,
				    struct comedi_subdevice *s,
				    struct comedi_insn *insn,
				    unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int i;

	for (i = 0; i < insn->n; i++) {
		c6xdigio_pwm_write(dev, chan, data[i]);
		/*    devpriv->ao_readback[chan] = data[i]; */
	}
	return i;
}

static int c6xdigio_ei_insn_read(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	unsigned int chan = CR_CHAN(insn->chanspec);
	int n;

	for (n = 0; n < insn->n; n++)
		data[n] = (c6xdigio_encoder_read(dev, chan) & 0xffffff);

	return n;
}

static void board_init(struct comedi_device *dev)
{
	c6xdigio_pwm_init(dev);
	c6xdigio_encoder_reset(dev);
}

static const struct pnp_device_id c6xdigio_pnp_tbl[] = {
	/* Standard LPT Printer Port */
	{.id = "PNP0400", .driver_data = 0},
	/* ECP Printer Port */
	{.id = "PNP0401", .driver_data = 0},
	{}
};

static struct pnp_driver c6xdigio_pnp_driver = {
	.name = "c6xdigio",
	.id_table = c6xdigio_pnp_tbl,
};

static int c6xdigio_attach(struct comedi_device *dev,
			   struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int ret;

	ret = comedi_request_region(dev, it->options[0], 0x03);
	if (ret)
		return ret;

	ret = comedi_alloc_subdevices(dev, 2);
	if (ret)
		return ret;

	/*  Make sure that PnP ports get activated */
	pnp_register_driver(&c6xdigio_pnp_driver);

	s = &dev->subdevices[0];
	/* pwm output subdevice */
	s->type = COMEDI_SUBD_AO;	/*  Not sure what to put here */
	s->subdev_flags = SDF_WRITEABLE;
	s->n_chan = 2;
	/*      s->trig[0] = c6xdigio_pwmo; */
	s->insn_write = c6xdigio_pwmo_insn_write;
	s->maxdata = 500;
	s->range_table = &range_bipolar10;	/*  A suitable lie */

	s = &dev->subdevices[1];
	/* encoder (counter) subdevice */
	s->type = COMEDI_SUBD_COUNTER;
	s->subdev_flags = SDF_READABLE | SDF_LSAMPL;
	s->n_chan = 2;
	/* s->trig[0] = c6xdigio_ei; */
	s->insn_read = c6xdigio_ei_insn_read;
	s->maxdata = 0xffffff;
	s->range_table = &range_unknown;

	/*  I will call this init anyway but more than likely the DSP board */
	/*  will not be connected when device driver is loaded. */
	board_init(dev);

	return 0;
}

static void c6xdigio_detach(struct comedi_device *dev)
{
	comedi_legacy_detach(dev);
	pnp_unregister_driver(&c6xdigio_pnp_driver);
}

static struct comedi_driver c6xdigio_driver = {
	.driver_name	= "c6xdigio",
	.module		= THIS_MODULE,
	.attach		= c6xdigio_attach,
	.detach		= c6xdigio_detach,
};
module_comedi_driver(c6xdigio_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
