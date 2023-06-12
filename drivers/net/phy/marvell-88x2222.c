// SPDX-License-Identifier: GPL-2.0+
/*
 * Marvell 88x2222 dual-port multi-speed ethernet transceiver.
 *
 * Supports:
 *	XAUI on the host side.
 *	1000Base-X or 10GBase-R on the line side.
 *	SGMII over 1000Base-X.
 */
#include <linux/module.h>
#include <linux/phy.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mdio.h>
#include <linux/marvell_phy.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/sfp.h>
#include <linux/netdevice.h>

/* Port PCS Configuration */
#define	MV_PCS_CONFIG		0xF002
#define	MV_PCS_HOST_XAUI	0x73
#define	MV_PCS_LINE_10GBR	(0x71 << 8)
#define	MV_PCS_LINE_1GBX_AN	(0x7B << 8)
#define	MV_PCS_LINE_SGMII_AN	(0x7F << 8)

/* Port Reset and Power Down */
#define	MV_PORT_RST	0xF003
#define	MV_LINE_RST_SW	BIT(15)
#define	MV_HOST_RST_SW	BIT(7)
#define	MV_PORT_RST_SW	(MV_LINE_RST_SW | MV_HOST_RST_SW)

/* PMD Receive Signal Detect */
#define	MV_RX_SIGNAL_DETECT		0x000A
#define	MV_RX_SIGNAL_DETECT_GLOBAL	BIT(0)

/* 1000Base-X/SGMII Control Register */
#define	MV_1GBX_CTRL		(0x2000 + MII_BMCR)

/* 1000BASE-X/SGMII Status Register */
#define	MV_1GBX_STAT		(0x2000 + MII_BMSR)

/* 1000Base-X Auto-Negotiation Advertisement Register */
#define	MV_1GBX_ADVERTISE	(0x2000 + MII_ADVERTISE)

/* 1000Base-X PHY Specific Status Register */
#define	MV_1GBX_PHY_STAT		0xA003
#define	MV_1GBX_PHY_STAT_AN_RESOLVED	BIT(11)
#define	MV_1GBX_PHY_STAT_DUPLEX		BIT(13)
#define	MV_1GBX_PHY_STAT_SPEED100	BIT(14)
#define	MV_1GBX_PHY_STAT_SPEED1000	BIT(15)

#define	AUTONEG_TIMEOUT	3

struct mv2222_data {
	phy_interface_t line_interface;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported);
	bool sfp_link;
};

/* SFI PMA transmit enable */
static int mv2222_tx_enable(struct phy_device *phydev)
{
	return phy_clear_bits_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_TXDIS,
				  MDIO_PMD_TXDIS_GLOBAL);
}

/* SFI PMA transmit disable */
static int mv2222_tx_disable(struct phy_device *phydev)
{
	return phy_set_bits_mmd(phydev, MDIO_MMD_PMAPMD, MDIO_PMA_TXDIS,
				MDIO_PMD_TXDIS_GLOBAL);
}

static int mv2222_soft_reset(struct phy_device *phydev)
{
	int val, ret;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_PORT_RST,
			    MV_PORT_RST_SW);
	if (ret < 0)
		return ret;

	return phy_read_mmd_poll_timeout(phydev, MDIO_MMD_VEND2, MV_PORT_RST,
					 val, !(val & MV_PORT_RST_SW),
					 5000, 1000000, true);
}

static int mv2222_disable_aneg(struct phy_device *phydev)
{
	int ret = phy_clear_bits_mmd(phydev, MDIO_MMD_PCS, MV_1GBX_CTRL,
				     BMCR_ANENABLE | BMCR_ANRESTART);
	if (ret < 0)
		return ret;

	return mv2222_soft_reset(phydev);
}

