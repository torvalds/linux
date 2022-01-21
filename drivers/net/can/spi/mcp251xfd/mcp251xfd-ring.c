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

	data = mcp251xfd_spi_cmd_write(priv, write_reg_buf, reg + first_byte);
	val_le32 = cpu_to_le32(val >> BITS_PER_BYTE * first_byte);
	memcpy(data, &val_le32, len);

	if (priv->devtype_data.quirks & MCP251XFD_QUIRK_CRC_REG) {
		u16 crc;

		mcp251xfd_spi_cmd_crc_set_len_in_reg(&write_reg_buf->crc.cmd,
						     len);
		/* CRC */
		len += sizeof(write_reg_buf->crc.cmd);
		crc = mcp251xfd_crc16_compute(&write_reg_buf->crc, len);
		put_unaligned_be16(crc, (void *)write_reg_buf + len);

		/* Total length */
		len += sizeof(write_reg_buf->crc.crc);
	} else {
		len += sizeof(write_reg_buf->nocrc.cmd);
	}

	return len;
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

void mcp251xfd_ring_init(struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_tef_ring *tef_ring;
	struct mcp251xfd_tx_ring *tx_ring;
	struct mcp251xfd_rx_ring *rx_ring, *prev_rx_ring = NULL;
	struct mcp251xfd_tx_obj *tx_obj;
	struct spi_transfer *xfer;
	u32 val;
	u16 addr;
	u8 len;
	int i, j;

	netdev_reset_queue(priv->ndev);

	/* TEF */
	tef_ring = priv->tef;
	tef_ring->head = 0;
	tef_ring->tail = 0;

	/* FIFO increment TEF tail pointer */
	addr = MCP251XFD_REG_TEFCON;
	val = MCP251XFD_REG_TEFCON_UINC;
	len = mcp251xfd_cmd_prepare_write_reg(priv, &tef_ring->uinc_buf,
					      addr, val, val);

	for (j = 0; j < ARRAY_SIZE(tef_ring->uinc_xfer); j++) {
		xfer = &tef_ring->uinc_xfer[j];
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

	/* TX */
	tx_ring = priv->tx;
	tx_ring->head = 0;
	tx_ring->tail = 0;
	tx_ring->base = mcp251xfd_get_tef_obj_addr(tx_ring->obj_num);

	/* FIFO request to send */
	addr = MCP251XFD_REG_FIFOCON(MCP251XFD_TX_FIFO);
	val = MCP251XFD_REG_FIFOCON_TXREQ | MCP251XFD_REG_FIFOCON_UINC;
	len = mcp251xfd_cmd_prepare_write_reg(priv, &tx_ring->rts_buf,
					      addr, val, val);

	mcp251xfd_for_each_tx_obj(tx_ring, tx_obj, i)
		mcp251xfd_tx_ring_init_tx_obj(priv, tx_ring, tx_obj, len, i);

	/* RX */
	mcp251xfd_for_each_rx_ring(priv, rx_ring, i) {
		rx_ring->head = 0;
		rx_ring->tail = 0;
		rx_ring->nr = i;
		rx_ring->fifo_nr = MCP251XFD_RX_FIFO(i);

		if (!prev_rx_ring)
			rx_ring->base =
				mcp251xfd_get_tx_obj_addr(tx_ring,
							  tx_ring->obj_num);
		else
			rx_ring->base = prev_rx_ring->base +
				prev_rx_ring->obj_size *
				prev_rx_ring->obj_num;

		prev_rx_ring = rx_ring;

		/* FIFO increment RX tail pointer */
		addr = MCP251XFD_REG_FIFOCON(rx_ring->fifo_nr);
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
	}
}

void mcp251xfd_ring_free(struct mcp251xfd_priv *priv)
{
	int i;

	for (i = ARRAY_SIZE(priv->rx) - 1; i >= 0; i--) {
		kfree(priv->rx[i]);
		priv->rx[i] = NULL;
	}
}

int mcp251xfd_ring_alloc(struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_tx_ring *tx_ring;
	struct mcp251xfd_rx_ring *rx_ring;
	int tef_obj_size, tx_obj_size, rx_obj_size;
	int tx_obj_num;
	int ram_free, i;

	tef_obj_size = sizeof(struct mcp251xfd_hw_tef_obj);
	if (mcp251xfd_is_fd_mode(priv)) {
		tx_obj_num = MCP251XFD_TX_OBJ_NUM_CANFD;
		tx_obj_size = sizeof(struct mcp251xfd_hw_tx_obj_canfd);
		rx_obj_size = sizeof(struct mcp251xfd_hw_rx_obj_canfd);
	} else {
		tx_obj_num = MCP251XFD_TX_OBJ_NUM_CAN;
		tx_obj_size = sizeof(struct mcp251xfd_hw_tx_obj_can);
		rx_obj_size = sizeof(struct mcp251xfd_hw_rx_obj_can);
	}

	tx_ring = priv->tx;
	tx_ring->obj_num = tx_obj_num;
	tx_ring->obj_size = tx_obj_size;

	ram_free = MCP251XFD_RAM_SIZE - tx_obj_num *
		(tef_obj_size + tx_obj_size);

	for (i = 0;
	     i < ARRAY_SIZE(priv->rx) && ram_free >= rx_obj_size;
	     i++) {
		int rx_obj_num;

		rx_obj_num = ram_free / rx_obj_size;
		rx_obj_num = min(1 << (fls(rx_obj_num) - 1),
				 MCP251XFD_RX_OBJ_NUM_MAX);

		rx_ring = kzalloc(sizeof(*rx_ring) + rx_obj_size * rx_obj_num,
				  GFP_KERNEL);
		if (!rx_ring) {
			mcp251xfd_ring_free(priv);
			return -ENOMEM;
		}
		rx_ring->obj_num = rx_obj_num;
		rx_ring->obj_size = rx_obj_size;
		priv->rx[i] = rx_ring;

		ram_free -= rx_ring->obj_num * rx_ring->obj_size;
	}
	priv->rx_ring_num = i;

	netdev_dbg(priv->ndev,
		   "FIFO setup: TEF: %d*%d bytes = %d bytes, TX: %d*%d bytes = %d bytes\n",
		   tx_obj_num, tef_obj_size, tef_obj_size * tx_obj_num,
		   tx_obj_num, tx_obj_size, tx_obj_size * tx_obj_num);

	mcp251xfd_for_each_rx_ring(priv, rx_ring, i) {
		netdev_dbg(priv->ndev,
			   "FIFO setup: RX-%d: %d*%d bytes = %d bytes\n",
			   i, rx_ring->obj_num, rx_ring->obj_size,
			   rx_ring->obj_size * rx_ring->obj_num);
	}

	netdev_dbg(priv->ndev,
		   "FIFO setup: free: %d bytes\n",
		   ram_free);

	return 0;
}
