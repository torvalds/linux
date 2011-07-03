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

#include "intel_sata.h"
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
 * struct sci_sas_identify_address_frame_protocols - This structure depicts the
 *    contents of bytes 2 and 3 in the SAS IDENTIFY ADDRESS FRAME (IAF).
 *
 * For specific information on each of these individual fields please reference
 * the SAS specification Link layer section on address frames.
 */
struct sci_sas_identify_address_frame_protocols {
	union {
		struct {
			u16 restricted1:1;
			u16 smp_initiator:1;
			u16 stp_initiator:1;
			u16 ssp_initiator:1;
			u16 reserved3:4;
			u16 restricted2:1;
			u16 smp_target:1;
			u16 stp_target:1;
			u16 ssp_target:1;
			u16 reserved4:4;
		} bits;

		u16 all;
	} u;

};

/**
 * struct sci_sas_identify_address_frame - This structure depicts the contents
 *    of the SAS IDENTIFY ADDRESS FRAME (IAF).
 *
 * For specific information on each of these individual fields please reference
 * the SAS specification Link layer section on address frames.
 */
struct sci_sas_identify_address_frame {
	u16 address_frame_type:4;
	u16 device_type:3;
	u16 reserved1:1;
	u16 reason:4;
	u16 reserved2:4;

	struct sci_sas_identify_address_frame_protocols protocols;

	struct sci_sas_address device_name;
	struct sci_sas_address sas_address;

	u32 phy_identifier:8;
	u32 break_reply_capable:1;
	u32 requested_in_zpsds:1;
	u32 in_zpsds_persistent:1;
	u32 reserved5:21;

	u32 reserved6[4];

};

/**
 * struct sas_capabilities - This structure depicts the various SAS
 *    capabilities supported by the directly attached target device.  For
 *    specific information on each of these individual fields please reference
 *    the SAS specification Phy layer section on speed negotiation windows.
 *
 *
 */
struct sas_capabilities {
	union {
#if defined (SCIC_SDS_4_ENABLED)
		struct {
			/**
			 * The SAS specification indicates the start bit shall always be set to
			 * 1.  This implementation will have the start bit set to 0 if the
			 * PHY CAPABILITIES were either not received or speed negotiation failed.
			 */
			u32 start:1;
			u32 tx_ssc_type:1;
			u32 reserved1:2;
			u32 requested_logical_link_rate:4;

			u32 gen1_without_ssc_supported:1;
			u32 gen1_with_ssc_supported:1;
			u32 gen2_without_ssc_supported:1;
			u32 gen2_with_ssc_supported:1;
			u32 gen3_without_ssc_supported:1;
			u32 gen3_with_ssc_supported:1;
			u32 reserved2:17;
			u32 parity:1;
		} bits;
#endif          /* (SCIC_SDS_4_ENABLED) */

		u32 all;
	} u;

};

/**
 * enum _SCI_SAS_LINK_RATE - This enumeration depicts the SAS specification
 *    defined link speeds.
 *
 *
 */
enum sci_sas_link_rate {
	SCI_SAS_NO_LINK_RATE = 0,
	SCI_SATA_SPINUP_HOLD = 0x3,
	SCI_SAS_150_GB = 0x8,
	SCI_SAS_300_GB = 0x9,
	SCI_SAS_600_GB = 0xA
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
 * struct sci_ssp_command_iu - This structure depicts the contents of the SSP
 *    COMMAND INFORMATION UNIT. For specific information on each of these
 *    individual fields please reference the SAS specification SSP transport
 *    layer section.
 *
 *
 */
struct sci_ssp_command_iu {
	u32 lun_upper;
	u32 lun_lower;

	u32 additional_cdb_length:6;
	u32 reserved0:2;
	u32 reserved1:8;
	u32 enable_first_burst:1;
	u32 task_priority:4;
	u32 task_attribute:3;
	u32 reserved2:8;

	u32 cdb[4];

};

/**
 * struct sci_ssp_task_iu - This structure depicts the contents of the SSP TASK
 *    INFORMATION UNIT. For specific information on each of these individual
 *    fields please reference the SAS specification SSP transport layer section.
 *
 *
 */
struct sci_ssp_task_iu {
	u32 lun_upper;
	u32 lun_lower;

	u32 reserved0:8;
	u32 task_function:8;
	u32 reserved1:8;
	u32 reserved2:8;

	u32 reserved3:16;
	u32 task_tag:16;

