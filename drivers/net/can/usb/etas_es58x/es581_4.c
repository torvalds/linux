// SPDX-License-Identifier: GPL-2.0

/* Driver for ETAS GmbH ES58X USB CAN(-FD) Bus Interfaces.
 *
 * File es581_4.c: Adds support to ETAS ES581.4.
 *
 * Copyright (c) 2019 Robert Bosch Engineering and Business Solutions. All rights reserved.
 * Copyright (c) 2020 ETAS K.K.. All rights reserved.
 * Copyright (c) 2020, 2021 Vincent Mailhol <mailhol.vincent@wanadoo.fr>
 */

#include <linux/kernel.h>
#include <asm/unaligned.h>

#include "es58x_core.h"
#include "es581_4.h"

/**
 * es581_4_sizeof_rx_tx_msg() - Calculate the actual length of the
 *	structure of a rx or tx message.
 * @msg: message of variable length, must have a dlc field.
 *
 * Even if RTR frames have actually no payload, the ES58X devices
 * still expect it. Must be a macro in order to accept several types
 * (struct es581_4_tx_can_msg and struct es581_4_rx_can_msg) as an
 * input.
 *
 * Return: length of the message.
 */
#define es581_4_sizeof_rx_tx_msg(msg)				\
	offsetof(typeof(msg), data[can_cc_dlc2len((msg).dlc)])

static u16 es581_4_get_msg_len(const union es58x_urb_cmd *urb_cmd)
{
	return get_unaligned_le16(&urb_cmd->es581_4_urb_cmd.msg_len);
}

static int es581_4_echo_msg(struct es58x_device *es58x_dev,
			    const struct es581_4_urb_cmd *es581_4_urb_cmd)
{
	struct net_device *netdev;
	const struct es581_4_bulk_echo_msg *bulk_echo_msg;
	const struct es581_4_echo_msg *echo_msg;
	u64 *tstamps = es58x_dev->timestamps;
	u16 msg_len;
	u32 first_packet_idx, packet_idx;
	unsigned int dropped = 0;
	int i, num_element, ret;

	bulk_echo_msg = &es581_4_urb_cmd->bulk_echo_msg;
	msg_len = get_unaligned_le16(&es581_4_urb_cmd->msg_len) -
	    sizeof(bulk_echo_msg->channel_no);
	num_element = es58x_msg_num_element(es58x_dev->dev,
					    bulk_echo_msg->echo_msg, msg_len);
	if (num_element <= 0)
		return num_element;

	ret = es58x_get_netdev(es58x_dev, bulk_echo_msg->channel_no,
			       ES581_4_CHANNEL_IDX_OFFSET, &netdev);
	if (ret)
		return ret;

	echo_msg = &bulk_echo_msg->echo_msg[0];
	first_packet_idx = get_unaligned_le32(&echo_msg->packet_idx);
	packet_idx = first_packet_idx;
	for (i = 0; i < num_element; i++) {
		u32 tmp_idx;

		echo_msg = &bulk_echo_msg->echo_msg[i];
		tmp_idx = get_unaligned_le32(&echo_msg->packet_idx);
		if (tmp_idx == packet_idx - 1) {
			if (net_ratelimit())
				netdev_warn(netdev,
					    "Received echo packet idx %u twice\n",
					    packet_idx - 1);
			dropped++;
			continue;
		}
		if (tmp_idx != packet_idx) {
			netdev_err(netdev, "Echo packet idx jumped from %u to %u\n",
				   packet_idx - 1, echo_msg->packet_idx);
			return -EBADMSG;
		}

		tstamps[i] = get_unaligned_le64(&echo_msg->timestamp);
		packet_idx++;
	}

	netdev->stats.tx_dropped += dropped;
	return es58x_can_get_echo_skb(netdev, first_packet_idx,
				      tstamps, num_element - dropped);
}