static int mv2222_enable_aneg(struct phy_device *phydev)
{
	int ret = phy_set_bits_mmd(phydev, MDIO_MMD_PCS, MV_1GBX_CTRL,
				   BMCR_ANENABLE | BMCR_RESET);
	if (ret < 0)
		return ret;

	return mv2222_soft_reset(phydev);
}

static int mv2222_set_sgmii_speed(struct phy_device *phydev)
{
	struct mv2222_data *priv = phydev->priv;

	switch (phydev->speed) {
	default:
	case SPEED_1000:
		if ((linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
				       priv->supported) ||
		     linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
				       priv->supported)))
			return phy_modify_mmd(phydev, MDIO_MMD_PCS,
					      MV_1GBX_CTRL,
					      BMCR_SPEED1000 | BMCR_SPEED100,
					      BMCR_SPEED1000);

		fallthrough;
	case SPEED_100:
		if ((linkmode_test_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
				       priv->supported) ||
		     linkmode_test_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT,
				       priv->supported)))
			return phy_modify_mmd(phydev, MDIO_MMD_PCS,
					      MV_1GBX_CTRL,
					      BMCR_SPEED1000 | BMCR_SPEED100,
					      BMCR_SPEED100);
		fallthrough;
	case SPEED_10:
		if ((linkmode_test_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT,
				       priv->supported) ||
		     linkmode_test_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT,
				       priv->supported)))
			return phy_modify_mmd(phydev, MDIO_MMD_PCS,
					      MV_1GBX_CTRL,
					      BMCR_SPEED1000 | BMCR_SPEED100,
					      BMCR_SPEED10);

		return -EINVAL;
	}
}

static bool mv2222_is_10g_capable(struct phy_device *phydev)
{
	struct mv2222_data *priv = phydev->priv;

	return (linkmode_test_bit(ETHTOOL_LINK_MODE_10000baseT_Full_BIT,
				  priv->supported) ||
		linkmode_test_bit(ETHTOOL_LINK_MODE_10000baseCR_Full_BIT,
				  priv->supported) ||
		linkmode_test_bit(ETHTOOL_LINK_MODE_10000baseSR_Full_BIT,
				  priv->supported) ||
		linkmode_test_bit(ETHTOOL_LINK_MODE_10000baseLR_Full_BIT,
				  priv->supported) ||
		linkmode_test_bit(ETHTOOL_LINK_MODE_10000baseLRM_Full_BIT,
				  priv->supported) ||
		linkmode_test_bit(ETHTOOL_LINK_MODE_10000baseER_Full_BIT,
				  priv->supported));
}

static bool mv2222_is_1gbx_capable(struct phy_device *phydev)
{
	struct mv2222_data *priv = phydev->priv;

	return linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseX_Full_BIT,
				 priv->supported);
}

static bool mv2222_is_sgmii_capable(struct phy_device *phydev)
{
	struct mv2222_data *priv = phydev->priv;

	return (linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT,
				  priv->supported) ||
		linkmode_test_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT,
				  priv->supported) ||
		linkmode_test_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT,
				  priv->supported) ||
		linkmode_test_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT,
				  priv->supported) ||
		linkmode_test_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT,
				  priv->supported) ||
		linkmode_test_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT,
				  priv->supported));
}

static int mv2222_config_line(struct phy_device *phydev)
{
	struct mv2222_data *priv = phydev->priv;

	switch (priv->line_interface) {
	case PHY_INTERFACE_MODE_10GBASER:
		return phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_PCS_CONFIG,
				     MV_PCS_HOST_XAUI | MV_PCS_LINE_10GBR);
	case PHY_INTERFACE_MODE_1000BASEX:
		return phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_PCS_CONFIG,
				     MV_PCS_HOST_XAUI | MV_PCS_LINE_1GBX_AN);
	case PHY_INTERFACE_MODE_SGMII:
		return phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_PCS_CONFIG,
				     MV_PCS_HOST_XAUI | MV_PCS_LINE_SGMII_AN);
	default:
		return -EINVAL;
	}
}

