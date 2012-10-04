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
 * Timeouts
 */
#define MEI_INTEROP_TIMEOUT    (HZ * 7)
#define MEI_CONNECT_TIMEOUT		3	/* at least 2 seconds */

#define CONNECT_TIMEOUT        15	/* HPS definition */
#define INIT_CLIENTS_TIMEOUT   15	/* HPS definition */

#define IAMTHIF_STALL_TIMER		12	/* seconds */
#define IAMTHIF_READ_TIMER		10000	/* ms */

/*
 * Internal Clients Number
 */
#define MEI_WD_HOST_CLIENT_ID          1
#define MEI_IAMTHIF_HOST_CLIENT_ID     2

/*
 * MEI device IDs
 */
#define MEI_DEV_ID_82946GZ    0x2974  /* 82946GZ/GL */
#define MEI_DEV_ID_82G35      0x2984  /* 82G35 Express */
#define MEI_DEV_ID_82Q965     0x2994  /* 82Q963/Q965 */
#define MEI_DEV_ID_82G965     0x29A4  /* 82P965/G965 */

#define MEI_DEV_ID_82GM965    0x2A04  /* Mobile PM965/GM965 */
#define MEI_DEV_ID_82GME965   0x2A14  /* Mobile GME965/GLE960 */

#define MEI_DEV_ID_ICH9_82Q35 0x29B4  /* 82Q35 Express */
#define MEI_DEV_ID_ICH9_82G33 0x29C4  /* 82G33/G31/P35/P31 Express */
#define MEI_DEV_ID_ICH9_82Q33 0x29D4  /* 82Q33 Express */
#define MEI_DEV_ID_ICH9_82X38 0x29E4  /* 82X38/X48 Express */
#define MEI_DEV_ID_ICH9_3200  0x29F4  /* 3200/3210 Server */

#define MEI_DEV_ID_ICH9_6     0x28B4  /* Bearlake */
#define MEI_DEV_ID_ICH9_7     0x28C4  /* Bearlake */
#define MEI_DEV_ID_ICH9_8     0x28D4  /* Bearlake */
#define MEI_DEV_ID_ICH9_9     0x28E4  /* Bearlake */
#define MEI_DEV_ID_ICH9_10    0x28F4  /* Bearlake */

#define MEI_DEV_ID_ICH9M_1    0x2A44  /* Cantiga */
#define MEI_DEV_ID_ICH9M_2    0x2A54  /* Cantiga */
#define MEI_DEV_ID_ICH9M_3    0x2A64  /* Cantiga */
#define MEI_DEV_ID_ICH9M_4    0x2A74  /* Cantiga */

#define MEI_DEV_ID_ICH10_1    0x2E04  /* Eaglelake */
#define MEI_DEV_ID_ICH10_2    0x2E14  /* Eaglelake */
#define MEI_DEV_ID_ICH10_3    0x2E24  /* Eaglelake */
#define MEI_DEV_ID_ICH10_4    0x2E34  /* Eaglelake */

#define MEI_DEV_ID_IBXPK_1    0x3B64  /* Calpella */
#define MEI_DEV_ID_IBXPK_2    0x3B65  /* Calpella */

#define MEI_DEV_ID_CPT_1      0x1C3A  /* Couger Point */
#define MEI_DEV_ID_PBG_1      0x1D3A  /* C600/X79 Patsburg */

#define MEI_DEV_ID_PPT_1      0x1E3A  /* Panther Point */
#define MEI_DEV_ID_PPT_2      0x1CBA  /* Panther Point */
#define MEI_DEV_ID_PPT_3      0x1DBA  /* Panther Point */

#define MEI_DEV_ID_LPT        0x8C3A  /* Lynx Point */
#define MEI_DEV_ID_LPT_LP     0x9C3A  /* Lynx Point LP */
/*
 * MEI HW Section
 */

/* MEI registers */
/* H_CB_WW - Host Circular Buffer (CB) Write Window register */
#define H_CB_WW    0
/* H_CSR - Host Control Status register */
#define H_CSR      4
/* ME_CB_RW - ME Circular Buffer Read Window register (read only) */
#define ME_CB_RW   8
/* ME_CSR_HA - ME Control Status Host Access register (read only) */
#define ME_CSR_HA  0xC


