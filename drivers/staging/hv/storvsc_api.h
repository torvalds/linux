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

#include "vmbus_api.h"

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
	enum storvsc_request_type type;
	u32 host;
	u32 bus;
	u32 target_id;
	u32 lun_id;
	u8 *cdb;
	u32 cdb_len;
	u32 status;
	u32 bytes_xfer;

	unsigned char *sense_buffer;
	u32 sense_buffer_size;

	void *context;

	void (*on_io_completion)(struct hv_storvsc_request *request);

	/* This points to the memory after DataBuffer */
	void *extension;

	struct hv_multipage_buffer data_buffer;
};

/* Represents the block vsc driver */
struct storvsc_driver_object {
	/* Must be the first field */
	/* Which is a bug FIXME! */
	struct hv_driver base;

	/* Set by caller (in bytes) */
	u32 ring_buffer_size;

	/* Allocate this much private extension for each I/O request */
	u32 request_ext_size;

	/* Maximum # of requests in flight per channel/device */
	u32 max_outstanding_req_per_channel;

	/* Specific to this driver */
	int (*on_io_request)(struct hv_device *device,
			   struct hv_storvsc_request *request);
};

struct storvsc_device_info {
	unsigned int port_number;
	unsigned char path_id;
	unsigned char target_id;
};

/* Interface */
int stor_vsc_initialize(struct hv_driver *driver);
int stor_vsc_on_host_reset(struct hv_device *device);
int blk_vsc_initialize(struct hv_driver *driver);

#endif /* _STORVSC_API_H_ */