	u32 reserved4[3];

};

#define SSP_RESPONSE_IU_MAX_DATA 64

#define SCI_SSP_RESPONSE_IU_DATA_PRESENT_MASK   (0x03)


#define sci_ssp_get_sense_data_length(sense_data_length_buffer)	\
	SCIC_BUILD_DWORD(sense_data_length_buffer)

#define sci_ssp_get_response_data_length(response_data_length_buffer) \
	SCIC_BUILD_DWORD(response_data_length_buffer)

/**
 * struct sci_ssp_response_iu - This structure depicts the contents of the SSP
 *    RESPONSE INFORMATION UNIT. For specific information on each of these
 *    individual fields please reference the SAS specification SSP transport
 *    layer section.
 *
 *
 */
struct sci_ssp_response_iu {
	u8 reserved0[8];

	u8 retry_delay_timer[2];
	u8 data_present;
	u8 status;

	u8 reserved1[4];
	u8 sense_data_length[4];
	u8 response_data_length[4];

	u32 data[SSP_RESPONSE_IU_MAX_DATA];

};

/**
 * enum _SCI_SAS_DATA_PRESENT_TYPE - This enumeration depicts the SAS
 *    specification defined SSP data present types in struct sci_ssp_response_iu.
 *
 *
 */
enum sci_ssp_response_iu_data_present_type {
	SCI_SSP_RESPONSE_IU_NO_DATA = 0x00,
	SCI_SSP_RESPONSE_IU_RESPONSE_DATA = 0x01,
	SCI_SSP_RESPONSE_IU_SENSE_DATA = 0x02
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

/**
 * struct smp_request_header - This structure defines the contents of an SMP
 *    Request header.
 *
 * For specific information on each of these individual fields please reference
 * the SAS specification.
 */
struct smp_request_header {
	u8 smp_frame_type;              /* byte 0 */
	u8 function;                    /* byte 1 */
	u8 allocated_response_length;   /* byte 2 */
	u8 request_length;              /* byte 3 */
};

/**
 * struct smp_response_header - This structure depicts the contents of the SAS
 *    SMP DISCOVER RESPONSE frame.  For specific information on each of these
 *    individual fields please reference the SAS specification Link layer
 *    section on address frames.
 *
 *
 */
struct smp_response_header {
	u8 smp_frame_type;      /* byte 0 */
	u8 function;            /* byte 1 */
	u8 function_result;     /* byte 2 */
	u8 response_length;     /* byte 3 */
};

/**
 * struct smp_request_general - This structure defines the contents of an SMP
 *    Request that is comprised of the struct smp_request_header and a CRC.
 *
 * For specific information on each of these individual fields please reference
 * the SAS specification.
 */
struct smp_request_general {
	u32 crc;      /* bytes 4-7 */

};

/**
 * struct smp_request_phy_identifier - This structure defines the contents of
 *    an SMP Request that is comprised of the struct smp_request_header and a phy
 *    identifier. Examples: SMP_REQUEST_DISCOVER, SMP_REQUEST_REPORT_PHY_SATA.
 *
 * For specific information on each of these individual fields please reference
 * the SAS specification.
 */
struct smp_request_phy_identifier {
	u32 reserved_byte4_7;           /* bytes 4-7 */

	u32 ignore_zone_group:1;      /* byte 8 */
	u32 reserved_byte8:7;

	u32 phy_identifier:8;         /* byte 9 */
	u32 reserved_byte10:8;        /* byte 10 */
	u32 reserved_byte11:8;        /* byte 11 */

};

/**
 * struct smp_request_configure_route_information - This structure defines the
 *    contents of an SMP Configure Route Information request.
 *
 * For specific information on each of these individual fields please reference
 * the SAS specification.
 */
struct smp_request_configure_route_information {
	u32 expected_expander_change_count:16;        /* bytes 4-5 */
	u32 expander_route_index_high:8;
	u32 expander_route_index:8;                   /* bytes 6-7 */

	u32 reserved_byte8:8;                         /* bytes 8 */
	u32 phy_identifier:8;                         /* bytes 9 */
	u32 reserved_byte_10_11:16;                   /* bytes 10-11 */

	u32 reserved_byte_12_bit_0_6:7;
	u32 disable_route_entry:1;    /* byte 12 */
	u32 reserved_byte_13_15:24;   /* bytes 13-15 */

	u32 routed_sas_address[2];      /* bytes 16-23 */
	u8 reserved_byte_24_39[16];     /* bytes 24-39 */

};

/**
 * struct smp_request_phy_control - This structure defines the contents of an
 *    SMP Phy Controler request.
 *
 * For specific information on each of these individual fields please reference
 * the SAS specification.
 */
struct smp_request_phy_control {
	u16 expected_expander_change_count;     /* byte 4-5 */

