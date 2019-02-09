/*
 * ISHTP bus layer messages handling
 *
 * Copyright (c) 2003-2016, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

#ifndef _ISHTP_HBM_H_
#define _ISHTP_HBM_H_

#include <linux/uuid.h>

struct ishtp_device;
struct ishtp_msg_hdr;
struct ishtp_cl;

/*
 * Timeouts in Seconds
 */
#define ISHTP_INTEROP_TIMEOUT		7 /* Timeout on ready message */

#define ISHTP_CL_CONNECT_TIMEOUT	15 /* HPS: Client Connect Timeout */

/*
 * ISHTP Version
 */
#define HBM_MINOR_VERSION		0
#define HBM_MAJOR_VERSION		1

/* Host bus message command opcode */
#define ISHTP_HBM_CMD_OP_MSK		0x7f
/* Host bus message command RESPONSE */
#define ISHTP_HBM_CMD_RES_MSK		0x80

/*
 * ISHTP Bus Message Command IDs
 */
#define HOST_START_REQ_CMD		0x01
#define HOST_START_RES_CMD		0x81

#define HOST_STOP_REQ_CMD		0x02
#define HOST_STOP_RES_CMD		0x82

#define FW_STOP_REQ_CMD			0x03

#define HOST_ENUM_REQ_CMD		0x04
#define HOST_ENUM_RES_CMD		0x84

#define HOST_CLIENT_PROPERTIES_REQ_CMD	0x05
#define HOST_CLIENT_PROPERTIES_RES_CMD	0x85

#define CLIENT_CONNECT_REQ_CMD		0x06
#define CLIENT_CONNECT_RES_CMD		0x86

#define CLIENT_DISCONNECT_REQ_CMD	0x07
#define CLIENT_DISCONNECT_RES_CMD	0x87

#define ISHTP_FLOW_CONTROL_CMD		0x08

#define DMA_BUFFER_ALLOC_NOTIFY		0x11
#define DMA_BUFFER_ALLOC_RESPONSE	0x91

#define DMA_XFER			0x12
#define DMA_XFER_ACK			0x92

/*
 * ISHTP Stop Reason
 * used by hbm_host_stop_request.reason
 */
#define	DRIVER_STOP_REQUEST		0x00

/*
 * ISHTP BUS Interface Section
 */
struct ishtp_msg_hdr {
	uint32_t fw_addr:8;
	uint32_t host_addr:8;
	uint32_t length:9;
	uint32_t reserved:6;
	uint32_t msg_complete:1;
} __packed;

struct ishtp_bus_message {
	uint8_t hbm_cmd;
	uint8_t data[0];
} __packed;

/**
 * struct hbm_cl_cmd - client specific host bus command
 *	CONNECT, DISCONNECT, and FlOW CONTROL
 *
 * @hbm_cmd - bus message command header
 * @fw_addr - address of the fw client
 * @host_addr - address of the client in the driver
 * @data
 */
struct ishtp_hbm_cl_cmd {
	uint8_t hbm_cmd;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t data;
};

struct hbm_version {
	uint8_t minor_version;
	uint8_t major_version;
} __packed;

struct hbm_host_version_request {
	uint8_t hbm_cmd;
	uint8_t reserved;
	struct hbm_version host_version;
} __packed;

struct hbm_host_version_response {
	uint8_t hbm_cmd;
	uint8_t host_version_supported;
	struct hbm_version fw_max_version;
} __packed;

struct hbm_host_stop_request {
	uint8_t hbm_cmd;
	uint8_t reason;
	uint8_t reserved[2];
} __packed;

struct hbm_host_stop_response {
	uint8_t hbm_cmd;
	uint8_t reserved[3];
} __packed;

struct hbm_host_enum_request {
	uint8_t hbm_cmd;
	uint8_t reserved[3];
} __packed;

struct hbm_host_enum_response {
	uint8_t hbm_cmd;
	uint8_t reserved[3];
	uint8_t valid_addresses[32];
} __packed;

struct ishtp_client_properties {
	uuid_le protocol_name;
	uint8_t protocol_version;
	uint8_t max_number_of_connections;
	uint8_t fixed_address;
	uint8_t single_recv_buf;
	uint32_t max_msg_length;
	uint8_t dma_hdr_len;
#define	ISHTP_CLIENT_DMA_ENABLED	0x80
	uint8_t reserved4;
	uint8_t reserved5;
	uint8_t reserved6;
} __packed;

struct hbm_props_request {
	uint8_t hbm_cmd;
	uint8_t address;
	uint8_t reserved[2];
} __packed;

struct hbm_props_response {
	uint8_t hbm_cmd;
	uint8_t address;
	uint8_t status;
	uint8_t reserved[1];
	struct ishtp_client_properties client_properties;
} __packed;

