// SPDX-License-Identifier: GPL-2.0
//
// mcp251xfd - Microchip MCP251xFD Family CAN controller driver
//
// Copyright (c) 2019, 2020, 2021 Pengutronix,
//               Marc Kleine-Budde <kernel@pengutronix.de>
//
// Based on:
//
// CAN bus driver for Microchip 25XXFD CAN Controller with SPI Interface
//
// Copyright (c) 2019 Martin Sperl <kernel@martin.sperl.org>
//

#include <asm/unaligned.h>

#include "mcp251xfd.h"
#include "mcp251xfd-ram.h"

static inline u8
mcp251xfd_cmd_prepare_write_reg(const struct mcp251xfd_priv *priv,
				union mcp251xfd_write_reg_buf *write_reg_buf,
				const u16 reg, const u32 mask, const u32 val)
{
	u8 first_byte, last_byte, len;
	u8 *data;
	__le32 val_le32;

	first_byte = mcp251xfd_first_byte_set(mask);
	last_byte = mcp251xfd_last_byte_set(mask);
	len = last_byte - first_byte + 1;

	data = mcp251xfd_spi_cmd_write(priv, write_reg_buf, reg + first_byte, len);
	val_le32 = cpu_to_le32(val >> BITS_PER_BYTE * first_byte);
	memcpy(data, &val_le32, len);

	if (!(priv->devtype_data.quirks & MCP251XFD_QUIRK_CRC_REG)) {
		len += sizeof(write_reg_buf->nocrc.cmd);
	} else if (len == 1) {
		u16 crc;

		/* CRC */
		len += sizeof(write_reg_buf->safe.cmd);
		crc = mcp251xfd_crc16_compute(&write_reg_buf->safe, len);
		put_unaligned_be16(crc, (void *)write_reg_buf + len);

		/* Total length */
		len += sizeof(write_reg_buf->safe.crc);
	} else {
		u16 crc;

		mcp251xfd_spi_cmd_crc_set_len_in_reg(&write_reg_buf->crc.cmd,
						     len);
		/* CRC */
		len += sizeof(write_reg_buf->crc.cmd);
		crc = mcp251xfd_crc16_compute(&write_reg_buf->crc, len);
		put_unaligned_be16(crc, (void *)write_reg_buf + len);

		/* Total length */
		len += sizeof(write_reg_buf->crc.crc);
	}

	return len;
}

static void
mcp251xfd_ring_init_tef(struct mcp251xfd_priv *priv, u16 *base)
{
	struct mcp251xfd_tef_ring *tef_ring;
	struct spi_transfer *xfer;
	u32 val;
	u16 addr;
	u8 len;
	int i;

	/* TEF */
	tef_ring = priv->tef;
	tef_ring->head = 0;
	tef_ring->tail = 0;

	/* TEF- and TX-FIFO have same number of objects */
	*base = mcp251xfd_get_tef_obj_addr(priv->tx->obj_num);

	/* FIFO IRQ enable */
	addr = MCP251XFD_REG_TEFCON;
	val = MCP251XFD_REG_TEFCON_TEFOVIE | MCP251XFD_REG_TEFCON_TEFNEIE;

	len = mcp251xfd_cmd_prepare_write_reg(priv, &tef_ring->irq_enable_buf,
					      addr, val, val);
	tef_ring->irq_enable_xfer.tx_buf = &tef_ring->irq_enable_buf;
	tef_ring->irq_enable_xfer.len = len;
	spi_message_init_with_transfers(&tef_ring->irq_enable_msg,
					&tef_ring->irq_enable_xfer, 1);

	/* FIFO increment TEF tail pointer */
	addr = MCP251XFD_REG_TEFCON;
	val = MCP251XFD_REG_TEFCON_UINC;
	len = mcp251xfd_cmd_prepare_write_reg(priv, &tef_ring->uinc_buf,
					      addr, val, val);

	for (i = 0; i < ARRAY_SIZE(tef_ring->uinc_xfer); i++) {
		xfer = &tef_ring->uinc_xfer[i];
		xfer->tx_buf = &tef_ring->uinc_buf;
		xfer->len = len;
		xfer->cs_change = 1;
		xfer->cs_change_delay.value = 0;
		xfer->cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;
	}

	/* "cs_change == 1" on the last transfer results in an active
	 * chip select after the complete SPI message. This causes the
	 * controller to interpret the next register access as
	 * data. Set "cs_change" of the last transfer to "0" to
	 * properly deactivate the chip select at the end of the
	 * message.
	 */
	xfer->cs_change = 0;

	if (priv->tx_coalesce_usecs_irq || priv->tx_obj_num_coalesce_irq) {
		val = MCP251XFD_REG_TEFCON_UINC |
			MCP251XFD_REG_TEFCON_TEFOVIE |
			MCP251XFD_REG_TEFCON_TEFHIE;

		len = mcp251xfd_cmd_prepare_write_reg(priv,
						      &tef_ring->uinc_irq_disable_buf,
						      addr, val, val);
		xfer->tx_buf = &tef_ring->uinc_irq_disable_buf;
		xfer->len = len;
	}
}

