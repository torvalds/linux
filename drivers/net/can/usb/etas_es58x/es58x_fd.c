// SPDX-License-Identifier: GPL-2.0

/* Driver for ETAS GmbH ES58X USB CAN(-FD) Bus Interfaces.
 *
 * File es58x_fd.c: Adds support to ETAS ES582.1 and ES584.1 (naming
 * convention: we use the term "ES58X FD" when referring to those two
 * variants together).
 *
 * Copyright (c) 2019 Robert Bosch Engineering and Business Solutions. All rights reserved.
 * Copyright (c) 2020 ETAS K.K.. All rights reserved.
 * Copyright (c) 2020-2022 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#include <asm/unaligned.h>
#include <linux/kernel.h>
#include <linux/units.h>

#include "es58x_core.h"
#include "es58x_fd.h"

/**
 * es58x_fd_sizeof_rx_tx_msg() - Calculate the actual length of the
 *	structure of a rx or tx message.
 * @msg: message of variable length, must have a dlc and a len fields.
 *
 * Even if RTR frames have actually no payload, the ES58X devices
 * still expect it. Must be a macro in order to accept several types
 * (struct es58x_fd_tx_can_msg and struct es58x_fd_rx_can_msg) as an
 * input.
 *
 * Return: length of the message.
 */
#define es58x_fd_sizeof_rx_tx_msg(msg)					\
({									\
	typeof(msg) __msg = (msg);					\
	size_t __msg_len;						\
									\
	if (__msg.flags & ES58X_FLAG_FD_DATA)				\
		__msg_len = canfd_sanitize_len(__msg.len);		\
	else								\
		__msg_len = can_cc_dlc2len(__msg.dlc);			\
									\
	offsetof(typeof(__msg), data[__msg_len]);			\
})

static enum es58x_fd_cmd_type es58x_fd_cmd_type(struct net_device *netdev)
{
	u32 ctrlmode = es58x_priv(netdev)->can.ctrlmode;

	if (ctrlmode & (CAN_CTRLMODE_FD | CAN_CTRLMODE_FD_NON_ISO))
		return ES58X_FD_CMD_TYPE_CANFD;
	else
		return ES58X_FD_CMD_TYPE_CAN;
}

static u16 es58x_fd_get_msg_len(const union es58x_urb_cmd *urb_cmd)
{
	return get_unaligned_le16(&urb_cmd->es58x_fd_urb_cmd.msg_len);
}

static int es58x_fd_echo_msg(struct net_device *netdev,
			     const struct es58x_fd_urb_cmd *es58x_fd_urb_cmd)
{
	struct es58x_priv *priv = es58x_priv(netdev);
	const struct es58x_fd_echo_msg *echo_msg;
	struct es58x_device *es58x_dev = priv->es58x_dev;
	u64 *tstamps = es58x_dev->timestamps;
	u16 msg_len = get_unaligned_le16(&es58x_fd_urb_cmd->msg_len);
	int i, num_element;
	u32 rcv_packet_idx;

	const u32 mask = GENMASK(BITS_PER_TYPE(mask) - 1,
				 BITS_PER_TYPE(echo_msg->packet_idx));

	num_element = es58x_msg_num_element(es58x_dev->dev,
					    es58x_fd_urb_cmd->echo_msg,
					    msg_len);
	if (num_element < 0)
		return num_element;
	echo_msg = es58x_fd_urb_cmd->echo_msg;

	rcv_packet_idx = (priv->tx_tail & mask) | echo_msg[0].packet_idx;
	for (i = 0; i < num_element; i++) {
		if ((u8)rcv_packet_idx != echo_msg[i].packet_idx) {
			netdev_err(netdev, "Packet idx jumped from %u to %u\n",
				   (u8)rcv_packet_idx - 1,
				   echo_msg[i].packet_idx);
			return -EBADMSG;
		}

		tstamps[i] = get_unaligned_le64(&echo_msg[i].timestamp);
		rcv_packet_idx++;
	}

	return es58x_can_get_echo_skb(netdev, priv->tx_tail, tstamps, num_element);
}

