/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2003-2022, Intel Corporation. All rights reserved
 * Intel Management Engine Interface (Intel MEI) Linux driver
 */

#ifndef _MEI_HW_TYPES_H_
#define _MEI_HW_TYPES_H_

#include <linux/mei.h>

/*
 * Timeouts in Seconds
 */
#define MEI_HW_READY_TIMEOUT        2  /* Timeout on ready message */
#define MEI_CONNECT_TIMEOUT         3  /* HPS: at least 2 seconds */

#define MEI_CL_CONNECT_TIMEOUT     15  /* HPS: Client Connect Timeout */
#define MEI_CL_CONNECT_TIMEOUT_SLOW 30 /* HPS: Client Connect Timeout, slow FW */
#define MEI_CLIENTS_INIT_TIMEOUT   15  /* HPS: Clients Enumeration Timeout */

#define MEI_PGI_TIMEOUT             1  /* PG Isolation time response 1 sec */
#define MEI_D0I3_TIMEOUT            5  /* D0i3 set/unset max response time */
#define MEI_HBM_TIMEOUT             1  /* 1 second */
#define MEI_HBM_TIMEOUT_SLOW        5  /* 5 second, slow FW */

#define MKHI_RCV_TIMEOUT 500 /* receive timeout in msec */
#define MKHI_RCV_TIMEOUT_SLOW 10000 /* receive timeout in msec, slow FW */

/*
 * FW page size for DMA allocations
 */
#define MEI_FW_PAGE_SIZE 4096UL

/*
 * MEI Version
 */
#define HBM_MINOR_VERSION                   2
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
 * MEI version with immediate reply to enum request support
 */
#define HBM_MINOR_VERSION_IE               0
#define HBM_MAJOR_VERSION_IE               2

/*
 * MEI version with disconnect on connection timeout support
 */
#define HBM_MINOR_VERSION_DOT              0
#define HBM_MAJOR_VERSION_DOT              2

/*
 * MEI version with notification support
 */
#define HBM_MINOR_VERSION_EV               0
#define HBM_MAJOR_VERSION_EV               2

/*
 * MEI version with fixed address client support
 */
#define HBM_MINOR_VERSION_FA               0
#define HBM_MAJOR_VERSION_FA               2

/*
 * MEI version with OS ver message support
 */
#define HBM_MINOR_VERSION_OS               0
#define HBM_MAJOR_VERSION_OS               2

/*
 * MEI version with dma ring support
 */
#define HBM_MINOR_VERSION_DR               1
#define HBM_MAJOR_VERSION_DR               2

/*
 * MEI version with vm tag support
 */
#define HBM_MINOR_VERSION_VT               2
#define HBM_MAJOR_VERSION_VT               2

/*
 * MEI version with GSC support
 */
#define HBM_MINOR_VERSION_GSC              2
#define HBM_MAJOR_VERSION_GSC              2

/*
 * MEI version with capabilities message support
 */
#define HBM_MINOR_VERSION_CAP              2
#define HBM_MAJOR_VERSION_CAP              2

/*
 * MEI version with client DMA support
 */
#define HBM_MINOR_VERSION_CD               2
#define HBM_MAJOR_VERSION_CD               2

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

#define MEI_HBM_DMA_SETUP_REQ_CMD           0x12
#define MEI_HBM_DMA_SETUP_RES_CMD           0x92

#define MEI_HBM_CAPABILITIES_REQ_CMD        0x13
#define MEI_HBM_CAPABILITIES_RES_CMD        0x93

#define MEI_HBM_CLIENT_DMA_MAP_REQ_CMD      0x14
#define MEI_HBM_CLIENT_DMA_MAP_RES_CMD      0x94

#define MEI_HBM_CLIENT_DMA_UNMAP_REQ_CMD    0x15
#define MEI_HBM_CLIENT_DMA_UNMAP_RES_CMD    0x95

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
enum mei_cl_disconnect_status {
	MEI_CL_DISCONN_SUCCESS = MEI_HBMS_SUCCESS
};