static void
mcp251xfd_tx_ring_init_tx_obj(const struct mcp251xfd_priv *priv,
			      const struct mcp251xfd_tx_ring *ring,
			      struct mcp251xfd_tx_obj *tx_obj,
			      const u8 rts_buf_len,
			      const u8 n)
{
	struct spi_transfer *xfer;
	u16 addr;

	/* FIFO load */
	addr = mcp251xfd_get_tx_obj_addr(ring, n);
	if (priv->devtype_data.quirks & MCP251XFD_QUIRK_CRC_TX)
		mcp251xfd_spi_cmd_write_crc_set_addr(&tx_obj->buf.crc.cmd,
						     addr);
	else
		mcp251xfd_spi_cmd_write_nocrc(&tx_obj->buf.nocrc.cmd,
					      addr);

	xfer = &tx_obj->xfer[0];
	xfer->tx_buf = &tx_obj->buf;
	xfer->len = 0;	/* actual len is assigned on the fly */
	xfer->cs_change = 1;
	xfer->cs_change_delay.value = 0;
	xfer->cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;

	/* FIFO request to send */
	xfer = &tx_obj->xfer[1];
	xfer->tx_buf = &ring->rts_buf;
	xfer->len = rts_buf_len;

	/* SPI message */
	spi_message_init_with_transfers(&tx_obj->msg, tx_obj->xfer,
					ARRAY_SIZE(tx_obj->xfer));
}

static void
mcp251xfd_ring_init_tx(struct mcp251xfd_priv *priv, u16 *base, u8 *fifo_nr)
{
	struct mcp251xfd_tx_ring *tx_ring;
	struct mcp251xfd_tx_obj *tx_obj;
	u32 val;
	u16 addr;
	u8 len;
	int i;

	tx_ring = priv->tx;
	tx_ring->head = 0;
	tx_ring->tail = 0;
	tx_ring->base = *base;
	tx_ring->nr = 0;
	tx_ring->fifo_nr = *fifo_nr;

	*base = mcp251xfd_get_tx_obj_addr(tx_ring, tx_ring->obj_num);
	*fifo_nr += 1;

	/* FIFO request to send */
	addr = MCP251XFD_REG_FIFOCON(tx_ring->fifo_nr);
	val = MCP251XFD_REG_FIFOCON_TXREQ | MCP251XFD_REG_FIFOCON_UINC;
	len = mcp251xfd_cmd_prepare_write_reg(priv, &tx_ring->rts_buf,
					      addr, val, val);

	mcp251xfd_for_each_tx_obj(tx_ring, tx_obj, i)
		mcp251xfd_tx_ring_init_tx_obj(priv, tx_ring, tx_obj, len, i);
}

