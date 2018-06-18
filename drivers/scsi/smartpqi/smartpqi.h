/*
 *    driver for Microsemi PQI-based storage controllers
 *    Copyright (c) 2016-2017 Microsemi Corporation
 *    Copyright (c) 2016 PMC-Sierra, Inc.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; version 2 of the License.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    Questions/Comments/Bugfixes to esc.storagedev@microsemi.com
 *
 */

#include <linux/io-64-nonatomic-lo-hi.h>

#if !defined(_SMARTPQI_H)
#define _SMARTPQI_H

#pragma pack(1)

#define PQI_DEVICE_SIGNATURE	"PQI DREG"

/* This structure is defined by the PQI specification. */
struct pqi_device_registers {
	__le64	signature;
	u8	function_and_status_code;
	u8	reserved[7];
	u8	max_admin_iq_elements;
	u8	max_admin_oq_elements;
	u8	admin_iq_element_length;	/* in 16-byte units */
	u8	admin_oq_element_length;	/* in 16-byte units */
	__le16	max_reset_timeout;		/* in 100-millisecond units */
	u8	reserved1[2];
	__le32	legacy_intx_status;
	__le32	legacy_intx_mask_set;
	__le32	legacy_intx_mask_clear;
	u8	reserved2[28];
	__le32	device_status;
	u8	reserved3[4];
	__le64	admin_iq_pi_offset;
	__le64	admin_oq_ci_offset;
	__le64	admin_iq_element_array_addr;
	__le64	admin_oq_element_array_addr;
	__le64	admin_iq_ci_addr;
	__le64	admin_oq_pi_addr;
	u8	admin_iq_num_elements;
	u8	admin_oq_num_elements;
	__le16	admin_queue_int_msg_num;
	u8	reserved4[4];
	__le32	device_error;
	u8	reserved5[4];
	__le64	error_details;
	__le32	device_reset;
	__le32	power_action;
	u8	reserved6[104];
};

/*
 * controller registers
 *
 * These are defined by the Microsemi implementation.
 *
 * Some registers (those named sis_*) are only used when in
 * legacy SIS mode before we transition the controller into
 * PQI mode.  There are a number of other SIS mode registers,
 * but we don't use them, so only the SIS registers that we
 * care about are defined here.  The offsets mentioned in the
 * comments are the offsets from the PCIe BAR 0.
 */
struct pqi_ctrl_registers {
	u8	reserved[0x20];
	__le32	sis_host_to_ctrl_doorbell;		/* 20h */
	u8	reserved1[0x34 - (0x20 + sizeof(__le32))];
	__le32	sis_interrupt_mask;			/* 34h */
	u8	reserved2[0x9c - (0x34 + sizeof(__le32))];
	__le32	sis_ctrl_to_host_doorbell;		/* 9Ch */
	u8	reserved3[0xa0 - (0x9c + sizeof(__le32))];
	__le32	sis_ctrl_to_host_doorbell_clear;	/* A0h */
	u8	reserved4[0xb0 - (0xa0 + sizeof(__le32))];
	__le32	sis_driver_scratch;			/* B0h */
	u8	reserved5[0xbc - (0xb0 + sizeof(__le32))];
	__le32	sis_firmware_status;			/* BCh */
	u8	reserved6[0x1000 - (0xbc + sizeof(__le32))];
	__le32	sis_mailbox[8];				/* 1000h */
	u8	reserved7[0x4000 - (0x1000 + (sizeof(__le32) * 8))];
	/*
	 * The PQI spec states that the PQI registers should be at
	 * offset 0 from the PCIe BAR 0.  However, we can't map
	 * them at offset 0 because that would break compatibility
	 * with the SIS registers.  So we map them at offset 4000h.
	 */
	struct pqi_device_registers pqi_registers;	/* 4000h */
};

#define PQI_DEVICE_REGISTERS_OFFSET	0x4000

enum pqi_io_path {
	RAID_PATH = 0,
	AIO_PATH = 1
};

enum pqi_irq_mode {
	IRQ_MODE_NONE,
	IRQ_MODE_INTX,
	IRQ_MODE_MSIX
};

struct pqi_sg_descriptor {
	__le64	address;
	__le32	length;
	__le32	flags;
};

/* manifest constants for the flags field of pqi_sg_descriptor */
#define CISS_SG_LAST	0x40000000
#define CISS_SG_CHAIN	0x80000000

struct pqi_iu_header {
	u8	iu_type;
	u8	reserved;
	__le16	iu_length;	/* in bytes - does not include the length */
				/* of this header */
	__le16	response_queue_id;	/* specifies the OQ where the */
					/*   response IU is to be delivered */
	u8	work_area[2];	/* reserved for driver use */
};

/*
 * According to the PQI spec, the IU header is only the first 4 bytes of our
 * pqi_iu_header structure.
 */
#define PQI_REQUEST_HEADER_LENGTH	4

struct pqi_general_admin_request {
	struct pqi_iu_header header;
	__le16	request_id;
	u8	function_code;
	union {
		struct {
			u8	reserved[33];
			__le32	buffer_length;
			struct pqi_sg_descriptor sg_descriptor;
		} report_device_capability;

		struct {
			u8	reserved;
			__le16	queue_id;
			u8	reserved1[2];
			__le64	element_array_addr;
			__le64	ci_addr;
			__le16	num_elements;
			__le16	element_length;
			u8	queue_protocol;
			u8	reserved2[23];
			__le32	vendor_specific;
		} create_operational_iq;

		struct {
			u8	reserved;
			__le16	queue_id;
			u8	reserved1[2];
			__le64	element_array_addr;
			__le64	pi_addr;
			__le16	num_elements;
			__le16	element_length;
			u8	queue_protocol;
			u8	reserved2[3];
			__le16	int_msg_num;
			__le16	coalescing_count;
			__le32	min_coalescing_time;
			__le32	max_coalescing_time;
			u8	reserved3[8];
			__le32	vendor_specific;
		} create_operational_oq;

		struct {
			u8	reserved;
			__le16	queue_id;
			u8	reserved1[50];
		} delete_operational_queue;

