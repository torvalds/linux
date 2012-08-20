/*
    comedi/drivers/comedi_bond.c
    A Comedi driver to 'bond' or merge multiple drivers and devices as one.

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>
    Copyright (C) 2005 Calin A. Culianu <calin@ajvar.org>

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
Driver: comedi_bond
Description: A driver to 'bond' (merge) multiple subdevices from multiple
	     devices together as one.
Devices:
Author: ds
Updated: Mon, 10 Oct 00:18:25 -0500
Status: works

This driver allows you to 'bond' (merge) multiple comedi subdevices
(coming from possibly difference boards and/or drivers) together.  For
example, if you had a board with 2 different DIO subdevices, and
another with 1 DIO subdevice, you could 'bond' them with this driver
so that they look like one big fat DIO subdevice.  This makes writing
applications slightly easier as you don't have to worry about managing
different subdevices in the application -- you just worry about
indexing one linear array of channel id's.

Right now only DIO subdevices are supported as that's the personal itch
I am scratching with this driver.  If you want to add support for AI and AO
subdevs, go right on ahead and do so!

Commands aren't supported -- although it would be cool if they were.

Configuration Options:
  List of comedi-minors to bond.  All subdevices of the same type
  within each minor will be concatenated together in the order given here.
*/

#include <linux/string.h>
#include <linux/slab.h>
#include "../comedi.h"
#include "../comedilib.h"
#include "../comedidev.h"

/* The maxiumum number of channels per subdevice. */
#define MAX_CHANS 256

struct BondedDevice {
	struct comedi_device *dev;
	unsigned minor;
	unsigned subdev;
	unsigned subdev_type;
	unsigned nchans;
	unsigned chanid_offset;	/* The offset into our unified linear
				   channel-id's of chanid 0 on this
				   subdevice. */
};

/* this structure is for data unique to this hardware driver.  If
   several hardware drivers keep similar information in this structure,
   feel free to suggest moving the variable to the struct comedi_device struct.  */
struct comedi_bond_private {
# define MAX_BOARD_NAME 256
	char name[MAX_BOARD_NAME];
	struct BondedDevice **devs;
	unsigned ndevs;
	struct BondedDevice *chanIdDevMap[MAX_CHANS];
	unsigned nchans;
};

/* DIO devices are slightly special.  Although it is possible to
 * implement the insn_read/insn_write interface, it is much more
 * useful to applications if you implement the insn_bits interface.
 * This allows packed reading/writing of the DIO channels.  The
 * comedi core can convert between insn_bits and insn_read/write */
static int bonding_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	struct comedi_bond_private *devpriv = dev->private;
#define LSAMPL_BITS (sizeof(unsigned int)*8)
	unsigned nchans = LSAMPL_BITS, num_done = 0, i;

	if (devpriv->nchans < nchans)
		nchans = devpriv->nchans;

	/* The insn data is a mask in data[0] and the new data
	 * in data[1], each channel cooresponding to a bit. */
	for (i = 0; num_done < nchans && i < devpriv->ndevs; ++i) {
		struct BondedDevice *bdev = devpriv->devs[i];
		/* Grab the channel mask and data of only the bits corresponding
		   to this subdevice.. need to shift them to zero position of
		   course. */
		/* Bits corresponding to this subdev. */
		unsigned int subdevMask = ((1 << bdev->nchans) - 1);
		unsigned int writeMask, dataBits;

		/* Argh, we have >= LSAMPL_BITS chans.. take all bits */
		if (bdev->nchans >= LSAMPL_BITS)
			subdevMask = (unsigned int)(-1);

		writeMask = (data[0] >> num_done) & subdevMask;
		dataBits = (data[1] >> num_done) & subdevMask;

		/* Read/Write the new digital lines */
		if (comedi_dio_bitfield(bdev->dev, bdev->subdev, writeMask,
					&dataBits) != 2)
			return -EINVAL;

		/* Make room for the new bits in data[1], the return value */
		data[1] &= ~(subdevMask << num_done);
		/* Put the bits in the return value */
		data[1] |= (dataBits & subdevMask) << num_done;
		/* Save the new bits to the saved state.. */
		s->state = data[1];

		num_done += bdev->nchans;
	}

	return insn->n;
}

