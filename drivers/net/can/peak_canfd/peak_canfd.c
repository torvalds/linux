/*
 * Copyright (C) 2007, 2011 Wolfgang Grandegger <wg@grandegger.com>
 * Copyright (C) 2012 Stephane Grosjean <s.grosjean@peak-system.com>
 *
 * Copyright (C) 2016  PEAK System-Technik GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the version 2 of the GNU General Public License
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/can.h>
#include <linux/can/dev.h>

#include "peak_canfd_user.h"

/* internal IP core cache size (used as default echo skbs max number) */
#define PCANFD_ECHO_SKB_MAX		24

/* bittiming ranges of the PEAK-System PC CAN-FD interfaces */
static const struct can_bittiming_const peak_canfd_nominal_const = {
	.name = "peak_canfd",
	.tseg1_min = 1,
	.tseg1_max = (1 << PUCAN_TSLOW_TSGEG1_BITS),
	.tseg2_min = 1,
	.tseg2_max = (1 << PUCAN_TSLOW_TSGEG2_BITS),
	.sjw_max = (1 << PUCAN_TSLOW_SJW_BITS),
	.brp_min = 1,
	.brp_max = (1 << PUCAN_TSLOW_BRP_BITS),
	.brp_inc = 1,
};

static const struct can_bittiming_const peak_canfd_data_const = {
	.name = "peak_canfd",
	.tseg1_min = 1,
	.tseg1_max = (1 << PUCAN_TFAST_TSGEG1_BITS),
	.tseg2_min = 1,
	.tseg2_max = (1 << PUCAN_TFAST_TSGEG2_BITS),
	.sjw_max = (1 << PUCAN_TFAST_SJW_BITS),
	.brp_min = 1,
	.brp_max = (1 << PUCAN_TFAST_BRP_BITS),
	.brp_inc = 1,
};

static struct peak_canfd_priv *pucan_init_cmd(struct peak_canfd_priv *priv)
{
	priv->cmd_len = 0;
	return priv;
}

static void *pucan_add_cmd(struct peak_canfd_priv *priv, int cmd_op)
{
	struct pucan_command *cmd;

	if (priv->cmd_len + sizeof(*cmd) > priv->cmd_maxlen)
		return NULL;

	cmd = priv->cmd_buffer + priv->cmd_len;

	/* reset all unused bit to default */
	memset(cmd, 0, sizeof(*cmd));

	cmd->opcode_channel = pucan_cmd_opcode_channel(priv->index, cmd_op);
	priv->cmd_len += sizeof(*cmd);

	return cmd;
}

static int pucan_write_cmd(struct peak_canfd_priv *priv)
{
	int err;

	if (priv->pre_cmd) {
		err = priv->pre_cmd(priv);
		if (err)
			return err;
	}

	err = priv->write_cmd(priv);
	if (err)
		return err;

	if (priv->post_cmd)
		err = priv->post_cmd(priv);

	return err;
}

/* uCAN commands interface functions */
static int pucan_set_reset_mode(struct peak_canfd_priv *priv)
{
	pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_RESET_MODE);
	return pucan_write_cmd(priv);
}

static int pucan_set_normal_mode(struct peak_canfd_priv *priv)
{
	int err;

	pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_NORMAL_MODE);
	err = pucan_write_cmd(priv);
	if (!err)
		priv->can.state = CAN_STATE_ERROR_ACTIVE;

	return err;
}

static int pucan_set_listen_only_mode(struct peak_canfd_priv *priv)
{
	int err;

	pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_LISTEN_ONLY_MODE);
	err = pucan_write_cmd(priv);
	if (!err)
		priv->can.state = CAN_STATE_ERROR_ACTIVE;

	return err;
}

