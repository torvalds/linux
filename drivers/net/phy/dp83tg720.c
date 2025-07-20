// SPDX-License-Identifier: GPL-2.0
/* Driver for the Texas Instruments DP83TG720 PHY
 * Copyright (c) 2023 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
 */
#include <linux/bitfield.h>
#include <linux/ethtool_netlink.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/random.h>

#include "open_alliance_helpers.h"

/*
 * DP83TG720S_POLL_ACTIVE_LINK - Polling interval in milliseconds when the link
 *				 is active.
 * DP83TG720S_POLL_NO_LINK_MIN - Minimum polling interval in milliseconds when
 *				 the link is down.
 * DP83TG720S_POLL_NO_LINK_MAX - Maximum polling interval in milliseconds when
 *				 the link is down.
 *
 * These values are not documented or officially recommended by the vendor but
 * were determined through empirical testing. They achieve a good balance in
 * minimizing the number of reset retries while ensuring reliable link recovery
 * within a reasonable timeframe.
 */
#define DP83TG720S_POLL_ACTIVE_LINK		1000
#define DP83TG720S_POLL_NO_LINK_MIN		100
#define DP83TG720S_POLL_NO_LINK_MAX		1000

#define DP83TG720S_PHY_ID			0x2000a284

/* MDIO_MMD_VEND2 registers */
#define DP83TG720S_MII_REG_10			0x10
#define DP83TG720S_STS_MII_INT			BIT(7)
#define DP83TG720S_LINK_STATUS			BIT(0)

/* TDR Configuration Register (0x1E) */
#define DP83TG720S_TDR_CFG			0x1e
/* 1b = TDR start, 0b = No TDR */
#define DP83TG720S_TDR_START			BIT(15)
/* 1b = TDR auto on link down, 0b = Manual TDR start */
#define DP83TG720S_CFG_TDR_AUTO_RUN		BIT(14)
/* 1b = TDR done, 0b = TDR in progress */
#define DP83TG720S_TDR_DONE			BIT(1)
/* 1b = TDR fail, 0b = TDR success */
#define DP83TG720S_TDR_FAIL			BIT(0)

#define DP83TG720S_PHY_RESET			0x1f
#define DP83TG720S_HW_RESET			BIT(15)

#define DP83TG720S_LPS_CFG3			0x18c
/* Power modes are documented as bit fields but used as values */
/* Power Mode 0 is Normal mode */
#define DP83TG720S_LPS_CFG3_PWR_MODE_0		BIT(0)

/* Open Aliance 1000BaseT1 compatible HDD.TDR Fault Status Register */
#define DP83TG720S_TDR_FAULT_STATUS		0x30f

/* Register 0x0301: TDR Configuration 2 */
#define DP83TG720S_TDR_CFG2			0x301

/* Register 0x0303: TDR Configuration 3 */
#define DP83TG720S_TDR_CFG3			0x303

/* Register 0x0304: TDR Configuration 4 */
#define DP83TG720S_TDR_CFG4			0x304

/* Register 0x0405: Unknown Register */
#define DP83TG720S_UNKNOWN_0405			0x405

#define DP83TG720S_LINK_QUAL_3			0x547
#define DP83TG720S_LINK_LOSS_CNT_MASK		GENMASK(15, 10)

/* Register 0x0576: TDR Master Link Down Control */
#define DP83TG720S_TDR_MASTER_LINK_DOWN		0x576

#define DP83TG720S_RGMII_DELAY_CTRL		0x602
/* In RGMII mode, Enable or disable the internal delay for RXD */
#define DP83TG720S_RGMII_RX_CLK_SEL		BIT(1)
/* In RGMII mode, Enable or disable the internal delay for TXD */
#define DP83TG720S_RGMII_TX_CLK_SEL		BIT(0)

