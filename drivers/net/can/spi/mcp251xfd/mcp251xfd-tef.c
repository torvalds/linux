// SPDX-License-Identifier: GPL-2.0
//
// mcp251xfd - Microchip MCP251xFD Family CAN controller driver
//
// Copyright (c) 2019, 2020, 2021, 2023 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//
// Based on:
//
// CAN bus driver for Microchip 25XXFD CAN Controller with SPI Interface
//
// Copyright (c) 2019 Martin Sperl <kernel@martin.sperl.org>
//

#include <linux/bitfield.h>

#include "mcp251xfd.h"

static inline bool mcp251xfd_tx_fifo_sta_empty(u32 fifo_sta)
{
	return fifo_sta & MCP251XFD_REG_FIFOSTA_TFERFFIF;
}

static inline bool mcp251xfd_tx_fifo_sta_less_than_half_full(u32 fifo_sta)
{
	return fifo_sta & MCP251XFD_REG_FIFOSTA_TFHRFHIF;
}

static inline int
mcp251xfd_tef_tail_get_from_chip(const struct mcp251xfd_priv *priv,
				 u8 *tef_tail)
{
	u32 tef_ua;
	int err;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_TEFUA, &tef_ua);
	if (err)
		return err;

	*tef_tail = tef_ua / sizeof(struct mcp251xfd_hw_tef_obj);

	return 0;
}

static int mcp251xfd_check_tef_tail(const struct mcp251xfd_priv *priv)
{
	u8 tef_tail_chip, tef_tail;
	int err;

	if (!IS_ENABLED(CONFIG_CAN_MCP251XFD_SANITY))
		return 0;

	err = mcp251xfd_tef_tail_get_from_chip(priv, &tef_tail_chip);
	if (err)
		return err;

	tef_tail = mcp251xfd_get_tef_tail(priv);
	if (tef_tail_chip != tef_tail) {
		netdev_err(priv->ndev,
			   "TEF tail of chip (0x%02x) and ours (0x%08x) inconsistent.\n",
			   tef_tail_chip, tef_tail);
		return -EILSEQ;
	}

	return 0;
}

static int
mcp251xfd_handle_tefif_one(struct mcp251xfd_priv *priv,
			   const struct mcp251xfd_hw_tef_obj *hw_tef_obj,
			   unsigned int *frame_len_ptr)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	u32 seq, tef_tail_masked, tef_tail;
	struct sk_buff *skb;

	 /* Use the MCP2517FD mask on the MCP2518FD, too. We only
	  * compare 7 bits, this is enough to detect old TEF objects.
	  */
	seq = FIELD_GET(MCP251XFD_OBJ_FLAGS_SEQ_MCP2517FD_MASK,
			hw_tef_obj->flags);
	tef_tail_masked = priv->tef->tail &
		field_mask(MCP251XFD_OBJ_FLAGS_SEQ_MCP2517FD_MASK);

	/* According to mcp2518fd erratum DS80000789E 6. the FIFOCI
	 * bits of a FIFOSTA register, here the TX FIFO tail index
	 * might be corrupted and we might process past the TEF FIFO's
	 * head into old CAN frames.
	 *
	 * Compare the sequence number of the currently processed CAN
	 * frame with the expected sequence number. Abort with
	 * -EBADMSG if an old CAN frame is detected.
	 */
	if (seq != tef_tail_masked) {
		netdev_dbg(priv->ndev, "%s: chip=0x%02x ring=0x%02x\n", __func__,
			   seq, tef_tail_masked);
		stats->tx_fifo_errors++;

		return -EBADMSG;
	}

	tef_tail = mcp251xfd_get_tef_tail(priv);
	skb = priv->can.echo_skb[tef_tail];
	if (skb)
		mcp251xfd_skb_set_timestamp_raw(priv, skb, hw_tef_obj->ts);
	stats->tx_bytes +=
		can_rx_offload_get_echo_skb_queue_timestamp(&priv->offload,
							    tef_tail, hw_tef_obj->ts,
							    frame_len_ptr);
	stats->tx_packets++;
	priv->tef->tail++;

	return 0;
}

static int
mcp251xfd_get_tef_len(struct mcp251xfd_priv *priv, u8 *len_p)
{
	const struct mcp251xfd_tx_ring *tx_ring = priv->tx;
	const u8 shift = tx_ring->obj_num_shift_to_u8;
	u8 chip_tx_tail, tail, len;
	u32 fifo_sta;
	int err;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_FIFOSTA(priv->tx->fifo_nr),
			  &fifo_sta);
	if (err)
		return err;

	/* If the chip says the TX-FIFO is empty, but there are no TX
	 * buffers free in the ring, we assume all have been sent.
	 */
	if (mcp251xfd_tx_fifo_sta_empty(fifo_sta) &&
	    mcp251xfd_get_tx_free(tx_ring) == 0) {
		*len_p = tx_ring->obj_num;
		return 0;
	}

	chip_tx_tail = FIELD_GET(MCP251XFD_REG_FIFOSTA_FIFOCI_MASK, fifo_sta);

	err =  mcp251xfd_check_tef_tail(priv);
	if (err)
		return err;
	tail = mcp251xfd_get_tef_tail(priv);

	/* First shift to full u8. The subtraction works on signed
	 * values, that keeps the difference steady around the u8
	 * overflow. The right shift acts on len, which is an u8.
	 */
	BUILD_BUG_ON(sizeof(tx_ring->obj_num) != sizeof(chip_tx_tail));
	BUILD_BUG_ON(sizeof(tx_ring->obj_num) != sizeof(tail));
	BUILD_BUG_ON(sizeof(tx_ring->obj_num) != sizeof(len));

	len = (chip_tx_tail << shift) - (tail << shift);
	len >>= shift;

	/* According to mcp2518fd erratum DS80000789E 6. the FIFOCI
	 * bits of a FIFOSTA register, here the TX-FIFO tail index
	 * might be corrupted.
	 *
	 * However here it seems the bit indicating that the TX-FIFO
	 * is empty (MCP251XFD_REG_FIFOSTA_TFERFFIF) is not correct
	 * while the TX-FIFO tail index is.
	 *
	 * We assume the TX-FIFO is empty, i.e. all pending CAN frames
	 * haven been send, if:
	 * - Chip's head and tail index are equal (len == 0).
	 * - The TX-FIFO is less than half full.
	 *   (The TX-FIFO empty case has already been checked at the
	 *    beginning of this function.)
	 * - No free buffers in the TX ring.
	 */
	if (len == 0 && mcp251xfd_tx_fifo_sta_less_than_half_full(fifo_sta) &&
	    mcp251xfd_get_tx_free(tx_ring) == 0)
		len = tx_ring->obj_num;

	*len_p = len;

	return 0;
}