static int pucan_set_timing_slow(struct peak_canfd_priv *priv,
				 const struct can_bittiming *pbt)
{
	struct pucan_timing_slow *cmd;

	cmd = pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_TIMING_SLOW);

	cmd->sjw_t = PUCAN_TSLOW_SJW_T(pbt->sjw - 1,
				priv->can.ctrlmode & CAN_CTRLMODE_3_SAMPLES);
	cmd->tseg1 = PUCAN_TSLOW_TSEG1(pbt->prop_seg + pbt->phase_seg1 - 1);
	cmd->tseg2 = PUCAN_TSLOW_TSEG2(pbt->phase_seg2 - 1);
	cmd->brp = cpu_to_le16(PUCAN_TSLOW_BRP(pbt->brp - 1));

	cmd->ewl = 96;	/* default */

	netdev_dbg(priv->ndev,
		   "nominal: brp=%u tseg1=%u tseg2=%u sjw=%u\n",
		   le16_to_cpu(cmd->brp), cmd->tseg1, cmd->tseg2, cmd->sjw_t);

	return pucan_write_cmd(priv);
}

static int pucan_set_timing_fast(struct peak_canfd_priv *priv,
				 const struct can_bittiming *pbt)
{
	struct pucan_timing_fast *cmd;

	cmd = pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_TIMING_FAST);

	cmd->sjw = PUCAN_TFAST_SJW(pbt->sjw - 1);
	cmd->tseg1 = PUCAN_TFAST_TSEG1(pbt->prop_seg + pbt->phase_seg1 - 1);
	cmd->tseg2 = PUCAN_TFAST_TSEG2(pbt->phase_seg2 - 1);
	cmd->brp = cpu_to_le16(PUCAN_TFAST_BRP(pbt->brp - 1));

	netdev_dbg(priv->ndev,
		   "data: brp=%u tseg1=%u tseg2=%u sjw=%u\n",
		   le16_to_cpu(cmd->brp), cmd->tseg1, cmd->tseg2, cmd->sjw);

	return pucan_write_cmd(priv);
}

static int pucan_set_std_filter(struct peak_canfd_priv *priv, u8 row, u32 mask)
{
	struct pucan_std_filter *cmd;

	cmd = pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_SET_STD_FILTER);

	/* all the 11-bits CAN ID values are represented by one bit in a
	 * 64 rows array of 32 bits: the upper 6 bits of the CAN ID select the
	 * row while the lowest 5 bits select the bit in that row.
	 *
	 * bit	filter
	 * 1	passed
	 * 0	discarded
	 */

	/* select the row */
	cmd->idx = row;

	/* set/unset bits in the row */
	cmd->mask = cpu_to_le32(mask);

	return pucan_write_cmd(priv);
}

static int pucan_tx_abort(struct peak_canfd_priv *priv, u16 flags)
{
	struct pucan_tx_abort *cmd;

	cmd = pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_TX_ABORT);

	cmd->flags = cpu_to_le16(flags);

	return pucan_write_cmd(priv);
}

static int pucan_clr_err_counters(struct peak_canfd_priv *priv)
{
	struct pucan_wr_err_cnt *cmd;

	cmd = pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_WR_ERR_CNT);

	cmd->sel_mask = cpu_to_le16(PUCAN_WRERRCNT_TE | PUCAN_WRERRCNT_RE);
	cmd->tx_counter = 0;
	cmd->rx_counter = 0;

	return pucan_write_cmd(priv);
}

static int pucan_set_options(struct peak_canfd_priv *priv, u16 opt_mask)
{
	struct pucan_options *cmd;

	cmd = pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_SET_EN_OPTION);

	cmd->options = cpu_to_le16(opt_mask);

	return pucan_write_cmd(priv);
}

static int pucan_clr_options(struct peak_canfd_priv *priv, u16 opt_mask)
{
	struct pucan_options *cmd;

	cmd = pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_CLR_DIS_OPTION);

	cmd->options = cpu_to_le16(opt_mask);

	return pucan_write_cmd(priv);
}

static int pucan_setup_rx_barrier(struct peak_canfd_priv *priv)
{
	pucan_add_cmd(pucan_init_cmd(priv), PUCAN_CMD_RX_BARRIER);

	return pucan_write_cmd(priv);
}

