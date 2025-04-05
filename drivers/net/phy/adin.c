// SPDX-License-Identifier: GPL-2.0+
/*
 *  Driver for Analog Devices Industrial Ethernet PHYs
 *
 * Copyright 2019 Analog Devices Inc.
 */
#include <linux/kernel.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ethtool_netlink.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/property.h>

#define PHY_ID_ADIN1200				0x0283bc20
#define PHY_ID_ADIN1300				0x0283bc30

#define ADIN1300_MII_EXT_REG_PTR		0x0010
#define ADIN1300_MII_EXT_REG_DATA		0x0011

#define ADIN1300_PHY_CTRL1			0x0012
#define   ADIN1300_AUTO_MDI_EN			BIT(10)
#define   ADIN1300_MAN_MDIX_EN			BIT(9)
#define   ADIN1300_DIAG_CLK_EN			BIT(2)

#define ADIN1300_RX_ERR_CNT			0x0014

#define ADIN1300_PHY_CTRL_STATUS2		0x0015
#define   ADIN1300_NRG_PD_EN			BIT(3)
#define   ADIN1300_NRG_PD_TX_EN			BIT(2)
#define   ADIN1300_NRG_PD_STATUS		BIT(1)

#define ADIN1300_PHY_CTRL2			0x0016
#define   ADIN1300_DOWNSPEED_AN_100_EN		BIT(11)
#define   ADIN1300_DOWNSPEED_AN_10_EN		BIT(10)
#define   ADIN1300_GROUP_MDIO_EN		BIT(6)
#define   ADIN1300_DOWNSPEEDS_EN	\
	(ADIN1300_DOWNSPEED_AN_100_EN | ADIN1300_DOWNSPEED_AN_10_EN)

#define ADIN1300_PHY_CTRL3			0x0017
#define   ADIN1300_LINKING_EN			BIT(13)
#define   ADIN1300_DOWNSPEED_RETRIES_MSK	GENMASK(12, 10)

#define ADIN1300_INT_MASK_REG			0x0018
#define   ADIN1300_INT_MDIO_SYNC_EN		BIT(9)
#define   ADIN1300_INT_ANEG_STAT_CHNG_EN	BIT(8)
#define   ADIN1300_INT_ANEG_PAGE_RX_EN		BIT(6)
#define   ADIN1300_INT_IDLE_ERR_CNT_EN		BIT(5)
#define   ADIN1300_INT_MAC_FIFO_OU_EN		BIT(4)
#define   ADIN1300_INT_RX_STAT_CHNG_EN		BIT(3)
#define   ADIN1300_INT_LINK_STAT_CHNG_EN	BIT(2)
#define   ADIN1300_INT_SPEED_CHNG_EN		BIT(1)
#define   ADIN1300_INT_HW_IRQ_EN		BIT(0)
#define ADIN1300_INT_MASK_EN	\
	(ADIN1300_INT_LINK_STAT_CHNG_EN | ADIN1300_INT_HW_IRQ_EN)
#define ADIN1300_INT_STATUS_REG			0x0019

#define ADIN1300_PHY_STATUS1			0x001a
#define   ADIN1300_PAIR_01_SWAP			BIT(11)

/* EEE register addresses, accessible via Clause 22 access using
 * ADIN1300_MII_EXT_REG_PTR & ADIN1300_MII_EXT_REG_DATA.
 * The bit-fields are the same as specified by IEEE for EEE.
 */
#define ADIN1300_EEE_CAP_REG			0x8000
#define ADIN1300_EEE_ADV_REG			0x8001
#define ADIN1300_EEE_LPABLE_REG			0x8002

#define ADIN1300_FLD_EN_REG			0x8E27
#define   ADIN1300_FLD_PCS_ERR_100_EN		BIT(7)
#define   ADIN1300_FLD_PCS_ERR_1000_EN		BIT(6)
#define   ADIN1300_FLD_SLCR_OUT_STUCK_100_EN	BIT(5)
#define   ADIN1300_FLD_SLCR_OUT_STUCK_1000_EN	BIT(4)
#define   ADIN1300_FLD_SLCR_IN_ZDET_100_EN	BIT(3)
#define   ADIN1300_FLD_SLCR_IN_ZDET_1000_EN	BIT(2)
#define   ADIN1300_FLD_SLCR_IN_INVLD_100_EN	BIT(1)
#define   ADIN1300_FLD_SLCR_IN_INVLD_1000_EN	BIT(0)
/* These bits are the ones which are enabled by default. */
#define ADIN1300_FLD_EN_ON	\
	(ADIN1300_FLD_SLCR_OUT_STUCK_100_EN | \
	 ADIN1300_FLD_SLCR_OUT_STUCK_1000_EN | \
	 ADIN1300_FLD_SLCR_IN_ZDET_100_EN | \
	 ADIN1300_FLD_SLCR_IN_ZDET_1000_EN | \
	 ADIN1300_FLD_SLCR_IN_INVLD_1000_EN)

