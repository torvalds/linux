/*
    module/8255.h
    Header file for 8255

    COMEDI - Linux Control and Measurement Device Interface
    Copyright (C) 1998 David A. Schleef <ds@schleef.org>

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

#ifndef _8255_H
#define _8255_H

#include "../comedidev.h"

#if defined(CONFIG_COMEDI_8255) || defined(CONFIG_COMEDI_8255_MODULE)

int subdev_8255_init(struct comedi_device *dev, struct comedi_subdevice *s,
		     int (*cb) (int, int, int, unsigned long),
		     unsigned long arg);
int subdev_8255_init_irq(struct comedi_device *dev, struct comedi_subdevice *s,
			 int (*cb) (int, int, int, unsigned long),
			 unsigned long arg);
void subdev_8255_cleanup(struct comedi_device *dev, struct comedi_subdevice *s);
void subdev_8255_interrupt(struct comedi_device *dev,
			   struct comedi_subdevice *s);

#else

static inline int subdev_8255_init(struct comedi_device *dev,
				   struct comedi_subdevice *s, void *x,
				   unsigned long y)
{
	printk("8255 support not configured -- disabling subdevice\n");

	s->type = COMEDI_SUBD_UNUSED;

	return 0;
}

static inline void subdev_8255_cleanup(struct comedi_device *dev,
				       struct comedi_subdevice *s)
{
}

#endif

#endif
