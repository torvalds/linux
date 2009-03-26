/**
 * @file memain.c
 *
 * @brief Main Meilhaus device driver.
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

#ifndef __KERNEL__
#  define __KERNEL__
#endif

#ifndef MODULE
#  define MODULE
#endif

#include <linux/module.h>
#include <linux/pci.h>
//#include <linux/usb.h>
#include <linux/errno.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/rwsem.h>

#include "medefines.h"
#include "metypes.h"
#include "meerror.h"

#include "medebug.h"
#include "memain.h"
#include "medevice.h"
#include "meioctl.h"
#include "mecommon.h"

/* Module parameters
*/

#ifdef BOSCH
static unsigned int me_bosch_fw = 0;
EXPORT_SYMBOL(me_bosch_fw);

# ifdef module_param
module_param(me_bosch_fw, int, S_IRUGO);
# else
MODULE_PARM(me_bosch_fw, "i");
# endif

MODULE_PARM_DESC(me_bosch_fw,
		 "Flags which signals the ME-4600 driver to load the bosch firmware (default = 0).");
#endif //BOSCH

/* Global Driver Lock
*/

static struct file *me_filep = NULL;
static int me_count = 0;
static DEFINE_SPINLOCK(me_lock);
static DECLARE_RWSEM(me_rwsem);

/* Board instances are kept in a global list */
LIST_HEAD(me_device_list);

/* Prototypes
*/

static int me_probe_pci(struct pci_dev *dev, const struct pci_device_id *id);
static void me_remove_pci(struct pci_dev *dev);
static int insert_to_device_list(me_device_t *n_device);
static int replace_with_dummy(int vendor_id, int device_id, int serial_no);
static void clear_device_list(void);
static int me_open(struct inode *inode_ptr, struct file *filep);
static int me_release(struct inode *, struct file *);
static int me_ioctl(struct inode *, struct file *, unsigned int, unsigned long);
//static int me_probe_usb(struct usb_interface *interface, const struct usb_device_id *id);
//static void me_disconnect_usb(struct usb_interface *interface);

/* File operations provided by the module
*/

static const struct file_operations me_file_operations = {
	.owner = THIS_MODULE,
	.ioctl = me_ioctl,
	.open = me_open,
	.release = me_release,
};

static const struct pci_device_id me_pci_table[] = {
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1000) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1000_A) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1000_B) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1400) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME140A) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME140B) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME14E0) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME14EA) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME14EB) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME140C) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME140D) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1600_4U) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1600_8U) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1600_12U) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1600_16U) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME1600_16U_8I) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4610) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4650) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4660) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4660I) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4670) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4670I) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4670S) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4670IS) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4680) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4680I) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4680S) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME4680IS) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6004) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6008) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME600F) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6014) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6018) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME601F) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6034) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6038) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME603F) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6104) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6108) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME610F) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6114) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6118) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME611F) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6134) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6138) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME613F) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6044) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6048) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME604F) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6054) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6058) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME605F) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6074) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6078) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME607F) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6144) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6148) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME614F) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6154) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6158) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME615F) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6174) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6178) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME617F) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6259) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME6359) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME0630) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME8100_A) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME8100_B) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME8200_A) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME8200_B) },

	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME0940) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME0950) },
	{ PCI_VDEVICE(MEILHAUS, PCI_DEVICE_ID_MEILHAUS_ME0960) },

	{ }
};
MODULE_DEVICE_TABLE(pci, me_pci_table);

static struct pci_driver me_pci_driver = {
	.name = MEMAIN_NAME,
	.id_table = me_pci_table,
	.probe = me_probe_pci,
	.remove = __devexit_p(me_remove_pci),
};

/*
static struct usb_device_id me_usb_table[] = {
	{ USB_DEVICE(USB_VENDOR_ID_MEPHISTO_S1, USB_DEVICE_ID_MEPHISTO_S1) },
	{ 0 }
};
MODULE_DEVICE_TABLE (usb, me_usb_table);

static struct usb_driver me_usb_driver =
{
	.name = MEMAIN_NAME,
	.id_table = me_usb_table,
	.probe = me_probe_usb,
	.disconnect = me_disconnect_usb
};
*/

#ifdef ME_LOCK_MULTIPLEX_TEMPLATE
ME_LOCK_MULTIPLEX_TEMPLATE("me_lock_device",
			   me_lock_device_t,
			   me_lock_device,
			   me_device_lock_device,
			   (device, filep, karg.lock, karg.flags))

    ME_LOCK_MULTIPLEX_TEMPLATE("me_lock_subdevice",
			   me_lock_subdevice_t,
			   me_lock_subdevice,
			   me_device_lock_subdevice,
			   (device, filep, karg.subdevice, karg.lock,
			karg.flags))
#else
#error macro ME_LOCK_MULTIPLEX_TEMPLATE not defined
#endif

#ifdef ME_IO_MULTIPLEX_TEMPLATE
ME_IO_MULTIPLEX_TEMPLATE("me_io_irq_start",
			 me_io_irq_start_t,
			 me_io_irq_start,
			 me_device_io_irq_start,
			 (device,
			  filep,
			  karg.subdevice,
			  karg.channel,
			  karg.irq_source,
			  karg.irq_edge, karg.irq_arg, karg.flags))

    ME_IO_MULTIPLEX_TEMPLATE("me_io_irq_wait",
			 me_io_irq_wait_t,
			 me_io_irq_wait,
			 me_device_io_irq_wait,
			 (device,
		      filep,
		      karg.subdevice,
		      karg.channel,
		      &karg.irq_count, &karg.value, karg.time_out, karg.flags))

    ME_IO_MULTIPLEX_TEMPLATE("me_io_irq_stop",
			 me_io_irq_stop_t,
			 me_io_irq_stop,
			 me_device_io_irq_stop,
			 (device,
		      filep, karg.subdevice, karg.channel, karg.flags))

    ME_IO_MULTIPLEX_TEMPLATE("me_io_reset_device",
			 me_io_reset_device_t,
			 me_io_reset_device,
			 me_device_io_reset_device, (device, filep, karg.flags))

    ME_IO_MULTIPLEX_TEMPLATE("me_io_reset_subdevice",
			 me_io_reset_subdevice_t,
			 me_io_reset_subdevice,
			 me_device_io_reset_subdevice,
			 (device, filep, karg.subdevice, karg.flags))

    ME_IO_MULTIPLEX_TEMPLATE("me_io_single_config",
			 me_io_single_config_t,
			 me_io_single_config,
			 me_device_io_single_config,
			 (device,
		      filep,
		      karg.subdevice,
		      karg.channel,
		      karg.single_config,
		      karg.ref,
		      karg.trig_chan,
		      karg.trig_type, karg.trig_edge, karg.flags))

    ME_IO_MULTIPLEX_TEMPLATE("me_io_stream_new_values",
			 me_io_stream_new_values_t,
			 me_io_stream_new_values,
			 me_device_io_stream_new_values,
			 (device,
		      filep,
		      karg.subdevice, karg.time_out, &karg.count, karg.flags))

    ME_IO_MULTIPLEX_TEMPLATE("me_io_stream_read",
			 me_io_stream_read_t,
			 me_io_stream_read,
			 me_device_io_stream_read,
			 (device,
		      filep,
		      karg.subdevice,
		      karg.read_mode, karg.values, &karg.count, karg.flags))

    ME_IO_MULTIPLEX_TEMPLATE("me_io_stream_status",
			 me_io_stream_status_t,
			 me_io_stream_status,
			 me_device_io_stream_status,
			 (device,
		      filep,
		      karg.subdevice,
		      karg.wait, &karg.status, &karg.count, karg.flags))

    ME_IO_MULTIPLEX_TEMPLATE("me_io_stream_write",
			 me_io_stream_write_t,
			 me_io_stream_write,
			 me_device_io_stream_write,
			 (device,
		      filep,
		      karg.subdevice,
		      karg.write_mode, karg.values, &karg.count, karg.flags))
