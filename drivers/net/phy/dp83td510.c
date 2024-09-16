// SPDX-License-Identifier: GPL-2.0
/* Driver for the Texas Instruments DP83TD510 PHY
 * Copyright (c) 2022 Pengutronix, Oleksij Rempel <kernel@pengutronix.de>
 */

#include <linux/bitfield.h>
#include <linux/ethtool_netlink.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>

#define DP83TD510E_PHY_ID			0x20000181

/* MDIO_MMD_VEND2 registers */
#define DP83TD510E_PHY_STS			0x10
/* Bit 7 - mii_interrupt, active high. Clears on read.
 * Note: Clearing does not necessarily deactivate IRQ pin if interrupts pending.
 * This differs from the DP83TD510E datasheet (2020) which states this bit
 * clears on write 0.
 */
#define DP83TD510E_STS_MII_INT			BIT(7)
#define DP83TD510E_LINK_STATUS			BIT(0)

#define DP83TD510E_GEN_CFG			0x11
#define DP83TD510E_GENCFG_INT_POLARITY		BIT(3)
#define DP83TD510E_GENCFG_INT_EN		BIT(1)
#define DP83TD510E_GENCFG_INT_OE		BIT(0)

#define DP83TD510E_INTERRUPT_REG_1		0x12
#define DP83TD510E_INT1_LINK			BIT(13)
#define DP83TD510E_INT1_LINK_EN			BIT(5)

#define DP83TD510E_CTRL				0x1f
#define DP83TD510E_CTRL_HW_RESET		BIT(15)
#define DP83TD510E_CTRL_SW_RESET		BIT(14)

#define DP83TD510E_AN_STAT_1			0x60c
#define DP83TD510E_MASTER_SLAVE_RESOL_FAIL	BIT(15)

#define DP83TD510E_MSE_DETECT			0xa85

#define DP83TD510_SQI_MAX	7

/* Register values are converted to SNR(dB) as suggested by
 * "Application Report - DP83TD510E Cable Diagnostics Toolkit":
 * SNR(dB) = -10 * log10 (VAL/2^17) - 1.76 dB.
 * SQI ranges are implemented according to "OPEN ALLIANCE - Advanced diagnostic
 * features for 100BASE-T1 automotive Ethernet PHYs"
 */
static const u16 dp83td510_mse_sqi_map[] = {
	0x0569, /* < 18dB */
	0x044c, /* 18dB =< SNR < 19dB */
	0x0369, /* 19dB =< SNR < 20dB */
	0x02b6, /* 20dB =< SNR < 21dB */
	0x0227, /* 21dB =< SNR < 22dB */
	0x01b6, /* 22dB =< SNR < 23dB */
	0x015b, /* 23dB =< SNR < 24dB */
	0x0000  /* 24dB =< SNR */
};

struct dp83td510_priv {
	bool alcd_test_active;
};

/* Time Domain Reflectometry (TDR) Functionality of DP83TD510 PHY
 *
 * I assume that this PHY is using a variation of Spread Spectrum Time Domain
 * Reflectometry (SSTDR) rather than the commonly used TDR found in many PHYs.
 * Here are the following observations which likely confirm this:
 * - The DP83TD510 PHY transmits a modulated signal of configurable length
 *   (default 16000 µs) instead of a single pulse pattern, which is typical
 *   for traditional TDR.
 * - The pulse observed on the wire, triggered by the HW RESET register, is not
 *   part of the cable testing process.
 *
 * I assume that SSTDR seems to be a logical choice for the 10BaseT1L
 * environment due to improved noise resistance, making it suitable for
 * environments  with significant electrical noise, such as long 10BaseT1L cable
 * runs.
 *
 * Configuration Variables:
 * The SSTDR variation used in this PHY involves more configuration variables
 * that can dramatically affect the functionality and precision of cable
 * testing. Since most of  these configuration options are either not well
 * documented or documented with minimal details, the following sections
 * describe my understanding and observations of these variables and their
 * impact on TDR functionality.
 *
 * Timeline:
 *     ,<--cfg_pre_silence_time
 *     |            ,<-SSTDR Modulated Transmission
 *     |	    |            ,<--cfg_post_silence_time
 *     |	    |            |             ,<--Force Link Mode
 * |<--'-->|<-------'------->|<--'-->|<--------'------->|
 *
 * - cfg_pre_silence_time: Optional silence time before TDR transmission starts.
 * - SSTDR Modulated Transmission: Transmission duration configured by
 *   cfg_tdr_tx_duration and amplitude configured by cfg_tdr_tx_type.
 * - cfg_post_silence_time: Silence time after TDR transmission.
 * - Force Link Mode: If nothing is configured after cfg_post_silence_time,
 *   the PHY continues in force link mode without autonegotiation.
 */

