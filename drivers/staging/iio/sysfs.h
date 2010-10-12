/* The industrial I/O core
 *
 *Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * General attributes
 */

#ifndef _INDUSTRIAL_IO_SYSFS_H_
#define _INDUSTRIAL_IO_SYSFS_H_

#include "iio.h"

/**
 * struct iio_event_attr - event control attribute
 * @dev_attr:	underlying device attribute
 * @mask:	mask for the event when detecting
 * @listel:	list header to allow addition to list of event handlers
*/
struct iio_event_attr {
	struct device_attribute dev_attr;
	int mask;
	struct iio_event_handler_list *listel;
};

#define to_iio_event_attr(_dev_attr) \
	container_of(_dev_attr, struct iio_event_attr, dev_attr)

/**
 * struct iio_chrdev_minor_attr - simple attribute to allow reading of chrdev
 *				minor number
 * @dev_attr:	underlying device attribute
 * @minor:	the minor number
 */
struct iio_chrdev_minor_attr {
	struct device_attribute dev_attr;
	int minor;
};

void
__init_iio_chrdev_minor_attr(struct iio_chrdev_minor_attr *minor_attr,
			   const char *name,
			   struct module *owner,
			   int id);


#define to_iio_chrdev_minor_attr(_dev_attr) \
	container_of(_dev_attr, struct iio_chrdev_minor_attr, dev_attr);

/**
 * struct iio_dev_attr - iio specific device attribute
 * @dev_attr:	underlying device attribute
 * @address:	associated register address
 * @val2:	secondary attribute value
 */
struct iio_dev_attr {
	struct device_attribute dev_attr;
	int address;
	int val2;
};

#define to_iio_dev_attr(_dev_attr)				\
	container_of(_dev_attr, struct iio_dev_attr, dev_attr)

ssize_t iio_read_const_attr(struct device *dev,
			    struct device_attribute *attr,
			    char *len);

/**
 * struct iio_const_attr - constant device specific attribute
 *                         often used for things like available modes
 * @string:	attribute string
 * @dev_attr:	underlying device attribute
 */
struct iio_const_attr {
	const char *string;
	struct device_attribute dev_attr;
};

#define to_iio_const_attr(_dev_attr) \
	container_of(_dev_attr, struct iio_const_attr, dev_attr)

/* Some attributes will be hard coded (device dependent) and not require an
   address, in these cases pass a negative */
#define IIO_ATTR(_name, _mode, _show, _store, _addr)		\
	{ .dev_attr = __ATTR(_name, _mode, _show, _store),	\
	  .address = _addr }

#define IIO_ATTR_2(_name, _mode, _show, _store, _addr, _val2)	\
	{ .dev_attr = __ATTR(_name, _mode, _show, _store),	\
			.address = _addr,			\
			.val2 = _val2 }

#define IIO_DEVICE_ATTR(_name, _mode, _show, _store, _addr)	\
	struct iio_dev_attr iio_dev_attr_##_name		\
	= IIO_ATTR(_name, _mode, _show, _store, _addr)

#define IIO_DEVICE_ATTR_NAMED(_vname, _name, _mode, _show, _store, _addr) \
	struct iio_dev_attr iio_dev_attr_##_vname			\
	= IIO_ATTR(_name, _mode, _show, _store, _addr)

#define IIO_DEVICE_ATTR_2(_name, _mode, _show, _store, _addr, _val2)	\
	struct iio_dev_attr iio_dev_attr_##_name			\
	= IIO_ATTR_2(_name, _mode, _show, _store, _addr, _val2)

#define IIO_CONST_ATTR(_name, _string)					\
	struct iio_const_attr iio_const_attr_##_name			\
	= { .string = _string,						\
	    .dev_attr = __ATTR(_name, S_IRUGO, iio_read_const_attr, NULL)}

/* Generic attributes of onetype or another */

/**
 * IIO_DEV_ATTR_REV - revision number for the device
 * @_show: output method for the attribute
 *
 * Very much device dependent.
 **/
#define IIO_DEV_ATTR_REV(_show)			\
	IIO_DEVICE_ATTR(revision, S_IRUGO, _show, NULL, 0)

/**
 * IIO_DEV_ATTR_NAME - chip type dependent identifier
 * @_show: output method for the attribute
 **/
#define IIO_DEV_ATTR_NAME(_show)				\
	IIO_DEVICE_ATTR(name, S_IRUGO, _show, NULL, 0)

/**
 * IIO_DEV_ATTR_SAMP_FREQ - sets any internal clock frequency
 * @_mode: sysfs file mode/permissions
 * @_show: output method for the attribute
 * @_store: input method for the attribute
 **/
#define IIO_DEV_ATTR_SAMP_FREQ(_mode, _show, _store)			\
	IIO_DEVICE_ATTR(sampling_frequency, _mode, _show, _store, 0)

/**
 * IIO_DEV_ATTR_AVAIL_SAMP_FREQ - list available sampling frequencies
 * @_show: output method for the attribute
 *
 * May be mode dependent on some devices
 **/
