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

/**
 * This file defines all of the SCSI related constants, enumerations, and
 *    types.  Please note that this file does not necessarily contain an
 *    exhaustive list of all constants, commands, sub-commands, etc.
 *
 *
 */

#ifndef _SCSI_H__
#define _SCSI_H__


/*
 * ******************************************************************************
 * * C O N S T A N T S   A N D   M A C R O S
 * ****************************************************************************** */

/**
 * enum _SCSI_TASK_MGMT_REQUEST_CODES - This enumberation contains the
 *    constants to be used for SCSI task management request codes.  SAM does
 *    not specify any particular values for these codes so constants used here
 *    are the same as those specified in SAS.
 *
 *
 */
enum scsi_task_mgmt_request_codes {
	SCSI_TASK_REQUEST_ABORT_TASK           = 0x01,
	SCSI_TASK_REQUEST_ABORT_TASK_SET       = 0x02,
	SCSI_TASK_REQUEST_CLEAR_TASK_SET       = 0x04,
	SCSI_TASK_REQUEST_LOGICAL_UNIT_RESET   = 0x08,
	SCSI_TASK_REQUEST_I_T_NEXUS_RESET      = 0x10,
	SCSI_TASK_REQUEST_CLEAR_ACA            = 0x40,
	SCSI_TASK_REQUEST_QUERY_TASK           = 0x80,
	SCSI_TASK_REQUEST_QUERY_TASK_SET       = 0x81,
	SCSI_TASK_REQUEST_QUERY_UNIT_ATTENTION = 0x82,

};

/**
 * enum _SCSI_TASK_MGMT_RESPONSE_CODES - This enumeration contains all of the
 *    SCSI task management response codes.
 *
 *
 */
enum scsi_task_mgmt_response_codes {
	SCSI_TASK_MGMT_FUNC_COMPLETE      = 0,
	SCSI_INVALID_FRAME                = 2,
	SCSI_TASK_MGMT_FUNC_NOT_SUPPORTED = 4,
	SCSI_TASK_MGMT_FUNC_FAILED        = 5,
	SCSI_TASK_MGMT_FUNC_SUCCEEDED     = 8,
	SCSI_INVALID_LUN                  = 9
};

/**
 * enum _SCSI_SENSE_RESPONSE_CODE - this enumeration depicts the types of sense
 *    data responses as per SPC-3.
 *
 *
 */
enum scsi_sense_response_code {
	SCSI_FIXED_CURRENT_RESPONSE_CODE       = 0x70,
	SCSI_FIXED_DEFERRED_RESPONSE_CODE      = 0x71,
	SCSI_DESCRIPTOR_CURRENT_RESPONSE_CODE  = 0x72,
	SCSI_DESCRIPTOR_DEFERRED_RESPONSE_CODE = 0x73

};

/*
 * This constant represents the valid bit located in byte 0 of a FIXED
 * format sense data. */
#define SCSI_FIXED_SENSE_DATA_VALID_BIT   0x80

#define SCSI_FIXED_SENSE_DATA_BASE_LENGTH 18

/* This value is used in the DATAPRES field of the SCSI Response IU. */
#define SCSI_RESPONSE_DATA_PRES_SENSE_DATA 0x02

/**
 *
 *
 * SCSI_SENSE_KEYS These constants delineate all of the SCSI protocol sense key
 * constants
 */
#define SCSI_SENSE_NO_SENSE        0x00
#define SCSI_SENSE_RECOVERED_ERROR 0x01
#define SCSI_SENSE_NOT_READY       0x02
#define SCSI_SENSE_MEDIUM_ERROR    0x03
#define SCSI_SENSE_HARDWARE_ERROR  0x04
#define SCSI_SENSE_ILLEGAL_REQUEST 0x05
#define SCSI_SENSE_UNIT_ATTENTION  0x06
#define SCSI_SENSE_DATA_PROTECT    0x07
#define SCSI_SENSE_BLANK_CHECK     0x08
#define SCSI_SENSE_VENDOR_SPECIFIC 0x09
#define SCSI_SENSE_COPY_ABORTED    0x0A
#define SCSI_SENSE_ABORTED_COMMAND 0x0B
#define SCSI_SENSE_VOLUME_OVERFLOW 0x0D
#define SCSI_SENSE_MISCOMPARE      0x0E

