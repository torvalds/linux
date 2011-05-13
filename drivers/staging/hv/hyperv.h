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

#ifndef _HYPERV_H
#define _HYPERV_H

struct hv_guid {
	unsigned char data[16];
};

#define MAX_PAGE_BUFFER_COUNT				16
#define MAX_MULTIPAGE_BUFFER_COUNT			32 /* 128K */

#pragma pack(push, 1)

/* Single-page buffer */
struct hv_page_buffer {
	u32 len;
	u32 offset;
	u64 pfn;
};

/* Multiple-page buffer */
struct hv_multipage_buffer {
	/* Length and Offset determines the # of pfns in the array */
	u32 len;
	u32 offset;
	u64 pfn_array[MAX_MULTIPAGE_BUFFER_COUNT];
};

/* 0x18 includes the proprietary packet header */
#define MAX_PAGE_BUFFER_PACKET		(0x18 +			\
					(sizeof(struct hv_page_buffer) * \
					 MAX_PAGE_BUFFER_COUNT))
#define MAX_MULTIPAGE_BUFFER_PACKET	(0x18 +			\
					 sizeof(struct hv_multipage_buffer))


#pragma pack(pop)

struct hv_ring_buffer {
	/* Offset in bytes from the start of ring data below */
	u32 write_index;

	/* Offset in bytes from the start of ring data below */
	u32 read_index;

	u32 interrupt_mask;

	/* Pad it to PAGE_SIZE so that data starts on page boundary */
	u8	reserved[4084];

	/* NOTE:
	 * The interrupt_mask field is used only for channels but since our
	 * vmbus connection also uses this data structure and its data starts
	 * here, we commented out this field.
	 */

	/*
	 * Ring data starts here + RingDataStartOffset
	 * !!! DO NOT place any fields below this !!!
	 */
	u8 buffer[0];
} __packed;

struct hv_ring_buffer_info {
	struct hv_ring_buffer *ring_buffer;
	u32 ring_size;			/* Include the shared header */
	spinlock_t ring_lock;

	u32 ring_datasize;		/* < ring_size */
	u32 ring_data_startoffset;
};

struct hv_ring_buffer_debug_info {
	u32 current_interrupt_mask;
	u32 current_read_index;
	u32 current_write_index;
	u32 bytes_avail_toread;
	u32 bytes_avail_towrite;
};

/*
 * We use the same version numbering for all Hyper-V modules.
 *
 * Definition of versioning is as follows;
 *
 *	Major Number	Changes for these scenarios;
 *			1.	When a new version of Windows Hyper-V
 *				is released.
 *			2.	A Major change has occurred in the
 *				Linux IC's.
 *			(For example the merge for the first time
 *			into the kernel) Every time the Major Number
 *			changes, the Revision number is reset to 0.
 *	Minor Number	Changes when new functionality is added
 *			to the Linux IC's that is not a bug fix.
 *
 * 3.1 - Added completed hv_utils driver. Shutdown/Heartbeat/Timesync
 */
#define HV_DRV_VERSION           "3.1"


/*
 * A revision number of vmbus that is used for ensuring both ends on a
 * partition are using compatible versions.
 */
#define VMBUS_REVISION_NUMBER		13

/* Make maximum size of pipe payload of 16K */
#define MAX_PIPE_DATA_PAYLOAD		(sizeof(u8) * 16384)

/* Define PipeMode values. */
#define VMBUS_PIPE_TYPE_BYTE		0x00000000
#define VMBUS_PIPE_TYPE_MESSAGE		0x00000004

/* The size of the user defined data buffer for non-pipe offers. */
#define MAX_USER_DEFINED_BYTES		120

/* The size of the user defined data buffer for pipe offers. */
#define MAX_PIPE_USER_DEFINED_BYTES	116

/*
 * At the center of the Channel Management library is the Channel Offer. This
 * struct contains the fundamental information about an offer.
 */
struct vmbus_channel_offer {
	struct hv_guid if_type;
	struct hv_guid if_instance;
	u64 int_latency; /* in 100ns units */
	u32 if_revision;
	u32 server_ctx_size;	/* in bytes */
	u16 chn_flags;
	u16 mmio_megabytes;		/* in bytes * 1024 * 1024 */

	union {
		/* Non-pipes: The user has MAX_USER_DEFINED_BYTES bytes. */
		struct {
			unsigned char user_def[MAX_USER_DEFINED_BYTES];
		} std;

		/*
		 * Pipes:
		 * The following sructure is an integrated pipe protocol, which
		 * is implemented on top of standard user-defined data. Pipe
		 * clients have MAX_PIPE_USER_DEFINED_BYTES left for their own
		 * use.
		 */
		struct {
			u32  pipe_mode;
			unsigned char user_def[MAX_PIPE_USER_DEFINED_BYTES];
		} pipe;
	} u;
	u32 padding;
} __packed;

/* Server Flags */
#define VMBUS_CHANNEL_ENUMERATE_DEVICE_INTERFACE	1
#define VMBUS_CHANNEL_SERVER_SUPPORTS_TRANSFER_PAGES	2
#define VMBUS_CHANNEL_SERVER_SUPPORTS_GPADLS		4
#define VMBUS_CHANNEL_NAMED_PIPE_MODE			0x10
#define VMBUS_CHANNEL_LOOPBACK_OFFER			0x100
#define VMBUS_CHANNEL_PARENT_OFFER			0x200
#define VMBUS_CHANNEL_REQUEST_MONITORED_NOTIFICATION	0x400


#endif /* _HYPERV_H */
