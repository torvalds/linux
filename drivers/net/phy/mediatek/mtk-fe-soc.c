// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for MT7628 Embedded Switch internal Fast Ethernet PHYs
 */
#include <linux/module.h>
#include <linux/phy.h>

#define MTK_FPHY_ID_MT7628	0x03a29410
#define MTK_EXT_PAGE_ACCESS	0x1f

static int mt7628_phy_read_page(struct phy_device *phydev)
{
	return __phy_read(phydev, MTK_EXT_PAGE_ACCESS);
}

static int mt7628_phy_write_page(struct phy_device *phydev, int page)
{
	return __phy_write(phydev, MTK_EXT_PAGE_ACCESS, page);
}

static int mt7628_phy_config_init(struct phy_device *phydev)
{
	/*
	 * This undocumented bit is required for the PHYs to be able to
	 * establish 100mbps links.
	 */
	return phy_modify_paged(phydev, 0x8000, 30, BIT(13), BIT(13));
}

static struct phy_driver mtk_soc_fe_phy_driver[] = {
	{
		PHY_ID_MATCH_EXACT(MTK_FPHY_ID_MT7628),
		.name		= "MediaTek MT7628 PHY",
		.config_init	= mt7628_phy_config_init,
		.read_page	= mt7628_phy_read_page,
		.write_page	= mt7628_phy_write_page,
		.suspend	= genphy_suspend,
		.resume		= genphy_resume,
	},
};

module_phy_driver(mtk_soc_fe_phy_driver);
static const struct mdio_device_id __maybe_unused mtk_soc_fe_phy_tbl[] = {
	{ PHY_ID_MATCH_EXACT(MTK_FPHY_ID_MT7628) },
	{ }
};

MODULE_DESCRIPTION("MediaTek SoC Fast Ethernet PHY driver");
MODULE_AUTHOR("Joris Vaisvila <joey@tinyisr.com>");
MODULE_LICENSE("GPL");

MODULE_DEVICE_TABLE(mdio, mtk_soc_fe_phy_tbl);