/**
 *
 *
 * SCSI_ADDITIONAL_SENSE_CODES These constants delineate all of the SCSI
 * protocol additional sense code constants.
 */
#define SCSI_ASC_NO_ADDITIONAL_SENSE             0x00
#define SCSI_ASC_INITIALIZING_COMMAND_REQUIRED   0x04
#define SCSI_ASC_LUN_SELF_TEST_IN_PROGRESS       0x04
#define SCSI_ASC_LUN_FORMAT_IN_PROGRESS          0x04
#define SCSI_ASC_LUN_NOT_RESPOND_TO_SELECTION    0x05
#define SCSI_ASC_UNRECOVERED_READ_ERROR          0x11
#define SCSI_ASC_INVALID_COMMAND_OPERATION_CODE  0x20
#define SCSI_ASC_LBA_OUT_OF_RANGE                0x21
#define SCSI_ASC_INVALID_FIELD_IN_CDB            0x24
#define SCSI_ASC_INVALID_FIELD_IN_PARM_LIST      0x26
#define SCSI_ASC_WRITE_PROTECTED                 0x27
#define SCSI_ASC_NOT_READY_TO_READY_CHANGE       0x28
#define SCSI_ASC_SAVING_PARMS_NOT_SUPPORTED      0x39
#define SCSI_ASC_MEDIUM_NOT_PRESENT              0x3A
#define SCSI_ASC_INTERNAL_TARGET_FAILURE         0x44
#define SCSI_ASC_IU_CRC_ERROR_DETECTED           0x47
#define SCSI_ASC_MEDIUM_REMOVAL_REQUEST          0x5A
#define SCSI_ASC_COMMAND_SEQUENCE_ERROR          0x2C
#define SCSI_ASC_MEDIA_LOAD_OR_EJECT_FAILED      0x53
#define SCSI_ASC_HARDWARE_IMPENDING_FAILURE      0x5D
#define SCSI_ASC_POWER_STATE_CHANGE              0x5E
#define SCSI_DIAGNOSTIC_FAILURE_ON_COMPONENT     0x40
#define SCSI_ASC_ATA_DEVICE_FEATURE_NOT_ENABLED  0x67

/**
 *
 *
 * SCSI_ADDITIONAL_SENSE_CODE_QUALIFIERS This enumeration contains all of the
 * used SCSI protocol additional sense code qualifier constants.
 */
