/* SPDX-License-Identifier: GPL-2.0 */

/* Driver for ETAS GmbH ES58X USB CAN(-FD) Bus Interfaces.
 *
 * File es58x_fd.h: Definitions and declarations specific to ETAS
 * ES582.1 and ES584.1 (naming convention: we use the term "ES58X FD"
 * when referring to those two variants together).
 *
 * Copyright (c) 2019 Robert Bosch Engineering and Business Solutions. All rights reserved.
 * Copyright (c) 2020 ETAS K.K.. All rights reserved.
 * Copyright (c) 2020, 2021 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#ifndef __ES58X_FD_H__
#define __ES58X_FD_H__

#include <linux/types.h>

#define ES582_1_NUM_CAN_CH 2
#define ES584_1_NUM_CAN_CH 1
#define ES58X_FD_NUM_CAN_CH 2
#define ES58X_FD_CHANNEL_IDX_OFFSET 0

#define ES58X_FD_TX_BULK_MAX 100
#define ES58X_FD_RX_BULK_MAX 100
#define ES58X_FD_ECHO_BULK_MAX 100

enum es58x_fd_cmd_type {
	ES58X_FD_CMD_TYPE_CAN = 0x03,
	ES58X_FD_CMD_TYPE_CANFD = 0x04,
	ES58X_FD_CMD_TYPE_DEVICE = 0xFF
};

/* Command IDs for ES58X_FD_CMD_TYPE_{CAN,CANFD}. */
enum es58x_fd_can_cmd_id {
	ES58X_FD_CAN_CMD_ID_ENABLE_CHANNEL = 0x01,
	ES58X_FD_CAN_CMD_ID_DISABLE_CHANNEL = 0x02,
	ES58X_FD_CAN_CMD_ID_TX_MSG = 0x05,
	ES58X_FD_CAN_CMD_ID_ECHO_MSG = 0x07,
	ES58X_FD_CAN_CMD_ID_RX_MSG = 0x10,
	ES58X_FD_CAN_CMD_ID_ERROR_OR_EVENT_MSG = 0x11,
	ES58X_FD_CAN_CMD_ID_RESET_RX = 0x20,
	ES58X_FD_CAN_CMD_ID_RESET_TX = 0x21,
	ES58X_FD_CAN_CMD_ID_TX_MSG_NO_ACK = 0x55
};

/* Command IDs for ES58X_FD_CMD_TYPE_DEVICE. */
enum es58x_fd_dev_cmd_id {
	ES58X_FD_DEV_CMD_ID_GETTIMETICKS = 0x01,
	ES58X_FD_DEV_CMD_ID_TIMESTAMP = 0x02
};

/**
 * enum es58x_fd_ctrlmode - Controller mode.
 * @ES58X_FD_CTRLMODE_ACTIVE: send and receive messages.
 * @ES58X_FD_CTRLMODE_PASSIVE: only receive messages (monitor). Do not
 *	send anything, not even the acknowledgment bit.
 * @ES58X_FD_CTRLMODE_FD: CAN FD according to ISO11898-1.
 * @ES58X_FD_CTRLMODE_FD_NON_ISO: follow Bosch CAN FD Specification
 *	V1.0
 * @ES58X_FD_CTRLMODE_DISABLE_PROTOCOL_EXCEPTION_HANDLING: How to
 *	behave when CAN FD reserved bit is monitored as
 *	dominant. (c.f. ISO 11898-1:2015, section 10.4.2.4 "Control
 *	field", paragraph "r0 bit"). 0 (not disable = enable): send
 *	error frame. 1 (disable): goes into bus integration mode
 *	(c.f. below).
 * @ES58X_FD_CTRLMODE_EDGE_FILTER_DURING_BUS_INTEGRATION: 0: Edge
 *	filtering is disabled. 1: Edge filtering is enabled. Two
 *	consecutive dominant bits required to detect an edge for hard
 *	synchronization.
 */
