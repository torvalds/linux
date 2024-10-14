// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2023, 2024 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include <net/netdev_queues.h>

#include "rockchip_canfd.h"

static bool rkcanfd_can_frame_header_equal(const struct canfd_frame *const cfd1,
					   const struct canfd_frame *const cfd2,
					   const bool is_canfd)
{
	const u8 mask_flags = CANFD_BRS | CANFD_ESI | CANFD_FDF;
	canid_t mask = CAN_EFF_FLAG;

	if (canfd_sanitize_len(cfd1->len) != canfd_sanitize_len(cfd2->len))
		return false;

	if (!is_canfd)
		mask |= CAN_RTR_FLAG;

	if (cfd1->can_id & CAN_EFF_FLAG)
		mask |= CAN_EFF_MASK;
	else
		mask |= CAN_SFF_MASK;

	if ((cfd1->can_id & mask) != (cfd2->can_id & mask))
		return false;

	if (is_canfd &&
	    (cfd1->flags & mask_flags) != (cfd2->flags & mask_flags))
		return false;

	return true;
}

static bool rkcanfd_can_frame_data_equal(const struct canfd_frame *cfd1,
					 const struct canfd_frame *cfd2,
					 const bool is_canfd)
{
	u8 len;

	if (!is_canfd && (cfd1->can_id & CAN_RTR_FLAG))
		return true;

	len = canfd_sanitize_len(cfd1->len);

	return !memcmp(cfd1->data, cfd2->data, len);
}

static unsigned int
rkcanfd_fifo_header_to_cfd_header(const struct rkcanfd_priv *priv,
				  const struct rkcanfd_fifo_header *header,
				  struct canfd_frame *cfd)
{
	unsigned int len = sizeof(*cfd) - sizeof(cfd->data);
	u8 dlc;

	if (header->frameinfo & RKCANFD_REG_FD_FRAMEINFO_FRAME_FORMAT)
		cfd->can_id = FIELD_GET(RKCANFD_REG_FD_ID_EFF, header->id) |
			CAN_EFF_FLAG;
	else
		cfd->can_id = FIELD_GET(RKCANFD_REG_FD_ID_SFF, header->id);

	dlc = FIELD_GET(RKCANFD_REG_FD_FRAMEINFO_DATA_LENGTH,
			header->frameinfo);

	/* CAN-FD */
	if (header->frameinfo & RKCANFD_REG_FD_FRAMEINFO_FDF) {
		cfd->len = can_fd_dlc2len(dlc);

		/* The cfd is not allocated by alloc_canfd_skb(), so
		 * set CANFD_FDF here.
		 */
		cfd->flags |= CANFD_FDF;

		if (header->frameinfo & RKCANFD_REG_FD_FRAMEINFO_BRS)
			cfd->flags |= CANFD_BRS;
	} else {
		cfd->len = can_cc_dlc2len(dlc);

		if (header->frameinfo & RKCANFD_REG_FD_FRAMEINFO_RTR) {
			cfd->can_id |= CAN_RTR_FLAG;

			return len;
		}
	}

	return len + cfd->len;
}

static int rkcanfd_rxstx_filter(struct rkcanfd_priv *priv,
				const struct canfd_frame *cfd_rx, const u32 ts,
				bool *tx_done)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct rkcanfd_stats *rkcanfd_stats = &priv->stats;
	const struct canfd_frame *cfd_nominal;
	const struct sk_buff *skb;
	unsigned int tx_tail;

	tx_tail = rkcanfd_get_tx_tail(priv);
	skb = priv->can.echo_skb[tx_tail];
	if (!skb) {
		netdev_err(priv->ndev,
			   "%s: echo_skb[%u]=NULL tx_head=0x%08x tx_tail=0x%08x\n",
			   __func__, tx_tail,
			   priv->tx_head, priv->tx_tail);

		return -ENOMSG;
	}
	cfd_nominal = (struct canfd_frame *)skb->data;

	/* We RX'ed a frame identical to our pending TX frame. */
	if (rkcanfd_can_frame_header_equal(cfd_rx, cfd_nominal,
					   cfd_rx->flags & CANFD_FDF) &&
	    rkcanfd_can_frame_data_equal(cfd_rx, cfd_nominal,
					 cfd_rx->flags & CANFD_FDF)) {
		unsigned int frame_len;

		rkcanfd_handle_tx_done_one(priv, ts, &frame_len);

		WRITE_ONCE(priv->tx_tail, priv->tx_tail + 1);
		netif_subqueue_completed_wake(priv->ndev, 0, 1, frame_len,
					      rkcanfd_get_effective_tx_free(priv),
					      RKCANFD_TX_START_THRESHOLD);

		*tx_done = true;

		return 0;
	}

	if (!(priv->devtype_data.quirks & RKCANFD_QUIRK_RK3568_ERRATUM_6))
		return 0;

	/* Erratum 6: Extended frames may be send as standard frames.
	 *
	 * Not affected if:
	 * - TX'ed a standard frame -or-
	 * - RX'ed an extended frame
	 */
	if (!(cfd_nominal->can_id & CAN_EFF_FLAG) ||
	    (cfd_rx->can_id & CAN_EFF_FLAG))
		return 0;

	/* Not affected if:
	 * - standard part and RTR flag of the TX'ed frame
	 *   is not equal the CAN-ID and RTR flag of the RX'ed frame.
	 */
	if ((cfd_nominal->can_id & (CAN_RTR_FLAG | CAN_SFF_MASK)) !=
	    (cfd_rx->can_id & (CAN_RTR_FLAG | CAN_SFF_MASK)))
		return 0;

	/* Not affected if:
	 * - length is not the same
	 */
	if (cfd_nominal->len != cfd_rx->len)
		return 0;

	/* Not affected if:
	 * - the data of non RTR frames is different
	 */
	if (!(cfd_nominal->can_id & CAN_RTR_FLAG) &&
	    memcmp(cfd_nominal->data, cfd_rx->data, cfd_nominal->len))
		return 0;

	/* Affected by Erratum 6 */
	u64_stats_update_begin(&rkcanfd_stats->syncp);
	u64_stats_inc(&rkcanfd_stats->tx_extended_as_standard_errors);
	u64_stats_update_end(&rkcanfd_stats->syncp);

	/* Manual handling of CAN Bus Error counters. See
	 * rkcanfd_get_corrected_berr_counter() for detailed
	 * explanation.
	 */
	if (priv->bec.txerr)
		priv->bec.txerr--;

	*tx_done = true;

	stats->tx_packets++;
	stats->tx_errors++;

	rkcanfd_xmit_retry(priv);

	return 0;
}

