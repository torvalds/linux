/* Broadcom NetXtreme-C/E network driver.
 *
 * Copyright (c) 2014-2016 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 */

#ifndef BNXT_HSI_H
#define BNXT_HSI_H

/* per-context HW statistics -- chip view */
struct ctx_hw_stats  {
	__le64 rx_ucast_pkts;
	__le64 rx_mcast_pkts;
	__le64 rx_bcast_pkts;
	__le64 rx_discard_pkts;
	__le64 rx_drop_pkts;
	__le64 rx_ucast_bytes;
	__le64 rx_mcast_bytes;
	__le64 rx_bcast_bytes;
	__le64 tx_ucast_pkts;
	__le64 tx_mcast_pkts;
	__le64 tx_bcast_pkts;
	__le64 tx_discard_pkts;
	__le64 tx_drop_pkts;
	__le64 tx_ucast_bytes;
	__le64 tx_mcast_bytes;
	__le64 tx_bcast_bytes;
	__le64 tpa_pkts;
	__le64 tpa_bytes;
	__le64 tpa_events;
	__le64 tpa_aborts;
};

/* Statistics Ejection Buffer Completion Record (16 bytes) */
struct eject_cmpl {
	__le16 type;
	#define EJECT_CMPL_TYPE_MASK				    0x3fUL
	#define EJECT_CMPL_TYPE_SFT				    0
	#define EJECT_CMPL_TYPE_STAT_EJECT			   (0x1aUL << 0)
	__le16 len;
	__le32 opaque;
	__le32 v;
	#define EJECT_CMPL_V					    0x1UL
	__le32 unused_2;
};

/* HWRM Completion Record (16 bytes) */
struct hwrm_cmpl {
	__le16 type;
	#define HWRM_CMPL_TYPE_MASK				    0x3fUL
	#define HWRM_CMPL_TYPE_SFT				    0
	#define HWRM_CMPL_TYPE_HWRM_DONE			   (0x20UL << 0)
	__le16 sequence_id;
	__le32 unused_1;
	__le32 v;
	#define HWRM_CMPL_V					    0x1UL
	__le32 unused_3;
};

/* HWRM Forwarded Request (16 bytes) */
struct hwrm_fwd_req_cmpl {
	__le16 req_len_type;
	#define HWRM_FWD_REQ_CMPL_TYPE_MASK			    0x3fUL
	#define HWRM_FWD_REQ_CMPL_TYPE_SFT			    0
	#define HWRM_FWD_REQ_CMPL_TYPE_HWRM_FWD_REQ		   (0x22UL << 0)
	#define HWRM_FWD_REQ_CMPL_REQ_LEN_MASK			    0xffc0UL
	#define HWRM_FWD_REQ_CMPL_REQ_LEN_SFT			    6
	__le16 source_id;
	__le32 unused_0;
	__le32 req_buf_addr_v[2];
	#define HWRM_FWD_REQ_CMPL_V				    0x1UL
	#define HWRM_FWD_REQ_CMPL_REQ_BUF_ADDR_MASK		    0xfffffffeUL
	#define HWRM_FWD_REQ_CMPL_REQ_BUF_ADDR_SFT		    1
};

/* HWRM Forwarded Response (16 bytes) */
struct hwrm_fwd_resp_cmpl {
	__le16 type;
	#define HWRM_FWD_RESP_CMPL_TYPE_MASK			    0x3fUL
	#define HWRM_FWD_RESP_CMPL_TYPE_SFT			    0
	#define HWRM_FWD_RESP_CMPL_TYPE_HWRM_FWD_RESP		   (0x24UL << 0)
	__le16 source_id;
	__le16 resp_len;
	__le16 unused_1;
	__le32 resp_buf_addr_v[2];
	#define HWRM_FWD_RESP_CMPL_V				    0x1UL
	#define HWRM_FWD_RESP_CMPL_RESP_BUF_ADDR_MASK		    0xfffffffeUL
	#define HWRM_FWD_RESP_CMPL_RESP_BUF_ADDR_SFT		    1
};

/* HWRM Asynchronous Event Completion Record (16 bytes) */
struct hwrm_async_event_cmpl {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_TYPE_MASK		    0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_TYPE_SFT			    0
	#define HWRM_ASYNC_EVENT_CMPL_TYPE_HWRM_ASYNC_EVENT       (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_LINK_STATUS_CHANGE (0x0UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_LINK_MTU_CHANGE    (0x1UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CHANGE  (0x2UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_DCB_CONFIG_CHANGE  (0x3UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_PORT_CONN_NOT_ALLOWED (0x4UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CFG_NOT_ALLOWED (0x5UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_LINK_SPEED_CFG_CHANGE (0x6UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_FUNC_DRVR_UNLOAD   (0x10UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_FUNC_DRVR_LOAD     (0x11UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_PF_DRVR_UNLOAD     (0x20UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_PF_DRVR_LOAD       (0x21UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_VF_FLR		   (0x30UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_VF_MAC_ADDR_CHANGE (0x31UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_PF_VF_COMM_STATUS_CHANGE (0x32UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_VF_CFG_CHANGE      (0x33UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_EVENT_ID_HWRM_ERROR	   (0xffUL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_V			    0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_OPAQUE_MASK		    0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_OPAQUE_SFT		    1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
};

