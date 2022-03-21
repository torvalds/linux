// SPDX-License-Identifier: GPL-2.0
//
// mcp251xfd - Microchip MCP251xFD Family CAN controller driver
//
// Copyright (c) 2019, 2020 Pengutronix,
//                          Marc Kleine-Budde <kernel@pengutronix.de>
//
// Based on:
//
// CAN bus driver for Microchip 25XXFD CAN Controller with SPI Interface
//
// Copyright (c) 2019 Martin Sperl <kernel@martin.sperl.org>
//

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>

#include <asm/unaligned.h>

#include "mcp251xfd.h"

#define DEVICE_NAME "mcp251xfd"

static const struct mcp251xfd_devtype_data mcp251xfd_devtype_data_mcp2517fd = {
	.quirks = MCP251XFD_QUIRK_MAB_NO_WARN | MCP251XFD_QUIRK_CRC_REG |
		MCP251XFD_QUIRK_CRC_RX | MCP251XFD_QUIRK_CRC_TX |
		MCP251XFD_QUIRK_ECC,
	.model = MCP251XFD_MODEL_MCP2517FD,
};

static const struct mcp251xfd_devtype_data mcp251xfd_devtype_data_mcp2518fd = {
	.quirks = MCP251XFD_QUIRK_CRC_REG | MCP251XFD_QUIRK_CRC_RX |
		MCP251XFD_QUIRK_CRC_TX | MCP251XFD_QUIRK_ECC,
	.model = MCP251XFD_MODEL_MCP2518FD,
};

/* Autodetect model, start with CRC enabled. */
static const struct mcp251xfd_devtype_data mcp251xfd_devtype_data_mcp251xfd = {
	.quirks = MCP251XFD_QUIRK_CRC_REG | MCP251XFD_QUIRK_CRC_RX |
		MCP251XFD_QUIRK_CRC_TX | MCP251XFD_QUIRK_ECC,
	.model = MCP251XFD_MODEL_MCP251XFD,
};

static const struct can_bittiming_const mcp251xfd_bittiming_const = {
	.name = DEVICE_NAME,
	.tseg1_min = 2,
	.tseg1_max = 256,
	.tseg2_min = 1,
	.tseg2_max = 128,
	.sjw_max = 128,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 1,
};

static const struct can_bittiming_const mcp251xfd_data_bittiming_const = {
	.name = DEVICE_NAME,
	.tseg1_min = 1,
	.tseg1_max = 32,
	.tseg2_min = 1,
	.tseg2_max = 16,
	.sjw_max = 16,
	.brp_min = 1,
	.brp_max = 256,
	.brp_inc = 1,
};

static const char *__mcp251xfd_get_model_str(enum mcp251xfd_model model)
{
	switch (model) {
	case MCP251XFD_MODEL_MCP2517FD:
		return "MCP2517FD";
	case MCP251XFD_MODEL_MCP2518FD:
		return "MCP2518FD";
	case MCP251XFD_MODEL_MCP251XFD:
		return "MCP251xFD";
	}

	return "<unknown>";
}

static inline const char *
mcp251xfd_get_model_str(const struct mcp251xfd_priv *priv)
{
	return __mcp251xfd_get_model_str(priv->devtype_data.model);
}

static const char *mcp251xfd_get_mode_str(const u8 mode)
{
	switch (mode) {
	case MCP251XFD_REG_CON_MODE_MIXED:
		return "Mixed (CAN FD/CAN 2.0)";
	case MCP251XFD_REG_CON_MODE_SLEEP:
		return "Sleep";
	case MCP251XFD_REG_CON_MODE_INT_LOOPBACK:
		return "Internal Loopback";
	case MCP251XFD_REG_CON_MODE_LISTENONLY:
		return "Listen Only";
	case MCP251XFD_REG_CON_MODE_CONFIG:
		return "Configuration";
	case MCP251XFD_REG_CON_MODE_EXT_LOOPBACK:
		return "External Loopback";
	case MCP251XFD_REG_CON_MODE_CAN2_0:
		return "CAN 2.0";
	case MCP251XFD_REG_CON_MODE_RESTRICTED:
		return "Restricted Operation";
	}

	return "<unknown>";
}

static inline int mcp251xfd_vdd_enable(const struct mcp251xfd_priv *priv)
{
	if (!priv->reg_vdd)
		return 0;

	return regulator_enable(priv->reg_vdd);
}

static inline int mcp251xfd_vdd_disable(const struct mcp251xfd_priv *priv)
{
	if (!priv->reg_vdd)
		return 0;

	return regulator_disable(priv->reg_vdd);
}

static inline int
mcp251xfd_transceiver_enable(const struct mcp251xfd_priv *priv)
{
	if (!priv->reg_xceiver)
		return 0;

	return regulator_enable(priv->reg_xceiver);
}

static inline int
mcp251xfd_transceiver_disable(const struct mcp251xfd_priv *priv)
{
	if (!priv->reg_xceiver)
		return 0;

	return regulator_disable(priv->reg_xceiver);
}

static int mcp251xfd_clks_and_vdd_enable(const struct mcp251xfd_priv *priv)
{
	int err;

	err = clk_prepare_enable(priv->clk);
	if (err)
		return err;

	err = mcp251xfd_vdd_enable(priv);
	if (err)
		clk_disable_unprepare(priv->clk);

	/* Wait for oscillator stabilisation time after power up */
	usleep_range(MCP251XFD_OSC_STAB_SLEEP_US,
		     2 * MCP251XFD_OSC_STAB_SLEEP_US);

	return err;
}

static int mcp251xfd_clks_and_vdd_disable(const struct mcp251xfd_priv *priv)
{
	int err;

	err = mcp251xfd_vdd_disable(priv);
	if (err)
		return err;

	clk_disable_unprepare(priv->clk);

	return 0;
}

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

static inline int
mcp251xfd_tx_tail_get_from_chip(const struct mcp251xfd_priv *priv,
				u8 *tx_tail)
{
	u32 fifo_sta;
	int err;

	err = regmap_read(priv->map_reg,
			  MCP251XFD_REG_FIFOSTA(MCP251XFD_TX_FIFO),
			  &fifo_sta);
	if (err)
		return err;

	*tx_tail = FIELD_GET(MCP251XFD_REG_FIFOSTA_FIFOCI_MASK, fifo_sta);

	return 0;
}

static inline int
mcp251xfd_rx_head_get_from_chip(const struct mcp251xfd_priv *priv,
				const struct mcp251xfd_rx_ring *ring,
				u8 *rx_head)
{
	u32 fifo_sta;
	int err;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_FIFOSTA(ring->fifo_nr),
			  &fifo_sta);
	if (err)
		return err;

	*rx_head = FIELD_GET(MCP251XFD_REG_FIFOSTA_FIFOCI_MASK, fifo_sta);

	return 0;
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

static void mcp251xfd_ring_init(struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_tx_ring *tx_ring;
	struct mcp251xfd_rx_ring *rx_ring, *prev_rx_ring = NULL;
	struct mcp251xfd_tx_obj *tx_obj;
	u32 val;
	u16 addr;
	u8 len;
	int i;

	/* TEF */
	priv->tef.head = 0;
	priv->tef.tail = 0;

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
	}
}

static void mcp251xfd_ring_free(struct mcp251xfd_priv *priv)
{
	int i;

	for (i = ARRAY_SIZE(priv->rx) - 1; i >= 0; i--) {
		kfree(priv->rx[i]);
		priv->rx[i] = NULL;
	}
}

