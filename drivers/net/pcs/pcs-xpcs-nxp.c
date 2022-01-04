// SPDX-License-Identifier: GPL-2.0
/* Copyright 2021 NXP Semiconductors
 */
#include <linux/pcs/pcs-xpcs.h>
#include "pcs-xpcs.h"

/* LANE_DRIVER1_0 register */
#define SJA1110_LANE_DRIVER1_0		0x8038
#define SJA1110_TXDRV(x)		(((x) << 12) & GENMASK(14, 12))

/* LANE_DRIVER2_0 register */
#define SJA1110_LANE_DRIVER2_0		0x803a
#define SJA1110_TXDRVTRIM_LSB(x)	((x) & GENMASK_ULL(15, 0))

/* LANE_DRIVER2_1 register */
#define SJA1110_LANE_DRIVER2_1		0x803b
#define SJA1110_LANE_DRIVER2_1_RSV	BIT(9)
#define SJA1110_TXDRVTRIM_MSB(x)	(((x) & GENMASK_ULL(23, 16)) >> 16)

/* LANE_TRIM register */
#define SJA1110_LANE_TRIM		0x8040
#define SJA1110_TXTEN			BIT(11)
#define SJA1110_TXRTRIM(x)		(((x) << 8) & GENMASK(10, 8))
#define SJA1110_TXPLL_BWSEL		BIT(7)
#define SJA1110_RXTEN			BIT(6)
#define SJA1110_RXRTRIM(x)		(((x) << 3) & GENMASK(5, 3))
#define SJA1110_CDR_GAIN		BIT(2)
#define SJA1110_ACCOUPLE_RXVCM_EN	BIT(0)

/* LANE_DATAPATH_1 register */
#define SJA1110_LANE_DATAPATH_1		0x8037

/* POWERDOWN_ENABLE register */
#define SJA1110_POWERDOWN_ENABLE	0x8041
#define SJA1110_TXPLL_PD		BIT(12)
#define SJA1110_TXPD			BIT(11)
#define SJA1110_RXPKDETEN		BIT(10)
#define SJA1110_RXCH_PD			BIT(9)
#define SJA1110_RXBIAS_PD		BIT(8)
#define SJA1110_RESET_SER_EN		BIT(7)
#define SJA1110_RESET_SER		BIT(6)
#define SJA1110_RESET_DES		BIT(5)
#define SJA1110_RCVEN			BIT(4)

/* RXPLL_CTRL0 register */
#define SJA1110_RXPLL_CTRL0		0x8065
#define SJA1110_RXPLL_FBDIV(x)		(((x) << 2) & GENMASK(9, 2))

/* RXPLL_CTRL1 register */
#define SJA1110_RXPLL_CTRL1		0x8066
#define SJA1110_RXPLL_REFDIV(x)		((x) & GENMASK(4, 0))

/* TXPLL_CTRL0 register */
#define SJA1110_TXPLL_CTRL0		0x806d
#define SJA1110_TXPLL_FBDIV(x)		((x) & GENMASK(11, 0))

/* TXPLL_CTRL1 register */
#define SJA1110_TXPLL_CTRL1		0x806e
#define SJA1110_TXPLL_REFDIV(x)		((x) & GENMASK(5, 0))

/* RX_DATA_DETECT register */
#define SJA1110_RX_DATA_DETECT		0x8045

/* RX_CDR_CTLE register */
#define SJA1110_RX_CDR_CTLE		0x8042

/* In NXP SJA1105, the PCS is integrated with a PMA that has the TX lane
 * polarity inverted by default (PLUS is MINUS, MINUS is PLUS). To obtain
 * normal non-inverted behavior, the TX lane polarity must be inverted in the
 * PCS, via the DIGITAL_CONTROL_2 register.
 */
int nxp_sja1105_sgmii_pma_config(struct dw_xpcs *xpcs)
{
	return xpcs_write(xpcs, MDIO_MMD_VEND2, DW_VR_MII_DIG_CTRL2,
			  DW_VR_MII_DIG_CTRL2_TX_POL_INV);
}