		struct {
			u8	reserved;
			__le16	queue_id;
			u8	reserved1[46];
			__le32	vendor_specific;
		} change_operational_iq_properties;

	} data;
};

struct pqi_general_admin_response {
	struct pqi_iu_header header;
	__le16	request_id;
	u8	function_code;
	u8	status;
	union {
		struct {
			u8	status_descriptor[4];
			__le64	iq_pi_offset;
			u8	reserved[40];
		} create_operational_iq;

		struct {
			u8	status_descriptor[4];
			__le64	oq_ci_offset;
			u8	reserved[40];
		} create_operational_oq;
	} data;
};

struct pqi_iu_layer_descriptor {
	u8	inbound_spanning_supported : 1;
	u8	reserved : 7;
	u8	reserved1[5];
	__le16	max_inbound_iu_length;
	u8	outbound_spanning_supported : 1;
	u8	reserved2 : 7;
	u8	reserved3[5];
	__le16	max_outbound_iu_length;
};

struct pqi_device_capability {
	__le16	data_length;
	u8	reserved[6];
	u8	iq_arbitration_priority_support_bitmask;
	u8	maximum_aw_a;
	u8	maximum_aw_b;
	u8	maximum_aw_c;
	u8	max_arbitration_burst : 3;
	u8	reserved1 : 4;
	u8	iqa : 1;
	u8	reserved2[2];
	u8	iq_freeze : 1;
	u8	reserved3 : 7;
	__le16	max_inbound_queues;
	__le16	max_elements_per_iq;
	u8	reserved4[4];
	__le16	max_iq_element_length;
	__le16	min_iq_element_length;
	u8	reserved5[2];
	__le16	max_outbound_queues;
	__le16	max_elements_per_oq;
	__le16	intr_coalescing_time_granularity;
	__le16	max_oq_element_length;
	__le16	min_oq_element_length;
	u8	reserved6[24];
	struct pqi_iu_layer_descriptor iu_layer_descriptors[32];
};

#define PQI_MAX_EMBEDDED_SG_DESCRIPTORS		4

struct pqi_raid_path_request {
	struct pqi_iu_header header;
	__le16	request_id;
	__le16	nexus_id;
	__le32	buffer_length;
	u8	lun_number[8];
	__le16	protocol_specific;
	u8	data_direction : 2;
	u8	partial : 1;
	u8	reserved1 : 4;
	u8	fence : 1;
	__le16	error_index;
	u8	reserved2;
	u8	task_attribute : 3;
	u8	command_priority : 4;
	u8	reserved3 : 1;
	u8	reserved4 : 2;
	u8	additional_cdb_bytes_usage : 3;
	u8	reserved5 : 3;
	u8	cdb[32];
	struct pqi_sg_descriptor
		sg_descriptors[PQI_MAX_EMBEDDED_SG_DESCRIPTORS];
};

struct pqi_aio_path_request {
	struct pqi_iu_header header;
	__le16	request_id;
	u8	reserved1[2];
	__le32	nexus_id;
	__le32	buffer_length;
	u8	data_direction : 2;
	u8	partial : 1;
	u8	memory_type : 1;
	u8	fence : 1;
	u8	encryption_enable : 1;
	u8	reserved2 : 2;
	u8	task_attribute : 3;
	u8	command_priority : 4;
	u8	reserved3 : 1;
	__le16	data_encryption_key_index;
	__le32	encrypt_tweak_lower;
	__le32	encrypt_tweak_upper;
	u8	cdb[16];
	__le16	error_index;
	u8	num_sg_descriptors;
	u8	cdb_length;
	u8	lun_number[8];
	u8	reserved4[4];
	struct pqi_sg_descriptor
		sg_descriptors[PQI_MAX_EMBEDDED_SG_DESCRIPTORS];
};

struct pqi_io_response {
	struct pqi_iu_header header;
	__le16	request_id;
	__le16	error_index;
	u8	reserved2[4];
};

struct pqi_general_management_request {
	struct pqi_iu_header header;
	__le16	request_id;
	union {
		struct {
			u8	reserved[2];
			__le32	buffer_length;
			struct pqi_sg_descriptor sg_descriptors[3];
		} report_event_configuration;

		struct {
			__le16	global_event_oq_id;
			__le32	buffer_length;
			struct pqi_sg_descriptor sg_descriptors[3];
		} set_event_configuration;
	} data;
};

struct pqi_event_descriptor {
	u8	event_type;
	u8	reserved;
	__le16	oq_id;
};

struct pqi_event_config {
	u8	reserved[2];
	u8	num_event_descriptors;
	u8	reserved1;
	struct pqi_event_descriptor descriptors[1];
};

#define PQI_MAX_EVENT_DESCRIPTORS	255

struct pqi_event_response {
	struct pqi_iu_header header;
	u8	event_type;
	u8	reserved2 : 7;
	u8	request_acknowlege : 1;
	__le16	event_id;
	__le32	additional_event_id;
	u8	data[16];
};

struct pqi_event_acknowledge_request {
	struct pqi_iu_header header;
	u8	event_type;
	u8	reserved2;
	__le16	event_id;
	__le32	additional_event_id;
};

struct pqi_task_management_request {
	struct pqi_iu_header header;
	__le16	request_id;
	__le16	nexus_id;
	u8	reserved[4];
	u8	lun_number[8];
	__le16	protocol_specific;
	__le16	outbound_queue_id_to_manage;
	__le16	request_id_to_manage;
	u8	task_management_function;
	u8	reserved2 : 7;
	u8	fence : 1;
};

#define SOP_TASK_MANAGEMENT_LUN_RESET	0x8

struct pqi_task_management_response {
	struct pqi_iu_header header;
	__le16	request_id;
	__le16	nexus_id;
	u8	additional_response_info[3];
	u8	response_code;
};

struct pqi_aio_error_info {
	u8	status;
	u8	service_response;
	u8	data_present;
	u8	reserved;
	__le32	residual_count;
	__le16	data_length;
	__le16	reserved1;
	u8	data[256];
};

