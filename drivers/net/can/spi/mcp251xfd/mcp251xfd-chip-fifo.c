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

#include <linux/bitfield.h>

#include "mcp251xfd.h"

static int
mcp251xfd_chip_rx_fifo_init_one(const struct mcp251xfd_priv *priv,
				const struct mcp251xfd_rx_ring *ring)
{
	u32 fifo_con;

	/* Enable RXOVIE on _all_ RX FIFOs, not just the last one.
	 *
	 * FIFOs hit by a RX MAB overflow and RXOVIE enabled will
	 * generate a RXOVIF, use this to properly detect RX MAB
	 * overflows.
	 */
	fifo_con = FIELD_PREP(MCP251XFD_REG_FIFOCON_FSIZE_MASK,
			      ring->obj_num - 1) |
		MCP251XFD_REG_FIFOCON_RXTSEN |
		MCP251XFD_REG_FIFOCON_RXOVIE |
		MCP251XFD_REG_FIFOCON_TFNRFNIE;

	if (mcp251xfd_is_fd_mode(priv))
		fifo_con |= FIELD_PREP(MCP251XFD_REG_FIFOCON_PLSIZE_MASK,
				       MCP251XFD_REG_FIFOCON_PLSIZE_64);
	else
		fifo_con |= FIELD_PREP(MCP251XFD_REG_FIFOCON_PLSIZE_MASK,
				       MCP251XFD_REG_FIFOCON_PLSIZE_8);

	return regmap_write(priv->map_reg,
			    MCP251XFD_REG_FIFOCON(ring->fifo_nr), fifo_con);
}

static int
mcp251xfd_chip_rx_filter_init_one(const struct mcp251xfd_priv *priv,
				  const struct mcp251xfd_rx_ring *ring)
{
	u32 fltcon;

	fltcon = MCP251XFD_REG_FLTCON_FLTEN(ring->nr) |
		MCP251XFD_REG_FLTCON_FBP(ring->nr, ring->fifo_nr);

	return regmap_update_bits(priv->map_reg,
				  MCP251XFD_REG_FLTCON(ring->nr >> 2),
				  MCP251XFD_REG_FLTCON_FLT_MASK(ring->nr),
				  fltcon);
}

int mcp251xfd_chip_fifo_init(const struct mcp251xfd_priv *priv)
{
	const struct mcp251xfd_tx_ring *tx_ring = priv->tx;
	const struct mcp251xfd_rx_ring *rx_ring;
	u32 val;
	int err, n;

	/* TEF */
	val = FIELD_PREP(MCP251XFD_REG_TEFCON_FSIZE_MASK,
			 tx_ring->obj_num - 1) |
		MCP251XFD_REG_TEFCON_TEFTSEN |
		MCP251XFD_REG_TEFCON_TEFOVIE |
		MCP251XFD_REG_TEFCON_TEFNEIE;

	err = regmap_write(priv->map_reg, MCP251XFD_REG_TEFCON, val);
	if (err)
		return err;

	/* FIFO 1 - TX */
	val = FIELD_PREP(MCP251XFD_REG_FIFOCON_FSIZE_MASK,
			 tx_ring->obj_num - 1) |
		MCP251XFD_REG_FIFOCON_TXEN |
		MCP251XFD_REG_FIFOCON_TXATIE;

	if (mcp251xfd_is_fd_mode(priv))
		val |= FIELD_PREP(MCP251XFD_REG_FIFOCON_PLSIZE_MASK,
				  MCP251XFD_REG_FIFOCON_PLSIZE_64);
	else
		val |= FIELD_PREP(MCP251XFD_REG_FIFOCON_PLSIZE_MASK,
				  MCP251XFD_REG_FIFOCON_PLSIZE_8);

	if (priv->can.ctrlmode & CAN_CTRLMODE_ONE_SHOT)
		val |= FIELD_PREP(MCP251XFD_REG_FIFOCON_TXAT_MASK,
				  MCP251XFD_REG_FIFOCON_TXAT_ONE_SHOT);
	else
		val |= FIELD_PREP(MCP251XFD_REG_FIFOCON_TXAT_MASK,
				  MCP251XFD_REG_FIFOCON_TXAT_UNLIMITED);

	err = regmap_write(priv->map_reg,
			   MCP251XFD_REG_FIFOCON(MCP251XFD_TX_FIFO),
			   val);
	if (err)
		return err;

	/* RX FIFOs */
	mcp251xfd_for_each_rx_ring(priv, rx_ring, n) {
		err = mcp251xfd_chip_rx_fifo_init_one(priv, rx_ring);
		if (err)
			return err;

		err = mcp251xfd_chip_rx_filter_init_one(priv, rx_ring);
		if (err)
			return err;
	}

	return 0;
}
