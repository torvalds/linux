/* SPDX-License-Identifier: GPL-2.0 */

/* Driver for ETAS GmbH ES58X USB CAN(-FD) Bus Interfaces.
 *
 * File es581_4.h: Definitions and declarations specific to ETAS
 * ES581.4.
 *
 * Copyright (c) 2019 Robert Bosch Engineering and Business Solutions. All rights reserved.
 * Copyright (c) 2020 ETAS K.K.. All rights reserved.
 * Copyright (c) 2020, 2021 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#ifndef __ES581_4_H__
#define __ES581_4_H__

#include <linux/types.h>

#define ES581_4_NUM_CAN_CH 2
#define ES581_4_CHANNEL_IDX_OFFSET 1

#define ES581_4_TX_BULK_MAX 25
#define ES581_4_RX_BULK_MAX 30
#define ES581_4_ECHO_BULK_MAX 30

enum es581_4_cmd_type {
	ES581_4_CAN_COMMAND_TYPE = 0x45
};

enum es581_4_cmd_id {
	ES581_4_CMD_ID_OPEN_CHANNEL = 0x01,
	ES581_4_CMD_ID_CLOSE_CHANNEL = 0x02,
	ES581_4_CMD_ID_SET_BITTIMING = 0x03,
	ES581_4_CMD_ID_ENABLE_CHANNEL = 0x04,
	ES581_4_CMD_ID_TX_MSG = 0x05,
	ES581_4_CMD_ID_RX_MSG = 0x06,
	ES581_4_CMD_ID_RESET_RX = 0x0A,
	ES581_4_CMD_ID_RESET_TX = 0x0B,
	ES581_4_CMD_ID_DISABLE_CHANNEL = 0x0C,
	ES581_4_CMD_ID_TIMESTAMP = 0x0E,
	ES581_4_CMD_ID_RESET_DEVICE = 0x28,
	ES581_4_CMD_ID_ECHO = 0x71,
	ES581_4_CMD_ID_DEVICE_ERR = 0x72
};

enum es581_4_rx_type {
	ES581_4_RX_TYPE_MESSAGE = 1,
	ES581_4_RX_TYPE_ERROR = 3,
	ES581_4_RX_TYPE_EVENT = 4
};

/**
 * struct es581_4_tx_conf_msg - Channel configuration.
 * @bitrate: Bitrate.
 * @sample_point: Sample point is in percent [0..100].
 * @samples_per_bit: type enum es58x_samples_per_bit.
 * @bit_time: Number of time quanta in one bit.
 * @sjw: Synchronization Jump Width.
 * @sync_edge: type enum es58x_sync_edge.
 * @physical_layer: type enum es58x_physical_layer.
 * @echo_mode: type enum es58x_echo_mode.
 * @channel_no: Channel number, starting from 1. Not to be confused
 *	with channed_idx of the ES58X FD which starts from 0.
 */
struct es581_4_tx_conf_msg {
	__le32 bitrate;
	__le32 sample_point;
	__le32 samples_per_bit;
	__le32 bit_time;
	__le32 sjw;
	__le32 sync_edge;
	__le32 physical_layer;
	__le32 echo_mode;
	u8 channel_no;
} __packed;

struct es581_4_tx_can_msg {
	__le32 can_id;
	__le32 packet_idx;
	__le16 flags;
	u8 channel_no;
	u8 dlc;
	u8 data[CAN_MAX_DLEN];
} __packed;

/* The ES581.4 allows bulk transfer.  */
struct es581_4_bulk_tx_can_msg {
	u8 num_can_msg;
	/* Using type "u8[]" instead of "struct es581_4_tx_can_msg[]"
	 * for tx_msg_buf because each member has a flexible size.
	 */
	u8 tx_can_msg_buf[ES581_4_TX_BULK_MAX *
			  sizeof(struct es581_4_tx_can_msg)];
} __packed;

struct es581_4_echo_msg {
	__le64 timestamp;
	__le32 packet_idx;
} __packed;

struct es581_4_bulk_echo_msg {
	u8 channel_no;
	struct es581_4_echo_msg echo_msg[ES581_4_ECHO_BULK_MAX];
} __packed;

