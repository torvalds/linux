// SPDX-License-Identifier: BSD-3-Clause
/* Copyright (c) 2016-2018, NXP Semiconductors
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#include <linux/packing.h>
#include "sja1105.h"

#define SJA1105_SIZE_CGU_CMD	4

struct sja1105_cfg_pad_mii_tx {
	u64 d32_os;
	u64 d32_ipud;
	u64 d10_os;
	u64 d10_ipud;
	u64 ctrl_os;
	u64 ctrl_ipud;
	u64 clk_os;
	u64 clk_ih;
	u64 clk_ipud;
};

/* UM10944 Table 82.
 * IDIV_0_C to IDIV_4_C control registers
 * (addr. 10000Bh to 10000Fh)
 */
struct sja1105_cgu_idiv {
	u64 clksrc;
	u64 autoblock;
	u64 idiv;
	u64 pd;
};

/* PLL_1_C control register
 *
 * SJA1105 E/T: UM10944 Table 81 (address 10000Ah)
 * SJA1105 P/Q/R/S: UM11040 Table 116 (address 10000Ah)
 */
struct sja1105_cgu_pll_ctrl {
	u64 pllclksrc;
	u64 msel;
	u64 autoblock;
	u64 psel;
	u64 direct;
	u64 fbsel;
	u64 bypass;
	u64 pd;
};

enum {
	CLKSRC_MII0_TX_CLK	= 0x00,
	CLKSRC_MII0_RX_CLK	= 0x01,
	CLKSRC_MII1_TX_CLK	= 0x02,
	CLKSRC_MII1_RX_CLK	= 0x03,
	CLKSRC_MII2_TX_CLK	= 0x04,
	CLKSRC_MII2_RX_CLK	= 0x05,
	CLKSRC_MII3_TX_CLK	= 0x06,
	CLKSRC_MII3_RX_CLK	= 0x07,
	CLKSRC_MII4_TX_CLK	= 0x08,
	CLKSRC_MII4_RX_CLK	= 0x09,
	CLKSRC_PLL0		= 0x0B,
	CLKSRC_PLL1		= 0x0E,
	CLKSRC_IDIV0		= 0x11,
	CLKSRC_IDIV1		= 0x12,
	CLKSRC_IDIV2		= 0x13,
	CLKSRC_IDIV3		= 0x14,
	CLKSRC_IDIV4		= 0x15,
};

/* UM10944 Table 83.
 * MIIx clock control registers 1 to 30
 * (addresses 100013h to 100035h)
 */
struct sja1105_cgu_mii_ctrl {
	u64 clksrc;
	u64 autoblock;
	u64 pd;
};

static void sja1105_cgu_idiv_packing(void *buf, struct sja1105_cgu_idiv *idiv,
				     enum packing_op op)
{
	const int size = 4;

	sja1105_packing(buf, &idiv->clksrc,    28, 24, size, op);
	sja1105_packing(buf, &idiv->autoblock, 11, 11, size, op);
	sja1105_packing(buf, &idiv->idiv,       5,  2, size, op);
	sja1105_packing(buf, &idiv->pd,         0,  0, size, op);
}

static int sja1105_cgu_idiv_config(struct sja1105_private *priv, int port,
				   bool enabled, int factor)
{
	const struct sja1105_regs *regs = priv->info->regs;
	struct device *dev = priv->ds->dev;
	struct sja1105_cgu_idiv idiv;
	u8 packed_buf[SJA1105_SIZE_CGU_CMD] = {0};

	if (enabled && factor != 1 && factor != 10) {
		dev_err(dev, "idiv factor must be 1 or 10\n");
		return -ERANGE;
	}

	/* Payload for packed_buf */
	idiv.clksrc    = 0x0A;            /* 25MHz */
	idiv.autoblock = 1;               /* Block clk automatically */
	idiv.idiv      = factor - 1;      /* Divide by 1 or 10 */
	idiv.pd        = enabled ? 0 : 1; /* Power down? */
	sja1105_cgu_idiv_packing(packed_buf, &idiv, PACK);

	return sja1105_spi_send_packed_buf(priv, SPI_WRITE,
					   regs->cgu_idiv[port], packed_buf,
					   SJA1105_SIZE_CGU_CMD);
}