static int es58x_fd_rx_can_msg(struct net_device *netdev,
			       const struct es58x_fd_urb_cmd *es58x_fd_urb_cmd)
{
	struct es58x_device *es58x_dev = es58x_priv(netdev)->es58x_dev;
	const u8 *rx_can_msg_buf = es58x_fd_urb_cmd->rx_can_msg_buf;
	u16 rx_can_msg_buf_len = get_unaligned_le16(&es58x_fd_urb_cmd->msg_len);
	int pkts, ret;

	ret = es58x_check_msg_max_len(es58x_dev->dev,
				      es58x_fd_urb_cmd->rx_can_msg_buf,
				      rx_can_msg_buf_len);
	if (ret)
		return ret;

	for (pkts = 0; rx_can_msg_buf_len > 0; pkts++) {
		const struct es58x_fd_rx_can_msg *rx_can_msg =
		    (const struct es58x_fd_rx_can_msg *)rx_can_msg_buf;
		bool is_can_fd = !!(rx_can_msg->flags & ES58X_FLAG_FD_DATA);
		/* rx_can_msg_len is the length of the rx_can_msg
		 * buffer. Not to be confused with rx_can_msg->len
		 * which is the length of the CAN payload
		 * rx_can_msg->data.
		 */
		u16 rx_can_msg_len = es58x_fd_sizeof_rx_tx_msg(*rx_can_msg);

		if (rx_can_msg_len > rx_can_msg_buf_len) {
			netdev_err(netdev,
				   "%s: Expected a rx_can_msg of size %d but only %d bytes are left in rx_can_msg_buf\n",
				   __func__,
				   rx_can_msg_len, rx_can_msg_buf_len);
			return -EMSGSIZE;
		}
		if (rx_can_msg->len > CANFD_MAX_DLEN) {
			netdev_err(netdev,
				   "%s: Data length is %d but maximum should be %d\n",
				   __func__, rx_can_msg->len, CANFD_MAX_DLEN);
			return -EMSGSIZE;
		}

		if (netif_running(netdev)) {
			u64 tstamp = get_unaligned_le64(&rx_can_msg->timestamp);
			canid_t can_id = get_unaligned_le32(&rx_can_msg->can_id);
			u8 dlc;

			if (is_can_fd)
				dlc = can_fd_len2dlc(rx_can_msg->len);
			else
				dlc = rx_can_msg->dlc;

			ret = es58x_rx_can_msg(netdev, tstamp, rx_can_msg->data,
					       can_id, rx_can_msg->flags, dlc);
			if (ret)
				break;
		}

		rx_can_msg_buf_len -= rx_can_msg_len;
		rx_can_msg_buf += rx_can_msg_len;
	}

	if (!netif_running(netdev)) {
		if (net_ratelimit())
			netdev_info(netdev,
				    "%s: %s is down, dropping %d rx packets\n",
				    __func__, netdev->name, pkts);
		netdev->stats.rx_dropped += pkts;
	}

	return ret;
}

static int es58x_fd_rx_event_msg(struct net_device *netdev,
				 const struct es58x_fd_urb_cmd *es58x_fd_urb_cmd)
{
	struct es58x_device *es58x_dev = es58x_priv(netdev)->es58x_dev;
	u16 msg_len = get_unaligned_le16(&es58x_fd_urb_cmd->msg_len);
	const struct es58x_fd_rx_event_msg *rx_event_msg;
	int ret;

	rx_event_msg = &es58x_fd_urb_cmd->rx_event_msg;
	ret = es58x_check_msg_len(es58x_dev->dev, *rx_event_msg, msg_len);
	if (ret)
		return ret;

	return es58x_rx_err_msg(netdev, rx_event_msg->error_code,
				rx_event_msg->event_code,
				get_unaligned_le64(&rx_event_msg->timestamp));
}

static int es58x_fd_rx_cmd_ret_u32(struct net_device *netdev,
				   const struct es58x_fd_urb_cmd *es58x_fd_urb_cmd,
				   enum es58x_ret_type cmd_ret_type)
{
	struct es58x_device *es58x_dev = es58x_priv(netdev)->es58x_dev;
	u16 msg_len = get_unaligned_le16(&es58x_fd_urb_cmd->msg_len);
	int ret;

	ret = es58x_check_msg_len(es58x_dev->dev,
				  es58x_fd_urb_cmd->rx_cmd_ret_le32, msg_len);
	if (ret)
		return ret;

	return es58x_rx_cmd_ret_u32(netdev, cmd_ret_type,
				    get_unaligned_le32(&es58x_fd_urb_cmd->rx_cmd_ret_le32));
}

