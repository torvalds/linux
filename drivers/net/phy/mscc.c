/*
 * Driver for Microsemi VSC85xx PHYs
 *
 * Author: Nagaraju Lakkaraju
 * License: Dual MIT/GPL
 * Copyright (c) 2016 Microsemi Corporation
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mdio.h>
#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/of.h>
#include <dt-bindings/net/mscc-phy-vsc8531.h>

enum rgmii_rx_clock_delay {
	RGMII_RX_CLK_DELAY_0_2_NS = 0,
	RGMII_RX_CLK_DELAY_0_8_NS = 1,
	RGMII_RX_CLK_DELAY_1_1_NS = 2,
	RGMII_RX_CLK_DELAY_1_7_NS = 3,
	RGMII_RX_CLK_DELAY_2_0_NS = 4,
	RGMII_RX_CLK_DELAY_2_3_NS = 5,
	RGMII_RX_CLK_DELAY_2_6_NS = 6,
	RGMII_RX_CLK_DELAY_3_4_NS = 7
};

/* Microsemi VSC85xx PHY registers */
/* IEEE 802. Std Registers */
#define MSCC_PHY_EXT_PHY_CNTL_1           23
#define MAC_IF_SELECTION_MASK             0x1800
#define MAC_IF_SELECTION_GMII             0
#define MAC_IF_SELECTION_RMII             1
#define MAC_IF_SELECTION_RGMII            2
#define MAC_IF_SELECTION_POS              11
#define FAR_END_LOOPBACK_MODE_MASK        0x0008

#define MII_VSC85XX_INT_MASK		  25
#define MII_VSC85XX_INT_MASK_MASK	  0xa000
#define MII_VSC85XX_INT_STATUS		  26

#define MSCC_PHY_WOL_MAC_CONTROL          27
#define EDGE_RATE_CNTL_POS                5
#define EDGE_RATE_CNTL_MASK               0x00E0

#define MSCC_EXT_PAGE_ACCESS		  31
#define MSCC_PHY_PAGE_STANDARD		  0x0000 /* Standard registers */
#define MSCC_PHY_PAGE_EXTENDED_2	  0x0002 /* Extended reg - page 2 */

/* Extended Page 2 Registers */
#define MSCC_PHY_RGMII_CNTL		  20
#define RGMII_RX_CLK_DELAY_MASK		  0x0070
#define RGMII_RX_CLK_DELAY_POS		  4

/* Microsemi PHY ID's */
#define PHY_ID_VSC8531			  0x00070570
#define PHY_ID_VSC8541			  0x00070770

struct edge_rate_table {
	u16 vddmac;
	int slowdown[MSCC_SLOWDOWN_MAX];
};

struct edge_rate_table edge_table[MSCC_VDDMAC_MAX] = {
	{3300, { 0, -2, -4,  -7,  -10, -17, -29, -53} },
	{2500, { 0, -3, -6,  -10, -14, -23, -37, -63} },
	{1800, { 0, -5, -9,  -16, -23, -35, -52, -76} },
	{1500, { 0, -6, -14, -21, -29, -42, -58, -77} },
};

struct vsc8531_private {
	u8 edge_slowdown;
	u16 vddmac;
};

static int vsc85xx_phy_page_set(struct phy_device *phydev, u8 page)
{
	int rc;

	rc = phy_write(phydev, MSCC_EXT_PAGE_ACCESS, page);
	return rc;
}

static u8 edge_rate_magic_get(u16 vddmac,
			      int slowdown)
{
	int rc = (MSCC_SLOWDOWN_MAX - 1);
	u8 vdd;
	u8 sd;

	for (vdd = 0; vdd < MSCC_VDDMAC_MAX; vdd++) {
		if (edge_table[vdd].vddmac == vddmac) {
			for (sd = 0; sd < MSCC_SLOWDOWN_MAX; sd++) {
				if (edge_table[vdd].slowdown[sd] <= slowdown) {
					rc = (MSCC_SLOWDOWN_MAX - sd - 1);
					break;
				}
			}
		}
	}

	return rc;
}