static void
sja1105_cgu_mii_control_packing(void *buf, struct sja1105_cgu_mii_ctrl *cmd,
				enum packing_op op)
{
	const int size = 4;

	sja1105_packing(buf, &cmd->clksrc,    28, 24, size, op);
	sja1105_packing(buf, &cmd->autoblock, 11, 11, size, op);
	sja1105_packing(buf, &cmd->pd,         0,  0, size, op);
}

static int sja1105_cgu_mii_tx_clk_config(struct sja1105_private *priv,
					 int port, sja1105_mii_role_t role)
{
	const struct sja1105_regs *regs = priv->info->regs;
	struct sja1105_cgu_mii_ctrl mii_tx_clk;
	const int mac_clk_sources[] = {
		CLKSRC_MII0_TX_CLK,
		CLKSRC_MII1_TX_CLK,
		CLKSRC_MII2_TX_CLK,
		CLKSRC_MII3_TX_CLK,
		CLKSRC_MII4_TX_CLK,
	};
	const int phy_clk_sources[] = {
		CLKSRC_IDIV0,
		CLKSRC_IDIV1,
		CLKSRC_IDIV2,
		CLKSRC_IDIV3,
		CLKSRC_IDIV4,
	};
	u8 packed_buf[SJA1105_SIZE_CGU_CMD] = {0};
	int clksrc;

	if (role == XMII_MAC)
		clksrc = mac_clk_sources[port];
	else
		clksrc = phy_clk_sources[port];

	/* Payload for packed_buf */
	mii_tx_clk.clksrc    = clksrc;
	mii_tx_clk.autoblock = 1;  /* Autoblock clk while changing clksrc */
	mii_tx_clk.pd        = 0;  /* Power Down off => enabled */
	sja1105_cgu_mii_control_packing(packed_buf, &mii_tx_clk, PACK);

	return sja1105_spi_send_packed_buf(priv, SPI_WRITE,
					   regs->mii_tx_clk[port], packed_buf,
					   SJA1105_SIZE_CGU_CMD);
}

static int
sja1105_cgu_mii_rx_clk_config(struct sja1105_private *priv, int port)
{
	const struct sja1105_regs *regs = priv->info->regs;
	struct sja1105_cgu_mii_ctrl mii_rx_clk;
	u8 packed_buf[SJA1105_SIZE_CGU_CMD] = {0};
	const int clk_sources[] = {
		CLKSRC_MII0_RX_CLK,
		CLKSRC_MII1_RX_CLK,
		CLKSRC_MII2_RX_CLK,
		CLKSRC_MII3_RX_CLK,
		CLKSRC_MII4_RX_CLK,
	};

	/* Payload for packed_buf */
	mii_rx_clk.clksrc    = clk_sources[port];
	mii_rx_clk.autoblock = 1;  /* Autoblock clk while changing clksrc */
	mii_rx_clk.pd        = 0;  /* Power Down off => enabled */
	sja1105_cgu_mii_control_packing(packed_buf, &mii_rx_clk, PACK);

	return sja1105_spi_send_packed_buf(priv, SPI_WRITE,
					   regs->mii_rx_clk[port], packed_buf,
					   SJA1105_SIZE_CGU_CMD);
}

static int
sja1105_cgu_mii_ext_tx_clk_config(struct sja1105_private *priv, int port)
{
	const struct sja1105_regs *regs = priv->info->regs;
	struct sja1105_cgu_mii_ctrl mii_ext_tx_clk;
	u8 packed_buf[SJA1105_SIZE_CGU_CMD] = {0};
	const int clk_sources[] = {
		CLKSRC_IDIV0,
		CLKSRC_IDIV1,
		CLKSRC_IDIV2,
		CLKSRC_IDIV3,
		CLKSRC_IDIV4,
	};

	/* Payload for packed_buf */
	mii_ext_tx_clk.clksrc    = clk_sources[port];
	mii_ext_tx_clk.autoblock = 1; /* Autoblock clk while changing clksrc */
	mii_ext_tx_clk.pd        = 0; /* Power Down off => enabled */
	sja1105_cgu_mii_control_packing(packed_buf, &mii_ext_tx_clk, PACK);

	return sja1105_spi_send_packed_buf(priv, SPI_WRITE,
					   regs->mii_ext_tx_clk[port],
					   packed_buf, SJA1105_SIZE_CGU_CMD);
}

