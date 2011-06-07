/* The industrial I/O core
 *
 * Copyright (c) 2008 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#ifndef _INDUSTRIAL_IO_H_
#define _INDUSTRIAL_IO_H_

#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/irq.h>
#include "sysfs.h"
#include "chrdev.h"

/* IIO TODO LIST */
/*
 * Provide means of adjusting timer accuracy.
 * Currently assumes nano seconds.
 */

/* Event interface flags */
#define IIO_BUSY_BIT_POS 1

/* naughty temporary hack to match these against the event version
   - need to flattern these together */
enum iio_chan_type {
	/* real channel types */
	IIO_IN,
	IIO_CURRENT,
	IIO_POWER,
	IIO_ACCEL,
	IIO_IN_DIFF,
	IIO_GYRO,
	IIO_MAGN,
	IIO_LIGHT,
	IIO_INTENSITY,
	IIO_PROXIMITY,
	IIO_TEMP,
	IIO_INCLI,
	IIO_ROT,
	IIO_ANGL,
	IIO_TIMESTAMP,
};

#define IIO_MOD_X			0
#define IIO_MOD_LIGHT_BOTH		0
#define IIO_MOD_Y			1
#define IIO_MOD_LIGHT_IR		1
#define IIO_MOD_Z			2
#define IIO_MOD_X_AND_Y			3
#define IIO_MOD_X_ANX_Z			4
#define IIO_MOD_Y_AND_Z			5
#define IIO_MOD_X_AND_Y_AND_Z		6
#define IIO_MOD_X_OR_Y			7
#define IIO_MOD_X_OR_Z			8
#define IIO_MOD_Y_OR_Z			9
#define IIO_MOD_X_OR_Y_OR_Z		10

/* Could add the raw attributes as well - allowing buffer only devices */
enum iio_chan_info_enum {
	IIO_CHAN_INFO_SCALE_SHARED,
	IIO_CHAN_INFO_SCALE_SEPARATE,
	IIO_CHAN_INFO_OFFSET_SHARED,
	IIO_CHAN_INFO_OFFSET_SEPARATE,
	IIO_CHAN_INFO_CALIBSCALE_SHARED,
	IIO_CHAN_INFO_CALIBSCALE_SEPARATE,
	IIO_CHAN_INFO_CALIBBIAS_SHARED,
	IIO_CHAN_INFO_CALIBBIAS_SEPARATE,
	IIO_CHAN_INFO_PEAK_SHARED,
	IIO_CHAN_INFO_PEAK_SEPARATE,
	IIO_CHAN_INFO_PEAK_SCALE_SHARED,
	IIO_CHAN_INFO_PEAK_SCALE_SEPARATE,
};

/**
 * struct iio_chan_spec - specification of a single channel
 * @type:		What type of measurement is the channel making.
 * @channel:		What number or name do we wish to asign the channel.
 * @channel2:		If there is a second number for a differential
 *			channel then this is it. If modified is set then the
 *			value here specifies the modifier.
 * @address:		Driver specific identifier.
 * @scan_index:	Monotonic index to give ordering in scans when read
 *			from a buffer.
 * @scan_type:		Sign:		's' or 'u' to specify signed or unsigned
 *			realbits:	Number of valid bits of data
 *			storage_bits:	Realbits + padding
 *			shift:		Shift right by this before masking out
 *					realbits.
 * @info_mask:		What information is to be exported about this channel.
 *			This includes calibbias, scale etc.
 * @event_mask:	What events can this channel produce.
 * @extend_name:	Allows labeling of channel attributes with an
 *			informative name. Note this has no effect codes etc,
 *			unlike modifiers.
 * @processed_val:	Flag to specify the data access attribute should be
 *			*_input rather than *_raw.
 * @modified:		Does a modifier apply to this channel. What these are
 *			depends on the channel type.  Modifier is set in
 *			channel2. Examples are IIO_MOD_X for axial sensors about
 *			the 'x' axis.
 * @indexed:		Specify the channel has a numerical index. If not,
 *			the value in channel will be suppressed for attribute
 *			but not for event codes. Typically set it to 0 when
 *			the index is false.
 */
struct iio_chan_spec {
	enum iio_chan_type	type;
	int			channel;
	int			channel2;
	unsigned long		address;
	int			scan_index;
	struct {
		char	sign;
		u8	realbits;
		u8	storagebits;
		u8	shift;
	} scan_type;
	const long		info_mask;
	const long		event_mask;
	const char		*extend_name;
	unsigned		processed_val:1;
	unsigned		modified:1;
	unsigned		indexed:1;
};
/* Meant for internal use only */
void __iio_device_attr_deinit(struct device_attribute *dev_attr);
int __iio_device_attr_init(struct device_attribute *dev_attr,
			   const char *postfix,
			   struct iio_chan_spec const *chan,
			   ssize_t (*readfunc)(struct device *dev,
					       struct device_attribute *attr,
					       char *buf),
			   ssize_t (*writefunc)(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t len),
			   bool generic);
