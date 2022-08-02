/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-21 Intel Corporation.
 */

#ifndef IOSM_IPC_MUX_H
#define IOSM_IPC_MUX_H

#include "iosm_ipc_protocol.h"

#define IPC_MEM_MAX_UL_DG_ENTRIES	100
#define IPC_MEM_MAX_TDS_MUX_AGGR_UL	60

#define IPC_MEM_MAX_ADB_BUF_SIZE (16 * 1024)
#define IPC_MEM_MAX_UL_ADB_BUF_SIZE IPC_MEM_MAX_ADB_BUF_SIZE
#define IPC_MEM_MAX_DL_ADB_BUF_SIZE IPC_MEM_MAX_ADB_BUF_SIZE

/* Size of the buffer for the IP MUX Lite data buffer. */
#define IPC_MEM_MAX_DL_MUX_LITE_BUF_SIZE (2 * 1024)

/* TD counts for IP MUX Lite */
#define IPC_MEM_MAX_TDS_MUX_LITE_UL 800
#define IPC_MEM_MAX_TDS_MUX_LITE_DL 1200

/* open session request (AP->CP) */
#define MUX_CMD_OPEN_SESSION 1

/* response to open session request (CP->AP) */
#define MUX_CMD_OPEN_SESSION_RESP 2

/* close session request (AP->CP) */
#define MUX_CMD_CLOSE_SESSION 3

/* response to close session request (CP->AP) */
#define MUX_CMD_CLOSE_SESSION_RESP 4

/* Flow control command with mask of the flow per queue/flow. */
#define MUX_LITE_CMD_FLOW_CTL 5

/* ACK the flow control command. Shall have the same Transaction ID as the
 * matching FLOW_CTL command.
 */
#define MUX_LITE_CMD_FLOW_CTL_ACK 6

/* Command for report packet indicating link quality metrics. */
#define MUX_LITE_CMD_LINK_STATUS_REPORT 7

/* Response to a report packet */
#define MUX_LITE_CMD_LINK_STATUS_REPORT_RESP 8

/* Used to reset a command/response state. */
#define MUX_CMD_INVALID 255

/* command response : command processed successfully */
#define MUX_CMD_RESP_SUCCESS 0

/* MUX for route link devices */
#define IPC_MEM_WWAN_MUX BIT(0)

/* Initiated actions to change the state of the MUX object. */
enum mux_event {
	MUX_E_INACTIVE, /* No initiated actions. */
	MUX_E_MUX_SESSION_OPEN, /* Create the MUX channel and a session. */
	MUX_E_MUX_SESSION_CLOSE, /* Release a session. */
	MUX_E_MUX_CHANNEL_CLOSE, /* Release the MUX channel. */
	MUX_E_NO_ORDERS, /* No MUX order. */
	MUX_E_NOT_APPLICABLE, /* Defect IP MUX. */
};

/* MUX session open command. */
struct mux_session_open {
	enum mux_event event;
	__le32 if_id;
};

/* MUX session close command. */
struct mux_session_close {
	enum mux_event event;
	__le32 if_id;
};

/* MUX channel close command. */
struct mux_channel_close {
	enum mux_event event;
};

/* Default message type to find out the right message type. */
struct mux_common {
	enum mux_event event;
};

/* List of ops in MUX mode. */
union mux_msg {
	struct mux_session_open session_open;
	struct mux_session_close session_close;
	struct mux_channel_close channel_close;
	struct mux_common common;
};

/* Parameter definition of the open session command. */
struct mux_cmd_open_session {
	u8 flow_ctrl; /* 0: Flow control disabled (flow allowed). */
	/* 1: Flow control enabled (flow not allowed)*/
	u8 ipv4v6_hints; /* 0: IPv4/IPv6 hints not supported.*/
	/* 1: IPv4/IPv6 hints supported*/
	__le16 reserved2; /* Reserved. Set to zero. */
	__le32 dl_head_pad_len; /* Maximum length supported */
	/* for DL head padding on a datagram. */
};

