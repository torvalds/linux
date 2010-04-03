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

/*
 * The previous block comment is used to automatically generate
 * documentation in Comedi and Comedilib.  The fields:
 *
 * Driver: the name of the driver
 * Description: a short phrase describing the driver.  Don't list boards.
 * Devices: a full list of the boards that attempt to be supported by
 *   the driver.  Format is "(manufacturer) board name [comedi name]",
 *   where comedi_name is the name that is used to configure the board.
 *   See the comment near board_name: in the struct comedi_driver structure
 *   below.  If (manufacturer) or [comedi name] is missing, the previous
 *   value is used.
 * Author: you
 * Updated: date when the _documentation_ was last updated.  Use 'date -R'
 *   to get a value for this.
 * Status: a one-word description of the status.  Valid values are:
 *   works - driver works correctly on most boards supported, and
 *     passes comedi_test.
 *   unknown - unknown.  Usually put there by ds.
 *   experimental - may not work in any particular release.  Author
 *     probably wants assistance testing it.
 *   bitrotten - driver has not been update in a long time, probably
 *     doesn't work, and probably is missing support for significant
 *     Comedi interface features.
 *   untested - author probably wrote it "blind", and is believed to
 *     work, but no confirmation.
 *
 * These headers should be followed by a blank line, and any comments
 * you wish to say about the driver.  The comment area is the place
 * to put any known bugs, limitations, unsupported features, supported
 * command triggers, whether or not commands are supported on particular
 * subdevices, etc.
 *
 * Somewhere in the comment should be information about configuration
 * options that are used with comedi_config.
 */

#include "../comedilib.h"
#include "../comedidev.h"
#include <linux/string.h>

/* The maxiumum number of channels per subdevice. */
#define MAX_CHANS 256

#define MODULE_NAME "comedi_bond"
#ifdef MODULE_LICENSE
MODULE_LICENSE("GPL");
#endif
#ifndef STR
#  define STR1(x) #x
#  define STR(x) STR1(x)
#endif

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "If true, print extra cryptic debugging output useful"
		 "only to developers.");

#define LOG_MSG(x...) printk(KERN_INFO MODULE_NAME": "x)
#define DEBUG(x...)							\
	do {								\
		if (debug)						\
			printk(KERN_DEBUG MODULE_NAME": DEBUG: "x);	\
	} while (0)
#define WARNING(x...)  printk(KERN_WARNING MODULE_NAME ": WARNING: "x)
#define ERROR(x...)  printk(KERN_ERR MODULE_NAME ": INTERNAL ERROR: "x)
MODULE_AUTHOR("Calin A. Culianu");
MODULE_DESCRIPTION(MODULE_NAME "A driver for COMEDI to bond multiple COMEDI "
		   "devices together as one.  In the words of John Lennon: "
		   "'And the world will live as one...'");

/*
 * Board descriptions for two imaginary boards.  Describing the
 * boards in this way is optional, and completely driver-dependent.
 * Some drivers use arrays such as this, other do not.
 */
struct BondingBoard {
	const char *name;
};

static const struct BondingBoard bondingBoards[] = {
	{
	 .name = MODULE_NAME,
	 },
};

/*
 * Useful for shorthand access to the particular board structure
 */
#define thisboard ((const struct BondingBoard *)dev->board_ptr)

struct BondedDevice {
	void *dev;
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
struct Private {
# define MAX_BOARD_NAME 256
	char name[MAX_BOARD_NAME];
	struct BondedDevice **devs;
	unsigned ndevs;
	struct BondedDevice *chanIdDevMap[MAX_CHANS];
	unsigned nchans;
};

/*
 * most drivers define the following macro to make it easy to
 * access the private structure.
 */
#define devpriv ((struct Private *)dev->private)

/*
 * The struct comedi_driver structure tells the Comedi core module
 * which functions to call to configure/deconfigure (attach/detach)
 * the board, and also about the kernel module that contains
 * the device code.
 */
static int bonding_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it);
static int bonding_detach(struct comedi_device *dev);
/** Build Private array of all devices.. */
static int doDevConfig(struct comedi_device *dev, struct comedi_devconfig *it);
static void doDevUnconfig(struct comedi_device *dev);
/* Ugly implementation of realloc that always copies memory around -- I'm lazy,
 * what can I say?  I like to do wasteful memcopies.. :) */
static void *Realloc(const void *ptr, size_t len, size_t old_len);

static struct comedi_driver driver_bonding = {
	.driver_name = MODULE_NAME,
	.module = THIS_MODULE,
	.attach = bonding_attach,
	.detach = bonding_detach,
	/* It is not necessary to implement the following members if you are
	 * writing a driver for a ISA PnP or PCI card */
	/* Most drivers will support multiple types of boards by
	 * having an array of board structures.  These were defined
	 * in skel_boards[] above.  Note that the element 'name'
	 * was first in the structure -- Comedi uses this fact to
	 * extract the name of the board without knowing any details
	 * about the structure except for its length.
	 * When a device is attached (by comedi_config), the name
	 * of the device is given to Comedi, and Comedi tries to
	 * match it by going through the list of board names.  If
	 * there is a match, the address of the pointer is put
	 * into dev->board_ptr and driver->attach() is called.
	 *
	 * Note that these are not necessary if you can determine
	 * the type of board in software.  ISA PnP, PCI, and PCMCIA
	 * devices are such boards.
	 */
	.board_name = &bondingBoards[0].name,
	.offset = sizeof(struct BondingBoard),
	.num_names = ARRAY_SIZE(bondingBoards),
};