/* handle the reception of one CAN frame */
static int pucan_handle_can_rx(struct peak_canfd_priv *priv,
			       struct pucan_rx_msg *msg)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct canfd_frame *cf;
	struct sk_buff *skb;
	const u16 rx_msg_flags = le16_to_cpu(msg->flags);
	u8 cf_len;

	if (rx_msg_flags & PUCAN_MSG_EXT_DATA_LEN)
		cf_len = can_dlc2len(get_canfd_dlc(pucan_msg_get_dlc(msg)));
	else
		cf_len = get_can_dlc(pucan_msg_get_dlc(msg));

	/* if this frame is an echo, */
	if (rx_msg_flags & PUCAN_MSG_LOOPED_BACK) {
		unsigned long flags;

		spin_lock_irqsave(&priv->echo_lock, flags);
		can_get_echo_skb(priv->ndev, msg->client);

		/* count bytes of the echo instead of skb */
		stats->tx_bytes += cf_len;
		stats->tx_packets++;

		/* restart tx queue (a slot is free) */
		netif_wake_queue(priv->ndev);

		spin_unlock_irqrestore(&priv->echo_lock, flags);

		/* if this frame is only an echo, stop here. Otherwise,
		 * continue to push this application self-received frame into
		 * its own rx queue.
		 */
		if (!(rx_msg_flags & PUCAN_MSG_SELF_RECEIVE))
			return 0;
	}

	/* otherwise, it should be pushed into rx fifo */
	if (rx_msg_flags & PUCAN_MSG_EXT_DATA_LEN) {
		/* CANFD frame case */
		skb = alloc_canfd_skb(priv->ndev, &cf);
		if (!skb)
			return -ENOMEM;

		if (rx_msg_flags & PUCAN_MSG_BITRATE_SWITCH)
			cf->flags |= CANFD_BRS;

		if (rx_msg_flags & PUCAN_MSG_ERROR_STATE_IND)
			cf->flags |= CANFD_ESI;
	} else {
		/* CAN 2.0 frame case */
		skb = alloc_can_skb(priv->ndev, (struct can_frame **)&cf);
		if (!skb)
			return -ENOMEM;
	}

	cf->can_id = le32_to_cpu(msg->can_id);
	cf->len = cf_len;

	if (rx_msg_flags & PUCAN_MSG_EXT_ID)
		cf->can_id |= CAN_EFF_FLAG;

	if (rx_msg_flags & PUCAN_MSG_RTR)
		cf->can_id |= CAN_RTR_FLAG;
	else
		memcpy(cf->data, msg->d, cf->len);

	stats->rx_bytes += cf->len;
	stats->rx_packets++;

	netif_rx(skb);

	return 0;
}

/* handle rx/tx error counters notification */
static int pucan_handle_error(struct peak_canfd_priv *priv,
			      struct pucan_error_msg *msg)
{
	priv->bec.txerr = msg->tx_err_cnt;
	priv->bec.rxerr = msg->rx_err_cnt;

	return 0;
}

/* handle status notification */
static int pucan_handle_status(struct peak_canfd_priv *priv,
			       struct pucan_status_msg *msg)
{
	struct net_device *ndev = priv->ndev;
	struct net_device_stats *stats = &ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;

	/* this STATUS is the CNF of the RX_BARRIER: Tx path can be setup */
	if (pucan_status_is_rx_barrier(msg)) {

		if (priv->enable_tx_path) {
			int err = priv->enable_tx_path(priv);

			if (err)
				return err;
		}

		/* start network queue (echo_skb array is empty) */
		netif_start_queue(ndev);

		return 0;
	}

	skb = alloc_can_err_skb(ndev, &cf);