static void
mcp251xfd_ring_init_rx(struct mcp251xfd_priv *priv, u16 *base, u8 *fifo_nr)
{
	struct mcp251xfd_rx_ring *rx_ring;
	struct spi_transfer *xfer;
	u32 val;
	u16 addr;
	u8 len;
	int i, j;

	mcp251xfd_for_each_rx_ring(priv, rx_ring, i) {
		rx_ring->head = 0;
		rx_ring->tail = 0;
		rx_ring->base = *base;
		rx_ring->nr = i;
		rx_ring->fifo_nr = *fifo_nr;

		*base = mcp251xfd_get_rx_obj_addr(rx_ring, rx_ring->obj_num);
		*fifo_nr += 1;

		/* FIFO IRQ enable */
		addr = MCP251XFD_REG_FIFOCON(rx_ring->fifo_nr);
		val = MCP251XFD_REG_FIFOCON_RXOVIE |
			MCP251XFD_REG_FIFOCON_TFNRFNIE;
		len = mcp251xfd_cmd_prepare_write_reg(priv, &rx_ring->irq_enable_buf,
						      addr, val, val);
		rx_ring->irq_enable_xfer.tx_buf = &rx_ring->irq_enable_buf;
		rx_ring->irq_enable_xfer.len = len;
		spi_message_init_with_transfers(&rx_ring->irq_enable_msg,
						&rx_ring->irq_enable_xfer, 1);

		/* FIFO increment RX tail pointer */
		val = MCP251XFD_REG_FIFOCON_UINC;
		len = mcp251xfd_cmd_prepare_write_reg(priv, &rx_ring->uinc_buf,
						      addr, val, val);

		for (j = 0; j < ARRAY_SIZE(rx_ring->uinc_xfer); j++) {
			xfer = &rx_ring->uinc_xfer[j];
			xfer->tx_buf = &rx_ring->uinc_buf;
			xfer->len = len;
			xfer->cs_change = 1;
			xfer->cs_change_delay.value = 0;
			xfer->cs_change_delay.unit = SPI_DELAY_UNIT_NSECS;
		}

		/* "cs_change == 1" on the last transfer results in an
		 * active chip select after the complete SPI
		 * message. This causes the controller to interpret
		 * the next register access as data. Set "cs_change"
		 * of the last transfer to "0" to properly deactivate
		 * the chip select at the end of the message.
		 */
		xfer->cs_change = 0;

		/* Use 1st RX-FIFO for IRQ coalescing. If enabled
		 * (rx_coalesce_usecs_irq or rx_max_coalesce_frames_irq
		 * is activated), use the last transfer to disable:
		 *
		 * - TFNRFNIE (Receive FIFO Not Empty Interrupt)
		 *
		 * and enable:
		 *
		 * - TFHRFHIE (Receive FIFO Half Full Interrupt)
		 *   - or -
		 * - TFERFFIE (Receive FIFO Full Interrupt)
		 *
		 * depending on rx_max_coalesce_frames_irq.
		 *
		 * The RXOVIE (Overflow Interrupt) is always enabled.
		 */
		if (rx_ring->nr == 0 && (priv->rx_coalesce_usecs_irq ||
					 priv->rx_obj_num_coalesce_irq)) {
			val = MCP251XFD_REG_FIFOCON_UINC |
				MCP251XFD_REG_FIFOCON_RXOVIE;

			if (priv->rx_obj_num_coalesce_irq == rx_ring->obj_num)
				val |= MCP251XFD_REG_FIFOCON_TFERFFIE;
			else if (priv->rx_obj_num_coalesce_irq)
				val |= MCP251XFD_REG_FIFOCON_TFHRFHIE;

			len = mcp251xfd_cmd_prepare_write_reg(priv,
							      &rx_ring->uinc_irq_disable_buf,
							      addr, val, val);
			xfer->tx_buf = &rx_ring->uinc_irq_disable_buf;
			xfer->len = len;
		}
	}
}