#define ADIN1300_CLOCK_STOP_REG			0x9400
#define ADIN1300_LPI_WAKE_ERR_CNT_REG		0xa000

#define ADIN1300_CDIAG_RUN			0xba1b
#define   ADIN1300_CDIAG_RUN_EN			BIT(0)

/*
 * The XSIM3/2/1 and XSHRT3/2/1 are actually relative.
 * For CDIAG_DTLD_RSLTS(0) it's ADIN1300_CDIAG_RSLT_XSIM3/2/1
 * For CDIAG_DTLD_RSLTS(1) it's ADIN1300_CDIAG_RSLT_XSIM3/2/0
 * For CDIAG_DTLD_RSLTS(2) it's ADIN1300_CDIAG_RSLT_XSIM3/1/0
 * For CDIAG_DTLD_RSLTS(3) it's ADIN1300_CDIAG_RSLT_XSIM2/1/0
 */
#define ADIN1300_CDIAG_DTLD_RSLTS(x)		(0xba1d + (x))
#define   ADIN1300_CDIAG_RSLT_BUSY		BIT(10)
#define   ADIN1300_CDIAG_RSLT_XSIM3		BIT(9)
#define   ADIN1300_CDIAG_RSLT_XSIM2		BIT(8)
#define   ADIN1300_CDIAG_RSLT_XSIM1		BIT(7)
#define   ADIN1300_CDIAG_RSLT_SIM		BIT(6)
#define   ADIN1300_CDIAG_RSLT_XSHRT3		BIT(5)
#define   ADIN1300_CDIAG_RSLT_XSHRT2		BIT(4)
#define   ADIN1300_CDIAG_RSLT_XSHRT1		BIT(3)
#define   ADIN1300_CDIAG_RSLT_SHRT		BIT(2)
#define   ADIN1300_CDIAG_RSLT_OPEN		BIT(1)
#define   ADIN1300_CDIAG_RSLT_GOOD		BIT(0)

#define ADIN1300_CDIAG_FLT_DIST(x)		(0xba21 + (x))

#define ADIN1300_GE_SOFT_RESET_REG		0xff0c
#define   ADIN1300_GE_SOFT_RESET		BIT(0)

#define ADIN1300_GE_CLK_CFG_REG			0xff1f
#define   ADIN1300_GE_CLK_CFG_MASK		GENMASK(5, 0)
#define   ADIN1300_GE_CLK_CFG_RCVR_125		BIT(5)
#define   ADIN1300_GE_CLK_CFG_FREE_125		BIT(4)
#define   ADIN1300_GE_CLK_CFG_REF_EN		BIT(3)
#define   ADIN1300_GE_CLK_CFG_HRT_RCVR		BIT(2)
#define   ADIN1300_GE_CLK_CFG_HRT_FREE		BIT(1)
#define   ADIN1300_GE_CLK_CFG_25		BIT(0)

#define ADIN1300_GE_RGMII_CFG_REG		0xff23
#define   ADIN1300_GE_RGMII_RX_MSK		GENMASK(8, 6)
#define   ADIN1300_GE_RGMII_RX_SEL(x)		\
		FIELD_PREP(ADIN1300_GE_RGMII_RX_MSK, x)
#define   ADIN1300_GE_RGMII_GTX_MSK		GENMASK(5, 3)
#define   ADIN1300_GE_RGMII_GTX_SEL(x)		\
		FIELD_PREP(ADIN1300_GE_RGMII_GTX_MSK, x)
#define   ADIN1300_GE_RGMII_RXID_EN		BIT(2)
#define   ADIN1300_GE_RGMII_TXID_EN		BIT(1)
#define   ADIN1300_GE_RGMII_EN			BIT(0)

/* RGMII internal delay settings for rx and tx for ADIN1300 */
#define ADIN1300_RGMII_1_60_NS			0x0001
#define ADIN1300_RGMII_1_80_NS			0x0002
#define	ADIN1300_RGMII_2_00_NS			0x0000
#define	ADIN1300_RGMII_2_20_NS			0x0006
#define	ADIN1300_RGMII_2_40_NS			0x0007

#define ADIN1300_GE_RMII_CFG_REG		0xff24
#define   ADIN1300_GE_RMII_FIFO_DEPTH_MSK	GENMASK(6, 4)
#define   ADIN1300_GE_RMII_FIFO_DEPTH_SEL(x)	\
		FIELD_PREP(ADIN1300_GE_RMII_FIFO_DEPTH_MSK, x)