static int es58x_fd_tx_ack_msg(struct net_device *netdev,
			       const struct es58x_fd_urb_cmd *es58x_fd_urb_cmd)
{
	struct es58x_device *es58x_dev = es58x_priv(netdev)->es58x_dev;
	const struct es58x_fd_tx_ack_msg *tx_ack_msg;
	u16 msg_len = get_unaligned_le16(&es58x_fd_urb_cmd->msg_len);
	int ret;

	tx_ack_msg = &es58x_fd_urb_cmd->tx_ack_msg;
	ret = es58x_check_msg_len(es58x_dev->dev, *tx_ack_msg, msg_len);
	if (ret)
		return ret;

	return es58x_tx_ack_msg(netdev,
				get_unaligned_le16(&tx_ack_msg->tx_free_entries),
				get_unaligned_le32(&tx_ack_msg->rx_cmd_ret_le32));
}

static int es58x_fd_can_cmd_id(struct es58x_device *es58x_dev,
			       const struct es58x_fd_urb_cmd *es58x_fd_urb_cmd)
{
	struct net_device *netdev;
	int ret;

	ret = es58x_get_netdev(es58x_dev, es58x_fd_urb_cmd->channel_idx,
			       ES58X_FD_CHANNEL_IDX_OFFSET, &netdev);
	if (ret)
		return ret;

	switch ((enum es58x_fd_can_cmd_id)es58x_fd_urb_cmd->cmd_id) {
	case ES58X_FD_CAN_CMD_ID_ENABLE_CHANNEL:
		return es58x_fd_rx_cmd_ret_u32(netdev, es58x_fd_urb_cmd,
					       ES58X_RET_TYPE_ENABLE_CHANNEL);

	case ES58X_FD_CAN_CMD_ID_DISABLE_CHANNEL:
		return es58x_fd_rx_cmd_ret_u32(netdev, es58x_fd_urb_cmd,
					       ES58X_RET_TYPE_DISABLE_CHANNEL);

	case ES58X_FD_CAN_CMD_ID_TX_MSG:
		return es58x_fd_tx_ack_msg(netdev, es58x_fd_urb_cmd);

	case ES58X_FD_CAN_CMD_ID_ECHO_MSG:
		return es58x_fd_echo_msg(netdev, es58x_fd_urb_cmd);

	case ES58X_FD_CAN_CMD_ID_RX_MSG:
		return es58x_fd_rx_can_msg(netdev, es58x_fd_urb_cmd);

	case ES58X_FD_CAN_CMD_ID_RESET_RX:
		return es58x_fd_rx_cmd_ret_u32(netdev, es58x_fd_urb_cmd,
					       ES58X_RET_TYPE_RESET_RX);

	case ES58X_FD_CAN_CMD_ID_RESET_TX:
		return es58x_fd_rx_cmd_ret_u32(netdev, es58x_fd_urb_cmd,
					       ES58X_RET_TYPE_RESET_TX);

	case ES58X_FD_CAN_CMD_ID_ERROR_OR_EVENT_MSG:
		return es58x_fd_rx_event_msg(netdev, es58x_fd_urb_cmd);

	default:
		return -EBADRQC;
	}
}

static int es58x_fd_device_cmd_id(struct es58x_device *es58x_dev,
				  const struct es58x_fd_urb_cmd *es58x_fd_urb_cmd)
{
	u16 msg_len = get_unaligned_le16(&es58x_fd_urb_cmd->msg_len);
	int ret;

	switch ((enum es58x_fd_dev_cmd_id)es58x_fd_urb_cmd->cmd_id) {
	case ES58X_FD_DEV_CMD_ID_TIMESTAMP:
		ret = es58x_check_msg_len(es58x_dev->dev,
					  es58x_fd_urb_cmd->timestamp, msg_len);
		if (ret)
			return ret;
		es58x_rx_timestamp(es58x_dev,
				   get_unaligned_le64(&es58x_fd_urb_cmd->timestamp));
		return 0;

	default:
		return -EBADRQC;
	}
}