static int
sja1105_cgu_mii_ext_rx_clk_config(struct sja1105_private *priv, int port)
{
	const struct sja1105_regs *regs = priv->info->regs;
	struct sja1105_cgu_mii_ctrl mii_ext_rx_clk;
	u8 packed_buf[SJA1105_SIZE_CGU_CMD] = {0};
	const int clk_sources[] = {
		CLKSRC_IDIV0,
		CLKSRC_IDIV1,
		CLKSRC_IDIV2,
		CLKSRC_IDIV3,
		CLKSRC_IDIV4,
	};

	/* Payload for packed_buf */
	mii_ext_rx_clk.clksrc    = clk_sources[port];
	mii_ext_rx_clk.autoblock = 1; /* Autoblock clk while changing clksrc */
	mii_ext_rx_clk.pd        = 0; /* Power Down off => enabled */
	sja1105_cgu_mii_control_packing(packed_buf, &mii_ext_rx_clk, PACK);

	return sja1105_spi_send_packed_buf(priv, SPI_WRITE,
					   regs->mii_ext_rx_clk[port],
					   packed_buf, SJA1105_SIZE_CGU_CMD);
}

static int sja1105_mii_clocking_setup(struct sja1105_private *priv, int port,
				      sja1105_mii_role_t role)
{
	struct device *dev = priv->ds->dev;
	int rc;

	dev_dbg(dev, "Configuring MII-%s clocking\n",
		(role == XMII_MAC) ? "MAC" : "PHY");
	/* If role is MAC, disable IDIV
	 * If role is PHY, enable IDIV and configure for 1/1 divider
	 */
	rc = sja1105_cgu_idiv_config(priv, port, (role == XMII_PHY), 1);
	if (rc < 0)
		return rc;

	/* Configure CLKSRC of MII_TX_CLK_n
	 *   * If role is MAC, select TX_CLK_n
	 *   * If role is PHY, select IDIV_n
	 */
	rc = sja1105_cgu_mii_tx_clk_config(priv, port, role);
	if (rc < 0)
		return rc;

	/* Configure CLKSRC of MII_RX_CLK_n
	 * Select RX_CLK_n
	 */
	rc = sja1105_cgu_mii_rx_clk_config(priv, port);
	if (rc < 0)
		return rc;

	if (role == XMII_PHY) {
		/* Per MII spec, the PHY (which is us) drives the TX_CLK pin */

		/* Configure CLKSRC of EXT_TX_CLK_n
		 * Select IDIV_n
		 */
		rc = sja1105_cgu_mii_ext_tx_clk_config(priv, port);
		if (rc < 0)
			return rc;

		/* Configure CLKSRC of EXT_RX_CLK_n
		 * Select IDIV_n
		 */
		rc = sja1105_cgu_mii_ext_rx_clk_config(priv, port);
		if (rc < 0)
			return rc;
	}
	return 0;
}

static void
sja1105_cgu_pll_control_packing(void *buf, struct sja1105_cgu_pll_ctrl *cmd,
				enum packing_op op)
{
	const int size = 4;

	sja1105_packing(buf, &cmd->pllclksrc, 28, 24, size, op);
	sja1105_packing(buf, &cmd->msel,      23, 16, size, op);
	sja1105_packing(buf, &cmd->autoblock, 11, 11, size, op);
	sja1105_packing(buf, &cmd->psel,       9,  8, size, op);
	sja1105_packing(buf, &cmd->direct,     7,  7, size, op);
	sja1105_packing(buf, &cmd->fbsel,      6,  6, size, op);
	sja1105_packing(buf, &cmd->bypass,     1,  1, size, op);
	sja1105_packing(buf, &cmd->pd,         0,  0, size, op);
}