struct pqi_raid_error_info {
	u8	data_in_result;
	u8	data_out_result;
	u8	reserved[3];
	u8	status;
	__le16	status_qualifier;
	__le16	sense_data_length;
	__le16	response_data_length;
	__le32	data_in_transferred;
	__le32	data_out_transferred;
	u8	data[256];
};

#define PQI_REQUEST_IU_TASK_MANAGEMENT			0x13
#define PQI_REQUEST_IU_RAID_PATH_IO			0x14
#define PQI_REQUEST_IU_AIO_PATH_IO			0x15
#define PQI_REQUEST_IU_GENERAL_ADMIN			0x60
#define PQI_REQUEST_IU_REPORT_VENDOR_EVENT_CONFIG	0x72
#define PQI_REQUEST_IU_SET_VENDOR_EVENT_CONFIG		0x73
#define PQI_REQUEST_IU_ACKNOWLEDGE_VENDOR_EVENT		0xf6

#define PQI_RESPONSE_IU_GENERAL_MANAGEMENT		0x81
#define PQI_RESPONSE_IU_TASK_MANAGEMENT			0x93
#define PQI_RESPONSE_IU_GENERAL_ADMIN			0xe0
#define PQI_RESPONSE_IU_RAID_PATH_IO_SUCCESS		0xf0
#define PQI_RESPONSE_IU_AIO_PATH_IO_SUCCESS		0xf1
#define PQI_RESPONSE_IU_RAID_PATH_IO_ERROR		0xf2
#define PQI_RESPONSE_IU_AIO_PATH_IO_ERROR		0xf3
#define PQI_RESPONSE_IU_AIO_PATH_DISABLED		0xf4
#define PQI_RESPONSE_IU_VENDOR_EVENT			0xf5

#define PQI_GENERAL_ADMIN_FUNCTION_REPORT_DEVICE_CAPABILITY	0x0
#define PQI_GENERAL_ADMIN_FUNCTION_CREATE_IQ			0x10
#define PQI_GENERAL_ADMIN_FUNCTION_CREATE_OQ			0x11
#define PQI_GENERAL_ADMIN_FUNCTION_DELETE_IQ			0x12
#define PQI_GENERAL_ADMIN_FUNCTION_DELETE_OQ			0x13
#define PQI_GENERAL_ADMIN_FUNCTION_CHANGE_IQ_PROPERTY		0x14

#define PQI_GENERAL_ADMIN_STATUS_SUCCESS	0x0

#define PQI_IQ_PROPERTY_IS_AIO_QUEUE	0x1

#define PQI_GENERAL_ADMIN_IU_LENGTH		0x3c
#define PQI_PROTOCOL_SOP			0x0

#define PQI_DATA_IN_OUT_GOOD					0x0
#define PQI_DATA_IN_OUT_UNDERFLOW				0x1
#define PQI_DATA_IN_OUT_BUFFER_ERROR				0x40
#define PQI_DATA_IN_OUT_BUFFER_OVERFLOW				0x41
#define PQI_DATA_IN_OUT_BUFFER_OVERFLOW_DESCRIPTOR_AREA		0x42
#define PQI_DATA_IN_OUT_BUFFER_OVERFLOW_BRIDGE			0x43
#define PQI_DATA_IN_OUT_PCIE_FABRIC_ERROR			0x60
#define PQI_DATA_IN_OUT_PCIE_COMPLETION_TIMEOUT			0x61
#define PQI_DATA_IN_OUT_PCIE_COMPLETER_ABORT_RECEIVED		0x62
#define PQI_DATA_IN_OUT_PCIE_UNSUPPORTED_REQUEST_RECEIVED	0x63
#define PQI_DATA_IN_OUT_PCIE_ECRC_CHECK_FAILED			0x64
#define PQI_DATA_IN_OUT_PCIE_UNSUPPORTED_REQUEST		0x65
#define PQI_DATA_IN_OUT_PCIE_ACS_VIOLATION			0x66
#define PQI_DATA_IN_OUT_PCIE_TLP_PREFIX_BLOCKED			0x67
#define PQI_DATA_IN_OUT_PCIE_POISONED_MEMORY_READ		0x6F
#define PQI_DATA_IN_OUT_ERROR					0xf0
#define PQI_DATA_IN_OUT_PROTOCOL_ERROR				0xf1
#define PQI_DATA_IN_OUT_HARDWARE_ERROR				0xf2
#define PQI_DATA_IN_OUT_UNSOLICITED_ABORT			0xf3
#define PQI_DATA_IN_OUT_ABORTED					0xf4
#define PQI_DATA_IN_OUT_TIMEOUT					0xf5

#define CISS_CMD_STATUS_SUCCESS			0x0
#define CISS_CMD_STATUS_TARGET_STATUS		0x1
#define CISS_CMD_STATUS_DATA_UNDERRUN		0x2
#define CISS_CMD_STATUS_DATA_OVERRUN		0x3
#define CISS_CMD_STATUS_INVALID			0x4
#define CISS_CMD_STATUS_PROTOCOL_ERROR		0x5
#define CISS_CMD_STATUS_HARDWARE_ERROR		0x6
#define CISS_CMD_STATUS_CONNECTION_LOST		0x7
#define CISS_CMD_STATUS_ABORTED			0x8
#define CISS_CMD_STATUS_ABORT_FAILED		0x9
#define CISS_CMD_STATUS_UNSOLICITED_ABORT	0xa
#define CISS_CMD_STATUS_TIMEOUT			0xb
#define CISS_CMD_STATUS_UNABORTABLE		0xc
#define CISS_CMD_STATUS_TMF			0xd
#define CISS_CMD_STATUS_AIO_DISABLED		0xe

#define PQI_CMD_STATUS_ABORTED	CISS_CMD_STATUS_ABORTED

#define PQI_NUM_EVENT_QUEUE_ELEMENTS	32
#define PQI_EVENT_OQ_ELEMENT_LENGTH	sizeof(struct pqi_event_response)

