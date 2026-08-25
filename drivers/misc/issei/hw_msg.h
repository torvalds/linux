/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2023-2026 Intel Corporation */
#ifndef _ISSEI_HW_MSG_H_
#define _ISSEI_HW_MSG_H_

#include <linux/types.h>
#include <linux/uuid.h>

#define HAM_CB_MESSAGE_ID_REQ	0x8086cafe
#define HAM_CB_MESSAGE_ID_RES	0xcafe8086
#define HAM_CB_MESSAGE_VER	0x1

/**
 * struct ham_setup_shared_memory_req - shared memory setup request
 * @msg_id: message id, should be %HAM_CB_MESSAGE_ID_REQ
 * @ver: message version (%HAM_CB_MESSAGE_VER)
 * @reserved: reserved
 * @buffer_physical_address: physical address of DMA buffer
 * @host_to_fw_section_length: memory size for host to fw communication
 * @fw_to_host_section_length: memory size for fw to host communication
 * @control_length: memory size for control buffer
 */
struct ham_setup_shared_memory_req {
	u32 msg_id;
	u16 ver;
	u16 reserved;
	u64 buffer_physical_address;
	u32 host_to_fw_section_length;
	u32 fw_to_host_section_length;
	u32 control_length;
} __packed __aligned(4);

/**
 * struct ham_setup_shared_memory_res - shared memory setup response
 * @msg_id: message id, should be %HAM_CB_MESSAGE_ID_RES
 * @status: operation status
 */
struct ham_setup_shared_memory_res {
	u32 msg_id;
	u32 status;
};

/**
 * struct control_buffer - control buffer structure
 * @h2f_counter_wr: write counter host to fw
 * @h2f_counter_rd: read counter host to fw
 * @f2h_counter_wr: write counter fw to host
 * @f2h_counter_rd: read counter fw to host
 */
struct control_buffer {
	u32 h2f_counter_wr;
	u32 h2f_counter_rd;
	u32 f2h_counter_wr;
	u32 f2h_counter_rd;
};

/* HAM messages over DMA */

/**
 * struct ham_message_header - message header over DMA
 * @length: message length (payload only, not including header)
 * @fw_id: firmware client id (0 means Bus Message)
 * @host_id: host client id (0 means Bus Message)
 * @flags: message flags
 * @status: operation status
 * @reserved: reserved
 */
struct ham_message_header {
	u32 length;
	u16 fw_id;
	u16 host_id;
	u32 flags;
	u32 status;
	u32 reserved;
};

/* Bus Commands */
#define HAM_BUS_CMD_START_REQ 0x00
#define HAM_BUS_CMD_START_RSP 0x80
#define HAM_BUS_CMD_CLIENT_REQ 0x01
#define HAM_BUS_CMD_CLIENT_RSP 0x81

/**
 * struct ham_bus_message - bus message header
 * @cmd: command code
 */
struct ham_bus_message {
	u32 cmd;
};

#define HAM_SUPPORTED_VERSION 0x01

/**
 * struct ham_start_message_req - start message
 * @header: bus message header (%HAM_BUS_CMD_START_REQ)
 * @supported_version: supported protocol version
 * @heci_capabilities_length: protocol capabilities length in bytes
 * @heci_capabilities: protocol capabilities data
 */
struct ham_start_message_req {
	struct ham_bus_message header;
	u16 supported_version;
	u8 heci_capabilities_length;
	u8 heci_capabilities[] __counted_by(heci_capabilities_length);
} __packed;

/**
 * struct ham_start_message_res - start message response
 * @header: bus message header (%HAM_BUS_CMD_START_RSP)
 * @fw_version: firmware version (four u16 blocks)
 * @supported_version: supported protocol version
 * @heci_capabilities_length: protocol capabilities length in bytes
 * @heci_capabilities: protocol capabilities data
 */
struct ham_start_message_res {
	struct ham_bus_message header;
	u16 fw_version[4];
	u16 supported_version;
	u8 heci_capabilities_length;
	u8 heci_capabilities[] __counted_by(heci_capabilities_length);
} __packed;

/**
 * struct ham_get_clients_req - clients list request
 * @header: bus message header (%HAM_BUS_CMD_CLIENT_REQ)
 */
struct ham_get_clients_req {
	struct ham_bus_message header;
};

/**
 * struct ham_client_properties - single client properties
 * @client_number: client id in firmware
 * @protocol_ver: client protocol version
 * @reserved: reserved
 * @client_uuid: protocol name (UUID)
 * @client_mtu: max message length supported by client
 * @flags: client flags
 */
struct ham_client_properties {
	u16 client_number;
	u8 protocol_ver;
	u8 reserved;
	uuid_t client_uuid;
	u32 client_mtu;
	u32 flags;
};

/**
 * struct ham_get_clients_res - client properties response
 * @header: bus message header (%HAM_BUS_CMD_CLIENT_RSP)
 * @client_count: number of clients in firmware
 * @reserved: reserved
 * @clients_props: list of client properties
 */
struct ham_get_clients_res {
	struct ham_bus_message header;
	u16 client_count;
	u16 reserved;
	struct ham_client_properties clients_props[] __counted_by(client_count);
};

#endif /* _ISSEI_HW_MSG_H_ */