#else
#error macro ME_IO_MULTIPLEX_TEMPLATE not defined
#endif

#ifdef ME_QUERY_MULTIPLEX_STR_TEMPLATE
ME_QUERY_MULTIPLEX_STR_TEMPLATE("me_query_name_device",
				me_query_name_device_t,
				me_query_name_device,
				me_device_query_name_device, (device, &msg))

    ME_QUERY_MULTIPLEX_STR_TEMPLATE("me_query_name_device_driver",
				me_query_name_device_driver_t,
				me_query_name_device_driver,
				me_device_query_name_device_driver,
				(device, &msg))

    ME_QUERY_MULTIPLEX_STR_TEMPLATE("me_query_description_device",
				me_query_description_device_t,
				me_query_description_device,
				me_device_query_description_device,
				(device, &msg))
#else
#error macro ME_QUERY_MULTIPLEX_STR_TEMPLATE not defined
#endif

#ifdef ME_QUERY_MULTIPLEX_TEMPLATE
ME_QUERY_MULTIPLEX_TEMPLATE("me_query_info_device",
			    me_query_info_device_t,
			    me_query_info_device,
			    me_device_query_info_device,
			    (device,
			     &karg.vendor_id,
			     &karg.device_id,
			     &karg.serial_no,
			     &karg.bus_type,
			     &karg.bus_no,
			     &karg.dev_no, &karg.func_no, &karg.plugged))

    ME_QUERY_MULTIPLEX_TEMPLATE("me_query_number_subdevices",
			    me_query_number_subdevices_t,
			    me_query_number_subdevices,
			    me_device_query_number_subdevices,
			    (device, &karg.number))

    ME_QUERY_MULTIPLEX_TEMPLATE("me_query_number_channels",
			    me_query_number_channels_t,
			    me_query_number_channels,
			    me_device_query_number_channels,
			    (device, karg.subdevice, &karg.number))

    ME_QUERY_MULTIPLEX_TEMPLATE("me_query_subdevice_by_type",
			    me_query_subdevice_by_type_t,
			    me_query_subdevice_by_type,
			    me_device_query_subdevice_by_type,
			    (device,
			 karg.start_subdevice,
			 karg.type, karg.subtype, &karg.subdevice))

    ME_QUERY_MULTIPLEX_TEMPLATE("me_query_subdevice_type",
			    me_query_subdevice_type_t,
			    me_query_subdevice_type,
			    me_device_query_subdevice_type,
			    (device, karg.subdevice, &karg.type, &karg.subtype))

    ME_QUERY_MULTIPLEX_TEMPLATE("me_query_subdevice_caps",
			    me_query_subdevice_caps_t,
			    me_query_subdevice_caps,
			    me_device_query_subdevice_caps,
			    (device, karg.subdevice, &karg.caps))

    ME_QUERY_MULTIPLEX_TEMPLATE("me_query_subdevice_caps_args",
			    me_query_subdevice_caps_args_t,
			    me_query_subdevice_caps_args,
			    me_device_query_subdevice_caps_args,
			    (device, karg.subdevice, karg.cap, karg.args,
			 karg.count))

    ME_QUERY_MULTIPLEX_TEMPLATE("me_query_number_ranges",
			    me_query_number_ranges_t,
			    me_query_number_ranges,
			    me_device_query_number_ranges,
			    (device, karg.subdevice, karg.unit, &karg.number))

    ME_QUERY_MULTIPLEX_TEMPLATE("me_query_range_by_min_max",
			    me_query_range_by_min_max_t,
			    me_query_range_by_min_max,
			    me_device_query_range_by_min_max,
			    (device,
			 karg.subdevice,
			 karg.unit,
			 &karg.min, &karg.max, &karg.max_data, &karg.range))

    ME_QUERY_MULTIPLEX_TEMPLATE("me_query_range_info",
			    me_query_range_info_t,
			    me_query_range_info,
			    me_device_query_range_info,
			    (device,
			 karg.subdevice,
			 karg.range,
			 &karg.unit, &karg.min, &karg.max, &karg.max_data))

    ME_QUERY_MULTIPLEX_TEMPLATE("me_query_timer",
			    me_query_timer_t,
			    me_query_timer,
			    me_device_query_timer,
			    (device,
			 karg.subdevice,
			 karg.timer,
			 &karg.base_frequency,
			 &karg.min_ticks, &karg.max_ticks))

    ME_QUERY_MULTIPLEX_TEMPLATE("me_query_version_device_driver",
			    me_query_version_device_driver_t,
			    me_query_version_device_driver,
			    me_device_query_version_device_driver,
			    (device, &karg.version))
#else
#error macro ME_QUERY_MULTIPLEX_TEMPLATE not defined
#endif

/** ******************************************************************************** **/

static me_device_t *get_dummy_instance(unsigned short vendor_id,
				       unsigned short device_id,
				       unsigned int serial_no,
				       int bus_type,
				       int bus_no, int dev_no, int func_no)
{
	int err;
	me_dummy_constructor_t constructor = NULL;
	me_device_t *instance;

	PDEBUG("executed.\n");

	if ((constructor = symbol_get(medummy_constructor)) == NULL) {
		err = request_module(MEDUMMY_NAME);

		if (err) {
			PERROR("Error while request for module %s.\n",
			       MEDUMMY_NAME);
			return NULL;
		}

		if ((constructor = symbol_get(medummy_constructor)) == NULL) {
			PERROR("Can't get %s driver module constructor.\n",
			       MEDUMMY_NAME);
			return NULL;
		}
	}

	if ((instance = (*constructor) (vendor_id,
					device_id,
					serial_no,
					bus_type,
					bus_no, dev_no, func_no)) == NULL)
		symbol_put(medummy_constructor);

	return instance;
}

