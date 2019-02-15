// SPDX-License-Identifier: GPL-2.0+
/*
 * Marvell 10G 88x3310 PHY driver
 *
 * Based upon the ID registers, this PHY appears to be a mixture of IPs
 * from two different companies.
 *
 * There appears to be several different data paths through the PHY which
 * are automatically managed by the PHY.  The following has been determined
 * via observation and experimentation for a setup using single-lane Serdes:
 *
 *       SGMII PHYXS -- BASE-T PCS -- 10G PMA -- AN -- Copper (for <= 1G)
 *  10GBASE-KR PHYXS -- BASE-T PCS -- 10G PMA -- AN -- Copper (for 10G)
 *  10GBASE-KR PHYXS -- BASE-R PCS -- Fiber
 *
 * With XAUI, observation shows:
 *
 *        XAUI PHYXS -- <appropriate PCS as above>
 *
 * and no switching of the host interface mode occurs.
 *
 * If both the fiber and copper ports are connected, the first to gain
 * link takes priority and the other port is completely locked out.
 */
#include <linux/ctype.h>
#include <linux/hwmon.h>
#include <linux/marvell_phy.h>
#include <linux/phy.h>

enum {
	MV_PCS_BASE_T		= 0x0000,
	MV_PCS_BASE_R		= 0x1000,
	MV_PCS_1000BASEX	= 0x2000,

	MV_PCS_PAIRSWAP		= 0x8182,
	MV_PCS_PAIRSWAP_MASK	= 0x0003,
	MV_PCS_PAIRSWAP_AB	= 0x0002,
	MV_PCS_PAIRSWAP_NONE	= 0x0003,

	/* These registers appear at 0x800X and 0xa00X - the 0xa00X control
	 * registers appear to set themselves to the 0x800X when AN is
	 * restarted, but status registers appear readable from either.
	 */
	MV_AN_CTRL1000		= 0x8000, /* 1000base-T control register */
	MV_AN_STAT1000		= 0x8001, /* 1000base-T status register */

	/* Vendor2 MMD registers */
	MV_V2_TEMP_CTRL		= 0xf08a,
	MV_V2_TEMP_CTRL_MASK	= 0xc000,
	MV_V2_TEMP_CTRL_SAMPLE	= 0x0000,
	MV_V2_TEMP_CTRL_DISABLE	= 0xc000,
	MV_V2_TEMP		= 0xf08c,
	MV_V2_TEMP_UNKNOWN	= 0x9600, /* unknown function */
};

struct mv3310_priv {
	struct device *hwmon_dev;
	char *hwmon_name;
};

#ifdef CONFIG_HWMON
static umode_t mv3310_hwmon_is_visible(const void *data,
				       enum hwmon_sensor_types type,
				       u32 attr, int channel)
{
	if (type == hwmon_chip && attr == hwmon_chip_update_interval)
		return 0444;
	if (type == hwmon_temp && attr == hwmon_temp_input)
		return 0444;
	return 0;
}

static int mv3310_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *value)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	int temp;

	if (type == hwmon_chip && attr == hwmon_chip_update_interval) {
		*value = MSEC_PER_SEC;
		return 0;
	}

	if (type == hwmon_temp && attr == hwmon_temp_input) {
		temp = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_TEMP);
		if (temp < 0)
			return temp;

		*value = ((temp & 0xff) - 75) * 1000;

		return 0;
	}

	return -EOPNOTSUPP;
}

static const struct hwmon_ops mv3310_hwmon_ops = {
	.is_visible = mv3310_hwmon_is_visible,
	.read = mv3310_hwmon_read,
};

static u32 mv3310_hwmon_chip_config[] = {
	HWMON_C_REGISTER_TZ | HWMON_C_UPDATE_INTERVAL,
	0,
};

static const struct hwmon_channel_info mv3310_hwmon_chip = {
	.type = hwmon_chip,
	.config = mv3310_hwmon_chip_config,
};

static u32 mv3310_hwmon_temp_config[] = {
	HWMON_T_INPUT,
	0,
};

static const struct hwmon_channel_info mv3310_hwmon_temp = {
	.type = hwmon_temp,
	.config = mv3310_hwmon_temp_config,
};

static const struct hwmon_channel_info *mv3310_hwmon_info[] = {
	&mv3310_hwmon_chip,
	&mv3310_hwmon_temp,
	NULL,
};

static const struct hwmon_chip_info mv3310_hwmon_chip_info = {
	.ops = &mv3310_hwmon_ops,
	.info = mv3310_hwmon_info,
};