#define   ADIN1300_GE_RMII_EN			BIT(0)

/* RMII fifo depth values */
#define ADIN1300_RMII_4_BITS			0x0000
#define ADIN1300_RMII_8_BITS			0x0001
#define ADIN1300_RMII_12_BITS			0x0002
#define ADIN1300_RMII_16_BITS			0x0003
#define ADIN1300_RMII_20_BITS			0x0004
#define ADIN1300_RMII_24_BITS			0x0005

/**
 * struct adin_cfg_reg_map - map a config value to aregister value
 * @cfg:	value in device configuration
 * @reg:	value in the register
 */
struct adin_cfg_reg_map {
	int cfg;
	int reg;
};

static const struct adin_cfg_reg_map adin_rgmii_delays[] = {
	{ 1600, ADIN1300_RGMII_1_60_NS },
	{ 1800, ADIN1300_RGMII_1_80_NS },
	{ 2000, ADIN1300_RGMII_2_00_NS },
	{ 2200, ADIN1300_RGMII_2_20_NS },
	{ 2400, ADIN1300_RGMII_2_40_NS },
	{ },
};

static const struct adin_cfg_reg_map adin_rmii_fifo_depths[] = {
	{ 4,  ADIN1300_RMII_4_BITS },
	{ 8,  ADIN1300_RMII_8_BITS },
	{ 12, ADIN1300_RMII_12_BITS },
	{ 16, ADIN1300_RMII_16_BITS },
	{ 20, ADIN1300_RMII_20_BITS },
	{ 24, ADIN1300_RMII_24_BITS },
	{ },
};

/**
 * struct adin_clause45_mmd_map - map to convert Clause 45 regs to Clause 22
 * @devad:		device address used in Clause 45 access
 * @cl45_regnum:	register address defined by Clause 45
 * @adin_regnum:	equivalent register address accessible via Clause 22
 */
struct adin_clause45_mmd_map {
	int devad;
	u16 cl45_regnum;
	u16 adin_regnum;
};

static const struct adin_clause45_mmd_map adin_clause45_mmd_map[] = {
	{ MDIO_MMD_PCS,	MDIO_PCS_EEE_ABLE,	ADIN1300_EEE_CAP_REG },
	{ MDIO_MMD_AN,	MDIO_AN_EEE_LPABLE,	ADIN1300_EEE_LPABLE_REG },
	{ MDIO_MMD_AN,	MDIO_AN_EEE_ADV,	ADIN1300_EEE_ADV_REG },
	{ MDIO_MMD_PCS,	MDIO_CTRL1,		ADIN1300_CLOCK_STOP_REG },
	{ MDIO_MMD_PCS, MDIO_PCS_EEE_WK_ERR,	ADIN1300_LPI_WAKE_ERR_CNT_REG },
};

struct adin_hw_stat {
	const char *string;
	u16 reg1;
	u16 reg2;
};

static const struct adin_hw_stat adin_hw_stats[] = {
	{ "total_frames_checked_count",		0x940A, 0x940B }, /* hi + lo */
	{ "length_error_frames_count",		0x940C },
	{ "alignment_error_frames_count",	0x940D },
	{ "symbol_error_count",			0x940E },
	{ "oversized_frames_count",		0x940F },
	{ "undersized_frames_count",		0x9410 },
	{ "odd_nibble_frames_count",		0x9411 },
	{ "odd_preamble_packet_count",		0x9412 },
	{ "dribble_bits_frames_count",		0x9413 },
	{ "false_carrier_events_count",		0x9414 },
};

/**
 * struct adin_priv - ADIN PHY driver private data
 * @stats:		statistic counters for the PHY
 */
struct adin_priv {
	u64			stats[ARRAY_SIZE(adin_hw_stats)];
};

static int adin_lookup_reg_value(const struct adin_cfg_reg_map *tbl, int cfg)
{
	size_t i;

	for (i = 0; tbl[i].cfg; i++) {
		if (tbl[i].cfg == cfg)
			return tbl[i].reg;
	}

	return -EINVAL;
}

static u32 adin_get_reg_value(struct phy_device *phydev,
			      const char *prop_name,
			      const struct adin_cfg_reg_map *tbl,
			      u32 dflt)
{
	struct device *dev = &phydev->mdio.dev;
	u32 val;
	int rc;

	if (device_property_read_u32(dev, prop_name, &val))
		return dflt;

	rc = adin_lookup_reg_value(tbl, val);
	if (rc < 0) {
		phydev_warn(phydev,
			    "Unsupported value %u for %s using default (%u)\n",
			    val, prop_name, dflt);
		return dflt;
	}

	return rc;
}