static int __devinit me_probe_pci(struct pci_dev *dev,
		const struct pci_device_id *id)
{
	int err;
	me_pci_constructor_t constructor = NULL;
#ifdef BOSCH
	me_bosch_constructor_t constructor_bosch = NULL;
#endif
	me_device_t *n_device = NULL;
	uint32_t device;

	char constructor_name[24] = "me0000_pci_constructor";
	char module_name[7] = "me0000";

	PDEBUG("executed.\n");
	device = dev->device;
	if ((device & 0xF000) == 0x6000) {	// Exceptions: me61xx, me62xx, me63xx are handled by one driver.
		device &= 0xF0FF;
	}

	constructor_name[2] += (char)((device >> 12) & 0x000F);
	constructor_name[3] += (char)((device >> 8) & 0x000F);
	PDEBUG("constructor_name: %s\n", constructor_name);
	module_name[2] += (char)((device >> 12) & 0x000F);
	module_name[3] += (char)((device >> 8) & 0x000F);
	PDEBUG("module_name: %s\n", module_name);

	if ((constructor =
	     (me_pci_constructor_t) symbol_get(constructor_name)) == NULL) {
		if (request_module(module_name)) {
			PERROR("Error while request for module %s.\n",
			       module_name);
			return -ENODEV;
		}

		if ((constructor =
		     (me_pci_constructor_t) symbol_get(constructor_name)) ==
		    NULL) {
			PERROR("Can't get %s driver module constructor.\n",
			       module_name);
			return -ENODEV;
		}
	}
#ifdef BOSCH
	if ((device & 0xF000) == 0x4000) {	// Bosch build has differnt constructor for me4600.
		if ((n_device =
		     (*constructor_bosch) (dev, me_bosch_fw)) == NULL) {
			symbol_put(constructor_name);
			PERROR
			    ("Can't get device instance of %s driver module.\n",
			     module_name);
			return -ENODEV;
		}
	} else {
#endif
		if ((n_device = (*constructor) (dev)) == NULL) {
			symbol_put(constructor_name);
			PERROR
			    ("Can't get device instance of %s driver module.\n",
			     module_name);
			return -ENODEV;
		}
#ifdef BOSCH
	}
#endif

	insert_to_device_list(n_device);
	err =
	    n_device->me_device_io_reset_device(n_device, NULL,
						ME_IO_RESET_DEVICE_NO_FLAGS);
	if (err) {
		PERROR("Error while reseting device.\n");
	} else {
		PDEBUG("Reseting device was sucessful.\n");
	}
	return ME_ERRNO_SUCCESS;
}

static void release_instance(me_device_t *device)
{
	int vendor_id;
	int device_id;
	int serial_no;
	int bus_type;
	int bus_no;
	int dev_no;
	int func_no;
	int plugged;

	uint32_t dev_id;

	char constructor_name[24] = "me0000_pci_constructor";

	PDEBUG("executed.\n");

	device->me_device_query_info_device(device,
					    &vendor_id,
					    &device_id,
					    &serial_no,
					    &bus_type,
					    &bus_no,
					    &dev_no, &func_no, &plugged);

	dev_id = device_id;
	device->me_device_destructor(device);

	if (plugged != ME_PLUGGED_IN) {
		PDEBUG("release: medummy_constructor\n");

		symbol_put("medummy_constructor");
	} else {
		if ((dev_id & 0xF000) == 0x6000) {	// Exceptions: me61xx, me62xx, me63xx are handled by one driver.
			dev_id &= 0xF0FF;
		}

		constructor_name[2] += (char)((dev_id >> 12) & 0x000F);
		constructor_name[3] += (char)((dev_id >> 8) & 0x000F);
		PDEBUG("release: %s\n", constructor_name);

		symbol_put(constructor_name);
	}
}

static int insert_to_device_list(me_device_t *n_device)
{
	me_device_t *o_device = NULL;

	struct list_head *pos;
	int n_vendor_id;
	int n_device_id;
	int n_serial_no;
	int n_bus_type;
	int n_bus_no;
	int n_dev_no;
	int n_func_no;
	int n_plugged;
	int o_vendor_id;
	int o_device_id;
	int o_serial_no;
	int o_bus_type;
	int o_bus_no;
	int o_dev_no;
	int o_func_no;
	int o_plugged;

	PDEBUG("executed.\n");

	n_device->me_device_query_info_device(n_device,
					      &n_vendor_id,
					      &n_device_id,
					      &n_serial_no,
					      &n_bus_type,
					      &n_bus_no,
					      &n_dev_no,
					      &n_func_no, &n_plugged);

	down_write(&me_rwsem);

	list_for_each(pos, &me_device_list) {
		o_device = list_entry(pos, me_device_t, list);
		o_device->me_device_query_info_device(o_device,
						      &o_vendor_id,
						      &o_device_id,
						      &o_serial_no,
						      &o_bus_type,
						      &o_bus_no,
						      &o_dev_no,
						      &o_func_no, &o_plugged);

		if (o_plugged == ME_PLUGGED_OUT) {
			if (((o_vendor_id == n_vendor_id) &&
			     (o_device_id == n_device_id) &&
			     (o_serial_no == n_serial_no) &&
			     (o_bus_type == n_bus_type)) ||
			    ((o_vendor_id == n_vendor_id) &&
			     (o_device_id == n_device_id) &&
			     (o_bus_type == n_bus_type) &&
			     (o_bus_no == n_bus_no) &&
			     (o_dev_no == n_dev_no) &&
			     (o_func_no == n_func_no))) {
				n_device->list.prev = pos->prev;
				n_device->list.next = pos->next;
				pos->prev->next = &n_device->list;
				pos->next->prev = &n_device->list;
				release_instance(o_device);
				break;
			}
		}
	}

	if (pos == &me_device_list) {
		list_add_tail(&n_device->list, &me_device_list);
	}

	up_write(&me_rwsem);

	return 0;
}

static void __devexit me_remove_pci(struct pci_dev *dev)
{
	int vendor_id = dev->vendor;
	int device_id = dev->device;
	int subsystem_vendor = dev->subsystem_vendor;
	int subsystem_device = dev->subsystem_device;
	int serial_no = (subsystem_device << 16) | subsystem_vendor;

	PDEBUG("executed.\n");

	PINFO("Vendor id = 0x%08X\n", vendor_id);
	PINFO("Device id = 0x%08X\n", device_id);
	PINFO("Serial Number = 0x%08X\n", serial_no);

	replace_with_dummy(vendor_id, device_id, serial_no);
}

static int replace_with_dummy(int vendor_id, int device_id, int serial_no)
{

	struct list_head *pos;
	me_device_t *n_device = NULL;
	me_device_t *o_device = NULL;
	int o_vendor_id;
	int o_device_id;
	int o_serial_no;
	int o_bus_type;
	int o_bus_no;
	int o_dev_no;
	int o_func_no;
	int o_plugged;

	PDEBUG("executed.\n");

	down_write(&me_rwsem);

	list_for_each(pos, &me_device_list) {
		o_device = list_entry(pos, me_device_t, list);
		o_device->me_device_query_info_device(o_device,
						      &o_vendor_id,
						      &o_device_id,
						      &o_serial_no,
						      &o_bus_type,
						      &o_bus_no,
						      &o_dev_no,
						      &o_func_no, &o_plugged);

		if (o_plugged == ME_PLUGGED_IN) {
			if (((o_vendor_id == vendor_id) &&
			     (o_device_id == device_id) &&
			     (o_serial_no == serial_no))) {
				n_device = get_dummy_instance(o_vendor_id,
							      o_device_id,
							      o_serial_no,
							      o_bus_type,
							      o_bus_no,
							      o_dev_no,
							      o_func_no);

				if (!n_device) {
					up_write(&me_rwsem);
					PERROR("Cannot get dummy instance.\n");
					return 1;
				}

				n_device->list.prev = pos->prev;

				n_device->list.next = pos->next;
				pos->prev->next = &n_device->list;
				pos->next->prev = &n_device->list;
				release_instance(o_device);
				break;
			}
		}
	}

	up_write(&me_rwsem);

	return 0;
}