static int mv3310_hwmon_config(struct phy_device *phydev, bool enable)
{
	u16 val;
	int ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_TEMP,
			    MV_V2_TEMP_UNKNOWN);
	if (ret < 0)
		return ret;

	val = enable ? MV_V2_TEMP_CTRL_SAMPLE : MV_V2_TEMP_CTRL_DISABLE;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND2, MV_V2_TEMP_CTRL,
			      MV_V2_TEMP_CTRL_MASK, val);
}

static void mv3310_hwmon_disable(void *data)
{
	struct phy_device *phydev = data;

	mv3310_hwmon_config(phydev, false);
}

static int mv3310_hwmon_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct mv3310_priv *priv = dev_get_drvdata(&phydev->mdio.dev);
	int i, j, ret;

	priv->hwmon_name = devm_kstrdup(dev, dev_name(dev), GFP_KERNEL);
	if (!priv->hwmon_name)
		return -ENODEV;

	for (i = j = 0; priv->hwmon_name[i]; i++) {
		if (isalnum(priv->hwmon_name[i])) {
			if (i != j)
				priv->hwmon_name[j] = priv->hwmon_name[i];
			j++;
		}
	}
	priv->hwmon_name[j] = '\0';

	ret = mv3310_hwmon_config(phydev, true);
	if (ret)
		return ret;

	ret = devm_add_action_or_reset(dev, mv3310_hwmon_disable, phydev);
	if (ret)
		return ret;

	priv->hwmon_dev = devm_hwmon_device_register_with_info(dev,
				priv->hwmon_name, phydev,
				&mv3310_hwmon_chip_info, NULL);

	return PTR_ERR_OR_ZERO(priv->hwmon_dev);
}
#else
static inline int mv3310_hwmon_config(struct phy_device *phydev, bool enable)
{
	return 0;
}

static int mv3310_hwmon_probe(struct phy_device *phydev)
{
	return 0;
}
#endif

static int mv3310_probe(struct phy_device *phydev)
{
	struct mv3310_priv *priv;
	u32 mmd_mask = MDIO_DEVS_PMAPMD | MDIO_DEVS_AN;
	int ret;

	if (!phydev->is_c45 ||
	    (phydev->c45_ids.devices_in_package & mmd_mask) != mmd_mask)
		return -ENODEV;

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&phydev->mdio.dev, priv);

	ret = mv3310_hwmon_probe(phydev);
	if (ret)
		return ret;

	return 0;
}

static int mv3310_suspend(struct phy_device *phydev)
{
	return 0;
}

static int mv3310_resume(struct phy_device *phydev)
{
	return mv3310_hwmon_config(phydev, true);
}

static int mv3310_config_init(struct phy_device *phydev)
{
	int ret, val;

	/* Check that the PHY interface type is compatible */
	if (phydev->interface != PHY_INTERFACE_MODE_SGMII &&
	    phydev->interface != PHY_INTERFACE_MODE_XAUI &&
	    phydev->interface != PHY_INTERFACE_MODE_RXAUI &&
	    phydev->interface != PHY_INTERFACE_MODE_10GKR)
		return -ENODEV;

	if (phydev->c45_ids.devices_in_package & MDIO_DEVS_AN) {
		val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
		if (val < 0)
			return val;

		if (val & MDIO_AN_STAT1_ABLE)
			__set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT,
				  phydev->supported);
	}

	ret = genphy_c45_pma_read_abilities(phydev);
	if (ret)
		return ret;

	return 0;
}

static int mv3310_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	u16 reg;
	int ret;

	/* We don't support manual MDI control */
	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;

	if (phydev->autoneg == AUTONEG_DISABLE) {
		ret = genphy_c45_pma_setup_forced(phydev);
		if (ret < 0)
			return ret;

		return genphy_c45_an_disable_aneg(phydev);
	}

	ret = genphy_c45_an_config_aneg(phydev);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	/* Clause 45 has no standardized support for 1000BaseT, therefore
	 * use vendor registers for this mode.
	 */
	reg = linkmode_adv_to_mii_ctrl1000_t(phydev->advertising);
	ret = phy_modify_mmd_changed(phydev, MDIO_MMD_AN, MV_AN_CTRL1000,
			     ADVERTISE_1000FULL | ADVERTISE_1000HALF, reg);
	if (ret < 0)
		return ret;
	if (ret > 0)
		changed = true;

	if (!changed) {
		/* Configure and restart aneg if it wasn't set before */
		ret = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_CTRL1);
		if (ret < 0)
			return ret;

		if (!(ret & MDIO_AN_CTRL1_ENABLE))
			changed = 1;
	}

	if (changed)
		ret = genphy_c45_restart_aneg(phydev);

	return ret;
}