/* HWRM Asynchronous Event Completion Record for link status change (16 bytes) */
struct hwrm_async_event_cmpl_link_status_change {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_TYPE_MASK 0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_TYPE_SFT  0
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_EVENT_ID_LINK_STATUS_CHANGE (0x0UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_V	    0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_OPAQUE_MASK 0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_OPAQUE_SFT 1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_EVENT_DATA1_LINK_CHANGE 0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_EVENT_DATA1_LINK_CHANGE_DOWN (0x0UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_EVENT_DATA1_LINK_CHANGE_UP (0x1UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_EVENT_DATA1_LINK_CHANGE_LAST    HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_EVENT_DATA1_LINK_CHANGE_UP
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_EVENT_DATA1_PORT_MASK 0xeUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_EVENT_DATA1_PORT_SFT 1
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_EVENT_DATA1_PORT_ID_MASK 0xffff0UL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_STATUS_CHANGE_EVENT_DATA1_PORT_ID_SFT 4
};

/* HWRM Asynchronous Event Completion Record for link MTU change (16 bytes) */
struct hwrm_async_event_cmpl_link_mtu_change {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_MTU_CHANGE_TYPE_MASK    0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_MTU_CHANGE_TYPE_SFT     0
	#define HWRM_ASYNC_EVENT_CMPL_LINK_MTU_CHANGE_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_MTU_CHANGE_EVENT_ID_LINK_MTU_CHANGE (0x1UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_MTU_CHANGE_V	    0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_MTU_CHANGE_OPAQUE_MASK  0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_MTU_CHANGE_OPAQUE_SFT   1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_MTU_CHANGE_EVENT_DATA1_NEW_MTU_MASK 0xffffUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_MTU_CHANGE_EVENT_DATA1_NEW_MTU_SFT 0
};

/* HWRM Asynchronous Event Completion Record for link speed change (16 bytes) */
struct hwrm_async_event_cmpl_link_speed_change {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_TYPE_MASK  0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_TYPE_SFT   0
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_ID_LINK_SPEED_CHANGE (0x2UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_V	    0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_OPAQUE_MASK 0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_OPAQUE_SFT 1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_FORCE 0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_MASK 0xfffeUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_SFT 1
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_100MB (0x1UL << 1)
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_1GB (0xaUL << 1)
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_2GB (0x14UL << 1)
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_2_5GB (0x19UL << 1)
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_10GB (0x64UL << 1)
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_20GB (0xc8UL << 1)
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_25GB (0xfaUL << 1)
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_40GB (0x190UL << 1)
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_50GB (0x1f4UL << 1)
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_100GB (0x3e8UL << 1)
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_10MB (0xffffUL << 1)
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_LAST    HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_NEW_LINK_SPEED_100MBPS_10MB
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_PORT_ID_MASK 0xffff0000UL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CHANGE_EVENT_DATA1_PORT_ID_SFT 16
};

/* HWRM Asynchronous Event Completion Record for DCB Config change (16 bytes) */
struct hwrm_async_event_cmpl_dcb_config_change {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_DCB_CONFIG_CHANGE_TYPE_MASK  0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_DCB_CONFIG_CHANGE_TYPE_SFT   0
	#define HWRM_ASYNC_EVENT_CMPL_DCB_CONFIG_CHANGE_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_DCB_CONFIG_CHANGE_EVENT_ID_DCB_CONFIG_CHANGE (0x3UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_DCB_CONFIG_CHANGE_V	    0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_DCB_CONFIG_CHANGE_OPAQUE_MASK 0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_DCB_CONFIG_CHANGE_OPAQUE_SFT 1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_DCB_CONFIG_CHANGE_EVENT_DATA1_PORT_ID_MASK 0xffffUL
	#define HWRM_ASYNC_EVENT_CMPL_DCB_CONFIG_CHANGE_EVENT_DATA1_PORT_ID_SFT 0
};

/* HWRM Asynchronous Event Completion Record for port connection not allowed (16 bytes) */
struct hwrm_async_event_cmpl_port_conn_not_allowed {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_TYPE_MASK 0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_TYPE_SFT 0
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_EVENT_ID_PORT_CONN_NOT_ALLOWED (0x4UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_V      0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_OPAQUE_MASK 0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_OPAQUE_SFT 1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_EVENT_DATA1_PORT_ID_MASK 0xffffUL
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_EVENT_DATA1_PORT_ID_SFT 0
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_EVENT_DATA1_ENFORCEMENT_POLICY_MASK 0xff0000UL
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_EVENT_DATA1_ENFORCEMENT_POLICY_SFT 16
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_EVENT_DATA1_ENFORCEMENT_POLICY_NONE (0x0UL << 16)
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_EVENT_DATA1_ENFORCEMENT_POLICY_DISABLETX (0x1UL << 16)
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_EVENT_DATA1_ENFORCEMENT_POLICY_WARNINGMSG (0x2UL << 16)
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_EVENT_DATA1_ENFORCEMENT_POLICY_PWRDOWN (0x3UL << 16)
	#define HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_EVENT_DATA1_ENFORCEMENT_POLICY_LAST    HWRM_ASYNC_EVENT_CMPL_PORT_CONN_NOT_ALLOWED_EVENT_DATA1_ENFORCEMENT_POLICY_PWRDOWN
};

/* HWRM Asynchronous Event Completion Record for link speed config not allowed (16 bytes) */
struct hwrm_async_event_cmpl_link_speed_cfg_not_allowed {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_NOT_ALLOWED_TYPE_MASK 0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_NOT_ALLOWED_TYPE_SFT 0
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_NOT_ALLOWED_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_NOT_ALLOWED_EVENT_ID_LINK_SPEED_CFG_NOT_ALLOWED (0x5UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_NOT_ALLOWED_V 0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_NOT_ALLOWED_OPAQUE_MASK 0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_NOT_ALLOWED_OPAQUE_SFT 1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_NOT_ALLOWED_EVENT_DATA1_PORT_ID_MASK 0xffffUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_NOT_ALLOWED_EVENT_DATA1_PORT_ID_SFT 0
};

/* HWRM Asynchronous Event Completion Record for link speed configuration change (16 bytes) */
struct hwrm_async_event_cmpl_link_speed_cfg_change {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_CHANGE_TYPE_MASK 0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_CHANGE_TYPE_SFT 0
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_CHANGE_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_CHANGE_EVENT_ID_LINK_SPEED_CFG_CHANGE (0x6UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_CHANGE_V      0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_CHANGE_OPAQUE_MASK 0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_CHANGE_OPAQUE_SFT 1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_CHANGE_EVENT_DATA1_PORT_ID_MASK 0xffffUL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_CHANGE_EVENT_DATA1_PORT_ID_SFT 0
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_CHANGE_EVENT_DATA1_SUPPORTED_LINK_SPEEDS_CHANGE 0x10000UL
	#define HWRM_ASYNC_EVENT_CMPL_LINK_SPEED_CFG_CHANGE_EVENT_DATA1_ILLEGAL_LINK_SPEED_CFG 0x20000UL
};

/* HWRM Asynchronous Event Completion Record for Function Driver Unload (16 bytes) */
struct hwrm_async_event_cmpl_func_drvr_unload {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_UNLOAD_TYPE_MASK   0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_UNLOAD_TYPE_SFT    0
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_UNLOAD_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_UNLOAD_EVENT_ID_FUNC_DRVR_UNLOAD (0x10UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_UNLOAD_V	    0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_UNLOAD_OPAQUE_MASK 0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_UNLOAD_OPAQUE_SFT  1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_UNLOAD_EVENT_DATA1_FUNC_ID_MASK 0xffffUL
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_UNLOAD_EVENT_DATA1_FUNC_ID_SFT 0
};

/* HWRM Asynchronous Event Completion Record for Function Driver load (16 bytes) */
struct hwrm_async_event_cmpl_func_drvr_load {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_LOAD_TYPE_MASK     0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_LOAD_TYPE_SFT      0
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_LOAD_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_LOAD_EVENT_ID_FUNC_DRVR_LOAD (0x11UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_LOAD_V		    0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_LOAD_OPAQUE_MASK   0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_LOAD_OPAQUE_SFT    1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_LOAD_EVENT_DATA1_FUNC_ID_MASK 0xffffUL
	#define HWRM_ASYNC_EVENT_CMPL_FUNC_DRVR_LOAD_EVENT_DATA1_FUNC_ID_SFT 0
};

/* HWRM Asynchronous Event Completion Record for PF Driver Unload (16 bytes) */
struct hwrm_async_event_cmpl_pf_drvr_unload {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_UNLOAD_TYPE_MASK     0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_UNLOAD_TYPE_SFT      0
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_UNLOAD_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_UNLOAD_EVENT_ID_PF_DRVR_UNLOAD (0x20UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_UNLOAD_V		    0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_UNLOAD_OPAQUE_MASK   0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_UNLOAD_OPAQUE_SFT    1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_UNLOAD_EVENT_DATA1_FUNC_ID_MASK 0xffffUL
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_UNLOAD_EVENT_DATA1_FUNC_ID_SFT 0
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_UNLOAD_EVENT_DATA1_PORT_MASK 0x70000UL
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_UNLOAD_EVENT_DATA1_PORT_SFT 16
};

/* HWRM Asynchronous Event Completion Record for PF Driver load (16 bytes) */
struct hwrm_async_event_cmpl_pf_drvr_load {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_LOAD_TYPE_MASK       0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_LOAD_TYPE_SFT	    0
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_LOAD_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_LOAD_EVENT_ID_PF_DRVR_LOAD (0x21UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_LOAD_V		    0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_LOAD_OPAQUE_MASK     0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_LOAD_OPAQUE_SFT      1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_LOAD_EVENT_DATA1_FUNC_ID_MASK 0xffffUL
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_LOAD_EVENT_DATA1_FUNC_ID_SFT 0
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_LOAD_EVENT_DATA1_PORT_MASK 0x70000UL
	#define HWRM_ASYNC_EVENT_CMPL_PF_DRVR_LOAD_EVENT_DATA1_PORT_SFT 16
};

/* HWRM Asynchronous Event Completion Record for VF FLR (16 bytes) */
struct hwrm_async_event_cmpl_vf_flr {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_VF_FLR_TYPE_MASK		    0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_VF_FLR_TYPE_SFT		    0
	#define HWRM_ASYNC_EVENT_CMPL_VF_FLR_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_VF_FLR_EVENT_ID_VF_FLR      (0x30UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_VF_FLR_V			    0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_VF_FLR_OPAQUE_MASK	    0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_VF_FLR_OPAQUE_SFT	    1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_VF_FLR_EVENT_DATA1_VF_ID_MASK 0xffffUL
	#define HWRM_ASYNC_EVENT_CMPL_VF_FLR_EVENT_DATA1_VF_ID_SFT 0
};

/* HWRM Asynchronous Event Completion Record for VF MAC Addr change (16 bytes) */
struct hwrm_async_event_cmpl_vf_mac_addr_change {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_VF_MAC_ADDR_CHANGE_TYPE_MASK 0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_VF_MAC_ADDR_CHANGE_TYPE_SFT  0
	#define HWRM_ASYNC_EVENT_CMPL_VF_MAC_ADDR_CHANGE_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_VF_MAC_ADDR_CHANGE_EVENT_ID_VF_MAC_ADDR_CHANGE (0x31UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_VF_MAC_ADDR_CHANGE_V	    0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_VF_MAC_ADDR_CHANGE_OPAQUE_MASK 0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_VF_MAC_ADDR_CHANGE_OPAQUE_SFT 1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_VF_MAC_ADDR_CHANGE_EVENT_DATA1_VF_ID_MASK 0xffffUL
	#define HWRM_ASYNC_EVENT_CMPL_VF_MAC_ADDR_CHANGE_EVENT_DATA1_VF_ID_SFT 0
};

/* HWRM Asynchronous Event Completion Record for PF-VF communication status change (16 bytes) */
struct hwrm_async_event_cmpl_pf_vf_comm_status_change {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_PF_VF_COMM_STATUS_CHANGE_TYPE_MASK 0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_PF_VF_COMM_STATUS_CHANGE_TYPE_SFT 0
	#define HWRM_ASYNC_EVENT_CMPL_PF_VF_COMM_STATUS_CHANGE_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_PF_VF_COMM_STATUS_CHANGE_EVENT_ID_PF_VF_COMM_STATUS_CHANGE (0x32UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_PF_VF_COMM_STATUS_CHANGE_V   0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_PF_VF_COMM_STATUS_CHANGE_OPAQUE_MASK 0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_PF_VF_COMM_STATUS_CHANGE_OPAQUE_SFT 1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_PF_VF_COMM_STATUS_CHANGE_EVENT_DATA1_COMM_ESTABLISHED 0x1UL
};

/* HWRM Asynchronous Event Completion Record for VF configuration change (16 bytes) */
struct hwrm_async_event_cmpl_vf_cfg_change {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_VF_CFG_CHANGE_TYPE_MASK      0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_VF_CFG_CHANGE_TYPE_SFT       0
	#define HWRM_ASYNC_EVENT_CMPL_VF_CFG_CHANGE_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_VF_CFG_CHANGE_EVENT_ID_VF_CFG_CHANGE (0x33UL << 0)
	__le32 event_data2;
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_VF_CFG_CHANGE_V		    0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_VF_CFG_CHANGE_OPAQUE_MASK    0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_VF_CFG_CHANGE_OPAQUE_SFT     1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_VF_CFG_CHANGE_EVENT_DATA1_MTU_CHANGE 0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_VF_CFG_CHANGE_EVENT_DATA1_MRU_CHANGE 0x2UL
	#define HWRM_ASYNC_EVENT_CMPL_VF_CFG_CHANGE_EVENT_DATA1_DFLT_MAC_ADDR_CHANGE 0x4UL
	#define HWRM_ASYNC_EVENT_CMPL_VF_CFG_CHANGE_EVENT_DATA1_DFLT_VLAN_CHANGE 0x8UL
};

/* HWRM Asynchronous Event Completion Record for HWRM Error (16 bytes) */
struct hwrm_async_event_cmpl_hwrm_error {
	__le16 type;
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_TYPE_MASK	    0x3fUL
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_TYPE_SFT	    0
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_TYPE_HWRM_ASYNC_EVENT (0x2eUL << 0)
	__le16 event_id;
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_EVENT_ID_HWRM_ERROR (0xffUL << 0)
	__le32 event_data2;
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_EVENT_DATA2_SEVERITY_MASK 0xffUL
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_EVENT_DATA2_SEVERITY_SFT 0
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_EVENT_DATA2_SEVERITY_WARNING (0x0UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_EVENT_DATA2_SEVERITY_NONFATAL (0x1UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_EVENT_DATA2_SEVERITY_FATAL (0x2UL << 0)
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_EVENT_DATA2_SEVERITY_LAST    HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_EVENT_DATA2_SEVERITY_FATAL
	u8 opaque_v;
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_V		    0x1UL
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_OPAQUE_MASK       0xfeUL
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_OPAQUE_SFT	    1
	u8 timestamp_lo;
	__le16 timestamp_hi;
	__le32 event_data1;
	#define HWRM_ASYNC_EVENT_CMPL_HWRM_ERROR_EVENT_DATA1_TIMESTAMP 0x1UL
};

/* HW Resource Manager Specification 1.2.2 */
#define HWRM_VERSION_MAJOR	1
#define HWRM_VERSION_MINOR	2
#define HWRM_VERSION_UPDATE	2

#define HWRM_VERSION_STR	"1.2.2"
/*
 * Following is the signature for HWRM message field that indicates not
 * applicable (All F's). Need to cast it the size of the field if needed.
 */
#define HWRM_NA_SIGNATURE	((__le32)(-1))
#define HWRM_MAX_REQ_LEN    (128)  /* hwrm_func_buf_rgtr */
#define HWRM_MAX_RESP_LEN    (176)  /* hwrm_func_qstats */
#define HW_HASH_INDEX_SIZE      0x80    /* 7 bit indirection table index. */
#define HW_HASH_KEY_SIZE	40
#define HWRM_RESP_VALID_KEY      1 /* valid key for HWRM response */
/* Input (16 bytes) */
struct input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
};

/* Output (8 bytes) */
struct output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
};

/* Command numbering (8 bytes) */
struct cmd_nums {
	__le16 req_type;
	#define HWRM_VER_GET					   (0x0UL)
	#define HWRM_FUNC_BUF_UNRGTR				   (0xeUL)
	#define HWRM_FUNC_VF_CFG				   (0xfUL)
	#define RESERVED1					   (0x10UL)
	#define HWRM_FUNC_RESET				   (0x11UL)
	#define HWRM_FUNC_GETFID				   (0x12UL)
	#define HWRM_FUNC_VF_ALLOC				   (0x13UL)
	#define HWRM_FUNC_VF_FREE				   (0x14UL)
	#define HWRM_FUNC_QCAPS				   (0x15UL)
	#define HWRM_FUNC_QCFG					   (0x16UL)
	#define HWRM_FUNC_CFG					   (0x17UL)
	#define HWRM_FUNC_QSTATS				   (0x18UL)
	#define HWRM_FUNC_CLR_STATS				   (0x19UL)
	#define HWRM_FUNC_DRV_UNRGTR				   (0x1aUL)
	#define HWRM_FUNC_VF_RESC_FREE				   (0x1bUL)
	#define HWRM_FUNC_VF_VNIC_IDS_QUERY			   (0x1cUL)
	#define HWRM_FUNC_DRV_RGTR				   (0x1dUL)
	#define HWRM_FUNC_DRV_QVER				   (0x1eUL)
	#define HWRM_FUNC_BUF_RGTR				   (0x1fUL)
	#define HWRM_PORT_PHY_CFG				   (0x20UL)
	#define HWRM_PORT_MAC_CFG				   (0x21UL)
	#define HWRM_PORT_TS_QUERY				   (0x22UL)
	#define HWRM_PORT_QSTATS				   (0x23UL)
	#define HWRM_PORT_LPBK_QSTATS				   (0x24UL)
	#define HWRM_PORT_CLR_STATS				   (0x25UL)
	#define HWRM_PORT_LPBK_CLR_STATS			   (0x26UL)
	#define HWRM_PORT_PHY_QCFG				   (0x27UL)
	#define HWRM_PORT_MAC_QCFG				   (0x28UL)
	#define HWRM_PORT_BLINK_LED				   (0x29UL)
	#define HWRM_PORT_PHY_QCAPS				   (0x2aUL)
	#define HWRM_PORT_PHY_I2C_WRITE			   (0x2bUL)
	#define HWRM_PORT_PHY_I2C_READ				   (0x2cUL)
	#define HWRM_QUEUE_QPORTCFG				   (0x30UL)
	#define HWRM_QUEUE_QCFG				   (0x31UL)
	#define HWRM_QUEUE_CFG					   (0x32UL)
	#define HWRM_QUEUE_BUFFERS_QCFG			   (0x33UL)
	#define HWRM_QUEUE_BUFFERS_CFG				   (0x34UL)
	#define HWRM_QUEUE_PFCENABLE_QCFG			   (0x35UL)
	#define HWRM_QUEUE_PFCENABLE_CFG			   (0x36UL)
	#define HWRM_QUEUE_PRI2COS_QCFG			   (0x37UL)
	#define HWRM_QUEUE_PRI2COS_CFG				   (0x38UL)
	#define HWRM_QUEUE_COS2BW_QCFG				   (0x39UL)
	#define HWRM_QUEUE_COS2BW_CFG				   (0x3aUL)
	#define HWRM_VNIC_ALLOC				   (0x40UL)
	#define HWRM_VNIC_FREE					   (0x41UL)
	#define HWRM_VNIC_CFG					   (0x42UL)
	#define HWRM_VNIC_QCFG					   (0x43UL)
	#define HWRM_VNIC_TPA_CFG				   (0x44UL)
	#define HWRM_VNIC_TPA_QCFG				   (0x45UL)
	#define HWRM_VNIC_RSS_CFG				   (0x46UL)
	#define HWRM_VNIC_RSS_QCFG				   (0x47UL)
	#define HWRM_VNIC_PLCMODES_CFG				   (0x48UL)
	#define HWRM_VNIC_PLCMODES_QCFG			   (0x49UL)
	#define HWRM_RING_ALLOC				   (0x50UL)
	#define HWRM_RING_FREE					   (0x51UL)
	#define HWRM_RING_CMPL_RING_QAGGINT_PARAMS		   (0x52UL)
	#define HWRM_RING_CMPL_RING_CFG_AGGINT_PARAMS		   (0x53UL)
	#define HWRM_RING_RESET				   (0x5eUL)
	#define HWRM_RING_GRP_ALLOC				   (0x60UL)
	#define HWRM_RING_GRP_FREE				   (0x61UL)
	#define HWRM_VNIC_RSS_COS_LB_CTX_ALLOC			   (0x70UL)
	#define HWRM_VNIC_RSS_COS_LB_CTX_FREE			   (0x71UL)
	#define HWRM_CFA_L2_FILTER_ALLOC			   (0x90UL)
	#define HWRM_CFA_L2_FILTER_FREE			   (0x91UL)
	#define HWRM_CFA_L2_FILTER_CFG				   (0x92UL)
	#define HWRM_CFA_L2_SET_RX_MASK			   (0x93UL)
	#define RESERVED3					   (0x94UL)
	#define HWRM_CFA_TUNNEL_FILTER_ALLOC			   (0x95UL)
	#define HWRM_CFA_TUNNEL_FILTER_FREE			   (0x96UL)
	#define HWRM_CFA_ENCAP_RECORD_ALLOC			   (0x97UL)
	#define HWRM_CFA_ENCAP_RECORD_FREE			   (0x98UL)
	#define HWRM_CFA_NTUPLE_FILTER_ALLOC			   (0x99UL)
	#define HWRM_CFA_NTUPLE_FILTER_FREE			   (0x9aUL)
	#define HWRM_CFA_NTUPLE_FILTER_CFG			   (0x9bUL)
	#define HWRM_CFA_EM_FLOW_ALLOC				   (0x9cUL)
	#define HWRM_CFA_EM_FLOW_FREE				   (0x9dUL)
	#define HWRM_CFA_EM_FLOW_CFG				   (0x9eUL)
	#define HWRM_TUNNEL_DST_PORT_QUERY			   (0xa0UL)
	#define HWRM_TUNNEL_DST_PORT_ALLOC			   (0xa1UL)
	#define HWRM_TUNNEL_DST_PORT_FREE			   (0xa2UL)
	#define HWRM_STAT_CTX_ALLOC				   (0xb0UL)
	#define HWRM_STAT_CTX_FREE				   (0xb1UL)
	#define HWRM_STAT_CTX_QUERY				   (0xb2UL)
	#define HWRM_STAT_CTX_CLR_STATS			   (0xb3UL)
	#define HWRM_FW_RESET					   (0xc0UL)
	#define HWRM_FW_QSTATUS				   (0xc1UL)
	#define HWRM_EXEC_FWD_RESP				   (0xd0UL)
	#define HWRM_REJECT_FWD_RESP				   (0xd1UL)
	#define HWRM_FWD_RESP					   (0xd2UL)
	#define HWRM_FWD_ASYNC_EVENT_CMPL			   (0xd3UL)
	#define HWRM_TEMP_MONITOR_QUERY			   (0xe0UL)
	#define HWRM_DBG_READ_DIRECT				   (0xff10UL)
	#define HWRM_DBG_READ_INDIRECT				   (0xff11UL)
	#define HWRM_DBG_WRITE_DIRECT				   (0xff12UL)
	#define HWRM_DBG_WRITE_INDIRECT			   (0xff13UL)
	#define HWRM_DBG_DUMP					   (0xff14UL)
	#define HWRM_NVM_MODIFY				   (0xfff4UL)
	#define HWRM_NVM_VERIFY_UPDATE				   (0xfff5UL)
	#define HWRM_NVM_GET_DEV_INFO				   (0xfff6UL)
	#define HWRM_NVM_ERASE_DIR_ENTRY			   (0xfff7UL)
	#define HWRM_NVM_MOD_DIR_ENTRY				   (0xfff8UL)
	#define HWRM_NVM_FIND_DIR_ENTRY			   (0xfff9UL)
	#define HWRM_NVM_GET_DIR_ENTRIES			   (0xfffaUL)
	#define HWRM_NVM_GET_DIR_INFO				   (0xfffbUL)
	#define HWRM_NVM_RAW_DUMP				   (0xfffcUL)
	#define HWRM_NVM_READ					   (0xfffdUL)
	#define HWRM_NVM_WRITE					   (0xfffeUL)
	#define HWRM_NVM_RAW_WRITE_BLK				   (0xffffUL)
	__le16 unused_0[3];
};

/* Return Codes (8 bytes) */
struct ret_codes {
	__le16 error_code;
	#define HWRM_ERR_CODE_SUCCESS				   (0x0UL)
	#define HWRM_ERR_CODE_FAIL				   (0x1UL)
	#define HWRM_ERR_CODE_INVALID_PARAMS			   (0x2UL)
	#define HWRM_ERR_CODE_RESOURCE_ACCESS_DENIED		   (0x3UL)
	#define HWRM_ERR_CODE_RESOURCE_ALLOC_ERROR		   (0x4UL)
	#define HWRM_ERR_CODE_INVALID_FLAGS			   (0x5UL)
	#define HWRM_ERR_CODE_INVALID_ENABLES			   (0x6UL)
	#define HWRM_ERR_CODE_HWRM_ERROR			   (0xfUL)
	#define HWRM_ERR_CODE_UNKNOWN_ERR			   (0xfffeUL)
	#define HWRM_ERR_CODE_CMD_NOT_SUPPORTED		   (0xffffUL)
	__le16 unused_0[3];
};

/* Output (16 bytes) */
struct hwrm_err_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 opaque_0;
	__le16 opaque_1;
	u8 cmd_err;
	u8 valid;
};

/* Port Tx Statistics Formats (408 bytes) */
struct tx_port_stats {
	__le64 tx_64b_frames;
	__le64 tx_65b_127b_frames;
	__le64 tx_128b_255b_frames;
	__le64 tx_256b_511b_frames;
	__le64 tx_512b_1023b_frames;
	__le64 tx_1024b_1518_frames;
	__le64 tx_good_vlan_frames;
	__le64 tx_1519b_2047_frames;
	__le64 tx_2048b_4095b_frames;
	__le64 tx_4096b_9216b_frames;
	__le64 tx_9217b_16383b_frames;
	__le64 tx_good_frames;
	__le64 tx_total_frames;
	__le64 tx_ucast_frames;
	__le64 tx_mcast_frames;
	__le64 tx_bcast_frames;
	__le64 tx_pause_frames;
	__le64 tx_pfc_frames;
	__le64 tx_jabber_frames;
	__le64 tx_fcs_err_frames;
	__le64 tx_control_frames;
	__le64 tx_oversz_frames;
	__le64 tx_single_dfrl_frames;
	__le64 tx_multi_dfrl_frames;
	__le64 tx_single_coll_frames;
	__le64 tx_multi_coll_frames;
	__le64 tx_late_coll_frames;
	__le64 tx_excessive_coll_frames;
	__le64 tx_frag_frames;
	__le64 tx_err;
	__le64 tx_tagged_frames;
	__le64 tx_dbl_tagged_frames;
	__le64 tx_runt_frames;
	__le64 tx_fifo_underruns;
	__le64 tx_pfc_ena_frames_pri0;
	__le64 tx_pfc_ena_frames_pri1;
	__le64 tx_pfc_ena_frames_pri2;
	__le64 tx_pfc_ena_frames_pri3;
	__le64 tx_pfc_ena_frames_pri4;
	__le64 tx_pfc_ena_frames_pri5;
	__le64 tx_pfc_ena_frames_pri6;
	__le64 tx_pfc_ena_frames_pri7;
	__le64 tx_eee_lpi_events;
	__le64 tx_eee_lpi_duration;
	__le64 tx_llfc_logical_msgs;
	__le64 tx_hcfc_msgs;
	__le64 tx_total_collisions;
	__le64 tx_bytes;
	__le64 tx_xthol_frames;
	__le64 tx_stat_discard;
	__le64 tx_stat_error;
};

/* Port Rx Statistics Formats (528 bytes) */
struct rx_port_stats {
	__le64 rx_64b_frames;
	__le64 rx_65b_127b_frames;
	__le64 rx_128b_255b_frames;
	__le64 rx_256b_511b_frames;
	__le64 rx_512b_1023b_frames;
	__le64 rx_1024b_1518_frames;
	__le64 rx_good_vlan_frames;
	__le64 rx_1519b_2047b_frames;
	__le64 rx_2048b_4095b_frames;
	__le64 rx_4096b_9216b_frames;
	__le64 rx_9217b_16383b_frames;
	__le64 rx_total_frames;
	__le64 rx_ucast_frames;
	__le64 rx_mcast_frames;
	__le64 rx_bcast_frames;
	__le64 rx_fcs_err_frames;
	__le64 rx_ctrl_frames;
	__le64 rx_pause_frames;
	__le64 rx_pfc_frames;
	__le64 rx_unsupported_opcode_frames;
	__le64 rx_unsupported_da_pausepfc_frames;
	__le64 rx_wrong_sa_frames;
	__le64 rx_align_err_frames;
	__le64 rx_oor_len_frames;
	__le64 rx_code_err_frames;
	__le64 rx_false_carrier_frames;
	__le64 rx_ovrsz_frames;
	__le64 rx_jbr_frames;
	__le64 rx_mtu_err_frames;
	__le64 rx_match_crc_frames;
	__le64 rx_promiscuous_frames;
	__le64 rx_tagged_frames;
	__le64 rx_double_tagged_frames;
	__le64 rx_trunc_frames;
	__le64 rx_good_frames;
	__le64 rx_pfc_xon2xoff_frames_pri0;
	__le64 rx_pfc_xon2xoff_frames_pri1;
	__le64 rx_pfc_xon2xoff_frames_pri2;
	__le64 rx_pfc_xon2xoff_frames_pri3;
	__le64 rx_pfc_xon2xoff_frames_pri4;
	__le64 rx_pfc_xon2xoff_frames_pri5;
	__le64 rx_pfc_xon2xoff_frames_pri6;
	__le64 rx_pfc_xon2xoff_frames_pri7;
	__le64 rx_pfc_ena_frames_pri0;
	__le64 rx_pfc_ena_frames_pri1;
	__le64 rx_pfc_ena_frames_pri2;
	__le64 rx_pfc_ena_frames_pri3;
	__le64 rx_pfc_ena_frames_pri4;
	__le64 rx_pfc_ena_frames_pri5;
	__le64 rx_pfc_ena_frames_pri6;
	__le64 rx_pfc_ena_frames_pri7;
	__le64 rx_sch_crc_err_frames;
	__le64 rx_undrsz_frames;
	__le64 rx_frag_frames;
	__le64 rx_eee_lpi_events;
	__le64 rx_eee_lpi_duration;
	__le64 rx_llfc_physical_msgs;
	__le64 rx_llfc_logical_msgs;
	__le64 rx_llfc_msgs_with_crc_err;
	__le64 rx_hcfc_msgs;
	__le64 rx_hcfc_msgs_with_crc_err;
	__le64 rx_bytes;
	__le64 rx_runt_bytes;
	__le64 rx_runt_frames;
	__le64 rx_stat_discard;
	__le64 rx_stat_err;
};

/* hwrm_ver_get */
/* Input (24 bytes) */
struct hwrm_ver_get_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	u8 hwrm_intf_maj;
	u8 hwrm_intf_min;
	u8 hwrm_intf_upd;
	u8 unused_0[5];
};

/* Output (128 bytes) */
struct hwrm_ver_get_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	u8 hwrm_intf_maj;
	u8 hwrm_intf_min;
	u8 hwrm_intf_upd;
	u8 hwrm_intf_rsvd;
	u8 hwrm_fw_maj;
	u8 hwrm_fw_min;
	u8 hwrm_fw_bld;
	u8 hwrm_fw_rsvd;
	u8 mgmt_fw_maj;
	u8 mgmt_fw_min;
	u8 mgmt_fw_bld;
	u8 mgmt_fw_rsvd;
	u8 netctrl_fw_maj;
	u8 netctrl_fw_min;
	u8 netctrl_fw_bld;
	u8 netctrl_fw_rsvd;
	__le32 reserved1;
	u8 roce_fw_maj;
	u8 roce_fw_min;
	u8 roce_fw_bld;
	u8 roce_fw_rsvd;
	char hwrm_fw_name[16];
	char mgmt_fw_name[16];
	char netctrl_fw_name[16];
	__le32 reserved2[4];
	char roce_fw_name[16];
	__le16 chip_num;
	u8 chip_rev;
	u8 chip_metal;
	u8 chip_bond_id;
	u8 chip_platform_type;
	#define VER_GET_RESP_CHIP_PLATFORM_TYPE_ASIC		   (0x0UL << 0)
	#define VER_GET_RESP_CHIP_PLATFORM_TYPE_FPGA		   (0x1UL << 0)
	#define VER_GET_RESP_CHIP_PLATFORM_TYPE_PALLADIUM	   (0x2UL << 0)
	__le16 max_req_win_len;
	__le16 max_resp_len;
	__le16 def_req_timeout;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_func_reset */
/* Input (24 bytes) */
struct hwrm_func_reset_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 enables;
	#define FUNC_RESET_REQ_ENABLES_VF_ID_VALID		    0x1UL
	__le16 vf_id;
	u8 func_reset_level;
	#define FUNC_RESET_REQ_FUNC_RESET_LEVEL_RESETALL	   (0x0UL << 0)
	#define FUNC_RESET_REQ_FUNC_RESET_LEVEL_RESETME	   (0x1UL << 0)
	#define FUNC_RESET_REQ_FUNC_RESET_LEVEL_RESETCHILDREN     (0x2UL << 0)
	#define FUNC_RESET_REQ_FUNC_RESET_LEVEL_RESETVF	   (0x3UL << 0)
	u8 unused_0;
};

/* Output (16 bytes) */
struct hwrm_func_reset_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_func_getfid */
/* Input (24 bytes) */
struct hwrm_func_getfid_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 enables;
	#define FUNC_GETFID_REQ_ENABLES_PCI_ID			    0x1UL
	__le16 pci_id;
	__le16 unused_0;
};

/* Output (16 bytes) */
struct hwrm_func_getfid_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le16 fid;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 unused_4;
	u8 valid;
};

/* hwrm_func_vf_alloc */
/* Input (24 bytes) */
struct hwrm_func_vf_alloc_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 enables;
	#define FUNC_VF_ALLOC_REQ_ENABLES_FIRST_VF_ID		    0x1UL
	__le16 first_vf_id;
	__le16 num_vfs;
};

/* Output (16 bytes) */
struct hwrm_func_vf_alloc_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le16 first_vf_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 unused_4;
	u8 valid;
};

/* hwrm_func_vf_free */
/* Input (24 bytes) */
struct hwrm_func_vf_free_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 enables;
	#define FUNC_VF_FREE_REQ_ENABLES_FIRST_VF_ID		    0x1UL
	__le16 first_vf_id;
	__le16 num_vfs;
};

/* Output (16 bytes) */
struct hwrm_func_vf_free_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_func_vf_cfg */
/* Input (32 bytes) */
struct hwrm_func_vf_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 enables;
	#define FUNC_VF_CFG_REQ_ENABLES_MTU			    0x1UL
	#define FUNC_VF_CFG_REQ_ENABLES_GUEST_VLAN		    0x2UL
	#define FUNC_VF_CFG_REQ_ENABLES_ASYNC_EVENT_CR		    0x4UL
	#define FUNC_VF_CFG_REQ_ENABLES_DFLT_MAC_ADDR		    0x8UL
	__le16 mtu;
	__le16 guest_vlan;
	__le16 async_event_cr;
	u8 dflt_mac_addr[6];
};

/* Output (16 bytes) */
struct hwrm_func_vf_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_func_qcaps */
/* Input (24 bytes) */
struct hwrm_func_qcaps_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 fid;
	__le16 unused_0[3];
};

/* Output (80 bytes) */
struct hwrm_func_qcaps_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le16 fid;
	__le16 port_id;
	__le32 flags;
	#define FUNC_QCAPS_RESP_FLAGS_PUSH_MODE_SUPPORTED	    0x1UL
	#define FUNC_QCAPS_RESP_FLAGS_GLOBAL_MSIX_AUTOMASKING      0x2UL
	#define FUNC_QCAPS_RESP_FLAGS_PTP_SUPPORTED		    0x4UL
	u8 mac_address[6];
	__le16 max_rsscos_ctx;
	__le16 max_cmpl_rings;
	__le16 max_tx_rings;
	__le16 max_rx_rings;
	__le16 max_l2_ctxs;
	__le16 max_vnics;
	__le16 first_vf_id;
	__le16 max_vfs;
	__le16 max_stat_ctx;
	__le32 max_encap_records;
	__le32 max_decap_records;
	__le32 max_tx_em_flows;
	__le32 max_tx_wm_flows;
	__le32 max_rx_em_flows;
	__le32 max_rx_wm_flows;
	__le32 max_mcast_filters;
	__le32 max_flow_id;
	__le32 max_hw_ring_grps;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_func_qcfg */
/* Input (24 bytes) */
struct hwrm_func_qcfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 fid;
	__le16 unused_0[3];
};

