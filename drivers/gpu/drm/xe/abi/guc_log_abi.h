/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _ABI_GUC_LOG_ABI_H
#define _ABI_GUC_LOG_ABI_H

#include <linux/types.h>

/* GuC logging buffer types */
enum guc_log_buffer_type {
	GUC_LOG_BUFFER_CRASH_DUMP,
	GUC_LOG_BUFFER_DEBUG,
	GUC_LOG_BUFFER_CAPTURE,
};

#define GUC_LOG_BUFFER_TYPE_MAX		3

/**
 * struct guc_log_buffer_state - GuC log buffer state
 *
 * Below state structure is used for coordination of retrieval of GuC firmware
 * logs. Separate state is maintained for each log buffer type.
 * read_ptr points to the location where Xe read last in log buffer and
 * is read only for GuC firmware. write_ptr is incremented by GuC with number
 * of bytes written for each log entry and is read only for Xe.
 * When any type of log buffer becomes half full, GuC sends a flush interrupt.
 * GuC firmware expects that while it is writing to 2nd half of the buffer,
 * first half would get consumed by Host and then get a flush completed
 * acknowledgment from Host, so that it does not end up doing any overwrite
 * causing loss of logs. So when buffer gets half filled & Xe has requested
 * for interrupt, GuC will set flush_to_file field, set the sampled_write_ptr
 * to the value of write_ptr and raise the interrupt.
 * On receiving the interrupt Xe should read the buffer, clear flush_to_file
 * field and also update read_ptr with the value of sample_write_ptr, before
 * sending an acknowledgment to GuC. marker & version fields are for internal
 * usage of GuC and opaque to Xe. buffer_full_cnt field is incremented every
 * time GuC detects the log buffer overflow.
 */
struct guc_log_buffer_state {
	/** @marker: buffer state start marker */
	u32 marker[2];
	/** @read_ptr: the last byte offset that was read by KMD previously */
	u32 read_ptr;
	/**
	 * @write_ptr: the next byte offset location that will be written by
	 * GuC
	 */
	u32 write_ptr;
	/** @size: Log buffer size */
	u32 size;
	/**
	 * @sampled_write_ptr: Log buffer write pointer
	 * This is written by GuC to the byte offset of the next free entry in
	 * the buffer on log buffer half full or state capture notification
	 */
	u32 sampled_write_ptr;
	/**
	 * @wrap_offset: wraparound offset
	 * This is the byte offset of location 1 byte after last valid guc log
	 * event entry written by Guc firmware before there was a wraparound.
	 * This field is updated by guc firmware and should be used by Host
	 * when copying buffer contents to file.
	 */
	u32 wrap_offset;
	/** @flags: Flush to file flag and buffer full count */
	u32 flags;
#define	GUC_LOG_BUFFER_STATE_FLUSH_TO_FILE	GENMASK(0, 0)
#define	GUC_LOG_BUFFER_STATE_BUFFER_FULL_CNT	GENMASK(4, 1)
	/** @version: The Guc-Log-Entry format version */
	u32 version;
} __packed;

#endif
