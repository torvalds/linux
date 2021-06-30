// SPDX-License-Identifier: GPL-2.0
/* NXP C45 PHY driver
 * Copyright (C) 2021 NXP
 * Author: Radu Pirea <radu-nicolae.pirea@oss.nxp.com>
 */

#include <linux/delay.h>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>
#include <linux/kernel.h>
#include <linux/mii.h>
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/processor.h>
#include <linux/property.h>

#define PHY_ID_TJA_1103			0x001BB010

#define PMAPMD_B100T1_PMAPMD_CTL	0x0834
#define B100T1_PMAPMD_CONFIG_EN		BIT(15)
#define B100T1_PMAPMD_MASTER		BIT(14)
#define MASTER_MODE			(B100T1_PMAPMD_CONFIG_EN | \
					 B100T1_PMAPMD_MASTER)
#define SLAVE_MODE			(B100T1_PMAPMD_CONFIG_EN)

#define VEND1_DEVICE_CONTROL		0x0040
#define DEVICE_CONTROL_RESET		BIT(15)
#define DEVICE_CONTROL_CONFIG_GLOBAL_EN	BIT(14)
#define DEVICE_CONTROL_CONFIG_ALL_EN	BIT(13)

#define VEND1_PHY_IRQ_ACK		0x80A0
#define VEND1_PHY_IRQ_EN		0x80A1
#define VEND1_PHY_IRQ_STATUS		0x80A2
#define PHY_IRQ_LINK_EVENT		BIT(1)

#define VEND1_PHY_CONTROL		0x8100
#define PHY_CONFIG_EN			BIT(14)
#define PHY_START_OP			BIT(0)

#define VEND1_PHY_CONFIG		0x8108
#define PHY_CONFIG_AUTO			BIT(0)

#define VEND1_SIGNAL_QUALITY		0x8320
#define SQI_VALID			BIT(14)
#define SQI_MASK			GENMASK(2, 0)
#define MAX_SQI				SQI_MASK

#define VEND1_CABLE_TEST		0x8330
#define CABLE_TEST_ENABLE		BIT(15)
#define CABLE_TEST_START		BIT(14)
#define CABLE_TEST_VALID		BIT(13)
#define CABLE_TEST_OK			0x00
#define CABLE_TEST_SHORTED		0x01
#define CABLE_TEST_OPEN			0x02
#define CABLE_TEST_UNKNOWN		0x07

#define VEND1_PORT_CONTROL		0x8040
#define PORT_CONTROL_EN			BIT(14)

#define VEND1_PORT_INFRA_CONTROL	0xAC00
#define PORT_INFRA_CONTROL_EN		BIT(14)

#define VEND1_RXID			0xAFCC
#define VEND1_TXID			0xAFCD
#define ID_ENABLE			BIT(15)

#define VEND1_ABILITIES			0xAFC4
#define RGMII_ID_ABILITY		BIT(15)
#define RGMII_ABILITY			BIT(14)
#define RMII_ABILITY			BIT(10)
#define REVMII_ABILITY			BIT(9)
#define MII_ABILITY			BIT(8)
#define SGMII_ABILITY			BIT(0)

#define VEND1_MII_BASIC_CONFIG		0xAFC6
#define MII_BASIC_CONFIG_REV		BIT(8)
#define MII_BASIC_CONFIG_SGMII		0x9
#define MII_BASIC_CONFIG_RGMII		0x7
#define MII_BASIC_CONFIG_RMII		0x5
#define MII_BASIC_CONFIG_MII		0x4

#define VEND1_SYMBOL_ERROR_COUNTER	0x8350
#define VEND1_LINK_DROP_COUNTER		0x8352
#define VEND1_LINK_LOSSES_AND_FAILURES	0x8353
#define VEND1_R_GOOD_FRAME_CNT		0xA950
#define VEND1_R_BAD_FRAME_CNT		0xA952
#define VEND1_R_RXER_FRAME_CNT		0xA954
#define VEND1_RX_PREAMBLE_COUNT		0xAFCE
#define VEND1_TX_PREAMBLE_COUNT		0xAFCF
#define VEND1_RX_IPG_LENGTH		0xAFD0
#define VEND1_TX_IPG_LENGTH		0xAFD1
#define COUNTER_EN			BIT(15)

#define RGMII_PERIOD_PS			8000U
#define PS_PER_DEGREE			div_u64(RGMII_PERIOD_PS, 360)
#define MIN_ID_PS			1644U
#define MAX_ID_PS			2260U
#define DEFAULT_ID_PS			2000U