/* Output (72 bytes) */
struct hwrm_func_qcfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le16 fid;
	__le16 port_id;
	__le16 vlan;
	u8 unused_0;
	u8 unused_1;
	u8 mac_address[6];
	__le16 pci_id;
	__le16 alloc_rsscos_ctx;
	__le16 alloc_cmpl_rings;
	__le16 alloc_tx_rings;
	__le16 alloc_rx_rings;
	__le16 alloc_l2_ctx;
	__le16 alloc_vnics;
	__le16 mtu;
	__le16 mru;
	__le16 stat_ctx_id;
	u8 port_partition_type;
	#define FUNC_QCFG_RESP_PORT_PARTITION_TYPE_SPF		   (0x0UL << 0)
	#define FUNC_QCFG_RESP_PORT_PARTITION_TYPE_MPFS	   (0x1UL << 0)
	#define FUNC_QCFG_RESP_PORT_PARTITION_TYPE_NPAR1_0	   (0x2UL << 0)
	#define FUNC_QCFG_RESP_PORT_PARTITION_TYPE_NPAR1_5	   (0x3UL << 0)
	#define FUNC_QCFG_RESP_PORT_PARTITION_TYPE_NPAR2_0	   (0x4UL << 0)
	#define FUNC_QCFG_RESP_PORT_PARTITION_TYPE_UNKNOWN	   (0xffUL << 0)
	u8 unused_2;
	__le16 dflt_vnic_id;
	u8 unused_3;
	u8 unused_4;
	__le32 min_bw;
	__le32 max_bw;
	u8 evb_mode;
	#define FUNC_QCFG_RESP_EVB_MODE_NO_EVB			   (0x0UL << 0)
	#define FUNC_QCFG_RESP_EVB_MODE_VEB			   (0x1UL << 0)
	#define FUNC_QCFG_RESP_EVB_MODE_VEPA			   (0x2UL << 0)
	u8 unused_5;
	__le16 unused_6;
	__le32 alloc_mcast_filters;
	__le32 alloc_hw_ring_grps;
	u8 unused_7;
	u8 unused_8;
	u8 unused_9;
	u8 valid;
};

/* hwrm_func_cfg */
/* Input (88 bytes) */
struct hwrm_func_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 fid;
	u8 unused_0;
	u8 unused_1;
	__le32 flags;
	#define FUNC_CFG_REQ_FLAGS_PROM_MODE			    0x1UL
	#define FUNC_CFG_REQ_FLAGS_SRC_MAC_ADDR_CHECK		    0x2UL
	#define FUNC_CFG_REQ_FLAGS_SRC_IP_ADDR_CHECK		    0x4UL
	#define FUNC_CFG_REQ_FLAGS_VLAN_PRI_MATCH		    0x8UL
	#define FUNC_CFG_REQ_FLAGS_DFLT_PRI_NOMATCH		    0x10UL
	#define FUNC_CFG_REQ_FLAGS_DISABLE_PAUSE		    0x20UL
	#define FUNC_CFG_REQ_FLAGS_DISABLE_STP			    0x40UL
	#define FUNC_CFG_REQ_FLAGS_DISABLE_LLDP		    0x80UL
	#define FUNC_CFG_REQ_FLAGS_DISABLE_PTPV2		    0x100UL
	__le32 enables;
	#define FUNC_CFG_REQ_ENABLES_MTU			    0x1UL
	#define FUNC_CFG_REQ_ENABLES_MRU			    0x2UL
	#define FUNC_CFG_REQ_ENABLES_NUM_RSSCOS_CTXS		    0x4UL
	#define FUNC_CFG_REQ_ENABLES_NUM_CMPL_RINGS		    0x8UL
	#define FUNC_CFG_REQ_ENABLES_NUM_TX_RINGS		    0x10UL
	#define FUNC_CFG_REQ_ENABLES_NUM_RX_RINGS		    0x20UL
	#define FUNC_CFG_REQ_ENABLES_NUM_L2_CTXS		    0x40UL
	#define FUNC_CFG_REQ_ENABLES_NUM_VNICS			    0x80UL
	#define FUNC_CFG_REQ_ENABLES_NUM_STAT_CTXS		    0x100UL
	#define FUNC_CFG_REQ_ENABLES_DFLT_MAC_ADDR		    0x200UL
	#define FUNC_CFG_REQ_ENABLES_DFLT_VLAN			    0x400UL
	#define FUNC_CFG_REQ_ENABLES_DFLT_IP_ADDR		    0x800UL
	#define FUNC_CFG_REQ_ENABLES_MIN_BW			    0x1000UL
	#define FUNC_CFG_REQ_ENABLES_MAX_BW			    0x2000UL
	#define FUNC_CFG_REQ_ENABLES_ASYNC_EVENT_CR		    0x4000UL
	#define FUNC_CFG_REQ_ENABLES_VLAN_ANTISPOOF_MODE	    0x8000UL
	#define FUNC_CFG_REQ_ENABLES_ALLOWED_VLAN_PRIS		    0x10000UL
	#define FUNC_CFG_REQ_ENABLES_EVB_MODE			    0x20000UL
	#define FUNC_CFG_REQ_ENABLES_NUM_MCAST_FILTERS		    0x40000UL
	#define FUNC_CFG_REQ_ENABLES_NUM_HW_RING_GRPS		    0x80000UL
	__le16 mtu;
	__le16 mru;
	__le16 num_rsscos_ctxs;
	__le16 num_cmpl_rings;
	__le16 num_tx_rings;
	__le16 num_rx_rings;
	__le16 num_l2_ctxs;
	__le16 num_vnics;
	__le16 num_stat_ctxs;
	__le16 num_hw_ring_grps;
	u8 dflt_mac_addr[6];
	__le16 dflt_vlan;
	__be32 dflt_ip_addr[4];
	__le32 min_bw;
	__le32 max_bw;
	__le16 async_event_cr;
	u8 vlan_antispoof_mode;
	#define FUNC_CFG_REQ_VLAN_ANTISPOOF_MODE_NOCHECK	   (0x0UL << 0)
	#define FUNC_CFG_REQ_VLAN_ANTISPOOF_MODE_VALIDATE_VLAN    (0x1UL << 0)
	#define FUNC_CFG_REQ_VLAN_ANTISPOOF_MODE_INSERT_IF_VLANDNE (0x2UL << 0)
	#define FUNC_CFG_REQ_VLAN_ANTISPOOF_MODE_INSERT_OR_OVERRIDE_VLAN (0x3UL << 0)
	u8 allowed_vlan_pris;
	u8 evb_mode;
	#define FUNC_CFG_REQ_EVB_MODE_NO_EVB			   (0x0UL << 0)
	#define FUNC_CFG_REQ_EVB_MODE_VEB			   (0x1UL << 0)
	#define FUNC_CFG_REQ_EVB_MODE_VEPA			   (0x2UL << 0)
	u8 unused_2;
	__le16 num_mcast_filters;
};

/* Output (16 bytes) */
struct hwrm_func_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_func_qstats */
/* Input (24 bytes) */
struct hwrm_func_qstats_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 fid;
	__le16 unused_0[3];
};

/* Output (176 bytes) */
struct hwrm_func_qstats_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le64 tx_ucast_pkts;
	__le64 tx_mcast_pkts;
	__le64 tx_bcast_pkts;
	__le64 tx_err_pkts;
	__le64 tx_drop_pkts;
	__le64 tx_ucast_bytes;
	__le64 tx_mcast_bytes;
	__le64 tx_bcast_bytes;
	__le64 rx_ucast_pkts;
	__le64 rx_mcast_pkts;
	__le64 rx_bcast_pkts;
	__le64 rx_err_pkts;
	__le64 rx_drop_pkts;
	__le64 rx_ucast_bytes;
	__le64 rx_mcast_bytes;
	__le64 rx_bcast_bytes;
	__le64 rx_agg_pkts;
	__le64 rx_agg_bytes;
	__le64 rx_agg_events;
	__le64 rx_agg_aborts;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_func_clr_stats */
/* Input (24 bytes) */
struct hwrm_func_clr_stats_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 fid;
	__le16 unused_0[3];
};

/* Output (16 bytes) */
struct hwrm_func_clr_stats_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_func_vf_resc_free */
/* Input (24 bytes) */
struct hwrm_func_vf_resc_free_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 vf_id;
	__le16 unused_0[3];
};

/* Output (16 bytes) */
struct hwrm_func_vf_resc_free_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_func_vf_vnic_ids_query */
/* Input (32 bytes) */
struct hwrm_func_vf_vnic_ids_query_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 vf_id;
	u8 unused_0;
	u8 unused_1;
	__le32 max_vnic_id_cnt;
	__le64 vnic_id_tbl_addr;
};

/* Output (16 bytes) */
struct hwrm_func_vf_vnic_ids_query_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 vnic_id_cnt;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_func_drv_rgtr */
/* Input (80 bytes) */
struct hwrm_func_drv_rgtr_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define FUNC_DRV_RGTR_REQ_FLAGS_FWD_ALL_MODE		    0x1UL
	#define FUNC_DRV_RGTR_REQ_FLAGS_FWD_NONE_MODE		    0x2UL
	__le32 enables;
	#define FUNC_DRV_RGTR_REQ_ENABLES_OS_TYPE		    0x1UL
	#define FUNC_DRV_RGTR_REQ_ENABLES_VER			    0x2UL
	#define FUNC_DRV_RGTR_REQ_ENABLES_TIMESTAMP		    0x4UL
	#define FUNC_DRV_RGTR_REQ_ENABLES_VF_REQ_FWD		    0x8UL
	#define FUNC_DRV_RGTR_REQ_ENABLES_ASYNC_EVENT_FWD	    0x10UL
	__le16 os_type;
	#define FUNC_DRV_RGTR_REQ_OS_TYPE_UNKNOWN		   (0x0UL << 0)
	#define FUNC_DRV_RGTR_REQ_OS_TYPE_OTHER		   (0x1UL << 0)
	#define FUNC_DRV_RGTR_REQ_OS_TYPE_MSDOS		   (0xeUL << 0)
	#define FUNC_DRV_RGTR_REQ_OS_TYPE_WINDOWS		   (0x12UL << 0)
	#define FUNC_DRV_RGTR_REQ_OS_TYPE_SOLARIS		   (0x1dUL << 0)
	#define FUNC_DRV_RGTR_REQ_OS_TYPE_LINUX		   (0x24UL << 0)
	#define FUNC_DRV_RGTR_REQ_OS_TYPE_FREEBSD		   (0x2aUL << 0)
	#define FUNC_DRV_RGTR_REQ_OS_TYPE_ESXI			   (0x68UL << 0)
	#define FUNC_DRV_RGTR_REQ_OS_TYPE_WIN864		   (0x73UL << 0)
	#define FUNC_DRV_RGTR_REQ_OS_TYPE_WIN2012R2		   (0x74UL << 0)
	u8 ver_maj;
	u8 ver_min;
	u8 ver_upd;
	u8 unused_0;
	__le16 unused_1;
	__le32 timestamp;
	__le32 unused_2;
	__le32 vf_req_fwd[8];
	__le32 async_event_fwd[8];
};

