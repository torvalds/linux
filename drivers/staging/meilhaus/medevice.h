/*
 * Copyright (C) 2007 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * Source File : medevice.h
 * Author      : GG (Guenter Gebhardt)  <support@meilhaus.de>
 */

#ifndef _MEDEVICE_H_
#define _MEDEVICE_H_

#ifndef KBUILD_MODNAME
#  define KBUILD_MODNAME KBUILD_STR(memain)
#endif

#include <linux/pci.h>
//#include <linux/usb.h>
#include <linux/fs.h>
#include <linux/spinlock.h>

#include "metypes.h"
#include "meslist.h"
#include "medlock.h"

#ifdef __KERNEL__

/**
 * @brief Defines a pointer type to a PCI constructor function.
 */
typedef struct me_device *(*me_pci_constructor_t) (struct pci_dev *);

/**
 * @brief Defines a pointer type to a ME-4000 PCI constructor function.
 */
#ifdef BOSCH
typedef struct me_device *(*me_bosch_constructor_t) (struct pci_dev *,
						     int me_bosch_fw);
#endif

/**
 * @brief Defines a pointer type to a USB constructor function.
 */
//typedef struct me_device *(*me_usb_constructor_t)(struct usb_interface *);

/**
 * @brief Defines a pointer type to the dummy constructor function.
 */
typedef struct me_device *(*me_dummy_constructor_t) (unsigned short vendor_id,
						     unsigned short device_id,
						     unsigned int serial_no,
						     int bus_type,
						     int bus_no,
						     int dev_no, int func_no);

//extern me_usb_constructor_t mephisto_s1_constructor __attribute__ ((weak));

/**
 * @brief Holds the PCI device information.
 */
typedef struct me_pci_info {
	struct pci_dev *pci_device;			/**< Kernel PCI device structure. */
	uint32_t reg_bases[6];				/**< The base adresses of the PCI bars. */
	uint32_t reg_sizes[6];				/**< The sizes of the PCI bars. */

	uint32_t pci_bus_no;				/**< PCI bus number. */
	uint32_t pci_dev_no;				/**< PCI device number. */
	uint32_t pci_func_no;				/**< PCI function number. */

	uint16_t vendor_id;					/**< Meilhaus PCI vendor id. */
	uint16_t device_id;					/**< Meilhaus device id. */
	uint8_t hw_revision;				/**< Hardware revision of the device. */
	uint32_t serial_no;					/**< Serial number of the device. */
} me_pci_info_t;

/**
 * @brief Holds the USB device information.
 */
//typedef struct me_usb_info {
//} me_usb_info_t;

/**
 * @brief The Meilhaus device base class structure.
 */