int mcp251xfd_ring_init(struct mcp251xfd_priv *priv)
{
	const struct mcp251xfd_rx_ring *rx_ring;
	u16 base = 0, ram_used;
	u8 fifo_nr = 1;
	int i;

	netdev_reset_queue(priv->ndev);

	mcp251xfd_ring_init_tef(priv, &base);
	mcp251xfd_ring_init_rx(priv, &base, &fifo_nr);
	mcp251xfd_ring_init_tx(priv, &base, &fifo_nr);

	/* mcp251xfd_handle_rxif() will iterate over all RX rings.
	 * Rings with their corresponding bit set in
	 * priv->regs_status.rxif are read out.
	 *
	 * If the chip is configured for only 1 RX-FIFO, and if there
	 * is an RX interrupt pending (RXIF in INT register is set),
	 * it must be the 1st RX-FIFO.
	 *
	 * We mark the RXIF of the 1st FIFO as pending here, so that
	 * we can skip the read of the RXIF register in
	 * mcp251xfd_read_regs_status() for the 1 RX-FIFO only case.
	 *
	 * If we use more than 1 RX-FIFO, this value gets overwritten
	 * in mcp251xfd_read_regs_status(), so set it unconditionally
	 * here.
	 */
	priv->regs_status.rxif = BIT(priv->rx[0]->fifo_nr);

	if (priv->tx_obj_num_coalesce_irq) {
		netdev_dbg(priv->ndev,
			   "FIFO setup: TEF:         0x%03x: %2d*%zu bytes = %4zu bytes (coalesce)\n",
			   mcp251xfd_get_tef_obj_addr(0),
			   priv->tx_obj_num_coalesce_irq,
			   sizeof(struct mcp251xfd_hw_tef_obj),
			   priv->tx_obj_num_coalesce_irq *
			   sizeof(struct mcp251xfd_hw_tef_obj));

		netdev_dbg(priv->ndev,
			   "                         0x%03x: %2d*%zu bytes = %4zu bytes\n",
			   mcp251xfd_get_tef_obj_addr(priv->tx_obj_num_coalesce_irq),
			   priv->tx->obj_num - priv->tx_obj_num_coalesce_irq,
			   sizeof(struct mcp251xfd_hw_tef_obj),
			   (priv->tx->obj_num - priv->tx_obj_num_coalesce_irq) *
			   sizeof(struct mcp251xfd_hw_tef_obj));
	} else {
		netdev_dbg(priv->ndev,
			   "FIFO setup: TEF:         0x%03x: %2d*%zu bytes = %4zu bytes\n",
			   mcp251xfd_get_tef_obj_addr(0),
			   priv->tx->obj_num, sizeof(struct mcp251xfd_hw_tef_obj),
			   priv->tx->obj_num * sizeof(struct mcp251xfd_hw_tef_obj));
	}

	mcp251xfd_for_each_rx_ring(priv, rx_ring, i) {
		if (rx_ring->nr == 0 && priv->rx_obj_num_coalesce_irq) {
			netdev_dbg(priv->ndev,
				   "FIFO setup: RX-%u: FIFO %u/0x%03x: %2u*%u bytes = %4u bytes (coalesce)\n",
				   rx_ring->nr, rx_ring->fifo_nr,
				   mcp251xfd_get_rx_obj_addr(rx_ring, 0),
				   priv->rx_obj_num_coalesce_irq, rx_ring->obj_size,
				   priv->rx_obj_num_coalesce_irq * rx_ring->obj_size);

			if (priv->rx_obj_num_coalesce_irq == MCP251XFD_FIFO_DEPTH)
				continue;

			netdev_dbg(priv->ndev,
				   "                         0x%03x: %2u*%u bytes = %4u bytes\n",
				   mcp251xfd_get_rx_obj_addr(rx_ring,
							     priv->rx_obj_num_coalesce_irq),
				   rx_ring->obj_num - priv->rx_obj_num_coalesce_irq,
				   rx_ring->obj_size,
				   (rx_ring->obj_num - priv->rx_obj_num_coalesce_irq) *
				   rx_ring->obj_size);
		} else {
			netdev_dbg(priv->ndev,
				   "FIFO setup: RX-%u: FIFO %u/0x%03x: %2u*%u bytes = %4u bytes\n",
				   rx_ring->nr, rx_ring->fifo_nr,
				   mcp251xfd_get_rx_obj_addr(rx_ring, 0),
				   rx_ring->obj_num, rx_ring->obj_size,
				   rx_ring->obj_num * rx_ring->obj_size);
		}
	}

	netdev_dbg(priv->ndev,
		   "FIFO setup: TX:   FIFO %u/0x%03x: %2u*%u bytes = %4u bytes\n",
		   priv->tx->fifo_nr,
		   mcp251xfd_get_tx_obj_addr(priv->tx, 0),
		   priv->tx->obj_num, priv->tx->obj_size,
		   priv->tx->obj_num * priv->tx->obj_size);

	netdev_dbg(priv->ndev,
		   "FIFO setup: free:                             %4d bytes\n",
		   MCP251XFD_RAM_SIZE - (base - MCP251XFD_RAM_START));

	ram_used = base - MCP251XFD_RAM_START;
	if (ram_used > MCP251XFD_RAM_SIZE) {
		netdev_err(priv->ndev,
			   "Error during ring configuration, using more RAM (%u bytes) than available (%u bytes).\n",
			   ram_used, MCP251XFD_RAM_SIZE);
		return -ENOMEM;
	}

	return 0;
}