/* Parameter definition of the open session response. */
struct mux_cmd_open_session_resp {
	__le32 response; /* Response code */
	u8 flow_ctrl; /* 0: Flow control disabled (flow allowed). */
	/* 1: Flow control enabled (flow not allowed) */
	u8 ipv4v6_hints; /* 0: IPv4/IPv6 hints not supported */
	/* 1: IPv4/IPv6 hints supported */
	__le16 reserved2; /* Reserved. Set to zero. */
	__le32 ul_head_pad_len; /* Actual length supported for */
	/* UL head padding on adatagram.*/
};

/* Parameter definition of the close session response code */
struct mux_cmd_close_session_resp {
	__le32 response;
};

/* Parameter definition of the flow control command. */
struct mux_cmd_flow_ctl {
	__le32 mask; /* indicating the desired flow control */
	/* state for various flows/queues */
};

/* Parameter definition of the link status report code*/
struct mux_cmd_link_status_report {
	u8 payload;
};

/* Parameter definition of the link status report response code. */
struct mux_cmd_link_status_report_resp {
	__le32 response;
};

/**
 * union mux_cmd_param - Union-definition of the command parameters.
 * @open_session:	Inband command for open session
 * @open_session_resp:	Inband command for open session response
 * @close_session_resp:	Inband command for close session response
 * @flow_ctl:		In-band flow control on the opened interfaces
 * @link_status:	In-band Link Status Report
 * @link_status_resp:	In-band command for link status report response
 */
union mux_cmd_param {
	struct mux_cmd_open_session open_session;
	struct mux_cmd_open_session_resp open_session_resp;
	struct mux_cmd_close_session_resp close_session_resp;
	struct mux_cmd_flow_ctl flow_ctl;
	struct mux_cmd_link_status_report link_status;
	struct mux_cmd_link_status_report_resp link_status_resp;
};

/* States of the MUX object.. */
enum mux_state {
	MUX_S_INACTIVE, /* IP MUX is unused. */
	MUX_S_ACTIVE, /* IP MUX channel is available. */
	MUX_S_ERROR, /* Defect IP MUX. */
};

/* Supported MUX protocols. */
enum ipc_mux_protocol {
	MUX_UNKNOWN,
	MUX_LITE,
	MUX_AGGREGATION,
};

/* Supported UL data transfer methods. */
enum ipc_mux_ul_flow {
	MUX_UL_UNKNOWN,
	MUX_UL, /* Normal UL data transfer */
	MUX_UL_ON_CREDITS, /* UL data transfer will be based on credits */
};

/* List of the MUX session. */
struct mux_session {
	struct iosm_wwan *wwan; /*Network i/f used for communication*/
	int if_id; /* i/f id for session open message.*/
	u32 flags;
	u32 ul_head_pad_len; /* Nr of bytes for UL head padding. */
	u32 dl_head_pad_len; /* Nr of bytes for DL head padding. */
	struct sk_buff_head ul_list; /* skb entries for an ADT. */
	u32 flow_ctl_mask; /* UL flow control */
	u32 flow_ctl_en_cnt; /* Flow control Enable cmd count */
	u32 flow_ctl_dis_cnt; /* Flow Control Disable cmd count */
	int ul_flow_credits; /* UL flow credits */
	u8 net_tx_stop:1,
	   flush:1; /* flush net interface ? */
};

/**
 * struct mux_adth_dg - Structure of the datagram in the Aggregated Datagram
 *			Table Header.
 * @datagram_index :	Index (in bytes) to the k-th datagram in the table.
 *			Index shall count from the start of the block including
 *			the 16-byte header. This value shall be non-zero.
 * @datagram_length:	Length of the k-th datagram including the head padding.
 *			This value shall be non-zero.
 * @service_class:	Service class identifier for the datagram.
 * @reserved:		Reserved bytes. Set to zero
 */
struct mux_adth_dg {
	__le32 datagram_index;
	__le16 datagram_length;
	u8 service_class;
	u8 reserved;
};

/**
 * struct mux_qlth_ql - Structure of the queue level in the Aggregated
 *			Datagram Queue Level Table Header.
 * @nr_of_bytes:	Number of bytes available to transmit in the queue.
 */
struct mux_qlth_ql {
	__le32 nr_of_bytes;
};