/* Normal Rx CAN Message */
struct es581_4_rx_can_msg {
	__le64 timestamp;
	u8 rx_type;		/* type enum es581_4_rx_type */
	u8 flags;		/* type enum es58x_flag */
	u8 channel_no;
	u8 dlc;
	__le32 can_id;
	u8 data[CAN_MAX_DLEN];
} __packed;

struct es581_4_rx_err_msg {
	__le64 timestamp;
	__le16 rx_type;		/* type enum es581_4_rx_type */
	__le16 flags;		/* type enum es58x_flag */
	u8 channel_no;
	u8 __padding[2];
	u8 dlc;
	__le32 tag;		/* Related to the CAN filtering. Unused in this module */
	__le32 can_id;
	__le32 error;		/* type enum es58x_error */
	__le32 destination;	/* Unused in this module */
} __packed;

struct es581_4_rx_event_msg {
	__le64 timestamp;
	__le16 rx_type;		/* type enum es581_4_rx_type */
	u8 channel_no;
	u8 __padding;
	__le32 tag;		/* Related to the CAN filtering. Unused in this module */
	__le32 event;		/* type enum es58x_event */
	__le32 destination;	/* Unused in this module */
} __packed;

struct es581_4_tx_ack_msg {
	__le16 tx_free_entries;	/* Number of remaining free entries in the device TX queue */
	u8 channel_no;
	u8 rx_cmd_ret_u8;	/* type enum es58x_cmd_ret_code_u8 */
} __packed;

struct es581_4_rx_cmd_ret {
	__le32 rx_cmd_ret_le32;
	u8 channel_no;
	u8 __padding[3];
} __packed;

/**
 * struct es581_4_urb_cmd - Commands received from or sent to the
 *	ES581.4 device.
 * @SOF: Start of Frame.
 * @cmd_type: Command Type (type: enum es581_4_cmd_type). The CRC
 *	calculation starts at this position.
 * @cmd_id: Command ID (type: enum es581_4_cmd_id).
 * @msg_len: Length of the message, excluding CRC (i.e. length of the
 *	union).
 * @tx_conf_msg: Channel configuration.
 * @bulk_tx_can_msg: Tx messages.
 * @rx_can_msg: Array of Rx messages.
 * @bulk_echo_msg: Tx message being looped back.
 * @rx_err_msg: Error message.
 * @rx_event_msg: Event message.
 * @tx_ack_msg: Tx acknowledgment message.
 * @rx_cmd_ret: Command return code.
 * @timestamp: Timestamp reply.
 * @rx_cmd_ret_u8: Rx 8 bits return code (type: enum
 *	es58x_cmd_ret_code_u8).
 * @raw_msg: Message raw payload.
 * @reserved_for_crc16_do_not_use: The structure ends with a
 *	CRC16. Because the structures in above union are of variable
 *	lengths, we can not predict the offset of the CRC in
 *	advance. Use functions es58x_get_crc() and es58x_set_crc() to
 *	manipulate it.
 */
struct es581_4_urb_cmd {
	__le16 SOF;
	u8 cmd_type;
	u8 cmd_id;
	__le16 msg_len;

	union {
		struct es581_4_tx_conf_msg tx_conf_msg;
		struct es581_4_bulk_tx_can_msg bulk_tx_can_msg;
		struct es581_4_rx_can_msg rx_can_msg[ES581_4_RX_BULK_MAX];
		struct es581_4_bulk_echo_msg bulk_echo_msg;
		struct es581_4_rx_err_msg rx_err_msg;
		struct es581_4_rx_event_msg rx_event_msg;
		struct es581_4_tx_ack_msg tx_ack_msg;
		struct es581_4_rx_cmd_ret rx_cmd_ret;
		__le64 timestamp;
		u8 rx_cmd_ret_u8;
		u8 raw_msg[0];
	} __packed;

	__le16 reserved_for_crc16_do_not_use;
} __packed;

#define ES581_4_URB_CMD_HEADER_LEN (offsetof(struct es581_4_urb_cmd, raw_msg))
#define ES581_4_TX_URB_CMD_MAX_LEN					\
	ES58X_SIZEOF_URB_CMD(struct es581_4_urb_cmd, bulk_tx_can_msg)
#define ES581_4_RX_URB_CMD_MAX_LEN					\
	ES58X_SIZEOF_URB_CMD(struct es581_4_urb_cmd, rx_can_msg)

#endif /* __ES581_4_H__ */