static int mcp251xfd_ring_alloc(struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_tx_ring *tx_ring;
	struct mcp251xfd_rx_ring *rx_ring;
	int tef_obj_size, tx_obj_size, rx_obj_size;
	int tx_obj_num;
	int ram_free, i;

	tef_obj_size = sizeof(struct mcp251xfd_hw_tef_obj);
	/* listen-only mode works like FD mode */
	if (priv->can.ctrlmode & (CAN_CTRLMODE_LISTENONLY | CAN_CTRLMODE_FD)) {
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
		rx_obj_num = min(1 << (fls(rx_obj_num) - 1), 32);

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

static inline int
mcp251xfd_chip_get_mode(const struct mcp251xfd_priv *priv, u8 *mode)
{
	u32 val;
	int err;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_CON, &val);
	if (err)
		return err;

	*mode = FIELD_GET(MCP251XFD_REG_CON_OPMOD_MASK, val);

	return 0;
}

static int
__mcp251xfd_chip_set_mode(const struct mcp251xfd_priv *priv,
			  const u8 mode_req, bool nowait)
{
	u32 con, con_reqop;
	int err;

	con_reqop = FIELD_PREP(MCP251XFD_REG_CON_REQOP_MASK, mode_req);
	err = regmap_update_bits(priv->map_reg, MCP251XFD_REG_CON,
				 MCP251XFD_REG_CON_REQOP_MASK, con_reqop);
	if (err)
		return err;

	if (mode_req == MCP251XFD_REG_CON_MODE_SLEEP || nowait)
		return 0;

	err = regmap_read_poll_timeout(priv->map_reg, MCP251XFD_REG_CON, con,
				       FIELD_GET(MCP251XFD_REG_CON_OPMOD_MASK,
						 con) == mode_req,
				       MCP251XFD_POLL_SLEEP_US,
				       MCP251XFD_POLL_TIMEOUT_US);
	if (err) {
		u8 mode = FIELD_GET(MCP251XFD_REG_CON_OPMOD_MASK, con);

		netdev_err(priv->ndev,
			   "Controller failed to enter mode %s Mode (%u) and stays in %s Mode (%u).\n",
			   mcp251xfd_get_mode_str(mode_req), mode_req,
			   mcp251xfd_get_mode_str(mode), mode);
		return err;
	}

	return 0;
}

static inline int
mcp251xfd_chip_set_mode(const struct mcp251xfd_priv *priv,
			const u8 mode_req)
{
	return __mcp251xfd_chip_set_mode(priv, mode_req, false);
}

static inline int
mcp251xfd_chip_set_mode_nowait(const struct mcp251xfd_priv *priv,
			       const u8 mode_req)
{
	return __mcp251xfd_chip_set_mode(priv, mode_req, true);
}

static inline bool mcp251xfd_osc_invalid(u32 reg)
{
	return reg == 0x0 || reg == 0xffffffff;
}

static int mcp251xfd_chip_clock_enable(const struct mcp251xfd_priv *priv)
{
	u32 osc, osc_reference, osc_mask;
	int err;

	/* Set Power On Defaults for "Clock Output Divisor" and remove
	 * "Oscillator Disable" bit.
	 */
	osc = FIELD_PREP(MCP251XFD_REG_OSC_CLKODIV_MASK,
			 MCP251XFD_REG_OSC_CLKODIV_10);
	osc_reference = MCP251XFD_REG_OSC_OSCRDY;
	osc_mask = MCP251XFD_REG_OSC_OSCRDY | MCP251XFD_REG_OSC_PLLRDY;

	/* Note:
	 *
	 * If the controller is in Sleep Mode the following write only
	 * removes the "Oscillator Disable" bit and powers it up. All
	 * other bits are unaffected.
	 */
	err = regmap_write(priv->map_reg, MCP251XFD_REG_OSC, osc);
	if (err)
		return err;

	/* Wait for "Oscillator Ready" bit */
	err = regmap_read_poll_timeout(priv->map_reg, MCP251XFD_REG_OSC, osc,
				       (osc & osc_mask) == osc_reference,
				       MCP251XFD_OSC_STAB_SLEEP_US,
				       MCP251XFD_OSC_STAB_TIMEOUT_US);
	if (mcp251xfd_osc_invalid(osc)) {
		netdev_err(priv->ndev,
			   "Failed to detect %s (osc=0x%08x).\n",
			   mcp251xfd_get_model_str(priv), osc);
		return -ENODEV;
	} else if (err == -ETIMEDOUT) {
		netdev_err(priv->ndev,
			   "Timeout waiting for Oscillator Ready (osc=0x%08x, osc_reference=0x%08x)\n",
			   osc, osc_reference);
		return -ETIMEDOUT;
	} else if (err) {
		return err;
	}

	return 0;
}

static int mcp251xfd_chip_softreset_do(const struct mcp251xfd_priv *priv)
{
	const __be16 cmd = mcp251xfd_cmd_reset();
	int err;

	/* The Set Mode and SPI Reset command only seems to works if
	 * the controller is not in Sleep Mode.
	 */
	err = mcp251xfd_chip_clock_enable(priv);
	if (err)
		return err;

	err = mcp251xfd_chip_set_mode(priv, MCP251XFD_REG_CON_MODE_CONFIG);
	if (err)
		return err;

	/* spi_write_then_read() works with non DMA-safe buffers */
	return spi_write_then_read(priv->spi, &cmd, sizeof(cmd), NULL, 0);
}

static int mcp251xfd_chip_softreset_check(const struct mcp251xfd_priv *priv)
{
	u32 osc, osc_reference;
	u8 mode;
	int err;

	err = mcp251xfd_chip_get_mode(priv, &mode);
	if (err)
		return err;

	if (mode != MCP251XFD_REG_CON_MODE_CONFIG) {
		netdev_info(priv->ndev,
			    "Controller not in Config Mode after reset, but in %s Mode (%u).\n",
			    mcp251xfd_get_mode_str(mode), mode);
		return -ETIMEDOUT;
	}

	osc_reference = MCP251XFD_REG_OSC_OSCRDY |
		FIELD_PREP(MCP251XFD_REG_OSC_CLKODIV_MASK,
			   MCP251XFD_REG_OSC_CLKODIV_10);

	/* check reset defaults of OSC reg */
	err = regmap_read(priv->map_reg, MCP251XFD_REG_OSC, &osc);
	if (err)
		return err;

	if (osc != osc_reference) {
		netdev_info(priv->ndev,
			    "Controller failed to reset. osc=0x%08x, reference value=0x%08x\n",
			    osc, osc_reference);
		return -ETIMEDOUT;
	}

	return 0;
}

static int mcp251xfd_chip_softreset(const struct mcp251xfd_priv *priv)
{
	int err, i;

	for (i = 0; i < MCP251XFD_SOFTRESET_RETRIES_MAX; i++) {
		if (i)
			netdev_info(priv->ndev,
				    "Retrying to reset Controller.\n");

		err = mcp251xfd_chip_softreset_do(priv);
		if (err == -ETIMEDOUT)
			continue;
		if (err)
			return err;

		err = mcp251xfd_chip_softreset_check(priv);
		if (err == -ETIMEDOUT)
			continue;
		if (err)
			return err;

		return 0;
	}

	if (err)
		return err;

	return -ETIMEDOUT;
}

static int mcp251xfd_chip_clock_init(const struct mcp251xfd_priv *priv)
{
	u32 osc;
	int err;

	/* Activate Low Power Mode on Oscillator Disable. This only
	 * works on the MCP2518FD. The MCP2517FD will go into normal
	 * Sleep Mode instead.
	 */
	osc = MCP251XFD_REG_OSC_LPMEN |
		FIELD_PREP(MCP251XFD_REG_OSC_CLKODIV_MASK,
			   MCP251XFD_REG_OSC_CLKODIV_10);
	err = regmap_write(priv->map_reg, MCP251XFD_REG_OSC, osc);
	if (err)
		return err;

	/* Set Time Base Counter Prescaler to 1.
	 *
	 * This means an overflow of the 32 bit Time Base Counter
	 * register at 40 MHz every 107 seconds.
	 */
	return regmap_write(priv->map_reg, MCP251XFD_REG_TSCON,
			    MCP251XFD_REG_TSCON_TBCEN);
}

static int mcp251xfd_set_bittiming(const struct mcp251xfd_priv *priv)
{
	const struct can_bittiming *bt = &priv->can.bittiming;
	const struct can_bittiming *dbt = &priv->can.data_bittiming;
	u32 val = 0;
	s8 tdco;
	int err;

	/* CAN Control Register
	 *
	 * - no transmit bandwidth sharing
	 * - config mode
	 * - disable transmit queue
	 * - store in transmit FIFO event
	 * - transition to restricted operation mode on system error
	 * - ESI is transmitted recessive when ESI of message is high or
	 *   CAN controller error passive
	 * - restricted retransmission attempts,
	 *   use TQXCON_TXAT and FIFOCON_TXAT
	 * - wake-up filter bits T11FILTER
	 * - use CAN bus line filter for wakeup
	 * - protocol exception is treated as a form error
	 * - Do not compare data bytes
	 */
	val = FIELD_PREP(MCP251XFD_REG_CON_REQOP_MASK,
			 MCP251XFD_REG_CON_MODE_CONFIG) |
		MCP251XFD_REG_CON_STEF |
		MCP251XFD_REG_CON_ESIGM |
		MCP251XFD_REG_CON_RTXAT |
		FIELD_PREP(MCP251XFD_REG_CON_WFT_MASK,
			   MCP251XFD_REG_CON_WFT_T11FILTER) |
		MCP251XFD_REG_CON_WAKFIL |
		MCP251XFD_REG_CON_PXEDIS;

	if (!(priv->can.ctrlmode & CAN_CTRLMODE_FD_NON_ISO))
		val |= MCP251XFD_REG_CON_ISOCRCEN;

	err = regmap_write(priv->map_reg, MCP251XFD_REG_CON, val);
	if (err)
		return err;

	/* Nominal Bit Time */
	val = FIELD_PREP(MCP251XFD_REG_NBTCFG_BRP_MASK, bt->brp - 1) |
		FIELD_PREP(MCP251XFD_REG_NBTCFG_TSEG1_MASK,
			   bt->prop_seg + bt->phase_seg1 - 1) |
		FIELD_PREP(MCP251XFD_REG_NBTCFG_TSEG2_MASK,
			   bt->phase_seg2 - 1) |
		FIELD_PREP(MCP251XFD_REG_NBTCFG_SJW_MASK, bt->sjw - 1);

	err = regmap_write(priv->map_reg, MCP251XFD_REG_NBTCFG, val);
	if (err)
		return err;

	if (!(priv->can.ctrlmode & CAN_CTRLMODE_FD))
		return 0;

	/* Data Bit Time */
	val = FIELD_PREP(MCP251XFD_REG_DBTCFG_BRP_MASK, dbt->brp - 1) |
		FIELD_PREP(MCP251XFD_REG_DBTCFG_TSEG1_MASK,
			   dbt->prop_seg + dbt->phase_seg1 - 1) |
		FIELD_PREP(MCP251XFD_REG_DBTCFG_TSEG2_MASK,
			   dbt->phase_seg2 - 1) |
		FIELD_PREP(MCP251XFD_REG_DBTCFG_SJW_MASK, dbt->sjw - 1);

	err = regmap_write(priv->map_reg, MCP251XFD_REG_DBTCFG, val);
	if (err)
		return err;

	/* Transmitter Delay Compensation */
	tdco = clamp_t(int, dbt->brp * (dbt->prop_seg + dbt->phase_seg1),
		       -64, 63);
	val = FIELD_PREP(MCP251XFD_REG_TDC_TDCMOD_MASK,
			 MCP251XFD_REG_TDC_TDCMOD_AUTO) |
		FIELD_PREP(MCP251XFD_REG_TDC_TDCO_MASK, tdco);

	return regmap_write(priv->map_reg, MCP251XFD_REG_TDC, val);
}

static int mcp251xfd_chip_rx_int_enable(const struct mcp251xfd_priv *priv)
{
	u32 val;

	if (!priv->rx_int)
		return 0;

	/* Configure GPIOs:
	 * - PIN0: GPIO Input
	 * - PIN1: GPIO Input/RX Interrupt
	 *
	 * PIN1 must be Input, otherwise there is a glitch on the
	 * rx-INT line. It happens between setting the PIN as output
	 * (in the first byte of the SPI transfer) and configuring the
	 * PIN as interrupt (in the last byte of the SPI transfer).
	 */
	val = MCP251XFD_REG_IOCON_PM0 | MCP251XFD_REG_IOCON_TRIS1 |
		MCP251XFD_REG_IOCON_TRIS0;
	return regmap_write(priv->map_reg, MCP251XFD_REG_IOCON, val);
}

static int mcp251xfd_chip_rx_int_disable(const struct mcp251xfd_priv *priv)
{
	u32 val;

	if (!priv->rx_int)
		return 0;

	/* Configure GPIOs:
	 * - PIN0: GPIO Input
	 * - PIN1: GPIO Input
	 */
	val = MCP251XFD_REG_IOCON_PM1 | MCP251XFD_REG_IOCON_PM0 |
		MCP251XFD_REG_IOCON_TRIS1 | MCP251XFD_REG_IOCON_TRIS0;
	return regmap_write(priv->map_reg, MCP251XFD_REG_IOCON, val);
}

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

	if (priv->can.ctrlmode & (CAN_CTRLMODE_LISTENONLY | CAN_CTRLMODE_FD))
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

static int mcp251xfd_chip_fifo_init(const struct mcp251xfd_priv *priv)
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

	if (priv->can.ctrlmode & (CAN_CTRLMODE_LISTENONLY | CAN_CTRLMODE_FD))
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

static int mcp251xfd_chip_ecc_init(struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_ecc *ecc = &priv->ecc;
	void *ram;
	u32 val = 0;
	int err;

	ecc->ecc_stat = 0;

	if (priv->devtype_data.quirks & MCP251XFD_QUIRK_ECC)
		val = MCP251XFD_REG_ECCCON_ECCEN;

	err = regmap_update_bits(priv->map_reg, MCP251XFD_REG_ECCCON,
				 MCP251XFD_REG_ECCCON_ECCEN, val);
	if (err)
		return err;

	ram = kzalloc(MCP251XFD_RAM_SIZE, GFP_KERNEL);
	if (!ram)
		return -ENOMEM;

	err = regmap_raw_write(priv->map_reg, MCP251XFD_RAM_START, ram,
			       MCP251XFD_RAM_SIZE);
	kfree(ram);

	return err;
}

static inline void mcp251xfd_ecc_tefif_successful(struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_ecc *ecc = &priv->ecc;

	ecc->ecc_stat = 0;
}

static u8 mcp251xfd_get_normal_mode(const struct mcp251xfd_priv *priv)
{
	u8 mode;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
		mode = MCP251XFD_REG_CON_MODE_LISTENONLY;
	else if (priv->can.ctrlmode & CAN_CTRLMODE_FD)
		mode = MCP251XFD_REG_CON_MODE_MIXED;
	else
		mode = MCP251XFD_REG_CON_MODE_CAN2_0;

	return mode;
}

static int
__mcp251xfd_chip_set_normal_mode(const struct mcp251xfd_priv *priv,
				 bool nowait)
{
	u8 mode;

	mode = mcp251xfd_get_normal_mode(priv);

	return __mcp251xfd_chip_set_mode(priv, mode, nowait);
}

static inline int
mcp251xfd_chip_set_normal_mode(const struct mcp251xfd_priv *priv)
{
	return __mcp251xfd_chip_set_normal_mode(priv, false);
}

static inline int
mcp251xfd_chip_set_normal_mode_nowait(const struct mcp251xfd_priv *priv)
{
	return __mcp251xfd_chip_set_normal_mode(priv, true);
}

static int mcp251xfd_chip_interrupts_enable(const struct mcp251xfd_priv *priv)
{
	u32 val;
	int err;

	val = MCP251XFD_REG_CRC_FERRIE | MCP251XFD_REG_CRC_CRCERRIE;
	err = regmap_write(priv->map_reg, MCP251XFD_REG_CRC, val);
	if (err)
		return err;

	val = MCP251XFD_REG_ECCCON_DEDIE | MCP251XFD_REG_ECCCON_SECIE;
	err = regmap_update_bits(priv->map_reg, MCP251XFD_REG_ECCCON, val, val);
	if (err)
		return err;

	val = MCP251XFD_REG_INT_CERRIE |
		MCP251XFD_REG_INT_SERRIE |
		MCP251XFD_REG_INT_RXOVIE |
		MCP251XFD_REG_INT_TXATIE |
		MCP251XFD_REG_INT_SPICRCIE |
		MCP251XFD_REG_INT_ECCIE |
		MCP251XFD_REG_INT_TEFIE |
		MCP251XFD_REG_INT_MODIE |
		MCP251XFD_REG_INT_RXIE;

	if (priv->can.ctrlmode & CAN_CTRLMODE_BERR_REPORTING)
		val |= MCP251XFD_REG_INT_IVMIE;

	return regmap_write(priv->map_reg, MCP251XFD_REG_INT, val);
}

static int mcp251xfd_chip_interrupts_disable(const struct mcp251xfd_priv *priv)
{
	int err;
	u32 mask;

	err = regmap_write(priv->map_reg, MCP251XFD_REG_INT, 0);
	if (err)
		return err;

	mask = MCP251XFD_REG_ECCCON_DEDIE | MCP251XFD_REG_ECCCON_SECIE;
	err = regmap_update_bits(priv->map_reg, MCP251XFD_REG_ECCCON,
				 mask, 0x0);
	if (err)
		return err;

	return regmap_write(priv->map_reg, MCP251XFD_REG_CRC, 0);
}

static int mcp251xfd_chip_stop(struct mcp251xfd_priv *priv,
			       const enum can_state state)
{
	priv->can.state = state;

	mcp251xfd_chip_interrupts_disable(priv);
	mcp251xfd_chip_rx_int_disable(priv);
	return mcp251xfd_chip_set_mode(priv, MCP251XFD_REG_CON_MODE_SLEEP);
}

static int mcp251xfd_chip_start(struct mcp251xfd_priv *priv)
{
	int err;

	err = mcp251xfd_chip_softreset(priv);
	if (err)
		goto out_chip_stop;

	err = mcp251xfd_chip_clock_init(priv);
	if (err)
		goto out_chip_stop;

	err = mcp251xfd_set_bittiming(priv);
	if (err)
		goto out_chip_stop;

	err = mcp251xfd_chip_rx_int_enable(priv);
	if (err)
		goto out_chip_stop;

	err = mcp251xfd_chip_ecc_init(priv);
	if (err)
		goto out_chip_stop;

	mcp251xfd_ring_init(priv);

	err = mcp251xfd_chip_fifo_init(priv);
	if (err)
		goto out_chip_stop;

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	err = mcp251xfd_chip_set_normal_mode(priv);
	if (err)
		goto out_chip_stop;

	return 0;

 out_chip_stop:
	mcp251xfd_chip_stop(priv, CAN_STATE_STOPPED);

	return err;
}

static int mcp251xfd_set_mode(struct net_device *ndev, enum can_mode mode)
{
	struct mcp251xfd_priv *priv = netdev_priv(ndev);
	int err;

	switch (mode) {
	case CAN_MODE_START:
		err = mcp251xfd_chip_start(priv);
		if (err)
			return err;

		err = mcp251xfd_chip_interrupts_enable(priv);
		if (err) {
			mcp251xfd_chip_stop(priv, CAN_STATE_STOPPED);
			return err;
		}

		netif_wake_queue(ndev);
		break;

	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int __mcp251xfd_get_berr_counter(const struct net_device *ndev,
					struct can_berr_counter *bec)
{
	const struct mcp251xfd_priv *priv = netdev_priv(ndev);
	u32 trec;
	int err;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_TREC, &trec);
	if (err)
		return err;

	if (trec & MCP251XFD_REG_TREC_TXBO)
		bec->txerr = 256;
	else
		bec->txerr = FIELD_GET(MCP251XFD_REG_TREC_TEC_MASK, trec);
	bec->rxerr = FIELD_GET(MCP251XFD_REG_TREC_REC_MASK, trec);

	return 0;
}

static int mcp251xfd_get_berr_counter(const struct net_device *ndev,
				      struct can_berr_counter *bec)
{
	const struct mcp251xfd_priv *priv = netdev_priv(ndev);

	/* Avoid waking up the controller if the interface is down */
	if (!(ndev->flags & IFF_UP))
		return 0;

	/* The controller is powered down during Bus Off, use saved
	 * bec values.
	 */
	if (priv->can.state == CAN_STATE_BUS_OFF) {
		*bec = priv->bec;
		return 0;
	}

	return __mcp251xfd_get_berr_counter(ndev, bec);
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
mcp251xfd_handle_tefif_recover(const struct mcp251xfd_priv *priv, const u32 seq)
{
	const struct mcp251xfd_tx_ring *tx_ring = priv->tx;
	u32 tef_sta;
	int err;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_TEFSTA, &tef_sta);
	if (err)
		return err;

	if (tef_sta & MCP251XFD_REG_TEFSTA_TEFOVIF) {
		netdev_err(priv->ndev,
			   "Transmit Event FIFO buffer overflow.\n");
		return -ENOBUFS;
	}

	netdev_info(priv->ndev,
		    "Transmit Event FIFO buffer %s. (seq=0x%08x, tef_tail=0x%08x, tef_head=0x%08x, tx_head=0x%08x)\n",
		    tef_sta & MCP251XFD_REG_TEFSTA_TEFFIF ?
		    "full" : tef_sta & MCP251XFD_REG_TEFSTA_TEFNEIF ?
		    "not empty" : "empty",
		    seq, priv->tef.tail, priv->tef.head, tx_ring->head);

	/* The Sequence Number in the TEF doesn't match our tef_tail. */
	return -EAGAIN;
}

static int
mcp251xfd_handle_tefif_one(struct mcp251xfd_priv *priv,
			   const struct mcp251xfd_hw_tef_obj *hw_tef_obj)
{
	struct mcp251xfd_tx_ring *tx_ring = priv->tx;
	struct net_device_stats *stats = &priv->ndev->stats;
	u32 seq, seq_masked, tef_tail_masked;
	int err;

	seq = FIELD_GET(MCP251XFD_OBJ_FLAGS_SEQ_MCP2518FD_MASK,
			hw_tef_obj->flags);

	/* Use the MCP2517FD mask on the MCP2518FD, too. We only
	 * compare 7 bits, this should be enough to detect
	 * net-yet-completed, i.e. old TEF objects.
	 */
	seq_masked = seq &
		field_mask(MCP251XFD_OBJ_FLAGS_SEQ_MCP2517FD_MASK);
	tef_tail_masked = priv->tef.tail &
		field_mask(MCP251XFD_OBJ_FLAGS_SEQ_MCP2517FD_MASK);
	if (seq_masked != tef_tail_masked)
		return mcp251xfd_handle_tefif_recover(priv, seq);

	stats->tx_bytes +=
		can_rx_offload_get_echo_skb(&priv->offload,
					    mcp251xfd_get_tef_tail(priv),
					    hw_tef_obj->ts);
	stats->tx_packets++;

	/* finally increment the TEF pointer */
	err = regmap_update_bits(priv->map_reg, MCP251XFD_REG_TEFCON,
				 GENMASK(15, 8),
				 MCP251XFD_REG_TEFCON_UINC);
	if (err)
		return err;

	priv->tef.tail++;
	tx_ring->tail++;

	return mcp251xfd_check_tef_tail(priv);
}

static int mcp251xfd_tef_ring_update(struct mcp251xfd_priv *priv)
{
	const struct mcp251xfd_tx_ring *tx_ring = priv->tx;
	unsigned int new_head;
	u8 chip_tx_tail;
	int err;

	err = mcp251xfd_tx_tail_get_from_chip(priv, &chip_tx_tail);
	if (err)
		return err;

	/* chip_tx_tail, is the next TX-Object send by the HW.
	 * The new TEF head must be >= the old head, ...
	 */
	new_head = round_down(priv->tef.head, tx_ring->obj_num) + chip_tx_tail;
	if (new_head <= priv->tef.head)
		new_head += tx_ring->obj_num;

	/* ... but it cannot exceed the TX head. */
	priv->tef.head = min(new_head, tx_ring->head);

	return mcp251xfd_check_tef_tail(priv);
}

static inline int
mcp251xfd_tef_obj_read(const struct mcp251xfd_priv *priv,
		       struct mcp251xfd_hw_tef_obj *hw_tef_obj,
		       const u8 offset, const u8 len)
{
	const struct mcp251xfd_tx_ring *tx_ring = priv->tx;

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
				sizeof(*hw_tef_obj) / sizeof(u32) * len);
}

static int mcp251xfd_handle_tefif(struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_hw_tef_obj hw_tef_obj[MCP251XFD_TX_OBJ_NUM_MAX];
	u8 tef_tail, len, l;
	int err, i;

	err = mcp251xfd_tef_ring_update(priv);
	if (err)
		return err;

	tef_tail = mcp251xfd_get_tef_tail(priv);
	len = mcp251xfd_get_tef_len(priv);
	l = mcp251xfd_get_tef_linear_len(priv);
	err = mcp251xfd_tef_obj_read(priv, hw_tef_obj, tef_tail, l);
	if (err)
		return err;

	if (l < len) {
		err = mcp251xfd_tef_obj_read(priv, &hw_tef_obj[l], 0, len - l);
		if (err)
			return err;
	}

	for (i = 0; i < len; i++) {
		err = mcp251xfd_handle_tefif_one(priv, &hw_tef_obj[i]);
		/* -EAGAIN means the Sequence Number in the TEF
		 * doesn't match our tef_tail. This can happen if we
		 * read the TEF objects too early. Leave loop let the
		 * interrupt handler call us again.
		 */
		if (err == -EAGAIN)
			goto out_netif_wake_queue;
		if (err)
			return err;
	}

 out_netif_wake_queue:
	mcp251xfd_ecc_tefif_successful(priv);

	if (mcp251xfd_get_tx_free(priv->tx)) {
		/* Make sure that anybody stopping the queue after
		 * this sees the new tx_ring->tail.
		 */
		smp_mb();
		netif_wake_queue(priv->ndev);
	}

	return 0;
}

static int
mcp251xfd_rx_ring_update(const struct mcp251xfd_priv *priv,
			 struct mcp251xfd_rx_ring *ring)
{
	u32 new_head;
	u8 chip_rx_head;
	int err;

	err = mcp251xfd_rx_head_get_from_chip(priv, ring, &chip_rx_head);
	if (err)
		return err;

	/* chip_rx_head, is the next RX-Object filled by the HW.
	 * The new RX head must be >= the old head.
	 */
	new_head = round_down(ring->head, ring->obj_num) + chip_rx_head;
	if (new_head <= ring->head)
		new_head += ring->obj_num;

	ring->head = new_head;

	return mcp251xfd_check_rx_tail(priv, ring);
}

static void
mcp251xfd_hw_rx_obj_to_skb(const struct mcp251xfd_priv *priv,
			   const struct mcp251xfd_hw_rx_obj_canfd *hw_rx_obj,
			   struct sk_buff *skb)
{
	struct canfd_frame *cfd = (struct canfd_frame *)skb->data;

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

	/* CANFD */
	if (hw_rx_obj->flags & MCP251XFD_OBJ_FLAGS_FDF) {
		u8 dlc;

		if (hw_rx_obj->flags & MCP251XFD_OBJ_FLAGS_ESI)
			cfd->flags |= CANFD_ESI;

		if (hw_rx_obj->flags & MCP251XFD_OBJ_FLAGS_BRS)
			cfd->flags |= CANFD_BRS;

		dlc = FIELD_GET(MCP251XFD_OBJ_FLAGS_DLC, hw_rx_obj->flags);
		cfd->len = can_dlc2len(get_canfd_dlc(dlc));
	} else {
		if (hw_rx_obj->flags & MCP251XFD_OBJ_FLAGS_RTR)
			cfd->can_id |= CAN_RTR_FLAG;

		cfd->len = get_can_dlc(FIELD_GET(MCP251XFD_OBJ_FLAGS_DLC,
						 hw_rx_obj->flags));
	}

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
	int err;

	if (hw_rx_obj->flags & MCP251XFD_OBJ_FLAGS_FDF)
		skb = alloc_canfd_skb(priv->ndev, &cfd);
	else
		skb = alloc_can_skb(priv->ndev, (struct can_frame **)&cfd);

	if (!skb) {
		stats->rx_dropped++;
		return 0;
	}

	mcp251xfd_hw_rx_obj_to_skb(priv, hw_rx_obj, skb);
	err = can_rx_offload_queue_sorted(&priv->offload, skb, hw_rx_obj->ts);
	if (err)
		stats->rx_fifo_errors++;

	ring->tail++;

	/* finally increment the RX pointer */
	return regmap_update_bits(priv->map_reg,
				  MCP251XFD_REG_FIFOCON(ring->fifo_nr),
				  GENMASK(15, 8),
				  MCP251XFD_REG_FIFOCON_UINC);
}

static inline int
mcp251xfd_rx_obj_read(const struct mcp251xfd_priv *priv,
		      const struct mcp251xfd_rx_ring *ring,
		      struct mcp251xfd_hw_rx_obj_canfd *hw_rx_obj,
		      const u8 offset, const u8 len)
{
	int err;

	err = regmap_bulk_read(priv->map_rx,
			       mcp251xfd_get_rx_obj_addr(ring, offset),
			       hw_rx_obj,
			       len * ring->obj_size / sizeof(u32));

	return err;
}

static int
mcp251xfd_handle_rxif_ring(struct mcp251xfd_priv *priv,
			   struct mcp251xfd_rx_ring *ring)
{
	struct mcp251xfd_hw_rx_obj_canfd *hw_rx_obj = ring->obj;
	u8 rx_tail, len;
	int err, i;

	err = mcp251xfd_rx_ring_update(priv, ring);
	if (err)
		return err;

	while ((len = mcp251xfd_get_rx_linear_len(ring))) {
		rx_tail = mcp251xfd_get_rx_tail(ring);

		err = mcp251xfd_rx_obj_read(priv, ring, hw_rx_obj,
					    rx_tail, len);
		if (err)
			return err;

		for (i = 0; i < len; i++) {
			err = mcp251xfd_handle_rxif_one(priv, ring,
							(void *)hw_rx_obj +
							i * ring->obj_size);
			if (err)
				return err;
		}
	}

	return 0;
}

static int mcp251xfd_handle_rxif(struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_rx_ring *ring;
	int err, n;

	mcp251xfd_for_each_rx_ring(priv, ring, n) {
		err = mcp251xfd_handle_rxif_ring(priv, ring);
		if (err)
			return err;
	}

	return 0;
}

static inline int mcp251xfd_get_timestamp(const struct mcp251xfd_priv *priv,
					  u32 *timestamp)
{
	return regmap_read(priv->map_reg, MCP251XFD_REG_TBC, timestamp);
}

static struct sk_buff *
mcp251xfd_alloc_can_err_skb(const struct mcp251xfd_priv *priv,
			    struct can_frame **cf, u32 *timestamp)
{
	int err;

	err = mcp251xfd_get_timestamp(priv, timestamp);
	if (err)
		return NULL;

	return alloc_can_err_skb(priv->ndev, cf);
}

static int mcp251xfd_handle_rxovif(struct mcp251xfd_priv *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct mcp251xfd_rx_ring *ring;
	struct sk_buff *skb;
	struct can_frame *cf;
	u32 timestamp, rxovif;
	int err, i;

	stats->rx_over_errors++;
	stats->rx_errors++;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_RXOVIF, &rxovif);
	if (err)
		return err;

	mcp251xfd_for_each_rx_ring(priv, ring, i) {
		if (!(rxovif & BIT(ring->fifo_nr)))
			continue;

		/* If SERRIF is active, there was a RX MAB overflow. */
		if (priv->regs_status.intf & MCP251XFD_REG_INT_SERRIF) {
			netdev_info(priv->ndev,
				    "RX-%d: MAB overflow detected.\n",
				    ring->nr);
		} else {
			netdev_info(priv->ndev,
				    "RX-%d: FIFO overflow.\n", ring->nr);
		}

		err = regmap_update_bits(priv->map_reg,
					 MCP251XFD_REG_FIFOSTA(ring->fifo_nr),
					 MCP251XFD_REG_FIFOSTA_RXOVIF,
					 0x0);
		if (err)
			return err;
	}

	skb = mcp251xfd_alloc_can_err_skb(priv, &cf, &timestamp);
	if (!skb)
		return 0;

	cf->can_id |= CAN_ERR_CRTL;
	cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;

	err = can_rx_offload_queue_sorted(&priv->offload, skb, timestamp);
	if (err)
		stats->rx_fifo_errors++;

	return 0;
}

