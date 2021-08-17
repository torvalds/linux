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
 * ipc_mux_dl_decode -Route the DL packet through the IP MUX layer
 *		      depending on Header.
 * @ipc_mux:	Pointer to MUX data-struct
 * @skb:	Pointer to ipc_skb.
 */
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
 * Returns: 0 in success and failure value on error
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

#endif