	/* test state error bits according to their priority */
	if (pucan_status_is_busoff(msg)) {
		netdev_dbg(ndev, "Bus-off entry status\n");
		priv->can.state = CAN_STATE_BUS_OFF;
		priv->can.can_stats.bus_off++;
		can_bus_off(ndev);
		if (skb)
			cf->can_id |= CAN_ERR_BUSOFF;

	} else if (pucan_status_is_passive(msg)) {
		netdev_dbg(ndev, "Error passive status\n");
		priv->can.state = CAN_STATE_ERROR_PASSIVE;
		priv->can.can_stats.error_passive++;
		if (skb) {
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] = (priv->bec.txerr > priv->bec.rxerr) ?
					CAN_ERR_CRTL_TX_PASSIVE :
					CAN_ERR_CRTL_RX_PASSIVE;
			cf->data[6] = priv->bec.txerr;
			cf->data[7] = priv->bec.rxerr;
		}

	} else if (pucan_status_is_warning(msg)) {
		netdev_dbg(ndev, "Error warning status\n");
		priv->can.state = CAN_STATE_ERROR_WARNING;
		priv->can.can_stats.error_warning++;
		if (skb) {
			cf->can_id |= CAN_ERR_CRTL;
			cf->data[1] = (priv->bec.txerr > priv->bec.rxerr) ?
					CAN_ERR_CRTL_TX_WARNING :
					CAN_ERR_CRTL_RX_WARNING;
			cf->data[6] = priv->bec.txerr;
			cf->data[7] = priv->bec.rxerr;
		}

	} else if (priv->can.state != CAN_STATE_ERROR_ACTIVE) {
		/* back to ERROR_ACTIVE */
		netdev_dbg(ndev, "Error active status\n");
		can_change_state(ndev, cf, CAN_STATE_ERROR_ACTIVE,
				 CAN_STATE_ERROR_ACTIVE);
	} else {
		dev_kfree_skb(skb);
		return 0;
	}

	if (!skb) {
		stats->rx_dropped++;
		return -ENOMEM;
	}

	stats->rx_packets++;
	stats->rx_bytes += cf->can_dlc;
	netif_rx(skb);

	return 0;
}

/* handle uCAN Rx overflow notification */
static int pucan_handle_cache_critical(struct peak_canfd_priv *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct can_frame *cf;
	struct sk_buff *skb;

	stats->rx_over_errors++;
	stats->rx_errors++;

	skb = alloc_can_err_skb(priv->ndev, &cf);
	if (!skb) {
		stats->rx_dropped++;
		return -ENOMEM;
	}

	cf->can_id |= CAN_ERR_CRTL;
	cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;

	cf->data[6] = priv->bec.txerr;
	cf->data[7] = priv->bec.rxerr;

	stats->rx_bytes += cf->can_dlc;
	stats->rx_packets++;
	netif_rx(skb);

	return 0;
}

/* handle a single uCAN message */
int peak_canfd_handle_msg(struct peak_canfd_priv *priv,
			  struct pucan_rx_msg *msg)
{
	u16 msg_type = le16_to_cpu(msg->type);
	int msg_size = le16_to_cpu(msg->size);
	int err;

	if (!msg_size || !msg_type) {
		/* null packet found: end of list */
		goto exit;
	}

	switch (msg_type) {
	case PUCAN_MSG_CAN_RX:
		err = pucan_handle_can_rx(priv, (struct pucan_rx_msg *)msg);
		break;
	case PUCAN_MSG_ERROR:
		err = pucan_handle_error(priv, (struct pucan_error_msg *)msg);
		break;
	case PUCAN_MSG_STATUS:
		err = pucan_handle_status(priv, (struct pucan_status_msg *)msg);
		break;
	case PUCAN_MSG_CACHE_CRITICAL:
		err = pucan_handle_cache_critical(priv);
		break;
	default:
		err = 0;
	}

	if (err < 0)
		return err;

exit:
	return msg_size;
}

/* handle a list of rx_count messages from rx_msg memory address */
int peak_canfd_handle_msgs_list(struct peak_canfd_priv *priv,
				struct pucan_rx_msg *msg_list, int msg_count)
{
	void *msg_ptr = msg_list;
	int i, msg_size = 0;

	for (i = 0; i < msg_count; i++) {
		msg_size = peak_canfd_handle_msg(priv, msg_ptr);

		/* a null packet can be found at the end of a list */
		if (msg_size <= 0)
			break;

		msg_ptr += ALIGN(msg_size, 4);
	}

	if (msg_size < 0)
		return msg_size;

	return i;
}

static int peak_canfd_start(struct peak_canfd_priv *priv)
{
	int err;

	err = pucan_clr_err_counters(priv);
	if (err)
		goto err_exit;

	priv->echo_idx = 0;

	priv->bec.txerr = 0;
	priv->bec.rxerr = 0;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		err = pucan_set_listen_only_mode(priv);
	else
		err = pucan_set_normal_mode(priv);

err_exit:
	return err;
}

static void peak_canfd_stop(struct peak_canfd_priv *priv)
{
	int err;

	/* go back to RESET mode */
	err = pucan_set_reset_mode(priv);
	if (err) {
		netdev_err(priv->ndev, "channel %u reset failed\n",
			   priv->index);
	} else {
		/* abort last Tx (MUST be done in RESET mode only!) */
		pucan_tx_abort(priv, PUCAN_TX_ABORT_FLUSH);
	}
}