/* Switch between 1G (1000Base-X/SGMII) and 10G (10GBase-R) modes */
static int mv2222_swap_line_type(struct phy_device *phydev)
{
	struct mv2222_data *priv = phydev->priv;
	bool changed = false;
	int ret;

	switch (priv->line_interface) {
	case PHY_INTERFACE_MODE_10GBASER:
		if (mv2222_is_1gbx_capable(phydev)) {
			priv->line_interface = PHY_INTERFACE_MODE_1000BASEX;
			changed = true;
		}

		if (mv2222_is_sgmii_capable(phydev)) {
			priv->line_interface = PHY_INTERFACE_MODE_SGMII;
			changed = true;
		}

		break;
	case PHY_INTERFACE_MODE_1000BASEX:
	case PHY_INTERFACE_MODE_SGMII:
		if (mv2222_is_10g_capable(phydev)) {
			priv->line_interface = PHY_INTERFACE_MODE_10GBASER;
			changed = true;
		}

		break;
	default:
		return -EINVAL;
	}

	if (changed) {
		ret = mv2222_config_line(phydev);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int mv2222_setup_forced(struct phy_device *phydev)
{
	struct mv2222_data *priv = phydev->priv;
	int ret;

	if (priv->line_interface == PHY_INTERFACE_MODE_10GBASER) {
		if (phydev->speed < SPEED_10000 &&
		    phydev->speed != SPEED_UNKNOWN) {
			ret = mv2222_swap_line_type(phydev);
			if (ret < 0)
				return ret;
		}
	}

	if (priv->line_interface == PHY_INTERFACE_MODE_SGMII) {
		ret = mv2222_set_sgmii_speed(phydev);
		if (ret < 0)
			return ret;
	}

	return mv2222_disable_aneg(phydev);
}

static int mv2222_config_aneg(struct phy_device *phydev)
{
	struct mv2222_data *priv = phydev->priv;
	int ret, adv;

	/* SFP is not present, do nothing */
	if (priv->line_interface == PHY_INTERFACE_MODE_NA)
		return 0;

	if (phydev->autoneg == AUTONEG_DISABLE ||
	    priv->line_interface == PHY_INTERFACE_MODE_10GBASER)
		return mv2222_setup_forced(phydev);

	adv = linkmode_adv_to_mii_adv_x(priv->supported,
					ETHTOOL_LINK_MODE_1000baseX_Full_BIT);

	ret = phy_modify_mmd(phydev, MDIO_MMD_PCS, MV_1GBX_ADVERTISE,
			     ADVERTISE_1000XFULL |
			     ADVERTISE_1000XPAUSE | ADVERTISE_1000XPSE_ASYM,
			     adv);
	if (ret < 0)
		return ret;

	return mv2222_enable_aneg(phydev);
}

static int mv2222_aneg_done(struct phy_device *phydev)
{
	int ret;

	if (mv2222_is_10g_capable(phydev)) {
		ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_STAT1);
		if (ret < 0)
			return ret;

		if (ret & MDIO_STAT1_LSTATUS)
			return 1;
	}

	ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MV_1GBX_STAT);
	if (ret < 0)
		return ret;

	return (ret & BMSR_ANEGCOMPLETE);
}

/* Returns negative on error, 0 if link is down, 1 if link is up */
static int mv2222_read_status_10g(struct phy_device *phydev)
{
	static int timeout;
	int val, link = 0;

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_STAT1);
	if (val < 0)
		return val;

	if (val & MDIO_STAT1_LSTATUS) {
		link = 1;

		/* 10GBASE-R do not support auto-negotiation */
		phydev->autoneg = AUTONEG_DISABLE;
		phydev->speed = SPEED_10000;
		phydev->duplex = DUPLEX_FULL;
	} else {
		if (phydev->autoneg == AUTONEG_ENABLE) {
			timeout++;

			if (timeout > AUTONEG_TIMEOUT) {
				timeout = 0;

				val = mv2222_swap_line_type(phydev);
				if (val < 0)
					return val;

				return mv2222_config_aneg(phydev);
			}
		}
	}

	return link;
}

