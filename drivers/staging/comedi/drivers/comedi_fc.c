/*
    comedi/drivers/comedi_fc.c

    This is a place for code driver writers wish to share between
    two or more drivers.  fc is short
    for frank-common.

    Author:  Frank Mori Hess <fmhess@users.sourceforge.net>
    Copyright (C) 2002 Frank Mori Hess

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

************************************************************************/

#include "../comedidev.h"

#include "comedi_fc.h"

static void increment_scan_progress(struct comedi_subdevice *subd,
				    unsigned int num_bytes)
{
	struct comedi_async *async = subd->async;
	unsigned int scan_length = cfc_bytes_per_scan(subd);

	async->scan_progress += num_bytes;
	if (async->scan_progress >= scan_length) {
		async->scan_progress %= scan_length;
		async->events |= COMEDI_CB_EOS;
	}
}

/* Writes an array of data points to comedi's buffer */
unsigned int cfc_write_array_to_buffer(struct comedi_subdevice *subd,
				       void *data, unsigned int num_bytes)
{
	struct comedi_async *async = subd->async;
	unsigned int retval;

	if (num_bytes == 0)
		return 0;

	retval = comedi_buf_write_alloc(async, num_bytes);
	if (retval != num_bytes) {
		dev_warn(subd->device->class_dev, "comedi: buffer overrun\n");
		async->events |= COMEDI_CB_OVERFLOW;
		return 0;
	}

	comedi_buf_memcpy_to(async, 0, data, num_bytes);
	comedi_buf_write_free(async, num_bytes);
	increment_scan_progress(subd, num_bytes);
	async->events |= COMEDI_CB_BLOCK;

	return num_bytes;
}
EXPORT_SYMBOL(cfc_write_array_to_buffer);

unsigned int cfc_read_array_from_buffer(struct comedi_subdevice *subd,
					void *data, unsigned int num_bytes)
{
	struct comedi_async *async = subd->async;

	if (num_bytes == 0)
		return 0;

	num_bytes = comedi_buf_read_alloc(async, num_bytes);
	comedi_buf_memcpy_from(async, 0, data, num_bytes);
	comedi_buf_read_free(async, num_bytes);
	increment_scan_progress(subd, num_bytes);
	async->events |= COMEDI_CB_BLOCK;

	return num_bytes;
}
EXPORT_SYMBOL(cfc_read_array_from_buffer);

unsigned int cfc_handle_events(struct comedi_device *dev,
			       struct comedi_subdevice *subd)
{
	unsigned int events = subd->async->events;

	if (events == 0)
		return events;

	if (events & (COMEDI_CB_EOA | COMEDI_CB_ERROR | COMEDI_CB_OVERFLOW))
		subd->cancel(dev, subd);

	comedi_event(dev, subd);

	return events;
}
EXPORT_SYMBOL(cfc_handle_events);

MODULE_AUTHOR("Frank Mori Hess <fmhess@users.sourceforge.net>");
MODULE_DESCRIPTION("Shared functions for Comedi low-level drivers");
MODULE_LICENSE("GPL");

static int __init comedi_fc_init_module(void)
{
	return 0;
}

static void __exit comedi_fc_cleanup_module(void)
{
}

module_init(comedi_fc_init_module);
module_exit(comedi_fc_cleanup_module);