void mcp251xfd_ring_free(struct mcp251xfd_priv *priv)
{
	int i;

	for (i = ARRAY_SIZE(priv->rx) - 1; i >= 0; i--) {
		kfree(priv->rx[i]);
		priv->rx[i] = NULL;
	}
}

static enum hrtimer_restart mcp251xfd_rx_irq_timer(struct hrtimer *t)
{
	struct mcp251xfd_priv *priv = container_of(t, struct mcp251xfd_priv,
						   rx_irq_timer);
	struct mcp251xfd_rx_ring *ring = priv->rx[0];

	if (test_bit(MCP251XFD_FLAGS_DOWN, priv->flags))
		return HRTIMER_NORESTART;

	spi_async(priv->spi, &ring->irq_enable_msg);

	return HRTIMER_NORESTART;
}

static enum hrtimer_restart mcp251xfd_tx_irq_timer(struct hrtimer *t)
{
	struct mcp251xfd_priv *priv = container_of(t, struct mcp251xfd_priv,
						   tx_irq_timer);
	struct mcp251xfd_tef_ring *ring = priv->tef;

	if (test_bit(MCP251XFD_FLAGS_DOWN, priv->flags))
		return HRTIMER_NORESTART;

	spi_async(priv->spi, &ring->irq_enable_msg);

	return HRTIMER_NORESTART;
}

