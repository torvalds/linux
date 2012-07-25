/*
    comedi/drivers/pcmda12.c
    Driver for Winsystems PC-104 based PCM-D/A-12 8-channel AO board.

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2006 Calin A. Culianu <calin@ajvar.org>

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
Driver: pcmda12
Description: A driver for the Winsystems PCM-D/A-12
Devices: [Winsystems] PCM-D/A-12 (pcmda12)
Author: Calin Culianu <calin@ajvar.org>
Updated: Fri, 13 Jan 2006 12:01:01 -0500
Status: works

A driver for the relatively straightforward-to-program PCM-D/A-12.
This board doesn't support commands, and the only way to set its
analog output range is to jumper the board.  As such,
comedi_data_write() ignores the range value specified.

The board uses 16 consecutive I/O addresses starting at the I/O port
base address.  Each address corresponds to the LSB then MSB of a
particular channel from 0-7.

Note that the board is not ISA-PNP capable and thus
needs the I/O port comedi_config parameter.

Note that passing a nonzero value as the second config option will
enable "simultaneous xfer" mode for this board, in which AO writes
will not take effect until a subsequent read of any AO channel.  This
is so that one can speed up programming by preloading all AO registers
with values before simultaneously setting them to take effect with one
read command.

Configuration Options:
  [0] - I/O port base address
  [1] - Do Simultaneous Xfer (see description)
*/

#include "../comedidev.h"

#include <linux/pci.h>		/* for PCI devices */

#define SDEV_NO ((int)(s - dev->subdevices))
#define CHANS 8
#define IOSIZE 16
#define LSB(x) ((unsigned char)((x) & 0xff))
#define MSB(x) ((unsigned char)((((unsigned short)(x))>>8) & 0xff))
#define LSB_PORT(chan) (dev->iobase + (chan)*2)
#define MSB_PORT(chan) (LSB_PORT(chan)+1)
#define BITS 12

/*
 * Bords
 */
struct pcmda12_board {
	const char *name;
};

/* note these have no effect and are merely here for reference..
   these are configured by jumpering the board! */
static const struct comedi_lrange pcmda12_ranges = {
	3,
	{
	 UNI_RANGE(5), UNI_RANGE(10), BIP_RANGE(5)
	 }
};

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct pcmda12_board *)dev->board_ptr)

struct pcmda12_private {

	unsigned int ao_readback[CHANS];
	int simultaneous_xfer_mode;
};

#define devpriv ((struct pcmda12_private *)(dev->private))

static void zero_chans(struct comedi_device *dev)
{				/* sets up an
				   ASIC chip to defaults */
	int i;
	for (i = 0; i < CHANS; ++i) {
/*      /\* do this as one instruction?? *\/ */
/*      outw(0, LSB_PORT(chan)); */
		outb(0, LSB_PORT(i));
		outb(0, MSB_PORT(i));
	}
	inb(LSB_PORT(0));	/* update chans. */
}

static int ao_winsn(struct comedi_device *dev, struct comedi_subdevice *s,
		    struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);

	/* Writing a list of values to an AO channel is probably not
	 * very useful, but that's how the interface is defined. */
	for (i = 0; i < insn->n; ++i) {

/*      /\* do this as one instruction?? *\/ */
/*      outw(data[i], LSB_PORT(chan)); */

		/* Need to do this as two instructions due to 8-bit bus?? */
		/*  first, load the low byte */
		outb(LSB(data[i]), LSB_PORT(chan));
		/*  next, write the high byte */
		outb(MSB(data[i]), MSB_PORT(chan));

		/* save shadow register */
		devpriv->ao_readback[chan] = data[i];

		if (!devpriv->simultaneous_xfer_mode)
			inb(LSB_PORT(chan));
	}

	/* return the number of samples written */
	return i;
}

/* AO subdevices should have a read insn as well as a write insn.

   Usually this means copying a value stored in devpriv->ao_readback.
   However, since this driver supports simultaneous xfer then sometimes
   this function actually accomplishes work.

   Simultaneaous xfer mode is accomplished by loading ALL the values
   you want for AO in all the channels, then READing off one of the AO
   registers to initiate the instantaneous simultaneous update of all
   DAC outputs, which makes all AO channels update simultaneously.
   This is useful for some control applications, I would imagine.
*/
static int ao_rinsn(struct comedi_device *dev, struct comedi_subdevice *s,
		    struct comedi_insn *insn, unsigned int *data)
{
	int i;
	int chan = CR_CHAN(insn->chanspec);

	for (i = 0; i < insn->n; i++) {
		if (devpriv->simultaneous_xfer_mode)
			inb(LSB_PORT(chan));
		/* read back shadow register */
		data[i] = devpriv->ao_readback[chan];
	}

	return i;
}

static int pcmda12_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;
	unsigned long iobase;

	iobase = it->options[0];
	printk(KERN_INFO
	       "comedi%d: %s: io: %lx %s ", dev->minor, dev->driver->driver_name,
	       iobase, it->options[1] ? "simultaneous xfer mode enabled" : "");

	if (!request_region(iobase, IOSIZE, dev->driver->driver_name)) {
		printk("I/O port conflict\n");
		return -EIO;
	}
	dev->iobase = iobase;

/*
 * Initialize dev->board_name.  Note that we can use the "thisboard"
 * macro now, since we just initialized it in the last line.
 */
	dev->board_name = thisboard->name;

/*
 * Allocate the private structure area.  alloc_private() is a
 * convenient macro defined in comedidev.h.
 */
	if (alloc_private(dev, sizeof(struct pcmda12_private)) < 0) {
		printk(KERN_ERR "cannot allocate private data structure\n");
		return -ENOMEM;
	}

	devpriv->simultaneous_xfer_mode = it->options[1];

	/*
	 * Allocate the subdevice structures.  alloc_subdevice() is a
	 * convenient macro defined in comedidev.h.
	 *
	 * Allocate 2 subdevs (32 + 16 DIO lines) or 3 32 DIO subdevs for the
	 * 96-channel version of the board.
	 */
	if (alloc_subdevices(dev, 1) < 0) {
		printk(KERN_ERR "cannot allocate subdevice data structures\n");
		return -ENOMEM;
	}

	s = dev->subdevices;
	s->private = NULL;
	s->maxdata = (0x1 << BITS) - 1;
	s->range_table = &pcmda12_ranges;
	s->type = COMEDI_SUBD_AO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = CHANS;
	s->insn_write = &ao_winsn;
	s->insn_read = &ao_rinsn;

	zero_chans(dev);	/* clear out all the registers, basically */

	printk(KERN_INFO "attached\n");

	return 1;
}

static void pcmda12_detach(struct comedi_device *dev)
{
	if (dev->iobase)
		release_region(dev->iobase, IOSIZE);
}

static const struct pcmda12_board pcmda12_boards[] = {
	{
		.name	= "pcmda12",
	},
};

static struct comedi_driver pcmda12_driver = {
	.driver_name	= "pcmda12",
	.module		= THIS_MODULE,
	.attach		= pcmda12_attach,
	.detach		= pcmda12_detach,
	.board_name	= &pcmda12_boards[0].name,
	.offset		= sizeof(struct pcmda12_board),
	.num_names	= ARRAY_SIZE(pcmda12_boards),
};
module_comedi_driver(pcmda12_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");