static int sja1105_cgu_rgmii_tx_clk_config(struct sja1105_private *priv,
					   int port, sja1105_speed_t speed)
{
	const struct sja1105_regs *regs = priv->info->regs;
	struct sja1105_cgu_mii_ctrl txc;
	u8 packed_buf[SJA1105_SIZE_CGU_CMD] = {0};
	int clksrc;

	if (speed == SJA1105_SPEED_1000MBPS) {
		clksrc = CLKSRC_PLL0;
	} else {
		int clk_sources[] = {CLKSRC_IDIV0, CLKSRC_IDIV1, CLKSRC_IDIV2,
				     CLKSRC_IDIV3, CLKSRC_IDIV4};
		clksrc = clk_sources[port];
	}

	/* RGMII: 125MHz for 1000, 25MHz for 100, 2.5MHz for 10 */
	txc.clksrc = clksrc;
	/* Autoblock clk while changing clksrc */
	txc.autoblock = 1;
	/* Power Down off => enabled */
	txc.pd = 0;
	sja1105_cgu_mii_control_packing(packed_buf, &txc, PACK);

	return sja1105_spi_send_packed_buf(priv, SPI_WRITE,
					   regs->rgmii_tx_clk[port],
					   packed_buf, SJA1105_SIZE_CGU_CMD);
}

/* AGU */
static void
sja1105_cfg_pad_mii_tx_packing(void *buf, struct sja1105_cfg_pad_mii_tx *cmd,
			       enum packing_op op)
{
	const int size = 4;

	sja1105_packing(buf, &cmd->d32_os,   28, 27, size, op);
	sja1105_packing(buf, &cmd->d32_ipud, 25, 24, size, op);
	sja1105_packing(buf, &cmd->d10_os,   20, 19, size, op);
	sja1105_packing(buf, &cmd->d10_ipud, 17, 16, size, op);
	sja1105_packing(buf, &cmd->ctrl_os,  12, 11, size, op);
	sja1105_packing(buf, &cmd->ctrl_ipud, 9,  8, size, op);
	sja1105_packing(buf, &cmd->clk_os,    4,  3, size, op);
	sja1105_packing(buf, &cmd->clk_ih,    2,  2, size, op);
	sja1105_packing(buf, &cmd->clk_ipud,  1,  0, size, op);
}

static int sja1105_rgmii_cfg_pad_tx_config(struct sja1105_private *priv,
					   int port)
{
	const struct sja1105_regs *regs = priv->info->regs;
	struct sja1105_cfg_pad_mii_tx pad_mii_tx;
	u8 packed_buf[SJA1105_SIZE_CGU_CMD] = {0};

	/* Payload */
	pad_mii_tx.d32_os    = 3; /* TXD[3:2] output stage: */
				  /*          high noise/high speed */
	pad_mii_tx.d10_os    = 3; /* TXD[1:0] output stage: */
				  /*          high noise/high speed */
	pad_mii_tx.d32_ipud  = 2; /* TXD[3:2] input stage: */
				  /*          plain input (default) */
	pad_mii_tx.d10_ipud  = 2; /* TXD[1:0] input stage: */
				  /*          plain input (default) */
	pad_mii_tx.ctrl_os   = 3; /* TX_CTL / TX_ER output stage */
	pad_mii_tx.ctrl_ipud = 2; /* TX_CTL / TX_ER input stage (default) */
	pad_mii_tx.clk_os    = 3; /* TX_CLK output stage */
	pad_mii_tx.clk_ih    = 0; /* TX_CLK input hysteresis (default) */
	pad_mii_tx.clk_ipud  = 2; /* TX_CLK input stage (default) */
	sja1105_cfg_pad_mii_tx_packing(packed_buf, &pad_mii_tx, PACK);

	return sja1105_spi_send_packed_buf(priv, SPI_WRITE,
					   regs->rgmii_pad_mii_tx[port],
					   packed_buf, SJA1105_SIZE_CGU_CMD);
}

