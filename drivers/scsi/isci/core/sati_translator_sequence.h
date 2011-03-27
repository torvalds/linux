/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SATI_TRANSLATOR_SEQUENCE_H_
#define _SATI_TRANSLATOR_SEQUENCE_H_

/**
 * This file contains all of the defintions for the SATI translator sequence.
 *    A translator sequence is simply a defintion for the various sequences of
 *    commands that occur in this translator.
 *
 *
 */

#include "sati_device.h"

/**
 * enum _sati_translator_sequence_type - This enumeration defines the possible
 *    sequence types for the translator.
 *
 *
 */
enum sati_translator_sequence_type {
	/* SCSI Primary Command (SPC) sequences. */
	SATI_SEQUENCE_REPORT_LUNS,
	SATI_SEQUENCE_TEST_UNIT_READY,
	SATI_SEQUENCE_INQUIRY_STANDARD,
	SATI_SEQUENCE_INQUIRY_SUPPORTED_PAGES,
	SATI_SEQUENCE_INQUIRY_SERIAL_NUMBER,
	SATI_SEQUENCE_INQUIRY_DEVICE_ID,
	SATI_SEQUENCE_INQUIRY_BLOCK_DEVICE,
	SATI_SEQUENCE_MODE_SENSE_6_CACHING,
	SATI_SEQUENCE_MODE_SENSE_6_INFORMATIONAL_EXCP_CONTROL,
	SATI_SEQUENCE_MODE_SENSE_6_READ_WRITE_ERROR,
	SATI_SEQUENCE_MODE_SENSE_6_DISCONNECT_RECONNECT,
	SATI_SEQUENCE_MODE_SENSE_6_CONTROL,
	SATI_SEQUENCE_MODE_SENSE_6_ALL_PAGES,
	SATI_SEQUENCE_MODE_SENSE_10_CACHING,
	SATI_SEQUENCE_MODE_SENSE_10_INFORMATIONAL_EXCP_CONTROL,
	SATI_SEQUENCE_MODE_SENSE_10_READ_WRITE_ERROR,
	SATI_SEQUENCE_MODE_SENSE_10_DISCONNECT_RECONNECT,
	SATI_SEQUENCE_MODE_SENSE_10_CONTROL,
	SATI_SEQUENCE_MODE_SENSE_10_ALL_PAGES,
	SATI_SEQUENCE_MODE_SELECT_MODE_PAGE_CACHING,
	SATI_SEQUENCE_MODE_SELECT_MODE_POWER_CONDITION,
	SATI_SEQUENCE_MODE_SELECT_MODE_INFORMATION_EXCEPT_CONTROL,

	/* Log Sense Sequences */
	SATI_SEQUENCE_LOG_SENSE_SELF_TEST_LOG_PAGE,
	SATI_SEQUENCE_LOG_SENSE_EXTENDED_SELF_TEST_LOG_PAGE,
	SATI_SEQUENCE_LOG_SENSE_SUPPORTED_LOG_PAGE,
	SATI_SEQUENCE_LOG_SENSE_INFO_EXCEPTION_LOG_PAGE,

	/* SCSI Block Command (SBC) sequences. */

	SATI_SEQUENCE_READ_6,
	SATI_SEQUENCE_READ_10,
	SATI_SEQUENCE_READ_12,
	SATI_SEQUENCE_READ_16,

	SATI_SEQUENCE_READ_CAPACITY_10,
	SATI_SEQUENCE_READ_CAPACITY_16,

	SATI_SEQUENCE_SYNCHRONIZE_CACHE,

	SATI_SEQUENCE_VERIFY_10,
	SATI_SEQUENCE_VERIFY_12,
	SATI_SEQUENCE_VERIFY_16,

	SATI_SEQUENCE_WRITE_6,
	SATI_SEQUENCE_WRITE_10,
	SATI_SEQUENCE_WRITE_12,
	SATI_SEQUENCE_WRITE_16,

	SATI_SEQUENCE_START_STOP_UNIT,

	SATI_SEQUENCE_REASSIGN_BLOCKS,

	/* SCSI Task Requests sequences */

	SATI_SEQUENCE_LUN_RESET,

	SATI_SEQUENCE_REQUEST_SENSE_SMART_RETURN_STATUS,
	SATI_SEQUENCE_REQUEST_SENSE_CHECK_POWER_MODE,

	SATI_SEQUENCE_WRITE_LONG

};

#define SATI_SEQUENCE_TYPE_READ_MIN SATI_SEQUENCE_READ_6
#define SATI_SEQUENCE_TYPE_READ_MAX SATI_SEQUENCE_READ_16

