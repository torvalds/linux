/*
    kcomedilib/data.c
    implements comedi_data_*() functions

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 2000 David A. Schleef <ds@schleef.org>

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

#include "../comedi.h"
#include "../comedilib.h"
#include "../comedidev.h"

#include <linux/string.h>
#include <linux/delay.h>

int comedi_data_write(void *dev, unsigned int subdev, unsigned int chan,
		      unsigned int range, unsigned int aref, unsigned int data)
{
	struct comedi_insn insn;

	memset(&insn, 0, sizeof(insn));
	insn.insn = INSN_WRITE;
	insn.n = 1;
	insn.data = &data;
	insn.subdev = subdev;
	insn.chanspec = CR_PACK(chan, range, aref);

	return comedi_do_insn(dev, &insn);
}

int comedi_data_read(void *dev, unsigned int subdev, unsigned int chan,
		     unsigned int range, unsigned int aref, unsigned int *data)
{
	struct comedi_insn insn;

	memset(&insn, 0, sizeof(insn));
	insn.insn = INSN_READ;
	insn.n = 1;
	insn.data = data;
	insn.subdev = subdev;
	insn.chanspec = CR_PACK(chan, range, aref);

	return comedi_do_insn(dev, &insn);
}

int comedi_data_read_hint(void *dev, unsigned int subdev,
			  unsigned int chan, unsigned int range,
			  unsigned int aref)
{
	struct comedi_insn insn;
	unsigned int dummy_data;

	memset(&insn, 0, sizeof(insn));
	insn.insn = INSN_READ;
	insn.n = 0;
	insn.data = &dummy_data;
	insn.subdev = subdev;
	insn.chanspec = CR_PACK(chan, range, aref);

	return comedi_do_insn(dev, &insn);
}

int comedi_data_read_delayed(void *dev, unsigned int subdev,
			     unsigned int chan, unsigned int range,
			     unsigned int aref, unsigned int *data,
			     unsigned int nano_sec)
{
	int retval;

	retval = comedi_data_read_hint(dev, subdev, chan, range, aref);
	if (retval < 0)
		return retval;

	udelay((nano_sec + 999) / 1000);

	return comedi_data_read(dev, subdev, chan, range, aref, data);
}
