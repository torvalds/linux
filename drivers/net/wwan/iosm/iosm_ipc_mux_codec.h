/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-21 Intel Corporation.
 */

#ifndef IOSM_IPC_MUX_CODEC_H
#define IOSM_IPC_MUX_CODEC_H

#include "iosm_ipc_mux.h"

/* Queue level size and reporting
 * >1 is enable, 0 is disable
 */
#define MUX_QUEUE_LEVEL 1

/* ADB finish timer value */
#define IOSM_AGGR_MUX_ADB_FINISH_TIMEOUT_NSEC (500 * 1000)

/* Enables the flow control (Flow is not allowed) */
#define IOSM_AGGR_MUX_CMD_FLOW_CTL_ENABLE 5

/* Disables the flow control (Flow is allowed) */
#define IOSM_AGGR_MUX_CMD_FLOW_CTL_DISABLE 6

/* ACK the flow control command. Shall have the same Transaction ID as the
 * matching FLOW_CTL command
 */
#define IOSM_AGGR_MUX_CMD_FLOW_CTL_ACK 7

/* Aggregation Protocol Command for report packet indicating link quality
 */
#define IOSM_AGGR_MUX_CMD_LINK_STATUS_REPORT 8

/* Response to a report packet */
#define IOSM_AGGR_MUX_CMD_LINK_STATUS_REPORT_RESP 9

/* ACBH: Signature of the Aggregated Command Block Header. */
#define IOSM_AGGR_MUX_SIG_ACBH 0x48424341

/* ADTH: Signature of the Aggregated Datagram Table Header. */
#define IOSM_AGGR_MUX_SIG_ADTH 0x48544441

/* ADBH: Signature of the Aggregated Data Block Header. */
#define IOSM_AGGR_MUX_SIG_ADBH 0x48424441

/* ADGH: Signature of the Datagram Header. */
#define IOSM_AGGR_MUX_SIG_ADGH 0x48474441

/* Size of the buffer for the IP MUX commands. */
#define MUX_MAX_UL_ACB_BUF_SIZE 256

/* Maximum number of packets in a go per session */
#define MUX_MAX_UL_DG_ENTRIES 100

/* ADGH: Signature of the Datagram Header. */
#define MUX_SIG_ADGH 0x48474441

/* CMDH: Signature of the Command Header. */
#define MUX_SIG_CMDH 0x48444D43

/* QLTH: Signature of the Queue Level Table */
#define MUX_SIG_QLTH 0x48544C51

/* FCTH: Signature of the Flow Credit Table */
#define MUX_SIG_FCTH 0x48544346

/* MUX UL session threshold factor */
#define IPC_MEM_MUX_UL_SESS_FCOFF_THRESHOLD_FACTOR (4)

/* Size of the buffer for the IP MUX Lite data buffer. */
#define IPC_MEM_MAX_DL_MUX_LITE_BUF_SIZE (2 * 1024)

/* MUX UL session threshold in number of packets */
#define IPC_MEM_MUX_UL_SESS_FCON_THRESHOLD (64)

/* Default time out for sending IPC session commands like
 * open session, close session etc
 * unit : milliseconds
 */
#define IPC_MUX_CMD_RUN_DEFAULT_TIMEOUT 1000 /* 1 second */

/* MUX UL flow control lower threshold in bytes */
#define IPC_MEM_MUX_UL_FLOWCTRL_LOW_B 10240 /* 10KB */

/* MUX UL flow control higher threshold in bytes (5ms worth of data)*/
#define IPC_MEM_MUX_UL_FLOWCTRL_HIGH_B (110 * 1024)

/**
 * struct mux_cmdh - Structure of Command Header.
 * @signature:		Signature of the Command Header.
 * @cmd_len:		Length (in bytes) of the Aggregated Command Block.
 * @if_id:		ID of the interface the commands in the table belong to.
 * @reserved:		Reserved. Set to zero.
 * @next_cmd_index:	Index (in bytes) to the next command in the buffer.
 * @command_type:	Command Enum. See table Session Management chapter for
 *			details.
 * @transaction_id:	The Transaction ID shall be unique to the command
 * @param:		Optional parameters used with the command.
 */