static int vsc85xx_edge_rate_cntl_set(struct phy_device *phydev,
				      u8 edge_rate)
{
	int rc;
	u16 reg_val;

	mutex_lock(&phydev->lock);
	rc = vsc85xx_phy_page_set(phydev, MSCC_PHY_PAGE_EXTENDED_2);
	if (rc != 0)
		goto out_unlock;
	reg_val = phy_read(phydev, MSCC_PHY_WOL_MAC_CONTROL);
	reg_val &= ~(EDGE_RATE_CNTL_MASK);
	reg_val |= (edge_rate << EDGE_RATE_CNTL_POS);
	rc = phy_write(phydev, MSCC_PHY_WOL_MAC_CONTROL, reg_val);
	if (rc != 0)
		goto out_unlock;
	rc = vsc85xx_phy_page_set(phydev, MSCC_PHY_PAGE_STANDARD);

out_unlock:
	mutex_unlock(&phydev->lock);

	return rc;
}

static int vsc85xx_mac_if_set(struct phy_device *phydev,
			      phy_interface_t interface)
{
	int rc;
	u16 reg_val;

	mutex_lock(&phydev->lock);
	reg_val = phy_read(phydev, MSCC_PHY_EXT_PHY_CNTL_1);
	reg_val &= ~(MAC_IF_SELECTION_MASK);
	switch (interface) {
	case PHY_INTERFACE_MODE_RGMII:
		reg_val |= (MAC_IF_SELECTION_RGMII << MAC_IF_SELECTION_POS);
		break;
	case PHY_INTERFACE_MODE_RMII:
		reg_val |= (MAC_IF_SELECTION_RMII << MAC_IF_SELECTION_POS);
		break;
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_GMII:
		reg_val |= (MAC_IF_SELECTION_GMII << MAC_IF_SELECTION_POS);
		break;
	default:
		rc = -EINVAL;
		goto out_unlock;
	}
	rc = phy_write(phydev, MSCC_PHY_EXT_PHY_CNTL_1, reg_val);
	if (rc != 0)
		goto out_unlock;

	rc = genphy_soft_reset(phydev);

out_unlock:
	mutex_unlock(&phydev->lock);

	return rc;
}

static int vsc85xx_default_config(struct phy_device *phydev)
{
	int rc;
	u16 reg_val;

	mutex_lock(&phydev->lock);
	rc = vsc85xx_phy_page_set(phydev, MSCC_PHY_PAGE_EXTENDED_2);
	if (rc != 0)
		goto out_unlock;

	reg_val = phy_read(phydev, MSCC_PHY_RGMII_CNTL);
	reg_val &= ~(RGMII_RX_CLK_DELAY_MASK);
	reg_val |= (RGMII_RX_CLK_DELAY_1_1_NS << RGMII_RX_CLK_DELAY_POS);
	phy_write(phydev, MSCC_PHY_RGMII_CNTL, reg_val);
	rc = vsc85xx_phy_page_set(phydev, MSCC_PHY_PAGE_STANDARD);

out_unlock:
	mutex_unlock(&phydev->lock);

	return rc;
}

#ifdef CONFIG_OF_MDIO
static int vsc8531_of_init(struct phy_device *phydev)
{
	int rc;
	struct vsc8531_private *vsc8531 = phydev->priv;
	struct device *dev = &phydev->mdio.dev;
	struct device_node *of_node = dev->of_node;

	if (!of_node)
		return -ENODEV;

	rc = of_property_read_u16(of_node, "vsc8531,vddmac",
				  &vsc8531->vddmac);
	if (rc == -EINVAL)
		vsc8531->vddmac = MSCC_VDDMAC_3300;
	rc = of_property_read_u8(of_node, "vsc8531,edge-slowdown",
				 &vsc8531->edge_slowdown);
	if (rc == -EINVAL)
		vsc8531->edge_slowdown = 0;

	rc = 0;
	return rc;
}
#else
static int vsc8531_of_init(struct phy_device *phydev)
{
	return 0;
}
#endif /* CONFIG_OF_MDIO */

