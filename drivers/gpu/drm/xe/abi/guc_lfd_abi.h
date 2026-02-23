/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _ABI_GUC_LFD_ABI_H_
#define _ABI_GUC_LFD_ABI_H_

#include <linux/types.h>

#include "guc_lic_abi.h"

/* The current major version of GuC-Log-File format. */
#define GUC_LFD_FORMAT_VERSION_MAJOR		0x0001
/* The current minor version of GuC-Log-File format. */
#define GUC_LFD_FORMAT_VERSION_MINOR		0x0000

/** enum guc_lfd_type - Log format descriptor type */
enum guc_lfd_type {
	/**
	 * @GUC_LFD_TYPE_FW_REQUIRED_RANGE_START: Start of range for
	 * required LFDs from GuC
	 * @GUC_LFD_TYPE_FW_VERSION: GuC Firmware Version structure.
	 * @GUC_LFD_TYPE_GUC_DEVICE_ID: GuC microcontroller device ID.
	 * @GUC_LFD_TYPE_TSC_FREQUENCY: Frequency of GuC timestamps.
	 * @GUC_LFD_TYPE_GMD_ID: HW GMD ID.
	 * @GUC_LFD_TYPE_BUILD_PLATFORM_ID: GuC build platform ID.
	 * @GUC_LFD_TYPE_FW_REQUIRED_RANGE_END: End of range for
	 * required LFDs from GuC
	 */
	GUC_LFD_TYPE_FW_REQUIRED_RANGE_START	= 0x1,
	GUC_LFD_TYPE_FW_VERSION			= 0x1,
	GUC_LFD_TYPE_GUC_DEVICE_ID		= 0x2,
	GUC_LFD_TYPE_TSC_FREQUENCY		= 0x3,
	GUC_LFD_TYPE_GMD_ID			= 0x4,
	GUC_LFD_TYPE_BUILD_PLATFORM_ID		= 0x5,
	GUC_LFD_TYPE_FW_REQUIRED_RANGE_END	= 0x1FFF,

	/**
	 * @GUC_LFD_TYPE_FW_OPTIONAL_RANGE_START: Start of range for
	 * optional LFDs from GuC
	 * @GUC_LFD_TYPE_LOG_EVENTS_BUFFER: Log-event-entries buffer.
	 * @GUC_LFD_TYPE_FW_CRASH_DUMP: GuC generated crash-dump blob.
	 * @GUC_LFD_TYPE_FW_OPTIONAL_RANGE_END: End of range for
	 * optional LFDs from GuC
	 */
	GUC_LFD_TYPE_FW_OPTIONAL_RANGE_START	= 0x2000,
	GUC_LFD_TYPE_LOG_EVENTS_BUFFER		= 0x2000,
	GUC_LFD_TYPE_FW_CRASH_DUMP		= 0x2001,
	GUC_LFD_TYPE_FW_OPTIONAL_RANGE_END	= 0x3FFF,

	/**
	 * @GUC_LFD_TYPE_KMD_REQUIRED_RANGE_START: Start of range for
	 * required KMD LFDs
	 * @GUC_LFD_TYPE_OS_ID: An identifier for the OS.
	 * @GUC_LFD_TYPE_KMD_REQUIRED_RANGE_END: End of this range for
	 * required KMD LFDs
	 */
	GUC_LFD_TYPE_KMD_REQUIRED_RANGE_START	= 0x4000,
	GUC_LFD_TYPE_OS_ID			= 0x4000,
	GUC_LFD_TYPE_KMD_REQUIRED_RANGE_END	= 0x5FFF,

	/**
	 * @GUC_LFD_TYPE_KMD_OPTIONAL_RANGE_START: Start of range for
	 * optional KMD LFDs
	 * @GUC_LFD_TYPE_BINARY_SCHEMA_FORMAT: Binary representation of
	 * GuC log-events schema.
	 * @GUC_LFD_TYPE_HOST_COMMENT: ASCII string containing comments
	 * from the host/KMD.
	 * @GUC_LFD_TYPE_TIMESTAMP_ANCHOR: A timestamp anchor, to convert
	 * between host and GuC timestamp.
	 * @GUC_LFD_TYPE_TIMESTAMP_ANCHOR_CONFIG: Timestamp anchor
	 * configuration, definition of timestamp frequency and bit width.
	 * @GUC_LFD_TYPE_KMD_OPTIONAL_RANGE_END: End of this range for
	 * optional KMD LFDs
	 */
	GUC_LFD_TYPE_KMD_OPTIONAL_RANGE_START	= 0x6000,
	GUC_LFD_TYPE_BINARY_SCHEMA_FORMAT	= 0x6000,
	GUC_LFD_TYPE_HOST_COMMENT		= 0x6001,
	GUC_LFD_TYPE_TIMESTAMP_ANCHOR		= 0x6002,
	GUC_LFD_TYPE_TIMESTAMP_ANCHOR_CONFIG	= 0x6003,
	GUC_LFD_TYPE_KMD_OPTIONAL_RANGE_END	= 0x7FFF,