#define SCSI_ASCQ_NO_ADDITIONAL_SENSE                    0x00
#define SCSI_ASCQ_INVALID_FIELD_IN_CDB                   0x00
#define SCSI_ASCQ_INVALID_FIELD_IN_PARM_LIST             0x00
#define SCSI_ASCQ_LUN_NOT_RESPOND_TO_SELECTION           0x00
#define SCSI_ASCQ_INTERNAL_TARGET_FAILURE                0x00
#define SCSI_ASCQ_LBA_OUT_OF_RANGE                       0x00
#define SCSI_ASCQ_MEDIUM_NOT_PRESENT                     0x00
#define SCSI_ASCQ_NOT_READY_TO_READY_CHANGE              0x00
#define SCSI_ASCQ_WRITE_PROTECTED                        0x00
#define SCSI_ASCQ_UNRECOVERED_READ_ERROR                 0x00
#define SCSI_ASCQ_SAVING_PARMS_NOT_SUPPORTED             0x00
#define SCSI_ASCQ_INVALID_COMMAND_OPERATION_CODE         0x00
#define SCSI_ASCQ_MEDIUM_REMOVAL_REQUEST                 0x01
#define SCSI_ASCQ_INITIALIZING_COMMAND_REQUIRED          0x02
#define SCSI_ASCQ_IU_CRC_ERROR_DETECTED                  0x03
#define SCSI_ASCQ_LUN_FORMAT_IN_PROGRESS                 0x04
#define SCSI_ASCQ_LUN_SELF_TEST_IN_PROGRESS              0x09
#define SCSI_ASCQ_GENERAL_HARD_DRIVE_FAILURE             0x10
#define SCSI_ASCQ_IDLE_CONDITION_ACTIVATE_BY_COMMAND     0x03
#define SCSI_ASCQ_STANDBY_CONDITION_ACTIVATE_BY_COMMAND  0x04
#define SCSI_ASCQ_POWER_STATE_CHANGE_TO_IDLE             0x42
#define SCSI_ASCQ_POWER_STATE_CHANGE_TO_STANDBY          0x43
#define SCSI_ASCQ_ATA_DEVICE_FEATURE_NOT_ENABLED         0x0B
#define SCSI_ASCQ_UNRECOVERED_READ_ERROR_AUTO_REALLOCATE_FAIL    0x04



/**
 *
 *
 * SCSI_STATUS_CODES These constants define all of the used SCSI status values.
 */
#define SCSI_STATUS_GOOD            0x00
#define SCSI_STATUS_CHECK_CONDITION 0x02
#define SCSI_STATUS_CONDITION_MET   0x04
#define SCSI_STATUS_BUSY            0x08
#define SCSI_STATUS_TASKFULL        0x28
#define SCSI_STATUS_ACA             0x30
#define SCSI_STATUS_ABORT           0x40

/**
 *
 *
 * SCSI_OPERATION_CODES These constants delineate all of the SCSI
 * command/operation codes.
 */
#define SCSI_INQUIRY                0x12
#define SCSI_READ_CAPACITY_10       0x25
#define SCSI_SERVICE_ACTION_IN_16   0x9E
#define SCSI_TEST_UNIT_READY        0x00
#define SCSI_START_STOP_UNIT        0x1B
#define SCSI_SYNCHRONIZE_CACHE_10   0x35
#define SCSI_SYNCHRONIZE_CACHE_16   0x91
#define SCSI_REQUEST_SENSE          0x03
#define SCSI_REPORT_LUNS            0xA0
#define SCSI_REASSIGN_BLOCKS        0x07
#define SCSI_READ_6                 0x08
#define SCSI_READ_10                0x28
#define SCSI_READ_12                0xA8
#define SCSI_READ_16                0x88
#define SCSI_WRITE_6                0x0A
#define SCSI_WRITE_10               0x2A
#define SCSI_WRITE_12               0xAA
#define SCSI_WRITE_16               0x8A
#define SCSI_VERIFY_10              0x2F
#define SCSI_VERIFY_12              0xAF
#define SCSI_VERIFY_16              0x8F
#define SCSI_SEEK_6                 0x01
#define SCSI_SEEK_10                0x02
#define SCSI_WRITE_VERIFY           0x2E
#define SCSI_FORMAT_UNIT            0x04
#define SCSI_READ_BUFFER            0x3C
#define SCSI_WRITE_BUFFER           0x3B
#define SCSI_SEND_DIAGNOSTIC        0x1D
#define SCSI_RECEIVE_DIAGNOSTIC     0x1C
#define SCSI_MODE_SENSE_6           0x1A
#define SCSI_MODE_SENSE_10          0x5A
#define SCSI_MODE_SELECT_6          0x15
#define SCSI_MODE_SELECT_10         0x55
#define SCSI_MAINTENANCE_IN         0xA3
#define SCSI_LOG_SENSE              0x4D
#define SCSI_LOG_SELECT             0x4C
#define SCSI_RESERVE_6              0x16
#define SCSI_RESERVE_10             0x56
#define SCSI_RELEASE_6              0x17
#define SCSI_RELEASE_10             0x57
#define SCSI_ATA_PASSTHRU_12        0xA1
#define SCSI_ATA_PASSTHRU_16        0x85
#define SCSI_WRITE_LONG_10          0x3F
#define SCSI_WRITE_LONG_16          0x9F
#define SCSI_PERSISTENT_RESERVE_IN  0x5E
#define SCSI_PERSISTENT_RESERVE_OUT 0x5F

