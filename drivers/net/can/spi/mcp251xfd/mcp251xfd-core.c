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

#include <linux/unaligned.h>
#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>

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

static const struct mcp251xfd_devtype_data mcp251xfd_devtype_data_mcp251863 = {
	.quirks = MCP251XFD_QUIRK_CRC_REG | MCP251XFD_QUIRK_CRC_RX |
		MCP251XFD_QUIRK_CRC_TX | MCP251XFD_QUIRK_ECC,
	.model = MCP251XFD_MODEL_MCP251863,
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

/* The datasheet of the mcp2518fd (DS20006027B) specifies a range of
 * [-64,63] for TDCO, indicating a relative TDCO.
 *
 * Manual tests have shown, that using a relative TDCO configuration
 * results in bus off, while an absolute configuration works.
 *
 * For TDCO use the max value (63) from the data sheet, but 0 as the
 * minimum.
 */
static const struct can_tdc_const mcp251xfd_tdc_const = {
	.tdcv_min = 0,
	.tdcv_max = 63,
	.tdco_min = 0,
	.tdco_max = 63,
	.tdcf_min = 0,
	.tdcf_max = 0,
};

static const char *__mcp251xfd_get_model_str(enum mcp251xfd_model model)
{
	switch (model) {
	case MCP251XFD_MODEL_MCP2517FD:
		return "MCP2517FD";
	case MCP251XFD_MODEL_MCP2518FD:
		return "MCP2518FD";
	case MCP251XFD_MODEL_MCP251863:
		return "MCP251863";
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

static const char *
mcp251xfd_get_osc_str(const u32 osc, const u32 osc_reference)
{
	switch (~osc & osc_reference &
		(MCP251XFD_REG_OSC_OSCRDY | MCP251XFD_REG_OSC_PLLRDY)) {
	case MCP251XFD_REG_OSC_PLLRDY:
		return "PLL";
	case MCP251XFD_REG_OSC_OSCRDY:
		return "Oscillator";
	case MCP251XFD_REG_OSC_PLLRDY | MCP251XFD_REG_OSC_OSCRDY:
		return "Oscillator/PLL";
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

static inline bool mcp251xfd_reg_invalid(u32 reg)
{
	return reg == 0x0 || reg == 0xffffffff;
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
	const struct can_bittiming *bt = &priv->can.bittiming;
	unsigned long timeout_us = MCP251XFD_POLL_TIMEOUT_US;
	u32 con = 0, con_reqop, osc = 0;
	u8 mode;
	int err;

	con_reqop = FIELD_PREP(MCP251XFD_REG_CON_REQOP_MASK, mode_req);
	err = regmap_update_bits(priv->map_reg, MCP251XFD_REG_CON,
				 MCP251XFD_REG_CON_REQOP_MASK, con_reqop);
	if (err == -EBADMSG) {
		netdev_err(priv->ndev,
			   "Failed to set Requested Operation Mode.\n");

		return -ENODEV;
	} else if (err) {
		return err;
	}

	if (mode_req == MCP251XFD_REG_CON_MODE_SLEEP || nowait)
		return 0;

	if (bt->bitrate)
		timeout_us = max_t(unsigned long, timeout_us,
				   MCP251XFD_FRAME_LEN_MAX_BITS * USEC_PER_SEC /
				   bt->bitrate);

	err = regmap_read_poll_timeout(priv->map_reg, MCP251XFD_REG_CON, con,
				       !mcp251xfd_reg_invalid(con) &&
				       FIELD_GET(MCP251XFD_REG_CON_OPMOD_MASK,
						 con) == mode_req,
				       MCP251XFD_POLL_SLEEP_US, timeout_us);
	if (err != -ETIMEDOUT && err != -EBADMSG)
		return err;

	/* Ignore return value.
	 * Print below error messages, even if this fails.
	 */
	regmap_read(priv->map_reg, MCP251XFD_REG_OSC, &osc);

	if (mcp251xfd_reg_invalid(con)) {
		netdev_err(priv->ndev,
			   "Failed to read CAN Control Register (con=0x%08x, osc=0x%08x).\n",
			   con, osc);

		return -ENODEV;
	}

	mode = FIELD_GET(MCP251XFD_REG_CON_OPMOD_MASK, con);
	netdev_err(priv->ndev,
		   "Controller failed to enter mode %s Mode (%u) and stays in %s Mode (%u) (con=0x%08x, osc=0x%08x).\n",
		   mcp251xfd_get_mode_str(mode_req), mode_req,
		   mcp251xfd_get_mode_str(mode), mode,
		   con, osc);

	return -ETIMEDOUT;
}

static inline int
mcp251xfd_chip_set_mode(const struct mcp251xfd_priv *priv,
			const u8 mode_req)
{
	return __mcp251xfd_chip_set_mode(priv, mode_req, false);
}

static inline int __maybe_unused
mcp251xfd_chip_set_mode_nowait(const struct mcp251xfd_priv *priv,
			       const u8 mode_req)
{
	return __mcp251xfd_chip_set_mode(priv, mode_req, true);
}

static int
mcp251xfd_chip_wait_for_osc_ready(const struct mcp251xfd_priv *priv,
				  u32 osc_reference, u32 osc_mask)
{
	u32 osc;
	int err;

	err = regmap_read_poll_timeout(priv->map_reg, MCP251XFD_REG_OSC, osc,
				       !mcp251xfd_reg_invalid(osc) &&
				       (osc & osc_mask) == osc_reference,
				       MCP251XFD_OSC_STAB_SLEEP_US,
				       MCP251XFD_OSC_STAB_TIMEOUT_US);
	if (err != -ETIMEDOUT)
		return err;

	if (mcp251xfd_reg_invalid(osc)) {
		netdev_err(priv->ndev,
			   "Failed to read Oscillator Configuration Register (osc=0x%08x).\n",
			   osc);
		return -ENODEV;
	}

	netdev_err(priv->ndev,
		   "Timeout waiting for %s ready (osc=0x%08x, osc_reference=0x%08x, osc_mask=0x%08x).\n",
		   mcp251xfd_get_osc_str(osc, osc_reference),
		   osc, osc_reference, osc_mask);

	return -ETIMEDOUT;
}

static int mcp251xfd_chip_wake(const struct mcp251xfd_priv *priv)
{
	u32 osc, osc_reference, osc_mask;
	int err;

	/* For normal sleep on MCP2517FD and MCP2518FD, clearing
	 * "Oscillator Disable" will wake the chip. For low power mode
	 * on MCP2518FD, asserting the chip select will wake the
	 * chip. Writing to the Oscillator register will wake it in
	 * both cases.
	 */
	osc = FIELD_PREP(MCP251XFD_REG_OSC_CLKODIV_MASK,
			 MCP251XFD_REG_OSC_CLKODIV_10);

	/* We cannot check for the PLL ready bit (either set or
	 * unset), as the PLL might be enabled. This can happen if the
	 * system reboots, while the mcp251xfd stays powered.
	 */
	osc_reference = MCP251XFD_REG_OSC_OSCRDY;
	osc_mask = MCP251XFD_REG_OSC_OSCRDY;

	/* If the controller is in Sleep Mode the following write only
	 * removes the "Oscillator Disable" bit and powers it up. All
	 * other bits are unaffected.
	 */
	err = regmap_write(priv->map_reg, MCP251XFD_REG_OSC, osc);
	if (err)
		return err;

	/* Sometimes the PLL is stuck enabled, the controller never
	 * sets the OSC Ready bit, and we get an -ETIMEDOUT. Our
	 * caller takes care of retry.
	 */
	return mcp251xfd_chip_wait_for_osc_ready(priv, osc_reference, osc_mask);
}

static inline int mcp251xfd_chip_sleep(const struct mcp251xfd_priv *priv)
{
	if (priv->pll_enable) {
		u32 osc;
		int err;

		/* Turn off PLL */
		osc = FIELD_PREP(MCP251XFD_REG_OSC_CLKODIV_MASK,
				 MCP251XFD_REG_OSC_CLKODIV_10);
		err = regmap_write(priv->map_reg, MCP251XFD_REG_OSC, osc);
		if (err)
			netdev_err(priv->ndev,
				   "Failed to disable PLL.\n");

		priv->spi->max_speed_hz = priv->spi_max_speed_hz_slow;
	}

	return mcp251xfd_chip_set_mode(priv, MCP251XFD_REG_CON_MODE_SLEEP);
}

static int mcp251xfd_chip_softreset_do(const struct mcp251xfd_priv *priv)
{
	const __be16 cmd = mcp251xfd_cmd_reset();
	int err;

	/* The Set Mode and SPI Reset command only works if the
	 * controller is not in Sleep Mode.
	 */
	err = mcp251xfd_chip_wake(priv);
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
	u32 osc_reference, osc_mask;
	u8 mode;
	int err;

	/* Check for reset defaults of OSC reg.
	 * This will take care of stabilization period.
	 */
	osc_reference = MCP251XFD_REG_OSC_OSCRDY |
		FIELD_PREP(MCP251XFD_REG_OSC_CLKODIV_MASK,
			   MCP251XFD_REG_OSC_CLKODIV_10);
	osc_mask = osc_reference | MCP251XFD_REG_OSC_PLLRDY;
	err = mcp251xfd_chip_wait_for_osc_ready(priv, osc_reference, osc_mask);
	if (err)
		return err;

	err = mcp251xfd_chip_get_mode(priv, &mode);
	if (err)
		return err;

	if (mode != MCP251XFD_REG_CON_MODE_CONFIG) {
		netdev_info(priv->ndev,
			    "Controller not in Config Mode after reset, but in %s Mode (%u).\n",
			    mcp251xfd_get_mode_str(mode), mode);
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
				    "Retrying to reset controller.\n");

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

	return err;
}

static int mcp251xfd_chip_clock_init(const struct mcp251xfd_priv *priv)
{
	u32 osc, osc_reference, osc_mask;
	int err;

	/* Activate Low Power Mode on Oscillator Disable. This only
	 * works on the MCP2518FD. The MCP2517FD will go into normal
	 * Sleep Mode instead.
	 */
	osc = MCP251XFD_REG_OSC_LPMEN |
		FIELD_PREP(MCP251XFD_REG_OSC_CLKODIV_MASK,
			   MCP251XFD_REG_OSC_CLKODIV_10);
	osc_reference = MCP251XFD_REG_OSC_OSCRDY;
	osc_mask = MCP251XFD_REG_OSC_OSCRDY | MCP251XFD_REG_OSC_PLLRDY;

	if (priv->pll_enable) {
		osc |= MCP251XFD_REG_OSC_PLLEN;
		osc_reference |= MCP251XFD_REG_OSC_PLLRDY;
	}

	err = regmap_write(priv->map_reg, MCP251XFD_REG_OSC, osc);
	if (err)
		return err;

	err = mcp251xfd_chip_wait_for_osc_ready(priv, osc_reference, osc_mask);
	if (err)
		return err;

	priv->spi->max_speed_hz = priv->spi_max_speed_hz_fast;

	return 0;
}

static int mcp251xfd_chip_timestamp_init(const struct mcp251xfd_priv *priv)
{
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
	u32 tdcmod, val = 0;
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
	if (priv->can.ctrlmode & CAN_CTRLMODE_TDC_AUTO)
		tdcmod = MCP251XFD_REG_TDC_TDCMOD_AUTO;
	else if (priv->can.ctrlmode & CAN_CTRLMODE_TDC_MANUAL)
		tdcmod = MCP251XFD_REG_TDC_TDCMOD_MANUAL;
	else
		tdcmod = MCP251XFD_REG_TDC_TDCMOD_DISABLED;

	val = FIELD_PREP(MCP251XFD_REG_TDC_TDCMOD_MASK, tdcmod) |
		FIELD_PREP(MCP251XFD_REG_TDC_TDCV_MASK, priv->can.tdc.tdcv) |
		FIELD_PREP(MCP251XFD_REG_TDC_TDCO_MASK, priv->can.tdc.tdco);

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

static u8 mcp251xfd_get_normal_mode(const struct mcp251xfd_priv *priv)
{
	u8 mode;

	if (priv->can.ctrlmode & CAN_CTRLMODE_LOOPBACK)
		mode = MCP251XFD_REG_CON_MODE_INT_LOOPBACK;
	else if (priv->can.ctrlmode & CAN_CTRLMODE_LISTENONLY)
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

static void mcp251xfd_chip_stop(struct mcp251xfd_priv *priv,
				const enum can_state state)
{
	priv->can.state = state;

	mcp251xfd_chip_interrupts_disable(priv);
	mcp251xfd_chip_rx_int_disable(priv);
	mcp251xfd_timestamp_stop(priv);
	mcp251xfd_chip_sleep(priv);
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

	err = mcp251xfd_chip_timestamp_init(priv);
	if (err)
		goto out_chip_stop;

	mcp251xfd_timestamp_start(priv);

	err = mcp251xfd_set_bittiming(priv);
	if (err)
		goto out_chip_stop;

	err = mcp251xfd_chip_rx_int_enable(priv);
	if (err)
		goto out_chip_stop;

	err = mcp251xfd_chip_ecc_init(priv);
	if (err)
		goto out_chip_stop;

	err = mcp251xfd_ring_init(priv);
	if (err)
		goto out_chip_stop;

	err = mcp251xfd_chip_fifo_init(priv);
	if (err)
		goto out_chip_stop;

	priv->can.state = CAN_STATE_ERROR_ACTIVE;

	err = mcp251xfd_chip_set_normal_mode(priv);
	if (err)
		goto out_chip_stop;

	return 0;

out_chip_stop:
	mcp251xfd_dump(priv);
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
		bec->txerr = CAN_BUS_OFF_THRESHOLD;
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

static struct sk_buff *
mcp251xfd_alloc_can_err_skb(struct mcp251xfd_priv *priv,
			    struct can_frame **cf, u32 *ts_raw)
{
	struct sk_buff *skb;
	int err;

	err = mcp251xfd_get_timestamp_raw(priv, ts_raw);
	if (err)
		return NULL;

	skb = alloc_can_err_skb(priv->ndev, cf);
	if (skb)
		mcp251xfd_skb_set_timestamp_raw(priv, skb, *ts_raw);

	return skb;
}

static int mcp251xfd_handle_rxovif(struct mcp251xfd_priv *priv)
{
	struct net_device_stats *stats = &priv->ndev->stats;
	struct mcp251xfd_rx_ring *ring;
	struct sk_buff *skb;
	struct can_frame *cf;
	u32 ts_raw, rxovif;
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
			if (net_ratelimit())
				netdev_dbg(priv->ndev,
					   "RX-%d: MAB overflow detected.\n",
					   ring->nr);
		} else {
			if (net_ratelimit())
				netdev_dbg(priv->ndev,
					   "RX-%d: FIFO overflow.\n",
					   ring->nr);
		}

		err = regmap_update_bits(priv->map_reg,
					 MCP251XFD_REG_FIFOSTA(ring->fifo_nr),
					 MCP251XFD_REG_FIFOSTA_RXOVIF,
					 0x0);
		if (err)
			return err;
	}

	skb = mcp251xfd_alloc_can_err_skb(priv, &cf, &ts_raw);
	if (!skb)
		return 0;

	cf->can_id |= CAN_ERR_CRTL;
	cf->data[1] = CAN_ERR_CRTL_RX_OVERFLOW;

	err = can_rx_offload_queue_timestamp(&priv->offload, skb, ts_raw);
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
	u32 bdiag1, ts_raw;
	struct sk_buff *skb;
	struct can_frame *cf = NULL;
	int err;

	err = mcp251xfd_get_timestamp_raw(priv, &ts_raw);
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

	mcp251xfd_skb_set_timestamp_raw(priv, skb, ts_raw);
	err = can_rx_offload_queue_timestamp(&priv->offload, skb, ts_raw);
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
	u32 trec, ts_raw;
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
	skb = mcp251xfd_alloc_can_err_skb(priv, &cf, &ts_raw);
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
		cf->can_id |= CAN_ERR_CNT;
		cf->data[6] = bec.txerr;
		cf->data[7] = bec.rxerr;
	}

	err = can_rx_offload_queue_timestamp(&priv->offload, skb, ts_raw);
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

	/* According to MCP2517FD errata DS80000792C 1., during a TX
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

	/* After the application requests Normal mode, the controller
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
	 * According to MCP2517FD Errata DS80000792C 1. a TX MAB
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
	 * According to MCP2517FD Errata DS80000792C 1. a RX MAB
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
	    !(offset == 0 || (offset == 1 && (mcp251xfd_is_2518FD(priv) ||
					      mcp251xfd_is_251863(priv))))) {
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
	 * mcp2517fd: DS80000789C 3., mcp2518fd: DS80000792E 2.,
	 * mcp251863: DS80000984A 2.
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

static int mcp251xfd_read_regs_status(struct mcp251xfd_priv *priv)
{
	const int val_bytes = regmap_get_val_bytes(priv->map_reg);
	size_t len;

	if (priv->rx_ring_num == 1)
		len = sizeof(priv->regs_status.intf);
	else
		len = sizeof(priv->regs_status);

	return regmap_bulk_read(priv->map_reg, MCP251XFD_REG_INT,
				&priv->regs_status, len / val_bytes);
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

			/* Assume 1st RX-FIFO pending, if other FIFOs
			 * are pending the main IRQ handler will take
			 * care.
			 */
			priv->regs_status.rxif = BIT(priv->rx[0]->fifo_nr);
			err = mcp251xfd_handle(priv, rxif);
			if (err)
				goto out_fail;

			handled = IRQ_HANDLED;

			/* We don't know which RX-FIFO is pending, but only
			 * handle the 1st RX-FIFO. Leave loop here if we have
			 * more than 1 RX-FIFO to avoid starvation.
			 */
		} while (priv->rx_ring_num == 1);

	do {
		u32 intf_pending, intf_pending_clearable;
		bool set_normal_mode = false;

		err = mcp251xfd_read_regs_status(priv);
		if (err)
			goto out_fail;

		intf_pending = FIELD_GET(MCP251XFD_REG_INT_IF_MASK,
					 priv->regs_status.intf) &
			FIELD_GET(MCP251XFD_REG_INT_IE_MASK,
				  priv->regs_status.intf);

		if (!(intf_pending)) {
			can_rx_offload_threaded_irq_finish(&priv->offload);
			return handled;
		}

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
			if (priv->can.state == CAN_STATE_BUS_OFF) {
				can_rx_offload_threaded_irq_finish(&priv->offload);
				return IRQ_HANDLED;
			}
		}

		handled = IRQ_HANDLED;
	} while (1);

out_fail:
	can_rx_offload_threaded_irq_finish(&priv->offload);

	netdev_err(priv->ndev, "IRQ handler returned %d (intf=0x%08x).\n",
		   err, priv->regs_status.intf);
	mcp251xfd_dump(priv);
	mcp251xfd_chip_interrupts_disable(priv);
	mcp251xfd_timestamp_stop(priv);

	return handled;
}

static int mcp251xfd_open(struct net_device *ndev)
{
	struct mcp251xfd_priv *priv = netdev_priv(ndev);
	const struct spi_device *spi = priv->spi;
	int err;

	err = open_candev(ndev);
	if (err)
		return err;

	err = pm_runtime_resume_and_get(ndev->dev.parent);
	if (err)
		goto out_close_candev;

	err = mcp251xfd_ring_alloc(priv);
	if (err)
		goto out_pm_runtime_put;

	err = mcp251xfd_transceiver_enable(priv);
	if (err)
		goto out_mcp251xfd_ring_free;

	mcp251xfd_timestamp_init(priv);

	err = mcp251xfd_chip_start(priv);
	if (err)
		goto out_transceiver_disable;

	clear_bit(MCP251XFD_FLAGS_DOWN, priv->flags);
	can_rx_offload_enable(&priv->offload);

	priv->wq = alloc_ordered_workqueue("%s-mcp251xfd_wq",
					   WQ_FREEZABLE | WQ_MEM_RECLAIM,
					   dev_name(&spi->dev));
	if (!priv->wq) {
		err = -ENOMEM;
		goto out_can_rx_offload_disable;
	}
	INIT_WORK(&priv->tx_work, mcp251xfd_tx_obj_write_sync);

	err = request_threaded_irq(spi->irq, NULL, mcp251xfd_irq,
				   IRQF_SHARED | IRQF_ONESHOT,
				   dev_name(&spi->dev), priv);
	if (err)
		goto out_destroy_workqueue;

	err = mcp251xfd_chip_interrupts_enable(priv);
	if (err)
		goto out_free_irq;

	netif_start_queue(ndev);

	return 0;

out_free_irq:
	free_irq(spi->irq, priv);
out_destroy_workqueue:
	destroy_workqueue(priv->wq);
out_can_rx_offload_disable:
	can_rx_offload_disable(&priv->offload);
	set_bit(MCP251XFD_FLAGS_DOWN, priv->flags);
out_transceiver_disable:
	mcp251xfd_transceiver_disable(priv);
out_mcp251xfd_ring_free:
	mcp251xfd_ring_free(priv);
out_pm_runtime_put:
	mcp251xfd_chip_stop(priv, CAN_STATE_STOPPED);
	pm_runtime_put(ndev->dev.parent);
out_close_candev:
	close_candev(ndev);

	return err;
}

static int mcp251xfd_stop(struct net_device *ndev)
{
	struct mcp251xfd_priv *priv = netdev_priv(ndev);

	netif_stop_queue(ndev);
	set_bit(MCP251XFD_FLAGS_DOWN, priv->flags);
	hrtimer_cancel(&priv->rx_irq_timer);
	hrtimer_cancel(&priv->tx_irq_timer);
	mcp251xfd_chip_interrupts_disable(priv);
	free_irq(ndev->irq, priv);
	destroy_workqueue(priv->wq);
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
	.ndo_eth_ioctl = can_eth_ioctl_hwts,
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

	/* The OSC_LPMEN is only supported on MCP2518FD and MCP251863,
	 * so use it to autodetect the model.
	 */
	err = regmap_update_bits(priv->map_reg, MCP251XFD_REG_OSC,
				 MCP251XFD_REG_OSC_LPMEN,
				 MCP251XFD_REG_OSC_LPMEN);
	if (err)
		return err;

	err = regmap_read(priv->map_reg, MCP251XFD_REG_OSC, &osc);
	if (err)
		return err;

	if (osc & MCP251XFD_REG_OSC_LPMEN) {
		/* We cannot distinguish between MCP2518FD and
		 * MCP251863. If firmware specifies MCP251863, keep
		 * it, otherwise set to MCP2518FD.
		 */
		if (mcp251xfd_is_251863(priv))
			devtype_data = &mcp251xfd_devtype_data_mcp251863;
		else
			devtype_data = &mcp251xfd_devtype_data_mcp2518fd;
	} else {
		devtype_data = &mcp251xfd_devtype_data_mcp2517fd;
	}

	if (!mcp251xfd_is_251XFD(priv) &&
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
mcp251xfd_register_get_dev_id(const struct mcp251xfd_priv *priv, u32 *dev_id,
			      u32 *effective_speed_hz_slow,
			      u32 *effective_speed_hz_fast)
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
	xfer[0].speed_hz = priv->spi_max_speed_hz_slow;
	xfer[1].rx_buf = buf_rx->data;
	xfer[1].len = sizeof(*dev_id);
	xfer[1].speed_hz = priv->spi_max_speed_hz_fast;

	mcp251xfd_spi_cmd_read_nocrc(&buf_tx->cmd, MCP251XFD_REG_DEVID);

	err = spi_sync_transfer(priv->spi, xfer, ARRAY_SIZE(xfer));
	if (err)
		goto out_kfree_buf_tx;

	*dev_id = get_unaligned_le32(buf_rx->data);
	*effective_speed_hz_slow = xfer[0].effective_speed_hz;
	*effective_speed_hz_fast = xfer[1].effective_speed_hz;

out_kfree_buf_tx:
	kfree(buf_tx);
out_kfree_buf_rx:
	kfree(buf_rx);

	return err;
}

#define MCP251XFD_QUIRK_ACTIVE(quirk) \
	(priv->devtype_data.quirks & MCP251XFD_QUIRK_##quirk ? '+' : '-')

static int
mcp251xfd_register_done(const struct mcp251xfd_priv *priv)
{
	u32 dev_id, effective_speed_hz_slow, effective_speed_hz_fast;
	unsigned long clk_rate;
	int err;

	err = mcp251xfd_register_get_dev_id(priv, &dev_id,
					    &effective_speed_hz_slow,
					    &effective_speed_hz_fast);
	if (err)
		return err;

	clk_rate = clk_get_rate(priv->clk);

	netdev_info(priv->ndev,
		    "%s rev%lu.%lu (%cRX_INT %cPLL %cMAB_NO_WARN %cCRC_REG %cCRC_RX %cCRC_TX %cECC %cHD o:%lu.%02luMHz c:%u.%02uMHz m:%u.%02uMHz rs:%u.%02uMHz es:%u.%02uMHz rf:%u.%02uMHz ef:%u.%02uMHz) successfully initialized.\n",
		    mcp251xfd_get_model_str(priv),
		    FIELD_GET(MCP251XFD_REG_DEVID_ID_MASK, dev_id),
		    FIELD_GET(MCP251XFD_REG_DEVID_REV_MASK, dev_id),
		    priv->rx_int ? '+' : '-',
		    priv->pll_enable ? '+' : '-',
		    MCP251XFD_QUIRK_ACTIVE(MAB_NO_WARN),
		    MCP251XFD_QUIRK_ACTIVE(CRC_REG),
		    MCP251XFD_QUIRK_ACTIVE(CRC_RX),
		    MCP251XFD_QUIRK_ACTIVE(CRC_TX),
		    MCP251XFD_QUIRK_ACTIVE(ECC),
		    MCP251XFD_QUIRK_ACTIVE(HALF_DUPLEX),
		    clk_rate / 1000000,
		    clk_rate % 1000000 / 1000 / 10,
		    priv->can.clock.freq / 1000000,
		    priv->can.clock.freq % 1000000 / 1000 / 10,
		    priv->spi_max_speed_hz_orig / 1000000,
		    priv->spi_max_speed_hz_orig % 1000000 / 1000 / 10,
		    priv->spi_max_speed_hz_slow / 1000000,
		    priv->spi_max_speed_hz_slow % 1000000 / 1000 / 10,
		    effective_speed_hz_slow / 1000000,
		    effective_speed_hz_slow % 1000000 / 1000 / 10,
		    priv->spi_max_speed_hz_fast / 1000000,
		    priv->spi_max_speed_hz_fast % 1000000 / 1000 / 10,
		    effective_speed_hz_fast / 1000000,
		    effective_speed_hz_fast % 1000000 / 1000 / 10);

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
		goto out_chip_sleep;

	err = mcp251xfd_chip_clock_init(priv);
	if (err == -ENODEV)
		goto out_runtime_disable;
	if (err)
		goto out_chip_sleep;

	err = mcp251xfd_register_chip_detect(priv);
	if (err)
		goto out_chip_sleep;

	err = mcp251xfd_register_check_rx_int(priv);
	if (err)
		goto out_chip_sleep;

	mcp251xfd_ethtool_init(priv);

	err = register_candev(ndev);
	if (err)
		goto out_chip_sleep;

	err = mcp251xfd_register_done(priv);
	if (err)
		goto out_unregister_candev;

	/* Put controller into sleep mode and let pm_runtime_put()
	 * disable the clocks and vdd. If CONFIG_PM is not enabled,
	 * the clocks and vdd will stay powered.
	 */
	err = mcp251xfd_chip_sleep(priv);
	if (err)
		goto out_unregister_candev;

	pm_runtime_put(ndev->dev.parent);

	return 0;

out_unregister_candev:
	unregister_candev(ndev);
out_chip_sleep:
	mcp251xfd_chip_sleep(priv);
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

	if (pm_runtime_enabled(ndev->dev.parent))
		pm_runtime_disable(ndev->dev.parent);
	else
		mcp251xfd_clks_and_vdd_disable(priv);
}

static const struct of_device_id mcp251xfd_of_match[] = {
	{
		.compatible = "microchip,mcp2517fd",
		.data = &mcp251xfd_devtype_data_mcp2517fd,
	}, {
		.compatible = "microchip,mcp2518fd",
		.data = &mcp251xfd_devtype_data_mcp2518fd,
	}, {
		.compatible = "microchip,mcp251863",
		.data = &mcp251xfd_devtype_data_mcp251863,
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
		.name = "mcp251863",
		.driver_data = (kernel_ulong_t)&mcp251xfd_devtype_data_mcp251863,
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
	struct net_device *ndev;
	struct mcp251xfd_priv *priv;
	struct gpio_desc *rx_int;
	struct regulator *reg_vdd, *reg_xceiver;
	struct clk *clk;
	bool pll_enable = false;
	u32 freq = 0;
	int err;

	if (!spi->irq)
		return dev_err_probe(&spi->dev, -ENXIO,
				     "No IRQ specified (maybe node \"interrupts-extended\" in DT missing)!\n");

	rx_int = devm_gpiod_get_optional(&spi->dev, "microchip,rx-int",
					 GPIOD_IN);
	if (IS_ERR(rx_int))
		return dev_err_probe(&spi->dev, PTR_ERR(rx_int),
				     "Failed to get RX-INT!\n");

	reg_vdd = devm_regulator_get_optional(&spi->dev, "vdd");
	if (PTR_ERR(reg_vdd) == -ENODEV)
		reg_vdd = NULL;
	else if (IS_ERR(reg_vdd))
		return dev_err_probe(&spi->dev, PTR_ERR(reg_vdd),
				     "Failed to get VDD regulator!\n");

	reg_xceiver = devm_regulator_get_optional(&spi->dev, "xceiver");
	if (PTR_ERR(reg_xceiver) == -ENODEV)
		reg_xceiver = NULL;
	else if (IS_ERR(reg_xceiver))
		return dev_err_probe(&spi->dev, PTR_ERR(reg_xceiver),
				     "Failed to get Transceiver regulator!\n");

	clk = devm_clk_get_optional(&spi->dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(&spi->dev, PTR_ERR(clk),
				     "Failed to get Oscillator (clock)!\n");
	if (clk) {
		freq = clk_get_rate(clk);
	} else {
		err = device_property_read_u32(&spi->dev, "clock-frequency",
					       &freq);
		if (err)
			return dev_err_probe(&spi->dev, err,
					     "Failed to get clock-frequency!\n");
	}

	/* Sanity check */
	if (freq < MCP251XFD_SYSCLOCK_HZ_MIN ||
	    freq > MCP251XFD_SYSCLOCK_HZ_MAX) {
		dev_err(&spi->dev,
			"Oscillator frequency (%u Hz) is too low or high.\n",
			freq);
		return -ERANGE;
	}

	if (freq <= MCP251XFD_SYSCLOCK_HZ_MAX / MCP251XFD_OSC_PLL_MULTIPLIER)
		pll_enable = true;

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
	if (pll_enable)
		priv->can.clock.freq *= MCP251XFD_OSC_PLL_MULTIPLIER;
	priv->can.do_set_mode = mcp251xfd_set_mode;
	priv->can.do_get_berr_counter = mcp251xfd_get_berr_counter;
	priv->can.bittiming_const = &mcp251xfd_bittiming_const;
	priv->can.data_bittiming_const = &mcp251xfd_data_bittiming_const;
	priv->can.tdc_const = &mcp251xfd_tdc_const;
	priv->can.ctrlmode_supported = CAN_CTRLMODE_LOOPBACK |
		CAN_CTRLMODE_LISTENONLY | CAN_CTRLMODE_BERR_REPORTING |
		CAN_CTRLMODE_FD | CAN_CTRLMODE_FD_NON_ISO |
		CAN_CTRLMODE_CC_LEN8_DLC | CAN_CTRLMODE_TDC_AUTO |
		CAN_CTRLMODE_TDC_MANUAL;
	set_bit(MCP251XFD_FLAGS_DOWN, priv->flags);
	priv->ndev = ndev;
	priv->spi = spi;
	priv->rx_int = rx_int;
	priv->clk = clk;
	priv->pll_enable = pll_enable;
	priv->reg_vdd = reg_vdd;
	priv->reg_xceiver = reg_xceiver;
	priv->devtype_data = *(struct mcp251xfd_devtype_data *)spi_get_device_match_data(spi);

	/* Errata Reference:
	 * mcp2517fd: DS80000792C 5., mcp2518fd: DS80000789E 4.,
	 * mcp251863: DS80000984A 4.
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
	 * Known good combinations are:
	 *
	 * MCP	ext-clk	SoC			SPI			SPI-clk		max-clk	parent-clk	config
	 *
	 * 2518	20 MHz	allwinner,sun8i-h3	allwinner,sun8i-h3-spi	 8333333 Hz	 83.33%	600000000 Hz	assigned-clocks = <&ccu CLK_SPIx>
	 * 2518	40 MHz	allwinner,sun8i-h3	allwinner,sun8i-h3-spi	16666667 Hz	 83.33%	600000000 Hz	assigned-clocks = <&ccu CLK_SPIx>
	 * 2517	40 MHz	atmel,sama5d27		atmel,at91rm9200-spi	16400000 Hz	 82.00%	 82000000 Hz	default
	 * 2518	40 MHz	atmel,sama5d27		atmel,at91rm9200-spi	16400000 Hz	 82.00%	 82000000 Hz	default
	 * 2518	40 MHz	fsl,imx6dl		fsl,imx51-ecspi		15000000 Hz	 75.00%	 30000000 Hz	default
	 * 2517	20 MHz	fsl,imx8mm		fsl,imx51-ecspi		 8333333 Hz	 83.33%	 16666667 Hz	assigned-clocks = <&clk IMX8MM_CLK_ECSPIx_ROOT>
	 *
	 */
	priv->spi_max_speed_hz_orig = spi->max_speed_hz;
	priv->spi_max_speed_hz_slow = min(spi->max_speed_hz,
					  freq / 2 / 1000 * 850);
	if (priv->pll_enable)
		priv->spi_max_speed_hz_fast = min(spi->max_speed_hz,
						  freq *
						  MCP251XFD_OSC_PLL_MULTIPLIER /
						  2 / 1000 * 850);
	else
		priv->spi_max_speed_hz_fast = priv->spi_max_speed_hz_slow;
	spi->max_speed_hz = priv->spi_max_speed_hz_slow;
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
	if (err) {
		dev_err_probe(&spi->dev, err, "Failed to detect %s.\n",
			      mcp251xfd_get_model_str(priv));
		goto out_can_rx_offload_del;
	}

	return 0;

out_can_rx_offload_del:
	can_rx_offload_del(&priv->offload);
out_free_candev:
	spi->max_speed_hz = priv->spi_max_speed_hz_orig;

	free_candev(ndev);

	return err;
}

static void mcp251xfd_remove(struct spi_device *spi)
{
	struct mcp251xfd_priv *priv = spi_get_drvdata(spi);
	struct net_device *ndev = priv->ndev;

	mcp251xfd_unregister(priv);
	can_rx_offload_del(&priv->offload);
	spi->max_speed_hz = priv->spi_max_speed_hz_orig;
	free_candev(ndev);
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