#define PQI_EVENT_TYPE_HOTPLUG			0x1
#define PQI_EVENT_TYPE_HARDWARE			0x2
#define PQI_EVENT_TYPE_PHYSICAL_DEVICE		0x4
#define PQI_EVENT_TYPE_LOGICAL_DEVICE		0x5
#define PQI_EVENT_TYPE_AIO_STATE_CHANGE		0xfd
#define PQI_EVENT_TYPE_AIO_CONFIG_CHANGE	0xfe

#pragma pack()

#define PQI_ERROR_BUFFER_ELEMENT_LENGTH		\
	sizeof(struct pqi_raid_error_info)

/* these values are based on our implementation */
#define PQI_ADMIN_IQ_NUM_ELEMENTS		8
#define PQI_ADMIN_OQ_NUM_ELEMENTS		20
#define PQI_ADMIN_IQ_ELEMENT_LENGTH		64
#define PQI_ADMIN_OQ_ELEMENT_LENGTH		64

#define PQI_OPERATIONAL_IQ_ELEMENT_LENGTH	128
#define PQI_OPERATIONAL_OQ_ELEMENT_LENGTH	16

#define PQI_MIN_MSIX_VECTORS		1
#define PQI_MAX_MSIX_VECTORS		64

/* these values are defined by the PQI spec */
#define PQI_MAX_NUM_ELEMENTS_ADMIN_QUEUE	255
#define PQI_MAX_NUM_ELEMENTS_OPERATIONAL_QUEUE	65535
#define PQI_QUEUE_ELEMENT_ARRAY_ALIGNMENT	64
#define PQI_QUEUE_ELEMENT_LENGTH_ALIGNMENT	16
#define PQI_ADMIN_INDEX_ALIGNMENT		64
#define PQI_OPERATIONAL_INDEX_ALIGNMENT		4

#define PQI_MIN_OPERATIONAL_QUEUE_ID		1
#define PQI_MAX_OPERATIONAL_QUEUE_ID		65535

#define PQI_AIO_SERV_RESPONSE_COMPLETE		0
#define PQI_AIO_SERV_RESPONSE_FAILURE		1
#define PQI_AIO_SERV_RESPONSE_TMF_COMPLETE	2
#define PQI_AIO_SERV_RESPONSE_TMF_SUCCEEDED	3
#define PQI_AIO_SERV_RESPONSE_TMF_REJECTED	4
#define PQI_AIO_SERV_RESPONSE_TMF_INCORRECT_LUN	5

#define PQI_AIO_STATUS_IO_ERROR			0x1
#define PQI_AIO_STATUS_IO_ABORTED		0x2
#define PQI_AIO_STATUS_NO_PATH_TO_DEVICE	0x3
#define PQI_AIO_STATUS_INVALID_DEVICE		0x4
#define PQI_AIO_STATUS_AIO_PATH_DISABLED	0xe
#define PQI_AIO_STATUS_UNDERRUN			0x51
#define PQI_AIO_STATUS_OVERRUN			0x75

typedef u32 pqi_index_t;

/* SOP data direction flags */
#define SOP_NO_DIRECTION_FLAG	0
#define SOP_WRITE_FLAG		1	/* host writes data to Data-Out */
					/* buffer */
#define SOP_READ_FLAG		2	/* host receives data from Data-In */
					/* buffer */
#define SOP_BIDIRECTIONAL	3	/* data is transferred from the */
					/* Data-Out buffer and data is */
					/* transferred to the Data-In buffer */

#define SOP_TASK_ATTRIBUTE_SIMPLE		0
#define SOP_TASK_ATTRIBUTE_HEAD_OF_QUEUE	1
#define SOP_TASK_ATTRIBUTE_ORDERED		2
#define SOP_TASK_ATTRIBUTE_ACA			4

#define SOP_TMF_COMPLETE		0x0
#define SOP_TMF_FUNCTION_SUCCEEDED	0x8

/* additional CDB bytes usage field codes */
#define SOP_ADDITIONAL_CDB_BYTES_0	0	/* 16-byte CDB */
#define SOP_ADDITIONAL_CDB_BYTES_4	1	/* 20-byte CDB */
#define SOP_ADDITIONAL_CDB_BYTES_8	2	/* 24-byte CDB */
#define SOP_ADDITIONAL_CDB_BYTES_12	3	/* 28-byte CDB */
#define SOP_ADDITIONAL_CDB_BYTES_16	4	/* 32-byte CDB */

/*
 * The purpose of this structure is to obtain proper alignment of objects in
 * an admin queue pair.
 */
struct pqi_admin_queues_aligned {
	__aligned(PQI_QUEUE_ELEMENT_ARRAY_ALIGNMENT)
		u8	iq_element_array[PQI_ADMIN_IQ_ELEMENT_LENGTH]
					[PQI_ADMIN_IQ_NUM_ELEMENTS];
	__aligned(PQI_QUEUE_ELEMENT_ARRAY_ALIGNMENT)
		u8	oq_element_array[PQI_ADMIN_OQ_ELEMENT_LENGTH]
					[PQI_ADMIN_OQ_NUM_ELEMENTS];
	__aligned(PQI_ADMIN_INDEX_ALIGNMENT) pqi_index_t iq_ci;
	__aligned(PQI_ADMIN_INDEX_ALIGNMENT) pqi_index_t oq_pi;
};

struct pqi_admin_queues {
	void		*iq_element_array;
	void		*oq_element_array;
	volatile pqi_index_t *iq_ci;
	volatile pqi_index_t *oq_pi;
	dma_addr_t	iq_element_array_bus_addr;
	dma_addr_t	oq_element_array_bus_addr;
	dma_addr_t	iq_ci_bus_addr;
	dma_addr_t	oq_pi_bus_addr;
	__le32 __iomem	*iq_pi;
	pqi_index_t	iq_pi_copy;
	__le32 __iomem	*oq_ci;
	pqi_index_t	oq_ci_copy;
	struct task_struct *task;
	u16		int_msg_num;
};