static int peak_canfd_set_mode(struct net_device *ndev, enum can_mode mode)
{
	struct peak_canfd_priv *priv = netdev_priv(ndev);

	switch (mode) {
	case CAN_MODE_START:
		peak_canfd_start(priv);
		netif_wake_queue(ndev);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int peak_canfd_get_berr_counter(const struct net_device *ndev,
				       struct can_berr_counter *bec)
{
	struct peak_canfd_priv *priv = netdev_priv(ndev);

	*bec = priv->bec;
	return 0;
}

static int peak_canfd_open(struct net_device *ndev)
{
	struct peak_canfd_priv *priv = netdev_priv(ndev);
	int i, err = 0;

	err = open_candev(ndev);
	if (err) {
		netdev_err(ndev, "open_candev() failed, error %d\n", err);
		goto err_exit;
	}

	err = pucan_set_reset_mode(priv);
	if (err)
		goto err_close;

	if (priv->can.ctrlmode & CAN_CTRLMODE_FD) {
		if (priv->can.ctrlmode & CAN_CTRLMODE_FD_NON_ISO)
			err = pucan_clr_options(priv, PUCAN_OPTION_CANDFDISO);
		else
			err = pucan_set_options(priv, PUCAN_OPTION_CANDFDISO);

		if (err)
			goto err_close;
	}

	/* set option: get rx/tx error counters */
	err = pucan_set_options(priv, PUCAN_OPTION_ERROR);
	if (err)
		goto err_close;

	/* accept all standard CAN ID */
	for (i = 0; i <= PUCAN_FLTSTD_ROW_IDX_MAX; i++)
		pucan_set_std_filter(priv, i, 0xffffffff);

	err = peak_canfd_start(priv);
	if (err)
		goto err_close;

	/* receiving the RB status says when Tx path is ready */
	err = pucan_setup_rx_barrier(priv);
	if (!err)
		goto err_exit;

err_close:
	close_candev(ndev);
err_exit:
	return err;
}

static int peak_canfd_set_bittiming(struct net_device *ndev)
{
	struct peak_canfd_priv *priv = netdev_priv(ndev);

	return pucan_set_timing_slow(priv, &priv->can.bittiming);
}

static int peak_canfd_set_data_bittiming(struct net_device *ndev)
{
	struct peak_canfd_priv *priv = netdev_priv(ndev);

	return pucan_set_timing_fast(priv, &priv->can.data_bittiming);
}

static int peak_canfd_close(struct net_device *ndev)
{
	struct peak_canfd_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	peak_canfd_stop(priv);
	close_candev(ndev);

	return 0;
}

static netdev_tx_t peak_canfd_start_xmit(struct sk_buff *skb,
					 struct net_device *ndev)
{
	struct peak_canfd_priv *priv = netdev_priv(ndev);
	struct net_device_stats *stats = &ndev->stats;
	struct canfd_frame *cf = (struct canfd_frame *)skb->data;
	struct pucan_tx_msg *msg;
	u16 msg_size, msg_flags;
	unsigned long flags;
	bool should_stop_tx_queue;
	int room_left;
	u8 can_dlc;

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	msg_size = ALIGN(sizeof(*msg) + cf->len, 4);
	msg = priv->alloc_tx_msg(priv, msg_size, &room_left);

	/* should never happen except under bus-off condition and (auto-)restart
	 * mechanism
	 */
	if (!msg) {
		stats->tx_dropped++;
		netif_stop_queue(ndev);
		return NETDEV_TX_BUSY;
	}

	msg->size = cpu_to_le16(msg_size);
	msg->type = cpu_to_le16(PUCAN_MSG_CAN_TX);
	msg_flags = 0;

	if (cf->can_id & CAN_EFF_FLAG) {
		msg_flags |= PUCAN_MSG_EXT_ID;
		msg->can_id = cpu_to_le32(cf->can_id & CAN_EFF_MASK);
	} else {
		msg->can_id = cpu_to_le32(cf->can_id & CAN_SFF_MASK);
	}

	if (can_is_canfd_skb(skb)) {
		/* CAN FD frame format */
		can_dlc = can_len2dlc(cf->len);

		msg_flags |= PUCAN_MSG_EXT_DATA_LEN;

		if (cf->flags & CANFD_BRS)
			msg_flags |= PUCAN_MSG_BITRATE_SWITCH;

		if (cf->flags & CANFD_ESI)
			msg_flags |= PUCAN_MSG_ERROR_STATE_IND;
	} else {
		/* CAN 2.0 frame format */
		can_dlc = cf->len;

		if (cf->can_id & CAN_RTR_FLAG)
			msg_flags |= PUCAN_MSG_RTR;
	}

	/* always ask loopback for echo management */
	msg_flags |= PUCAN_MSG_LOOPED_BACK;

	/* set driver specific bit to differentiate with application loopback */
	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		msg_flags |= PUCAN_MSG_SELF_RECEIVE;

	msg->flags = cpu_to_le16(msg_flags);
	msg->channel_dlc = PUCAN_MSG_CHANNEL_DLC(priv->index, can_dlc);
	memcpy(msg->d, cf->data, cf->len);

	/* struct msg client field is used as an index in the echo skbs ring */
	msg->client = priv->echo_idx;

	spin_lock_irqsave(&priv->echo_lock, flags);

	/* prepare and save echo skb in internal slot */
	can_put_echo_skb(skb, ndev, priv->echo_idx);

	/* move echo index to the next slot */
	priv->echo_idx = (priv->echo_idx + 1) % priv->can.echo_skb_max;

	/* if next slot is not free, stop network queue (no slot free in echo
	 * skb ring means that the controller did not write these frames on
	 * the bus: no need to continue).
	 */
	should_stop_tx_queue = !!(priv->can.echo_skb[priv->echo_idx]);

	/* stop network tx queue if not enough room to save one more msg too */
	if (priv->can.ctrlmode & CAN_CTRLMODE_FD)
		should_stop_tx_queue |= (room_left <
					(sizeof(*msg) + CANFD_MAX_DLEN));
	else
		should_stop_tx_queue |= (room_left <
					(sizeof(*msg) + CAN_MAX_DLEN));

	if (should_stop_tx_queue)
		netif_stop_queue(ndev);

	spin_unlock_irqrestore(&priv->echo_lock, flags);

	/* write the skb on the interface */
	priv->write_tx_msg(priv, msg);

	return NETDEV_TX_OK;
}

static const struct net_device_ops peak_canfd_netdev_ops = {
	.ndo_open = peak_canfd_open,
	.ndo_stop = peak_canfd_close,
	.ndo_start_xmit = peak_canfd_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

struct net_device *alloc_peak_canfd_dev(int sizeof_priv, int index,
					int echo_skb_max)
{
	struct net_device *ndev;
	struct peak_canfd_priv *priv;

	/* we DO support local echo */
	if (echo_skb_max < 0)
		echo_skb_max = PCANFD_ECHO_SKB_MAX;

	/* allocate the candev object */
	ndev = alloc_candev(sizeof_priv, echo_skb_max);
	if (!ndev)
		return NULL;

	priv = netdev_priv(ndev);

	/* complete now socket-can initialization side */
	priv->can.state = CAN_STATE_STOPPED;
	priv->can.bittiming_const = &peak_canfd_nominal_const;
	priv->can.data_bittiming_const = &peak_canfd_data_const;

	priv->can.do_set_mode = peak_canfd_set_mode;
	priv->can.do_get_berr_counter = peak_canfd_get_berr_counter;
	priv->can.do_set_bittiming = peak_canfd_set_bittiming;
	priv->can.do_set_data_bittiming = peak_canfd_set_data_bittiming;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
				       CAN_CTRLMODE_LISTENONLY |
				       CAN_CTRLMODE_3_SAMPLES |
				       CAN_CTRLMODE_FD |
				       CAN_CTRLMODE_FD_NON_ISO |
				       CAN_CTRLMODE_BERR_REPORTING;

	priv->ndev = ndev;
	priv->index = index;
	priv->cmd_len = 0;
	spin_lock_init(&priv->echo_lock);

	ndev->flags |= IFF_ECHO;
	ndev->netdev_ops = &peak_canfd_netdev_ops;
	ndev->dev_id = index;

	return ndev;
}