static int es581_4_rx_can_msg(struct es58x_device *es58x_dev,
			      const struct es581_4_urb_cmd *es581_4_urb_cmd,
			      u16 msg_len)
{
	const struct device *dev = es58x_dev->dev;
	struct net_device *netdev;
	int pkts, num_element, channel_no, ret;

	num_element = es58x_msg_num_element(dev, es581_4_urb_cmd->rx_can_msg,
					    msg_len);
	if (num_element <= 0)
		return num_element;

	channel_no = es581_4_urb_cmd->rx_can_msg[0].channel_no;
	ret = es58x_get_netdev(es58x_dev, channel_no,
			       ES581_4_CHANNEL_IDX_OFFSET, &netdev);
	if (ret)
		return ret;

	if (!netif_running(netdev)) {
		if (net_ratelimit())
			netdev_info(netdev,
				    "%s: %s is down, dropping %d rx packets\n",
				    __func__, netdev->name, num_element);
		netdev->stats.rx_dropped += num_element;
		return 0;
	}

	for (pkts = 0; pkts < num_element; pkts++) {
		const struct es581_4_rx_can_msg *rx_can_msg =
		    &es581_4_urb_cmd->rx_can_msg[pkts];
		u64 tstamp = get_unaligned_le64(&rx_can_msg->timestamp);
		canid_t can_id = get_unaligned_le32(&rx_can_msg->can_id);

		if (channel_no != rx_can_msg->channel_no)
			return -EBADMSG;

		ret = es58x_rx_can_msg(netdev, tstamp, rx_can_msg->data,
				       can_id, rx_can_msg->flags,
				       rx_can_msg->dlc);
		if (ret)
			break;
	}

	return ret;
}

static int es581_4_rx_err_msg(struct es58x_device *es58x_dev,
			      const struct es581_4_rx_err_msg *rx_err_msg)
{
	struct net_device *netdev;
	enum es58x_err error = get_unaligned_le32(&rx_err_msg->error);
	int ret;

	ret = es58x_get_netdev(es58x_dev, rx_err_msg->channel_no,
			       ES581_4_CHANNEL_IDX_OFFSET, &netdev);
	if (ret)
		return ret;

	return es58x_rx_err_msg(netdev, error, 0,
				get_unaligned_le64(&rx_err_msg->timestamp));
}

static int es581_4_rx_event_msg(struct es58x_device *es58x_dev,
				const struct es581_4_rx_event_msg *rx_event_msg)
{
	struct net_device *netdev;
	enum es58x_event event = get_unaligned_le32(&rx_event_msg->event);
	int ret;

	ret = es58x_get_netdev(es58x_dev, rx_event_msg->channel_no,
			       ES581_4_CHANNEL_IDX_OFFSET, &netdev);
	if (ret)
		return ret;

	return es58x_rx_err_msg(netdev, 0, event,
				get_unaligned_le64(&rx_event_msg->timestamp));
}

static int es581_4_rx_cmd_ret_u32(struct es58x_device *es58x_dev,
				  const struct es581_4_urb_cmd *es581_4_urb_cmd,
				  enum es58x_ret_type ret_type)
{
	struct net_device *netdev;
	const struct es581_4_rx_cmd_ret *rx_cmd_ret;
	u16 msg_len = get_unaligned_le16(&es581_4_urb_cmd->msg_len);
	int ret;

	ret = es58x_check_msg_len(es58x_dev->dev,
				  es581_4_urb_cmd->rx_cmd_ret, msg_len);
	if (ret)
		return ret;

	rx_cmd_ret = &es581_4_urb_cmd->rx_cmd_ret;

	ret = es58x_get_netdev(es58x_dev, rx_cmd_ret->channel_no,
			       ES581_4_CHANNEL_IDX_OFFSET, &netdev);
	if (ret)
		return ret;

	return es58x_rx_cmd_ret_u32(netdev, ret_type,
				    get_unaligned_le32(&rx_cmd_ret->rx_cmd_ret_le32));
}

static int es581_4_tx_ack_msg(struct es58x_device *es58x_dev,
			      const struct es581_4_urb_cmd *es581_4_urb_cmd)
{
	struct net_device *netdev;
	const struct es581_4_tx_ack_msg *tx_ack_msg;
	u16 msg_len = get_unaligned_le16(&es581_4_urb_cmd->msg_len);
	int ret;

	tx_ack_msg = &es581_4_urb_cmd->tx_ack_msg;
	ret = es58x_check_msg_len(es58x_dev->dev, *tx_ack_msg, msg_len);
	if (ret)
		return ret;

	if (tx_ack_msg->rx_cmd_ret_u8 != ES58X_RET_U8_OK)
		return es58x_rx_cmd_ret_u8(es58x_dev->dev,
					   ES58X_RET_TYPE_TX_MSG,
					   tx_ack_msg->rx_cmd_ret_u8);

	ret = es58x_get_netdev(es58x_dev, tx_ack_msg->channel_no,
			       ES581_4_CHANNEL_IDX_OFFSET, &netdev);
	if (ret)
		return ret;

	return es58x_tx_ack_msg(netdev,
				get_unaligned_le16(&tx_ack_msg->tx_free_entries),
				ES58X_RET_U32_OK);
}