struct pqi_queue_group {
	struct pqi_ctrl_info *ctrl_info;	/* backpointer */
	u16		iq_id[2];
	u16		oq_id;
	u16		int_msg_num;
	void		*iq_element_array[2];
	void		*oq_element_array;
	dma_addr_t	iq_element_array_bus_addr[2];
	dma_addr_t	oq_element_array_bus_addr;
	__le32 __iomem	*iq_pi[2];
	pqi_index_t	iq_pi_copy[2];
	volatile pqi_index_t *iq_ci[2];
	volatile pqi_index_t *oq_pi;
	dma_addr_t	iq_ci_bus_addr[2];
	dma_addr_t	oq_pi_bus_addr;
	__le32 __iomem	*oq_ci;
	pqi_index_t	oq_ci_copy;
	spinlock_t	submit_lock[2];	/* protect submission queue */
	struct list_head request_list[2];
};

struct pqi_event_queue {
	u16		oq_id;
	u16		int_msg_num;
	void		*oq_element_array;
	volatile pqi_index_t *oq_pi;
	dma_addr_t	oq_element_array_bus_addr;
	dma_addr_t	oq_pi_bus_addr;
	__le32 __iomem	*oq_ci;
	pqi_index_t	oq_ci_copy;
};

#define PQI_DEFAULT_QUEUE_GROUP		0
#define PQI_MAX_QUEUE_GROUPS		PQI_MAX_MSIX_VECTORS

struct pqi_encryption_info {
	u16	data_encryption_key_index;
	u32	encrypt_tweak_lower;
	u32	encrypt_tweak_upper;
};

#pragma pack(1)

#define PQI_CONFIG_TABLE_SIGNATURE	"CFGTABLE"
#define PQI_CONFIG_TABLE_MAX_LENGTH	((u16)~0)

/* configuration table section IDs */
#define PQI_CONFIG_TABLE_SECTION_GENERAL_INFO		0
#define PQI_CONFIG_TABLE_SECTION_FIRMWARE_FEATURES	1
#define PQI_CONFIG_TABLE_SECTION_FIRMWARE_ERRATA	2
#define PQI_CONFIG_TABLE_SECTION_DEBUG			3
#define PQI_CONFIG_TABLE_SECTION_HEARTBEAT		4

struct pqi_config_table {
	u8	signature[8];		/* "CFGTABLE" */
	__le32	first_section_offset;	/* offset in bytes from the base */
					/* address of this table to the */
					/* first section */
};

struct pqi_config_table_section_header {
	__le16	section_id;		/* as defined by the */
					/* PQI_CONFIG_TABLE_SECTION_* */
					/* manifest constants above */
	__le16	next_section_offset;	/* offset in bytes from base */
					/* address of the table of the */
					/* next section or 0 if last entry */
};

struct pqi_config_table_general_info {
	struct pqi_config_table_section_header header;
	__le32	section_length;		/* size of this section in bytes */
					/* including the section header */
	__le32	max_outstanding_requests;	/* max. outstanding */
						/* commands supported by */
						/* the controller */
	__le32	max_sg_size;		/* max. transfer size of a single */
					/* command */
	__le32	max_sg_per_request;	/* max. number of scatter-gather */
					/* entries supported in a single */
					/* command */
};

struct pqi_config_table_debug {
	struct pqi_config_table_section_header header;
	__le32	scratchpad;
};

struct pqi_config_table_heartbeat {
	struct pqi_config_table_section_header header;
	__le32	heartbeat_counter;
};

union pqi_reset_register {
	struct {
		u32	reset_type : 3;
		u32	reserved : 2;
		u32	reset_action : 3;
		u32	hold_in_pd1 : 1;
		u32	reserved2 : 23;
	} bits;
	u32	all_bits;
};

#define PQI_RESET_ACTION_RESET		0x1

#define PQI_RESET_TYPE_NO_RESET		0x0
#define PQI_RESET_TYPE_SOFT_RESET	0x1
#define PQI_RESET_TYPE_FIRM_RESET	0x2
#define PQI_RESET_TYPE_HARD_RESET	0x3

#define PQI_RESET_ACTION_COMPLETED	0x2

#define PQI_RESET_POLL_INTERVAL_MSECS	100

#define PQI_MAX_OUTSTANDING_REQUESTS		((u32)~0)
#define PQI_MAX_OUTSTANDING_REQUESTS_KDUMP	32
#define PQI_MAX_TRANSFER_SIZE			(1024U * 1024U)
#define PQI_MAX_TRANSFER_SIZE_KDUMP		(512 * 1024U)

#define RAID_MAP_MAX_ENTRIES		1024

#define PQI_PHYSICAL_DEVICE_BUS		0
#define PQI_RAID_VOLUME_BUS		1
#define PQI_HBA_BUS			2
#define PQI_EXTERNAL_RAID_VOLUME_BUS	3
#define PQI_MAX_BUS			PQI_EXTERNAL_RAID_VOLUME_BUS

struct report_lun_header {
	__be32	list_length;
	u8	extended_response;
	u8	reserved[3];
};

struct report_log_lun_extended_entry {
	u8	lunid[8];
	u8	volume_id[16];
};

struct report_log_lun_extended {
	struct report_lun_header header;
	struct report_log_lun_extended_entry lun_entries[1];
};

struct report_phys_lun_extended_entry {
	u8	lunid[8];
	__be64	wwid;
	u8	device_type;
	u8	device_flags;
	u8	lun_count;	/* number of LUNs in a multi-LUN device */
	u8	redundant_paths;
	u32	aio_handle;
};

/* for device_flags field of struct report_phys_lun_extended_entry */
#define REPORT_PHYS_LUN_DEV_FLAG_AIO_ENABLED	0x8

struct report_phys_lun_extended {
	struct report_lun_header header;
	struct report_phys_lun_extended_entry lun_entries[1];
};

struct raid_map_disk_data {
	u32	aio_handle;
	u8	xor_mult[2];
	u8	reserved[2];
};

/* constants for flags field of RAID map */
#define RAID_MAP_ENCRYPTION_ENABLED	0x1

