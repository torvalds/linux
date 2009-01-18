/*
 * Copyright (C) 2005 Meilhaus Electronic GmbH (support@meilhaus.de)
 *
 * Source File : meioctl.h
 * Author      : GG (Guenter Gebhardt)  <g.gebhardt@meilhaus.de>
 */

#ifndef _MEIOCTL_H_
#define _MEIOCTL_H_


/*=============================================================================
  Types for the input/output ioctls
  ===========================================================================*/

typedef struct me_io_irq_start {
	int device;
	int subdevice;
	int channel;
	int irq_source;
	int irq_edge;
	int irq_arg;
	int flags;
	int errno;
} me_io_irq_start_t;


typedef struct me_io_irq_wait {
	int device;
	int subdevice;
	int channel;
	int irq_count;
	int value;
	int time_out;
	int flags;
	int errno;
} me_io_irq_wait_t;


typedef struct me_io_irq_stop {
	int device;
	int subdevice;
	int channel;
	int flags;
	int errno;
} me_io_irq_stop_t;


typedef struct me_io_reset_device {
	int device;
	int flags;
	int errno;
} me_io_reset_device_t;


typedef struct me_io_reset_subdevice {
	int device;
	int subdevice;
	int flags;
	int errno;
} me_io_reset_subdevice_t;


typedef struct me_io_single_config {
	int device;
	int subdevice;
	int channel;
	int single_config;
	int ref;
	int trig_chan;
	int trig_type;
	int trig_edge;
	int flags;
	int errno;
} me_io_single_config_t;


typedef struct me_io_single {
	meIOSingle_t *single_list;
	int count;
	int flags;
	int errno;
} me_io_single_t;


typedef struct me_io_stream_config {
	int device;
	int subdevice;
	meIOStreamConfig_t *config_list;
	int count;
	meIOStreamTrigger_t trigger;
	int fifo_irq_threshold;
	int flags;
	int errno;
} me_io_stream_config_t;


typedef struct me_io_stream_new_values {
	int device;
	int subdevice;
	int time_out;
	int count;
	int flags;
	int errno;
} me_io_stream_new_values_t;


typedef struct me_io_stream_read {
	int device;
	int subdevice;
	int read_mode;
	int *values;
	int count;
	int flags;
	int errno;
} me_io_stream_read_t;


typedef struct me_io_stream_start {
	meIOStreamStart_t *start_list;
	int count;
	int flags;
	int errno;
} me_io_stream_start_t;


typedef struct me_io_stream_status {
	int device;
	int subdevice;
	int wait;
	int status;
	int count;
	int flags;
	int errno;
} me_io_stream_status_t;


typedef struct me_io_stream_stop {
	meIOStreamStop_t *stop_list;
	int count;
	int flags;
	int errno;
} me_io_stream_stop_t;


typedef struct me_io_stream_write {
	int device;
	int subdevice;
	int write_mode;
	int *values;
	int count;
	int flags;
	int errno;
} me_io_stream_write_t;


/*=============================================================================
  Types for the lock ioctls
  ===========================================================================*/

typedef struct me_lock_device {
	int device;
	int lock;
	int flags;
	int errno;
} me_lock_device_t;


typedef struct me_lock_driver {
	int flags;
	int lock;
	int errno;
} me_lock_driver_t;


typedef struct me_lock_subdevice {
	int device;
	int subdevice;
	int lock;
	int flags;
	int errno;
} me_lock_subdevice_t;


/*=============================================================================
  Types for the query ioctls
  ===========================================================================*/

typedef struct me_query_info_device {
	int device;
	int vendor_id;
	int device_id;
	int serial_no;
	int bus_type;
	int bus_no;
	int dev_no;
	int func_no;
	int plugged;
	int errno;
} me_query_info_device_t;


typedef struct me_query_description_device {
	int device;
	char *name;
	int count;
	int errno;
} me_query_description_device_t;


typedef struct me_query_name_device {
	int device;
	char *name;
	int count;
	int errno;
} me_query_name_device_t;


typedef struct me_query_name_device_driver {
	int device;
	char *name;
	int count;
	int errno;
} me_query_name_device_driver_t;


typedef struct me_query_version_main_driver {
	int version;
	int errno;
} me_query_version_main_driver_t;


typedef struct me_query_version_device_driver {
	int device;
	int version;
	int errno;
} me_query_version_device_driver_t;


typedef struct me_query_number_devices {
	int number;
	int errno;
} me_query_number_devices_t;


typedef struct me_query_number_subdevices {
	int device;
	int number;
	int errno;
} me_query_number_subdevices_t;


typedef struct me_query_number_channels {
	int device;
	int subdevice;
	int number;
	int errno;
} me_query_number_channels_t;