#define DP83TD510E_TDR_CFG				0x1e
#define DP83TD510E_TDR_START				BIT(15)
#define DP83TD510E_TDR_DONE				BIT(1)
#define DP83TD510E_TDR_FAIL				BIT(0)

#define DP83TD510E_TDR_CFG1				0x300
/* cfg_tdr_tx_type: Transmit voltage level for TDR.
 * 0 = 1V, 1 = 2.4V
 * Note: Using different voltage levels may not work
 * in all configuration variations. For example, setting
 * 2.4V may give different cable length measurements.
 * Other settings may be needed to make it work properly.
 */
#define DP83TD510E_TDR_TX_TYPE				BIT(12)
#define DP83TD510E_TDR_TX_TYPE_1V			0
#define DP83TD510E_TDR_TX_TYPE_2_4V			1
/* cfg_post_silence_time: Time after the TDR sequence. Since we force master mode
 * for the TDR will proceed with forced link state after this time. For Linux
 * it is better to set max value to avoid false link state detection.
 */
#define DP83TD510E_TDR_CFG1_POST_SILENCE_TIME		GENMASK(3, 2)
#define DP83TD510E_TDR_CFG1_POST_SILENCE_TIME_0MS	0
#define DP83TD510E_TDR_CFG1_POST_SILENCE_TIME_10MS	1
#define DP83TD510E_TDR_CFG1_POST_SILENCE_TIME_100MS	2
#define DP83TD510E_TDR_CFG1_POST_SILENCE_TIME_1000MS	3
/* cfg_pre_silence_time: Time before the TDR sequence. It should be enough to
 * settle down all pulses and reflections. Since for 10BASE-T1L we have
 * maximum 2000m cable length, we can set it to 1ms.
 */
#define DP83TD510E_TDR_CFG1_PRE_SILENCE_TIME		GENMASK(1, 0)
#define DP83TD510E_TDR_CFG1_PRE_SILENCE_TIME_0MS	0
#define DP83TD510E_TDR_CFG1_PRE_SILENCE_TIME_10MS	1
#define DP83TD510E_TDR_CFG1_PRE_SILENCE_TIME_100MS	2
#define DP83TD510E_TDR_CFG1_PRE_SILENCE_TIME_1000MS	3

#define DP83TD510E_TDR_CFG2				0x301
#define DP83TD510E_TDR_END_TAP_INDEX_1			GENMASK(14, 8)
#define DP83TD510E_TDR_END_TAP_INDEX_1_DEF		36
#define DP83TD510E_TDR_START_TAP_INDEX_1		GENMASK(6, 0)
#define DP83TD510E_TDR_START_TAP_INDEX_1_DEF		4

#define DP83TD510E_TDR_CFG3				0x302
/* cfg_tdr_tx_duration: Duration of the TDR transmission in microseconds.
 * This value sets the duration of the modulated signal used for TDR
 * measurements.
 * - Default: 16000 µs
 * - Observation: A minimum duration of 6000 µs is recommended to ensure
 *   accurate detection of cable faults. Durations shorter than 6000 µs may
 *   result in incomplete data, especially for shorter cables (e.g., 20 meters),
 *   leading to false "OK" results. Longer durations (e.g., 6000 µs or more)
 *   provide better accuracy, particularly for detecting open circuits.
 */