static int adin_config_rgmii_mode(struct phy_device *phydev)
{
	u32 val;
	int reg;

	if (!phy_interface_is_rgmii(phydev))
		return phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
					  ADIN1300_GE_RGMII_CFG_REG,
					  ADIN1300_GE_RGMII_EN);

	reg = phy_read_mmd(phydev, MDIO_MMD_VEND1, ADIN1300_GE_RGMII_CFG_REG);
	if (reg < 0)
		return reg;

	reg |= ADIN1300_GE_RGMII_EN;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) {
		reg |= ADIN1300_GE_RGMII_RXID_EN;

		val = adin_get_reg_value(phydev, "adi,rx-internal-delay-ps",
					 adin_rgmii_delays,
					 ADIN1300_RGMII_2_00_NS);
		reg &= ~ADIN1300_GE_RGMII_RX_MSK;
		reg |= ADIN1300_GE_RGMII_RX_SEL(val);
	} else {
		reg &= ~ADIN1300_GE_RGMII_RXID_EN;
	}

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		reg |= ADIN1300_GE_RGMII_TXID_EN;

		val = adin_get_reg_value(phydev, "adi,tx-internal-delay-ps",
					 adin_rgmii_delays,
					 ADIN1300_RGMII_2_00_NS);
		reg &= ~ADIN1300_GE_RGMII_GTX_MSK;
		reg |= ADIN1300_GE_RGMII_GTX_SEL(val);
	} else {
		reg &= ~ADIN1300_GE_RGMII_TXID_EN;
	}

	return phy_write_mmd(phydev, MDIO_MMD_VEND1,
			     ADIN1300_GE_RGMII_CFG_REG, reg);
}

static int adin_config_rmii_mode(struct phy_device *phydev)
{
	u32 val;
	int reg;

	if (phydev->interface != PHY_INTERFACE_MODE_RMII)
		return phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
					  ADIN1300_GE_RMII_CFG_REG,
					  ADIN1300_GE_RMII_EN);

	reg = phy_read_mmd(phydev, MDIO_MMD_VEND1, ADIN1300_GE_RMII_CFG_REG);
	if (reg < 0)
		return reg;

	reg |= ADIN1300_GE_RMII_EN;

	val = adin_get_reg_value(phydev, "adi,fifo-depth-bits",
				 adin_rmii_fifo_depths,
				 ADIN1300_RMII_8_BITS);

	reg &= ~ADIN1300_GE_RMII_FIFO_DEPTH_MSK;
	reg |= ADIN1300_GE_RMII_FIFO_DEPTH_SEL(val);

	return phy_write_mmd(phydev, MDIO_MMD_VEND1,
			     ADIN1300_GE_RMII_CFG_REG, reg);
}

static int adin_get_downshift(struct phy_device *phydev, u8 *data)
{
	int val, cnt, enable;

	val = phy_read(phydev, ADIN1300_PHY_CTRL2);
	if (val < 0)
		return val;

	cnt = phy_read(phydev, ADIN1300_PHY_CTRL3);
	if (cnt < 0)
		return cnt;

	enable = FIELD_GET(ADIN1300_DOWNSPEEDS_EN, val);
	cnt = FIELD_GET(ADIN1300_DOWNSPEED_RETRIES_MSK, cnt);

	*data = (enable && cnt) ? cnt : DOWNSHIFT_DEV_DISABLE;

	return 0;
}

static int adin_set_downshift(struct phy_device *phydev, u8 cnt)
{
	u16 val;
	int rc;

	if (cnt == DOWNSHIFT_DEV_DISABLE)
		return phy_clear_bits(phydev, ADIN1300_PHY_CTRL2,
				      ADIN1300_DOWNSPEEDS_EN);

	if (cnt > 7)
		return -E2BIG;

	val = FIELD_PREP(ADIN1300_DOWNSPEED_RETRIES_MSK, cnt);

	rc = phy_modify(phydev, ADIN1300_PHY_CTRL3,
			ADIN1300_DOWNSPEED_RETRIES_MSK,
			val);
	if (rc < 0)
		return rc;

	return phy_set_bits(phydev, ADIN1300_PHY_CTRL2,
			    ADIN1300_DOWNSPEEDS_EN);
}

static int adin_get_edpd(struct phy_device *phydev, u16 *tx_interval)
{
	int val;

	val = phy_read(phydev, ADIN1300_PHY_CTRL_STATUS2);
	if (val < 0)
		return val;

	if (ADIN1300_NRG_PD_EN & val) {
		if (val & ADIN1300_NRG_PD_TX_EN)
			/* default is 1 second */
			*tx_interval = ETHTOOL_PHY_EDPD_DFLT_TX_MSECS;
		else
			*tx_interval = ETHTOOL_PHY_EDPD_NO_TX;
	} else {
		*tx_interval = ETHTOOL_PHY_EDPD_DISABLE;
	}

	return 0;
}