/**
 * enum mei_ext_hdr_type - extended header type used in
 *    extended header TLV
 *
 * @MEI_EXT_HDR_NONE: sentinel
 * @MEI_EXT_HDR_VTAG: vtag header
 * @MEI_EXT_HDR_GSC: gsc header
 */
enum mei_ext_hdr_type {
	MEI_EXT_HDR_NONE = 0,
	MEI_EXT_HDR_VTAG = 1,
	MEI_EXT_HDR_GSC = 2,
};

/**
 * struct mei_ext_hdr - extend header descriptor (TLV)
 * @type: enum mei_ext_hdr_type
 * @length: length excluding descriptor
 */
struct mei_ext_hdr {
	u8 type;
	u8 length;
} __packed;

/**
 * struct mei_ext_meta_hdr - extend header meta data
 * @count: number of headers
 * @size: total size of the extended header list excluding meta header
 * @reserved: reserved
 * @hdrs: extended headers TLV list
 */
struct mei_ext_meta_hdr {
	u8 count;
	u8 size;
	u8 reserved[2];
	u8 hdrs[];
} __packed;

/**
 * struct mei_ext_hdr_vtag - extend header for vtag
 *
 * @hdr: standard extend header
 * @vtag: virtual tag
 * @reserved: reserved
 */
struct mei_ext_hdr_vtag {
	struct mei_ext_hdr hdr;
	u8 vtag;
	u8 reserved;
} __packed;

/*
 * Extended header iterator functions
 */
/**
 * mei_ext_begin - extended header iterator begin
 *
 * @meta: meta header of the extended header list
 *
 * Return: The first extended header
 */
static inline struct mei_ext_hdr *mei_ext_begin(struct mei_ext_meta_hdr *meta)
{
	return (struct mei_ext_hdr *)meta->hdrs;
}

/**
 * mei_ext_last - check if the ext is the last one in the TLV list
 *
 * @meta: meta header of the extended header list
 * @ext: a meta header on the list
 *
 * Return: true if ext is the last header on the list
 */
static inline bool mei_ext_last(struct mei_ext_meta_hdr *meta,
				struct mei_ext_hdr *ext)
{
	return (u8 *)ext >= (u8 *)meta + sizeof(*meta) + (meta->size * 4);
}

struct mei_gsc_sgl {
	u32 low;
	u32 high;
	u32 length;
} __packed;

#define GSC_HECI_MSG_KERNEL 0
#define GSC_HECI_MSG_USER   1

#define GSC_ADDRESS_TYPE_GTT   0
#define GSC_ADDRESS_TYPE_PPGTT 1
#define GSC_ADDRESS_TYPE_PHYSICAL_CONTINUOUS 2 /* max of 64K */
#define GSC_ADDRESS_TYPE_PHYSICAL_SGL 3

/**
 * struct mei_ext_hdr_gsc_h2f - extended header: gsc host to firmware interface
 *
 * @hdr: extended header
 * @client_id: GSC_HECI_MSG_KERNEL or GSC_HECI_MSG_USER
 * @addr_type: GSC_ADDRESS_TYPE_{GTT, PPGTT, PHYSICAL_CONTINUOUS, PHYSICAL_SGL}
 * @fence_id: synchronization marker
 * @input_address_count: number of input sgl buffers
 * @output_address_count: number of output sgl buffers
 * @reserved: reserved
 * @sgl: sg list
 */
struct mei_ext_hdr_gsc_h2f {
	struct mei_ext_hdr hdr;
	u8                 client_id;
	u8                 addr_type;
	u32                fence_id;
	u8                 input_address_count;
	u8                 output_address_count;
	u8                 reserved[2];
	struct mei_gsc_sgl sgl[];
} __packed;

/**
 * struct mei_ext_hdr_gsc_f2h - gsc firmware to host interface
 *
 * @hdr: extended header
 * @client_id: GSC_HECI_MSG_KERNEL or GSC_HECI_MSG_USER
 * @reserved: reserved
 * @fence_id: synchronization marker
 * @written: number of bytes written to firmware
 */