#define DP83TD510E_TDR_TX_DURATION_US			GENMASK(15, 0)
#define DP83TD510E_TDR_TX_DURATION_US_DEF		16000

#define DP83TD510E_TDR_FAULT_CFG1			0x303
#define DP83TD510E_TDR_FLT_LOC_OFFSET_1			GENMASK(14, 8)
#define DP83TD510E_TDR_FLT_LOC_OFFSET_1_DEF		4
#define DP83TD510E_TDR_FLT_INIT_1			GENMASK(7, 0)
#define DP83TD510E_TDR_FLT_INIT_1_DEF			62

#define DP83TD510E_TDR_FAULT_STAT			0x30c
#define DP83TD510E_TDR_PEAK_DETECT			BIT(11)
#define DP83TD510E_TDR_PEAK_SIGN			BIT(10)
#define DP83TD510E_TDR_PEAK_LOCATION			GENMASK(9, 0)

/* Not documented registers and values but recommended according to
 * "DP83TD510E Cable Diagnostics Toolkit revC"
 */
#define DP83TD510E_UNKN_030E				0x30e
#define DP83TD510E_030E_VAL				0x2520

#define DP83TD510E_ALCD_STAT				0xa9f
#define DP83TD510E_ALCD_COMPLETE			BIT(15)
#define DP83TD510E_ALCD_CABLE_LENGTH			GENMASK(10, 0)

static int dp83td510_config_intr(struct phy_device *phydev)
{
	int ret;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    DP83TD510E_INTERRUPT_REG_1,
				    DP83TD510E_INT1_LINK_EN);
		if (ret)
			return ret;

		ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2,
				       DP83TD510E_GEN_CFG,
				       DP83TD510E_GENCFG_INT_POLARITY |
				       DP83TD510E_GENCFG_INT_EN |
				       DP83TD510E_GENCFG_INT_OE);
	} else {
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    DP83TD510E_INTERRUPT_REG_1, 0x0);
		if (ret)
			return ret;

		ret = phy_clear_bits_mmd(phydev, MDIO_MMD_VEND2,
					 DP83TD510E_GEN_CFG,
					 DP83TD510E_GENCFG_INT_EN);
		if (ret)
			return ret;
	}

	return ret;
}

static irqreturn_t dp83td510_handle_interrupt(struct phy_device *phydev)
{
	int  ret;

	/* Read the current enabled interrupts */
	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TD510E_INTERRUPT_REG_1);
	if (ret < 0) {
		phy_error(phydev);
		return IRQ_NONE;
	} else if (!(ret & DP83TD510E_INT1_LINK_EN) ||
		   !(ret & DP83TD510E_INT1_LINK)) {
		return IRQ_NONE;
	}

	phy_trigger_machine(phydev);

	return IRQ_HANDLED;
}

static int dp83td510_read_status(struct phy_device *phydev)
{
	u16 phy_sts;
	int ret;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	phydev->pause = 0;
	phydev->asym_pause = 0;
	linkmode_zero(phydev->lp_advertising);

	phy_sts = phy_read(phydev, DP83TD510E_PHY_STS);

	phydev->link = !!(phy_sts & DP83TD510E_LINK_STATUS);
	if (phydev->link) {
		/* This PHY supports only one link mode: 10BaseT1L_Full */
		phydev->duplex = DUPLEX_FULL;
		phydev->speed = SPEED_10;

		if (phydev->autoneg == AUTONEG_ENABLE) {
			ret = genphy_c45_read_lpa(phydev);
			if (ret)
				return ret;

			phy_resolve_aneg_linkmode(phydev);
		}
	}

	if (phydev->autoneg == AUTONEG_ENABLE) {
		ret = genphy_c45_baset1_read_status(phydev);
		if (ret < 0)
			return ret;

		ret = phy_read_mmd(phydev, MDIO_MMD_VEND2,
				   DP83TD510E_AN_STAT_1);
		if (ret < 0)
			return ret;

		if (ret & DP83TD510E_MASTER_SLAVE_RESOL_FAIL)
			phydev->master_slave_state = MASTER_SLAVE_STATE_ERR;
	} else {
		return genphy_c45_pma_baset1_read_master_slave(phydev);
	}

	return 0;
}

