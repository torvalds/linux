// SPDX-License-Identifier: GPL-2.0+
/*
 * comedilib.h
 * Header file for kcomedilib
 *
 * COMEDI - Linux Control and Measurement Device Interface
 * Copyright (C) 1998-2001 David A. Schleef <ds@schleef.org>
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

#ifndef _LINUX_COMEDILIB_H
#define _LINUX_COMEDILIB_H

struct comedi_device *comedi_open(const char *path);
int comedi_close(struct comedi_device *dev);
int comedi_dio_get_config(struct comedi_device *dev, unsigned int subdev,
			  unsigned int chan, unsigned int *io);
int comedi_dio_config(struct comedi_device *dev, unsigned int subdev,
		      unsigned int chan, unsigned int io);
int comedi_dio_bitfield2(struct comedi_device *dev, unsigned int subdev,
			 unsigned int mask, unsigned int *bits,
			 unsigned int base_channel);
int comedi_find_subdevice_by_type(struct comedi_device *dev, int type,
				  unsigned int subd);
int comedi_get_n_channels(struct comedi_device *dev, unsigned int subdevice);

#endif