enum es58x_fd_ctrlmode {
	ES58X_FD_CTRLMODE_ACTIVE = 0,
	ES58X_FD_CTRLMODE_PASSIVE = BIT(0),
	ES58X_FD_CTRLMODE_FD = BIT(4),
	ES58X_FD_CTRLMODE_FD_NON_ISO = BIT(5),
	ES58X_FD_CTRLMODE_DISABLE_PROTOCOL_EXCEPTION_HANDLING = BIT(6),
	ES58X_FD_CTRLMODE_EDGE_FILTER_DURING_BUS_INTEGRATION = BIT(7)
};

struct es58x_fd_bittiming {
	__le32 bitrate;
	__le16 tseg1;		/* range: [tseg1_min-1..tseg1_max-1] */
	__le16 tseg2;		/* range: [tseg2_min-1..tseg2_max-1] */
	__le16 brp;		/* range: [brp_min-1..brp_max-1] */
	__le16 sjw;		/* range: [0..sjw_max-1] */
} __packed;

/**
 * struct es58x_fd_tx_conf_msg - Channel configuration.
 * @nominal_bittiming: Nominal bittiming.
 * @samples_per_bit: type enum es58x_samples_per_bit.
 * @sync_edge: type enum es58x_sync_edge.
 * @physical_layer: type enum es58x_physical_layer.
 * @echo_mode: type enum es58x_echo_mode.
 * @ctrlmode: type enum es58x_fd_ctrlmode.
 * @canfd_enabled: boolean (0: Classical CAN, 1: CAN and/or CANFD).
 * @data_bittiming: Bittiming for flexible data-rate transmission.
 * @tdc_enabled: Transmitter Delay Compensation switch (0: disabled,
 *	1: enabled). On very high bitrates, the delay between when the
 *	bit is sent and received on the CANTX and CANRX pins of the
 *	transceiver start to be significant enough for errors to occur
 *	and thus need to be compensated.
 * @tdco: Transmitter Delay Compensation Offset. Offset value, in time
 *	quanta, defining the delay between the start of the bit
 *	reception on the CANRX pin of the transceiver and the SSP
 *	(Secondary Sample Point). Valid values: 0 to 127.
 * @tdcf: Transmitter Delay Compensation Filter window. Defines the
 *	minimum value for the SSP position, in time quanta. The
 *	feature is enabled when TDCF is configured to a value greater
 *	than TDCO. Valid values: 0 to 127.
 *
 * Please refer to the microcontroller datasheet: "SAM
 * E701/S70/V70/V71 Family" section 49 "Controller Area Network
 * (MCAN)" for additional information.
 */
struct es58x_fd_tx_conf_msg {
	struct es58x_fd_bittiming nominal_bittiming;
	u8 samples_per_bit;
	u8 sync_edge;
	u8 physical_layer;
	u8 echo_mode;
	u8 ctrlmode;
	u8 canfd_enabled;
	struct es58x_fd_bittiming data_bittiming;
	u8 tdc_enabled;
	__le16 tdco;
	__le16 tdcf;
} __packed;

#define ES58X_FD_CAN_CONF_LEN					\
	(offsetof(struct es58x_fd_tx_conf_msg, canfd_enabled))
#define ES58X_FD_CANFD_CONF_LEN (sizeof(struct es58x_fd_tx_conf_msg))

struct es58x_fd_tx_can_msg {
	u8 packet_idx;
	__le32 can_id;
	u8 flags;
	union {
		u8 dlc;		/* Only if cmd_id is ES58X_FD_CMD_TYPE_CAN */
		u8 len;		/* Only if cmd_id is ES58X_FD_CMD_TYPE_CANFD */
	} __packed;
	u8 data[CANFD_MAX_DLEN];
} __packed;

#define ES58X_FD_CAN_TX_LEN						\
	(offsetof(struct es58x_fd_tx_can_msg, data[CAN_MAX_DLEN]))
#define ES58X_FD_CANFD_TX_LEN (sizeof(struct es58x_fd_tx_can_msg))

struct es58x_fd_rx_can_msg {
	__le64 timestamp;
	__le32 can_id;
	u8 flags;
	union {
		u8 dlc;		/* Only if cmd_id is ES58X_FD_CMD_TYPE_CAN */
		u8 len;		/* Only if cmd_id is ES58X_FD_CMD_TYPE_CANFD */
	} __packed;
	u8 data[CANFD_MAX_DLEN];
} __packed;