static int es58x_fd_handle_urb_cmd(struct es58x_device *es58x_dev,
				   const union es58x_urb_cmd *urb_cmd)
{
	const struct es58x_fd_urb_cmd *es58x_fd_urb_cmd;
	int ret;

	es58x_fd_urb_cmd = &urb_cmd->es58x_fd_urb_cmd;

	switch ((enum es58x_fd_cmd_type)es58x_fd_urb_cmd->cmd_type) {
	case ES58X_FD_CMD_TYPE_CAN:
	case ES58X_FD_CMD_TYPE_CANFD:
		ret = es58x_fd_can_cmd_id(es58x_dev, es58x_fd_urb_cmd);
		break;

	case ES58X_FD_CMD_TYPE_DEVICE:
		ret = es58x_fd_device_cmd_id(es58x_dev, es58x_fd_urb_cmd);
		break;

	default:
		ret = -EBADRQC;
		break;
	}

	if (ret == -EBADRQC)
		dev_err(es58x_dev->dev,
			"%s: Unknown command type (0x%02X) and command ID (0x%02X) combination\n",
			__func__, es58x_fd_urb_cmd->cmd_type,
			es58x_fd_urb_cmd->cmd_id);

	return ret;
}

static void es58x_fd_fill_urb_header(union es58x_urb_cmd *urb_cmd, u8 cmd_type,
				     u8 cmd_id, u8 channel_idx, u16 msg_len)
{
	struct es58x_fd_urb_cmd *es58x_fd_urb_cmd = &urb_cmd->es58x_fd_urb_cmd;

	es58x_fd_urb_cmd->SOF = cpu_to_le16(es58x_fd_param.tx_start_of_frame);
	es58x_fd_urb_cmd->cmd_type = cmd_type;
	es58x_fd_urb_cmd->cmd_id = cmd_id;
	es58x_fd_urb_cmd->channel_idx = channel_idx;
	es58x_fd_urb_cmd->msg_len = cpu_to_le16(msg_len);
}

static int es58x_fd_tx_can_msg(struct es58x_priv *priv,
			       const struct sk_buff *skb)
{
	struct es58x_device *es58x_dev = priv->es58x_dev;
	union es58x_urb_cmd *urb_cmd = priv->tx_urb->transfer_buffer;
	struct es58x_fd_urb_cmd *es58x_fd_urb_cmd = &urb_cmd->es58x_fd_urb_cmd;
	struct can_frame *cf = (struct can_frame *)skb->data;
	struct es58x_fd_tx_can_msg *tx_can_msg;
	bool is_fd = can_is_canfd_skb(skb);
	u16 msg_len;
	int ret;

	if (priv->tx_can_msg_cnt == 0) {
		msg_len = 0;
		es58x_fd_fill_urb_header(urb_cmd,
					 is_fd ? ES58X_FD_CMD_TYPE_CANFD
					       : ES58X_FD_CMD_TYPE_CAN,
					 ES58X_FD_CAN_CMD_ID_TX_MSG_NO_ACK,
					 priv->channel_idx, msg_len);
	} else {
		msg_len = es58x_fd_get_msg_len(urb_cmd);
	}

	ret = es58x_check_msg_max_len(es58x_dev->dev,
				      es58x_fd_urb_cmd->tx_can_msg_buf,
				      msg_len + sizeof(*tx_can_msg));
	if (ret)
		return ret;

	/* Fill message contents. */
	tx_can_msg = (typeof(tx_can_msg))&es58x_fd_urb_cmd->raw_msg[msg_len];
	tx_can_msg->packet_idx = (u8)priv->tx_head;
	put_unaligned_le32(es58x_get_raw_can_id(cf), &tx_can_msg->can_id);
	tx_can_msg->flags = (u8)es58x_get_flags(skb);
	if (is_fd)
		tx_can_msg->len = cf->len;
	else
		tx_can_msg->dlc = can_get_cc_dlc(cf, priv->can.ctrlmode);
	memcpy(tx_can_msg->data, cf->data, cf->len);

	/* Calculate new sizes */
	msg_len += es58x_fd_sizeof_rx_tx_msg(*tx_can_msg);
	priv->tx_urb->transfer_buffer_length = es58x_get_urb_cmd_len(es58x_dev,
								     msg_len);
	put_unaligned_le16(msg_len, &es58x_fd_urb_cmd->msg_len);

	return 0;
}

static void es58x_fd_convert_bittiming(struct es58x_fd_bittiming *es58x_fd_bt,
				       struct can_bittiming *bt)
{
	/* The actual value set in the hardware registers is one less
	 * than the functional value.
	 */
	const int offset = 1;