static int dp83td510_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	int ret;

	ret = genphy_c45_pma_baset1_setup_master_slave(phydev);
	if (ret < 0)
		return ret;

	if (phydev->autoneg == AUTONEG_DISABLE)
		return genphy_c45_an_disable_aneg(phydev);

	ret = genphy_c45_an_config_aneg(phydev);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	return genphy_c45_check_and_restart_aneg(phydev, changed);
}

static int dp83td510_get_sqi(struct phy_device *phydev)
{
	int sqi, ret;
	u16 mse_val;

	if (!phydev->link)
		return 0;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TD510E_MSE_DETECT);
	if (ret < 0)
		return ret;

	mse_val = 0xFFFF & ret;
	for (sqi = 0; sqi < ARRAY_SIZE(dp83td510_mse_sqi_map); sqi++) {
		if (mse_val >= dp83td510_mse_sqi_map[sqi])
			return sqi;
	}

	return -EINVAL;
}

static int dp83td510_get_sqi_max(struct phy_device *phydev)
{
	return DP83TD510_SQI_MAX;
}

/**
 * dp83td510_cable_test_start - Start the cable test for the DP83TD510 PHY.
 * @phydev: Pointer to the phy_device structure.
 *
 * This sequence is implemented according to the "Application Note DP83TD510E
 * Cable Diagnostics Toolkit revC".
 *
 * Returns: 0 on success, a negative error code on failure.
 */
static int dp83td510_cable_test_start(struct phy_device *phydev)
{
	struct dp83td510_priv *priv = phydev->priv;
	int ret;

	/* If link partner is active, we won't be able to use TDR, since
	 * we can't force link partner to be silent. The autonegotiation
	 * pulses will be too frequent and the TDR sequence will be
	 * too long. So, TDR will always fail. Since the link is established
	 * we already know that the cable is working, so we can get some
	 * extra information line the cable length using ALCD.
	 */
	if (phydev->link) {
		priv->alcd_test_active = true;
		return 0;
	}

	priv->alcd_test_active = false;

	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2, DP83TD510E_CTRL,
			       DP83TD510E_CTRL_HW_RESET);
	if (ret)
		return ret;

	ret = genphy_c45_an_disable_aneg(phydev);
	if (ret)
		return ret;

	/* Force master mode */
	ret = phy_set_bits_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_PMD_BT1_CTRL,
			       MDIO_PMA_PMD_BT1_CTRL_CFG_MST);
	if (ret)
		return ret;

	/* There is no official recommendation for this register, but it is
	 * better to use 1V for TDR since other values seems to be optimized
	 * for this amplitude. Except of amplitude, it is better to configure
	 * pre TDR silence time to 10ms to avoid false reflections (value 0
	 * seems to be too short, otherwise we need to implement own silence
	 * time). Also, post TDR silence time should be set to 1000ms to avoid
	 * false link state detection, it fits to the polling time of the
	 * PHY framework. The idea is to wait until
	 * dp83td510_cable_test_get_status() will be called and reconfigure
	 * the PHY to the default state within the post silence time window.
	 */
	ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2, DP83TD510E_TDR_CFG1,
			     DP83TD510E_TDR_TX_TYPE |
			     DP83TD510E_TDR_CFG1_POST_SILENCE_TIME |
			     DP83TD510E_TDR_CFG1_PRE_SILENCE_TIME,
			     DP83TD510E_TDR_TX_TYPE_1V |
			     DP83TD510E_TDR_CFG1_PRE_SILENCE_TIME_10MS |
			     DP83TD510E_TDR_CFG1_POST_SILENCE_TIME_1000MS);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, DP83TD510E_TDR_CFG2,
			    FIELD_PREP(DP83TD510E_TDR_END_TAP_INDEX_1,
				       DP83TD510E_TDR_END_TAP_INDEX_1_DEF) |
			    FIELD_PREP(DP83TD510E_TDR_START_TAP_INDEX_1,
				       DP83TD510E_TDR_START_TAP_INDEX_1_DEF));
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, DP83TD510E_TDR_FAULT_CFG1,
			    FIELD_PREP(DP83TD510E_TDR_FLT_LOC_OFFSET_1,
				       DP83TD510E_TDR_FLT_LOC_OFFSET_1_DEF) |
			    FIELD_PREP(DP83TD510E_TDR_FLT_INIT_1,
				       DP83TD510E_TDR_FLT_INIT_1_DEF));
	if (ret)
		return ret;

	/* Undocumented register, from the "Application Note DP83TD510E Cable
	 * Diagnostics Toolkit revC".
	 */
	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, DP83TD510E_UNKN_030E,
			    DP83TD510E_030E_VAL);
	if (ret)
		return ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, DP83TD510E_TDR_CFG3,
			    DP83TD510E_TDR_TX_DURATION_US_DEF);
	if (ret)
		return ret;

	ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2, DP83TD510E_CTRL,
			       DP83TD510E_CTRL_SW_RESET);
	if (ret)
		return ret;

	return phy_set_bits_mmd(phydev, MDIO_MMD_VEND2, DP83TD510E_TDR_CFG,
				DP83TD510E_TDR_START);
}