static int es581_4_dispatch_rx_cmd(struct es58x_device *es58x_dev,
				   const struct es581_4_urb_cmd *es581_4_urb_cmd)
{
	const struct device *dev = es58x_dev->dev;
	u16 msg_len = get_unaligned_le16(&es581_4_urb_cmd->msg_len);
	enum es581_4_rx_type rx_type = es581_4_urb_cmd->rx_can_msg[0].rx_type;
	int ret = 0;

	switch (rx_type) {
	case ES581_4_RX_TYPE_MESSAGE:
		return es581_4_rx_can_msg(es58x_dev, es581_4_urb_cmd, msg_len);

	case ES581_4_RX_TYPE_ERROR:
		ret = es58x_check_msg_len(dev, es581_4_urb_cmd->rx_err_msg,
					  msg_len);
		if (ret < 0)
			return ret;
		return es581_4_rx_err_msg(es58x_dev,
					  &es581_4_urb_cmd->rx_err_msg);

	case ES581_4_RX_TYPE_EVENT:
		ret = es58x_check_msg_len(dev, es581_4_urb_cmd->rx_event_msg,
					  msg_len);
		if (ret < 0)
			return ret;
		return es581_4_rx_event_msg(es58x_dev,
					    &es581_4_urb_cmd->rx_event_msg);

	default:
		dev_err(dev, "%s: Unknown rx_type 0x%02X\n", __func__, rx_type);
		return -EBADRQC;
	}
}

static int es581_4_handle_urb_cmd(struct es58x_device *es58x_dev,
				  const union es58x_urb_cmd *urb_cmd)
{
	const struct es581_4_urb_cmd *es581_4_urb_cmd;
	struct device *dev = es58x_dev->dev;
	u16 msg_len = es581_4_get_msg_len(urb_cmd);
	int ret;

	es581_4_urb_cmd = &urb_cmd->es581_4_urb_cmd;

	if (es581_4_urb_cmd->cmd_type != ES581_4_CAN_COMMAND_TYPE) {
		dev_err(dev, "%s: Unknown command type (0x%02X)\n",
			__func__, es581_4_urb_cmd->cmd_type);
		return -EBADRQC;
	}

	switch ((enum es581_4_cmd_id)es581_4_urb_cmd->cmd_id) {
	case ES581_4_CMD_ID_SET_BITTIMING:
		return es581_4_rx_cmd_ret_u32(es58x_dev, es581_4_urb_cmd,
					      ES58X_RET_TYPE_SET_BITTIMING);

	case ES581_4_CMD_ID_ENABLE_CHANNEL:
		return es581_4_rx_cmd_ret_u32(es58x_dev, es581_4_urb_cmd,
					      ES58X_RET_TYPE_ENABLE_CHANNEL);

	case ES581_4_CMD_ID_TX_MSG:
		return es581_4_tx_ack_msg(es58x_dev, es581_4_urb_cmd);

	case ES581_4_CMD_ID_RX_MSG:
		return es581_4_dispatch_rx_cmd(es58x_dev, es581_4_urb_cmd);

	case ES581_4_CMD_ID_RESET_RX:
		ret = es581_4_rx_cmd_ret_u32(es58x_dev, es581_4_urb_cmd,
					     ES58X_RET_TYPE_RESET_RX);
		return ret;

	case ES581_4_CMD_ID_RESET_TX:
		ret = es581_4_rx_cmd_ret_u32(es58x_dev, es581_4_urb_cmd,
					     ES58X_RET_TYPE_RESET_TX);
		return ret;

	case ES581_4_CMD_ID_DISABLE_CHANNEL:
		return es581_4_rx_cmd_ret_u32(es58x_dev, es581_4_urb_cmd,
					      ES58X_RET_TYPE_DISABLE_CHANNEL);

	case ES581_4_CMD_ID_TIMESTAMP:
		ret = es58x_check_msg_len(dev, es581_4_urb_cmd->timestamp,
					  msg_len);
		if (ret < 0)
			return ret;
		es58x_rx_timestamp(es58x_dev,
				   get_unaligned_le64(&es581_4_urb_cmd->timestamp));
		return 0;

	case ES581_4_CMD_ID_ECHO:
		return es581_4_echo_msg(es58x_dev, es581_4_urb_cmd);

	case ES581_4_CMD_ID_DEVICE_ERR:
		ret = es58x_check_msg_len(dev, es581_4_urb_cmd->rx_cmd_ret_u8,
					  msg_len);
		if (ret)
			return ret;
		return es58x_rx_cmd_ret_u8(dev, ES58X_RET_TYPE_DEVICE_ERR,
					   es581_4_urb_cmd->rx_cmd_ret_u8);

	default:
		dev_warn(dev, "%s: Unexpected command ID: 0x%02X\n",
			 __func__, es581_4_urb_cmd->cmd_id);
		return -EBADRQC;
	}
}

