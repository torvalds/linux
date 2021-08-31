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
#include <linux/delay.h>
#include <linux/hwmon.h>
#include <linux/marvell_phy.h>
#include <linux/phy.h>
#include <linux/sfp.h>
#include <linux/netdevice.h>

#define MV_PHY_ALASKA_NBT_QUIRK_MASK	0xfffffffe
#define MV_PHY_ALASKA_NBT_QUIRK_REV	(MARVELL_PHY_ID_88X3310 | 0xa)

enum {
	MV_PMA_FW_VER0		= 0xc011,
	MV_PMA_FW_VER1		= 0xc012,
	MV_PMA_21X0_PORT_CTRL	= 0xc04a,
	MV_PMA_21X0_PORT_CTRL_SWRST				= BIT(15),
	MV_PMA_21X0_PORT_CTRL_MACTYPE_MASK			= 0x7,
	MV_PMA_21X0_PORT_CTRL_MACTYPE_USXGMII			= 0x0,
	MV_PMA_2180_PORT_CTRL_MACTYPE_DXGMII			= 0x1,
	MV_PMA_2180_PORT_CTRL_MACTYPE_QXGMII			= 0x2,
	MV_PMA_21X0_PORT_CTRL_MACTYPE_5GBASER			= 0x4,
	MV_PMA_21X0_PORT_CTRL_MACTYPE_5GBASER_NO_SGMII_AN	= 0x5,
	MV_PMA_21X0_PORT_CTRL_MACTYPE_10GBASER_RATE_MATCH	= 0x6,
	MV_PMA_BOOT		= 0xc050,
	MV_PMA_BOOT_FATAL	= BIT(0),

	MV_PCS_BASE_T		= 0x0000,
	MV_PCS_BASE_R		= 0x1000,
	MV_PCS_1000BASEX	= 0x2000,

	MV_PCS_CSCR1		= 0x8000,
	MV_PCS_CSCR1_ED_MASK	= 0x0300,
	MV_PCS_CSCR1_ED_OFF	= 0x0000,
	MV_PCS_CSCR1_ED_RX	= 0x0200,
	MV_PCS_CSCR1_ED_NLP	= 0x0300,
	MV_PCS_CSCR1_MDIX_MASK	= 0x0060,
	MV_PCS_CSCR1_MDIX_MDI	= 0x0000,
	MV_PCS_CSCR1_MDIX_MDIX	= 0x0020,
	MV_PCS_CSCR1_MDIX_AUTO	= 0x0060,

	MV_PCS_CSSR1		= 0x8008,
	MV_PCS_CSSR1_SPD1_MASK	= 0xc000,
	MV_PCS_CSSR1_SPD1_SPD2	= 0xc000,
	MV_PCS_CSSR1_SPD1_1000	= 0x8000,
	MV_PCS_CSSR1_SPD1_100	= 0x4000,
	MV_PCS_CSSR1_SPD1_10	= 0x0000,
	MV_PCS_CSSR1_DUPLEX_FULL= BIT(13),
	MV_PCS_CSSR1_RESOLVED	= BIT(11),
	MV_PCS_CSSR1_MDIX	= BIT(6),
	MV_PCS_CSSR1_SPD2_MASK	= 0x000c,
	MV_PCS_CSSR1_SPD2_5000	= 0x0008,
	MV_PCS_CSSR1_SPD2_2500	= 0x0004,
	MV_PCS_CSSR1_SPD2_10000	= 0x0000,

	/* Temperature read register (88E2110 only) */
	MV_PCS_TEMP		= 0x8042,

	/* Number of ports on the device */
	MV_PCS_PORT_INFO	= 0xd00d,
	MV_PCS_PORT_INFO_NPORTS_MASK	= 0x0380,
	MV_PCS_PORT_INFO_NPORTS_SHIFT	= 7,

	/* These registers appear at 0x800X and 0xa00X - the 0xa00X control
	 * registers appear to set themselves to the 0x800X when AN is
	 * restarted, but status registers appear readable from either.
	 */
	MV_AN_CTRL1000		= 0x8000, /* 1000base-T control register */
	MV_AN_STAT1000		= 0x8001, /* 1000base-T status register */