static int vsc85xx_config_init(struct phy_device *phydev)
{
	int rc;
	struct vsc8531_private *vsc8531 = phydev->priv;
	u8 edge_rate;

	rc = vsc8531_of_init(phydev);
	if (rc)
		return rc;

	rc = vsc85xx_default_config(phydev);
	if (rc)
		return rc;

	rc = vsc85xx_mac_if_set(phydev, phydev->interface);
	if (rc)
		return rc;

	edge_rate = edge_rate_magic_get(vsc8531->vddmac,
					-(int)vsc8531->edge_slowdown);
	rc = vsc85xx_edge_rate_cntl_set(phydev, edge_rate);
	if (rc)
		return rc;

	rc = genphy_config_init(phydev);

	return rc;
}

static int vsc85xx_ack_interrupt(struct phy_device *phydev)
{
	int rc = 0;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		rc = phy_read(phydev, MII_VSC85XX_INT_STATUS);

	return (rc < 0) ? rc : 0;
}

static int vsc85xx_config_intr(struct phy_device *phydev)
{
	int rc;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED) {
		rc = phy_write(phydev, MII_VSC85XX_INT_MASK,
			       MII_VSC85XX_INT_MASK_MASK);
	} else {
		rc = phy_write(phydev, MII_VSC85XX_INT_MASK, 0);
		if (rc < 0)
			return rc;
		rc = phy_read(phydev, MII_VSC85XX_INT_STATUS);
	}

	return rc;
}

static int vsc85xx_probe(struct phy_device *phydev)
{
	struct vsc8531_private *vsc8531;

	vsc8531 = devm_kzalloc(&phydev->mdio.dev, sizeof(*vsc8531), GFP_KERNEL);
	if (!vsc8531)
		return -ENOMEM;

	phydev->priv = vsc8531;

	return 0;
}

/* Microsemi VSC85xx PHYs */
static struct phy_driver vsc85xx_driver[] = {
{
	.phy_id		= PHY_ID_VSC8531,
	.name		= "Microsemi VSC8531",
	.phy_id_mask    = 0xfffffff0,
	.features	= PHY_GBIT_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc85xx_config_init,
	.config_aneg    = &genphy_config_aneg,
	.aneg_done	= &genphy_aneg_done,
	.read_status    = &genphy_read_status,
	.ack_interrupt  = &vsc85xx_ack_interrupt,
	.config_intr    = &vsc85xx_config_intr,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe          = &vsc85xx_probe,
},
{
	.phy_id		= PHY_ID_VSC8541,
	.name		= "Microsemi VSC8541 SyncE",
	.phy_id_mask    = 0xfffffff0,
	.features	= PHY_GBIT_FEATURES,
	.flags		= PHY_HAS_INTERRUPT,
	.soft_reset	= &genphy_soft_reset,
	.config_init    = &vsc85xx_config_init,
	.config_aneg    = &genphy_config_aneg,
	.aneg_done	= &genphy_aneg_done,
	.read_status    = &genphy_read_status,
	.ack_interrupt  = &vsc85xx_ack_interrupt,
	.config_intr    = &vsc85xx_config_intr,
	.suspend	= &genphy_suspend,
	.resume		= &genphy_resume,
	.probe          = &vsc85xx_probe,
}

};

module_phy_driver(vsc85xx_driver);

static struct mdio_device_id __maybe_unused vsc85xx_tbl[] = {
	{ PHY_ID_VSC8531, 0xfffffff0, },
	{ PHY_ID_VSC8541, 0xfffffff0, },
	{ }
};

MODULE_DEVICE_TABLE(mdio, vsc85xx_tbl);

MODULE_DESCRIPTION("Microsemi VSC85xx PHY driver");
MODULE_AUTHOR("Nagaraju Lakkaraju");
MODULE_LICENSE("Dual MIT/GPL");
