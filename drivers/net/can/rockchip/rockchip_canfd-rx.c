// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2023, 2024 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//

#include "rockchip_canfd.h"

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

	len = rkcanfd_fifo_header_to_cfd_header(priv, header, cfd);

	/* Drop any received CAN-FD frames if CAN-FD mode is not
	 * requested.
	 */
	if (header->frameinfo & RKCANFD_REG_FD_FRAMEINFO_FDF &&
	    !(priv->can.ctrlmode & CAN_CTRLMODE_FD)) {
		stats->rx_dropped++;

		return 0;
	}

	if (header->frameinfo & RKCANFD_REG_FD_FRAMEINFO_FDF)
		skb = alloc_canfd_skb(priv->ndev, &skb_cfd);
	else
		skb = alloc_can_skb(priv->ndev, (struct can_frame **)&skb_cfd);

	if (!skb) {
		stats->rx_dropped++;

		return 0;
	}

	memcpy(skb_cfd, cfd, len);

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