static int sja1105_rgmii_clocking_setup(struct sja1105_private *priv, int port)
{
	struct device *dev = priv->ds->dev;
	struct sja1105_mac_config_entry *mac;
	sja1105_speed_t speed;
	int rc;

	mac = priv->static_config.tables[BLK_IDX_MAC_CONFIG].entries;
	speed = mac[port].speed;

	dev_dbg(dev, "Configuring port %d RGMII at speed %dMbps\n",
		port, speed);

	switch (speed) {
	case SJA1105_SPEED_1000MBPS:
		/* 1000Mbps, IDIV disabled (125 MHz) */
		rc = sja1105_cgu_idiv_config(priv, port, false, 1);
		break;
	case SJA1105_SPEED_100MBPS:
		/* 100Mbps, IDIV enabled, divide by 1 (25 MHz) */
		rc = sja1105_cgu_idiv_config(priv, port, true, 1);
		break;
	case SJA1105_SPEED_10MBPS:
		/* 10Mbps, IDIV enabled, divide by 10 (2.5 MHz) */
		rc = sja1105_cgu_idiv_config(priv, port, true, 10);
		break;
	case SJA1105_SPEED_AUTO:
		/* Skip CGU configuration if there is no speed available
		 * (e.g. link is not established yet)
		 */
		dev_dbg(dev, "Speed not available, skipping CGU config\n");
		return 0;
	default:
		rc = -EINVAL;
	}

	if (rc < 0) {
		dev_err(dev, "Failed to configure idiv\n");
		return rc;
	}
	rc = sja1105_cgu_rgmii_tx_clk_config(priv, port, speed);
	if (rc < 0) {
		dev_err(dev, "Failed to configure RGMII Tx clock\n");
		return rc;
	}
	rc = sja1105_rgmii_cfg_pad_tx_config(priv, port);
	if (rc < 0) {
		dev_err(dev, "Failed to configure Tx pad registers\n");
		return rc;
	}
	if (!priv->info->setup_rgmii_delay)
		return 0;

	return priv->info->setup_rgmii_delay(priv, port);
}

static int sja1105_cgu_rmii_ref_clk_config(struct sja1105_private *priv,
					   int port)
{
	const struct sja1105_regs *regs = priv->info->regs;
	struct sja1105_cgu_mii_ctrl ref_clk;
	u8 packed_buf[SJA1105_SIZE_CGU_CMD] = {0};
	const int clk_sources[] = {
		CLKSRC_MII0_TX_CLK,
		CLKSRC_MII1_TX_CLK,
		CLKSRC_MII2_TX_CLK,
		CLKSRC_MII3_TX_CLK,
		CLKSRC_MII4_TX_CLK,
	};

	/* Payload for packed_buf */
	ref_clk.clksrc    = clk_sources[port];
	ref_clk.autoblock = 1;      /* Autoblock clk while changing clksrc */
	ref_clk.pd        = 0;      /* Power Down off => enabled */
	sja1105_cgu_mii_control_packing(packed_buf, &ref_clk, PACK);

	return sja1105_spi_send_packed_buf(priv, SPI_WRITE,
					   regs->rmii_ref_clk[port],
					   packed_buf, SJA1105_SIZE_CGU_CMD);
}

static int
sja1105_cgu_rmii_ext_tx_clk_config(struct sja1105_private *priv, int port)
{
	const struct sja1105_regs *regs = priv->info->regs;
	struct sja1105_cgu_mii_ctrl ext_tx_clk;
	u8 packed_buf[SJA1105_SIZE_CGU_CMD] = {0};

	/* Payload for packed_buf */
	ext_tx_clk.clksrc    = CLKSRC_PLL1;
	ext_tx_clk.autoblock = 1;   /* Autoblock clk while changing clksrc */
	ext_tx_clk.pd        = 0;   /* Power Down off => enabled */
	sja1105_cgu_mii_control_packing(packed_buf, &ext_tx_clk, PACK);

	return sja1105_spi_send_packed_buf(priv, SPI_WRITE,
					   regs->rmii_ext_tx_clk[port],
					   packed_buf, SJA1105_SIZE_CGU_CMD);
}

