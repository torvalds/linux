// SPDX-License-Identifier: GPL-2.0+
/* Driver for Asix PHYs
 *
 * Author: Michael Schmitz <schmitzmic@gmail.com>
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mii.h>
#include <linux/phy.h>

#define PHY_ID_ASIX_AX88796B		0x003b1841

MODULE_DESCRIPTION("Asix PHY driver");
MODULE_AUTHOR("Michael Schmitz <schmitzmic@gmail.com>");
MODULE_LICENSE("GPL");

/**
 * asix_soft_reset - software reset the PHY via BMCR_RESET bit
 * @phydev: target phy_device struct
 *
 * Description: Perform a software PHY reset using the standard
 * BMCR_RESET bit and poll for the reset bit to be cleared.
 * Toggle BMCR_RESET bit off to accommodate broken AX8796B PHY implementation
 * such as used on the Individual Computers' X-Surf 100 Zorro card.
 *
 * Returns: 0 on success, < 0 on failure
 */
static int asix_soft_reset(struct phy_device *phydev)
{
	int ret;

	/* Asix PHY won't reset unless reset bit toggles */
	ret = phy_write(phydev, MII_BMCR, 0);
	if (ret < 0)
		return ret;

	return genphy_soft_reset(phydev);
}

static struct phy_driver asix_driver[] = { {
	.phy_id		= PHY_ID_ASIX_AX88796B,
	.name		= "Asix Electronics AX88796B",
	.phy_id_mask	= 0xfffffff0,
	.features	= PHY_BASIC_FEATURES,
	.soft_reset	= asix_soft_reset,
} };

module_phy_driver(asix_driver);

static struct mdio_device_id __maybe_unused asix_tbl[] = {
	{ PHY_ID_ASIX_AX88796B, 0xfffffff0 },
	{ }
};

MODULE_DEVICE_TABLE(mdio, asix_tbl);
