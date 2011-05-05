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

#ifndef _INTEL_SAS_H_
#define _INTEL_SAS_H_

/**
 * This file contains all of the definitions relating to structures, constants,
 *    etc. defined by the SAS specification.
 *
 *
 */
#include <linux/kernel.h>
#include "intel_scsi.h"

/**
 * struct sci_sas_address - This structure depicts how a SAS address is
 *    represented by SCI.
 *
 *
 */
struct sci_sas_address {
	/**
	 * This member contains the higher 32-bits of the SAS address.
	 */
	u32 high;

	/**
	 * This member contains the lower 32-bits of the SAS address.
	 */
	u32 low;

};

/**
 * enum _SCI_SAS_TASK_ATTRIBUTE - This enumeration depicts the SAM/SAS
 *    specification defined task attribute values for a command information
 *    unit.
 *
 *
 */
enum sci_sas_task_attribute {
	SCI_SAS_SIMPLE_ATTRIBUTE = 0,
	SCI_SAS_HEAD_OF_QUEUE_ATTRIBUTE = 1,
	SCI_SAS_ORDERED_ATTRIBUTE = 2,
	SCI_SAS_ACA_ATTRIBUTE = 4,
};

/**
 * enum _SCI_SAS_TASK_MGMT_FUNCTION - This enumeration depicts the SAM/SAS
 *    specification defined task management functions.
 *
 * This HARD_RESET function listed here is not actually defined as a task
 * management function in the industry standard.
 */
enum sci_sas_task_mgmt_function {
	SCI_SAS_ABORT_TASK = SCSI_TASK_REQUEST_ABORT_TASK,
	SCI_SAS_ABORT_TASK_SET = SCSI_TASK_REQUEST_ABORT_TASK_SET,
	SCI_SAS_CLEAR_TASK_SET = SCSI_TASK_REQUEST_CLEAR_TASK_SET,
	SCI_SAS_LOGICAL_UNIT_RESET = SCSI_TASK_REQUEST_LOGICAL_UNIT_RESET,
	SCI_SAS_I_T_NEXUS_RESET = SCSI_TASK_REQUEST_I_T_NEXUS_RESET,
	SCI_SAS_CLEAR_ACA = SCSI_TASK_REQUEST_CLEAR_ACA,
	SCI_SAS_QUERY_TASK = SCSI_TASK_REQUEST_QUERY_TASK,
	SCI_SAS_QUERY_TASK_SET = SCSI_TASK_REQUEST_QUERY_TASK_SET,
	SCI_SAS_QUERY_ASYNCHRONOUS_EVENT = SCSI_TASK_REQUEST_QUERY_UNIT_ATTENTION,
	SCI_SAS_HARD_RESET = 0xFF
};


/**
 * enum _SCI_SAS_FRAME_TYPE - This enumeration depicts the SAS specification
 *    defined SSP frame types.
 *
 *
 */
enum sci_sas_frame_type {
	SCI_SAS_DATA_FRAME = 0x01,
	SCI_SAS_XFER_RDY_FRAME = 0x05,
	SCI_SAS_COMMAND_FRAME = 0x06,
	SCI_SAS_RESPONSE_FRAME = 0x07,
	SCI_SAS_TASK_FRAME = 0x16
};

/**
 * struct sci_ssp_frame_header - This structure depicts the contents of an SSP
 *    frame header.  For specific information on the individual fields please
 *    reference the SAS specification transport layer SSP frame format.
 *
 *
 */
struct sci_ssp_frame_header {
	/* Word 0 */
	u32 hashed_destination_address:24;
	u32 frame_type:8;

	/* Word 1 */
	u32 hashed_source_address:24;
	u32 reserved1_0:8;

	/* Word 2 */
	u32 reserved2_2:6;
	u32 fill_bytes:2;
	u32 reserved2_1:3;
	u32 tlr_control:2;
	u32 retry_data_frames:1;
	u32 retransmit:1;
	u32 changing_data_pointer:1;
	u32 reserved2_0:16;

	/* Word 3 */
	u32 uiResv4;

	/* Word 4 */
	u16 target_port_transfer_tag;
	u16 tag;

	/* Word 5 */
	u32 data_offset;

};


#define PHY_OPERATION_NOP               0x00
#define PHY_OPERATION_LINK_RESET        0x01
#define PHY_OPERATION_HARD_RESET        0x02
#define PHY_OPERATION_DISABLE           0x03
#define PHY_OPERATION_CLEAR_ERROR_LOG   0x05
#define PHY_OPERATION_CLEAR_AFFILIATION 0x06

#define NPLR_PHY_ENABLED_UNK_LINK_RATE 0x00
#define NPLR_PHY_DISABLED     0x01
#define NPLR_PHY_ENABLED_SPD_NEG_FAILED   0x02
#define NPLR_PHY_ENABLED_SATA_HOLD  0x03
#define NPLR_PHY_ENABLED_1_5G    0x08
#define NPLR_PHY_ENABLED_3_0G    0x09

/* SMP Function Result values. */
#define SMP_RESULT_FUNCTION_ACCEPTED              0x00
#define SMP_RESULT_UNKNOWN_FUNCTION               0x01
#define SMP_RESULT_FUNCTION_FAILED                0x02
#define SMP_RESULT_INVALID_REQUEST_FRAME_LEN      0x03
#define SMP_RESULT_INAVALID_EXPANDER_CHANGE_COUNT 0x04
#define SMP_RESULT_BUSY                           0x05
#define SMP_RESULT_INCOMPLETE_DESCRIPTOR_LIST     0x06
#define SMP_RESULT_PHY_DOES_NOT_EXIST             0x10
#define SMP_RESULT_INDEX_DOES_NOT_EXIST           0x11
#define SMP_RESULT_PHY_DOES_NOT_SUPPORT_SATA      0x12
#define SMP_RESULT_UNKNOWN_PHY_OPERATION          0x13
#define SMP_RESULT_UNKNOWN_PHY_TEST_FUNCTION      0x14
#define SMP_RESULT_PHY_TEST_IN_PROGRESS           0x15
#define SMP_RESULT_PHY_VACANT                     0x16

/* Attached Device Types */
#define SMP_NO_DEVICE_ATTACHED      0
#define SMP_END_DEVICE_ONLY         1
#define SMP_EDGE_EXPANDER_DEVICE    2
#define SMP_FANOUT_EXPANDER_DEVICE  3

/* Expander phy routine attribute */
#define DIRECT_ROUTING_ATTRIBUTE        0
#define SUBTRACTIVE_ROUTING_ATTRIBUTE   1
#define TABLE_ROUTING_ATTRIBUTE         2

#endif /* _INTEL_SAS_H_ */