/* Deprecated */
#define IIO_DEV_ATTR_AVAIL_SAMP_FREQ(_show)				\
	IIO_DEVICE_ATTR(available_sampling_frequency, S_IRUGO, _show, NULL, 0)

#define IIO_DEV_ATTR_SAMP_FREQ_AVAIL(_show)				\
	IIO_DEVICE_ATTR(sampling_frequency_available, S_IRUGO, _show, NULL, 0)
/**
 * IIO_CONST_ATTR_AVAIL_SAMP_FREQ - list available sampling frequencies
 * @_string: frequency string for the attribute
 *
 * Constant version
 **/
/* Deprecated */
#define IIO_CONST_ATTR_AVAIL_SAMP_FREQ(_string)			\
	IIO_CONST_ATTR(available_sampling_frequency, _string)

#define IIO_CONST_ATTR_SAMP_FREQ_AVAIL(_string)			\
	IIO_CONST_ATTR(sampling_frequency_available, _string)

/**
 * IIO_DEV_ATTR_SCAN_MODE - select a scan mode
 * @_mode: sysfs file mode/permissions
 * @_show: output method for the attribute
 * @_store: input method for the attribute
 *
 * This is used when only certain combinations of inputs may be read in one
 * scan.
 **/
#define IIO_DEV_ATTR_SCAN_MODE(_mode, _show, _store)		\
	IIO_DEVICE_ATTR(scan_mode, _mode, _show, _store, 0)

/**
 * IIO_DEV_ATTR_AVAIL_SCAN_MODES - list available scan modes
 * @_show: output method for the attribute
 **/
#define IIO_DEV_ATTR_AVAIL_SCAN_MODES(_show)				\
	IIO_DEVICE_ATTR(available_scan_modes, S_IRUGO, _show, NULL, 0)

/**
 * IIO_DEV_ATTR_SCAN - result of scan of multiple channels
 * @_show: output method for the attribute
 **/
#define IIO_DEV_ATTR_SCAN(_show)		\
	IIO_DEVICE_ATTR(scan, S_IRUGO, _show, NULL, 0);

/**
 * IIO_DEV_ATTR_INPUT - direct read of a single input channel
 * @_number: input channel number
 * @_show: output method for the attribute
 **/
#define IIO_DEV_ATTR_INPUT(_number, _show)				\
	IIO_DEVICE_ATTR(in##_number, S_IRUGO, _show, NULL, _number)

/**
 * IIO_DEV_ATTR_SW_RING_ENABLE - enable software ring buffer
 * @_show: output method for the attribute
 * @_store: input method for the attribute
 *
 * Success may be dependent on attachment of trigger previously.
 **/
#define IIO_DEV_ATTR_SW_RING_ENABLE(_show, _store)			\
	IIO_DEVICE_ATTR(sw_ring_enable, S_IRUGO | S_IWUSR, _show, _store, 0)

/**
 * IIO_DEV_ATTR_HW_RING_ENABLE - enable hardware ring buffer
 * @_show: output method for the attribute
 * @_store: input method for the attribute
 *
 * This is a different attribute from the software one as one can envision
 * schemes where a combination of the two may be used.
 **/
#define IIO_DEV_ATTR_HW_RING_ENABLE(_show, _store)			\
	IIO_DEVICE_ATTR(hw_ring_enable, S_IRUGO | S_IWUSR, _show, _store, 0)

/**
 * IIO_DEV_ATTR_BPSE - set number of bits per scan element
 * @_mode: sysfs file mode/permissions
 * @_show: output method for the attribute
 * @_store: input method for the attribute
 **/
#define IIO_DEV_ATTR_BPSE(_mode, _show, _store)		\
	IIO_DEVICE_ATTR(bpse, _mode, _show, _store, 0)

/**
 * IIO_DEV_ATTR_BPSE_AVAILABLE - number of bits per scan element supported
 * @_show: output method for the attribute
 **/
#define IIO_DEV_ATTR_BPSE_AVAILABLE(_show)				\
	IIO_DEVICE_ATTR(bpse_available, S_IRUGO, _show, NULL, 0)

/**
 * IIO_DEV_ATTR_TEMP - many sensors have auxiliary temperature sensors
 * @_show: output method for the attribute
 **/
#define IIO_DEV_ATTR_TEMP(_show)			\
	IIO_DEVICE_ATTR(temp, S_IRUGO, _show, NULL, 0)

#define IIO_DEV_ATTR_TEMP_RAW(_show)			\
	IIO_DEVICE_ATTR(temp_raw, S_IRUGO, _show, NULL, 0)

/**
 * IIO_EVENT_SH - generic shared event handler
 * @_name: event name
 * @_handler: handler function to be called
 *
 * This is used in cases where more than one event may result from a single
 * handler.  Often the case that some alarm register must be read and multiple
 * alarms may have been triggered.
 **/
#define IIO_EVENT_SH(_name, _handler)					\
	static struct iio_event_handler_list				\
	iio_event_##_name = {						\
		.handler = _handler,					\
		.refcount = 0,						\
		.exist_lock = __MUTEX_INITIALIZER(iio_event_##_name	\
						  .exist_lock),		\
		.list = {						\
			.next = &iio_event_##_name.list,		\
			.prev = &iio_event_##_name.list,		\
		},							\
	};