/*
 * DP83TG720S_PKT_STAT_x registers correspond to similarly named registers
 * in the datasheet (PKT_STAT_1 through PKT_STAT_6). These registers store
 * 32-bit or 16-bit counters for TX and RX statistics and must be read in
 * sequence to ensure the counters are cleared correctly.
 *
 * - DP83TG720S_PKT_STAT_1: Contains TX packet count bits [15:0].
 * - DP83TG720S_PKT_STAT_2: Contains TX packet count bits [31:16].
 * - DP83TG720S_PKT_STAT_3: Contains TX error packet count.
 * - DP83TG720S_PKT_STAT_4: Contains RX packet count bits [15:0].
 * - DP83TG720S_PKT_STAT_5: Contains RX packet count bits [31:16].
 * - DP83TG720S_PKT_STAT_6: Contains RX error packet count.
 *
 * Keeping the register names as defined in the datasheet helps maintain
 * clarity and alignment with the documentation.
 */
#define DP83TG720S_PKT_STAT_1			0x639
#define DP83TG720S_PKT_STAT_2			0x63a
#define DP83TG720S_PKT_STAT_3			0x63b
#define DP83TG720S_PKT_STAT_4			0x63c
#define DP83TG720S_PKT_STAT_5			0x63d
#define DP83TG720S_PKT_STAT_6			0x63e

/* Register 0x083F: Unknown Register */
#define DP83TG720S_UNKNOWN_083F			0x83f

#define DP83TG720S_SQI_REG_1			0x871
#define DP83TG720S_SQI_OUT_WORST		GENMASK(7, 5)
#define DP83TG720S_SQI_OUT			GENMASK(3, 1)

#define DP83TG720_SQI_MAX			7

struct dp83tg720_stats {
	u64 link_loss_cnt;
	u64 tx_pkt_cnt;
	u64 tx_err_pkt_cnt;
	u64 rx_pkt_cnt;
	u64 rx_err_pkt_cnt;
};

struct dp83tg720_priv {
	struct dp83tg720_stats stats;
};

/**
 * dp83tg720_update_stats - Update the PHY statistics for the DP83TD510 PHY.
 * @phydev: Pointer to the phy_device structure.
 *
 * The function reads the PHY statistics registers and updates the statistics
 * structure.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
static int dp83tg720_update_stats(struct phy_device *phydev)
{
	struct dp83tg720_priv *priv = phydev->priv;
	u32 count;
	int ret;

	/* Read the link loss count */
	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_LINK_QUAL_3);
	if (ret < 0)
		return ret;
	/* link_loss_cnt */
	count = FIELD_GET(DP83TG720S_LINK_LOSS_CNT_MASK, ret);
	priv->stats.link_loss_cnt += count;

	/* The DP83TG720S_PKT_STAT registers are divided into two groups:
	 * - Group 1 (TX stats): DP83TG720S_PKT_STAT_1 to DP83TG720S_PKT_STAT_3
	 * - Group 2 (RX stats): DP83TG720S_PKT_STAT_4 to DP83TG720S_PKT_STAT_6
	 *
	 * Registers in each group are cleared only after reading them in a
	 * plain sequence (e.g., 1, 2, 3 for Group 1 or 4, 5, 6 for Group 2).
	 * Any deviation from the sequence, such as reading 1, 2, 1, 2, 3, will
	 * prevent the group from being cleared. Additionally, the counters
	 * for a group are frozen as soon as the first register in that group
	 * is accessed.
	 */
	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_PKT_STAT_1);
	if (ret < 0)
		return ret;
	/* tx_pkt_cnt_15_0 */
	count = ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_PKT_STAT_2);
	if (ret < 0)
		return ret;
	/* tx_pkt_cnt_31_16 */
	count |= ret << 16;
	priv->stats.tx_pkt_cnt += count;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_PKT_STAT_3);
	if (ret < 0)
		return ret;
	/* tx_err_pkt_cnt */
	priv->stats.tx_err_pkt_cnt += ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_PKT_STAT_4);
	if (ret < 0)
		return ret;
	/* rx_pkt_cnt_15_0 */
	count = ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_PKT_STAT_5);
	if (ret < 0)
		return ret;
	/* rx_pkt_cnt_31_16 */
	count |= ret << 16;
	priv->stats.rx_pkt_cnt += count;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_PKT_STAT_6);
	if (ret < 0)
		return ret;
	/* rx_err_pkt_cnt */
	priv->stats.rx_err_pkt_cnt += ret;

	return 0;
}