/**
 * struct mux_qlth -    Structure of Aggregated Datagram Queue Level Table
 *			Header.
 * @signature:          Signature of the Queue Level Table Header
 *                      Value: 0x48544C51 (ASCII characters: 'Q' 'L' 'T' 'H')
 * @table_length:       Length (in bytes) of the datagram table. This length
 *                      shall include the queue level table header size.
 *                      Minimum value:0x10
 * @if_id:              ID of the interface the queue levels in the table
 *                      belong to.
 * @reserved:           Reserved byte. Set to zero.
 * @next_table_index:   Index (in bytes) to the next table in the buffer. Index
 *                      shall count from the start of the block including the
 *                      16-byte header. Value of zero indicates end of the list.
 * @reserved2:          Reserved bytes. Set to zero
 * @ql:                 Queue level table with variable length
 */
struct mux_qlth {
	__le32 signature;
	__le16 table_length;
	u8 if_id;
	u8 reserved;
	__le32 next_table_index;
	__le32 reserved2;
	struct mux_qlth_ql ql;
};

/**
 * struct mux_adb - Structure of State of a single UL data block.
 * @dest_skb:		Current UL skb for the data block.
 * @buf:		ADB memory
 * @adgh:		ADGH pointer
 * @qlth_skb:		QLTH pointer
 * @next_table_index:	Pointer to next table index.
 * @free_list:		List of alloc. ADB for the UL sess.
 * @size:		Size of the ADB memory.
 * @if_cnt:		Statistic counter
 * @dg_cnt_total:	Datagram count total
 * @payload_size:	Payload Size
 * @dg:			Datagram table.
 * @pp_qlt:		Pointers to hold Queue Level Tables of session
 * @adbh:		ADBH pointer
 * @qlt_updated:	Queue level table updated
 * @dg_count:		Datagram count
 */
struct mux_adb {
	struct sk_buff *dest_skb;
	u8 *buf;
	struct mux_adgh *adgh;
	struct sk_buff *qlth_skb;
	u32 *next_table_index;
	struct sk_buff_head free_list;
	int size;
	u32 if_cnt;
	u32 dg_cnt_total;
	u32 payload_size;
	struct mux_adth_dg
		dg[IPC_MEM_MUX_IP_SESSION_ENTRIES][IPC_MEM_MAX_UL_DG_ENTRIES];
	struct mux_qlth *pp_qlt[IPC_MEM_MUX_IP_SESSION_ENTRIES];
	struct mux_adbh *adbh;
	u32 qlt_updated[IPC_MEM_MUX_IP_SESSION_ENTRIES];
	u32 dg_count[IPC_MEM_MUX_IP_SESSION_ENTRIES];
};

/**
 * struct mux_acb - Structure of Temporary ACB state.
 * @skb:		Used UL skb.
 * @if_id:		Session id.
 * @buf_p:		Command buffer.
 * @wanted_response:	Wanted Response
 * @got_response:	Got response
 * @cmd:		command
 * @got_param:		Received command/response parameter
 */
struct mux_acb {
	struct sk_buff *skb; /* Used UL skb. */
	int if_id; /* Session id. */
	u8 *buf_p;
	u32 wanted_response;
	u32 got_response;
	u32 cmd;
	union mux_cmd_param got_param; /* Received command/response parameter */
};

/**
 * struct iosm_mux - Structure of the data multiplexing over an IP channel.
 * @dev:		Pointer to device structure
 * @session:		Array of the MUX sessions.
 * @channel:		Reference to the IP MUX channel
 * @pcie:		Pointer to iosm_pcie struct
 * @imem:		Pointer to iosm_imem
 * @wwan:		Poinetr to iosm_wwan
 * @ipc_protocol:	Pointer to iosm_protocol
 * @channel_id:		Channel ID for MUX
 * @protocol:		Type of the MUX protocol
 * @ul_flow:		UL Flow type
 * @nr_sessions:	Number of sessions
 * @instance_id:	Instance ID
 * @state:		States of the MUX object
 * @event:		Initiated actions to change the state of the MUX object
 * @tx_transaction_id:	Transaction id for the ACB command.
 * @rr_next_session:	Next session number for round robin.
 * @ul_adb:		State of the UL ADB/ADGH.
 * @size_needed:	Variable to store the size needed during ADB preparation
 * @ul_data_pend_bytes:	Pending UL data to be processed in bytes
 * @acb:		Temporary ACB state
 * @wwan_q_offset:	This will hold the offset of the given instance
 *			Useful while passing or receiving packets from
 *			wwan/imem layer.
 * @adb_finish_timer:	Timer for forcefully finishing the ADB
 * @acb_tx_sequence_nr: Sequence number for the ACB header.
 * @params:		user configurable parameters
 * @adb_tx_sequence_nr: Sequence number for ADB header
 * @acc_adb_size:       Statistic data for logging
 * @acc_payload_size:   Statistic data for logging
 * @initialized:	MUX object is initialized
 * @ev_mux_net_transmit_pending:
 *			0 means inform the IPC tasklet to pass the
 *			accumulated uplink ADB to CP.
 * @adb_prep_ongoing:	Flag for ADB preparation status
 */