	/* Vendor2 MMD registers */
	MV_V2_PORT_CTRL		= 0xf001,
	MV_V2_PORT_CTRL_PWRDOWN					= BIT(11),
	MV_V2_33X0_PORT_CTRL_SWRST				= BIT(15),
	MV_V2_33X0_PORT_CTRL_MACTYPE_MASK			= 0x7,
	MV_V2_33X0_PORT_CTRL_MACTYPE_RXAUI			= 0x0,
	MV_V2_3310_PORT_CTRL_MACTYPE_XAUI_RATE_MATCH		= 0x1,
	MV_V2_3340_PORT_CTRL_MACTYPE_RXAUI_NO_SGMII_AN		= 0x1,
	MV_V2_33X0_PORT_CTRL_MACTYPE_RXAUI_RATE_MATCH		= 0x2,
	MV_V2_3310_PORT_CTRL_MACTYPE_XAUI			= 0x3,
	MV_V2_33X0_PORT_CTRL_MACTYPE_10GBASER			= 0x4,
	MV_V2_33X0_PORT_CTRL_MACTYPE_10GBASER_NO_SGMII_AN	= 0x5,
	MV_V2_33X0_PORT_CTRL_MACTYPE_10GBASER_RATE_MATCH	= 0x6,
	MV_V2_33X0_PORT_CTRL_MACTYPE_USXGMII			= 0x7,
	MV_V2_PORT_INTR_STS     = 0xf040,
	MV_V2_PORT_INTR_MASK    = 0xf043,
	MV_V2_PORT_INTR_STS_WOL_EN      = BIT(8),
	MV_V2_MAGIC_PKT_WORD0   = 0xf06b,
	MV_V2_MAGIC_PKT_WORD1   = 0xf06c,
	MV_V2_MAGIC_PKT_WORD2   = 0xf06d,
	/* Wake on LAN registers */
	MV_V2_WOL_CTRL          = 0xf06e,
	MV_V2_WOL_CTRL_CLEAR_STS        = BIT(15),
	MV_V2_WOL_CTRL_MAGIC_PKT_EN     = BIT(0),
	/* Temperature control/read registers (88X3310 only) */
	MV_V2_TEMP_CTRL		= 0xf08a,
	MV_V2_TEMP_CTRL_MASK	= 0xc000,
	MV_V2_TEMP_CTRL_SAMPLE	= 0x0000,
	MV_V2_TEMP_CTRL_DISABLE	= 0xc000,
	MV_V2_TEMP		= 0xf08c,
	MV_V2_TEMP_UNKNOWN	= 0x9600, /* unknown function */
};

struct mv3310_chip {
	void (*init_supported_interfaces)(unsigned long *mask);
	int (*get_mactype)(struct phy_device *phydev);
	int (*init_interface)(struct phy_device *phydev, int mactype);

#ifdef CONFIG_HWMON
	int (*hwmon_read_temp_reg)(struct phy_device *phydev);
#endif
};

struct mv3310_priv {
	DECLARE_BITMAP(supported_interfaces, PHY_INTERFACE_MODE_MAX);

	u32 firmware_ver;
	bool rate_match;
	phy_interface_t const_interface;

	struct device *hwmon_dev;
	char *hwmon_name;
};

static const struct mv3310_chip *to_mv3310_chip(struct phy_device *phydev)
{
	return phydev->drv->driver_data;
}

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

static int mv3310_hwmon_read_temp_reg(struct phy_device *phydev)
{
	return phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_TEMP);
}

static int mv2110_hwmon_read_temp_reg(struct phy_device *phydev)
{
	return phy_read_mmd(phydev, MDIO_MMD_PCS, MV_PCS_TEMP);
}

static int mv3310_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			     u32 attr, int channel, long *value)
{
	struct phy_device *phydev = dev_get_drvdata(dev);
	const struct mv3310_chip *chip = to_mv3310_chip(phydev);
	int temp;

	if (type == hwmon_chip && attr == hwmon_chip_update_interval) {
		*value = MSEC_PER_SEC;
		return 0;
	}

	if (type == hwmon_temp && attr == hwmon_temp_input) {
		temp = chip->hwmon_read_temp_reg(phydev);
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

	if (phydev->drv->phy_id != MARVELL_PHY_ID_88X3310)
		return 0;

	ret = phy_write_mmd(phydev, MDIO_MMD_VEND2, MV_V2_TEMP,
			    MV_V2_TEMP_UNKNOWN);
	if (ret < 0)
		return ret;

	val = enable ? MV_V2_TEMP_CTRL_SAMPLE : MV_V2_TEMP_CTRL_DISABLE;

	return phy_modify_mmd(phydev, MDIO_MMD_VEND2, MV_V2_TEMP_CTRL,
			      MV_V2_TEMP_CTRL_MASK, val);
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

static int mv3310_power_down(struct phy_device *phydev)
{
	return phy_set_bits_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_CTRL,
				MV_V2_PORT_CTRL_PWRDOWN);
}

static int mv3310_power_up(struct phy_device *phydev)
{
	struct mv3310_priv *priv = dev_get_drvdata(&phydev->mdio.dev);
	int ret;

	ret = phy_clear_bits_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_CTRL,
				 MV_V2_PORT_CTRL_PWRDOWN);

	if (phydev->drv->phy_id != MARVELL_PHY_ID_88X3310 ||
	    priv->firmware_ver < 0x00030000)
		return ret;

	return phy_set_bits_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_CTRL,
				MV_V2_33X0_PORT_CTRL_SWRST);
}

static int mv3310_reset(struct phy_device *phydev, u32 unit)
{
	int val, err;

	err = phy_modify_mmd(phydev, MDIO_MMD_PCS, unit + MDIO_CTRL1,
			     MDIO_CTRL1_RESET, MDIO_CTRL1_RESET);
	if (err < 0)
		return err;

	return phy_read_mmd_poll_timeout(phydev, MDIO_MMD_PCS,
					 unit + MDIO_CTRL1, val,
					 !(val & MDIO_CTRL1_RESET),
					 5000, 100000, true);
}

