/*
 * comedi_fc.c
 * This is a place for code driver writers wish to share between
 * two or more drivers.  fc is short for frank-common.
 *
 * Author: Frank Mori Hess <fmhess@users.sourceforge.net>
 * Copyright (C) 2002 Frank Mori Hess
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
#include "../comedidev.h"

#include "comedi_fc.h"

unsigned int cfc_bytes_per_scan(struct comedi_subdevice *s)
{
	unsigned int chanlist_len = s->async->cmd.chanlist_len;
	unsigned int num_samples;
	unsigned int bits_per_sample;

	switch (s->type) {
	case COMEDI_SUBD_DI:
	case COMEDI_SUBD_DO:
	case COMEDI_SUBD_DIO:
		bits_per_sample = 8 * bytes_per_sample(s);
		num_samples = (chanlist_len + bits_per_sample - 1) /
				bits_per_sample;
		break;
	default:
		num_samples = chanlist_len;
		break;
	}
	return num_samples * bytes_per_sample(s);
}
EXPORT_SYMBOL_GPL(cfc_bytes_per_scan);

void cfc_inc_scan_progress(struct comedi_subdevice *s, unsigned int num_bytes)
{
	struct comedi_async *async = s->async;
	unsigned int scan_length = cfc_bytes_per_scan(s);

	async->scan_progress += num_bytes;
	if (async->scan_progress >= scan_length) {
		async->scan_progress %= scan_length;
		async->events |= COMEDI_CB_EOS;
	}
}
EXPORT_SYMBOL_GPL(cfc_inc_scan_progress);

/* Writes an array of data points to comedi's buffer */
unsigned int cfc_write_array_to_buffer(struct comedi_subdevice *s,
				       void *data, unsigned int num_bytes)
{
	struct comedi_async *async = s->async;
	unsigned int retval;

	if (num_bytes == 0)
		return 0;

	retval = comedi_buf_write_alloc(async, num_bytes);
	if (retval != num_bytes) {
		dev_warn(s->device->class_dev, "buffer overrun\n");
		async->events |= COMEDI_CB_OVERFLOW;
		return 0;
	}

	comedi_buf_memcpy_to(async, 0, data, num_bytes);
	comedi_buf_write_free(async, num_bytes);
	cfc_inc_scan_progress(s, num_bytes);
	async->events |= COMEDI_CB_BLOCK;

	return num_bytes;
}
EXPORT_SYMBOL_GPL(cfc_write_array_to_buffer);

unsigned int cfc_read_array_from_buffer(struct comedi_subdevice *s,
					void *data, unsigned int num_bytes)
{
	struct comedi_async *async = s->async;

	if (num_bytes == 0)
		return 0;

	num_bytes = comedi_buf_read_alloc(async, num_bytes);
	comedi_buf_memcpy_from(async, 0, data, num_bytes);
	comedi_buf_read_free(async, num_bytes);
	cfc_inc_scan_progress(s, num_bytes);
	async->events |= COMEDI_CB_BLOCK;

	return num_bytes;
}
EXPORT_SYMBOL_GPL(cfc_read_array_from_buffer);

unsigned int cfc_handle_events(struct comedi_device *dev,
			       struct comedi_subdevice *s)
{
	unsigned int events = s->async->events;

	if (events == 0)
		return events;

	if (events & (COMEDI_CB_EOA | COMEDI_CB_ERROR | COMEDI_CB_OVERFLOW))
		s->cancel(dev, s);

	comedi_event(dev, s);

	return events;
}
EXPORT_SYMBOL_GPL(cfc_handle_events);

static int __init comedi_fc_init_module(void)
{
	return 0;
}
module_init(comedi_fc_init_module);

static void __exit comedi_fc_cleanup_module(void)
{
}
module_exit(comedi_fc_cleanup_module);

MODULE_AUTHOR("Frank Mori Hess <fmhess@users.sourceforge.net>");
MODULE_DESCRIPTION("Shared functions for Comedi low-level drivers");
MODULE_LICENSE("GPL");