struct iosm_mux {
	struct device *dev;
	struct mux_session session[IPC_MEM_MUX_IP_SESSION_ENTRIES];
	struct ipc_mem_channel *channel;
	struct iosm_pcie *pcie;
	struct iosm_imem *imem;
	struct iosm_wwan *wwan;
	struct iosm_protocol *ipc_protocol;
	int channel_id;
	enum ipc_mux_protocol protocol;
	enum ipc_mux_ul_flow ul_flow;
	int nr_sessions;
	int instance_id;
	enum mux_state state;
	enum mux_event event;
	u32 tx_transaction_id;
	int rr_next_session;
	struct mux_adb ul_adb;
	int size_needed;
	long long ul_data_pend_bytes;
	struct mux_acb acb;
	int wwan_q_offset;
	struct hrtimer adb_finish_timer;
	u16 acb_tx_sequence_nr;
	struct ipc_params *params;
	u16 adb_tx_sequence_nr;
	unsigned long long acc_adb_size;
	unsigned long long acc_payload_size;
	u8 initialized:1,
	   ev_mux_net_transmit_pending:1,
	   adb_prep_ongoing;
} __packed;

/* MUX configuration structure */
struct ipc_mux_config {
	enum ipc_mux_protocol protocol;
	enum ipc_mux_ul_flow ul_flow;
	int instance_id;
};

/**
 * ipc_mux_init - Allocates and Init MUX instance
 * @mux_cfg:	Pointer to MUX configuration structure
 * @ipc_imem:	Pointer to imem data-struct
 *
 * Returns: Initialized mux pointer on success else NULL
 */
struct iosm_mux *ipc_mux_init(struct ipc_mux_config *mux_cfg,
			      struct iosm_imem *ipc_imem);

/**
 * ipc_mux_deinit - Deallocates MUX instance
 * @ipc_mux:	Pointer to the MUX instance.
 */
void ipc_mux_deinit(struct iosm_mux *ipc_mux);

/**
 * ipc_mux_check_n_restart_tx - Checks for pending UL date bytes and then
 *				it restarts the net interface tx queue if
 *				device has set flow control as off.
 * @ipc_mux:	Pointer to MUX data-struct
 */
void ipc_mux_check_n_restart_tx(struct iosm_mux *ipc_mux);

/**
 * ipc_mux_get_active_protocol - Returns the active MUX protocol type.
 * @ipc_mux:	Pointer to MUX data-struct
 *
 * Returns: enum of type ipc_mux_protocol
 */
enum ipc_mux_protocol ipc_mux_get_active_protocol(struct iosm_mux *ipc_mux);

/**
 * ipc_mux_open_session - Opens a MUX session for IP traffic.
 * @ipc_mux:	Pointer to MUX data-struct
 * @session_nr:	Interface ID or session number
 *
 * Returns: channel id on success, failure value on error
 */
int ipc_mux_open_session(struct iosm_mux *ipc_mux, int session_nr);

/**
 * ipc_mux_close_session - Closes a MUX session.
 * @ipc_mux:	Pointer to MUX data-struct
 * @session_nr:	Interface ID or session number
 *
 * Returns: channel id on success, failure value on error
 */
int ipc_mux_close_session(struct iosm_mux *ipc_mux, int session_nr);

/**
 * ipc_mux_get_max_sessions - Retuns the maximum sessions supported on the
 *			      provided MUX instance..
 * @ipc_mux:	Pointer to MUX data-struct
 *
 * Returns: Number of sessions supported on Success and failure value on error
 */
int ipc_mux_get_max_sessions(struct iosm_mux *ipc_mux);
#endif