typedef struct me_device {
	/* Attributes */
	struct list_head list;				/**< Enables the device to be added to a dynamic list. */
//      int magic;                                                      /**< The magic number of the structure. */

	int bus_type;						/**< The descriminator for the union. */
	union {
		me_pci_info_t pci;				/**< PCI specific device information. */
//              me_usb_info_t usb;                              /**< USB specific device information. */
	} info;								/**< Holds the device information. */

	int irq;							/**< The irq assigned to this device. */

	me_dlock_t dlock;					/**< The device locking structure. */
	me_slist_t slist;					/**< The container holding all subdevices belonging to this device. */

	char *device_name;					/**< The name of the Meilhaus device. */
	char *device_description;			/**< The description of the Meilhaus device. */
	char *driver_name;					/**< The name of the device driver module supporting the device family. */

	/* Methods */
	int (*me_device_io_irq_start) (struct me_device * device,
				       struct file * filep,
				       int subdevice,
				       int channel,
				       int irq_source,
				       int irq_edge, int irq_arg, int flags);

	int (*me_device_io_irq_wait) (struct me_device * device,
				      struct file * filep,
				      int subdevice,
				      int channel,
				      int *irq_count,
				      int *value, int time_out, int flags);

	int (*me_device_io_irq_stop) (struct me_device * device,
				      struct file * filep,
				      int subdevice, int channel, int flags);

	int (*me_device_io_reset_device) (struct me_device * device,
					  struct file * filep, int flags);

	int (*me_device_io_reset_subdevice) (struct me_device * device,
					     struct file * filep,
					     int subdevice, int flags);

	int (*me_device_io_single_config) (struct me_device * device,
					   struct file * filep,
					   int subdevice,
					   int channel,
					   int single_config,
					   int ref,
					   int trig_chan,
					   int trig_type,
					   int trig_edge, int flags);

	int (*me_device_io_single_read) (struct me_device * device,
					 struct file * filep,
					 int subdevice,
					 int channel,
					 int *value, int time_out, int flags);

	int (*me_device_io_single_write) (struct me_device * device,
					  struct file * filep,
					  int subdevice,
					  int channel,
					  int value, int time_out, int flags);

	int (*me_device_io_stream_config) (struct me_device * device,
					   struct file * filep,
					   int subdevice,
					   meIOStreamConfig_t * config_list,
					   int count,
					   meIOStreamTrigger_t * trigger,
					   int fifo_irq_threshold, int flags);

	int (*me_device_io_stream_new_values) (struct me_device * device,
					       struct file * filep,
					       int subdevice,
					       int time_out,
					       int *count, int flags);

	int (*me_device_io_stream_read) (struct me_device * device,
					 struct file * filep,
					 int subdevice,
					 int read_mode,
					 int *values, int *count, int flags);

	int (*me_device_io_stream_start) (struct me_device * device,
					  struct file * filep,
					  int subdevice,
					  int start_mode,
					  int time_out, int flags);

	int (*me_device_io_stream_status) (struct me_device * device,
					   struct file * filep,
					   int subdevice,
					   int wait,
					   int *status, int *count, int flags);

	int (*me_device_io_stream_stop) (struct me_device * device,
					 struct file * filep,
					 int subdevice,
					 int stop_mode, int flags);

	int (*me_device_io_stream_write) (struct me_device * device,
					  struct file * filep,
					  int subdevice,
					  int write_mode,
					  int *values, int *count, int flags);

	int (*me_device_lock_device) (struct me_device * device,
				      struct file * filep, int lock, int flags);

	int (*me_device_lock_subdevice) (struct me_device * device,
					 struct file * filep,
					 int subdevice, int lock, int flags);

	int (*me_device_query_description_device) (struct me_device * device,
						   char **description);

	int (*me_device_query_info_device) (struct me_device * device,
					    int *vendor_id,
					    int *device_id,
					    int *serial_no,
					    int *bus_type,
					    int *bus_no,
					    int *dev_no,
					    int *func_no, int *plugged);

	int (*me_device_query_name_device) (struct me_device * device,
					    char **name);

	int (*me_device_query_name_device_driver) (struct me_device * device,
						   char **name);

	int (*me_device_query_number_subdevices) (struct me_device * device,
						  int *number);

	int (*me_device_query_number_channels) (struct me_device * device,
						int subdevice, int *number);

	int (*me_device_query_number_ranges) (struct me_device * device,
					      int subdevice,
					      int unit, int *count);

	int (*me_device_query_range_by_min_max) (struct me_device * device,
						 int subdevice,
						 int unit,
						 int *min,
						 int *max,
						 int *maxdata, int *range);

	int (*me_device_query_range_info) (struct me_device * device,
					   int subdevice,
					   int range,
					   int *unit,
					   int *min, int *max, int *maxdata);

	int (*me_device_query_subdevice_by_type) (struct me_device * device,
						  int start_subdevice,
						  int type,
						  int subtype, int *subdevice);

	int (*me_device_query_subdevice_type) (struct me_device * device,
					       int subdevice,
					       int *type, int *subtype);

	int (*me_device_query_subdevice_caps) (struct me_device * device,
					       int subdevice, int *caps);

	int (*me_device_query_subdevice_caps_args) (struct me_device * device,
						    int subdevice,
						    int cap,
						    int *args, int count);

	int (*me_device_query_timer) (struct me_device * device,
				      int subdevice,
				      int timer,
				      int *base_frequency,
				      uint64_t * min_ticks,
				      uint64_t * max_ticks);

	int (*me_device_query_version_device_driver) (struct me_device * device,
						      int *version);

	int (*me_device_config_load) (struct me_device * device,
				      struct file * filep,
				      me_cfg_device_entry_t * config);

	void (*me_device_destructor) (struct me_device * device);
} me_device_t;

/**
 * @brief Initializes a PCI device base class structure.
 *
 * @param pci_device The PCI device context as handed over by kernel.
 *
 * @return 0 on success.
 */
int me_device_pci_init(me_device_t * me_device, struct pci_dev *pci_device);

/**
 * @brief Initializes a USB device base class structure.
 *
 * @param usb_interface The USB device interface as handed over by kernel.
 *
 * @return 0 on success.
 */
//int me_device_usb_init(me_device_t *me_device, struct usb_interface *interface);

/**
  * @brief Deinitializes a device base class structure and frees any previously
  * requested resources related with this structure. It also frees any subdevice
  * instance hold by the subdevice list.
  *
  * @param me_device The device class to deinitialize.
  */
void me_device_deinit(me_device_t * me_device);

#endif
#endif