static void es581_4_fill_urb_header(union es58x_urb_cmd *urb_cmd, u8 cmd_type,
				    u8 cmd_id, u8 channel_idx, u16 msg_len)
{
	struct es581_4_urb_cmd *es581_4_urb_cmd = &urb_cmd->es581_4_urb_cmd;

	es581_4_urb_cmd->SOF = cpu_to_le16(es581_4_param.tx_start_of_frame);
	es581_4_urb_cmd->cmd_type = cmd_type;
	es581_4_urb_cmd->cmd_id = cmd_id;
	es581_4_urb_cmd->msg_len = cpu_to_le16(msg_len);
}

static int es581_4_tx_can_msg(struct es58x_priv *priv,
			      const struct sk_buff *skb)
{
	struct es58x_device *es58x_dev = priv->es58x_dev;
	union es58x_urb_cmd *urb_cmd = priv->tx_urb->transfer_buffer;
	struct es581_4_urb_cmd *es581_4_urb_cmd = &urb_cmd->es581_4_urb_cmd;
	struct can_frame *cf = (struct can_frame *)skb->data;
	struct es581_4_tx_can_msg *tx_can_msg;
	u16 msg_len;
	int ret;

	if (can_is_canfd_skb(skb))
		return -EMSGSIZE;

	if (priv->tx_can_msg_cnt == 0) {
		msg_len = sizeof(es581_4_urb_cmd->bulk_tx_can_msg.num_can_msg);
		es581_4_fill_urb_header(urb_cmd, ES581_4_CAN_COMMAND_TYPE,
					ES581_4_CMD_ID_TX_MSG,
					priv->channel_idx, msg_len);
		es581_4_urb_cmd->bulk_tx_can_msg.num_can_msg = 0;
	} else {
		msg_len = es581_4_get_msg_len(urb_cmd);
	}

	ret = es58x_check_msg_max_len(es58x_dev->dev,
				      es581_4_urb_cmd->bulk_tx_can_msg,
				      msg_len + sizeof(*tx_can_msg));
	if (ret)
		return ret;

	/* Fill message contents. */
	tx_can_msg = (typeof(tx_can_msg))&es581_4_urb_cmd->raw_msg[msg_len];
	put_unaligned_le32(es58x_get_raw_can_id(cf), &tx_can_msg->can_id);
	put_unaligned_le32(priv->tx_head, &tx_can_msg->packet_idx);
	put_unaligned_le16((u16)es58x_get_flags(skb), &tx_can_msg->flags);
	tx_can_msg->channel_no = priv->channel_idx + ES581_4_CHANNEL_IDX_OFFSET;
	tx_can_msg->dlc = can_get_cc_dlc(cf, priv->can.ctrlmode);

	memcpy(tx_can_msg->data, cf->data, cf->len);

	/* Calculate new sizes. */
	es581_4_urb_cmd->bulk_tx_can_msg.num_can_msg++;
	msg_len += es581_4_sizeof_rx_tx_msg(*tx_can_msg);
	priv->tx_urb->transfer_buffer_length = es58x_get_urb_cmd_len(es58x_dev,
								     msg_len);
	es581_4_urb_cmd->msg_len = cpu_to_le16(msg_len);

	return 0;
}

static int es581_4_set_bittiming(struct es58x_priv *priv)
{
	struct es581_4_tx_conf_msg tx_conf_msg = { 0 };
	struct can_bittiming *bt = &priv->can.bittiming;

	tx_conf_msg.bitrate = cpu_to_le32(bt->bitrate);
	/* bt->sample_point is in tenth of percent. Convert it to percent. */
	tx_conf_msg.sample_point = cpu_to_le32(bt->sample_point / 10U);
	tx_conf_msg.samples_per_bit = cpu_to_le32(ES58X_SAMPLES_PER_BIT_ONE);
	tx_conf_msg.bit_time = cpu_to_le32(can_bit_time(bt));
	tx_conf_msg.sjw = cpu_to_le32(bt->sjw);
	tx_conf_msg.sync_edge = cpu_to_le32(ES58X_SYNC_EDGE_SINGLE);
	tx_conf_msg.physical_layer =
	    cpu_to_le32(ES58X_PHYSICAL_LAYER_HIGH_SPEED);
	tx_conf_msg.echo_mode = cpu_to_le32(ES58X_ECHO_ON);
	tx_conf_msg.channel_no = priv->channel_idx + ES581_4_CHANNEL_IDX_OFFSET;

	return es58x_send_msg(priv->es58x_dev, ES581_4_CAN_COMMAND_TYPE,
			      ES581_4_CMD_ID_SET_BITTIMING, &tx_conf_msg,
			      sizeof(tx_conf_msg), priv->channel_idx);
}