static inline bool
rkcanfd_fifo_header_empty(const struct rkcanfd_fifo_header *header)
{
	/* Erratum 5: If the FIFO is empty, we read the same value for
	 * all elements.
	 */
	return header->frameinfo == header->id &&
		header->frameinfo == header->ts;
}

static int rkcanfd_handle_rx_int_one(struct rkcanfd_priv *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct canfd_frame cfd[1] = { }, *skb_cfd;
	struct rkcanfd_fifo_header header[1] = { };
	struct sk_buff *skb;
	unsigned int len;
	int err;

	/* read header into separate struct and convert it later */
	rkcanfd_read_rep(priv, RKCANFD_REG_RX_FIFO_RDATA,
			 header, sizeof(*header));
	/* read data directly into cfd */
	rkcanfd_read_rep(priv, RKCANFD_REG_RX_FIFO_RDATA,
			 cfd->data, sizeof(cfd->data));

	/* Erratum 5: Counters for TXEFIFO and RXFIFO may be wrong */
	if (rkcanfd_fifo_header_empty(header)) {
		struct rkcanfd_stats *rkcanfd_stats = &priv->stats;

		u64_stats_update_begin(&rkcanfd_stats->syncp);
		u64_stats_inc(&rkcanfd_stats->rx_fifo_empty_errors);
		u64_stats_update_end(&rkcanfd_stats->syncp);

		return 0;
	}

	len = rkcanfd_fifo_header_to_cfd_header(priv, header, cfd);

	/* Drop any received CAN-FD frames if CAN-FD mode is not
	 * requested.
	 */
	if (header->frameinfo & RKCANFD_REG_FD_FRAMEINFO_FDF &&
	    !(priv->can.ctrlmode & CAN_CTRLMODE_FD)) {
		stats->rx_dropped++;

		return 0;
	}

	if (rkcanfd_get_tx_pending(priv)) {
		bool tx_done = false;

		err = rkcanfd_rxstx_filter(priv, cfd, header->ts, &tx_done);
		if (err)
			return err;
		if (tx_done && !(priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK))
			return 0;
	}

	/* Manual handling of CAN Bus Error counters. See
	 * rkcanfd_get_corrected_berr_counter() for detailed
	 * explanation.
	 */
	if (priv->bec.rxerr)
		priv->bec.rxerr = min(CAN_ERROR_PASSIVE_THRESHOLD,
				      priv->bec.rxerr) - 1;

	if (header->frameinfo & RKCANFD_REG_FD_FRAMEINFO_FDF)
		skb = alloc_canfd_skb(priv->ndev, &skb_cfd);
	else
		skb = alloc_can_skb(priv->ndev, (struct can_frame **)&skb_cfd);

	if (!skb) {
		stats->rx_dropped++;

		return 0;
	}

	memcpy(skb_cfd, cfd, len);
	rkcanfd_skb_set_timestamp(priv, skb, header->ts);

	err = can_rx_offload_queue_timestamp(&priv->offload, skb, header->ts);
	if (err)
		stats->rx_fifo_errors++;

	return 0;
}

static inline unsigned int
rkcanfd_rx_fifo_get_len(const struct rkcanfd_priv *priv)
{
	const u32 reg = rkcanfd_read(priv, RKCANFD_REG_RX_FIFO_CTRL);

	return FIELD_GET(RKCANFD_REG_RX_FIFO_CTRL_RX_FIFO_CNT, reg);
}

int rkcanfd_handle_rx_int(struct rkcanfd_priv *priv)
{
	unsigned int len;
	int err;

	while ((len = rkcanfd_rx_fifo_get_len(priv))) {
		err = rkcanfd_handle_rx_int_one(priv);
		if (err)
			return err;
	}

	return 0;
}