/**
 * dp83td510_cable_test_get_tdr_status - Get the status of the TDR test for the
 *                                       DP83TD510 PHY.
 * @phydev: Pointer to the phy_device structure.
 * @finished: Pointer to a boolean that indicates whether the test is finished.
 *
 * The function sets the @finished flag to true if the test is complete.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
static int dp83td510_cable_test_get_tdr_status(struct phy_device *phydev,
					       bool *finished)
{
	int ret, stat;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TD510E_TDR_CFG);
	if (ret < 0)
		return ret;

	if (!(ret & DP83TD510E_TDR_DONE))
		return 0;

	if (!(ret & DP83TD510E_TDR_FAIL)) {
		int location;

		ret = phy_read_mmd(phydev, MDIO_MMD_VEND2,
				   DP83TD510E_TDR_FAULT_STAT);
		if (ret < 0)
			return ret;

		if (ret & DP83TD510E_TDR_PEAK_DETECT) {
			if (ret & DP83TD510E_TDR_PEAK_SIGN)
				stat = ETHTOOL_A_CABLE_RESULT_CODE_OPEN;
			else
				stat = ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT;

			location = FIELD_GET(DP83TD510E_TDR_PEAK_LOCATION,
					     ret) * 100;
			ethnl_cable_test_fault_length(phydev,
						      ETHTOOL_A_CABLE_PAIR_A,
						      location);
		} else {
			stat = ETHTOOL_A_CABLE_RESULT_CODE_OK;
		}
	} else {
		/* Most probably we have active link partner */
		stat = ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC;
	}

	*finished = true;

	ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A, stat);

	return phy_init_hw(phydev);
}