static int mv3310_get_edpd(struct phy_device *phydev, u16 *edpd)
{
	int val;

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MV_PCS_CSCR1);
	if (val < 0)
		return val;

	switch (val & MV_PCS_CSCR1_ED_MASK) {
	case MV_PCS_CSCR1_ED_NLP:
		*edpd = 1000;
		break;
	case MV_PCS_CSCR1_ED_RX:
		*edpd = ETHTOOL_PHY_EDPD_NO_TX;
		break;
	default:
		*edpd = ETHTOOL_PHY_EDPD_DISABLE;
		break;
	}
	return 0;
}

static int mv3310_set_edpd(struct phy_device *phydev, u16 edpd)
{
	u16 val;
	int err;

	switch (edpd) {
	case 1000:
	case ETHTOOL_PHY_EDPD_DFLT_TX_MSECS:
		val = MV_PCS_CSCR1_ED_NLP;
		break;

	case ETHTOOL_PHY_EDPD_NO_TX:
		val = MV_PCS_CSCR1_ED_RX;
		break;

	case ETHTOOL_PHY_EDPD_DISABLE:
		val = MV_PCS_CSCR1_ED_OFF;
		break;

	default:
		return -EINVAL;
	}

	err = phy_modify_mmd_changed(phydev, MDIO_MMD_PCS, MV_PCS_CSCR1,
				     MV_PCS_CSCR1_ED_MASK, val);
	if (err > 0)
		err = mv3310_reset(phydev, MV_PCS_BASE_T);

	return err;
}

static int mv3310_sfp_insert(void *upstream, const struct sfp_eeprom_id *id)
{
	struct phy_device *phydev = upstream;
	__ETHTOOL_DECLARE_LINK_MODE_MASK(support) = { 0, };
	phy_interface_t iface;

	sfp_parse_support(phydev->sfp_bus, id, support);
	iface = sfp_select_interface(phydev->sfp_bus, support);

	if (iface != PHY_INTERFACE_MODE_10GBASER) {
		dev_err(&phydev->mdio.dev, "incompatible SFP module inserted\n");
		return -EINVAL;
	}
	return 0;
}

static const struct sfp_upstream_ops mv3310_sfp_ops = {
	.attach = phy_sfp_attach,
	.detach = phy_sfp_detach,
	.module_insert = mv3310_sfp_insert,
};

static int mv3310_probe(struct phy_device *phydev)
{
	const struct mv3310_chip *chip = to_mv3310_chip(phydev);
	struct mv3310_priv *priv;
	u32 mmd_mask = MDIO_DEVS_PMAPMD | MDIO_DEVS_AN;
	int ret;

	if (!phydev->is_c45 ||
	    (phydev->c45_ids.devices_in_package & mmd_mask) != mmd_mask)
		return -ENODEV;

	ret = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MV_PMA_BOOT);
	if (ret < 0)
		return ret;

	if (ret & MV_PMA_BOOT_FATAL) {
		dev_warn(&phydev->mdio.dev,
			 "PHY failed to boot firmware, status=%04x\n", ret);
		return -ENODEV;
	}

	priv = devm_kzalloc(&phydev->mdio.dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(&phydev->mdio.dev, priv);

	ret = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MV_PMA_FW_VER0);
	if (ret < 0)
		return ret;

	priv->firmware_ver = ret << 16;

	ret = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MV_PMA_FW_VER1);
	if (ret < 0)
		return ret;

	priv->firmware_ver |= ret;

	phydev_info(phydev, "Firmware version %u.%u.%u.%u\n",
		    priv->firmware_ver >> 24, (priv->firmware_ver >> 16) & 255,
		    (priv->firmware_ver >> 8) & 255, priv->firmware_ver & 255);

	/* Powering down the port when not in use saves about 600mW */
	ret = mv3310_power_down(phydev);
	if (ret)
		return ret;

	ret = mv3310_hwmon_probe(phydev);
	if (ret)
		return ret;

	chip->init_supported_interfaces(priv->supported_interfaces);

	return phy_sfp_probe(phydev, &mv3310_sfp_ops);
}

static void mv3310_remove(struct phy_device *phydev)
{
	mv3310_hwmon_config(phydev, false);
}

static int mv3310_suspend(struct phy_device *phydev)
{
	return mv3310_power_down(phydev);
}

static int mv3310_resume(struct phy_device *phydev)
{
	int ret;

	ret = mv3310_power_up(phydev);
	if (ret)
		return ret;

	return mv3310_hwmon_config(phydev, true);
}

/* Some PHYs in the Alaska family such as the 88X3310 and the 88E2010
 * don't set bit 14 in PMA Extended Abilities (1.11), although they do
 * support 2.5GBASET and 5GBASET. For these models, we can still read their
 * 2.5G/5G extended abilities register (1.21). We detect these models based on
 * the PMA device identifier, with a mask matching models known to have this
 * issue
 */