/* register bits of H_CSR (Host Control Status register) */
/* Host Circular Buffer Depth - maximum number of 32-bit entries in CB */
#define H_CBD             0xFF000000
/* Host Circular Buffer Write Pointer */
#define H_CBWP            0x00FF0000
/* Host Circular Buffer Read Pointer */
#define H_CBRP            0x0000FF00
/* Host Reset */
#define H_RST             0x00000010
/* Host Ready */
#define H_RDY             0x00000008
/* Host Interrupt Generate */
#define H_IG              0x00000004
/* Host Interrupt Status */
#define H_IS              0x00000002
/* Host Interrupt Enable */
#define H_IE              0x00000001


/* register bits of ME_CSR_HA (ME Control Status Host Access register) */
/* ME CB (Circular Buffer) Depth HRA (Host Read Access) - host read only
access to ME_CBD */
#define ME_CBD_HRA        0xFF000000
/* ME CB Write Pointer HRA - host read only access to ME_CBWP */
#define ME_CBWP_HRA       0x00FF0000
/* ME CB Read Pointer HRA - host read only access to ME_CBRP */
#define ME_CBRP_HRA       0x0000FF00
/* ME Reset HRA - host read only access to ME_RST */
#define ME_RST_HRA        0x00000010
/* ME Ready HRA - host read only access to ME_RDY */
#define ME_RDY_HRA        0x00000008
/* ME Interrupt Generate HRA - host read only access to ME_IG */
#define ME_IG_HRA         0x00000004
/* ME Interrupt Status HRA - host read only access to ME_IS */
#define ME_IS_HRA         0x00000002
/* ME Interrupt Enable HRA - host read only access to ME_IE */
#define ME_IE_HRA         0x00000001

/*
 * MEI Version
 */
#define HBM_MINOR_VERSION                   0
#define HBM_MAJOR_VERSION                   1
#define HBM_TIMEOUT                         1	/* 1 second */

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

/*
 * Client Connect Status
 * used by hbm_client_connect_response.status
 */
enum client_connect_status_types {
	CCS_SUCCESS = 0x00,
	CCS_NOT_FOUND = 0x01,
	CCS_ALREADY_STARTED = 0x02,
	CCS_OUT_OF_RESOURCES = 0x03,
	CCS_MESSAGE_SMALL = 0x04
};

/*
 * Client Disconnect Status
 */
enum client_disconnect_status_types {
	CDS_SUCCESS = 0x00
};

/*
 *  MEI BUS Interface Section
 */
struct mei_msg_hdr {
	u32 me_addr:8;
	u32 host_addr:8;
	u32 length:9;
	u32 reserved:6;
	u32 msg_complete:1;
} __packed;


struct mei_bus_message {
	u8 hbm_cmd;
	u8 data[0];
} __packed;

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

struct hbm_host_enum_request {
	u8 hbm_cmd;
	u8 reserved[3];
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
	u8 address;
	u8 reserved[2];
} __packed;


struct hbm_props_response {
	u8 hbm_cmd;
	u8 address;
	u8 status;
	u8 reserved[1];
	struct mei_client_properties client_properties;
} __packed;

struct hbm_client_connect_request {
	u8 hbm_cmd;
	u8 me_addr;
	u8 host_addr;
	u8 reserved;
} __packed;

struct hbm_client_connect_response {
	u8 hbm_cmd;
	u8 me_addr;
	u8 host_addr;
	u8 status;
} __packed;

struct hbm_client_disconnect_request {
	u8 hbm_cmd;
	u8 me_addr;
	u8 host_addr;
	u8 reserved[1];
} __packed;

#define MEI_FC_MESSAGE_RESERVED_LENGTH           5

struct hbm_flow_control {
	u8 hbm_cmd;
	u8 me_addr;
	u8 host_addr;
	u8 reserved[MEI_FC_MESSAGE_RESERVED_LENGTH];
} __packed;

struct mei_me_client {
	struct mei_client_properties props;
	u8 client_id;
	u8 mei_flow_ctrl_creds;
} __packed;


#endif
