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

static inline bool mcp251xfd_rx_fifo_sta_empty(const u32 fifo_sta)
{
	return !(fifo_sta & MCP251XFD_REG_FIFOSTA_TFNRFNIF);
}

static inline bool mcp251xfd_rx_fifo_sta_full(const u32 fifo_sta)
{
	return fifo_sta & MCP251XFD_REG_FIFOSTA_TFERFFIF;
}

static inline int
mcp251xfd_rx_tail_get_from_chip(const struct mcp251xfd_priv *priv,
				const struct mcp251xfd_rx_ring *ring,
				u8 *rx_tail)
{
	u32 fifo_ua;
	int err;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_FIFOUA(ring->fifo_nr),
			  &fifo_ua);
	if (err)
		return err;

	fifo_ua -= ring->base - MCP251XFD_RAM_START;
	*rx_tail = fifo_ua / ring->obj_size;

	return 0;
}

static int
mcp251xfd_check_rx_tail(const struct mcp251xfd_priv *priv,
			const struct mcp251xfd_rx_ring *ring)
{
	u8 rx_tail_chip, rx_tail;
	int err;

	if (!IS_ENABLED(CONFIG_CAN_MCP251XFD_SANITY))
		return 0;

	err = mcp251xfd_rx_tail_get_from_chip(priv, ring, &rx_tail_chip);
	if (err)
		return err;

	rx_tail = mcp251xfd_get_rx_tail(ring);
	if (rx_tail_chip != rx_tail) {
		netdev_err(priv->ndev,
			   "RX tail of chip (%d) and ours (%d) inconsistent.\n",
			   rx_tail_chip, rx_tail);
		return -EILSEQ;
	}

	return 0;
}

static int
mcp251xfd_get_rx_len(const struct mcp251xfd_priv *priv,
		     const struct mcp251xfd_rx_ring *ring,
		     u8 *len_p)
{
	const u8 shift = ring->obj_num_shift_to_u8;
	u8 chip_head, tail, len;
	u32 fifo_sta;
	int err;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_FIFOSTA(ring->fifo_nr),
			  &fifo_sta);
	if (err)
		return err;

	if (mcp251xfd_rx_fifo_sta_empty(fifo_sta)) {
		*len_p = 0;
		return 0;
	}

	if (mcp251xfd_rx_fifo_sta_full(fifo_sta)) {
		*len_p = ring->obj_num;
		return 0;
	}

	chip_head = FIELD_GET(MCP251XFD_REG_FIFOSTA_FIFOCI_MASK, fifo_sta);

	err =  mcp251xfd_check_rx_tail(priv, ring);
	if (err)
		return err;
	tail = mcp251xfd_get_rx_tail(ring);

	/* First shift to full u8. The subtraction works on signed
	 * values, that keeps the difference steady around the u8
	 * overflow. The right shift acts on len, which is an u8.
	 */
	BUILD_BUG_ON(sizeof(ring->obj_num) != sizeof(chip_head));
	BUILD_BUG_ON(sizeof(ring->obj_num) != sizeof(tail));
	BUILD_BUG_ON(sizeof(ring->obj_num) != sizeof(len));

	len = (chip_head << shift) - (tail << shift);
	*len_p = len >> shift;

	return 0;
}

static void
mcp251xfd_hw_rx_obj_to_skb(const struct mcp251xfd_priv *priv,
			   const struct mcp251xfd_hw_rx_obj_canfd *hw_rx_obj,
			   struct sk_buff *skb)
{
	struct canfd_frame *cfd = (struct canfd_frame *)skb->data;
	u8 dlc;

	if (hw_rx_obj->flags & MCP251XFD_OBJ_FLAGS_IDE) {
		u32 sid, eid;

		eid = FIELD_GET(MCP251XFD_OBJ_ID_EID_MASK, hw_rx_obj->id);
		sid = FIELD_GET(MCP251XFD_OBJ_ID_SID_MASK, hw_rx_obj->id);

		cfd->can_id = CAN_EFF_FLAG |
			FIELD_PREP(MCP251XFD_REG_FRAME_EFF_EID_MASK, eid) |
			FIELD_PREP(MCP251XFD_REG_FRAME_EFF_SID_MASK, sid);
	} else {
		cfd->can_id = FIELD_GET(MCP251XFD_OBJ_ID_SID_MASK,
					hw_rx_obj->id);
	}

	dlc = FIELD_GET(MCP251XFD_OBJ_FLAGS_DLC_MASK, hw_rx_obj->flags);

	/* CANFD */
	if (hw_rx_obj->flags & MCP251XFD_OBJ_FLAGS_FDF) {
		if (hw_rx_obj->flags & MCP251XFD_OBJ_FLAGS_ESI)
			cfd->flags |= CANFD_ESI;

		if (hw_rx_obj->flags & MCP251XFD_OBJ_FLAGS_BRS)
			cfd->flags |= CANFD_BRS;

		cfd->len = can_fd_dlc2len(dlc);
	} else {
		if (hw_rx_obj->flags & MCP251XFD_OBJ_FLAGS_RTR)
			cfd->can_id |= CAN_RTR_FLAG;

		can_frame_set_cc_len((struct can_frame *)cfd, dlc,
				     priv->can.ctrlmode);
	}