struct nxp_c45_phy {
	u32 tx_delay;
	u32 rx_delay;
};

struct nxp_c45_phy_stats {
	const char	*name;
	u8		mmd;
	u16		reg;
	u8		off;
	u16		mask;
};

static const struct nxp_c45_phy_stats nxp_c45_hw_stats[] = {
	{ "phy_symbol_error_cnt", MDIO_MMD_VEND1,
		VEND1_SYMBOL_ERROR_COUNTER, 0, GENMASK(15, 0) },
	{ "phy_link_status_drop_cnt", MDIO_MMD_VEND1,
		VEND1_LINK_DROP_COUNTER, 8, GENMASK(13, 8) },
	{ "phy_link_availability_drop_cnt", MDIO_MMD_VEND1,
		VEND1_LINK_DROP_COUNTER, 0, GENMASK(5, 0) },
	{ "phy_link_loss_cnt", MDIO_MMD_VEND1,
		VEND1_LINK_LOSSES_AND_FAILURES, 10, GENMASK(15, 10) },
	{ "phy_link_failure_cnt", MDIO_MMD_VEND1,
		VEND1_LINK_LOSSES_AND_FAILURES, 0, GENMASK(9, 0) },
	{ "r_good_frame_cnt", MDIO_MMD_VEND1,
		VEND1_R_GOOD_FRAME_CNT, 0, GENMASK(15, 0) },
	{ "r_bad_frame_cnt", MDIO_MMD_VEND1,
		VEND1_R_BAD_FRAME_CNT, 0, GENMASK(15, 0) },
	{ "r_rxer_frame_cnt", MDIO_MMD_VEND1,
		VEND1_R_RXER_FRAME_CNT, 0, GENMASK(15, 0) },
	{ "rx_preamble_count", MDIO_MMD_VEND1,
		VEND1_RX_PREAMBLE_COUNT, 0, GENMASK(5, 0) },
	{ "tx_preamble_count", MDIO_MMD_VEND1,
		VEND1_TX_PREAMBLE_COUNT, 0, GENMASK(5, 0) },
	{ "rx_ipg_length", MDIO_MMD_VEND1,
		VEND1_RX_IPG_LENGTH, 0, GENMASK(8, 0) },
	{ "tx_ipg_length", MDIO_MMD_VEND1,
		VEND1_TX_IPG_LENGTH, 0, GENMASK(8, 0) },
};

static int nxp_c45_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(nxp_c45_hw_stats);
}

static void nxp_c45_get_strings(struct phy_device *phydev, u8 *data)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(nxp_c45_hw_stats); i++) {
		strncpy(data + i * ETH_GSTRING_LEN,
			nxp_c45_hw_stats[i].name, ETH_GSTRING_LEN);
	}
}

static void nxp_c45_get_stats(struct phy_device *phydev,
			      struct ethtool_stats *stats, u64 *data)
{
	size_t i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(nxp_c45_hw_stats); i++) {
		ret = phy_read_mmd(phydev, nxp_c45_hw_stats[i].mmd,
				   nxp_c45_hw_stats[i].reg);
		if (ret < 0) {
			data[i] = U64_MAX;
		} else {
			data[i] = ret & nxp_c45_hw_stats[i].mask;
			data[i] >>= nxp_c45_hw_stats[i].off;
		}
	}
}

static int nxp_c45_config_enable(struct phy_device *phydev)
{
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_DEVICE_CONTROL,
		      DEVICE_CONTROL_CONFIG_GLOBAL_EN |
		      DEVICE_CONTROL_CONFIG_ALL_EN);
	usleep_range(400, 450);

	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_PORT_CONTROL,
		      PORT_CONTROL_EN);
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_PHY_CONTROL,
		      PHY_CONFIG_EN);
	phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_PORT_INFRA_CONTROL,
		      PORT_INFRA_CONTROL_EN);

	return 0;
}

static int nxp_c45_start_op(struct phy_device *phydev)
{
	return phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PHY_CONTROL,
				PHY_START_OP);
}

static int nxp_c45_config_intr(struct phy_device *phydev)
{
	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		return phy_set_bits_mmd(phydev, MDIO_MMD_VEND1,
					VEND1_PHY_IRQ_EN, PHY_IRQ_LINK_EVENT);
	else
		return phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1,
					  VEND1_PHY_IRQ_EN, PHY_IRQ_LINK_EVENT);
}