struct mei_ext_hdr_gsc_f2h {
	struct mei_ext_hdr hdr;
	u8                 client_id;
	u8                 reserved;
	u32                fence_id;
	u32                written;
} __packed;

/**
 * mei_ext_next - following extended header on the TLV list
 *
 * @ext: current extend header
 *
 * Context: The function does not check for the overflows,
 *          one should call mei_ext_last before.
 *
 * Return: The following extend header after @ext
 */
static inline struct mei_ext_hdr *mei_ext_next(struct mei_ext_hdr *ext)
{
	return (struct mei_ext_hdr *)((u8 *)ext + (ext->length * 4));
}

/**
 * mei_ext_hdr_len - get ext header length in bytes
 *
 * @ext: extend header
 *
 * Return: extend header length in bytes
 */
static inline u32 mei_ext_hdr_len(const struct mei_ext_hdr *ext)
{
	if (!ext)
		return 0;

	return ext->length * sizeof(u32);
}

/**
 * struct mei_msg_hdr - MEI BUS Interface Section
 *
 * @me_addr: device address
 * @host_addr: host address
 * @length: message length
 * @reserved: reserved
 * @extended: message has extended header
 * @dma_ring: message is on dma ring
 * @internal: message is internal
 * @msg_complete: last packet of the message
 * @extension: extension of the header
 */
struct mei_msg_hdr {
	u32 me_addr:8;
	u32 host_addr:8;
	u32 length:9;
	u32 reserved:3;
	u32 extended:1;
	u32 dma_ring:1;
	u32 internal:1;
	u32 msg_complete:1;
	u32 extension[];
} __packed;

/* The length is up to 9 bits */
#define MEI_MSG_MAX_LEN_MASK GENMASK(9, 0)

struct mei_bus_message {
	u8 hbm_cmd;
	u8 data[];
} __packed;

/**
 * struct mei_hbm_cl_cmd - client specific host bus command
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
 * enum hbm_host_enum_flags - enumeration request flags (HBM version >= 2.0)
 *
 * @MEI_HBM_ENUM_F_ALLOW_ADD: allow dynamic clients add
 * @MEI_HBM_ENUM_F_IMMEDIATE_ENUM: allow FW to send answer immediately
 */
enum hbm_host_enum_flags {
	MEI_HBM_ENUM_F_ALLOW_ADD = BIT(0),
	MEI_HBM_ENUM_F_IMMEDIATE_ENUM = BIT(1),
};

/**
 * struct hbm_host_enum_request - enumeration request from host to fw
 *
 * @hbm_cmd : bus message command header
 * @flags   : request flags
 * @reserved: reserved
 */
struct hbm_host_enum_request {
	u8 hbm_cmd;
	u8 flags;
	u8 reserved[2];
} __packed;

struct hbm_host_enum_response {
	u8 hbm_cmd;
	u8 reserved[3];
	u8 valid_addresses[32];
} __packed;

/**
 * struct mei_client_properties - mei client properties
 *
 * @protocol_name: guid of the client
 * @protocol_version: client protocol version
 * @max_number_of_connections: number of possible connections.
 * @fixed_address: fixed me address (0 if the client is dynamic)
 * @single_recv_buf: 1 if all connections share a single receive buffer.
 * @vt_supported: the client support vtag
 * @reserved: reserved
 * @max_msg_length: MTU of the client
 */
struct mei_client_properties {
	uuid_le protocol_name;
	u8 protocol_version;
	u8 max_number_of_connections;
	u8 fixed_address;
	u8 single_recv_buf:1;
	u8 vt_supported:1;
	u8 reserved:6;
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
	u8 reserved;
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
	u8 reserved;
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
	u8 reserved;
} __packed;

/**
 * struct hbm_dma_mem_dscr - dma ring
 *
 * @addr_hi: the high 32bits of 64 bit address
 * @addr_lo: the low  32bits of 64 bit address
 * @size   : size in bytes (must be power of 2)
 */
