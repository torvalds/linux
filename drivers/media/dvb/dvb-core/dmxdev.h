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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DMXDEV_H_
#define _DMXDEV_H_

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <asm/semaphore.h>

#include <linux/dvb/dmx.h>

#include "dvbdev.h"
#include "demux.h"

enum dmxdevype {
	DMXDEV_TYPE_NONE,
	DMXDEV_TYPE_SEC,
	DMXDEV_TYPE_PES,
};

enum dmxdev_state {
	DMXDEV_STATE_FREE,
	DMXDEV_STATE_ALLOCATED,
	DMXDEV_STATE_SET,
	DMXDEV_STATE_GO,
	DMXDEV_STATE_DONE,
	DMXDEV_STATE_TIMEDOUT
};

struct dmxdev_buffer {
        u8 *data;
        int size;
        int pread;
        int pwrite;
	wait_queue_head_t queue;
        int error;
};

struct dmxdev_filter {
	struct dvb_device *dvbdev;

        union {
	        struct dmx_section_filter *sec;
	} filter;

        union {
                struct dmx_ts_feed *ts;
                struct dmx_section_feed *sec;
	} feed;

        union {
	        struct dmx_sct_filter_params sec;
	        struct dmx_pes_filter_params pes;
	} params;

        int type;
        enum dmxdev_state state;
        struct dmxdev *dev;
        struct dmxdev_buffer buffer;

	struct semaphore mutex;

        /* only for sections */
        struct timer_list timer;
        int todo;
        u8 secheader[3];

        u16 pid;
};


struct dmxdev_dvr {
        int state;
        struct dmxdev *dev;
        struct dmxdev_buffer buffer;
};


struct dmxdev {
	struct dvb_device *dvbdev;
	struct dvb_device *dvr_dvbdev;

        struct dmxdev_filter *filter;
        struct dmxdev_dvr *dvr;
        struct dmx_demux *demux;

        int filternum;
        int capabilities;
#define DMXDEV_CAP_DUPLEX 1
        struct dmx_frontend *dvr_orig_fe;

        struct dmxdev_buffer dvr_buffer;
#define DVR_BUFFER_SIZE (10*188*1024)

	struct semaphore mutex;
	spinlock_t lock;
};


int dvb_dmxdev_init(struct dmxdev *dmxdev, struct dvb_adapter *);
void dvb_dmxdev_release(struct dmxdev *dmxdev);

#endif /* _DMXDEV_H_ */