static void dp83tg720_get_link_stats(struct phy_device *phydev,
				     struct ethtool_link_ext_stats *link_stats)
{
	struct dp83tg720_priv *priv = phydev->priv;

	link_stats->link_down_events = priv->stats.link_loss_cnt;
}

static void dp83tg720_get_phy_stats(struct phy_device *phydev,
				    struct ethtool_eth_phy_stats *eth_stats,
				    struct ethtool_phy_stats *stats)
{
	struct dp83tg720_priv *priv = phydev->priv;

	stats->tx_packets = priv->stats.tx_pkt_cnt;
	stats->tx_errors = priv->stats.tx_err_pkt_cnt;
	stats->rx_packets = priv->stats.rx_pkt_cnt;
	stats->rx_errors = priv->stats.rx_err_pkt_cnt;
}

/**
 * dp83tg720_cable_test_start - Start the cable test for the DP83TG720 PHY.
 * @phydev: Pointer to the phy_device structure.
 *
 * This sequence is based on the documented procedure for the DP83TG720 PHY.
 *
 * Returns: 0 on success, a negative error code on failure.
 */
static int dp83tg720_cable_test_start(struct phy_device *phydev)
{
	int ret;

	/* Initialize the PHY to run the TDR test as described in the
	 * "DP83TG720S-Q1: Configuring for Open Alliance Specification
	 * Compliance (Rev. B)" application note.
	 * Most of the registers are not documented. Some of register names
	 * are guessed by comparing the register offsets with the DP83TD510E.
	 */

	/* Force master link down */
	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2,
			       DP83TG720S_TDR_MASTER_LINK_DOWN, 0x0400);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_TDR_CFG2,
			    0xa008);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_TDR_CFG3,
			    0x0928);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_TDR_CFG4,
			    0x0004);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_UNKNOWN_0405,
			    0x6400);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_UNKNOWN_083F,
			    0x3003);
	if (ret)
		return ret;

	/* Start the TDR */
	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_TDR_CFG,
			       DP83TG720S_TDR_START);
	if (ret)
		return ret;

	return 0;
}

/**
 * dp83tg720_cable_test_get_status - Get the status of the cable test for the
 *                                   DP83TG720 PHY.
 * @phydev: Pointer to the phy_device structure.
 * @finished: Pointer to a boolean that indicates whether the test is finished.
 *
 * The function sets the @finished flag to true if the test is complete.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
static int dp83tg720_cable_test_get_status(struct phy_device *phydev,
					   bool *finished)
{
	int ret, stat;

	*finished = false;

	/* Read the TDR status */
	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_TDR_CFG);
	if (ret < 0)
		return ret;

	/* Check if the TDR test is done */
	if (!(ret & DP83TG720S_TDR_DONE))
		return 0;

	/* Check for TDR test failure */
	if (!(ret & DP83TG720S_TDR_FAIL)) {
		int location;

		/* Read fault status */
		ret = phy_read_mmd(phydev, MDIO_MMD_VEND2,
				   DP83TG720S_TDR_FAULT_STATUS);
		if (ret < 0)
			return ret;

		/* Get fault type */
		stat = oa_1000bt1_get_ethtool_cable_result_code(ret);

		/* Determine fault location */
		location = oa_1000bt1_get_tdr_distance(ret);
		if (location > 0)
			ethnl_cable_test_fault_length(phydev,
						      ETHTOOL_A_CABLE_PAIR_A,
						      location);
	} else {
		/* Active link partner or other issues */
		stat = ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}

	*finished = true;

	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A, stat);

	/* save the current stats before resetting the PHY */
	ret = dp83tg720_update_stats(phydev);
	if (ret)
		return ret;

	return phy_init_hw(phydev);
}

static int dp83tg720_config_aneg(struct phy_device *phydev)
{
	int ret;

	/* Autoneg is not supported and this PHY supports only one speed.
	 * We need to care only about master/slave configuration if it was
	 * changed by user.
	 */
	ret = genphy_c45_pma_baset1_setup_master_slave(phydev);
	if (ret)
		return ret;

	/* Re-read role configuration to make changes visible even if
	 * the link is in administrative down state.
	 */
	return genphy_c45_pma_baset1_read_master_slave(phydev);
}