	u16 reserved_byte_6_7;                  /* byte 6-7 */
	u8 reserved_byte_8;                     /* byte 8 */

	u8 phy_identifier;                      /* byte 9 */
	u8 phy_operation;                       /* byte 10 */

	u8 update_partial_pathway_timeout_value:1;
	u8 reserved_byte_11_bit_1_7:7;        /* byte 11 */

	u8 reserved_byte_12_23[12];             /* byte 12-23 */

	u8 attached_device_name[8];             /* byte 24-31 */

	u8 reserved_byte_32_bit_3_0:4;        /* byte 32 */
	u8 programmed_minimum_physical_link_rate:4;

	u8 reserved_byte_33_bit_3_0:4; /* byte 33 */
	u8 programmed_maximum_physical_link_rate:4;

	u16 reserved_byte_34_35; /* byte 34-35 */

	u8 partial_pathway_timeout_value:4;
	u8 reserved_byte_36_bit_4_7:4;        /* byte 36 */

	u16 reserved_byte_37_38;                /* byte 37-38 */
	u8 reserved_byte_39;                    /* byte 39 */

};

/**
 * struct smp_request_vendor_specific - This structure depicts the vendor
 *    specific space for SMP request.
 *
 *
 */
 #define SMP_REQUEST_VENDOR_SPECIFIC_MAX_LENGTH 1016
struct smp_request_vendor_specific {
	u8 request_bytes[SMP_REQUEST_VENDOR_SPECIFIC_MAX_LENGTH];
};

/**
 * struct smp_request - This structure simply unionizes the existing request
 *    structures into a common request type.
 *
 *
 */
struct smp_request {
	struct smp_request_header header;

	union { /* bytes 4-N */
		struct smp_request_general report_general;
		struct smp_request_phy_identifier discover;
		struct smp_request_general report_manufacturer_information;
		struct smp_request_phy_identifier report_phy_sata;
		struct smp_request_phy_control phy_control;
		struct smp_request_phy_identifier report_phy_error_log;
		struct smp_request_phy_identifier report_route_information;
		struct smp_request_configure_route_information configure_route_information;
		struct smp_request_vendor_specific vendor_specific_request;
	} request;

};


/**
 * struct smp_response_report_general - This structure depicts the SMP Report
 *    General for expander devices.  It adheres to the SAS-2.1 specification.
 *
 * For specific information on each of these individual fields please reference
 * the SAS specification Application layer section on SMP.
 */
struct smp_response_report_general {
	u16 expander_change_count;              /* byte 4-5 */
	u16 expander_route_indexes;             /* byte 6-7 */

	u32 reserved_byte8:7;                 /* byte 8 bit 0-6 */
	u32 long_response:1;                  /* byte 8 bit 7 */

	u32 number_of_phys:8;                 /* byte 9 */

	u32 configurable_route_table:1;       /* byte 10 */
	u32 configuring:1;
	u32 configures_others:1;
	u32 open_reject_retry_supported:1;
	u32 stp_continue_awt:1;
	u32 self_configuring:1;
	u32 zone_configuring:1;
	u32 table_to_table_supported:1;

	u32 reserved_byte11:8;                /* byte 11 */

	u32 enclosure_logical_identifier_high;  /* byte 12-15 */
	u32 enclosure_logical_identifier_low;   /* byte 16-19 */

	u32 reserved_byte20_23;
	u32 reserved_byte24_27;

};

struct smp_response_report_general_long {
	struct smp_response_report_general sas1_1;

	struct {
		u16 reserved1;
		u16 stp_bus_inactivity_time_limit;
		u16 stp_max_connect_time_limit;
		u16 stp_smp_i_t_nexus_loss_time;

		u32 zoning_enabled:1;
		u32 zoning_supported:1;
		u32 physicaL_presence_asserted:1;
		u32 zone_locked:1;
		u32 reserved2:1;
		u32 num_zone_groups:3;
		u32 saving_zoning_enabled_supported:3;
		u32 saving_zone_perms_table_supported:1;
		u32 saving_zone_phy_info_supported:1;
		u32 saving_zone_manager_password_supported:1;
		u32 saving:1;
		u32 reserved3:1;
		u32 max_number_routed_sas_addresses:16;

		struct sci_sas_address active_zone_manager_sas_address;

		u16 zone_lock_inactivity_time_limit;
		u16 reserved4;

		u8 reserved5;
		u8 first_enclosure_connector_element_index;
		u8 number_of_enclosure_connector_element_indices;
		u8 reserved6;

