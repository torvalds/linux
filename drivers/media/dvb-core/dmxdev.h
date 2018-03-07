/*
 * dmxdev.h
 *
 * Copyright (C) 2000 Ralph Metzler & Marcus Metzler
 *                    for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _DMXDEV_H_
#define _DMXDEV_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/mutex.h>
#include <linux/slab.h>

#include <linux/dvb/dmx.h>

#include "dvbdev.h"
#include "demux.h"
#include "dvb_ringbuffer.h"

/**
 * enum dmxdev_type - type of demux filter type.
 *
 * @DMXDEV_TYPE_NONE:	no filter set.
 * @DMXDEV_TYPE_SEC:	section filter.
 * @DMXDEV_TYPE_PES:	Program Elementary Stream (PES) filter.
 */
enum dmxdev_type {
	DMXDEV_TYPE_NONE,
	DMXDEV_TYPE_SEC,
	DMXDEV_TYPE_PES,
};

/**
 * enum dmxdev_state - state machine for the dmxdev.
 *
 * @DMXDEV_STATE_FREE:		indicates that the filter is freed.
 * @DMXDEV_STATE_ALLOCATED:	indicates that the filter was allocated
 *				to be used.
 * @DMXDEV_STATE_SET:		indicates that the filter parameters are set.
 * @DMXDEV_STATE_GO:		indicates that the filter is running.
 * @DMXDEV_STATE_DONE:		indicates that a packet was already filtered
 *				and the filter is now disabled.
 *				Set only if %DMX_ONESHOT. See
 *				&dmx_sct_filter_params.
 * @DMXDEV_STATE_TIMEDOUT:	Indicates a timeout condition.
 */
enum dmxdev_state {
	DMXDEV_STATE_FREE,
	DMXDEV_STATE_ALLOCATED,
	DMXDEV_STATE_SET,
	DMXDEV_STATE_GO,
	DMXDEV_STATE_DONE,
	DMXDEV_STATE_TIMEDOUT
};

/**
 * struct dmxdev_feed - digital TV dmxdev feed
 *
 * @pid:	Program ID to be filtered
 * @ts:		pointer to &struct dmx_ts_feed
 * @next:	&struct list_head pointing to the next feed.
 */

struct dmxdev_feed {
	u16 pid;
	struct dmx_ts_feed *ts;
	struct list_head next;
};

/**
 * struct dmxdev_filter - digital TV dmxdev filter
 *
 * @filter:	a dmxdev filter. Currently used only for section filter:
 *		if the filter is Section, it contains a
 *		&struct dmx_section_filter @sec pointer.
 * @feed:	a dmxdev feed. Depending on the feed type, it can be:
 *		for TS feed: a &struct list_head @ts list of TS and PES
 *		feeds;
 *		for section feed: a &struct dmx_section_feed @sec pointer.
 * @params:	dmxdev filter parameters. Depending on the feed type, it
 *		can be:
 *		for section filter: a &struct dmx_sct_filter_params @sec
 *		embedded struct;
 *		for a TS filter: a &struct dmx_pes_filter_params @pes
 *		embedded struct.
 * @type:	type of the dmxdev filter, as defined by &enum dmxdev_type.
 * @state:	state of the dmxdev filter, as defined by &enum dmxdev_state.
 * @dev:	pointer to &struct dmxdev.
 * @buffer:	an embedded &struct dvb_ringbuffer buffer.
 * @mutex:	protects the access to &struct dmxdev_filter.
 * @timer:	&struct timer_list embedded timer, used to check for
 *		feed timeouts.
 *		Only for section filter.
 * @todo:	index for the @secheader.
 *		Only for section filter.
 * @secheader:	buffer cache to parse the section header.
 *		Only for section filter.
 */
struct dmxdev_filter {
	union {
		struct dmx_section_filter *sec;
	} filter;

	union {
		/* list of TS and PES feeds (struct dmxdev_feed) */
		struct list_head ts;
		struct dmx_section_feed *sec;
	} feed;

	union {
		struct dmx_sct_filter_params sec;
		struct dmx_pes_filter_params pes;
	} params;

	enum dmxdev_type type;
	enum dmxdev_state state;
	struct dmxdev *dev;
	struct dvb_ringbuffer buffer;

	struct mutex mutex;

	/* only for sections */
	struct timer_list timer;
	int todo;
	u8 secheader[3];
};

/**
 * struct dmxdev - Describes a digital TV demux device.
 *
 * @dvbdev:		pointer to &struct dvb_device associated with
 *			the demux device node.
 * @dvr_dvbdev:		pointer to &struct dvb_device associated with
 *			the dvr device node.
 * @filter:		pointer to &struct dmxdev_filter.
 * @demux:		pointer to &struct dmx_demux.
 * @filternum:		number of filters.
 * @capabilities:	demux capabilities as defined by &enum dmx_demux_caps.
 * @exit:		flag to indicate that the demux is being released.
 * @dvr_orig_fe:	pointer to &struct dmx_frontend.
 * @dvr_buffer:		embedded &struct dvb_ringbuffer for DVB output.
 * @mutex:		protects the usage of this structure.
 * @lock:		protects access to &dmxdev->filter->data.
 */
struct dmxdev {
	struct dvb_device *dvbdev;
	struct dvb_device *dvr_dvbdev;

	struct dmxdev_filter *filter;
	struct dmx_demux *demux;

	int filternum;
	int capabilities;

	unsigned int exit:1;
#define DMXDEV_CAP_DUPLEX 1
	struct dmx_frontend *dvr_orig_fe;

	struct dvb_ringbuffer dvr_buffer;
#define DVR_BUFFER_SIZE (10*188*1024)

	struct mutex mutex;
	spinlock_t lock;
};

/**
 * dvb_dmxdev_init - initializes a digital TV demux and registers both demux
 *	and DVR devices.
 *
 * @dmxdev: pointer to &struct dmxdev.
 * @adap: pointer to &struct dvb_adapter.
 */
int dvb_dmxdev_init(struct dmxdev *dmxdev, struct dvb_adapter *adap);

/**
 * dvb_dmxdev_release - releases a digital TV demux and unregisters it.
 *
 * @dmxdev: pointer to &struct dmxdev.
 */
void dvb_dmxdev_release(struct dmxdev *dmxdev);

#endif /* _DMXDEV_H_ */