/* Output (16 bytes) */
struct hwrm_func_drv_rgtr_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_func_drv_unrgtr */
/* Input (24 bytes) */
struct hwrm_func_drv_unrgtr_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define FUNC_DRV_UNRGTR_REQ_FLAGS_PREPARE_FOR_SHUTDOWN     0x1UL
	__le32 unused_0;
};

/* Output (16 bytes) */
struct hwrm_func_drv_unrgtr_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_func_buf_rgtr */
/* Input (128 bytes) */
struct hwrm_func_buf_rgtr_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 enables;
	#define FUNC_BUF_RGTR_REQ_ENABLES_VF_ID		    0x1UL
	#define FUNC_BUF_RGTR_REQ_ENABLES_ERR_BUF_ADDR		    0x2UL
	__le16 vf_id;
	__le16 req_buf_num_pages;
	__le16 req_buf_page_size;
	#define FUNC_BUF_RGTR_REQ_REQ_BUF_PAGE_SIZE_16B	   (0x4UL << 0)
	#define FUNC_BUF_RGTR_REQ_REQ_BUF_PAGE_SIZE_4K		   (0xcUL << 0)
	#define FUNC_BUF_RGTR_REQ_REQ_BUF_PAGE_SIZE_8K		   (0xdUL << 0)
	#define FUNC_BUF_RGTR_REQ_REQ_BUF_PAGE_SIZE_64K	   (0x10UL << 0)
	#define FUNC_BUF_RGTR_REQ_REQ_BUF_PAGE_SIZE_2M		   (0x16UL << 0)
	#define FUNC_BUF_RGTR_REQ_REQ_BUF_PAGE_SIZE_4M		   (0x17UL << 0)
	#define FUNC_BUF_RGTR_REQ_REQ_BUF_PAGE_SIZE_1G		   (0x1eUL << 0)
	__le16 req_buf_len;
	__le16 resp_buf_len;
	u8 unused_0;
	u8 unused_1;
	__le64 req_buf_page_addr0;
	__le64 req_buf_page_addr1;
	__le64 req_buf_page_addr2;
	__le64 req_buf_page_addr3;
	__le64 req_buf_page_addr4;
	__le64 req_buf_page_addr5;
	__le64 req_buf_page_addr6;
	__le64 req_buf_page_addr7;
	__le64 req_buf_page_addr8;
	__le64 req_buf_page_addr9;
	__le64 error_buf_addr;
	__le64 resp_buf_addr;
};

/* Output (16 bytes) */
struct hwrm_func_buf_rgtr_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_func_drv_qver */
/* Input (24 bytes) */
struct hwrm_func_drv_qver_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 reserved;
	__le16 fid;
	__le16 unused_0;
};

/* Output (16 bytes) */
struct hwrm_func_drv_qver_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le16 os_type;
	#define FUNC_DRV_QVER_RESP_OS_TYPE_UNKNOWN		   (0x0UL << 0)
	#define FUNC_DRV_QVER_RESP_OS_TYPE_OTHER		   (0x1UL << 0)
	#define FUNC_DRV_QVER_RESP_OS_TYPE_MSDOS		   (0xeUL << 0)
	#define FUNC_DRV_QVER_RESP_OS_TYPE_WINDOWS		   (0x12UL << 0)
	#define FUNC_DRV_QVER_RESP_OS_TYPE_SOLARIS		   (0x1dUL << 0)
	#define FUNC_DRV_QVER_RESP_OS_TYPE_LINUX		   (0x24UL << 0)
	#define FUNC_DRV_QVER_RESP_OS_TYPE_FREEBSD		   (0x2aUL << 0)
	#define FUNC_DRV_QVER_RESP_OS_TYPE_ESXI		   (0x68UL << 0)
	#define FUNC_DRV_QVER_RESP_OS_TYPE_WIN864		   (0x73UL << 0)
	#define FUNC_DRV_QVER_RESP_OS_TYPE_WIN2012R2		   (0x74UL << 0)
	u8 ver_maj;
	u8 ver_min;
	u8 ver_upd;
	u8 unused_0;
	u8 unused_1;
	u8 valid;
};