		u32 reserved7:7;
		u32 reduced_functionality:1;
		u32 time_to_reduce_functionality:8;
		u32 initial_time_to_reduce_functionality:8;
		u8 max_reduced_functionality_time;

		u16 last_self_config_status_descriptor_index;
		u16 max_number_of_stored_self_config_status_descriptors;

		u16 last_phy_event_list_descriptor_index;
		u16 max_number_of_stored_phy_event_list_descriptors;
	} sas2;

};

/**
 * struct smp_response_report_manufacturer_information - This structure depicts
 *    the SMP report manufacturer information for expander devices.  It adheres
 *    to the SAS-2.1 specification.
 *
 * For specific information on each of these individual fields please reference
 * the SAS specification Application layer section on SMP.
 */
struct smp_response_report_manufacturer_information {
	u32 expander_change_count:16; /* bytes 4-5 */
	u32 reserved1:16;

	u32 sas1_1_format:1;
	u32 reserved2:31;

	u8 vendor_id[8];
	u8 product_id[16];
	u8 product_revision_level[4];
	u8 component_vendor_id[8];
	u8 component_id[2];
	u8 component_revision_level;
	u8 reserved3;
	u8 vendor_specific[8];

};

#define SMP_RESPONSE_DISCOVER_FORMAT_1_1_SIZE 52
#define SMP_RESPONSE_DISCOVER_FORMAT_2_SIZE   116

/**
 * struct smp_discover_response_protocols - This structure depicts the discover
 *    response where the supported protocols by the remote phy are specified.
 *
 * For specific information on each of these individual fields please reference
 * the SAS specification Link layer section on address frames.
 */
struct smp_discover_response_protocols {
	union {
		struct {
			u16 attached_sata_host:1;
			u16 attached_smp_initiator:1;
			u16 attached_stp_initiator:1;
			u16 attached_ssp_initiator:1;
			u16 reserved3:4;
			u16 attached_sata_device:1;
			u16 attached_smp_target:1;
			u16 attached_stp_target:1;
			u16 attached_ssp_target:1;
			u16 reserved4:3;
			u16 attached_sata_port_selector:1;
		} bits;

		u16 all;
	} u;

};

/**
 * struct SMP_RESPONSE_DISCOVER_FORMAT - This structure defines the SMP phy
 *    discover response format. It handles both SAS1.1 and SAS 2 definitions.
 *    The unions indicate locations where the SAS specification versions differ
 *    from one another.
 *
 *
 */
struct smp_response_discover {

	union {
		struct {
			u8 reserved[2];
		} sas1_1;

		struct {
			u16 expander_change_count;
		} sas2;

	} u1;

	u8 reserved1[3];
	u8 phy_identifier;
	u8 reserved2[2];

	union {
		struct {
			u16 reserved1:4;
			u16 attached_device_type:3;
			u16 reserved2:1;
			u16 negotiated_physical_link_rate:4;
			u16 reserved3:4;
		} sas1_1;

		struct {
			u16 attached_reason:4;
			u16 attached_device_type:3;
			u16 reserved2:1;
			u16 negotiated_logical_link_rate:4;
			u16 reserved3:4;
		} sas2;

	} u2;

	struct smp_discover_response_protocols protocols;
	struct sci_sas_address sas_address;
	struct sci_sas_address attached_sas_address;

	u8 attached_phy_identifier;

	union {
		struct {
			u8 reserved;
		} sas1_1;

		struct {
			u8 attached_break_reply_capable:1;
			u8 attached_requested_inside_zpsds:1;
			u8 attached_inside_zpsds_persistent:1;
			u8 reserved1:5;
		} sas2;

	} u3;

	u8 reserved_for_identify[6];

	u32 hardware_min_physical_link_rate:4;
	u32 programmed_min_physical_link_rate:4;
	u32 hardware_max_physical_link_rate:4;
	u32 programmed_max_physical_link_rate:4;
	u32 phy_change_count:8;
	u32 partial_pathway_timeout_value:4;
	u32 reserved5:3;
	u32 virtual_phy:1;

	u32 routing_attribute:4;
	u32 reserved6:4;
	u32 connector_type:7;
	u32 reserved7:1;
	u32 connector_element_index:8;
	u32 connector_physical_link:8;

	u16 reserved8;
	u16 vendor_specific;

	union {
		struct {
			/**
			 * In the SAS 1.1 specification this structure ends after 52 bytes.
			 * As a result, the contents of this field should never have a
			 * real value.  It is undefined.
			 */
			u8 undefined[SMP_RESPONSE_DISCOVER_FORMAT_2_SIZE
				     - SMP_RESPONSE_DISCOVER_FORMAT_1_1_SIZE];
		} sas1_1;