#define IIO_ST(si, rb, sb, sh)						\
	{ .sign = si, .realbits = rb, .storagebits = sb, .shift = sh }

#define IIO_CHAN(_type, _mod, _indexed, _proc, _name, _chan, _chan2,	\
		 _inf_mask, _address, _si, _stype, _event_mask)		\
	{ .type = _type,						\
	  .modified = _mod,						\
	  .indexed = _indexed,						\
	  .processed_val = _proc,					\
	  .extend_name = _name,						\
	  .channel = _chan,						\
	  .channel2 = _chan2,						\
	  .info_mask = _inf_mask,					\
	  .address = _address,						\
	  .scan_index = _si,						\
	  .scan_type = _stype,						\
	  .event_mask = _event_mask }

#define IIO_CHAN_SOFT_TIMESTAMP(_si)					\
	{ .type = IIO_TIMESTAMP, .channel = -1,				\
			.scan_index = _si, .scan_type = IIO_ST('s', 64, 64, 0) }

int __iio_add_chan_devattr(const char *postfix,
			   const char *group,
			   struct iio_chan_spec const *chan,
			   ssize_t (*func)(struct device *dev,
					   struct device_attribute *attr,
					   char *buf),
			   ssize_t (*writefunc)(struct device *dev,
						struct device_attribute *attr,
						const char *buf,
						size_t len),
			   int mask,
			   bool generic,
			   struct device *dev,
			   struct list_head *attr_list);
/**
 * iio_get_time_ns() - utility function to get a time stamp for events etc
 **/
static inline s64 iio_get_time_ns(void)
{
	struct timespec ts;
	/*
	 * calls getnstimeofday.
	 * If hrtimers then up to ns accurate, if not microsecond.
	 */
	ktime_get_real_ts(&ts);

	return timespec_to_ns(&ts);
}

/* Device operating modes */
#define INDIO_DIRECT_MODE		0x01
#define INDIO_RING_TRIGGERED		0x02
#define INDIO_RING_HARDWARE_BUFFER	0x08

#define INDIO_ALL_RING_MODES (INDIO_RING_TRIGGERED | INDIO_RING_HARDWARE_BUFFER)

/* Vast majority of this is set by the industrialio subsystem on a
 * call to iio_device_register. */
#define IIO_VAL_INT 1
#define IIO_VAL_INT_PLUS_MICRO 2

/**
 * struct iio_info - constant information about device
 * @driver_module:	module structure used to ensure correct
 *			ownership of chrdevs etc
 * @num_interrupt_lines:number of physical interrupt lines from device
 * @event_attrs:	event control attributes
 * @attrs:		general purpose device attributes
 * @read_raw:		function to request a value from the device.
 *			mask specifies which value. Note 0 means a reading of
 *			the channel in question.  Return value will specify the
 *			type of value returned by the device. val and val2 will
 *			contain the elements making up the returned value.
 * @write_raw:		function to write a value to the device.
 *			Parameters are the same as for read_raw.
 * @read_event_config:	find out if the event is enabled.
 * @write_event_config:	set if the event is enabled.
 * @read_event_value:	read a value associated with the event. Meaning
 *			is event dependant. event_code specifies which event.
 * @write_event_value:	write the value associate with the event.
 *			Meaning is event dependent.
 **/
struct iio_info {
	struct module			*driver_module;
	int				num_interrupt_lines;
	struct attribute_group		*event_attrs;
	const struct attribute_group	*attrs;

	int (*read_raw)(struct iio_dev *indio_dev,
			struct iio_chan_spec const *chan,
			int *val,
			int *val2,
			long mask);

	int (*write_raw)(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan,
			 int val,
			 int val2,
			 long mask);

	int (*read_event_config)(struct iio_dev *indio_dev,
				 int event_code);

	int (*write_event_config)(struct iio_dev *indio_dev,
				  int event_code,
				  int state);

	int (*read_event_value)(struct iio_dev *indio_dev,
				int event_code,
				int *val);
	int (*write_event_value)(struct iio_dev *indio_dev,
				 int event_code,
				 int val);
};