/* hwrm_port_phy_cfg */
/* Input (56 bytes) */
struct hwrm_port_phy_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define PORT_PHY_CFG_REQ_FLAGS_RESET_PHY		    0x1UL
	#define PORT_PHY_CFG_REQ_FLAGS_FORCE_LINK_DOWN		    0x2UL
	#define PORT_PHY_CFG_REQ_FLAGS_FORCE			    0x4UL
	#define PORT_PHY_CFG_REQ_FLAGS_RESTART_AUTONEG		    0x8UL
	#define PORT_PHY_CFG_REQ_FLAGS_EEE_ENABLE		    0x10UL
	#define PORT_PHY_CFG_REQ_FLAGS_EEE_DISABLE		    0x20UL
	#define PORT_PHY_CFG_REQ_FLAGS_EEE_TX_LPI_ENABLE	    0x40UL
	#define PORT_PHY_CFG_REQ_FLAGS_EEE_TX_LPI_DISABLE	    0x80UL
	__le32 enables;
	#define PORT_PHY_CFG_REQ_ENABLES_AUTO_MODE		    0x1UL
	#define PORT_PHY_CFG_REQ_ENABLES_AUTO_DUPLEX		    0x2UL
	#define PORT_PHY_CFG_REQ_ENABLES_AUTO_PAUSE		    0x4UL
	#define PORT_PHY_CFG_REQ_ENABLES_AUTO_LINK_SPEED	    0x8UL
	#define PORT_PHY_CFG_REQ_ENABLES_AUTO_LINK_SPEED_MASK      0x10UL
	#define PORT_PHY_CFG_REQ_ENABLES_WIRESPEED		    0x20UL
	#define PORT_PHY_CFG_REQ_ENABLES_LPBK			    0x40UL
	#define PORT_PHY_CFG_REQ_ENABLES_PREEMPHASIS		    0x80UL
	#define PORT_PHY_CFG_REQ_ENABLES_FORCE_PAUSE		    0x100UL
	#define PORT_PHY_CFG_REQ_ENABLES_EEE_LINK_SPEED_MASK       0x200UL
	#define PORT_PHY_CFG_REQ_ENABLES_TX_LPI_TIMER		    0x400UL
	__le16 port_id;
	__le16 force_link_speed;
	#define PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_100MB	   (0x1UL << 0)
	#define PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_1GB		   (0xaUL << 0)
	#define PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_2GB		   (0x14UL << 0)
	#define PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_2_5GB	   (0x19UL << 0)
	#define PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_10GB		   (0x64UL << 0)
	#define PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_20GB		   (0xc8UL << 0)
	#define PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_25GB		   (0xfaUL << 0)
	#define PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_40GB		   (0x190UL << 0)
	#define PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_50GB		   (0x1f4UL << 0)
	#define PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_100GB	   (0x3e8UL << 0)
	#define PORT_PHY_CFG_REQ_FORCE_LINK_SPEED_10MB		   (0xffffUL << 0)
	u8 auto_mode;
	#define PORT_PHY_CFG_REQ_AUTO_MODE_NONE		   (0x0UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_MODE_ALL_SPEEDS		   (0x1UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_MODE_ONE_SPEED		   (0x2UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_MODE_ONE_OR_BELOW	   (0x3UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_MODE_SPEED_MASK		   (0x4UL << 0)
	u8 auto_duplex;
	#define PORT_PHY_CFG_REQ_AUTO_DUPLEX_HALF		   (0x0UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_DUPLEX_FULL		   (0x1UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_DUPLEX_BOTH		   (0x2UL << 0)
	u8 auto_pause;
	#define PORT_PHY_CFG_REQ_AUTO_PAUSE_TX			    0x1UL
	#define PORT_PHY_CFG_REQ_AUTO_PAUSE_RX			    0x2UL
	#define PORT_PHY_CFG_REQ_AUTO_PAUSE_AUTONEG_PAUSE	    0x4UL
	u8 unused_0;
	__le16 auto_link_speed;
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_100MB		   (0x1UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_1GB		   (0xaUL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_2GB		   (0x14UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_2_5GB		   (0x19UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_10GB		   (0x64UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_20GB		   (0xc8UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_25GB		   (0xfaUL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_40GB		   (0x190UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_50GB		   (0x1f4UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_100GB		   (0x3e8UL << 0)
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_10MB		   (0xffffUL << 0)
	__le16 auto_link_speed_mask;
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_100MBHD      0x1UL
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_100MB	    0x2UL
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_1GBHD	    0x4UL
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_1GB	    0x8UL
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_2GB	    0x10UL
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_2_5GB	    0x20UL
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_10GB	    0x40UL
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_20GB	    0x80UL
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_25GB	    0x100UL
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_40GB	    0x200UL
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_50GB	    0x400UL
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_100GB	    0x800UL
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_10MBHD       0x1000UL
	#define PORT_PHY_CFG_REQ_AUTO_LINK_SPEED_MASK_10MB	    0x2000UL
	u8 wirespeed;
	#define PORT_PHY_CFG_REQ_WIRESPEED_OFF			   (0x0UL << 0)
	#define PORT_PHY_CFG_REQ_WIRESPEED_ON			   (0x1UL << 0)
	u8 lpbk;
	#define PORT_PHY_CFG_REQ_LPBK_NONE			   (0x0UL << 0)
	#define PORT_PHY_CFG_REQ_LPBK_LOCAL			   (0x1UL << 0)
	#define PORT_PHY_CFG_REQ_LPBK_REMOTE			   (0x2UL << 0)
	u8 force_pause;
	#define PORT_PHY_CFG_REQ_FORCE_PAUSE_TX		    0x1UL
	#define PORT_PHY_CFG_REQ_FORCE_PAUSE_RX		    0x2UL
	u8 unused_1;
	__le32 preemphasis;
	__le16 eee_link_speed_mask;
	#define PORT_PHY_CFG_REQ_EEE_LINK_SPEED_MASK_RSVD1	    0x1UL
	#define PORT_PHY_CFG_REQ_EEE_LINK_SPEED_MASK_100MB	    0x2UL
	#define PORT_PHY_CFG_REQ_EEE_LINK_SPEED_MASK_RSVD2	    0x4UL
	#define PORT_PHY_CFG_REQ_EEE_LINK_SPEED_MASK_1GB	    0x8UL
	#define PORT_PHY_CFG_REQ_EEE_LINK_SPEED_MASK_RSVD3	    0x10UL
	#define PORT_PHY_CFG_REQ_EEE_LINK_SPEED_MASK_RSVD4	    0x20UL
	#define PORT_PHY_CFG_REQ_EEE_LINK_SPEED_MASK_10GB	    0x40UL
	u8 unused_2;
	u8 unused_3;
	__le32 tx_lpi_timer;
	__le32 unused_4;
	#define PORT_PHY_CFG_REQ_TX_LPI_TIMER_MASK		    0xffffffUL
	#define PORT_PHY_CFG_REQ_TX_LPI_TIMER_SFT		    0
};

/* Output (16 bytes) */
struct hwrm_port_phy_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_port_phy_qcfg */
/* Input (24 bytes) */
struct hwrm_port_phy_qcfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 port_id;
	__le16 unused_0[3];
};

/* Output (96 bytes) */
struct hwrm_port_phy_qcfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	u8 link;
	#define PORT_PHY_QCFG_RESP_LINK_NO_LINK		   (0x0UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_SIGNAL			   (0x1UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_LINK			   (0x2UL << 0)
	u8 unused_0;
	__le16 link_speed;
	#define PORT_PHY_QCFG_RESP_LINK_SPEED_100MB		   (0x1UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_SPEED_1GB		   (0xaUL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_SPEED_2GB		   (0x14UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_SPEED_2_5GB		   (0x19UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_SPEED_10GB		   (0x64UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_SPEED_20GB		   (0xc8UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_SPEED_25GB		   (0xfaUL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_SPEED_40GB		   (0x190UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_SPEED_50GB		   (0x1f4UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_SPEED_100GB		   (0x3e8UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_SPEED_10MB		   (0xffffUL << 0)
	u8 duplex;
	#define PORT_PHY_QCFG_RESP_DUPLEX_HALF			   (0x0UL << 0)
	#define PORT_PHY_QCFG_RESP_DUPLEX_FULL			   (0x1UL << 0)
	u8 pause;
	#define PORT_PHY_QCFG_RESP_PAUSE_TX			    0x1UL
	#define PORT_PHY_QCFG_RESP_PAUSE_RX			    0x2UL
	__le16 support_speeds;
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_100MBHD	    0x1UL
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_100MB	    0x2UL
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_1GBHD	    0x4UL
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_1GB		    0x8UL
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_2GB		    0x10UL
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_2_5GB	    0x20UL
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_10GB		    0x40UL
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_20GB		    0x80UL
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_25GB		    0x100UL
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_40GB		    0x200UL
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_50GB		    0x400UL
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_100GB	    0x800UL
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_10MBHD	    0x1000UL
	#define PORT_PHY_QCFG_RESP_SUPPORT_SPEEDS_10MB		    0x2000UL
	__le16 force_link_speed;
	#define PORT_PHY_QCFG_RESP_FORCE_LINK_SPEED_100MB	   (0x1UL << 0)
	#define PORT_PHY_QCFG_RESP_FORCE_LINK_SPEED_1GB	   (0xaUL << 0)
	#define PORT_PHY_QCFG_RESP_FORCE_LINK_SPEED_2GB	   (0x14UL << 0)
	#define PORT_PHY_QCFG_RESP_FORCE_LINK_SPEED_2_5GB	   (0x19UL << 0)
	#define PORT_PHY_QCFG_RESP_FORCE_LINK_SPEED_10GB	   (0x64UL << 0)
	#define PORT_PHY_QCFG_RESP_FORCE_LINK_SPEED_20GB	   (0xc8UL << 0)
	#define PORT_PHY_QCFG_RESP_FORCE_LINK_SPEED_25GB	   (0xfaUL << 0)
	#define PORT_PHY_QCFG_RESP_FORCE_LINK_SPEED_40GB	   (0x190UL << 0)
	#define PORT_PHY_QCFG_RESP_FORCE_LINK_SPEED_50GB	   (0x1f4UL << 0)
	#define PORT_PHY_QCFG_RESP_FORCE_LINK_SPEED_100GB	   (0x3e8UL << 0)
	#define PORT_PHY_QCFG_RESP_FORCE_LINK_SPEED_10MB	   (0xffffUL << 0)
	u8 auto_mode;
	#define PORT_PHY_QCFG_RESP_AUTO_MODE_NONE		   (0x0UL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_MODE_ALL_SPEEDS	   (0x1UL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_MODE_ONE_SPEED		   (0x2UL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_MODE_ONE_OR_BELOW	   (0x3UL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_MODE_SPEED_MASK	   (0x4UL << 0)
	u8 auto_pause;
	#define PORT_PHY_QCFG_RESP_AUTO_PAUSE_TX		    0x1UL
	#define PORT_PHY_QCFG_RESP_AUTO_PAUSE_RX		    0x2UL
	#define PORT_PHY_QCFG_RESP_AUTO_PAUSE_AUTONEG_PAUSE	    0x4UL
	__le16 auto_link_speed;
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_100MB	   (0x1UL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_1GB		   (0xaUL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_2GB		   (0x14UL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_2_5GB	   (0x19UL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_10GB	   (0x64UL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_20GB	   (0xc8UL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_25GB	   (0xfaUL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_40GB	   (0x190UL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_50GB	   (0x1f4UL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_100GB	   (0x3e8UL << 0)
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_10MB	   (0xffffUL << 0)
	__le16 auto_link_speed_mask;
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_100MBHD    0x1UL
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_100MB      0x2UL
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_1GBHD      0x4UL
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_1GB	    0x8UL
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_2GB	    0x10UL
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_2_5GB      0x20UL
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_10GB       0x40UL
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_20GB       0x80UL
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_25GB       0x100UL
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_40GB       0x200UL
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_50GB       0x400UL
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_100GB      0x800UL
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_10MBHD     0x1000UL
	#define PORT_PHY_QCFG_RESP_AUTO_LINK_SPEED_MASK_10MB       0x2000UL
	u8 wirespeed;
	#define PORT_PHY_QCFG_RESP_WIRESPEED_OFF		   (0x0UL << 0)
	#define PORT_PHY_QCFG_RESP_WIRESPEED_ON		   (0x1UL << 0)
	u8 lpbk;
	#define PORT_PHY_QCFG_RESP_LPBK_NONE			   (0x0UL << 0)
	#define PORT_PHY_QCFG_RESP_LPBK_LOCAL			   (0x1UL << 0)
	#define PORT_PHY_QCFG_RESP_LPBK_REMOTE			   (0x2UL << 0)
	u8 force_pause;
	#define PORT_PHY_QCFG_RESP_FORCE_PAUSE_TX		    0x1UL
	#define PORT_PHY_QCFG_RESP_FORCE_PAUSE_RX		    0x2UL
	u8 module_status;
	#define PORT_PHY_QCFG_RESP_MODULE_STATUS_NONE		   (0x0UL << 0)
	#define PORT_PHY_QCFG_RESP_MODULE_STATUS_DISABLETX	   (0x1UL << 0)
	#define PORT_PHY_QCFG_RESP_MODULE_STATUS_WARNINGMSG       (0x2UL << 0)
	#define PORT_PHY_QCFG_RESP_MODULE_STATUS_PWRDOWN	   (0x3UL << 0)
	#define PORT_PHY_QCFG_RESP_MODULE_STATUS_NOTINSERTED      (0x4UL << 0)
	#define PORT_PHY_QCFG_RESP_MODULE_STATUS_NOTAPPLICABLE    (0xffUL << 0)
	__le32 preemphasis;
	u8 phy_maj;
	u8 phy_min;
	u8 phy_bld;
	u8 phy_type;
	#define PORT_PHY_QCFG_RESP_PHY_TYPE_UNKNOWN		   (0x0UL << 0)
	#define PORT_PHY_QCFG_RESP_PHY_TYPE_BASECR		   (0x1UL << 0)
	#define PORT_PHY_QCFG_RESP_PHY_TYPE_BASEKR4		   (0x2UL << 0)
	#define PORT_PHY_QCFG_RESP_PHY_TYPE_BASELR		   (0x3UL << 0)
	#define PORT_PHY_QCFG_RESP_PHY_TYPE_BASESR		   (0x4UL << 0)
	#define PORT_PHY_QCFG_RESP_PHY_TYPE_BASEKR2		   (0x5UL << 0)
	#define PORT_PHY_QCFG_RESP_PHY_TYPE_BASEKX		   (0x6UL << 0)
	#define PORT_PHY_QCFG_RESP_PHY_TYPE_BASEKR		   (0x7UL << 0)
	#define PORT_PHY_QCFG_RESP_PHY_TYPE_BASET		   (0x8UL << 0)
	#define PORT_PHY_QCFG_RESP_PHY_TYPE_BASETE		   (0x9UL << 0)
	#define PORT_PHY_QCFG_RESP_PHY_TYPE_SGMIIEXTPHY	   (0xaUL << 0)
	u8 media_type;
	#define PORT_PHY_QCFG_RESP_MEDIA_TYPE_UNKNOWN		   (0x0UL << 0)
	#define PORT_PHY_QCFG_RESP_MEDIA_TYPE_TP		   (0x1UL << 0)
	#define PORT_PHY_QCFG_RESP_MEDIA_TYPE_DAC		   (0x2UL << 0)
	#define PORT_PHY_QCFG_RESP_MEDIA_TYPE_FIBRE		   (0x3UL << 0)
	u8 xcvr_pkg_type;
	#define PORT_PHY_QCFG_RESP_XCVR_PKG_TYPE_XCVR_INTERNAL    (0x1UL << 0)
	#define PORT_PHY_QCFG_RESP_XCVR_PKG_TYPE_XCVR_EXTERNAL    (0x2UL << 0)
	u8 eee_config_phy_addr;
	#define PORT_PHY_QCFG_RESP_PHY_ADDR_MASK		    0x1fUL
	#define PORT_PHY_QCFG_RESP_PHY_ADDR_SFT		    0
	#define PORT_PHY_QCFG_RESP_EEE_CONFIG_EEE_ENABLED	    0x20UL
	#define PORT_PHY_QCFG_RESP_EEE_CONFIG_EEE_ACTIVE	    0x40UL
	#define PORT_PHY_QCFG_RESP_EEE_CONFIG_EEE_TX_LPI	    0x80UL
	#define PORT_PHY_QCFG_RESP_EEE_CONFIG_MASK		    0xe0UL
	#define PORT_PHY_QCFG_RESP_EEE_CONFIG_SFT		    5
	u8 parallel_detect;
	#define PORT_PHY_QCFG_RESP_PARALLEL_DETECT		    0x1UL
	#define PORT_PHY_QCFG_RESP_RESERVED_MASK		    0xfeUL
	#define PORT_PHY_QCFG_RESP_RESERVED_SFT		    1
	__le16 link_partner_adv_speeds;
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_100MBHD 0x1UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_100MB   0x2UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_1GBHD   0x4UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_1GB     0x8UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_2GB     0x10UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_2_5GB   0x20UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_10GB    0x40UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_20GB    0x80UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_25GB    0x100UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_40GB    0x200UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_50GB    0x400UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_100GB   0x800UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_10MBHD  0x1000UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_SPEEDS_10MB    0x2000UL
	u8 link_partner_adv_auto_mode;
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_AUTO_MODE_NONE (0x0UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_AUTO_MODE_ALL_SPEEDS (0x1UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_AUTO_MODE_ONE_SPEED (0x2UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_AUTO_MODE_ONE_OR_BELOW (0x3UL << 0)
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_AUTO_MODE_SPEED_MASK (0x4UL << 0)
	u8 link_partner_adv_pause;
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_PAUSE_TX       0x1UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_PAUSE_RX       0x2UL
	__le16 adv_eee_link_speed_mask;
	#define PORT_PHY_QCFG_RESP_ADV_EEE_LINK_SPEED_MASK_RSVD1   0x1UL
	#define PORT_PHY_QCFG_RESP_ADV_EEE_LINK_SPEED_MASK_100MB   0x2UL
	#define PORT_PHY_QCFG_RESP_ADV_EEE_LINK_SPEED_MASK_RSVD2   0x4UL
	#define PORT_PHY_QCFG_RESP_ADV_EEE_LINK_SPEED_MASK_1GB     0x8UL
	#define PORT_PHY_QCFG_RESP_ADV_EEE_LINK_SPEED_MASK_RSVD3   0x10UL
	#define PORT_PHY_QCFG_RESP_ADV_EEE_LINK_SPEED_MASK_RSVD4   0x20UL
	#define PORT_PHY_QCFG_RESP_ADV_EEE_LINK_SPEED_MASK_10GB    0x40UL
	__le16 link_partner_adv_eee_link_speed_mask;
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_EEE_LINK_SPEED_MASK_RSVD1 0x1UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_EEE_LINK_SPEED_MASK_100MB 0x2UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_EEE_LINK_SPEED_MASK_RSVD2 0x4UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_EEE_LINK_SPEED_MASK_1GB 0x8UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_EEE_LINK_SPEED_MASK_RSVD3 0x10UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_EEE_LINK_SPEED_MASK_RSVD4 0x20UL
	#define PORT_PHY_QCFG_RESP_LINK_PARTNER_ADV_EEE_LINK_SPEED_MASK_10GB 0x40UL
	__le32 xcvr_identifier_type_tx_lpi_timer;
	#define PORT_PHY_QCFG_RESP_TX_LPI_TIMER_MASK		    0xffffffUL
	#define PORT_PHY_QCFG_RESP_TX_LPI_TIMER_SFT		    0
	#define PORT_PHY_QCFG_RESP_XCVR_IDENTIFIER_TYPE_MASK       0xff000000UL
	#define PORT_PHY_QCFG_RESP_XCVR_IDENTIFIER_TYPE_SFT	    24
	#define PORT_PHY_QCFG_RESP_XCVR_IDENTIFIER_TYPE_UNKNOWN   (0x0UL << 24)
	#define PORT_PHY_QCFG_RESP_XCVR_IDENTIFIER_TYPE_SFP       (0x3UL << 24)
	#define PORT_PHY_QCFG_RESP_XCVR_IDENTIFIER_TYPE_QSFP      (0xcUL << 24)
	#define PORT_PHY_QCFG_RESP_XCVR_IDENTIFIER_TYPE_QSFPPLUS  (0xdUL << 24)
	#define PORT_PHY_QCFG_RESP_XCVR_IDENTIFIER_TYPE_QSFP28    (0x11UL << 24)
	__le32 unused_1;
	char phy_vendor_name[16];
	char phy_vendor_partnumber[16];
	__le32 unused_2;
	u8 unused_3;
	u8 unused_4;
	u8 unused_5;
	u8 valid;
};

/* hwrm_port_mac_cfg */
/* Input (40 bytes) */
struct hwrm_port_mac_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define PORT_MAC_CFG_REQ_FLAGS_MATCH_LINK		    0x1UL
	#define PORT_MAC_CFG_REQ_FLAGS_COS_ASSIGNMENT_ENABLE       0x2UL
	#define PORT_MAC_CFG_REQ_FLAGS_TUNNEL_PRI2COS_ENABLE       0x4UL
	#define PORT_MAC_CFG_REQ_FLAGS_IP_DSCP2COS_ENABLE	    0x8UL
	#define PORT_MAC_CFG_REQ_FLAGS_PTP_RX_TS_CAPTURE_ENABLE    0x10UL
	#define PORT_MAC_CFG_REQ_FLAGS_PTP_RX_TS_CAPTURE_DISABLE   0x20UL
	#define PORT_MAC_CFG_REQ_FLAGS_PTP_TX_TS_CAPTURE_ENABLE    0x40UL
	#define PORT_MAC_CFG_REQ_FLAGS_PTP_TX_TS_CAPTURE_DISABLE   0x80UL
	__le32 enables;
	#define PORT_MAC_CFG_REQ_ENABLES_IPG			    0x1UL
	#define PORT_MAC_CFG_REQ_ENABLES_LPBK			    0x2UL
	#define PORT_MAC_CFG_REQ_ENABLES_IVLAN_PRI2COS_MAP_PRI     0x4UL
	#define PORT_MAC_CFG_REQ_ENABLES_LCOS_MAP_PRI		    0x8UL
	#define PORT_MAC_CFG_REQ_ENABLES_TUNNEL_PRI2COS_MAP_PRI    0x10UL
	#define PORT_MAC_CFG_REQ_ENABLES_DSCP2COS_MAP_PRI	    0x20UL
	#define PORT_MAC_CFG_REQ_ENABLES_RX_TS_CAPTURE_PTP_MSG_TYPE 0x40UL
	#define PORT_MAC_CFG_REQ_ENABLES_TX_TS_CAPTURE_PTP_MSG_TYPE 0x80UL
	__le16 port_id;
	u8 ipg;
	u8 lpbk;
	#define PORT_MAC_CFG_REQ_LPBK_NONE			   (0x0UL << 0)
	#define PORT_MAC_CFG_REQ_LPBK_LOCAL			   (0x1UL << 0)
	#define PORT_MAC_CFG_REQ_LPBK_REMOTE			   (0x2UL << 0)
	u8 ivlan_pri2cos_map_pri;
	u8 lcos_map_pri;
	u8 tunnel_pri2cos_map_pri;
	u8 dscp2pri_map_pri;
	__le16 rx_ts_capture_ptp_msg_type;
	__le16 tx_ts_capture_ptp_msg_type;
	__le32 unused_0;
};

/* Output (16 bytes) */
struct hwrm_port_mac_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le16 mru;
	__le16 mtu;
	u8 ipg;
	u8 lpbk;
	#define PORT_MAC_CFG_RESP_LPBK_NONE			   (0x0UL << 0)
	#define PORT_MAC_CFG_RESP_LPBK_LOCAL			   (0x1UL << 0)
	#define PORT_MAC_CFG_RESP_LPBK_REMOTE			   (0x2UL << 0)
	u8 unused_0;
	u8 valid;
};

/* hwrm_port_qstats */
/* Input (40 bytes) */
struct hwrm_port_qstats_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 port_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2[3];
	u8 unused_3;
	__le64 tx_stat_host_addr;
	__le64 rx_stat_host_addr;
};

/* Output (16 bytes) */
struct hwrm_port_qstats_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le16 tx_stat_size;
	__le16 rx_stat_size;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_port_lpbk_qstats */
/* Input (16 bytes) */
struct hwrm_port_lpbk_qstats_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
};

/* Output (96 bytes) */
struct hwrm_port_lpbk_qstats_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le64 lpbk_ucast_frames;
	__le64 lpbk_mcast_frames;
	__le64 lpbk_bcast_frames;
	__le64 lpbk_ucast_bytes;
	__le64 lpbk_mcast_bytes;
	__le64 lpbk_bcast_bytes;
	__le64 tx_stat_discard;
	__le64 tx_stat_error;
	__le64 rx_stat_discard;
	__le64 rx_stat_error;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_port_clr_stats */
/* Input (24 bytes) */
struct hwrm_port_clr_stats_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 port_id;
	__le16 unused_0[3];
};

/* Output (16 bytes) */
struct hwrm_port_clr_stats_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_port_lpbk_clr_stats */
/* Input (16 bytes) */
struct hwrm_port_lpbk_clr_stats_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
};

/* Output (16 bytes) */
struct hwrm_port_lpbk_clr_stats_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_port_blink_led */
/* Input (24 bytes) */
struct hwrm_port_blink_led_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 num_blinks;
	__le32 unused_0;
};

/* Output (16 bytes) */
struct hwrm_port_blink_led_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_port_phy_qcaps */
/* Input (24 bytes) */
struct hwrm_port_phy_qcaps_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 port_id;
	__le16 unused_0[3];
};

/* Output (24 bytes) */
struct hwrm_port_phy_qcaps_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	u8 eee_supported;
	#define PORT_PHY_QCAPS_RESP_EEE_SUPPORTED		    0x1UL
	#define PORT_PHY_QCAPS_RESP_RSVD1_MASK			    0xfeUL
	#define PORT_PHY_QCAPS_RESP_RSVD1_SFT			    1
	u8 unused_0;
	__le16 supported_speeds_force_mode;
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_100MBHD 0x1UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_100MB 0x2UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_1GBHD 0x4UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_1GB 0x8UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_2GB 0x10UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_2_5GB 0x20UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_10GB 0x40UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_20GB 0x80UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_25GB 0x100UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_40GB 0x200UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_50GB 0x400UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_100GB 0x800UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_10MBHD 0x1000UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_FORCE_MODE_10MB 0x2000UL
	__le16 supported_speeds_auto_mode;
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_100MBHD 0x1UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_100MB 0x2UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_1GBHD 0x4UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_1GB 0x8UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_2GB 0x10UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_2_5GB 0x20UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_10GB 0x40UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_20GB 0x80UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_25GB 0x100UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_40GB 0x200UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_50GB 0x400UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_100GB 0x800UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_10MBHD 0x1000UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_AUTO_MODE_10MB 0x2000UL
	__le16 supported_speeds_eee_mode;
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_EEE_MODE_RSVD1 0x1UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_EEE_MODE_100MB 0x2UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_EEE_MODE_RSVD2 0x4UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_EEE_MODE_1GB  0x8UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_EEE_MODE_RSVD3 0x10UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_EEE_MODE_RSVD4 0x20UL
	#define PORT_PHY_QCAPS_RESP_SUPPORTED_SPEEDS_EEE_MODE_10GB 0x40UL
	__le32 tx_lpi_timer_low;
	#define PORT_PHY_QCAPS_RESP_TX_LPI_TIMER_LOW_MASK	    0xffffffUL
	#define PORT_PHY_QCAPS_RESP_TX_LPI_TIMER_LOW_SFT	    0
	#define PORT_PHY_QCAPS_RESP_RSVD2_MASK			    0xff000000UL
	#define PORT_PHY_QCAPS_RESP_RSVD2_SFT			    24
	__le32 valid_tx_lpi_timer_high;
	#define PORT_PHY_QCAPS_RESP_TX_LPI_TIMER_HIGH_MASK	    0xffffffUL
	#define PORT_PHY_QCAPS_RESP_TX_LPI_TIMER_HIGH_SFT	    0
	#define PORT_PHY_QCAPS_RESP_VALID_MASK			    0xff000000UL
	#define PORT_PHY_QCAPS_RESP_VALID_SFT			    24
};

/* hwrm_port_phy_i2c_read */
/* Input (40 bytes) */
struct hwrm_port_phy_i2c_read_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	__le32 enables;
	#define PORT_PHY_I2C_READ_REQ_ENABLES_PAGE_OFFSET	    0x1UL
	__le16 port_id;
	u8 i2c_slave_addr;
	u8 unused_0;
	__le16 page_number;
	__le16 page_offset;
	u8 data_length;
	u8 unused_1[7];
};

/* Output (80 bytes) */
struct hwrm_port_phy_i2c_read_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 data[16];
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* Input (24 bytes) */
struct hwrm_queue_qportcfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define QUEUE_QPORTCFG_REQ_FLAGS_PATH			    0x1UL
	#define QUEUE_QPORTCFG_REQ_FLAGS_PATH_TX		   (0x0UL << 0)
	#define QUEUE_QPORTCFG_REQ_FLAGS_PATH_RX		   (0x1UL << 0)
	#define QUEUE_QPORTCFG_REQ_FLAGS_PATH_LAST    QUEUE_QPORTCFG_REQ_FLAGS_PATH_RX
	__le16 port_id;
	__le16 unused_0;
};

/* Output (32 bytes) */
struct hwrm_queue_qportcfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	u8 max_configurable_queues;
	u8 max_configurable_lossless_queues;
	u8 queue_cfg_allowed;
	u8 queue_buffers_cfg_allowed;
	u8 queue_pfcenable_cfg_allowed;
	u8 queue_pri2cos_cfg_allowed;
	u8 queue_cos2bw_cfg_allowed;
	u8 queue_id0;
	u8 queue_id0_service_profile;
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID0_SERVICE_PROFILE_LOSSY (0x0UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID0_SERVICE_PROFILE_LOSSLESS (0x1UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID0_SERVICE_PROFILE_UNKNOWN (0xffUL << 0)
	u8 queue_id1;
	u8 queue_id1_service_profile;
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID1_SERVICE_PROFILE_LOSSY (0x0UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID1_SERVICE_PROFILE_LOSSLESS (0x1UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID1_SERVICE_PROFILE_UNKNOWN (0xffUL << 0)
	u8 queue_id2;
	u8 queue_id2_service_profile;
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID2_SERVICE_PROFILE_LOSSY (0x0UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID2_SERVICE_PROFILE_LOSSLESS (0x1UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID2_SERVICE_PROFILE_UNKNOWN (0xffUL << 0)
	u8 queue_id3;
	u8 queue_id3_service_profile;
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID3_SERVICE_PROFILE_LOSSY (0x0UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID3_SERVICE_PROFILE_LOSSLESS (0x1UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID3_SERVICE_PROFILE_UNKNOWN (0xffUL << 0)
	u8 queue_id4;
	u8 queue_id4_service_profile;
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID4_SERVICE_PROFILE_LOSSY (0x0UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID4_SERVICE_PROFILE_LOSSLESS (0x1UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID4_SERVICE_PROFILE_UNKNOWN (0xffUL << 0)
	u8 queue_id5;
	u8 queue_id5_service_profile;
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID5_SERVICE_PROFILE_LOSSY (0x0UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID5_SERVICE_PROFILE_LOSSLESS (0x1UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID5_SERVICE_PROFILE_UNKNOWN (0xffUL << 0)
	u8 queue_id6;
	u8 queue_id6_service_profile;
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID6_SERVICE_PROFILE_LOSSY (0x0UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID6_SERVICE_PROFILE_LOSSLESS (0x1UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID6_SERVICE_PROFILE_UNKNOWN (0xffUL << 0)
	u8 queue_id7;
	u8 queue_id7_service_profile;
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID7_SERVICE_PROFILE_LOSSY (0x0UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID7_SERVICE_PROFILE_LOSSLESS (0x1UL << 0)
	#define QUEUE_QPORTCFG_RESP_QUEUE_ID7_SERVICE_PROFILE_UNKNOWN (0xffUL << 0)
	u8 valid;
};

/* hwrm_queue_cfg */
/* Input (40 bytes) */
struct hwrm_queue_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define QUEUE_CFG_REQ_FLAGS_PATH			    0x1UL
	#define QUEUE_CFG_REQ_FLAGS_PATH_TX			   (0x0UL << 0)
	#define QUEUE_CFG_REQ_FLAGS_PATH_RX			   (0x1UL << 0)
	#define QUEUE_CFG_REQ_FLAGS_PATH_LAST    QUEUE_CFG_REQ_FLAGS_PATH_RX
	__le32 enables;
	#define QUEUE_CFG_REQ_ENABLES_DFLT_LEN			    0x1UL
	#define QUEUE_CFG_REQ_ENABLES_SERVICE_PROFILE		    0x2UL
	__le32 queue_id;
	__le32 dflt_len;
	u8 service_profile;
	#define QUEUE_CFG_REQ_SERVICE_PROFILE_LOSSY		   (0x0UL << 0)
	#define QUEUE_CFG_REQ_SERVICE_PROFILE_LOSSLESS		   (0x1UL << 0)
	#define QUEUE_CFG_REQ_SERVICE_PROFILE_UNKNOWN		   (0xffUL << 0)
	u8 unused_0[7];
};

/* Output (16 bytes) */
struct hwrm_queue_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_queue_buffers_cfg */
/* Input (56 bytes) */
struct hwrm_queue_buffers_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define QUEUE_BUFFERS_CFG_REQ_FLAGS_PATH		    0x1UL
	#define QUEUE_BUFFERS_CFG_REQ_FLAGS_PATH_TX		   (0x0UL << 0)
	#define QUEUE_BUFFERS_CFG_REQ_FLAGS_PATH_RX		   (0x1UL << 0)
	#define QUEUE_BUFFERS_CFG_REQ_FLAGS_PATH_LAST    QUEUE_BUFFERS_CFG_REQ_FLAGS_PATH_RX
	__le32 enables;
	#define QUEUE_BUFFERS_CFG_REQ_ENABLES_RESERVED		    0x1UL
	#define QUEUE_BUFFERS_CFG_REQ_ENABLES_SHARED		    0x2UL
	#define QUEUE_BUFFERS_CFG_REQ_ENABLES_XOFF		    0x4UL
	#define QUEUE_BUFFERS_CFG_REQ_ENABLES_XON		    0x8UL
	#define QUEUE_BUFFERS_CFG_REQ_ENABLES_FULL		    0x10UL
	#define QUEUE_BUFFERS_CFG_REQ_ENABLES_NOTFULL		    0x20UL
	#define QUEUE_BUFFERS_CFG_REQ_ENABLES_MAX		    0x40UL
	__le32 queue_id;
	__le32 reserved;
	__le32 shared;
	__le32 xoff;
	__le32 xon;
	__le32 full;
	__le32 notfull;
	__le32 max;
};

/* Output (16 bytes) */
struct hwrm_queue_buffers_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_queue_pfcenable_cfg */
/* Input (24 bytes) */
struct hwrm_queue_pfcenable_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define QUEUE_PFCENABLE_CFG_REQ_FLAGS_PRI0_PFC_ENABLED     0x1UL
	#define QUEUE_PFCENABLE_CFG_REQ_FLAGS_PRI1_PFC_ENABLED     0x2UL
	#define QUEUE_PFCENABLE_CFG_REQ_FLAGS_PRI2_PFC_ENABLED     0x4UL
	#define QUEUE_PFCENABLE_CFG_REQ_FLAGS_PRI3_PFC_ENABLED     0x8UL
	#define QUEUE_PFCENABLE_CFG_REQ_FLAGS_PRI4_PFC_ENABLED     0x10UL
	#define QUEUE_PFCENABLE_CFG_REQ_FLAGS_PRI5_PFC_ENABLED     0x20UL
	#define QUEUE_PFCENABLE_CFG_REQ_FLAGS_PRI6_PFC_ENABLED     0x40UL
	#define QUEUE_PFCENABLE_CFG_REQ_FLAGS_PRI7_PFC_ENABLED     0x80UL
	__le16 port_id;
	__le16 unused_0;
};

/* Output (16 bytes) */
struct hwrm_queue_pfcenable_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_queue_pri2cos_cfg */
/* Input (40 bytes) */
struct hwrm_queue_pri2cos_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define QUEUE_PRI2COS_CFG_REQ_FLAGS_PATH		    0x1UL
	#define QUEUE_PRI2COS_CFG_REQ_FLAGS_PATH_TX		   (0x0UL << 0)
	#define QUEUE_PRI2COS_CFG_REQ_FLAGS_PATH_RX		   (0x1UL << 0)
	#define QUEUE_PRI2COS_CFG_REQ_FLAGS_PATH_LAST    QUEUE_PRI2COS_CFG_REQ_FLAGS_PATH_RX
	#define QUEUE_PRI2COS_CFG_REQ_FLAGS_IVLAN		    0x2UL
	__le32 enables;
	u8 port_id;
	u8 pri0_cos_queue_id;
	u8 pri1_cos_queue_id;
	u8 pri2_cos_queue_id;
	u8 pri3_cos_queue_id;
	u8 pri4_cos_queue_id;
	u8 pri5_cos_queue_id;
	u8 pri6_cos_queue_id;
	u8 pri7_cos_queue_id;
	u8 unused_0[7];
};

/* Output (16 bytes) */
struct hwrm_queue_pri2cos_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_queue_cos2bw_cfg */
/* Input (128 bytes) */
struct hwrm_queue_cos2bw_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	__le32 enables;
	#define QUEUE_COS2BW_CFG_REQ_ENABLES_COS_QUEUE_ID0_VALID   0x1UL
	#define QUEUE_COS2BW_CFG_REQ_ENABLES_COS_QUEUE_ID1_VALID   0x2UL
	#define QUEUE_COS2BW_CFG_REQ_ENABLES_COS_QUEUE_ID2_VALID   0x4UL
	#define QUEUE_COS2BW_CFG_REQ_ENABLES_COS_QUEUE_ID3_VALID   0x8UL
	#define QUEUE_COS2BW_CFG_REQ_ENABLES_COS_QUEUE_ID4_VALID   0x10UL
	#define QUEUE_COS2BW_CFG_REQ_ENABLES_COS_QUEUE_ID5_VALID   0x20UL
	#define QUEUE_COS2BW_CFG_REQ_ENABLES_COS_QUEUE_ID6_VALID   0x40UL
	#define QUEUE_COS2BW_CFG_REQ_ENABLES_COS_QUEUE_ID7_VALID   0x80UL
	__le16 port_id;
	u8 queue_id0;
	u8 unused_0;
	__le32 queue_id0_min_bw;
	__le32 queue_id0_max_bw;
	u8 queue_id0_tsa_assign;
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID0_TSA_ASSIGN_SP      (0x0UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID0_TSA_ASSIGN_ETS     (0x1UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID0_TSA_ASSIGN_RESERVED_FIRST (0x2UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID0_TSA_ASSIGN_RESERVED_LAST (0xffffUL << 0)
	u8 queue_id0_pri_lvl;
	u8 queue_id0_bw_weight;
	u8 queue_id1;
	__le32 queue_id1_min_bw;
	__le32 queue_id1_max_bw;
	u8 queue_id1_tsa_assign;
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID1_TSA_ASSIGN_SP      (0x0UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID1_TSA_ASSIGN_ETS     (0x1UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID1_TSA_ASSIGN_RESERVED_FIRST (0x2UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID1_TSA_ASSIGN_RESERVED_LAST (0xffffUL << 0)
	u8 queue_id1_pri_lvl;
	u8 queue_id1_bw_weight;
	u8 queue_id2;
	__le32 queue_id2_min_bw;
	__le32 queue_id2_max_bw;
	u8 queue_id2_tsa_assign;
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID2_TSA_ASSIGN_SP      (0x0UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID2_TSA_ASSIGN_ETS     (0x1UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID2_TSA_ASSIGN_RESERVED_FIRST (0x2UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID2_TSA_ASSIGN_RESERVED_LAST (0xffffUL << 0)
	u8 queue_id2_pri_lvl;
	u8 queue_id2_bw_weight;
	u8 queue_id3;
	__le32 queue_id3_min_bw;
	__le32 queue_id3_max_bw;
	u8 queue_id3_tsa_assign;
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID3_TSA_ASSIGN_SP      (0x0UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID3_TSA_ASSIGN_ETS     (0x1UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID3_TSA_ASSIGN_RESERVED_FIRST (0x2UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID3_TSA_ASSIGN_RESERVED_LAST (0xffffUL << 0)
	u8 queue_id3_pri_lvl;
	u8 queue_id3_bw_weight;
	u8 queue_id4;
	__le32 queue_id4_min_bw;
	__le32 queue_id4_max_bw;
	u8 queue_id4_tsa_assign;
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID4_TSA_ASSIGN_SP      (0x0UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID4_TSA_ASSIGN_ETS     (0x1UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID4_TSA_ASSIGN_RESERVED_FIRST (0x2UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID4_TSA_ASSIGN_RESERVED_LAST (0xffffUL << 0)
	u8 queue_id4_pri_lvl;
	u8 queue_id4_bw_weight;
	u8 queue_id5;
	__le32 queue_id5_min_bw;
	__le32 queue_id5_max_bw;
	u8 queue_id5_tsa_assign;
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID5_TSA_ASSIGN_SP      (0x0UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID5_TSA_ASSIGN_ETS     (0x1UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID5_TSA_ASSIGN_RESERVED_FIRST (0x2UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID5_TSA_ASSIGN_RESERVED_LAST (0xffffUL << 0)
	u8 queue_id5_pri_lvl;
	u8 queue_id5_bw_weight;
	u8 queue_id6;
	__le32 queue_id6_min_bw;
	__le32 queue_id6_max_bw;
	u8 queue_id6_tsa_assign;
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID6_TSA_ASSIGN_SP      (0x0UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID6_TSA_ASSIGN_ETS     (0x1UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID6_TSA_ASSIGN_RESERVED_FIRST (0x2UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID6_TSA_ASSIGN_RESERVED_LAST (0xffffUL << 0)
	u8 queue_id6_pri_lvl;
	u8 queue_id6_bw_weight;
	u8 queue_id7;
	__le32 queue_id7_min_bw;
	__le32 queue_id7_max_bw;
	u8 queue_id7_tsa_assign;
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID7_TSA_ASSIGN_SP      (0x0UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID7_TSA_ASSIGN_ETS     (0x1UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID7_TSA_ASSIGN_RESERVED_FIRST (0x2UL << 0)
	#define QUEUE_COS2BW_CFG_REQ_QUEUE_ID7_TSA_ASSIGN_RESERVED_LAST (0xffffUL << 0)
	u8 queue_id7_pri_lvl;
	u8 queue_id7_bw_weight;
	u8 unused_1[5];
};

/* Output (16 bytes) */
struct hwrm_queue_cos2bw_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_vnic_alloc */
/* Input (24 bytes) */
struct hwrm_vnic_alloc_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define VNIC_ALLOC_REQ_FLAGS_DEFAULT			    0x1UL
	__le32 unused_0;
};

/* Output (16 bytes) */
struct hwrm_vnic_alloc_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 vnic_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_vnic_free */
/* Input (24 bytes) */
struct hwrm_vnic_free_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 vnic_id;
	__le32 unused_0;
};

/* Output (16 bytes) */
struct hwrm_vnic_free_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_vnic_cfg */
/* Input (40 bytes) */
struct hwrm_vnic_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define VNIC_CFG_REQ_FLAGS_DEFAULT			    0x1UL
	#define VNIC_CFG_REQ_FLAGS_VLAN_STRIP_MODE		    0x2UL
	#define VNIC_CFG_REQ_FLAGS_BD_STALL_MODE		    0x4UL
	#define VNIC_CFG_REQ_FLAGS_ROCE_DUAL_VNIC_MODE		    0x8UL
	#define VNIC_CFG_REQ_FLAGS_ROCE_ONLY_VNIC_MODE		    0x10UL
	__le32 enables;
	#define VNIC_CFG_REQ_ENABLES_DFLT_RING_GRP		    0x1UL
	#define VNIC_CFG_REQ_ENABLES_RSS_RULE			    0x2UL
	#define VNIC_CFG_REQ_ENABLES_COS_RULE			    0x4UL
	#define VNIC_CFG_REQ_ENABLES_LB_RULE			    0x8UL
	#define VNIC_CFG_REQ_ENABLES_MRU			    0x10UL
	__le16 vnic_id;
	__le16 dflt_ring_grp;
	__le16 rss_rule;
	__le16 cos_rule;
	__le16 lb_rule;
	__le16 mru;
	__le32 unused_0;
};

/* Output (16 bytes) */
struct hwrm_vnic_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_vnic_tpa_cfg */
/* Input (40 bytes) */
struct hwrm_vnic_tpa_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define VNIC_TPA_CFG_REQ_FLAGS_TPA			    0x1UL
	#define VNIC_TPA_CFG_REQ_FLAGS_ENCAP_TPA		    0x2UL
	#define VNIC_TPA_CFG_REQ_FLAGS_RSC_WND_UPDATE		    0x4UL
	#define VNIC_TPA_CFG_REQ_FLAGS_GRO			    0x8UL
	#define VNIC_TPA_CFG_REQ_FLAGS_AGG_WITH_ECN		    0x10UL
	#define VNIC_TPA_CFG_REQ_FLAGS_AGG_WITH_SAME_GRE_SEQ       0x20UL
	#define VNIC_TPA_CFG_REQ_FLAGS_GRO_IPID_CHECK		    0x40UL
	#define VNIC_TPA_CFG_REQ_FLAGS_GRO_TTL_CHECK		    0x80UL
	__le32 enables;
	#define VNIC_TPA_CFG_REQ_ENABLES_MAX_AGG_SEGS		    0x1UL
	#define VNIC_TPA_CFG_REQ_ENABLES_MAX_AGGS		    0x2UL
	#define VNIC_TPA_CFG_REQ_ENABLES_MAX_AGG_TIMER		    0x4UL
	#define VNIC_TPA_CFG_REQ_ENABLES_MIN_AGG_LEN		    0x8UL
	__le16 vnic_id;
	__le16 max_agg_segs;
	#define VNIC_TPA_CFG_REQ_MAX_AGG_SEGS_1		   (0x0UL << 0)
	#define VNIC_TPA_CFG_REQ_MAX_AGG_SEGS_2		   (0x1UL << 0)
	#define VNIC_TPA_CFG_REQ_MAX_AGG_SEGS_4		   (0x2UL << 0)
	#define VNIC_TPA_CFG_REQ_MAX_AGG_SEGS_8		   (0x3UL << 0)
	#define VNIC_TPA_CFG_REQ_MAX_AGG_SEGS_MAX		   (0x1fUL << 0)
	__le16 max_aggs;
	#define VNIC_TPA_CFG_REQ_MAX_AGGS_1			   (0x0UL << 0)
	#define VNIC_TPA_CFG_REQ_MAX_AGGS_2			   (0x1UL << 0)
	#define VNIC_TPA_CFG_REQ_MAX_AGGS_4			   (0x2UL << 0)
	#define VNIC_TPA_CFG_REQ_MAX_AGGS_8			   (0x3UL << 0)
	#define VNIC_TPA_CFG_REQ_MAX_AGGS_16			   (0x4UL << 0)
	#define VNIC_TPA_CFG_REQ_MAX_AGGS_MAX			   (0x7UL << 0)
	u8 unused_0;
	u8 unused_1;
	__le32 max_agg_timer;
	__le32 min_agg_len;
};

/* Output (16 bytes) */
struct hwrm_vnic_tpa_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_vnic_rss_cfg */
/* Input (48 bytes) */
struct hwrm_vnic_rss_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 hash_type;
	#define VNIC_RSS_CFG_REQ_HASH_TYPE_IPV4		    0x1UL
	#define VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV4		    0x2UL
	#define VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV4		    0x4UL
	#define VNIC_RSS_CFG_REQ_HASH_TYPE_IPV6		    0x8UL
	#define VNIC_RSS_CFG_REQ_HASH_TYPE_TCP_IPV6		    0x10UL
	#define VNIC_RSS_CFG_REQ_HASH_TYPE_UDP_IPV6		    0x20UL
	__le32 unused_0;
	__le64 ring_grp_tbl_addr;
	__le64 hash_key_tbl_addr;
	__le16 rss_ctx_idx;
	__le16 unused_1[3];
};

/* Output (16 bytes) */
struct hwrm_vnic_rss_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_vnic_plcmodes_cfg */
/* Input (40 bytes) */
struct hwrm_vnic_plcmodes_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define VNIC_PLCMODES_CFG_REQ_FLAGS_REGULAR_PLACEMENT      0x1UL
	#define VNIC_PLCMODES_CFG_REQ_FLAGS_JUMBO_PLACEMENT	    0x2UL
	#define VNIC_PLCMODES_CFG_REQ_FLAGS_HDS_IPV4		    0x4UL
	#define VNIC_PLCMODES_CFG_REQ_FLAGS_HDS_IPV6		    0x8UL
	#define VNIC_PLCMODES_CFG_REQ_FLAGS_HDS_FCOE		    0x10UL
	#define VNIC_PLCMODES_CFG_REQ_FLAGS_HDS_ROCE		    0x20UL
	__le32 enables;
	#define VNIC_PLCMODES_CFG_REQ_ENABLES_JUMBO_THRESH_VALID   0x1UL
	#define VNIC_PLCMODES_CFG_REQ_ENABLES_HDS_OFFSET_VALID     0x2UL
	#define VNIC_PLCMODES_CFG_REQ_ENABLES_HDS_THRESHOLD_VALID  0x4UL
	__le32 vnic_id;
	__le16 jumbo_thresh;
	__le16 hds_offset;
	__le16 hds_threshold;
	__le16 unused_0[3];
};

/* Output (16 bytes) */
struct hwrm_vnic_plcmodes_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_vnic_rss_cos_lb_ctx_alloc */
/* Input (16 bytes) */
struct hwrm_vnic_rss_cos_lb_ctx_alloc_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
};

/* Output (16 bytes) */
struct hwrm_vnic_rss_cos_lb_ctx_alloc_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le16 rss_cos_lb_ctx_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 unused_4;
	u8 valid;
};

/* hwrm_vnic_rss_cos_lb_ctx_free */
/* Input (24 bytes) */
struct hwrm_vnic_rss_cos_lb_ctx_free_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 rss_cos_lb_ctx_id;
	__le16 unused_0[3];
};

/* Output (16 bytes) */
struct hwrm_vnic_rss_cos_lb_ctx_free_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_ring_alloc */
/* Input (80 bytes) */
struct hwrm_ring_alloc_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 enables;
	#define RING_ALLOC_REQ_ENABLES_RESERVED1		    0x1UL
	#define RING_ALLOC_REQ_ENABLES_RESERVED2		    0x2UL
	#define RING_ALLOC_REQ_ENABLES_RESERVED3		    0x4UL
	#define RING_ALLOC_REQ_ENABLES_STAT_CTX_ID_VALID	    0x8UL
	#define RING_ALLOC_REQ_ENABLES_RESERVED4		    0x10UL
	#define RING_ALLOC_REQ_ENABLES_MAX_BW_VALID		    0x20UL
	u8 ring_type;
	#define RING_ALLOC_REQ_RING_TYPE_CMPL			   (0x0UL << 0)
	#define RING_ALLOC_REQ_RING_TYPE_TX			   (0x1UL << 0)
	#define RING_ALLOC_REQ_RING_TYPE_RX			   (0x2UL << 0)
	u8 unused_0;
	__le16 unused_1;
	__le64 page_tbl_addr;
	__le32 fbo;
	u8 page_size;
	u8 page_tbl_depth;
	u8 unused_2;
	u8 unused_3;
	__le32 length;
	__le16 logical_id;
	__le16 cmpl_ring_id;
	__le16 queue_id;
	u8 unused_4;
	u8 unused_5;
	__le32 reserved1;
	__le16 reserved2;
	u8 unused_6;
	u8 unused_7;
	__le32 reserved3;
	__le32 stat_ctx_id;
	__le32 reserved4;
	__le32 max_bw;
	u8 int_mode;
	#define RING_ALLOC_REQ_INT_MODE_LEGACY			   (0x0UL << 0)
	#define RING_ALLOC_REQ_INT_MODE_RSVD			   (0x1UL << 0)
	#define RING_ALLOC_REQ_INT_MODE_MSIX			   (0x2UL << 0)
	#define RING_ALLOC_REQ_INT_MODE_POLL			   (0x3UL << 0)
	u8 unused_8[3];
};

/* Output (16 bytes) */
struct hwrm_ring_alloc_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le16 ring_id;
	__le16 logical_ring_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_ring_free */
/* Input (24 bytes) */
struct hwrm_ring_free_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	u8 ring_type;
	#define RING_FREE_REQ_RING_TYPE_CMPL			   (0x0UL << 0)
	#define RING_FREE_REQ_RING_TYPE_TX			   (0x1UL << 0)
	#define RING_FREE_REQ_RING_TYPE_RX			   (0x2UL << 0)
	u8 unused_0;
	__le16 ring_id;
	__le32 unused_1;
};

/* Output (16 bytes) */
struct hwrm_ring_free_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_ring_cmpl_ring_qaggint_params */
/* Input (24 bytes) */
struct hwrm_ring_cmpl_ring_qaggint_params_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 ring_id;
	__le16 unused_0[3];
};

/* Output (32 bytes) */
struct hwrm_ring_cmpl_ring_qaggint_params_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le16 flags;
	#define RING_CMPL_RING_QAGGINT_PARAMS_RESP_FLAGS_TIMER_RESET 0x1UL
	#define RING_CMPL_RING_QAGGINT_PARAMS_RESP_FLAGS_RING_IDLE 0x2UL
	__le16 num_cmpl_dma_aggr;
	__le16 num_cmpl_dma_aggr_during_int;
	__le16 cmpl_aggr_dma_tmr;
	__le16 cmpl_aggr_dma_tmr_during_int;
	__le16 int_lat_tmr_min;
	__le16 int_lat_tmr_max;
	__le16 num_cmpl_aggr_int;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_ring_cmpl_ring_cfg_aggint_params */
/* Input (40 bytes) */
struct hwrm_ring_cmpl_ring_cfg_aggint_params_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 ring_id;
	__le16 flags;
	#define RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_TIMER_RESET 0x1UL
	#define RING_CMPL_RING_CFG_AGGINT_PARAMS_REQ_FLAGS_RING_IDLE 0x2UL
	__le16 num_cmpl_dma_aggr;
	__le16 num_cmpl_dma_aggr_during_int;
	__le16 cmpl_aggr_dma_tmr;
	__le16 cmpl_aggr_dma_tmr_during_int;
	__le16 int_lat_tmr_min;
	__le16 int_lat_tmr_max;
	__le16 num_cmpl_aggr_int;
	__le16 unused_0[3];
};

/* Output (16 bytes) */
struct hwrm_ring_cmpl_ring_cfg_aggint_params_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_ring_reset */
/* Input (24 bytes) */
struct hwrm_ring_reset_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	u8 ring_type;
	#define RING_RESET_REQ_RING_TYPE_CMPL			   (0x0UL << 0)
	#define RING_RESET_REQ_RING_TYPE_TX			   (0x1UL << 0)
	#define RING_RESET_REQ_RING_TYPE_RX			   (0x2UL << 0)
	u8 unused_0;
	__le16 ring_id;
	__le32 unused_1;
};

/* Output (16 bytes) */
struct hwrm_ring_reset_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_ring_grp_alloc */
/* Input (24 bytes) */
struct hwrm_ring_grp_alloc_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 cr;
	__le16 rr;
	__le16 ar;
	__le16 sc;
};

/* Output (16 bytes) */
struct hwrm_ring_grp_alloc_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 ring_group_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_ring_grp_free */
/* Input (24 bytes) */
struct hwrm_ring_grp_free_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 ring_group_id;
	__le32 unused_0;
};

/* Output (16 bytes) */
struct hwrm_ring_grp_free_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_cfa_l2_filter_alloc */
/* Input (96 bytes) */
struct hwrm_cfa_l2_filter_alloc_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define CFA_L2_FILTER_ALLOC_REQ_FLAGS_PATH		    0x1UL
	#define CFA_L2_FILTER_ALLOC_REQ_FLAGS_PATH_TX		   (0x0UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_FLAGS_PATH_RX		   (0x1UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_FLAGS_PATH_LAST    CFA_L2_FILTER_ALLOC_REQ_FLAGS_PATH_RX
	#define CFA_L2_FILTER_ALLOC_REQ_FLAGS_LOOPBACK		    0x2UL
	#define CFA_L2_FILTER_ALLOC_REQ_FLAGS_DROP		    0x4UL
	#define CFA_L2_FILTER_ALLOC_REQ_FLAGS_OUTERMOST	    0x8UL
	__le32 enables;
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_ADDR	    0x1UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_ADDR_MASK       0x2UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_OVLAN	    0x4UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_OVLAN_MASK      0x8UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_IVLAN	    0x10UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_L2_IVLAN_MASK      0x20UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_T_L2_ADDR	    0x40UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_T_L2_ADDR_MASK     0x80UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_T_L2_OVLAN	    0x100UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_T_L2_OVLAN_MASK    0x200UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_T_L2_IVLAN	    0x400UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_T_L2_IVLAN_MASK    0x800UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_SRC_TYPE	    0x1000UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_SRC_ID		    0x2000UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_TUNNEL_TYPE	    0x4000UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_DST_ID		    0x8000UL
	#define CFA_L2_FILTER_ALLOC_REQ_ENABLES_MIRROR_VNIC_ID     0x10000UL
	u8 l2_addr[6];
	u8 unused_0;
	u8 unused_1;
	u8 l2_addr_mask[6];
	__le16 l2_ovlan;
	__le16 l2_ovlan_mask;
	__le16 l2_ivlan;
	__le16 l2_ivlan_mask;
	u8 unused_2;
	u8 unused_3;
	u8 t_l2_addr[6];
	u8 unused_4;
	u8 unused_5;
	u8 t_l2_addr_mask[6];
	__le16 t_l2_ovlan;
	__le16 t_l2_ovlan_mask;
	__le16 t_l2_ivlan;
	__le16 t_l2_ivlan_mask;
	u8 src_type;
	#define CFA_L2_FILTER_ALLOC_REQ_SRC_TYPE_NPORT		   (0x0UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_SRC_TYPE_PF		   (0x1UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_SRC_TYPE_VF		   (0x2UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_SRC_TYPE_VNIC		   (0x3UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_SRC_TYPE_KONG		   (0x4UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_SRC_TYPE_APE		   (0x5UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_SRC_TYPE_BONO		   (0x6UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_SRC_TYPE_TANG		   (0x7UL << 0)
	u8 unused_6;
	__le32 src_id;
	u8 tunnel_type;
	#define CFA_L2_FILTER_ALLOC_REQ_TUNNEL_TYPE_NONTUNNEL     (0x0UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_TUNNEL_TYPE_VXLAN	   (0x1UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_TUNNEL_TYPE_NVGRE	   (0x2UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_TUNNEL_TYPE_L2GRE	   (0x3UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_TUNNEL_TYPE_IPIP	   (0x4UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_TUNNEL_TYPE_GENEVE	   (0x5UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_TUNNEL_TYPE_MPLS	   (0x6UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_TUNNEL_TYPE_STT	   (0x7UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_TUNNEL_TYPE_IPGRE	   (0x8UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_TUNNEL_TYPE_ANYTUNNEL     (0xffUL << 0)
	u8 unused_7;
	__le16 dst_id;
	__le16 mirror_vnic_id;
	u8 pri_hint;
	#define CFA_L2_FILTER_ALLOC_REQ_PRI_HINT_NO_PREFER	   (0x0UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_PRI_HINT_ABOVE_FILTER     (0x1UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_PRI_HINT_BELOW_FILTER     (0x2UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_PRI_HINT_MAX		   (0x3UL << 0)
	#define CFA_L2_FILTER_ALLOC_REQ_PRI_HINT_MIN		   (0x4UL << 0)
	u8 unused_8;
	__le32 unused_9;
	__le64 l2_filter_id_hint;
};

/* Output (24 bytes) */
struct hwrm_cfa_l2_filter_alloc_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le64 l2_filter_id;
	__le32 flow_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_cfa_l2_filter_free */
/* Input (24 bytes) */
struct hwrm_cfa_l2_filter_free_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le64 l2_filter_id;
};

/* Output (16 bytes) */
struct hwrm_cfa_l2_filter_free_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_cfa_l2_filter_cfg */
/* Input (40 bytes) */
struct hwrm_cfa_l2_filter_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define CFA_L2_FILTER_CFG_REQ_FLAGS_PATH		    0x1UL
	#define CFA_L2_FILTER_CFG_REQ_FLAGS_PATH_TX		   (0x0UL << 0)
	#define CFA_L2_FILTER_CFG_REQ_FLAGS_PATH_RX		   (0x1UL << 0)
	#define CFA_L2_FILTER_CFG_REQ_FLAGS_PATH_LAST    CFA_L2_FILTER_CFG_REQ_FLAGS_PATH_RX
	#define CFA_L2_FILTER_CFG_REQ_FLAGS_DROP		    0x2UL
	__le32 enables;
	#define CFA_L2_FILTER_CFG_REQ_ENABLES_DST_ID		    0x1UL
	#define CFA_L2_FILTER_CFG_REQ_ENABLES_NEW_MIRROR_VNIC_ID   0x2UL
	__le64 l2_filter_id;
	__le32 dst_id;
	__le32 new_mirror_vnic_id;
};

/* Output (16 bytes) */
struct hwrm_cfa_l2_filter_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_cfa_l2_set_rx_mask */
/* Input (40 bytes) */
struct hwrm_cfa_l2_set_rx_mask_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 vnic_id;
	__le32 mask;
	#define CFA_L2_SET_RX_MASK_REQ_MASK_RESERVED		    0x1UL
	#define CFA_L2_SET_RX_MASK_REQ_MASK_MCAST		    0x2UL
	#define CFA_L2_SET_RX_MASK_REQ_MASK_ALL_MCAST		    0x4UL
	#define CFA_L2_SET_RX_MASK_REQ_MASK_BCAST		    0x8UL
	#define CFA_L2_SET_RX_MASK_REQ_MASK_PROMISCUOUS	    0x10UL
	#define CFA_L2_SET_RX_MASK_REQ_MASK_OUTERMOST		    0x20UL
	__le64 mc_tbl_addr;
	__le32 num_mc_entries;
	__le32 unused_0;
};

/* Output (16 bytes) */
struct hwrm_cfa_l2_set_rx_mask_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_cfa_tunnel_filter_alloc */
/* Input (88 bytes) */
struct hwrm_cfa_tunnel_filter_alloc_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_FLAGS_LOOPBACK	    0x1UL
	__le32 enables;
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_ENABLES_L2_FILTER_ID   0x1UL
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_ENABLES_L2_ADDR	    0x2UL
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_ENABLES_L2_IVLAN       0x4UL
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_ENABLES_L3_ADDR	    0x8UL
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_ENABLES_L3_ADDR_TYPE   0x10UL
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_ENABLES_T_L3_ADDR_TYPE 0x20UL
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_ENABLES_T_L3_ADDR      0x40UL
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_ENABLES_TUNNEL_TYPE    0x80UL
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_ENABLES_VNI	    0x100UL
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_ENABLES_DST_VNIC_ID    0x200UL
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_ENABLES_MIRROR_VNIC_ID 0x400UL
	__le64 l2_filter_id;
	u8 l2_addr[6];
	__le16 l2_ivlan;
	__le32 l3_addr[4];
	__le32 t_l3_addr[4];
	u8 l3_addr_type;
	u8 t_l3_addr_type;
	u8 tunnel_type;
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_TUNNEL_TYPE_NONTUNNEL (0x0UL << 0)
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_TUNNEL_TYPE_VXLAN     (0x1UL << 0)
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_TUNNEL_TYPE_NVGRE     (0x2UL << 0)
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_TUNNEL_TYPE_L2GRE     (0x3UL << 0)
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_TUNNEL_TYPE_IPIP      (0x4UL << 0)
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_TUNNEL_TYPE_GENEVE    (0x5UL << 0)
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_TUNNEL_TYPE_MPLS      (0x6UL << 0)
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_TUNNEL_TYPE_STT       (0x7UL << 0)
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_TUNNEL_TYPE_IPGRE     (0x8UL << 0)
	#define CFA_TUNNEL_FILTER_ALLOC_REQ_TUNNEL_TYPE_ANYTUNNEL (0xffUL << 0)
	u8 unused_0;
	__le32 vni;
	__le32 dst_vnic_id;
	__le32 mirror_vnic_id;
};

/* Output (24 bytes) */
struct hwrm_cfa_tunnel_filter_alloc_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le64 tunnel_filter_id;
	__le32 flow_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_cfa_tunnel_filter_free */
/* Input (24 bytes) */
struct hwrm_cfa_tunnel_filter_free_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le64 tunnel_filter_id;
};

/* Output (16 bytes) */
struct hwrm_cfa_tunnel_filter_free_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_cfa_encap_record_alloc */
/* Input (32 bytes) */
struct hwrm_cfa_encap_record_alloc_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define CFA_ENCAP_RECORD_ALLOC_REQ_FLAGS_LOOPBACK	    0x1UL
	u8 encap_type;
	#define CFA_ENCAP_RECORD_ALLOC_REQ_ENCAP_TYPE_VXLAN       (0x1UL << 0)
	#define CFA_ENCAP_RECORD_ALLOC_REQ_ENCAP_TYPE_NVGRE       (0x2UL << 0)
	#define CFA_ENCAP_RECORD_ALLOC_REQ_ENCAP_TYPE_L2GRE       (0x3UL << 0)
	#define CFA_ENCAP_RECORD_ALLOC_REQ_ENCAP_TYPE_IPIP	   (0x4UL << 0)
	#define CFA_ENCAP_RECORD_ALLOC_REQ_ENCAP_TYPE_GENEVE      (0x5UL << 0)
	#define CFA_ENCAP_RECORD_ALLOC_REQ_ENCAP_TYPE_MPLS	   (0x6UL << 0)
	#define CFA_ENCAP_RECORD_ALLOC_REQ_ENCAP_TYPE_VLAN	   (0x7UL << 0)
	#define CFA_ENCAP_RECORD_ALLOC_REQ_ENCAP_TYPE_IPGRE       (0x8UL << 0)
	u8 unused_0;
	__le16 unused_1;
	__le32 encap_data[16];
};

/* Output (16 bytes) */
struct hwrm_cfa_encap_record_alloc_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 encap_record_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_cfa_encap_record_free */
/* Input (24 bytes) */
struct hwrm_cfa_encap_record_free_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 encap_record_id;
	__le32 unused_0;
};

/* Output (16 bytes) */
struct hwrm_cfa_encap_record_free_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_cfa_ntuple_filter_alloc */
/* Input (128 bytes) */
struct hwrm_cfa_ntuple_filter_alloc_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 flags;
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_FLAGS_LOOPBACK	    0x1UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_FLAGS_DROP		    0x2UL
	__le32 enables;
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_L2_FILTER_ID   0x1UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_ETHERTYPE      0x2UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_TUNNEL_TYPE    0x4UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_MACADDR    0x8UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_IPADDR_TYPE    0x10UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_IPADDR     0x20UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_IPADDR_MASK 0x40UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_IPADDR     0x80UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_IPADDR_MASK 0x100UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_IP_PROTOCOL    0x200UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_PORT       0x400UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_SRC_PORT_MASK  0x800UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_PORT       0x1000UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_PORT_MASK  0x2000UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_PRI_HINT       0x4000UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_NTUPLE_FILTER_ID 0x8000UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_ID	    0x10000UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_MIRROR_VNIC_ID 0x20000UL
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_ENABLES_DST_MACADDR    0x40000UL
	__le64 l2_filter_id;
	u8 src_macaddr[6];
	__be16 ethertype;
	u8 ip_addr_type;
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_IP_ADDR_TYPE_UNKNOWN  (0x0UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_IP_ADDR_TYPE_IPV4     (0x4UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_IP_ADDR_TYPE_IPV6     (0x6UL << 0)
	u8 ip_protocol;
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_IP_PROTOCOL_UNKNOWN   (0x0UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_IP_PROTOCOL_UDP       (0x6UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_IP_PROTOCOL_TCP       (0x11UL << 0)
	__le16 dst_id;
	__le16 mirror_vnic_id;
	u8 tunnel_type;
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_TUNNEL_TYPE_NONTUNNEL (0x0UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_TUNNEL_TYPE_VXLAN     (0x1UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_TUNNEL_TYPE_NVGRE     (0x2UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_TUNNEL_TYPE_L2GRE     (0x3UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_TUNNEL_TYPE_IPIP      (0x4UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_TUNNEL_TYPE_GENEVE    (0x5UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_TUNNEL_TYPE_MPLS      (0x6UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_TUNNEL_TYPE_STT       (0x7UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_TUNNEL_TYPE_IPGRE     (0x8UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_TUNNEL_TYPE_ANYTUNNEL (0xffUL << 0)
	u8 pri_hint;
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_PRI_HINT_NO_PREFER    (0x0UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_PRI_HINT_ABOVE	   (0x1UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_PRI_HINT_BELOW	   (0x2UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_PRI_HINT_HIGHEST      (0x3UL << 0)
	#define CFA_NTUPLE_FILTER_ALLOC_REQ_PRI_HINT_LOWEST       (0x4UL << 0)
	__be32 src_ipaddr[4];
	__be32 src_ipaddr_mask[4];
	__be32 dst_ipaddr[4];
	__be32 dst_ipaddr_mask[4];
	__be16 src_port;
	__be16 src_port_mask;
	__be16 dst_port;
	__be16 dst_port_mask;
	__le64 ntuple_filter_id_hint;
};

/* Output (24 bytes) */
struct hwrm_cfa_ntuple_filter_alloc_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le64 ntuple_filter_id;
	__le32 flow_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_cfa_ntuple_filter_free */
/* Input (24 bytes) */
struct hwrm_cfa_ntuple_filter_free_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le64 ntuple_filter_id;
};

/* Output (16 bytes) */
struct hwrm_cfa_ntuple_filter_free_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_cfa_ntuple_filter_cfg */
/* Input (40 bytes) */
struct hwrm_cfa_ntuple_filter_cfg_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 enables;
	#define CFA_NTUPLE_FILTER_CFG_REQ_ENABLES_NEW_DST_ID       0x1UL
	#define CFA_NTUPLE_FILTER_CFG_REQ_ENABLES_NEW_MIRROR_VNIC_ID 0x2UL
	__le32 unused_0;
	__le64 ntuple_filter_id;
	__le32 new_dst_id;
	__le32 new_mirror_vnic_id;
};

/* Output (16 bytes) */
struct hwrm_cfa_ntuple_filter_cfg_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_tunnel_dst_port_query */
/* Input (24 bytes) */
struct hwrm_tunnel_dst_port_query_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	u8 tunnel_type;
	#define TUNNEL_DST_PORT_QUERY_REQ_TUNNEL_TYPE_VXLAN       (0x1UL << 0)
	#define TUNNEL_DST_PORT_QUERY_REQ_TUNNEL_TYPE_GENEVE      (0x5UL << 0)
	u8 unused_0[7];
};

/* Output (16 bytes) */
struct hwrm_tunnel_dst_port_query_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le16 tunnel_dst_port_id;
	__be16 tunnel_dst_port_val;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_tunnel_dst_port_alloc */
/* Input (24 bytes) */
struct hwrm_tunnel_dst_port_alloc_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	u8 tunnel_type;
	#define TUNNEL_DST_PORT_ALLOC_REQ_TUNNEL_TYPE_VXLAN       (0x1UL << 0)
	#define TUNNEL_DST_PORT_ALLOC_REQ_TUNNEL_TYPE_GENEVE      (0x5UL << 0)
	u8 unused_0;
	__be16 tunnel_dst_port_val;
	__le32 unused_1;
};

/* Output (16 bytes) */
struct hwrm_tunnel_dst_port_alloc_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le16 tunnel_dst_port_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 unused_4;
	u8 valid;
};

/* hwrm_tunnel_dst_port_free */
/* Input (24 bytes) */
struct hwrm_tunnel_dst_port_free_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	u8 tunnel_type;
	#define TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_VXLAN	   (0x1UL << 0)
	#define TUNNEL_DST_PORT_FREE_REQ_TUNNEL_TYPE_GENEVE       (0x5UL << 0)
	u8 unused_0;
	__le16 tunnel_dst_port_id;
	__le32 unused_1;
};

/* Output (16 bytes) */
struct hwrm_tunnel_dst_port_free_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_stat_ctx_alloc */
/* Input (32 bytes) */
struct hwrm_stat_ctx_alloc_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le64 stats_dma_addr;
	__le32 update_period_ms;
	__le32 unused_0;
};

/* Output (16 bytes) */
struct hwrm_stat_ctx_alloc_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 stat_ctx_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_stat_ctx_free */
/* Input (24 bytes) */
struct hwrm_stat_ctx_free_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 stat_ctx_id;
	__le32 unused_0;
};

/* Output (16 bytes) */
struct hwrm_stat_ctx_free_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 stat_ctx_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_stat_ctx_query */
/* Input (24 bytes) */
struct hwrm_stat_ctx_query_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 stat_ctx_id;
	__le32 unused_0;
};

/* Output (176 bytes) */
struct hwrm_stat_ctx_query_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le64 tx_ucast_pkts;
	__le64 tx_mcast_pkts;
	__le64 tx_bcast_pkts;
	__le64 tx_err_pkts;
	__le64 tx_drop_pkts;
	__le64 tx_ucast_bytes;
	__le64 tx_mcast_bytes;
	__le64 tx_bcast_bytes;
	__le64 rx_ucast_pkts;
	__le64 rx_mcast_pkts;
	__le64 rx_bcast_pkts;
	__le64 rx_err_pkts;
	__le64 rx_drop_pkts;
	__le64 rx_ucast_bytes;
	__le64 rx_mcast_bytes;
	__le64 rx_bcast_bytes;
	__le64 rx_agg_pkts;
	__le64 rx_agg_bytes;
	__le64 rx_agg_events;
	__le64 rx_agg_aborts;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_stat_ctx_clr_stats */
/* Input (24 bytes) */
struct hwrm_stat_ctx_clr_stats_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 stat_ctx_id;
	__le32 unused_0;
};

/* Output (16 bytes) */
struct hwrm_stat_ctx_clr_stats_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_fw_reset */
/* Input (24 bytes) */
struct hwrm_fw_reset_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	u8 embedded_proc_type;
	#define FW_RESET_REQ_EMBEDDED_PROC_TYPE_BOOT		   (0x0UL << 0)
	#define FW_RESET_REQ_EMBEDDED_PROC_TYPE_MGMT		   (0x1UL << 0)
	#define FW_RESET_REQ_EMBEDDED_PROC_TYPE_NETCTRL	   (0x2UL << 0)
	#define FW_RESET_REQ_EMBEDDED_PROC_TYPE_ROCE		   (0x3UL << 0)
	#define FW_RESET_REQ_EMBEDDED_PROC_TYPE_RSVD		   (0x4UL << 0)
	u8 selfrst_status;
	#define FW_RESET_REQ_SELFRST_STATUS_SELFRSTNONE	   (0x0UL << 0)
	#define FW_RESET_REQ_SELFRST_STATUS_SELFRSTASAP	   (0x1UL << 0)
	#define FW_RESET_REQ_SELFRST_STATUS_SELFRSTPCIERST	   (0x2UL << 0)
	__le16 unused_0[3];
};

/* Output (16 bytes) */
struct hwrm_fw_reset_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	u8 selfrst_status;
	#define FW_RESET_RESP_SELFRST_STATUS_SELFRSTNONE	   (0x0UL << 0)
	#define FW_RESET_RESP_SELFRST_STATUS_SELFRSTASAP	   (0x1UL << 0)
	#define FW_RESET_RESP_SELFRST_STATUS_SELFRSTPCIERST       (0x2UL << 0)
	u8 unused_0;
	__le16 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 unused_4;
	u8 valid;
};

/* hwrm_fw_qstatus */
/* Input (24 bytes) */
struct hwrm_fw_qstatus_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	u8 embedded_proc_type;
	#define FW_QSTATUS_REQ_EMBEDDED_PROC_TYPE_BOOT		   (0x0UL << 0)
	#define FW_QSTATUS_REQ_EMBEDDED_PROC_TYPE_MGMT		   (0x1UL << 0)
	#define FW_QSTATUS_REQ_EMBEDDED_PROC_TYPE_NETCTRL	   (0x2UL << 0)
	#define FW_QSTATUS_REQ_EMBEDDED_PROC_TYPE_ROCE		   (0x3UL << 0)
	#define FW_QSTATUS_REQ_EMBEDDED_PROC_TYPE_RSVD		   (0x4UL << 0)
	u8 unused_0[7];
};

/* Output (16 bytes) */
struct hwrm_fw_qstatus_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	u8 selfrst_status;
	#define FW_QSTATUS_RESP_SELFRST_STATUS_SELFRSTNONE	   (0x0UL << 0)
	#define FW_QSTATUS_RESP_SELFRST_STATUS_SELFRSTASAP	   (0x1UL << 0)
	#define FW_QSTATUS_RESP_SELFRST_STATUS_SELFRSTPCIERST     (0x2UL << 0)
	u8 unused_0;
	__le16 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 unused_4;
	u8 valid;
};

/* hwrm_exec_fwd_resp */
/* Input (128 bytes) */
struct hwrm_exec_fwd_resp_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 encap_request[26];
	__le16 encap_resp_target_id;
	__le16 unused_0[3];
};

/* Output (16 bytes) */
struct hwrm_exec_fwd_resp_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_reject_fwd_resp */
/* Input (128 bytes) */
struct hwrm_reject_fwd_resp_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 encap_request[26];
	__le16 encap_resp_target_id;
	__le16 unused_0[3];
};

/* Output (16 bytes) */
struct hwrm_reject_fwd_resp_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_fwd_resp */
/* Input (40 bytes) */
struct hwrm_fwd_resp_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 encap_resp_target_id;
	__le16 encap_resp_cmpl_ring;
	__le16 encap_resp_len;
	u8 unused_0;
	u8 unused_1;
	__le64 encap_resp_addr;
	__le32 encap_resp[24];
};

/* Output (16 bytes) */
struct hwrm_fwd_resp_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_fwd_async_event_cmpl */
/* Input (32 bytes) */
struct hwrm_fwd_async_event_cmpl_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 encap_async_event_target_id;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2[3];
	u8 unused_3;
	__le32 encap_async_event_cmpl[4];
};

/* Output (16 bytes) */
struct hwrm_fwd_async_event_cmpl_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_temp_monitor_query */
/* Input (16 bytes) */
struct hwrm_temp_monitor_query_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
};

/* Output (16 bytes) */
struct hwrm_temp_monitor_query_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	u8 temp;
	u8 unused_0;
	__le16 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 unused_4;
	u8 valid;
};

/* hwrm_nvm_raw_write_blk */
/* Input (32 bytes) */
struct hwrm_nvm_raw_write_blk_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le64 host_src_addr;
	__le32 dest_addr;
	__le32 len;
};

/* Output (16 bytes) */
struct hwrm_nvm_raw_write_blk_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_nvm_read */
/* Input (40 bytes) */
struct hwrm_nvm_read_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le64 host_dest_addr;
	__le16 dir_idx;
	u8 unused_0;
	u8 unused_1;
	__le32 offset;
	__le32 len;
	__le32 unused_2;
};

/* Output (16 bytes) */
struct hwrm_nvm_read_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_nvm_raw_dump */
/* Input (32 bytes) */
struct hwrm_nvm_raw_dump_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le64 host_dest_addr;
	__le32 offset;
	__le32 len;
};

/* Output (16 bytes) */
struct hwrm_nvm_raw_dump_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_nvm_get_dir_entries */
/* Input (24 bytes) */
struct hwrm_nvm_get_dir_entries_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le64 host_dest_addr;
};

/* Output (16 bytes) */
struct hwrm_nvm_get_dir_entries_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_nvm_get_dir_info */
/* Input (16 bytes) */
struct hwrm_nvm_get_dir_info_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
};

/* Output (24 bytes) */
struct hwrm_nvm_get_dir_info_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 entries;
	__le32 entry_length;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_nvm_write */
/* Input (48 bytes) */
struct hwrm_nvm_write_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le64 host_src_addr;
	__le16 dir_type;
	__le16 dir_ordinal;
	__le16 dir_ext;
	__le16 dir_attr;
	__le32 dir_data_length;
	__le16 option;
	__le16 flags;
	#define NVM_WRITE_REQ_FLAGS_KEEP_ORIG_ACTIVE_IMG	    0x1UL
	__le32 dir_item_length;
	__le32 unused_0;
};

/* Output (16 bytes) */
struct hwrm_nvm_write_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 dir_item_length;
	__le16 dir_idx;
	u8 unused_0;
	u8 valid;
};

/* hwrm_nvm_modify */
/* Input (40 bytes) */
struct hwrm_nvm_modify_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le64 host_src_addr;
	__le16 dir_idx;
	u8 unused_0;
	u8 unused_1;
	__le32 offset;
	__le32 len;
	__le32 unused_2;
};

/* Output (16 bytes) */
struct hwrm_nvm_modify_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_nvm_find_dir_entry */
/* Input (32 bytes) */
struct hwrm_nvm_find_dir_entry_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 enables;
	#define NVM_FIND_DIR_ENTRY_REQ_ENABLES_DIR_IDX_VALID       0x1UL
	__le16 dir_idx;
	__le16 dir_type;
	__le16 dir_ordinal;
	__le16 dir_ext;
	u8 opt_ordinal;
	#define NVM_FIND_DIR_ENTRY_REQ_OPT_ORDINAL_MASK	    0x3UL
	#define NVM_FIND_DIR_ENTRY_REQ_OPT_ORDINAL_SFT		    0
	#define NVM_FIND_DIR_ENTRY_REQ_OPT_ORDINAL_EQ		   (0x0UL << 0)
	#define NVM_FIND_DIR_ENTRY_REQ_OPT_ORDINAL_GE		   (0x1UL << 0)
	#define NVM_FIND_DIR_ENTRY_REQ_OPT_ORDINAL_GT		   (0x2UL << 0)
	u8 unused_1[3];
};

/* Output (32 bytes) */
struct hwrm_nvm_find_dir_entry_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 dir_item_length;
	__le32 dir_data_length;
	__le32 fw_ver;
	__le16 dir_ordinal;
	__le16 dir_idx;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_nvm_erase_dir_entry */
/* Input (24 bytes) */
struct hwrm_nvm_erase_dir_entry_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 dir_idx;
	__le16 unused_0[3];
};