static int mv3310_aneg_done(struct phy_device *phydev)
{
	int val;

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MV_PCS_BASE_R + MDIO_STAT1);
	if (val < 0)
		return val;

	if (val & MDIO_STAT1_LSTATUS)
		return 1;

	return genphy_c45_aneg_done(phydev);
}

static void mv3310_update_interface(struct phy_device *phydev)
{
	if ((phydev->interface == PHY_INTERFACE_MODE_SGMII ||
	     phydev->interface == PHY_INTERFACE_MODE_10GKR) && phydev->link) {
		/* The PHY automatically switches its serdes interface (and
		 * active PHYXS instance) between Cisco SGMII and 10GBase-KR
		 * modes according to the speed.  Florian suggests setting
		 * phydev->interface to communicate this to the MAC. Only do
		 * this if we are already in either SGMII or 10GBase-KR mode.
		 */
		if (phydev->speed == SPEED_10000)
			phydev->interface = PHY_INTERFACE_MODE_10GKR;
		else if (phydev->speed >= SPEED_10 &&
			 phydev->speed < SPEED_10000)
			phydev->interface = PHY_INTERFACE_MODE_SGMII;
	}
}

/* 10GBASE-ER,LR,LRM,SR do not support autonegotiation. */
static int mv3310_read_10gbr_status(struct phy_device *phydev)
{
	phydev->link = 1;
	phydev->speed = SPEED_10000;
	phydev->duplex = DUPLEX_FULL;

	mv3310_update_interface(phydev);

	return 0;
}

static int mv3310_read_status(struct phy_device *phydev)
{
	int val;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	linkmode_zero(phydev->lp_advertising);
	phydev->link = 0;
	phydev->pause = 0;
	phydev->asym_pause = 0;
	phydev->mdix = 0;

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MV_PCS_BASE_R + MDIO_STAT1);
	if (val < 0)
		return val;

	if (val & MDIO_STAT1_LSTATUS)
		return mv3310_read_10gbr_status(phydev);

	val = genphy_c45_read_link(phydev);
	if (val < 0)
		return val;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	if (val < 0)
		return val;

	if (val & MDIO_AN_STAT1_COMPLETE) {
		val = genphy_c45_read_lpa(phydev);
		if (val < 0)
			return val;

		/* Read the link partner's 1G advertisement */
		val = phy_read_mmd(phydev, MDIO_MMD_AN, MV_AN_STAT1000);
		if (val < 0)
			return val;

		mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising, val);

		if (phydev->autoneg == AUTONEG_ENABLE)
			phy_resolve_aneg_linkmode(phydev);
	}

	if (phydev->autoneg != AUTONEG_ENABLE) {
		val = genphy_c45_read_pma(phydev);
		if (val < 0)
			return val;
	}

	if (phydev->speed == SPEED_10000) {
		val = genphy_c45_read_mdix(phydev);
		if (val < 0)
			return val;
	} else {
		val = phy_read_mmd(phydev, MDIO_MMD_PCS, MV_PCS_PAIRSWAP);
		if (val < 0)
			return val;

		switch (val & MV_PCS_PAIRSWAP_MASK) {
		case MV_PCS_PAIRSWAP_AB:
			phydev->mdix = ETH_TP_MDI_X;
			break;
		case MV_PCS_PAIRSWAP_NONE:
			phydev->mdix = ETH_TP_MDI;
			break;
		default:
			phydev->mdix = ETH_TP_MDI_INVALID;
			break;
		}
	}

	mv3310_update_interface(phydev);

	return 0;
}

static struct phy_driver mv3310_drivers[] = {
	{
		.phy_id		= 0x002b09aa,
		.phy_id_mask	= MARVELL_PHY_ID_MASK,
		.name		= "mv88x3310",
		.features	= PHY_10GBIT_FEATURES,
		.soft_reset	= gen10g_no_soft_reset,
		.config_init	= mv3310_config_init,
		.probe		= mv3310_probe,
		.suspend	= mv3310_suspend,
		.resume		= mv3310_resume,
		.config_aneg	= mv3310_config_aneg,
		.aneg_done	= mv3310_aneg_done,
		.read_status	= mv3310_read_status,
	},
};

module_phy_driver(mv3310_drivers);

static struct mdio_device_id __maybe_unused mv3310_tbl[] = {
	{ 0x002b09aa, MARVELL_PHY_ID_MASK },
	{ },
};
MODULE_DEVICE_TABLE(mdio, mv3310_tbl);
MODULE_DESCRIPTION("Marvell Alaska X 10Gigabit Ethernet PHY driver (MV88X3310)");
MODULE_LICENSE("GPL");
