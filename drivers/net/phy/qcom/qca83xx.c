// SPDX-License-Identifier: GPL-2.0+

#include <linux/phy.h>
#include <linux/module.h>

#include "qcom.h"

#define AT803X_DEBUG_REG_3C			0x3C

#define AT803X_DEBUG_REG_GREEN			0x3D
#define   AT803X_DEBUG_GATE_CLK_IN1000		BIT(6)

#define MDIO_AZ_DEBUG				0x800D

#define QCA8327_A_PHY_ID			0x004dd033
#define QCA8327_B_PHY_ID			0x004dd034
#define QCA8337_PHY_ID				0x004dd036

#define QCA8K_DEVFLAGS_REVISION_MASK		GENMASK(2, 0)

static struct at803x_hw_stat qca83xx_hw_stats[] = {
	{ "phy_idle_errors", 0xa, GENMASK(7, 0), PHY},
	{ "phy_receive_errors", 0x15, GENMASK(15, 0), PHY},
	{ "eee_wake_errors", 0x16, GENMASK(15, 0), MMD},
};

struct qca83xx_priv {
	u64 stats[ARRAY_SIZE(qca83xx_hw_stats)];
};

MODULE_DESCRIPTION("Qualcomm Atheros QCA83XX PHY driver");
MODULE_AUTHOR("Matus Ujhelyi");
MODULE_AUTHOR("Christian Marangi <ansuelsmth@gmail.com>");
MODULE_LICENSE("GPL");

static int qca83xx_get_sset_count(struct phy_device *phydev)
{
	return ARRAY_SIZE(qca83xx_hw_stats);
}

static void qca83xx_get_strings(struct phy_device *phydev, u8 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qca83xx_hw_stats); i++) {
		strscpy(data + i * ETH_GSTRING_LEN,
			qca83xx_hw_stats[i].string, ETH_GSTRING_LEN);
	}
}

static u64 qca83xx_get_stat(struct phy_device *phydev, int i)
{
	struct at803x_hw_stat stat = qca83xx_hw_stats[i];
	struct qca83xx_priv *priv = phydev->priv;
	int val;
	u64 ret;

	if (stat.access_type == MMD)
		val = phy_read_mmd(phydev, MDIO_MMD_PCS, stat.reg);
	else
		val = phy_read(phydev, stat.reg);

	if (val < 0) {
		ret = U64_MAX;
	} else {
		val = val & stat.mask;
		priv->stats[i] += val;
		ret = priv->stats[i];
	}

	return ret;
}

static void qca83xx_get_stats(struct phy_device *phydev,
			      struct ethtool_stats *stats, u64 *data)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(qca83xx_hw_stats); i++)
		data[i] = qca83xx_get_stat(phydev, i);
}

static int qca83xx_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct qca83xx_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	phydev->priv = priv;

	return 0;
}

static int qca83xx_config_init(struct phy_device *phydev)
{
	u8 switch_revision;

	switch_revision = phydev->dev_flags & QCA8K_DEVFLAGS_REVISION_MASK;

	switch (switch_revision) {
	case 1:
		/* For 100M waveform */
		at803x_debug_reg_write(phydev, AT803X_DEBUG_ANALOG_TEST_CTRL, 0x02ea);
		/* Turn on Gigabit clock */
		at803x_debug_reg_write(phydev, AT803X_DEBUG_REG_GREEN, 0x68a0);
		break;

	case 2:
		phy_write_mmd(phydev, MDIO_MMD_AN, MDIO_AN_EEE_ADV, 0x0);
		fallthrough;
	case 4:
		phy_write_mmd(phydev, MDIO_MMD_PCS, MDIO_AZ_DEBUG, 0x803f);
		at803x_debug_reg_write(phydev, AT803X_DEBUG_REG_GREEN, 0x6860);
		at803x_debug_reg_write(phydev, AT803X_DEBUG_SYSTEM_CTRL_MODE, 0x2c46);
		at803x_debug_reg_write(phydev, AT803X_DEBUG_REG_3C, 0x6000);
		break;
	}

	/* Following original QCA sourcecode set port to prefer master */
	phy_set_bits(phydev, MII_CTRL1000, CTL1000_PREFER_MASTER);

	return 0;
}

static int qca8327_config_init(struct phy_device *phydev)
{
	/* QCA8327 require DAC amplitude adjustment for 100m set to +6%.
	 * Disable on init and enable only with 100m speed following
	 * qca original source code.
	 */
	at803x_debug_reg_mask(phydev, AT803X_DEBUG_ANALOG_TEST_CTRL,
			      QCA8327_DEBUG_MANU_CTRL_EN, 0);

	return qca83xx_config_init(phydev);
}