static void clear_device_list(void)
{

	struct list_head *entry;
	me_device_t *device;

	// Clear the device info list .
	down_write(&me_rwsem);

	while (!list_empty(&me_device_list)) {
		entry = me_device_list.next;
		device = list_entry(entry, me_device_t, list);
		list_del(entry);
		release_instance(device);
	}

	up_write(&me_rwsem);
}

static int lock_driver(struct file *filep, int lock, int flags)
{
	int err = ME_ERRNO_SUCCESS;
	me_device_t *device;

	PDEBUG("executed.\n");

	down_read(&me_rwsem);

	spin_lock(&me_lock);

	switch (lock) {

	case ME_LOCK_SET:
		if (me_count) {
			PERROR
			    ("Driver System is currently used by another process.\n");
			err = ME_ERRNO_USED;
		} else if ((me_filep != NULL) && (me_filep != filep)) {
			PERROR
			    ("Driver System is already logged by another process.\n");
			err = ME_ERRNO_LOCKED;
		} else {
			list_for_each_entry(device, &me_device_list, list) {
				err =
				    device->me_device_lock_device(device, filep,
								  ME_LOCK_CHECK,
								  flags);

				if (err)
					break;
			}

			if (!err)
				me_filep = filep;
		}

		break;

	case ME_LOCK_RELEASE:
		if ((me_filep != NULL) && (me_filep != filep)) {
			err = ME_ERRNO_SUCCESS;
		} else {
			list_for_each_entry(device, &me_device_list, list) {
				device->me_device_lock_device(device, filep,
							      ME_LOCK_RELEASE,
							      flags);
			}

			me_filep = NULL;
		}

		break;

	default:
		PERROR("Invalid lock specified.\n");

		err = ME_ERRNO_INVALID_LOCK;

		break;
	}

	spin_unlock(&me_lock);

	up_read(&me_rwsem);

	return err;
}

static int me_lock_driver(struct file *filep, me_lock_driver_t *arg)
{
	int err = 0;

	me_lock_driver_t lock;

	PDEBUG("executed.\n");

	err = copy_from_user(&lock, arg, sizeof(me_lock_driver_t));

	if (err) {
		PERROR("Can't copy arguments to kernel space.\n");
		return -EFAULT;
	}

	lock.errno = lock_driver(filep, lock.lock, lock.flags);

	err = copy_to_user(arg, &lock, sizeof(me_lock_driver_t));

	if (err) {
		PERROR("Can't copy query back to user space.\n");
		return -EFAULT;
	}

	return ME_ERRNO_SUCCESS;
}

static int me_open(struct inode *inode_ptr, struct file *filep)
{

	PDEBUG("executed.\n");
	// Nothing to do here.
	return 0;
}

static int me_release(struct inode *inode_ptr, struct file *filep)
{

	PDEBUG("executed.\n");
	lock_driver(filep, ME_LOCK_RELEASE, ME_LOCK_DRIVER_NO_FLAGS);

	return 0;
}

static int me_query_version_main_driver(struct file *filep,
					me_query_version_main_driver_t *arg)
{
	int err;
	me_query_version_main_driver_t karg;

	PDEBUG("executed.\n");

	karg.version = ME_VERSION_DRIVER;
	karg.errno = ME_ERRNO_SUCCESS;

	err = copy_to_user(arg, &karg, sizeof(me_query_version_main_driver_t));

	if (err) {
		PERROR("Can't copy query back to user space.\n");
		return -EFAULT;
	}

	return 0;
}

static int me_config_load_device(struct file *filep,
				 me_cfg_device_entry_t *karg, int device_no)
{

	int err = ME_ERRNO_SUCCESS;
	int k = 0;

	struct list_head *pos = NULL;
	me_device_t *device = NULL;

	PDEBUG("executed.\n");

	list_for_each(pos, &me_device_list) {
		if (k == device_no) {
			device = list_entry(pos, me_device_t, list);
			break;
		}

		k++;
	}

	if (pos == &me_device_list) {
		PERROR("Invalid device number specified.\n");
		return ME_ERRNO_INVALID_DEVICE;
	} else {
		spin_lock(&me_lock);

		if ((me_filep != NULL) && (me_filep != filep)) {
			spin_unlock(&me_lock);
			PERROR("Resource is locked by another process.\n");
			return ME_ERRNO_LOCKED;
		} else {
			me_count++;
			spin_unlock(&me_lock);

			err =
			    device->me_device_config_load(device, filep, karg);

			spin_lock(&me_lock);
			me_count--;
			spin_unlock(&me_lock);
		}
	}

	return err;
}