static irqreturn_t nxp_c45_handle_interrupt(struct phy_device *phydev)
{
	irqreturn_t ret = IRQ_NONE;
	int irq;

	irq = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_PHY_IRQ_STATUS);
	if (irq & PHY_IRQ_LINK_EVENT) {
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_PHY_IRQ_ACK,
			      PHY_IRQ_LINK_EVENT);
		phy_trigger_machine(phydev);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static int nxp_c45_soft_reset(struct phy_device *phydev)
{
	int ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_DEVICE_CONTROL,
			    DEVICE_CONTROL_RESET);
	if (ret)
		return ret;

	return phy_read_mmd_poll_timeout(phydev, MDIO_MMD_VEND1,
					 VEND1_DEVICE_CONTROL, ret,
					 !(ret & DEVICE_CONTROL_RESET), 20000,
					 240000, false);
}

static int nxp_c45_cable_test_start(struct phy_device *phydev)
{
	return phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_CABLE_TEST,
			     CABLE_TEST_ENABLE | CABLE_TEST_START);
}

static int nxp_c45_cable_test_get_status(struct phy_device *phydev,
					 bool *finished)
{
	int ret;
	u8 cable_test_result;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_CABLE_TEST);
	if (!(ret & CABLE_TEST_VALID)) {
		*finished = false;
		return 0;
	}

	*finished = true;
	cable_test_result = ret & GENMASK(2, 0);

	switch (cable_test_result) {
	case CABLE_TEST_OK:
		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
					ETHTOOL_A_CABLE_RESULT_CODE_OK);
		break;
	case CABLE_TEST_SHORTED:
		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
					ETHTOOL_A_CABLE_RESULT_CODE_SAME_SHORT);
		break;
	case CABLE_TEST_OPEN:
		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
					ETHTOOL_A_CABLE_RESULT_CODE_OPEN);
		break;
	default:
		ethnl_cable_test_result(phydev, ETHTOOL_A_CABLE_PAIR_A,
					ETHTOOL_A_CABLE_RESULT_CODE_UNSPEC);
	}

	phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_CABLE_TEST,
			   CABLE_TEST_ENABLE);

	return nxp_c45_start_op(phydev);
}

static int nxp_c45_setup_master_slave(struct phy_device *phydev)
{
	switch (phydev->master_slave_set) {
	case MASTER_SLAVE_CFG_MASTER_FORCE:
	case MASTER_SLAVE_CFG_MASTER_PREFERRED:
		phy_write_mmd(phydev, MDIO_MMD_PMAPMD, PMAPMD_B100T1_PMAPMD_CTL,
			      MASTER_MODE);
		break;
	case MASTER_SLAVE_CFG_SLAVE_PREFERRED:
	case MASTER_SLAVE_CFG_SLAVE_FORCE:
		phy_write_mmd(phydev, MDIO_MMD_PMAPMD, PMAPMD_B100T1_PMAPMD_CTL,
			      SLAVE_MODE);
		break;
	case MASTER_SLAVE_CFG_UNKNOWN:
	case MASTER_SLAVE_CFG_UNSUPPORTED:
		return 0;
	default:
		phydev_warn(phydev, "Unsupported Master/Slave mode\n");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int nxp_c45_read_master_slave(struct phy_device *phydev)
{
	int reg;

	phydev->master_slave_get = MASTER_SLAVE_CFG_UNKNOWN;
	phydev->master_slave_state = MASTER_SLAVE_STATE_UNKNOWN;

	reg = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, PMAPMD_B100T1_PMAPMD_CTL);
	if (reg < 0)
		return reg;

	if (reg & B100T1_PMAPMD_MASTER) {
		phydev->master_slave_get = MASTER_SLAVE_CFG_MASTER_FORCE;
		phydev->master_slave_state = MASTER_SLAVE_STATE_MASTER;
	} else {
		phydev->master_slave_get = MASTER_SLAVE_CFG_SLAVE_FORCE;
		phydev->master_slave_state = MASTER_SLAVE_STATE_SLAVE;
	}

	return 0;
}

static int nxp_c45_config_aneg(struct phy_device *phydev)
{
	return nxp_c45_setup_master_slave(phydev);
}

static int nxp_c45_read_status(struct phy_device *phydev)
{
	int ret;

	ret = genphy_c45_read_status(phydev);
	if (ret)
		return ret;

	ret = nxp_c45_read_master_slave(phydev);
	if (ret)
		return ret;

	return 0;
}

static int nxp_c45_get_sqi(struct phy_device *phydev)
{
	int reg;

	reg = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_SIGNAL_QUALITY);
	if (!(reg & SQI_VALID))
		return -EINVAL;

	reg &= SQI_MASK;

	return reg;
}