static bool mv3310_has_pma_ngbaset_quirk(struct phy_device *phydev)
{
	if (!(phydev->c45_ids.devices_in_package & MDIO_DEVS_PMAPMD))
		return false;

	/* Only some revisions of the 88X3310 family PMA seem to be impacted */
	return (phydev->c45_ids.device_ids[MDIO_MMD_PMAPMD] &
		MV_PHY_ALASKA_NBT_QUIRK_MASK) == MV_PHY_ALASKA_NBT_QUIRK_REV;
}

static int mv2110_get_mactype(struct phy_device *phydev)
{
	int mactype;

	mactype = phy_read_mmd(phydev, MDIO_MMD_PMAPMD, MV_PMA_21X0_PORT_CTRL);
	if (mactype < 0)
		return mactype;

	return mactype & MV_PMA_21X0_PORT_CTRL_MACTYPE_MASK;
}

static int mv3310_get_mactype(struct phy_device *phydev)
{
	int mactype;

	mactype = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_PORT_CTRL);
	if (mactype < 0)
		return mactype;

	return mactype & MV_V2_33X0_PORT_CTRL_MACTYPE_MASK;
}

static int mv2110_init_interface(struct phy_device *phydev, int mactype)
{
	struct mv3310_priv *priv = dev_get_drvdata(&phydev->mdio.dev);

	priv->rate_match = false;

	if (mactype == MV_PMA_21X0_PORT_CTRL_MACTYPE_10GBASER_RATE_MATCH)
		priv->rate_match = true;

	if (mactype == MV_PMA_21X0_PORT_CTRL_MACTYPE_USXGMII)
		priv->const_interface = PHY_INTERFACE_MODE_USXGMII;
	else if (mactype == MV_PMA_21X0_PORT_CTRL_MACTYPE_10GBASER_RATE_MATCH)
		priv->const_interface = PHY_INTERFACE_MODE_10GBASER;
	else if (mactype == MV_PMA_21X0_PORT_CTRL_MACTYPE_5GBASER ||
		 mactype == MV_PMA_21X0_PORT_CTRL_MACTYPE_5GBASER_NO_SGMII_AN)
		priv->const_interface = PHY_INTERFACE_MODE_NA;
	else
		return -EINVAL;

	return 0;
}

static int mv3310_init_interface(struct phy_device *phydev, int mactype)
{
	struct mv3310_priv *priv = dev_get_drvdata(&phydev->mdio.dev);

	priv->rate_match = false;

	if (mactype == MV_V2_33X0_PORT_CTRL_MACTYPE_10GBASER_RATE_MATCH ||
	    mactype == MV_V2_33X0_PORT_CTRL_MACTYPE_RXAUI_RATE_MATCH ||
	    mactype == MV_V2_3310_PORT_CTRL_MACTYPE_XAUI_RATE_MATCH)
		priv->rate_match = true;

	if (mactype == MV_V2_33X0_PORT_CTRL_MACTYPE_USXGMII)
		priv->const_interface = PHY_INTERFACE_MODE_USXGMII;
	else if (mactype == MV_V2_33X0_PORT_CTRL_MACTYPE_10GBASER_RATE_MATCH ||
		 mactype == MV_V2_33X0_PORT_CTRL_MACTYPE_10GBASER_NO_SGMII_AN ||
		 mactype == MV_V2_33X0_PORT_CTRL_MACTYPE_10GBASER)
		priv->const_interface = PHY_INTERFACE_MODE_10GBASER;
	else if (mactype == MV_V2_33X0_PORT_CTRL_MACTYPE_RXAUI_RATE_MATCH ||
		 mactype == MV_V2_33X0_PORT_CTRL_MACTYPE_RXAUI)
		priv->const_interface = PHY_INTERFACE_MODE_RXAUI;
	else if (mactype == MV_V2_3310_PORT_CTRL_MACTYPE_XAUI_RATE_MATCH ||
		 mactype == MV_V2_3310_PORT_CTRL_MACTYPE_XAUI)
		priv->const_interface = PHY_INTERFACE_MODE_XAUI;
	else
		return -EINVAL;

	return 0;
}

static int mv3340_init_interface(struct phy_device *phydev, int mactype)
{
	struct mv3310_priv *priv = dev_get_drvdata(&phydev->mdio.dev);
	int err = 0;

	priv->rate_match = false;

	if (mactype == MV_V2_3340_PORT_CTRL_MACTYPE_RXAUI_NO_SGMII_AN)
		priv->const_interface = PHY_INTERFACE_MODE_RXAUI;
	else
		err = mv3310_init_interface(phydev, mactype);

	return err;
}

static int mv3310_config_init(struct phy_device *phydev)
{
	struct mv3310_priv *priv = dev_get_drvdata(&phydev->mdio.dev);
	const struct mv3310_chip *chip = to_mv3310_chip(phydev);
	int err, mactype;

	/* Check that the PHY interface type is compatible */
	if (!test_bit(phydev->interface, priv->supported_interfaces))
		return -ENODEV;

	phydev->mdix_ctrl = ETH_TP_MDI_AUTO;

	/* Power up so reset works */
	err = mv3310_power_up(phydev);
	if (err)
		return err;

	mactype = chip->get_mactype(phydev);
	if (mactype < 0)
		return mactype;

	err = chip->init_interface(phydev, mactype);
	if (err) {
		phydev_err(phydev, "MACTYPE configuration invalid\n");
		return err;
	}

	/* Enable EDPD mode - saving 600mW */
	return mv3310_set_edpd(phydev, ETHTOOL_PHY_EDPD_DFLT_TX_MSECS);
}

