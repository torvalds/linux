/*
 * comedi_bond.c
 * A Comedi driver to 'bond' or merge multiple drivers and devices as one.
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 2000 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2005 Calin A. Culianu <calin@ajvar.org>
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
 * Driver: comedi_bond
 * Description: A driver to 'bond' (merge) multiple subdevices from multiple
 * devices together as one.
 * Devices:
 * Author: ds
 * Updated: Mon, 10 Oct 00:18:25 -0500
 * Status: works
 *
 * This driver allows you to 'bond' (merge) multiple comedi subdevices
 * (coming from possibly difference boards and/or drivers) together.  For
 * example, if you had a board with 2 different DIO subdevices, and
 * another with 1 DIO subdevice, you could 'bond' them with this driver
 * so that they look like one big fat DIO subdevice.  This makes writing
 * applications slightly easier as you don't have to worry about managing
 * different subdevices in the application -- you just worry about
 * indexing one linear array of channel id's.
 *
 * Right now only DIO subdevices are supported as that's the personal itch
 * I am scratching with this driver.  If you want to add support for AI and AO
 * subdevs, go right on ahead and do so!
 *
 * Commands aren't supported -- although it would be cool if they were.
 *
 * Configuration Options:
 *   List of comedi-minors to bond.  All subdevices of the same type
 *   within each minor will be concatenated together in the order given here.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "../comedi.h"
#include "../comedilib.h"
#include "../comedidev.h"

struct bonded_device {
	struct comedi_device *dev;
	unsigned minor;
	unsigned subdev;
	unsigned nchans;
};

struct comedi_bond_private {
	char name[256];
	struct bonded_device **devs;
	unsigned ndevs;
	unsigned nchans;
};

static int bonding_dio_insn_bits(struct comedi_device *dev,
				 struct comedi_subdevice *s,
				 struct comedi_insn *insn, unsigned int *data)
{
	struct comedi_bond_private *devpriv = dev->private;
	unsigned int n_left, n_done, base_chan;
	unsigned int write_mask, data_bits;
	struct bonded_device **devs;

	write_mask = data[0];
	data_bits = data[1];
	base_chan = CR_CHAN(insn->chanspec);
	/* do a maximum of 32 channels, starting from base_chan. */
	n_left = devpriv->nchans - base_chan;
	if (n_left > 32)
		n_left = 32;

	n_done = 0;
	devs = devpriv->devs;
	do {
		struct bonded_device *bdev = *devs++;

		if (base_chan < bdev->nchans) {
			/* base channel falls within bonded device */
			unsigned int b_chans, b_mask, b_write_mask, b_data_bits;
			int ret;

			/*
			 * Get num channels to do for bonded device and set
			 * up mask and data bits for bonded device.
			 */
			b_chans = bdev->nchans - base_chan;
			if (b_chans > n_left)
				b_chans = n_left;
			b_mask = (b_chans < 32) ? ((1 << b_chans) - 1)
						: 0xffffffff;
			b_write_mask = (write_mask >> n_done) & b_mask;
			b_data_bits = (data_bits >> n_done) & b_mask;
			/* Read/Write the new digital lines. */
			ret = comedi_dio_bitfield2(bdev->dev, bdev->subdev,
						   b_write_mask, &b_data_bits,
						   base_chan);
			if (ret < 0)
				return ret;
			/* Place read bits into data[1]. */
			data[1] &= ~(b_mask << n_done);
			data[1] |= (b_data_bits & b_mask) << n_done;
			/*
			 * Set up for following bonded device (if still have
			 * channels to read/write).
			 */
			base_chan = 0;
			n_done += b_chans;
			n_left -= b_chans;
		} else {
			/* Skip bonded devices before base channel. */
			base_chan -= bdev->nchans;
		}
	} while (n_left);

	return insn->n;
}

static int bonding_dio_insn_config(struct comedi_device *dev,
				   struct comedi_subdevice *s,
				   struct comedi_insn *insn, unsigned int *data)
{
	struct comedi_bond_private *devpriv = dev->private;
	unsigned int chan = CR_CHAN(insn->chanspec);
	int ret;
	struct bonded_device *bdev;
	struct bonded_device **devs;

	/*
	 * Locate bonded subdevice and adjust channel.
	 */
	devs = devpriv->devs;
	for (bdev = *devs++; chan >= bdev->nchans; bdev = *devs++)
		chan -= bdev->nchans;