static int mcp251xfd_handle_txatif(struct mcp251xfd_priv *priv)
{
	netdev_info(priv->ndev, "%s\n", __func__);

	return 0;
}

static int mcp251xfd_handle_ivmif(struct mcp251xfd_priv *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	u32 bdiag1, timestamp;
	struct sk_buff *skb;
	struct can_frame *cf = NULL;
	int err;

	err = mcp251xfd_get_timestamp(priv, &timestamp);
	if (err)
		return err;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_BDIAG1, &bdiag1);
	if (err)
		return err;

	/* Write 0s to clear error bits, don't write 1s to non active
	 * bits, as they will be set.
	 */
	err = regmap_write(priv->map_reg, MCP251XFD_REG_BDIAG1, 0x0);
	if (err)
		return err;

	priv->can.can_stats.bus_error++;

	skb = alloc_can_err_skb(priv->ndev, &cf);
	if (cf)
		cf->can_id |= CAN_ERR_PROT | CAN_ERR_BUSERROR;

	/* Controller misconfiguration */
	if (WARN_ON(bdiag1 & MCP251XFD_REG_BDIAG1_DLCMM))
		netdev_err(priv->ndev,
			   "recv'd DLC is larger than PLSIZE of FIFO element.");

	/* RX errors */
	if (bdiag1 & (MCP251XFD_REG_BDIAG1_DCRCERR |
		      MCP251XFD_REG_BDIAG1_NCRCERR)) {
		netdev_dbg(priv->ndev, "CRC error\n");

		stats->rx_errors++;
		if (cf)
			cf->data[3] |= CAN_ERR_PROT_LOC_CRC_SEQ;
	}
	if (bdiag1 & (MCP251XFD_REG_BDIAG1_DSTUFERR |
		      MCP251XFD_REG_BDIAG1_NSTUFERR)) {
		netdev_dbg(priv->ndev, "Stuff error\n");

		stats->rx_errors++;
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_STUFF;
	}
	if (bdiag1 & (MCP251XFD_REG_BDIAG1_DFORMERR |
		      MCP251XFD_REG_BDIAG1_NFORMERR)) {
		netdev_dbg(priv->ndev, "Format error\n");

		stats->rx_errors++;
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_FORM;
	}

	/* TX errors */
	if (bdiag1 & MCP251XFD_REG_BDIAG1_NACKERR) {
		netdev_dbg(priv->ndev, "NACK error\n");

		stats->tx_errors++;
		if (cf) {
			cf->can_id |= CAN_ERR_ACK;
			cf->data[2] |= CAN_ERR_PROT_TX;
		}
	}
	if (bdiag1 & (MCP251XFD_REG_BDIAG1_DBIT1ERR |
		      MCP251XFD_REG_BDIAG1_NBIT1ERR)) {
		netdev_dbg(priv->ndev, "Bit1 error\n");

		stats->tx_errors++;
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_TX | CAN_ERR_PROT_BIT1;
	}
	if (bdiag1 & (MCP251XFD_REG_BDIAG1_DBIT0ERR |
		      MCP251XFD_REG_BDIAG1_NBIT0ERR)) {
		netdev_dbg(priv->ndev, "Bit0 error\n");

		stats->tx_errors++;
		if (cf)
			cf->data[2] |= CAN_ERR_PROT_TX | CAN_ERR_PROT_BIT0;
	}

	if (!cf)
		return 0;

	err = can_rx_offload_queue_sorted(&priv->offload, skb, timestamp);
	if (err)
		stats->rx_fifo_errors++;

	return 0;
}