typedef struct me_query_number_ranges {
	int device;
	int subdevice;
	int channel;
	int unit;
	int number;
	int errno;
} me_query_number_ranges_t;


typedef struct me_query_subdevice_by_type {
	int device;
	int start_subdevice;
	int type;
	int subtype;
	int subdevice;
	int errno;
} me_query_subdevice_by_type_t;


typedef struct me_query_subdevice_type {
	int device;
	int subdevice;
	int type;
	int subtype;
	int errno;
} me_query_subdevice_type_t;


typedef struct me_query_subdevice_caps {
	int device;
	int subdevice;
	int caps;
	int errno;
} me_query_subdevice_caps_t;


typedef struct me_query_subdevice_caps_args {
	int device;
	int subdevice;
	int cap;
	int args[8];
	int count;
	int errno;
} me_query_subdevice_caps_args_t;


typedef struct me_query_timer {
	int device;
	int subdevice;
	int timer;
	int base_frequency;
	long long min_ticks;
	long long max_ticks;
	int errno;
} me_query_timer_t;


typedef struct me_query_range_by_min_max {
	int device;
	int subdevice;
	int channel;
	int unit;
	int min;
	int max;
	int max_data;
	int range;
	int errno;
} me_query_range_by_min_max_t;


typedef struct me_query_range_info {
	int device;
	int subdevice;
	int channel;
	int unit;
	int range;
	int min;
	int max;
	int max_data;
	int errno;
} me_query_range_info_t;


/*=============================================================================
  Types for the configuration ioctls
  ===========================================================================*/

typedef struct me_cfg_tcpip_location {
	int access_type;
	char *remote_host;
	int remote_device_number;
} me_cfg_tcpip_location_t;


typedef union me_cfg_tcpip {
	int access_type;
	me_cfg_tcpip_location_t location;
} me_cfg_tcpip_t;


typedef struct me_cfg_pci_hw_location {
	unsigned int bus_type;
	unsigned int bus_no;
	unsigned int device_no;
	unsigned int function_no;
} me_cfg_pci_hw_location_t;

/*
typedef struct me_cfg_usb_hw_location {
	unsigned int bus_type;
	unsigned int root_hub_no;
} me_cfg_usb_hw_location_t;
*/

typedef union me_cfg_hw_location {
	unsigned int bus_type;
	me_cfg_pci_hw_location_t pci;
//	me_cfg_usb_hw_location_t usb;
} me_cfg_hw_location_t;


typedef struct me_cfg_device_info {
	unsigned int vendor_id;
	unsigned int device_id;
	unsigned int serial_no;
	me_cfg_hw_location_t hw_location;
} me_cfg_device_info_t;


typedef struct me_cfg_subdevice_info {
	int type;
	int sub_type;
	unsigned int number_channels;
} me_cfg_subdevice_info_t;


typedef struct me_cfg_range_entry {
	int unit;
	double min;
	double max;
	unsigned int max_data;
} me_cfg_range_entry_t;


typedef struct me_cfg_mux32m_device {
	int type;
	int timed;
	unsigned int ai_channel;
	unsigned int dio_device;
	unsigned int dio_subdevice;
	unsigned int timer_device;
	unsigned int timer_subdevice;
	unsigned int mux32s_count;
} me_cfg_mux32m_device_t;


typedef struct me_cfg_demux32_device {
	int type;
	int timed;
	unsigned int ao_channel;
	unsigned int dio_device;
	unsigned int dio_subdevice;
	unsigned int timer_device;
	unsigned int timer_subdevice;
} me_cfg_demux32_device_t;


typedef union me_cfg_external_device {
	int type;
	me_cfg_mux32m_device_t mux32m;
	me_cfg_demux32_device_t demux32;
} me_cfg_external_device_t;


typedef struct me_cfg_subdevice_entry {
	me_cfg_subdevice_info_t info;
	me_cfg_range_entry_t *range_list;
	unsigned int count;
	int locked;
	me_cfg_external_device_t external_device;
} me_cfg_subdevice_entry_t;


typedef struct me_cfg_device_entry {
	me_cfg_tcpip_t tcpip;
	me_cfg_device_info_t info;
	me_cfg_subdevice_entry_t *subdevice_list;
	unsigned int count;
} me_cfg_device_entry_t;


typedef struct me_config_load {
	me_cfg_device_entry_t *device_list;
	unsigned int count;
	int errno;
} me_config_load_t;


/*=============================================================================
  The ioctls of the board
  ===========================================================================*/

#define MEMAIN_MAGIC 'y'