	es58x_fd_bt->bitrate = cpu_to_le32(bt->bitrate);
	es58x_fd_bt->tseg1 =
	    cpu_to_le16(bt->prop_seg + bt->phase_seg1 - offset);
	es58x_fd_bt->tseg2 = cpu_to_le16(bt->phase_seg2 - offset);
	es58x_fd_bt->brp = cpu_to_le16(bt->brp - offset);
	es58x_fd_bt->sjw = cpu_to_le16(bt->sjw - offset);
}

static int es58x_fd_enable_channel(struct es58x_priv *priv)
{
	struct es58x_device *es58x_dev = priv->es58x_dev;
	struct net_device *netdev = es58x_dev->netdev[priv->channel_idx];
	struct es58x_fd_tx_conf_msg tx_conf_msg = { 0 };
	u32 ctrlmode;
	size_t conf_len = 0;

	es58x_fd_convert_bittiming(&tx_conf_msg.nominal_bittiming,
				   &priv->can.bittiming);
	ctrlmode = priv->can.ctrlmode;

	if (ctrlmode & CAN_CTRLMODE_3_SAMPLES)
		tx_conf_msg.samples_per_bit = ES58X_SAMPLES_PER_BIT_THREE;
	else
		tx_conf_msg.samples_per_bit = ES58X_SAMPLES_PER_BIT_ONE;
	tx_conf_msg.sync_edge = ES58X_SYNC_EDGE_SINGLE;
	tx_conf_msg.physical_layer = ES58X_PHYSICAL_LAYER_HIGH_SPEED;
	tx_conf_msg.echo_mode = ES58X_ECHO_ON;
	if (ctrlmode & CAN_CTRLMODE_LISTENONLY)
		tx_conf_msg.ctrlmode |= ES58X_FD_CTRLMODE_PASSIVE;
	else
		tx_conf_msg.ctrlmode |=  ES58X_FD_CTRLMODE_ACTIVE;

	if (ctrlmode & CAN_CTRLMODE_FD_NON_ISO) {
		tx_conf_msg.ctrlmode |= ES58X_FD_CTRLMODE_FD_NON_ISO;
		tx_conf_msg.canfd_enabled = 1;
	} else if (ctrlmode & CAN_CTRLMODE_FD) {
		tx_conf_msg.ctrlmode |= ES58X_FD_CTRLMODE_FD;
		tx_conf_msg.canfd_enabled = 1;
	}

	if (tx_conf_msg.canfd_enabled) {
		es58x_fd_convert_bittiming(&tx_conf_msg.data_bittiming,
					   &priv->can.data_bittiming);

		if (can_tdc_is_enabled(&priv->can)) {
			tx_conf_msg.tdc_enabled = 1;
			tx_conf_msg.tdco = cpu_to_le16(priv->can.tdc.tdco);
			tx_conf_msg.tdcf = cpu_to_le16(priv->can.tdc.tdcf);
		}

		conf_len = ES58X_FD_CANFD_CONF_LEN;
	} else {
		conf_len = ES58X_FD_CAN_CONF_LEN;
	}

	return es58x_send_msg(es58x_dev, es58x_fd_cmd_type(netdev),
			      ES58X_FD_CAN_CMD_ID_ENABLE_CHANNEL,
			      &tx_conf_msg, conf_len, priv->channel_idx);
}

static int es58x_fd_disable_channel(struct es58x_priv *priv)
{
	/* The type (ES58X_FD_CMD_TYPE_CAN or ES58X_FD_CMD_TYPE_CANFD) does
	 * not matter here.
	 */
	return es58x_send_msg(priv->es58x_dev, ES58X_FD_CMD_TYPE_CAN,
			      ES58X_FD_CAN_CMD_ID_DISABLE_CHANNEL,
			      ES58X_EMPTY_MSG, 0, priv->channel_idx);
}

static int es58x_fd_get_timestamp(struct es58x_device *es58x_dev)
{
	return es58x_send_msg(es58x_dev, ES58X_FD_CMD_TYPE_DEVICE,
			      ES58X_FD_DEV_CMD_ID_TIMESTAMP, ES58X_EMPTY_MSG,
			      0, ES58X_CHANNEL_IDX_NA);
}

/* Nominal bittiming constants for ES582.1 and ES584.1 as specified in
 * the microcontroller datasheet: "SAM E70/S70/V70/V71 Family" section
 * 49.6.8 "MCAN Nominal Bit Timing and Prescaler Register" from
 * Microchip.
 *
 * The values from the specification are the hardware register
 * values. To convert them to the functional values, all ranges were
 * incremented by 1 (e.g. range [0..n-1] changed to [1..n]).
 */