static int nxp_c45_get_sqi_max(struct phy_device *phydev)
{
	return MAX_SQI;
}

static int nxp_c45_check_delay(struct phy_device *phydev, u32 delay)
{
	if (delay < MIN_ID_PS) {
		phydev_err(phydev, "delay value smaller than %u\n", MIN_ID_PS);
		return -EINVAL;
	}

	if (delay > MAX_ID_PS) {
		phydev_err(phydev, "delay value higher than %u\n", MAX_ID_PS);
		return -EINVAL;
	}

	return 0;
}

static u64 nxp_c45_get_phase_shift(u64 phase_offset_raw)
{
	/* The delay in degree phase is 73.8 + phase_offset_raw * 0.9.
	 * To avoid floating point operations we'll multiply by 10
	 * and get 1 decimal point precision.
	 */
	phase_offset_raw *= 10;
	phase_offset_raw -= 738;
	return div_u64(phase_offset_raw, 9);
}

static void nxp_c45_disable_delays(struct phy_device *phydev)
{
	phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_TXID, ID_ENABLE);
	phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_RXID, ID_ENABLE);
}

static void nxp_c45_set_delays(struct phy_device *phydev)
{
	struct nxp_c45_phy *priv = phydev->priv;
	u64 tx_delay = priv->tx_delay;
	u64 rx_delay = priv->rx_delay;
	u64 degree;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		degree = div_u64(tx_delay, PS_PER_DEGREE);
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_TXID,
			      ID_ENABLE | nxp_c45_get_phase_shift(degree));
	} else {
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_TXID,
				   ID_ENABLE);
	}

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) {
		degree = div_u64(rx_delay, PS_PER_DEGREE);
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_RXID,
			      ID_ENABLE | nxp_c45_get_phase_shift(degree));
	} else {
		phy_clear_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_RXID,
				   ID_ENABLE);
	}
}

static int nxp_c45_get_delays(struct phy_device *phydev)
{
	struct nxp_c45_phy *priv = phydev->priv;
	int ret;

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID) {
		ret = device_property_read_u32(&phydev->mdio.dev,
					       "tx-internal-delay-ps",
					       &priv->tx_delay);
		if (ret)
			priv->tx_delay = DEFAULT_ID_PS;

		ret = nxp_c45_check_delay(phydev, priv->tx_delay);
		if (ret) {
			phydev_err(phydev,
				   "tx-internal-delay-ps invalid value\n");
			return ret;
		}
	}

	if (phydev->interface == PHY_INTERFACE_MODE_RGMII_ID ||
	    phydev->interface == PHY_INTERFACE_MODE_RGMII_RXID) {
		ret = device_property_read_u32(&phydev->mdio.dev,
					       "rx-internal-delay-ps",
					       &priv->rx_delay);
		if (ret)
			priv->rx_delay = DEFAULT_ID_PS;

		ret = nxp_c45_check_delay(phydev, priv->rx_delay);
		if (ret) {
			phydev_err(phydev,
				   "rx-internal-delay-ps invalid value\n");
			return ret;
		}
	}

	return 0;
}

