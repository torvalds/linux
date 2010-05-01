/*
    linux/include/comedilib.h
    header file for kcomedilib

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998-2001 David A. Schleef <ds@schleef.org>

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

#ifndef _LINUX_COMEDILIB_H
#define _LINUX_COMEDILIB_H

void *comedi_open(const char *path);
int comedi_close(void *dev);
int comedi_dio_config(void *dev, unsigned int subdev, unsigned int chan,
		      unsigned int io);
int comedi_dio_bitfield(void *dev, unsigned int subdev, unsigned int mask,
			unsigned int *bits);
int comedi_find_subdevice_by_type(void *dev, int type, unsigned int subd);
int comedi_get_n_channels(void *dev, unsigned int subdevice);

/* internal to kcomedilb */
int comedi_do_insn(void *dev, struct comedi_insn *insn);

#endif
