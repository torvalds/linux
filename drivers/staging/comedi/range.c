/*
    module/range.c
    comedi routines for voltage ranges

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1997-8 David A. Schleef <ds@schleef.org>

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

#include <linux/uaccess.h>
#include "comedidev.h"
#include "comedi_internal.h"

const struct comedi_lrange range_bipolar10 = { 1, {BIP_RANGE(10)} };
EXPORT_SYMBOL_GPL(range_bipolar10);
const struct comedi_lrange range_bipolar5 = { 1, {BIP_RANGE(5)} };
EXPORT_SYMBOL_GPL(range_bipolar5);
const struct comedi_lrange range_bipolar2_5 = { 1, {BIP_RANGE(2.5)} };
EXPORT_SYMBOL_GPL(range_bipolar2_5);
const struct comedi_lrange range_unipolar10 = { 1, {UNI_RANGE(10)} };
EXPORT_SYMBOL_GPL(range_unipolar10);
const struct comedi_lrange range_unipolar5 = { 1, {UNI_RANGE(5)} };
EXPORT_SYMBOL_GPL(range_unipolar5);
const struct comedi_lrange range_unipolar2_5 = { 1, {UNI_RANGE(2.5)} };
EXPORT_SYMBOL_GPL(range_unipolar2_5);
const struct comedi_lrange range_0_20mA = { 1, {RANGE_mA(0, 20)} };
EXPORT_SYMBOL_GPL(range_0_20mA);
const struct comedi_lrange range_4_20mA = { 1, {RANGE_mA(4, 20)} };
EXPORT_SYMBOL_GPL(range_4_20mA);
const struct comedi_lrange range_0_32mA = { 1, {RANGE_mA(0, 32)} };
EXPORT_SYMBOL_GPL(range_0_32mA);
const struct comedi_lrange range_unknown = { 1, {{0, 1000000, UNIT_none} } };
EXPORT_SYMBOL_GPL(range_unknown);

/*
	COMEDI_RANGEINFO
	range information ioctl

	arg:
		pointer to rangeinfo structure

	reads:
		range info structure

	writes:
		n struct comedi_krange structures to rangeinfo->range_ptr
*/
int do_rangeinfo_ioctl(struct comedi_device *dev,
		       struct comedi_rangeinfo __user *arg)
{
	struct comedi_rangeinfo it;
	int subd, chan;
	const struct comedi_lrange *lr;
	struct comedi_subdevice *s;

	if (copy_from_user(&it, arg, sizeof(struct comedi_rangeinfo)))
		return -EFAULT;
	subd = (it.range_type >> 24) & 0xf;
	chan = (it.range_type >> 16) & 0xff;

	if (!dev->attached)
		return -EINVAL;
	if (subd >= dev->n_subdevices)
		return -EINVAL;
	s = &dev->subdevices[subd];
	if (s->range_table) {
		lr = s->range_table;
	} else if (s->range_table_list) {
		if (chan >= s->n_chan)
			return -EINVAL;
		lr = s->range_table_list[chan];
	} else {
		return -EINVAL;
	}

	if (RANGE_LENGTH(it.range_type) != lr->length) {
		DPRINTK("wrong length %d should be %d (0x%08x)\n",
			RANGE_LENGTH(it.range_type), lr->length, it.range_type);
		return -EINVAL;
	}

	if (copy_to_user(it.range_ptr, lr->range,
			 sizeof(struct comedi_krange) * lr->length))
		return -EFAULT;

	return 0;
}

static int aref_invalid(struct comedi_subdevice *s, unsigned int chanspec)
{
	unsigned int aref;

	/*  disable reporting invalid arefs... maybe someday */
	return 0;

	aref = CR_AREF(chanspec);
	switch (aref) {
	case AREF_DIFF:
		if (s->subdev_flags & SDF_DIFF)
			return 0;
		break;
	case AREF_COMMON:
		if (s->subdev_flags & SDF_COMMON)
			return 0;
		break;
	case AREF_GROUND:
		if (s->subdev_flags & SDF_GROUND)
			return 0;
		break;
	case AREF_OTHER:
		if (s->subdev_flags & SDF_OTHER)
			return 0;
		break;
	default:
		break;
	}
	DPRINTK("subdevice does not support aref %i", aref);
	return 1;
}

/*
   This function checks each element in a channel/gain list to make
   make sure it is valid.
*/
int comedi_check_chanlist(struct comedi_subdevice *s, int n,
			  unsigned int *chanlist)
{
	struct comedi_device *dev = s->device;
	int i;
	int chan;

	if (s->range_table) {
		for (i = 0; i < n; i++)
			if (CR_CHAN(chanlist[i]) >= s->n_chan ||
			    CR_RANGE(chanlist[i]) >= s->range_table->length
			    || aref_invalid(s, chanlist[i])) {
				dev_warn(dev->class_dev,
					 "bad chanlist[%d]=0x%08x in_chan=%d range length=%d\n",
					 i, chanlist[i], s->n_chan,
					 s->range_table->length);
				return -EINVAL;
			}
	} else if (s->range_table_list) {
		for (i = 0; i < n; i++) {
			chan = CR_CHAN(chanlist[i]);
			if (chan >= s->n_chan ||
			    CR_RANGE(chanlist[i]) >=
			    s->range_table_list[chan]->length
			    || aref_invalid(s, chanlist[i])) {
				dev_warn(dev->class_dev,
					 "bad chanlist[%d]=0x%08x\n",
					 i, chanlist[i]);
				return -EINVAL;
			}
		}
	} else {
		dev_err(dev->class_dev, "(bug) no range type list!\n");
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(comedi_check_chanlist);