static int mcp251xfd_handle_cerrif(struct mcp251xfd_priv *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct sk_buff *skb;
	struct can_frame *cf = NULL;
	enum can_state new_state, rx_state, tx_state;
	u32 trec, timestamp;
	int err;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_TREC, &trec);
	if (err)
		return err;

	if (trec & MCP251XFD_REG_TREC_TXBO)
		tx_state = CAN_STATE_BUS_OFF;
	else if (trec & MCP251XFD_REG_TREC_TXBP)
		tx_state = CAN_STATE_ERROR_PASSIVE;
	else if (trec & MCP251XFD_REG_TREC_TXWARN)
		tx_state = CAN_STATE_ERROR_WARNING;
	else
		tx_state = CAN_STATE_ERROR_ACTIVE;

	if (trec & MCP251XFD_REG_TREC_RXBP)
		rx_state = CAN_STATE_ERROR_PASSIVE;
	else if (trec & MCP251XFD_REG_TREC_RXWARN)
		rx_state = CAN_STATE_ERROR_WARNING;
	else
		rx_state = CAN_STATE_ERROR_ACTIVE;

	new_state = max(tx_state, rx_state);
	if (new_state == priv->can.state)
		return 0;

	/* The skb allocation might fail, but can_change_state()
	 * handles cf == NULL.
	 */
	skb = mcp251xfd_alloc_can_err_skb(priv, &cf, &timestamp);
	can_change_state(priv->ndev, cf, tx_state, rx_state);

	if (new_state == CAN_STATE_BUS_OFF) {
		/* As we're going to switch off the chip now, let's
		 * save the error counters and return them to
		 * userspace, if do_get_berr_counter() is called while
		 * the chip is in Bus Off.
		 */
		err = __mcp251xfd_get_berr_counter(priv->ndev, &priv->bec);
		if (err)
			return err;

		mcp251xfd_chip_stop(priv, CAN_STATE_BUS_OFF);
		can_bus_off(priv->ndev);
	}

	if (!skb)
		return 0;

	if (new_state != CAN_STATE_BUS_OFF) {
		struct can_berr_counter bec;

		err = mcp251xfd_get_berr_counter(priv->ndev, &bec);
		if (err)
			return err;
		cf->data[6] = bec.txerr;
		cf->data[7] = bec.rxerr;
	}

	err = can_rx_offload_queue_sorted(&priv->offload, skb, timestamp);
	if (err)
		stats->rx_fifo_errors++;

	return 0;
}