static int me_config_load(struct file *filep, me_config_load_t *arg)
{
	int err;
	int i;
	me_config_load_t cfg_setup;
	me_config_load_t karg_cfg_setup;

	struct list_head *pos = NULL;

	struct list_head new_list;
	me_device_t *o_device;
	me_device_t *n_device;
	int o_vendor_id;
	int o_device_id;
	int o_serial_no;
	int o_bus_type;
	int o_bus_no;
	int o_dev_no;
	int o_func_no;
	int o_plugged;

	PDEBUG("executed.\n");

	// Copy argument to kernel space.
	err = copy_from_user(&karg_cfg_setup, arg, sizeof(me_config_load_t));

	if (err) {
		PERROR("Can't copy arguments to kernel space.\n");
		return -EFAULT;
	}
	// Allocate kernel buffer for device list.
	cfg_setup.device_list =
	    kmalloc(sizeof(me_cfg_device_entry_t) * karg_cfg_setup.count,
		    GFP_KERNEL);

	if (!cfg_setup.device_list) {
		PERROR("Can't get buffer %li for device list.\n",
		       sizeof(me_cfg_device_entry_t) * karg_cfg_setup.count);
		return -ENOMEM;
	}
	// Copy device list to kernel space.
	err =
	    copy_from_user(cfg_setup.device_list, karg_cfg_setup.device_list,
			   sizeof(me_cfg_device_entry_t) *
			   karg_cfg_setup.count);

	if (err) {
		PERROR("Can't copy device list to kernel space.\n");
		kfree(cfg_setup.device_list);
		return -EFAULT;
	}

	cfg_setup.count = karg_cfg_setup.count;

	INIT_LIST_HEAD(&new_list);

	down_write(&me_rwsem);

	spin_lock(&me_lock);

	if ((me_filep != NULL) && (me_filep != filep)) {
		spin_unlock(&me_lock);
		PERROR("Driver System is logged by another process.\n");
		karg_cfg_setup.errno = ME_ERRNO_LOCKED;
	} else {
		me_count++;
		spin_unlock(&me_lock);

		for (i = 0; i < karg_cfg_setup.count; i++) {
			PDEBUG("me_config_load() device=%d.\n", i);
			if (cfg_setup.device_list[i].tcpip.access_type ==
			    ME_ACCESS_TYPE_LOCAL) {
				list_for_each(pos, &me_device_list) {
					o_device =
					    list_entry(pos, me_device_t, list);
					o_device->
					    me_device_query_info_device
					    (o_device, &o_vendor_id,
					     &o_device_id, &o_serial_no,
					     &o_bus_type, &o_bus_no, &o_dev_no,
					     &o_func_no, &o_plugged);

					if (cfg_setup.device_list[i].info.
					    hw_location.bus_type ==
					    ME_BUS_TYPE_PCI) {
						if (((o_vendor_id ==
						      cfg_setup.device_list[i].
						      info.vendor_id)
						     && (o_device_id ==
							 cfg_setup.
							 device_list[i].info.
							 device_id)
						     && (o_serial_no ==
							 cfg_setup.
							 device_list[i].info.
							 serial_no)
						     && (o_bus_type ==
							 cfg_setup.
							 device_list[i].info.
							 hw_location.bus_type))
						    ||
						    ((o_vendor_id ==
						      cfg_setup.device_list[i].
						      info.vendor_id)
						     && (o_device_id ==
							 cfg_setup.
							 device_list[i].info.
							 device_id)
						     && (o_bus_type ==
							 cfg_setup.
							 device_list[i].info.
							 hw_location.bus_type)
						     && (o_bus_no ==
							 cfg_setup.
							 device_list[i].info.
							 hw_location.pci.bus_no)
						     && (o_dev_no ==
							 cfg_setup.
							 device_list[i].info.
							 hw_location.pci.
							 device_no)
						     && (o_func_no ==
							 cfg_setup.
							 device_list[i].info.
							 hw_location.pci.
							 function_no))) {
							list_move_tail(pos,
								       &new_list);
							break;
						}
					}
/*
					else if (cfg_setup.device_list[i].info.hw_location.bus_type == ME_BUS_TYPE_USB)
					{
						if (((o_vendor_id == cfg_setup.device_list[i].info.vendor_id) &&
						        (o_device_id == cfg_setup.device_list[i].info.device_id) &&
						        (o_serial_no == cfg_setup.device_list[i].info.serial_no) &&
						        (o_bus_type == cfg_setup.device_list[i].info.hw_location.bus_type)) ||
						        ((o_vendor_id == cfg_setup.device_list[i].info.vendor_id) &&
						         (o_device_id == cfg_setup.device_list[i].info.device_id) &&
						         (o_bus_type == cfg_setup.device_list[i].info.hw_location.bus_type) &&
						         (o_bus_no == cfg_setup.device_list[i].info.hw_location.usb.root_hub_no)))
						{
							list_move_tail(pos, &new_list);
							break;
						}
					}
*/
					else {
						PERROR("Wrong bus type: %d.\n",
						       cfg_setup.device_list[i].
						       info.hw_location.
						       bus_type);
					}
				}

				if (pos == &me_device_list) {	// Device is not already in the list
					if (cfg_setup.device_list[i].info.
					    hw_location.bus_type ==
					    ME_BUS_TYPE_PCI) {
						n_device =
						    get_dummy_instance
						    (cfg_setup.device_list[i].
						     info.vendor_id,
						     cfg_setup.device_list[i].
						     info.device_id,
						     cfg_setup.device_list[i].
						     info.serial_no,
						     cfg_setup.device_list[i].
						     info.hw_location.bus_type,
						     cfg_setup.device_list[i].
						     info.hw_location.pci.
						     bus_no,
						     cfg_setup.device_list[i].
						     info.hw_location.pci.
						     device_no,
						     cfg_setup.device_list[i].
						     info.hw_location.pci.
						     function_no);

						if (!n_device) {
							PERROR
							    ("Can't get dummy instance.\n");
							kfree(cfg_setup.
							      device_list);
							spin_lock(&me_lock);
							me_count--;
							spin_unlock(&me_lock);
							up_write(&me_rwsem);
							return -EFAULT;
						}

						list_add_tail(&n_device->list,
							      &new_list);
					}
/*
					else if (cfg_setup.device_list[i].info.hw_location.bus_type == ME_BUS_TYPE_USB)
					{
						n_device = get_dummy_instance(
						               cfg_setup.device_list[i].info.vendor_id,
						               cfg_setup.device_list[i].info.device_id,
						               cfg_setup.device_list[i].info.serial_no,
						               cfg_setup.device_list[i].info.hw_location.bus_type,
						               cfg_setup.device_list[i].info.hw_location.usb.root_hub_no,
						               0,
						               0);

						if (!n_device)
						{
							PERROR("Can't get dummy instance.\n");
							kfree(cfg_setup.device_list);
							spin_lock(&me_lock);
							me_count--;
							spin_unlock(&me_lock);
							up_write(&me_rwsem);
							return -EFAULT;
						}

						list_add_tail(&n_device->list, &new_list);
					}
*/
				}
			} else {
				n_device = get_dummy_instance(0,
							      0, 0, 0, 0, 0, 0);

				if (!n_device) {
					PERROR("Can't get dummy instance.\n");
					kfree(cfg_setup.device_list);
					spin_lock(&me_lock);
					me_count--;
					spin_unlock(&me_lock);
					up_write(&me_rwsem);
					return -EFAULT;
				}

				list_add_tail(&n_device->list, &new_list);
			}
		}

		while (!list_empty(&me_device_list)) {
			o_device =
			    list_entry(me_device_list.next, me_device_t, list);
			o_device->me_device_query_info_device(o_device,
							      &o_vendor_id,
							      &o_device_id,
							      &o_serial_no,
							      &o_bus_type,
							      &o_bus_no,
							      &o_dev_no,
							      &o_func_no,
							      &o_plugged);

			if (o_plugged == ME_PLUGGED_IN) {
				list_move_tail(me_device_list.next, &new_list);
			} else {
				list_del(me_device_list.next);
				release_instance(o_device);
			}
		}

		// Move temporary new list to global driver list.
		list_splice(&new_list, &me_device_list);

		karg_cfg_setup.errno = ME_ERRNO_SUCCESS;
	}

	for (i = 0; i < cfg_setup.count; i++) {

		karg_cfg_setup.errno =
		    me_config_load_device(filep, &cfg_setup.device_list[i], i);
		if (karg_cfg_setup.errno) {
			PERROR("me_config_load_device(%d)=%d\n", i,
			       karg_cfg_setup.errno);
			break;
		}
	}

	spin_lock(&me_lock);

	me_count--;
	spin_unlock(&me_lock);
	up_write(&me_rwsem);

	err = copy_to_user(arg, &karg_cfg_setup, sizeof(me_config_load_t));

	if (err) {
		PERROR("Can't copy config list to user space.\n");
		kfree(cfg_setup.device_list);
		return -EFAULT;
	}

	kfree(cfg_setup.device_list);
	return 0;
}

