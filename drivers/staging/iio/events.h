/* The industrial I/O - event passing to userspace
 *
 * Copyright (c) 2008-2011 Jonathan Cameron
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef _IIO_EVENTS_H_
#define _IIO_EVENTS_H_

#include <linux/ioctl.h>
#include <linux/types.h>
#include "types.h"

/**
 * struct iio_event_data - The actual event being pushed to userspace
 * @id:		event identifier
 * @timestamp:	best estimate of time of event occurrence (often from
 *		the interrupt handler)
 */
struct iio_event_data {
	__u64	id;
	__s64	timestamp;
};

#define IIO_GET_EVENT_FD_IOCTL _IOR('i', 0x90, int)

enum iio_event_type {
	IIO_EV_TYPE_THRESH,
	IIO_EV_TYPE_MAG,
	IIO_EV_TYPE_ROC,
	IIO_EV_TYPE_THRESH_ADAPTIVE,
	IIO_EV_TYPE_MAG_ADAPTIVE,
};

enum iio_event_direction {
	IIO_EV_DIR_EITHER,
	IIO_EV_DIR_RISING,
	IIO_EV_DIR_FALLING,
};

/**
 * IIO_EVENT_CODE() - create event identifier
 * @chan_type:	Type of the channel. Should be one of enum iio_chan_type.
 * @diff:	Whether the event is for an differential channel or not.
 * @modifier:	Modifier for the channel. Should be one of enum iio_modifier.
 * @direction:	Direction of the event. One of enum iio_event_direction.
 * @type:	Type of the event. Should be one enum iio_event_type.
 * @chan:	Channel number for non-differential channels.
 * @chan1:	First channel number for differential channels.
 * @chan2:	Second channel number for differential channels.
 */

#define IIO_EVENT_CODE(chan_type, diff, modifier, direction,		\
		       type, chan, chan1, chan2)			\
	(((u64)type << 56) | ((u64)diff << 55) |			\
	 ((u64)direction << 48) | ((u64)modifier << 40) |		\
	 ((u64)chan_type << 32) | (((u16)chan2) << 16) | ((u16)chan1) | \
	 ((u16)chan))


#define IIO_EV_DIR_MAX 4
#define IIO_EV_BIT(type, direction)			\
	(1 << (type*IIO_EV_DIR_MAX + direction))

/**
 * IIO_MOD_EVENT_CODE() - create event identifier for modified channels
 * @chan_type:	Type of the channel. Should be one of enum iio_chan_type.
 * @number:	Channel number.
 * @modifier:	Modifier for the channel. Should be one of enum iio_modifier.
 * @type:	Type of the event. Should be one enum iio_event_type.
 * @direction:	Direction of the event. One of enum iio_event_direction.
 */

#define IIO_MOD_EVENT_CODE(chan_type, number, modifier,		\
			   type, direction)				\
	IIO_EVENT_CODE(chan_type, 0, modifier, direction, type, number, 0, 0)

/**
 * IIO_UNMOD_EVENT_CODE() - create event identifier for unmodified channels
 * @chan_type:	Type of the channel. Should be one of enum iio_chan_type.
 * @number:	Channel number.
 * @type:	Type of the event. Should be one enum iio_event_type.
 * @direction:	Direction of the event. One of enum iio_event_direction.
 */

#define IIO_UNMOD_EVENT_CODE(chan_type, number, type, direction)	\
	IIO_EVENT_CODE(chan_type, 0, 0, direction, type, number, 0, 0)

#define IIO_EVENT_CODE_EXTRACT_TYPE(mask) ((mask >> 56) & 0xFF)

#define IIO_EVENT_CODE_EXTRACT_DIR(mask) ((mask >> 48) & 0xCF)

#define IIO_EVENT_CODE_EXTRACT_CHAN_TYPE(mask) ((mask >> 32) & 0xFF)

/* Event code number extraction depends on which type of event we have.
 * Perhaps review this function in the future*/
#define IIO_EVENT_CODE_EXTRACT_NUM(mask) ((__s16)(mask & 0xFFFF))

#define IIO_EVENT_CODE_EXTRACT_MODIFIER(mask) ((mask >> 40) & 0xFF)

#endif
