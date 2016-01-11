/*
 *
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2003-2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#ifndef _MEI_HW_TYPES_H_
#define _MEI_HW_TYPES_H_

#include <linux/uuid.h>

/*
 * Timeouts in Seconds
 */
#define MEI_HW_READY_TIMEOUT        2  /* Timeout on ready message */
#define MEI_CONNECT_TIMEOUT         3  /* HPS: at least 2 seconds */

#define MEI_CL_CONNECT_TIMEOUT     15  /* HPS: Client Connect Timeout */
#define MEI_CLIENTS_INIT_TIMEOUT   15  /* HPS: Clients Enumeration Timeout */

#define MEI_IAMTHIF_STALL_TIMER    12  /* HPS */
#define MEI_IAMTHIF_READ_TIMER     10  /* HPS */

#define MEI_PGI_TIMEOUT             1  /* PG Isolation time response 1 sec */
#define MEI_D0I3_TIMEOUT            5  /* D0i3 set/unset max response time */
#define MEI_HBM_TIMEOUT             1  /* 1 second */

/*
 * MEI Version
 */
#define HBM_MINOR_VERSION                   0
#define HBM_MAJOR_VERSION                   2

/*
 * MEI version with PGI support
 */
#define HBM_MINOR_VERSION_PGI               1
#define HBM_MAJOR_VERSION_PGI               1

/*
 * MEI version with Dynamic clients support
 */
#define HBM_MINOR_VERSION_DC               0
#define HBM_MAJOR_VERSION_DC               2

/*
 * MEI version with disconnect on connection timeout support
 */
#define HBM_MINOR_VERSION_DOT              0
#define HBM_MAJOR_VERSION_DOT              2

/*
 * MEI version with notifcation support
 */
#define HBM_MINOR_VERSION_EV               0
#define HBM_MAJOR_VERSION_EV               2

/* Host bus message command opcode */
#define MEI_HBM_CMD_OP_MSK                  0x7f
/* Host bus message command RESPONSE */
#define MEI_HBM_CMD_RES_MSK                 0x80

/*
 * MEI Bus Message Command IDs
 */
#define HOST_START_REQ_CMD                  0x01
#define HOST_START_RES_CMD                  0x81

#define HOST_STOP_REQ_CMD                   0x02
#define HOST_STOP_RES_CMD                   0x82

#define ME_STOP_REQ_CMD                     0x03

#define HOST_ENUM_REQ_CMD                   0x04
#define HOST_ENUM_RES_CMD                   0x84

#define HOST_CLIENT_PROPERTIES_REQ_CMD      0x05
#define HOST_CLIENT_PROPERTIES_RES_CMD      0x85

#define CLIENT_CONNECT_REQ_CMD              0x06
#define CLIENT_CONNECT_RES_CMD              0x86

#define CLIENT_DISCONNECT_REQ_CMD           0x07
#define CLIENT_DISCONNECT_RES_CMD           0x87

#define MEI_FLOW_CONTROL_CMD                0x08

#define MEI_PG_ISOLATION_ENTRY_REQ_CMD      0x0a
#define MEI_PG_ISOLATION_ENTRY_RES_CMD      0x8a
#define MEI_PG_ISOLATION_EXIT_REQ_CMD       0x0b
#define MEI_PG_ISOLATION_EXIT_RES_CMD       0x8b

#define MEI_HBM_ADD_CLIENT_REQ_CMD          0x0f
#define MEI_HBM_ADD_CLIENT_RES_CMD          0x8f

#define MEI_HBM_NOTIFY_REQ_CMD              0x10
#define MEI_HBM_NOTIFY_RES_CMD              0x90
#define MEI_HBM_NOTIFICATION_CMD            0x11

/*
 * MEI Stop Reason
 * used by hbm_host_stop_request.reason
 */
enum mei_stop_reason_types {
	DRIVER_STOP_REQUEST = 0x00,
	DEVICE_D1_ENTRY = 0x01,
	DEVICE_D2_ENTRY = 0x02,
	DEVICE_D3_ENTRY = 0x03,
	SYSTEM_S1_ENTRY = 0x04,
	SYSTEM_S2_ENTRY = 0x05,
	SYSTEM_S3_ENTRY = 0x06,
	SYSTEM_S4_ENTRY = 0x07,
	SYSTEM_S5_ENTRY = 0x08
};