static int adin_set_edpd(struct phy_device *phydev, u16 tx_interval)
{
	u16 val;

	if (tx_interval == ETHTOOL_PHY_EDPD_DISABLE)
		return phy_clear_bits(phydev, ADIN1300_PHY_CTRL_STATUS2,
				(ADIN1300_NRG_PD_EN | ADIN1300_NRG_PD_TX_EN));

	val = ADIN1300_NRG_PD_EN;

	switch (tx_interval) {
	case 1000: /* 1 second */
		fallthrough;
	case ETHTOOL_PHY_EDPD_DFLT_TX_MSECS:
		val |= ADIN1300_NRG_PD_TX_EN;
		fallthrough;
	case ETHTOOL_PHY_EDPD_NO_TX:
		break;
	default:
		return -EINVAL;
	}

	return phy_modify(phydev, ADIN1300_PHY_CTRL_STATUS2,
			  (ADIN1300_NRG_PD_EN | ADIN1300_NRG_PD_TX_EN),
			  val);
}

static int adin_get_fast_down(struct phy_device *phydev, u8 *msecs)
{
	int reg;

	reg = phy_read_mmd(phydev, MDIO_MMD_VEND1, ADIN1300_FLD_EN_REG);
	if (reg < 0)
		return reg;

	if (reg & ADIN1300_FLD_EN_ON)
		*msecs = ETHTOOL_PHY_FAST_LINK_DOWN_ON;
	else
		*msecs = ETHTOOL_PHY_FAST_LINK_DOWN_OFF;

	return 0;
}

static int adin_set_fast_down(struct phy_device *phydev, const u8 *msecs)
{
	if (*msecs == ETHTOOL_PHY_FAST_LINK_DOWN_ON)
		return phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
					ADIN1300_FLD_EN_REG,
					ADIN1300_FLD_EN_ON);

	if (*msecs == ETHTOOL_PHY_FAST_LINK_DOWN_OFF)
		return phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
					  ADIN1300_FLD_EN_REG,
					  ADIN1300_FLD_EN_ON);

	return -EINVAL;
}

static int adin_get_tunable(struct phy_device *phydev,
			    struct ethtool_tunable *tuna, void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return adin_get_downshift(phydev, data);
	case ETHTOOL_PHY_EDPD:
		return adin_get_edpd(phydev, data);
	case ETHTOOL_PHY_FAST_LINK_DOWN:
		return adin_get_fast_down(phydev, data);
	default:
		return -EOPNOTSUPP;
	}
}

static int adin_set_tunable(struct phy_device *phydev,
			    struct ethtool_tunable *tuna, const void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_DOWNSHIFT:
		return adin_set_downshift(phydev, *(const u8 *)data);
	case ETHTOOL_PHY_EDPD:
		return adin_set_edpd(phydev, *(const u16 *)data);
	case ETHTOOL_PHY_FAST_LINK_DOWN:
		return adin_set_fast_down(phydev, data);
	default:
		return -EOPNOTSUPP;
	}
}

static int adin_config_clk_out(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	const char *val = NULL;
	u8 sel = 0;

	device_property_read_string(dev, "adi,phy-output-clock", &val);
	if (!val) {
		/* property not present, do not enable GP_CLK pin */
	} else if (strcmp(val, "25mhz-reference") == 0) {
		sel |= ADIN1300_GE_CLK_CFG_25;
	} else if (strcmp(val, "125mhz-free-running") == 0) {
		sel |= ADIN1300_GE_CLK_CFG_FREE_125;
	} else if (strcmp(val, "adaptive-free-running") == 0) {
		sel |= ADIN1300_GE_CLK_CFG_HRT_FREE;
	} else {
		phydev_err(phydev, "invalid adi,phy-output-clock\n");
		return -EINVAL;
	}

	if (device_property_read_bool(dev, "adi,phy-output-reference-clock"))
		sel |= ADIN1300_GE_CLK_CFG_REF_EN;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND1, ADIN1300_GE_CLK_CFG_REG,
			      ADIN1300_GE_CLK_CFG_MASK, sel);
}

static int adin_config_init(struct phy_device *phydev)
{
	int rc;

	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;

	rc = adin_config_rgmii_mode(phydev);
	if (rc < 0)
		return rc;

	rc = adin_config_rmii_mode(phydev);
	if (rc < 0)
		return rc;

	rc = adin_set_downshift(phydev, 4);
	if (rc < 0)
		return rc;

	rc = adin_set_edpd(phydev, ETHTOOL_PHY_EDPD_DFLT_TX_MSECS);
	if (rc < 0)
		return rc;

	rc = adin_config_clk_out(phydev);
	if (rc < 0)
		return rc;

	phydev_dbg(phydev, "PHY is using mode '%s'\n",
		   phy_modes(phydev->interface));

	return 0;
}