#define ME_IO_IRQ_ENABLE				_IOR (MEMAIN_MAGIC, 1, me_io_irq_start_t)
#define ME_IO_IRQ_WAIT					_IOR (MEMAIN_MAGIC, 2, me_io_irq_wait_t)
#define ME_IO_IRQ_DISABLE				_IOR (MEMAIN_MAGIC, 3, me_io_irq_stop_t)

#define ME_IO_RESET_DEVICE				_IOW (MEMAIN_MAGIC, 4, me_io_reset_device_t)
#define ME_IO_RESET_SUBDEVICE			_IOW (MEMAIN_MAGIC, 5, me_io_reset_subdevice_t)

#define ME_IO_SINGLE					_IOWR(MEMAIN_MAGIC, 6, me_io_single_t)
#define ME_IO_SINGLE_CONFIG				_IOW (MEMAIN_MAGIC, 7, me_io_single_config_t)

#define ME_IO_STREAM_CONFIG				_IOW (MEMAIN_MAGIC, 8, me_io_stream_config_t)
#define ME_IO_STREAM_NEW_VALUES			_IOR (MEMAIN_MAGIC, 9, me_io_stream_new_values_t)
#define ME_IO_STREAM_READ				_IOR (MEMAIN_MAGIC, 10, me_io_stream_read_t)
#define ME_IO_STREAM_START				_IOW (MEMAIN_MAGIC, 11, me_io_stream_start_t)
#define ME_IO_STREAM_STATUS				_IOR (MEMAIN_MAGIC, 12, me_io_stream_status_t)
#define ME_IO_STREAM_STOP				_IOW (MEMAIN_MAGIC, 13, me_io_stream_stop_t)
#define ME_IO_STREAM_WRITE				_IOW (MEMAIN_MAGIC, 14, me_io_stream_write_t)

#define ME_LOCK_DRIVER					_IOW (MEMAIN_MAGIC, 15, me_lock_driver_t)
#define ME_LOCK_DEVICE					_IOW (MEMAIN_MAGIC, 16, me_lock_device_t)
#define ME_LOCK_SUBDEVICE				_IOW (MEMAIN_MAGIC, 17, me_lock_subdevice_t)

#define ME_QUERY_DESCRIPTION_DEVICE		_IOR (MEMAIN_MAGIC, 18, me_query_description_device_t)

#define ME_QUERY_INFO_DEVICE			_IOR (MEMAIN_MAGIC, 19, me_query_info_device_t)

#define ME_QUERY_NAME_DEVICE			_IOR (MEMAIN_MAGIC, 20, me_query_name_device_t)
#define ME_QUERY_NAME_DEVICE_DRIVER		_IOR (MEMAIN_MAGIC, 21, me_query_name_device_driver_t)

#define ME_QUERY_NUMBER_DEVICES			_IOR (MEMAIN_MAGIC, 22, me_query_number_devices_t)
#define ME_QUERY_NUMBER_SUBDEVICES		_IOR (MEMAIN_MAGIC, 23, me_query_number_subdevices_t)
#define ME_QUERY_NUMBER_CHANNELS		_IOR (MEMAIN_MAGIC, 24, me_query_number_channels_t)
#define ME_QUERY_NUMBER_RANGES			_IOR (MEMAIN_MAGIC, 25, me_query_number_ranges_t)

#define ME_QUERY_RANGE_BY_MIN_MAX		_IOR (MEMAIN_MAGIC, 26, me_query_range_by_min_max_t)
#define ME_QUERY_RANGE_INFO				_IOR (MEMAIN_MAGIC, 27, me_query_range_info_t)

#define ME_QUERY_SUBDEVICE_BY_TYPE		_IOR (MEMAIN_MAGIC, 28, me_query_subdevice_by_type_t)
#define ME_QUERY_SUBDEVICE_TYPE			_IOR (MEMAIN_MAGIC, 29, me_query_subdevice_type_t)
#define ME_QUERY_SUBDEVICE_CAPS			_IOR (MEMAIN_MAGIC, 29, me_query_subdevice_caps_t)
#define ME_QUERY_SUBDEVICE_CAPS_ARGS	_IOR (MEMAIN_MAGIC, 30, me_query_subdevice_caps_args_t)

#define ME_QUERY_TIMER					_IOR (MEMAIN_MAGIC, 31, me_query_timer_t)

#define ME_QUERY_VERSION_DEVICE_DRIVER	_IOR (MEMAIN_MAGIC, 32, me_query_version_device_driver_t)
#define ME_QUERY_VERSION_MAIN_DRIVER	_IOR (MEMAIN_MAGIC, 33, me_query_version_main_driver_t)

#define ME_CONFIG_LOAD					_IOWR(MEMAIN_MAGIC, 34, me_config_load_t)

#endif