static int nxp_c45_set_phy_mode(struct phy_device *phydev)
{
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND1, VEND1_ABILITIES);
	phydev_dbg(phydev, "Clause 45 managed PHY abilities 0x%x\n", ret);

	switch (phydev->interface) {
	case PHY_INTERFACE_MODE_RGMII:
		if (!(ret & RGMII_ABILITY)) {
			phydev_err(phydev, "rgmii mode not supported\n");
			return -EINVAL;
		}
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_MII_BASIC_CONFIG,
			      MII_BASIC_CONFIG_RGMII);
		nxp_c45_disable_delays(phydev);
		break;
	case PHY_INTERFACE_MODE_RGMII_ID:
	case PHY_INTERFACE_MODE_RGMII_TXID:
	case PHY_INTERFACE_MODE_RGMII_RXID:
		if (!(ret & RGMII_ID_ABILITY)) {
			phydev_err(phydev, "rgmii-id, rgmii-txid, rgmii-rxid modes are not supported\n");
			return -EINVAL;
		}
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_MII_BASIC_CONFIG,
			      MII_BASIC_CONFIG_RGMII);
		ret = nxp_c45_get_delays(phydev);
		if (ret)
			return ret;

		nxp_c45_set_delays(phydev);
		break;
	case PHY_INTERFACE_MODE_MII:
		if (!(ret & MII_ABILITY)) {
			phydev_err(phydev, "mii mode not supported\n");
			return -EINVAL;
		}
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_MII_BASIC_CONFIG,
			      MII_BASIC_CONFIG_MII);
		break;
	case PHY_INTERFACE_MODE_REVMII:
		if (!(ret & REVMII_ABILITY)) {
			phydev_err(phydev, "rev-mii mode not supported\n");
			return -EINVAL;
		}
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_MII_BASIC_CONFIG,
			      MII_BASIC_CONFIG_MII | MII_BASIC_CONFIG_REV);
		break;
	case PHY_INTERFACE_MODE_RMII:
		if (!(ret & RMII_ABILITY)) {
			phydev_err(phydev, "rmii mode not supported\n");
			return -EINVAL;
		}
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_MII_BASIC_CONFIG,
			      MII_BASIC_CONFIG_RMII);
		break;
	case PHY_INTERFACE_MODE_SGMII:
		if (!(ret & SGMII_ABILITY)) {
			phydev_err(phydev, "sgmii mode not supported\n");
			return -EINVAL;
		}
		phy_write_mmd(phydev, MDIO_MMD_VEND1, VEND1_MII_BASIC_CONFIG,
			      MII_BASIC_CONFIG_SGMII);
		break;
	case PHY_INTERFACE_MODE_INTERNAL:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int nxp_c45_config_init(struct phy_device *phydev)
{
	int ret;

	ret = nxp_c45_config_enable(phydev);
	if (ret) {
		phydev_err(phydev, "Failed to enable config\n");
		return ret;
	}

	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_PHY_CONFIG,
			 PHY_CONFIG_AUTO);

	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_LINK_DROP_COUNTER,
			 COUNTER_EN);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_RX_PREAMBLE_COUNT,
			 COUNTER_EN);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_TX_PREAMBLE_COUNT,
			 COUNTER_EN);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_RX_IPG_LENGTH,
			 COUNTER_EN);
	phy_set_bits_mmd(phydev, MDIO_MMD_VEND1, VEND1_TX_IPG_LENGTH,
			 COUNTER_EN);

	ret = nxp_c45_set_phy_mode(phydev);
	if (ret)
		return ret;

	phydev->autoneg = AUTONEG_DISABLE;

	return nxp_c45_start_op(phydev);
}

static int nxp_c45_probe(struct phy_device *phydev)
{
	struct nxp_c45_phy *priv;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	return 0;
}

static struct phy_driver nxp_c45_driver[] = {
	{
		PHY_ID_MATCH_MODEL(PHY_ID_TJA_1103),
		.name			= "NXP C45 TJA1103",
		.features		= PHY_BASIC_T1_FEATURES,
		.probe			= nxp_c45_probe,
		.soft_reset		= nxp_c45_soft_reset,
		.config_aneg		= nxp_c45_config_aneg,
		.config_init		= nxp_c45_config_init,
		.config_intr		= nxp_c45_config_intr,
		.handle_interrupt	= nxp_c45_handle_interrupt,
		.read_status		= nxp_c45_read_status,
		.suspend		= genphy_c45_pma_suspend,
		.resume			= genphy_c45_pma_resume,
		.get_sset_count		= nxp_c45_get_sset_count,
		.get_strings		= nxp_c45_get_strings,
		.get_stats		= nxp_c45_get_stats,
		.cable_test_start	= nxp_c45_cable_test_start,
		.cable_test_get_status	= nxp_c45_cable_test_get_status,
		.set_loopback		= genphy_c45_loopback,
		.get_sqi		= nxp_c45_get_sqi,
		.get_sqi_max		= nxp_c45_get_sqi_max,
	},
};

module_phy_driver(nxp_c45_driver);

static struct mdio_device_id __maybe_unused nxp_c45_tbl[] = {
	{ PHY_ID_MATCH_MODEL(PHY_ID_TJA_1103) },
	{ /*sentinel*/ },
};

MODULE_DEVICE_TABLE(mdio, nxp_c45_tbl);

MODULE_AUTHOR("Radu Pirea <radu-nicolae.pirea@oss.nxp.com>");
MODULE_DESCRIPTION("NXP C45 PHY driver");
MODULE_LICENSE("GPL v2");