static int adin_phy_ack_intr(struct phy_device *phydev)
{
	/* Clear pending interrupts */
	int rc = phy_read(phydev, ADIN1300_INT_STATUS_REG);

	return rc < 0 ? rc : 0;
}

static int adin_phy_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		err = adin_phy_ack_intr(phydev);
		if (err)
			return err;

		err = phy_set_bits(phydev, ADIN1300_INT_MASK_REG,
				   ADIN1300_INT_MASK_EN);
	} else {
		err = phy_clear_bits(phydev, ADIN1300_INT_MASK_REG,
				     ADIN1300_INT_MASK_EN);
		if (err)
			return err;

		err = adin_phy_ack_intr(phydev);
	}

	return err;
}

static irqreturn_t adin_phy_handle_interrupt(struct phy_device *phydev)
{
	int irq_status;

	irq_status = phy_read(phydev, ADIN1300_INT_STATUS_REG);
	if (irq_status < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	}

	if (!(irq_status & ADIN1300_INT_LINK_STAT_CHNG_EN))
		return IRQ_NONE;

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int adin_cl45_to_adin_reg(struct phy_device *phydev, int devad,
				 u16 cl45_regnum)
{
	const struct adin_clause45_mmd_map *m;
	int i;

	if (devad == MDIO_MMD_VEND1)
		return cl45_regnum;

	for (i = 0; i < ARRAY_SIZE(adin_clause45_mmd_map); i++) {
		m = &adin_clause45_mmd_map[i];
		if (m->devad == devad && m->cl45_regnum == cl45_regnum)
			return m->adin_regnum;
	}

	phydev_err(phydev,
		   "No translation available for devad: %d reg: %04x\n",
		   devad, cl45_regnum);

	return -EINVAL;
}

static int adin_read_mmd(struct phy_device *phydev, int devad, u16 regnum)
{
	struct mii_bus *bus = phydev->mdio.bus;
	int phy_addr = phydev->mdio.addr;
	int adin_regnum;
	int err;

	adin_regnum = adin_cl45_to_adin_reg(phydev, devad, regnum);
	if (adin_regnum < 0)
		return adin_regnum;

	err = __mdiobus_write(bus, phy_addr, ADIN1300_MII_EXT_REG_PTR,
			      adin_regnum);
	if (err)
		return err;

	return __mdiobus_read(bus, phy_addr, ADIN1300_MII_EXT_REG_DATA);
}

static int adin_write_mmd(struct phy_device *phydev, int devad, u16 regnum,
			  u16 val)
{
	struct mii_bus *bus = phydev->mdio.bus;
	int phy_addr = phydev->mdio.addr;
	int adin_regnum;
	int err;

	adin_regnum = adin_cl45_to_adin_reg(phydev, devad, regnum);
	if (adin_regnum < 0)
		return adin_regnum;

	err = __mdiobus_write(bus, phy_addr, ADIN1300_MII_EXT_REG_PTR,
			      adin_regnum);
	if (err)
		return err;

	return __mdiobus_write(bus, phy_addr, ADIN1300_MII_EXT_REG_DATA, val);
}

static int adin_config_mdix(struct phy_device *phydev)
{
	bool auto_en, mdix_en;
	int reg;

	mdix_en = false;
	auto_en = false;
	switch (phydev->mdix_ctrl) {
	case ETH_TP_MDI:
		break;
	case ETH_TP_MDI_X:
		mdix_en = true;
		break;
	case ETH_TP_MDI_AUTO:
		auto_en = true;
		break;
	default:
		return -EINVAL;
	}

	reg = phy_read(phydev, ADIN1300_PHY_CTRL1);
	if (reg < 0)
		return reg;

	if (mdix_en)
		reg |= ADIN1300_MAN_MDIX_EN;
	else
		reg &= ~ADIN1300_MAN_MDIX_EN;

	if (auto_en)
		reg |= ADIN1300_AUTO_MDI_EN;
	else
		reg &= ~ADIN1300_AUTO_MDI_EN;

	return phy_write(phydev, ADIN1300_PHY_CTRL1, reg);
}

static int adin_config_aneg(struct phy_device *phydev)
{
	int ret;

	ret = phy_clear_bits(phydev, ADIN1300_PHY_CTRL1, ADIN1300_DIAG_CLK_EN);
	if (ret < 0)
		return ret;

	ret = phy_set_bits(phydev, ADIN1300_PHY_CTRL3, ADIN1300_LINKING_EN);
	if (ret < 0)
		return ret;

	ret = adin_config_mdix(phydev);
	if (ret)
		return ret;

	return genphy_config_aneg(phydev);
}

static int adin_mdix_update(struct phy_device *phydev)
{
	bool auto_en, mdix_en;
	bool swapped;
	int reg;

	reg = phy_read(phydev, ADIN1300_PHY_CTRL1);
	if (reg < 0)
		return reg;

	auto_en = !!(reg & ADIN1300_AUTO_MDI_EN);
	mdix_en = !!(reg & ADIN1300_MAN_MDIX_EN);

	/* If MDI/MDIX is forced, just read it from the control reg */
	if (!auto_en) {
		if (mdix_en)
			phydev->mdix = ETH_TP_MDI_X;
		else
			phydev->mdix = ETH_TP_MDI;
		return 0;
	}

	/**
	 * Otherwise, we need to deduce it from the PHY status2 reg.
	 * When Auto-MDI is enabled, the ADIN1300_MAN_MDIX_EN bit implies
	 * a preference for MDIX when it is set.
	 */
	reg = phy_read(phydev, ADIN1300_PHY_STATUS1);
	if (reg < 0)
		return reg;

	swapped = !!(reg & ADIN1300_PAIR_01_SWAP);

	if (mdix_en != swapped)
		phydev->mdix = ETH_TP_MDI_X;
	else
		phydev->mdix = ETH_TP_MDI;

	return 0;
}

static int adin_read_status(struct phy_device *phydev)
{
	int ret;

	ret = adin_mdix_update(phydev);
	if (ret < 0)
		return ret;

	return genphy_read_status(phydev);
}

static int adin_soft_reset(struct phy_device *phydev)
{
	int rc;

	/* The reset bit is self-clearing, set it and wait */
	rc = phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
			      ADIN1300_GE_SOFT_RESET_REG,
			      ADIN1300_GE_SOFT_RESET);
	if (rc < 0)
		return rc;

	msleep(20);

	/* If we get a read error something may be wrong */
	rc = phy_read_mmd(phydev, MDIO_MMD_VEND1,
			  ADIN1300_GE_SOFT_RESET_REG);

	return rc < 0 ? rc : 0;
}