static void qca83xx_link_change_notify(struct phy_device *phydev)
{
	/* Set DAC Amplitude adjustment to +6% for 100m on link running */
	if (phydev->state == PHY_RUNNING) {
		if (phydev->speed == SPEED_100)
			at803x_debug_reg_mask(phydev, AT803X_DEBUG_ANALOG_TEST_CTRL,
					      QCA8327_DEBUG_MANU_CTRL_EN,
					      QCA8327_DEBUG_MANU_CTRL_EN);
	} else {
		/* Reset DAC Amplitude adjustment */
		at803x_debug_reg_mask(phydev, AT803X_DEBUG_ANALOG_TEST_CTRL,
				      QCA8327_DEBUG_MANU_CTRL_EN, 0);
	}
}

static int qca83xx_resume(struct phy_device *phydev)
{
	int ret, val;

	/* Skip reset if not suspended */
	if (!phydev->suspended)
		return 0;

	/* Reinit the port, reset values set by suspend */
	qca83xx_config_init(phydev);

	/* Reset the port on port resume */
	phy_set_bits(phydev, MII_BMCR, BMCR_RESET | BMCR_ANENABLE);

	/* On resume from suspend the switch execute a reset and
	 * restart auto-negotiation. Wait for reset to complete.
	 */
	ret = phy_read_poll_timeout(phydev, MII_BMCR, val, !(val & BMCR_RESET),
				    50000, 600000, true);
	if (ret)
		return ret;

	usleep_range(1000, 2000);

	return 0;
}

static int qca83xx_suspend(struct phy_device *phydev)
{
	at803x_debug_reg_mask(phydev, AT803X_DEBUG_REG_GREEN,
			      AT803X_DEBUG_GATE_CLK_IN1000, 0);

	at803x_debug_reg_mask(phydev, AT803X_DEBUG_REG_HIB_CTRL,
			      AT803X_DEBUG_HIB_CTRL_EN_ANY_CHANGE |
			      AT803X_DEBUG_HIB_CTRL_SEL_RST_80U, 0);

	return 0;
}

static int qca8337_suspend(struct phy_device *phydev)
{
	/* Only QCA8337 support actual suspend. */
	genphy_suspend(phydev);

	return qca83xx_suspend(phydev);
}

static int qca8327_suspend(struct phy_device *phydev)
{
	u16 mask = 0;

	/* QCA8327 cause port unreliability when phy suspend
	 * is set.
	 */
	mask |= ~(BMCR_SPEED1000 | BMCR_FULLDPLX);
	phy_modify(phydev, MII_BMCR, mask, 0);

	return qca83xx_suspend(phydev);
}

static struct phy_driver qca83xx_driver[] = {
{
	/* QCA8337 */
	PHY_ID_MATCH_EXACT(QCA8337_PHY_ID),
	.name			= "Qualcomm Atheros 8337 internal PHY",
	/* PHY_GBIT_FEATURES */
	.probe			= qca83xx_probe,
	.flags			= PHY_IS_INTERNAL,
	.config_init		= qca83xx_config_init,
	.soft_reset		= genphy_soft_reset,
	.get_sset_count		= qca83xx_get_sset_count,
	.get_strings		= qca83xx_get_strings,
	.get_stats		= qca83xx_get_stats,
	.suspend		= qca8337_suspend,
	.resume			= qca83xx_resume,
}, {
	/* QCA8327-A from switch QCA8327-AL1A */
	PHY_ID_MATCH_EXACT(QCA8327_A_PHY_ID),
	.name			= "Qualcomm Atheros 8327-A internal PHY",
	/* PHY_GBIT_FEATURES */
	.link_change_notify	= qca83xx_link_change_notify,
	.probe			= qca83xx_probe,
	.flags			= PHY_IS_INTERNAL,
	.config_init		= qca8327_config_init,
	.soft_reset		= genphy_soft_reset,
	.get_sset_count		= qca83xx_get_sset_count,
	.get_strings		= qca83xx_get_strings,
	.get_stats		= qca83xx_get_stats,
	.suspend		= qca8327_suspend,
	.resume			= qca83xx_resume,
}, {
	/* QCA8327-B from switch QCA8327-BL1A */
	PHY_ID_MATCH_EXACT(QCA8327_B_PHY_ID),
	.name			= "Qualcomm Atheros 8327-B internal PHY",
	/* PHY_GBIT_FEATURES */
	.link_change_notify	= qca83xx_link_change_notify,
	.probe			= qca83xx_probe,
	.flags			= PHY_IS_INTERNAL,
	.config_init		= qca8327_config_init,
	.soft_reset		= genphy_soft_reset,
	.get_sset_count		= qca83xx_get_sset_count,
	.get_strings		= qca83xx_get_strings,
	.get_stats		= qca83xx_get_stats,
	.suspend		= qca8327_suspend,
	.resume			= qca83xx_resume,
}, };

module_phy_driver(qca83xx_driver);

static struct mdio_device_id __maybe_unused qca83xx_tbl[] = {
	{ PHY_ID_MATCH_EXACT(QCA8337_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(QCA8327_A_PHY_ID) },
	{ PHY_ID_MATCH_EXACT(QCA8327_B_PHY_ID) },
	{ }
};

MODULE_DEVICE_TABLE(mdio, qca83xx_tbl);