#define ES58X_FD_CAN_RX_LEN						\
	(offsetof(struct es58x_fd_rx_can_msg, data[CAN_MAX_DLEN]))
#define ES58X_FD_CANFD_RX_LEN (sizeof(struct es58x_fd_rx_can_msg))

struct es58x_fd_echo_msg {
	__le64 timestamp;
	u8 packet_idx;
} __packed;

struct es58x_fd_rx_event_msg {
	__le64 timestamp;
	__le32 can_id;
	u8 flags;		/* type enum es58x_flag */
	u8 error_type;		/* 0: event, 1: error */
	u8 error_code;
	u8 event_code;
} __packed;

struct es58x_fd_tx_ack_msg {
	__le32 rx_cmd_ret_le32;	/* type enum es58x_cmd_ret_code_u32 */
	__le16 tx_free_entries;	/* Number of remaining free entries in the device TX queue */
} __packed;

/**
 * struct es58x_fd_urb_cmd - Commands received from or sent to the
 *	ES58X FD device.
 * @SOF: Start of Frame.
 * @cmd_type: Command Type (type: enum es58x_fd_cmd_type). The CRC
 *	calculation starts at this position.
 * @cmd_id: Command ID (type: enum es58x_fd_cmd_id).
 * @channel_idx: Channel index starting at 0.
 * @msg_len: Length of the message, excluding CRC (i.e. length of the
 *	union).
 * @tx_conf_msg: Channel configuration.
 * @tx_can_msg_buf: Concatenation of Tx messages. Type is "u8[]"
 *	instead of "struct es58x_fd_tx_msg[]" because the structure
 *	has a flexible size.
 * @rx_can_msg_buf: Concatenation Rx messages. Type is "u8[]" instead
 *	of "struct es58x_fd_rx_msg[]" because the structure has a
 *	flexible size.
 * @echo_msg: Array of echo messages (e.g. Tx messages being looped
 *	back).
 * @rx_event_msg: Error or event message.
 * @tx_ack_msg: Tx acknowledgment message.
 * @timestamp: Timestamp reply.
 * @rx_cmd_ret_le32: Rx 32 bits return code (type: enum
 *	es58x_cmd_ret_code_u32).
 * @raw_msg: Message raw payload.
 * @reserved_for_crc16_do_not_use: The structure ends with a
 *	CRC16. Because the structures in above union are of variable
 *	lengths, we can not predict the offset of the CRC in
 *	advance. Use functions es58x_get_crc() and es58x_set_crc() to
 *	manipulate it.
 */
struct es58x_fd_urb_cmd {
	__le16 SOF;
	u8 cmd_type;
	u8 cmd_id;
	u8 channel_idx;
	__le16 msg_len;

	union {
		struct es58x_fd_tx_conf_msg tx_conf_msg;
		u8 tx_can_msg_buf[ES58X_FD_TX_BULK_MAX * ES58X_FD_CANFD_TX_LEN];
		u8 rx_can_msg_buf[ES58X_FD_RX_BULK_MAX * ES58X_FD_CANFD_RX_LEN];
		struct es58x_fd_echo_msg echo_msg[ES58X_FD_ECHO_BULK_MAX];
		struct es58x_fd_rx_event_msg rx_event_msg;
		struct es58x_fd_tx_ack_msg tx_ack_msg;
		__le64 timestamp;
		__le32 rx_cmd_ret_le32;
		u8 raw_msg[0];
	} __packed;

	__le16 reserved_for_crc16_do_not_use;
} __packed;

#define ES58X_FD_URB_CMD_HEADER_LEN (offsetof(struct es58x_fd_urb_cmd, raw_msg))
#define ES58X_FD_TX_URB_CMD_MAX_LEN					\
	ES58X_SIZEOF_URB_CMD(struct es58x_fd_urb_cmd, tx_can_msg_buf)
#define ES58X_FD_RX_URB_CMD_MAX_LEN					\
	ES58X_SIZEOF_URB_CMD(struct es58x_fd_urb_cmd, rx_can_msg_buf)

#endif /* __ES58X_FD_H__ */