/**
 *
 *
 * SCSI_SERVICE_ACTION_IN_CODES Service action in operations.
 */
#define SCSI_SERVICE_ACTION_IN_CODES_READ_CAPACITY_16     0x10

#define SCSI_SERVICE_ACTION_MASK 0x1f

/**
 *
 *
 * SCSI_MAINTENANCE_IN_SERVICE_ACTION_CODES MAINTENANCE IN service action codes.
 */
#define SCSI_REPORT_TASK_MGMT  0x0D
#define SCSI_REPORT_OP_CODES   0x0C

/**
 *
 *
 * SCSI_MODE_PAGE_CONTROLS These constants delineate all of the used SCSI Mode
 * Page control values.
 */
#define SCSI_MODE_SENSE_PC_CURRENT     0x0
#define SCSI_MODE_SENSE_PC_CHANGEABLE  0x1
#define SCSI_MODE_SENSE_PC_DEFAULT     0x2
#define SCSI_MODE_SENSE_PC_SAVED       0x3

#define SCSI_MODE_SENSE_PC_SHIFT           0x06
#define SCSI_MODE_SENSE_PAGE_CODE_ENABLE   0x3F
#define SCSI_MODE_SENSE_DBD_ENABLE         0x08
#define SCSI_MODE_SENSE_LLBAA_ENABLE       0x10

/**
 *
 *
 * SCSI_MODE_PAGE_CODES These constants delineate all of the used SCSI Mode
 * Page codes.
 */
#define SCSI_MODE_PAGE_READ_WRITE_ERROR           0x01
#define SCSI_MODE_PAGE_DISCONNECT_RECONNECT       0x02
#define SCSI_MODE_PAGE_CACHING                    0x08
#define SCSI_MODE_PAGE_CONTROL                    0x0A
#define SCSI_MODE_PAGE_PROTOCOL_SPECIFIC_PORT     0x19
#define SCSI_MODE_PAGE_POWER_CONDITION            0x1A
#define SCSI_MODE_PAGE_INFORMATIONAL_EXCP_CONTROL 0x1C
#define SCSI_MODE_PAGE_ALL_PAGES                  0x3F

#define SCSI_MODE_SENSE_ALL_SUB_PAGES_CODE         0xFF
#define SCSI_MODE_SENSE_NO_SUB_PAGES_CODE          0x0
#define SCSI_MODE_SENSE_PROTOCOL_PORT_NUM_SUBPAGES 0x1
#define SCSI_MODE_PAGE_CACHE_PAGE_WCE_BIT          0x04
#define SCSI_MODE_PAGE_CACHE_PAGE_DRA_BIT          0x20
#define SCSI_MODE_PAGE_DEXCPT_ENABLE               0x08
#define SCSI_MODE_SENSE_HEADER_FUA_ENABLE          0x10
#define SCSI_MODE_PAGE_POWER_CONDITION_STANDBY     0x1
#define SCSI_MODE_PAGE_POWER_CONDITION_IDLE        0x2

#define SCSI_MODE_SENSE_6_HEADER_LENGTH              4
#define SCSI_MODE_SENSE_10_HEADER_LENGTH             8
#define SCSI_MODE_SENSE_STD_BLOCK_DESCRIPTOR_LENGTH  8
#define SCSI_MODE_SENSE_LLBA_BLOCK_DESCRIPTOR_LENGTH 16