static inline int
mcp251xfd_tef_obj_read(const struct mcp251xfd_priv *priv,
		       struct mcp251xfd_hw_tef_obj *hw_tef_obj,
		       const u8 offset, const u8 len)
{
	const struct mcp251xfd_tx_ring *tx_ring = priv->tx;
	const int val_bytes = regmap_get_val_bytes(priv->map_rx);

	if (IS_ENABLED(CONFIG_CAN_MCP251XFD_SANITY) &&
	    (offset > tx_ring->obj_num ||
	     len > tx_ring->obj_num ||
	     offset + len > tx_ring->obj_num)) {
		netdev_err(priv->ndev,
			   "Trying to read too many TEF objects (max=%d, offset=%d, len=%d).\n",
			   tx_ring->obj_num, offset, len);
		return -ERANGE;
	}

	return regmap_bulk_read(priv->map_rx,
				mcp251xfd_get_tef_obj_addr(offset),
				hw_tef_obj,
				sizeof(*hw_tef_obj) / val_bytes * len);
}

static inline void mcp251xfd_ecc_tefif_successful(struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_ecc *ecc = &priv->ecc;

	ecc->ecc_stat = 0;
}

int mcp251xfd_handle_tefif(struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_hw_tef_obj hw_tef_obj[MCP251XFD_TX_OBJ_NUM_MAX];
	unsigned int total_frame_len = 0;
	u8 tef_tail, len, l;
	int err, i;

	err = mcp251xfd_get_tef_len(priv, &len);
	if (err)
		return err;

	tef_tail = mcp251xfd_get_tef_tail(priv);
	l = mcp251xfd_get_tef_linear_len(priv, len);
	err = mcp251xfd_tef_obj_read(priv, hw_tef_obj, tef_tail, l);
	if (err)
		return err;

	if (l < len) {
		err = mcp251xfd_tef_obj_read(priv, &hw_tef_obj[l], 0, len - l);
		if (err)
			return err;
	}

	for (i = 0; i < len; i++) {
		unsigned int frame_len = 0;

		err = mcp251xfd_handle_tefif_one(priv, &hw_tef_obj[i], &frame_len);
		/* -EBADMSG means we're affected by mcp2518fd erratum
		 * DS80000789E 6., i.e. the Sequence Number in the TEF
		 * doesn't match our tef_tail. Don't process any
		 * further and mark processed frames as good.
		 */
		if (err == -EBADMSG)
			goto out_netif_wake_queue;
		if (err)
			return err;

		total_frame_len += frame_len;
	}

out_netif_wake_queue:
	len = i;	/* number of handled goods TEFs */
	if (len) {
		struct mcp251xfd_tef_ring *ring = priv->tef;
		struct mcp251xfd_tx_ring *tx_ring = priv->tx;
		int offset;

		ring->head += len;

		/* Increment the TEF FIFO tail pointer 'len' times in
		 * a single SPI message.
		 *
		 * Note:
		 * Calculate offset, so that the SPI transfer ends on
		 * the last message of the uinc_xfer array, which has
		 * "cs_change == 0", to properly deactivate the chip
		 * select.
		 */
		offset = ARRAY_SIZE(ring->uinc_xfer) - len;
		err = spi_sync_transfer(priv->spi,
					ring->uinc_xfer + offset, len);
		if (err)
			return err;

		tx_ring->tail += len;
		netdev_completed_queue(priv->ndev, len, total_frame_len);

		err = mcp251xfd_check_tef_tail(priv);
		if (err)
			return err;
	}

	mcp251xfd_ecc_tefif_successful(priv);

	if (mcp251xfd_get_tx_free(priv->tx)) {
		/* Make sure that anybody stopping the queue after
		 * this sees the new tx_ring->tail.
		 */
		smp_mb();
		netif_wake_queue(priv->ndev);
	}

	if (priv->tx_coalesce_usecs_irq)
		hrtimer_start(&priv->tx_irq_timer,
			      ns_to_ktime(priv->tx_coalesce_usecs_irq *
					  NSEC_PER_USEC),
			      HRTIMER_MODE_REL);

	return 0;
}