	/*
	 * @GUC_LFD_TYPE_RESERVED_RANGE_START: Start of reserved range
	 * @GUC_LFD_TYPE_RESERVED_RANGE_END: End of reserved range
	 */
	GUC_LFD_TYPE_RESERVED_RANGE_START	= 0x8000,
	GUC_LFD_TYPE_RESERVED_RANGE_END		= 0xFFFF,
};

/** enum guc_lfd_os_type - OS Type LFD-ID */
enum guc_lfd_os_type {
	/** @GUC_LFD_OS_TYPE_OSID_WIN: Windows OS */
	GUC_LFD_OS_TYPE_OSID_WIN = 0x1,
	/** @GUC_LFD_OS_TYPE_OSID_LIN: Linux OS */
	GUC_LFD_OS_TYPE_OSID_LIN = 0x2,
	/** @GUC_LFD_OS_TYPE_OSID_VMW: VMWare OS */
	GUC_LFD_OS_TYPE_OSID_VMW = 0x3,
	/** @GUC_LFD_OS_TYPE_OSID_OTHER: Other */
	GUC_LFD_OS_TYPE_OSID_OTHER = 0x4,
};

/** struct guc_lfd_data - A generic header structure for all LFD blocks */
struct guc_lfd_data {
	/** @header: A 32 bits dword, contains multiple bit fields */
	u32 header;
	/* LFD type. See guc_lfd_type */
#define GUC_LFD_DATA_HEADER_MASK_TYPE		GENMASK(31, 16)
#define GUC_LFD_DATA_HEADER_MASK_MAGIC		GENMASK(15, 0)

	/** @data_count: Number of dwords the `data` field contains. */
	u32 data_count;
	/** @data: Data defined by GUC_LFD_DATA_HEADER_MASK_TYPE */
	u32 data[] __counted_by(data_count);
} __packed;

/**
 * struct guc_lfd_data_log_events_buf - GuC Log Events Buffer.
 * This is optional fw LFD data
 */
struct guc_lfd_data_log_events_buf {
	/**
	 * @log_events_format_version: version of GuC log format of buffer
	 */
	u32 log_events_format_version;
	/**
	 * @log_event: The log event data.
	 * Size in dwords is LFD block size - 1.
	 */
	u32 log_event[];
} __packed;

/** struct guc_lfd_data_os_info - OS Version Information. */
struct guc_lfd_data_os_info {
	/**
	 * @os_id: enum values to identify the OS brand.
	 * See guc_lfd_os_type for the range of types
	 */
	u32 os_id;
	/**
	 * @build_version: ASCII string containing OS build version
	 * information based on os_id. String is padded with null
	 * characters to ensure its DWORD aligned.
	 * Size in dwords is LFD block size - 1.
	 */
	char build_version[];
} __packed;

/**
 * struct guc_lfd_file_header - Header of GuC Log Streaming-LFD-File Format.
 * This structure encapsulates the layout of the guc-log-file format
 */
struct guc_lfd_file_header {
	/**
	 * @magic: A magic number set by producer of a GuC log file to
	 * identify that file is a valid guc-log-file containing a stream
	 * of LFDs.
	 */
	u64 magic;
	/** @version: Version of this file format layout */
	u32 version;
#define GUC_LFD_FILE_HEADER_VERSION_MASK_MAJOR	GENMASK(31, 16)
#define GUC_LFD_FILE_HEADER_VERSION_MASK_MINOR	GENMASK(15, 0)

	/** @stream: A stream of one or more guc_lfd_data LFD blocks */
	u32 stream[];
} __packed;

#endif