static int
mcp251xfd_handle_modif(const struct mcp251xfd_priv *priv, bool *set_normal_mode)
{
	const u8 mode_reference = mcp251xfd_get_normal_mode(priv);
	u8 mode;
	int err;

	err = mcp251xfd_chip_get_mode(priv, &mode);
	if (err)
		return err;

	if (mode == mode_reference) {
		netdev_dbg(priv->ndev,
			   "Controller changed into %s Mode (%u).\n",
			   mcp251xfd_get_mode_str(mode), mode);
		return 0;
	}

	/* According to MCP2517FD errata DS80000792B 1., during a TX
	 * MAB underflow, the controller will transition to Restricted
	 * Operation Mode or Listen Only Mode (depending on SERR2LOM).
	 *
	 * However this is not always the case. If SERR2LOM is
	 * configured for Restricted Operation Mode (SERR2LOM not set)
	 * the MCP2517FD will sometimes transition to Listen Only Mode
	 * first. When polling this bit we see that it will transition
	 * to Restricted Operation Mode shortly after.
	 */
	if ((priv->devtype_data.quirks & MCP251XFD_QUIRK_MAB_NO_WARN) &&
	    (mode == MCP251XFD_REG_CON_MODE_RESTRICTED ||
	     mode == MCP251XFD_REG_CON_MODE_LISTENONLY))
		netdev_dbg(priv->ndev,
			   "Controller changed into %s Mode (%u).\n",
			   mcp251xfd_get_mode_str(mode), mode);
	else
		netdev_err(priv->ndev,
			   "Controller changed into %s Mode (%u).\n",
			   mcp251xfd_get_mode_str(mode), mode);

	/* After the application requests Normal mode, the Controller
	 * will automatically attempt to retransmit the message that
	 * caused the TX MAB underflow.
	 *
	 * However, if there is an ECC error in the TX-RAM, we first
	 * have to reload the tx-object before requesting Normal
	 * mode. This is done later in mcp251xfd_handle_eccif().
	 */
	if (priv->regs_status.intf & MCP251XFD_REG_INT_ECCIF) {
		*set_normal_mode = true;
		return 0;
	}

	return mcp251xfd_chip_set_normal_mode_nowait(priv);
}

static int mcp251xfd_handle_serrif(struct mcp251xfd_priv *priv)
{
	struct mcp251xfd_ecc *ecc = &priv->ecc;
	struct net_device_stats *stats = &priv->ndev->stats;
	bool handled = false;

	/* TX MAB underflow
	 *
	 * According to MCP2517FD Errata DS80000792B 1. a TX MAB
	 * underflow is indicated by SERRIF and MODIF.
	 *
	 * In addition to the effects mentioned in the Errata, there
	 * are Bus Errors due to the aborted CAN frame, so a IVMIF
	 * will be seen as well.
	 *
	 * Sometimes there is an ECC error in the TX-RAM, which leads
	 * to a TX MAB underflow.
	 *
	 * However, probably due to a race condition, there is no
	 * associated MODIF pending.
	 *
	 * Further, there are situations, where the SERRIF is caused
	 * by an ECC error in the TX-RAM, but not even the ECCIF is
	 * set. This only seems to happen _after_ the first occurrence
	 * of a ECCIF (which is tracked in ecc->cnt).
	 *
	 * Treat all as a known system errors..
	 */
	if ((priv->regs_status.intf & MCP251XFD_REG_INT_MODIF &&
	     priv->regs_status.intf & MCP251XFD_REG_INT_IVMIF) ||
	    priv->regs_status.intf & MCP251XFD_REG_INT_ECCIF ||
	    ecc->cnt) {
		const char *msg;

		if (priv->regs_status.intf & MCP251XFD_REG_INT_ECCIF ||
		    ecc->cnt)
			msg = "TX MAB underflow due to ECC error detected.";
		else
			msg = "TX MAB underflow detected.";

		if (priv->devtype_data.quirks & MCP251XFD_QUIRK_MAB_NO_WARN)
			netdev_dbg(priv->ndev, "%s\n", msg);
		else
			netdev_info(priv->ndev, "%s\n", msg);

		stats->tx_aborted_errors++;
		stats->tx_errors++;
		handled = true;
	}

	/* RX MAB overflow
	 *
	 * According to MCP2517FD Errata DS80000792B 1. a RX MAB
	 * overflow is indicated by SERRIF.
	 *
	 * In addition to the effects mentioned in the Errata, (most
	 * of the times) a RXOVIF is raised, if the FIFO that is being
	 * received into has the RXOVIE activated (and we have enabled
	 * RXOVIE on all FIFOs).
	 *
	 * Sometimes there is no RXOVIF just a RXIF is pending.
	 *
	 * Treat all as a known system errors..
	 */
	if (priv->regs_status.intf & MCP251XFD_REG_INT_RXOVIF ||
	    priv->regs_status.intf & MCP251XFD_REG_INT_RXIF) {
		stats->rx_dropped++;
		handled = true;
	}

	if (!handled)
		netdev_err(priv->ndev,
			   "Unhandled System Error Interrupt (intf=0x%08x)!\n",
			   priv->regs_status.intf);

	return 0;
}

static int
mcp251xfd_handle_eccif_recover(struct mcp251xfd_priv *priv, u8 nr)
{
	struct mcp251xfd_tx_ring *tx_ring = priv->tx;
	struct mcp251xfd_ecc *ecc = &priv->ecc;
	struct mcp251xfd_tx_obj *tx_obj;
	u8 chip_tx_tail, tx_tail, offset;
	u16 addr;
	int err;

	addr = FIELD_GET(MCP251XFD_REG_ECCSTAT_ERRADDR_MASK, ecc->ecc_stat);

	err = mcp251xfd_tx_tail_get_from_chip(priv, &chip_tx_tail);
	if (err)
		return err;

	tx_tail = mcp251xfd_get_tx_tail(tx_ring);
	offset = (nr - chip_tx_tail) & (tx_ring->obj_num - 1);

	/* Bail out if one of the following is met:
	 * - tx_tail information is inconsistent
	 * - for mcp2517fd: offset not 0
	 * - for mcp2518fd: offset not 0 or 1
	 */
	if (chip_tx_tail != tx_tail ||
	    !(offset == 0 || (offset == 1 && mcp251xfd_is_2518(priv)))) {
		netdev_err(priv->ndev,
			   "ECC Error information inconsistent (addr=0x%04x, nr=%d, tx_tail=0x%08x(%d), chip_tx_tail=%d, offset=%d).\n",
			   addr, nr, tx_ring->tail, tx_tail, chip_tx_tail,
			   offset);
		return -EINVAL;
	}

	netdev_info(priv->ndev,
		    "Recovering %s ECC Error at address 0x%04x (in TX-RAM, tx_obj=%d, tx_tail=0x%08x(%d), offset=%d).\n",
		    ecc->ecc_stat & MCP251XFD_REG_ECCSTAT_SECIF ?
		    "Single" : "Double",
		    addr, nr, tx_ring->tail, tx_tail, offset);

	/* reload tx_obj into controller RAM ... */
	tx_obj = &tx_ring->obj[nr];
	err = spi_sync_transfer(priv->spi, tx_obj->xfer, 1);
	if (err)
		return err;

	/* ... and trigger retransmit */
	return mcp251xfd_chip_set_normal_mode(priv);
}

static int
mcp251xfd_handle_eccif(struct mcp251xfd_priv *priv, bool set_normal_mode)
{
	struct mcp251xfd_ecc *ecc = &priv->ecc;
	const char *msg;
	bool in_tx_ram;
	u32 ecc_stat;
	u16 addr;
	u8 nr;
	int err;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_ECCSTAT, &ecc_stat);
	if (err)
		return err;

	err = regmap_update_bits(priv->map_reg, MCP251XFD_REG_ECCSTAT,
				 MCP251XFD_REG_ECCSTAT_IF_MASK, ~ecc_stat);
	if (err)
		return err;

	/* Check if ECC error occurred in TX-RAM */
	addr = FIELD_GET(MCP251XFD_REG_ECCSTAT_ERRADDR_MASK, ecc_stat);
	err = mcp251xfd_get_tx_nr_by_addr(priv->tx, &nr, addr);
	if (!err)
		in_tx_ram = true;
	else if (err == -ENOENT)
		in_tx_ram = false;
	else
		return err;

	/* Errata Reference:
	 * mcp2517fd: DS80000789B, mcp2518fd: DS80000792C 2.
	 *
	 * ECC single error correction does not work in all cases:
	 *
	 * Fix/Work Around:
	 * Enable single error correction and double error detection
	 * interrupts by setting SECIE and DEDIE. Handle SECIF as a
	 * detection interrupt and do not rely on the error
	 * correction. Instead, handle both interrupts as a
	 * notification that the RAM word at ERRADDR was corrupted.
	 */
	if (ecc_stat & MCP251XFD_REG_ECCSTAT_SECIF)
		msg = "Single ECC Error detected at address";
	else if (ecc_stat & MCP251XFD_REG_ECCSTAT_DEDIF)
		msg = "Double ECC Error detected at address";
	else
		return -EINVAL;

	if (!in_tx_ram) {
		ecc->ecc_stat = 0;

		netdev_notice(priv->ndev, "%s 0x%04x.\n", msg, addr);
	} else {
		/* Re-occurring error? */
		if (ecc->ecc_stat == ecc_stat) {
			ecc->cnt++;
		} else {
			ecc->ecc_stat = ecc_stat;
			ecc->cnt = 1;
		}

		netdev_info(priv->ndev,
			    "%s 0x%04x (in TX-RAM, tx_obj=%d), occurred %d time%s.\n",
			    msg, addr, nr, ecc->cnt, ecc->cnt > 1 ? "s" : "");

		if (ecc->cnt >= MCP251XFD_ECC_CNT_MAX)
			return mcp251xfd_handle_eccif_recover(priv, nr);
	}

	if (set_normal_mode)
		return mcp251xfd_chip_set_normal_mode_nowait(priv);

	return 0;
}

