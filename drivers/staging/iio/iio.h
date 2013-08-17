
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
#include "types.h"
/* IIO TODO LIST */
/*
 * Provide means of adjusting timer accuracy.
 * Currently assumes nano seconds.
 */

enum iio_data_type {
	IIO_RAW,
	IIO_PROCESSED,
};

/* Could add the raw attributes as well - allowing buffer only devices */
enum iio_chan_info_enum {
	/* 0 is reserved for raw attributes */
	IIO_CHAN_INFO_SCALE = 1,
	IIO_CHAN_INFO_OFFSET,
	IIO_CHAN_INFO_CALIBSCALE,
	IIO_CHAN_INFO_CALIBBIAS,
	IIO_CHAN_INFO_PEAK,
	IIO_CHAN_INFO_PEAK_SCALE,
	IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW,
	IIO_CHAN_INFO_AVERAGE_RAW,
	IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY,
};

#define IIO_CHAN_INFO_SHARED_BIT(type) BIT(type*2)
#define IIO_CHAN_INFO_SEPARATE_BIT(type) BIT(type*2 + 1)

#define IIO_CHAN_INFO_SCALE_SEPARATE_BIT		\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_SCALE)
#define IIO_CHAN_INFO_SCALE_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_SCALE)
#define IIO_CHAN_INFO_OFFSET_SEPARATE_BIT			\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_OFFSET)
#define IIO_CHAN_INFO_OFFSET_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_OFFSET)
#define IIO_CHAN_INFO_CALIBSCALE_SEPARATE_BIT			\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_CALIBSCALE)
#define IIO_CHAN_INFO_CALIBSCALE_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_CALIBSCALE)
#define IIO_CHAN_INFO_CALIBBIAS_SEPARATE_BIT			\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_CALIBBIAS)
#define IIO_CHAN_INFO_CALIBBIAS_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_CALIBBIAS)
#define IIO_CHAN_INFO_PEAK_SEPARATE_BIT			\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_PEAK)
#define IIO_CHAN_INFO_PEAK_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_PEAK)
#define IIO_CHAN_INFO_PEAKSCALE_SEPARATE_BIT			\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_PEAKSCALE)
#define IIO_CHAN_INFO_PEAKSCALE_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_PEAKSCALE)
#define IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW_SEPARATE_BIT	\
	IIO_CHAN_INFO_SEPARATE_BIT(				\
		IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW)
#define IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW_SHARED_BIT	\
	IIO_CHAN_INFO_SHARED_BIT(				\
		IIO_CHAN_INFO_QUADRATURE_CORRECTION_RAW)
#define IIO_CHAN_INFO_AVERAGE_RAW_SEPARATE_BIT			\
	IIO_CHAN_INFO_SEPARATE_BIT(IIO_CHAN_INFO_AVERAGE_RAW)
#define IIO_CHAN_INFO_AVERAGE_RAW_SHARED_BIT			\
	IIO_CHAN_INFO_SHARED_BIT(IIO_CHAN_INFO_AVERAGE_RAW)
#define IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SHARED_BIT \
	IIO_CHAN_INFO_SHARED_BIT(			       \
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY)
#define IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY_SEPARATE_BIT \
	IIO_CHAN_INFO_SEPARATE_BIT(			       \
		IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY)

enum iio_endian {
	IIO_CPU,
	IIO_BE,
	IIO_LE,
};

struct iio_chan_spec;
struct iio_dev;

/**
 * struct iio_chan_spec_ext_info - Extended channel info attribute
 * @name:	Info attribute name
 * @shared:	Whether this attribute is shared between all channels.
 * @read:	Read callback for this info attribute, may be NULL.
 * @write:	Write callback for this info attribute, may be NULL.
 */
struct iio_chan_spec_ext_info {
	const char *name;
	bool shared;
	ssize_t (*read)(struct iio_dev *, struct iio_chan_spec const *,
			char *buf);
	ssize_t (*write)(struct iio_dev *, struct iio_chan_spec const *,
			const char *buf, size_t len);
};