static int dp83tg720_read_status(struct phy_device *phydev)
{
	u16 phy_sts;
	int ret;

	phydev->pause = 0;
	phydev->asym_pause = 0;

	/* Most of Clause 45 registers are not present, so we can't use
	 * genphy_c45_read_status() here.
	 */
	phy_sts = phy_read(phydev, DP83TG720S_MII_REG_10);
	phydev->link = !!(phy_sts & DP83TG720S_LINK_STATUS);
	if (!phydev->link) {
		/* save the current stats before resetting the PHY */
		ret = dp83tg720_update_stats(phydev);
		if (ret)
			return ret;

		/* According to the "DP83TC81x, DP83TG72x Software
		 * Implementation Guide", the PHY needs to be reset after a
		 * link loss or if no link is created after at least 100ms.
		 *
		 * Currently we are polling with the PHY_STATE_TIME (1000ms)
		 * interval, which is still enough for not automotive use cases.
		 */
		ret = phy_init_hw(phydev);
		if (ret)
			return ret;

		/* Sleep 600ms for PHY stabilization post-reset.
		 * Empirically chosen value (not documented).
		 * Helps reduce reset bounces with link partners having similar
		 * issues.
		 */
		msleep(600);

		/* After HW reset we need to restore master/slave configuration.
		 * genphy_c45_pma_baset1_read_master_slave() call will be done
		 * by the dp83tg720_config_aneg() function.
		 */
		ret = dp83tg720_config_aneg(phydev);
		if (ret)
			return ret;

		phydev->speed = SPEED_UNKNOWN;
		phydev->duplex = DUPLEX_UNKNOWN;
	} else {
		/* PMA/PMD control 1 register (Register 1.0) is present, but it
		 * doesn't contain the link speed information.
		 * So genphy_c45_read_pma() can't be used here.
		 */
		ret = genphy_c45_pma_baset1_read_master_slave(phydev);
		if (ret)
			return ret;

		phydev->duplex = DUPLEX_FULL;
		phydev->speed = SPEED_1000;
	}

	return 0;
}

static int dp83tg720_get_sqi(struct phy_device *phydev)
{
	int ret;

	if (!phydev->link)
		return 0;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_SQI_REG_1);
	if (ret < 0)
		return ret;

	return FIELD_GET(DP83TG720S_SQI_OUT, ret);
}

static int dp83tg720_get_sqi_max(struct phy_device *phydev)
{
	return DP83TG720_SQI_MAX;
}

static int dp83tg720_config_rgmii_delay(struct phy_device *phydev)
{
	u16 rgmii_delay_mask;
	u16 rgmii_delay = 0;

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		rgmii_delay = 0;
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
		rgmii_delay = DP83TG720S_RGMII_RX_CLK_SEL |
				DP83TG720S_RGMII_TX_CLK_SEL;
		break;
	case PHY_INTERFACE_MODE_RGMII_RXID:
		rgmii_delay = DP83TG720S_RGMII_RX_CLK_SEL;
		break;
	case PHY_INTERFACE_MODE_RGMII_TXID:
		rgmii_delay = DP83TG720S_RGMII_TX_CLK_SEL;
		break;
	default:
		return 0;
	}

	rgmii_delay_mask = DP83TG720S_RGMII_RX_CLK_SEL |
		DP83TG720S_RGMII_TX_CLK_SEL;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND2,
			      DP83TG720S_RGMII_DELAY_CTRL, rgmii_delay_mask,
			      rgmii_delay);
}

static int dp83tg720_config_init(struct phy_device *phydev)
{
	int ret;

	/* Software Restart is not enough to recover from a link failure.
	 * Using Hardware Reset instead.
	 */
	ret = phy_write(phydev, DP83TG720S_PHY_RESET, DP83TG720S_HW_RESET);
	if (ret)
		return ret;

	/* Wait until MDC can be used again.
	 * The wait value of one 1ms is documented in "DP83TG720S-Q1 1000BASE-T1
	 * Automotive Ethernet PHY with SGMII and RGMII" datasheet.
	 */
	usleep_range(1000, 2000);

	if (phy_interface_is_rgmii(phydev)) {
		ret = dp83tg720_config_rgmii_delay(phydev);
		if (ret)
			return ret;
	}

	/* In case the PHY is bootstrapped in managed mode, we need to
	 * wake it.
	 */
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, DP83TG720S_LPS_CFG3,
			    DP83TG720S_LPS_CFG3_PWR_MODE_0);
	if (ret)
		return ret;

	/* Make role configuration visible for ethtool on init and after
	 * rest.
	 */
	return genphy_c45_pma_baset1_read_master_slave(phydev);
}

