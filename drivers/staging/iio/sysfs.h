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

#define IIO_CONST_ATTR_NAMED(_vname, _name, _string)			\
	struct iio_const_attr iio_const_attr_##_vname			\
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
 * IIO_DEV_ATTR_RESET: resets the device
 **/
#define IIO_DEV_ATTR_RESET(_store)			\
	IIO_DEVICE_ATTR(reset, S_IWUSR, NULL, _store, 0)

/**
 * IIO_CONST_ATTR_NAME - constant identifier
 * @_string: the name
 **/
#define IIO_CONST_ATTR_NAME(_string)				\
	IIO_CONST_ATTR(name, _string)

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
#define IIO_CONST_ATTR_SAMP_FREQ_AVAIL(_string)			\
	IIO_CONST_ATTR(sampling_frequency_available, _string)

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

#define IIO_DEV_ATTR_TEMP_RAW(_show)			\
	IIO_DEVICE_ATTR(temp_raw, S_IRUGO, _show, NULL, 0)

#define IIO_CONST_ATTR_TEMP_OFFSET(_string)		\
	IIO_CONST_ATTR(temp_offset, _string)

#define IIO_CONST_ATTR_TEMP_SCALE(_string)		\
	IIO_CONST_ATTR(temp_scale, _string)

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

#define IIO_EV_CLASS_BUFFER		0
#define IIO_EV_CLASS_IN			1
#define IIO_EV_CLASS_ACCEL		2
#define IIO_EV_CLASS_GYRO		3
#define IIO_EV_CLASS_MAGN		4
#define IIO_EV_CLASS_LIGHT		5
#define IIO_EV_CLASS_PROXIMITY		6

#define IIO_EV_MOD_X			0
#define IIO_EV_MOD_Y			1
#define IIO_EV_MOD_Z			2
#define IIO_EV_MOD_X_AND_Y		3
#define IIO_EV_MOD_X_ANX_Z		4
#define IIO_EV_MOD_Y_AND_Z		5
#define IIO_EV_MOD_X_AND_Y_AND_Z	6
#define IIO_EV_MOD_X_OR_Y		7
#define IIO_EV_MOD_X_OR_Z		8
#define IIO_EV_MOD_Y_OR_Z		9
#define IIO_EV_MOD_X_OR_Y_OR_Z		10

#define IIO_EV_TYPE_THRESH		0
#define IIO_EV_TYPE_MAG			1
#define IIO_EV_TYPE_ROC			2

#define IIO_EV_DIR_EITHER		0
#define IIO_EV_DIR_RISING		1
#define IIO_EV_DIR_FALLING		2

#define IIO_EVENT_CODE(channelclass, orient_bit, number,		\
		       modifier, type, direction)			\
	(channelclass | (orient_bit << 8) | ((number) << 9) |		\
	 ((modifier) << 13) | ((type) << 16) | ((direction) << 24))

#define IIO_MOD_EVENT_CODE(channelclass, number, modifier,		\
			   type, direction)				\
	IIO_EVENT_CODE(channelclass, 1, number, modifier, type, direction)

#define IIO_UNMOD_EVENT_CODE(channelclass, number, type, direction)	\
	IIO_EVENT_CODE(channelclass, 0, number, 0, type, direction)


#define IIO_BUFFER_EVENT_CODE(code)		\
	(IIO_EV_CLASS_BUFFER | (code << 8))

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

#define IIO_EVENT_CODE_RING_50_FULL	IIO_BUFFER_EVENT_CODE(0)
#define IIO_EVENT_CODE_RING_75_FULL	IIO_BUFFER_EVENT_CODE(1)
#define IIO_EVENT_CODE_RING_100_FULL	IIO_BUFFER_EVENT_CODE(2)

#endif /* _INDUSTRIAL_IO_SYSFS_H_ */
