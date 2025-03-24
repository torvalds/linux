/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * ISHTP firmware loader header
 *
 * Copyright (c) 2024, Intel Corporation.
 */

#ifndef _ISHTP_LOADER_H_
#define _ISHTP_LOADER_H_

#include <linux/bits.h>
#include <linux/jiffies.h>
#include <linux/sizes.h>
#include <linux/types.h>

#include "ishtp-dev.h"

struct work_struct;

#define LOADER_MSG_SIZE \
	(IPC_PAYLOAD_SIZE - sizeof(struct ishtp_msg_hdr))

/*
 * ISHTP firmware loader protocol definition
 */
#define LOADER_CMD_XFER_QUERY		0	/* SW -> FW */
#define LOADER_CMD_XFER_FRAGMENT	1	/* SW -> FW */
#define LOADER_CMD_START		2	/* SW -> FW */

/* Only support DMA mode */
#define LOADER_XFER_MODE_DMA BIT(0)

/**
 * union loader_msg_header - ISHTP firmware loader message header
 * @command: Command type
 * @is_response: Indicates if the message is a response
 * @has_next: Indicates if there is a next message
 * @reserved: Reserved for future use
 * @status: Status of the message
 * @val32: entire header as a 32-bit value
 */
union loader_msg_header {
	struct {
		__u32 command:7;
		__u32 is_response:1;
		__u32 has_next:1;
		__u32 reserved:15;
		__u32 status:8;
	};
	__u32 val32;
};

/**
 * struct loader_xfer_query - ISHTP firmware loader transfer query packet
 * @header: Header of the message
 * @image_size: Size of the image
 */
struct loader_xfer_query {
	__le32 header;
	__le32 image_size;
};

/**
 * struct loader_version - ISHTP firmware loader version
 * @value: Value of the version
 * @major: Major version
 * @minor: Minor version
 * @hotfix: Hotfix version
 * @build: Build version
 */
struct loader_version {
	union {
		__le32 value;
		struct {
			__u8 major;
			__u8 minor;
			__u8 hotfix;
			__u8 build;
		};
	};
};

/**
 * struct loader_capability - ISHTP firmware loader capability
 * @max_fw_image_size: Maximum firmware image size
 * @support_mode: Support mode
 * @reserved: Reserved for future use
 * @platform: Platform
 * @max_dma_buf_size: Maximum DMA buffer size, multiples of 4096
 */
struct loader_capability {
	__le32 max_fw_image_size;
	__le16 support_mode;
	__u8 reserved;
	__u8 platform;
	__le32 max_dma_buf_size;
};

/**
 * struct loader_xfer_query_ack - ISHTP firmware loader transfer query acknowledgment
 * @header: Header of the message
 * @version_major: ISH Major version
 * @version_minor: ISH Minor version
 * @version_hotfix: ISH Hotfix version
 * @version_build: ISH Build version
 * @protocol_version: Protocol version
 * @loader_version: Loader version
 * @capability: Loader capability
 */
struct loader_xfer_query_ack {
	__le32 header;
	__le16 version_major;
	__le16 version_minor;
	__le16 version_hotfix;
	__le16 version_build;
	__le32 protocol_version;
	struct loader_version loader_version;
	struct loader_capability capability;
};

/**
 * struct loader_xfer_fragment - ISHTP firmware loader transfer fragment
 * @header: Header of the message
 * @xfer_mode: Transfer mode
 * @offset: Offset
 * @size: Size
 * @is_last: Is last
 */
struct loader_xfer_fragment {
	__le32 header;
	__le32 xfer_mode;
	__le32 offset;
	__le32 size;
	__le32 is_last;
};

/**
 * struct loader_xfer_fragment_ack - ISHTP firmware loader transfer fragment acknowledgment
 * @header: Header of the message
 */
struct loader_xfer_fragment_ack {
	__le32 header;
};

/**
 * struct fragment_dscrpt - ISHTP firmware loader fragment descriptor
 * @ddr_adrs: The address in host DDR
 * @fw_off: The offset of the fragment in the fw image
 * @length: The length of the fragment
 */
struct fragment_dscrpt {
	__le64 ddr_adrs;
	__le32 fw_off;
	__le32 length;
};

#define FRAGMENT_MAX_NUM \
	((LOADER_MSG_SIZE - sizeof(struct loader_xfer_dma_fragment)) / \
	 sizeof(struct fragment_dscrpt))

/**
 * struct loader_xfer_dma_fragment - ISHTP firmware loader transfer DMA fragment
 * @fragment: Fragment
 * @fragment_cnt: How many descriptors in the fragment_tbl
 * @fragment_tbl: Fragment table
 */
struct loader_xfer_dma_fragment {
	struct loader_xfer_fragment fragment;
	__le32 fragment_cnt;
	struct fragment_dscrpt fragment_tbl[] __counted_by(fragment_cnt);
};

/**
 * struct loader_start - ISHTP firmware loader start
 * @header: Header of the message
 */
struct loader_start {
	__le32 header;
};

/**
 * struct loader_start_ack - ISHTP firmware loader start acknowledgment
 * @header: Header of the message
 */
struct loader_start_ack {
	__le32 header;
};

union loader_recv_message {
	__le32 header;
	struct loader_xfer_query_ack query_ack;
	struct loader_xfer_fragment_ack fragment_ack;
	struct loader_start_ack start_ack;
	__u8 raw_data[LOADER_MSG_SIZE];
};

/*
 * ISHTP firmware loader internal use
 */
/* ISHTP firmware loader command timeout */
#define ISHTP_LOADER_TIMEOUT msecs_to_jiffies(100)

/* ISHTP firmware loader retry times */
#define ISHTP_LOADER_RETRY_TIMES 3

/**
 * struct ish_firmware_variant - ISH firmware variant
 * @device: PCI Device ID
 * @filename: The firmware file name
 */
struct ish_firmware_variant {
	unsigned short device;
	const char *filename;
};

/*
 * ISHTP firmware loader API for ISHTP hbm
 */

/* ISHTP capability bit for firmware loader */
#define ISHTP_SUPPORT_CAP_LOADER BIT(4)

/* Firmware loader address */
#define ISHTP_LOADER_CLIENT_ADDR 16

/**
 * ishtp_loader_work - The work function to start the firmware loading process
 * @work: The work structure
 */
void ishtp_loader_work(struct work_struct *work);

/* ISH Manifest alignment in binary is 4KB aligned */
#define ISH_MANIFEST_ALIGNMENT SZ_4K

/* Signature for ISH global manifest */
#define ISH_GLOBAL_SIG 0x47485349	/* FourCC 'I', 'S', 'H', 'G' */

struct version_in_manifest {
	__le16 major;
	__le16 minor;
	__le16 hotfix;
	__le16 build;
};

/**
 * struct ish_global_manifest - global manifest for ISH
 * @sig_fourcc: Signature FourCC, should be 'I', 'S', 'H', 'G'.
 * @len: Length of the manifest.
 * @header_version: Version of the manifest header.
 * @flags: Flags for additional information.
 * @base_ver: Base version of Intel's released firmware.
 * @reserved: Reserved space for future use.
 * @prj_ver: Vendor-customized project version.
 */
struct ish_global_manifest {
	__le32 sig_fourcc;
	__le32 len;
	__le32 header_version;
	__le32 flags;
	struct version_in_manifest base_ver;
	__le32 reserved[13];
	struct version_in_manifest prj_ver;
};

#endif /* _ISHTP_LOADER_H_ */