struct raid_map {
	__le32	structure_size;		/* size of entire structure in bytes */
	__le32	volume_blk_size;	/* bytes / block in the volume */
	__le64	volume_blk_cnt;		/* logical blocks on the volume */
	u8	phys_blk_shift;		/* shift factor to convert between */
					/* units of logical blocks and */
					/* physical disk blocks */
	u8	parity_rotation_shift;	/* shift factor to convert between */
					/* units of logical stripes and */
					/* physical stripes */
	__le16	strip_size;		/* blocks used on each disk / stripe */
	__le64	disk_starting_blk;	/* first disk block used in volume */
	__le64	disk_blk_cnt;		/* disk blocks used by volume / disk */
	__le16	data_disks_per_row;	/* data disk entries / row in the map */
	__le16	metadata_disks_per_row;	/* mirror/parity disk entries / row */
					/* in the map */
	__le16	row_cnt;		/* rows in each layout map */
	__le16	layout_map_count;	/* layout maps (1 map per */
					/* mirror parity group) */
	__le16	flags;
	__le16	data_encryption_key_index;
	u8	reserved[16];
	struct raid_map_disk_data disk_data[RAID_MAP_MAX_ENTRIES];
};

#pragma pack()

#define RAID_CTLR_LUNID		"\0\0\0\0\0\0\0\0"

struct pqi_scsi_dev {
	int	devtype;		/* as reported by INQUIRY commmand */
	u8	device_type;		/* as reported by */
					/* BMIC_IDENTIFY_PHYSICAL_DEVICE */
					/* only valid for devtype = TYPE_DISK */
	int	bus;
	int	target;
	int	lun;
	u8	scsi3addr[8];
	__be64	wwid;
	u8	volume_id[16];
	u8	is_physical_device : 1;
	u8	is_external_raid_device : 1;
	u8	target_lun_valid : 1;
	u8	device_gone : 1;
	u8	new_device : 1;
	u8	keep_device : 1;
	u8	volume_offline : 1;
	bool	aio_enabled;		/* only valid for physical disks */
	bool	in_reset;
	bool	device_offline;
	u8	vendor[8];		/* bytes 8-15 of inquiry data */
	u8	model[16];		/* bytes 16-31 of inquiry data */
	u64	sas_address;
	u8	raid_level;
	u16	queue_depth;		/* max. queue_depth for this device */
	u16	advertised_queue_depth;
	u32	aio_handle;
	u8	volume_status;
	u8	active_path_index;
	u8	path_map;
	u8	bay;
	u8	box[8];
	u16	phys_connector[8];
	bool	raid_bypass_configured;	/* RAID bypass configured */
	bool	raid_bypass_enabled;	/* RAID bypass enabled */
	int	offload_to_mirror;	/* Send next RAID bypass request */
					/* to mirror drive. */
	struct raid_map *raid_map;	/* RAID bypass map */

	struct pqi_sas_port *sas_port;
	struct scsi_device *sdev;

	struct list_head scsi_device_list_entry;
	struct list_head new_device_list_entry;
	struct list_head add_list_entry;
	struct list_head delete_list_entry;

	atomic_t scsi_cmds_outstanding;
};

/* VPD inquiry pages */
#define SCSI_VPD_SUPPORTED_PAGES	0x0	/* standard page */
#define SCSI_VPD_DEVICE_ID		0x83	/* standard page */
#define CISS_VPD_LV_DEVICE_GEOMETRY	0xc1	/* vendor-specific page */
#define CISS_VPD_LV_BYPASS_STATUS	0xc2	/* vendor-specific page */
#define CISS_VPD_LV_STATUS		0xc3	/* vendor-specific page */

#define VPD_PAGE	(1 << 8)

#pragma pack(1)

/* structure for CISS_VPD_LV_STATUS */
struct ciss_vpd_logical_volume_status {
	u8	peripheral_info;
	u8	page_code;
	u8	reserved;
	u8	page_length;
	u8	volume_status;
	u8	reserved2[3];
	__be32	flags;
};

#pragma pack()

/* constants for volume_status field of ciss_vpd_logical_volume_status */
#define CISS_LV_OK					0
#define CISS_LV_FAILED					1
#define CISS_LV_NOT_CONFIGURED				2
#define CISS_LV_DEGRADED				3
#define CISS_LV_READY_FOR_RECOVERY			4
#define CISS_LV_UNDERGOING_RECOVERY			5
#define CISS_LV_WRONG_PHYSICAL_DRIVE_REPLACED		6
#define CISS_LV_PHYSICAL_DRIVE_CONNECTION_PROBLEM	7
#define CISS_LV_HARDWARE_OVERHEATING			8
#define CISS_LV_HARDWARE_HAS_OVERHEATED			9
#define CISS_LV_UNDERGOING_EXPANSION			10
#define CISS_LV_NOT_AVAILABLE				11
#define CISS_LV_QUEUED_FOR_EXPANSION			12
#define CISS_LV_DISABLED_SCSI_ID_CONFLICT		13
#define CISS_LV_EJECTED					14
#define CISS_LV_UNDERGOING_ERASE			15
/* state 16 not used */
#define CISS_LV_READY_FOR_PREDICTIVE_SPARE_REBUILD	17
#define CISS_LV_UNDERGOING_RPI				18
#define CISS_LV_PENDING_RPI				19
#define CISS_LV_ENCRYPTED_NO_KEY			20
/* state 21 not used */
#define CISS_LV_UNDERGOING_ENCRYPTION			22
#define CISS_LV_UNDERGOING_ENCRYPTION_REKEYING		23
#define CISS_LV_ENCRYPTED_IN_NON_ENCRYPTED_CONTROLLER	24
#define CISS_LV_PENDING_ENCRYPTION			25
#define CISS_LV_PENDING_ENCRYPTION_REKEYING		26
#define CISS_LV_NOT_SUPPORTED				27
#define CISS_LV_STATUS_UNAVAILABLE			255

/* constants for flags field of ciss_vpd_logical_volume_status */
#define CISS_LV_FLAGS_NO_HOST_IO	0x1	/* volume not available for */
						/* host I/O */

/* for SAS hosts and SAS expanders */
struct pqi_sas_node {
	struct device *parent_dev;
	struct list_head port_list_head;
};