	if (!(hw_rx_obj->flags & MCP251XFD_OBJ_FLAGS_RTR))
		memcpy(cfd->data, hw_rx_obj->data, cfd->len);
}

static int
mcp251xfd_handle_rxif_one(struct mcp251xfd_priv *priv,
			  struct mcp251xfd_rx_ring *ring,
			  const struct mcp251xfd_hw_rx_obj_canfd *hw_rx_obj)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct sk_buff *skb;
	struct canfd_frame *cfd;
	u64 timestamp;
	int err;

	/* According to mcp2518fd erratum DS80000789E 6. the FIFOCI
	 * bits of a FIFOSTA register, here the RX FIFO head index
	 * might be corrupted and we might process past the RX FIFO's
	 * head into old CAN frames.
	 *
	 * Compare the timestamp of currently processed CAN frame with
	 * last valid frame received. Abort with -EBADMSG if an old
	 * CAN frame is detected.
	 */
	timestamp = timecounter_cyc2time(&priv->tc, hw_rx_obj->ts);
	if (timestamp <= ring->last_valid) {
		stats->rx_fifo_errors++;

		return -EBADMSG;
	}
	ring->last_valid = timestamp;

	if (hw_rx_obj->flags & MCP251XFD_OBJ_FLAGS_FDF)
		skb = alloc_canfd_skb(priv->ndev, &cfd);
	else
		skb = alloc_can_skb(priv->ndev, (struct can_frame **)&cfd);

	if (!skb) {
		stats->rx_dropped++;
		return 0;
	}

	mcp251xfd_skb_set_timestamp(skb, timestamp);
	mcp251xfd_hw_rx_obj_to_skb(priv, hw_rx_obj, skb);
	err = can_rx_offload_queue_timestamp(&priv->offload, skb, hw_rx_obj->ts);
	if (err)
		stats->rx_fifo_errors++;

	return 0;
}

static inline int
mcp251xfd_rx_obj_read(const struct mcp251xfd_priv *priv,
		      const struct mcp251xfd_rx_ring *ring,
		      struct mcp251xfd_hw_rx_obj_canfd *hw_rx_obj,
		      const u8 offset, const u8 len)
{
	const int val_bytes = regmap_get_val_bytes(priv->map_rx);
	int err;

	err = regmap_bulk_read(priv->map_rx,
			       mcp251xfd_get_rx_obj_addr(ring, offset),
			       hw_rx_obj,
			       len * ring->obj_size / val_bytes);

	return err;
}

static int
mcp251xfd_handle_rxif_ring_uinc(const struct mcp251xfd_priv *priv,
				struct mcp251xfd_rx_ring *ring,
				u8 len)
{
	int offset;
	int err;

	if (!len)
		return 0;

	ring->head += len;

	/* Increment the RX FIFO tail pointer 'len' times in a
	 * single SPI message.
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

	ring->tail += len;

	return 0;
}

static int
mcp251xfd_handle_rxif_ring(struct mcp251xfd_priv *priv,
			   struct mcp251xfd_rx_ring *ring)
{
	struct mcp251xfd_hw_rx_obj_canfd *hw_rx_obj = ring->obj;
	u8 rx_tail, len, l;
	int err, i;

	err = mcp251xfd_get_rx_len(priv, ring, &len);
	if (err)
		return err;

	while ((l = mcp251xfd_get_rx_linear_len(ring, len))) {
		rx_tail = mcp251xfd_get_rx_tail(ring);

		err = mcp251xfd_rx_obj_read(priv, ring, hw_rx_obj,
					    rx_tail, l);
		if (err)
			return err;

		for (i = 0; i < l; i++) {
			err = mcp251xfd_handle_rxif_one(priv, ring,
							(void *)hw_rx_obj +
							i * ring->obj_size);

			/* -EBADMSG means we're affected by mcp2518fd
			 * erratum DS80000789E 6., i.e. the timestamp
			 * in the RX object is older that the last
			 * valid received CAN frame. Don't process any
			 * further and mark processed frames as good.
			 */
			if (err == -EBADMSG)
				return mcp251xfd_handle_rxif_ring_uinc(priv, ring, i);
			else if (err)
				return err;
		}

		err = mcp251xfd_handle_rxif_ring_uinc(priv, ring, l);
		if (err)
			return err;

		len -= l;
	}

	return 0;
}

int mcp251xfd_handle_rxif(struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_rx_ring *ring;
	int err, n;

	mcp251xfd_for_each_rx_ring(priv, ring, n) {
		/* - if RX IRQ coalescing is active always handle ring 0
		 * - only handle rings if RX IRQ is active
		 */
		if ((ring->nr > 0 || !priv->rx_obj_num_coalesce_irq) &&
		    !(priv->regs_status.rxif & BIT(ring->fifo_nr)))
			continue;

		err = mcp251xfd_handle_rxif_ring(priv, ring);
		if (err)
			return err;
	}

	if (priv->rx_coalesce_usecs_irq)
		hrtimer_start(&priv->rx_irq_timer,
			      ns_to_ktime(priv->rx_coalesce_usecs_irq *
					  NSEC_PER_USEC),
			      HRTIMER_MODE_REL);

	return 0;
}
