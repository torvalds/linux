/*
 *
 * Copyright (c) 2011, Microsoft Corporation.
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
 *   K. Y. Srinivasan <kys@microsoft.com>
 *
 */

#ifndef _HYPERV_STORAGE_H
#define _HYPERV_STORAGE_H


/* vstorage.w revision number.  This is used in the case of a version match, */
/* to alert the user that structure sizes may be mismatched even though the */
/* protocol versions match. */


#define REVISION_STRING(REVISION_) #REVISION_
#define FILL_VMSTOR_REVISION(RESULT_LVALUE_)				\
	do {								\
		char *revision_string					\
			= REVISION_STRING($Rev : 6 $) + 6;		\
		RESULT_LVALUE_ = 0;					\
		while (*revision_string >= '0'				\
			&& *revision_string <= '9') {			\
			RESULT_LVALUE_ *= 10;				\
			RESULT_LVALUE_ += *revision_string - '0';	\
			revision_string++;				\
		}							\
	} while (0)

/* Major/minor macros.  Minor version is in LSB, meaning that earlier flat */
/* version numbers will be interpreted as "0.x" (i.e., 1 becomes 0.1). */
#define VMSTOR_PROTOCOL_MAJOR(VERSION_)		(((VERSION_) >> 8) & 0xff)
#define VMSTOR_PROTOCOL_MINOR(VERSION_)		(((VERSION_))      & 0xff)
#define VMSTOR_PROTOCOL_VERSION(MAJOR_, MINOR_)	((((MAJOR_) & 0xff) << 8) | \
						 (((MINOR_) & 0xff)))
#define VMSTOR_INVALID_PROTOCOL_VERSION		(-1)

/* Version history: */
/* V1 Beta                    0.1 */
/* V1 RC < 2008/1/31          1.0 */
/* V1 RC > 2008/1/31          2.0 */
#define VMSTOR_PROTOCOL_VERSION_CURRENT VMSTOR_PROTOCOL_VERSION(2, 0)




/*  This will get replaced with the max transfer length that is possible on */
/*  the host adapter. */
/*  The max transfer length will be published when we offer a vmbus channel. */
#define MAX_TRANSFER_LENGTH	0x40000
#define DEFAULT_PACKET_SIZE (sizeof(struct vmdata_gpa_direct) +	\
			sizeof(struct vstor_packet) +		\
			sizesizeof(u64) * (MAX_TRANSFER_LENGTH / PAGE_SIZE)))


/*  Packet structure describing virtual storage requests. */
enum vstor_packet_operation {
	VSTOR_OPERATION_COMPLETE_IO		= 1,
	VSTOR_OPERATION_REMOVE_DEVICE		= 2,
	VSTOR_OPERATION_EXECUTE_SRB		= 3,
	VSTOR_OPERATION_RESET_LUN		= 4,
	VSTOR_OPERATION_RESET_ADAPTER		= 5,
	VSTOR_OPERATION_RESET_BUS		= 6,
	VSTOR_OPERATION_BEGIN_INITIALIZATION	= 7,
	VSTOR_OPERATION_END_INITIALIZATION	= 8,
	VSTOR_OPERATION_QUERY_PROTOCOL_VERSION	= 9,
	VSTOR_OPERATION_QUERY_PROPERTIES	= 10,
	VSTOR_OPERATION_MAXIMUM			= 10
};

/*
 * Platform neutral description of a scsi request -
 * this remains the same across the write regardless of 32/64 bit
 * note: it's patterned off the SCSI_PASS_THROUGH structure
 */
#define CDB16GENERIC_LENGTH			0x10

#ifndef SENSE_BUFFER_SIZE
#define SENSE_BUFFER_SIZE			0x12
#endif

#define MAX_DATA_BUF_LEN_WITH_PADDING		0x14

struct vmscsi_request {
	unsigned short length;
	unsigned char srb_status;
	unsigned char scsi_status;

	unsigned char port_number;
	unsigned char path_id;
	unsigned char target_id;
	unsigned char lun;

	unsigned char cdb_length;
	unsigned char sense_info_length;
	unsigned char data_in;
	unsigned char reserved;

	unsigned int data_transfer_length;

	union {
		unsigned char cdb[CDB16GENERIC_LENGTH];
		unsigned char sense_data[SENSE_BUFFER_SIZE];
		unsigned char reserved_array[MAX_DATA_BUF_LEN_WITH_PADDING];
	};
} __attribute((packed));


/*
 * This structure is sent during the intialization phase to get the different
 * properties of the channel.
 */
struct vmstorage_channel_properties {
	unsigned short protocol_version;
	unsigned char path_id;
	unsigned char target_id;

	/* Note: port number is only really known on the client side */
	unsigned int port_number;
	unsigned int flags;
	unsigned int max_transfer_bytes;

	/*  This id is unique for each channel and will correspond with */
	/*  vendor specific data in the inquirydata */
	unsigned long long unique_id;
} __packed;

/*  This structure is sent during the storage protocol negotiations. */
struct vmstorage_protocol_version {
	/* Major (MSW) and minor (LSW) version numbers. */
	unsigned short major_minor;

	/*
	 * Revision number is auto-incremented whenever this file is changed
	 * (See FILL_VMSTOR_REVISION macro above).  Mismatch does not
	 * definitely indicate incompatibility--but it does indicate mismatched
	 * builds.
	 */
	unsigned short revision;
} __packed;

/* Channel Property Flags */
#define STORAGE_CHANNEL_REMOVABLE_FLAG		0x1
#define STORAGE_CHANNEL_EMULATED_IDE_FLAG	0x2

struct vstor_packet {
	/* Requested operation type */
	enum vstor_packet_operation operation;

	/*  Flags - see below for values */
	unsigned int flags;

	/* Status of the request returned from the server side. */
	unsigned int status;

	/* Data payload area */
	union {
		/*
		 * Structure used to forward SCSI commands from the
		 * client to the server.
		 */
		struct vmscsi_request vm_srb;

		/* Structure used to query channel properties. */
		struct vmstorage_channel_properties storage_channel_properties;

		/* Used during version negotiations. */
		struct vmstorage_protocol_version version;
	};
} __packed;

/* Packet flags */
/*
 * This flag indicates that the server should send back a completion for this
 * packet.
 */
#define REQUEST_COMPLETION_FLAG	0x1

/*  This is the set of flags that the vsc can set in any packets it sends */
#define VSC_LEGAL_FLAGS		(REQUEST_COMPLETION_FLAG)


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
#define STORVSC_MAX_CMD_LEN				16

struct hv_storvsc_request;

/* Matches Windows-end */
enum storvsc_request_type {
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

#endif /* _HYPERV_STORAGE_H */