/**
 *
 *
 * SATI_SEQUENCE_STATES These constants depict the various state values
 * associated with a translation sequence.
 */
#define SATI_SEQUENCE_STATE_INITIAL        0
#define SATI_SEQUENCE_STATE_TRANSLATE_DATA 1
#define SATI_SEQUENCE_STATE_AWAIT_RESPONSE 2
#define SATI_SEQUENCE_STATE_FINAL          3
#define SATI_SEQUENCE_STATE_INCOMPLETE     4

/**
 *
 *
 * SATI_DATA_DIRECTIONS These constants depict the various types of data
 * directions for a translation sequence.  Data can flow in/out (read/write) or
 * no data at all.
 */
#define SATI_DATA_DIRECTION_NONE 0
#define SATI_DATA_DIRECTION_IN   1
#define SATI_DATA_DIRECTION_OUT  2

/**
 * struct SATI_MODE_SELECT_PROCESSING_STATE - This structure contains all of
 *    the current processing states for processing mode select 6 and 10
 *    commands' parameter fields.
 *
 *
 */
struct sati_mode_select_processing_state {
	u8 *mode_pages;
	u32 mode_page_offset;
	u32 mode_pages_size;
	u32 size_of_data_processed;
	u32 total_ata_command_sent;
	u32 ata_command_sent_for_cmp; /* cmp: current mode page */
	bool current_mode_page_processed;
};


enum sati_reassign_blocks_ata_command_status {
	SATI_REASSIGN_BLOCKS_READY_TO_SEND,
	SATI_REASSIGN_BLOCKS_COMMAND_FAIL,
	SATI_REASSIGN_BLOCKS_COMMAND_SUCCESS,
};

/**
 * struct sati_reassign_blocks_processing_state - This structure contains all
 *    of the current processing states for processing reassign block command's
 *    parameter fields.
 *
 *
 */
struct sati_reassign_blocks_processing_state {
	u32 lba_offset;
	u32 block_lists_size;
	u8 lba_size;
	u32 size_of_data_processed;
	u32 ata_command_sent_for_current_lba;
	bool current_lba_processed;
	enum  sati_reassign_blocks_ata_command_status ata_command_status;

};

#define SATI_ATAPI_REQUEST_SENSE_CDB_LENGTH 12

/**
 * struct sati_atapi_data - The SATI_ATAPI_DATA structure is for sati atapi IO
 *    specific data.
 *
 *
 */
struct sati_atapi_data {
	u8 request_sense_cdb[SATI_ATAPI_REQUEST_SENSE_CDB_LENGTH];
};

/**
 * struct sati_translator_sequence - This structure contains all of the
 *    translation information associated with a particular request.
 *
 *
 */
struct sati_translator_sequence {
	/**
	 * This field contains the sequence type determined by the SATI.
	 */
	u8 type;

	/**
	 * This field indicates the current state for the sequence.
	 */
	u8 state;

	/**
	 * This field indicates the data direction (none, read, or write) for
	 * the translated request.
	 */
	u8 data_direction;

	/**
	 * This field contains the SATA/ATA protocol to be utilized during
	 * the IO transfer.
	 */
	u8 protocol;

	/**
	 * This field is utilized for sequences requiring data translation.
	 * It specifies the amount of data requested by the caller from the
	 * operation.  It's necessary, because at times the user requests less
	 * data than is available.  Thus, we need to avoid overrunning the
	 * buffer.
	 */
	u32 allocation_length;

	/**
	 * This field specifies the amount of data that will actually be
	 * transfered across the wire for this ATA request.
	 */
	u32 ata_transfer_length;

	/**
	 * This field specifies the amount of data bytes that have been
	 * set in a translation sequence. It will be incremented every time
	 * a data byte has been set by a sati translation.
	 */
	u16 number_data_bytes_set;

	/**
	 * This field indicates whether or not the sense response has been set
	 * by the translation sequence.
	 */
	bool is_sense_response_set;

	/**
	 * This field specifies the remote device context for which this
	 * translator sequence is destined.
	 */
	struct sati_device *device;

	/**
	 * This field is utilized to provide the translator with memory space
	 * required for translations that utilize multiple requests.
	 */
	union {
		u32 translated_command;
		u32 move_sector_count;
		u32 scratch;
		struct sati_reassign_blocks_processing_state
			reassign_blocks_process_state;
		struct sati_mode_select_processing_state process_state;
		struct sati_atapi_data sati_atapi_data;
	} command_specific_data;

};



#endif /* _SATI_TRANSLATOR_SEQUENCE_H_ */