static int mcp251xfd_handle_spicrcif(struct mcp251xfd_priv *priv)
{
	int err;
	u32 crc;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_CRC, &crc);
	if (err)
		return err;

	err = regmap_update_bits(priv->map_reg, MCP251XFD_REG_CRC,
				 MCP251XFD_REG_CRC_IF_MASK,
				 ~crc);
	if (err)
		return err;

	if (crc & MCP251XFD_REG_CRC_FERRIF)
		netdev_notice(priv->ndev, "CRC write command format error.\n");
	else if (crc & MCP251XFD_REG_CRC_CRCERRIF)
		netdev_notice(priv->ndev,
			      "CRC write error detected. CRC=0x%04lx.\n",
			      FIELD_GET(MCP251XFD_REG_CRC_MASK, crc));

	return 0;
}

#define mcp251xfd_handle(priv, irq, ...) \
({ \
	struct mcp251xfd_priv *_priv = (priv); \
	int err; \
\
	err = mcp251xfd_handle_##irq(_priv, ## __VA_ARGS__); \
	if (err) \
		netdev_err(_priv->ndev, \
			"IRQ handler mcp251xfd_handle_%s() returned %d.\n", \
			__stringify(irq), err); \
	err; \
})

static irqreturn_t mcp251xfd_irq(int irq, void *dev_id)
{
	struct mcp251xfd_priv *priv = dev_id;
	irqreturn_t handled = IRQ_NONE;
	int err;

	if (priv->rx_int)
		do {
			int rx_pending;

			rx_pending = gpiod_get_value_cansleep(priv->rx_int);
			if (!rx_pending)
				break;

			err = mcp251xfd_handle(priv, rxif);
			if (err)
				goto out_fail;

			handled = IRQ_HANDLED;
		} while (1);

	do {
		u32 intf_pending, intf_pending_clearable;
		bool set_normal_mode = false;

		err = regmap_bulk_read(priv->map_reg, MCP251XFD_REG_INT,
				       &priv->regs_status,
				       sizeof(priv->regs_status) /
				       sizeof(u32));
		if (err)
			goto out_fail;

		intf_pending = FIELD_GET(MCP251XFD_REG_INT_IF_MASK,
					 priv->regs_status.intf) &
			FIELD_GET(MCP251XFD_REG_INT_IE_MASK,
				  priv->regs_status.intf);

		if (!(intf_pending))
			return handled;

		/* Some interrupts must be ACKed in the
		 * MCP251XFD_REG_INT register.
		 * - First ACK then handle, to avoid lost-IRQ race
		 *   condition on fast re-occurring interrupts.
		 * - Write "0" to clear active IRQs, "1" to all other,
		 *   to avoid r/m/w race condition on the
		 *   MCP251XFD_REG_INT register.
		 */
		intf_pending_clearable = intf_pending &
			MCP251XFD_REG_INT_IF_CLEARABLE_MASK;
		if (intf_pending_clearable) {
			err = regmap_update_bits(priv->map_reg,
						 MCP251XFD_REG_INT,
						 MCP251XFD_REG_INT_IF_MASK,
						 ~intf_pending_clearable);
			if (err)
				goto out_fail;
		}

		if (intf_pending & MCP251XFD_REG_INT_MODIF) {
			err = mcp251xfd_handle(priv, modif, &set_normal_mode);
			if (err)
				goto out_fail;
		}

		if (intf_pending & MCP251XFD_REG_INT_RXIF) {
			err = mcp251xfd_handle(priv, rxif);
			if (err)
				goto out_fail;
		}

		if (intf_pending & MCP251XFD_REG_INT_TEFIF) {
			err = mcp251xfd_handle(priv, tefif);
			if (err)
				goto out_fail;
		}

		if (intf_pending & MCP251XFD_REG_INT_RXOVIF) {
			err = mcp251xfd_handle(priv, rxovif);
			if (err)
				goto out_fail;
		}

		if (intf_pending & MCP251XFD_REG_INT_TXATIF) {
			err = mcp251xfd_handle(priv, txatif);
			if (err)
				goto out_fail;
		}

		if (intf_pending & MCP251XFD_REG_INT_IVMIF) {
			err = mcp251xfd_handle(priv, ivmif);
			if (err)
				goto out_fail;
		}

		if (intf_pending & MCP251XFD_REG_INT_SERRIF) {
			err = mcp251xfd_handle(priv, serrif);
			if (err)
				goto out_fail;
		}

		if (intf_pending & MCP251XFD_REG_INT_ECCIF) {
			err = mcp251xfd_handle(priv, eccif, set_normal_mode);
			if (err)
				goto out_fail;
		}

		if (intf_pending & MCP251XFD_REG_INT_SPICRCIF) {
			err = mcp251xfd_handle(priv, spicrcif);
			if (err)
				goto out_fail;
		}

		/* On the MCP2527FD and MCP2518FD, we don't get a
		 * CERRIF IRQ on the transition TX ERROR_WARNING -> TX
		 * ERROR_ACTIVE.
		 */
		if (intf_pending & MCP251XFD_REG_INT_CERRIF ||
		    priv->can.state > CAN_STATE_ERROR_ACTIVE) {
			err = mcp251xfd_handle(priv, cerrif);
			if (err)
				goto out_fail;

			/* In Bus Off we completely shut down the
			 * controller. Every subsequent register read
			 * will read bogus data, and if
			 * MCP251XFD_QUIRK_CRC_REG is enabled the CRC
			 * check will fail, too. So leave IRQ handler
			 * directly.
			 */
			if (priv->can.state == CAN_STATE_BUS_OFF)
				return IRQ_HANDLED;
		}

		handled = IRQ_HANDLED;
	} while (1);

 out_fail:
	netdev_err(priv->ndev, "IRQ handler returned %d (intf=0x%08x).\n",
		   err, priv->regs_status.intf);
	mcp251xfd_chip_interrupts_disable(priv);

	return handled;
}

static inline struct
mcp251xfd_tx_obj *mcp251xfd_get_tx_obj_next(struct mcp251xfd_tx_ring *tx_ring)
{
	u8 tx_head;

	tx_head = mcp251xfd_get_tx_head(tx_ring);

	return &tx_ring->obj[tx_head];
}

static void
mcp251xfd_tx_obj_from_skb(const struct mcp251xfd_priv *priv,
			  struct mcp251xfd_tx_obj *tx_obj,
			  const struct sk_buff *skb,
			  unsigned int seq)
{
	const struct canfd_frame *cfd = (struct canfd_frame *)skb->data;
	struct mcp251xfd_hw_tx_obj_raw *hw_tx_obj;
	union mcp251xfd_tx_obj_load_buf *load_buf;
	u8 dlc;
	u32 id, flags;
	int offset, len;

	if (cfd->can_id & CAN_EFF_FLAG) {
		u32 sid, eid;

		sid = FIELD_GET(MCP251XFD_REG_FRAME_EFF_SID_MASK, cfd->can_id);
		eid = FIELD_GET(MCP251XFD_REG_FRAME_EFF_EID_MASK, cfd->can_id);

		id = FIELD_PREP(MCP251XFD_OBJ_ID_EID_MASK, eid) |
			FIELD_PREP(MCP251XFD_OBJ_ID_SID_MASK, sid);

		flags = MCP251XFD_OBJ_FLAGS_IDE;
	} else {
		id = FIELD_PREP(MCP251XFD_OBJ_ID_SID_MASK, cfd->can_id);
		flags = 0;
	}

	/* Use the MCP2518FD mask even on the MCP2517FD. It doesn't
	 * harm, only the lower 7 bits will be transferred into the
	 * TEF object.
	 */
	dlc = can_len2dlc(cfd->len);
	flags |= FIELD_PREP(MCP251XFD_OBJ_FLAGS_SEQ_MCP2518FD_MASK, seq) |
		FIELD_PREP(MCP251XFD_OBJ_FLAGS_DLC, dlc);

	if (cfd->can_id & CAN_RTR_FLAG)
		flags |= MCP251XFD_OBJ_FLAGS_RTR;

	/* CANFD */
	if (can_is_canfd_skb(skb)) {
		if (cfd->flags & CANFD_ESI)
			flags |= MCP251XFD_OBJ_FLAGS_ESI;

		flags |= MCP251XFD_OBJ_FLAGS_FDF;

		if (cfd->flags & CANFD_BRS)
			flags |= MCP251XFD_OBJ_FLAGS_BRS;
	}

	load_buf = &tx_obj->buf;
	if (priv->devtype_data.quirks & MCP251XFD_QUIRK_CRC_TX)
		hw_tx_obj = &load_buf->crc.hw_tx_obj;
	else
		hw_tx_obj = &load_buf->nocrc.hw_tx_obj;

	put_unaligned_le32(id, &hw_tx_obj->id);
	put_unaligned_le32(flags, &hw_tx_obj->flags);

	/* Clear data at end of CAN frame */
	offset = round_down(cfd->len, sizeof(u32));
	len = round_up(can_dlc2len(dlc), sizeof(u32)) - offset;
	if (MCP251XFD_SANITIZE_CAN && len)
		memset(hw_tx_obj->data + offset, 0x0, len);
	memcpy(hw_tx_obj->data, cfd->data, cfd->len);

	/* Number of bytes to be written into the RAM of the controller */
	len = sizeof(hw_tx_obj->id) + sizeof(hw_tx_obj->flags);
	if (MCP251XFD_SANITIZE_CAN)
		len += round_up(can_dlc2len(dlc), sizeof(u32));
	else
		len += round_up(cfd->len, sizeof(u32));

	if (priv->devtype_data.quirks & MCP251XFD_QUIRK_CRC_TX) {
		u16 crc;

		mcp251xfd_spi_cmd_crc_set_len_in_ram(&load_buf->crc.cmd,
						     len);
		/* CRC */
		len += sizeof(load_buf->crc.cmd);
		crc = mcp251xfd_crc16_compute(&load_buf->crc, len);
		put_unaligned_be16(crc, (void *)load_buf + len);

		/* Total length */
		len += sizeof(load_buf->crc.crc);
	} else {
		len += sizeof(load_buf->nocrc.cmd);
	}

	tx_obj->xfer[0].len = len;
}

static int mcp251xfd_tx_obj_write(const struct mcp251xfd_priv *priv,
				  struct mcp251xfd_tx_obj *tx_obj)
{
	return spi_async(priv->spi, &tx_obj->msg);
}

static bool mcp251xfd_tx_busy(const struct mcp251xfd_priv *priv,
			      struct mcp251xfd_tx_ring *tx_ring)
{
	if (mcp251xfd_get_tx_free(tx_ring) > 0)
		return false;

	netif_stop_queue(priv->ndev);

	/* Memory barrier before checking tx_free (head and tail) */
	smp_mb();

	if (mcp251xfd_get_tx_free(tx_ring) == 0) {
		netdev_dbg(priv->ndev,
			   "Stopping tx-queue (tx_head=0x%08x, tx_tail=0x%08x, len=%d).\n",
			   tx_ring->head, tx_ring->tail,
			   tx_ring->head - tx_ring->tail);

		return true;
	}