#define SCSI_MODE_PAGE_INFORMATIONAL_EXCP_DXCPT_ENABLE 0x08
#define SCSI_MODE_PAGE_19_SAS_ID         0x6
#define SCSI_MODE_PAGE_19_SUB1_PAGE_NUM  0x1
#define SCSI_MODE_PAGE_19_SUB1_PC        0x59

#define SCSI_MODE_HEADER_MEDIUM_TYPE_SBC 0x00

/* Mode Select constrains related masks value */
#define SCSI_MODE_SELECT_PF_BIT                       0x1
#define SCSI_MODE_SELECT_PF_MASK                      0x10
#define SCSI_MODE_SELECT_MODE_PAGE_MRIE_BYTE          0x6
#define SCSI_MODE_SELECT_MODE_PAGE_MRIE_MASK          0x0F
#define SCSI_MODE_SELECT_MODE_PAGE_SPF_MASK           0x40
#define SCSI_MODE_SELECT_MODE_PAGE_01_AWRE_MASK       0x80
#define SCSI_MODE_SELECT_MODE_PAGE_01_ARRE_MASK       0x40
#define SCSI_MODE_SELECT_MODE_PAGE_01_RC_ERBITS_MASK  0x1F
#define SCSI_MODE_SELECT_MODE_PAGE_08_FSW_LBCSS_NVDIS 0xC1
#define SCSI_MODE_SELECT_MODE_PAGE_1C_PERF_TEST       0x84
#define SCSI_MODE_SELECT_MODE_PAGE_0A_TST_TMF_RLEC    0xF1
#define SCSI_MODE_SELECT_MODE_PAGE_0A_MODIFIER        0xF0
#define SCSI_MODE_SELECT_MODE_PAGE_0A_UA_SWP          0x38
#define SCSI_MODE_SELECT_MODE_PAGE_0A_TAS_AUTO        0x47


#define SCSI_CONTROL_BYTE_NACA_BIT_ENABLE  0x04
#define SCSI_MOVE_FUA_BIT_ENABLE           0x08
#define SCSI_READ_CAPACITY_PMI_BIT_ENABLE  0x01
#define SCSI_READ_CAPACITY_10_DATA_LENGTH  8
#define SCSI_READ_CAPACITY_16_DATA_LENGTH  32

/* Inquiry constants */
#define SCSI_INQUIRY_EVPD_ENABLE          0x01
#define SCSI_INQUIRY_PAGE_CODE_OFFSET     0x02
#define SCSI_INQUIRY_SUPPORTED_PAGES_PAGE 0x00
#define SCSI_INQUIRY_UNIT_SERIAL_NUM_PAGE 0x80
#define SCSI_INQUIRY_DEVICE_ID_PAGE       0x83
#define SCSI_INQUIRY_ATA_INFORMATION_PAGE 0x89
#define SCSI_INQUIRY_BLOCK_DEVICE_PAGE    0xB1
#define SCSI_INQUIRY_BLOCK_DEVICE_LENGTH  0x3C
#define SCSI_INQUIRY_STANDARD_ALLOCATION_LENGTH 0x24    /* 36 */

#define SCSI_REQUEST_SENSE_ALLOCATION_LENGTH   0xFC     /* 252 */

/** Defines the log page codes that are use in gathing Smart data
 */
#define SCSI_LOG_PAGE_SUPPORTED_PAGES       0x00
#define SCSI_LOG_PAGE_INFORMATION_EXCEPTION 0x2F
#define SCSI_LOG_PAGE_SELF_TEST             0x10

/**
 *
 *
 * SCSI_INQUIRY_VPD The following are constants used with vital product data
 * inquiry pages. Values are already shifted into the proper nibble location.
 */
#define SCSI_PIV_ENABLE                 0x80
#define SCSI_LUN_ASSOCIATION            0x00
#define SCSI_TARGET_PORT_ASSOCIATION    0x10