static int adin_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(adin_hw_stats);
}

static void adin_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(adin_hw_stats); i++)
		ethtool_puts(&data, adin_hw_stats[i].string);
}

static int adin_read_mmd_stat_regs(struct phy_device *phydev,
				   const struct adin_hw_stat *stat,
				   u32 *val)
{
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, stat->reg1);
	if (ret < 0)
		return ret;

	*val = (ret & 0xffff);

	if (stat->reg2 == 0)
		return 0;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, stat->reg2);
	if (ret < 0)
		return ret;

	*val <<= 16;
	*val |= (ret & 0xffff);

	return 0;
}

static u64 adin_get_stat(struct phy_device *phydev, int i)
{
	const struct adin_hw_stat *stat = &adin_hw_stats[i];
	struct adin_priv *priv = phydev->priv;
	u32 val;
	int ret;

	if (stat->reg1 > 0x1f) {
		ret = adin_read_mmd_stat_regs(phydev, stat, &val);
		if (ret < 0)
			return (u64)(~0);
	} else {
		ret = phy_read(phydev, stat->reg1);
		if (ret < 0)
			return (u64)(~0);
		val = (ret & 0xffff);
	}

	priv->stats[i] += val;

	return priv->stats[i];
}

static void adin_get_stats(struct phy_device *phydev,
			   struct ethtool_stats *stats, u64 *data)
{
	int i, rc;

	/* latch copies of all the frame-checker counters */
	rc = phy_read(phydev, ADIN1300_RX_ERR_CNT);
	if (rc < 0)
		return;

	for (i = 0; i < ARRAY_SIZE(adin_hw_stats); i++)
		data[i] = adin_get_stat(phydev, i);
}

static int adin_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct adin_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	return 0;
}

static int adin_cable_test_start(struct phy_device *phydev)
{
	int ret;

	ret = phy_clear_bits(phydev, ADIN1300_PHY_CTRL3, ADIN1300_LINKING_EN);
	if (ret < 0)
		return ret;

	ret = phy_clear_bits(phydev, ADIN1300_PHY_CTRL1, ADIN1300_DIAG_CLK_EN);
	if (ret < 0)
		return ret;

	/* wait a bit for the clock to stabilize */
	msleep(50);

	return phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, ADIN1300_CDIAG_RUN,
				ADIN1300_CDIAG_RUN_EN);
}