static int bonding_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	struct comedi_bond_private *devpriv = dev->private;
	int chan = CR_CHAN(insn->chanspec), ret, io_bits = s->io_bits;
	unsigned int io;
	struct BondedDevice *bdev;

	if (chan < 0 || chan >= devpriv->nchans)
		return -EINVAL;
	bdev = devpriv->chanIdDevMap[chan];

	/* The input or output configuration of each digital line is
	 * configured by a special insn_config instruction.  chanspec
	 * contains the channel to be changed, and data[0] contains the
	 * value COMEDI_INPUT or COMEDI_OUTPUT. */
	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
		io = COMEDI_OUTPUT;	/* is this really necessary? */
		io_bits |= 1 << chan;
		break;
	case INSN_CONFIG_DIO_INPUT:
		io = COMEDI_INPUT;	/* is this really necessary? */
		io_bits &= ~(1 << chan);
		break;
	case INSN_CONFIG_DIO_QUERY:
		data[1] =
		    (io_bits & (1 << chan)) ? COMEDI_OUTPUT : COMEDI_INPUT;
		return insn->n;
		break;
	default:
		return -EINVAL;
		break;
	}
	/* 'real' channel id for this subdev.. */
	chan -= bdev->chanid_offset;
	ret = comedi_dio_config(bdev->dev, bdev->subdev, chan, io);
	if (ret != 1)
		return -EINVAL;
	/* Finally, save the new io_bits values since we didn't get
	   an error above. */
	s->io_bits = io_bits;
	return insn->n;
}

static void *Realloc(const void *oldmem, size_t newlen, size_t oldlen)
{
	void *newmem = kmalloc(newlen, GFP_KERNEL);

	if (newmem && oldmem)
		memcpy(newmem, oldmem, min(oldlen, newlen));
	kfree(oldmem);
	return newmem;
}