#define SCSI_VEN_UNIQUE_IDENTIFIER_TYPE 0x00
#define SCSI_NAA_IDENTIFIER_TYPE        0x03

#define SCSI_T10_IDENTIFIER_TYPE        0x01
#define SCSI_BINARY_CODE_SET            0x01
#define SCSI_ASCII_CODE_SET             0x02
#define SCSI_FC_PROTOCOL_IDENTIFIER     0x00
#define SCSI_SAS_PROTOCOL_IDENTIFIER    0x60

#define SCSI_VERIFY_BYTCHK_ENABLED      0x02

#define SCSI_SYNCHRONIZE_CACHE_IMMED_ENABLED 0x02
/**
 *
 *
 * SCSI_START_STOP_UNIT_POWER_CONDITION_CODES The following are SCSI Start Stop
 * Unit command Power Condition codes.
 */
#define SCSI_START_STOP_UNIT_POWER_CONDITION_START_VALID       0x0
#define SCSI_START_STOP_UNIT_POWER_CONDITION_ACTIVE            0x1
#define SCSI_START_STOP_UNIT_POWER_CONDITION_IDLE              0x2
#define SCSI_START_STOP_UNIT_POWER_CONDITION_STANDBY           0x3
#define SCSI_START_STOP_UNIT_POWER_CONDITION_LU_CONTROL        0x7
#define SCSI_START_STOP_UNIT_POWER_CONDITION_FORCE_S_CONTROL   0xB

#define SCSI_START_STOP_UNIT_IMMED_MASK            0x1
#define SCSI_START_STOP_UNIT_IMMED_SHIFT           0

#define SCSI_START_STOP_UNIT_START_BIT_MASK        0x1
#define SCSI_START_STOP_UNIT_START_BIT_SHIFT       0

#define SCSI_START_STOP_UNIT_LOEJ_BIT_MASK         0x2
#define SCSI_START_STOP_UNIT_LOEJ_BIT_SHIFT        1

#define SCSI_START_STOP_UNIT_NO_FLUSH_MASK         0x4
#define SCSI_START_STOP_UNIT_NO_FLUSH_SHIFT        2

#define SCSI_START_STOP_UNIT_POWER_CONDITION_MODIFIER_MASK   0xF
#define SCSI_START_STOP_UNIT_POWER_CONDITION_MODIFIER_SHIFT  0

#define SCSI_START_STOP_UNIT_POWER_CONDITION_MASK  0xF0
#define SCSI_START_STOP_UNIT_POWER_CONDITION_SHIFT 4

#define SCSI_LOG_SENSE_PC_FIELD_MASK      0xC0
#define SCSI_LOG_SENSE_PC_FIELD_SHIFT     6

#define SCSI_LOG_SENSE_PAGE_CODE_FIELD_MASK      0x3F
#define SCSI_LOG_SENSE_PAGE_CODE_FIELD_SHIFT     0

/**
 *
 *
 * MRIE - Method of reporting informational exceptions codes
 */
#define NO_REPORTING_INFO_EXCEPTION_CONDITION      0x0
#define ASYNCHRONOUS_EVENT_REPORTING               0x1
#define ESTABLISH_UNIT_ATTENTION_CONDITION         0x2
#define CONDITIONALLY_GENERATE_RECOVERED_ERROR     0x3
#define UNCONDITIONALLY_GENERATE_RECOVERED_ERROR   0x4
#define GENERATE_NO_SENSE                          0x5
#define REPORT_INFO_EXCEPTION_CONDITION_ON_REQUEST 0x6

#define SCSI_INFORMATION_EXCEPTION_DEXCPT_BIT      0x08

/* Reassign Blocks masks */
#define SCSI_REASSIGN_BLOCKS_LONGLBA_BIT           0x02
#define SCSI_REASSIGN_BLOCKS_LONGLIST_BIT          0x01

#endif /* _SCSI_H_ */