static int dp83tg720_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct dp83tg720_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	return 0;
}

/**
 * dp83tg720_get_next_update_time - Determine the next update time for PHY
 *                                  state
 * @phydev: Pointer to the phy_device structure
 *
 * This function addresses a limitation of the DP83TG720 PHY, which cannot
 * reliably detect or report a stable link state. To recover from such
 * scenarios, the PHY must be periodically reset when the link is down. However,
 * if the link partner also runs Linux with the same driver, synchronized reset
 * intervals can lead to a deadlock where the link never establishes due to
 * simultaneous resets on both sides.
 *
 * To avoid this, the function implements randomized polling intervals when the
 * link is down. It ensures that reset intervals are desynchronized by
 * introducing a random delay between a configured minimum and maximum range.
 * When the link is up, a fixed polling interval is used to minimize overhead.
 *
 * This mechanism guarantees that the link will reestablish within 10 seconds
 * in the worst-case scenario.
 *
 * Return: Time (in jiffies) until the next update event for the PHY state
 * machine.
 */
static unsigned int dp83tg720_get_next_update_time(struct phy_device *phydev)
{
	unsigned int next_time_jiffies;

	if (phydev->link) {
		/* When the link is up, use a fixed 1000ms interval
		 * (in jiffies)
		 */
		next_time_jiffies =
			msecs_to_jiffies(DP83TG720S_POLL_ACTIVE_LINK);
	} else {
		unsigned int min_jiffies, max_jiffies, rand_jiffies;

		/* When the link is down, randomize interval between min/max
		 * (in jiffies)
		 */
		min_jiffies = msecs_to_jiffies(DP83TG720S_POLL_NO_LINK_MIN);
		max_jiffies = msecs_to_jiffies(DP83TG720S_POLL_NO_LINK_MAX);

		rand_jiffies = min_jiffies +
			get_random_u32_below(max_jiffies - min_jiffies + 1);
		next_time_jiffies = rand_jiffies;
	}

	/* Ensure the polling time is at least one jiffy */
	return max(next_time_jiffies, 1U);
}

static struct phy_driver dp83tg720_driver[] = {
{
	PHY_ID_MATCH_MODEL(DP83TG720S_PHY_ID),
	.name		= "TI DP83TG720S",

	.flags          = PHY_POLL_CABLE_TEST,
	.probe		= dp83tg720_probe,
	.config_aneg	= dp83tg720_config_aneg,
	.read_status	= dp83tg720_read_status,
	.get_features	= genphy_c45_pma_read_ext_abilities,
	.config_init	= dp83tg720_config_init,
	.get_sqi	= dp83tg720_get_sqi,
	.get_sqi_max	= dp83tg720_get_sqi_max,
	.cable_test_start = dp83tg720_cable_test_start,
	.cable_test_get_status = dp83tg720_cable_test_get_status,
	.get_link_stats	= dp83tg720_get_link_stats,
	.get_phy_stats	= dp83tg720_get_phy_stats,
	.update_stats	= dp83tg720_update_stats,
	.get_next_update_time = dp83tg720_get_next_update_time,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
} };
module_phy_driver(dp83tg720_driver);

static const struct mdio_device_id __maybe_unused dp83tg720_tbl[] = {
	{ PHY_ID_MATCH_MODEL(DP83TG720S_PHY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(mdio, dp83tg720_tbl);

MODULE_DESCRIPTION("Texas Instruments DP83TG720S PHY driver");
MODULE_AUTHOR("Oleksij Rempel <kernel@pengutronix.de>");
MODULE_LICENSE("GPL");