static int doDevConfig(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_bond_private *devpriv = dev->private;
	int i;
	struct comedi_device *devs_opened[COMEDI_NUM_BOARD_MINORS];

	memset(devs_opened, 0, sizeof(devs_opened));
	devpriv->name[0] = 0;
	/* Loop through all comedi devices specified on the command-line,
	   building our device list */
	for (i = 0; i < COMEDI_NDEVCONFOPTS && (!i || it->options[i]); ++i) {
		char file[] = "/dev/comediXXXXXX";
		int minor = it->options[i];
		struct comedi_device *d;
		int sdev = -1, nchans, tmp;
		struct BondedDevice *bdev = NULL;

		if (minor < 0 || minor >= COMEDI_NUM_BOARD_MINORS) {
			dev_err(dev->class_dev,
				"Minor %d is invalid!\n", minor);
			return 0;
		}
		if (minor == dev->minor) {
			dev_err(dev->class_dev,
				"Cannot bond this driver to itself!\n");
			return 0;
		}
		if (devs_opened[minor]) {
			dev_err(dev->class_dev,
				"Minor %d specified more than once!\n", minor);
			return 0;
		}

		snprintf(file, sizeof(file), "/dev/comedi%u", minor);
		file[sizeof(file) - 1] = 0;

		d = devs_opened[minor] = comedi_open(file);

		if (!d) {
			dev_err(dev->class_dev,
				"Minor %u could not be opened\n", minor);
			return 0;
		}

		/* Do DIO, as that's all we support now.. */
		while ((sdev = comedi_find_subdevice_by_type(d, COMEDI_SUBD_DIO,
							     sdev + 1)) > -1) {
			nchans = comedi_get_n_channels(d, sdev);
			if (nchans <= 0) {
				dev_err(dev->class_dev,
					"comedi_get_n_channels() returned %d on minor %u subdev %d!\n",
					nchans, minor, sdev);
				return 0;
			}
			bdev = kmalloc(sizeof(*bdev), GFP_KERNEL);
			if (!bdev) {
				dev_err(dev->class_dev, "Out of memory\n");
				return 0;
			}
			bdev->dev = d;
			bdev->minor = minor;
			bdev->subdev = sdev;
			bdev->subdev_type = COMEDI_SUBD_DIO;
			bdev->nchans = nchans;
			bdev->chanid_offset = devpriv->nchans;

			/* map channel id's to BondedDevice * pointer.. */
			while (nchans--)
				devpriv->chanIdDevMap[devpriv->nchans++] = bdev;

			/* Now put bdev pointer at end of devpriv->devs array
			 * list.. */

			/* ergh.. ugly.. we need to realloc :(  */
			tmp = devpriv->ndevs * sizeof(bdev);
			devpriv->devs =
			    Realloc(devpriv->devs,
				    ++devpriv->ndevs * sizeof(bdev), tmp);
			if (!devpriv->devs) {
				dev_err(dev->class_dev,
					"Could not allocate memory. Out of memory?\n");
				return 0;
			}

			devpriv->devs[devpriv->ndevs - 1] = bdev;
			{
	/** Append dev:subdev to devpriv->name */
				char buf[20];
				int left =
				    MAX_BOARD_NAME - strlen(devpriv->name) - 1;
				snprintf(buf, sizeof(buf), "%d:%d ", dev->minor,
					 bdev->subdev);
				buf[sizeof(buf) - 1] = 0;
				strncat(devpriv->name, buf, left);
			}

		}
	}

	if (!devpriv->nchans) {
		dev_err(dev->class_dev, "No channels found!\n");
		return 0;
	}

	return 1;
}

static int bonding_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct comedi_bond_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	ret = alloc_private(dev, sizeof(*devpriv));
	if (ret)
		return ret;
	devpriv = dev->private;

	/*
	 * Setup our bonding from config params.. sets up our private struct..
	 */
	if (!doDevConfig(dev, it))
		return -EINVAL;

	dev->board_name = devpriv->name;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	s = dev->subdevices + 0;
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = devpriv->nchans;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = bonding_dio_insn_bits;
	s->insn_config = bonding_dio_insn_config;

	dev_info(dev->class_dev,
		"%s: %s attached, %u channels from %u devices\n",
		dev->driver->driver_name, dev->board_name,
		devpriv->nchans, devpriv->ndevs);

	return 1;
}

static void bonding_detach(struct comedi_device *dev)
{
	struct comedi_bond_private *devpriv = dev->private;
	unsigned long devs_closed = 0;

	if (devpriv) {
		while (devpriv->ndevs-- && devpriv->devs) {
			struct BondedDevice *bdev;

			bdev = devpriv->devs[devpriv->ndevs];
			if (!bdev)
				continue;
			if (!(devs_closed & (0x1 << bdev->minor))) {
				comedi_close(bdev->dev);
				devs_closed |= (0x1 << bdev->minor);
			}
			kfree(bdev);
		}
		kfree(devpriv->devs);
		devpriv->devs = NULL;
		kfree(devpriv);
		dev->private = NULL;
	}
}

static struct comedi_driver bonding_driver = {
	.driver_name	= "comedi_bond",
	.module		= THIS_MODULE,
	.attach		= bonding_attach,
	.detach		= bonding_detach,
};
module_comedi_driver(bonding_driver);

MODULE_AUTHOR("Calin A. Culianu");
MODULE_DESCRIPTION("comedi_bond: A driver for COMEDI to bond multiple COMEDI "
		   "devices together as one.  In the words of John Lennon: "
		   "'And the world will live as one...'");
MODULE_LICENSE("GPL");