struct pqi_sas_port {
	struct list_head port_list_entry;
	u64	sas_address;
	struct sas_port *port;
	int	next_phy_index;
	struct list_head phy_list_head;
	struct pqi_sas_node *parent_node;
	struct sas_rphy *rphy;
};

struct pqi_sas_phy {
	struct list_head phy_list_entry;
	struct sas_phy *phy;
	struct pqi_sas_port *parent_port;
	bool	added_to_port;
};

struct pqi_io_request {
	atomic_t	refcount;
	u16		index;
	void (*io_complete_callback)(struct pqi_io_request *io_request,
		void *context);
	void		*context;
	u8		raid_bypass : 1;
	int		status;
	struct pqi_queue_group *queue_group;
	struct scsi_cmnd *scmd;
	void		*error_info;
	struct pqi_sg_descriptor *sg_chain_buffer;
	dma_addr_t	sg_chain_buffer_dma_handle;
	void		*iu;
	struct list_head request_list_entry;
};

#define PQI_NUM_SUPPORTED_EVENTS	6

struct pqi_event {
	bool	pending;
	u8	event_type;
	__le16	event_id;
	__le32	additional_event_id;
};

#define PQI_RESERVED_IO_SLOTS_LUN_RESET			1
#define PQI_RESERVED_IO_SLOTS_EVENT_ACK			PQI_NUM_SUPPORTED_EVENTS
#define PQI_RESERVED_IO_SLOTS_SYNCHRONOUS_REQUESTS	3
#define PQI_RESERVED_IO_SLOTS				\
	(PQI_RESERVED_IO_SLOTS_LUN_RESET + PQI_RESERVED_IO_SLOTS_EVENT_ACK + \
	PQI_RESERVED_IO_SLOTS_SYNCHRONOUS_REQUESTS)

struct pqi_ctrl_info {
	unsigned int	ctrl_id;
	struct pci_dev	*pci_dev;
	char		firmware_version[11];
	void __iomem	*iomem_base;
	struct pqi_ctrl_registers __iomem *registers;
	struct pqi_device_registers __iomem *pqi_registers;
	u32		max_sg_entries;
	u32		config_table_offset;
	u32		config_table_length;
	u16		max_inbound_queues;
	u16		max_elements_per_iq;
	u16		max_iq_element_length;
	u16		max_outbound_queues;
	u16		max_elements_per_oq;
	u16		max_oq_element_length;
	u32		max_transfer_size;
	u32		max_outstanding_requests;
	u32		max_io_slots;
	unsigned int	scsi_ml_can_queue;
	unsigned short	sg_tablesize;
	unsigned int	max_sectors;
	u32		error_buffer_length;
	void		*error_buffer;
	dma_addr_t	error_buffer_dma_handle;
	size_t		sg_chain_buffer_length;
	unsigned int	num_queue_groups;
	u16		max_hw_queue_index;
	u16		num_elements_per_iq;
	u16		num_elements_per_oq;
	u16		max_inbound_iu_length_per_firmware;
	u16		max_inbound_iu_length;
	unsigned int	max_sg_per_iu;
	void		*admin_queue_memory_base;
	u32		admin_queue_memory_length;
	dma_addr_t	admin_queue_memory_base_dma_handle;
	void		*queue_memory_base;
	u32		queue_memory_length;
	dma_addr_t	queue_memory_base_dma_handle;
	struct pqi_admin_queues admin_queues;
	struct pqi_queue_group queue_groups[PQI_MAX_QUEUE_GROUPS];
	struct pqi_event_queue event_queue;
	enum pqi_irq_mode irq_mode;
	int		max_msix_vectors;
	int		num_msix_vectors_enabled;
	int		num_msix_vectors_initialized;
	int		event_irq;
	struct Scsi_Host *scsi_host;

	struct mutex	scan_mutex;
	struct mutex	lun_reset_mutex;
	bool		controller_online;
	bool		block_requests;
	u8		inbound_spanning_supported : 1;
	u8		outbound_spanning_supported : 1;
	u8		pqi_mode_enabled : 1;
	u8		pqi_reset_quiesce_supported : 1;

	struct list_head scsi_device_list;
	spinlock_t	scsi_device_list_lock;

	struct delayed_work rescan_work;
	struct delayed_work update_time_work;

	struct pqi_sas_node *sas_host;
	u64		sas_address;

	struct pqi_io_request *io_request_pool;
	u16		next_io_request_slot;

	struct pqi_event events[PQI_NUM_SUPPORTED_EVENTS];
	struct work_struct event_work;

	atomic_t	num_interrupts;
	int		previous_num_interrupts;
	u32		previous_heartbeat_count;
	__le32 __iomem	*heartbeat_counter;
	struct timer_list heartbeat_timer;
	struct work_struct ctrl_offline_work;

	struct semaphore sync_request_sem;
	atomic_t	num_busy_threads;
	atomic_t	num_blocked_threads;
	wait_queue_head_t block_requests_wait;

	struct list_head raid_bypass_retry_list;
	spinlock_t	raid_bypass_retry_list_lock;
	struct work_struct raid_bypass_retry_work;
};

enum pqi_ctrl_mode {
	SIS_MODE = 0,
	PQI_MODE
};

/*
 * assume worst case: SATA queue depth of 31 minus 4 internal firmware commands
 */
#define PQI_PHYSICAL_DISK_DEFAULT_MAX_QUEUE_DEPTH	27

/* CISS commands */
#define CISS_READ		0xc0
#define CISS_REPORT_LOG		0xc2	/* Report Logical LUNs */
#define CISS_REPORT_PHYS	0xc3	/* Report Physical LUNs */
#define CISS_GET_RAID_MAP	0xc8

/* constants for CISS_REPORT_LOG/CISS_REPORT_PHYS commands */
#define CISS_REPORT_LOG_EXTENDED		0x1
#define CISS_REPORT_PHYS_EXTENDED		0x2