/**
 * struct iio_chan_spec - specification of a single channel
 * @type:		What type of measurement is the channel making.
 * @channel:		What number or name do we wish to assign the channel.
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
 *			endianness:	little or big endian
 * @info_mask:		What information is to be exported about this channel.
 *			This includes calibbias, scale etc.
 * @event_mask:	What events can this channel produce.
 * @ext_info:		Array of extended info attributes for this channel.
 *			The array is NULL terminated, the last element should
 *			have it's name field set to NULL.
 * @extend_name:	Allows labeling of channel attributes with an
 *			informative name. Note this has no effect codes etc,
 *			unlike modifiers.
 * @datasheet_name:	A name used in in kernel mapping of channels. It should
 *			correspond to the first name that the channel is referred
 *			to by in the datasheet (e.g. IND), or the nearest
 *			possible compound name (e.g. IND-INC).
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
 * @differential:	Channel is differential.
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
		enum iio_endian endianness;
	} scan_type;
	long			info_mask;
	long			event_mask;
	const struct iio_chan_spec_ext_info *ext_info;
	char			*extend_name;
	const char		*datasheet_name;
	unsigned		processed_val:1;
	unsigned		modified:1;
	unsigned		indexed:1;
	unsigned		output:1;
	unsigned		differential:1;
};

#define IIO_ST(si, rb, sb, sh)						\
	{ .sign = si, .realbits = rb, .storagebits = sb, .shift = sh }

/* Macro assumes input channels */
#define IIO_CHAN(_type, _mod, _indexed, _proc, _name, _chan, _chan2, \
		 _inf_mask, _address, _si, _stype, _event_mask)		\
	{ .type = _type,						\
	  .output = 0,							\
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
#define INDIO_BUFFER_TRIGGERED		0x02
#define INDIO_BUFFER_HARDWARE		0x08

#define INDIO_ALL_BUFFER_MODES					\
	(INDIO_BUFFER_TRIGGERED | INDIO_BUFFER_HARDWARE)

struct iio_trigger; /* forward declaration */
struct iio_dev;

/**
 * struct iio_info - constant information about device
 * @driver_module:	module structure used to ensure correct
 *			ownership of chrdevs etc
 * @event_attrs:	event control attributes
 * @attrs:		general purpose device attributes
 * @read_raw:		function to request a value from the device.
 *			mask specifies which value. Note 0 means a reading of
 *			the channel in question.  Return value will specify the
 *			type of value returned by the device. val and val2 will
 *			contain the elements making up the returned value.
 * @write_raw:		function to write a value to the device.
 *			Parameters are the same as for read_raw.
 * @write_raw_get_fmt:	callback function to query the expected
 *			format/precision. If not set by the driver, write_raw
 *			returns IIO_VAL_INT_PLUS_MICRO.
 * @read_event_config:	find out if the event is enabled.
 * @write_event_config:	set if the event is enabled.
 * @read_event_value:	read a value associated with the event. Meaning
 *			is event dependant. event_code specifies which event.
 * @write_event_value:	write the value associated with the event.
 *			Meaning is event dependent.
 * @validate_trigger:	function to validate the trigger when the
 *			current trigger gets changed.
 **/
struct iio_info {
	struct module			*driver_module;
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

	int (*write_raw_get_fmt)(struct iio_dev *indio_dev,
			 struct iio_chan_spec const *chan,
			 long mask);

	int (*read_event_config)(struct iio_dev *indio_dev,
				 u64 event_code);

	int (*write_event_config)(struct iio_dev *indio_dev,
				  u64 event_code,
				  int state);

	int (*read_event_value)(struct iio_dev *indio_dev,
				u64 event_code,
				int *val);
	int (*write_event_value)(struct iio_dev *indio_dev,
				 u64 event_code,
				 int val);
	int (*validate_trigger)(struct iio_dev *indio_dev,
				struct iio_trigger *trig);
	int (*update_scan_mode)(struct iio_dev *indio_dev,
				const unsigned long *scan_mask);
	int (*debugfs_reg_access)(struct iio_dev *indio_dev,
				  unsigned reg, unsigned writeval,
				  unsigned *readval);
};

/**
 * struct iio_buffer_setup_ops - buffer setup related callbacks
 * @preenable:		[DRIVER] function to run prior to marking buffer enabled
 * @postenable:		[DRIVER] function to run after marking buffer enabled
 * @predisable:		[DRIVER] function to run prior to marking buffer
 *			disabled
 * @postdisable:	[DRIVER] function to run after marking buffer disabled
 */
struct iio_buffer_setup_ops {
	int				(*preenable)(struct iio_dev *);
	int				(*postenable)(struct iio_dev *);
	int				(*predisable)(struct iio_dev *);
	int				(*postdisable)(struct iio_dev *);
};

/**
 * struct iio_dev - industrial I/O device
 * @id:			[INTERN] used to identify device internally
 * @modes:		[DRIVER] operating modes supported by device
 * @currentmode:	[DRIVER] current operating mode
 * @dev:		[DRIVER] device structure, should be assigned a parent
 *			and owner
 * @event_interface:	[INTERN] event chrdevs associated with interrupt lines
 * @buffer:		[DRIVER] any buffer present
 * @mlock:		[INTERN] lock used to prevent simultaneous device state
 *			changes
 * @available_scan_masks: [DRIVER] optional array of allowed bitmasks
 * @masklength:		[INTERN] the length of the mask established from
 *			channels
 * @active_scan_mask:	[INTERN] union of all scan masks requested by buffers
 * @trig:		[INTERN] current device trigger (buffer modes)
 * @pollfunc:		[DRIVER] function run on trigger being received
 * @channels:		[DRIVER] channel specification structure table
 * @num_channels:	[DRIVER] number of chanels specified in @channels.
 * @channel_attr_list:	[INTERN] keep track of automatically created channel
 *			attributes
 * @chan_attr_group:	[INTERN] group for all attrs in base directory
 * @name:		[DRIVER] name of the device.
 * @info:		[DRIVER] callbacks and constant info from driver
 * @info_exist_lock:	[INTERN] lock to prevent use during removal
 * @chrdev:		[INTERN] associated character device
 * @groups:		[INTERN] attribute groups
 * @groupcounter:	[INTERN] index of next attribute group
 * @flags:		[INTERN] file ops related flags including busy flag.
 * @debugfs_dentry:	[INTERN] device specific debugfs dentry.
 * @cached_reg_addr:	[INTERN] cached register address for debugfs reads.
 */
struct iio_dev {
	int				id;

	int				modes;
	int				currentmode;
	struct device			dev;

	struct iio_event_interface	*event_interface;

	struct iio_buffer		*buffer;
	struct mutex			mlock;

	const unsigned long		*available_scan_masks;
	unsigned			masklength;
	const unsigned long		*active_scan_mask;
	struct iio_trigger		*trig;
	struct iio_poll_func		*pollfunc;

	struct iio_chan_spec const	*channels;
	int				num_channels;

	struct list_head		channel_attr_list;
	struct attribute_group		chan_attr_group;
	const char			*name;
	const struct iio_info		*info;
	struct mutex			info_exist_lock;
	const struct iio_buffer_setup_ops	*setup_ops;
	struct cdev			chrdev;
#define IIO_MAX_GROUPS 6
	const struct attribute_group	*groups[IIO_MAX_GROUPS + 1];
	int				groupcounter;

	unsigned long			flags;
#if defined(CONFIG_DEBUG_FS)
	struct dentry			*debugfs_dentry;
	unsigned			cached_reg_addr;
#endif
};

/**
 * iio_find_channel_from_si() - get channel from its scan index
 * @indio_dev:		device
 * @si:			scan index to match
 */
const struct iio_chan_spec
*iio_find_channel_from_si(struct iio_dev *indio_dev, int si);

/**
 * iio_device_register() - register a device with the IIO subsystem
 * @indio_dev:		Device structure filled by the device driver
 **/
int iio_device_register(struct iio_dev *indio_dev);

/**
 * iio_device_unregister() - unregister a device from the IIO subsystem
 * @indio_dev:		Device structure representing the device.
 **/
void iio_device_unregister(struct iio_dev *indio_dev);

/**
 * iio_push_event() - try to add event to the list for userspace reading
 * @indio_dev:		IIO device structure
 * @ev_code:		What event
 * @timestamp:		When the event occurred
 **/
int iio_push_event(struct iio_dev *indio_dev, u64 ev_code, s64 timestamp);

extern struct bus_type iio_bus_type;

/**
 * iio_put_device() - reference counted deallocation of struct device
 * @dev: the iio_device containing the device
 **/
static inline void iio_put_device(struct iio_dev *indio_dev)
{
	if (indio_dev)
		put_device(&indio_dev->dev);
};

/* Can we make this smaller? */
#define IIO_ALIGN L1_CACHE_BYTES
/**
 * iio_allocate_device() - allocate an iio_dev from a driver
 * @sizeof_priv: Space to allocate for private structure.
 **/
struct iio_dev *iio_allocate_device(int sizeof_priv);

static inline void *iio_priv(const struct iio_dev *indio_dev)
{
	return (char *)indio_dev + ALIGN(sizeof(struct iio_dev), IIO_ALIGN);
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
void iio_free_device(struct iio_dev *indio_dev);

/**
 * iio_buffer_enabled() - helper function to test if the buffer is enabled
 * @indio_dev:		IIO device info structure for device
 **/
static inline bool iio_buffer_enabled(struct iio_dev *indio_dev)
{
	return indio_dev->currentmode
		& (INDIO_BUFFER_TRIGGERED | INDIO_BUFFER_HARDWARE);
};

/**
 * iio_get_debugfs_dentry() - helper function to get the debugfs_dentry
 * @indio_dev:		IIO device info structure for device
 **/
#if defined(CONFIG_DEBUG_FS)
static inline struct dentry *iio_get_debugfs_dentry(struct iio_dev *indio_dev)
{
	return indio_dev->debugfs_dentry;
};
#else
static inline struct dentry *iio_get_debugfs_dentry(struct iio_dev *indio_dev)
{
	return NULL;
};
#endif

#endif /* _INDUSTRIAL_IO_H_ */