	netif_start_queue(priv->ndev);

	return false;
}

static netdev_tx_t mcp251xfd_start_xmit(struct sk_buff *skb,
					struct net_device *ndev)
{
	struct mcp251xfd_priv *priv = netdev_priv(ndev);
	struct mcp251xfd_tx_ring *tx_ring = priv->tx;
	struct mcp251xfd_tx_obj *tx_obj;
	u8 tx_head;
	int err;

	if (can_dropped_invalid_skb(ndev, skb))
		return NETDEV_TX_OK;

	if (mcp251xfd_tx_busy(priv, tx_ring))
		return NETDEV_TX_BUSY;

	tx_obj = mcp251xfd_get_tx_obj_next(tx_ring);
	mcp251xfd_tx_obj_from_skb(priv, tx_obj, skb, tx_ring->head);

	/* Stop queue if we occupy the complete TX FIFO */
	tx_head = mcp251xfd_get_tx_head(tx_ring);
	tx_ring->head++;
	if (tx_ring->head - tx_ring->tail >= tx_ring->obj_num)
		netif_stop_queue(ndev);

	can_put_echo_skb(skb, ndev, tx_head);

	err = mcp251xfd_tx_obj_write(priv, tx_obj);
	if (err)
		goto out_err;

	return NETDEV_TX_OK;

 out_err:
	netdev_err(priv->ndev, "ERROR in %s: %d\n", __func__, err);

	return NETDEV_TX_OK;
}

static int mcp251xfd_open(struct net_device *ndev)
{
	struct mcp251xfd_priv *priv = netdev_priv(ndev);
	const struct spi_device *spi = priv->spi;
	int err;

	err = pm_runtime_get_sync(ndev->dev.parent);
	if (err < 0) {
		pm_runtime_put_noidle(ndev->dev.parent);
		return err;
	}

	err = open_candev(ndev);
	if (err)
		goto out_pm_runtime_put;

	err = mcp251xfd_ring_alloc(priv);
	if (err)
		goto out_close_candev;

	err = mcp251xfd_transceiver_enable(priv);
	if (err)
		goto out_mcp251xfd_ring_free;

	err = mcp251xfd_chip_start(priv);
	if (err)
		goto out_transceiver_disable;

	can_rx_offload_enable(&priv->offload);

	err = request_threaded_irq(spi->irq, NULL, mcp251xfd_irq,
				   IRQF_ONESHOT, dev_name(&spi->dev),
				   priv);
	if (err)
		goto out_can_rx_offload_disable;

	err = mcp251xfd_chip_interrupts_enable(priv);
	if (err)
		goto out_free_irq;

	netif_start_queue(ndev);

	return 0;

 out_free_irq:
	free_irq(spi->irq, priv);
 out_can_rx_offload_disable:
	can_rx_offload_disable(&priv->offload);
 out_transceiver_disable:
	mcp251xfd_transceiver_disable(priv);
 out_mcp251xfd_ring_free:
	mcp251xfd_ring_free(priv);
 out_close_candev:
	close_candev(ndev);
 out_pm_runtime_put:
	mcp251xfd_chip_stop(priv, CAN_STATE_STOPPED);
	pm_runtime_put(ndev->dev.parent);

	return err;
}

static int mcp251xfd_stop(struct net_device *ndev)
{
	struct mcp251xfd_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	mcp251xfd_chip_interrupts_disable(priv);
	free_irq(ndev->irq, priv);
	can_rx_offload_disable(&priv->offload);
	mcp251xfd_chip_stop(priv, CAN_STATE_STOPPED);
	mcp251xfd_transceiver_disable(priv);
	mcp251xfd_ring_free(priv);
	close_candev(ndev);

	pm_runtime_put(ndev->dev.parent);

	return 0;
}

static const struct net_device_ops mcp251xfd_netdev_ops = {
	.ndo_open = mcp251xfd_open,
	.ndo_stop = mcp251xfd_stop,
	.ndo_start_xmit	= mcp251xfd_start_xmit,
	.ndo_change_mtu = can_change_mtu,
};

static void
mcp251xfd_register_quirks(struct mcp251xfd_priv *priv)
{
	const struct spi_device *spi = priv->spi;
	const struct spi_controller *ctlr = spi->controller;

	if (ctlr->flags & SPI_CONTROLLER_HALF_DUPLEX)
		priv->devtype_data.quirks |= MCP251XFD_QUIRK_HALF_DUPLEX;
}

static int mcp251xfd_register_chip_detect(struct mcp251xfd_priv *priv)
{
	const struct net_device *ndev = priv->ndev;
	const struct mcp251xfd_devtype_data *devtype_data;
	u32 osc;
	int err;

	/* The OSC_LPMEN is only supported on MCP2518FD, so use it to
	 * autodetect the model.
	 */
	err = regmap_update_bits(priv->map_reg, MCP251XFD_REG_OSC,
				 MCP251XFD_REG_OSC_LPMEN,
				 MCP251XFD_REG_OSC_LPMEN);
	if (err)
		return err;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_OSC, &osc);
	if (err)
		return err;

	if (osc & MCP251XFD_REG_OSC_LPMEN)
		devtype_data = &mcp251xfd_devtype_data_mcp2518fd;
	else
		devtype_data = &mcp251xfd_devtype_data_mcp2517fd;

	if (!mcp251xfd_is_251X(priv) &&
	    priv->devtype_data.model != devtype_data->model) {
		netdev_info(ndev,
			    "Detected %s, but firmware specifies a %s. Fixing up.\n",
			    __mcp251xfd_get_model_str(devtype_data->model),
			    mcp251xfd_get_model_str(priv));
	}
	priv->devtype_data = *devtype_data;

	/* We need to preserve the Half Duplex Quirk. */
	mcp251xfd_register_quirks(priv);

	/* Re-init regmap with quirks of detected model. */
	return mcp251xfd_regmap_init(priv);
}

static int mcp251xfd_register_check_rx_int(struct mcp251xfd_priv *priv)
{
	int err, rx_pending;

	if (!priv->rx_int)
		return 0;

	err = mcp251xfd_chip_rx_int_enable(priv);
	if (err)
		return err;

	/* Check if RX_INT is properly working. The RX_INT should not
	 * be active after a softreset.
	 */
	rx_pending = gpiod_get_value_cansleep(priv->rx_int);

	err = mcp251xfd_chip_rx_int_disable(priv);
	if (err)
		return err;

	if (!rx_pending)
		return 0;

	netdev_info(priv->ndev,
		    "RX_INT active after softreset, disabling RX_INT support.\n");
	devm_gpiod_put(&priv->spi->dev, priv->rx_int);
	priv->rx_int = NULL;

	return 0;
}

static int
mcp251xfd_register_get_dev_id(const struct mcp251xfd_priv *priv,
			      u32 *dev_id, u32 *effective_speed_hz)
{
	struct mcp251xfd_map_buf_nocrc *buf_rx;
	struct mcp251xfd_map_buf_nocrc *buf_tx;
	struct spi_transfer xfer[2] = { };
	int err;

	buf_rx = kzalloc(sizeof(*buf_rx), GFP_KERNEL);
	if (!buf_rx)
		return -ENOMEM;

	buf_tx = kzalloc(sizeof(*buf_tx), GFP_KERNEL);
	if (!buf_tx) {
		err = -ENOMEM;
		goto out_kfree_buf_rx;
	}

	xfer[0].tx_buf = buf_tx;
	xfer[0].len = sizeof(buf_tx->cmd);
	xfer[1].rx_buf = buf_rx->data;
	xfer[1].len = sizeof(dev_id);

	mcp251xfd_spi_cmd_read_nocrc(&buf_tx->cmd, MCP251XFD_REG_DEVID);
	err = spi_sync_transfer(priv->spi, xfer, ARRAY_SIZE(xfer));
	if (err)
		goto out_kfree_buf_tx;

	*dev_id = be32_to_cpup((__be32 *)buf_rx->data);
	*effective_speed_hz = xfer->effective_speed_hz;

 out_kfree_buf_tx:
	kfree(buf_tx);
 out_kfree_buf_rx:
	kfree(buf_rx);

	return 0;
}