static int me_io_stream_start(struct file *filep, me_io_stream_start_t *arg)
{
	int err;
	int i, k;

	struct list_head *pos;
	me_device_t *device;
	me_io_stream_start_t karg;
	meIOStreamStart_t *list;

	PDEBUG("executed.\n");

	err = copy_from_user(&karg, arg, sizeof(me_io_stream_start_t));

	if (err) {
		PERROR("Can't copy arguments to kernel space.\n");
		return -EFAULT;
	}

	karg.errno = ME_ERRNO_SUCCESS;

	list = kmalloc(sizeof(meIOStreamStart_t) * karg.count, GFP_KERNEL);

	if (!list) {
		PERROR("Can't get buffer for start list.\n");
		return -ENOMEM;
	}

	err =
	    copy_from_user(list, karg.start_list,
			   sizeof(meIOStreamStart_t) * karg.count);

	if (err) {
		PERROR("Can't copy start list to kernel space.\n");
		kfree(list);
		return -EFAULT;
	}

	spin_lock(&me_lock);

	if ((me_filep != NULL) && (me_filep != filep)) {
		spin_unlock(&me_lock);
		PERROR("Driver System is logged by another process.\n");

		for (i = 0; i < karg.count; i++) {
			list[i].iErrno = ME_ERRNO_LOCKED;
		}
	} else {
		me_count++;
		spin_unlock(&me_lock);

		for (i = 0; i < karg.count; i++) {
			down_read(&me_rwsem);
			k = 0;
			list_for_each(pos, &me_device_list) {
				if (k == list[i].iDevice) {
					device =
					    list_entry(pos, me_device_t, list);
					break;
				}

				k++;
			}

			if (pos == &me_device_list) {
				up_read(&me_rwsem);
				PERROR("Invalid device number specified.\n");
				list[i].iErrno = ME_ERRNO_INVALID_DEVICE;
				karg.errno = ME_ERRNO_INVALID_DEVICE;
				break;
			} else {
				list[i].iErrno =
				    device->me_device_io_stream_start(device,
								      filep,
								      list[i].
								      iSubdevice,
								      list[i].
								      iStartMode,
								      list[i].
								      iTimeOut,
								      list[i].
								      iFlags);

				if (list[i].iErrno) {
					up_read(&me_rwsem);
					karg.errno = list[i].iErrno;
					break;
				}
			}

			up_read(&me_rwsem);
		}

		spin_lock(&me_lock);

		me_count--;
		spin_unlock(&me_lock);
	}

	err = copy_to_user(arg, &karg, sizeof(me_io_stream_start_t));

	if (err) {
		PERROR("Can't copy arguments to user space.\n");
		kfree(list);
		return -EFAULT;
	}

	err =
	    copy_to_user(karg.start_list, list,
			 sizeof(meIOStreamStart_t) * karg.count);

	if (err) {
		PERROR("Can't copy start list to user space.\n");
		kfree(list);
		return -EFAULT;
	}

	kfree(list);

	return err;
}

static int me_io_single(struct file *filep, me_io_single_t *arg)
{
	int err;
	int i, k;

	struct list_head *pos;
	me_device_t *device;
	me_io_single_t karg;
	meIOSingle_t *list;

	PDEBUG("executed.\n");

	err = copy_from_user(&karg, arg, sizeof(me_io_single_t));

	if (err) {
		PERROR("Can't copy arguments to kernel space.\n");
		return -EFAULT;
	}

	karg.errno = ME_ERRNO_SUCCESS;

	list = kmalloc(sizeof(meIOSingle_t) * karg.count, GFP_KERNEL);

	if (!list) {
		PERROR("Can't get buffer for single list.\n");
		return -ENOMEM;
	}

	err =
	    copy_from_user(list, karg.single_list,
			   sizeof(meIOSingle_t) * karg.count);

	if (err) {
		PERROR("Can't copy single list to kernel space.\n");
		kfree(list);
		return -EFAULT;
	}

	spin_lock(&me_lock);

	if ((me_filep != NULL) && (me_filep != filep)) {
		spin_unlock(&me_lock);
		PERROR("Driver System is logged by another process.\n");

		for (i = 0; i < karg.count; i++) {
			list[i].iErrno = ME_ERRNO_LOCKED;
		}
	} else {
		me_count++;
		spin_unlock(&me_lock);

		for (i = 0; i < karg.count; i++) {
			k = 0;

			down_read(&me_rwsem);

			list_for_each(pos, &me_device_list) {
				if (k == list[i].iDevice) {
					device =
					    list_entry(pos, me_device_t, list);
					break;
				}

				k++;
			}

			if (pos == &me_device_list) {
				up_read(&me_rwsem);
				PERROR("Invalid device number specified.\n");
				list[i].iErrno = ME_ERRNO_INVALID_DEVICE;
				karg.errno = ME_ERRNO_INVALID_DEVICE;
				break;
			} else {
				if (list[i].iDir == ME_DIR_OUTPUT) {
					list[i].iErrno =
					    device->
					    me_device_io_single_write(device,
								      filep,
								      list[i].
								      iSubdevice,
								      list[i].
								      iChannel,
								      list[i].
								      iValue,
								      list[i].
								      iTimeOut,
								      list[i].
								      iFlags);

					if (list[i].iErrno) {
						up_read(&me_rwsem);
						karg.errno = list[i].iErrno;
						break;
					}
				} else if (list[i].iDir == ME_DIR_INPUT) {
					list[i].iErrno =
					    device->
					    me_device_io_single_read(device,
								     filep,
								     list[i].
								     iSubdevice,
								     list[i].
								     iChannel,
								     &list[i].
								     iValue,
								     list[i].
								     iTimeOut,
								     list[i].
								     iFlags);

					if (list[i].iErrno) {
						up_read(&me_rwsem);
						karg.errno = list[i].iErrno;
						break;
					}
				} else {
					up_read(&me_rwsem);
					PERROR
					    ("Invalid single direction specified.\n");
					list[i].iErrno = ME_ERRNO_INVALID_DIR;
					karg.errno = ME_ERRNO_INVALID_DIR;
					break;
				}
			}

			up_read(&me_rwsem);
		}

		spin_lock(&me_lock);

		me_count--;
		spin_unlock(&me_lock);
	}

	err = copy_to_user(arg, &karg, sizeof(me_io_single_t));

	if (err) {
		PERROR("Can't copy arguments to user space.\n");
		return -EFAULT;
	}

	err =
	    copy_to_user(karg.single_list, list,
			 sizeof(meIOSingle_t) * karg.count);

	if (err) {
		PERROR("Can't copy single list to user space.\n");
		kfree(list);
		return -EFAULT;
	}

	kfree(list);

	return err;
}

