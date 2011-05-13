/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */


#ifndef _STORVSC_API_H_
#define _STORVSC_API_H_

#include <linux/kernel.h>
#include <linux/wait.h>
#include "hyperv_storage.h"
#include "hyperv.h"

/* Defines */
#define STORVSC_RING_BUFFER_SIZE			(20*PAGE_SIZE)
#define BLKVSC_RING_BUFFER_SIZE				(20*PAGE_SIZE)

#define STORVSC_MAX_IO_REQUESTS				128

/*
 * In Hyper-V, each port/path/target maps to 1 scsi host adapter.  In
 * reality, the path/target is not used (ie always set to 0) so our
 * scsi host adapter essentially has 1 bus with 1 target that contains
 * up to 256 luns.
 */
#define STORVSC_MAX_LUNS_PER_TARGET			64
#define STORVSC_MAX_TARGETS				1
#define STORVSC_MAX_CHANNELS				1

struct hv_storvsc_request;

/* Matches Windows-end */
enum storvsc_request_type{
	WRITE_TYPE,
	READ_TYPE,
	UNKNOWN_TYPE,
};


struct hv_storvsc_request {
	struct hv_storvsc_request *request;
	struct hv_device *device;

	/* Synchronize the request/response if needed */
	struct completion wait_event;

	unsigned char *sense_buffer;
	void *context;
	void (*on_io_completion)(struct hv_storvsc_request *request);
	struct hv_multipage_buffer data_buffer;

	struct vstor_packet vstor_packet;
};


struct storvsc_device_info {
	u32 ring_buffer_size;
	unsigned int port_number;
	unsigned char path_id;
	unsigned char target_id;
};

struct storvsc_major_info {
	int major;
	int index;
	bool do_register;
	char *devname;
	char *diskname;
};

/* A storvsc device is a device object that contains a vmbus channel */
struct storvsc_device {
	struct hv_device *device;

	/* 0 indicates the device is being destroyed */
	atomic_t ref_count;

	bool	 drain_notify;
	atomic_t num_outstanding_req;

	wait_queue_head_t waiting_to_drain;

	/*
	 * Each unique Port/Path/Target represents 1 channel ie scsi
	 * controller. In reality, the pathid, targetid is always 0
	 * and the port is set by us
	 */
	unsigned int port_number;
	unsigned char path_id;
	unsigned char target_id;

	/* Used for vsc/vsp channel reset process */
	struct hv_storvsc_request init_request;
	struct hv_storvsc_request reset_request;
};


/* Get the stordevice object iff exists and its refcount > 1 */
static inline struct storvsc_device *get_stor_device(struct hv_device *device)
{
	struct storvsc_device *stor_device;

	stor_device = (struct storvsc_device *)device->ext;
	if (stor_device && atomic_read(&stor_device->ref_count) > 1)
		atomic_inc(&stor_device->ref_count);
	else
		stor_device = NULL;

	return stor_device;
}


static inline void put_stor_device(struct hv_device *device)
{
	struct storvsc_device *stor_device;

	stor_device = (struct storvsc_device *)device->ext;

	atomic_dec(&stor_device->ref_count);
}

static inline void storvsc_wait_to_drain(struct storvsc_device *dev)
{
	dev->drain_notify = true;
	wait_event(dev->waiting_to_drain,
		   atomic_read(&dev->num_outstanding_req) == 0);
	dev->drain_notify = false;
}

/* Interface */

int storvsc_dev_add(struct hv_device *device,
				void *additional_info);
int storvsc_dev_remove(struct hv_device *device);

int storvsc_do_io(struct hv_device *device,
				struct hv_storvsc_request *request);

int storvsc_get_major_info(struct storvsc_device_info *device_info,
				struct storvsc_major_info *major_info);

#endif /* _STORVSC_API_H_ */