/* Returns negative on error, 0 if link is down, 1 if link is up */
static int mv2222_read_status_1g(struct phy_device *phydev)
{
	static int timeout;
	int val, link = 0;

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MV_1GBX_STAT);
	if (val < 0)
		return val;

	if (phydev->autoneg == AUTONEG_ENABLE &&
	    !(val & BMSR_ANEGCOMPLETE)) {
		timeout++;

		if (timeout > AUTONEG_TIMEOUT) {
			timeout = 0;

			val = mv2222_swap_line_type(phydev);
			if (val < 0)
				return val;

			return mv2222_config_aneg(phydev);
		}

		return 0;
	}

	if (!(val & BMSR_LSTATUS))
		return 0;

	link = 1;

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MV_1GBX_PHY_STAT);
	if (val < 0)
		return val;

	if (val & MV_1GBX_PHY_STAT_AN_RESOLVED) {
		if (val & MV_1GBX_PHY_STAT_DUPLEX)
			phydev->duplex = DUPLEX_FULL;
		else
			phydev->duplex = DUPLEX_HALF;

		if (val & MV_1GBX_PHY_STAT_SPEED1000)
			phydev->speed = SPEED_1000;
		else if (val & MV_1GBX_PHY_STAT_SPEED100)
			phydev->speed = SPEED_100;
		else
			phydev->speed = SPEED_10;
	}

	return link;
}

static bool mv2222_link_is_operational(struct phy_device *phydev)
{
	struct mv2222_data *priv = phydev->priv;
	int val;

	val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MV_RX_SIGNAL_DETECT);
	if (val < 0 || !(val & MV_RX_SIGNAL_DETECT_GLOBAL))
		return false;

	if (phydev->sfp_bus && !priv->sfp_link)
		return false;

	return true;
}

static int mv2222_read_status(struct phy_device *phydev)
{
	struct mv2222_data *priv = phydev->priv;
	int link;

	phydev->link = 0;
	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;

	if (!mv2222_link_is_operational(phydev))
		return 0;

	if (priv->line_interface == PHY_INTERFACE_MODE_10GBASER)
		link = mv2222_read_status_10g(phydev);
	else
		link = mv2222_read_status_1g(phydev);

	if (link < 0)
		return link;

	phydev->link = link;

	return 0;
}

static int mv2222_resume(struct phy_device *phydev)
{
	return mv2222_tx_enable(phydev);
}

static int mv2222_suspend(struct phy_device *phydev)
{
	return mv2222_tx_disable(phydev);
}

static int mv2222_get_features(struct phy_device *phydev)
{
	/* All supported linkmodes are set at probe */

	return 0;
}

static int mv2222_config_init(struct phy_device *phydev)
{
	if (phydev->interface != PHY_INTERFACE_MODE_XAUI)
		return -EINVAL;

	return 0;
}