/**
 * enum mei_hbm_status  - mei host bus messages return values
 *
 * @MEI_HBMS_SUCCESS           : status success
 * @MEI_HBMS_CLIENT_NOT_FOUND  : client not found
 * @MEI_HBMS_ALREADY_EXISTS    : connection already established
 * @MEI_HBMS_REJECTED          : connection is rejected
 * @MEI_HBMS_INVALID_PARAMETER : invalid parameter
 * @MEI_HBMS_NOT_ALLOWED       : operation not allowed
 * @MEI_HBMS_ALREADY_STARTED   : system is already started
 * @MEI_HBMS_NOT_STARTED       : system not started
 *
 * @MEI_HBMS_MAX               : sentinel
 */
enum mei_hbm_status {
	MEI_HBMS_SUCCESS           = 0,
	MEI_HBMS_CLIENT_NOT_FOUND  = 1,
	MEI_HBMS_ALREADY_EXISTS    = 2,
	MEI_HBMS_REJECTED          = 3,
	MEI_HBMS_INVALID_PARAMETER = 4,
	MEI_HBMS_NOT_ALLOWED       = 5,
	MEI_HBMS_ALREADY_STARTED   = 6,
	MEI_HBMS_NOT_STARTED       = 7,

	MEI_HBMS_MAX
};


/*
 * Client Connect Status
 * used by hbm_client_connect_response.status
 */
enum mei_cl_connect_status {
	MEI_CL_CONN_SUCCESS          = MEI_HBMS_SUCCESS,
	MEI_CL_CONN_NOT_FOUND        = MEI_HBMS_CLIENT_NOT_FOUND,
	MEI_CL_CONN_ALREADY_STARTED  = MEI_HBMS_ALREADY_EXISTS,
	MEI_CL_CONN_OUT_OF_RESOURCES = MEI_HBMS_REJECTED,
	MEI_CL_CONN_MESSAGE_SMALL    = MEI_HBMS_INVALID_PARAMETER,
	MEI_CL_CONN_NOT_ALLOWED      = MEI_HBMS_NOT_ALLOWED,
};

/*
 * Client Disconnect Status
 */
enum  mei_cl_disconnect_status {
	MEI_CL_DISCONN_SUCCESS = MEI_HBMS_SUCCESS
};

/*
 *  MEI BUS Interface Section
 */
struct mei_msg_hdr {
	u32 me_addr:8;
	u32 host_addr:8;
	u32 length:9;
	u32 reserved:5;
	u32 internal:1;
	u32 msg_complete:1;
} __packed;


struct mei_bus_message {
	u8 hbm_cmd;
	u8 data[0];
} __packed;

/**
 * struct hbm_cl_cmd - client specific host bus command
 *	CONNECT, DISCONNECT, and FlOW CONTROL
 *
 * @hbm_cmd: bus message command header
 * @me_addr: address of the client in ME
 * @host_addr: address of the client in the driver
 * @data: generic data
 */
struct mei_hbm_cl_cmd {
	u8 hbm_cmd;
	u8 me_addr;
	u8 host_addr;
	u8 data;
};

struct hbm_version {
	u8 minor_version;
	u8 major_version;
} __packed;

struct hbm_host_version_request {
	u8 hbm_cmd;
	u8 reserved;
	struct hbm_version host_version;
} __packed;

struct hbm_host_version_response {
	u8 hbm_cmd;
	u8 host_version_supported;
	struct hbm_version me_max_version;
} __packed;

struct hbm_host_stop_request {
	u8 hbm_cmd;
	u8 reason;
	u8 reserved[2];
} __packed;

struct hbm_host_stop_response {
	u8 hbm_cmd;
	u8 reserved[3];
} __packed;

struct hbm_me_stop_request {
	u8 hbm_cmd;
	u8 reason;
	u8 reserved[2];
} __packed;

/**
 * struct hbm_host_enum_request -  enumeration request from host to fw
 *
 * @hbm_cmd: bus message command header
 * @allow_add: allow dynamic clients add HBM version >= 2.0
 * @reserved: reserved
 */
struct hbm_host_enum_request {
	u8 hbm_cmd;
	u8 allow_add;
	u8 reserved[2];
} __packed;

struct hbm_host_enum_response {
	u8 hbm_cmd;
	u8 reserved[3];
	u8 valid_addresses[32];
} __packed;

struct mei_client_properties {
	uuid_le protocol_name;
	u8 protocol_version;
	u8 max_number_of_connections;
	u8 fixed_address;
	u8 single_recv_buf;
	u32 max_msg_length;
} __packed;

