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

#include "comedidev.h"
#include <asm/uaccess.h>

const comedi_lrange range_bipolar10 = { 1, {BIP_RANGE(10)} };
const comedi_lrange range_bipolar5 = { 1, {BIP_RANGE(5)} };
const comedi_lrange range_bipolar2_5 = { 1, {BIP_RANGE(2.5)} };
const comedi_lrange range_unipolar10 = { 1, {UNI_RANGE(10)} };
const comedi_lrange range_unipolar5 = { 1, {UNI_RANGE(5)} };
const comedi_lrange range_unknown = { 1, {{0, 1000000, UNIT_none}} };

/*
   	COMEDI_RANGEINFO
	range information ioctl

	arg:
		pointer to rangeinfo structure

	reads:
		range info structure

	writes:
		n comedi_krange structures to rangeinfo->range_ptr
*/
int do_rangeinfo_ioctl(comedi_device * dev, comedi_rangeinfo * arg)
{
	comedi_rangeinfo it;
	int subd, chan;
	const comedi_lrange *lr;
	comedi_subdevice *s;

	if (copy_from_user(&it, arg, sizeof(comedi_rangeinfo)))
		return -EFAULT;
	subd = (it.range_type >> 24) & 0xf;
	chan = (it.range_type >> 16) & 0xff;

	if (!dev->attached)
		return -EINVAL;
	if (subd >= dev->n_subdevices)
		return -EINVAL;
	s = dev->subdevices + subd;
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
			sizeof(comedi_krange) * lr->length))
		return -EFAULT;

	return 0;
}

static int aref_invalid(comedi_subdevice * s, unsigned int chanspec)
{
	unsigned int aref;

	// disable reporting invalid arefs... maybe someday
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
int check_chanlist(comedi_subdevice * s, int n, unsigned int *chanlist)
{
	int i;
	int chan;

	if (s->range_table) {
		for (i = 0; i < n; i++)
			if (CR_CHAN(chanlist[i]) >= s->n_chan ||
				CR_RANGE(chanlist[i]) >= s->range_table->length
				|| aref_invalid(s, chanlist[i])) {
				rt_printk
					("bad chanlist[%d]=0x%08x n_chan=%d range length=%d\n",
					i, chanlist[i], s->n_chan,
					s->range_table->length);
#if 0
				for (i = 0; i < n; i++) {
					printk("[%d]=0x%08x\n", i, chanlist[i]);
				}
#endif
				return -EINVAL;
			}
	} else if (s->range_table_list) {
		for (i = 0; i < n; i++) {
			chan = CR_CHAN(chanlist[i]);
			if (chan >= s->n_chan ||
				CR_RANGE(chanlist[i]) >=
				s->range_table_list[chan]->length
				|| aref_invalid(s, chanlist[i])) {
				rt_printk("bad chanlist[%d]=0x%08x\n", i,
					chanlist[i]);
				return -EINVAL;
			}
		}
	} else {
		rt_printk("comedi: (bug) no range type list!\n");
		return -EINVAL;
	}
	return 0;
}
