// SPDX-License-Identifier: GPL-2.0+
/*
 * kcomedilib/kcomedilib.c
 * a comedlib interface for kernel modules
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1997-2000 David A. Schleef <ds@schleef.org>
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

#include <linux/module.h>

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/io.h>

#include "../comedi.h"
#include "../comedilib.h"
#include "../comedidev.h"

MODULE_AUTHOR("David Schleef <ds@schleef.org>");
MODULE_DESCRIPTION("Comedi kernel library");
MODULE_LICENSE("GPL");

struct comedi_device *comedi_open(const char *filename)
{
	struct comedi_device *dev, *retval = NULL;
	unsigned int minor;

	if (strncmp(filename, "/dev/comedi", 11) != 0)
		return NULL;

	if (kstrtouint(filename + 11, 0, &minor))
		return NULL;

	if (minor >= COMEDI_NUM_BOARD_MINORS)
		return NULL;

	dev = comedi_dev_get_from_minor(minor);
	if (!dev)
		return NULL;

	down_read(&dev->attach_lock);
	if (dev->attached)
		retval = dev;
	else
		retval = NULL;
	up_read(&dev->attach_lock);

	if (!retval)
		comedi_dev_put(dev);

	return retval;
}
EXPORT_SYMBOL_GPL(comedi_open);

int comedi_close(struct comedi_device *dev)
{
	comedi_dev_put(dev);
	return 0;
}
EXPORT_SYMBOL_GPL(comedi_close);

static int comedi_do_insn(struct comedi_device *dev,
			  struct comedi_insn *insn,
			  unsigned int *data)
{
	struct comedi_subdevice *s;
	int ret;

	mutex_lock(&dev->mutex);

	if (!dev->attached) {
		ret = -EINVAL;
		goto error;
	}

	/* a subdevice instruction */
	if (insn->subdev >= dev->n_subdevices) {
		ret = -EINVAL;
		goto error;
	}
	s = &dev->subdevices[insn->subdev];

	if (s->type == COMEDI_SUBD_UNUSED) {
		dev_err(dev->class_dev,
			"%d not usable subdevice\n", insn->subdev);
		ret = -EIO;
		goto error;
	}

	/* XXX check lock */

	ret = comedi_check_chanlist(s, 1, &insn->chanspec);
	if (ret < 0) {
		dev_err(dev->class_dev, "bad chanspec\n");
		ret = -EINVAL;
		goto error;
	}

	if (s->busy) {
		ret = -EBUSY;
		goto error;
	}
	s->busy = dev;

	switch (insn->insn) {
	case INSN_BITS:
		ret = s->insn_bits(dev, s, insn, data);
		break;
	case INSN_CONFIG:
		/* XXX should check instruction length */
		ret = s->insn_config(dev, s, insn, data);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	s->busy = NULL;
error:

	mutex_unlock(&dev->mutex);
	return ret;
}

int comedi_dio_get_config(struct comedi_device *dev, unsigned int subdev,
			  unsigned int chan, unsigned int *io)
{
	struct comedi_insn insn;
	unsigned int data[2];
	int ret;

	memset(&insn, 0, sizeof(insn));
	insn.insn = INSN_CONFIG;
	insn.n = 2;
	insn.subdev = subdev;
	insn.chanspec = CR_PACK(chan, 0, 0);
	data[0] = INSN_CONFIG_DIO_QUERY;
	data[1] = 0;
	ret = comedi_do_insn(dev, &insn, data);
	if (ret >= 0)
		*io = data[1];
	return ret;
}
EXPORT_SYMBOL_GPL(comedi_dio_get_config);

int comedi_dio_config(struct comedi_device *dev, unsigned int subdev,
		      unsigned int chan, unsigned int io)
{
	struct comedi_insn insn;

	memset(&insn, 0, sizeof(insn));
	insn.insn = INSN_CONFIG;
	insn.n = 1;
	insn.subdev = subdev;
	insn.chanspec = CR_PACK(chan, 0, 0);

	return comedi_do_insn(dev, &insn, &io);
}
EXPORT_SYMBOL_GPL(comedi_dio_config);

int comedi_dio_bitfield2(struct comedi_device *dev, unsigned int subdev,
			 unsigned int mask, unsigned int *bits,
			 unsigned int base_channel)
{
	struct comedi_insn insn;
	unsigned int data[2];
	unsigned int n_chan;
	unsigned int shift;
	int ret;

	base_channel = CR_CHAN(base_channel);
	n_chan = comedi_get_n_channels(dev, subdev);
	if (base_channel >= n_chan)
		return -EINVAL;

	memset(&insn, 0, sizeof(insn));
	insn.insn = INSN_BITS;
	insn.chanspec = base_channel;
	insn.n = 2;
	insn.subdev = subdev;

	data[0] = mask;
	data[1] = *bits;

	/*
	 * Most drivers ignore the base channel in insn->chanspec.
	 * Fix this here if the subdevice has <= 32 channels.
	 */
	if (n_chan <= 32) {
		shift = base_channel;
		if (shift) {
			insn.chanspec = 0;
			data[0] <<= shift;
			data[1] <<= shift;
		}
	} else {
		shift = 0;
	}

	ret = comedi_do_insn(dev, &insn, data);
	*bits = data[1] >> shift;
	return ret;
}
EXPORT_SYMBOL_GPL(comedi_dio_bitfield2);

int comedi_find_subdevice_by_type(struct comedi_device *dev, int type,
				  unsigned int subd)
{
	struct comedi_subdevice *s;
	int ret = -ENODEV;

	down_read(&dev->attach_lock);
	if (dev->attached)
		for (; subd < dev->n_subdevices; subd++) {
			s = &dev->subdevices[subd];
			if (s->type == type) {
				ret = subd;
				break;
			}
		}
	up_read(&dev->attach_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(comedi_find_subdevice_by_type);

int comedi_get_n_channels(struct comedi_device *dev, unsigned int subdevice)
{
	int n;

	down_read(&dev->attach_lock);
	if (!dev->attached || subdevice >= dev->n_subdevices)
		n = 0;
	else
		n = dev->subdevices[subdevice].n_chan;
	up_read(&dev->attach_lock);

	return n;
}
EXPORT_SYMBOL_GPL(comedi_get_n_channels);

static int __init kcomedilib_module_init(void)
{
	return 0;
}

static void __exit kcomedilib_module_exit(void)
{
}

module_init(kcomedilib_module_init);
module_exit(kcomedilib_module_exit);