static int es581_4_enable_channel(struct es58x_priv *priv)
{
	int ret;
	u8 msg = priv->channel_idx + ES581_4_CHANNEL_IDX_OFFSET;

	ret = es581_4_set_bittiming(priv);
	if (ret)
		return ret;

	return es58x_send_msg(priv->es58x_dev, ES581_4_CAN_COMMAND_TYPE,
			      ES581_4_CMD_ID_ENABLE_CHANNEL, &msg, sizeof(msg),
			      priv->channel_idx);
}

static int es581_4_disable_channel(struct es58x_priv *priv)
{
	u8 msg = priv->channel_idx + ES581_4_CHANNEL_IDX_OFFSET;

	return es58x_send_msg(priv->es58x_dev, ES581_4_CAN_COMMAND_TYPE,
			      ES581_4_CMD_ID_DISABLE_CHANNEL, &msg, sizeof(msg),
			      priv->channel_idx);
}

static int es581_4_reset_device(struct es58x_device *es58x_dev)
{
	return es58x_send_msg(es58x_dev, ES581_4_CAN_COMMAND_TYPE,
			      ES581_4_CMD_ID_RESET_DEVICE,
			      ES58X_EMPTY_MSG, 0, ES58X_CHANNEL_IDX_NA);
}

static int es581_4_get_timestamp(struct es58x_device *es58x_dev)
{
	return es58x_send_msg(es58x_dev, ES581_4_CAN_COMMAND_TYPE,
			      ES581_4_CMD_ID_TIMESTAMP,
			      ES58X_EMPTY_MSG, 0, ES58X_CHANNEL_IDX_NA);
}

/* Nominal bittiming constants for ES581.4 as specified in the
 * microcontroller datasheet: "Stellaris(R) LM3S5B91 Microcontroller"
 * table 17-4 "CAN Protocol Ranges" from Texas Instruments.
 */
static const struct can_bittiming_const es581_4_bittiming_const = {
	.name = "ES581.4",
	.tseg1_min = 1,
	.tseg1_max = 8,
	.tseg2_min = 1,
	.tseg2_max = 8,
	.sjw_max = 4,
	.brp_min = 1,
	.brp_max = 128,
	.brp_inc = 1
};

const struct es58x_parameters es581_4_param = {
	.bittiming_const = &es581_4_bittiming_const,
	.data_bittiming_const = NULL,
	.tdc_const = NULL,
	.bitrate_max = 1 * CAN_MBPS,
	.clock = {.freq = 50 * CAN_MHZ},
	.ctrlmode_supported = CAN_CTRLMODE_CC_LEN8_DLC,
	.tx_start_of_frame = 0xAFAF,
	.rx_start_of_frame = 0xFAFA,
	.tx_urb_cmd_max_len = ES581_4_TX_URB_CMD_MAX_LEN,
	.rx_urb_cmd_max_len = ES581_4_RX_URB_CMD_MAX_LEN,
	/* Size of internal device TX queue is 330.
	 *
	 * However, we witnessed some ES58X_ERR_PROT_CRC errors from
	 * the device and thus, echo_skb_max was lowered to the
	 * empirical value of 75 which seems stable and then rounded
	 * down to become a power of two.
	 *
	 * Root cause of those ES58X_ERR_PROT_CRC errors is still
	 * unclear.
	 */
	.fifo_mask = 63, /* echo_skb_max = 64 */
	.dql_min_limit = CAN_FRAME_LEN_MAX * 50, /* Empirical value. */
	.tx_bulk_max = ES581_4_TX_BULK_MAX,
	.urb_cmd_header_len = ES581_4_URB_CMD_HEADER_LEN,
	.rx_urb_max = ES58X_RX_URBS_MAX,
	.tx_urb_max = ES58X_TX_URBS_MAX
};

const struct es58x_operators es581_4_ops = {
	.get_msg_len = es581_4_get_msg_len,
	.handle_urb_cmd = es581_4_handle_urb_cmd,
	.fill_urb_header = es581_4_fill_urb_header,
	.tx_can_msg = es581_4_tx_can_msg,
	.enable_channel = es581_4_enable_channel,
	.disable_channel = es581_4_disable_channel,
	.reset_device = es581_4_reset_device,
	.get_timestamp = es581_4_get_timestamp
};