struct mux_cmdh {
	__le32 signature;
	__le16 cmd_len;
	u8 if_id;
	u8 reserved;
	__le32 next_cmd_index;
	__le32 command_type;
	__le32 transaction_id;
	union mux_cmd_param param;
};

/**
 * struct mux_acbh -    Structure of the Aggregated Command Block Header.
 * @signature:          Signature of the Aggregated Command Block Header.
 * @reserved:           Reserved bytes. Set to zero.
 * @sequence_nr:        Block sequence number.
 * @block_length:       Length (in bytes) of the Aggregated Command Block.
 * @first_cmd_index:    Index (in bytes) to the first command in the buffer.
 */
struct mux_acbh {
	__le32 signature;
	__le16 reserved;
	__le16 sequence_nr;
	__le32 block_length;
	__le32 first_cmd_index;
};

/**
 * struct mux_adbh - Structure of the Aggregated Data Block Header.
 * @signature:		Signature of the Aggregated Data Block Header.
 * @reserved:		Reserved bytes. Set to zero.
 * @sequence_nr:	Block sequence number.
 * @block_length:	Length (in bytes) of the Aggregated Data Block.
 * @first_table_index:	Index (in bytes) to the first Datagram Table in
 *			the buffer.
 */
struct mux_adbh {
	__le32 signature;
	__le16 reserved;
	__le16 sequence_nr;
	__le32 block_length;
	__le32 first_table_index;
};

/**
 * struct mux_adth - Structure of the Aggregated Datagram Table Header.
 * @signature:          Signature of the Aggregated Datagram Table Header.
 * @table_length:       Length (in bytes) of the datagram table.
 * @if_id:              ID of the interface the datagrams in the table
 *                      belong to.
 * @opt_ipv4v6:         Indicates IPv4(=0)/IPv6(=1) hint.
 * @reserved:           Reserved bits. Set to zero.
 * @next_table_index:   Index (in bytes) to the next Datagram Table in
 *                      the buffer.
 * @reserved2:          Reserved bytes. Set to zero
 * @dg:                 datagramm table with variable length
 */
struct mux_adth {
	__le32 signature;
	__le16 table_length;
	u8 if_id;
	u8 opt_ipv4v6;
	__le32 next_table_index;
	__le32 reserved2;
	struct mux_adth_dg dg[];
};

/**
 * struct mux_adgh - Aggregated Datagram Header.
 * @signature:		Signature of the Aggregated Datagram Header(0x48474441)
 * @length:		Length (in bytes) of the datagram header. This length
 *			shall include the header size. Min value: 0x10
 * @if_id:		ID of the interface the datagrams belong to
 * @opt_ipv4v6:		Indicates IPv4(=0)/IPv6(=1), It is optional if not
 *			used set it to zero.
 * @reserved:		Reserved bits. Set to zero.
 * @service_class:	Service class identifier for the datagram.
 * @next_count:		Count of the datagrams that shall be following this
 *			datagrams for this interface. A count of zero means
 *			the next datagram may not belong to this interface.
 * @reserved1:		Reserved bytes, Set to zero
 */
struct mux_adgh {
	__le32 signature;
	__le16 length;
	u8 if_id;
	u8 opt_ipv4v6;
	u8 service_class;
	u8 next_count;
	u8 reserved1[6];
};

/**
 * struct mux_lite_cmdh - MUX Lite Command Header
 * @signature:		Signature of the Command Header(0x48444D43)
 * @cmd_len:		Length (in bytes) of the command. This length shall
 *			include the header size. Minimum value: 0x10
 * @if_id:		ID of the interface the commands in the table belong to.
 * @reserved:		Reserved Set to zero.
 * @command_type:	Command Enum.
 * @transaction_id:	4 byte value shall be generated and sent along with a
 *			command Responses and ACKs shall have the same
 *			Transaction ID as their commands. It shall be unique to
 *			the command transaction on the given interface.
 * @param:		Optional parameters used with the command.
 */
struct mux_lite_cmdh {
	__le32 signature;
	__le16 cmd_len;
	u8 if_id;
	u8 reserved;
	__le32 command_type;
	__le32 transaction_id;
	union mux_cmd_param param;
};

