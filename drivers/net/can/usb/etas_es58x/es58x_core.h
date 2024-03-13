/* SPDX-License-Identifier: GPL-2.0 */

/* Driver for ETAS GmbH ES58X USB CAN(-FD) Bus Interfaces.
 *
 * File es58x_core.h: All common definitions and declarations.
 *
 * Copyright (c) 2019 Robert Bosch Engineering and Business Solutions. All rights reserved.
 * Copyright (c) 2020 ETAS K.K.. All rights reserved.
 * Copyright (c) 2020, 2021 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#ifndef __ES58X_COMMON_H__
#define __ES58X_COMMON_H__

#include <linux/types.h>
#include <linux/usb.h>
#include <linux/netdevice.h>
#include <linux/can.h>
#include <linux/can/dev.h>

#include "es581_4.h"
#include "es58x_fd.h"

/* Driver constants */
#define ES58X_RX_URBS_MAX 5	/* Empirical value */
#define ES58X_TX_URBS_MAX 6	/* Empirical value */

#define ES58X_MAX(param)				\
	(ES581_4_##param > ES58X_FD_##param ?		\
		ES581_4_##param : ES58X_FD_##param)
#define ES58X_TX_BULK_MAX ES58X_MAX(TX_BULK_MAX)
#define ES58X_RX_BULK_MAX ES58X_MAX(RX_BULK_MAX)
#define ES58X_ECHO_BULK_MAX ES58X_MAX(ECHO_BULK_MAX)
#define ES58X_NUM_CAN_CH_MAX ES58X_MAX(NUM_CAN_CH)

/* Use this when channel index is irrelevant (e.g. device
 * timestamp).
 */
#define ES58X_CHANNEL_IDX_NA 0xFF
#define ES58X_EMPTY_MSG NULL

/* Threshold on consecutive CAN_STATE_ERROR_PASSIVE. If we receive
 * ES58X_CONSECUTIVE_ERR_PASSIVE_MAX times the event
 * ES58X_ERR_CRTL_PASSIVE in a row without any successful RX or TX,
 * we force the device to switch to CAN_STATE_BUS_OFF state.
 */
#define ES58X_CONSECUTIVE_ERR_PASSIVE_MAX 254

/* A magic number sent by the ES581.4 to inform it is alive. */
#define ES58X_HEARTBEAT 0x11

/**
 * enum es58x_driver_info - Quirks of the device.
 * @ES58X_DUAL_CHANNEL: Device has two CAN channels. If this flag is
 *	not set, it is implied that the device has only one CAN
 *	channel.
 * @ES58X_FD_FAMILY: Device is CAN-FD capable. If this flag is not
 *	set, the device only supports classical CAN.
 */
enum es58x_driver_info {
	ES58X_DUAL_CHANNEL = BIT(0),
	ES58X_FD_FAMILY = BIT(1)
};

enum es58x_echo {
	ES58X_ECHO_OFF = 0,
	ES58X_ECHO_ON = 1
};

/**
 * enum es58x_physical_layer - Type of the physical layer.
 * @ES58X_PHYSICAL_LAYER_HIGH_SPEED: High-speed CAN (c.f. ISO
 *	11898-2).
 *
 * Some products of the ETAS portfolio also support low-speed CAN
 * (c.f. ISO 11898-3). However, all the devices in scope of this
 * driver do not support the option, thus, the enum has only one
 * member.
 */
enum es58x_physical_layer {
	ES58X_PHYSICAL_LAYER_HIGH_SPEED = 1
};

enum es58x_samples_per_bit {
	ES58X_SAMPLES_PER_BIT_ONE = 1,
	ES58X_SAMPLES_PER_BIT_THREE = 2
};

/**
 * enum es58x_sync_edge - Synchronization method.
 * @ES58X_SYNC_EDGE_SINGLE: ISO CAN specification defines the use of a
 *	single edge synchronization.  The synchronization should be
 *	done on recessive to dominant level change.
 *
 * For information, ES582.1 and ES584.1 also support a double
 * synchronization, requiring both recessive to dominant then dominant
 * to recessive level change. However, this is not supported in
 * SocketCAN framework, thus, the enum has only one member.
 */
enum es58x_sync_edge {
	ES58X_SYNC_EDGE_SINGLE = 1
};

/**
 * enum es58x_flag - CAN flags for RX/TX messages.
 * @ES58X_FLAG_EFF: Extended Frame Format (EFF).
 * @ES58X_FLAG_RTR: Remote Transmission Request (RTR).
 * @ES58X_FLAG_FD_BRS: Bit rate switch (BRS): second bitrate for
 *	payload data.
 * @ES58X_FLAG_FD_ESI: Error State Indicator (ESI): tell if the
 *	transmitting node is in error passive mode.
 * @ES58X_FLAG_FD_DATA: CAN FD frame.
 */
enum es58x_flag {
	ES58X_FLAG_EFF = BIT(0),
	ES58X_FLAG_RTR = BIT(1),
	ES58X_FLAG_FD_BRS = BIT(3),
	ES58X_FLAG_FD_ESI = BIT(5),
	ES58X_FLAG_FD_DATA = BIT(6)
};

/**
 * enum es58x_err - CAN error detection.
 * @ES58X_ERR_OK: No errors.
 * @ES58X_ERR_PROT_STUFF: Bit stuffing error: more than 5 consecutive
 *	equal bits.
 * @ES58X_ERR_PROT_FORM: Frame format error.
 * @ES58X_ERR_ACK: Received no ACK on transmission.
 * @ES58X_ERR_PROT_BIT: Single bit error.
 * @ES58X_ERR_PROT_CRC: Incorrect 15, 17 or 21 bits CRC.
 * @ES58X_ERR_PROT_BIT1: Unable to send recessive bit: tried to send
 *	recessive bit 1 but monitored dominant bit 0.
 * @ES58X_ERR_PROT_BIT0: Unable to send dominant bit: tried to send
 *	dominant bit 0 but monitored recessive bit 1.
 * @ES58X_ERR_PROT_OVERLOAD: Bus overload.
 * @ES58X_ERR_PROT_UNSPEC: Unspecified.
 *
 * Please refer to ISO 11898-1:2015, section 10.11 "Error detection"
 * and section 10.13 "Overload signaling" for additional details.
 */
enum es58x_err {
	ES58X_ERR_OK = 0,
	ES58X_ERR_PROT_STUFF = BIT(0),
	ES58X_ERR_PROT_FORM = BIT(1),
	ES58X_ERR_ACK = BIT(2),
	ES58X_ERR_PROT_BIT = BIT(3),
	ES58X_ERR_PROT_CRC = BIT(4),
	ES58X_ERR_PROT_BIT1 = BIT(5),
	ES58X_ERR_PROT_BIT0 = BIT(6),
	ES58X_ERR_PROT_OVERLOAD = BIT(7),
	ES58X_ERR_PROT_UNSPEC = BIT(31)
};

/**
 * enum es58x_event - CAN error codes returned by the device.
 * @ES58X_EVENT_OK: No errors.
 * @ES58X_EVENT_CRTL_ACTIVE: Active state: both TR and RX error count
 *	is less than 128.
 * @ES58X_EVENT_CRTL_PASSIVE: Passive state: either TX or RX error
 *	count is greater than 127.
 * @ES58X_EVENT_CRTL_WARNING: Warning state: either TX or RX error
 *	count is greater than 96.
 * @ES58X_EVENT_BUSOFF: Bus off.
 * @ES58X_EVENT_SINGLE_WIRE: Lost connection on either CAN high or CAN
 *	low.
 *
 * Please refer to ISO 11898-1:2015, section 12.1.4 "Rules of fault
 * confinement" for additional details.
 */
enum es58x_event {
	ES58X_EVENT_OK = 0,
	ES58X_EVENT_CRTL_ACTIVE = BIT(0),
	ES58X_EVENT_CRTL_PASSIVE = BIT(1),
	ES58X_EVENT_CRTL_WARNING = BIT(2),
	ES58X_EVENT_BUSOFF = BIT(3),
	ES58X_EVENT_SINGLE_WIRE = BIT(4)
};

/* enum es58x_ret_u8 - Device return error codes, 8 bit format.
 *
 * Specific to ES581.4.
 */
enum es58x_ret_u8 {
	ES58X_RET_U8_OK = 0x00,
	ES58X_RET_U8_ERR_UNSPECIFIED_FAILURE = 0x80,
	ES58X_RET_U8_ERR_NO_MEM = 0x81,
	ES58X_RET_U8_ERR_BAD_CRC = 0x99
};

/* enum es58x_ret_u32 - Device return error codes, 32 bit format.
 */
enum es58x_ret_u32 {
	ES58X_RET_U32_OK = 0x00000000UL,
	ES58X_RET_U32_ERR_UNSPECIFIED_FAILURE = 0x80000000UL,
	ES58X_RET_U32_ERR_NO_MEM = 0x80004001UL,
	ES58X_RET_U32_WARN_PARAM_ADJUSTED = 0x40004000UL,
	ES58X_RET_U32_WARN_TX_MAYBE_REORDER = 0x40004001UL,
	ES58X_RET_U32_ERR_TIMEDOUT = 0x80000008UL,
	ES58X_RET_U32_ERR_FIFO_FULL = 0x80003002UL,
	ES58X_RET_U32_ERR_BAD_CONFIG = 0x80004000UL,
	ES58X_RET_U32_ERR_NO_RESOURCE = 0x80004002UL
};

/* enum es58x_ret_type - Type of the command returned by the ES58X
 *	device.
 */
enum es58x_ret_type {
	ES58X_RET_TYPE_SET_BITTIMING,
	ES58X_RET_TYPE_ENABLE_CHANNEL,
	ES58X_RET_TYPE_DISABLE_CHANNEL,
	ES58X_RET_TYPE_TX_MSG,
	ES58X_RET_TYPE_RESET_RX,
	ES58X_RET_TYPE_RESET_TX,
	ES58X_RET_TYPE_DEVICE_ERR
};

union es58x_urb_cmd {
	struct es581_4_urb_cmd es581_4_urb_cmd;
	struct es58x_fd_urb_cmd es58x_fd_urb_cmd;
	struct {		/* Common header parts of all variants */
		__le16 sof;
		u8 cmd_type;
		u8 cmd_id;
	} __packed;
	DECLARE_FLEX_ARRAY(u8, raw_cmd);
};

/**
 * struct es58x_priv - All information specific to a CAN channel.
 * @can: struct can_priv must be the first member (Socket CAN relies
 *	on the fact that function netdev_priv() returns a pointer to
 *	a struct can_priv).
 * @es58x_dev: pointer to the corresponding ES58X device.
 * @tx_urb: Used as a buffer to concatenate the TX messages and to do
 *	a bulk send. Please refer to es58x_start_xmit() for more
 *	details.
 * @tx_tail: Index of the oldest packet still pending for
 *	completion. @tx_tail & echo_skb_mask represents the beginning
 *	of the echo skb FIFO, i.e. index of the first element.
 * @tx_head: Index of the next packet to be sent to the
 *	device. @tx_head & echo_skb_mask represents the end of the
 *	echo skb FIFO plus one, i.e. the first free index.
 * @tx_can_msg_cnt: Number of messages in @tx_urb.
 * @tx_can_msg_is_fd: false: all messages in @tx_urb are Classical
 *	CAN, true: all messages in @tx_urb are CAN FD. Rationale:
 *	ES58X FD devices do not allow to mix Classical CAN and FD CAN
 *	frames in one single bulk transmission.
 * @err_passive_before_rtx_success: The ES58X device might enter in a
 *	state in which it keeps alternating between error passive
 *	and active states. This counter keeps track of the number of
 *	error passive and if it gets bigger than
 *	ES58X_CONSECUTIVE_ERR_PASSIVE_MAX, es58x_rx_err_msg() will
 *	force the status to bus-off.
 * @channel_idx: Channel index, starts at zero.
 */
struct es58x_priv {
	struct can_priv can;
	struct es58x_device *es58x_dev;
	struct urb *tx_urb;

	u32 tx_tail;
	u32 tx_head;

	u8 tx_can_msg_cnt;
	bool tx_can_msg_is_fd;

	u8 err_passive_before_rtx_success;

	u8 channel_idx;
};

/**
 * struct es58x_parameters - Constant parameters of a given hardware
 *	variant.
 * @bittiming_const: Nominal bittimming constant parameters.
 * @data_bittiming_const: Data bittiming constant parameters.
 * @tdc_const: Transmission Delay Compensation constant parameters.
 * @bitrate_max: Maximum bitrate supported by the device.
 * @clock: CAN clock parameters.
 * @ctrlmode_supported: List of supported modes. Please refer to
 *	can/netlink.h file for additional details.
 * @tx_start_of_frame: Magic number at the beginning of each TX URB
 *	command.
 * @rx_start_of_frame: Magic number at the beginning of each RX URB
 *	command.
 * @tx_urb_cmd_max_len: Maximum length of a TX URB command.
 * @rx_urb_cmd_max_len: Maximum length of a RX URB command.
 * @fifo_mask: Bit mask to quickly convert the tx_tail and tx_head
 *	field of the struct es58x_priv into echo_skb
 *	indexes. Properties: @fifo_mask = echo_skb_max - 1 where
 *	echo_skb_max must be a power of two. Also, echo_skb_max must
 *	not exceed the maximum size of the device internal TX FIFO
 *	length. This parameter is used to control the network queue
 *	wake/stop logic.
 * @dql_min_limit: Dynamic Queue Limits (DQL) absolute minimum limit
 *	of bytes allowed to be queued on this network device transmit
 *	queue. Used by the Byte Queue Limits (BQL) to determine how
 *	frequently the xmit_more flag will be set to true in
 *	es58x_start_xmit(). Set this value higher to optimize for
 *	throughput but be aware that it might have a negative impact
 *	on the latency! This value can also be set dynamically. Please
 *	refer to Documentation/ABI/testing/sysfs-class-net-queues for
 *	more details.
 * @tx_bulk_max: Maximum number of TX messages that can be sent in one
 *	single URB packet.
 * @urb_cmd_header_len: Length of the URB command header.
 * @rx_urb_max: Number of RX URB to be allocated during device probe.
 * @tx_urb_max: Number of TX URB to be allocated during device probe.
 */
struct es58x_parameters {
	const struct can_bittiming_const *bittiming_const;
	const struct can_bittiming_const *data_bittiming_const;
	const struct can_tdc_const *tdc_const;
	u32 bitrate_max;
	struct can_clock clock;
	u32 ctrlmode_supported;
	u16 tx_start_of_frame;
	u16 rx_start_of_frame;
	u16 tx_urb_cmd_max_len;
	u16 rx_urb_cmd_max_len;
	u16 fifo_mask;
	u16 dql_min_limit;
	u8 tx_bulk_max;
	u8 urb_cmd_header_len;
	u8 rx_urb_max;
	u8 tx_urb_max;
};

/**
 * struct es58x_operators - Function pointers used to encode/decode
 *	the TX/RX messages.
 * @get_msg_len: Get field msg_len of the urb_cmd. The offset of
 *	msg_len inside urb_cmd depends of the device model.
 * @handle_urb_cmd: Decode the URB command received from the device
 *	and dispatch it to the relevant sub function.
 * @fill_urb_header: Fill the header of urb_cmd.
 * @tx_can_msg: Encode a TX CAN message and add it to the bulk buffer
 *	cmd_buf of es58x_dev.
 * @enable_channel: Start the CAN channel.
 * @disable_channel: Stop the CAN channel.
 * @reset_device: Full reset of the device. N.B: this feature is only
 *	present on the ES581.4. For ES58X FD devices, this field is
 *	set to NULL.
 * @get_timestamp: Request a timestamp from the ES58X device.
 */
struct es58x_operators {
	u16 (*get_msg_len)(const union es58x_urb_cmd *urb_cmd);
	int (*handle_urb_cmd)(struct es58x_device *es58x_dev,
			      const union es58x_urb_cmd *urb_cmd);
	void (*fill_urb_header)(union es58x_urb_cmd *urb_cmd, u8 cmd_type,
				u8 cmd_id, u8 channel_idx, u16 cmd_len);
	int (*tx_can_msg)(struct es58x_priv *priv, const struct sk_buff *skb);
	int (*enable_channel)(struct es58x_priv *priv);
	int (*disable_channel)(struct es58x_priv *priv);
	int (*reset_device)(struct es58x_device *es58x_dev);
	int (*get_timestamp)(struct es58x_device *es58x_dev);
};

/**
 * struct es58x_device - All information specific to an ES58X device.
 * @dev: Device information.
 * @udev: USB device information.
 * @netdev: Array of our CAN channels.
 * @param: The constant parameters.
 * @ops: Operators.
 * @rx_pipe: USB reception pipe.
 * @tx_pipe: USB transmission pipe.
 * @rx_urbs: Anchor for received URBs.
 * @tx_urbs_busy: Anchor for TX URBs which were send to the device.
 * @tx_urbs_idle: Anchor for TX USB which are idle. This driver
 *	allocates the memory for the URBs during the probe. When a TX
 *	URB is needed, it can be taken from this anchor. The network
 *	queue wake/stop logic should prevent this URB from getting
 *	empty. Please refer to es58x_get_tx_urb() for more details.
 * @tx_urbs_idle_cnt: number of urbs in @tx_urbs_idle.
 * @ktime_req_ns: kernel timestamp when es58x_set_realtime_diff_ns()
 *	was called.
 * @realtime_diff_ns: difference in nanoseconds between the clocks of
 *	the ES58X device and the kernel.
 * @timestamps: a temporary buffer to store the time stamps before
 *	feeding them to es58x_can_get_echo_skb(). Can only be used
 *	in RX branches.
 * @num_can_ch: Number of CAN channel (i.e. number of elements of @netdev).
 * @opened_channel_cnt: number of channels opened. Free of race
 *	conditions because its two users (net_device_ops:ndo_open()
 *	and net_device_ops:ndo_close()) guarantee that the network
 *	stack big kernel lock (a.k.a. rtnl_mutex) is being hold.
 * @rx_cmd_buf_len: Length of @rx_cmd_buf.
 * @rx_cmd_buf: The device might split the URB commands in an
 *	arbitrary amount of pieces. This buffer is used to concatenate
 *	all those pieces. Can only be used in RX branches. This field
 *	has to be the last one of the structure because it is has a
 *	flexible size (c.f. es58x_sizeof_es58x_device() function).
 */
struct es58x_device {
	struct device *dev;
	struct usb_device *udev;
	struct net_device *netdev[ES58X_NUM_CAN_CH_MAX];

	const struct es58x_parameters *param;
	const struct es58x_operators *ops;

	unsigned int rx_pipe;
	unsigned int tx_pipe;

	struct usb_anchor rx_urbs;
	struct usb_anchor tx_urbs_busy;
	struct usb_anchor tx_urbs_idle;
	atomic_t tx_urbs_idle_cnt;

	u64 ktime_req_ns;
	s64 realtime_diff_ns;

	u64 timestamps[ES58X_ECHO_BULK_MAX];

	u8 num_can_ch;
	u8 opened_channel_cnt;

	u16 rx_cmd_buf_len;
	union es58x_urb_cmd rx_cmd_buf;
};

/**
 * es58x_sizeof_es58x_device() - Calculate the maximum length of
 *	struct es58x_device.
 * @es58x_dev_param: The constant parameters of the device.
 *
 * The length of struct es58x_device depends on the length of its last
 * field: rx_cmd_buf. This macro allows to optimize the memory
 * allocation.
 *
 * Return: length of struct es58x_device.
 */
static inline size_t es58x_sizeof_es58x_device(const struct es58x_parameters
					       *es58x_dev_param)
{
	return offsetof(struct es58x_device, rx_cmd_buf) +
		es58x_dev_param->rx_urb_cmd_max_len;
}

static inline int __es58x_check_msg_len(const struct device *dev,
					const char *stringified_msg,
					size_t actual_len, size_t expected_len)
{
	if (expected_len != actual_len) {
		dev_err(dev,
			"Length of %s is %zu but received command is %zu.\n",
			stringified_msg, expected_len, actual_len);
		return -EMSGSIZE;
	}
	return 0;
}

/**
 * es58x_check_msg_len() - Check the size of a received message.
 * @dev: Device, used to print error messages.
 * @msg: Received message, must not be a pointer.
 * @actual_len: Length of the message as advertised in the command header.
 *
 * Must be a macro in order to accept the different types of messages
 * as an input. Can be use with any of the messages which have a fixed
 * length. Check for an exact match of the size.
 *
 * Return: zero on success, -EMSGSIZE if @actual_len differs from the
 * expected length.
 */
#define es58x_check_msg_len(dev, msg, actual_len)			\
	__es58x_check_msg_len(dev, __stringify(msg),			\
			      actual_len, sizeof(msg))

static inline int __es58x_check_msg_max_len(const struct device *dev,
					    const char *stringified_msg,
					    size_t actual_len,
					    size_t expected_len)
{
	if (actual_len > expected_len) {
		dev_err(dev,
			"Maximum length for %s is %zu but received command is %zu.\n",
			stringified_msg, expected_len, actual_len);
		return -EOVERFLOW;
	}
	return 0;
}

/**
 * es58x_check_msg_max_len() - Check the maximum size of a received message.
 * @dev: Device, used to print error messages.
 * @msg: Received message, must not be a pointer.
 * @actual_len: Length of the message as advertised in the command header.
 *
 * Must be a macro in order to accept the different types of messages
 * as an input. To be used with the messages of variable sizes. Only
 * check that the message is not bigger than the maximum expected
 * size.
 *
 * Return: zero on success, -EOVERFLOW if @actual_len is greater than
 * the expected length.
 */
#define es58x_check_msg_max_len(dev, msg, actual_len)			\
	__es58x_check_msg_max_len(dev, __stringify(msg),		\
				  actual_len, sizeof(msg))

static inline int __es58x_msg_num_element(const struct device *dev,
					  const char *stringified_msg,
					  size_t actual_len, size_t msg_len,
					  size_t elem_len)
{
	size_t actual_num_elem = actual_len / elem_len;
	size_t expected_num_elem = msg_len / elem_len;

	if (actual_num_elem == 0) {
		dev_err(dev,
			"Minimum length for %s is %zu but received command is %zu.\n",
			stringified_msg, elem_len, actual_len);
		return -EMSGSIZE;
	} else if ((actual_len % elem_len) != 0) {
		dev_err(dev,
			"Received command length: %zu is not a multiple of %s[0]: %zu\n",
			actual_len, stringified_msg, elem_len);
		return -EMSGSIZE;
	} else if (actual_num_elem > expected_num_elem) {
		dev_err(dev,
			"Array %s is supposed to have %zu elements each of size %zu...\n",
			stringified_msg, expected_num_elem, elem_len);
		dev_err(dev,
			"... But received command has %zu elements (total length %zu).\n",
			actual_num_elem, actual_len);
		return -EOVERFLOW;
	}
	return actual_num_elem;
}

/**
 * es58x_msg_num_element() - Check size and give the number of
 *	elements in a message of array type.
 * @dev: Device, used to print error messages.
 * @msg: Received message, must be an array.
 * @actual_len: Length of the message as advertised in the command
 *	header.
 *
 * Must be a macro in order to accept the different types of messages
 * as an input. To be used on message of array type. Array's element
 * has to be of fixed size (else use es58x_check_msg_max_len()). Check
 * that the total length is an exact multiple of the length of a
 * single element.
 *
 * Return: number of elements in the array on success, -EOVERFLOW if
 * @actual_len is greater than the expected length, -EMSGSIZE if
 * @actual_len is not a multiple of a single element.
 */
#define es58x_msg_num_element(dev, msg, actual_len)			\
({									\
	size_t __elem_len = sizeof((msg)[0]) + __must_be_array(msg);	\
	__es58x_msg_num_element(dev, __stringify(msg), actual_len,	\
				sizeof(msg), __elem_len);		\
})

/**
 * es58x_priv() - Get the priv member and cast it to struct es58x_priv.
 * @netdev: CAN network device.
 *
 * Return: ES58X device.
 */
static inline struct es58x_priv *es58x_priv(struct net_device *netdev)
{
	return (struct es58x_priv *)netdev_priv(netdev);
}

/**
 * ES58X_SIZEOF_URB_CMD() - Calculate the maximum length of an urb
 *	command for a given message field name.
 * @es58x_urb_cmd_type: type (either "struct es581_4_urb_cmd" or
 *	"struct es58x_fd_urb_cmd").
 * @msg_field: name of the message field.
 *
 * Must be a macro in order to accept the different command types as
 * an input.
 *
 * Return: length of the urb command.
 */
#define ES58X_SIZEOF_URB_CMD(es58x_urb_cmd_type, msg_field)		\
	(offsetof(es58x_urb_cmd_type, raw_msg)				\
		+ sizeof_field(es58x_urb_cmd_type, msg_field)		\
		+ sizeof_field(es58x_urb_cmd_type,			\
			       reserved_for_crc16_do_not_use))

/**
 * es58x_get_urb_cmd_len() - Calculate the actual length of an urb
 *	command for a given message length.
 * @es58x_dev: ES58X device.
 * @msg_len: Length of the message.
 *
 * Add the header and CRC lengths to the message length.
 *
 * Return: length of the urb command.
 */
static inline size_t es58x_get_urb_cmd_len(struct es58x_device *es58x_dev,
					   u16 msg_len)
{
	return es58x_dev->param->urb_cmd_header_len + msg_len + sizeof(u16);
}

/**
 * es58x_get_netdev() - Get the network device.
 * @es58x_dev: ES58X device.
 * @channel_no: The channel number as advertised in the urb command.
 * @channel_idx_offset: Some of the ES58x starts channel numbering
 *	from 0 (ES58X FD), others from 1 (ES581.4).
 * @netdev: CAN network device.
 *
 * Do a sanity check on the index provided by the device.
 *
 * Return: zero on success, -ECHRNG if the received channel number is
 *	out of range and -ENODEV if the network device is not yet
 *	configured.
 */
static inline int es58x_get_netdev(struct es58x_device *es58x_dev,
				   int channel_no, int channel_idx_offset,
				   struct net_device **netdev)
{
	int channel_idx = channel_no - channel_idx_offset;

	*netdev = NULL;
	if (channel_idx < 0 || channel_idx >= es58x_dev->num_can_ch)
		return -ECHRNG;

	*netdev = es58x_dev->netdev[channel_idx];
	if (!*netdev || !netif_device_present(*netdev))
		return -ENODEV;

	return 0;
}

/**
 * es58x_get_raw_can_id() - Get the CAN ID.
 * @cf: CAN frame.
 *
 * Mask the CAN ID in order to only keep the significant bits.
 *
 * Return: the raw value of the CAN ID.
 */
static inline int es58x_get_raw_can_id(const struct can_frame *cf)
{
	if (cf->can_id & CAN_EFF_FLAG)
		return cf->can_id & CAN_EFF_MASK;
	else
		return cf->can_id & CAN_SFF_MASK;
}

/**
 * es58x_get_flags() - Get the CAN flags.
 * @skb: socket buffer of a CAN message.
 *
 * Return: the CAN flag as an enum es58x_flag.
 */
static inline enum es58x_flag es58x_get_flags(const struct sk_buff *skb)
{
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	enum es58x_flag es58x_flags = 0;

	if (cf->can_id & CAN_EFF_FLAG)
		es58x_flags |= ES58X_FLAG_EFF;

	if (can_is_canfd_skb(skb)) {
		es58x_flags |= ES58X_FLAG_FD_DATA;
		if (cf->flags & CANFD_BRS)
			es58x_flags |= ES58X_FLAG_FD_BRS;
		if (cf->flags & CANFD_ESI)
			es58x_flags |= ES58X_FLAG_FD_ESI;
	} else if (cf->can_id & CAN_RTR_FLAG)
		/* Remote frames are only defined in Classical CAN frames */
		es58x_flags |= ES58X_FLAG_RTR;

	return es58x_flags;
}

int es58x_can_get_echo_skb(struct net_device *netdev, u32 packet_idx,
			   u64 *tstamps, unsigned int pkts);
int es58x_tx_ack_msg(struct net_device *netdev, u16 tx_free_entries,
		     enum es58x_ret_u32 rx_cmd_ret_u32);
int es58x_rx_can_msg(struct net_device *netdev, u64 timestamp, const u8 *data,
		     canid_t can_id, enum es58x_flag es58x_flags, u8 dlc);
int es58x_rx_err_msg(struct net_device *netdev, enum es58x_err error,
		     enum es58x_event event, u64 timestamp);
void es58x_rx_timestamp(struct es58x_device *es58x_dev, u64 timestamp);
int es58x_rx_cmd_ret_u8(struct device *dev, enum es58x_ret_type cmd_ret_type,
			enum es58x_ret_u8 rx_cmd_ret_u8);
int es58x_rx_cmd_ret_u32(struct net_device *netdev,
			 enum es58x_ret_type cmd_ret_type,
			 enum es58x_ret_u32 rx_cmd_ret_u32);
int es58x_send_msg(struct es58x_device *es58x_dev, u8 cmd_type, u8 cmd_id,
		   const void *msg, u16 cmd_len, int channel_idx);

extern const struct es58x_parameters es581_4_param;
extern const struct es58x_operators es581_4_ops;

extern const struct es58x_parameters es58x_fd_param;
extern const struct es58x_operators es58x_fd_ops;

#endif /* __ES58X_COMMON_H__ */