	/*
	 * The input or output configuration of each digital line is
	 * configured by a special insn_config instruction.  chanspec
	 * contains the channel to be changed, and data[0] contains the
	 * configuration instruction INSN_CONFIG_DIO_OUTPUT,
	 * INSN_CONFIG_DIO_INPUT or INSN_CONFIG_DIO_QUERY.
	 *
	 * Note that INSN_CONFIG_DIO_OUTPUT == COMEDI_OUTPUT,
	 * and INSN_CONFIG_DIO_INPUT == COMEDI_INPUT.  This is deliberate ;)
	 */
	switch (data[0]) {
	case INSN_CONFIG_DIO_OUTPUT:
	case INSN_CONFIG_DIO_INPUT:
		ret = comedi_dio_config(bdev->dev, bdev->subdev, chan, data[0]);
		break;
	case INSN_CONFIG_DIO_QUERY:
		ret = comedi_dio_get_config(bdev->dev, bdev->subdev, chan,
					    &data[1]);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	if (ret >= 0)
		ret = insn->n;
	return ret;
}

static int do_dev_config(struct comedi_device *dev, struct comedi_devconfig *it)
{
	struct comedi_bond_private *devpriv = dev->private;
	DECLARE_BITMAP(devs_opened, COMEDI_NUM_BOARD_MINORS);
	int i;

	memset(&devs_opened, 0, sizeof(devs_opened));
	devpriv->name[0] = 0;
	/*
	 * Loop through all comedi devices specified on the command-line,
	 * building our device list.
	 */
	for (i = 0; i < COMEDI_NDEVCONFOPTS && (!i || it->options[i]); ++i) {
		char file[sizeof("/dev/comediXXXXXX")];
		int minor = it->options[i];
		struct comedi_device *d;
		int sdev = -1, nchans;
		struct bonded_device *bdev;
		struct bonded_device **devs;

		if (minor < 0 || minor >= COMEDI_NUM_BOARD_MINORS) {
			dev_err(dev->class_dev,
				"Minor %d is invalid!\n", minor);
			return -EINVAL;
		}
		if (minor == dev->minor) {
			dev_err(dev->class_dev,
				"Cannot bond this driver to itself!\n");
			return -EINVAL;
		}
		if (test_and_set_bit(minor, devs_opened)) {
			dev_err(dev->class_dev,
				"Minor %d specified more than once!\n", minor);
			return -EINVAL;
		}

		snprintf(file, sizeof(file), "/dev/comedi%d", minor);
		file[sizeof(file) - 1] = 0;

		d = comedi_open(file);

		if (!d) {
			dev_err(dev->class_dev,
				"Minor %u could not be opened\n", minor);
			return -ENODEV;
		}

		/* Do DIO, as that's all we support now.. */
		while ((sdev = comedi_find_subdevice_by_type(d, COMEDI_SUBD_DIO,
							     sdev + 1)) > -1) {
			nchans = comedi_get_n_channels(d, sdev);
			if (nchans <= 0) {
				dev_err(dev->class_dev,
					"comedi_get_n_channels() returned %d on minor %u subdev %d!\n",
					nchans, minor, sdev);
				return -EINVAL;
			}
			bdev = kmalloc(sizeof(*bdev), GFP_KERNEL);
			if (!bdev)
				return -ENOMEM;

			bdev->dev = d;
			bdev->minor = minor;
			bdev->subdev = sdev;
			bdev->nchans = nchans;
			devpriv->nchans += nchans;

			/*
			 * Now put bdev pointer at end of devpriv->devs array
			 * list..
			 */

			/* ergh.. ugly.. we need to realloc :(  */
			devs = krealloc(devpriv->devs,
					(devpriv->ndevs + 1) * sizeof(*devs),
					GFP_KERNEL);
			if (!devs) {
				dev_err(dev->class_dev,
					"Could not allocate memory. Out of memory?\n");
				kfree(bdev);
				return -ENOMEM;
			}
			devpriv->devs = devs;
			devpriv->devs[devpriv->ndevs++] = bdev;
			{
				/* Append dev:subdev to devpriv->name */
				char buf[20];

				snprintf(buf, sizeof(buf), "%u:%u ",
					 bdev->minor, bdev->subdev);
				strlcat(devpriv->name, buf,
					sizeof(devpriv->name));
			}
		}
	}

	if (!devpriv->nchans) {
		dev_err(dev->class_dev, "No channels found!\n");
		return -EINVAL;
	}

	return 0;
}

static int bonding_attach(struct comedi_device *dev,
			  struct comedi_devconfig *it)
{
	struct comedi_bond_private *devpriv;
	struct comedi_subdevice *s;
	int ret;

	devpriv = comedi_alloc_devpriv(dev, sizeof(*devpriv));
	if (!devpriv)
		return -ENOMEM;

	/*
	 * Setup our bonding from config params.. sets up our private struct..
	 */
	ret = do_dev_config(dev, it);
	if (ret)
		return ret;

	dev->board_name = devpriv->name;

	ret = comedi_alloc_subdevices(dev, 1);
	if (ret)
		return ret;

	s = &dev->subdevices[0];
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

	return 0;
}

static void bonding_detach(struct comedi_device *dev)
{
	struct comedi_bond_private *devpriv = dev->private;

	if (devpriv && devpriv->devs) {
		DECLARE_BITMAP(devs_closed, COMEDI_NUM_BOARD_MINORS);

		memset(&devs_closed, 0, sizeof(devs_closed));
		while (devpriv->ndevs--) {
			struct bonded_device *bdev;

			bdev = devpriv->devs[devpriv->ndevs];
			if (!bdev)
				continue;
			if (!test_and_set_bit(bdev->minor, devs_closed))
				comedi_close(bdev->dev);
			kfree(bdev);
		}
		kfree(devpriv->devs);
		devpriv->devs = NULL;
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
MODULE_DESCRIPTION("comedi_bond: A driver for COMEDI to bond multiple COMEDI devices together as one.");
MODULE_LICENSE("GPL");
