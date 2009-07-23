/*
   comedi/drivers/dt2815.c
   Hardware driver for Data Translation DT2815

   COMEDI - Linux Control and Measurement Device Interface
   Copyright (C) 1999 Anders Blomdell <anders.blomdell@control.lth.se>

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
Driver: dt2815
Description: Data Translation DT2815
Author: ds
Status: mostly complete, untested
Devices: [Data Translation] DT2815 (dt2815)

I'm not sure anyone has ever tested this board.  If you have information
contrary, please update.

Configuration options:
  [0] - I/O port base base address
  [1] - IRQ (unused)
  [2] - Voltage unipolar/bipolar configuration
          0 == unipolar 5V  (0V -- +5V)
	  1 == bipolar 5V  (-5V -- +5V)
  [3] - Current offset configuration
          0 == disabled  (0mA -- +32mAV)
          1 == enabled  (+4mA -- +20mAV)
  [4] - Firmware program configuration
          0 == program 1 (see manual table 5-4)
          1 == program 2 (see manual table 5-4)
          2 == program 3 (see manual table 5-4)
          3 == program 4 (see manual table 5-4)
  [5] - Analog output 0 range configuration
          0 == voltage
          1 == current
  [6] - Analog output 1 range configuration (same options)
  [7] - Analog output 2 range configuration (same options)
  [8] - Analog output 3 range configuration (same options)
  [9] - Analog output 4 range configuration (same options)
  [10] - Analog output 5 range configuration (same options)
  [11] - Analog output 6 range configuration (same options)
  [12] - Analog output 7 range configuration (same options)
*/

#include "../comedidev.h"

#include <linux/ioport.h>
#include <linux/delay.h>

static const struct comedi_lrange range_dt2815_ao_32_current = { 1, {
			RANGE_mA(0, 32)
	}
};
static const struct comedi_lrange range_dt2815_ao_20_current = { 1, {
			RANGE_mA(4, 20)
	}
};

#define DT2815_SIZE 2

#define DT2815_DATA 0
#define DT2815_STATUS 1

static int dt2815_attach(struct comedi_device *dev, struct comedi_devconfig *it);
static int dt2815_detach(struct comedi_device *dev);
static struct comedi_driver driver_dt2815 = {
	.driver_name = "dt2815",
	.module = THIS_MODULE,
	.attach = dt2815_attach,
	.detach = dt2815_detach,
};

COMEDI_INITCLEANUP(driver_dt2815);

static void dt2815_free_resources(struct comedi_device *dev);

struct dt2815_private {

	const struct comedi_lrange *range_type_list[8];
	unsigned int ao_readback[8];
};


#define devpriv ((struct dt2815_private *)dev->private)

static int dt2815_wait_for_status(struct comedi_device *dev, int status)
{
	int i;

	for (i = 0; i < 100; i++) {
		if (inb(dev->iobase + DT2815_STATUS) == status)
			break;
	}
	return status;
}

static int dt2815_ao_insn_read(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++) {
		data[i] = devpriv->ao_readback[chan];
	}

	return i;
}

static int dt2815_ao_insn(struct comedi_device *dev, struct comedi_subdevice *s,
	struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);
	unsigned int status;
	unsigned int lo, hi;

	for (i = 0; i < insn->n; i++) {
		lo = ((data[i] & 0x0f) << 4) | (chan << 1) | 0x01;
		hi = (data[i] & 0xff0) >> 4;

		status = dt2815_wait_for_status(dev, 0x00);
		if (status != 0) {
			printk
				("dt2815: failed to write low byte on %d reason %x\n",
				chan, status);
			return -EBUSY;
		}

		outb(lo, dev->iobase + DT2815_DATA);

		status = dt2815_wait_for_status(dev, 0x10);
		if (status != 0x10) {
			printk
				("dt2815: failed to write high byte on %d reason %x\n",
				chan, status);
			return -EBUSY;
		}
		devpriv->ao_readback[chan] = data[i];
	}
	return i;
}

/*
  options[0]   Board base address
  options[1]   IRQ (not applicable)
  options[2]   Voltage unipolar/bipolar configuration
                 0 == unipolar 5V  (0V -- +5V)
		 1 == bipolar 5V  (-5V -- +5V)
  options[3]   Current offset configuration
                 0 == disabled  (0mA -- +32mAV)
                 1 == enabled  (+4mA -- +20mAV)
  options[4]   Firmware program configuration
                 0 == program 1 (see manual table 5-4)
                 1 == program 2 (see manual table 5-4)
                 2 == program 3 (see manual table 5-4)
                 3 == program 4 (see manual table 5-4)
  options[5]   Analog output 0 range configuration
                 0 == voltage
                 1 == current
  options[6]   Analog output 1 range configuration
  ...
  options[12]   Analog output 7 range configuration
                 0 == voltage
                 1 == current
 */

static int dt2815_attach(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	int i;
	const struct comedi_lrange *current_range_type, *voltage_range_type;
	unsigned long iobase;

	iobase = it->options[0];
	printk("comedi%d: dt2815: 0x%04lx ", dev->minor, iobase);
	if (!request_region(iobase, DT2815_SIZE, "dt2815")) {
		printk("I/O port conflict\n");
		return -EIO;
	}

	dev->iobase = iobase;
	dev->board_name = "dt2815";

	if (alloc_subdevices(dev, 1) < 0)
		return -ENOMEM;
	if (alloc_private(dev, sizeof(struct dt2815_private)) < 0)
		return -ENOMEM;

	s = dev->subdevices;
	/* ao subdevice */
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_WRITABLE;
	s->maxdata = 0xfff;
	s->n_chan = 8;
	s->insn_write = dt2815_ao_insn;
	s->insn_read = dt2815_ao_insn_read;
	s->range_table_list = devpriv->range_type_list;

	current_range_type = (it->options[3])
		? &range_dt2815_ao_20_current : &range_dt2815_ao_32_current;
	voltage_range_type = (it->options[2])
		? &range_bipolar5 : &range_unipolar5;
	for (i = 0; i < 8; i++) {
		devpriv->range_type_list[i] = (it->options[5 + i])
			? current_range_type : voltage_range_type;
	}

	/* Init the 2815 */
	outb(0x00, dev->iobase + DT2815_STATUS);
	for (i = 0; i < 100; i++) {
		/* This is incredibly slow (approx 20 ms) */
		unsigned int status;

		udelay(1000);
		status = inb(dev->iobase + DT2815_STATUS);
		if (status == 4) {
			unsigned int program;
			program = (it->options[4] & 0x3) << 3 | 0x7;
			outb(program, dev->iobase + DT2815_DATA);
			printk(", program: 0x%x (@t=%d)\n", program, i);
			break;
		} else if (status != 0x00) {
			printk("dt2815: unexpected status 0x%x (@t=%d)\n",
				status, i);
			if (status & 0x60) {
				outb(0x00, dev->iobase + DT2815_STATUS);
			}
		}
	}

	printk("\n");

	return 0;
}

static void dt2815_free_resources(struct comedi_device *dev)
{
	if (dev->iobase)
		release_region(dev->iobase, DT2815_SIZE);
}

static int dt2815_detach(struct comedi_device *dev)
{
	printk("comedi%d: dt2815: remove\n", dev->minor);

	dt2815_free_resources(dev);

	return 0;
}