		struct {
			struct sci_sas_address attached_device_name;

			u32 zoning_enabled:1;
			u32 inside_zpsds:1;
			u32 zone_group_persistent:1;
			u32 reserved1:1;
			u32 requested_inside_zpsds:1;
			u32 inside_zpsds_persistent:1;
			u32 requested_inside_zpsds_changed_by_expander:1;
			u32 reserved2:1;
			u32 reserved_for_zoning_fields:16;
			u32 zone_group:8;

			u8 self_configuration_status;
			u8 self_configuration_levels_completed;
			u16 reserved_for_self_config_fields;

			struct sci_sas_address self_configuration_sas_address;

			u32 programmed_phy_capabilities;
			u32 current_phy_capabilities;
			u32 attached_phy_capabilities;

			u32 reserved3;

			u32 reserved4:16;
			u32 negotiated_physical_link_rate:4;
			u32 reason:4;
			u32 hardware_muxing_supported:1;
			u32 negotiated_ssc:1;
			u32 reserved5:6;

			u32 default_zoning_enabled:1;
			u32 reserved6:1;
			u32 default_zone_group_persistent:1;
			u32 reserved7:1;
			u32 default_requested_inside_zpsds:1;
			u32 default_inside_zpsds_persistent:1;
			u32 reserved8:2;
			u32 reserved9:16;
			u32 default_zone_group:8;

			u32 saved_zoning_enabled:1;
			u32 reserved10:1;
			u32 saved_zone_group_persistent:1;
			u32 reserved11:1;
			u32 saved_requested_inside_zpsds:1;
			u32 saved_inside_zpsds_persistent:1;
			u32 reserved12:18;
			u32 saved_zone_group:8;

			u32 reserved14:2;
			u32 shadow_zone_group_persistent:1;
			u32 reserved15:1;
			u32 shadow_requested_inside_zpsds:1;
			u32 shadow_inside_zpsds_persistent:1;
			u32 reserved16:18;
			u32 shadow_zone_group:8;

			u8 device_slot_number;
			u8 device_slot_group_number;
			u8 device_slot_group_output_connector[6];
		} sas2;

	} u4;

};

/**
 * struct smp_response_report_phy_sata - This structure depicts the contents of
 *    the SAS SMP REPORT PHY SATA frame.  For specific information on each of
 *    these individual fields please reference the SAS specification Link layer
 *    section on address frames.
 *
 *
 */
struct smp_response_report_phy_sata {
	u32 ignored_byte_4_7; /* bytes 4-7 */

	u32 affiliations_valid:1;
	u32 affiliations_supported:1;
	u32 reserved_byte11:6;        /* byte 11 */
	u32 ignored_byte10:8;         /* byte 10 */
	u32 phy_identifier:8;         /* byte  9 */
	u32 reserved_byte_8:8;        /* byte  8 */

	u32 reserved_12_15;
	u32 stp_sas_address[2];
	u8 device_to_host_fis[20];
	u32 reserved_44_47;
	u32 affiliated_stp_initiator_sas_address[2];

};

struct smp_response_vendor_specific {
	u8 response_bytes[SMP_REQUEST_VENDOR_SPECIFIC_MAX_LENGTH];
};

union smp_response_body {
	struct smp_response_report_general report_general;
	struct smp_response_report_manufacturer_information report_manufacturer_information;
	struct smp_response_discover discover;
	struct smp_response_report_phy_sata report_phy_sata;
	struct smp_response_vendor_specific vendor_specific_response;
};

/**
 * struct smp_response - This structure simply unionizes the existing response
 *    structures into a common response type.
 *
 *
 */
struct smp_response {
	struct smp_response_header header;

	union smp_response_body response;

};

/* SMP Request Functions */
#define SMP_FUNCTION_REPORT_GENERAL                   0x00
#define SMP_FUNCTION_REPORT_MANUFACTURER_INFORMATION  0x01
#define SMP_FUNCTION_DISCOVER                         0x10
#define SMP_FUNCTION_REPORT_PHY_ERROR_LOG             0x11
#define SMP_FUNCTION_REPORT_PHY_SATA                  0x12
#define SMP_FUNCTION_REPORT_ROUTE_INFORMATION         0X13
#define SMP_FUNCTION_CONFIGURE_ROUTE_INFORMATION      0X90
#define SMP_FUNCTION_PHY_CONTROL                      0x91
#define SMP_FUNCTION_PHY_TEST                         0x92

#define SMP_FRAME_TYPE_REQUEST          0x40
#define SMP_FRAME_TYPE_RESPONSE         0x41

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