/**
 * struct hbm_client_connect_request - connect/disconnect request
 *
 * @hbm_cmd - bus message command header
 * @fw_addr - address of the fw client
 * @host_addr - address of the client in the driver
 * @reserved
 */
struct hbm_client_connect_request {
	uint8_t hbm_cmd;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t reserved;
} __packed;

/**
 * struct hbm_client_connect_response - connect/disconnect response
 *
 * @hbm_cmd - bus message command header
 * @fw_addr - address of the fw client
 * @host_addr - address of the client in the driver
 * @status - status of the request
 */
struct hbm_client_connect_response {
	uint8_t hbm_cmd;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t status;
} __packed;


#define ISHTP_FC_MESSAGE_RESERVED_LENGTH		5

struct hbm_flow_control {
	uint8_t hbm_cmd;
	uint8_t fw_addr;
	uint8_t host_addr;
	uint8_t reserved[ISHTP_FC_MESSAGE_RESERVED_LENGTH];
} __packed;

struct dma_alloc_notify {
	uint8_t hbm;
	uint8_t status;
	uint8_t reserved[2];
	uint32_t buf_size;
	uint64_t buf_address;
	/* [...] May come more size/address pairs */
} __packed;

struct dma_xfer_hbm {
	uint8_t hbm;
	uint8_t fw_client_id;
	uint8_t host_client_id;
	uint8_t reserved;
	uint64_t msg_addr;
	uint32_t msg_length;
	uint32_t reserved2;
} __packed;

/* System state */
#define ISHTP_SYSTEM_STATE_CLIENT_ADDR		13

#define SYSTEM_STATE_SUBSCRIBE			0x1
#define SYSTEM_STATE_STATUS			0x2
#define SYSTEM_STATE_QUERY_SUBSCRIBERS		0x3
#define SYSTEM_STATE_STATE_CHANGE_REQ		0x4
/*indicates suspend and resume states*/
#define SUSPEND_STATE_BIT			(1<<1)

struct ish_system_states_header {
	uint32_t cmd;
	uint32_t cmd_status;	/*responses will have this set*/
} __packed;

struct ish_system_states_subscribe {
	struct ish_system_states_header hdr;
	uint32_t states;
} __packed;

struct ish_system_states_status {
	struct ish_system_states_header hdr;
	uint32_t supported_states;
	uint32_t states_status;
} __packed;

struct ish_system_states_query_subscribers {
	struct ish_system_states_header hdr;
} __packed;

struct ish_system_states_state_change_req {
	struct ish_system_states_header hdr;
	uint32_t requested_states;
	uint32_t states_status;
} __packed;

/**
 * enum ishtp_hbm_state - host bus message protocol state
 *
 * @ISHTP_HBM_IDLE : protocol not started
 * @ISHTP_HBM_START : start request message was sent
 * @ISHTP_HBM_ENUM_CLIENTS : enumeration request was sent
 * @ISHTP_HBM_CLIENT_PROPERTIES : acquiring clients properties
 */
enum ishtp_hbm_state {
	ISHTP_HBM_IDLE = 0,
	ISHTP_HBM_START,
	ISHTP_HBM_STARTED,
	ISHTP_HBM_ENUM_CLIENTS,
	ISHTP_HBM_CLIENT_PROPERTIES,
	ISHTP_HBM_WORKING,
	ISHTP_HBM_STOPPED,
};

static inline void ishtp_hbm_hdr(struct ishtp_msg_hdr *hdr, size_t length)
{
	hdr->host_addr = 0;
	hdr->fw_addr = 0;
	hdr->length = length;
	hdr->msg_complete = 1;
	hdr->reserved = 0;
}

int ishtp_hbm_start_req(struct ishtp_device *dev);
int ishtp_hbm_start_wait(struct ishtp_device *dev);
int ishtp_hbm_cl_flow_control_req(struct ishtp_device *dev,
				  struct ishtp_cl *cl);
int ishtp_hbm_cl_disconnect_req(struct ishtp_device *dev, struct ishtp_cl *cl);
int ishtp_hbm_cl_connect_req(struct ishtp_device *dev, struct ishtp_cl *cl);
void ishtp_hbm_enum_clients_req(struct ishtp_device *dev);
void bh_hbm_work_fn(struct work_struct *work);
void recv_hbm(struct ishtp_device *dev, struct ishtp_msg_hdr *ishtp_hdr);
void recv_fixed_cl_msg(struct ishtp_device *dev,
	struct ishtp_msg_hdr *ishtp_hdr);
void ishtp_hbm_dispatch(struct ishtp_device *dev,
	struct ishtp_bus_message *hdr);

void ishtp_query_subscribers(struct ishtp_device *dev);

/* Exported I/F */
void ishtp_send_suspend(struct ishtp_device *dev);
void ishtp_send_resume(struct ishtp_device *dev);

#endif /* _ISHTP_HBM_H_ */