/**
 * dp83td510_cable_test_get_alcd_status - Get the status of the ALCD test for the
 *                                        DP83TD510 PHY.
 * @phydev: Pointer to the phy_device structure.
 * @finished: Pointer to a boolean that indicates whether the test is finished.
 *
 * The function sets the @finished flag to true if the test is complete.
 * The function reads the cable length and reports it to the user.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
static int dp83td510_cable_test_get_alcd_status(struct phy_device *phydev,
						bool *finished)
{
	unsigned int location;
	int ret, phy_sts;

	phy_sts = phy_read(phydev, DP83TD510E_PHY_STS);

	if (!(phy_sts & DP83TD510E_LINK_STATUS)) {
		/* If the link is down, we can't do any thing usable now */
		ethnl_cable_test_result_with_src(phydev, ETHTOOL_A_CABLE_PAIR_A,
						 ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC,
						 ETHTOOL_A_CABLE_INF_SRC_ALCD);
		*finished = true;
		return 0;
	}

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, DP83TD510E_ALCD_STAT);
	if (ret < 0)
		return ret;

	if (!(ret & DP83TD510E_ALCD_COMPLETE))
		return 0;

	location = FIELD_GET(DP83TD510E_ALCD_CABLE_LENGTH, ret) * 100;

	ethnl_cable_test_fault_length_with_src(phydev, ETHTOOL_A_CABLE_PAIR_A,
					       location,
					       ETHTOOL_A_CABLE_INF_SRC_ALCD);

	ethnl_cable_test_result_with_src(phydev, ETHTOOL_A_CABLE_PAIR_A,
					 ETHTOOL_A_CABLE_RESULT_CODE_OK,
					 ETHTOOL_A_CABLE_INF_SRC_ALCD);
	*finished = true;

	return 0;
}

/**
 * dp83td510_cable_test_get_status - Get the status of the cable test for the
 *                                   DP83TD510 PHY.
 * @phydev: Pointer to the phy_device structure.
 * @finished: Pointer to a boolean that indicates whether the test is finished.
 *
 * The function sets the @finished flag to true if the test is complete.
 *
 * Returns: 0 on success or a negative error code on failure.
 */
static int dp83td510_cable_test_get_status(struct phy_device *phydev,
					   bool *finished)
{
	struct dp83td510_priv *priv = phydev->priv;
	*finished = false;

	if (priv->alcd_test_active)
		return dp83td510_cable_test_get_alcd_status(phydev, finished);

	return dp83td510_cable_test_get_tdr_status(phydev, finished);
}

static int dp83td510_get_features(struct phy_device *phydev)
{
	/* This PHY can't respond on MDIO bus if no RMII clock is enabled.
	 * In case RMII mode is used (most meaningful mode for this PHY) and
	 * the PHY do not have own XTAL, and CLK providing MAC is not probed,
	 * we won't be able to read all needed ability registers.
	 * So provide it manually.
	 */

	linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, phydev->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, phydev->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_Pause_BIT, phydev->supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_10baseT1L_Full_BIT,
			 phydev->supported);

	return 0;
}

static int dp83td510_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct dp83td510_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	return 0;
}

static struct phy_driver dp83td510_driver[] = {
{
	PHY_ID_MATCH_MODEL(DP83TD510E_PHY_ID),
	.name		= "TI DP83TD510E",

	.flags          = PHY_POLL_CABLE_TEST,
	.probe		= dp83td510_probe,
	.config_aneg	= dp83td510_config_aneg,
	.read_status	= dp83td510_read_status,
	.get_features	= dp83td510_get_features,
	.config_intr	= dp83td510_config_intr,
	.handle_interrupt = dp83td510_handle_interrupt,
	.get_sqi	= dp83td510_get_sqi,
	.get_sqi_max	= dp83td510_get_sqi_max,
	.cable_test_start = dp83td510_cable_test_start,
	.cable_test_get_status = dp83td510_cable_test_get_status,

	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
} };
module_phy_driver(dp83td510_driver);

static struct mdio_device_id __maybe_unused dp83td510_tbl[] = {
	{ PHY_ID_MATCH_MODEL(DP83TD510E_PHY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(mdio, dp83td510_tbl);

MODULE_DESCRIPTION("Texas Instruments DP83TD510E PHY driver");
MODULE_AUTHOR("Oleksij Rempel <kernel@pengutronix.de>");
MODULE_LICENSE("GPL v2");