static int mv3310_get_features(struct phy_device *phydev)
{
	int ret, val;

	ret = genphy_c45_pma_read_abilities(phydev);
	if (ret)
		return ret;

	if (mv3310_has_pma_ngbaset_quirk(phydev)) {
		val = phy_read_mmd(phydev, MDIO_MMD_PMAPMD,
				   MDIO_PMA_NG_EXTABLE);
		if (val < 0)
			return val;

		linkmode_mod_bit(ETHTOOL_LINK_MODE_2500baseT_Full_BIT,
				 phydev->supported,
				 val & MDIO_PMA_NG_EXTABLE_2_5GBT);

		linkmode_mod_bit(ETHTOOL_LINK_MODE_5000baseT_Full_BIT,
				 phydev->supported,
				 val & MDIO_PMA_NG_EXTABLE_5GBT);
	}

	return 0;
}

static int mv3310_config_mdix(struct phy_device *phydev)
{
	u16 val;
	int err;

	switch (phydev->mdix_ctrl) {
	case ETH_TP_MDI_AUTO:
		val = MV_PCS_CSCR1_MDIX_AUTO;
		break;
	case ETH_TP_MDI_X:
		val = MV_PCS_CSCR1_MDIX_MDIX;
		break;
	case ETH_TP_MDI:
		val = MV_PCS_CSCR1_MDIX_MDI;
		break;
	default:
		return -EINVAL;
	}

	err = phy_modify_mmd_changed(phydev, MDIO_MMD_PCS, MV_PCS_CSCR1,
				     MV_PCS_CSCR1_MDIX_MASK, val);
	if (err > 0)
		err = mv3310_reset(phydev, MV_PCS_BASE_T);

	return err;
}

static int mv3310_config_aneg(struct phy_device *phydev)
{
	bool changed = false;
	u16 reg;
	int ret;

	ret = mv3310_config_mdix(phydev);
	if (ret < 0)
		return ret;

	if (phydev->autoneg == AUTONEG_DISABLE)
		return genphy_c45_pma_setup_forced(phydev);

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

	return genphy_c45_check_and_restart_aneg(phydev, changed);
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
	struct mv3310_priv *priv = dev_get_drvdata(&phydev->mdio.dev);

	if (!phydev->link)
		return;

	/* In all of the "* with Rate Matching" modes the PHY interface is fixed
	 * at 10Gb. The PHY adapts the rate to actual wire speed with help of
	 * internal 16KB buffer.
	 *
	 * In USXGMII mode the PHY interface mode is also fixed.
	 */
	if (priv->rate_match ||
	    priv->const_interface == PHY_INTERFACE_MODE_USXGMII) {
		phydev->interface = priv->const_interface;
		return;
	}

	/* The PHY automatically switches its serdes interface (and active PHYXS
	 * instance) between Cisco SGMII, 2500BaseX, 5GBase-R and 10GBase-R /
	 * xaui / rxaui modes according to the speed.
	 * Florian suggests setting phydev->interface to communicate this to the
	 * MAC. Only do this if we are already in one of the above modes.
	 */
	switch (phydev->speed) {
	case SPEED_10000:
		phydev->interface = priv->const_interface;
		break;
	case SPEED_5000:
		phydev->interface = PHY_INTERFACE_MODE_5GBASER;
		break;
	case SPEED_2500:
		phydev->interface = PHY_INTERFACE_MODE_2500BASEX;
		break;
	case SPEED_1000:
	case SPEED_100:
	case SPEED_10:
		phydev->interface = PHY_INTERFACE_MODE_SGMII;
		break;
	default:
		break;
	}
}

/* 10GBASE-ER,LR,LRM,SR do not support autonegotiation. */
static int mv3310_read_status_10gbaser(struct phy_device *phydev)
{
	phydev->link = 1;
	phydev->speed = SPEED_10000;
	phydev->duplex = DUPLEX_FULL;
	phydev->port = PORT_FIBRE;

	return 0;
}