static int me_io_stream_config(struct file *filep, me_io_stream_config_t *arg)
{
	int err;
	int k = 0;

	struct list_head *pos;
	me_device_t *device;
	me_io_stream_config_t karg;
	meIOStreamConfig_t *list;

	PDEBUG("executed.\n");

	err = copy_from_user(&karg, arg, sizeof(me_io_stream_config_t));

	if (err) {
		PERROR("Can't copy arguments to kernel space.\n");
		return -EFAULT;
	}

	list = kmalloc(sizeof(meIOStreamConfig_t) * karg.count, GFP_KERNEL);

	if (!list) {
		PERROR("Can't get buffer for config list.\n");
		return -ENOMEM;
	}

	err =
	    copy_from_user(list, karg.config_list,
			   sizeof(meIOStreamConfig_t) * karg.count);

	if (err) {
		PERROR("Can't copy config list to kernel space.\n");
		kfree(list);
		return -EFAULT;
	}

	spin_lock(&me_lock);

	if ((me_filep != NULL) && (me_filep != filep)) {
		spin_unlock(&me_lock);
		PERROR("Driver System is logged by another process.\n");
		karg.errno = ME_ERRNO_LOCKED;
	} else {
		me_count++;
		spin_unlock(&me_lock);

		down_read(&me_rwsem);

		list_for_each(pos, &me_device_list) {
			if (k == karg.device) {
				device = list_entry(pos, me_device_t, list);
				break;
			}

			k++;
		}

		if (pos == &me_device_list) {
			PERROR("Invalid device number specified.\n");
			karg.errno = ME_ERRNO_INVALID_DEVICE;
		} else {
			karg.errno =
			    device->me_device_io_stream_config(device, filep,
							       karg.subdevice,
							       list, karg.count,
							       &karg.trigger,
							       karg.
							       fifo_irq_threshold,
							       karg.flags);
		}

		up_read(&me_rwsem);

		spin_lock(&me_lock);
		me_count--;
		spin_unlock(&me_lock);
	}

	err = copy_to_user(arg, &karg, sizeof(me_io_stream_config_t));

	if (err) {
		PERROR("Can't copy back to user space.\n");
		kfree(list);
		return -EFAULT;
	}

	kfree(list);

	return err;
}

static int me_query_number_devices(struct file *filep,
				   me_query_number_devices_t *arg)
{
	int err;
	me_query_number_devices_t karg;

	struct list_head *pos;

	PDEBUG("executed.\n");

	karg.number = 0;
	down_read(&me_rwsem);
	list_for_each(pos, &me_device_list) {
		karg.number++;
	}

	up_read(&me_rwsem);

	karg.errno = ME_ERRNO_SUCCESS;

	err = copy_to_user(arg, &karg, sizeof(me_query_number_devices_t));

	if (err) {
		PERROR("Can't copy query back to user space.\n");
		return -EFAULT;
	}

	return 0;
}

static int me_io_stream_stop(struct file *filep, me_io_stream_stop_t *arg)
{
	int err;
	int i, k;

	struct list_head *pos;
	me_device_t *device;
	me_io_stream_stop_t karg;
	meIOStreamStop_t *list;

	PDEBUG("executed.\n");

	err = copy_from_user(&karg, arg, sizeof(me_io_stream_stop_t));

	if (err) {
		PERROR("Can't copy arguments to kernel space.\n");
		return -EFAULT;
	}

	karg.errno = ME_ERRNO_SUCCESS;

	list = kmalloc(sizeof(meIOStreamStop_t) * karg.count, GFP_KERNEL);

	if (!list) {
		PERROR("Can't get buffer for stop list.\n");
		return -ENOMEM;
	}

	err =
	    copy_from_user(list, karg.stop_list,
			   sizeof(meIOStreamStop_t) * karg.count);

	if (err) {
		PERROR("Can't copy stop list to kernel space.\n");
		kfree(list);
		return -EFAULT;
	}

	spin_lock(&me_lock);

	if ((me_filep != NULL) && (me_filep != filep)) {
		spin_unlock(&me_lock);
		PERROR("Driver System is logged by another process.\n");

		for (i = 0; i < karg.count; i++) {
			list[i].iErrno = ME_ERRNO_LOCKED;
		}
	} else {
		me_count++;
		spin_unlock(&me_lock);

		for (i = 0; i < karg.count; i++) {
			k = 0;
			down_read(&me_rwsem);
			list_for_each(pos, &me_device_list) {
				if (k == list[i].iDevice) {
					device =
					    list_entry(pos, me_device_t, list);
					break;
				}

				k++;
			}

			if (pos == &me_device_list) {
				up_read(&me_rwsem);
				PERROR("Invalid device number specified.\n");
				list[i].iErrno = ME_ERRNO_INVALID_DEVICE;
				karg.errno = ME_ERRNO_INVALID_DEVICE;
				break;
			} else {
				list[i].iErrno =
				    device->me_device_io_stream_stop(device,
								     filep,
								     list[i].
								     iSubdevice,
								     list[i].
								     iStopMode,
								     list[i].
								     iFlags);

				if (list[i].iErrno) {
					up_read(&me_rwsem);
					karg.errno = list[i].iErrno;
					break;
				}
			}

			up_read(&me_rwsem);
		}

		spin_lock(&me_lock);

		me_count--;
		spin_unlock(&me_lock);
	}

	err = copy_to_user(arg, &karg, sizeof(me_io_stream_stop_t));

	if (err) {
		PERROR("Can't copy arguments to user space.\n");
		return -EFAULT;
	}

	err =
	    copy_to_user(karg.stop_list, list,
			 sizeof(meIOStreamStop_t) * karg.count);

	if (err) {
		PERROR("Can't copy stop list to user space.\n");
		kfree(list);
		return -EFAULT;
	}

	kfree(list);

	return err;
}

/*  //me_probe_usb
static int me_probe_usb(struct usb_interface *interface, const struct usb_device_id *id)
{
	//int err;
	//me_usb_constructor_t *constructor = NULL;
	me_device_t *n_device = NULL;

	PDEBUG("executed.\n");

	switch (id->idProduct)
	{
			case USB_DEVICE_ID_MEPHISTO_S1:
				if((constructor = symbol_get(mephisto_s1_constructor)) == NULL){
					err = request_module(MEPHISTO_S1_NAME);
					if(err){
						PERROR("Error while request for module %s.\n", MEPHISTO_S1_NAME);
						return -ENODEV;
					}
					if((constructor = symbol_get(mephisto_s1_constructor)) == NULL){
						PERROR("Can't get %s driver module constructor.\n", MEPHISTO_S1_NAME);
						return -ENODEV;
					}
				}

				if((n_device = (*constructor)(interface)) == NULL){
					symbol_put(mephisto_s1_constructor);
					PERROR("Can't get device instance of %s driver module.\n", MEPHISTO_S1_NAME);
					return -ENODEV;
				}

				break;

		default:
			PERROR("Invalid product id.\n");

			return -EINVAL;
	}

	return insert_to_device_list(n_device);
}
*/

/*  //me_disconnect_usb
static void me_disconnect_usb(struct usb_interface *interface)
{

	struct usb_device *device = interface_to_usbdev(interface);
	int vendor_id = device->descriptor.idVendor;
	int device_id = device->descriptor.idProduct;
	int serial_no;

	sscanf(&device->serial[2], "%x", &serial_no);

	PDEBUG("executed.\n");

	PINFO("Vendor id = 0x%08X\n", vendor_id);
	PINFO("Device id = 0x%08X\n", device_id);
	PINFO("Serial Number = 0x%08X\n", serial_no);

	replace_with_dummy(vendor_id, device_id, serial_no);
}
*/

static int me_ioctl(struct inode *inodep,
		    struct file *filep, unsigned int service, unsigned long arg)
{

	PDEBUG("executed.\n");

	if (_IOC_TYPE(service) != MEMAIN_MAGIC) {
		PERROR("Invalid magic number.\n");
		return -ENOTTY;
	}