struct hbm_props_request {
	u8 hbm_cmd;
	u8 me_addr;
	u8 reserved[2];
} __packed;

struct hbm_props_response {
	u8 hbm_cmd;
	u8 me_addr;
	u8 status;
	u8 reserved[1];
	struct mei_client_properties client_properties;
} __packed;

/**
 * struct hbm_add_client_request - request to add a client
 *     might be sent by fw after enumeration has already completed
 *
 * @hbm_cmd: bus message command header
 * @me_addr: address of the client in ME
 * @reserved: reserved
 * @client_properties: client properties
 */
struct hbm_add_client_request {
	u8 hbm_cmd;
	u8 me_addr;
	u8 reserved[2];
	struct mei_client_properties client_properties;
} __packed;

/**
 * struct hbm_add_client_response - response to add a client
 *     sent by the host to report client addition status to fw
 *
 * @hbm_cmd: bus message command header
 * @me_addr: address of the client in ME
 * @status: if HBMS_SUCCESS then the client can now accept connections.
 * @reserved: reserved
 */
struct hbm_add_client_response {
	u8 hbm_cmd;
	u8 me_addr;
	u8 status;
	u8 reserved[1];
} __packed;

/**
 * struct hbm_power_gate - power gate request/response
 *
 * @hbm_cmd: bus message command header
 * @reserved: reserved
 */
struct hbm_power_gate {
	u8 hbm_cmd;
	u8 reserved[3];
} __packed;

/**
 * struct hbm_client_connect_request - connect/disconnect request
 *
 * @hbm_cmd: bus message command header
 * @me_addr: address of the client in ME
 * @host_addr: address of the client in the driver
 * @reserved: reserved
 */
struct hbm_client_connect_request {
	u8 hbm_cmd;
	u8 me_addr;
	u8 host_addr;
	u8 reserved;
} __packed;

/**
 * struct hbm_client_connect_response - connect/disconnect response
 *
 * @hbm_cmd: bus message command header
 * @me_addr: address of the client in ME
 * @host_addr: address of the client in the driver
 * @status: status of the request
 */
struct hbm_client_connect_response {
	u8 hbm_cmd;
	u8 me_addr;
	u8 host_addr;
	u8 status;
} __packed;


#define MEI_FC_MESSAGE_RESERVED_LENGTH           5

struct hbm_flow_control {
	u8 hbm_cmd;
	u8 me_addr;
	u8 host_addr;
	u8 reserved[MEI_FC_MESSAGE_RESERVED_LENGTH];
} __packed;

#define MEI_HBM_NOTIFICATION_START 1
#define MEI_HBM_NOTIFICATION_STOP  0
/**
 * struct hbm_notification_request - start/stop notification request
 *
 * @hbm_cmd: bus message command header
 * @me_addr: address of the client in ME
 * @host_addr: address of the client in the driver
 * @start:  start = 1 or stop = 0 asynchronous notifications
 */
struct hbm_notification_request {
	u8 hbm_cmd;
	u8 me_addr;
	u8 host_addr;
	u8 start;
} __packed;

/**
 * struct hbm_notification_response - start/stop notification response
 *
 * @hbm_cmd: bus message command header
 * @me_addr: address of the client in ME
 * @host_addr: - address of the client in the driver
 * @status: (mei_hbm_status) response status for the request
 *  - MEI_HBMS_SUCCESS: successful stop/start
 *  - MEI_HBMS_CLIENT_NOT_FOUND: if the connection could not be found.
 *  - MEI_HBMS_ALREADY_STARTED: for start requests for a previously
 *                         started notification.
 *  - MEI_HBMS_NOT_STARTED: for stop request for a connected client for whom
 *                         asynchronous notifications are currently disabled.
 *
 * @start:  start = 1 or stop = 0 asynchronous notifications
 * @reserved: reserved
 */
struct hbm_notification_response {
	u8 hbm_cmd;
	u8 me_addr;
	u8 host_addr;
	u8 status;
	u8 start;
	u8 reserved[3];
} __packed;

/**
 * struct hbm_notification - notification event
 *
 * @hbm_cmd: bus message command header
 * @me_addr:  address of the client in ME
 * @host_addr:  address of the client in the driver
 * @reserved: reserved for alignment
 */
struct hbm_notification {
	u8 hbm_cmd;
	u8 me_addr;
	u8 host_addr;
	u8 reserved[1];
} __packed;

#endif