/* BMIC commands */
#define BMIC_IDENTIFY_CONTROLLER		0x11
#define BMIC_IDENTIFY_PHYSICAL_DEVICE		0x15
#define BMIC_READ				0x26
#define BMIC_WRITE				0x27
#define BMIC_SENSE_CONTROLLER_PARAMETERS	0x64
#define BMIC_SENSE_SUBSYSTEM_INFORMATION	0x66
#define BMIC_WRITE_HOST_WELLNESS		0xa5
#define BMIC_FLUSH_CACHE			0xc2

#define SA_FLUSH_CACHE				0x1

#define MASKED_DEVICE(lunid)			((lunid)[3] & 0xc0)
#define CISS_GET_LEVEL_2_BUS(lunid)		((lunid)[7] & 0x3f)
#define CISS_GET_LEVEL_2_TARGET(lunid)		((lunid)[6])
#define CISS_GET_DRIVE_NUMBER(lunid)		\
	(((CISS_GET_LEVEL_2_BUS((lunid)) - 1) << 8) + \
	CISS_GET_LEVEL_2_TARGET((lunid)))

#define NO_TIMEOUT		((unsigned long) -1)

#pragma pack(1)

struct bmic_identify_controller {
	u8	configured_logical_drive_count;
	__le32	configuration_signature;
	u8	firmware_version[4];
	u8	reserved[145];
	__le16	extended_logical_unit_count;
	u8	reserved1[34];
	__le16	firmware_build_number;
	u8	reserved2[100];
	u8	controller_mode;
	u8	reserved3[32];
};

struct bmic_identify_physical_device {
	u8	scsi_bus;		/* SCSI Bus number on controller */
	u8	scsi_id;		/* SCSI ID on this bus */
	__le16	block_size;		/* sector size in bytes */
	__le32	total_blocks;		/* number for sectors on drive */
	__le32	reserved_blocks;	/* controller reserved (RIS) */
	u8	model[40];		/* Physical Drive Model */
	u8	serial_number[40];	/* Drive Serial Number */
	u8	firmware_revision[8];	/* drive firmware revision */
	u8	scsi_inquiry_bits;	/* inquiry byte 7 bits */
	u8	compaq_drive_stamp;	/* 0 means drive not stamped */
	u8	last_failure_reason;
	u8	flags;
	u8	more_flags;
	u8	scsi_lun;		/* SCSI LUN for phys drive */
	u8	yet_more_flags;
	u8	even_more_flags;
	__le32	spi_speed_rules;
	u8	phys_connector[2];	/* connector number on controller */
	u8	phys_box_on_bus;	/* phys enclosure this drive resides */
	u8	phys_bay_in_box;	/* phys drv bay this drive resides */
	__le32	rpm;			/* drive rotational speed in RPM */
	u8	device_type;		/* type of drive */
	u8	sata_version;		/* only valid when device_type = */
					/* BMIC_DEVICE_TYPE_SATA */
	__le64	big_total_block_count;
	__le64	ris_starting_lba;
	__le32	ris_size;
	u8	wwid[20];
	u8	controller_phy_map[32];
	__le16	phy_count;
	u8	phy_connected_dev_type[256];
	u8	phy_to_drive_bay_num[256];
	__le16	phy_to_attached_dev_index[256];
	u8	box_index;
	u8	reserved;
	__le16	extra_physical_drive_flags;
	u8	negotiated_link_rate[256];
	u8	phy_to_phy_map[256];
	u8	redundant_path_present_map;
	u8	redundant_path_failure_map;
	u8	active_path_number;
	__le16	alternate_paths_phys_connector[8];
	u8	alternate_paths_phys_box_on_port[8];
	u8	multi_lun_device_lun_count;
	u8	minimum_good_fw_revision[8];
	u8	unique_inquiry_bytes[20];
	u8	current_temperature_degrees;
	u8	temperature_threshold_degrees;
	u8	max_temperature_degrees;
	u8	logical_blocks_per_phys_block_exp;
	__le16	current_queue_depth_limit;
	u8	switch_name[10];
	__le16	switch_port;
	u8	alternate_paths_switch_name[40];
	u8	alternate_paths_switch_port[8];
	__le16	power_on_hours;
	__le16	percent_endurance_used;
	u8	drive_authentication;
	u8	smart_carrier_authentication;
	u8	smart_carrier_app_fw_version;
	u8	smart_carrier_bootloader_fw_version;
	u8	sanitize_flags;
	u8	encryption_key_flags;
	u8	encryption_key_name[64];
	__le32	misc_drive_flags;
	__le16	dek_index;
	__le16	hba_drive_encryption_flags;
	__le16	max_overwrite_time;
	__le16	max_block_erase_time;
	__le16	max_crypto_erase_time;
	u8	connector_info[5];
	u8	connector_name[8][8];
	u8	page_83_identifier[16];
	u8	maximum_link_rate[256];
	u8	negotiated_physical_link_rate[256];
	u8	box_connector_name[8];
	u8	padding_to_multiple_of_512[9];
};

struct bmic_flush_cache {
	u8	disable_flag;
	u8	system_power_action;
	u8	ndu_flush;
	u8	shutdown_event;
	u8	reserved[28];
};

/* for shutdown_event member of struct bmic_flush_cache */
enum bmic_flush_cache_shutdown_event {
	NONE_CACHE_FLUSH_ONLY = 0,
	SHUTDOWN = 1,
	HIBERNATE = 2,
	SUSPEND = 3,
	RESTART = 4
};

#pragma pack()

int pqi_add_sas_host(struct Scsi_Host *shost, struct pqi_ctrl_info *ctrl_info);
void pqi_delete_sas_host(struct pqi_ctrl_info *ctrl_info);
int pqi_add_sas_device(struct pqi_sas_node *pqi_sas_node,
	struct pqi_scsi_dev *device);
void pqi_remove_sas_device(struct pqi_scsi_dev *device);
struct pqi_scsi_dev *pqi_find_device_by_sas_rphy(
	struct pqi_ctrl_info *ctrl_info, struct sas_rphy *rphy);
void pqi_prep_for_scsi_done(struct scsi_cmnd *scmd);

extern struct sas_function_template pqi_sas_transport_functions;

#endif /* _SMARTPQI_H */
