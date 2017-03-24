/*
 * Copyright (C) 2015 Microchip Technology
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/phy.h>
#include <linux/microchipphy.h>

#define DRIVER_AUTHOR	"WOOJUNG HUH <woojung.huh@microchip.com>"
#define DRIVER_DESC	"Microchip LAN88XX PHY driver"

struct lan88xx_priv {
	int	chip_id;
	int	chip_rev;
	__u32	wolopts;
};

static int lan88xx_phy_config_intr(struct phy_device *phydev)
{
	int rc;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		/* unmask all source and clear them before enable */
		rc = phy_write(phydev, LAN88XX_INT_MASK, 0x7FFF);
		rc = phy_read(phydev, LAN88XX_INT_STS);
		rc = phy_write(phydev, LAN88XX_INT_MASK,
			       LAN88XX_INT_MASK_MDINTPIN_EN_ |
			       LAN88XX_INT_MASK_LINK_CHANGE_);
	} else {
		rc = phy_write(phydev, LAN88XX_INT_MASK, 0);
	}

	return rc < 0 ? rc : 0;
}

static int lan88xx_phy_ack_interrupt(struct phy_device *phydev)
{
	int rc = phy_read(phydev, LAN88XX_INT_STS);

	return rc < 0 ? rc : 0;
}

static int lan88xx_suspend(struct phy_device *phydev)
{
	struct lan88xx_priv *priv = phydev->priv;

	/* do not power down PHY when WOL is enabled */
	if (!priv->wolopts)
		genphy_suspend(phydev);

	return 0;
}

static int lan88xx_probe(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct lan88xx_priv *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->wolopts = 0;

	/* these values can be used to identify internal PHY */
	priv->chip_id = phy_read_mmd_indirect(phydev, LAN88XX_MMD3_CHIP_ID, 3);
	priv->chip_rev = phy_read_mmd_indirect(phydev, LAN88XX_MMD3_CHIP_REV,
					       3);

	phydev->priv = priv;

	return 0;
}

static void lan88xx_remove(struct phy_device *phydev)
{
	struct device *dev = &phydev->mdio.dev;
	struct lan88xx_priv *priv = phydev->priv;

	if (priv)
		devm_kfree(dev, priv);
}

static int lan88xx_set_wol(struct phy_device *phydev,
			   struct ethtool_wolinfo *wol)
{
	struct lan88xx_priv *priv = phydev->priv;

	priv->wolopts = wol->wolopts;

	return 0;
}

static void lan88xx_set_mdix(struct phy_device *phydev)
{
	int buf;
	int val;

	switch (phydev->mdix_ctrl) {
	case ETH_TP_MDI:
		val = LAN88XX_EXT_MODE_CTRL_MDI_;
		break;
	case ETH_TP_MDI_X:
		val = LAN88XX_EXT_MODE_CTRL_MDI_X_;
		break;
	case ETH_TP_MDI_AUTO:
		val = LAN88XX_EXT_MODE_CTRL_AUTO_MDIX_;
		break;
	default:
		return;
	}

	phy_write(phydev, LAN88XX_EXT_PAGE_ACCESS, LAN88XX_EXT_PAGE_SPACE_1);
	buf = phy_read(phydev, LAN88XX_EXT_MODE_CTRL);
	buf &= ~LAN88XX_EXT_MODE_CTRL_MDIX_MASK_;
	buf |= val;
	phy_write(phydev, LAN88XX_EXT_MODE_CTRL, buf);
	phy_write(phydev, LAN88XX_EXT_PAGE_ACCESS, LAN88XX_EXT_PAGE_SPACE_0);
}

static int lan88xx_config_aneg(struct phy_device *phydev)
{
	lan88xx_set_mdix(phydev);

	return genphy_config_aneg(phydev);
}

static struct phy_driver microchip_phy_driver[] = {
{
	.phy_id		= 0x0007c130,
	.phy_id_mask	= 0xfffffff0,
	.name		= "Microchip LAN88xx",

	.features	= PHY_GBIT_FEATURES,
	.flags		= PHY_HAS_INTERRUPT | PHY_HAS_MAGICANEG,

	.probe		= lan88xx_probe,
	.remove		= lan88xx_remove,

	.config_init	= genphy_config_init,
	.config_aneg	= lan88xx_config_aneg,
	.read_status	= genphy_read_status,

	.ack_interrupt	= lan88xx_phy_ack_interrupt,
	.config_intr	= lan88xx_phy_config_intr,

	.suspend	= lan88xx_suspend,
	.resume		= genphy_resume,
	.set_wol	= lan88xx_set_wol,
} };

module_phy_driver(microchip_phy_driver);

static struct mdio_device_id __maybe_unused microchip_tbl[] = {
	{ 0x0007c130, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, microchip_tbl);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");
