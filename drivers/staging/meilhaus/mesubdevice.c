/**
 * @file mesubdevice.c
 *
 * @brief Subdevice base class implemention.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 */

/*
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef __KERNEL__
#  define __KERNEL__
#endif

#include <linux/slab.h>

#include "medefines.h"
#include "meerror.h"

#include "medebug.h"
#include "mesubdevice.h"

static int me_subdevice_io_irq_start(struct me_subdevice *subdevice,
				     struct file *filep,
				     int channel,
				     int irq_source,
				     int irq_edge, int irq_arg, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_io_irq_wait(struct me_subdevice *subdevice,
				    struct file *filep,
				    int channel,
				    int *irq_count,
				    int *value, int time_out, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_io_irq_stop(struct me_subdevice *subdevice,
				    struct file *filep, int channel, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_io_reset_subdevice(struct me_subdevice *subdevice,
					   struct file *filep, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_io_single_config(struct me_subdevice *subdevice,
					 struct file *filep,
					 int channel,
					 int single_config,
					 int ref,
					 int trig_chan,
					 int trig_type,
					 int trig_edge, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_io_single_read(struct me_subdevice *subdevice,
				       struct file *filep,
				       int channel,
				       int *value, int time_out, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_io_single_write(struct me_subdevice *subdevice,
					struct file *filep,
					int channel,
					int value, int time_out, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_io_stream_config(struct me_subdevice *subdevice,
					 struct file *filep,
					 meIOStreamConfig_t * config_list,
					 int count,
					 meIOStreamTrigger_t * trigger,
					 int fifo_irq_threshold, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_io_stream_new_values(struct me_subdevice *subdevice,
					     struct file *filep,
					     int time_out,
					     int *count, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_io_stream_read(struct me_subdevice *subdevice,
				       struct file *filep,
				       int read_mode,
				       int *values, int *count, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_io_stream_start(struct me_subdevice *subdevice,
					struct file *filep,
					int start_mode, int time_out, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_io_stream_status(struct me_subdevice *subdevice,
					 struct file *filep,
					 int wait,
					 int *status, int *count, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_io_stream_stop(struct me_subdevice *subdevice,
				       struct file *filep,
				       int stop_mode, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_io_stream_write(struct me_subdevice *subdevice,
					struct file *filep,
					int write_mode,
					int *values, int *count, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_lock_subdevice(me_subdevice_t * subdevice,
				       struct file *filep, int lock, int flags)
{
	PDEBUG("executed.\n");
	return me_slock_lock(&subdevice->lock, filep, lock);
}

static int me_subdevice_query_number_channels(struct me_subdevice *subdevice,
					      int *number)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_query_number_ranges(struct me_subdevice *subdevice,
					    int unit, int *count)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_query_range_by_min_max(struct me_subdevice *subdevice,
					       int unit,
					       int *min,
					       int *max,
					       int *maxdata, int *range)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_query_range_info(struct me_subdevice *subdevice,
					 int range,
					 int *unit,
					 int *min, int *max, int *maxdata)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_query_subdevice_type(struct me_subdevice *subdevice,
					     int *type, int *subtype)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_query_subdevice_caps(struct me_subdevice *subdevice,
					     int *caps)
{
	PDEBUG("executed.\n");
	*caps = 0;
	return ME_ERRNO_SUCCESS;
}

static int me_subdevice_query_subdevice_caps_args(struct me_subdevice
						  *subdevice, int cap,
						  int *args, int count)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_query_timer(struct me_subdevice *subdevice,
				    int timer,
				    int *base_frequency,
				    long long *min_ticks, long long *max_ticks)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int me_subdevice_config_load(struct me_subdevice *subdevice,
				    me_cfg_device_entry_t * config)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_SUCCESS;
}

static void me_subdevice_destructor(struct me_subdevice *subdevice)
{
	PDEBUG("executed.\n");
	me_subdevice_deinit(subdevice);
	kfree(subdevice);
}

int me_subdevice_init(me_subdevice_t * subdevice)
{
	int err;

	PDEBUG("executed.\n");

	/* Init list head */
	INIT_LIST_HEAD(&subdevice->list);

	/* Initialize the subdevice lock instance */

	err = me_slock_init(&subdevice->lock);

	if (err) {
		PERROR("Cannot initialize subdevice lock instance.\n");
		return 1;
	}

	/* Subdevice base class methods */
	subdevice->me_subdevice_io_irq_start = me_subdevice_io_irq_start;
	subdevice->me_subdevice_io_irq_wait = me_subdevice_io_irq_wait;
	subdevice->me_subdevice_io_irq_stop = me_subdevice_io_irq_stop;
	subdevice->me_subdevice_io_reset_subdevice =
	    me_subdevice_io_reset_subdevice;
	subdevice->me_subdevice_io_single_config =
	    me_subdevice_io_single_config;
	subdevice->me_subdevice_io_single_read = me_subdevice_io_single_read;
	subdevice->me_subdevice_io_single_write = me_subdevice_io_single_write;
	subdevice->me_subdevice_io_stream_config =
	    me_subdevice_io_stream_config;
	subdevice->me_subdevice_io_stream_new_values =
	    me_subdevice_io_stream_new_values;
	subdevice->me_subdevice_io_stream_read = me_subdevice_io_stream_read;
	subdevice->me_subdevice_io_stream_start = me_subdevice_io_stream_start;
	subdevice->me_subdevice_io_stream_status =
	    me_subdevice_io_stream_status;
	subdevice->me_subdevice_io_stream_stop = me_subdevice_io_stream_stop;
	subdevice->me_subdevice_io_stream_write = me_subdevice_io_stream_write;
	subdevice->me_subdevice_lock_subdevice = me_subdevice_lock_subdevice;
	subdevice->me_subdevice_query_number_channels =
	    me_subdevice_query_number_channels;
	subdevice->me_subdevice_query_number_ranges =
	    me_subdevice_query_number_ranges;
	subdevice->me_subdevice_query_range_by_min_max =
	    me_subdevice_query_range_by_min_max;
	subdevice->me_subdevice_query_range_info =
	    me_subdevice_query_range_info;
	subdevice->me_subdevice_query_subdevice_type =
	    me_subdevice_query_subdevice_type;
	subdevice->me_subdevice_query_subdevice_caps =
	    me_subdevice_query_subdevice_caps;
	subdevice->me_subdevice_query_subdevice_caps_args =
	    me_subdevice_query_subdevice_caps_args;
	subdevice->me_subdevice_query_timer = me_subdevice_query_timer;
	subdevice->me_subdevice_config_load = me_subdevice_config_load;
	subdevice->me_subdevice_destructor = me_subdevice_destructor;

	return 0;
}

void me_subdevice_deinit(me_subdevice_t * subdevice)
{
	PDEBUG("executed.\n");
	me_subdevice_io_reset_subdevice(subdevice, NULL,
					ME_IO_RESET_SUBDEVICE_NO_FLAGS);
	me_slock_deinit(&subdevice->lock);
}