static int mv3310_read_status_copper(struct phy_device *phydev)
{
	int cssr1, speed, val;

	val = genphy_c45_read_link(phydev);
	if (val < 0)
		return val;

	val = phy_read_mmd(phydev, MDIO_MMD_AN, MDIO_STAT1);
	if (val < 0)
		return val;

	cssr1 = phy_read_mmd(phydev, MDIO_MMD_PCS, MV_PCS_CSSR1);
	if (cssr1 < 0)
		return val;

	/* If the link settings are not resolved, mark the link down */
	if (!(cssr1 & MV_PCS_CSSR1_RESOLVED)) {
		phydev->link = 0;
		return 0;
	}

	/* Read the copper link settings */
	speed = cssr1 & MV_PCS_CSSR1_SPD1_MASK;
	if (speed == MV_PCS_CSSR1_SPD1_SPD2)
		speed |= cssr1 & MV_PCS_CSSR1_SPD2_MASK;

	switch (speed) {
	case MV_PCS_CSSR1_SPD1_SPD2 | MV_PCS_CSSR1_SPD2_10000:
		phydev->speed = SPEED_10000;
		break;

	case MV_PCS_CSSR1_SPD1_SPD2 | MV_PCS_CSSR1_SPD2_5000:
		phydev->speed = SPEED_5000;
		break;

	case MV_PCS_CSSR1_SPD1_SPD2 | MV_PCS_CSSR1_SPD2_2500:
		phydev->speed = SPEED_2500;
		break;

	case MV_PCS_CSSR1_SPD1_1000:
		phydev->speed = SPEED_1000;
		break;

	case MV_PCS_CSSR1_SPD1_100:
		phydev->speed = SPEED_100;
		break;

	case MV_PCS_CSSR1_SPD1_10:
		phydev->speed = SPEED_10;
		break;
	}

	phydev->duplex = cssr1 & MV_PCS_CSSR1_DUPLEX_FULL ?
			 DUPLEX_FULL : DUPLEX_HALF;
	phydev->port = PORT_TP;
	phydev->mdix = cssr1 & MV_PCS_CSSR1_MDIX ?
		       ETH_TP_MDI_X : ETH_TP_MDI;

	if (val & MDIO_AN_STAT1_COMPLETE) {
		val = genphy_c45_read_lpa(phydev);
		if (val < 0)
			return val;

		/* Read the link partner's 1G advertisement */
		val = phy_read_mmd(phydev, MDIO_MMD_AN, MV_AN_STAT1000);
		if (val < 0)
			return val;

		mii_stat1000_mod_linkmode_lpa_t(phydev->lp_advertising, val);

		/* Update the pause status */
		phy_resolve_aneg_pause(phydev);
	}

	return 0;
}

static int mv3310_read_status(struct phy_device *phydev)
{
	int err, val;

	phydev->speed = SPEED_UNKNOWN;
	phydev->duplex = DUPLEX_UNKNOWN;
	linkmode_zero(phydev->lp_advertising);
	phydev->link = 0;
	phydev->pause = 0;
	phydev->asym_pause = 0;
	phydev->mdix = ETH_TP_MDI_INVALID;

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MV_PCS_BASE_R + MDIO_STAT1);
	if (val < 0)
		return val;

	if (val & MDIO_STAT1_LSTATUS)
		err = mv3310_read_status_10gbaser(phydev);
	else
		err = mv3310_read_status_copper(phydev);
	if (err < 0)
		return err;

	if (phydev->link)
		mv3310_update_interface(phydev);

	return 0;
}

static int mv3310_get_tunable(struct phy_device *phydev,
			      struct ethtool_tunable *tuna, void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_EDPD:
		return mv3310_get_edpd(phydev, data);
	default:
		return -EOPNOTSUPP;
	}
}

static int mv3310_set_tunable(struct phy_device *phydev,
			      struct ethtool_tunable *tuna, const void *data)
{
	switch (tuna->id) {
	case ETHTOOL_PHY_EDPD:
		return mv3310_set_edpd(phydev, *(u16 *)data);
	default:
		return -EOPNOTSUPP;
	}
}

static void mv3310_init_supported_interfaces(unsigned long *mask)
{
	__set_bit(PHY_INTERFACE_MODE_SGMII, mask);
	__set_bit(PHY_INTERFACE_MODE_2500BASEX, mask);
	__set_bit(PHY_INTERFACE_MODE_5GBASER, mask);
	__set_bit(PHY_INTERFACE_MODE_XAUI, mask);
	__set_bit(PHY_INTERFACE_MODE_RXAUI, mask);
	__set_bit(PHY_INTERFACE_MODE_10GBASER, mask);
	__set_bit(PHY_INTERFACE_MODE_USXGMII, mask);
}

static void mv3340_init_supported_interfaces(unsigned long *mask)
{
	__set_bit(PHY_INTERFACE_MODE_SGMII, mask);
	__set_bit(PHY_INTERFACE_MODE_2500BASEX, mask);
	__set_bit(PHY_INTERFACE_MODE_5GBASER, mask);
	__set_bit(PHY_INTERFACE_MODE_RXAUI, mask);
	__set_bit(PHY_INTERFACE_MODE_10GBASER, mask);
	__set_bit(PHY_INTERFACE_MODE_USXGMII, mask);
}

static void mv2110_init_supported_interfaces(unsigned long *mask)
{
	__set_bit(PHY_INTERFACE_MODE_SGMII, mask);
	__set_bit(PHY_INTERFACE_MODE_2500BASEX, mask);
	__set_bit(PHY_INTERFACE_MODE_5GBASER, mask);
	__set_bit(PHY_INTERFACE_MODE_10GBASER, mask);
	__set_bit(PHY_INTERFACE_MODE_USXGMII, mask);
}

static void mv2111_init_supported_interfaces(unsigned long *mask)
{
	__set_bit(PHY_INTERFACE_MODE_SGMII, mask);
	__set_bit(PHY_INTERFACE_MODE_2500BASEX, mask);
	__set_bit(PHY_INTERFACE_MODE_10GBASER, mask);
	__set_bit(PHY_INTERFACE_MODE_USXGMII, mask);
}

