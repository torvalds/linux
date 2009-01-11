/* Device driver for Meilhaus ME-DUMMY devices.
 * ===========================================
 *
 *    Copyright (C) 2005 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 *    This file is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * User application could also include the kernel header files. But the
 * real kernel functions are protected by #ifdef __KERNEL__.
 */
#ifndef __KERNEL__
#  define __KERNEL__
#endif

/*
 * This must be defined before module.h is included. Not needed, when
 * it is a built in driver.
 */
#ifndef MODULE
#  define MODULE
#endif

#include <linux/module.h>
#include <linux/slab.h>

#include "meerror.h"
#include "meinternal.h"

#include "meids.h"
#include "mecommon.h"
#include "medevice.h"
#include "medebug.h"

#include "medummy.h"

static int medummy_io_irq_start(me_device_t * device,
				struct file *filep,
				int subdevice,
				int channel,
				int irq_source,
				int irq_edge, int irq_arg, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_irq_wait(me_device_t * device,
			       struct file *filep,
			       int subdevice,
			       int channel,
			       int *irq_count,
			       int *value, int timeout, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_irq_stop(me_device_t * device,
			       struct file *filep,
			       int subdevice, int channel, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_reset_device(me_device_t * device,
				   struct file *filep, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_reset_subdevice(me_device_t * device,
				      struct file *filep,
				      int subdevice, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_single_config(me_device_t * device,
				    struct file *filep,
				    int subdevice,
				    int channel,
				    int single_config,
				    int ref,
				    int trig_chan,
				    int trig_type, int trig_edge, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_single_read(me_device_t * device,
				  struct file *filep,
				  int subdevice,
				  int channel,
				  int *value, int time_out, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_single_write(me_device_t * device,
				   struct file *filep,
				   int subdevice,
				   int channel,
				   int value, int time_out, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_stream_config(me_device_t * device,
				    struct file *filep,
				    int subdevice,
				    meIOStreamConfig_t * config_list,
				    int count,
				    meIOStreamTrigger_t * trigger,
				    int fifo_irq_threshold, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_stream_new_values(me_device_t * device,
					struct file *filep,
					int subdevice,
					int timeout, int *count, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_stream_read(me_device_t * device,
				  struct file *filep,
				  int subdevice,
				  int read_mode,
				  int *values, int *count, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_stream_start(me_device_t * device,
				   struct file *filep,
				   int subdevice,
				   int start_mode, int time_out, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_stream_status(me_device_t * device,
				    struct file *filep,
				    int subdevice,
				    int wait,
				    int *status, int *values, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_stream_stop(me_device_t * device,
				  struct file *filep,
				  int subdevice, int stop_mode, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_io_stream_write(me_device_t * device,
				   struct file *filep,
				   int subdevice,
				   int write_mode,
				   int *values, int *count, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_lock_device(me_device_t * device,
			       struct file *filep, int lock, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_lock_subdevice(me_device_t * device,
				  struct file *filep,
				  int subdevice, int lock, int flags)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_query_description_device(me_device_t * device,
					    char **description)
{
	medummy_device_t *instance = (medummy_device_t *) device;

	PDEBUG("executed.\n");

//      if (instance->magic != MEDUMMY_MAGIC_NUMBER)
//      {
//              PERROR("Wrong magic number.\n");
//              return ME_ERRNO_INTERNAL;
//      }

	switch (instance->device_id) {

	case PCI_DEVICE_ID_MEILHAUS_ME1000:

	case PCI_DEVICE_ID_MEILHAUS_ME1000_A:

	case PCI_DEVICE_ID_MEILHAUS_ME1000_B:
		*description = ME1000_DESCRIPTION_DEVICE_ME1000;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1400:
		*description = ME1400_DESCRIPTION_DEVICE_ME1400;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140A:
		*description = ME1400_DESCRIPTION_DEVICE_ME1400A;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140B:
		*description = ME1400_DESCRIPTION_DEVICE_ME1400B;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME14E0:
		*description = ME1400_DESCRIPTION_DEVICE_ME1400E;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME14EA:
		*description = ME1400_DESCRIPTION_DEVICE_ME1400EA;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME14EB:
		*description = ME1400_DESCRIPTION_DEVICE_ME1400EB;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140C:
		*description = ME1400_DESCRIPTION_DEVICE_ME1400C;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140D:
		*description = ME1400_DESCRIPTION_DEVICE_ME1400D;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_4U:
		*description = ME1600_DESCRIPTION_DEVICE_ME16004U;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_8U:
		*description = ME1600_DESCRIPTION_DEVICE_ME16008U;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_12U:
		*description = ME1600_DESCRIPTION_DEVICE_ME160012U;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_16U:
		*description = ME1600_DESCRIPTION_DEVICE_ME160016U;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_16U_8I:
		*description = ME1600_DESCRIPTION_DEVICE_ME160016U8I;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4610:
		*description = ME4600_DESCRIPTION_DEVICE_ME4610;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4650:
		*description = ME4600_DESCRIPTION_DEVICE_ME4650;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4660:
		*description = ME4600_DESCRIPTION_DEVICE_ME4660;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4660I:
		*description = ME4600_DESCRIPTION_DEVICE_ME4660I;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4660S:
		*description = ME4600_DESCRIPTION_DEVICE_ME4660S;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4660IS:
		*description = ME4600_DESCRIPTION_DEVICE_ME4660IS;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4670:
		*description = ME4600_DESCRIPTION_DEVICE_ME4670;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4670I:
		*description = ME4600_DESCRIPTION_DEVICE_ME4670I;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4670S:
		*description = ME4600_DESCRIPTION_DEVICE_ME4670S;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4670IS:
		*description = ME4600_DESCRIPTION_DEVICE_ME4670IS;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4680:
		*description = ME4600_DESCRIPTION_DEVICE_ME4680;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4680I:
		*description = ME4600_DESCRIPTION_DEVICE_ME4680I;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4680S:
		*description = ME4600_DESCRIPTION_DEVICE_ME4680S;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4680IS:
		*description = ME4600_DESCRIPTION_DEVICE_ME4680IS;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6004:
		*description = ME6000_DESCRIPTION_DEVICE_ME60004;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6008:
		*description = ME6000_DESCRIPTION_DEVICE_ME60008;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME600F:
		*description = ME6000_DESCRIPTION_DEVICE_ME600016;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6014:
		*description = ME6000_DESCRIPTION_DEVICE_ME6000I4;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6018:
		*description = ME6000_DESCRIPTION_DEVICE_ME6000I8;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME601F:
		*description = ME6000_DESCRIPTION_DEVICE_ME6000I16;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6034:
		*description = ME6000_DESCRIPTION_DEVICE_ME6000ISLE4;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6038:
		*description = ME6000_DESCRIPTION_DEVICE_ME6000ISLE8;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME603F:
		*description = ME6000_DESCRIPTION_DEVICE_ME6000ISLE16;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6104:
		*description = ME6000_DESCRIPTION_DEVICE_ME61004;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6108:
		*description = ME6000_DESCRIPTION_DEVICE_ME61008;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME610F:
		*description = ME6000_DESCRIPTION_DEVICE_ME610016;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6114:
		*description = ME6000_DESCRIPTION_DEVICE_ME6100I4;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6118:
		*description = ME6000_DESCRIPTION_DEVICE_ME6100I8;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME611F:
		*description = ME6000_DESCRIPTION_DEVICE_ME6100I16;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6134:
		*description = ME6000_DESCRIPTION_DEVICE_ME6100ISLE4;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6138:
		*description = ME6000_DESCRIPTION_DEVICE_ME6100ISLE8;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME613F:
		*description = ME6000_DESCRIPTION_DEVICE_ME6100ISLE16;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6044:
		*description = ME6000_DESCRIPTION_DEVICE_ME60004DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6048:
		*description = ME6000_DESCRIPTION_DEVICE_ME60008DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME604F:
		*description = ME6000_DESCRIPTION_DEVICE_ME600016DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6054:
		*description = ME6000_DESCRIPTION_DEVICE_ME6000I4DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6058:
		*description = ME6000_DESCRIPTION_DEVICE_ME6000I8DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME605F:
		*description = ME6000_DESCRIPTION_DEVICE_ME6000I16DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6074:
		*description = ME6000_DESCRIPTION_DEVICE_ME6000ISLE4DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6078:
		*description = ME6000_DESCRIPTION_DEVICE_ME6000ISLE8DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME607F:
		*description = ME6000_DESCRIPTION_DEVICE_ME6000ISLE16DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6144:
		*description = ME6000_DESCRIPTION_DEVICE_ME61004DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6148:
		*description = ME6000_DESCRIPTION_DEVICE_ME61008DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME614F:
		*description = ME6000_DESCRIPTION_DEVICE_ME610016DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6154:
		*description = ME6000_DESCRIPTION_DEVICE_ME6100I4DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6158:
		*description = ME6000_DESCRIPTION_DEVICE_ME6100I8DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME615F:
		*description = ME6000_DESCRIPTION_DEVICE_ME6100I16DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6174:
		*description = ME6000_DESCRIPTION_DEVICE_ME6100ISLE4DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6178:
		*description = ME6000_DESCRIPTION_DEVICE_ME6100ISLE8DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME617F:
		*description = ME6000_DESCRIPTION_DEVICE_ME6100ISLE16DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6259:
		*description = ME6000_DESCRIPTION_DEVICE_ME6200I9DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6359:
		*description = ME6000_DESCRIPTION_DEVICE_ME6300I9DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME0630:
		*description = ME0600_DESCRIPTION_DEVICE_ME0630;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME8100_A:
		*description = ME8100_DESCRIPTION_DEVICE_ME8100A;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME8100_B:
		*description = ME8100_DESCRIPTION_DEVICE_ME8100B;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME0940:
		*description = ME0900_DESCRIPTION_DEVICE_ME0940;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME0950:
		*description = ME0900_DESCRIPTION_DEVICE_ME0950;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME0960:
		*description = ME0900_DESCRIPTION_DEVICE_ME0960;

		break;
/*
		case USB_DEVICE_ID_MEPHISTO_S1:
			*description = MEPHISTO_S1_DESCRIPTION_DEVICE;

			break;
*/
	default:
		*description = EMPTY_DESCRIPTION_DEVICE;
		PERROR("Invalid device id in device info.\n");

		return ME_ERRNO_INTERNAL;
	}

	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_query_info_device(me_device_t * device,
				     int *vendor_id,
				     int *device_id,
				     int *serial_no,
				     int *bus_type,
				     int *bus_no,
				     int *dev_no, int *func_no, int *plugged)
{
	medummy_device_t *instance = (medummy_device_t *) device;

	PDEBUG("executed.\n");

//      if (instance->magic != MEDUMMY_MAGIC_NUMBER)
//      {
//              PERROR("Wrong magic number.\n");
//              return ME_ERRNO_INTERNAL;
//      }

	*vendor_id = instance->vendor_id;
	*device_id = instance->device_id;
	*serial_no = instance->serial_no;
	*bus_type = instance->bus_type;
	*bus_no = instance->bus_no;
	*dev_no = instance->dev_no;
	*func_no = instance->func_no;
	*plugged = ME_PLUGGED_OUT;

	return ME_ERRNO_SUCCESS;
}

static int medummy_query_name_device_driver(me_device_t * device, char **name)
{
	PDEBUG("executed.\n");
	*name = MEDUMMY_NAME_DRIVER;
	return ME_ERRNO_SUCCESS;
}

static int medummy_query_name_device(me_device_t * device, char **name)
{
	medummy_device_t *instance = (medummy_device_t *) device;

	PDEBUG("executed.\n");

// // //        if (instance->magic != MEDUMMY_MAGIC_NUMBER)
// // //        {
// // //                PERROR("Wrong magic number.\n");
// // //                return ME_ERRNO_INTERNAL;
// // //        }

	switch (instance->device_id) {

	case PCI_DEVICE_ID_MEILHAUS_ME1000:

	case PCI_DEVICE_ID_MEILHAUS_ME1000_A:

	case PCI_DEVICE_ID_MEILHAUS_ME1000_B:
		*name = ME1000_NAME_DEVICE_ME1000;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1400:
		*name = ME1400_NAME_DEVICE_ME1400;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140A:
		*name = ME1400_NAME_DEVICE_ME1400A;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140B:
		*name = ME1400_NAME_DEVICE_ME1400B;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME14E0:
		*name = ME1400_NAME_DEVICE_ME1400E;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME14EA:
		*name = ME1400_NAME_DEVICE_ME1400EA;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME14EB:
		*name = ME1400_NAME_DEVICE_ME1400EB;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140C:
		*name = ME1400_NAME_DEVICE_ME1400C;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME140D:
		*name = ME1400_NAME_DEVICE_ME1400D;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_4U:
		*name = ME1600_NAME_DEVICE_ME16004U;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_8U:
		*name = ME1600_NAME_DEVICE_ME16008U;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_12U:
		*name = ME1600_NAME_DEVICE_ME160012U;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_16U:
		*name = ME1600_NAME_DEVICE_ME160016U;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME1600_16U_8I:
		*name = ME1600_NAME_DEVICE_ME160016U8I;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4610:
		*name = ME4600_NAME_DEVICE_ME4610;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4650:
		*name = ME4600_NAME_DEVICE_ME4650;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4660:
		*name = ME4600_NAME_DEVICE_ME4660;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4660I:
		*name = ME4600_NAME_DEVICE_ME4660I;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4670:
		*name = ME4600_NAME_DEVICE_ME4670;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4670I:
		*name = ME4600_NAME_DEVICE_ME4670I;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4670S:
		*name = ME4600_NAME_DEVICE_ME4670S;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4670IS:
		*name = ME4600_NAME_DEVICE_ME4670IS;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4680:
		*name = ME4600_NAME_DEVICE_ME4680;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4680I:
		*name = ME4600_NAME_DEVICE_ME4680I;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4680S:
		*name = ME4600_NAME_DEVICE_ME4680S;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME4680IS:
		*name = ME4600_NAME_DEVICE_ME4680IS;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6004:
		*name = ME6000_NAME_DEVICE_ME60004;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6008:
		*name = ME6000_NAME_DEVICE_ME60008;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME600F:
		*name = ME6000_NAME_DEVICE_ME600016;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6014:
		*name = ME6000_NAME_DEVICE_ME6000I4;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6018:
		*name = ME6000_NAME_DEVICE_ME6000I8;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME601F:
		*name = ME6000_NAME_DEVICE_ME6000I16;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6034:
		*name = ME6000_NAME_DEVICE_ME6000ISLE4;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6038:
		*name = ME6000_NAME_DEVICE_ME6000ISLE8;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME603F:
		*name = ME6000_NAME_DEVICE_ME6000ISLE16;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6104:
		*name = ME6000_NAME_DEVICE_ME61004;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6108:
		*name = ME6000_NAME_DEVICE_ME61008;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME610F:
		*name = ME6000_NAME_DEVICE_ME610016;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6114:
		*name = ME6000_NAME_DEVICE_ME6100I4;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6118:
		*name = ME6000_NAME_DEVICE_ME6100I8;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME611F:
		*name = ME6000_NAME_DEVICE_ME6100I16;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6134:
		*name = ME6000_NAME_DEVICE_ME6100ISLE4;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6138:
		*name = ME6000_NAME_DEVICE_ME6100ISLE8;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME613F:
		*name = ME6000_NAME_DEVICE_ME6100ISLE16;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6044:
		*name = ME6000_NAME_DEVICE_ME60004DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6048:
		*name = ME6000_NAME_DEVICE_ME60008DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME604F:
		*name = ME6000_NAME_DEVICE_ME600016DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6054:
		*name = ME6000_NAME_DEVICE_ME6000I4DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6058:
		*name = ME6000_NAME_DEVICE_ME6000I8DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME605F:
		*name = ME6000_NAME_DEVICE_ME6000I16DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6074:
		*name = ME6000_NAME_DEVICE_ME6000ISLE4DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6078:
		*name = ME6000_NAME_DEVICE_ME6000ISLE8DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME607F:
		*name = ME6000_NAME_DEVICE_ME6000ISLE16DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6144:
		*name = ME6000_NAME_DEVICE_ME61004DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6148:
		*name = ME6000_NAME_DEVICE_ME61008DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME614F:
		*name = ME6000_NAME_DEVICE_ME610016DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6154:
		*name = ME6000_NAME_DEVICE_ME6100I4DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6158:
		*name = ME6000_NAME_DEVICE_ME6100I8DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME615F:
		*name = ME6000_NAME_DEVICE_ME6100I16DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6174:
		*name = ME6000_NAME_DEVICE_ME6100ISLE4DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME6178:
		*name = ME6000_NAME_DEVICE_ME6100ISLE8DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME617F:
		*name = ME6000_NAME_DEVICE_ME6100ISLE16DIO;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME0630:
		*name = ME0600_NAME_DEVICE_ME0630;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME8100_A:
		*name = ME8100_NAME_DEVICE_ME8100A;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME8100_B:
		*name = ME8100_NAME_DEVICE_ME8100B;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME0940:
		*name = ME0900_NAME_DEVICE_ME0940;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME0950:
		*name = ME0900_NAME_DEVICE_ME0950;

		break;

	case PCI_DEVICE_ID_MEILHAUS_ME0960:
		*name = ME0900_NAME_DEVICE_ME0960;

		break;
/*
		case USB_DEVICE_ID_MEPHISTO_S1:
			*name = MEPHISTO_S1_NAME_DEVICE;

			break;
*/
	default:
		*name = EMPTY_NAME_DEVICE;
		PERROR("Invalid PCI device id.\n");

		return ME_ERRNO_INTERNAL;
	}

	return ME_ERRNO_SUCCESS;
}

static int medummy_query_number_subdevices(me_device_t * device, int *number)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_query_number_channels(me_device_t * device,
					 int subdevice, int *number)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_query_number_ranges(me_device_t * device,
				       int subdevice, int unit, int *count)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_query_subdevice_type(me_device_t * device,
					int subdevice, int *type, int *subtype)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_query_subdevice_caps(me_device_t * device,
					int subdevice, int *caps)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_query_subdevice_caps_args(me_device_t * device,
					     int subdevice,
					     int cap, int *args, int count)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_NOT_SUPPORTED;
}

static int medummy_query_subdevice_by_type(me_device_t * device,
					   int start_subdevice,
					   int type,
					   int subtype, int *subdevice)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_query_range_by_min_max(me_device_t * device,
					  int subdevice,
					  int unit,
					  int *min,
					  int *max, int *maxdata, int *range)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_query_range_info(me_device_t * device,
				    int subdevice,
				    int range,
				    int *unit, int *min, int *max, int *maxdata)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

int medummy_query_timer(me_device_t * device,
			int subdevice,
			int timer,
			int *base_frequency,
			uint64_t * min_ticks, uint64_t * max_ticks)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_DEVICE_UNPLUGGED;
}

static int medummy_query_version_device_driver(me_device_t * device,
					       int *version)
{
	PDEBUG("executed.\n");

	*version = ME_VERSION_DRIVER;
	return ME_ERRNO_SUCCESS;
}

static void medummy_destructor(me_device_t * device)
{
	PDEBUG("executed.\n");
	kfree(device);
}

static int init_device_info(unsigned short vendor_id,
			    unsigned short device_id,
			    unsigned int serial_no,
			    int bus_type,
			    int bus_no,
			    int dev_no,
			    int func_no, medummy_device_t * instance)
{
	PDEBUG("executed.\n");

//      instance->magic = MEDUMMY_MAGIC_NUMBER;
	instance->vendor_id = vendor_id;
	instance->device_id = device_id;
	instance->serial_no = serial_no;
	instance->bus_type = bus_type;
	instance->bus_no = bus_no;
	instance->dev_no = dev_no;
	instance->func_no = func_no;

	return 0;
}

static int medummy_config_load(me_device_t * device, struct file *filep,
			       me_cfg_device_entry_t * config)
{
	PDEBUG("executed.\n");
	return ME_ERRNO_SUCCESS;
}

static int init_device_instance(me_device_t * device)
{
	PDEBUG("executed.\n");

	INIT_LIST_HEAD(&device->list);

	device->me_device_io_irq_start = medummy_io_irq_start;
	device->me_device_io_irq_wait = medummy_io_irq_wait;
	device->me_device_io_irq_stop = medummy_io_irq_stop;
	device->me_device_io_reset_device = medummy_io_reset_device;
	device->me_device_io_reset_subdevice = medummy_io_reset_subdevice;
	device->me_device_io_single_config = medummy_io_single_config;
	device->me_device_io_single_read = medummy_io_single_read;
	device->me_device_io_single_write = medummy_io_single_write;
	device->me_device_io_stream_config = medummy_io_stream_config;
	device->me_device_io_stream_new_values = medummy_io_stream_new_values;
	device->me_device_io_stream_read = medummy_io_stream_read;
	device->me_device_io_stream_start = medummy_io_stream_start;
	device->me_device_io_stream_status = medummy_io_stream_status;
	device->me_device_io_stream_stop = medummy_io_stream_stop;
	device->me_device_io_stream_write = medummy_io_stream_write;

	device->me_device_lock_device = medummy_lock_device;
	device->me_device_lock_subdevice = medummy_lock_subdevice;

	device->me_device_query_description_device =
	    medummy_query_description_device;
	device->me_device_query_info_device = medummy_query_info_device;
	device->me_device_query_name_device_driver =
	    medummy_query_name_device_driver;
	device->me_device_query_name_device = medummy_query_name_device;

	device->me_device_query_number_subdevices =
	    medummy_query_number_subdevices;
	device->me_device_query_number_channels = medummy_query_number_channels;
	device->me_device_query_number_ranges = medummy_query_number_ranges;

	device->me_device_query_range_by_min_max =
	    medummy_query_range_by_min_max;
	device->me_device_query_range_info = medummy_query_range_info;

	device->me_device_query_subdevice_type = medummy_query_subdevice_type;
	device->me_device_query_subdevice_by_type =
	    medummy_query_subdevice_by_type;
	device->me_device_query_subdevice_caps = medummy_query_subdevice_caps;
	device->me_device_query_subdevice_caps_args =
	    medummy_query_subdevice_caps_args;

	device->me_device_query_timer = medummy_query_timer;

	device->me_device_query_version_device_driver =
	    medummy_query_version_device_driver;

	device->me_device_destructor = medummy_destructor;
	device->me_device_config_load = medummy_config_load;
	return 0;
}

me_device_t *medummy_constructor(unsigned short vendor_id,
				 unsigned short device_id,
				 unsigned int serial_no,
				 int bus_type,
				 int bus_no, int dev_no, int func_no)
{
	int result = 0;
	medummy_device_t *instance;

	PDEBUG("executed.\n");

	/* Allocate structure for device attributes */
	instance = kmalloc(sizeof(medummy_device_t), GFP_KERNEL);

	if (!instance) {
		PERROR("Can't get memory for device instance.\n");
		return NULL;
	}

	memset(instance, 0, sizeof(medummy_device_t));

	/* Initialize device info */
	result = init_device_info(vendor_id,
				  device_id,
				  serial_no,
				  bus_type, bus_no, dev_no, func_no, instance);

	if (result) {
		PERROR("Cannot init baord info.\n");
		kfree(instance);
		return NULL;
	}

	/* Initialize device instance */
	result = init_device_instance((me_device_t *) instance);

	if (result) {
		PERROR("Cannot init baord info.\n");
		kfree(instance);
		return NULL;
	}

	return (me_device_t *) instance;
}

// Init and exit of module.

static int __init dummy_init(void)
{
	PDEBUG("executed.\n");
	return 0;
}

static void __exit dummy_exit(void)
{
	PDEBUG("executed.\n");
}

module_init(dummy_init);

module_exit(dummy_exit);

// Administrative stuff for modinfo.
MODULE_AUTHOR("Guenter Gebhardt <g.gebhardt@meilhaus.de>");
MODULE_DESCRIPTION("Device Driver Module for Meilhaus ME-DUMMY Devices");
MODULE_SUPPORTED_DEVICE("Meilhaus ME-DUMMY Devices");
MODULE_LICENSE("GPL");

// Export the constructor.
EXPORT_SYMBOL(medummy_constructor);