	PDEBUG("service number: 0x%x.\n", service);

	switch (service) {
	case ME_IO_IRQ_ENABLE:
		return me_io_irq_start(filep, (me_io_irq_start_t *) arg);

	case ME_IO_IRQ_WAIT:
		return me_io_irq_wait(filep, (me_io_irq_wait_t *) arg);

	case ME_IO_IRQ_DISABLE:
		return me_io_irq_stop(filep, (me_io_irq_stop_t *) arg);

	case ME_IO_RESET_DEVICE:
		return me_io_reset_device(filep, (me_io_reset_device_t *) arg);

	case ME_IO_RESET_SUBDEVICE:
		return me_io_reset_subdevice(filep,
					     (me_io_reset_subdevice_t *) arg);

	case ME_IO_SINGLE_CONFIG:
		return me_io_single_config(filep,
					   (me_io_single_config_t *) arg);

	case ME_IO_SINGLE:
		return me_io_single(filep, (me_io_single_t *) arg);

	case ME_IO_STREAM_CONFIG:
		return me_io_stream_config(filep,
					   (me_io_stream_config_t *) arg);

	case ME_IO_STREAM_NEW_VALUES:
		return me_io_stream_new_values(filep,
					       (me_io_stream_new_values_t *)
					       arg);

	case ME_IO_STREAM_READ:
		return me_io_stream_read(filep, (me_io_stream_read_t *) arg);

	case ME_IO_STREAM_START:
		return me_io_stream_start(filep, (me_io_stream_start_t *) arg);

	case ME_IO_STREAM_STATUS:
		return me_io_stream_status(filep,
					   (me_io_stream_status_t *) arg);

	case ME_IO_STREAM_STOP:
		return me_io_stream_stop(filep, (me_io_stream_stop_t *) arg);

	case ME_IO_STREAM_WRITE:
		return me_io_stream_write(filep, (me_io_stream_write_t *) arg);

	case ME_LOCK_DRIVER:
		return me_lock_driver(filep, (me_lock_driver_t *) arg);

	case ME_LOCK_DEVICE:
		return me_lock_device(filep, (me_lock_device_t *) arg);

	case ME_LOCK_SUBDEVICE:
		return me_lock_subdevice(filep, (me_lock_subdevice_t *) arg);

	case ME_QUERY_INFO_DEVICE:
		return me_query_info_device(filep,
					    (me_query_info_device_t *) arg);

	case ME_QUERY_DESCRIPTION_DEVICE:
		return me_query_description_device(filep,
						   (me_query_description_device_t
						    *) arg);

	case ME_QUERY_NAME_DEVICE:
		return me_query_name_device(filep,
					    (me_query_name_device_t *) arg);

	case ME_QUERY_NAME_DEVICE_DRIVER:
		return me_query_name_device_driver(filep,
						   (me_query_name_device_driver_t
						    *) arg);

	case ME_QUERY_NUMBER_DEVICES:
		return me_query_number_devices(filep,
					       (me_query_number_devices_t *)
					       arg);

	case ME_QUERY_NUMBER_SUBDEVICES:
		return me_query_number_subdevices(filep,
						  (me_query_number_subdevices_t
						   *) arg);

	case ME_QUERY_NUMBER_CHANNELS:
		return me_query_number_channels(filep,
						(me_query_number_channels_t *)
						arg);

	case ME_QUERY_NUMBER_RANGES:
		return me_query_number_ranges(filep,
					      (me_query_number_ranges_t *) arg);

	case ME_QUERY_RANGE_BY_MIN_MAX:
		return me_query_range_by_min_max(filep,
						 (me_query_range_by_min_max_t *)
						 arg);

	case ME_QUERY_RANGE_INFO:
		return me_query_range_info(filep,
					   (me_query_range_info_t *) arg);

	case ME_QUERY_SUBDEVICE_BY_TYPE:
		return me_query_subdevice_by_type(filep,
						  (me_query_subdevice_by_type_t
						   *) arg);

	case ME_QUERY_SUBDEVICE_TYPE:
		return me_query_subdevice_type(filep,
					       (me_query_subdevice_type_t *)
					       arg);

	case ME_QUERY_SUBDEVICE_CAPS:
		return me_query_subdevice_caps(filep,
					       (me_query_subdevice_caps_t *)
					       arg);

	case ME_QUERY_SUBDEVICE_CAPS_ARGS:
		return me_query_subdevice_caps_args(filep,
						    (me_query_subdevice_caps_args_t
						     *) arg);

	case ME_QUERY_TIMER:
		return me_query_timer(filep, (me_query_timer_t *) arg);

	case ME_QUERY_VERSION_MAIN_DRIVER:
		return me_query_version_main_driver(filep,
						    (me_query_version_main_driver_t
						     *) arg);

	case ME_QUERY_VERSION_DEVICE_DRIVER:
		return me_query_version_device_driver(filep,
						      (me_query_version_device_driver_t
						       *) arg);

	case ME_CONFIG_LOAD:
		return me_config_load(filep, (me_config_load_t *) arg);
	}

	PERROR("Invalid ioctl number.\n");
	return -ENOTTY;
}

static struct miscdevice me_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = MEMAIN_NAME,
	.fops = &me_file_operations,
};

// Init and exit of module.
static int memain_init(void)
{
	int result = 0;

	PDEBUG("executed.\n");

	// Register pci driver. This will return 0 if the PCI subsystem is not available.
	result = pci_register_driver(&me_pci_driver);

	if (result < 0) {
		PERROR("Can't register pci driver.\n");
		goto INIT_ERROR_1;
	}

/*
	// Register usb driver. This will return -ENODEV if no USB subsystem is available.
	result = usb_register(&me_usb_driver);

	if (result)
	{
		if (result == -ENODEV)
		{
			PERROR("No USB subsystem available.\n");
		}
		else
		{
			PERROR("Can't register usb driver.\n");
			goto INIT_ERROR_2;
		}
	}
*/
	result = misc_register(&me_miscdev);
	if (result < 0) {
		printk(KERN_ERR MEMAIN_NAME ": can't register misc device\n");
		goto INIT_ERROR_3;
	}

	return 0;

      INIT_ERROR_3:
//      usb_deregister(&me_usb_driver);

//INIT_ERROR_2:
	pci_unregister_driver(&me_pci_driver);
	clear_device_list();

      INIT_ERROR_1:
	return result;
}

static void __exit memain_exit(void)
{
	PDEBUG("executed.\n");

	misc_deregister(&me_miscdev);
	pci_unregister_driver(&me_pci_driver);
//      usb_deregister(&me_usb_driver);
	clear_device_list();
}

module_init(memain_init);
module_exit(memain_exit);

// Administrative stuff for modinfo.
MODULE_AUTHOR
    ("Guenter Gebhardt <g.gebhardt@meilhaus.de> & Krzysztof Gantzke <k.gantzke@meilhaus.de>");
MODULE_DESCRIPTION("Central module for Meilhaus Driver System.");
MODULE_SUPPORTED_DEVICE("Meilhaus PCI/cPCI boards.");
MODULE_LICENSE("GPL");