/**
 * struct mux_lite_vfl - value field in generic table
 * @nr_of_bytes:	Number of bytes available to transmit in the queue.
 */
struct mux_lite_vfl {
	__le32 nr_of_bytes;
};

/**
 * struct ipc_mem_lite_gen_tbl - Generic table format for Queue Level
 *				 and Flow Credit
 * @signature:	Signature of the table
 * @length:	Length of the table
 * @if_id:	ID of the interface the table belongs to
 * @vfl_length:	Value field length
 * @reserved:	Reserved
 * @vfl:	Value field of variable length
 */
struct ipc_mem_lite_gen_tbl {
	__le32 signature;
	__le16 length;
	u8 if_id;
	u8 vfl_length;
	u32 reserved[2];
	struct mux_lite_vfl vfl;
};

/**
 * struct mux_type_cmdh - Structure of command header for mux lite and aggr
 * @ack_lite:	MUX Lite Command Header pointer
 * @ack_aggr:	Command Header pointer
 */
union mux_type_cmdh {
	struct mux_lite_cmdh *ack_lite;
	struct mux_cmdh *ack_aggr;
};

/**
 * struct mux_type_header - Structure of mux header type
 * @adgh:	Aggregated Datagram Header pointer
 * @adbh:	Aggregated Data Block Header pointer
 */
union mux_type_header {
	struct mux_adgh *adgh;
	struct mux_adbh *adbh;
};

void ipc_mux_dl_decode(struct iosm_mux *ipc_mux, struct sk_buff *skb);

/**
 * ipc_mux_dl_acb_send_cmds - Respond to the Command blocks.
 * @ipc_mux:		Pointer to MUX data-struct
 * @cmd_type:		Command
 * @if_id:		Session interface id.
 * @transaction_id:	Command transaction id.
 * @param:		Pointer to command params.
 * @res_size:		Response size
 * @blocking:		True for blocking send
 * @respond:		If true return transaction ID
 *
 * Returns:		0 in success and failure value on error
 */
int ipc_mux_dl_acb_send_cmds(struct iosm_mux *ipc_mux, u32 cmd_type, u8 if_id,
			     u32 transaction_id, union mux_cmd_param *param,
			     size_t res_size, bool blocking, bool respond);

/**
 * ipc_mux_netif_tx_flowctrl - Enable/Disable TX flow control on MUX sessions.
 * @session:	Pointer to mux_session struct
 * @idx:	Session ID
 * @on:		true for Enable and false for disable flow control
 */
void ipc_mux_netif_tx_flowctrl(struct mux_session *session, int idx, bool on);

/**
 * ipc_mux_ul_trigger_encode - Route the UL packet through the IP MUX layer
 *			       for encoding.
 * @ipc_mux:	Pointer to MUX data-struct
 * @if_id:	Session ID.
 * @skb:	Pointer to ipc_skb.
 *
 * Returns: 0 if successfully encoded
 *	    failure value on error
 *	    -EBUSY if packet has to be retransmitted.
 */
int ipc_mux_ul_trigger_encode(struct iosm_mux *ipc_mux, int if_id,
			      struct sk_buff *skb);
/**
 * ipc_mux_ul_data_encode - UL encode function for calling from Tasklet context.
 * @ipc_mux:	Pointer to MUX data-struct
 *
 * Returns: TRUE if any packet of any session is encoded FALSE otherwise.
 */
bool ipc_mux_ul_data_encode(struct iosm_mux *ipc_mux);

/**
 * ipc_mux_ul_encoded_process - Handles the Modem processed UL data by adding
 *				the SKB to the UL free list.
 * @ipc_mux:	Pointer to MUX data-struct
 * @skb:	Pointer to ipc_skb.
 */
void ipc_mux_ul_encoded_process(struct iosm_mux *ipc_mux, struct sk_buff *skb);

void ipc_mux_ul_adb_finish(struct iosm_mux *ipc_mux);

void ipc_mux_ul_adb_update_ql(struct iosm_mux *ipc_mux, struct mux_adb *p_adb,
			      int session_id, int qlth_n_ql_size,
			      struct sk_buff_head *ul_list);

#endif