/* Output (16 bytes) */
struct hwrm_nvm_erase_dir_entry_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_nvm_get_dev_info */
/* Input (16 bytes) */
struct hwrm_nvm_get_dev_info_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
};

/* Output (32 bytes) */
struct hwrm_nvm_get_dev_info_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le16 manufacturer_id;
	__le16 device_id;
	__le32 sector_size;
	__le32 nvram_size;
	__le32 reserved_size;
	__le32 available_size;
	u8 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 valid;
};

/* hwrm_nvm_mod_dir_entry */
/* Input (32 bytes) */
struct hwrm_nvm_mod_dir_entry_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le32 enables;
	#define NVM_MOD_DIR_ENTRY_REQ_ENABLES_CHECKSUM		    0x1UL
	__le16 dir_idx;
	__le16 dir_ordinal;
	__le16 dir_ext;
	__le16 dir_attr;
	__le32 checksum;
};

/* Output (16 bytes) */
struct hwrm_nvm_mod_dir_entry_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

/* hwrm_nvm_verify_update */
/* Input (24 bytes) */
struct hwrm_nvm_verify_update_input {
	__le16 req_type;
	__le16 cmpl_ring;
	__le16 seq_id;
	__le16 target_id;
	__le64 resp_addr;
	__le16 dir_type;
	__le16 dir_ordinal;
	__le16 dir_ext;
	__le16 unused_0;
};

/* Output (16 bytes) */
struct hwrm_nvm_verify_update_output {
	__le16 error_code;
	__le16 req_type;
	__le16 seq_id;
	__le16 resp_len;
	__le32 unused_0;
	u8 unused_1;
	u8 unused_2;
	u8 unused_3;
	u8 valid;
};

#endif