static const struct mv3310_chip mv3310_type = {
	.init_supported_interfaces = mv3310_init_supported_interfaces,
	.get_mactype = mv3310_get_mactype,
	.init_interface = mv3310_init_interface,

#ifdef CONFIG_HWMON
	.hwmon_read_temp_reg = mv3310_hwmon_read_temp_reg,
#endif
};

static const struct mv3310_chip mv3340_type = {
	.init_supported_interfaces = mv3340_init_supported_interfaces,
	.get_mactype = mv3310_get_mactype,
	.init_interface = mv3340_init_interface,

#ifdef CONFIG_HWMON
	.hwmon_read_temp_reg = mv3310_hwmon_read_temp_reg,
#endif
};

static const struct mv3310_chip mv2110_type = {
	.init_supported_interfaces = mv2110_init_supported_interfaces,
	.get_mactype = mv2110_get_mactype,
	.init_interface = mv2110_init_interface,

#ifdef CONFIG_HWMON
	.hwmon_read_temp_reg = mv2110_hwmon_read_temp_reg,
#endif
};

static const struct mv3310_chip mv2111_type = {
	.init_supported_interfaces = mv2111_init_supported_interfaces,
	.get_mactype = mv2110_get_mactype,
	.init_interface = mv2110_init_interface,

#ifdef CONFIG_HWMON
	.hwmon_read_temp_reg = mv2110_hwmon_read_temp_reg,
#endif
};

static int mv3310_get_number_of_ports(struct phy_device *phydev)
{
	int ret;

	ret = phy_read_mmd(phydev, MDIO_MMD_PCS, MV_PCS_PORT_INFO);
	if (ret < 0)
		return ret;

	ret &= MV_PCS_PORT_INFO_NPORTS_MASK;
	ret >>= MV_PCS_PORT_INFO_NPORTS_SHIFT;

	return ret + 1;
}

static int mv3310_match_phy_device(struct phy_device *phydev)
{
	if ((phydev->c45_ids.device_ids[MDIO_MMD_PMAPMD] &
	     MARVELL_PHY_ID_MASK) != MARVELL_PHY_ID_88X3310)
		return 0;

	return mv3310_get_number_of_ports(phydev) == 1;
}

static int mv3340_match_phy_device(struct phy_device *phydev)
{
	if ((phydev->c45_ids.device_ids[MDIO_MMD_PMAPMD] &
	     MARVELL_PHY_ID_MASK) != MARVELL_PHY_ID_88X3310)
		return 0;

	return mv3310_get_number_of_ports(phydev) == 4;
}

static int mv211x_match_phy_device(struct phy_device *phydev, bool has_5g)
{
	int val;

	if ((phydev->c45_ids.device_ids[MDIO_MMD_PMAPMD] &
	     MARVELL_PHY_ID_MASK) != MARVELL_PHY_ID_88E2110)
		return 0;

	val = phy_read_mmd(phydev, MDIO_MMD_PCS, MDIO_SPEED);
	if (val < 0)
		return val;

	return !!(val & MDIO_PCS_SPEED_5G) == has_5g;
}

static int mv2110_match_phy_device(struct phy_device *phydev)
{
	return mv211x_match_phy_device(phydev, true);
}

static int mv2111_match_phy_device(struct phy_device *phydev)
{
	return mv211x_match_phy_device(phydev, false);
}

static void mv3110_get_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	int ret;

	wol->supported = WAKE_MAGIC;
	wol->wolopts = 0;

	ret = phy_read_mmd(phydev, MDIO_MMD_VEND2, MV_V2_WOL_CTRL);
	if (ret < 0)
		return;

	if (ret & MV_V2_WOL_CTRL_MAGIC_PKT_EN)
		wol->wolopts |= WAKE_MAGIC;
}

static int mv3110_set_wol(struct phy_device *phydev,
			  struct ethtool_wolinfo *wol)
{
	int ret;

	if (wol->wolopts & WAKE_MAGIC) {
		/* Enable the WOL interrupt */
		ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2,
				       MV_V2_PORT_INTR_MASK,
				       MV_V2_PORT_INTR_STS_WOL_EN);
		if (ret < 0)
			return ret;

