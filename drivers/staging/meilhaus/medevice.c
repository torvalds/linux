/**
 * @file medevice.c
 *
 * @brief Meilhaus device base class.
 * @note Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 * @author Guenter Gebhardt
 * @author Krzysztof Gantzke	(k.gantzke@meilhaus.de)
 */

/*
 * Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
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

#include "mecommon.h"
#include "meinternal.h"
#include "medefines.h"
#include "meerror.h"

#include "medebug.h"
#include "medevice.h"

#ifndef __KERNEL__
#  define __KERNEL__
#endif

static int me_device_io_irq_start(struct me_device *device,
				  struct file *filep,
				  int subdevice,
				  int channel,
				  int irq_source,
				  int irq_edge, int irq_arg, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.
	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_irq_start(s,
						   filep,
						   channel,
						   irq_source,
						   irq_edge, irq_arg, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_irq_wait(struct me_device *device,
				 struct file *filep,
				 int subdevice,
				 int channel,
				 int *irq_count,
				 int *value, int time_out, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.
	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_irq_wait(s,
						  filep,
						  channel,
						  irq_count,
						  value, time_out, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_irq_stop(struct me_device *device,
				 struct file *filep,
				 int subdevice, int channel, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.
	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_irq_stop(s, filep, channel, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_reset_device(struct me_device *device,
				     struct file *filep, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;
	int i, n;

	PDEBUG("executed.\n");

	/* Get the number of subdevices. */
	n = me_slist_get_number_subdevices(&device->slist);

	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}

	/* Reset every subdevice in list. */
	for (i = 0; i < n; i++) {
		s = me_slist_get_subdevice(&device->slist, i);
		err = s->me_subdevice_io_reset_subdevice(s, filep, flags);

		if (err) {
			PERROR("Cannot reset subdevice.\n");
			break;
		}
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_reset_subdevice(struct me_device *device,
					struct file *filep,
					int subdevice, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_reset_subdevice(s, filep, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_single_config(struct me_device *device,
				      struct file *filep,
				      int subdevice,
				      int channel,
				      int single_config,
				      int ref,
				      int trig_chan,
				      int trig_type, int trig_edge, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_single_config(s,
						       filep,
						       channel,
						       single_config,
						       ref,
						       trig_chan,
						       trig_type,
						       trig_edge, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_single_read(struct me_device *device,
				    struct file *filep,
				    int subdevice,
				    int channel,
				    int *value, int time_out, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_single_read(s,
						     filep,
						     channel,
						     value, time_out, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_single_write(struct me_device *device,
				     struct file *filep,
				     int subdevice,
				     int channel,
				     int value, int time_out, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_single_write(s,
						      filep,
						      channel,
						      value, time_out, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_stream_config(struct me_device *device,
				      struct file *filep,
				      int subdevice,
				      meIOStreamConfig_t *config_list,
				      int count,
				      meIOStreamTrigger_t *trigger,
				      int fifo_irq_threshold, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_stream_config(s,
						       filep,
						       config_list,
						       count,
						       trigger,
						       fifo_irq_threshold,
						       flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_stream_new_values(struct me_device *device,
					  struct file *filep,
					  int subdevice,
					  int time_out, int *count, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_stream_new_values(s,
							   filep,
							   time_out,
							   count, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_stream_read(struct me_device *device,
				    struct file *filep,
				    int subdevice,
				    int read_mode,
				    int *values, int *count, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_stream_read(s,
						     filep,
						     read_mode,
						     values, count, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_stream_start(struct me_device *device,
				     struct file *filep,
				     int subdevice,
				     int start_mode, int time_out, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_stream_start(s,
						      filep,
						      start_mode,
						      time_out, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_stream_status(struct me_device *device,
				      struct file *filep,
				      int subdevice,
				      int wait,
				      int *status, int *count, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_stream_status(s,
						       filep,
						       wait,
						       status, count, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_stream_stop(struct me_device *device,
				    struct file *filep,
				    int subdevice, int stop_mode, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_stream_stop(s,
						     filep, stop_mode, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_io_stream_write(struct me_device *device,
				     struct file *filep,
				     int subdevice,
				     int write_mode,
				     int *values, int *count, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_io_stream_write(s,
						      filep,
						      write_mode,
						      values, count, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_lock_device(struct me_device *device,
				 struct file *filep, int lock, int flags)
{
	PDEBUG("executed.\n");

	return me_dlock_lock(&device->dlock,
			     filep, lock, flags, &device->slist);
}

static int me_device_lock_subdevice(struct me_device *device,
				    struct file *filep,
				    int subdevice, int lock, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Enter device.
	err = me_dlock_enter(&device->dlock, filep);

	if (err) {
		PERROR("Cannot enter device.\n");
		return err;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_lock_subdevice(s, filep, lock, flags);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	// Exit device.
	me_dlock_exit(&device->dlock, filep);

	return err;
}

static int me_device_query_description_device(struct me_device *device,
					      char **description)
{
	PDEBUG("executed.\n");
	*description = device->device_description;
	return ME_ERRNO_SUCCESS;
}

static int me_device_query_info_device(struct me_device *device,
				       int *vendor_id,
				       int *device_id,
				       int *serial_no,
				       int *bus_type,
				       int *bus_no,
				       int *dev_no, int *func_no, int *plugged)
{
	PDEBUG("executed.\n");

	if (device->bus_type == ME_BUS_TYPE_PCI) {
		*vendor_id = device->info.pci.vendor_id;
		*device_id = device->info.pci.device_id;
		*serial_no = device->info.pci.serial_no;
		*bus_type = ME_BUS_TYPE_PCI;
		*bus_no = device->info.pci.pci_bus_no;
		*dev_no = device->info.pci.pci_dev_no;
		*func_no = device->info.pci.pci_func_no;
		*plugged = ME_PLUGGED_IN;
	} else {
		*plugged = ME_PLUGGED_OUT;
	}
	return ME_ERRNO_SUCCESS;
}

static int me_device_query_name_device(struct me_device *device, char **name)
{
	PDEBUG("executed.\n");
	*name = device->device_name;
	return ME_ERRNO_SUCCESS;
}

static int me_device_query_name_device_driver(struct me_device *device,
					      char **name)
{
	PDEBUG("executed.\n");
	*name = device->driver_name;
	return ME_ERRNO_SUCCESS;
}

static int me_device_query_number_subdevices(struct me_device *device,
					     int *number)
{
	PDEBUG("executed.\n");
	return me_slist_query_number_subdevices(&device->slist, number);
}

static int me_device_query_number_channels(struct me_device *device,
					   int subdevice, int *number)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_query_number_channels(s, number);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	return err;
}

static int me_device_query_number_ranges(struct me_device *device,
					 int subdevice, int unit, int *count)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_query_number_ranges(s, unit, count);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	return err;
}

static int me_device_query_range_by_min_max(struct me_device *device,
					    int subdevice,
					    int unit,
					    int *min,
					    int *max, int *maxdata, int *range)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_query_range_by_min_max(s,
							     unit,
							     min,
							     max,
							     maxdata, range);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	return err;
}

static int me_device_query_range_info(struct me_device *device,
				      int subdevice,
				      int range,
				      int *unit,
				      int *min, int *max, int *maxdata)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_query_range_info(s,
						       range,
						       unit, min, max, maxdata);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	return err;
}

static int me_device_query_subdevice_by_type(struct me_device *device,
					     int start_subdevice,
					     int type,
					     int subtype, int *subdevice)
{
	PDEBUG("executed.\n");

	return me_slist_get_subdevice_by_type(&device->slist,
					      start_subdevice,
					      type, subtype, subdevice);
}

static int me_device_query_subdevice_type(struct me_device *device,
					  int subdevice,
					  int *type, int *subtype)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_query_subdevice_type(s, type, subtype);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	return err;
}

static int me_device_query_subdevice_caps(struct me_device *device,
					  int subdevice, int *caps)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_query_subdevice_caps(s, caps);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	return err;
}

static int me_device_query_subdevice_caps_args(struct me_device *device,
					       int subdevice,
					       int cap, int *args, int count)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_query_subdevice_caps_args(s,
								cap,
								args, count);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	return err;
}

static int me_device_query_timer(struct me_device *device,
				 int subdevice,
				 int timer,
				 int *base_frequency,
				 uint64_t *min_ticks, uint64_t *max_ticks)
{
	int err = ME_ERRNO_SUCCESS;
	me_subdevice_t *s;

	PDEBUG("executed.\n");

	// Check subdevice index.

	if (subdevice >= me_slist_get_number_subdevices(&device->slist)) {
		PERROR("Invalid subdevice.\n");
		return ME_ERRNO_INVALID_SUBDEVICE;
	}
	// Get subdevice instance.
	s = me_slist_get_subdevice(&device->slist, subdevice);

	if (s) {
		// Call subdevice method.
		err = s->me_subdevice_query_timer(s,
						  timer,
						  base_frequency,
						  min_ticks, max_ticks);
	} else {
		// Something really bad happened.
		PERROR("Cannot get subdevice instance.\n");
		err = ME_ERRNO_INTERNAL;
	}

	return err;
}

static int me_device_query_version_device_driver(struct me_device *device,
						 int *version)
/** @todo Versions shold be read from driver. I must overwrite this function in each module. Here should be returned an error!
*/
{
	PDEBUG("executed.\n");
	*version = ME_VERSION_DRIVER;
	return ME_ERRNO_SUCCESS;
}

static int me_device_config_load(struct me_device *device, struct file *filep,
				 me_cfg_device_entry_t *config)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_SUCCESS;	//If no need for config return success.
//      return ME_ERRNO_NOT_SUPPORTED;
}

static void me_device_destructor(me_device_t *me_device)
{
	PDEBUG("executed.\n");
	me_device_deinit(me_device);
	kfree(me_device);
}

/* //me_device_usb_init
int me_device_usb_init(me_device_t *me_device, struct usb_interface *interface)
{
	PDEBUG("executed.\n");
	return -1;
}
*/

static int get_device_descriptions(uint16_t device_id,
				   char **device_name,
				   char **device_description,
				   char **driver_name)
/** @todo This is wrong concept! Static table has too strong limitations!
* 'device_name' and 'driver_name' should be calculated from 'device_id'
* 'device_description' should be read from device or moved to user space and handled by library!
*/
{
	PDEBUG("executed.\n");

	switch (device_id) {
	case PCI_DEVICE_ID_MEILHAUS_ME1000:
	case PCI_DEVICE_ID_MEILHAUS_ME1000_A:
	case PCI_DEVICE_ID_MEILHAUS_ME1000_B:
		*device_name = ME1000_NAME_DEVICE_ME1000;
		*device_description = ME1000_DESCRIPTION_DEVICE_ME1000;
		*driver_name = ME1000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1400:
		*device_name = ME1400_NAME_DEVICE_ME1400;
		*device_description = ME1400_DESCRIPTION_DEVICE_ME1400;
		*driver_name = ME1400_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140A:
		*device_name = ME1400_NAME_DEVICE_ME1400A;
		*device_description = ME1400_DESCRIPTION_DEVICE_ME1400A;
		*driver_name = ME1400_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140B:
		*device_name = ME1400_NAME_DEVICE_ME1400B;
		*device_description = ME1400_DESCRIPTION_DEVICE_ME1400B;
		*driver_name = ME1400_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME14E0:
		*device_name = ME1400_NAME_DEVICE_ME1400E;
		*device_description = ME1400_DESCRIPTION_DEVICE_ME1400E;
		*driver_name = ME1400_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME14EA:
		*device_name = ME1400_NAME_DEVICE_ME1400EA;
		*device_description = ME1400_DESCRIPTION_DEVICE_ME1400EA;
		*driver_name = ME1400_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME14EB:
		*device_name = ME1400_NAME_DEVICE_ME1400EB;
		*device_description = ME1400_DESCRIPTION_DEVICE_ME1400EB;
		*driver_name = ME1400_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140C:
		*device_name = ME1400_NAME_DEVICE_ME1400C;
		*device_description = ME1400_DESCRIPTION_DEVICE_ME1400C;
		*driver_name = ME1400_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140D:
		*device_name = ME1400_NAME_DEVICE_ME1400D;
		*device_description = ME1400_DESCRIPTION_DEVICE_ME1400D;
		*driver_name = ME1400_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_4U:
		*device_name = ME1600_NAME_DEVICE_ME16004U;
		*device_description = ME1600_DESCRIPTION_DEVICE_ME16004U;
		*driver_name = ME1600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_8U:
		*device_name = ME1600_NAME_DEVICE_ME16008U;
		*device_description = ME1600_DESCRIPTION_DEVICE_ME16008U;
		*driver_name = ME1600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_12U:
		*device_name = ME1600_NAME_DEVICE_ME160012U;
		*device_description = ME1600_DESCRIPTION_DEVICE_ME160012U;
		*driver_name = ME1600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_16U:
		*device_name = ME1600_NAME_DEVICE_ME160016U;
		*device_description = ME1600_DESCRIPTION_DEVICE_ME160016U;
		*driver_name = ME1600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_16U_8I:
		*device_name = ME1600_NAME_DEVICE_ME160016U8I;
		*device_description = ME1600_DESCRIPTION_DEVICE_ME160016U8I;
		*driver_name = ME1600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4610:
		*device_name = ME4600_NAME_DEVICE_ME4610;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4610;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4650:
		*device_name = ME4600_NAME_DEVICE_ME4650;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4650;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4660:
		*device_name = ME4600_NAME_DEVICE_ME4660;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4660;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4660I:
		*device_name = ME4600_NAME_DEVICE_ME4660I;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4660I;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4660S:
		*device_name = ME4600_NAME_DEVICE_ME4660S;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4660S;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4660IS:
		*device_name = ME4600_NAME_DEVICE_ME4660IS;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4660IS;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4670:
		*device_name = ME4600_NAME_DEVICE_ME4670;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4670;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4670I:
		*device_name = ME4600_NAME_DEVICE_ME4670I;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4670I;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4670S:
		*device_name = ME4600_NAME_DEVICE_ME4670S;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4670S;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4670IS:
		*device_name = ME4600_NAME_DEVICE_ME4670IS;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4670IS;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4680:
		*device_name = ME4600_NAME_DEVICE_ME4680;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4680;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4680I:
		*device_name = ME4600_NAME_DEVICE_ME4680I;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4680I;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4680S:
		*device_name = ME4600_NAME_DEVICE_ME4680S;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4680S;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4680IS:
		*device_name = ME4600_NAME_DEVICE_ME4680IS;
		*device_description = ME4600_DESCRIPTION_DEVICE_ME4680IS;
		*driver_name = ME4600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6004:
		*device_name = ME6000_NAME_DEVICE_ME60004;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME60004;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6008:
		*device_name = ME6000_NAME_DEVICE_ME60008;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME60008;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME600F:
		*device_name = ME6000_NAME_DEVICE_ME600016;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME600016;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6014:
		*device_name = ME6000_NAME_DEVICE_ME6000I4;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6000I4;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6018:
		*device_name = ME6000_NAME_DEVICE_ME6000I8;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6000I8;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME601F:
		*device_name = ME6000_NAME_DEVICE_ME6000I16;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6000I16;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6034:
		*device_name = ME6000_NAME_DEVICE_ME6000ISLE4;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6000ISLE4;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6038:
		*device_name = ME6000_NAME_DEVICE_ME6000ISLE8;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6000ISLE8;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME603F:
		*device_name = ME6000_NAME_DEVICE_ME6000ISLE16;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6000ISLE16;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6104:
		*device_name = ME6000_NAME_DEVICE_ME61004;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME61004;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6108:
		*device_name = ME6000_NAME_DEVICE_ME61008;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME61008;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME610F:
		*device_name = ME6000_NAME_DEVICE_ME610016;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME610016;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6114:
		*device_name = ME6000_NAME_DEVICE_ME6100I4;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6100I4;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6118:
		*device_name = ME6000_NAME_DEVICE_ME6100I8;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6100I8;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME611F:
		*device_name = ME6000_NAME_DEVICE_ME6100I16;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6100I16;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6134:
		*device_name = ME6000_NAME_DEVICE_ME6100ISLE4;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6100ISLE4;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6138:
		*device_name = ME6000_NAME_DEVICE_ME6100ISLE8;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6100ISLE8;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME613F:
		*device_name = ME6000_NAME_DEVICE_ME6100ISLE16;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6100ISLE16;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6044:
		*device_name = ME6000_NAME_DEVICE_ME60004DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME60004DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6048:
		*device_name = ME6000_NAME_DEVICE_ME60008DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME60008DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME604F:
		*device_name = ME6000_NAME_DEVICE_ME600016DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME600016DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6054:
		*device_name = ME6000_NAME_DEVICE_ME6000I4DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6000I4DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6058:
		*device_name = ME6000_NAME_DEVICE_ME6000I8DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6000I8DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME605F:
		*device_name = ME6000_NAME_DEVICE_ME6000I16DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6000I16DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6074:
		*device_name = ME6000_NAME_DEVICE_ME6000ISLE4DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6000ISLE4DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6078:
		*device_name = ME6000_NAME_DEVICE_ME6000ISLE8DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6000ISLE8DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME607F:
		*device_name = ME6000_NAME_DEVICE_ME6000ISLE16DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6000ISLE16DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6144:
		*device_name = ME6000_NAME_DEVICE_ME61004DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME61004DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6148:
		*device_name = ME6000_NAME_DEVICE_ME61008DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME61008DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME614F:
		*device_name = ME6000_NAME_DEVICE_ME610016DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME610016DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6154:
		*device_name = ME6000_NAME_DEVICE_ME6100I4DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6100I4DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6158:
		*device_name = ME6000_NAME_DEVICE_ME6100I8DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6100I8DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME615F:
		*device_name = ME6000_NAME_DEVICE_ME6100I16DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6100I16DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6174:
		*device_name = ME6000_NAME_DEVICE_ME6100ISLE4DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6100ISLE4DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6178:
		*device_name = ME6000_NAME_DEVICE_ME6100ISLE8DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6100ISLE8DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME617F:
		*device_name = ME6000_NAME_DEVICE_ME6100ISLE16DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6100ISLE16DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6259:
		*device_name = ME6000_NAME_DEVICE_ME6200I9DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6200I9DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6359:
		*device_name = ME6000_NAME_DEVICE_ME6300I9DIO;
		*device_description = ME6000_DESCRIPTION_DEVICE_ME6300I9DIO;
		*driver_name = ME6000_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME0630:
		*device_name = ME0600_NAME_DEVICE_ME0630;
		*device_description = ME0600_DESCRIPTION_DEVICE_ME0630;
		*driver_name = ME0600_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME8100_A:
		*device_name = ME8100_NAME_DEVICE_ME8100A;
		*device_description = ME8100_DESCRIPTION_DEVICE_ME8100A;
		*driver_name = ME8100_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME8100_B:
		*device_name = ME8100_NAME_DEVICE_ME8100B;
		*device_description = ME8100_DESCRIPTION_DEVICE_ME8100B;
		*driver_name = ME8100_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME8200_A:
		*device_name = ME8200_NAME_DEVICE_ME8200A;
		*device_description = ME8200_DESCRIPTION_DEVICE_ME8200A;
		*driver_name = ME8200_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME8200_B:
		*device_name = ME8200_NAME_DEVICE_ME8200B;
		*device_description = ME8200_DESCRIPTION_DEVICE_ME8200B;
		*driver_name = ME8200_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME0940:
		*device_name = ME0900_NAME_DEVICE_ME0940;
		*device_description = ME0900_DESCRIPTION_DEVICE_ME0940;
		*driver_name = ME0900_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME0950:
		*device_name = ME0900_NAME_DEVICE_ME0950;
		*device_description = ME0900_DESCRIPTION_DEVICE_ME0950;
		*driver_name = ME0900_NAME_DRIVER;
		break;

	case PCI_DEVICE_ID_MEILHAUS_ME0960:
		*device_name = ME0900_NAME_DEVICE_ME0960;
		*device_description = ME0900_DESCRIPTION_DEVICE_ME0960;
		*driver_name = ME0900_NAME_DRIVER;
		break;
/*
		case USB_DEVICE_ID_MEPHISTO_S1:
			*device_name = MEPHISTO_S1_NAME_DEVICE;
			*device_description = MEPHISTO_S1_DESCRIPTION_DEVICE;
			*driver_name = MEPHISTO_S1_NAME_DRIVER;
			break;
*/
	default:
		*device_name = EMPTY_NAME_DEVICE;
		*device_description = EMPTY_DESCRIPTION_DEVICE;
		*driver_name = EMPTY_NAME_DRIVER;

		PERROR("Invalid device id.\n");

		return 1;
	}

	return 0;
}

int me_device_pci_init(me_device_t *me_device, struct pci_dev *pci_device)
{
	int err;
	int i;

	PDEBUG("executed.\n");

	// Initialize device list head.
	INIT_LIST_HEAD(&me_device->list);

	// Initialize device description strings.
	err = get_device_descriptions(pci_device->device,
				      &me_device->device_name,
				      &me_device->device_description,
				      &me_device->driver_name);

	if (err) {
		PERROR("Cannot initialize device description strings.\n");
		return 1;
	}
	// Enable the pci device.
	err = pci_enable_device(pci_device);

	if (err < 0) {
		PERROR("Cannot enable PCI device.\n");
		return 1;
	}
	// Request the PCI register regions.
	err = pci_request_regions(pci_device, me_device->device_name);

	if (err < 0) {
		PERROR("Cannot request PCI regions.\n");
		goto ERROR_0;
	}
	// The bus carrying the device is a PCI bus.
	me_device->bus_type = ME_BUS_TYPE_PCI;

	// Store the PCI information for later usage.
	me_device->info.pci.pci_device = pci_device;

	// Get PCI register bases and sizes.
	for (i = 0; i < 6; i++) {
		me_device->info.pci.reg_bases[i] =
		    pci_resource_start(pci_device, i);
		me_device->info.pci.reg_sizes[i] =
		    pci_resource_len(pci_device, i);
	}

	// Get the PCI location.
	me_device->info.pci.pci_bus_no = pci_device->bus->number;
	me_device->info.pci.pci_dev_no = PCI_SLOT(pci_device->devfn);
	me_device->info.pci.pci_func_no = PCI_FUNC(pci_device->devfn);

	// Get Meilhaus specific device information.
	me_device->info.pci.vendor_id = pci_device->vendor;
	me_device->info.pci.device_id = pci_device->device;
	pci_read_config_byte(pci_device, 0x08,
			     &me_device->info.pci.hw_revision);
	pci_read_config_dword(pci_device, 0x2C, &me_device->info.pci.serial_no);

	// Get the interrupt request number.
	me_device->irq = pci_device->irq;

	// Initialize device lock instance.
	err = me_dlock_init(&me_device->dlock);

	if (err) {
		PERROR("Cannot initialize device lock instance.\n");
		goto ERROR_1;
	}
	// Initialize subdevice list instance.
	me_slist_init(&me_device->slist);

	if (err) {
		PERROR("Cannot initialize subdevice list instance.\n");
		goto ERROR_2;
	}
	// Initialize method pointers.
	me_device->me_device_io_irq_start = me_device_io_irq_start;
	me_device->me_device_io_irq_wait = me_device_io_irq_wait;
	me_device->me_device_io_irq_stop = me_device_io_irq_stop;
	me_device->me_device_io_reset_device = me_device_io_reset_device;
	me_device->me_device_io_reset_subdevice = me_device_io_reset_subdevice;
	me_device->me_device_io_single_config = me_device_io_single_config;
	me_device->me_device_io_single_read = me_device_io_single_read;
	me_device->me_device_io_single_write = me_device_io_single_write;
	me_device->me_device_io_stream_config = me_device_io_stream_config;
	me_device->me_device_io_stream_new_values =
	    me_device_io_stream_new_values;
	me_device->me_device_io_stream_read = me_device_io_stream_read;
	me_device->me_device_io_stream_start = me_device_io_stream_start;
	me_device->me_device_io_stream_status = me_device_io_stream_status;
	me_device->me_device_io_stream_stop = me_device_io_stream_stop;
	me_device->me_device_io_stream_write = me_device_io_stream_write;
	me_device->me_device_lock_device = me_device_lock_device;
	me_device->me_device_lock_subdevice = me_device_lock_subdevice;
	me_device->me_device_query_description_device =
	    me_device_query_description_device;
	me_device->me_device_query_info_device = me_device_query_info_device;
	me_device->me_device_query_name_device = me_device_query_name_device;
	me_device->me_device_query_name_device_driver =
	    me_device_query_name_device_driver;
	me_device->me_device_query_number_subdevices =
	    me_device_query_number_subdevices;
	me_device->me_device_query_number_channels =
	    me_device_query_number_channels;
	me_device->me_device_query_number_ranges =
	    me_device_query_number_ranges;
	me_device->me_device_query_range_by_min_max =
	    me_device_query_range_by_min_max;
	me_device->me_device_query_range_info = me_device_query_range_info;
	me_device->me_device_query_subdevice_by_type =
	    me_device_query_subdevice_by_type;
	me_device->me_device_query_subdevice_type =
	    me_device_query_subdevice_type;
	me_device->me_device_query_subdevice_caps =
	    me_device_query_subdevice_caps;
	me_device->me_device_query_subdevice_caps_args =
	    me_device_query_subdevice_caps_args;
	me_device->me_device_query_timer = me_device_query_timer;
	me_device->me_device_query_version_device_driver =
	    me_device_query_version_device_driver;
	me_device->me_device_config_load = me_device_config_load;
	me_device->me_device_destructor = me_device_destructor;

	return 0;

      ERROR_0:
	me_dlock_deinit(&me_device->dlock);

      ERROR_1:
	pci_release_regions(pci_device);

      ERROR_2:
	pci_disable_device(pci_device);

	return 1;
}

void me_device_deinit(me_device_t *me_device)
{
	PDEBUG("executed.\n");

	me_slist_deinit(&me_device->slist);
	me_dlock_deinit(&me_device->dlock);

	if (me_device->bus_type == ME_BUS_TYPE_PCI) {
		pci_release_regions(me_device->info.pci.pci_device);
		pci_disable_device(me_device->info.pci.pci_device);
	}
/*
	else
	{
		// Must be an USB device.
	}
*/
}