static int adin_cable_test_report_trans(int result)
{
	int mask;

	if (result & ADIN1300_CDIAG_RSLT_GOOD)
		return ETHTOOL_A_CABLE_RESULT_CODE_OK;
	if (result & ADIN1300_CDIAG_RSLT_OPEN)
		return ETHTOOL_A_CABLE_RESULT_CODE_OPEN;

	/* short with other pairs */
	mask = ADIN1300_CDIAG_RSLT_XSHRT3 |
	       ADIN1300_CDIAG_RSLT_XSHRT2 |
	       ADIN1300_CDIAG_RSLT_XSHRT1;
	if (result & mask)
		return ETHTOOL_A_CABLE_RESULT_CODE_CROSS_SHORT;

	if (result & ADIN1300_CDIAG_RSLT_SHRT)
		return ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;

	return ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
}

static int adin_cable_test_report_pair(struct phy_device *phydev,
				       unsigned int pair)
{
	int fault_rslt;
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1,
			   ADIN1300_CDIAG_DTLD_RSLTS(pair));
	if (ret < 0)
		return ret;

	fault_rslt = adin_cable_test_report_trans(ret);

	ret = ethnl_cable_test_result(phydev, pair, fault_rslt);
	if (ret < 0)
		return ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1,
			   ADIN1300_CDIAG_FLT_DIST(pair));
	if (ret < 0)
		return ret;

	switch (fault_rslt) {
	case ETHTOOL_A_CABLE_RESULT_CODE_OPEN:
	case ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT:
	case ETHTOOL_A_CABLE_RESULT_CODE_CROSS_SHORT:
		return ethnl_cable_test_fault_length(phydev, pair, ret * 100);
	default:
		return  0;
	}
}

static int adin_cable_test_report(struct phy_device *phydev)
{
	unsigned int pair;
	int ret;

	for (pair = ETHTOOL_A_CABLE_PAIR_A; pair <= ETHTOOL_A_CABLE_PAIR_D; pair++) {
		ret = adin_cable_test_report_pair(phydev, pair);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int adin_cable_test_get_status(struct phy_device *phydev,
				      bool *finished)
{
	int ret;

	*finished = false;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, ADIN1300_CDIAG_RUN);
	if (ret < 0)
		return ret;

	if (ret & ADIN1300_CDIAG_RUN_EN)
		return 0;

	*finished = true;

	return adin_cable_test_report(phydev);
}

static struct phy_driver adin_driver[] = {
	{
		PHY_ID_MATCH_MODEL(PHY_ID_ADIN1200),
		.name		= "ADIN1200",
		.flags		= PHY_POLL_CABLE_TEST,
		.probe		= adin_probe,
		.config_init	= adin_config_init,
		.soft_reset	= adin_soft_reset,
		.config_aneg	= adin_config_aneg,
		.read_status	= adin_read_status,
		.get_tunable	= adin_get_tunable,
		.set_tunable	= adin_set_tunable,
		.config_intr	= adin_phy_config_intr,
		.handle_interrupt = adin_phy_handle_interrupt,
		.get_sset_count	= adin_get_sset_count,
		.get_strings	= adin_get_strings,
		.get_stats	= adin_get_stats,
		.resume		= genphy_resume,
		.suspend	= genphy_suspend,
		.read_mmd	= adin_read_mmd,
		.write_mmd	= adin_write_mmd,
		.cable_test_start	= adin_cable_test_start,
		.cable_test_get_status	= adin_cable_test_get_status,
	},
	{
		PHY_ID_MATCH_MODEL(PHY_ID_ADIN1300),
		.name		= "ADIN1300",
		.flags		= PHY_POLL_CABLE_TEST,
		.probe		= adin_probe,
		.config_init	= adin_config_init,
		.soft_reset	= adin_soft_reset,
		.config_aneg	= adin_config_aneg,
		.read_status	= adin_read_status,
		.get_tunable	= adin_get_tunable,
		.set_tunable	= adin_set_tunable,
		.config_intr	= adin_phy_config_intr,
		.handle_interrupt = adin_phy_handle_interrupt,
		.get_sset_count	= adin_get_sset_count,
		.get_strings	= adin_get_strings,
		.get_stats	= adin_get_stats,
		.resume		= genphy_resume,
		.suspend	= genphy_suspend,
		.read_mmd	= adin_read_mmd,
		.write_mmd	= adin_write_mmd,
		.cable_test_start	= adin_cable_test_start,
		.cable_test_get_status	= adin_cable_test_get_status,
	},
};

module_phy_driver(adin_driver);

static const struct mdio_device_id __maybe_unused adin_tbl[] = {
	{ PHY_ID_MATCH_MODEL(PHY_ID_ADIN1200) },
	{ PHY_ID_MATCH_MODEL(PHY_ID_ADIN1300) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, adin_tbl);
MODULE_DESCRIPTION("Analog Devices Industrial Ethernet PHY driver");
MODULE_LICENSE("GPL");