static int bonding_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data);
static int bonding_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn,
				   unsigned int *data);

/*
 * Attach is called by the Comedi core to configure the driver
 * for a particular board.  If you specified a board_name array
 * in the driver structure, dev->board_ptr contains that
 * address.
 */
static int bonding_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct comedi_subdevice *s;

	LOG_MSG("comedi%d\n", dev->minor);

	/*
	 * Allocate the private structure area.  alloc_private() is a
	 * convenient macro defined in comedidev.h.
	 */
	if (alloc_private(dev, sizeof(struct Private)) < 0)
		return -ENOMEM;

	/*
	 * Setup our bonding from config params.. sets up our Private struct..
	 */
	if (!doDevConfig(dev, it))
		return -EINVAL;

	/*
	 * Initialize dev->board_name.  Note that we can use the "thisboard"
	 * macro now, since we just initialized it in the last line.
	 */
	dev->board_name = devpriv->name;

	/*
	 * Allocate the subdevice structures.  alloc_subdevice() is a
	 * convenient macro defined in comedidev.h.
	 */
	if (alloc_subdevices(dev, 1) < 0)
		return -ENOMEM;

	s = dev->subdevices + 0;
	s->type = COMEDI_SUBD_DIO;
	s->subdev_flags = SDF_READABLE | SDF_WRITABLE;
	s->n_chan = devpriv->nchans;
	s->maxdata = 1;
	s->range_table = &range_digital;
	s->insn_bits = bonding_dio_insn_bits;
	s->insn_config = bonding_dio_insn_config;

	LOG_MSG("attached with %u DIO channels coming from %u different "
		"subdevices all bonded together.  "
		"John Lennon would be proud!\n",
		devpriv->nchans, devpriv->ndevs);

	return 1;
}

/*
 * _detach is called to deconfigure a device.  It should deallocate
 * resources.
 * This function is also called when _attach() fails, so it should be
 * careful not to release resources that were not necessarily
 * allocated by _attach().  dev->private and dev->subdevices are
 * deallocated automatically by the core.
 */
static int bonding_detach(struct comedi_device *dev)
{
	LOG_MSG("comedi%d: remove\n", dev->minor);
	doDevUnconfig(dev);
	return 0;
}

/* DIO devices are slightly special.  Although it is possible to
 * implement the insn_read/insn_write interface, it is much more
 * useful to applications if you implement the insn_bits interface.
 * This allows packed reading/writing of the DIO channels.  The
 * comedi core can convert between insn_bits and insn_read/write */
static int bonding_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
#define LSAMPL_BITS (sizeof(unsigned int)*8)
	unsigned nchans = LSAMPL_BITS, num_done = 0, i;
	if (insn->n != 2)
		return -EINVAL;

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
	int i;
	void *devs_opened[COMEDI_NUM_BOARD_MINORS];

	memset(devs_opened, 0, sizeof(devs_opened));
	devpriv->name[0] = 0;;
	/* Loop through all comedi devices specified on the command-line,
	   building our device list */
	for (i = 0; i < COMEDI_NDEVCONFOPTS && (!i || it->options[i]); ++i) {
		char file[] = "/dev/comediXXXXXX";
		int minor = it->options[i];
		void *d;
		int sdev = -1, nchans, tmp;
		struct BondedDevice *bdev = NULL;

		if (minor < 0 || minor >= COMEDI_NUM_BOARD_MINORS) {
			ERROR("Minor %d is invalid!\n", minor);
			return 0;
		}
		if (minor == dev->minor) {
			ERROR("Cannot bond this driver to itself!\n");
			return 0;
		}
		if (devs_opened[minor]) {
			ERROR("Minor %d specified more than once!\n", minor);
			return 0;
		}

		snprintf(file, sizeof(file), "/dev/comedi%u", minor);
		file[sizeof(file) - 1] = 0;

		d = devs_opened[minor] = comedi_open(file);

		if (!d) {
			ERROR("Minor %u could not be opened\n", minor);
			return 0;
		}

		/* Do DIO, as that's all we support now.. */
		while ((sdev = comedi_find_subdevice_by_type(d, COMEDI_SUBD_DIO,
							     sdev + 1)) > -1) {
			nchans = comedi_get_n_channels(d, sdev);
			if (nchans <= 0) {
				ERROR("comedi_get_n_channels() returned %d "
				      "on minor %u subdev %d!\n",
				      nchans, minor, sdev);
				return 0;
			}
			bdev = kmalloc(sizeof(*bdev), GFP_KERNEL);
			if (!bdev) {
				ERROR("Out of memory.\n");
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
				ERROR("Could not allocate memory. "
				      "Out of memory?");
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
		ERROR("No channels found!\n");
		return 0;
	}

	return 1;
}

static void doDevUnconfig(struct comedi_device *dev)
{
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

static int __init init(void)
{
	return comedi_driver_register(&driver_bonding);
}

static void __exit cleanup(void)
{
	comedi_driver_unregister(&driver_bonding);
}

module_init(init);
module_exit(cleanup);