static int mv2222_sfp_insert(void *upstream, const struct sfp_eeprom_id *id)
{
	DECLARE_PHY_INTERFACE_MASK(interfaces);
	struct phy_device *phydev = upstream;
	phy_interface_t sfp_interface;
	struct mv2222_data *priv;
	struct device *dev;
	int ret;

	__ETHTOOL_DECLARE_LINK_MODE_MASK(sfp_supported) = { 0, };

	priv = phydev->priv;
	dev = &phydev->mdio.dev;

	sfp_parse_support(phydev->sfp_bus, id, sfp_supported, interfaces);
	phydev->port = sfp_parse_port(phydev->sfp_bus, id, sfp_supported);
	sfp_interface = sfp_select_interface(phydev->sfp_bus, sfp_supported);

	dev_info(dev, "%s SFP module inserted\n", phy_modes(sfp_interface));

	if (sfp_interface != PHY_INTERFACE_MODE_10GBASER &&
	    sfp_interface != PHY_INTERFACE_MODE_1000BASEX &&
	    sfp_interface != PHY_INTERFACE_MODE_SGMII) {
		dev_err(dev, "Incompatible SFP module inserted\n");

		return -EINVAL;
	}

	priv->line_interface = sfp_interface;
	linkmode_and(priv->supported, phydev->supported, sfp_supported);

	ret = mv2222_config_line(phydev);
	if (ret < 0)
		return ret;

	if (mutex_trylock(&phydev->lock)) {
		ret = mv2222_config_aneg(phydev);
		mutex_unlock(&phydev->lock);
	}

	return ret;
}

static void mv2222_sfp_remove(void *upstream)
{
	struct phy_device *phydev = upstream;
	struct mv2222_data *priv;

	priv = phydev->priv;

	priv->line_interface = PHY_INTERFACE_MODE_NA;
	linkmode_zero(priv->supported);
	phydev->port = PORT_NONE;
}

static void mv2222_sfp_link_up(void *upstream)
{
	struct phy_device *phydev = upstream;
	struct mv2222_data *priv;

	priv = phydev->priv;
	priv->sfp_link = true;
}

static void mv2222_sfp_link_down(void *upstream)
{
	struct phy_device *phydev = upstream;
	struct mv2222_data *priv;

	priv = phydev->priv;
	priv->sfp_link = false;
}

static const struct sfp_upstream_ops sfp_phy_ops = {
	.module_insert = mv2222_sfp_insert,
	.module_remove = mv2222_sfp_remove,
	.link_up = mv2222_sfp_link_up,
	.link_down = mv2222_sfp_link_down,
	.attach = phy_sfp_attach,
	.detach = phy_sfp_detach,
};

static int mv2222_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct mv2222_data *priv = NULL;

	__ETHTOOL_DECLARE_LINK_MODE_MASK(supported) = { 0, };

	linkmode_set_bit(ETHTOOL_LINK_MODE_Autoneg_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_Pause_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_Asym_Pause_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_FIBRE_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_TP_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_10baseT_Half_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_10baseT_Full_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Half_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_100baseT_Full_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseT_Half_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseT_Full_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_1000baseX_Full_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseT_Full_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseCR_Full_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseSR_Full_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseLR_Full_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseLRM_Full_BIT, supported);
	linkmode_set_bit(ETHTOOL_LINK_MODE_10000baseER_Full_BIT, supported);

	linkmode_copy(phydev->supported, supported);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->line_interface = PHY_INTERFACE_MODE_NA;
	phydev->priv = priv;

	return phy_sfp_probe(phydev, &sfp_phy_ops);
}

static struct phy_driver mv2222_drivers[] = {
	{
		.phy_id = MARVELL_PHY_ID_88X2222,
		.phy_id_mask = MARVELL_PHY_ID_MASK,
		.name = "Marvell 88X2222",
		.get_features = mv2222_get_features,
		.soft_reset = mv2222_soft_reset,
		.config_init = mv2222_config_init,
		.config_aneg = mv2222_config_aneg,
		.aneg_done = mv2222_aneg_done,
		.probe = mv2222_probe,
		.suspend = mv2222_suspend,
		.resume = mv2222_resume,
		.read_status = mv2222_read_status,
	},
};
module_phy_driver(mv2222_drivers);

static struct mdio_device_id __maybe_unused mv2222_tbl[] = {
	{ MARVELL_PHY_ID_88X2222, MARVELL_PHY_ID_MASK },
	{ }
};
MODULE_DEVICE_TABLE(mdio, mv2222_tbl);

MODULE_DESCRIPTION("Marvell 88x2222 ethernet transceiver driver");
MODULE_LICENSE("GPL");