		/* Store the device address for the magic packet */
		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    MV_V2_MAGIC_PKT_WORD2,
				    ((phydev->attached_dev->dev_addr[5] << 8) |
				    phydev->attached_dev->dev_addr[4]));
		if (ret < 0)
			return ret;

		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    MV_V2_MAGIC_PKT_WORD1,
				    ((phydev->attached_dev->dev_addr[3] << 8) |
				    phydev->attached_dev->dev_addr[2]));
		if (ret < 0)
			return ret;

		ret = phy_write_mmd(phydev, MDIO_MMD_VEND2,
				    MV_V2_MAGIC_PKT_WORD0,
				    ((phydev->attached_dev->dev_addr[1] << 8) |
				    phydev->attached_dev->dev_addr[0]));
		if (ret < 0)
			return ret;

		/* Clear WOL status and enable magic packet matching */
		ret = phy_set_bits_mmd(phydev, MDIO_MMD_VEND2,
				       MV_V2_WOL_CTRL,
				       MV_V2_WOL_CTRL_MAGIC_PKT_EN |
				       MV_V2_WOL_CTRL_CLEAR_STS);
		if (ret < 0)
			return ret;
	} else {
		/* Disable magic packet matching & reset WOL status bit */
		ret = phy_modify_mmd(phydev, MDIO_MMD_VEND2,
				     MV_V2_WOL_CTRL,
				     MV_V2_WOL_CTRL_MAGIC_PKT_EN,
				     MV_V2_WOL_CTRL_CLEAR_STS);
		if (ret < 0)
			return ret;
	}

	/* Reset the clear WOL status bit as it does not self-clear */
	return phy_clear_bits_mmd(phydev, MDIO_MMD_VEND2,
				  MV_V2_WOL_CTRL,
				  MV_V2_WOL_CTRL_CLEAR_STS);
}

static struct phy_driver mv3310_drivers[] = {
	{
		.phy_id		= MARVELL_PHY_ID_88X3310,
		.phy_id_mask	= MARVELL_PHY_ID_MASK,
		.match_phy_device = mv3310_match_phy_device,
		.name		= "mv88x3310",
		.driver_data	= &mv3310_type,
		.get_features	= mv3310_get_features,
		.config_init	= mv3310_config_init,
		.probe		= mv3310_probe,
		.suspend	= mv3310_suspend,
		.resume		= mv3310_resume,
		.config_aneg	= mv3310_config_aneg,
		.aneg_done	= mv3310_aneg_done,
		.read_status	= mv3310_read_status,
		.get_tunable	= mv3310_get_tunable,
		.set_tunable	= mv3310_set_tunable,
		.remove		= mv3310_remove,
		.set_loopback	= genphy_c45_loopback,
		.get_wol	= mv3110_get_wol,
		.set_wol	= mv3110_set_wol,
	},
	{
		.phy_id		= MARVELL_PHY_ID_88X3310,
		.phy_id_mask	= MARVELL_PHY_ID_MASK,
		.match_phy_device = mv3340_match_phy_device,
		.name		= "mv88x3340",
		.driver_data	= &mv3340_type,
		.get_features	= mv3310_get_features,
		.config_init	= mv3310_config_init,
		.probe		= mv3310_probe,
		.suspend	= mv3310_suspend,
		.resume		= mv3310_resume,
		.config_aneg	= mv3310_config_aneg,
		.aneg_done	= mv3310_aneg_done,
		.read_status	= mv3310_read_status,
		.get_tunable	= mv3310_get_tunable,
		.set_tunable	= mv3310_set_tunable,
		.remove		= mv3310_remove,
		.set_loopback	= genphy_c45_loopback,
	},
	{
		.phy_id		= MARVELL_PHY_ID_88E2110,
		.phy_id_mask	= MARVELL_PHY_ID_MASK,
		.match_phy_device = mv2110_match_phy_device,
		.name		= "mv88e2110",
		.driver_data	= &mv2110_type,
		.probe		= mv3310_probe,
		.suspend	= mv3310_suspend,
		.resume		= mv3310_resume,
		.config_init	= mv3310_config_init,
		.config_aneg	= mv3310_config_aneg,
		.aneg_done	= mv3310_aneg_done,
		.read_status	= mv3310_read_status,
		.get_tunable	= mv3310_get_tunable,
		.set_tunable	= mv3310_set_tunable,
		.remove		= mv3310_remove,
		.set_loopback	= genphy_c45_loopback,
		.get_wol	= mv3110_get_wol,
		.set_wol	= mv3110_set_wol,
	},
	{
		.phy_id		= MARVELL_PHY_ID_88E2110,
		.phy_id_mask	= MARVELL_PHY_ID_MASK,
		.match_phy_device = mv2111_match_phy_device,
		.name		= "mv88e2111",
		.driver_data	= &mv2111_type,
		.probe		= mv3310_probe,
		.suspend	= mv3310_suspend,
		.resume		= mv3310_resume,
		.config_init	= mv3310_config_init,
		.config_aneg	= mv3310_config_aneg,
		.aneg_done	= mv3310_aneg_done,
		.read_status	= mv3310_read_status,
		.get_tunable	= mv3310_get_tunable,
		.set_tunable	= mv3310_set_tunable,
		.remove		= mv3310_remove,
		.set_loopback	= genphy_c45_loopback,
	},
};

module_phy_driver(mv3310_drivers);

static struct mdio_device_id __maybe_unused mv3310_tbl[] = {
	{ MARVELL_PHY_ID_88X3310, MARVELL_PHY_ID_MASK },
	{ MARVELL_PHY_ID_88E2110, MARVELL_PHY_ID_MASK },
	{ },
};
MODULE_DEVICE_TABLE(mdio, mv3310_tbl);
MODULE_DESCRIPTION("Marvell Alaska X/M multi-gigabit Ethernet PHY driver");
MODULE_LICENSE("GPL");