/**
 * IIO_EVENT_ATTR_SH - generic shared event attribute
 * @_name: event name
 * @_ev_list: event handler list
 * @_show: output method for the attribute
 * @_store: input method for the attribute
 * @_mask: mask used when detecting the event
 *
 * An attribute with an associated IIO_EVENT_SH
 **/
#define IIO_EVENT_ATTR_SH(_name, _ev_list, _show, _store, _mask)	\
	static struct iio_event_attr					\
	iio_event_attr_##_name						\
	= { .dev_attr = __ATTR(_name, S_IRUGO | S_IWUSR,		\
			       _show, _store),				\
	    .mask = _mask,						\
	    .listel = &_ev_list };

#define IIO_EVENT_ATTR_NAMED_SH(_vname, _name, _ev_list, _show, _store, _mask) \
	static struct iio_event_attr					\
	iio_event_attr_##_vname						\
	= { .dev_attr = __ATTR(_name, S_IRUGO | S_IWUSR,		\
			       _show, _store),				\
	    .mask = _mask,						\
	    .listel = &_ev_list };

/**
 * IIO_EVENT_ATTR - non-shared event attribute
 * @_name: event name
 * @_show: output method for the attribute
 * @_store: input method for the attribute
 * @_mask: mask used when detecting the event
 * @_handler: handler function to be called
 **/
#define IIO_EVENT_ATTR(_name, _show, _store, _mask, _handler)		\
	IIO_EVENT_SH(_name, _handler);					\
	static struct							\
	iio_event_attr							\
	iio_event_attr_##_name						\
	= { .dev_attr = __ATTR(_name, S_IRUGO | S_IWUSR,		\
			       _show, _store),				\
	    .mask = _mask,						\
	    .listel = &iio_event_##_name };				\

/**
 * IIO_EVENT_ATTR_DATA_RDY - event driven by data ready signal
 * @_show: output method for the attribute
 * @_store: input method for the attribute
 * @_mask: mask used when detecting the event
 * @_handler: handler function to be called
 *
 * Not typically implemented in devices where full triggering support
 * has been implemented.
 **/
#define IIO_EVENT_ATTR_DATA_RDY(_show, _store, _mask, _handler) \
	IIO_EVENT_ATTR(data_rdy, _show, _store, _mask, _handler)

#define IIO_EVENT_CODE_DATA_RDY		100
#define IIO_EVENT_CODE_RING_BASE	200
#define IIO_EVENT_CODE_ACCEL_BASE	300
#define IIO_EVENT_CODE_GYRO_BASE	400
#define IIO_EVENT_CODE_ADC_BASE		500
#define IIO_EVENT_CODE_MISC_BASE	600
#define IIO_EVENT_CODE_LIGHT_BASE	700

#define IIO_EVENT_CODE_DEVICE_SPECIFIC	1000

/**
 * IIO_EVENT_ATTR_RING_50_FULL - ring buffer event to indicate 50% full
 * @_show: output method for the attribute
 * @_store: input method for the attribute
 * @_mask: mask used when detecting the event
 * @_handler: handler function to be called
 **/
#define IIO_EVENT_ATTR_RING_50_FULL(_show, _store, _mask, _handler)	\
	IIO_EVENT_ATTR(ring_50_full, _show, _store, _mask, _handler)

/**
 * IIO_EVENT_ATTR_RING_50_FULL_SH - shared ring event to indicate 50% full
 * @_evlist: event handler list
 * @_show: output method for the attribute
 * @_store: input method for the attribute
 * @_mask: mask used when detecting the event
 **/
#define IIO_EVENT_ATTR_RING_50_FULL_SH(_evlist, _show, _store, _mask)	\
	IIO_EVENT_ATTR_SH(ring_50_full, _evlist, _show, _store, _mask)

/**
 * IIO_EVENT_ATTR_RING_75_FULL_SH - shared ring event to indicate 75% full
 * @_evlist: event handler list
 * @_show: output method for the attribute
 * @_store: input method for the attribute
 * @_mask: mask used when detecting the event
 **/
#define IIO_EVENT_ATTR_RING_75_FULL_SH(_evlist, _show, _store, _mask)	\
	IIO_EVENT_ATTR_SH(ring_75_full, _evlist, _show, _store, _mask)

#define IIO_EVENT_CODE_RING_50_FULL	IIO_EVENT_CODE_RING_BASE
#define IIO_EVENT_CODE_RING_75_FULL	(IIO_EVENT_CODE_RING_BASE + 1)
#define IIO_EVENT_CODE_RING_100_FULL	(IIO_EVENT_CODE_RING_BASE + 2)

#endif /* _INDUSTRIAL_IO_SYSFS_H_ */