struct hbm_dma_mem_dscr {
	u32 addr_hi;
	u32 addr_lo;
	u32 size;
} __packed;

enum {
	DMA_DSCR_HOST = 0,
	DMA_DSCR_DEVICE = 1,
	DMA_DSCR_CTRL = 2,
	DMA_DSCR_NUM,
};

/**
 * struct hbm_dma_setup_request - dma setup request
 *
 * @hbm_cmd: bus message command header
 * @reserved: reserved for alignment
 * @dma_dscr: dma descriptor for HOST, DEVICE, and CTRL
 */
struct hbm_dma_setup_request {
	u8 hbm_cmd;
	u8 reserved[3];
	struct hbm_dma_mem_dscr dma_dscr[DMA_DSCR_NUM];
} __packed;

/**
 * struct hbm_dma_setup_response - dma setup response
 *
 * @hbm_cmd: bus message command header
 * @status: 0 on success; otherwise DMA setup failed.
 * @reserved: reserved for alignment
 */
struct hbm_dma_setup_response {
	u8 hbm_cmd;
	u8 status;
	u8 reserved[2];
} __packed;

/**
 * struct hbm_dma_ring_ctrl - dma ring control block
 *
 * @hbuf_wr_idx: host circular buffer write index in slots
 * @reserved1: reserved for alignment
 * @hbuf_rd_idx: host circular buffer read index in slots
 * @reserved2: reserved for alignment
 * @dbuf_wr_idx: device circular buffer write index in slots
 * @reserved3: reserved for alignment
 * @dbuf_rd_idx: device circular buffer read index in slots
 * @reserved4: reserved for alignment
 */
struct hbm_dma_ring_ctrl {
	u32 hbuf_wr_idx;
	u32 reserved1;
	u32 hbuf_rd_idx;
	u32 reserved2;
	u32 dbuf_wr_idx;
	u32 reserved3;
	u32 dbuf_rd_idx;
	u32 reserved4;
} __packed;

/* virtual tag supported */
#define HBM_CAP_VT BIT(0)

/* gsc extended header support */
#define HBM_CAP_GSC BIT(1)

/* client dma supported */
#define HBM_CAP_CD BIT(2)

/**
 * struct hbm_capability_request - capability request from host to fw
 *
 * @hbm_cmd : bus message command header
 * @capability_requested: bitmask of capabilities requested by host
 */
struct hbm_capability_request {
	u8 hbm_cmd;
	u8 capability_requested[3];
} __packed;

/**
 * struct hbm_capability_response - capability response from fw to host
 *
 * @hbm_cmd : bus message command header
 * @capability_granted: bitmask of capabilities granted by FW
 */
struct hbm_capability_response {
	u8 hbm_cmd;
	u8 capability_granted[3];
} __packed;

/**
 * struct hbm_client_dma_map_request - client dma map request from host to fw
 *
 * @hbm_cmd: bus message command header
 * @client_buffer_id: client buffer id
 * @reserved: reserved
 * @address_lsb: DMA address LSB
 * @address_msb: DMA address MSB
 * @size: DMA size
 */
struct hbm_client_dma_map_request {
	u8 hbm_cmd;
	u8 client_buffer_id;
	u8 reserved[2];
	u32 address_lsb;
	u32 address_msb;
	u32 size;
} __packed;

/**
 * struct hbm_client_dma_unmap_request - client dma unmap request
 *        from the host to the firmware
 *
 * @hbm_cmd: bus message command header
 * @status: unmap status
 * @client_buffer_id: client buffer id
 * @reserved: reserved
 */
struct hbm_client_dma_unmap_request {
	u8 hbm_cmd;
	u8 status;
	u8 client_buffer_id;
	u8 reserved;
} __packed;

/**
 * struct hbm_client_dma_response - client dma unmap response
 *        from the firmware to the host
 *
 * @hbm_cmd: bus message command header
 * @status: command status
 */
struct hbm_client_dma_response {
	u8 hbm_cmd;
	u8 status;
} __packed;

#endif