const struct can_ram_config mcp251xfd_ram_config = {
	.rx = {
		.size[CAN_RAM_MODE_CAN] = sizeof(struct mcp251xfd_hw_rx_obj_can),
		.size[CAN_RAM_MODE_CANFD] = sizeof(struct mcp251xfd_hw_rx_obj_canfd),
		.min = MCP251XFD_RX_OBJ_NUM_MIN,
		.max = MCP251XFD_RX_OBJ_NUM_MAX,
		.def[CAN_RAM_MODE_CAN] = CAN_RAM_NUM_MAX,
		.def[CAN_RAM_MODE_CANFD] = CAN_RAM_NUM_MAX,
		.fifo_num = MCP251XFD_FIFO_RX_NUM,
		.fifo_depth_min = MCP251XFD_RX_FIFO_DEPTH_MIN,
		.fifo_depth_coalesce_min = MCP251XFD_RX_FIFO_DEPTH_COALESCE_MIN,
	},
	.tx = {
		.size[CAN_RAM_MODE_CAN] = sizeof(struct mcp251xfd_hw_tef_obj) +
			sizeof(struct mcp251xfd_hw_tx_obj_can),
		.size[CAN_RAM_MODE_CANFD] = sizeof(struct mcp251xfd_hw_tef_obj) +
			sizeof(struct mcp251xfd_hw_tx_obj_canfd),
		.min = MCP251XFD_TX_OBJ_NUM_MIN,
		.max = MCP251XFD_TX_OBJ_NUM_MAX,
		.def[CAN_RAM_MODE_CAN] = MCP251XFD_TX_OBJ_NUM_CAN_DEFAULT,
		.def[CAN_RAM_MODE_CANFD] = MCP251XFD_TX_OBJ_NUM_CANFD_DEFAULT,
		.fifo_num = MCP251XFD_FIFO_TX_NUM,
		.fifo_depth_min = MCP251XFD_TX_FIFO_DEPTH_MIN,
		.fifo_depth_coalesce_min = MCP251XFD_TX_FIFO_DEPTH_COALESCE_MIN,
	},
	.size = MCP251XFD_RAM_SIZE,
	.fifo_depth = MCP251XFD_FIFO_DEPTH,
};

int mcp251xfd_ring_alloc(struct mcp251xfd_priv *priv)
{
	const bool fd_mode = mcp251xfd_is_fd_mode(priv);
	struct mcp251xfd_tx_ring *tx_ring = priv->tx;
	struct mcp251xfd_rx_ring *rx_ring;
	u8 tx_obj_size, rx_obj_size;
	u8 rem, i;

	/* switching from CAN-2.0 to CAN-FD mode or vice versa */
	if (fd_mode != test_bit(MCP251XFD_FLAGS_FD_MODE, priv->flags)) {
		struct can_ram_layout layout;

		can_ram_get_layout(&layout, &mcp251xfd_ram_config, NULL, NULL, fd_mode);
		priv->rx_obj_num = layout.default_rx;
		tx_ring->obj_num = layout.default_tx;
	}

	if (fd_mode) {
		tx_obj_size = sizeof(struct mcp251xfd_hw_tx_obj_canfd);
		rx_obj_size = sizeof(struct mcp251xfd_hw_rx_obj_canfd);
		set_bit(MCP251XFD_FLAGS_FD_MODE, priv->flags);
	} else {
		tx_obj_size = sizeof(struct mcp251xfd_hw_tx_obj_can);
		rx_obj_size = sizeof(struct mcp251xfd_hw_rx_obj_can);
		clear_bit(MCP251XFD_FLAGS_FD_MODE, priv->flags);
	}

	tx_ring->obj_size = tx_obj_size;

	rem = priv->rx_obj_num;
	for (i = 0; i < ARRAY_SIZE(priv->rx) && rem; i++) {
		u8 rx_obj_num;

		if (i == 0 && priv->rx_obj_num_coalesce_irq)
			rx_obj_num = min_t(u8, priv->rx_obj_num_coalesce_irq * 2,
					   MCP251XFD_FIFO_DEPTH);
		else
			rx_obj_num = min_t(u8, rounddown_pow_of_two(rem),
					   MCP251XFD_FIFO_DEPTH);
		rem -= rx_obj_num;

		rx_ring = kzalloc(sizeof(*rx_ring) + rx_obj_size * rx_obj_num,
				  GFP_KERNEL);
		if (!rx_ring) {
			mcp251xfd_ring_free(priv);
			return -ENOMEM;
		}

		rx_ring->obj_num = rx_obj_num;
		rx_ring->obj_size = rx_obj_size;
		priv->rx[i] = rx_ring;
	}
	priv->rx_ring_num = i;

	hrtimer_init(&priv->rx_irq_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	priv->rx_irq_timer.function = mcp251xfd_rx_irq_timer;

	hrtimer_init(&priv->tx_irq_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	priv->tx_irq_timer.function = mcp251xfd_tx_irq_timer;

	return 0;
}
