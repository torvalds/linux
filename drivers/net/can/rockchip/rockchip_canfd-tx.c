// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2023, 2024 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include <net/netdev_queues.h>

#include "rockchip_canfd.h"

static bool rkcanfd_tx_tail_is_eff(const struct rkcanfd_priv *priv)
{
	const struct canfd_frame *cfd;
	const struct sk_buff *skb;
	unsigned int tx_tail;

	if (!rkcanfd_get_tx_pending(priv))
		return false;

	tx_tail = rkcanfd_get_tx_tail(priv);
	skb = priv->can.echo_skb[tx_tail];
	if (!skb) {
		netdev_err(priv->ndev,
			   "%s: echo_skb[%u]=NULL tx_head=0x%08x tx_tail=0x%08x\n",
			   __func__, tx_tail,
			   priv->tx_head, priv->tx_tail);

		return false;
	}

	cfd = (struct canfd_frame *)skb->data;

	return cfd->can_id & CAN_EFF_FLAG;
}

unsigned int rkcanfd_get_effective_tx_free(const struct rkcanfd_priv *priv)
{
	if (priv->devtype_data.quirks & RKCANFD_QUIRK_RK3568_ERRATUM_6 &&
	    rkcanfd_tx_tail_is_eff(priv))
		return 0;

	return rkcanfd_get_tx_free(priv);
}

static void rkcanfd_start_xmit_write_cmd(const struct rkcanfd_priv *priv,
					 const u32 reg_cmd)
{
	if (priv->devtype_data.quirks & RKCANFD_QUIRK_RK3568_ERRATUM_12)
		rkcanfd_write(priv, RKCANFD_REG_MODE, priv->reg_mode_default |
			      RKCANFD_REG_MODE_SPACE_RX_MODE);

	rkcanfd_write(priv, RKCANFD_REG_CMD, reg_cmd);

	if (priv->devtype_data.quirks & RKCANFD_QUIRK_RK3568_ERRATUM_12)
		rkcanfd_write(priv, RKCANFD_REG_MODE, priv->reg_mode_default);
}

void rkcanfd_xmit_retry(struct rkcanfd_priv *priv)
{
	const unsigned int tx_head = rkcanfd_get_tx_head(priv);
	const u32 reg_cmd = RKCANFD_REG_CMD_TX_REQ(tx_head);

	rkcanfd_start_xmit_write_cmd(priv, reg_cmd);
}

netdev_tx_t rkcanfd_start_xmit(struct sk_buff *skb, struct net_device *ndev)
{
	struct rkcanfd_priv *priv = netdev_priv(ndev);
	u32 reg_frameinfo, reg_id, reg_cmd;
	unsigned int tx_head, frame_len;
	const struct canfd_frame *cfd;
	int err;
	u8 i;

	if (can_dev_dropped_skb(ndev, skb))
		return NETDEV_TX_OK;

	if (!netif_subqueue_maybe_stop(priv->ndev, 0,
				       rkcanfd_get_effective_tx_free(priv),
				       RKCANFD_TX_STOP_THRESHOLD,
				       RKCANFD_TX_START_THRESHOLD)) {
		if (net_ratelimit())
			netdev_info(priv->ndev,
				    "Stopping tx-queue (tx_head=0x%08x, tx_tail=0x%08x, tx_pending=%d)\n",
				    priv->tx_head, priv->tx_tail,
				    rkcanfd_get_tx_pending(priv));

		return NETDEV_TX_BUSY;
	}

	cfd = (struct canfd_frame *)skb->data;

	if (cfd->can_id & CAN_EFF_FLAG) {
		reg_frameinfo = RKCANFD_REG_FD_FRAMEINFO_FRAME_FORMAT;
		reg_id = FIELD_PREP(RKCANFD_REG_FD_ID_EFF, cfd->can_id);
	} else {
		reg_frameinfo = 0;
		reg_id = FIELD_PREP(RKCANFD_REG_FD_ID_SFF, cfd->can_id);
	}

	if (cfd->can_id & CAN_RTR_FLAG)
		reg_frameinfo |= RKCANFD_REG_FD_FRAMEINFO_RTR;

	if (can_is_canfd_skb(skb)) {
		reg_frameinfo |= RKCANFD_REG_FD_FRAMEINFO_FDF;

		if (cfd->flags & CANFD_BRS)
			reg_frameinfo |= RKCANFD_REG_FD_FRAMEINFO_BRS;

		reg_frameinfo |= FIELD_PREP(RKCANFD_REG_FD_FRAMEINFO_DATA_LENGTH,
					    can_fd_len2dlc(cfd->len));
	} else {
		reg_frameinfo |= FIELD_PREP(RKCANFD_REG_FD_FRAMEINFO_DATA_LENGTH,
					    cfd->len);
	}

	tx_head = rkcanfd_get_tx_head(priv);
	reg_cmd = RKCANFD_REG_CMD_TX_REQ(tx_head);

	rkcanfd_write(priv, RKCANFD_REG_FD_TXFRAMEINFO, reg_frameinfo);
	rkcanfd_write(priv, RKCANFD_REG_FD_TXID, reg_id);
	for (i = 0; i < cfd->len; i += 4)
		rkcanfd_write(priv, RKCANFD_REG_FD_TXDATA0 + i,
			      *(u32 *)(cfd->data + i));

	frame_len = can_skb_get_frame_len(skb);
	err = can_put_echo_skb(skb, ndev, tx_head, frame_len);
	if (!err)
		netdev_sent_queue(priv->ndev, frame_len);

	WRITE_ONCE(priv->tx_head, priv->tx_head + 1);

	rkcanfd_start_xmit_write_cmd(priv, reg_cmd);

	netif_subqueue_maybe_stop(priv->ndev, 0,
				  rkcanfd_get_effective_tx_free(priv),
				  RKCANFD_TX_STOP_THRESHOLD,
				  RKCANFD_TX_START_THRESHOLD);

	return NETDEV_TX_OK;
}

void rkcanfd_handle_tx_done_one(struct rkcanfd_priv *priv, const u32 ts,
				unsigned int *frame_len_p)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	unsigned int tx_tail;
	struct sk_buff *skb;

	tx_tail = rkcanfd_get_tx_tail(priv);
	skb = priv->can.echo_skb[tx_tail];

	/* Manual handling of CAN Bus Error counters. See
	 * rkcanfd_get_corrected_berr_counter() for detailed
	 * explanation.
	 */
	if (priv->bec.txerr)
		priv->bec.txerr--;

	if (skb)
		rkcanfd_skb_set_timestamp(priv, skb, ts);
	stats->tx_bytes +=
		can_rx_offload_get_echo_skb_queue_timestamp(&priv->offload,
							    tx_tail, ts,
							    frame_len_p);
	stats->tx_packets++;
}