/**
 * struct iio_dev - industrial I/O device
 * @id:			[INTERN] used to identify device internally
 * @dev_data:		[DRIVER] device specific data
 * @modes:		[DRIVER] operating modes supported by device
 * @currentmode:	[DRIVER] current operating mode
 * @dev:		[DRIVER] device structure, should be assigned a parent
 *			and owner
 * @event_interfaces:	[INTERN] event chrdevs associated with interrupt lines
 * @ring:		[DRIVER] any ring buffer present
 * @mlock:		[INTERN] lock used to prevent simultaneous device state
 *			changes
 * @available_scan_masks: [DRIVER] optional array of allowed bitmasks
 * @trig:		[INTERN] current device trigger (ring buffer modes)
 * @pollfunc:		[DRIVER] function run on trigger being received
 * @channels:		[DRIVER] channel specification structure table
 * @num_channels:	[DRIVER] number of chanels specified in @channels.
 * @channel_attr_list:	[INTERN] keep track of automatically created channel
 *			attributes.
 * @name:		[DRIVER] name of the device.
 **/
struct iio_dev {
	int				id;
	void				*dev_data;
	int				modes;
	int				currentmode;
	struct device			dev;

	struct iio_event_interface	*event_interfaces;

	struct iio_ring_buffer		*ring;
	struct mutex			mlock;

	u32				*available_scan_masks;
	struct iio_trigger		*trig;
	struct iio_poll_func		*pollfunc;

	struct iio_chan_spec const *channels;
	int num_channels;

	struct list_head channel_attr_list;
	const char *name;
	const struct iio_info *info;
};

/**
 * iio_device_register() - register a device with the IIO subsystem
 * @dev_info:		Device structure filled by the device driver
 **/
int iio_device_register(struct iio_dev *dev_info);

/**
 * iio_device_unregister() - unregister a device from the IIO subsystem
 * @dev_info:		Device structure representing the device.
 **/
void iio_device_unregister(struct iio_dev *dev_info);

/**
 * iio_push_event() - try to add event to the list for userspace reading
 * @dev_info:		IIO device structure
 * @ev_line:		Which event line (hardware interrupt)
 * @ev_code:		What event
 * @timestamp:		When the event occurred
 **/
int iio_push_event(struct iio_dev *dev_info,
		  int ev_line,
		  int ev_code,
		  s64 timestamp);

/* Used to distinguish between bipolar and unipolar scan elemenents.
 * Whilst this may seem obvious, we may well want to change the representation
 * in the future!*/
#define IIO_SIGNED(a) -(a)
#define IIO_UNSIGNED(a) (a)

extern dev_t iio_devt;
extern struct bus_type iio_bus_type;

/**
 * iio_put_device() - reference counted deallocation of struct device
 * @dev: the iio_device containing the device
 **/
static inline void iio_put_device(struct iio_dev *dev)
{
	if (dev)
		put_device(&dev->dev);
};

/**
 * to_iio_dev() - get iio_dev for which we have the struct device
 * @d: the struct device
 **/
static inline struct iio_dev *to_iio_dev(struct device *d)
{
	return container_of(d, struct iio_dev, dev);
};

/**
 * iio_dev_get_devdata() - helper function gets device specific data
 * @d: the iio_dev associated with the device
 **/
static inline void *iio_dev_get_devdata(struct iio_dev *d)
{
	return d->dev_data;
}


/* Can we make this smaller? */
#define IIO_ALIGN L1_CACHE_BYTES
/**
 * iio_allocate_device() - allocate an iio_dev from a driver
 * @sizeof_priv: Space to allocate for private structure.
 **/
struct iio_dev *iio_allocate_device(int sizeof_priv);

static inline void *iio_priv(const struct iio_dev *dev)
{
	return (char *)dev + ALIGN(sizeof(struct iio_dev), IIO_ALIGN);
}

static inline struct iio_dev *iio_priv_to_dev(void *priv)
{
	return (struct iio_dev *)((char *)priv -
				  ALIGN(sizeof(struct iio_dev), IIO_ALIGN));
}

/**
 * iio_free_device() - free an iio_dev from a driver
 * @dev: the iio_dev associated with the device
 **/
void iio_free_device(struct iio_dev *dev);

/**
 * iio_put() - internal module reference count reduce
 **/
void iio_put(void);

/**
 * iio_get() - internal module reference count increase
 **/
void iio_get(void);

/**
 * iio_device_get_chrdev_minor() - get an unused minor number
 **/
int iio_device_get_chrdev_minor(void);
void iio_device_free_chrdev_minor(int val);

/**
 * iio_ring_enabled() - helper function to test if any form of ring is enabled
 * @dev_info:		IIO device info structure for device
 **/
static inline bool iio_ring_enabled(struct iio_dev *dev_info)
{
	return dev_info->currentmode
		& (INDIO_RING_TRIGGERED
		   | INDIO_RING_HARDWARE_BUFFER);
};

struct ida;

int iio_get_new_ida_val(struct ida *this_ida);
void iio_free_ida_val(struct ida *this_ida, int id);
#endif /* _INDUSTRIAL_IO_H_ */