#define MCP251XFD_QUIRK_ACTIVE(quirk) \
	(priv->devtype_data.quirks & MCP251XFD_QUIRK_##quirk ? '+' : '-')

static int
mcp251xfd_register_done(const struct mcp251xfd_priv *priv)
{
	u32 dev_id, effective_speed_hz;
	int err;

	err = mcp251xfd_register_get_dev_id(priv, &dev_id,
					    &effective_speed_hz);
	if (err)
		return err;

	netdev_info(priv->ndev,
		    "%s rev%lu.%lu (%cRX_INT %cMAB_NO_WARN %cCRC_REG %cCRC_RX %cCRC_TX %cECC %cHD c:%u.%02uMHz m:%u.%02uMHz r:%u.%02uMHz e:%u.%02uMHz) successfully initialized.\n",
		    mcp251xfd_get_model_str(priv),
		    FIELD_GET(MCP251XFD_REG_DEVID_ID_MASK, dev_id),
		    FIELD_GET(MCP251XFD_REG_DEVID_REV_MASK, dev_id),
		    priv->rx_int ? '+' : '-',
		    MCP251XFD_QUIRK_ACTIVE(MAB_NO_WARN),
		    MCP251XFD_QUIRK_ACTIVE(CRC_REG),
		    MCP251XFD_QUIRK_ACTIVE(CRC_RX),
		    MCP251XFD_QUIRK_ACTIVE(CRC_TX),
		    MCP251XFD_QUIRK_ACTIVE(ECC),
		    MCP251XFD_QUIRK_ACTIVE(HALF_DUPLEX),
		    priv->can.clock.freq / 1000000,
		    priv->can.clock.freq % 1000000 / 1000 / 10,
		    priv->spi_max_speed_hz_orig / 1000000,
		    priv->spi_max_speed_hz_orig % 1000000 / 1000 / 10,
		    priv->spi->max_speed_hz / 1000000,
		    priv->spi->max_speed_hz % 1000000 / 1000 / 10,
		    effective_speed_hz / 1000000,
		    effective_speed_hz % 1000000 / 1000 / 10);

	return 0;
}

static int mcp251xfd_register(struct mcp251xfd_priv *priv)
{
	struct net_device *ndev = priv->ndev;
	int err;

	err = mcp251xfd_clks_and_vdd_enable(priv);
	if (err)
		return err;

	pm_runtime_get_noresume(ndev->dev.parent);
	err = pm_runtime_set_active(ndev->dev.parent);
	if (err)
		goto out_runtime_put_noidle;
	pm_runtime_enable(ndev->dev.parent);

	mcp251xfd_register_quirks(priv);

	err = mcp251xfd_chip_softreset(priv);
	if (err == -ENODEV)
		goto out_runtime_disable;
	if (err)
		goto out_chip_set_mode_sleep;

	err = mcp251xfd_register_chip_detect(priv);
	if (err)
		goto out_chip_set_mode_sleep;

	err = mcp251xfd_register_check_rx_int(priv);
	if (err)
		goto out_chip_set_mode_sleep;

	err = register_candev(ndev);
	if (err)
		goto out_chip_set_mode_sleep;

	err = mcp251xfd_register_done(priv);
	if (err)
		goto out_unregister_candev;

	/* Put controller into sleep mode and let pm_runtime_put()
	 * disable the clocks and vdd. If CONFIG_PM is not enabled,
	 * the clocks and vdd will stay powered.
	 */
	err = mcp251xfd_chip_set_mode(priv, MCP251XFD_REG_CON_MODE_SLEEP);
	if (err)
		goto out_unregister_candev;

	pm_runtime_put(ndev->dev.parent);

	return 0;

 out_unregister_candev:
	unregister_candev(ndev);
 out_chip_set_mode_sleep:
	mcp251xfd_chip_set_mode(priv, MCP251XFD_REG_CON_MODE_SLEEP);
 out_runtime_disable:
	pm_runtime_disable(ndev->dev.parent);
 out_runtime_put_noidle:
	pm_runtime_put_noidle(ndev->dev.parent);
	mcp251xfd_clks_and_vdd_disable(priv);

	return err;
}

static inline void mcp251xfd_unregister(struct mcp251xfd_priv *priv)
{
	struct net_device *ndev	= priv->ndev;

	unregister_candev(ndev);

	pm_runtime_get_sync(ndev->dev.parent);
	pm_runtime_put_noidle(ndev->dev.parent);
	mcp251xfd_clks_and_vdd_disable(priv);
	pm_runtime_disable(ndev->dev.parent);
}

static const struct of_device_id mcp251xfd_of_match[] = {
	{
		.compatible = "microchip,mcp2517fd",
		.data = &mcp251xfd_devtype_data_mcp2517fd,
	}, {
		.compatible = "microchip,mcp2518fd",
		.data = &mcp251xfd_devtype_data_mcp2518fd,
	}, {
		.compatible = "microchip,mcp251xfd",
		.data = &mcp251xfd_devtype_data_mcp251xfd,
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, mcp251xfd_of_match);

static const struct spi_device_id mcp251xfd_id_table[] = {
	{
		.name = "mcp2517fd",
		.driver_data = (kernel_ulong_t)&mcp251xfd_devtype_data_mcp2517fd,
	}, {
		.name = "mcp2518fd",
		.driver_data = (kernel_ulong_t)&mcp251xfd_devtype_data_mcp2518fd,
	}, {
		.name = "mcp251xfd",
		.driver_data = (kernel_ulong_t)&mcp251xfd_devtype_data_mcp251xfd,
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(spi, mcp251xfd_id_table);

static int mcp251xfd_probe(struct spi_device *spi)
{
	const void *match;
	struct net_device *ndev;
	struct mcp251xfd_priv *priv;
	struct gpio_desc *rx_int;
	struct regulator *reg_vdd, *reg_xceiver;
	struct clk *clk;
	u32 freq;
	int err;

	if (!spi->irq)
		return dev_err_probe(&spi->dev, -ENXIO,
				     "No IRQ specified (maybe node \"interrupts-extended\" in DT missing)!\n");

	rx_int = devm_gpiod_get_optional(&spi->dev, "microchip,rx-int",
					 GPIOD_IN);
	if (PTR_ERR(rx_int) == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	else if (IS_ERR(rx_int))
		return PTR_ERR(rx_int);

	reg_vdd = devm_regulator_get_optional(&spi->dev, "vdd");
	if (PTR_ERR(reg_vdd) == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	else if (PTR_ERR(reg_vdd) == -ENODEV)
		reg_vdd = NULL;
	else if (IS_ERR(reg_vdd))
		return PTR_ERR(reg_vdd);

	reg_xceiver = devm_regulator_get_optional(&spi->dev, "xceiver");
	if (PTR_ERR(reg_xceiver) == -EPROBE_DEFER)
		return -EPROBE_DEFER;
	else if (PTR_ERR(reg_xceiver) == -ENODEV)
		reg_xceiver = NULL;
	else if (IS_ERR(reg_xceiver))
		return PTR_ERR(reg_xceiver);

	clk = devm_clk_get(&spi->dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(&spi->dev, "No Oscillator (clock) defined.\n");
		return PTR_ERR(clk);
	}
	freq = clk_get_rate(clk);

	/* Sanity check */
	if (freq < MCP251XFD_SYSCLOCK_HZ_MIN ||
	    freq > MCP251XFD_SYSCLOCK_HZ_MAX) {
		dev_err(&spi->dev,
			"Oscillator frequency (%u Hz) is too low or high.\n",
			freq);
		return -ERANGE;
	}

	if (freq <= MCP251XFD_SYSCLOCK_HZ_MAX / MCP251XFD_OSC_PLL_MULTIPLIER) {
		dev_err(&spi->dev,
			"Oscillator frequency (%u Hz) is too low and PLL is not supported.\n",
			freq);
		return -ERANGE;
	}

	ndev = alloc_candev(sizeof(struct mcp251xfd_priv),
			    MCP251XFD_TX_OBJ_NUM_MAX);
	if (!ndev)
		return -ENOMEM;

	SET_NETDEV_DEV(ndev, &spi->dev);

	ndev->netdev_ops = &mcp251xfd_netdev_ops;
	ndev->irq = spi->irq;
	ndev->flags |= IFF_ECHO;

	priv = netdev_priv(ndev);
	spi_set_drvdata(spi, priv);
	priv->can.clock.freq = freq;
	priv->can.do_set_mode = mcp251xfd_set_mode;
	priv->can.do_get_berr_counter = mcp251xfd_get_berr_counter;
	priv->can.bittiming_const = &mcp251xfd_bittiming_const;
	priv->can.data_bittiming_const = &mcp251xfd_data_bittiming_const;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LISTENONLY |
		CAN_CTRLMODE_BERR_REPORTING | CAN_CTRLMODE_FD |
		CAN_CTRLMODE_FD_NON_ISO;
	priv->ndev = ndev;
	priv->spi = spi;
	priv->rx_int = rx_int;
	priv->clk = clk;
	priv->reg_vdd = reg_vdd;
	priv->reg_xceiver = reg_xceiver;

	match = device_get_match_data(&spi->dev);
	if (match)
		priv->devtype_data = *(struct mcp251xfd_devtype_data *)match;
	else
		priv->devtype_data = *(struct mcp251xfd_devtype_data *)
			spi_get_device_id(spi)->driver_data;

	/* Errata Reference:
	 * mcp2517fd: DS80000792C 5., mcp2518fd: DS80000789C 4.
	 *
	 * The SPI can write corrupted data to the RAM at fast SPI
	 * speeds:
	 *
	 * Simultaneous activity on the CAN bus while writing data to
	 * RAM via the SPI interface, with high SCK frequency, can
	 * lead to corrupted data being written to RAM.
	 *
	 * Fix/Work Around:
	 * Ensure that FSCK is less than or equal to 0.85 *
	 * (FSYSCLK/2).
	 *
	 * Known good and bad combinations are:
	 *
	 * MCP	ext-clk	SoC			SPI			SPI-clk		max-clk	parent-clk	Status	config
	 *
	 * 2518	20 MHz	allwinner,sun8i-h3	allwinner,sun8i-h3-spi	 8333333 Hz	 83.33%	600000000 Hz	good	assigned-clocks = <&ccu CLK_SPIx>
	 * 2518	20 MHz	allwinner,sun8i-h3	allwinner,sun8i-h3-spi	 9375000 Hz	 93.75%	600000000 Hz	bad	assigned-clocks = <&ccu CLK_SPIx>
	 * 2518	40 MHz	allwinner,sun8i-h3	allwinner,sun8i-h3-spi	16666667 Hz	 83.33%	600000000 Hz	good	assigned-clocks = <&ccu CLK_SPIx>
	 * 2518	40 MHz	allwinner,sun8i-h3	allwinner,sun8i-h3-spi	18750000 Hz	 93.75%	600000000 Hz	bad	assigned-clocks = <&ccu CLK_SPIx>
	 * 2517	20 MHz	fsl,imx8mm		fsl,imx51-ecspi		 8333333 Hz	 83.33%	 16666667 Hz	good	assigned-clocks = <&clk IMX8MM_CLK_ECSPIx_ROOT>
	 * 2517	20 MHz	fsl,imx8mm		fsl,imx51-ecspi		 9523809 Hz	 95.34%	 28571429 Hz	bad	assigned-clocks = <&clk IMX8MM_CLK_ECSPIx_ROOT>
	 * 2517 40 MHz	atmel,sama5d27		atmel,at91rm9200-spi	16400000 Hz	 82.00%	 82000000 Hz	good	default
	 * 2518 40 MHz	atmel,sama5d27		atmel,at91rm9200-spi	16400000 Hz	 82.00%	 82000000 Hz	good	default
	 *
	 */
	priv->spi_max_speed_hz_orig = spi->max_speed_hz;
	spi->max_speed_hz = min(spi->max_speed_hz, freq / 2 / 1000 * 850);
	spi->bits_per_word = 8;
	spi->rt = true;
	err = spi_setup(spi);
	if (err)
		goto out_free_candev;

	err = mcp251xfd_regmap_init(priv);
	if (err)
		goto out_free_candev;

	err = can_rx_offload_add_manual(ndev, &priv->offload,
					MCP251XFD_NAPI_WEIGHT);
	if (err)
		goto out_free_candev;

	err = mcp251xfd_register(priv);
	if (err)
		goto out_can_rx_offload_del;

	return 0;

 out_can_rx_offload_del:
	can_rx_offload_del(&priv->offload);
 out_free_candev:
	spi->max_speed_hz = priv->spi_max_speed_hz_orig;

	free_candev(ndev);

	return err;
}

static int mcp251xfd_remove(struct spi_device *spi)
{
	struct mcp251xfd_priv *priv = spi_get_drvdata(spi);
	struct net_device *ndev = priv->ndev;

	can_rx_offload_del(&priv->offload);
	mcp251xfd_unregister(priv);
	spi->max_speed_hz = priv->spi_max_speed_hz_orig;
	free_candev(ndev);

	return 0;
}

static int __maybe_unused mcp251xfd_runtime_suspend(struct device *device)
{
	const struct mcp251xfd_priv *priv = dev_get_drvdata(device);

	return mcp251xfd_clks_and_vdd_disable(priv);
}

static int __maybe_unused mcp251xfd_runtime_resume(struct device *device)
{
	const struct mcp251xfd_priv *priv = dev_get_drvdata(device);

	return mcp251xfd_clks_and_vdd_enable(priv);
}

static const struct dev_pm_ops mcp251xfd_pm_ops = {
	SET_RUNTIME_PM_OPS(mcp251xfd_runtime_suspend,
			   mcp251xfd_runtime_resume, NULL)
};

static struct spi_driver mcp251xfd_driver = {
	.driver = {
		.name = DEVICE_NAME,
		.pm = &mcp251xfd_pm_ops,
		.of_match_table = mcp251xfd_of_match,
	},
	.probe = mcp251xfd_probe,
	.remove = mcp251xfd_remove,
	.id_table = mcp251xfd_id_table,
};
module_spi_driver(mcp251xfd_driver);

MODULE_AUTHOR("Marc Kleine-Budde <mkl@pengutronix.de>");
MODULE_DESCRIPTION("Microchip MCP251xFD Family CAN controller driver");
MODULE_LICENSE("GPL v2");