static const struct can_bittiming_const es58x_fd_nom_bittiming_const = {
	.name = "ES582.1/ES584.1",
	.tseg1_min = 2,
	.tseg1_max = 256,
	.tseg2_min = 2,
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 1,
	.brp_max = 512,
	.brp_inc = 1
};

/* Data bittiming constants for ES582.1 and ES584.1 as specified in
 * the microcontroller datasheet: "SAM E70/S70/V70/V71 Family" section
 * 49.6.4 "MCAN Data Bit Timing and Prescaler Register" from
 * Microchip.
 */
static const struct can_bittiming_const es58x_fd_data_bittiming_const = {
	.name = "ES582.1/ES584.1",
	.tseg1_min = 2,
	.tseg1_max = 32,
	.tseg2_min = 1,
	.tseg2_max = 16,
	.sjw_max = 8,
	.brp_min = 1,
	.brp_max = 32,
	.brp_inc = 1
};

/* Transmission Delay Compensation constants for ES582.1 and ES584.1
 * as specified in the microcontroller datasheet: "SAM E70/S70/V70/V71
 * Family" section 49.6.15 "MCAN Transmitter Delay Compensation
 * Register" from Microchip.
 */
static const struct can_tdc_const es58x_tdc_const = {
	.tdcv_min = 0,
	.tdcv_max = 0, /* Manual mode not supported. */
	.tdco_min = 0,
	.tdco_max = 127,
	.tdcf_min = 0,
	.tdcf_max = 127
};

const struct es58x_parameters es58x_fd_param = {
	.bittiming_const = &es58x_fd_nom_bittiming_const,
	.data_bittiming_const = &es58x_fd_data_bittiming_const,
	.tdc_const = &es58x_tdc_const,
	/* The devices use NXP TJA1044G transievers which guarantee
	 * the timing for data rates up to 5 Mbps. Bitrates up to 8
	 * Mbps work in an optimal environment but are not recommended
	 * for production environment.
	 */
	.bitrate_max = 8 * MEGA /* BPS */,
	.clock = {.freq = 80 * MEGA /* Hz */},
	.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK | CAN_CTRLMODE_LISTENONLY |
	    CAN_CTRLMODE_3_SAMPLES | CAN_CTRLMODE_FD | CAN_CTRLMODE_FD_NON_ISO |
	    CAN_CTRLMODE_CC_LEN8_DLC | CAN_CTRLMODE_TDC_AUTO,
	.tx_start_of_frame = 0xCEFA,	/* FACE in little endian */
	.rx_start_of_frame = 0xFECA,	/* CAFE in little endian */
	.tx_urb_cmd_max_len = ES58X_FD_TX_URB_CMD_MAX_LEN,
	.rx_urb_cmd_max_len = ES58X_FD_RX_URB_CMD_MAX_LEN,
	/* Size of internal device TX queue is 500.
	 *
	 * However, when reaching value around 278, the device's busy
	 * LED turns on and thus maximum value of 500 is never reached
	 * in practice. Also, when this value is too high, some error
	 * on the echo_msg were witnessed when the device is
	 * recovering from bus off.
	 *
	 * For above reasons, a value that would prevent the device
	 * from becoming busy was chosen. In practice, BQL would
	 * prevent the value from even getting closer to below
	 * maximum, so no impact on performance was measured.
	 */
	.fifo_mask = 255, /* echo_skb_max = 256 */
	.dql_min_limit = CAN_FRAME_LEN_MAX * 15, /* Empirical value. */
	.tx_bulk_max = ES58X_FD_TX_BULK_MAX,
	.urb_cmd_header_len = ES58X_FD_URB_CMD_HEADER_LEN,
	.rx_urb_max = ES58X_RX_URBS_MAX,
	.tx_urb_max = ES58X_TX_URBS_MAX
};

const struct es58x_operators es58x_fd_ops = {
	.get_msg_len = es58x_fd_get_msg_len,
	.handle_urb_cmd = es58x_fd_handle_urb_cmd,
	.fill_urb_header = es58x_fd_fill_urb_header,
	.tx_can_msg = es58x_fd_tx_can_msg,
	.enable_channel = es58x_fd_enable_channel,
	.disable_channel = es58x_fd_disable_channel,
	.reset_device = NULL, /* Not implemented in the device firmware. */
	.get_timestamp = es58x_fd_get_timestamp
};