static int nxp_sja1110_pma_config(struct dw_xpcs *xpcs,
				  u16 txpll_fbdiv, u16 txpll_refdiv,
				  u16 rxpll_fbdiv, u16 rxpll_refdiv,
				  u16 rx_cdr_ctle)
{
	u16 val;
	int ret;

	/* Program TX PLL feedback divider and reference divider settings for
	 * correct oscillation frequency.
	 */
	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, SJA1110_TXPLL_CTRL0,
			 SJA1110_TXPLL_FBDIV(txpll_fbdiv));
	if (ret < 0)
		return ret;

	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, SJA1110_TXPLL_CTRL1,
			 SJA1110_TXPLL_REFDIV(txpll_refdiv));
	if (ret < 0)
		return ret;

	/* Program transmitter amplitude and disable amplitude trimming */
	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, SJA1110_LANE_DRIVER1_0,
			 SJA1110_TXDRV(0x5));
	if (ret < 0)
		return ret;

	val = SJA1110_TXDRVTRIM_LSB(0xffffffull);

	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, SJA1110_LANE_DRIVER2_0, val);
	if (ret < 0)
		return ret;

	val = SJA1110_TXDRVTRIM_MSB(0xffffffull) | SJA1110_LANE_DRIVER2_1_RSV;

	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, SJA1110_LANE_DRIVER2_1, val);
	if (ret < 0)
		return ret;

	/* Enable input and output resistor terminations for low BER. */
	val = SJA1110_ACCOUPLE_RXVCM_EN | SJA1110_CDR_GAIN |
	      SJA1110_RXRTRIM(4) | SJA1110_RXTEN | SJA1110_TXPLL_BWSEL |
	      SJA1110_TXRTRIM(3) | SJA1110_TXTEN;

	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, SJA1110_LANE_TRIM, val);
	if (ret < 0)
		return ret;

	/* Select PCS as transmitter data source. */
	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, SJA1110_LANE_DATAPATH_1, 0);
	if (ret < 0)
		return ret;

	/* Program RX PLL feedback divider and reference divider for correct
	 * oscillation frequency.
	 */
	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, SJA1110_RXPLL_CTRL0,
			 SJA1110_RXPLL_FBDIV(rxpll_fbdiv));
	if (ret < 0)
		return ret;

	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, SJA1110_RXPLL_CTRL1,
			 SJA1110_RXPLL_REFDIV(rxpll_refdiv));
	if (ret < 0)
		return ret;

	/* Program threshold for receiver signal detector.
	 * Enable control of RXPLL by receiver signal detector to disable RXPLL
	 * when an input signal is not present.
	 */
	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, SJA1110_RX_DATA_DETECT, 0x0005);
	if (ret < 0)
		return ret;

	/* Enable TX and RX PLLs and circuits.
	 * Release reset of PMA to enable data flow to/from PCS.
	 */
	ret = xpcs_read(xpcs, MDIO_MMD_VEND2, SJA1110_POWERDOWN_ENABLE);
	if (ret < 0)
		return ret;

	val = ret & ~(SJA1110_TXPLL_PD | SJA1110_TXPD | SJA1110_RXCH_PD |
		      SJA1110_RXBIAS_PD | SJA1110_RESET_SER_EN |
		      SJA1110_RESET_SER | SJA1110_RESET_DES);
	val |= SJA1110_RXPKDETEN | SJA1110_RCVEN;

	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, SJA1110_POWERDOWN_ENABLE, val);
	if (ret < 0)
		return ret;

	/* Program continuous-time linear equalizer (CTLE) settings. */
	ret = xpcs_write(xpcs, MDIO_MMD_VEND2, SJA1110_RX_CDR_CTLE,
			 rx_cdr_ctle);
	if (ret < 0)
		return ret;

	return 0;
}

int nxp_sja1110_sgmii_pma_config(struct dw_xpcs *xpcs)
{
	return nxp_sja1110_pma_config(xpcs, 0x19, 0x1, 0x19, 0x1, 0x212a);
}

int nxp_sja1110_2500basex_pma_config(struct dw_xpcs *xpcs)
{
	return nxp_sja1110_pma_config(xpcs, 0x7d, 0x2, 0x7d, 0x2, 0x732a);
}