static int sja1105_cgu_rmii_pll_config(struct sja1105_private *priv)
{
	const struct sja1105_regs *regs = priv->info->regs;
	u8 packed_buf[SJA1105_SIZE_CGU_CMD] = {0};
	struct sja1105_cgu_pll_ctrl pll = {0};
	struct device *dev = priv->ds->dev;
	int rc;

	/* PLL1 must be enabled and output 50 Mhz.
	 * This is done by writing first 0x0A010941 to
	 * the PLL_1_C register and then deasserting
	 * power down (PD) 0x0A010940.
	 */

	/* Step 1: PLL1 setup for 50Mhz */
	pll.pllclksrc = 0xA;
	pll.msel      = 0x1;
	pll.autoblock = 0x1;
	pll.psel      = 0x1;
	pll.direct    = 0x0;
	pll.fbsel     = 0x1;
	pll.bypass    = 0x0;
	pll.pd        = 0x1;

	sja1105_cgu_pll_control_packing(packed_buf, &pll, PACK);
	rc = sja1105_spi_send_packed_buf(priv, SPI_WRITE, regs->rmii_pll1,
					 packed_buf, SJA1105_SIZE_CGU_CMD);
	if (rc < 0) {
		dev_err(dev, "failed to configure PLL1 for 50MHz\n");
		return rc;
	}

	/* Step 2: Enable PLL1 */
	pll.pd = 0x0;

	sja1105_cgu_pll_control_packing(packed_buf, &pll, PACK);
	rc = sja1105_spi_send_packed_buf(priv, SPI_WRITE, regs->rmii_pll1,
					 packed_buf, SJA1105_SIZE_CGU_CMD);
	if (rc < 0) {
		dev_err(dev, "failed to enable PLL1\n");
		return rc;
	}
	return rc;
}

static int sja1105_rmii_clocking_setup(struct sja1105_private *priv, int port,
				       sja1105_mii_role_t role)
{
	struct device *dev = priv->ds->dev;
	int rc;

	dev_dbg(dev, "Configuring RMII-%s clocking\n",
		(role == XMII_MAC) ? "MAC" : "PHY");
	/* AH1601.pdf chapter 2.5.1. Sources */
	if (role == XMII_MAC) {
		/* Configure and enable PLL1 for 50Mhz output */
		rc = sja1105_cgu_rmii_pll_config(priv);
		if (rc < 0)
			return rc;
	}
	/* Disable IDIV for this port */
	rc = sja1105_cgu_idiv_config(priv, port, false, 1);
	if (rc < 0)
		return rc;
	/* Source to sink mappings */
	rc = sja1105_cgu_rmii_ref_clk_config(priv, port);
	if (rc < 0)
		return rc;
	if (role == XMII_MAC) {
		rc = sja1105_cgu_rmii_ext_tx_clk_config(priv, port);
		if (rc < 0)
			return rc;
	}
	return 0;
}

int sja1105_clocking_setup_port(struct sja1105_private *priv, int port)
{
	struct sja1105_xmii_params_entry *mii;
	struct device *dev = priv->ds->dev;
	sja1105_phy_interface_t phy_mode;
	sja1105_mii_role_t role;
	int rc;

	mii = priv->static_config.tables[BLK_IDX_XMII_PARAMS].entries;

	/* RGMII etc */
	phy_mode = mii->xmii_mode[port];
	/* MAC or PHY, for applicable types (not RGMII) */
	role = mii->phy_mac[port];

	switch (phy_mode) {
	case XMII_MODE_MII:
		rc = sja1105_mii_clocking_setup(priv, port, role);
		break;
	case XMII_MODE_RMII:
		rc = sja1105_rmii_clocking_setup(priv, port, role);
		break;
	case XMII_MODE_RGMII:
		rc = sja1105_rgmii_clocking_setup(priv, port);
		break;
	default:
		dev_err(dev, "Invalid interface mode specified: %d\n",
			phy_mode);
		return -EINVAL;
	}
	if (rc)
		dev_err(dev, "Clocking setup for port %d failed: %d\n",
			port, rc);
	return rc;
}

int sja1105_clocking_setup(struct sja1105_private *priv)
{
	int port, rc;

	for (port = 0; port < SJA1105_NUM_PORTS; port++) {
		rc = sja1105_clocking_setup_port(priv, port);
		if (rc < 0)
			return rc;
	}
	return 0;
}
